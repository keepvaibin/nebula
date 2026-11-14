#pragma once

// The rendering settings (config.ini), set before galaxy::render::initialize() and changed at run time through
// set_render_config(); the render thread takes a snapshot at each frame boundary and applies changes there
// Window dimensions change through the window itself. Settings the renderer
// does not implement are clamped off.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "galaxy/game_step.h"

namespace galaxy::render {

inline constexpr float kMinFrameRate = 1.0f;
inline constexpr float kMaxFrameRate = 1000.0f;
// The bloom settings' top (percent of the game's and the camera's own strength).
inline constexpr unsigned kMaxBloom = 200;

struct RenderConfig {

    // DXGI Present sync interval: 0 off, 1 every vblank, 2 every other vblank. 1 can back-pressure the command
    // stream through DWM and lower the frame rate on some systems.
    // On Apple's systems it is on unless turned off: a Metal layer that is not synchronised puts each picture up the
    // moment it is drawn, part way down a refresh (torn in full screen, uneven in a window), and synchronised it costs
    // no frame rate there (the game's frames already keep the display's vsync).
#if defined(__APPLE__)
    unsigned vsync_interval      = 1;
#else
    unsigned vsync_interval      = 0;
#endif

    // Swap-chain options, read once at start.

    // scene target multisampling (1, 2, 4 or 8), up to what the adapter supports; single-sampled with temporal upscaling or
    // ray tracing.
    unsigned msaa_samples        = 1;

    // Not implemented; always clamped false.
    bool     taa_enabled         = false;

    // Frames per second presented (0: the display's rate, at least 60; see present_frame_rate).
    float    max_fps             = static_cast<float>(kGameStepsPerSecond);

    // Submits the game's records at their own 60 Hz cadence instead of one present per game frame.
    bool     frame_rate_decouple = false;

    // Mip LOD bias in units of 0.5 LOD levels (0 full detail, 2 quarter), applied to every sampler.
    unsigned texture_lod_bias    = 0;

    // Anisotropic filtering for mipmapped samplers: 1 off, 2/4/8/16 D3D12_SAMPLER_DESC::MaxAnisotropy.
    unsigned anisotropic_filtering = 1;

    // Generates mip chains for color textures the game samples with linear filtering but without authored mips.
    // Authored mips are decoded unchanged.
    bool     enhanced_mipmaps    = false;
    // Curved models rounded once when loaded (shapes::refine_curved, curved_surfaces.h).
    bool     curved_surfaces     = true;

    // Lit color channels evaluated per pixel instead of per vertex (the light formulas on the interpolated
    // eye-space position and normal), in specialized triangle pipelines.
    bool     per_pixel_lighting  = false;

    // Lit and environment-mapped draws shaded per pixel in linear light, with highlights, a sky/ground ambient and
    // Fresnel on environment maps (enhanced_lighting.h). Takes precedence over per_pixel_lighting for those draws.
    bool     enhanced_lighting   = false;
    // The enhanced lighting's scene in a wide target with its camera (scene_camera.h). No row: its look is not accepted.
    bool     wide_scene          = true;
    // Ambient occlusion of the 3D scene (ambient_occlusion.h); ray-traced lighting has its own.
    bool     ambient_occlusion   = false;
    // Which: SSAO instead of GTAO.
    bool     ambient_occlusion_ssao = false;
    // The game's bloom (game_bloom.h) at `bloom` percent of its strength (0 off, up to kMaxBloom).
    unsigned bloom               = 100;

    // The 2D layer (HUD, menus, text) renders at the window's resolution over the resampled 3D scene when the
    // internal resolution is lower. Off by default: the scene then takes two resamples (to the integer output
    // scale, then to the window) instead of one.
    bool     hud_output_resolution = false;
    // With an internal height (render_internal_height): the 2D layer drawn into the scene at the internal size, and the
    // picture enlarged once at present, instead of the scene enlarged into a window-size target the 2D layer is drawn over
    // (render_hud_at_output). Faster, text softer; hud_output_resolution wins. Off: as without it.
    bool     hud_internal_resolution = false;

