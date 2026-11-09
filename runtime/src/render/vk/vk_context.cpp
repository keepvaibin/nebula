// SPDX-License-Identifier: GPL-3.0-only
// The shared Vulkan device, its submissions and deferred destruction (vk_context.h).
#include "galaxy/run_options.h"
#include "galaxy/render/vk/vk_context.h"
#include "galaxy/render/render_config.h"

#include "galaxy/render/vk/temporal_upscaler_vk.h"
#if defined(_WIN32)
#include "galaxy/render/xess_library.h"
#include "galaxy/render/vk/streamline_vk.h"
#endif

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#include <dxgi.h>
#include <wrl/client.h>
#include <xess/xess_vk.h>
#endif

#include "galaxy/render/gpu_types.h"

#include <vk_mem_alloc.h>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

#if !defined(_WIN32)
#include <time.h>
#endif

namespace galaxy::render::vk {

namespace {
std::array<std::atomic<std::int64_t>, static_cast<unsigned>(DriverCall::Count)> g_driver_cpu{};
}  // namespace

std::int64_t thread_cpu_ns() noexcept {
#if defined(_WIN32)
    return 0;
#else
    timespec now{};
    return clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) == 0 ? std::int64_t{now.tv_sec} * 1000000000 + now.tv_nsec : 0;
#endif
}

void note_driver_cpu(DriverCall call, std::int64_t ns) noexcept {
    g_driver_cpu[static_cast<unsigned>(call)].fetch_add(ns, std::memory_order_relaxed);
}

std::array<std::int64_t, static_cast<unsigned>(DriverCall::Count)> take_driver_cpu() noexcept {
    std::array<std::int64_t, static_cast<unsigned>(DriverCall::Count)> taken{};
    for (unsigned i = 0; i < taken.size(); ++i) {
        taken[i] = g_driver_cpu[i].exchange(0, std::memory_order_relaxed);
    }
    return taken;
}

void check(VkResult result, const char* what) {
    if (result < VK_SUCCESS) {
        throw std::runtime_error(std::string("[vulkan] ") + what + " failed: VkResult " + std::to_string(result));
    }
}

namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL debug_message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data,
                                             void*) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT && data != nullptr && data->pMessage != nullptr) {
        std::fprintf(stderr, "[vulkan] %s\n", data->pMessage);
    }
    return VK_FALSE;
}

bool has_layer(const char* name) {
    std::uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    return std::any_of(layers.begin(), layers.end(),
                       [name](const VkLayerProperties& layer) { return std::strcmp(layer.layerName, name) == 0; });
}

