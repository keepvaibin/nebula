// The rendering settings: config.ini loading and saving, the GALAXY_<KEY> overrides, and the data folders beside
// config.ini.
#include "galaxy/gx/render_config.h"
#include "galaxy/atomic_file.h"
#include "galaxy/platform_paths.h"

#include <algorithm>
#include <bit>
#include <atomic>
#include <chrono>
#include <cstdint>
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

// Guarded by s_lock.
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

static void trim(char* s, char* out, std::size_t out_cap) {
    const char* p = s;
    while (*p == ' ' || *p == '\t') ++p;
    std::size_t n = std::strlen(p);
    while (n > 0 && (p[n-1] == ' ' || p[n-1] == '\t' || p[n-1] == '\r' || p[n-1] == '\n')) --n;
    n = std::min(n, out_cap - 1);
    std::memcpy(out, p, n);
    out[n] = '\0';
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
    else if (std::strcmp(key, "curved_surfaces") == 0) cfg.curved_surfaces = parse_bool(val, cfg.curved_surfaces);
    else if (std::strcmp(key, "enhanced_lighting") == 0) cfg.enhanced_lighting = parse_bool(val, cfg.enhanced_lighting);
    else if (std::strcmp(key, "ambient_occlusion") == 0) cfg.ambient_occlusion = parse_bool(val, cfg.ambient_occlusion);
    else if (std::strcmp(key, "bloom_mode") == 0) cfg.bloom_mode = std::min(parse_uint(val, cfg.bloom_mode), kBloomEnhanced);
    else if (std::strcmp(key, "bloom") == 0) cfg.bloom = std::min(parse_uint(val, cfg.bloom), kMaxBloom);
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
    else if (std::strcmp(key, "rt_lens_bloom") == 0) cfg.rt_lens_bloom = parse_bool(val, cfg.rt_lens_bloom);
    else if (std::strcmp(key, "rt_sun_rays") == 0) cfg.rt_sun_rays = parse_bool(val, cfg.rt_sun_rays);
    else if (std::strcmp(key, "rt_water") == 0) cfg.rt_water = parse_bool(val, cfg.rt_water);
    else if (std::strcmp(key, "rt_shadow_quality") == 0) cfg.rt_shadow_quality = std::clamp(parse_uint(val, 0), 0u, 3u);
    else if (std::strcmp(key, "rt_ao_enabled")  == 0) cfg.rt_ao_enabled       = parse_bool(val, false);
    else if (std::strcmp(key, "rt_reflections") == 0) cfg.rt_reflections      = parse_bool(val, cfg.rt_reflections);
    else if (std::strcmp(key, "texture_packs") == 0) cfg.texture_packs = parse_bool(val, cfg.texture_packs);
    else if (std::strcmp(key, "texture_pack_preload") == 0) cfg.texture_pack_preload = parse_bool(val, cfg.texture_pack_preload);
    else if (std::strcmp(key, "texture_dump") == 0) cfg.texture_dump = parse_bool(val, cfg.texture_dump);
    else if (std::strcmp(key, "texture_pack_budget_mb") == 0) cfg.texture_pack_budget_mb = std::clamp(parse_uint(val, cfg.texture_pack_budget_mb), 64u, 65536u);
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
            // An overlong line is skipped whole, so its tail does not read as another setting.
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
        trim(line, key_buf, sizeof(key_buf));
        trim(eq + 1, val_buf, sizeof(val_buf));
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
    apply_env(cfg, "GALAXY_CURVED_SURFACES", "curved_surfaces");
    apply_env(cfg, "GALAXY_ENHANCED_LIGHTING", "enhanced_lighting");
    apply_env(cfg, "GALAXY_AMBIENT_OCCLUSION", "ambient_occlusion");
    apply_env(cfg, "GALAXY_BLOOM_MODE", "bloom_mode");
    apply_env(cfg, "GALAXY_BLOOM", "bloom");
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
    apply_env(cfg, "GALAXY_RT_LENS_BLOOM", "rt_lens_bloom");
    apply_env(cfg, "GALAXY_RT_SUN_RAYS", "rt_sun_rays");
    apply_env(cfg, "GALAXY_RT_WATER", "rt_water");
    apply_env(cfg, "GALAXY_RT_AO_ENABLED", "rt_ao_enabled");
    apply_env(cfg, "GALAXY_RT_REFLECTIONS", "rt_reflections");
    apply_env(cfg, "GALAXY_TEXTURE_PACKS", "texture_packs");
    apply_env(cfg, "GALAXY_TEXTURE_PACK_PRELOAD", "texture_pack_preload");
    apply_env(cfg, "GALAXY_TEXTURE_DUMP", "texture_dump");
    apply_env(cfg, "GALAXY_TEXTURE_PACK_BUDGET_MB", "texture_pack_budget_mb");
    apply_env(cfg, "GALAXY_WINDOW_WIDTH", "window_width");
    apply_env(cfg, "GALAXY_WINDOW_HEIGHT", "window_height");
    apply_env(cfg, "GALAXY_DISPLAY_MODE", "display_mode");
    apply_env(cfg, "GALAXY_MONITOR", "monitor");
    apply_env(cfg, "GALAXY_GPU", "gpu");
    apply_env(cfg, "GALAXY_ASPECT", "aspect");
}

