#pragma once

// RenderConfig: user-visible rendering settings. Set before galaxy::gx::initialize().
// Mutate via set_render_config() at runtime; changes are polled on begin_frame through a
// lock-protected snapshot.
//
// efb_scale is applied live: the render thread re-creates the EFB targets at the next frame
// boundary when the configured value differs from the live one. Window dimensions change
// through the window itself (resize, or the overlay's output presets). Other implemented
// settings are also applied live. Fields reserved for future renderer work are clamped off
// until the renderer has the required native D3D12 path and capability checks.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace galaxy::gx {

// Physical EFB/XFB backing scale. The guest's logical EFB/VI dimensions and
// addresses remain unchanged. 6x reaches a 3840-pixel internal EFB width,
// 12x covers 32:9 at 2160 lines and 16x (10240x8448) stays well inside the
// D3D12 16384-texel limit. Higher scales are opt-in; the default remains 1x.
inline constexpr unsigned kMaxEfbScale = 16u;
inline constexpr float kMinFrameRate = 1.0f;
inline constexpr float kMaxFrameRate = 60.0f;

struct RenderConfig {
    // EFB resolution multiplier (1-kMaxEfbScale).  efb_scale=2 -> 1280x1056 and acts as
    // SSAA for the native D3D12 path. Default 1 = native Wii internal
    // resolution, the lightest GPU path with no added supersampling.
    unsigned efb_scale           = 1;

    // DXGI Present sync interval: 0=off, 1=vblank-synced, 2=every other vblank. The native
    // Release path defaults to a 60 Hz host cap with tearing disabled. Sync interval 1 can
    // back-pressure the command stream through DWM and lower the frame rate on some systems.
    unsigned vsync_interval      = 0;

    // Startup-only D3D12 presentation controls. They are persisted so direct
    // runtime launches, the GUI launcher, and diagnostics share the same
    // smooth Release path. Changes require swap-chain recreation.
    bool     wait_for_frame_latency = false;
    unsigned swap_chain_frame_latency = 2;
    bool     allow_tearing = false;
    bool     flip_discard = true;

    // Reserved for future native AA work. The live renderer currently clamps
    // this to 1 because its EFB/depth targets and PSOs are single-sample.
    unsigned msaa_samples        = 1;

    // Reserved for future native TAA work. Clamped false until motion vectors,
    // jitter, and history resources exist.
    bool     taa_enabled         = false;

    // Maximum frame rate (Hz), 1-60; the settings offer 30 and 60.
    // Enforced by the render thread
    // before Present() when vsync_interval is 0 and realtime VI pacing is not
    // active. Normal Release gameplay uses the native VI scheduler as the
    // authoritative 60 Hz clock so the render thread does not sleep while
    // holding back queued GX work.
    float    max_fps             = 60.0f;

    // When true the game-engine GX FIFO is submitted at its own 60Hz cadence.
    // This remains opt-in until render interpolation and stable frame memory
    // snapshots are complete; the default preserves one present per game frame.
    bool     frame_rate_decouple = false;

    // Mip LOD bias in units of 0.5 LOD levels (0=full detail, 2=quarter).
    // Applied to all sampler descriptors.
    unsigned texture_lod_bias    = 0;

    // D3D12 anisotropic filtering for mipmapped GX samplers. 1 disables it;
    // 2/4/8/16 map directly to D3D12_SAMPLER_DESC::MaxAnisotropy.
    unsigned anisotropic_filtering = 1;

    // Generate native mip chains for color textures that the game samples with
    // linear filtering but without authored GX mips. Authored mips are still
    // decoded from guest memory unchanged.
    bool     enhanced_mipmaps    = false;

    // Enhancement: lit GX color channels are evaluated per pixel instead of
    // per vertex (same XF light formulas on the interpolated eye-space
    // position and normal). Applies to specialized triangle pipelines.
    bool     per_pixel_lighting  = false;

    // Presentation image quality (post_process.h), applied to the presented
    // frame on the render thread. aa_mode: 0 off, 1 SMAA, 2 auto (SMAA unless
    // the game image already has 1.5x the window's pixels on both axes),
    // 3 FXAA.
    // smaa_quality: 0 low .. 3 ultra. upscaler: 0 bilinear, 1 FidelityFX
    // FSR 1 when the game image is smaller than the window, 2 FidelityFX FSR 3
    // (temporal: jittered 3D scene, upscaled before the 2D layer; the scene
    // renders at the window's scale divided by the temporal_quality ratio:
    // 0 native AA 1.0, 1 quality 1.5, 2 balanced 1.7, 3 performance 2.0,
    // 4 ultra performance 3.0). sharpness: RCAS strength 0-100, 0 skips it.
    // Internal resolution follows the window: after a resize settles (not
    // during an edge drag), efb_scale becomes the smallest scale that covers
    // the window's game picture, as the launcher's "Match Output" does at
    // start. The renderer re-creates its targets once, at a frame boundary.
    bool     internal_follow_window = true;
    // The 2D layer (HUD, menus, text) renders at the window's resolution over
    // the resampled 3D scene when the internal resolution is lower. Off by
    // default: the scene then takes two resamples (to the integer output
    // scale, then to the window) instead of one.
    bool     hud_output_resolution = false;