bool has_instance_extension(const char* name) {
    std::uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
    return std::any_of(extensions.begin(), extensions.end(), [name](const VkExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

#if defined(_WIN32)
// The PCI subsystem id of the DXGI adapter with this LUID: Vulkan does not report it, and gpu_identity names it. 0
// when none matches.
std::uint32_t dxgi_subsystem(std::uint64_t luid) {
    if (luid == 0u) {
        return 0u;
    }
    const HMODULE module = LoadLibraryExW(L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    using Create = HRESULT(WINAPI*)(REFIID, void**);
    const auto create = module != nullptr ? reinterpret_cast<Create>(GetProcAddress(module, "CreateDXGIFactory1")) : nullptr;
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (create == nullptr || FAILED(create(IID_PPV_ARGS(&factory)))) {
        return 0u;
    }
    for (UINT index = 0u;; ++index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) {
            return 0u;
        }
        DXGI_ADAPTER_DESC1 description{};
        std::uint64_t adapter_luid = 0u;
        if (SUCCEEDED(adapter->GetDesc1(&description))) {
            static_assert(sizeof(description.AdapterLuid) == sizeof(adapter_luid));
            std::memcpy(&adapter_luid, &description.AdapterLuid, sizeof(adapter_luid));
            if (adapter_luid == luid) {
                return description.SubSysId;
            }
        }
    }
}
#endif

// A device as Nebula Settings names it.
struct Adapter {
    std::string identity;  // gpu_identity
    std::uint32_t subsys = 0u;
    std::uint64_t luid = 0u;  // 0: the driver gives none
};

Adapter adapter_of(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties core{};
    vkGetPhysicalDeviceProperties(device, &core);
    Adapter adapter;
    if (core.apiVersion >= VK_API_VERSION_1_1) {
        VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &id};
        vkGetPhysicalDeviceProperties2(device, &properties);
        if (id.deviceLUIDValid == VK_TRUE) {
            static_assert(VK_LUID_SIZE == sizeof(adapter.luid));
            std::memcpy(&adapter.luid, id.deviceLUID, sizeof(adapter.luid));
        }
    }
#if defined(_WIN32)
    adapter.subsys = dxgi_subsystem(adapter.luid);
#endif
    adapter.identity = render::gpu_identity(core.vendorID, core.deviceID, adapter.subsys);
    return adapter;
}

bool has_device_extension(VkPhysicalDevice device, const char* name) {
    std::uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
    return std::any_of(extensions.begin(), extensions.end(), [name](const VkExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

}  // namespace

Texture::~Texture() {
    Context& context = Context::get();
    if (!context.ready()) {
        return;
    }
    const VkDevice device = context.device();
    VmaAllocator_T* const allocator = context.allocator();
    const VkImage image_handle = image;
    VmaAllocation_T* const allocation_handle = allocation;
    const VkImageView views[2] = {view, target_view};
    const VkFramebuffer framebuffers[3] = {framebuffer, scene_framebuffer, scene_resolve_framebuffer};
    context.defer([=] {
        for (const VkFramebuffer f : framebuffers) {
            if (f != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device, f, nullptr);
            }
        }
        for (const VkImageView v : views) {
            if (v != VK_NULL_HANDLE) {
                vkDestroyImageView(device, v, nullptr);
            }
        }
        if (image_handle != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, image_handle, allocation_handle);
        }
    });
}

Buffer::~Buffer() {
    Context& context = Context::get();
    if (!context.ready() || buffer == VK_NULL_HANDLE) {
        return;
    }
    VmaAllocator_T* const allocator = context.allocator();
    const VkBuffer buffer_handle = buffer;
    VmaAllocation_T* const allocation_handle = allocation;
    const std::uint64_t address_base = address;
    Context* const owner = &context;
    context.defer([=] {
        owner->release_address(address_base);
        vmaDestroyBuffer(allocator, buffer_handle, allocation_handle);
    });
}

Context& Context::get() noexcept {
    static Context context;
    return context;
}

bool Context::initialize(const std::string& chosen_gpu, bool prefer_integrated, bool validation,
                         std::vector<const char*> instance_extensions,
                         const std::function<VkSurfaceKHR(VkInstance)>& create_surface, VkSurfaceKHR* surface) {
    if (device_ != VK_NULL_HANDLE) {
        return true;
    }
    failure_.clear();
    if (volkInitialize() != VK_SUCCESS) {
        std::fprintf(stderr, "[vulkan] no Vulkan loader\n");
        failure_ = "no Vulkan driver";
        return false;
    }
    std::vector<const char*> layers;
    if (validation && has_layer("VK_LAYER_KHRONOS_validation")) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }
    // Object names and the validation messages only with the validation layer: a name is a driver call per object,
    // and the Android emulator's driver crashes on them.
    const bool debug_utils = !layers.empty() && has_instance_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (debug_utils) {
        instance_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Nebula";
    app.pEngineName = "Nebula";
    // FSR 3's shaders are SPIR-V 1.5: a Vulkan 1.2 instance where the loader has it. A 1.3 loader gets a 1.3 instance, so
    // a 1.3 device takes synchronization2 as core Vulkan (no extension).
    std::uint32_t loader_version = VK_API_VERSION_1_1;
    if (vkEnumerateInstanceVersion != nullptr) {
        (void)vkEnumerateInstanceVersion(&loader_version);
    }
    app.apiVersion = loader_version >= VK_API_VERSION_1_3                    ? VK_API_VERSION_1_3
                     : fsr3_compiled() && loader_version >= VK_API_VERSION_1_2 ? VK_API_VERSION_1_2
                                                                               : VK_API_VERSION_1_1;
#if defined(_WIN32)
    // DLSS: what Streamline needs of the instance, where its files and an RTX adapter are here.
    bool streamline = streamline_vk::start();
    if (streamline) {
        for (const char* name : streamline_vk::needs().instance_extensions) {
            streamline = streamline && has_instance_extension(name);
        }
    }
    if (streamline) {
        for (const char* name : streamline_vk::needs().instance_extensions) {
            if (std::none_of(instance_extensions.begin(), instance_extensions.end(),
                             [&](const char* have) { return std::strcmp(have, name) == 0; })) {
                instance_extensions.push_back(name);
            }
        }
        // Frame generation's features are Vulkan 1.3's.
        app.apiVersion = std::max(app.apiVersion, streamline_vk::needs().frame_generation && loader_version >= VK_API_VERSION_1_3
                                                      ? VK_API_VERSION_1_3
                                                      : VK_API_VERSION_1_2);
    }
#endif
#if defined(_WIN32)
    // XeSS's instance needs, where its library is here.
    if (const auto required = xess_library::get<decltype(xessVKGetRequiredInstanceExtensions)>("xessVKGetRequiredInstanceExtensions")) {
        std::uint32_t count = 0u;
        const char* const* names = nullptr;
        std::uint32_t version = 0u;
        if (required(&count, &names, &version) == XESS_RESULT_SUCCESS) {
            for (std::uint32_t i = 0u; i < count; ++i) {
                if (std::none_of(instance_extensions.begin(), instance_extensions.end(),
                                 [&](const char* have) { return std::strcmp(have, names[i]) == 0; })) {
                    instance_extensions.push_back(names[i]);
                }
            }
            app.apiVersion = std::max(app.apiVersion, version);
            xess_instance_ready_ = true;
        }
    }
#endif
    VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pApplicationInfo = &app;
    instance_info.enabledExtensionCount = static_cast<std::uint32_t>(instance_extensions.size());
    instance_info.ppEnabledExtensionNames = instance_extensions.data();
    instance_info.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    instance_info.ppEnabledLayerNames = layers.data();
    if (vkCreateInstance(&instance_info, nullptr, &instance_) != VK_SUCCESS) {
        std::fprintf(stderr, "[vulkan] vkCreateInstance failed\n");
        return false;
    }
    volkLoadInstance(instance_);
    if (debug_utils && !layers.empty()) {
        VkDebugUtilsMessengerCreateInfoEXT messenger{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        messenger.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messenger.pfnUserCallback = debug_message;
        vkCreateDebugUtilsMessengerEXT(instance_, &messenger, nullptr, &messenger_);
    }
    debug_names_ = debug_utils;
    VkSurfaceKHR window_surface = VK_NULL_HANDLE;
    // Every failure from here leaves no instance, device or surface behind.
    const auto fail = [&](const char* message) {
        std::fprintf(stderr, "[vulkan] %s\n", message);
        failure_ = message;
        if (window_surface != VK_NULL_HANDLE) {
            if (device_ != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device_);
            }
            vkDestroySurfaceKHR(instance_, window_surface, nullptr);
            if (surface != nullptr) {
                *surface = VK_NULL_HANDLE;
            }
        }
        shutdown();
        return false;
    };
    if (create_surface) {
        window_surface = create_surface(instance_);
        if (window_surface == VK_NULL_HANDLE) {
            return fail("the window's surface could not be created");
        }
        if (surface != nullptr) {
            *surface = window_surface;
        }
    }

    // The GPU chosen in Nebula Settings, else the first device of the preferred kind; each needs a graphics queue
    // (that presents to the surface) and the swapchain extension.
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());
    const VkPhysicalDeviceType wanted =
        prefer_integrated ? VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU : VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    {
        std::vector<std::pair<unsigned, VkPhysicalDevice>> ranked;
        ranked.reserve(devices.size());
        for (const VkPhysicalDevice device : devices) {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(device, &properties);
            const bool chosen = !chosen_gpu.empty() && adapter_of(device).identity == chosen_gpu;
            ranked.emplace_back((chosen ? 2u : 0u) + (properties.deviceType == wanted ? 1u : 0u), device);
        }
        std::stable_sort(ranked.begin(), ranked.end(),
                         [](const auto& a, const auto& b) { return a.first > b.first; });
        for (std::size_t i = 0; i < ranked.size(); ++i) {
            devices[i] = ranked[i].second;
        }
    }
    bool vulkan_1_0_only = false;
    for (const VkPhysicalDevice candidate : devices) {
        if (!has_device_extension(candidate, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            continue;
        }
        // Vulkan 1.1 at least (the memory allocator's and the device queries' entry points).
        VkPhysicalDeviceProperties candidate_properties{};
        vkGetPhysicalDeviceProperties(candidate, &candidate_properties);
        if (candidate_properties.apiVersion < VK_API_VERSION_1_1) {
            vulkan_1_0_only = true;
            continue;
        }
        std::uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
        std::vector<VkQueueFamilyProperties> props(families);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, props.data());
        for (std::uint32_t i = 0; i < families; ++i) {
            VkBool32 presents = VK_TRUE;
            if (window_surface != VK_NULL_HANDLE &&
                vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, window_surface, &presents) != VK_SUCCESS) {
                presents = VK_FALSE;
            }
            if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u && presents == VK_TRUE) {
                physical_ = candidate;
                queue_family_ = i;
                break;
            }
        }
        if (physical_ != VK_NULL_HANDLE) {
            break;
        }
    }
    if (physical_ == VK_NULL_HANDLE) {
        return fail(vulkan_1_0_only ? "the GPU's Vulkan driver is version 1.0; Nebula needs Vulkan 1.1"
                                    : "no device with a graphics queue that presents to the window, and swapchains");
    }
    vkGetPhysicalDeviceProperties(physical_, &properties_);
    device_name_ = properties_.deviceName;
    {
        Adapter adapter = adapter_of(physical_);
        gpu_id_ = std::move(adapter.identity);
        subsys_id_ = adapter.subsys;
        luid_ = adapter.luid;
    }
    if (!chosen_gpu.empty()) {
        std::printf("[gpu] chosen %s %s\n", chosen_gpu.c_str(),
                    gpu_id_ == chosen_gpu ? "in use" : "not present or unusable; choosing automatically");
    }
    identity_ = identify(physical_, has_device_extension(physical_, VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME));
    quirks_ = quirks_for(identity_);
    VkPhysicalDeviceFeatures available{};
    vkGetPhysicalDeviceFeatures(physical_, &available);
    // GALAXY_VK_TEST_NO_FEATURE=<names, comma-separated>: this GPU acts without those features, as a phone's may, so
    // the paths for it run on any GPU. "indexing": no descriptor indexing (draws bind their own tables).
    bool test_no_indexing = false;
    if (const char* const names = ::galaxy::run_options().vk_test_no_feature) {
        const std::string list = std::string(",") + names + ",";
        const auto off = [&list](const char* name) { return list.find(std::string(",") + name + ",") != std::string::npos; };
        if (off("bc")) available.textureCompressionBC = VK_FALSE;
        if (off("anisotropy")) available.samplerAnisotropy = VK_FALSE;
        if (off("gather")) available.shaderImageGatherExtended = VK_FALSE;
        test_no_indexing = off("indexing");
    }
    VkPhysicalDeviceFeatures enabled{};
    enabled.sampleRateShading = available.sampleRateShading;
    sample_rate_shading_ = available.sampleRateShading == VK_TRUE;
    enabled.samplerAnisotropy = available.samplerAnisotropy;
    enabled.textureCompressionBC = available.textureCompressionBC;
    enabled.depthClamp = available.depthClamp;
    enabled.independentBlend = available.independentBlend;
    enabled.shaderImageGatherExtended = available.shaderImageGatherExtended;
    enabled.pipelineStatisticsQuery = available.pipelineStatisticsQuery;
    sampler_anisotropy_ = available.samplerAnisotropy == VK_TRUE;
    bc_textures_ = available.textureCompressionBC == VK_TRUE;
    gather_extended_ = available.shaderImageGatherExtended == VK_TRUE;
    pipeline_statistics_ = available.pipelineStatisticsQuery == VK_TRUE;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = queue_family_;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    std::vector<const char*> device_extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    // On a Vulkan 1.2 instance and device one chain carries every promoted feature: the separate structures must not
    // appear beside it.
    const bool core_12 = app.apiVersion >= VK_API_VERSION_1_2 && properties_.apiVersion >= VK_API_VERSION_1_2;
    VkPhysicalDeviceVulkan11Features features11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceOpticalFlowFeaturesNV optical_flow{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_FEATURES_NV};
    // Descriptor indexing: one array of every texture and one of every sampler, written while bound and read by an
    // index that is the same for every pixel of a draw (no non-uniform indexing). Core in Vulkan 1.2, else
    // VK_EXT_descriptor_indexing. It is used only when all of it is here.
    const bool indexing_extension =
        !core_12 && has_device_extension(physical_, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    std::string indexing_note = test_no_indexing ? "switched off by GALAXY_VK_TEST_NO_FEATURE" : "no descriptor indexing";
    if (!test_no_indexing && (core_12 || indexing_extension)) {
        VkPhysicalDeviceDescriptorIndexingFeatures have{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &have;
        vkGetPhysicalDeviceFeatures2(physical_, &features);
        VkPhysicalDeviceDescriptorIndexingProperties limits{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES};
        VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties.pNext = &limits;
        vkGetPhysicalDeviceProperties2(physical_, &properties);
        // The shaders' arrays are runtime-sized (OpTypeRuntimeArray: runtimeDescriptorArray) and the image set's length is
        // chosen at allocation (descriptorBindingVariableDescriptorCount).
        const bool bits = have.runtimeDescriptorArray == VK_TRUE && have.descriptorBindingPartiallyBound == VK_TRUE &&
                          have.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE &&
                          have.descriptorBindingUpdateUnusedWhilePending == VK_TRUE &&
                          have.descriptorBindingVariableDescriptorCount == VK_TRUE &&
                          available.shaderSampledImageArrayDynamicIndexing == VK_TRUE;
        // The image array's upper bound: the set and per-stage update-after-bind image limits, what the per-stage resource
        // limit leaves beside the sampler array and set 0's buffers (16 with room), and half of what all pools may hold
        // (a larger set is made while the frames that read the smaller finish: together at most twice the larger).
        const std::uint64_t others = std::uint64_t{kBindlessSamplerSlots} + 16u;
        const auto beyond = [others](std::uint64_t limit) { return limit > others ? limit - others : 0u; };
        const std::uint64_t images =
            std::min({std::uint64_t{limits.maxDescriptorSetUpdateAfterBindSampledImages},
                      std::uint64_t{limits.maxPerStageDescriptorUpdateAfterBindSampledImages},
                      beyond(limits.maxPerStageUpdateAfterBindResources), beyond(limits.maxUpdateAfterBindDescriptorsInAllPools) / 2u,
                      std::uint64_t{kBindlessTextureSlotsCap}});
        const bool room = images >= kBindlessTextureSlotsMin &&
                          limits.maxDescriptorSetUpdateAfterBindSamplers >= kBindlessSamplerSlots &&
                          limits.maxPerStageDescriptorUpdateAfterBindSamplers >= kBindlessSamplerSlots;
        bindless_ = bits && room;
        texture_array_max_ = bindless_ ? static_cast<std::uint32_t>(images) : 0u;
        indexing_note = bindless_ ? "descriptor indexing"
                        : !bits   ? "descriptor indexing lacks a needed feature"
                                  : "descriptor indexing limits below the texture and sampler arrays";
    }
    {
        // A bound table's maps: what the per-stage image, sampler and resource limits leave beside the three constants
        // buffers and two storage buffers of set 0.
        const VkPhysicalDeviceLimits& limits = properties_.limits;
        const std::uint32_t by_resources = limits.maxPerStageResources > 16u ? (limits.maxPerStageResources - 16u) / 2u : 0u;
        table_slots_ = std::min({limits.maxPerStageDescriptorSampledImages, limits.maxPerStageDescriptorSamplers, by_resources});
    }
    std::vector<float> queue_priorities(1u, priority);
    VkDeviceQueueCreateInfo queue_infos[2]{};
    std::uint32_t queue_info_count = 1u;
    bool generation_device = false;
    std::uint32_t optical_flow_family = UINT32_MAX;
#if defined(_WIN32)
    const bool dlss_device = streamline && core_12 && streamline_vk::device_meets_needs(physical_);
    if (dlss_device) {
        const streamline_vk::Needs& needs = streamline_vk::needs();
        for (const char* name : needs.device_extensions) {
            device_extensions.push_back(name);
        }
        features12 = needs.features12;
        std::uint32_t families = 0u;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_, &families, nullptr);
        std::vector<VkQueueFamilyProperties> props(families);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_, &families, props.data());
        std::uint32_t extra = needs.graphics_queues + needs.compute_queues;
        // Frame generation on a Vulkan 1.3 instance and device that give all it asks.
        const std::uint32_t generation_extra = needs.generation_graphics_queues + needs.generation_compute_queues;
        if (needs.frame_generation && app.apiVersion >= VK_API_VERSION_1_3 &&
            1u + generation_extra <= props[queue_family_].queueCount &&
            streamline_vk::device_meets_generation_needs(physical_, optical_flow_family)) {
            generation_device = true;
            extra = generation_extra;
            for (const char* name : needs.generation_extensions) {
                device_extensions.push_back(name);
            }
            features13.synchronization2 = VK_TRUE;
            VkPhysicalDeviceVulkan13Features have13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
            VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            features.pNext = &have13;
            vkGetPhysicalDeviceFeatures2(physical_, &features);
            features13.maintenance4 = have13.maintenance4;
            optical_flow.opticalFlow = optical_flow_family != UINT32_MAX ? VK_TRUE : VK_FALSE;
        }
        if (1u + extra <= props[queue_family_].queueCount) {
            queue_priorities.assign(1u + extra, priority);
            queue_info.queueCount = 1u + extra;
            queue_info.pQueuePriorities = queue_priorities.data();
        }
    }
