#pragma once

// The rendering settings (config.ini), set before galaxy::gx::initialize() and changed at run time through
// set_render_config(); the render thread takes a snapshot at each frame boundary and applies changes there
// (efb_scale re-creates the EFB targets). Window dimensions change through the window itself. Settings the renderer
// does not implement are clamped off.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace galaxy::gx {

// The largest EFB/XFB backing scale; the game's logical EFB/VI sizes and addresses are unchanged. 12x covers 32:9 at
// 2160 lines; 16x (10240x8448) stays inside D3D12's 16384-texel limit.
inline constexpr unsigned kMaxEfbScale = 16u;
inline constexpr float kMinFrameRate = 1.0f;
inline constexpr float kMaxFrameRate = 1000.0f;
// The bloom settings' top (percent of the game's and the camera's own strength).
inline constexpr unsigned kMaxBloom = 200;
// RenderConfig::bloom_mode.
inline constexpr unsigned kBloomNative = 0;
inline constexpr unsigned kBloomEnhanced = 1;

struct RenderConfig {
    // EFB resolution multiplier, 1..kMaxEfbScale (2: 1280x1056, supersampling the game's 640x528). 1 is the Wii's
    // own resolution.
    unsigned efb_scale           = 1;

    // DXGI Present sync interval: 0 off, 1 every vblank, 2 every other vblank. 1 can back-pressure the command
    // stream through DWM and lower the frame rate on some systems.
    unsigned vsync_interval      = 0;

    // Swap-chain options, read once at start.
    bool     wait_for_frame_latency = false;
    unsigned swap_chain_frame_latency = 2;
    bool     allow_tearing = false;
    bool     flip_discard = true;

    // EFB multisampling (1, 2, 4 or 8), up to what the adapter supports; single-sampled with temporal upscaling or
    // ray tracing.
    unsigned msaa_samples        = 1;

    // Not implemented; always clamped false.
    bool     taa_enabled         = false;

    // Frames per second presented (0: the display's rate, at least 60; see present_frame_rate).
    float    max_fps             = 60.0f;

    // Submits the game's GX FIFO at its own 60 Hz cadence instead of one present per game frame.
    bool     frame_rate_decouple = false;

    // Mip LOD bias in units of 0.5 LOD levels (0 full detail, 2 quarter), applied to every sampler.
    unsigned texture_lod_bias    = 0;

    // Anisotropic filtering for mipmapped GX samplers: 1 off, 2/4/8/16 D3D12_SAMPLER_DESC::MaxAnisotropy.
    unsigned anisotropic_filtering = 1;

    // Generates mip chains for color textures the game samples with linear filtering but without authored mips.
    // Authored mips are decoded unchanged.
    bool     enhanced_mipmaps    = false;
    // Curved models rounded on the GPU where the renderer tessellates (curved_surfaces.h).
    bool     curved_surfaces     = true;

    // Lit GX color channels evaluated per pixel instead of per vertex (the XF light formulas on the interpolated
    // eye-space position and normal), in specialized triangle pipelines.
    bool     per_pixel_lighting  = false;

    // Lit and environment-mapped draws shaded per pixel in linear light, with highlights, a sky/ground ambient and
    // Fresnel on environment maps (enhanced_lighting.h). Takes precedence over per_pixel_lighting for those draws.
    bool     enhanced_lighting   = false;
    // Ambient occlusion of the 3D scene (ambient_occlusion.h); ray-traced lighting has its own.
    bool     ambient_occlusion   = false;
    // The game's bloom: kBloomNative its own chain at the console's size, kBloomEnhanced the renderer's at the internal
    // resolution (game_bloom.h) at `bloom` percent of its strength (0 off, up to kMaxBloom).
    unsigned bloom_mode          = kBloomEnhanced;
    unsigned bloom               = 100;

    // Match Output: after a resize settles (not during an edge drag), efb_scale becomes the smallest scale whose game
    // picture covers the window; the renderer re-creates its targets once, at a frame boundary. Off by default: the
    // internal resolution is the player's setting.
    bool     internal_follow_window = false;
    // Match Output may step its scale down while the GPU stays above 80% busy (RendererD3D12::update_gpu_budget).
    // Off by default: it would change what the player chose.
    bool     gpu_budget = false;
    // The 2D layer (HUD, menus, text) renders at the window's resolution over the resampled 3D scene when the
    // internal resolution is lower. Off by default: the scene then takes two resamples (to the integer output
    // scale, then to the window) instead of one.
    bool     hud_output_resolution = false;

