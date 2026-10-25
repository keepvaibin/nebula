// SPDX-License-Identifier: GPL-3.0-only
// NVIDIA Streamline at run time: DLSS super resolution, DLSS Frame Generation and Reflex (streamline_bridge.h).
#include "galaxy/gx/streamline_bridge.h"

#include "galaxy/gx/temporal_upscaler.h"
#include "galaxy/gx_d3d12.h"
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
#endif

namespace galaxy::gx {
namespace streamline {

std::wstring default_directory() {
    wchar_t value[MAX_PATH]{};
    std::size_t length = 0u;
    if (_wgetenv_s(&length, value, MAX_PATH, L"GALAXY_STREAMLINE_DIR") == 0 && length > 1u) {
        return value;
    }
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
    PFun_slGetFeatureFunction* get_feature_function{};
    PFun_slGetNewFrameToken* get_new_frame_token{};
    PFun_slSetConstants* set_constants{};
    PFun_slSetTagForFrame* set_tag_for_frame{};
    PFun_slEvaluateFeature* evaluate_feature{};
    PFun_slFreeResources* free_resources{};
    PFun_slDLSSGetOptimalSettings* dlss_get_optimal_settings{};
    PFun_slDLSSSetOptions* dlss_set_options{};
    PFun_slDLSSGSetOptions* dlssg_set_options{};
    PFun_slDLSSGGetState* dlssg_get_state{};
    PFun_slReflexSetOptions* reflex_set_options{};
    PFun_slReflexSleep* reflex_sleep{};
    PFun_slPCLSetMarker* pcl_set_marker{};
    bool initialized = false;
    bool dlss = false;
    bool dlssg_loaded = false;      // requested at startup and its plugins installed
    bool dlssg = false;             // runs on the adapter
    bool markers = false;           // PCL latency markers
};
Api g_api;
// The presented frame's index (Streamline frame tokens) and frame generation's
// applied setting and last reported state.
std::uint32_t g_frame_index = 0u;
unsigned g_dlssg_max = 0u;
bool g_dlssg_dynamic = false;
float g_dlssg_applied_target = -1.0f;
unsigned g_dlssg_applied = ~0u;
unsigned g_dlssg_presented = 1u;
unsigned g_dlssg_status = 0u;
bool g_dlssg_tagged = false;
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

const char* result_name(sl::Result result) {
    switch (result) {
    case sl::Result::eOk: return "ok";
    case sl::Result::eErrorOSOutOfDate: return "Windows is too old";
    case sl::Result::eErrorDriverOutOfDate: return "the NVIDIA driver is too old";
    case sl::Result::eErrorNoSupportedAdapterFound: return "no supported NVIDIA adapter";
    case sl::Result::eErrorAdapterNotSupported: return "the adapter is not supported";
    case sl::Result::eErrorFeatureMissing: return "the DLSS plugin is missing";
    case sl::Result::eErrorFeatureNotSupported: return "DLSS is not supported";
    default: return "error";
    }
}

// Frame generation on the attached adapter: its functions, Reflex low latency
// (DLSS-G requires it), the most frames it generates.
void attach_frame_generation(const sl::AdapterInfo& info) {
    sl::Result result = g_api.is_feature_supported(sl::kFeatureDLSS_G, info);
    if (result != sl::Result::eOk) {
        std::cerr << "[streamline] DLSS Frame Generation unavailable: " << result_name(result) << '\n';
        return;
    }
    void* set_options = nullptr;
    void* get_state = nullptr;
    void* reflex_options = nullptr;
    void* reflex_sleep = nullptr;
    void* pcl_marker = nullptr;
    if (g_api.get_feature_function(sl::kFeatureDLSS_G, "slDLSSGSetOptions", set_options) != sl::Result::eOk ||
        g_api.get_feature_function(sl::kFeatureDLSS_G, "slDLSSGGetState", get_state) != sl::Result::eOk ||
        g_api.get_feature_function(sl::kFeatureReflex, "slReflexSetOptions", reflex_options) != sl::Result::eOk ||
        g_api.get_feature_function(sl::kFeatureReflex, "slReflexSleep", reflex_sleep) != sl::Result::eOk ||
        g_api.get_feature_function(sl::kFeaturePCL, "slPCLSetMarker", pcl_marker) != sl::Result::eOk ||
        set_options == nullptr || get_state == nullptr || reflex_options == nullptr || reflex_sleep == nullptr ||
        pcl_marker == nullptr) {
        std::cerr << "[streamline] DLSS Frame Generation plugin functions missing\n";
        return;
    }
    g_api.dlssg_set_options = reinterpret_cast<PFun_slDLSSGSetOptions*>(set_options);
    g_api.dlssg_get_state = reinterpret_cast<PFun_slDLSSGGetState*>(get_state);
    g_api.reflex_set_options = reinterpret_cast<PFun_slReflexSetOptions*>(reflex_options);
    g_api.reflex_sleep = reinterpret_cast<PFun_slReflexSleep*>(reflex_sleep);
    g_api.pcl_set_marker = reinterpret_cast<PFun_slPCLSetMarker*>(pcl_marker);
    sl::ReflexOptions reflex{};
    reflex.mode = sl::ReflexMode::eLowLatency;
    result = g_api.reflex_set_options(reflex);
    if (result != sl::Result::eOk) {
        std::cerr << "[streamline] Reflex: " << result_name(result) << '\n';
        return;
    }
    sl::DLSSGState state{};
    if (g_api.dlssg_get_state(sl::ViewportHandle{0u}, state, nullptr) == sl::Result::eOk) {
        g_dlssg_max = std::max(state.numFramesToGenerateMax, 1u);
        g_dlssg_dynamic = state.bIsDynamicMFGSupported == sl::Boolean::eTrue;
    } else {
        g_dlssg_max = 1u;
    }
    g_api.markers = true;
    g_api.dlssg = true;
    std::cerr << "[streamline] DLSS Frame Generation ready (up to " << g_dlssg_max + 1u << "x"
              << (g_dlssg_dynamic ? ", dynamic" : "") << ")\n";
}

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
        // Streamline's own check: an Authenticode signature chaining to a
        // trusted root, by Streamline's signing key.
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
            !resolve(module, "slSetD3DDevice", api.set_d3d_device) ||
            !resolve(module, "slUpgradeInterface", api.upgrade_interface) ||
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
        attach_frame_generation(info);
        boot_step("streamline: frame generation");
    }
    return true;
}