#endif
    // FSR 3 frame generation's acquiring and presenting queues, last in the family.
    std::uint32_t generation_queues_at = 0u;
    if (fsr_frame_generation_compiled()) {
        std::uint32_t families = 0u;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_, &families, nullptr);
        std::vector<VkQueueFamilyProperties> props(families);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_, &families, props.data());
        if (queue_info.queueCount + 2u <= props[queue_family_].queueCount) {
            generation_queues_at = queue_info.queueCount;
            queue_priorities.assign(queue_info.queueCount + 2u, priority);
            queue_info.queueCount += 2u;
            queue_info.pQueuePriorities = queue_priorities.data();
        }
    }
    queue_infos[0] = queue_info;
    if (optical_flow_family != UINT32_MAX) {
        queue_infos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_infos[1].queueFamilyIndex = optical_flow_family;
        queue_infos[1].queueCount = 1u;
        queue_infos[1].pQueuePriorities = &priority;
        queue_info_count = 2u;
    }
    // A device that is a portability subset (MoltenVK) must have it enabled; Nebula uses nothing it leaves out.
    if (has_device_extension(physical_, "VK_KHR_portability_subset")) {
        device_extensions.push_back("VK_KHR_portability_subset");
    }
    // Presents at a requested display time (Android's swapchain implements it for every driver): the frame pacing of
    // RendererVk::end_frame.
    display_timing_ = has_device_extension(physical_, VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
    if (display_timing_) {
        device_extensions.push_back(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
    }
    // The driver's per-heap budget (the internal-resolution memory check), where it reports one.
    memory_budget_ = has_device_extension(physical_, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    if (memory_budget_) {
        device_extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    }
    // Depth clipping apart from depth clamping: with both, fragment depth (shader-written too) is clamped to the
    // viewport's range while primitives are still clipped, as D3D does.
    VkPhysicalDeviceDepthClipEnableFeaturesEXT depth_clip{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT};
    if (available.depthClamp == VK_TRUE && has_device_extension(physical_, VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME)) {
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &depth_clip;
        vkGetPhysicalDeviceFeatures2(physical_, &features);
        depth_clip_enable_ = depth_clip.depthClipEnable == VK_TRUE;
        if (depth_clip_enable_) {
            device_extensions.push_back(VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME);
        }
    }
    // Synchronization2 (barriers_vk.h): copy, resolve and clear apart from other transfers, stages per image. A device
    // and instance of Vulkan 1.3 (the frame generation device always) take it as core through the 1.3 features chain;
    // a 1.2 device takes the extension where it has it; any other uses version-1 barriers.
    const bool core_13 = app.apiVersion >= VK_API_VERSION_1_3 && properties_.apiVersion >= VK_API_VERSION_1_3;
    bool sync2_core = generation_device;
    if (core_13 && !generation_device) {
        VkPhysicalDeviceVulkan13Features have13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &have13;
        vkGetPhysicalDeviceFeatures2(physical_, &features);
        sync2_core = have13.synchronization2 == VK_TRUE;
        features13.synchronization2 = have13.synchronization2;
    }
    VkPhysicalDeviceSynchronization2FeaturesKHR sync2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR};
    bool sync2_extension = false;
    if (!sync2_core && has_device_extension(physical_, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)) {
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &sync2;
        vkGetPhysicalDeviceFeatures2(physical_, &features);
        sync2_extension = sync2.synchronization2 == VK_TRUE;
        if (sync2_extension) {
            device_extensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
        }
        sync2.pNext = nullptr;
    }
    // FSR 3 (temporal_upscaler_vk.cpp): half floats, 16-bit storage and its compute shaders' subgroup sizes, and a depth
    // aspect read while the stencil aspect stays an attachment (its barriers name the depth alone).
    VkPhysicalDeviceShaderFloat16Int8Features float16{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
    VkPhysicalDevice16BitStorageFeatures storage16{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES};
    VkPhysicalDeviceSubgroupSizeControlFeaturesEXT subgroup{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT};
    VkPhysicalDeviceSeparateDepthStencilLayoutsFeaturesKHR separate{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES_KHR};
    if (fsr3_compiled() && core_12 && has_device_extension(physical_, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME) &&
        has_device_extension(physical_, VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME) &&
        has_device_extension(physical_, VK_KHR_SEPARATE_DEPTH_STENCIL_LAYOUTS_EXTENSION_NAME) &&
        has_device_extension(physical_, VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME) &&
        available.independentBlend == VK_TRUE && available.shaderInt16 == VK_TRUE &&
        available.shaderStorageImageWriteWithoutFormat == VK_TRUE &&
        available.shaderStorageImageExtendedFormats == VK_TRUE) {
        float16.pNext = &storage16;
        storage16.pNext = &subgroup;
        subgroup.pNext = &separate;
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &float16;
        vkGetPhysicalDeviceFeatures2(physical_, &features);
        temporal_upscaling_ = float16.shaderFloat16 == VK_TRUE && storage16.storageBuffer16BitAccess == VK_TRUE &&
                              subgroup.subgroupSizeControl == VK_TRUE && subgroup.computeFullSubgroups == VK_TRUE &&
                              separate.separateDepthStencilLayouts == VK_TRUE;
        if (temporal_upscaling_) {
            enabled.shaderInt16 = VK_TRUE;
            device_extensions.push_back(VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);
            device_extensions.push_back(VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME);
            device_extensions.push_back(VK_KHR_SEPARATE_DEPTH_STENCIL_LAYOUTS_EXTENSION_NAME);
            device_extensions.push_back(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);
            enabled.shaderStorageImageWriteWithoutFormat = VK_TRUE;
            enabled.shaderStorageImageExtendedFormats = VK_TRUE;
        }
    }
    depth_clamp_ = available.depthClamp == VK_TRUE;
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    if (depth_clip_enable_) {
        depth_clip.pNext = nullptr;
        depth_clip.depthClipEnable = VK_TRUE;
        device_info.pNext = &depth_clip;
    }
    if (sync2_extension) {
        sync2.pNext = const_cast<void*>(device_info.pNext);
        device_info.pNext = &sync2;
    }
    if (core_12) {
        if (temporal_upscaling_) {
            features11.storageBuffer16BitAccess = VK_TRUE;
            features12.shaderFloat16 = VK_TRUE;
            features12.separateDepthStencilLayouts = VK_TRUE;
            if (sync2_core) {
                // On a 1.3 chain the subgroup features are its own: the separate structure must not appear beside it.
                features13.subgroupSizeControl = subgroup.subgroupSizeControl;
                features13.computeFullSubgroups = subgroup.computeFullSubgroups;
            } else {
                subgroup.pNext = const_cast<void*>(device_info.pNext);
                device_info.pNext = &subgroup;
            }
        }
        if (sync2_core) {
            // The 1.3 features structure: synchronization2 (and, on the frame generation device, its own features).
            void* tail = const_cast<void*>(device_info.pNext);
            if (generation_device) {
                optical_flow.pNext = tail;
                tail = &optical_flow;
            }
            features13.pNext = tail;
            device_info.pNext = &features13;
        }
        features12.pNext = const_cast<void*>(device_info.pNext);
        features11.pNext = &features12;
        device_info.pNext = &features11;
    }
    VkPhysicalDeviceDescriptorIndexingFeatures indexing_enabled{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
    if (bindless_) {
        enabled.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
        if (core_12) {
            features12.runtimeDescriptorArray = VK_TRUE;
            features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
            features12.descriptorBindingPartiallyBound = VK_TRUE;
            features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
            features12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
        } else {
            indexing_enabled.runtimeDescriptorArray = VK_TRUE;
            indexing_enabled.descriptorBindingVariableDescriptorCount = VK_TRUE;
            indexing_enabled.descriptorBindingPartiallyBound = VK_TRUE;
            indexing_enabled.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
            indexing_enabled.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
            indexing_enabled.pNext = const_cast<void*>(device_info.pNext);
            device_info.pNext = &indexing_enabled;
            device_extensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
        }
    }
#if defined(_WIN32)
    // XeSS's device needs, where its library is here: its features through the chain (its function adds to it).
    VkPhysicalDeviceFeatures2 enabled_chain{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    const auto xess_extensions = xess_library::get<decltype(xessVKGetRequiredDeviceExtensions)>("xessVKGetRequiredDeviceExtensions");
    const auto xess_features = xess_library::get<decltype(xessVKGetRequiredDeviceFeatures)>("xessVKGetRequiredDeviceFeatures");
    if (xess_instance_ready_ && xess_extensions != nullptr && xess_features != nullptr) {
        std::uint32_t count = 0u;
        const char* const* names = nullptr;
        bool have_all = xess_extensions(instance_, physical_, &count, &names) == XESS_RESULT_SUCCESS;
        for (std::uint32_t i = 0u; have_all && i < count; ++i) {
            have_all = has_device_extension(physical_, names[i]);
        }
        if (have_all && available.shaderStorageImageWriteWithoutFormat == VK_TRUE) {
            enabled.shaderStorageImageWriteWithoutFormat = VK_TRUE;
            enabled_chain.features = enabled;
            enabled_chain.pNext = const_cast<void*>(device_info.pNext);
            void* chain = &enabled_chain;
            if (xess_features(instance_, physical_, &chain) == XESS_RESULT_SUCCESS) {
                for (std::uint32_t i = 0u; i < count; ++i) {
                    if (std::none_of(device_extensions.begin(), device_extensions.end(),
                                     [&](const char* have) { return std::strcmp(have, names[i]) == 0; })) {
                        device_extensions.push_back(names[i]);
                    }
                }
                device_info.pNext = chain;
                xess_upscaling_ = true;
            }
        }
    }
#endif
    // A library that added its own Vulkan 1.3 features structure (a chain may hold each structure once): ours leaves
    // after its bits are set in the library's.
    if (sync2_core) {
        auto* ours = &features13;
        VkPhysicalDeviceVulkan13Features* other = nullptr;
        for (auto* node = static_cast<VkBaseOutStructure*>(const_cast<void*>(device_info.pNext)); node != nullptr; node = node->pNext) {
            if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES &&
                node != reinterpret_cast<VkBaseOutStructure*>(ours)) {
                other = reinterpret_cast<VkPhysicalDeviceVulkan13Features*>(node);
            }
        }
        if (other != nullptr) {
            other->synchronization2 = VK_TRUE;
            other->maintenance4 = (other->maintenance4 || ours->maintenance4) ? VK_TRUE : VK_FALSE;
            other->subgroupSizeControl = (other->subgroupSizeControl || ours->subgroupSizeControl) ? VK_TRUE : VK_FALSE;
            other->computeFullSubgroups = (other->computeFullSubgroups || ours->computeFullSubgroups) ? VK_TRUE : VK_FALSE;
            for (auto* node = reinterpret_cast<VkBaseOutStructure*>(&device_info); node->pNext != nullptr; node = node->pNext) {
                if (node->pNext == reinterpret_cast<VkBaseOutStructure*>(ours)) {
                    node->pNext = reinterpret_cast<VkBaseOutStructure*>(ours)->pNext;
                    break;
                }
            }
        }
    }
    // A library that added its own synchronization2 structure (a chain may hold each structure once): ours leaves.
    if (sync2_extension) {
        const auto* ours = reinterpret_cast<const VkBaseInStructure*>(&sync2);
        bool other = false;
        for (const auto* node = static_cast<const VkBaseInStructure*>(device_info.pNext); node != nullptr; node = node->pNext) {
            other = other || (node != ours && node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES);
        }
        if (other) {
            for (auto* node = reinterpret_cast<VkBaseInStructure*>(&device_info); node->pNext != nullptr;
                 node = const_cast<VkBaseInStructure*>(node->pNext)) {
                if (node->pNext == ours) {
                    node->pNext = ours->pNext;
                    break;
                }
            }
        }
    }
    device_info.queueCreateInfoCount = queue_info_count;
    queue_infos[0] = queue_info;
    device_info.pQueueCreateInfos = queue_infos;
    device_info.enabledExtensionCount = static_cast<std::uint32_t>(device_extensions.size());
    device_info.ppEnabledExtensionNames = device_extensions.data();
    device_info.pEnabledFeatures = xess_upscaling_ ? nullptr : &enabled;
    std::sort(device_extensions.begin(), device_extensions.end(), [](const char* a, const char* b) { return std::strcmp(a, b) < 0; });
    device_extensions.erase(std::unique(device_extensions.begin(), device_extensions.end(),
                                        [](const char* a, const char* b) { return std::strcmp(a, b) == 0; }),
                            device_extensions.end());
    device_info.enabledExtensionCount = static_cast<std::uint32_t>(device_extensions.size());
    device_info.ppEnabledExtensionNames = device_extensions.data();
    enabled_device_extensions_.assign(device_extensions.begin(), device_extensions.end());
    if (vkCreateDevice(physical_, &device_info, nullptr, &device_) != VK_SUCCESS) {
        device_ = VK_NULL_HANDLE;
        return fail("vkCreateDevice failed");
    }
    volkLoadDevice(device_);
    pipeline_barrier2_ = sync2_core ? vkCmdPipelineBarrier2 : sync2_extension ? vkCmdPipelineBarrier2KHR : nullptr;
    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
    if (generation_queues_at != 0u) {
        vkGetDeviceQueue(device_, queue_family_, generation_queues_at, &generation_present_queue_);
        vkGetDeviceQueue(device_, queue_family_, generation_queues_at + 1u, &generation_acquire_queue_);
    }
#if defined(_WIN32)
    if (dlss_device) {
        (void)streamline_vk::attach(instance_, physical_, device_, queue_family_, 1u, generation_device, optical_flow_family);
        // The surface again, through Streamline: it finds the window from it. The window and its queue support stay the same.
        if (generation_device && create_surface && window_surface != VK_NULL_HANDLE) {
            streamline_vk::hook_surface_calls(instance_);
            vkDestroySurfaceKHR(instance_, window_surface, nullptr);
            window_surface = create_surface(instance_);
            if (surface != nullptr) {
                *surface = window_surface;
            }
            if (window_surface == VK_NULL_HANDLE) {
                return fail("the window's surface could not be created");
            }
        }
    }
#endif

    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo allocator_info{};
    allocator_info.vulkanApiVersion = VK_API_VERSION_1_1;
    allocator_info.physicalDevice = physical_;
    allocator_info.device = device_;
    allocator_info.instance = instance_;
    allocator_info.pVulkanFunctions = &functions;
    if (memory_budget_) {
        allocator_info.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    }
    if (vmaCreateAllocator(&allocator_info, &allocator_) != VK_SUCCESS) {
        allocator_ = nullptr;
        return fail("vmaCreateAllocator failed");
    }
    std::fprintf(stderr, "[vulkan] device=\"%s\" api=%u.%u driver=0x%x validation=%d\n", device_name_.c_str(),
                 VK_API_VERSION_MAJOR(properties_.apiVersion), VK_API_VERSION_MINOR(properties_.apiVersion),
                 properties_.driverVersion, layers.empty() ? 0 : 1);
    std::fprintf(stderr, "[vulkan] driver: %s; workarounds: %s\n", describe(identity_).c_str(),
                 describe(quirks_).c_str());
    if (bindless_) {
        std::fprintf(stderr, "[vulkan] textures: bindless, image array from %u up to %u slots, %u sampler slots (%s)\n",
                     kBindlessTextureSlotsMin, texture_array_max_, kBindlessSamplerSlots, indexing_note.c_str());
    } else {
        std::fprintf(stderr, "[vulkan] textures: bound tables of up to %u maps (%s)\n", table_slots_, indexing_note.c_str());
    }
    std::fprintf(stderr,
                 "[vulkan] features: depth-clamp=%d depth-clip=%d bc=%d "
                 "anisotropy=%d display-timing=%d queues=%u temporal-upscaling=%d frame-generation-queues=%d "
                 "synchronization2=%s\n",
                 depth_clamp_, depth_clip_enable_, bc_textures_,
                 sampler_anisotropy_, display_timing_, queue_info.queueCount, temporal_upscaling_ ? 1 : 0,
                 generation_present_queue_ != VK_NULL_HANDLE ? 1 : 0,
                 sync2_core ? "core" : sync2_extension ? "extension" : "off");
    // The memory BufferPlace chooses from: each heap's size (L: device-local), each type's heap and flags (L device
    // local, V host visible, C coherent, $ cached).
    {
        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceMemoryProperties(physical_, &memory);
        std::string text;
        for (std::uint32_t i = 0; i < memory.memoryHeapCount; ++i) {
            const VkMemoryHeap& heap = memory.memoryHeaps[i];
            text += " heap" + std::to_string(i) + '=' + std::to_string(heap.size >> 20u) + "MB" +
                    ((heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0u ? "L" : "");
        }
        for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
            const VkMemoryPropertyFlags flags = memory.memoryTypes[i].propertyFlags;
            text += " type" + std::to_string(i) + "=h" + std::to_string(memory.memoryTypes[i].heapIndex) + ':' +
                    ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0u ? "L" : "") +
                    ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0u ? "V" : "") +
                    ((flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0u ? "C" : "") +
                    ((flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) != 0u ? "$" : "");
        }
        std::fprintf(stderr, "[vulkan] memory:%s\n", text.c_str());
    }
    return true;
}

void Context::shutdown() noexcept {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
#if defined(_WIN32)
    streamline_vk::shutdown();
#endif
    {
        std::lock_guard<std::mutex> lock(deferred_mutex_);
        for (Deferred& deferred : deferred_) {
            deferred.destroy();
        }
        deferred_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(submit_mutex_);
        for (const Pending& pending : pending_) {
            vkDestroyFence(device_, pending.fence, nullptr);
        }
        for (const VkFence fence : free_fences_) {
            vkDestroyFence(device_, fence, nullptr);
        }
        pending_.clear();
        completed_ = submitted_.load(std::memory_order_acquire);
        free_fences_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(address_mutex_);
        for (auto& page : address_pages_) delete page.exchange(nullptr, std::memory_order_relaxed);
        free_address_ids_.clear();
        next_address_id_ = 1u;
    }
    if (allocator_ != nullptr) {
        vmaDestroyAllocator(allocator_);
        allocator_ = nullptr;
    }
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
        pipeline_barrier2_ = nullptr;
    }
    if (messenger_ != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
        messenger_ = VK_NULL_HANDLE;
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
    physical_ = VK_NULL_HANDLE;
    queue_ = VK_NULL_HANDLE;
}

std::uint64_t Context::submit(std::span<const VkCommandBuffer> commands, VkSemaphore wait_semaphore,
                              VkPipelineStageFlags wait_stage, VkSemaphore signal_semaphore) {
    std::lock_guard<std::mutex> lock(submit_mutex_);
    VkFence fence = VK_NULL_HANDLE;
    // A free fence may still be in a wait() that read it before it signalled; reset none while one is.
    if (!free_fences_.empty() && waiters_ == 0) {
        fence = free_fences_.back();
        free_fences_.pop_back();
        check(vkResetFences(device_, 1, &fence), "vkResetFences");
    } else {
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        check(vkCreateFence(device_, &fence_info, nullptr, &fence), "vkCreateFence");
    }
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = static_cast<std::uint32_t>(commands.size());
    submit.pCommandBuffers = commands.data();
    if (wait_semaphore != VK_NULL_HANDLE) {
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &wait_semaphore;
        submit.pWaitDstStageMask = &wait_stage;
    }
    if (signal_semaphore != VK_NULL_HANDLE) {
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &signal_semaphore;
    }
    const std::int64_t cpu_start = thread_cpu_ns();
    const VkResult result = vkQueueSubmit(queue_, 1, &submit, fence);
    note_driver_cpu(DriverCall::Submit, thread_cpu_ns() - cpu_start);
    if (result != VK_SUCCESS) {
        free_fences_.push_back(fence);
        check(result, "vkQueueSubmit");
    }
    const std::uint64_t value = submitted_.load(std::memory_order_relaxed) + 1u;
    pending_.push_back({value, fence});
    submitted_.store(value, std::memory_order_release);
    return value;
}

std::uint64_t Context::completed_value() {
    std::lock_guard<std::mutex> lock(submit_mutex_);
    std::size_t done = 0;
    const std::int64_t cpu_start = thread_cpu_ns();
    while (done < pending_.size() && vkGetFenceStatus(device_, pending_[done].fence) == VK_SUCCESS) {
        completed_ = pending_[done].value;
        free_fences_.push_back(pending_[done].fence);
        ++done;
    }
    note_driver_cpu(DriverCall::Poll, thread_cpu_ns() - cpu_start);
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(done));
    return completed_;
}

bool Context::wait(std::uint64_t value, std::uint64_t timeout_ns) {
    VkFence fence = VK_NULL_HANDLE;
    {
        std::lock_guard<std::mutex> lock(submit_mutex_);
        if (value <= completed_) {
            return true;
        }
        for (const Pending& pending : pending_) {
            if (pending.value >= value) {
                fence = pending.fence;
                break;
            }
        }
        if (fence == VK_NULL_HANDLE) {
            return false;  // never submitted
        }
        ++waiters_;
    }
    const std::int64_t cpu_start = thread_cpu_ns();
    const VkResult result = vkWaitForFences(device_, 1, &fence, VK_TRUE, timeout_ns);
    note_driver_cpu(DriverCall::Wait, thread_cpu_ns() - cpu_start);
    {
        std::lock_guard<std::mutex> lock(submit_mutex_);
        --waiters_;
    }
    if (result != VK_SUCCESS) {
        return false;
    }
    static_cast<void>(completed_value());
    return true;
}

std::uint64_t Context::submit_frame(std::span<const VkCommandBuffer> commands, VkSemaphore wait_semaphore,
                                    VkPipelineStageFlags wait_stage, VkSemaphore signal_semaphore) {
    const std::uint64_t value = submit(commands, wait_semaphore, wait_stage, signal_semaphore);
    std::lock_guard<std::mutex> lock(deferred_mutex_);
    for (Deferred& deferred : deferred_) {
        if (deferred.value == 0u) {
            deferred.value = value;
        }
    }
    return value;
}

void Context::defer(std::function<void()> destroy) {
    // Commands recorded into the open frame may name the object: it waits for that frame's submission (a peek or
    // one-time submission in between carries none of them).
    std::lock_guard<std::mutex> lock(deferred_mutex_);
    deferred_.push_back({0u, std::move(destroy)});
}

void Context::collect() {
    const std::uint64_t completed = completed_value();
    std::vector<Deferred> due;
    {
        std::lock_guard<std::mutex> lock(deferred_mutex_);
        auto split = std::stable_partition(deferred_.begin(), deferred_.end(), [completed](const Deferred& d) {
            return d.value == 0u || d.value > completed;
        });
        due.assign(std::make_move_iterator(split), std::make_move_iterator(deferred_.end()));
        deferred_.erase(split, deferred_.end());
    }
    for (Deferred& deferred : due) {
        deferred.destroy();
    }
}

std::shared_ptr<Texture> Context::create_texture(std::uint32_t width, std::uint32_t height, std::uint32_t levels,
                                                 VkFormat format, VkImageUsageFlags usage, bool render_target,
                                                 const char* name, VkSampleCountFlagBits samples) {
    auto texture = std::make_shared<Texture>();
    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = levels;
    image_info.arrayLayers = 1;
    image_info.samples = samples;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = usage;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VmaAllocationInfo allocated{};
    if (vmaCreateImage(allocator_, &image_info, &alloc_info, &texture->image, &texture->allocation, &allocated) !=
        VK_SUCCESS) {
        return {};
    }
    texture->format = format;
    texture->width = width;
    texture->height = height;
    texture->levels = levels;
    texture->usage = image_info.usage;
    texture->allocation_bytes = allocated.size;
    const bool depth = format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D24_UNORM_S8_UINT ||
        format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D16_UNORM;
    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = texture->image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange = {static_cast<VkImageAspectFlags>(depth ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                                         : VK_IMAGE_ASPECT_COLOR_BIT),
                                  0, levels, 0, 1};
    if ((usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0u &&
        vkCreateImageView(device_, &view_info, nullptr, &texture->view) != VK_SUCCESS) {
        return {};
    }
    if (render_target) {
        view_info.subresourceRange.levelCount = 1;
        if (format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        if (vkCreateImageView(device_, &view_info, nullptr, &texture->target_view) != VK_SUCCESS) {
            return {};
        }
    }
    if (name != nullptr) {
        set_name(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(texture->image), name);
    }
    return texture;
}

bool Context::grey_view(Texture& texture) const {
    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = texture.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8_UNORM;
    view_info.components = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R,
                            VK_COMPONENT_SWIZZLE_R};
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, texture.levels, 0, 1};
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(device_, &view_info, nullptr, &view) != VK_SUCCESS) {
        return false;
    }
    if (texture.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, texture.view, nullptr);
    }
    texture.view = view;
    return true;
}

