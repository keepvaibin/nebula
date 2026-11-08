// SPDX-License-Identifier: GPL-3.0-only
// streamline_vk.h.
#include "galaxy/render/vk/streamline_vk.h"

#include "galaxy/render/render_config.h"
#include "galaxy/render/streamline_bridge.h"
#include "../streamline_common.h"
#include "../streamline_frame_generation.h"

#include <dxgi1_4.h>

#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_helpers_vk.h>
#include <sl_security.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>
#include <type_traits>
#include <wrl/client.h>

namespace galaxy::render {

namespace {

struct Api {
    PFun_slInit* init{};
    PFun_slShutdown* shutdown{};
    PFun_slIsFeatureSupported* is_feature_supported{};
    PFun_slGetFeatureRequirements* get_feature_requirements{};
    PFun_slSetVulkanInfo* set_vulkan_info{};
    PFun_slGetFeatureFunction* get_feature_function{};
    PFun_slGetNewFrameToken* get_new_frame_token{};
    PFun_slSetConstants* set_constants{};
    PFun_slSetTagForFrame* set_tag_for_frame{};
    PFun_slEvaluateFeature* evaluate_feature{};
    PFun_slFreeResources* free_resources{};
    PFun_slDLSSGetOptimalSettings* dlss_get_optimal_settings{};
    PFun_slDLSSSetOptions* dlss_set_options{};
    bool initialized = false;
    bool dlss = false;
};
Api g_api;
vk::streamline_vk::Needs g_needs;
std::wstring g_directory;
streamline_common::FrameGeneration g_generation;
HMODULE g_interposer = nullptr;

// Each name once, in the order they came.
void add_names(std::vector<const char*>& names, const char* const* more, std::uint32_t count) {
    for (std::uint32_t i = 0u; i < count; ++i) {
        if (std::none_of(names.begin(), names.end(), [&](const char* have) { return std::strcmp(have, more[i]) == 0; })) {
            names.push_back(more[i]);
        }
    }
}

template <typename Function>
bool resolve(HMODULE module, const char* name, Function*& out) {
    out = reinterpret_cast<Function*>(GetProcAddress(module, name));
    if (out == nullptr) {
        std::cerr << "[streamline] sl.interposer.dll lacks " << name << '\n';
    }
    return out != nullptr;
}

void log_message(sl::LogType type, const char* message) {
    if (type != sl::LogType::eInfo && message != nullptr) {
        std::cerr << "[streamline] " << message;
        const std::size_t length = std::strlen(message);
        if (length == 0u || message[length - 1u] != '\n') std::cerr << '\n';
    }
}

// An NVIDIA RTX adapter, read before any Vulkan object exists: the library is loaded only for those.
bool rtx_adapter_present() {
    HMODULE module = LoadLibraryExW(L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module == nullptr) {
        return false;
    }
    using Create = HRESULT(WINAPI*)(REFIID, void**);
    const auto create = reinterpret_cast<Create>(GetProcAddress(module, "CreateDXGIFactory1"));
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (create == nullptr || FAILED(create(IID_PPV_ARGS(&factory)))) {
        return false;
    }
    for (UINT index = 0u;; ++index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) {
            return false;
        }
        DXGI_ADAPTER_DESC1 description{};
        if (SUCCEEDED(adapter->GetDesc1(&description)) && description.VendorId == 0x10DEu &&
            std::wcsstr(description.Description, L"RTX") != nullptr) {
            return true;
        }
    }
}

}  // namespace

namespace streamline {

bool dlss_supported() noexcept { return g_api.initialized && g_api.dlss; }

bool frame_generation_supported() noexcept { return g_api.initialized && g_generation.supported(); }

unsigned frame_generation_max() noexcept { return g_generation.max(); }

bool frame_generation_dynamic() noexcept { return g_generation.dynamic(); }

unsigned frame_index() noexcept { return g_generation.frame_index(); }

void marker(Marker m) noexcept { g_generation.marker(m); }

void reflex_sleep() noexcept { g_generation.sleep(); }

void set_frame_generation(unsigned generated, bool dynamic, float target_fps) noexcept { g_generation.set(generated, dynamic, target_fps); }

unsigned frames_presented() noexcept { return g_generation.presented(); }

}  // namespace streamline