bool dlss_supported() noexcept { return g_api.initialized && g_api.dlss; }

bool frame_generation_supported() noexcept { return g_api.initialized && g_api.dlssg; }

unsigned frame_generation_max() noexcept { return frame_generation_supported() ? g_dlssg_max : 0u; }

bool frame_generation_dynamic() noexcept { return frame_generation_supported() && g_dlssg_dynamic; }

unsigned frame_index() noexcept { return g_frame_index; }

void marker(Marker m) noexcept {
    if (!g_api.initialized || !g_api.markers) {
        if (m == Marker::kPresentEnd) {
            ++g_frame_index;
        }
        return;
    }
    sl::FrameToken* token = nullptr;
    const std::uint32_t index = g_frame_index;
    if (g_api.get_new_frame_token(token, &index) == sl::Result::eOk && token != nullptr) {
        (void)g_api.pcl_set_marker(static_cast<sl::PCLMarker>(static_cast<unsigned>(m)), *token);
    }
    if (m == Marker::kPresentEnd) {
        ++g_frame_index;
        g_dlssg_tagged = false;
    }
}

void reflex_sleep() noexcept {
    if (!frame_generation_supported() || g_api.reflex_sleep == nullptr) {
        return;
    }
    sl::FrameToken* token = nullptr;
    const std::uint32_t index = g_frame_index;
    if (g_api.get_new_frame_token(token, &index) == sl::Result::eOk && token != nullptr) {
        (void)g_api.reflex_sleep(*token);
    }
}