bool Context::device_memory_budget(std::uint64_t& usage, std::uint64_t& budget) const noexcept {
    usage = budget = 0u;
    if (allocator_ == nullptr) {
        return false;
    }
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physical_, &memory);
    std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> budgets{};
    vmaGetHeapBudgets(allocator_, budgets.data());
    // The largest device-local heap holds the render targets (the only one on most devices).
    for (std::uint32_t i = 0; i < memory.memoryHeapCount; ++i) {
        if ((memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0u && budgets[i].budget > budget) {
            budget = budgets[i].budget;
            usage = budgets[i].usage;
        }
    }
    return budget != 0u;
}

MappedBuffer Context::create_mapped_buffer(std::uint64_t size, VkBufferUsageFlags usage, const char* name,
                                           BufferPlace place) {
    MappedBuffer out;
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    // Mapped memory is coherent: the shared ring code never flushes (every Vulkan device has a host-visible coherent
    // type). With sequential writes, preferring the device picks device memory that is also host-visible where there
    // is some, else host memory.
    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = place == BufferPlace::Host ? VMA_MEMORY_USAGE_AUTO_PREFER_HOST : VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (place != BufferPlace::Device) {
        alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }
    VmaAllocationInfo allocated{};
    if (vmaCreateBuffer(allocator_, &buffer_info, &alloc_info, &out.buffer, &out.allocation, &allocated) != VK_SUCCESS) {
        return {};
    }
    out.mapped = static_cast<std::byte*>(allocated.pMappedData);
    out.size = size;
    if (name != nullptr) {
        set_name(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(out.buffer), name);
    }
    return out;
}

