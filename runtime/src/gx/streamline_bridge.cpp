// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/streamline_bridge.h"

#include "galaxy/gx/temporal_upscaler.h"

#include <ShlObj.h>

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
    PWSTR base = nullptr;
    std::wstring directory;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)) && base != nullptr) {
        directory = std::wstring(base) + L"\\Nebula\\vendor\\streamline-2.14.1";
    }
    CoTaskMemFree(base);
    return directory;
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
    bool initialized = false;
    bool dlss = false;
};
Api g_api;
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

}  // namespace

namespace streamline {

bool startup(const std::wstring& directory) noexcept {
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
        static const sl::Feature features[] = {sl::kFeatureDLSS};
        preferences.featuresToLoad = features;
        preferences.numFeaturesToLoad = 1u;
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
        std::cerr << "[streamline] the DLSS plugin's functions are missing\n";
        return false;
    }
    g_api.dlss_get_optimal_settings = reinterpret_cast<PFun_slDLSSGetOptimalSettings*>(optimal);
    g_api.dlss_set_options = reinterpret_cast<PFun_slDLSSSetOptions*>(options);
    g_api.dlss = true;
    std::cerr << "[streamline] DLSS ready\n";
    return true;
}

bool dlss_supported() noexcept { return g_api.initialized && g_api.dlss; }

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
        sl::FrameToken* token = nullptr;
        const std::uint32_t index = frame_++;
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
        static const float jitter_sign = [] {
            char value[8]{};
            std::size_t length = 0u;
            return getenv_s(&length, value, sizeof(value), "GALAXY_DLSS_JITTER_SIGN") == 0 && length > 1u &&
                           value[0] == '-'
                       ? -1.0f : 1.0f;
        }();
        constants.jitterOffset = sl::float2(jitter_sign * in.jitter_x, jitter_sign * in.jitter_y);
        constants.mvecScale = sl::float2(1.0f / static_cast<float>(in.render_width),
                                         1.0f / static_cast<float>(in.render_height));
        constants.cameraPinholeOffset = sl::float2(0.0f, 0.0f);
        constants.cameraPos = sl::float3(0.0f, 0.0f, 0.0f);
        constants.cameraUp = sl::float3(0.0f, 1.0f, 0.0f);
        constants.cameraRight = sl::float3(1.0f, 0.0f, 0.0f);
        constants.cameraFwd = sl::float3(0.0f, 0.0f, 1.0f);
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
    std::uint32_t frame_ = 0u;
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

#else  // !GALAXY_WITH_STREAMLINE

namespace streamline {
bool startup(const std::wstring&) noexcept { return false; }
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

#endif

}  // namespace galaxy::gx