    // Presentation image quality (post_process.h), applied to the presented frame on the render thread; false
    // presents with the plain bilinear blit.
    bool     post_process        = true;
    // 0 off, 1 SMAA, 2 auto (SMAA unless the game image already has 1.5x the window's pixels on both axes), 3 FXAA.
    unsigned aa_mode             = 0;
    // 0 low .. 3 ultra.
    unsigned smaa_quality        = 2;
    // 0 bilinear; 1 FidelityFX FSR 1 when the game image is smaller than the window; 2 FidelityFX FSR 3, 3 NVIDIA
    // DLSS (Streamline), 4 Intel XeSS and 5 Snapdragon Game Super Resolution 2, all temporal: the jittered 3D scene is
    // upscaled before the 2D layer and renders at the window's scale divided by the temporal_quality ratio.
    unsigned upscaler            = 0;
    // 2: FSR 3 frame generation (fsr_frame_generation.h), one frame between each two, with FSR 3 or XeSS. 1: DLSS Frame Generation
    // (GRAPHICS > INTERPOLATION): nonzero has its generated frames reach max_fps (0: the
    // display's refresh rate) instead of Nebula's interpolated ones. It runs with DLSS (upscaler 3) and the Frame
    // Generation plugins, loaded when the game starts with it on; while it runs the game presents its own 60 frames.
    // Without it the target falls back to Nebula's interpolation.
    unsigned frame_generation    = 0;
    // 0 native AA 1.0, 1 quality 1.5, 2 balanced 1.7, 3 performance 2.0, 4 ultra performance 3.0, 5 ultra quality 1.3
    // (not DLSS, which has no such mode: it runs 5 as quality).
    unsigned temporal_quality    = 1;
    // FSR 1: 0 renders at the output's size; 1-5 render at the output scale divided by the temporal_quality ratios.
    unsigned spatial_quality     = 0;
    // Upscaling off (upscaler 0): 0 follows the content area of the output; otherwise the selected vertical pixel
    // count. Width follows the current content aspect, including a fold or window resize. Ignored with an upscaler on
    // (render_internal_height).
    unsigned internal_height     = 0;
    // RCAS strength 0-100; 0 skips the pass.
    unsigned sharpness           = 0;
    // HDR10 output (post_process_settings.h): offered only while the display reports HDR mode, and written only then
    // (get_display_hdr); the picture is otherwise the SDR one, bit for bit. paper_white is the luminance of the picture's white
    // in nits (kPaperWhiteMin..kPaperWhiteMax).
    bool     hdr                 = false;
    unsigned paper_white         = 200;

    // Ray tracing, on a device that can trace (rt_caps.h); ignored elsewhere.
    bool     ray_tracing_enabled = false;
    // The preset (plan 19.1): 0 medium, 1 high, 2 low, 3 reference (path traced, to compare), 4 custom (the parts as
    // set one by one). A preset sets every part (rt_apply_preset).
    unsigned rt_quality          = 0;
    // The parts, each its own setting, in RtPart's order (sun shadows, local lights, sky and bounce light, reflections,
    // glass and water, light in the air): 0 off, 1 low, 2 medium, 3 high.
    std::array<std::uint8_t, 6> rt_parts{2u, 2u, 2u, 2u, 2u, 2u};
    // Plan 19.2: what one shadow ray a pixel cost this GPU at the output size (ms), measured once by the tracer in the
    // first stage it traced with a sun (0: not yet). It sets the preset (high under 0.35 ms, medium under 1.2, else
    // low) unless the player chose one: rt_chosen, set by the Settings rows.
    float    rt_probe_ms         = 0.0f;
    bool     rt_chosen           = false;
    // Light the lens scatters around every bright surface.
    bool     rt_lens_bloom       = true;

    // Texture packs (texture_pack.h): replacements from the mods'
    // textures/ folders. preload loads the whole pack at start instead of each texture at its first
    // use; dump writes every newly decoded texture once, as a PNG under its pack name, to
    // texture_dump_directory(). The replacements on the GPU stay within texture_pack_budget_mb
    // (unused ones are evicted first).
    bool     texture_packs       = true;
    bool     texture_pack_preload = false;
    bool     texture_dump        = false;
    unsigned texture_pack_budget_mb = 2048;