unsigned dlss_min_render_width(unsigned quality, unsigned output_width, unsigned output_height) noexcept {
    if (!streamline::dlss_supported() || g_api.dlss_get_optimal_settings == nullptr || output_width == 0u || output_height == 0u) {
        return 0u;
    }
    sl::DLSSOptions options{};
    options.mode = streamline_common::dlss_mode(quality);
    options.outputWidth = output_width;
    options.outputHeight = output_height;
    sl::DLSSOptimalSettings settings{};
    return g_api.dlss_get_optimal_settings(options, settings) == sl::Result::eOk ? settings.renderWidthMin : 0u;
}

namespace vk::streamline_vk {

bool start() noexcept {
    if (g_api.initialized) {
        return true;
    }
    try {
        g_directory = streamline::default_directory();
        const std::wstring interposer = g_directory + L"\\sl.interposer.dll";
        if (g_directory.empty() || GetFileAttributesW(interposer.c_str()) == INVALID_FILE_ATTRIBUTES || !rtx_adapter_present()) {
            return false;
        }
        if (!sl::security::verifyEmbeddedSignature(interposer.c_str())) {
            std::cerr << "[streamline] sl.interposer.dll is not signed by NVIDIA: not loaded\n";
            return false;
        }
        HMODULE module = LoadLibraryExW(interposer.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (module == nullptr) {
            std::cerr << "[streamline] sl.interposer.dll did not load\n";
            return false;
        }
        Api api{};
        if (!resolve(module, "slInit", api.init) || !resolve(module, "slShutdown", api.shutdown) ||
            !resolve(module, "slIsFeatureSupported", api.is_feature_supported) ||
            !resolve(module, "slGetFeatureRequirements", api.get_feature_requirements) ||
            !resolve(module, "slSetVulkanInfo", api.set_vulkan_info) ||
            !resolve(module, "slGetFeatureFunction", api.get_feature_function) ||
            !resolve(module, "slGetNewFrameToken", api.get_new_frame_token) ||
            !resolve(module, "slSetConstants", api.set_constants) ||
            !resolve(module, "slSetTagForFrame", api.set_tag_for_frame) ||
            !resolve(module, "slEvaluateFeature", api.evaluate_feature) ||
            !resolve(module, "slFreeResources", api.free_resources)) {
            return false;
        }
        sl::Preferences preferences{};
        preferences.showConsole = false;
        preferences.logLevel = sl::LogLevel::eDefault;
        static const wchar_t* paths[1]{};
        paths[0] = g_directory.c_str();
        preferences.pathsToPlugins = paths;
        preferences.numPathsToPlugins = 1u;
        preferences.logMessageCallback = &log_message;
        preferences.flags = sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eUseManualHooking |
                            sl::PreferenceFlags::eUseFrameBasedResourceTagging;
        // Frame generation needs Reflex (PCL loads with the core). Loaded whenever it is installed, whatever the setting.
        const auto installed = [&](const wchar_t* name) {
            return GetFileAttributesW((g_directory + L"\\" + name).c_str()) != INVALID_FILE_ATTRIBUTES;
        };
        const bool generation = installed(L"sl.dlss_g.dll") && installed(L"nvngx_dlssg.dll") && installed(L"sl.reflex.dll") &&
                                installed(L"sl.pcl.dll");
        static const sl::Feature features[] = {sl::kFeatureDLSS, sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL};
        preferences.featuresToLoad = features;
        preferences.numFeaturesToLoad = generation ? 4u : 1u;
        preferences.engine = sl::EngineType::eCustom;
        preferences.engineVersion = "Nebula";
        preferences.projectId = "7c3e2b1a-5d4f-4e8a-9b6c-1f2e3d4c5b6a";
        preferences.renderAPI = sl::RenderAPI::eVulkan;
        sl::Result result = api.init(preferences, sl::kSDKVersion);
        if (result != sl::Result::eOk) {
            std::cerr << "[streamline] slInit: " << static_cast<int>(result) << '\n';
            return false;
        }
        sl::FeatureRequirements requirements{};
        result = api.get_feature_requirements(sl::kFeatureDLSS, requirements);
        if (result != sl::Result::eOk) {
            std::cerr << "[streamline] slGetFeatureRequirements: " << static_cast<int>(result) << '\n';
            (void)api.shutdown();
            return false;
        }
        if (requirements.vkNumFeatures13 != 0u) {
            std::cerr << "[streamline] it asks for Vulkan 1.3 features\n";
            (void)api.shutdown();
            return false;
        }
        g_needs = Needs{};
        g_needs.instance_extensions.assign(requirements.vkInstanceExtensions, requirements.vkInstanceExtensions + requirements.vkNumInstanceExtensions);
        g_needs.device_extensions.assign(requirements.vkDeviceExtensions, requirements.vkDeviceExtensions + requirements.vkNumDeviceExtensions);
        g_needs.features12 = sl::getVkPhysicalDeviceVulkan12Features(requirements.vkNumFeatures12, requirements.vkFeatures12);
        g_needs.graphics_queues = requirements.vkNumGraphicsQueuesRequired;
        g_needs.compute_queues = requirements.vkNumComputeQueuesRequired;
        if (generation) {
            sl::FeatureRequirements own{};
            sl::FeatureRequirements reflex{};
            if (api.get_feature_requirements(sl::kFeatureDLSS_G, own) == sl::Result::eOk &&
                api.get_feature_requirements(sl::kFeatureReflex, reflex) == sl::Result::eOk &&
                (static_cast<std::uint32_t>(own.flags) & static_cast<std::uint32_t>(sl::FeatureRequirementFlags::eVulkanSupported)) != 0u) {
                g_needs.frame_generation = true;
                add_names(g_needs.instance_extensions, own.vkInstanceExtensions, own.vkNumInstanceExtensions);
                add_names(g_needs.generation_extensions, own.vkDeviceExtensions, own.vkNumDeviceExtensions);
                add_names(g_needs.generation_extensions, reflex.vkDeviceExtensions, reflex.vkNumDeviceExtensions);
                g_needs.generation_graphics_queues = own.vkNumGraphicsQueuesRequired;
                g_needs.generation_compute_queues = own.vkNumComputeQueuesRequired;
            }
        }
        api.initialized = true;
        g_api = api;
        g_interposer = module;
        g_generation.is_feature_supported = api.is_feature_supported;
        g_generation.get_feature_function = api.get_feature_function;
        g_generation.get_new_frame_token = api.get_new_frame_token;
        g_generation.set_tag_for_frame = api.set_tag_for_frame;
        return true;
    } catch (...) {
        return false;
    }
}

const Needs& needs() noexcept { return g_needs; }

bool device_meets_needs(VkPhysicalDevice physical) noexcept {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical, &properties);
    if (properties.apiVersion < VK_API_VERSION_1_2) {
        return false;
    }
    VkPhysicalDeviceVulkan12Features have{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext = &have;
    vkGetPhysicalDeviceFeatures2(physical, &features);
    // Every field the structure holds: VkBool32s after its type and pointer.
    constexpr std::size_t kHeader = offsetof(VkPhysicalDeviceVulkan12Features, samplerMirrorClampToEdge);
    const auto* wanted = reinterpret_cast<const VkBool32*>(reinterpret_cast<const char*>(&g_needs.features12) + kHeader);
    const auto* given = reinterpret_cast<const VkBool32*>(reinterpret_cast<const char*>(&have) + kHeader);
    for (std::size_t i = 0u; i < (sizeof(have) - kHeader) / sizeof(VkBool32); ++i) {
        if (wanted[i] == VK_TRUE && given[i] != VK_TRUE) {
            return false;
        }
    }
    std::uint32_t count = 0u;
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data());
    for (const char* name : g_needs.device_extensions) {
        if (std::none_of(extensions.begin(), extensions.end(),
                         [name](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; })) {
            return false;
        }
    }
    return true;
}

bool device_meets_generation_needs(VkPhysicalDevice physical, std::uint32_t& optical_flow_family) noexcept {
    optical_flow_family = UINT32_MAX;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical, &properties);
    if (!g_needs.frame_generation || properties.apiVersion < VK_API_VERSION_1_3) {
        return false;
    }
    VkPhysicalDeviceOpticalFlowFeaturesNV optical{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_FEATURES_NV};
    VkPhysicalDeviceVulkan13Features have{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    have.pNext = &optical;
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext = &have;
    vkGetPhysicalDeviceFeatures2(physical, &features);
    if (have.synchronization2 != VK_TRUE) {
        return false;
    }
    std::uint32_t count = 0u;
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data());
    for (const char* name : g_needs.generation_extensions) {
        if (std::none_of(extensions.begin(), extensions.end(),
                         [name](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; })) {
            std::cerr << "[streamline] frame generation needs " << name << '\n';
            return false;
        }
    }
    if (optical.opticalFlow == VK_TRUE) {
        std::uint32_t families = 0u;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, nullptr);
        std::vector<VkQueueFamilyProperties> props(families);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, props.data());
        constexpr VkQueueFlags kOther = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
        for (std::uint32_t i = 0u; i < families; ++i) {
            if ((props[i].queueFlags & VK_QUEUE_OPTICAL_FLOW_BIT_NV) != 0u && (props[i].queueFlags & kOther) == 0u) {
                optical_flow_family = i;
                break;
            }
        }
    }
    return true;
}