bool tag_frame_generation(const FrameGenerationInputs& in) noexcept {
    if (!frame_generation_supported() || in.list == nullptr || in.depth == nullptr || in.motion == nullptr) {
        return false;
    }
    sl::FrameToken* token = nullptr;
    const std::uint32_t index = g_frame_index;
    if (g_api.get_new_frame_token(token, &index) != sl::Result::eOk || token == nullptr) {
        return false;
    }
    sl::Resource depth(sl::ResourceType::eTex2d, in.depth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    sl::Resource motion(sl::ResourceType::eTex2d, in.motion, D3D12_RESOURCE_STATE_RENDER_TARGET);
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
    g_dlssg_tagged = true;
    return true;
}

void tag_backbuffer_region(ID3D12GraphicsCommandList* list, unsigned left, unsigned top, unsigned width,
                           unsigned height) noexcept {
    if (!frame_generation_supported() || !g_dlssg_tagged) {
        return;
    }
    sl::FrameToken* token = nullptr;
    const std::uint32_t index = g_frame_index;
    if (g_api.get_new_frame_token(token, &index) != sl::Result::eOk || token == nullptr) {
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
    if (!frame_generation_supported() || !g_dlssg_tagged) {
        return;
    }
    sl::FrameToken* token = nullptr;
    const std::uint32_t index = g_frame_index;
    if (g_api.get_new_frame_token(token, &index) != sl::Result::eOk || token == nullptr) {
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
    if (!frame_generation_supported()) {
        return;
    }
    // Only frames whose scene was tagged are generated from.
    dynamic = dynamic && g_dlssg_dynamic && generated != 0u;
    const unsigned wanted = g_dlssg_tagged ? std::min(generated, g_dlssg_max) : 0u;
    const float target = dynamic && wanted != 0u ? target_fps : -1.0f;
    if (wanted != g_dlssg_applied || target != g_dlssg_applied_target) {
        g_dlssg_applied_target = target;
        sl::DLSSGOptions options{};
        options.mode = wanted == 0u ? sl::DLSSGMode::eOff : dynamic ? sl::DLSSGMode::eDynamic : sl::DLSSGMode::eOn;
        options.numFramesToGenerate = std::max(wanted, 1u);
        options.dynamicTargetFrameRate = dynamic ? target_fps : 0.0f;
        // Off between scenes (menus, loading) keeps its resources: no stutter coming back.
        options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
        const sl::Result result = g_api.dlssg_set_options(sl::ViewportHandle{0u}, options);
        if (result != sl::Result::eOk) {
            std::cerr << "[dlss-g] options failed: " << result_name(result) << '\n';
        } else if (g_dlssg_applied == ~0u || (g_dlssg_applied == 0u) != (wanted == 0u)) {
            std::cerr << "[dlss-g] " << (wanted != 0u ? "on, generated frames " : "off")
                      << (wanted != 0u ? std::to_string(wanted) : std::string()) << '\n';
        }
        g_dlssg_applied = wanted;
    }
    sl::DLSSGState state{};
    if (g_dlssg_applied != 0u && g_api.dlssg_get_state(sl::ViewportHandle{0u}, state, nullptr) == sl::Result::eOk) {
        g_dlssg_presented = std::max(state.numFramesActuallyPresented, 1u);
        const unsigned status = static_cast<unsigned>(state.status);
        static std::uint32_t reported_index = 0u;
        if (status != g_dlssg_status || g_frame_index - reported_index >= 600u) {
            reported_index = g_frame_index;
            std::cerr << "[dlss-g] frame " << g_frame_index << " status " << status << " presented "
                      << state.numFramesActuallyPresented << " max " << state.numFramesToGenerateMax
                      << " min-size " << state.minWidthOrHeight << '\n';
            g_dlssg_status = status;
        }
    } else if (g_dlssg_applied == 0u) {
        g_dlssg_presented = 1u;
    }
}

unsigned frames_presented() noexcept { return g_dlssg_presented; }

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
    g_api.dlssg = false;
    g_api.markers = false;
}

}  // namespace streamline

namespace {

sl::DLSSMode dlss_mode(unsigned quality) {
    switch (quality) {
    case 0: return sl::DLSSMode::eDLAA;
    case 1: return sl::DLSSMode::eMaxQuality;
    case 2: return sl::DLSSMode::eBalanced;
    case 3: return sl::DLSSMode::eMaxPerformance;
    default: return sl::DLSSMode::eUltraPerformance;
    }
}

float halton(unsigned index, unsigned base) {
    float result = 0.0f;
    float fraction = 1.0f / static_cast<float>(base);
    while (index > 0u) {
        result += fraction * static_cast<float>(index % base);
        index /= base;
        fraction /= static_cast<float>(base);
    }
    return result;
}

void identity(sl::float4x4& m) {
    for (unsigned r = 0; r < 4u; ++r) {
        m[r] = sl::float4(r == 0u ? 1.0f : 0.0f, r == 1u ? 1.0f : 0.0f, r == 2u ? 1.0f : 0.0f,
                          r == 3u ? 1.0f : 0.0f);
    }
}

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
        // A second evaluation before the present (an interpolated sub-frame, a second scene) takes a new frame
        // token: Streamline rejects different common constants under one token and fails the evaluation.
        static std::uint32_t evaluated = 0xFFFFFFFFu;
        if (streamline::frame_index() == evaluated) {
            ++g_frame_index;
        }
        sl::FrameToken* token = nullptr;
        const std::uint32_t index = streamline::frame_index();
        evaluated = index;
        if (g_api.get_new_frame_token(token, &index) != sl::Result::eOk || token == nullptr) {
            return failed("frame token");
        }
        sl::Constants constants{};
        // Motion vectors are dense (camera motion included): the matrices
        // only describe the projection.
        const float aspect = static_cast<float>(in.output_width) / static_cast<float>(std::max(in.output_height, 1u));
        const float y_scale = 1.0f / std::tan(0.5f * in.fov_y);
        identity(constants.cameraViewToClip);
        constants.cameraViewToClip[0].x = y_scale / aspect;
        constants.cameraViewToClip[1].y = y_scale;
        identity(constants.clipToCameraView);
        constants.clipToCameraView[0].x = aspect / y_scale;
        constants.clipToCameraView[1].y = 1.0f / y_scale;
        identity(constants.clipToLensClip);
        identity(constants.clipToPrevClip);
        identity(constants.prevClipToClip);
        constants.jitterOffset = sl::float2(in.jitter_x, in.jitter_y);
        constants.mvecScale = sl::float2(1.0f / static_cast<float>(in.render_width),
                                         1.0f / static_cast<float>(in.render_height));
        constants.cameraPinholeOffset = sl::float2(0.0f, 0.0f);
        constants.cameraPos = sl::float3(0.0f, 0.0f, 0.0f);
        constants.cameraUp = sl::float3(0.0f, 1.0f, 0.0f);
        constants.cameraRight = sl::float3(1.0f, 0.0f, 0.0f);
        constants.cameraFwd = sl::float3(0.0f, 0.0f, 1.0f);
        if (in.view_known) {
            // The view's rows are the camera's right, up and back in world space (orthonormal for
            // the game's cameras); its position is -R^T t. Frame generation's scene change
            // detection reads them.
            const auto& v = in.view;
            constants.cameraRight = sl::float3(v[0], v[1], v[2]);
            constants.cameraUp = sl::float3(v[4], v[5], v[6]);
            constants.cameraFwd = sl::float3(-v[8], -v[9], -v[10]);
            constants.cameraPos = sl::float3(-(v[0] * v[3] + v[4] * v[7] + v[8] * v[11]),
                                             -(v[1] * v[3] + v[5] * v[7] + v[9] * v[11]),
                                             -(v[2] * v[3] + v[6] * v[7] + v[10] * v[11]));
        }
        constants.cameraNear = in.camera_near;
        constants.cameraFar = in.camera_far;
        constants.cameraFOV = in.fov_y;
        constants.cameraAspectRatio = aspect;
        constants.depthInverted = sl::Boolean::eTrue;
        constants.cameraMotionIncluded = sl::Boolean::eTrue;
        constants.motionVectors3D = sl::Boolean::eFalse;
        constants.reset = in.reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
        constants.orthographicProjection = sl::Boolean::eFalse;
        constants.motionVectorsDilated = sl::Boolean::eFalse;
        constants.motionVectorsJittered = sl::Boolean::eFalse;
        if (g_api.set_constants(constants, *token, viewport_) != sl::Result::eOk) {
            return failed("constants");
        }
        if (options_.outputWidth != in.output_width || options_.outputHeight != in.output_height) {
            options_.mode = mode_;
            options_.outputWidth = in.output_width;
            options_.outputHeight = in.output_height;
            // The EFB is gamma-encoded RGBA8 without an exposure value.
            options_.colorBuffersHDR = sl::Boolean::eFalse;
            options_.useAutoExposure = sl::Boolean::eTrue;
            if (g_api.dlss_set_options(viewport_, options_) != sl::Result::eOk) {
                options_.outputWidth = 0u;
                return failed("options");
            }
        }
        sl::Resource color(sl::ResourceType::eTex2d, in.color, D3D12_RESOURCE_STATE_RENDER_TARGET);
        sl::Resource depth(sl::ResourceType::eTex2d, in.depth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        sl::Resource motion(sl::ResourceType::eTex2d, in.motion, D3D12_RESOURCE_STATE_RENDER_TARGET);
        sl::Resource output(sl::ResourceType::eTex2d, in.output, D3D12_RESOURCE_STATE_RENDER_TARGET);
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

    // DLSS programming guide: at least 8 phases times the area ratio.
    unsigned jitter_phase_count(unsigned render_width, unsigned output_width) const override {
        const float ratio = static_cast<float>(output_width) / static_cast<float>(std::max(render_width, 1u));
        return std::max(8u, static_cast<unsigned>(std::ceil(8.0f * ratio * ratio)));
    }

    void jitter_offset(unsigned index, unsigned phase_count, float& x, float& y) const override {
        const unsigned i = phase_count != 0u ? index % phase_count + 1u : index + 1u;
        x = halton(i, 2u) - 0.5f;
        y = halton(i, 3u) - 0.5f;
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
    // the render scales are whole EFB scales: the requested mode, else the next higher-quality one
    // whose range holds the render width (NGX fails every evaluate outside it).
    unsigned mode = std::min(quality, 4u);
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
bool frame_generation_supported() noexcept { return false; }
unsigned frame_generation_max() noexcept { return 0u; }
unsigned frame_index() noexcept { return 0u; }
void marker(Marker) noexcept {}
void reflex_sleep() noexcept {}
bool tag_frame_generation(const FrameGenerationInputs&) noexcept { return false; }
void tag_hudless(ID3D12GraphicsCommandList*, ID3D12Resource*, unsigned, unsigned) noexcept {}
void tag_backbuffer_region(ID3D12GraphicsCommandList*, unsigned, unsigned, unsigned, unsigned) noexcept {}
void set_frame_generation(unsigned, bool, float) noexcept {}
bool frame_generation_dynamic() noexcept { return false; }
unsigned frames_presented() noexcept { return 1u; }
bool active() noexcept { return false; }
bool attach_device(ID3D12Device*, const LUID&) noexcept { return false; }
bool dlss_supported() noexcept { return false; }
Microsoft::WRL::ComPtr<ID3D12Device> queue_device(ID3D12Device* device) noexcept { return device; }
Microsoft::WRL::ComPtr<IDXGIFactory4> swap_chain_factory(IDXGIFactory4* factory) noexcept { return factory; }
void shutdown() noexcept {}
}  // namespace streamline

std::unique_ptr<TemporalUpscaler> create_dlss_upscaler(unsigned, unsigned, unsigned, unsigned, unsigned) {
    return nullptr;
}

unsigned dlss_min_render_width(unsigned, unsigned, unsigned) noexcept { return 0u; }

#endif

}  // namespace galaxy::gx