    // Presentation image quality (post_process.h), applied to the presented frame on the render thread; false
    // presents with the plain bilinear blit.
    bool     post_process        = true;
    // 0 off, 1 SMAA, 2 auto (SMAA unless the game image already has 1.5x the window's pixels on both axes), 3 FXAA.
    unsigned aa_mode             = 0;
    // 0 low .. 3 ultra.
    unsigned smaa_quality        = 2;
    // 0 bilinear; 1 FidelityFX FSR 1 when the game image is smaller than the window; 2 FidelityFX FSR 3 and 3 NVIDIA
    // DLSS (Streamline), both temporal: the jittered 3D scene is upscaled before the 2D layer and renders at the
    // window's scale divided by the temporal_quality ratio.
    unsigned upscaler            = 0;
    // DLSS Frame Generation (GRAPHICS > INTERPOLATION): nonzero has its generated frames reach max_fps (0: the
    // display's refresh rate) instead of Nebula's interpolated ones. It runs with DLSS (upscaler 3) and the Frame
    // Generation plugins, loaded when the game starts with it on; while it runs the game presents its own 60 frames.
    // Without it the target falls back to Nebula's interpolation.
    unsigned frame_generation    = 0;
    // 0 native AA 1.0, 1 quality 1.5, 2 balanced 1.7, 3 performance 2.0, 4 ultra performance 3.0.
    unsigned temporal_quality    = 1;
    // Bilinear and FSR 1: 0 renders at the INTERNAL RES row; 1-4 render at the output scale divided by 1.5, 1.7, 2
    // or 3 (the temporal_quality ratios).
    unsigned spatial_quality     = 0;
    // RCAS strength 0-100; 0 skips the pass.
    unsigned sharpness           = 0;

    // Ray tracing on adapters with DXR 1.1, shader model 6.5 and dedicated video memory; ignored elsewhere.
    // rt_shadow_quality and rt_ao_enabled are not implemented and clamped off.
    bool     ray_tracing_enabled = false;
    // Ray tracing quality: 0 medium (lighting at a share of the pixels, 2 sky rays, one glow light per
    // sample), 1 high (every pixel up to 4K-class pictures, 4 sky rays, two glow lights, reflections that
    // shade what they hit off screen).
    unsigned rt_quality          = 1;
    // The ray-traced effects players choose, each read every frame: the reflections on the scene's surfaces (metal,
    // gloss, glass, crystal; off: the game's painted ones, SceneRayTracer::surface_reflections_active), the camera's
    // lens bloom and the sun's shafts (ray_tracing_camera.cpp), the traced seas, lakes and pools (their reflections
    // and refraction; off: the game's own water).
    bool     rt_reflections      = true;
    bool     rt_lens_bloom       = true;
    bool     rt_sun_rays         = true;
    bool     rt_water            = true;
    unsigned rt_shadow_quality   = 0;   // 0=off, 1=low, 2=medium, 3=high
    bool     rt_ao_enabled       = false;

    // Texture packs (texture_pack.h): replacements from texture_pack_directory() and the mods'
    // textures/ folders. preload loads the whole pack at start instead of each texture at its first
    // use; dump writes every newly decoded texture once, as a PNG under its pack name, to
    // texture_dump_directory(). The replacements on the GPU stay within texture_pack_budget_mb
    // (unused ones are evicted first).
    bool     texture_packs       = true;
    bool     texture_pack_preload = false;
    bool     texture_dump        = false;
    unsigned texture_pack_budget_mb = 2048;

    // Back-buffer size (the present blit scales the EFB to it): in windowed mode the client size, limited to the
    // monitor's work area.
    unsigned window_width        = 854;
    unsigned window_height       = 480;

    // Display, reconciled by the render thread at a frame boundary: 0 windowed, 1 borderless fullscreen. Exclusive
    // fullscreen (2) is not offered; a saved 2 opens borderless.
    unsigned display_mode        = 1;
    // GDI device name of the monitor (UTF-8, e.g. \\.\DISPLAY2); empty or unknown selects the primary monitor.
    std::array<char, 64> monitor{};
    // The GPU this session renders on, as gpu_identity() writes it ("10de:2b85:89ef1043"); empty, or a
    // GPU that is not present, picks one automatically (GALAXY_GPU_PREFERENCE). Set at start-up from
    // config.ini (GALAXY_GPU overrides) and kept for the session; see next_start_gpu().
    std::array<char, 32> gpu{};
    unsigned fullscreen_width    = 0;
    unsigned fullscreen_height   = 0;
    unsigned fullscreen_refresh_hz = 0;
    // Game picture aspect, an index of kAspectRatios (0 follows the window's shape). Applied at the next video
    // frame.
    unsigned aspect_mode         = 0;

    friend bool operator==(const RenderConfig&, const RenderConfig&) = default;
};

