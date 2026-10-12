#pragma once

// RenderConfig: user-visible rendering settings.  Set before
// galaxy::gx::initialize(); mutate via set_render_config() at runtime
// (changes are polled on begin_frame through a lock-protected snapshot).
//
// efb_scale is applied live: the render thread re-creates the EFB targets at
// the next frame boundary when the configured value differs from the live
// one. Window dimensions change through the window itself (resize, or the
// overlay's output presets). The remaining implemented settings are live-hot. Fields reserved for future
// renderer work are clamped off until the renderer has the required native
// D3D12 path and capability checks.

#include <array>
#include <cstdint>

namespace galaxy::gx {

// Physical EFB/XFB backing scale. The guest's logical EFB/VI dimensions and
// addresses remain unchanged. 6x reaches a 3840-pixel internal EFB width,
// 12x covers 32:9 at 2160 lines and 16x (10240x8448) stays well inside the
// D3D12 16384-texel limit. Higher scales are opt-in; the default remains 1x.
inline constexpr unsigned kMaxEfbScale = 16u;
inline constexpr float kMinFrameRate = 1.0f;
inline constexpr float kMaxFrameRate = 1000.0f;

struct RenderConfig {
    // EFB resolution multiplier (1-kMaxEfbScale).  efb_scale=2 -> 1280x1056 and acts as
    // SSAA for the native D3D12 path. Default 1 = native Wii internal
    // resolution, the lightest GPU path with no added supersampling.
    unsigned efb_scale           = 1;

    // DXGI Present sync interval: 0=off, 1=vblank-synced, 2=every other
    // vblank.  The native Release path defaults to a 60 Hz host cap with
    // tearing disabled; sync-interval 1 can back-pressure the command stream
    // through DWM and produced the observed ~24 FPS cadence on some systems.
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

    // Maximum frame rate (Hz); 0 = unlimited, otherwise finite 1-1000 Hz.
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
    unsigned anisotropic_filtering = 8;

    // Generate native mip chains for color textures that the game samples with
    // linear filtering but without authored GX mips. Authored mips are still
    // decoded from guest memory unchanged.
    bool     enhanced_mipmaps    = true;

    // Enhancement: lit GX color channels are evaluated per pixel instead of
    // per vertex (same XF light formulas on the interpolated eye-space
    // position and normal). Applies to specialized triangle pipelines.
    bool     per_pixel_lighting  = true;

    // Presentation image quality (post_process.h), applied to the presented
    // frame on the render thread. aa_mode: 0 off, 1 SMAA, 2 auto (SMAA unless
    // the game image already has 1.5x the window's pixels on both axes).
    // smaa_quality: 0 low .. 3 ultra. upscaler: 0 bilinear, 1 FidelityFX
    // FSR 1 when the game image is smaller than the window. sharpness: RCAS
    // strength 0-100, 0 skips the pass.
    // Internal resolution follows the window: after a resize settles (not
    // during an edge drag), efb_scale becomes the smallest scale that covers
    // the window's game picture, as the launcher's "Match Output" does at
    // start. The renderer re-creates its targets once, at a frame boundary.
    bool     internal_follow_window = false;

    // false presents with the plain bilinear blit (A/B and fallback).
    bool     post_process        = true;
    unsigned aa_mode             = 2;
    unsigned smaa_quality        = 2;
    unsigned upscaler            = 1;
    unsigned sharpness           = 20;

    // Reserved for future DXR work. Clamped off until there is a real DXR
    // capability query, acceleration-structure path, ray PSO, and visible
    // native effect.
    bool     ray_tracing_enabled = false;
    unsigned rt_shadow_quality   = 0;   // 0=off, 1=low, 2=medium, 3=high
    bool     rt_ao_enabled       = false;
    bool     rt_reflections      = false;

    // Back-buffer dimensions (may differ from EFB; the present blit scales).
    unsigned window_width        = 854;
    unsigned window_height       = 480;
};

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
    // fields), so the GPU's execution time no longer serializes the game's
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
