#include "galaxy/gx/render_config.h"
#include "galaxy/atomic_file.h"
#include "galaxy/platform_paths.h"

#include <algorithm>
#include <bit>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <locale>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <system_error>

#if defined(_WIN32)
#pragma comment(lib, "Shell32.lib")
#endif

namespace galaxy::gx {

namespace {

// Full replacement and snapshots are protected by the same lock.
static RenderConfig s_config{};
static unsigned s_next_start_efb_scale = 1u;
static RenderFeatureCaps s_feature_caps{};
static std::shared_mutex s_lock;
// Last-write time of config.ini when this process last read or wrote it.
static std::uint64_t s_config_write_time = 0u;
// The gpu config.ini named when this process last read or wrote it (next_start_gpu).
static std::array<char, 32> s_file_gpu{};

static std::uint64_t file_write_time(const std::filesystem::path& path) noexcept {
#if defined(_WIN32)
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (path.empty() || !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return 0u;
    }
    return (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32u) |
        data.ftLastWriteTime.dwLowDateTime;
#else
    std::error_code error;
    const auto time = path.empty() ? std::filesystem::file_time_type{} : std::filesystem::last_write_time(path, error);
    return path.empty() || error ? 0u : static_cast<std::uint64_t>(time.time_since_epoch().count());
#endif
}

// ---------------------------------------------------------------------------
// Minimal key=value INI parser
// ---------------------------------------------------------------------------

static void trim(char* s, std::size_t len, char* out, std::size_t out_cap) {
    const char* p = s;
    while (*p == ' ' || *p == '\t') ++p;
    std::size_t n = std::strlen(p);
    while (n > 0 && (p[n-1] == ' ' || p[n-1] == '\t' || p[n-1] == '\r' || p[n-1] == '\n')) --n;
    n = std::min(n, out_cap - 1);
    std::memcpy(out, p, n);
    out[n] = '\0';
    (void)len;
}

static unsigned parse_uint(const char* v, unsigned def) {
    if (v == nullptr || *v == '\0') return def;
    for (const unsigned char* p =
             reinterpret_cast<const unsigned char*>(v); *p != '\0'; ++p) {
        if (std::isdigit(*p) == 0) return def;
    }
    char* end = nullptr;
    errno = 0;
    const unsigned long r = std::strtoul(v, &end, 10);
    return (end != v && *end == '\0' && errno != ERANGE &&
            r <= std::numeric_limits<unsigned>::max())
        ? static_cast<unsigned>(r)
        : def;
}

static unsigned parse_window_dimension(const char* v, unsigned def) {
    const unsigned parsed = parse_uint(v, 0u);
    return parsed >= 1u &&
            parsed <= static_cast<unsigned>(std::numeric_limits<int>::max())
        ? parsed
        : def;
}

static float parse_float(const char* v, float def) {
    std::istringstream input(v);
    input.imbue(std::locale::classic());
    float value = 0.0f;
    if (!(input >> value)) return def;
    input >> std::ws;
    return input.eof() && std::isfinite(value) &&
        (value == 0.0f || (value >= kMinFrameRate && value <= kMaxFrameRate))
        ? value : def;
}

static bool parse_bool(const char* v, bool def) {
    if (std::strcmp(v, "1") == 0 || std::strcmp(v, "true") == 0) return true;
    if (std::strcmp(v, "0") == 0 || std::strcmp(v, "false") == 0) return false;
    return def;
}

// A choice given by index or by name (case-insensitive): names[i] selects i.
template <std::size_t N>
static unsigned parse_choice(const char* v, const char* const (&names)[N], unsigned def) {
    if (v == nullptr || *v == '\0') return def;
    const unsigned index = parse_uint(v, N);
    if (index < N) return index;
    for (std::size_t i = 0; i < N; ++i) {
        if (_stricmp(v, names[i]) == 0) return static_cast<unsigned>(i);
    }
    return def;
}

// config.ini's aspect: a name (kAspectNames), or an index of the list before 3:2.
static unsigned parse_aspect_mode(const char* v, unsigned def) {
    static constexpr unsigned kBefore3To2[] = {0u, 1u, 3u, 4u, 5u, 6u};
    if (v == nullptr || *v == '\0') return def;
    const unsigned index = parse_uint(v, kAspectModeCount);
    if (index < std::size(kBefore3To2)) return kBefore3To2[index];
    for (unsigned i = 0; i < kAspectModeCount; ++i) {
        if (_stricmp(v, kAspectNames[i]) == 0) return i;
    }
    return def;
}

static bool read_env_bool(const char* name, bool fallback) {
    char value[16]{};
    std::size_t length = 0;
    if (getenv_s(&length, value, sizeof(value), name) != 0 || length <= 1u) {
        return fallback;
    }

    char normalized[16]{};
    const std::size_t count =
        std::min<std::size_t>(length - 1u, sizeof(normalized) - 1u);
    for (std::size_t i = 0; i < count; ++i) {
        normalized[i] = static_cast<char>(
            std::tolower(static_cast<unsigned char>(value[i])));
    }
    if (std::strcmp(normalized, "0") == 0 ||
        std::strcmp(normalized, "false") == 0 ||
        std::strcmp(normalized, "no") == 0 ||
        std::strcmp(normalized, "off") == 0) {
        return false;
    }
    if (std::strcmp(normalized, "1") == 0 ||
        std::strcmp(normalized, "true") == 0 ||
        std::strcmp(normalized, "yes") == 0 ||
        std::strcmp(normalized, "on") == 0) {
        return true;
    }
    return fallback;
}

static unsigned read_env_uint_clamped(
    const char* name,
    unsigned fallback,
    unsigned minimum,
    unsigned maximum) {
    char value[32]{};
    std::size_t length = 0;
    if (getenv_s(&length, value, sizeof(value), name) != 0 || length <= 1u) {
        return fallback;
    }

    for (const unsigned char* p =
             reinterpret_cast<const unsigned char*>(value); *p != '\0'; ++p) {
        if (std::isdigit(*p) == 0) return fallback;
    }

    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (end == value || end == nullptr || *end != '\0' || errno == ERANGE) {
        return fallback;
    }
    return static_cast<unsigned>(std::clamp<unsigned long>(
        parsed,
        minimum,
        maximum));
}

static void apply_kv(RenderConfig& cfg, const char* key, const char* val) {
    if (std::strcmp(key, "efb_scale")           == 0) cfg.efb_scale           = std::clamp(parse_uint(val, 1), 1u, kMaxEfbScale);
    else if (std::strcmp(key, "vsync_interval") == 0) cfg.vsync_interval      = std::clamp(parse_uint(val, cfg.vsync_interval), 0u, 2u);
    else if (std::strcmp(key, "wait_for_frame_latency") == 0) cfg.wait_for_frame_latency = parse_bool(val, cfg.wait_for_frame_latency);
    else if (std::strcmp(key, "swap_chain_frame_latency") == 0) cfg.swap_chain_frame_latency = std::clamp(parse_uint(val, cfg.swap_chain_frame_latency), 1u, 2u);
    else if (std::strcmp(key, "allow_tearing") == 0) cfg.allow_tearing = parse_bool(val, cfg.allow_tearing);
    else if (std::strcmp(key, "flip_discard") == 0) cfg.flip_discard = parse_bool(val, cfg.flip_discard);
    else if (std::strcmp(key, "msaa_samples")   == 0) cfg.msaa_samples        = std::clamp(parse_uint(val, 1), 1u, 8u);
    else if (std::strcmp(key, "taa_enabled")    == 0) cfg.taa_enabled         = parse_bool(val, false);
    else if (std::strcmp(key, "max_fps")        == 0) cfg.max_fps             = parse_float(val, cfg.max_fps);
    else if (std::strcmp(key, "frame_rate_decouple") == 0) cfg.frame_rate_decouple = parse_bool(val, cfg.frame_rate_decouple);
    else if (std::strcmp(key, "texture_lod_bias") == 0) cfg.texture_lod_bias = std::clamp(parse_uint(val, 0), 0u, 8u);
    else if (std::strcmp(key, "anisotropic_filtering") == 0) cfg.anisotropic_filtering = std::clamp(parse_uint(val, cfg.anisotropic_filtering), 1u, 16u);
    else if (std::strcmp(key, "enhanced_mipmaps") == 0) cfg.enhanced_mipmaps = parse_bool(val, cfg.enhanced_mipmaps);
    else if (std::strcmp(key, "per_pixel_lighting") == 0) cfg.per_pixel_lighting = parse_bool(val, cfg.per_pixel_lighting);
    else if (std::strcmp(key, "enhanced_lighting") == 0) cfg.enhanced_lighting = parse_bool(val, cfg.enhanced_lighting);
    else if (std::strcmp(key, "ambient_occlusion") == 0) cfg.ambient_occlusion = parse_bool(val, cfg.ambient_occlusion);
    else if (std::strcmp(key, "post_process") == 0) cfg.post_process = parse_bool(val, cfg.post_process);
    else if (std::strcmp(key, "internal_follow_window") == 0) cfg.internal_follow_window = parse_bool(val, cfg.internal_follow_window);
    else if (std::strcmp(key, "gpu_budget") == 0) cfg.gpu_budget = parse_bool(val, cfg.gpu_budget);
    else if (std::strcmp(key, "hud_output_resolution") == 0) cfg.hud_output_resolution = parse_bool(val, cfg.hud_output_resolution);
    else if (std::strcmp(key, "aa") == 0) {
        static constexpr const char* kNames[] = {"off", "smaa", "auto", "fxaa"};
        cfg.aa_mode = parse_choice(val, kNames, cfg.aa_mode);
    } else if (std::strcmp(key, "smaa_quality") == 0) {
        static constexpr const char* kNames[] = {"low", "medium", "high", "ultra"};
        cfg.smaa_quality = parse_choice(val, kNames, cfg.smaa_quality);
    } else if (std::strcmp(key, "upscaler") == 0) {
        static constexpr const char* kNames[] = {"bilinear", "fsr", "fsr3", "dlss"};
        cfg.upscaler = parse_choice(val, kNames, cfg.upscaler);
    } else if (std::strcmp(key, "frame_generation") == 0) {
        cfg.frame_generation = std::min(parse_uint(val, cfg.frame_generation), 5u);
    } else if (std::strcmp(key, "temporal_quality") == 0) {
        static constexpr const char* kNames[] = {
            "native", "quality", "balanced", "performance", "ultra_performance"};
        cfg.temporal_quality = parse_choice(val, kNames, cfg.temporal_quality);
    } else if (std::strcmp(key, "spatial_quality") == 0) {
        static constexpr const char* kNames[] = {
            "manual", "quality", "balanced", "performance", "ultra_performance"};
        cfg.spatial_quality = parse_choice(val, kNames, cfg.spatial_quality);
    } else if (std::strcmp(key, "sharpness") == 0) {
        cfg.sharpness = std::min(parse_uint(val, cfg.sharpness), 100u);
    }
    else if (std::strcmp(key, "ray_tracing_enabled") == 0) cfg.ray_tracing_enabled = parse_bool(val, false);
    else if (std::strcmp(key, "rt_quality") == 0) {
        static constexpr const char* kNames[] = {"medium", "high", "low"};
        cfg.rt_quality = parse_choice(val, kNames, cfg.rt_quality);
    }
    else if (std::strcmp(key, "rt_shadow_quality") == 0) cfg.rt_shadow_quality = std::clamp(parse_uint(val, 0), 0u, 3u);
    else if (std::strcmp(key, "rt_ao_enabled")  == 0) cfg.rt_ao_enabled       = parse_bool(val, false);
    else if (std::strcmp(key, "rt_reflections") == 0) cfg.rt_reflections      = parse_bool(val, false);
    else if (std::strcmp(key, "window_width")   == 0) cfg.window_width        = parse_window_dimension(val, cfg.window_width);
    else if (std::strcmp(key, "window_height")  == 0) cfg.window_height       = parse_window_dimension(val, cfg.window_height);
    else if (std::strcmp(key, "display_mode") == 0) {
        static constexpr const char* kNames[] = {"windowed", "borderless", "fullscreen"};
        cfg.display_mode = parse_choice(val, kNames, cfg.display_mode);
    } else if (std::strcmp(key, "monitor") == 0) {
        cfg.monitor.fill('\0');
        const std::size_t length = std::min(std::strlen(val), cfg.monitor.size() - 1u);
        std::memcpy(cfg.monitor.data(), val, length);
    } else if (std::strcmp(key, "gpu") == 0) {
        cfg.gpu.fill('\0');
        const std::size_t length = std::min(std::strlen(val), cfg.gpu.size() - 1u);
        std::memcpy(cfg.gpu.data(), val, length);
    }
    else if (std::strcmp(key, "fullscreen_width") == 0) cfg.fullscreen_width = std::min(parse_uint(val, 0u), 16384u);
    else if (std::strcmp(key, "fullscreen_height") == 0) cfg.fullscreen_height = std::min(parse_uint(val, 0u), 16384u);
    else if (std::strcmp(key, "fullscreen_refresh_hz") == 0) cfg.fullscreen_refresh_hz = std::min(parse_uint(val, 0u), 1000u);
    else if (std::strcmp(key, "aspect") == 0) {
        cfg.aspect_mode = parse_aspect_mode(val, cfg.aspect_mode);
    }
}

static void parse_ini(FILE* f, RenderConfig& cfg) {
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        if (std::strchr(line, '\n') == nullptr && !std::feof(f)) {
            // A physical overlong record is ignored as a whole. Its suffix
            // must not become another setting on the next fgets call.
            int c;
            do { c = std::fgetc(f); } while (c != '\n' && c != EOF);
            continue;
        }
        char key_buf[128], val_buf[256];
        char* eq = std::strchr(line, '=');
        if (!eq) continue;
        if (static_cast<std::size_t>(eq - line) >= sizeof(key_buf) ||
            std::strlen(eq + 1) >= sizeof(val_buf)) continue;
        *eq = '\0';
        trim(line, static_cast<std::size_t>(eq - line), key_buf, sizeof(key_buf));
        trim(eq + 1, std::strlen(eq + 1), val_buf, sizeof(val_buf));
        if (key_buf[0] == '#' || key_buf[0] == ';' || key_buf[0] == '\0') continue;
        apply_kv(cfg, key_buf, val_buf);
    }
}

static void apply_env(RenderConfig& cfg, const char* env_name, const char* key) {
    char value[128]{};
    std::size_t length = 0;
    if (getenv_s(&length, value, sizeof(value), env_name) == 0 &&
        length > 1) {
        apply_kv(cfg, key, value);
    }
}

static void apply_env_overrides(RenderConfig& cfg) {
    apply_env(cfg, "GALAXY_EFB_SCALE", "efb_scale");
    apply_env(cfg, "GALAXY_VSYNC_INTERVAL", "vsync_interval");
    apply_env(cfg, "GALAXY_WAIT_FOR_FRAME_LATENCY", "wait_for_frame_latency");
    apply_env(cfg, "GALAXY_SWAP_CHAIN_FRAME_LATENCY", "swap_chain_frame_latency");
    apply_env(cfg, "GALAXY_ALLOW_TEARING", "allow_tearing");
    apply_env(cfg, "GALAXY_FLIP_DISCARD", "flip_discard");
    apply_env(cfg, "GALAXY_MSAA_SAMPLES", "msaa_samples");
    apply_env(cfg, "GALAXY_TAA_ENABLED", "taa_enabled");
    apply_env(cfg, "GALAXY_MAX_FPS", "max_fps");
    apply_env(cfg, "GALAXY_FRAME_RATE_DECOUPLE", "frame_rate_decouple");
    apply_env(cfg, "GALAXY_TEXTURE_LOD_BIAS", "texture_lod_bias");
    apply_env(cfg, "GALAXY_ANISOTROPIC_FILTERING", "anisotropic_filtering");
    apply_env(cfg, "GALAXY_ENHANCED_MIPMAPS", "enhanced_mipmaps");
    apply_env(cfg, "GALAXY_PER_PIXEL_LIGHTING", "per_pixel_lighting");
    apply_env(cfg, "GALAXY_ENHANCED_LIGHTING", "enhanced_lighting");
    apply_env(cfg, "GALAXY_AMBIENT_OCCLUSION", "ambient_occlusion");
    apply_env(cfg, "GALAXY_POST_PROCESS", "post_process");
    apply_env(cfg, "GALAXY_INTERNAL_FOLLOW_WINDOW", "internal_follow_window");
    apply_env(cfg, "GALAXY_GPU_BUDGET", "gpu_budget");
    apply_env(cfg, "GALAXY_HUD_OUTPUT_RESOLUTION", "hud_output_resolution");
    apply_env(cfg, "GALAXY_AA", "aa");
    apply_env(cfg, "GALAXY_SMAA_QUALITY", "smaa_quality");
    apply_env(cfg, "GALAXY_UPSCALER", "upscaler");
    apply_env(cfg, "GALAXY_FRAME_GENERATION", "frame_generation");
    apply_env(cfg, "GALAXY_TEMPORAL_QUALITY", "temporal_quality");
    apply_env(cfg, "GALAXY_SPATIAL_QUALITY", "spatial_quality");
    apply_env(cfg, "GALAXY_SHARPNESS", "sharpness");
    apply_env(cfg, "GALAXY_RAY_TRACING", "ray_tracing_enabled");
    apply_env(cfg, "GALAXY_RAY_TRACING_ENABLED", "ray_tracing_enabled");
    apply_env(cfg, "GALAXY_RT_SHADOW_QUALITY", "rt_shadow_quality");
    apply_env(cfg, "GALAXY_RT_QUALITY", "rt_quality");
    apply_env(cfg, "GALAXY_RT_AO_ENABLED", "rt_ao_enabled");
    apply_env(cfg, "GALAXY_RT_REFLECTIONS", "rt_reflections");
    apply_env(cfg, "GALAXY_WINDOW_WIDTH", "window_width");
    apply_env(cfg, "GALAXY_WINDOW_HEIGHT", "window_height");
    // Launch variable of earlier launchers and the test tools:
    // GALAXY_FULLSCREEN selects borderless.
    {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(&length, value, sizeof(value), "GALAXY_FULLSCREEN") == 0 && length > 1u) {
            cfg.display_mode = read_env_bool("GALAXY_FULLSCREEN", false) ? kDisplayBorderless
                                                                        : kDisplayWindowed;
        }
    }
    apply_env(cfg, "GALAXY_DISPLAY_MODE", "display_mode");
    apply_env(cfg, "GALAXY_MONITOR", "monitor");
    apply_env(cfg, "GALAXY_GPU", "gpu");
    apply_env(cfg, "GALAXY_ASPECT", "aspect");
}