std::shared_ptr<Buffer> Context::create_buffer(std::uint64_t size, VkBufferUsageFlags usage, const char* name,
                                               BufferPlace place) {
    MappedBuffer made = create_mapped_buffer(size, usage, name, place);
    if (made.buffer == VK_NULL_HANDLE) {
        return {};
    }
    auto buffer = std::make_shared<Buffer>();
    buffer->buffer = made.buffer;
    buffer->allocation = made.allocation;
    buffer->mapped = made.mapped;
    buffer->size = made.size;
    buffer->address = register_address(made.buffer);
    if (buffer->address == 0u) {
        return {};  // the reference's release destroys the buffer
    }
    return buffer;
}

std::shared_ptr<Buffer> Context::create_readback_buffer(std::uint64_t size, const char* name, VkBufferUsageFlags usage) {
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    // Cached host memory: the CPU reads every byte, which uncached write-combined memory makes slow. Where cached
    // memory's invalidation costs more (Mali, Adreno), coherent memory, cached as well where the device has such a type.
    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    if (quirks_.coherent_readback) {
        alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        alloc_info.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    }
    auto buffer = std::make_shared<Buffer>();
    VmaAllocationInfo allocated{};
    if (vmaCreateBuffer(allocator_, &buffer_info, &alloc_info, &buffer->buffer, &buffer->allocation, &allocated) !=
        VK_SUCCESS) {
        buffer->buffer = VK_NULL_HANDLE;
        return {};
    }
    buffer->mapped = static_cast<std::byte*>(allocated.pMappedData);
    buffer->size = size;
    if (name != nullptr) {
        set_name(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(buffer->buffer), name);
    }
    return buffer;
}