// Frames per second the game picture is presented at. The game renders 60; lower rates present the newest frame on
// their own cadence, higher rates add interpolated frames between game frames. Unlimited (0) is the display rate (at
// least 60). VSync limits the rate to the display's.
inline double present_frame_rate(float max_fps, unsigned vsync_interval, double refresh_hz) {
    double rate = max_fps > 0.0f ? static_cast<double>(max_fps)
                                 : (refresh_hz > 1.0 ? std::max(refresh_hz, 60.0) : 60.0);
    if (vsync_interval != 0u && refresh_hz > 1.0) rate = std::min(rate, refresh_hz);
    return rate;
}

// The most interpolated frames one game frame needs at a present rate: ceil(rate / 60) - 1 (75 Hz shows one between
// most pairs of game frames, 144 Hz two between some), none at 60 and below, at most 7.
inline unsigned interpolated_frames_per_game_frame(double rate) {
    if (!(rate > 61.0)) return 0u;
    const double frames = std::ceil(rate / 60.0 - 1e-3) - 1.0;
    return static_cast<unsigned>(std::clamp(frames, 1.0, 7.0));
}

inline constexpr unsigned kDisplayWindowed = 0u;
inline constexpr unsigned kDisplayBorderless = 1u;
inline constexpr unsigned kDisplayExclusive = 2u;
inline constexpr unsigned kAspectModeCount = 7u;
// Width:height of each aspect_mode (0 = follow the window), narrowest first.
inline constexpr unsigned kAspectRatios[kAspectModeCount][2] = {
    {0u, 0u}, {4u, 3u}, {3u, 2u}, {16u, 10u}, {16u, 9u}, {21u, 9u}, {32u, 9u}};
// Their names in config.ini (aspect=). A number there is an index of the list before 3:2 (auto, 4:3, 16:10, 16:9,
// 21:9, 32:9).
inline constexpr const char* kAspectNames[kAspectModeCount] = {"auto", "4:3", "3:2", "16:10", "16:9", "21:9", "32:9"};

// Window sizes of exactly an aspect_mode's shape that fit max_width x max_height, smallest first; none for
// aspect_mode 0. The launcher's GraphicsSettings.AspectWindowSizes lists the same sizes.
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