    // false presents with the plain bilinear blit (A/B and fallback).
    bool     post_process        = true;
    unsigned aa_mode             = 0;
    unsigned smaa_quality        = 2;
    unsigned upscaler            = 0;
    unsigned temporal_quality    = 1;
    unsigned sharpness           = 0;

    // Reserved for future DXR work. Clamped off until there is a real DXR
    // capability query, acceleration-structure path, ray PSO, and visible
    // native effect.
    bool     ray_tracing_enabled = false;
    unsigned rt_shadow_quality   = 0;   // 0=off, 1=low, 2=medium, 3=high
    bool     rt_ao_enabled       = false;
    bool     rt_reflections      = false;

    // Back-buffer dimensions (may differ from EFB; the present blit scales).
    // In windowed mode this is the client size, limited to the monitor's
    // work area.
    unsigned window_width        = 854;
    unsigned window_height       = 480;

    // Display, reconciled by the render thread at a frame boundary.
    // display_mode: 0 windowed, 1 borderless fullscreen. Exclusive
    // fullscreen (2) is not offered; a saved 2 opens borderless.
    unsigned display_mode        = 1;
    // GDI device name of the monitor (UTF-8, e.g. \\.\DISPLAY2); empty or
    // unknown selects the primary monitor.
    std::array<char, 64> monitor{};
    unsigned fullscreen_width    = 0;
    unsigned fullscreen_height   = 0;
    unsigned fullscreen_refresh_hz = 0;
    // Game picture aspect: 0 follows the window's shape, then 4:3, 16:10,
    // 16:9, 21:9 and 32:9. Applied at the next video frame.
    unsigned aspect_mode         = 0;
};

// Frame-rate choices: 30 and the game's own 60.
inline std::vector<unsigned> frame_rate_options() {
    return {30u, 60u};
}

// Frames per second the game picture is presented at. The game renders 60;
// lower rates present the newest frame on their own cadence, higher rates
// add interpolated frames between game frames. Unlimited (0) is the display
// rate (at least 60). VSync limits the rate to the display's.
inline double present_frame_rate(float max_fps, unsigned vsync_interval, double refresh_hz) {
    double rate = max_fps > 0.0f ? static_cast<double>(max_fps)
                                 : (refresh_hz > 1.0 ? std::max(refresh_hz, 60.0) : 60.0);
    if (vsync_interval != 0u && refresh_hz > 1.0) rate = std::min(rate, refresh_hz);
    return rate;
}

// Interpolated frames presented per game frame at a present rate:
// round(rate / 60) - 1, none at 60 and below, at most 7.
inline unsigned interpolated_frames_per_game_frame(double rate) {
    if (!(rate > 61.0)) return 0u;
    const double frames = std::floor(rate / 60.0 + 0.5) - 1.0;
    return static_cast<unsigned>(std::clamp(frames, 0.0, 7.0));
}

inline constexpr unsigned kDisplayWindowed = 0u;
inline constexpr unsigned kDisplayBorderless = 1u;
inline constexpr unsigned kDisplayExclusive = 2u;
inline constexpr unsigned kAspectModeCount = 6u;
// Width:height of each aspect_mode (0 = follow the window).
inline constexpr unsigned kAspectRatios[kAspectModeCount][2] = {
    {0u, 0u}, {4u, 3u}, {16u, 10u}, {16u, 9u}, {21u, 9u}, {32u, 9u}};

// Window sizes of exactly an aspect_mode's shape that fit max_width x
// max_height, smallest first; none for aspect_mode 0. The launcher's
// GraphicsSettings.AspectWindowSizes lists the same sizes.
inline std::vector<std::pair<unsigned, unsigned>> aspect_window_sizes(
    unsigned aspect_mode, unsigned max_width, unsigned max_height) {
    static constexpr unsigned kHeights[] = {
        360u, 480u, 540u, 600u, 720u, 768u, 800u, 900u, 960u, 1050u,
        1080u, 1200u, 1440u, 1536u, 1600u, 1800u, 2160u, 2400u, 2880u, 4320u};
    std::vector<std::pair<unsigned, unsigned>> sizes;
    if (aspect_mode == 0u || aspect_mode >= kAspectModeCount) return sizes;
    const unsigned ratio_w = kAspectRatios[aspect_mode][0];
    const unsigned ratio_h = kAspectRatios[aspect_mode][1];
    for (const unsigned height : kHeights) {
        if (height * ratio_w % ratio_h != 0u) continue;
        const unsigned width = height * ratio_w / ratio_h;
        if (width <= max_width && height <= max_height) sizes.emplace_back(width, height);
    }
    return sizes;
}

