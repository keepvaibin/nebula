#include "galaxy/gx/render_config.h"
#include "galaxy/atomic_file.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Shlobj.h>

#include <algorithm>
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
#include <sstream>
#include <string>

#pragma comment(lib, "Shell32.lib")

namespace galaxy::gx {

namespace {

// Full replacement and snapshots are protected by the same SRWLOCK.
static RenderConfig s_config{};
static unsigned s_next_start_efb_scale = 1u;
static RenderFeatureCaps s_feature_caps{};
static SRWLOCK      s_lock  = SRWLOCK_INIT;

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
    else if (std::strcmp(key, "ray_tracing_enabled") == 0) cfg.ray_tracing_enabled = parse_bool(val, false);
    else if (std::strcmp(key, "rt_shadow_quality") == 0) cfg.rt_shadow_quality = std::clamp(parse_uint(val, 0), 0u, 3u);
    else if (std::strcmp(key, "rt_ao_enabled")  == 0) cfg.rt_ao_enabled       = parse_bool(val, false);
    else if (std::strcmp(key, "rt_reflections") == 0) cfg.rt_reflections      = parse_bool(val, false);
    else if (std::strcmp(key, "window_width")   == 0) cfg.window_width        = parse_window_dimension(val, cfg.window_width);
    else if (std::strcmp(key, "window_height")  == 0) cfg.window_height       = parse_window_dimension(val, cfg.window_height);
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
    apply_env(cfg, "GALAXY_RAY_TRACING_ENABLED", "ray_tracing_enabled");
    apply_env(cfg, "GALAXY_RT_SHADOW_QUALITY", "rt_shadow_quality");
    apply_env(cfg, "GALAXY_RT_AO_ENABLED", "rt_ao_enabled");
    apply_env(cfg, "GALAXY_RT_REFLECTIONS", "rt_reflections");
    apply_env(cfg, "GALAXY_WINDOW_WIDTH", "window_width");
    apply_env(cfg, "GALAXY_WINDOW_HEIGHT", "window_height");
}

static void clamp_unimplemented_features(RenderConfig& cfg) {
    // Keep config/env keys parseable for forward compatibility, but never let
    // the live renderer claim unsupported paths are active.
    cfg.efb_scale = std::clamp(cfg.efb_scale, 1u, kMaxEfbScale);
    cfg.vsync_interval = std::clamp(cfg.vsync_interval, 0u, 2u);
    cfg.swap_chain_frame_latency =
        std::clamp(cfg.swap_chain_frame_latency, 1u, 2u);
    if (!std::isfinite(cfg.max_fps) ||
        (cfg.max_fps != 0.0f && (cfg.max_fps < kMinFrameRate || cfg.max_fps > kMaxFrameRate))) {
        cfg.max_fps = RenderConfig{}.max_fps;
    }
    cfg.texture_lod_bias = std::min(cfg.texture_lod_bias, 8u);
    cfg.anisotropic_filtering = std::clamp(cfg.anisotropic_filtering, 1u, 16u);
    // Win32 client geometry is signed-int based even though DXGI dimensions
    // are UINT. Keep direct API callers inside the common representable range.
    constexpr unsigned kMaxWindowDimension =
        static_cast<unsigned>(std::numeric_limits<int>::max());
    cfg.window_width = std::clamp(cfg.window_width, 1u, kMaxWindowDimension);
    cfg.window_height = std::clamp(cfg.window_height, 1u, kMaxWindowDimension);
    cfg.msaa_samples = 1;
    cfg.taa_enabled = false;
    cfg.ray_tracing_enabled = false;
    cfg.rt_shadow_quality = 0;
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

    wchar_t app_data[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(
            nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, app_data))) {
        return {};
    }
    return std::filesystem::path(app_data) / L"Nebula" / L"config.ini";
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

RenderConfig get_render_config() {
    AcquireSRWLockShared(&s_lock);
    const RenderConfig copy = s_config;
    ReleaseSRWLockShared(&s_lock);
    return copy;
}

GxTimingConfig resolve_gx_timing_config_from_environment() {
    GxTimingConfig config{};
    // Upper bound is a *test-enabling* ceiling, not a policy. The default stays
    // in GxTimingConfig (3 = the triple-buffered profile); an operator may now
    // select up to 8 so the decoupling experiment is reachable by an
    // environment value instead of a rebuild.
    //
    // Why the ceiling moved. The render chunk queue is the simulation thread's
    // only hard coupling to the renderer: render_frame() blocks in
    // queue_space_cv_.wait until fewer than render_queue_depth non-present
    // chunks are outstanding. Both retained recordings were captured with the
    // ceiling at 3, so "does the simulation's tail come from the renderer
    // falling behind, or from the guest's own work?" could not be tested at
    // all: 3 was the only depth either machine could ever run.
    //   - current-PC recording: 0 [gx-queue-wait] lines. No producer stall
    //     exceeded the 20 ms reporting threshold, so raising depth can only
    //     change timing, not remove a measured stall.
    //   - Arc recording: [gx-queue-wait] wait-us=146250, 230407 and 403307, all
    //     reporting queue-depth=4 against queue-limit=3. There the simulation
    //     demonstrably parks behind the renderer, and depth is the knob.
    // The retained snapshots are compact (the current-PC recording reports
    // snapshot-bytes ~1.2 MB for 38 ranges), and MemorySnapshot objects are
    // recycled through memory_snapshot_pool_, so each additional depth step
    // costs one retained snapshot, not one copy of guest memory. 8 therefore
    // stays bounded on a low-end machine; it is not an unbounded knob.
    //
    // This does not change any default and does not skip work: it only makes an
    // existing, already-accepted contract (queue_depth=4 is handled and logged,
    // never rejected) selectable. Revert the ceiling to 3 if a deeper queue is
    // ever shown to raise end-to-end input latency or to grow the working set.
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
    AcquireSRWLockExclusive(&s_lock);
    s_config = sanitized;
    ReleaseSRWLockExclusive(&s_lock);
}

unsigned get_next_start_efb_scale() {
    AcquireSRWLockShared(&s_lock);
    const unsigned copy = s_next_start_efb_scale;
    ReleaseSRWLockShared(&s_lock);
    return copy;
}

void stage_next_start_efb_scale(unsigned efb_scale) {
    const unsigned sanitized = std::clamp(efb_scale, 1u, kMaxEfbScale);
    AcquireSRWLockExclusive(&s_lock);
    s_next_start_efb_scale = sanitized;
    ReleaseSRWLockExclusive(&s_lock);
}

RenderFeatureCaps get_render_feature_caps() {
    AcquireSRWLockShared(&s_lock);
    const RenderFeatureCaps copy = s_feature_caps;
    ReleaseSRWLockShared(&s_lock);
    return copy;
}

void set_render_feature_caps(const RenderFeatureCaps& caps) {
    RenderFeatureCaps sanitized = caps;
    clamp_feature_caps(sanitized);
    AcquireSRWLockExclusive(&s_lock);
    s_feature_caps = sanitized;
    ReleaseSRWLockExclusive(&s_lock);
}

void load_render_config_from_file() {
    RenderConfig cfg = get_render_config();

    const std::filesystem::path path = config_path();
    if (!path.empty()) {
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"r") == 0 && f != nullptr) {
            parse_ini(f, cfg);
            std::fclose(f);
        }
    }

    apply_env_overrides(cfg);
    if (!experimental_frame_rate_decouple_allowed()) {
        cfg.frame_rate_decouple = false;
    }
    clamp_unimplemented_features(cfg);

    AcquireSRWLockExclusive(&s_lock);
    s_config = cfg;
    s_next_start_efb_scale = cfg.efb_scale;
    ReleaseSRWLockExclusive(&s_lock);
}

bool save_render_config_to_file() {
    AcquireSRWLockShared(&s_lock);
    RenderConfig cfg = s_config;
    const unsigned next_start_efb_scale = s_next_start_efb_scale;
    ReleaseSRWLockShared(&s_lock);
    clamp_unimplemented_features(cfg);
    const std::filesystem::path path = config_path();
    if (path.empty()) {
        return false;
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
             << "window_width=" << cfg.window_width << '\n'
             << "window_height=" << cfg.window_height << '\n';
    if (!contents) {
        return false;
    }
    return io::write_text_file_atomically(path, contents.str());
}

}  // namespace galaxy::gx