void Context::invalidate(const Buffer& buffer) const noexcept {
    vmaInvalidateAllocation(allocator_, buffer.allocation, 0, VK_WHOLE_SIZE);
}

std::uint64_t Context::register_address(VkBuffer buffer) {
    std::lock_guard<std::mutex> lock(address_mutex_);
    std::uint32_t id = 0;
    if (!free_address_ids_.empty()) {
        id = free_address_ids_.back();
        free_address_ids_.pop_back();
    } else if (next_address_id_ < kAddressIds) {
        id = next_address_id_++;
    } else {
        throw std::runtime_error("Vulkan buffer address ID representation exhausted");
    }
    auto& entry = address_pages_[id >> kAddressPageBits];
    AddressPage* page = entry.load(std::memory_order_relaxed);
    if (!page) {
        page = new AddressPage{};
        entry.store(page, std::memory_order_release);
    }
    page->slots[id & (kAddressPageSize - 1u)].store(buffer, std::memory_order_release);
    return static_cast<std::uint64_t>(id) << kAddressShift;
}

void Context::release_address(std::uint64_t address) noexcept {
    const std::uint64_t id = address >> kAddressShift;
    if (id == 0u || id >= kAddressIds) {
        return;
    }
    std::lock_guard<std::mutex> lock(address_mutex_);
    AddressPage* page = address_pages_[id >> kAddressPageBits].load(std::memory_order_relaxed);
    if (!page) return;
    page->slots[id & (kAddressPageSize - 1u)].store(VK_NULL_HANDLE, std::memory_order_release);
    free_address_ids_.push_back(static_cast<std::uint32_t>(id));
}