static void clamp_unimplemented_features(RenderConfig& cfg) {
    // Keep config/env keys parseable for forward compatibility, but never let
    // the live renderer claim unsupported paths are active.
    cfg.efb_scale = std::clamp(cfg.efb_scale, 1u, kMaxEfbScale);
    cfg.vsync_interval = std::clamp(cfg.vsync_interval, 0u, 2u);
    cfg.swap_chain_frame_latency =
        std::clamp(cfg.swap_chain_frame_latency, 1u, 2u);
    if (!std::isfinite(cfg.max_fps) || (cfg.max_fps != 0.0f && cfg.max_fps < kMinFrameRate) || cfg.max_fps > kMaxFrameRate) {
        cfg.max_fps = RenderConfig{}.max_fps;
    }
    cfg.texture_lod_bias = std::min(cfg.texture_lod_bias, 8u);
    cfg.anisotropic_filtering = std::clamp(cfg.anisotropic_filtering, 1u, 16u);
    cfg.aa_mode = std::min(cfg.aa_mode, 3u);
    cfg.smaa_quality = std::min(cfg.smaa_quality, 3u);
    cfg.upscaler = std::min(cfg.upscaler, 3u);
    cfg.frame_generation = std::min(cfg.frame_generation, 5u);
    cfg.temporal_quality = std::min(cfg.temporal_quality, 4u);
    cfg.spatial_quality = std::min(cfg.spatial_quality, 4u);
    cfg.sharpness = std::min(cfg.sharpness, 100u);
    // Win32 client geometry is signed-int based even though DXGI dimensions
    // are UINT. Keep direct API callers inside the common representable range.
    constexpr unsigned kMaxWindowDimension =
        static_cast<unsigned>(std::numeric_limits<int>::max());
    cfg.window_width = std::clamp(cfg.window_width, 1u, kMaxWindowDimension);
    cfg.window_height = std::clamp(cfg.window_height, 1u, kMaxWindowDimension);
    cfg.display_mode = std::min(cfg.display_mode, kDisplayBorderless);
    cfg.aspect_mode = std::min(cfg.aspect_mode, kAspectModeCount - 1u);
    cfg.monitor.back() = '\0';
    cfg.gpu.back() = '\0';
    cfg.msaa_samples = std::bit_floor(std::clamp(cfg.msaa_samples, 1u, 8u));
    cfg.taa_enabled = false;
    cfg.rt_shadow_quality = 0;
    cfg.rt_quality = std::min(cfg.rt_quality, 2u);
    cfg.rt_ao_enabled = false;
    cfg.rt_reflections = false;
}