bool attach(VkInstance instance, VkPhysicalDevice physical, VkDevice device, std::uint32_t queue_family,
            std::uint32_t first_extra_queue, bool frame_generation, std::uint32_t optical_flow_family) noexcept {
    if (!g_api.initialized) {
        return false;
    }
    sl::VulkanInfo info{};
    info.device = device;
    info.instance = instance;
    info.physicalDevice = physical;
    info.graphicsQueueFamily = queue_family;
    info.graphicsQueueIndex = first_extra_queue;
    info.computeQueueFamily = queue_family;
    info.computeQueueIndex = first_extra_queue + (frame_generation ? g_needs.generation_graphics_queues : g_needs.graphics_queues);
    if (frame_generation && optical_flow_family != UINT32_MAX) {
        info.opticalFlowQueueFamily = optical_flow_family;
        info.opticalFlowQueueIndex = 0u;
        info.useNativeOpticalFlowMode = true;
    }
    sl::Result result = g_api.set_vulkan_info(info);
    if (result != sl::Result::eOk) {
        std::cerr << "[streamline] slSetVulkanInfo: " << static_cast<int>(result) << '\n';
        return false;
    }
    sl::AdapterInfo adapter{};
    adapter.vkPhysicalDevice = physical;
    result = g_api.is_feature_supported(sl::kFeatureDLSS, adapter);
    if (result != sl::Result::eOk) {
        std::cerr << "[streamline] DLSS unavailable: " << static_cast<int>(result) << '\n';
        return false;
    }
    void* optimal = nullptr;
    void* options = nullptr;
    if (g_api.get_feature_function(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", optimal) != sl::Result::eOk ||
        g_api.get_feature_function(sl::kFeatureDLSS, "slDLSSSetOptions", options) != sl::Result::eOk || optimal == nullptr ||
        options == nullptr) {
        std::cerr << "[streamline] DLSS plugin functions missing\n";
        return false;
    }
    g_api.dlss_get_optimal_settings = reinterpret_cast<PFun_slDLSSGetOptimalSettings*>(optimal);
    g_api.dlss_set_options = reinterpret_cast<PFun_slDLSSSetOptions*>(options);
    g_api.dlss = true;
    std::cerr << "[streamline] DLSS ready\n";
    if (frame_generation) {
        g_generation.attach(adapter);
    }
    // Every present passes through it from here on: its guide asks that of manual hooking, whichever features run.
    const auto proxy = reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetProcAddress(g_interposer, "vkGetDeviceProcAddr"));
    if (proxy != nullptr) {
        const auto hook = [&](auto& function, const char* name) {
            if (const PFN_vkVoidFunction found = proxy(device, name); found != nullptr) {
                function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(found);
            }
        };
        hook(vkCreateSwapchainKHR, "vkCreateSwapchainKHR");
        hook(vkDestroySwapchainKHR, "vkDestroySwapchainKHR");
        hook(vkGetSwapchainImagesKHR, "vkGetSwapchainImagesKHR");
        hook(vkAcquireNextImageKHR, "vkAcquireNextImageKHR");
        hook(vkQueuePresentKHR, "vkQueuePresentKHR");
        hook(vkDeviceWaitIdle, "vkDeviceWaitIdle");
    }
    return true;
}