void Context::set_name(VkObjectType type, std::uint64_t handle, const char* name) const noexcept {
    if (!debug_names_ || vkSetDebugUtilsObjectNameEXT == nullptr) {
        return;
    }
    VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name;
    vkSetDebugUtilsObjectNameEXT(device_, &info);
}

}  // namespace galaxy::render::vk

namespace galaxy::render::gpu {

TextureRef texture_ref(TextureObject* texture) {
    return texture != nullptr ? texture->shared_from_this() : TextureRef{};
}

Resource* raw(const BufferRef& buffer) noexcept {
    return buffer != nullptr ? buffer->buffer : VK_NULL_HANDLE;
}

// Room past a buffer's end for a binding's fixed range at the last offset.
constexpr std::size_t kBindingRoom = 4096u;

bool device_removed(Device*) noexcept {
    return false;
}

UploadMemory create_upload_memory(Device*, std::size_t bytes, const char* name) {
    vk::Context& context = vk::Context::get();
    if (!context.ready() || bytes == 0u) {
        return {};
    }
    // One buffer serves any ring: vertices, indices, constants (uniform) and rows (storage). A binding's whole range must
    // lie in its buffer at any offset an allocation can have, hence the room past the end.
    std::shared_ptr<vk::Buffer> buffer = context.create_buffer(
        bytes + kBindingRoom,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        name);
    if (!buffer) {
        return {};
    }
    UploadMemory out;
    out.cpu = buffer->mapped;
    out.gpu = buffer->address;
    out.buffer = std::move(buffer);
    return out;
}

TextureRef create_render_texture(Device*, std::uint32_t width, std::uint32_t height) {
    return vk::Context::get().create_texture(
        width, height, 1u, vk::kSceneColorFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        true, "display picture copy");
}

}  // namespace galaxy::render::gpu