static void clamp_unimplemented_features(RenderConfig& cfg) {
    // Clamps every value into its range; settings the renderer does not implement stay parseable but off.
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
    // Win32 client geometry is int while DXGI's is UINT: keep sizes representable in both.
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
    // An empty or unreadable override never falls back to the default config.ini.
    if (GetLastError() != ERROR_ENVVAR_NOT_FOUND) return {};

#else
    // An empty override never falls back to the default config.ini.
    if (const char* override_path = std::getenv("GALAXY_RENDER_CONFIG_PATH"); override_path != nullptr) {
        return override_path[0] != '\0' ? std::filesystem::path(override_path) : std::filesystem::path{};
    }
#endif
    const std::filesystem::path folder = galaxy::nebula_data_directory();
    return folder.empty() ? folder : folder / "config.ini";
}

}  // namespace

RenderConfig get_render_config() {
    s_lock.lock_shared();
    const RenderConfig copy = s_config;
    s_lock.unlock_shared();
    return copy;
}

namespace {
std::atomic<bool> s_frame_generation_running{false};
// The game's latest frame generation report and when it came (steady clock, ms; 0 none yet).
std::atomic<bool> s_frame_generation_allowed{false};
std::atomic<std::int64_t> s_frame_generation_report_ms{0};
constexpr std::int64_t kFrameGenerationReportMs = 100;

std::int64_t steady_ms() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace

void set_frame_generation_running(bool running) noexcept {
    s_frame_generation_running.store(running, std::memory_order_relaxed);
}

bool frame_generation_running() noexcept { return s_frame_generation_running.load(std::memory_order_relaxed); }

namespace {
std::atomic<float> s_camera_water{-1.0f};
std::atomic<std::int64_t> s_camera_water_ms{0};
}  // namespace

void note_camera_water(float depth) noexcept {
    s_camera_water.store(depth, std::memory_order_relaxed);
    s_camera_water_ms.store(steady_ms(), std::memory_order_relaxed);
}

float camera_water_depth() noexcept {
    const std::int64_t at = s_camera_water_ms.load(std::memory_order_relaxed);
    return at != 0 && steady_ms() - at < kFrameGenerationReportMs ? s_camera_water.load(std::memory_order_relaxed)
                                                                   : -1.0f;
}

void note_frame_generation_allowed(bool allowed) noexcept {
    s_frame_generation_allowed.store(allowed, std::memory_order_relaxed);
    s_frame_generation_report_ms.store(steady_ms(), std::memory_order_relaxed);
}

bool frame_generation_allowed() noexcept {
    const std::int64_t at = s_frame_generation_report_ms.load(std::memory_order_relaxed);
    return s_frame_generation_allowed.load(std::memory_order_relaxed) && at != 0 &&
           steady_ms() - at < kFrameGenerationReportMs;
}

GxTimingConfig resolve_gx_timing_config_from_environment() {
    return GxTimingConfig{};
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

namespace {

std::atomic<std::uint64_t> s_rt_effect_frame{0};

// GALAXY_TEST_RT_EFFECT_AB's effect; -1 none.
int rt_effect_ab() {
    static const int effect = [] {
        char value[16]{};
        std::size_t length = 0u;
        if (getenv_s(&length, value, sizeof(value), "GALAXY_TEST_RT_EFFECT_AB") != 0 || length <= 1u) return -1;
        static constexpr const char* kNames[] = {"reflections", "water", "sun_rays", "lens_bloom"};
        for (int i = 0; i < 4; ++i) {
            if (std::strcmp(value, kNames[i]) == 0) return i;
        }
        return -1;
    }();
    return effect;
}

}  // namespace

bool rt_effect_on(const RenderConfig& cfg, RtEffect effect) noexcept {
    const bool configured = effect == RtEffect::Reflections ? cfg.rt_reflections
                            : effect == RtEffect::Water     ? cfg.rt_water
                            : effect == RtEffect::SunRays   ? cfg.rt_sun_rays
                                                            : cfg.rt_lens_bloom;
    return configured && !(rt_effect_ab() == static_cast<int>(effect) &&
                           (s_rt_effect_frame.load(std::memory_order_relaxed) & 1u) != 0u);
}

void set_rt_effect_frame(std::uint64_t frame) noexcept {
    s_rt_effect_frame.store(frame, std::memory_order_relaxed);
}

namespace {
std::atomic<bool> s_curved_surfaces_offered{false};
}  // namespace

void set_curved_surfaces_offered(bool offered) noexcept {
    s_curved_surfaces_offered.store(offered, std::memory_order_relaxed);
}

bool curved_surfaces_offered() noexcept {
    return s_curved_surfaces_offered.load(std::memory_order_relaxed);
}

void set_render_config(const RenderConfig& cfg) {
    RenderConfig sanitized = cfg;
    clamp_unimplemented_features(sanitized);
    s_lock.lock();
    s_config = sanitized;
    s_lock.unlock();
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
    // Frame-rate decoupling is not offered: its key is read but stays off.
    cfg.frame_rate_decouple = false;
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
    cfg.frame_rate_decouple = false;
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
    // The game never chooses the GPU: it writes back the gpu the file names now, which Nebula Settings may have
    // changed since the game last read it.
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
             << "curved_surfaces="
             << (cfg.curved_surfaces ? 1u : 0u) << '\n'
             << "per_pixel_lighting="
             << (cfg.per_pixel_lighting ? 1u : 0u) << '\n'
             << "enhanced_lighting="
             << (cfg.enhanced_lighting ? 1u : 0u) << '\n'
             << "ambient_occlusion="
             << (cfg.ambient_occlusion ? 1u : 0u) << '\n'
             << "bloom_mode=" << std::min(cfg.bloom_mode, kBloomEnhanced) << '\n'
             << "bloom=" << std::min(cfg.bloom, kMaxBloom) << '\n'
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
             << "texture_packs=" << (cfg.texture_packs ? 1u : 0u) << '\n'
             << "texture_pack_preload=" << (cfg.texture_pack_preload ? 1u : 0u) << '\n'
             << "texture_dump=" << (cfg.texture_dump ? 1u : 0u) << '\n'
             << "texture_pack_budget_mb=" << cfg.texture_pack_budget_mb << '\n'
             << "rt_quality=" << (cfg.rt_quality == 1u ? "high" : cfg.rt_quality == 2u ? "low" : "medium") << '\n'
             << "rt_reflections=" << (cfg.rt_reflections ? 1u : 0u) << '\n'
             << "rt_lens_bloom=" << (cfg.rt_lens_bloom ? 1u : 0u) << '\n'
             << "rt_sun_rays=" << (cfg.rt_sun_rays ? 1u : 0u) << '\n'
             << "rt_water=" << (cfg.rt_water ? 1u : 0u) << '\n'
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

namespace {

// A folder beside config.ini, or the environment's override (an empty override: none).
std::filesystem::path data_folder(const char* override_name, const char* folder) {
#if defined(_WIN32)
    const std::wstring name(override_name, override_name + std::strlen(override_name));
    SetLastError(ERROR_SUCCESS);
    const DWORD required = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
    if (required != 0u) {
        std::wstring value(required, L'\0');
        const DWORD copied = GetEnvironmentVariableW(name.c_str(), value.data(), required);
        if (copied == 0u || copied >= required) return {};
        value.resize(copied);
        return std::filesystem::path(value);
    }
    if (GetLastError() != ERROR_ENVVAR_NOT_FOUND) return {};
#else
    if (const char* value = std::getenv(override_name); value != nullptr) {
        return value[0] != '\0' ? std::filesystem::path(value) : std::filesystem::path{};
    }
#endif
    const std::filesystem::path config = config_path();
    return config.empty() ? std::filesystem::path{} : config.parent_path() / folder;
}

std::mutex s_mod_folders_mutex;
std::vector<std::filesystem::path> s_mod_folders;
std::atomic<std::uint64_t> s_mod_folders_generation{0};
std::vector<std::filesystem::path> s_movie_folders;
std::atomic<std::uint64_t> s_movie_folders_generation{0};

}  // namespace

std::filesystem::path texture_pack_directory() {
    return data_folder("GALAXY_TEXTURE_PACK_DIR", "TexturePacks");
}

std::filesystem::path texture_pack_test_directory() {
    // Only the environment's: texture packs are mods (mods.h); tests name a pack folder directly.
#if defined(_WIN32)
    static const bool named = GetEnvironmentVariableW(L"GALAXY_TEXTURE_PACK_DIR", nullptr, 0) != 0u;
#else
    static const bool named = std::getenv("GALAXY_TEXTURE_PACK_DIR") != nullptr;
#endif
    static const std::filesystem::path folder = named ? texture_pack_directory() : std::filesystem::path{};
    return folder;
}

std::filesystem::path texture_dump_directory() {
    return data_folder("GALAXY_TEXTURE_DUMP_DIR", "TextureDumps");
}

void set_texture_pack_mod_folders(std::vector<std::filesystem::path> folders) {
    const std::lock_guard<std::mutex> lock(s_mod_folders_mutex);
    s_mod_folders = std::move(folders);
    s_mod_folders_generation.fetch_add(1u, std::memory_order_release);
}

std::vector<std::filesystem::path> texture_pack_mod_folders() {
    const std::lock_guard<std::mutex> lock(s_mod_folders_mutex);
    return s_mod_folders;
}

std::uint64_t texture_pack_mod_folders_generation() noexcept {
    return s_mod_folders_generation.load(std::memory_order_acquire);
}

std::filesystem::path movie_directory() {
    static const std::filesystem::path folder = data_folder("GALAXY_MOVIE_DIR", "Movies");
    return folder;
}

void set_movie_mod_folders(std::vector<std::filesystem::path> folders) {
    const std::lock_guard<std::mutex> lock(s_mod_folders_mutex);
    s_movie_folders = std::move(folders);
    s_movie_folders_generation.fetch_add(1u, std::memory_order_release);
}

std::vector<std::filesystem::path> movie_mod_folders() {
    const std::lock_guard<std::mutex> lock(s_mod_folders_mutex);
    return s_movie_folders;
}

std::uint64_t movie_mod_folders_generation() noexcept {
    return s_movie_folders_generation.load(std::memory_order_acquire);
}

}  // namespace galaxy::gx
