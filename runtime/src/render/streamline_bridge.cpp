// SPDX-License-Identifier: GPL-3.0-only
// NVIDIA Streamline at run time: DLSS super resolution, DLSS Frame Generation and Reflex (streamline_bridge.h).
#include "galaxy/render/streamline_bridge.h"

#include "galaxy/render/temporal_upscaler.h"
#include "galaxy/render_api.h"
#include "galaxy/platform_paths.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

#if defined(GALAXY_WITH_STREAMLINE)
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_dlss_g.h>
#include <sl_pcl.h>
#include <sl_reflex.h>
#include <sl_security.h>
#include "locked_file.h"
#include "streamline_common.h"
#include "streamline_frame_generation.h"
#endif

namespace galaxy::render {
namespace streamline {

std::wstring default_directory() {
    const std::filesystem::path data = galaxy::nebula_data_directory();
    return data.empty() ? std::wstring{} : (data / L"vendor" / L"streamline-2.14.1").wstring();
}

}  // namespace streamline

#if defined(GALAXY_WITH_STREAMLINE)

namespace {

// The interposer's exports, resolved once (Nebula does not link Streamline).
struct Api {
    PFun_slInit* init{};
    PFun_slShutdown* shutdown{};
    PFun_slIsFeatureSupported* is_feature_supported{};
    PFun_slSetD3DDevice* set_d3d_device{};
    PFun_slUpgradeInterface* upgrade_interface{};
    PFun_slGetNativeInterface* get_native_interface{};
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
    bool dlssg_loaded = false;      // requested at startup and its plugins installed
};
Api g_api;
streamline_common::FrameGeneration g_generation;
std::wstring g_directory;
// Streamline's proxies live until it shuts down: a released device proxy
// clears Streamline's device, and its command queue proxy points at it.
Microsoft::WRL::ComPtr<ID3D12Device> g_device_proxy;
Microsoft::WRL::ComPtr<IDXGIFactory4> g_factory_proxy;

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

using streamline_common::result_name;

}  // namespace

namespace streamline {

bool startup(const std::wstring& directory, bool frame_generation) noexcept {
    if (g_api.initialized) {
        return true;
    }
    if (directory.empty()) {
        return false;
    }
    try {
        g_directory = directory;
        const std::wstring interposer = g_directory + L"\\sl.interposer.dll";
        if (GetFileAttributesW(interposer.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::cerr << "[streamline] not installed\n";
            return false;
        }
        HMODULE module = nullptr;
        {
            // Held from the check to the load: the file checked is the file loaded.
            const LockedFile held(interposer.c_str());
            if (!held.held()) {
                std::cerr << "[streamline] sl.interposer.dll could not be opened: not loaded\n";
                return false;
            }
            // Streamline's own check: an Authenticode signature chaining to a
            // trusted root, by Streamline's signing key.
            if (!sl::security::verifyEmbeddedSignature(interposer.c_str())) {
                std::cerr << "[streamline] sl.interposer.dll is not signed by NVIDIA: not loaded\n";
                return false;
            }
            module = LoadLibraryExW(interposer.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        }
        if (module == nullptr) {
            std::cerr << "[streamline] sl.interposer.dll did not load\n";
            return false;
        }
        Api api{};
        if (!resolve(module, "slInit", api.init) || !resolve(module, "slShutdown", api.shutdown) ||
            !resolve(module, "slIsFeatureSupported", api.is_feature_supported) ||
            !resolve(module, "slSetD3DDevice", api.set_d3d_device) ||
            !resolve(module, "slUpgradeInterface", api.upgrade_interface) ||
            !resolve(module, "slGetNativeInterface", api.get_native_interface) ||
            !resolve(module, "slGetFeatureFunction", api.get_feature_function) ||
            !resolve(module, "slGetNewFrameToken", api.get_new_frame_token) ||
            !resolve(module, "slSetConstants", api.set_constants) ||
            !resolve(module, "slSetTagForFrame", api.set_tag_for_frame) ||
            !resolve(module, "slEvaluateFeature", api.evaluate_feature) ||
            !resolve(module, "slFreeResources", api.free_resources)) {
            FreeLibrary(module);  // nothing was initialised: the interposer is not kept loaded
            return false;
        }
        sl::Preferences preferences{};
        preferences.showConsole = false;
        preferences.logLevel = sl::LogLevel::eDefault;
        static const wchar_t* paths[1]{};
        paths[0] = g_directory.c_str();
        preferences.pathsToPlugins = paths;
        preferences.numPathsToPlugins = 1u;
        preferences.pathToLogsAndData = nullptr;
        preferences.logMessageCallback = &log_message;
        // Manual hooking; no over-the-air updates: the installed, signed
        // plugins are the ones that run.
        preferences.flags = sl::PreferenceFlags::eDisableCLStateTracking |
                            sl::PreferenceFlags::eUseManualHooking |
                            sl::PreferenceFlags::eUseFrameBasedResourceTagging;
        // Frame generation: DLSS-G needs Reflex (PCL loads with the core).
        const auto installed = [&](const wchar_t* name) {
            return GetFileAttributesW((g_directory + L"\\" + name).c_str()) != INVALID_FILE_ATTRIBUTES;
        };
        // Loaded whenever it is installed, whatever the setting, so frame generation switches on and off without
        // a restart. Loading it does not turn it on.
        const bool dlssg = installed(L"sl.dlss_g.dll") && installed(L"nvngx_dlssg.dll") &&
                           installed(L"sl.reflex.dll") && installed(L"sl.pcl.dll");
        if (frame_generation && !dlssg) {
            std::cerr << "[streamline] DLSS Frame Generation is not installed\n";
        }
        static const sl::Feature features[] = {sl::kFeatureDLSS, sl::kFeatureDLSS_G, sl::kFeatureReflex,
                                               sl::kFeaturePCL};
        preferences.featuresToLoad = features;
        preferences.numFeaturesToLoad = dlssg ? 4u : 1u;
        preferences.engine = sl::EngineType::eCustom;
        preferences.engineVersion = "Nebula";
        preferences.projectId = "7c3e2b1a-5d4f-4e8a-9b6c-1f2e3d4c5b6a";
        preferences.renderAPI = sl::RenderAPI::eD3D12;
        const sl::Result result = api.init(preferences, sl::kSDKVersion);
        if (result != sl::Result::eOk) {
            std::cerr << "[streamline] slInit: " << result_name(result) << '\n';
            return false;
        }
        api.initialized = true;
        api.dlssg_loaded = dlssg;
        g_api = api;
        g_generation.is_feature_supported = api.is_feature_supported;
        g_generation.get_feature_function = api.get_feature_function;
        g_generation.get_new_frame_token = api.get_new_frame_token;
        g_generation.set_tag_for_frame = api.set_tag_for_frame;
        return true;
    } catch (...) {
        return false;
    }
}

bool active() noexcept { return g_api.initialized; }

bool attach_device(ID3D12Device* device, const LUID& adapter) noexcept {
    if (!g_api.initialized || device == nullptr) {
        return false;
    }
    sl::Result result = g_api.set_d3d_device(device);
    if (result != sl::Result::eOk) {
        std::cerr << "[streamline] slSetD3DDevice: " << result_name(result) << '\n';
        return false;
    }
    boot_step("streamline: device set");
    LUID luid = adapter;
    sl::AdapterInfo info{};
    info.deviceLUID = reinterpret_cast<std::uint8_t*>(&luid);
    info.deviceLUIDSizeInBytes = sizeof(LUID);
    result = g_api.is_feature_supported(sl::kFeatureDLSS, info);
    if (result != sl::Result::eOk) {
        std::cerr << "[streamline] DLSS unavailable: " << result_name(result) << '\n';
        return false;
    }
    void* optimal = nullptr;
    void* options = nullptr;
    if (g_api.get_feature_function(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", optimal) != sl::Result::eOk ||
        g_api.get_feature_function(sl::kFeatureDLSS, "slDLSSSetOptions", options) != sl::Result::eOk ||
        optimal == nullptr || options == nullptr) {
        std::cerr << "[streamline] DLSS plugin functions missing\n";
        return false;
    }
    g_api.dlss_get_optimal_settings = reinterpret_cast<PFun_slDLSSGetOptimalSettings*>(optimal);
    g_api.dlss_set_options = reinterpret_cast<PFun_slDLSSSetOptions*>(options);
    g_api.dlss = true;
    std::cerr << "[streamline] DLSS ready\n";
    boot_step("streamline: dlss");
    if (g_api.dlssg_loaded) {
        g_generation.attach(info);
        boot_step("streamline: frame generation");
    }
    return true;
}

bool dlss_supported() noexcept { return g_api.initialized && g_api.dlss; }

bool frame_generation_supported() noexcept { return g_api.initialized && g_generation.supported(); }

unsigned frame_generation_max() noexcept { return g_generation.max(); }

bool frame_generation_dynamic() noexcept { return g_generation.dynamic(); }

unsigned frame_index() noexcept { return g_generation.frame_index(); }

void marker(Marker m) noexcept { g_generation.marker(m); }

void reflex_sleep() noexcept { g_generation.sleep(); }

bool tag_frame_generation(const FrameGenerationInputs& in) noexcept {
    if (!frame_generation_supported() || in.list == nullptr || in.depth == nullptr || in.motion == nullptr) {
        return false;
    }
    sl::FrameToken* const token = g_generation.token();
    if (token == nullptr) {
        return false;
    }
    sl::Resource depth(sl::ResourceType::eTex2d, in.depth, in.common ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_DEPTH_WRITE);
    sl::Resource motion(sl::ResourceType::eTex2d, in.motion, in.common ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_RENDER_TARGET);
    sl::Extent render{};
    render.width = in.render_width;
    render.height = in.render_height;
    // The scene's targets are cleared and reused before the present: Streamline copies them now.
    const sl::ResourceTag tags[] = {
        sl::ResourceTag(&depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, &render),
        sl::ResourceTag(&motion, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, &render)};
    const sl::ViewportHandle viewport{0u};
    if (g_api.set_tag_for_frame(*token, viewport, tags, 2u, in.list) != sl::Result::eOk) {
        static bool reported = false;
        if (!reported) {
            reported = true;
            std::cerr << "[dlss-g] tagging failed\n";
        }
        return false;
    }
    g_generation.note_tagged();
    return true;
}

void tag_backbuffer_region(ID3D12GraphicsCommandList* list, unsigned left, unsigned top, unsigned width,
                           unsigned height) noexcept {
    if (!frame_generation_supported() || !g_generation.tagged() || width == 0u || height == 0u) {
        return;
    }
    sl::FrameToken* const token = g_generation.token();
    if (token == nullptr) {
        return;
    }
    sl::Extent region{};
    region.left = left;
    region.top = top;
    region.width = width;
    region.height = height;
    const sl::ResourceTag tag(nullptr, sl::kBufferTypeBackbuffer, sl::ResourceLifecycle{}, &region);
    (void)g_api.set_tag_for_frame(*token, sl::ViewportHandle{0u}, &tag, 1u, list);
}

void tag_hudless(ID3D12GraphicsCommandList* list, ID3D12Resource* hudless, unsigned width,
                 unsigned height) noexcept {
    if (!frame_generation_supported() || !g_generation.tagged()) {
        return;
    }
    sl::FrameToken* const token = g_generation.token();
    if (token == nullptr) {
        return;
    }
    sl::Resource resource(sl::ResourceType::eTex2d, hudless, D3D12_RESOURCE_STATE_RENDER_TARGET);
    sl::Extent extent{};
    extent.width = width;
    extent.height = height;
    const sl::ResourceTag tag(hudless != nullptr ? &resource : nullptr, sl::kBufferTypeHUDLessColor,
                              sl::ResourceLifecycle::eValidUntilPresent, hudless != nullptr ? &extent : nullptr);
    (void)g_api.set_tag_for_frame(*token, sl::ViewportHandle{0u}, &tag, 1u, list);
}

void set_frame_generation(unsigned generated, bool dynamic, float target_fps) noexcept {
    g_generation.set(generated, dynamic, target_fps);
}

unsigned frames_presented() noexcept { return g_generation.presented(); }

Microsoft::WRL::ComPtr<ID3D12Device> queue_device(ID3D12Device* device) noexcept {
    if (!g_api.initialized || device == nullptr) {
        return device;
    }
    if (!g_device_proxy) {
        // The proxy holds its own reference to the device; ours stays as is.
        void* proxy = device;
        if (g_api.upgrade_interface(&proxy) != sl::Result::eOk || proxy == nullptr || proxy == device) {
            return device;
        }
        g_device_proxy.Attach(static_cast<ID3D12Device*>(proxy));
    }
    return g_device_proxy;
}

ID3D12CommandQueue* native_queue(ID3D12CommandQueue* queue) noexcept {
    void* native = nullptr;
    if (g_api.initialized && queue != nullptr && g_api.get_native_interface(queue, &native) == sl::Result::eOk && native != nullptr) {
        return static_cast<ID3D12CommandQueue*>(native);
    }
    return queue;
}

Microsoft::WRL::ComPtr<IDXGIFactory4> swap_chain_factory(IDXGIFactory4* factory) noexcept {
    if (!g_api.initialized || factory == nullptr) {
        return factory;
    }
    if (!g_factory_proxy) {
        void* proxy = factory;
        if (g_api.upgrade_interface(&proxy) != sl::Result::eOk || proxy == nullptr || proxy == factory) {
            return factory;
        }
        g_factory_proxy.Attach(static_cast<IDXGIFactory4*>(proxy));
    }
    return g_factory_proxy;
}

void shutdown() noexcept {
    if (!g_api.initialized) {
        return;
    }
    const sl::Result result = g_api.shutdown();
    if (result != sl::Result::eOk) {
        std::cerr << "[streamline] slShutdown: " << result_name(result) << '\n';
    }
    // The interposer stays loaded: proxies released after this still run its code.
    g_factory_proxy.Reset();
    g_device_proxy.Reset();
    g_api.initialized = false;
    g_api.dlss = false;
    g_generation.shutdown();
}

}  // namespace streamline

namespace {

using streamline_common::dlss_mode;

class DlssUpscaler final : public TemporalUpscaler {
public:
    explicit DlssUpscaler(unsigned quality) : mode_(dlss_mode(quality)) {}

    ~DlssUpscaler() override {
        // The renderer waits for the GPU before it replaces an upscaler.
        if (g_api.dlss && g_api.free_resources != nullptr) {
            (void)g_api.free_resources(sl::kFeatureDLSS, viewport_);
        }
    }

    bool evaluate(const TemporalUpscaleInputs& in) override {
        if (!streamline::dlss_supported() || in.list == nullptr || in.color == nullptr ||
            in.depth == nullptr || in.motion == nullptr || in.output == nullptr) {
            return false;
        }
        // A second evaluation before the present (an interpolated generated frame, a second scene) takes a new frame
        // token: Streamline rejects different common constants under one token and fails the evaluation.
        static std::uint32_t evaluated = 0xFFFFFFFFu;
        if (streamline::frame_index() == evaluated) {
            g_generation.next_frame_index();
        }
        sl::FrameToken* token = nullptr;
        const std::uint32_t index = streamline::frame_index();
        evaluated = index;
        if (g_api.get_new_frame_token(token, &index) != sl::Result::eOk || token == nullptr) {
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
            // The scene target is gamma-encoded RGBA8 without an exposure value.
            options_.colorBuffersHDR = sl::Boolean::eFalse;
            options_.useAutoExposure = sl::Boolean::eTrue;
            if (g_api.dlss_set_options(viewport_, options_) != sl::Result::eOk) {
                options_.outputWidth = 0u;
                return failed("options");
            }
        }
        const D3D12_RESOURCE_STATES target = in.common ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_RENDER_TARGET;
        sl::Resource color(sl::ResourceType::eTex2d, in.color, target);
        sl::Resource depth(sl::ResourceType::eTex2d, in.depth, in.common ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_DEPTH_WRITE);
        sl::Resource motion(sl::ResourceType::eTex2d, in.motion, target);
        sl::Resource output(sl::ResourceType::eTex2d, in.output, target);
        sl::Extent render{};
        render.width = in.render_width;
        render.height = in.render_height;
        sl::Extent upscaled{};
        upscaled.width = in.output_width;
        upscaled.height = in.output_height;
        const sl::ResourceTag tags[] = {
            sl::ResourceTag(&color, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &render),
            sl::ResourceTag(&output, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &upscaled),
            sl::ResourceTag(&depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, &render),
            sl::ResourceTag(&motion, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, &render)};
        if (g_api.set_tag_for_frame(*token, viewport_, tags, 4u, in.list) != sl::Result::eOk) {
            return failed("tags");
        }
        const sl::BaseStructure* inputs[] = {&viewport_};
        const sl::Result result = g_api.evaluate_feature(sl::kFeatureDLSS, *token, inputs, 1u, in.list);
        if (result != sl::Result::eOk) {
            return failed(result_name(result));
        }
        return true;
    }

    unsigned jitter_phase_count(unsigned render_width, unsigned output_width) const override {
        return streamline_common::jitter_phases(render_width, output_width);
    }

    void jitter_offset(unsigned index, unsigned phase_count, float& x, float& y) const override {
        streamline_common::jitter(index, phase_count, x, y);
    }

    const char* name() const noexcept override { return "DLSS"; }
    bool own_barriers() const noexcept override { return true; }

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
};

}  // namespace

std::unique_ptr<TemporalUpscaler> create_dlss_upscaler(unsigned render_width, unsigned render_height,
                                                       unsigned output_width, unsigned output_height,
                                                       unsigned quality) {
    if (!streamline::dlss_supported() || render_width == 0u || render_height == 0u ||
        output_width == 0u || output_height == 0u) {
        return nullptr;
    }
    // A mode evaluates only render widths inside its range (ultra performance accepts one size), and
    // the render scales are whole scene target scales: the requested mode, else the next higher-quality one
    // whose range holds the render width (NGX fails every evaluate outside it).
    unsigned mode = quality == 5u ? 1u : std::min(quality, 4u);  // no ultra quality mode: quality
    if (g_api.dlss_get_optimal_settings != nullptr) {
        for (unsigned candidate = mode + 1u; candidate-- > 0u;) {
            sl::DLSSOptions options{};
            options.mode = dlss_mode(candidate);
            options.outputWidth = output_width;
            options.outputHeight = output_height;
            sl::DLSSOptimalSettings settings{};
            if (g_api.dlss_get_optimal_settings(options, settings) != sl::Result::eOk ||
                render_width < settings.renderWidthMin || render_width > settings.renderWidthMax) {
                continue;
            }
            std::cerr << "[dlss] " << render_width << 'x' << render_height << " -> " << output_width << 'x'
                      << output_height << " mode " << candidate << " of requested " << quality << " (range "
                      << settings.renderWidthMin << 'x' << settings.renderHeightMin << " .. "
                      << settings.renderWidthMax << 'x' << settings.renderHeightMax << ")\n";
            return std::make_unique<DlssUpscaler>(candidate);
        }
        std::cerr << "[dlss] " << render_width << 'x' << render_height << " -> " << output_width << 'x'
                  << output_height << ": outside every mode's range\n";
    }
    return std::make_unique<DlssUpscaler>(mode);
}

unsigned dlss_min_render_width(unsigned quality, unsigned output_width, unsigned output_height) noexcept {
    if (!streamline::dlss_supported() || g_api.dlss_get_optimal_settings == nullptr || output_width == 0u ||
        output_height == 0u) {
        return 0u;
    }
    sl::DLSSOptions options{};
    options.mode = dlss_mode(quality);
    options.outputWidth = output_width;
    options.outputHeight = output_height;
    sl::DLSSOptimalSettings settings{};
    return g_api.dlss_get_optimal_settings(options, settings) == sl::Result::eOk ? settings.renderWidthMin : 0u;
}

#else  // !GALAXY_WITH_STREAMLINE

namespace streamline {
bool startup(const std::wstring&, bool) noexcept { return false; }
#if !defined(GALAXY_STREAMLINE_VK)
bool frame_generation_supported() noexcept { return false; }
unsigned frame_generation_max() noexcept { return 0u; }
unsigned frame_index() noexcept { return 0u; }
void marker(Marker) noexcept {}
void reflex_sleep() noexcept {}
#endif
bool tag_frame_generation(const FrameGenerationInputs&) noexcept { return false; }
void tag_hudless(ID3D12GraphicsCommandList*, ID3D12Resource*, unsigned, unsigned) noexcept {}
void tag_backbuffer_region(ID3D12GraphicsCommandList*, unsigned, unsigned, unsigned, unsigned) noexcept {}
#if !defined(GALAXY_STREAMLINE_VK)
void set_frame_generation(unsigned, bool, float) noexcept {}
bool frame_generation_dynamic() noexcept { return false; }
unsigned frames_presented() noexcept { return 1u; }
#endif
bool active() noexcept { return false; }
bool attach_device(ID3D12Device*, const LUID&) noexcept { return false; }
#if !defined(GALAXY_STREAMLINE_VK)
bool dlss_supported() noexcept { return false; }
#endif
ID3D12CommandQueue* native_queue(ID3D12CommandQueue* queue) noexcept { return queue; }
Microsoft::WRL::ComPtr<ID3D12Device> queue_device(ID3D12Device* device) noexcept { return device; }
Microsoft::WRL::ComPtr<IDXGIFactory4> swap_chain_factory(IDXGIFactory4* factory) noexcept { return factory; }
void shutdown() noexcept {}
}  // namespace streamline

std::unique_ptr<TemporalUpscaler> create_dlss_upscaler(unsigned, unsigned, unsigned, unsigned, unsigned) {
    return nullptr;
}

#if !defined(GALAXY_STREAMLINE_VK)
unsigned dlss_min_render_width(unsigned, unsigned, unsigned) noexcept { return 0u; }
#endif

#endif

}  // namespace galaxy::render