void hook_surface_calls(VkInstance instance) noexcept {
    if (g_interposer == nullptr) {
        return;
    }
    const auto proxy = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(g_interposer, "vkGetInstanceProcAddr"));
    if (proxy == nullptr) {
        return;
    }
    if (const PFN_vkVoidFunction create = proxy(instance, "vkCreateWin32SurfaceKHR"); create != nullptr) {
        vkCreateWin32SurfaceKHR = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(create);
    }
    if (const PFN_vkVoidFunction destroy = proxy(instance, "vkDestroySurfaceKHR"); destroy != nullptr) {
        vkDestroySurfaceKHR = reinterpret_cast<PFN_vkDestroySurfaceKHR>(destroy);
    }
}

void shutdown() noexcept {
    if (!g_api.initialized) {
        return;
    }
    const sl::Result result = g_api.shutdown();
    if (result != sl::Result::eOk) {
        std::cerr << "[streamline] slShutdown: " << static_cast<int>(result) << '\n';
    }
    g_api.initialized = false;
    g_api.dlss = false;
    g_generation.shutdown();
}

namespace {

sl::Resource resource(const TemporalImage& image, VkImageLayout layout) {
    sl::Resource out(sl::ResourceType::eTex2d, image.image, nullptr, image.view, static_cast<std::uint32_t>(layout));
    out.width = image.width;
    out.height = image.height;
    out.nativeFormat = static_cast<std::uint32_t>(image.format);
    out.mipLevels = 1u;
    out.arrayLayers = 1u;
    out.flags = 0u;
    out.usage = image.usage;
    return out;
}

class DlssUpscaler final : public TemporalUpscaler {
public:
    explicit DlssUpscaler(unsigned quality) : mode_(streamline_common::dlss_mode(quality)) {}