    // Back-buffer size (the present blit scales the scene target to it): in windowed mode the client size, limited to the
    // monitor's work area.
    unsigned window_width        = 854;
    unsigned window_height       = 480;

    // Display, reconciled by the render thread at a frame boundary: 0 windowed, 1 borderless fullscreen. Exclusive
    // fullscreen (2) is not offered; a saved 2 opens borderless.
    unsigned display_mode        = 1;
    // GDI device name of the monitor (UTF-8, e.g. \\.\DISPLAY2); empty or unknown selects the primary monitor.
    std::array<char, 64> monitor{};
    // The GPU this session renders on, as gpu_identity() writes it ("10de:2b85:89ef1043"); empty, or a
    // GPU that is not present, picks one automatically (the high-performance GPU first). Set at start-up from
    // config.ini (GALAXY_GPU overrides) and kept for the session; see next_start_gpu().
    std::array<char, 32> gpu{};
    // Game picture aspect, an index of kAspectRatios (0 follows the window's shape). Applied at the next video
    // frame.
    unsigned aspect_mode         = 0;

    friend bool operator==(const RenderConfig&, const RenderConfig&) = default;
};

// Whether the lit colour channels are summed at the pixel: asked for, or needed by the enhanced lighting, or by ray
// tracing (a light's traced share is a pixel's).
[[nodiscard]] inline bool lights_at_pixel(const RenderConfig& config) noexcept {
    return config.per_pixel_lighting || config.enhanced_lighting || config.ray_tracing_enabled;
}

// Frames per second the game picture is presented at. The game renders 60; lower rates present the newest frame on
// their own cadence, higher rates add interpolated frames between game frames. Unlimited (0) is the display rate (at
// least 60). VSync limits the rate to the display's.
inline double present_frame_rate(float max_fps, unsigned vsync_interval, double refresh_hz) {
    double rate = max_fps > 0.0f ? static_cast<double>(max_fps)
                                 : (refresh_hz > 1.0 ? std::max(refresh_hz, kGameStepRate) : kGameStepRate);
    if (vsync_interval != 0u && refresh_hz > 1.0) rate = std::min(rate, refresh_hz);
    return rate;
}

// The most interpolated frames one game frame needs at a present rate: ceil(rate / 60) - 1 (75 Hz shows one between
// most pairs of game frames, 144 Hz two between some), none at 60 and below, at most 7.
inline unsigned interpolated_frames_per_game_frame(double rate) {
    if (!(rate > kGameStepRate + 1.0)) return 0u;
    const double frames = std::ceil(rate / kGameStepRate - 1e-3) - 1.0;
    return static_cast<unsigned>(std::clamp(frames, 1.0, 7.0));
}

inline constexpr std::array<unsigned, 9> kInternalHeights{0, 144, 240, 360, 480, 720, 1080, 1440, 2160};
inline unsigned normalized_internal_height(unsigned height) {
    return std::find(kInternalHeights.begin(), kInternalHeights.end(), height) != kInternalHeights.end() ? height : 0u;
}
// The exact height the 3D scene renders at: INTERNAL RESOLUTION with upscaling off. An upscaler's mode sets the
// scale instead (upscaler_render_fraction), so the two never multiply.
inline unsigned render_internal_height(const RenderConfig& cfg) {
    return cfg.upscaler == 0u ? normalized_internal_height(cfg.internal_height) : 0u;
}
// Whether the 2D layer is drawn at the window's resolution over the enlarged scene (begin_output_scene): asked for
// (hud_output_resolution), or an internal height without hud_internal_resolution.
inline bool render_hud_at_output(const RenderConfig& cfg) {
    return cfg.hud_output_resolution || (render_internal_height(cfg) != 0u && !cfg.hud_internal_resolution);
}
// A common sizing rule for both encoders. Bounds preserve aspect and fit `max_dimension`, the device's largest 2D
// texture (D3D12's required 16384, Vulkan's maxImageDimension2D).
inline std::pair<unsigned, unsigned> internal_extent(unsigned output_width, unsigned output_height,
                                                     unsigned height, unsigned max_dimension, float quality = 1.0f) {
    if (!output_width || !output_height) return {1u, 1u};
    height = normalized_internal_height(height);
    const double scale = (height ? double(height) / output_height : 1.0) * quality;
    const double bounded = std::min(scale, double(max_dimension) / std::max(output_width, output_height));
    return {std::max(1u, unsigned(std::lround(output_width * bounded))),
            std::max(1u, unsigned(std::lround(output_height * bounded)))};
}

inline constexpr unsigned kDisplayWindowed = 0u;
inline constexpr unsigned kDisplayBorderless = 1u;
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

// The backend's timing policy (get_frame_timing_config), the same for every subsystem.
struct RenderTimingConfig {
    // Non-present render chunks the game thread may have outstanding before render_frame() waits for the render
    // thread.
    unsigned render_queue_depth = 2;
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

// The settings as the renderer runs them, a copy taken under the lock (never a partial update): the player's, except
// while the tracer runs, when the scene renders single-sampled and is anti-aliased temporally (plan 16.3: the tracer
// reads one depth and one item a pixel): MSAA is set aside and, without a temporal upscaler chosen, FSR 3 runs, in its
// native anti-aliasing mode or at FSR 1's chosen scale.
[[nodiscard]] RenderConfig get_render_config();
// The player's settings as chosen and saved (Nebula Settings, config.ini): ray tracing off gives them back whole.
[[nodiscard]] RenderConfig get_render_config_chosen();
// Set as the tracer starts (with whether FSR 3 runs on this device) and stops.
void set_render_tracing(bool tracing, bool fsr3) noexcept;
// Whether the tracer runs (set_render_tracing).
[[nodiscard]] bool render_tracing() noexcept;

// FSR 3 frame generation is built into this renderer (RenderConfig::frame_generation 2; 1 is DLSS).
[[nodiscard]] bool fsr_frame_generation_compiled() noexcept;
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
    return frame_generation_running() ? static_cast<float>(kGameStepsPerSecond) : cfg.max_fps;
}
void set_render_config(const RenderConfig& cfg);

// Ray tracing's parts (plan 19.1), in RenderConfig::rt_parts' order; each 0 off, 1 low, 2 medium, 3 high.
enum class RtPart : std::uint8_t { Sun, Lights, Indirect, Reflect, Transmit, Volume, Count };
inline constexpr unsigned kRtPresetReference = 3u, kRtPresetCustom = 4u;
inline unsigned rt_part(const RenderConfig& cfg, RtPart part) noexcept {
    return part < RtPart::Count ? std::min<unsigned>(cfg.rt_parts[static_cast<std::size_t>(part)], 3u) : 0u;
}

// The presets' parts (plan 19.1): low leaves the painted reflections; reference takes high's parts and the reference
// path tracer draws the picture (FB's 22.9).
inline void rt_apply_preset(RenderConfig& cfg) noexcept {
    constexpr std::uint8_t kParts[4][6] = {
        {2u, 2u, 2u, 2u, 2u, 2u},  // medium
        {3u, 3u, 3u, 3u, 3u, 3u},  // high
        {1u, 1u, 1u, 0u, 1u, 1u},  // low
        {3u, 3u, 3u, 3u, 3u, 3u},  // reference
    };
    if (cfg.rt_quality < kRtPresetCustom) {
        std::copy(std::begin(kParts[cfg.rt_quality]), std::end(kParts[cfg.rt_quality]), cfg.rt_parts.begin());
    }
}

// Whether the renderer refines curved surfaces: the CURVED SURFACES row is offered and
// RenderConfig::curved_surfaces takes effect.
void set_curved_surfaces_offered(bool offered) noexcept;
[[nodiscard]] bool curved_surfaces_offered() noexcept;

// The scene target scale config.ini keeps for the next start. The overlay sets it with the live value so config.ini and the
// running renderer agree.
[[nodiscard]] RenderFeatureCaps get_render_feature_caps();
void set_render_feature_caps(const RenderFeatureCaps& caps);

// Both return RenderTimingConfig{}; runtime code uses get_frame_timing_config().
[[nodiscard]] RenderTimingConfig resolve_frame_timing_config_from_environment();
[[nodiscard]] const RenderTimingConfig& get_frame_timing_config();

// Reads config.ini (in the settings folder: --settings, else the data folder) if present, then the GALAXY_<KEY> overrides;
// missing keys keep their defaults. Called once by the host before initialize().
void load_render_config_from_file();

// Writes the implemented settings to the same config.ini; unimplemented ones are omitted.
bool save_render_config_to_file();

// load_render_config_from_file found no config.ini: this is the first start, and nothing the player chose exists yet.
[[nodiscard]] bool render_config_first_start() noexcept;
// The first start's internal height before it is measured (render_core.cpp's calibration): 480 on a GPU that shares the
// system's memory, else 1080; 0 (match display) when the output has no more lines than that.
[[nodiscard]] unsigned first_start_internal_height(bool shared_memory, unsigned output_lines) noexcept;
// The PERFORMANCE preset: the 3D scene and the 2D layer at 480 lines and one enlarge at present, every optional pass off.
void apply_performance_preset(RenderConfig& cfg) noexcept;

// A GPU's identity in config.ini's gpu key: PCI vendor, device and subsystem id in lowercase hex,
// "vvvv:dddd:ssssssss". The subsystem tells two boards of the same chip apart.
std::string gpu_identity(unsigned vendor_id, unsigned device_id, unsigned subsys_id);

// The gpu config.ini names, which the next start renders on. Nebula Settings owns the key: a reload
// takes it here and leaves RenderConfig::gpu as the session started, and every save writes the value
// the file has at that moment (this one when it cannot be read), never GALAXY_GPU.
std::string next_start_gpu();

// Records the adapter the renderer runs on (adapter.ini beside config.ini) for the launcher, which offers DLSS only
// on an NVIDIA RTX adapter: description (UTF-8), PCI vendor and device id, its gpu_identity() and whether it runs
// Nebula's ray tracing.
void record_render_adapter(const char* description, unsigned vendor_id, unsigned device_id,
                           unsigned subsys_id, bool ray_tracing) noexcept;
// When config.ini changed on disk since this process last read or wrote it (Nebula Settings saved it while the game
// runs), reads it over the live settings, as the in-game menu would apply them, and returns true.
bool reload_render_config_if_changed();

// Texture packs are mods (mods.h). TextureDumps beside config.ini takes dumps; empty when unknown.
[[nodiscard]] std::filesystem::path texture_dump_directory();
// The enabled mods' textures/ folders in load order (the host sets them at start); the generation changes with each
// set.
void set_texture_pack_mod_folders(std::vector<std::filesystem::path> folders, std::vector<std::uint32_t> categories_off = {});
// The disc's game id (RMGE01, RMGP01, RMGJ01; the host sets it at start, TexturePacks::kGameId until then): the packs'
// game folders and the dumps' are named by it.
void set_texture_pack_game_id(std::string id);
[[nodiscard]] std::string texture_pack_game_id();
[[nodiscard]] std::vector<std::filesystem::path> texture_pack_mod_folders();
// The categories (galaxy::textures::category_bit) off in each of them.
[[nodiscard]] std::vector<std::uint32_t> texture_pack_mod_categories_off();
[[nodiscard]] std::uint64_t texture_pack_mod_folders_generation() noexcept;
// HD movie replacements (movie_playback.h): the Movies folder beside config.ini, and
// the enabled mods' movies/ folders in load order (the host sets them at start).
[[nodiscard]] std::filesystem::path movie_directory();
// config.ini in the settings folder; empty when unknown.
[[nodiscard]] std::filesystem::path render_config_path();
// The shader caches' folder (Nebula\shadercache\RMGE01 in the local app data).
[[nodiscard]] std::filesystem::path shader_cache_directory();
void set_movie_mod_folders(std::vector<std::filesystem::path> folders);
[[nodiscard]] std::vector<std::filesystem::path> movie_mod_folders();
[[nodiscard]] std::uint64_t movie_mod_folders_generation() noexcept;

} // namespace galaxy::render