static void sanitize_feature_status(RenderFeatureStatus& status) {
    status.reason.back() = '\0';
    if (!status.implemented ||
        !status.runtime_supported ||
        !status.renderer_ready) {
        status.settings_exposed = false;
    }
}

static void clamp_feature_caps(RenderFeatureCaps& caps) {
    caps.adapter_description.back() = '\0';
    if (!caps.dxr_device5_supported || caps.dxr_raytracing_tier == 0u) {
        caps.dxr.runtime_supported = false;
    }
    if (!caps.streamline_bridge_built) {
        caps.streamline.implemented = false;
        caps.streamline.runtime_supported = false;
        caps.streamline.renderer_ready = false;
    }

    sanitize_feature_status(caps.msaa);
    sanitize_feature_status(caps.taa);
    sanitize_feature_status(caps.dxr);
    sanitize_feature_status(caps.streamline);
    sanitize_feature_status(caps.reflex);
    sanitize_feature_status(caps.dlss_super_resolution);
    sanitize_feature_status(caps.dlss_frame_generation);

    if (!caps.streamline.implemented ||
        !caps.streamline.runtime_supported ||
        !caps.streamline.renderer_ready) {
        caps.reflex.settings_exposed = false;
        caps.dlss_super_resolution.settings_exposed = false;
        caps.dlss_frame_generation.settings_exposed = false;
    }
    if (!caps.reflex.implemented ||
        !caps.reflex.runtime_supported ||
        !caps.reflex.renderer_ready) {
        caps.dlss_frame_generation.settings_exposed = false;
    }
}