// Startup-only ordering contract shared by runtime diagnostics and the GX backend.
// Keep these values in one resolved object: independent environment readers
// can otherwise make the native runtime log/act on a different policy than
// the render thread. The defaults are the Release timing profile; explicit
// environment values remain available for controlled A/B diagnostics.
struct GxTimingConfig {
    unsigned render_queue_depth = 3;
    bool vi_render_fifo_wait = false;
    bool pe_events_gpu_fence = true;
    bool render_live_memory_wait = true;
    bool render_memory_snapshot = false;
    // GALAXY_RENDER_LIVE_MEMORY=1 with neither the live-read barrier nor the
    // snapshot: the render thread reads guest RAM directly while the
    // simulation runs ahead (the hardware's GP model, Dolphin's dual-core
    // model). PE completion stays on real GPU fences, so the game's own
    // GXDrawDone discipline bounds how far ahead it writes.
    bool render_live_memory_reads = false;
    // With pe_events_gpu_fence, deliver a capture's PE finish/token events as
    // soon as its command list is submitted (every guest-memory read of the
    // capture, including texture decodes, is complete by then) instead of at
    // the GPU fence. Nothing the GPU produces is read back by the CPU through
    // guest memory (EFB copies stay GPU resources; peeks use asynchronous
    // fields), so the GPU's execution time does not serialize the game's
    // GXDrawDone. GALAXY_PE_EVENTS_AT_CONSUMPTION=0 restores fence delivery.
    bool pe_events_at_consumption = true;
};

struct RenderFeatureStatus {
    bool implemented = false;
    bool runtime_supported = false;
    bool renderer_ready = false;
    bool settings_exposed = false;
    std::array<char, 96> reason{};
};

struct RenderFeatureCaps {
    bool d3d12_device_created = false;
    unsigned d3d_feature_level = 0;
    std::uint32_t adapter_vendor_id = 0;
    std::uint32_t adapter_device_id = 0;
    std::uint32_t adapter_subsys_id = 0;
    std::uint32_t adapter_revision = 0;
    std::uint32_t adapter_luid_low = 0;
    std::uint32_t adapter_luid_high = 0;
    std::array<char, 128> adapter_description{};
    bool dxr_device5_supported = false;
    unsigned dxr_raytracing_tier = 0;
    bool streamline_bridge_built = false;
    RenderFeatureStatus msaa;
    RenderFeatureStatus taa;
    RenderFeatureStatus dxr;
    RenderFeatureStatus streamline;
    RenderFeatureStatus reflex;
    RenderFeatureStatus dlss_super_resolution;
    RenderFeatureStatus dlss_frame_generation;
};

[[nodiscard]] RenderFeatureStatus make_render_feature_status(
    bool implemented,
    bool runtime_supported,
    bool renderer_ready,
    bool settings_exposed,
    const char* reason);

// Thread-safe accessors. The stored value is copied while holding the shared
// lock so callers never observe a partial update.
[[nodiscard]] RenderConfig get_render_config();
void set_render_config(const RenderConfig& cfg);

// The persisted EFB scale for the next process start. The overlay sets it
// together with the live value so config.ini and the running renderer agree.
[[nodiscard]] unsigned get_next_start_efb_scale();
void stage_next_start_efb_scale(unsigned efb_scale);
[[nodiscard]] RenderFeatureCaps get_render_feature_caps();
void set_render_feature_caps(const RenderFeatureCaps& caps);

// resolve_* is intentionally uncached for deterministic configuration tests.
// Runtime code must consume get_gx_timing_config(), which resolves the process
// environment once and returns that same immutable object to every subsystem.
[[nodiscard]] GxTimingConfig resolve_gx_timing_config_from_environment();
[[nodiscard]] const GxTimingConfig& get_gx_timing_config();

// Reads %LOCALAPPDATA%\Nebula\config.ini if present and applies the
// parsed values; non-existent keys keep their defaults.  Called once before
// initialize() from native_runtime.cpp.
void load_render_config_from_file();

// Persists implemented, user-visible settings to the same config.ini. Future
// feature placeholders are intentionally omitted until their renderer paths
// are real and exposed.
bool save_render_config_to_file();

} // namespace galaxy::gx