// The GX backend's timing policy (get_gx_timing_config), the same for every subsystem.
struct GxTimingConfig {
    // Non-present render chunks the game thread may have outstanding before render_frame() waits for the render
    // thread.
    unsigned render_queue_depth = 2;
    bool vi_render_fifo_wait = false;
    bool pe_events_gpu_fence = true;
    bool render_live_memory_wait = false;
    bool render_memory_snapshot = false;
    // Neither the live-read barrier nor a snapshot: the render thread reads guest RAM directly while the game runs
    // ahead (the hardware's GP model, Dolphin's dual-core model). PE completion stays on GPU fences, so the game's own
    // GXDrawDone discipline bounds how far ahead it writes.
    bool render_live_memory_reads = true;
    // With pe_events_gpu_fence, a capture's PE finish/token events are delivered once its command list is submitted
    // (every guest-memory read of the capture, texture decodes included, is complete by then) instead of at the GPU
    // fence. Nothing the GPU produces is read back through guest memory (EFB copies stay GPU resources; peeks use
    // asynchronous fields), so GPU execution time does not serialize the game's GXDrawDone.
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

// A copy taken under the lock, never a partial update.
[[nodiscard]] RenderConfig get_render_config();

// While DLSS Frame Generation runs (set by the renderer) the game presents its own 60 frames; the generated ones
// raise the rate.
void set_frame_generation_running(bool running) noexcept;
[[nodiscard]] bool frame_generation_running() noexcept;
// The game's report each frame (NEBULA_CONTEXT_FRAME_GENERATION): play is on screen, so generated frames may be
// added. A report older than 100 ms (loading, other scenes, a game that never reports) counts as no.
void note_frame_generation_allowed(bool allowed) noexcept;
[[nodiscard]] bool frame_generation_allowed() noexcept;
// The game's report each frame (NEBULA_CONTEXT_CAMERA_WATER): the camera's depth under water in eye units, < 0 out of
// water. A report older than 100 ms counts as out of water.
void note_camera_water(float depth) noexcept;
[[nodiscard]] float camera_water_depth() noexcept;
// The rate the game presents at: max_fps, or 60 while frame generation runs.
[[nodiscard]] inline float presented_max_fps(const RenderConfig& cfg) noexcept {
    return frame_generation_running() ? 60.0f : cfg.max_fps;
}
void set_render_config(const RenderConfig& cfg);

// The ray-traced effects players choose (RenderConfig::rt_reflections, rt_water, rt_sun_rays, rt_lens_bloom), as the
// frame being drawn takes them. Tests: GALAXY_TEST_RT_EFFECT_AB=reflections|water|sun_rays|lens_bloom turns that one off
// in frames that end in an odd present (set_rt_effect_frame, the snapshots' numbering): at 60 fps consecutive
// pictures of one run differ by the effect alone.
enum class RtEffect : std::uint8_t { Reflections, Water, SunRays, LensBloom };
[[nodiscard]] bool rt_effect_on(const RenderConfig& cfg, RtEffect effect) noexcept;
// Whether the renderer tessellates (its curved surfaces' stages compiled): the CURVED SURFACES row is offered and
// RenderConfig::curved_surfaces takes effect.
void set_curved_surfaces_offered(bool offered) noexcept;
[[nodiscard]] bool curved_surfaces_offered() noexcept;
void set_rt_effect_frame(std::uint64_t frame) noexcept;

// The EFB scale config.ini keeps for the next start. The overlay sets it with the live value so config.ini and the
// running renderer agree.
void stage_next_start_efb_scale(unsigned efb_scale);
[[nodiscard]] RenderFeatureCaps get_render_feature_caps();
void set_render_feature_caps(const RenderFeatureCaps& caps);

// Both return GxTimingConfig{}; runtime code uses get_gx_timing_config().
[[nodiscard]] GxTimingConfig resolve_gx_timing_config_from_environment();
[[nodiscard]] const GxTimingConfig& get_gx_timing_config();

// Reads config.ini (in the data folder, or GALAXY_RENDER_CONFIG_PATH) if present, then the GALAXY_<KEY> overrides;
// missing keys keep their defaults. Called once by the host before initialize().
void load_render_config_from_file();

// Writes the implemented settings to the same config.ini; unimplemented ones are omitted.
bool save_render_config_to_file();

// A GPU's identity in config.ini's gpu key: PCI vendor, device and subsystem id in lowercase hex,
// "vvvv:dddd:ssssssss". The subsystem tells two boards of the same chip apart.
std::string gpu_identity(unsigned vendor_id, unsigned device_id, unsigned subsys_id);

// The gpu config.ini names, which the next start renders on. Nebula Settings owns the key: a reload
// takes it here and leaves RenderConfig::gpu as the session started, and every save writes the value
// the file has at that moment (this one when it cannot be read), never GALAXY_GPU.
std::string next_start_gpu();

// Records the adapter the renderer runs on (adapter.ini beside config.ini) for the launcher, which offers DLSS only
// on an NVIDIA RTX adapter: description, PCI vendor and device id, its gpu_identity(), the GPU preference the runtime
// started with (low: integrated first) and whether it runs Nebula's ray tracing.
void record_render_adapter(const wchar_t* description, unsigned vendor_id, unsigned device_id,
                           unsigned subsys_id, bool low_power_preference, bool ray_tracing) noexcept;
// When config.ini changed on disk since this process last read or wrote it (Nebula Settings saved it while the game
// runs), reads it over the live settings, as the in-game menu would apply them, and returns true.
bool reload_render_config_if_changed();

// Texture packs are mods (mods.h). TextureDumps beside config.ini takes dumps (GALAXY_TEXTURE_DUMP_DIR replaces it);
// GALAXY_TEXTURE_PACK_DIR names a pack folder for tests, below every mod. Empty when unknown or unset.
[[nodiscard]] std::filesystem::path texture_pack_directory();
[[nodiscard]] std::filesystem::path texture_pack_test_directory();
[[nodiscard]] std::filesystem::path texture_dump_directory();
// The enabled mods' textures/ folders in load order (the host sets them at start); the generation changes with each
// set.
void set_texture_pack_mod_folders(std::vector<std::filesystem::path> folders);
// The disc's game id (RMGE01, RMGP01, RMGJ01; the host sets it at start, TexturePacks::kGameId until then): the packs'
// game folders and the dumps' are named by it.
void set_texture_pack_game_id(std::string id);
[[nodiscard]] std::string texture_pack_game_id();
[[nodiscard]] std::vector<std::filesystem::path> texture_pack_mod_folders();
[[nodiscard]] std::uint64_t texture_pack_mod_folders_generation() noexcept;
// HD movie replacements (movie_replacement.h): the Movies folder beside config.ini (GALAXY_MOVIE_DIR replaces it), and
// the enabled mods' movies/ folders in load order (the host sets them at start).
[[nodiscard]] std::filesystem::path movie_directory();
// config.ini (GALAXY_RENDER_CONFIG_PATH replaces it); empty when unknown.
[[nodiscard]] std::filesystem::path render_config_path();
// The shader caches' folder (Nebula\shadercache\RMGE01 in the local app data).
[[nodiscard]] std::filesystem::path shader_cache_directory();
void set_movie_mod_folders(std::vector<std::filesystem::path> folders);
[[nodiscard]] std::vector<std::filesystem::path> movie_mod_folders();
[[nodiscard]] std::uint64_t movie_mod_folders_generation() noexcept;

} // namespace galaxy::gx