static bool experimental_frame_rate_decouple_allowed() {
    return read_env_bool("GALAXY_EXPERIMENTAL_FRAME_RATE_DECOUPLE", false);
}

static std::filesystem::path config_path() {
#if defined(_WIN32)
    SetLastError(ERROR_SUCCESS);
    const DWORD required = GetEnvironmentVariableW(L"GALAXY_RENDER_CONFIG_PATH", nullptr, 0);
    if (required != 0u) {
        std::wstring override_path(required, L'\0');
        const DWORD copied = GetEnvironmentVariableW(L"GALAXY_RENDER_CONFIG_PATH",
            override_path.data(), required);
        if (copied == 0u || copied >= required) return {};
        override_path.resize(copied);
        return std::filesystem::path(override_path);
    }
    // An explicitly empty or failed override must never redirect a save into
    // the user's default configuration by accident.
    if (GetLastError() != ERROR_ENVVAR_NOT_FOUND) return {};

#else
    // An explicitly empty override must never redirect a save into the user's default configuration.
    if (const char* override_path = std::getenv("GALAXY_RENDER_CONFIG_PATH"); override_path != nullptr) {
        return override_path[0] != '\0' ? std::filesystem::path(override_path) : std::filesystem::path{};
    }
#endif
    const std::filesystem::path folder = galaxy::nebula_data_directory();
    return folder.empty() ? folder : folder / "config.ini";
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

RenderConfig get_render_config() {
    s_lock.lock_shared();
    const RenderConfig copy = s_config;
    s_lock.unlock_shared();
    return copy;
}

namespace {
std::atomic<bool> s_frame_generation_running{false};
}  // namespace

void set_frame_generation_running(bool running) noexcept {
    s_frame_generation_running.store(running, std::memory_order_relaxed);
}

bool frame_generation_running() noexcept { return s_frame_generation_running.load(std::memory_order_relaxed); }

GxTimingConfig resolve_gx_timing_config_from_environment() {
    GxTimingConfig config{};
    // The upper bound is a test-enabling ceiling, not a policy. The default stays in GxTimingConfig
    // (3, the triple-buffered profile). An operator can select up to 8 through an environment value
    // instead of a rebuild.
    //
    // The render chunk queue is the simulation thread's only hard coupling to the renderer.
    // render_frame() blocks in queue_space_cv_.wait until fewer than render_queue_depth non-present
    // chunks are outstanding. With the ceiling at 3, the renderer's share of simulation stalls
    // could not be tested, because the ceiling allowed only depth 3.
    //
    // Depth 4 is handled and logged, never rejected. Raising the depth changes timing only. Each
    // depth step retains one MemorySnapshot, not a copy of guest memory, because snapshots are
    // recycled through memory_snapshot_pool_. 8 stays bounded on a low-end machine.
    // Revert the ceiling to 3 if a deeper queue raises end-to-end input latency or grows the
    // working set.
    config.render_queue_depth = read_env_uint_clamped(
        "GALAXY_RENDER_QUEUE_DEPTH",
        config.render_queue_depth,
        1u,
        8u);
    config.vi_render_fifo_wait = read_env_bool(
        "GALAXY_VI_RENDER_FIFO_WAIT",
        config.vi_render_fifo_wait);
    config.pe_events_gpu_fence = read_env_bool(
        "GALAXY_PE_EVENTS_GPU_FENCE",
        config.pe_events_gpu_fence);
    config.render_live_memory_wait = read_env_bool(
        "GALAXY_RENDER_LIVE_MEMORY_WAIT",
        config.render_live_memory_wait);
    const bool snapshot_requested = read_env_bool(
        "GALAXY_RENDER_MEMORY_SNAPSHOT",
        !config.render_live_memory_wait);
    config.render_memory_snapshot =
        !config.render_live_memory_wait && snapshot_requested;
    config.render_live_memory_reads =
        !config.render_live_memory_wait && !config.render_memory_snapshot &&
        read_env_bool("GALAXY_RENDER_LIVE_MEMORY", false);
    config.pe_events_at_consumption = read_env_bool(
        "GALAXY_PE_EVENTS_AT_CONSUMPTION",
        config.pe_events_at_consumption);
    return config;
}

const GxTimingConfig& get_gx_timing_config() {
    static const GxTimingConfig config =
        resolve_gx_timing_config_from_environment();
    return config;
}

RenderFeatureStatus make_render_feature_status(
    bool implemented,
    bool runtime_supported,
    bool renderer_ready,
    bool settings_exposed,
    const char* reason) {
    RenderFeatureStatus status{};
    status.implemented = implemented;
    status.runtime_supported = runtime_supported;
    status.renderer_ready = renderer_ready;
    status.settings_exposed = settings_exposed;
    if (reason != nullptr) {
        std::snprintf(
            status.reason.data(),
            status.reason.size(),
            "%s",
            reason);
    }
    return status;
}

void set_render_config(const RenderConfig& cfg) {
    RenderConfig sanitized = cfg;
    clamp_unimplemented_features(sanitized);
    s_lock.lock();
    s_config = sanitized;
    s_lock.unlock();
}

unsigned get_next_start_efb_scale() {
    s_lock.lock_shared();
    const unsigned copy = s_next_start_efb_scale;
    s_lock.unlock_shared();
    return copy;
}

void stage_next_start_efb_scale(unsigned efb_scale) {
    const unsigned sanitized = std::clamp(efb_scale, 1u, kMaxEfbScale);
    s_lock.lock();
    s_next_start_efb_scale = sanitized;
    s_lock.unlock();
}

RenderFeatureCaps get_render_feature_caps() {
    s_lock.lock_shared();
    const RenderFeatureCaps copy = s_feature_caps;
    s_lock.unlock_shared();
    return copy;
}

void set_render_feature_caps(const RenderFeatureCaps& caps) {
    RenderFeatureCaps sanitized = caps;
    clamp_feature_caps(sanitized);
    s_lock.lock();
    s_feature_caps = sanitized;
    s_lock.unlock();
}

void load_render_config_from_file() {
    RenderConfig cfg = get_render_config();
    // A file without the key is the automatic choice.
    cfg.gpu.fill('\0');

    const std::filesystem::path path = config_path();
    const std::uint64_t write_time = file_write_time(path);
    if (!path.empty()) {
        FILE* f = galaxy::open_path(path, "r");
        if (f != nullptr) {
            parse_ini(f, cfg);
            std::fclose(f);
        }
    }
    std::array<char, 32> file_gpu = cfg.gpu;
    file_gpu.back() = '\0';

    apply_env_overrides(cfg);
    if (!experimental_frame_rate_decouple_allowed()) {
        cfg.frame_rate_decouple = false;
    }
    clamp_unimplemented_features(cfg);

    s_lock.lock();
    s_config = cfg;
    s_next_start_efb_scale = cfg.efb_scale;
    s_config_write_time = write_time;
    s_file_gpu = file_gpu;
    s_lock.unlock();
}

bool reload_render_config_if_changed() {
    const std::filesystem::path path = config_path();
    const std::uint64_t write_time = file_write_time(path);
    s_lock.lock_shared();
    const std::uint64_t known = s_config_write_time;
    s_lock.unlock_shared();
    if (write_time == 0u || write_time == known) {
        return false;
    }
    RenderConfig cfg = get_render_config();
    FILE* f = galaxy::open_path(path, "r");
    if (f == nullptr) {
        return false;  // Being replaced; the next check reads it.
    }
    // The device was created on the session's GPU; a new choice is for the next start.
    const std::array<char, 32> session_gpu = cfg.gpu;
    cfg.gpu.fill('\0');
    parse_ini(f, cfg);
    std::fclose(f);
    std::array<char, 32> file_gpu = cfg.gpu;
    file_gpu.back() = '\0';
    apply_env_overrides(cfg);
    cfg.gpu = session_gpu;
    if (!experimental_frame_rate_decouple_allowed()) {
        cfg.frame_rate_decouple = false;
    }
    clamp_unimplemented_features(cfg);

    s_lock.lock();
    s_config = cfg;
    s_next_start_efb_scale = cfg.efb_scale;
    s_config_write_time = write_time;
    s_file_gpu = file_gpu;
    s_lock.unlock();
    return true;
}

bool save_render_config_to_file() {
    s_lock.lock_shared();
    RenderConfig cfg = s_config;
    const unsigned next_start_efb_scale = s_next_start_efb_scale;
    std::array<char, 32> gpu = s_file_gpu;
    s_lock.unlock_shared();
    clamp_unimplemented_features(cfg);
    const std::filesystem::path path = config_path();
    if (path.empty()) {
        return false;
    }
    // The game never chooses the GPU: it writes back the gpu the file names now, which Nebula Settings may
    // have changed since the game last read it (the in-game menu was open, or the change is younger than
    // the next check).
    {
        if (FILE* f = galaxy::open_path(path, "r"); f != nullptr) {
            RenderConfig file{};
            parse_ini(f, file);
            std::fclose(f);
            gpu = file.gpu;
        }
        gpu.back() = '\0';
    }

    std::ostringstream contents;
    contents.imbue(std::locale::classic());
    contents << "efb_scale="
             << std::clamp(next_start_efb_scale, 1u, kMaxEfbScale) << '\n'
             << "vsync_interval=" << cfg.vsync_interval << '\n'
             << "wait_for_frame_latency="
             << (cfg.wait_for_frame_latency ? 1u : 0u) << '\n'
             << "swap_chain_frame_latency="
             << std::clamp(cfg.swap_chain_frame_latency, 1u, 2u) << '\n'
             << "allow_tearing=" << (cfg.allow_tearing ? 1u : 0u) << '\n'
             << "flip_discard=" << (cfg.flip_discard ? 1u : 0u) << '\n'
             << std::setprecision(std::numeric_limits<float>::max_digits10)
             << "max_fps=" << cfg.max_fps << '\n'
             << "texture_lod_bias=" << cfg.texture_lod_bias << '\n'
             << "anisotropic_filtering="
             << std::clamp(cfg.anisotropic_filtering, 1u, 16u) << '\n'
             << "enhanced_mipmaps="
             << (cfg.enhanced_mipmaps ? 1u : 0u) << '\n'
             << "per_pixel_lighting="
             << (cfg.per_pixel_lighting ? 1u : 0u) << '\n'
             << "enhanced_lighting="
             << (cfg.enhanced_lighting ? 1u : 0u) << '\n'
             << "ambient_occlusion="
             << (cfg.ambient_occlusion ? 1u : 0u) << '\n'
             << "aa=" << cfg.aa_mode << '\n'
             << "smaa_quality=" << cfg.smaa_quality << '\n'
             << "msaa_samples=" << cfg.msaa_samples << '\n'
             << "upscaler=" << cfg.upscaler << '\n'
             << "frame_generation=" << cfg.frame_generation << '\n'
             << "temporal_quality=" << cfg.temporal_quality << '\n'
             << "spatial_quality=" << cfg.spatial_quality << '\n'
             << "sharpness=" << cfg.sharpness << '\n'
             << "internal_follow_window="
             << (cfg.internal_follow_window ? 1u : 0u) << '\n'
             << "gpu_budget=" << (cfg.gpu_budget ? 1u : 0u) << '\n'
             << "hud_output_resolution="
             << (cfg.hud_output_resolution ? 1u : 0u) << '\n'
             << "ray_tracing_enabled="
             << (cfg.ray_tracing_enabled ? 1u : 0u) << '\n'
             << "rt_quality=" << (cfg.rt_quality == 1u ? "high" : cfg.rt_quality == 2u ? "low" : "medium") << '\n'
             << "window_width=" << cfg.window_width << '\n'
             << "window_height=" << cfg.window_height << '\n'
             << "display_mode=" << cfg.display_mode << '\n'
             << "monitor=" << cfg.monitor.data() << '\n'
             << "gpu=" << gpu.data() << '\n'
             << "fullscreen_width=" << cfg.fullscreen_width << '\n'
             << "fullscreen_height=" << cfg.fullscreen_height << '\n'
             << "fullscreen_refresh_hz=" << cfg.fullscreen_refresh_hz << '\n'
             << "aspect=" << kAspectNames[std::min(cfg.aspect_mode, kAspectModeCount - 1u)] << '\n';
    if (!contents) {
        return false;
    }
    if (!io::write_text_file_atomically(path, contents.str())) {
        return false;
    }
    // This process's own write is not a change to take back in.
    const std::uint64_t write_time = file_write_time(path);
    s_lock.lock();
    s_config_write_time = write_time;
    s_file_gpu = gpu;
    s_lock.unlock();
    return true;
}

std::string gpu_identity(unsigned vendor_id, unsigned device_id, unsigned subsys_id) {
    char text[32]{};
    std::snprintf(text, sizeof(text), "%04x:%04x:%08x", vendor_id & 0xFFFFu, device_id & 0xFFFFu, subsys_id);
    return text;
}

std::string next_start_gpu() {
    s_lock.lock_shared();
    const std::array<char, 32> gpu = s_file_gpu;
    s_lock.unlock_shared();
    return gpu.data();
}

void record_render_adapter(const wchar_t* description, unsigned vendor_id, unsigned device_id,
                           unsigned subsys_id, bool low_power_preference, bool ray_tracing) noexcept {
    try {
        const std::filesystem::path config = config_path();
        if (config.empty() || description == nullptr) {
            return;
        }
        const std::u8string utf8 = std::filesystem::path(description).u8string();
        std::string name(utf8.begin(), utf8.end());
        name.resize(std::min<std::size_t>(name.size(), 255u));
        for (char& c : name) {
            if (c == '\r' || c == '\n') c = ' ';
        }
        std::ostringstream contents;
        contents.imbue(std::locale::classic());
        contents << "description=" << name << '\n'
                 << "vendor_id=" << vendor_id << '\n'
                 << "device_id=" << device_id << '\n'
                 << "id=" << gpu_identity(vendor_id, device_id, subsys_id) << '\n'
                 << "gpu_preference=" << (low_power_preference ? "low" : "high") << '\n'
                 << "ray_tracing=" << (ray_tracing ? 1 : 0) << '\n';
        static_cast<void>(io::write_text_file_atomically(config.parent_path() / L"adapter.ini", contents.str()));
    } catch (...) {
    }
}

}  // namespace galaxy::gx