    ~DlssUpscaler() override {
        // The renderer waits for the GPU before it replaces an upscaler.
        if (g_api.dlss && g_api.free_resources != nullptr) {
            (void)g_api.free_resources(sl::kFeatureDLSS, viewport_);
        }
    }

    bool evaluate(const TemporalUpscaleInputs& in) override {
        if (!streamline::dlss_supported() || in.commands == VK_NULL_HANDLE) {
            return false;
        }
        // A second evaluation before the present (an interpolated generated frame, a second scene) takes a new frame
        // token: Streamline rejects different constants under one.
        if (evaluated_ && g_generation.frame_index() == evaluated_index_) {
            g_generation.next_frame_index();
        }
        evaluated_ = true;
        evaluated_index_ = g_generation.frame_index();
        sl::FrameToken* const token = g_generation.token();
        if (token == nullptr) {
            return failed("frame token");
        }
        sl::Constants constants{};
        streamline_common::fill_constants(constants, in);
        if (g_api.set_constants(constants, *token, viewport_) != sl::Result::eOk) {
            return failed("constants");
        }
        if (options_.outputWidth != in.output_width || options_.outputHeight != in.output_height) {
            options_.mode = mode_;
            options_.outputWidth = in.output_width;
            options_.outputHeight = in.output_height;
            // The scene target is display values in RGBA8 without an exposure value.
            options_.colorBuffersHDR = sl::Boolean::eFalse;
            options_.useAutoExposure = sl::Boolean::eTrue;
            if (g_api.dlss_set_options(viewport_, options_) != sl::Result::eOk) {
                options_.outputWidth = 0u;
                return failed("options");
            }
        }
        temporal_barriers(in, false);
        sl::Resource color = resource(in.color, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        sl::Resource depth = resource(in.depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        sl::Resource motion = resource(in.motion, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        sl::Resource output = resource(in.output, VK_IMAGE_LAYOUT_GENERAL);
        sl::Extent render{};
        render.width = in.render_width;
        render.height = in.render_height;
        sl::Extent upscaled{};
        upscaled.width = in.output_width;
        upscaled.height = in.output_height;
        const sl::ResourceTag tags[] = {
            sl::ResourceTag(&color, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &render),
            sl::ResourceTag(&output, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &upscaled),
            sl::ResourceTag(&depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilEvaluate, &render),
            sl::ResourceTag(&motion, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilEvaluate, &render)};
        bool recorded = g_api.set_tag_for_frame(*token, viewport_, tags, 4u, in.commands) == sl::Result::eOk;
        if (!recorded) {
            failed("tags");
        } else {
            const sl::BaseStructure* inputs[] = {&viewport_};
            const sl::Result result = g_api.evaluate_feature(sl::kFeatureDLSS, *token, inputs, 1u, in.commands);
            recorded = result == sl::Result::eOk;
            if (!recorded) {
                failed("evaluate");
            }
        }
        temporal_barriers(in, true);
        return recorded;
    }

    unsigned jitter_phase_count(unsigned render_width, unsigned output_width) const override {
        return streamline_common::jitter_phases(render_width, output_width);
    }

    void jitter_offset(unsigned index, unsigned phase_count, float& x, float& y) const override {
        streamline_common::jitter(index, phase_count, x, y);
    }

    const char* name() const noexcept override { return "DLSS"; }

private:
    bool failed(const char* what) {
        if (!reported_) {
            reported_ = true;
            std::cerr << "[dlss] evaluate failed: " << what << '\n';
        }
        return false;
    }

    sl::ViewportHandle viewport_{0u};
    sl::DLSSMode mode_;
    sl::DLSSOptions options_{};
    bool reported_ = false;
    bool evaluated_ = false;
    std::uint32_t evaluated_index_ = 0u;
};

}  // namespace

void open_frame(bool amd_generation) noexcept {
    const RenderConfig cfg = get_render_config();
    set_frame_generation_running(amd_generation || (streamline::frame_generation_supported() && cfg.frame_generation == 1u &&
                                                    cfg.upscaler == 3u && streamline::dlss_supported()));
    if (frame_generation_running()) {
        g_generation.sleep();
    }
    g_generation.marker(streamline::Marker::kSimulationStart);
}

void mark_submit_start() noexcept {
    g_generation.marker(streamline::Marker::kSimulationEnd);
    g_generation.marker(streamline::Marker::kRenderSubmitStart);
}

void mark_submit_end() noexcept { g_generation.marker(streamline::Marker::kRenderSubmitEnd); }

void mark_present_start() noexcept { g_generation.marker(streamline::Marker::kPresentStart); }

void mark_present_end() noexcept { g_generation.marker(streamline::Marker::kPresentEnd); }

bool generation_vsync() noexcept { return g_generation.vsync(); }

void generation_off() noexcept { g_generation.set(0u, false, 0.0f); }

void set_generation(unsigned generated, bool dynamic, float target_fps) noexcept { g_generation.set(generated, dynamic, target_fps); }

void tag_generation_inputs(VkCommandBuffer commands, const TemporalImage& depth, const TemporalImage& motion, unsigned render_width,
                           unsigned render_height, VkImageLayout depth_layout, VkImageLayout motion_layout) noexcept {
    sl::FrameToken* const token = g_generation.supported() ? g_generation.token() : nullptr;
    if (token == nullptr) {
        return;
    }
    sl::Resource depth_resource = resource(depth, depth_layout);
    sl::Resource motion_resource = resource(motion, motion_layout);
    sl::Extent render{};
    render.width = render_width;
    render.height = render_height;
    // The scene's targets are cleared and reused before the present: Streamline copies them now.
    const sl::ResourceTag tags[] = {
        sl::ResourceTag(&depth_resource, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, &render),
        sl::ResourceTag(&motion_resource, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, &render)};
    if (g_api.set_tag_for_frame(*token, sl::ViewportHandle{0u}, tags, 2u, commands) == sl::Result::eOk) {
        g_generation.note_tagged();
    } else {
        static bool reported = false;
        if (!reported) {
            reported = true;
            std::cerr << "[dlss-g] tagging failed\n";
        }
    }
}

void tag_backbuffer_region(VkCommandBuffer commands, unsigned left, unsigned top, unsigned width, unsigned height) noexcept {
    sl::FrameToken* const token = g_generation.tagged() && width != 0u && height != 0u ? g_generation.token() : nullptr;
    if (token == nullptr) {
        return;
    }
    sl::Extent region{};
    region.left = left;
    region.top = top;
    region.width = width;
    region.height = height;
    const sl::ResourceTag tag(nullptr, sl::kBufferTypeBackbuffer, sl::ResourceLifecycle{}, &region);
    (void)g_api.set_tag_for_frame(*token, sl::ViewportHandle{0u}, &tag, 1u, commands);
}

void tag_hudless(VkCommandBuffer commands, const TemporalImage* picture) noexcept {
    sl::FrameToken* const token = g_generation.tagged() ? g_generation.token() : nullptr;
    if (token == nullptr) {
        return;
    }
    sl::Extent extent{};
    sl::Resource image{};
    if (picture != nullptr) {
        image = resource(*picture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        extent.width = picture->width;
        extent.height = picture->height;
    }
    const sl::ResourceTag tag(picture != nullptr ? &image : nullptr, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent,
                              picture != nullptr ? &extent : nullptr);
    (void)g_api.set_tag_for_frame(*token, sl::ViewportHandle{0u}, &tag, 1u, commands);
}

std::unique_ptr<TemporalUpscaler> create_dlss_upscaler(unsigned render_width, unsigned render_height, unsigned output_width,
                                                       unsigned output_height, unsigned quality) {
    if (!streamline::dlss_supported() || render_width == 0u || render_height == 0u || output_width == 0u || output_height == 0u) {
        return nullptr;
    }
    // A mode evaluates only render widths inside its range: the requested one, else the next higher-quality one that holds
    // this width.
    const unsigned mode = quality == 5u ? 1u : std::min(quality, 4u);  // no ultra quality mode: quality
    for (unsigned candidate = mode + 1u; candidate-- > 0u;) {
        sl::DLSSOptions options{};
        options.mode = streamline_common::dlss_mode(candidate);
        options.outputWidth = output_width;
        options.outputHeight = output_height;
        sl::DLSSOptimalSettings settings{};
        if (g_api.dlss_get_optimal_settings(options, settings) == sl::Result::eOk && render_width >= settings.renderWidthMin &&
            render_width <= settings.renderWidthMax) {
            std::cerr << "[dlss] " << render_width << 'x' << render_height << " -> " << output_width << 'x' << output_height << " mode "
                      << candidate << " of requested " << quality << '\n';
            return std::make_unique<DlssUpscaler>(candidate);
        }
    }
    std::cerr << "[dlss] " << render_width << 'x' << render_height << " -> " << output_width << 'x' << output_height
              << ": outside every mode's range\n";
    return std::make_unique<DlssUpscaler>(mode);
}

}  // namespace vk::streamline_vk

}  // namespace galaxy::render
