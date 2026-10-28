// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// SceneRayTracer: the game frame's opaque 3D draws as a ray-traced scene (DXR 1.1 inline ray
// queries). The geometry is rebuilt every game frame in eye space from the raster's own decoded
// vertices, index lists, VS constants and matrix palettes, so traced surfaces coincide with the
// rasterised ones. Render thread only.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include "galaxy/gx/rt_materials.h"
#include "galaxy/gx/rt_types.h"
#include "galaxy/gx/rt_reflections.h"
#include "galaxy/gx/rt_sky_sun.h"

#include <array>
#include <map>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace galaxy::gx {

struct RtBuffer {
    D3D12_GPU_VIRTUAL_ADDRESS base = 0;
    std::uint64_t bytes = 0;
};

struct RtSources {
    RtBuffer vertex_ring;
    RtBuffer resident;
    RtBuffer index_ring;
    RtBuffer constant_ring;
    RtBuffer matrix_ring;
};

struct RtUpload {
    void* cpu = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
    // The upload memory's buffer and this allocation's offset in it (for a raw view in the descriptor heap).
    ID3D12Resource* resource = nullptr;
    std::uint64_t offset = 0;
};

// The ray tracing engine's block at the end of the lighting's constants (rt_lighting.hlsl RtLight g_engine_*).
struct RtEngineConstants {
    std::uint32_t views[32];   // its buffers in the descriptor heap (rt_engine.hlsl kEv*)
    std::uint32_t size[4];     // full width, height; half width, height
    std::uint32_t info[4];     // flags, a-trous step, slot, a-trous parity
    float params[4];
    float water[4];            // the camera in water (x: 1), its depth (y, eye units): rt_lighting.hlsl water_medium
    // The table metals' colour as the game paints them, per draw (rt_engine.hlsl EnginePaintCS): the buffer's view
    // (~0: none), the traced scene's triangles, the draws the buffer holds.
    std::uint32_t paint[4];
};
static_assert(sizeof(RtEngineConstants) == 208u);

class SceneRayTracer {
public:
    using Allocate = RtUpload (*)(void* context, std::size_t bytes, std::size_t alignment);

    SceneRayTracer() = default;
    SceneRayTracer(const SceneRayTracer&) = delete;
    SceneRayTracer& operator=(const SceneRayTracer&) = delete;
    ~SceneRayTracer() { shutdown(); }

    // False without DXR 1.1 and shader model 6.5, or when a pipeline cannot be created.
    // `validate` adds the primary-ray check against the raster depth ([rt-stats]);
    // `scene_format` is the HDR scene EFB's colour (the composites draw into it during the scene),
    // `display_format` the EFB's after the camera (the glow bloom and the finish draw into it).
    bool initialize(ID3D12Device* device, std::uint64_t timestamp_frequency, bool validate,
                    DXGI_FORMAT scene_format, DXGI_FORMAT display_format);
    void shutdown() noexcept;
    // A tracer started on a worker takes over from `from` (RendererD3D12::reconcile_ray_tracing): the inputs the
    // renderer and the game set whether or not a tracer runs (its views, the stage's sun, the scene's up, camera and
    // lights), which a restart keeps (shutdown leaves them).
    void adopt_inputs(const SceneRayTracer& from) noexcept;
    // The buffers the first traced frames at this EFB size would otherwise make on the render thread (the engine's,
    // about 2 GB at 4x; the lighting's; the slots' pixel records and clear passes; the water pass's): made by the
    // starting worker, so neither the frame that takes the tracer over nor the first stage allocates them. A later
    // size is made where it is first traced.
    void prepare_buffers(std::uint32_t width, std::uint32_t height);
    // Whether prepare_buffers made them for this size.
    [[nodiscard]] bool prepared_for(std::uint32_t width, std::uint32_t height) const noexcept {
        return prepared_width_ == width && prepared_height_ == height;
    }
    [[nodiscard]] bool ready() const noexcept { return ready_; }
    // Every pipeline it has was made: the lighting's, the engine's and the reflections' (rt_prepare.h's complete start).
    [[nodiscard]] bool complete() const noexcept {
        return ready_ && lighting_enabled_ && engine_ready_ && reflections_.ready();
    }
    // The quality it started with (config rt_quality: 0 medium, 1 high, 2 low).
    [[nodiscard]] unsigned quality() const noexcept { return quality_; }

    // Start of a renderer frame: results of frames the GPU has finished are collected.
    void begin_frame(std::uint64_t frame_index);
    // The scenes' texture views: kTextureViewScenes * kTextureViewsPerScene descriptors of the frame's
    // shader-visible heap from heap index `first` (CPU handle `cpu`), which only the ray tracer writes.
    static constexpr std::uint32_t kTextureViewsPerScene = 4096u;
    static constexpr unsigned kTextureViewScenes = 4u;
    void set_texture_views(D3D12_CPU_DESCRIPTOR_HANDLE cpu, std::uint32_t first, UINT increment) noexcept;
    // Physical: the textures' de-light maps' views, kDelightViews descriptors of that heap from index `first`.
    static constexpr std::uint32_t kDelightViews = 4096u;
    void set_delight_views(D3D12_CPU_DESCRIPTOR_HANDLE cpu, std::uint32_t first, UINT increment) noexcept;
    // The fence value the next scene's texture views wait for before begin_scene (0: none; ~0: they are still in
    // unsubmitted work, and that scene draws untextured).
    [[nodiscard]] std::uint64_t texture_views_fence() const noexcept;
    // A renderer frame is submitted with `fence_value`: the texture views of scenes closed before it are free once it
    // completes.
    void note_submission(std::uint64_t fence_value) noexcept;
    void begin_scene() noexcept;
    void add_draw(const RtDraw& draw, const RtDrawInfo& info);
    void set_view(const RtView& view) noexcept;
    [[nodiscard]] bool scene_open() const noexcept { return scene_open_; }
    // From the trace to the scene's end: glow draws (kRtDrawGlow) are noted.
    [[nodiscard]] bool glow_open() const noexcept { return glow_open_; }
    // From the trace to the water's trace: translucent water draws (kRtDrawWater) are noted.
    [[nodiscard]] bool water_open() const noexcept { return water_open_; }
    // GALAXY_RT_LOG_SEQ (diagnostics): the renderer's window of logged frames.
    void set_log_seq(bool on) noexcept {
        log_seq_ = on;
        reflections_.set_log_seq(on);
    }
    [[nodiscard]] bool log_seq() const noexcept { return log_seq_; }
    // From the trace to the scene's end: opaque draws after the screen capture (kRtLatePhase) are noted.
    [[nodiscard]] bool late_open() const noexcept { return late_open_; }
    // From the 3D scene's start to its trace, under physical lighting: the game's sky lists' draws (kRtDrawSky) are
    // noted for the traced sky.
    [[nodiscard]] bool sky_open() const noexcept { return sky_open_; }
    void begin_sky() noexcept;
    // The player's up in eye space by the game's gravity (the scene marker; else the view's up): the physical camera's
    // exposure is set for the ground under the player.
    void set_scene_up(const std::array<float, 3>& up) noexcept { scene_up_ = up; }
    [[nodiscard]] bool accepts(const RtDrawInfo& info) const noexcept {
        return (info.surface & kRtDrawSky) != 0u ? sky_open_
               : (info.surface & kRtDrawGlow) != 0u ? glow_open_
               : (info.surface & kRtDrawWater) != 0u ? water_open_
               : info.phase == kRtLatePhase && !scene_open_ ? late_open_
                                                            : scene_open_;
    }

    // Transforms the draws noted since the last flush into the scene's eye-space vertex buffer.
    // Called before a render chunk's command list closes: the next chunk's frame begin rewinds
    // the upload rings the draws read from.
    void flush_geometry(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                        void* allocate_context);

    // Flushes the remaining draws, builds the acceleration structures and, with validation, runs
    // the primary-ray check against `depth_srv` (EFB depth in NON_PIXEL_SHADER_RESOURCE).
    // Closes the scene. Returns false when nothing was traced.
    bool trace_scene(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                     void* allocate_context, D3D12_GPU_DESCRIPTOR_HANDLE depth_srv, std::uint32_t width,
                     std::uint32_t height);

    // The game camera's view matrix (3x4 rows, world to eye) of the open scene; null when the
    // scene has none. The lighting history follows it from frame to frame.
    void set_camera(const std::array<float, 12>* view) noexcept;
    [[nodiscard]] bool lighting_active() const noexcept {
        return ready_ && lighting_enabled_ && !ab_original_ && !scene_untraced_;
    }
    // A scene shown untraced, as the game draws it (the file select: no stage or world, only its menu lights).
    void set_scene_untraced(bool untraced) noexcept { scene_untraced_ = untraced; }
    [[nodiscard]] bool scene_untraced() const noexcept { return scene_untraced_; }
    // The traced light is in the game's units (GALAXY_RT_PHYSICAL): lit draws show their full-light colour.
    [[nodiscard]] bool physical_active() const noexcept { return lighting_active() && physical_active_; }
    // GALAXY_RT_AB=1 (test runs): every other present shows the game's own picture (no traced
    // lighting, reflections or finish); the traced frames carry a mark in the EFB's top-left corner.
    [[nodiscard]] bool ab_original() const noexcept { return ab_original_; }
    // GxBackend at a scene's start: whether the present it becomes shows the game's own picture.
    void set_ab_original(bool original) noexcept { ab_original_ = ab_ && original; }
    // The present the scene being drawn will be shown in (the snapshots' numbering).
    void set_presents(std::uint64_t presents) noexcept { presents_ = presents; }
    // The traced sun's world direction (towards it: GALAXY_RT_SUN, else the game's visible sun)
    // and the distance of the game's sun model (0: none); false without a sun.
    [[nodiscard]] bool sun_direction(std::array<float, 3>& world, float* distance = nullptr) const noexcept;
    // While held (GxBackend: the sun arrived since the last camera cut) the game's sun keeps its last
    // direction however old it is; otherwise it expires after 2 s.
    void hold_sun(bool held) noexcept { sun_held_ = held; }
    // The open scene's key light while the game shows no sun (GxBackend: the world-fixed light its
    // lit draws take): world direction towards it, colour (0-1), strength (0-1, as it fades in and out)
    // and whether the sky shows it. It lights the scene as the sun does; one the sky does not show has
    // soft shadows (no disc: GALAXY_RT_KEY_SIZE degrees).
    void set_key_light(bool valid, const std::array<float, 3>& world, const std::array<float, 3>& color,
                       float strength, bool shown) noexcept;
    // The sun the sky shows (rt_sky_sun.h) since the last reset_sky: world direction towards it and
    // colour (peak 1); false while none stands out.
    [[nodiscard]] bool sky_sun(std::array<float, 3>& world, std::array<float, 3>& color) const noexcept;
    // A daylight sky's own key light while it shows no sun (rt_sky_sun.h SkyMap::key): world
    // direction towards it and its strength (0-1); false at night, in space, or with
    // GALAXY_RT_SKY_KEY=0.
    [[nodiscard]] bool sky_key(std::array<float, 3>& world, float& strength) const noexcept;
    // A new sky (GxBackend: a stage loaded): the sky map starts over.
    void reset_sky() noexcept;
    // The loaded stage's row of the sun table (rt_materials.h; null: none): its colour, strength,
    // disc and shafts apply to the sun the scene is lit by.
    void set_stage_sun(const RtStageSun* sun) noexcept { stage_sun_ = sun; }
    [[nodiscard]] const RtStageSun* stage_sun() const noexcept { return stage_sun_; }
    // Ray-traced lighting of the last traced scene (rt_lighting.hlsl): its result slot for
    // composite(), -1 for none. The interpolated sub-frames of that game frame composite the
    // same slot.
    [[nodiscard]] int light_slot() const noexcept { return light_slot_; }
    [[nodiscard]] bool reflections_active() const noexcept { return lighting_active() && reflections_.ready(); }
    // The traced reflections on the scene's surfaces: metal and gloss (the engine's specular, or the reflection pass
    // without the engine), glass and crystal (the clear pass). Off (RenderConfig::rt_reflections), those surfaces show
    // the game's painted reflections as they do without ray tracing; the water's own pass stays (reflections_active).
    [[nodiscard]] bool surface_reflections_active() const noexcept {
        return reflections_active() && surface_reflections_;
    }
    void set_surface_reflections(bool on) noexcept { surface_reflections_ = on; }
    // After trace_scene, before composite(): the last traced scene's reflections (rt_reflections.h)
    // from the EFB colour and depth as the opaque scene left them (NON_PIXEL_SHADER_RESOURCE, two
    // consecutive SRVs at `color_depth_table` in the bound heap).
    void trace_reflections(ID3D12GraphicsCommandList* list, Allocate allocate, void* allocate_context,
                           D3D12_GPU_DESCRIPTOR_HANDLE color_depth_table, std::uint32_t width, std::uint32_t height);
    // Multiplies the EFB colour `target` (width x height, the size the slot was traced at) by
    // lighting result `slot` at the depth `depth_srv` holds (PIXEL_SHADER_RESOURCE, in the bound
    // descriptor heap), then blends in the slot's reflections. `reproject`: the depth is an
    // interpolated sub-frame's, whose camera write_reprojection gave; `efb_scale`: the internal
    // resolution (EFB pixels per game pixel). Leaves its own root signature and pipeline bound.
    void composite(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE target,
                   D3D12_GPU_DESCRIPTOR_HANDLE depth_srv, D3D12_GPU_DESCRIPTOR_HANDLE color_srv, std::uint32_t width,
                   std::uint32_t height, int slot, bool reproject, unsigned efb_scale);
    // Before a sub-frame's replay: its camera (world to eye at t) and the view its depth is drawn with (`own_view`:
    // projection with the jitter in and depth terms, as scene_depth_view gives a traced frame's; null: the traced
    // frame's), for the composites it runs: they rebuild its points with its own view and take them to the traced frame
    // with the slot's.
    void write_reprojection(ID3D12GraphicsCommandList* list, const std::array<float, 12>& camera_t,
                            const RtView* own_view);
    // At the scene's end: its finished picture (EFB colour and depth, NON_PIXEL_SHADER_RESOURCE, two
    // consecutive SRVs at `color_depth_table` in the bound heap) becomes the light the next frame's rays
    // read where they hit, and the glows drawn since the trace become lights (their eye-space triangles,
    // from a geometry pass on the same sources, and what they added to the picture since
    // snapshot_glow_base()). `game_srv`: the game's own opaque picture (the lighting composite's source;
    // 0: none), whose light past the film shoulder joins the bloom.
    void capture_picture(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                         void* allocate_context, D3D12_GPU_DESCRIPTOR_HANDLE color_depth_table, std::uint32_t width,
                         std::uint32_t height, D3D12_GPU_DESCRIPTOR_HANDLE game_srv = {});
    // The lighting of the last traced scene as a texture (RGBA16F, one texel per sample) for the
    // translucent draws after it: written after the composite, PIXEL_SHADER_RESOURCE otherwise.
    void write_light_texture(ID3D12GraphicsCommandList* list, int slot);
    [[nodiscard]] bool light_texture_ready() const noexcept { return light_tex_ready_; }
    [[nodiscard]] ID3D12Resource* light_texture() const noexcept { return light_tex_.Get(); }
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE light_texture_srv() const noexcept { return light_tex_srv_; }
    [[nodiscard]] float light_texture_scale() const noexcept { return light_tex_scale_; }
    // The camera (ray_tracing_camera.cpp, rt_camera.hlsl). camera_widen: the RGBA8 EFB's picture (`picture_table`:
    // its colour view first, PIXEL_SHADER_RESOURCE) into the HDR scene EFB (`target`) as it is. camera_finish: the
    // HDR scene EFB's light (`picture_table`, NON_PIXEL and PIXEL_SHADER_RESOURCE) through bloom and the tone curve
    // into the RGBA8 EFB (`target`); without physical light the picture as it is, clamped. `slot` is the light slot of
    // the frame the picture shows (-1: none, not traced), whose light and sun the camera takes: a sub-frame replays a
    // frame the tracer has moved past. `subframe`: the sun's shafts turn into the sub-frame's camera (write_reprojection).
    [[nodiscard]] bool camera_ready() const noexcept { return camera_pso_ && camera_widen_pso_; }
    // The ray tracing engine (ray_tracing_engine.cpp, rt_engine.hlsl), active under physical, textured lighting.
    // composite_clear: the engine's clear surfaces (rt_engine.hlsl EngineClearCS) of `slot` over the picture
    // (`scene_srv`: its copy at the water's trace; `layer_srv`: the effects layer over it, colour and transmittance),
    // with the water's composite. set_engine_picture: the EFB colour the next trace reads (NON_PIXEL_SHADER_RESOURCE
    // during it; the renderer transitions it).
    void composite_clear(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE target,
                         D3D12_GPU_DESCRIPTOR_HANDLE depth_srv, std::uint32_t width, std::uint32_t height, unsigned slot,
                         bool reproject, D3D12_GPU_DESCRIPTOR_HANDLE scene_srv,
                         D3D12_GPU_DESCRIPTOR_HANDLE layer_srv = {});
    [[nodiscard]] bool engine_active() const noexcept {
        return engine_ready_ && physical_active_ && lighting_active() && light_textured_;
    }
    // The waterfall mist (rt_engine.hlsl MistLightCS, rt_lighting.hlsl MistCompositePS): the table's `mist` effects'
    // sprites are puffs of drops while the scene's water window is open (mist_open; GALAXY_RT_MIST=0: the sprites as
    // the game draws them). note_mist: one puff, its eye-space centre, width (sigma), optical depth through its centre
    // and brightness (its row's colour, white 1). mist_slot: the light slot whose puffs the water's trace lit this frame (-1: none). composite_mist: them over
    // the picture (`scene_srv`: its copy after the water's composites; `layer_srv`: the effects layer).
    [[nodiscard]] bool mist_open() const noexcept {
        return mist_wanted_ && water_open_ && engine_active() && surface_reflections_active() && engine_mist_pso_ &&
               mist_composite_pso_;
    }
    void note_mist(const float centre[3], float sigma, float depth, float white);
    [[nodiscard]] int mist_slot() const noexcept { return mist_slot_; }
    void composite_mist(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE target,
                        D3D12_GPU_DESCRIPTOR_HANDLE depth_srv, std::uint32_t width, std::uint32_t height, unsigned slot,
                        bool reproject, D3D12_GPU_DESCRIPTOR_HANDLE scene_srv, D3D12_GPU_DESCRIPTOR_HANDLE layer_srv);
    // The engine traces the clear surfaces (its clear pass runs with the water window's trace: the table's `traced`
    // glass and solids).
    [[nodiscard]] bool engine_clear_active() const noexcept {
        return engine_active() && engine_clear_wanted_ && surface_reflections_active();
    }
    void set_engine_picture(ID3D12Resource* picture) noexcept { engine_picture_ = picture; }
    // The raster's motion target (current to previous, render pixels) the next trace reads, or none: the renderer
    // transitions it to NON_PIXEL_SHADER_RESOURCE for the trace.
    void set_engine_motion(ID3D12Resource* motion) noexcept { engine_motion_ = motion; }
    bool camera_widen(ID3D12GraphicsCommandList* list, D3D12_GPU_DESCRIPTOR_HANDLE picture_table,
                      D3D12_CPU_DESCRIPTOR_HANDLE target, std::uint32_t width, std::uint32_t height);
    bool camera_finish(ID3D12GraphicsCommandList* list, D3D12_GPU_DESCRIPTOR_HANDLE picture_table,
                       D3D12_CPU_DESCRIPTOR_HANDLE target, std::uint32_t width, std::uint32_t height, int slot,
                       bool subframe);
    // Transforms the water draws noted since the last flush (before a render chunk's list closes,
    // as flush_geometry).
    void flush_water(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                     void* allocate_context);
    // Transforms the late draws noted since the last flush (as flush_water).
    void flush_late(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                    void* allocate_context);
    // Transforms the sky draws noted since the last flush (as flush_water).
    void flush_sky(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                   void* allocate_context);
    // After the translucent phases: the water draws noted since the trace become their own
    // acceleration structure and the reflections trace the water's surface from the picture (EFB
    // colour then depth, NON_PIXEL_SHADER_RESOURCE, two consecutive SRVs at `color_depth_table`).
    // Ends the water window. Returns the result slot for composite_water(), -1 for none.
    int trace_water(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                    void* allocate_context, D3D12_GPU_DESCRIPTOR_HANDLE color_depth_table, std::uint32_t width,
                    std::uint32_t height);
    // Blends the water reflections of `slot` into the EFB colour `target` at the depth `depth_srv`
    // holds (PIXEL_SHADER_RESOURCE); `reproject` as composite(). Leaves its own root signature and
    // pipeline bound.
    // `scene_srv`: the frame with the game's water drawn (a copy), which the water composite low-passes;
    // `layer_srv`: the effects layer drawn over the water and the clear surfaces (colour and transmittance; 0: none).
    void composite_water(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE target,
                         D3D12_GPU_DESCRIPTOR_HANDLE depth_srv, std::uint32_t width, std::uint32_t height, int slot,
                         bool reproject, D3D12_GPU_DESCRIPTOR_HANDLE scene_srv = {},
                         D3D12_GPU_DESCRIPTOR_HANDLE opaque_srv = {}, D3D12_GPU_DESCRIPTOR_HANDLE layer_srv = {});
    // After the composite (same views): the picture before the translucent phases; with `game_srv`
    // (the composite's source: the game's own opaque picture) the exposure's measurement.
    void snapshot_glow_base(ID3D12GraphicsCommandList* list, D3D12_GPU_DESCRIPTOR_HANDLE color_depth_table,
                            D3D12_GPU_DESCRIPTOR_HANDLE game_srv, std::uint32_t width, std::uint32_t height);
    [[nodiscard]] bool emitters_active() const noexcept { return lighting_active() && emission_ > 0.0f; }
    // The colour scale of glows (additive scene draws) while the lighting is on, else 1.
    // Glow models (indexed meshes: the Observatory core's fire) and glow sprites (direct vertices:
    // halos, sparks) have their own exposure and spill.
    [[nodiscard]] float glow_exposure(bool model) const noexcept {
        return lighting_active() ? (model ? glow_model_exposure_ : glow_exposure_) : 1.0f;
    }

private:
    static constexpr unsigned kResultSlots = 16u;
    static constexpr std::uint64_t kResultDelay = 8u;
    static constexpr std::uint32_t kEmitLogLights = 512u;   // the whole list (ray_tracing.cpp kEmitLights)
    static constexpr std::size_t kAuthoredLights = 512u;
    // Physical: the scene's emitting triangles at most (rt_lighting.hlsl kEmitTriangles).
    static constexpr std::uint32_t kEmitTriangles = 65536u;
    // Physical glow light sources (rt_lighting.hlsl GlowEmitterCS, kGlowEmitters): the table's glow draws' triangles in
    // the emitters' buffer after the scene's, in world space, lit from the next frame.
    static constexpr std::uint32_t kGlowEmitters = 16384u;
    // Offset of the eye's adaptation in the picture sums (ray_tracing.cpp kPictureStarColour + 16; rt_lighting.hlsl
    // kAdaptation): its factor and whether it is known.
    static constexpr std::uint64_t kPictureAdaptation = 208u;
    // g_emit_tris: header, 4 per scene emitter, the running power (scene then glow), 4 per glow emitter, glow powers.
    static constexpr std::uint64_t kEmitBufferBytes =
        (1ull + 5ull * kEmitTriangles + 6ull * kGlowEmitters) * 16u;
    // A light in the glow lights' lists (rt_lighting.hlsl kEmitStride): position, radius; power,
    // frames unseen or cumulative power; spot direction, cosine of its cone (0 0 0 for a point light).
    static constexpr std::uint32_t kEmitStride = 48u;
    static constexpr std::uint64_t kEmitLogBytes = 16u + kEmitLogLights * kEmitStride;
    static constexpr std::uint64_t kEmitLogSums = 64u;   // the picture sums after them
    // Then this frame's found lights (rt_lighting.hlsl kEmitFound) with each one's source (kEmitSource*) and identity.
    static constexpr std::uint32_t kEmitFound = 2048u;
    static constexpr std::uint64_t kEmitFoundBytes = 16u + std::uint64_t{kEmitFound} * (kEmitStride + 8u);
    static constexpr unsigned kStatsWords = 16u;
    static constexpr std::uint32_t kMaxTriangles = 4u * 1024u * 1024u;
    // Translucent water triangles of one scene.
    static constexpr std::uint32_t kWaterMaxTriangles = 128u * 1024u;
    // Late opaque triangles of one scene (kRtLatePhase).
    static constexpr std::uint32_t kLateMaxTriangles = 128u * 1024u;
    // Lighting results kept for the sub-frames of the game frames being interpolated.
    static constexpr unsigned kLightSlots = 4u;

    struct Retired {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        std::uint64_t frame = 0;
    };
    struct ResultSlot {
        std::uint64_t frame = 0;
        bool pending = false;
        bool validated = false;
        std::uint32_t draws = 0;
        std::uint32_t triangles = 0;
    };
    // GALAXY_RT_PASS_TIMING=1: GPU time of the passes outside trace_scene's timestamps ([rt-pass]).
    // The finish's compute passes then run one after another (a barrier after each) to be timed alone.
    enum Pass : std::uint32_t {
        kPassGeometry,
        kPassComposite,
        kPassGlowBase,
        kPassCapture,
        kPassPostCopy,
        kPassPostBright,
        kPassPostFocus,
        kPassPostShafts,
        kPassPostBlur,
        kPassPostFinish,
        kPassBloom,
        kPassLights,        // inside the capture: the glow and table lights found and merged
        kPassBloomBlur,     // inside the capture: the bloom's downsample and blurs
        kPassEmit,          // inside the lights: the glow and table light passes
        kPassMerge,         // inside the lights: the merge into the remembered lights
        kPassCamera,        // the camera at the scene's end (bloom, tone curve)
        kPassEngineGBuffer,     // inside the lighting: the engine's passes (rt_engine.hlsl)
        kPassEngineDirect,      // ReSTIR DI, temporal and spatial
        kPassEngineShadow,      // the sun shadow's two blurs and its stabilisation
        kPassEngineIndirect,    // ReSTIR GI, temporal and spatial (half resolution)
        kPassEngineSpecular,
        kPassEngineDenoise,     // temporal accumulation and the four a-trous iterations
        kPassEngineFinal,
        kPassEngineClear,       // the glass pass at the water's trace (outside the lighting)
        kPassEngineMist,        // the mist's light at the water's trace (outside the lighting)
        kPassMistComposite,     // the mist over the picture
        kPassEngineKernel,      // GALAXY_RT_PASS_TIMING=2: the engine's dispatches in their order (kEngineKernels)
        kPassCount = kPassEngineKernel + 24
    };
    static constexpr unsigned kEngineKernels = kPassCount - kPassEngineKernel;
    struct PassEntry {
        std::uint64_t frame = 0;
        std::uint32_t pass = 0;
        bool pending = false;
    };
    static constexpr unsigned kPassEntries = 512u;

    // `preserved`: triangles earlier chunks of the open scene wrote, kept when the buffers grow.
    bool ensure_capacity(ID3D12GraphicsCommandList* list, std::uint32_t triangles, std::uint32_t preserved);
    bool fill_constants(const RtSources& sources, Allocate allocate, void* allocate_context, std::uint32_t draws,
                        std::uint32_t triangle_base, std::uint32_t triangles, std::uint32_t width,
                        std::uint32_t height, std::uint32_t flags, RtUpload& frame);
    void write_dump();
    void log_emitters();
    void log_physical();
    bool create_lighting_pipelines(DXGI_FORMAT scene_format);
    bool create_camera_pipelines(DXGI_FORMAT scene_format, DXGI_FORMAT display_format);
    bool create_engine_pipelines();
    bool ensure_engine_buffers(std::uint32_t width, std::uint32_t height);
    // The engine's views and constants for this trace (false: it does not run, the scene is not traced).
    bool engine_prepare(std::uint32_t width, std::uint32_t height, unsigned slot, RtEngineConstants& c,
                        std::uint32_t& flags);
    // The light remap (rt_engine.hlsl engine_light_remap): this trace's lights by their draws' keys (the scene's
    // emitting triangles, then the glow emitters from `scene`, the point lights' `points`) against the last traced
    // frame's, uploaded; its view, or 0xFFFFFFFF. light_remap_commit keeps this trace's lights as the last (the
    // engine ran), or forgets them (it did not: the next trace keeps no light's history).
    struct LightGroup {
        std::uint32_t key;
        std::uint32_t first;   // its first light index (rt_lighting.hlsl emit_triangle_of)
        std::uint32_t count;
    };
    std::uint32_t light_remap(Allocate allocate, void* allocate_context, std::uint32_t scene, std::uint32_t lights,
                              const std::uint32_t* points, std::uint32_t point_count);
    void light_remap_commit(bool ran);
    std::vector<LightGroup> light_groups_;        // this trace's
    std::vector<LightGroup> light_groups_last_;   // the last traced frame's (empty: none)
    std::vector<std::uint32_t> light_points_;
    std::vector<std::uint32_t> light_points_last_;
    std::uint32_t light_remap_view_ = 0xFFFFFFFFu;
    // GALAXY_RT_LIGHT_METER=x,y,r (fractions of the picture's width and height, r of its width): per traced frame the
    // light on the ordinary surfaces in that disc ([rt-light-meter]).
    bool light_meter_ = false;
    std::array<float, 3> light_meter_disc_{};
    // GALAXY_RT_LOG_INTERVAL: frames between the emitter logs (GALAXY_RT_LOG_EMITTERS; 120).
    std::uint64_t emit_log_interval_ = 120u;
    std::uint64_t light_remap_traces_ = 0u;
    // Records the engine's passes. The frame's constants (`constants`, `bytes`, the engine block at `engine_at`) are
    // re-uploaded per a-trous step, and `frame` is rebound after.
    void trace_engine(ID3D12GraphicsCommandList* list, Allocate allocate, void* allocate_context, const void* constants,
                      std::size_t bytes, std::size_t engine_at, D3D12_GPU_VIRTUAL_ADDRESS frame, unsigned slot);
    // Slot `slot` holds the engine's result for a width x height frame.
    [[nodiscard]] bool engine_slot(unsigned slot, std::uint32_t width, std::uint32_t height) const noexcept;
    // The clear-surface pass (rt_engine.hlsl EngineClearCS) at the scene's end: the water window's glass and crystal by
    // their own geometry into slot `slot`'s clear buffer, the lighting's bindings of the slot's trace restored
    // (engine_bind_); `depth_table`: the EFB depth's view. False: none this scene.
    bool trace_engine_clear(ID3D12GraphicsCommandList* list, Allocate allocate, void* allocate_context,
                            D3D12_GPU_DESCRIPTOR_HANDLE depth_table, unsigned slot);
    // The lighting's root as the engine's trace of slot engine_bind_.slot bound it, with the constants at `constants` (a
    // copy of engine_bind_'s): for the passes at the scene's end (the clear pass, the mist's light).
    void bind_engine_end(ID3D12GraphicsCommandList* list, D3D12_GPU_VIRTUAL_ADDRESS constants,
                         D3D12_GPU_DESCRIPTOR_HANDLE depth_table);
    // The mist's light (rt_engine.hlsl MistLightCS) at the water's trace: this frame's puffs into the light slot's
    // records; sets mist_slot_. `water`: the water window's acceleration structure holds this frame's water.
    void trace_mist(ID3D12GraphicsCommandList* list, Allocate allocate, void* allocate_context,
                    D3D12_GPU_DESCRIPTOR_HANDLE depth_table, bool water);
    // A view of an acceleration structure in the scene's descriptor block.
    std::uint32_t tlas_view(D3D12_GPU_VIRTUAL_ADDRESS tlas);
    std::uint32_t buffer_uav(ID3D12Resource* resource, std::uint64_t bytes);
    std::uint32_t texture_srv(ID3D12Resource* resource);
    bool ensure_camera_pyramid(std::uint32_t width, std::uint32_t height);
    void bind_camera(ID3D12GraphicsCommandList* list, bool graphics_root, D3D12_GPU_DESCRIPTOR_HANDLE picture_table);
    // The geometry pass over `count` draws (triangles from `triangle_base`) into `positions` and
    // `surfaces`, and their texture coordinates and colours into `uv` where given.
    bool transform_draws(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                         void* allocate_context, const RtDraw* draws, std::size_t count, std::uint32_t triangle_base,
                         std::uint32_t triangles, ID3D12Resource* positions, D3D12_RESOURCE_STATES& positions_state,
                         ID3D12Resource* surfaces, D3D12_RESOURCE_STATES& surfaces_state, ID3D12Resource* uv = nullptr,
                         D3D12_RESOURCE_STATES* uv_state = nullptr);
    // The reflections' view of the scene traced into lighting slot `slot`.
    [[nodiscard]] RtReflectScene reflect_scene(unsigned slot) const;
    bool ensure_water_buffers();
    bool ensure_late_buffers();
    bool ensure_sky_buffers();
    // Ends the sky window: the sky draws noted this scene become sky_blas_ in sky_tlas_ (sky_built_; none: the
    // traced sky is off this frame).
    void finish_sky(ID3D12GraphicsCommandList4* list, const RtSources& sources, Allocate allocate,
                    void* allocate_context);
    // Ends the late window: the late draws noted this scene become late_blas_ (none: no late instance).
    void finish_late(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                     void* allocate_context);
    // The scene TLAS: the frame's geometry and, when built, the late geometry moved from its camera to this
    // one (instance 1, mask kLateMask: shadow, sky and reflection rays only).
    bool build_tlas(ID3D12GraphicsCommandList4* list, Allocate allocate, void* allocate_context);
    void capture_glow_lights(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                             void* allocate_context, D3D12_GPU_DESCRIPTOR_HANDLE color_depth_table,
                             std::uint32_t width, std::uint32_t height, std::uint32_t factor, bool glow);
    bool ensure_lighting_buffers(std::uint32_t samples);
    // EFB pixels per sample side for a width x height target.
    [[nodiscard]] std::uint32_t sample_factor(std::uint32_t width, std::uint32_t height) const noexcept;
    void trace_lighting(ID3D12GraphicsCommandList* list, Allocate allocate, void* allocate_context,
                        D3D12_GPU_DESCRIPTOR_HANDLE depth_srv, std::uint32_t width, std::uint32_t height);
    static void to_state(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES& state,
                         D3D12_RESOURCE_STATES wanted);
    bool create_buffer(std::uint64_t bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state,
                       D3D12_RESOURCE_FLAGS flags, Microsoft::WRL::ComPtr<ID3D12Resource>& out, const wchar_t* name);
    void retire(Microsoft::WRL::ComPtr<ID3D12Resource>& resource);
    // The pass's ring entry (-1: not timed); end_pass resolves both timestamps on the same list.
    int begin_pass(ID3D12GraphicsCommandList* list);
    void end_pass(ID3D12GraphicsCommandList* list, int entry, Pass pass);
    void collect_passes(std::uint64_t frame_index);
    void report();

    Microsoft::WRL::ComPtr<ID3D12Device5> device_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> geometry_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> validate_pso_;
    Microsoft::WRL::ComPtr<ID3D12Resource> positions_;
    // Per triangle, 16 bytes (ray_tracing.hlsl g_surfaces): vertex normals, draw flags.
    Microsoft::WRL::ComPtr<ID3D12Resource> surfaces_;
    Microsoft::WRL::ComPtr<ID3D12Resource> blas_;
    Microsoft::WRL::ComPtr<ID3D12Resource> tlas_;
    Microsoft::WRL::ComPtr<ID3D12Resource> scratch_;
    Microsoft::WRL::ComPtr<ID3D12Resource> stats_;
    Microsoft::WRL::ComPtr<ID3D12Resource> stats_zero_;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> timestamps_;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> pass_timestamps_;
    Microsoft::WRL::ComPtr<ID3D12Resource> pass_readback_;
    std::array<PassEntry, kPassEntries> pass_entries_{};
    unsigned next_pass_entry_ = 0;
    bool kernel_timing_ = false;
    std::array<const wchar_t*, kEngineKernels> kernel_names_{};
    std::vector<Retired> retired_;
    // GALAXY_RT_DUMP=<scene serial>: that scene's per-pixel validation is written as PPM
    // images to GALAXY_RT_DUMP_DIR (class, raster depth, traced depth).
    Microsoft::WRL::ComPtr<ID3D12Resource> dump_;
    Microsoft::WRL::ComPtr<ID3D12Resource> dump_readback_;
    std::uint64_t dump_scene_ = 0;
    // GALAXY_RT_DUMP_EVERY / _COUNT: further dumps every n scenes.
    std::uint64_t dump_every_ = 0;
    std::uint64_t dump_remaining_ = 0;
    std::array<float, 12> dump_camera_{};
    bool dump_camera_known_ = false;
    std::uint64_t dump_frame_ = 0;
    std::uint32_t dump_width_ = 0;
    std::uint32_t dump_height_ = 0;
    bool dump_pending_ = false;
    std::vector<RtDraw> dump_draws_;
    std::vector<RtDrawInfo> infos_;
    std::vector<RtDrawInfo> dump_infos_;
    std::vector<RtDraw> draws_;
    // Per draw of the open scene (RtDraw.flags bits 16-31; rt_lighting.hlsl g_draws, kDrawStride):
    // ambient and GX light 0 colours (RGBA8), the material table's emission (RGBA8 with its kind)
    // and intensity; roughness and metalness (unorm16 pair, 0xFFFF: the class's), the material colour
    // register (RGBA8), flags (kDrawTableTint), the relief; GX light 0's eye-space position (3 floats) and
    // its kind (0: none or attenuated, 1: no facing term, 2: n.l) for the day model's game shading; its fog
    // (RtDrawInfo::fog).
    std::vector<std::array<std::uint32_t, 16>> draw_colors_;
    // Per draw for high quality's textured hits (rt_reflections.hlsl g_draw_tex): texture view, colour
    // source (bit 0 vertex colour), its colour RGBA8, alpha test (the alpha it passes from in the high 8 bits,
    // the alpha map's view); and its upload.
    std::vector<std::array<std::uint32_t, 4>> draw_textures_;
    // The views g_draw_tex names: each scene's own block (set_texture_views), copied from the texture cache as
    // its draws arrive and kept until the GPU is done with the scene. The frame's descriptor ring rewinds its
    // tables at every renderer frame, while a scene's traces read its draws' textures renderer frames later.
    struct TextureViewBlock {
        std::uint64_t fence = 0;   // the submission after its scene closed (0: none)
        bool closed = false;       // its scene closed, not yet submitted
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> resources;
    };
    struct TextureViewSlot {
        std::uint32_t index = 0;
        ID3D12Resource* resource = nullptr;
    };
    [[nodiscard]] std::uint32_t texture_view(const RtTextureSource& source);
    static void dred_marker(ID3D12GraphicsCommandList* list, const wchar_t* name);
    // The engine's views of the clear-surface pass follow the trace's (rt_engine.hlsl kEvWaterScene .. kEvClear;
    // ray_tracing_engine.cpp asserts it against its views).
    static constexpr std::uint32_t kEngineClearViews = 23u;
    // A raw (ByteAddressBuffer) view of `bytes` of `resource` from `offset` among this scene's views; ~0 when none is
    // free or the views are unusable.
    [[nodiscard]] std::uint32_t buffer_view(ID3D12Resource* resource, std::uint64_t offset, std::uint64_t bytes);
    // Physical light for the reflections' off-screen hits (RtReflectScene::physical), from this scene's lighting.
    RtReflectPhysical reflect_physical_{};
    // Physical: the camera's exposure for the stage's light (RtLightConstants light_fit x) for the eye's adaptation.
    float physical_exposure_ = 0.0f;
    D3D12_CPU_DESCRIPTOR_HANDLE texture_view_cpu_{};
    std::uint32_t texture_view_first_ = 0;
    UINT texture_view_increment_ = 0;
    unsigned texture_view_scene_ = 0;
    std::uint32_t texture_view_count_ = 0;
    bool texture_views_usable_ = false;
    bool texture_view_full_logged_ = false;
    std::array<TextureViewBlock, kTextureViewScenes> texture_view_blocks_{};
    std::unordered_map<SIZE_T, TextureViewSlot> texture_view_slots_;
    D3D12_GPU_VIRTUAL_ADDRESS draw_texture_table_ = 0;
    // Physical: texture de-lighting (rt_delight.hlsl). Per texture-pack key the shading painted into the texture
    // (R8_SNORM log units, mips), made on the GPU in the trace of the scene that first shows the texture as a traced
    // draw's material and read from the next scene on (by EngineGBufferCS for the composite, by the hits' TEV). Kept
    // across scenes; past kDelightBytes or the views the least recently drawn go. GALAXY_RT_DELIGHT=0: off.
    struct DelightMap {
        Microsoft::WRL::ComPtr<ID3D12Resource> texture;
        std::uint32_t slot = 0;      // its view: delight_view_first_ + slot
        std::uint64_t bytes = 0;
        std::uint64_t used = 0;      // the last scene that drew it
        bool ready = false;          // its job is recorded
    };
    struct DelightJob {
        std::uint64_t pack = 0;
        std::uint32_t source = 0;    // the texture's view in this scene's block
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t source_levels = 1;
        std::uint32_t levels = 1;
        std::uint32_t wrap = 5u;     // RtDrawInfo.texture_wrap of the draw that made it
    };
    static constexpr std::uint64_t kDelightBytes = 256ull << 20u;
    static constexpr std::uint64_t kDelightScratchBytes = 8ull << 20u;
    static constexpr std::uint64_t kDelightTexelsPerFrame = 1ull << 20u;
    [[nodiscard]] std::uint32_t delight_view(const RtTextureSource& source, std::uint32_t wrap);
    void run_delight(ID3D12GraphicsCommandList* list);
    void reset_delight() noexcept;
    bool delight_ = true;
    float delight_relief_ = 2.0f;    // GALAXY_RT_DELIGHT_RELIEF: relief height in texels per log unit of shading
    D3D12_CPU_DESCRIPTOR_HANDLE delight_view_cpu_{};
    std::uint32_t delight_view_first_ = 0;
    UINT delight_view_increment_ = 0;
    std::unordered_map<std::uint64_t, DelightMap> delight_maps_;
    std::vector<DelightJob> delight_jobs_;
    std::vector<std::uint32_t> delight_free_;
    std::vector<std::pair<std::uint32_t, std::uint64_t>> delight_retired_slots_;   // slot, frame
    std::uint64_t delight_bytes_ = 0;
    std::uint64_t delight_made_ = 0;
    std::array<std::uint64_t, 6> delight_seen_{};   // [rt-delight] draw counts since the last report
    Microsoft::WRL::ComPtr<ID3D12Resource> delight_scratch_;
    D3D12_RESOURCE_STATES delight_scratch_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> delight_root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> delight_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> delight_mip_pso_;
    // High quality: per triangle its texture coordinates and colours (ray_tracing.hlsl g_tri_uv).
    Microsoft::WRL::ComPtr<ID3D12Resource> surface_uv_;
    D3D12_RESOURCE_STATES surface_uv_state_ = D3D12_RESOURCE_STATE_COMMON;
    // This scene's draw table in the frame's upload ring (trace_lighting), for its reflections; 0: none.
    D3D12_GPU_VIRTUAL_ADDRESS draw_table_ = 0;
    std::array<ResultSlot, kResultSlots> slots_{};
    RtView view_{};
    D3D12_RESOURCE_STATES positions_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES surfaces_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    std::uint64_t timestamp_frequency_ = 0;
    std::uint64_t frame_index_ = 0;
    std::uint64_t scene_serial_ = 0;
    std::uint64_t tlas_bytes_ = 0;
    std::uint64_t blas_bytes_ = 0;
    std::uint64_t scratch_bytes_ = 0;
    std::uint32_t capacity_ = 0;
    std::uint32_t triangles_ = 0;
    // Draws and triangles already transformed this scene.
    std::size_t flushed_draws_ = 0;
    std::uint32_t flushed_triangles_ = 0;
    bool ready_ = false;
    bool validate_ = false;
    bool view_known_ = false;
    bool scene_open_ = false;
    // A chunk of the open scene could not be transformed: it is not traced.
    bool scene_incomplete_ = false;

    // Ray-traced lighting (rt_lighting.hlsl). GALAXY_RT_LIGHT=0 turns it off; GALAXY_RT_RAYS (per
    // sample), _RANGE (eye units), _STRENGTH (0-1), _HISTORY (frames), _FACTOR (EFB pixels per
    // sample side, 1-4; by default 2, 3 above 6.2 and 4 above 16 megapixels), _CONTACT (light of
    // a hit at distance 0), _CONTACT_RANGE, _SKY (sky hue 0-1), _FALLBACK, _EMISSIVE, the sun's
    // world direction _SUN (x,y,z), _SUN_SIZE (degrees), _SUN_LIGHT, _SUN_COLOR (r,g,b), the sky
    // light's weight _SKY_LIGHT, the point lights' _POINT_LIGHT and _POINT_RANGE, and _VIEW=1
    // (the lighting alone; 2, 3, 4: the sun's, the sky's, the point lights' light; 5: the glow
    // terms; 6: the distance to what shadows the sun, 0.05 + 0.45 log10(eye units) / 6 on screen)
    // tune it.
    struct LightSlot {
        RtView view{};
        std::array<float, 12> camera{};
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t sample_width = 0;
        std::uint32_t sample_height = 0;
        std::uint32_t factor = 0;
        float near_plane = 0.0f;
        bool camera_known = false;
        std::array<float, 3> sun{};        // towards the traced sun (eye space)
        std::array<float, 3> sun_visible{};  // towards the sun the sky shows (eye space): the shafts' centre
        std::array<float, 3> sun_light{};  // its light
        bool sun_shown = false;            // the sky shows it (the game's BrightSun, or the sun its sky map holds)
        bool physical = false;             // traced in the stage's light units (physical_active_ then)
        bool pixels = false;               // light_pixels_ holds its per-pixel normals (physical)
        std::array<float, 4> water{};      // the camera in water (1), its depth (RtEngineConstants::water)
        std::array<float, 3> water_sky{};  // the stage's full light it scatters (the lighting's full_light)
    };
    Microsoft::WRL::ComPtr<ID3D12RootSignature> light_root_;
    bool physical_active_ = false;  // this frame had the game's area light (else it is not traced)
    // GALAXY_RT_REFERENCE_FROM: the present the engine's reference (GALAXY_RT_ENGINE_REFERENCE) starts at.
    std::uint64_t reference_from_ = 0;
    std::uint64_t presents_ = 0;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> capture_root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> capture_pso_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> composite_root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> composite_pso_;
    // Per sample: eye z (this frame and the previous), packed normal, raw light (then the first
    // filter's), accumulated light and frames (both frames); per result slot the light and eye
    // z; the previous picture's light and eye z, and its sky and scene sums.
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> light_z_;
    Microsoft::WRL::ComPtr<ID3D12Resource> light_normal_;
    Microsoft::WRL::ComPtr<ID3D12Resource> light_raw_;
    // Per sample: the camera ray's triangle + 1 (0: none) and its barycentrics (halves).
    Microsoft::WRL::ComPtr<ID3D12Resource> light_primary_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kLightSlots> light_final_;
    // Per light slot its traced frame's per-pixel shading normals and de-lighting (rt_engine.hlsl EngineGBufferCS).
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kLightSlots> light_pixels_;
    // Physical: the stage's full light in linear light (this frame's; per slot, its frame's), 1 otherwise.
    std::array<float, 3> light_full_{1.0f, 1.0f, 1.0f};
    std::array<float, 3> sky_fill_{};   // the ambient's fill as the picture shows it (RtReflectScene::sky_fill)
    std::array<std::array<float, 3>, kLightSlots> light_slot_full_{};
    std::array<std::uint64_t, kLightSlots> light_pixels_bytes_{};
    std::array<D3D12_RESOURCE_STATES, kLightSlots> light_pixels_state_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> picture_;
    Microsoft::WRL::ComPtr<ID3D12Resource> picture_sums_;
    // The exposure's measurement (lit and game luminance per bucket, snapshot_glow_base); always a UAV.
    Microsoft::WRL::ComPtr<ID3D12Resource> exposure_sums_;
    bool exposure_reset_ = true;
    // Glows (rt_lighting.hlsl GlowBaseCS, CaptureCS, GlowEmitCS, EmitMergeCS): per sample the
    // picture before the translucent phases and what they added; the glow draws' eye-space
    // triangles; this frame's glow lights; the remembered lights and their list for sampling.
    Microsoft::WRL::ComPtr<ID3D12Resource> glow_base_;
    Microsoft::WRL::ComPtr<ID3D12Resource> glow_added_;
    Microsoft::WRL::ComPtr<ID3D12Resource> glow_positions_;
    Microsoft::WRL::ComPtr<ID3D12Resource> glow_surfaces_;
    // Their texture coordinates and colours (kRtTriUvStride a triangle, as the scene's: GeometryCS), for the light of a
    // glow emitter that emits what it draws (GlowEmitterCS).
    Microsoft::WRL::ComPtr<ID3D12Resource> glow_uv_;
    Microsoft::WRL::ComPtr<ID3D12Resource> emit_tiles_;
    Microsoft::WRL::ComPtr<ID3D12Resource> emit_lights_;
    Microsoft::WRL::ComPtr<ID3D12Resource> emit_list_;
    Microsoft::WRL::ComPtr<ID3D12Resource> emit_readback_;   // GALAXY_RT_LOG_EMITTERS: the list's head
    Microsoft::WRL::ComPtr<ID3D12RootSignature> emit_root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> glow_base_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> sky_state_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> surface_emit_pso_;
    // The sky light (rt_lighting.hlsl SkyLightMapCS, SkyLightShCS): the captures' sky bins over time
    // (float4 per bin: colour, seen) and their spherical harmonics, which the rays that reach the
    // sky take their light from.
    Microsoft::WRL::ComPtr<ID3D12PipelineState> sky_light_map_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> sky_light_sh_pso_;
    // The lighting texture for lit translucents (rt_lighting.hlsl LightTexturePS).
    Microsoft::WRL::ComPtr<ID3D12RootSignature> light_tex_root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> light_tex_pso_;
    Microsoft::WRL::ComPtr<ID3D12Resource> light_tex_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> light_tex_rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> light_tex_srv_heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE light_tex_srv_{};
    D3D12_RESOURCE_STATES light_tex_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    std::uint32_t light_tex_width_ = 0u;
    std::uint32_t light_tex_height_ = 0u;
    float light_tex_scale_ = 0.5f;
    bool light_tex_ready_ = false;
    // The camera (ray_tracing_camera.cpp): its passes, the bloom pyramid's levels down and up (half-float RGB per
    // texel), GALAXY_RT_CAMERA_BLOOM (the share of the light the lens scatters), GALAXY_RT_TONEMAP (0 Khronos PBR
    // Neutral, 1 ACES, 2 clipped).
    Microsoft::WRL::ComPtr<ID3D12RootSignature> camera_root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> camera_down_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> camera_up_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> camera_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> camera_widen_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> camera_shafts_pso_;
    float camera_shafts_ = 1.0f;   // GALAXY_RT_CAMERA_SHAFTS: the sun shafts' strength (times the stage's rt-suns shafts)
    // The engine.
    enum EngineBuffer : unsigned {
        kGb0, kGb1, kDiTemp, kDi0, kDi1, kDirect, kGiTemp, kGi0, kGi1, kGiOut, kSpecular, kHistD0, kHistD1, kHistS0,
        kHistS1, kAtrousA, kAtrousB, kShadow0, kShadow1, kEngineBuffers,
    };
    struct EngineBufferSpec {
        std::uint32_t bytes;   // per pixel, or per half-resolution sample
        bool half;
        const wchar_t* name;
    };
    static const EngineBufferSpec kEngineBufferSpecs[kEngineBuffers];
    bool engine_ready_ = false;
    bool engine_running_ = false;   // this trace runs it
    bool engine_indirect_ = true;   // GALAXY_RT_ENGINE_INDIRECT
    bool engine_specular_ = true;   // GALAXY_RT_ENGINE_SPECULAR
    bool engine_points_ = true;     // GALAXY_RT_ENGINE_POINTS: the game's point lights and the star core
    bool engine_bounces_ = true;    // GALAXY_RT_ENGINE_BOUNCES: indirect hits take their cached light (every bounce)
    bool engine_anti_lag_ = true;   // GALAXY_RT_ENGINE_ANTILAG: the histories' anti-lag
    bool engine_light_history_ = true;   // GALAXY_RT_ENGINE_LIGHT_HISTORY=0: no temporal light reuse
    bool engine_clear_wanted_ = true;  // GALAXY_RT_ENGINE_CLEAR: the clear-surface pass
    bool surface_reflections_ = true;  // RenderConfig::rt_reflections (surface_reflections_active)
    bool engine_boiling_ = true;  // GALAXY_RT_ENGINE_BOILING=0: no boiling filter (rt_engine.hlsl boiling)
    // GALAXY_RT_ENGINE_FOOTPRINT=0: the filters' and the reuse's widths in pixels whatever the lines (kFootprintLines).
    bool engine_footprint_ = true;
    bool engine_motion_wanted_ = true;  // GALAXY_RT_ENGINE_MOTION=0: the histories by the camera's reprojection alone
    std::uint32_t engine_clear_mode_ = 1u;  // GALAXY_RT_ENGINE_CLEAR: 0 off, 1 on, 2-10 diagnostics (rt_engine.hlsl)
    bool engine_fresh_ = true;      // new buffers: no history
    std::uint32_t engine_view_ = 0;  // GALAXY_RT_ENGINE_VIEW (rt_engine.hlsl kEngineView)
    std::uint32_t engine_width_ = 0;
    std::uint32_t engine_height_ = 0;
    unsigned engine_parity_ = 0;
    ID3D12Resource* engine_picture_ = nullptr;
    ID3D12Resource* engine_motion_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> engine_gbuffer_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> engine_direct_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> engine_direct_spatial_pso_;
    // The table metals' painted colour per draw (rt_engine.hlsl EnginePaintClearCS, EnginePaintCS: kEnginePaintDraws
    // records of 16 bytes), so each material is metal or paint as a whole.
    Microsoft::WRL::ComPtr<ID3D12PipelineState> engine_paint_clear_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> engine_paint_pso_;
    Microsoft::WRL::ComPtr<ID3D12Resource> engine_paint_;
    std::uint32_t engine_paint_triangles_ = 0;   // this trace's triangles for EnginePaintCS (0: not run)
    // The light grid (rt_engine.hlsl EngineLightGridCS: kGridCells^3 cells x kGridSlots slots x 8 bytes).
    Microsoft::WRL::ComPtr<ID3D12PipelineState> engine_light_grid_pso_;
    Microsoft::WRL::ComPtr<ID3D12Resource> engine_light_grid_;
    // The world's radiance cache (rt_engine.hlsl EngineCacheCS; GALAXY_RT_ENGINE_CACHE=0: none).
    Microsoft::WRL::ComPtr<ID3D12PipelineState> engine_cache_pso_;
    Microsoft::WRL::ComPtr<ID3D12Resource> engine_radiance_cache_;
    bool engine_cache_wanted_ = true;
    bool engine_firefly_ = true;  // GALAXY_RT_ENGINE_FIREFLY=0: no firefly clamp on the histories' new samples
    bool engine_di_spatial_ = true;  // GALAXY_RT_ENGINE_DI_SPATIAL=0: no spatial reuse of the direct light
    // The indirect light at every pixel (High; the engine's buffers are made for it, ensure_engine_buffers).
    bool engine_gi_full_ = false;
    int engine_gi_full_env_ = -1;    // GALAXY_RT_ENGINE_GI_FULL: -1 by quality, 0 half, 1 full
    Microsoft::WRL::ComPtr<ID3D12PipelineState> engine_shadow_blur_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> engine_shadow_post_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> engine_shadow_stabilize_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> engine_indirect_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> engine_indirect_spatial_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> engine_specular_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> engine_temporal_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> engine_atrous_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> engine_final_pso_;
    // GALAXY_RT_ENGINE_REFERENCE: the converged reference (rt_engine.hlsl EngineReferenceCS) from present
    // GALAXY_RT_REFERENCE_FROM on, one path a pixel a present, GALAXY_RT_ENGINE_REFERENCE_BOUNCES (8) bounces;
    // [rt-reference-engine] prints the engine's diffuse light against it.
    Microsoft::WRL::ComPtr<ID3D12PipelineState> engine_reference_pso_;
    Microsoft::WRL::ComPtr<ID3D12Resource> engine_reference_sums_;
    bool engine_reference_ = false;
    // Light under water (GALAXY_RT_WATER_LIGHT, default on): the triangles of the water the last water pass built (its
    // acceleration structure stays until the next one), 0: none.
    bool water_light_ = true;
    std::uint32_t water_light_triangles_ = 0u;
    bool engine_reference_running_ = false;
    std::uint32_t engine_reference_bounces_ = 8u;
    std::uint64_t engine_reference_pixels_ = 0u;
    std::uint64_t engine_reference_paths_ = 0u;
    std::uint64_t engine_reference_present_ = ~0ull;
    std::uint64_t engine_reference_printed_ = 0u;
    std::array<std::uint64_t, 8> engine_reference_taken_paths_{};
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kEngineBuffers> engine_buffers_;
    // Per light slot: the engine's result (rt_engine.hlsl kEvSlot), read by the composite.
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 4> engine_slots_;
    std::array<D3D12_RESOURCE_STATES, 4> engine_slot_state_{};
    std::array<bool, 4> engine_slot_valid_{};
    // The clear-surface pass: its pipelines; per light slot the clear surfaces' reflections and transmittance
    // (rt_engine.hlsl kEvClear, 16 bytes a pixel), made when a scene first has clear surfaces.
    Microsoft::WRL::ComPtr<ID3D12PipelineState> engine_clear_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> clear_composite_pso_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 4> engine_clear_;
    std::array<D3D12_RESOURCE_STATES, 4> engine_clear_state_{};
    std::array<bool, 4> engine_clear_valid_{};
    // The waterfall mist: GALAXY_RT_MIST; its pipelines; this frame's puffs (8 floats each: eye-space centre, sigma,
    // optical depth; rt_engine.hlsl kEvMistIn); per light slot their records for the composite (rt_lighting.hlsl
    // kMistHeader + 3 a puff, uint4); the slot lit this frame.
    static constexpr std::uint32_t kMistMost = 512u;   // rt_engine.hlsl kMistMost
    static constexpr std::uint32_t kMistHeader = 6u;   // rt_lighting.hlsl kMistHeader
    bool mist_wanted_ = true;
    std::uint32_t mist_view_ = 0u;   // GALAXY_RT_MIST_VIEW: 1 no cut at the water, 2 the water found under the puffs
    Microsoft::WRL::ComPtr<ID3D12PipelineState> engine_mist_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mist_composite_pso_;
    std::vector<float> mist_puffs_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 4> mist_records_;
    std::array<D3D12_RESOURCE_STATES, 4> mist_records_state_{};
    int mist_slot_ = -1;
    // The slot's water was traced (trace_water; a frame of clear solids alone traces none, and composites none).
    std::array<bool, 4> water_traced_{};
    // The lighting's constants and bindings of the trace the engine ran (slot), for the clear pass at the scene's end.
    struct EngineBind {
        bool valid = false;
        unsigned slot = 0;
        std::vector<std::byte> constants;
        std::size_t engine_at = 0;
        D3D12_GPU_VIRTUAL_ADDRESS draws = 0;
        D3D12_GPU_VIRTUAL_ADDRESS tev = 0;
        D3D12_GPU_VIRTUAL_ADDRESS surface_uv = 0;
        D3D12_GPU_VIRTUAL_ADDRESS draw_textures = 0;
        D3D12_GPU_VIRTUAL_ADDRESS sky_map = 0;
        D3D12_GPU_VIRTUAL_ADDRESS emit_tris = 0;
        bool tev_records = false;   // the trace had TEV records (else `tev` is the draw table)
        bool textured = false;      // the trace had its draws' texture table (else `draw_textures` is the draw table)
    };
    EngineBind engine_bind_;
    // Per water draw (rt_engine.hlsl kEvClearDraws): the table's roughness (unorm16; 0xFFFF none), bit 16 the engine's
    // glass (glass or crystal; not mirror glass, not a metal coat).
    std::vector<std::uint32_t> water_clear_;
    // The eye's adaptation as the GPU eased it (rt_lighting.hlsl kAdaptation), read back kResultDelay frames later: the
    // camera's exposure on the CPU ([rt-stats]).
    static constexpr unsigned kAdaptationReadbacks = 4u;
    Microsoft::WRL::ComPtr<ID3D12Resource> adaptation_readback_;
    std::array<std::uint64_t, kAdaptationReadbacks> adaptation_readback_frame_{};
    unsigned adaptation_readback_next_ = 0;
    float adaptation_cpu_ = 1.0f;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> camera_pyramid_;
    std::array<D3D12_RESOURCE_STATES, 2> camera_pyramid_states_{};
    std::uint64_t camera_texels_ = 0;
    float camera_bloom_ = 0.04f;
    std::uint32_t camera_tone_ = 0;
    std::uint32_t prepared_width_ = 0;   // prepare_buffers' EFB size
    std::uint32_t prepared_height_ = 0;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> glow_emit_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> glow_emitter_pso_;
    // Physical: this scene's glow draws that are table light sources (GlowEmitterCS): first glow triangle, count, first
    // glow emitter, colour (RGBA8), radiance (float bits), its material texture's view, flags (kGlowEmitter*), its TEV
    // record in glow_tev_records_ (0xFFFFFFFF: none); the glow emitters it wrote, which the next frame's lighting
    // samples after the scene's emitters.
    std::vector<std::array<std::uint32_t, 8>> glow_emitters_;
    std::vector<std::uint32_t> glow_emitter_keys_;   // glow_emitters_'s draws' light keys
    std::vector<RtTevMaterial> glow_tev_records_;
    std::vector<RtTevStage> glow_tev_stages_;
    // The glow emitters the last GlowEmitterCS wrote, which this frame's lighting samples: per draw its light key, first
    // glow emitter and count.
    std::vector<std::array<std::uint32_t, 3>> glow_written_;
    std::uint32_t glow_emitter_slots_ = 0u;
    std::uint32_t glow_emit_count_ = 0u;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> emit_merge_pso_;
    D3D12_RESOURCE_STATES glow_base_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES glow_added_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES glow_positions_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES glow_uv_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES emit_tiles_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES emit_list_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    std::vector<RtDraw> glow_draws_;
    // The material table's point lights among this scene's draws: first triangle, count, colour and
    // flags (RGBA8: kind in the low byte), intensity (relative to a Luma), range, pulse, flags
    // (rt_lighting.hlsl kAuthored*) and the cosine of a spot light's cone.
    struct AuthoredLight {
        std::uint32_t first_triangle;
        std::uint32_t triangle_count;
        std::uint32_t color;
        float intensity;
        std::uint32_t material;   // its draw's material table entry, a strip's piece above bit 16 (its lights' identity)
        float pulse;
        std::uint32_t flags;
        float cone;
    };
    static_assert(sizeof(AuthoredLight) == 32u);
    std::vector<AuthoredLight> authored_;
    // Physical: the scene's emitting draws as area lights (EmitTriangleCS): first scene triangle, count, draw (bit 31:
    // two-sided), first emitter; the emitters (rt_lighting.hlsl g_emit_tris) and their passes.
    std::vector<std::array<std::uint32_t, 4>> emit_ranges_;
    std::vector<std::uint32_t> emit_range_keys_;   // emit_ranges_'s draws' light keys (RtDrawInfo.light_key)
    // Per water draw its waves for the reflections (rt_reflections.hlsl g_water_waves, 48 bytes): the wave map's view,
    // then RtDrawInfo.wave_params past its flags' word; per water triangle its texture coordinates (water_uv_).
    std::vector<std::array<std::uint32_t, 12>> water_waves_;
    bool water_game_waves_ = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> water_uv_;
    D3D12_RESOURCE_STATES water_uv_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    std::uint32_t emit_triangles_ = 0u;
    std::map<std::uint16_t, std::uint64_t> emit_materials_;   // [rt-emit]: emitting materials' triangles
    Microsoft::WRL::ComPtr<ID3D12Resource> emit_tris_;
    D3D12_RESOURCE_STATES emit_tris_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> emit_triangle_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> emit_scan_pso_;
    // Per glow draw (glow_draws_) for GlowEmitCS, in AuthoredLight's layout: its triangles, the material
    // table's colour and intensity for its texture where the table names it (flags kGlowTable).
    std::vector<AuthoredLight> glow_lights_;
    // Their material table entries (the emitter log names a light's source by them).
    std::vector<std::uint16_t> authored_materials_;
    std::vector<std::uint16_t> glow_materials_;
    std::vector<std::uint16_t> emit_log_authored_materials_;
    std::vector<std::uint16_t> emit_log_glow_materials_;
    // Per material the table names a light source, what became of its draws since the last emitter log
    // (EmitterOutcome): a light, a glow draw, drawn after the trace, or left out of the scene.
    enum EmitterOutcome : unsigned { kOutcomeLight, kOutcomeGlow, kOutcomeLate, kOutcomeClosed, kOutcomes };
    std::unordered_map<std::uint16_t, std::array<std::uint32_t, kOutcomes>> emitter_outcomes_;
    std::unordered_map<std::uint16_t, std::array<std::uint32_t, kOutcomes>> emit_log_outcomes_;
    // Black holes' lens draws among the glow draws (kRtDrawLens): first triangle and count in the glow
    // triangles; lens_ready_ once this frame's glow geometry holds them.
    static constexpr std::uint32_t kLensSlots = 4u;
    std::array<std::uint32_t, kLensSlots> lens_first_{};
    std::array<std::uint32_t, kLensSlots> lens_count_{};
    std::uint32_t lens_draws_ = 0u;
    bool lens_ready_ = false;
    float lens_strength_ = 1.0f;      // the bending (0: none)
    bool lens_view_ = false;          // rings at the found sizes
    // The material table's stars among the scene draws (`star`: the Observatory's core, Melty Molten's suns):
    // their first triangle and count in the scene's triangles; StarSphereCS fits each one's ball into the
    // lighting result after its samples (rt_lighting.hlsl kStarSlots).
    static constexpr std::uint32_t kStarSlots = 8u;
    std::array<std::uint32_t, kStarSlots> star_first_{};
    std::array<std::uint32_t, kStarSlots> star_count_{};
    std::uint32_t star_draws_ = 0u;
    // The scene's clear solids (kDrawTableSolid): the clear pass traces through them.
    std::uint32_t solid_draws_ = 0u;
    // The balls read back for the star core's light (it follows its ball): a few frames' copies of the result's
    // kStarSlots balls and the stars' colour over time (rt_lighting.hlsl kStarColour), each with the frame it was
    // taken (+1; 0: free) and whether the colour was copied.
    static constexpr unsigned kBallReadbacks = 4u;
    // After the balls and the colour: the probe's records (GALAXY_RT_PROBE: 0, 1, 3, 7, 8, and its surface's material
    // 11 to 14), the engine reference's comparison (2), the light meter's sums (4 to 6; the specular history's, 9).
    static constexpr unsigned kProbeRecords = 15u;
    static constexpr unsigned kBallReadbackBytes = kStarSlots * 16u + 16u + kProbeRecords * 16u;
    // GALAXY_RT_PROBE=x,y (diagnostics): an EFB pixel whose sample reports, once a second, the draw the camera sees and
    // what stops the sun's centre ray from it (draw record, material table entry, distance, instance).
    bool probe_ = false;
    std::array<std::uint32_t, 2> probe_pixel_{};
    std::array<std::uint32_t, 2> probe_sample_{};
    std::uint64_t probe_printed_ = 0u;
    std::vector<std::uint16_t> draw_materials_;   // per draw record of the scene: its material table entry
    std::array<std::vector<std::uint16_t>, kBallReadbacks> probe_materials_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> ball_readback_;
    std::array<std::uint64_t, kBallReadbacks> ball_readback_frame_{};
    std::array<bool, kBallReadbacks> ball_readback_colour_{};
    unsigned ball_readback_next_ = 0;
    std::uint64_t ball_read_frame_ = 0;
    float core_ball_radius_ = 0.0f;   // the largest star ball of the newest frame read (eye units; 0: none)
    // The stars' colour as the game draws them (their samples' mean over time; unknown until one is seen on the
    // stage): the star core's light takes its hue.
    std::array<float, 3> core_colour_{};
    bool core_colour_known_ = false;
    std::uint64_t star_colour_from_frame_ = ~0ull;   // copies taken after it hold this stage's colour
    Microsoft::WRL::ComPtr<ID3D12PipelineState> star_sphere_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> authored_emit_pso_;
    // GALAXY_RT_AUTHORED_POWER: a Luma's light (intensity 1). Sized for the logarithmic roll-off (rt_lighting.hlsl
    // soft_light): lamps and Lumas visibly light their surroundings while a lit scene stays near the game's brightness.
    float authored_power_ = 60000.0f;
    std::uint64_t authored_frames_ = 0u;
    std::uint32_t glow_triangles_ = 0u;
    bool glow_open_ = false;
    bool glow_base_valid_ = false;
    std::chrono::steady_clock::time_point emit_last_time_{};
    std::array<float, 3> emit_last_eye_{};
    bool emit_seen_ = false;
    bool log_emitters_ = false;      // GALAXY_RT_LOG_EMITTERS=1: [rt-emit] every 120 frames
    // GALAXY_RT_LOG_PHYSICAL=1: [rt-physical] with the emitter log: the physical light sources (the scene's emitting
    // triangles and the glow emitters) per material and the nearest the camera, from a readback of their buffer.
    bool log_physical_ = false;
    bool physical_log_pending_ = false;
    std::uint32_t physical_log_scene_ = 0u;
    std::uint32_t physical_log_glows_ = 0u;
    std::vector<std::uint16_t> physical_log_scene_materials_;  // per scene emitting triangle
    std::vector<std::uint16_t> physical_log_glow_materials_;   // per glow emitter slot
    std::vector<std::uint16_t> emit_range_materials_;          // emit_ranges_'s materials
    std::vector<std::uint16_t> glow_emitter_materials_;        // glow_emitters_'s materials
    Microsoft::WRL::ComPtr<ID3D12Resource> physical_readback_;
    float glow_cutoff_ = 0.004f;     // GALAXY_RT_GLOW_CUTOFF: a glow light's reach ends where its light is this
    float glow_reach_ = 30000.0f;    // GALAXY_RT_GLOW_REACH: the longest reach (eye units; a star's light)
    float glow_light_ = 1.0f;        // GALAXY_RT_GLOW_LIGHT: the glow lights' gain on surfaces
    float surface_light_ = 1.0f;     // GALAXY_RT_SURFACE_LIGHT: glowing surfaces' (lava) light on what sees them
    bool emit_log_pending_ = false;
    std::uint64_t emit_log_frame_ = 0u;
    std::array<float, 3> emit_log_eye_{};
    std::array<float, 6> emit_log_sun_{};   // sun light rgb, sky weight, keyed, strength
    // The logged scene's material-table point lights, glow draws and the glow draws the table names.
    std::array<std::uint32_t, 3> emit_log_draws_{};
    std::array<D3D12_RESOURCE_STATES, 2> light_z_state_{};
    std::array<D3D12_RESOURCE_STATES, kLightSlots> light_final_state_{};
    D3D12_RESOURCE_STATES picture_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES picture_sums_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    std::array<LightSlot, kLightSlots> light_slots_{};
    static_assert(RtReflections::kSlots == kLightSlots);
    // Reflections, in the lighting's result slots (GALAXY_RT_REFLECT=0 turns them off), and the
    // lights of the last traced lighting for their highlights.
    RtReflections reflections_;
    RtReflectLights reflect_lights_{};
    // Translucent water draws (kRtDrawWater) from the trace to trace_water: the draws, their
    // eye-space triangles and surface records, and their own acceleration structure.
    std::vector<RtDraw> water_draws_;
    std::uint32_t water_triangles_ = 0u;
    std::size_t water_flushed_draws_ = 0u;
    std::uint32_t water_flushed_triangles_ = 0u;
    bool water_open_ = false;
    bool log_seq_ = false;
    bool water_incomplete_ = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> water_positions_;
    Microsoft::WRL::ComPtr<ID3D12Resource> water_surfaces_;
    Microsoft::WRL::ComPtr<ID3D12Resource> water_blas_;
    Microsoft::WRL::ComPtr<ID3D12Resource> water_tlas_;
    Microsoft::WRL::ComPtr<ID3D12Resource> water_scratch_;
    Microsoft::WRL::ComPtr<ID3D12Resource> water_instance_;
    std::uint64_t water_scratch_tlas_ = 0u;   // the TLAS build's offset in the scratch buffer
    D3D12_RESOURCE_STATES water_positions_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES water_surfaces_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    // Late opaque draws (kRtLatePhase) from the trace to the scene's end: drawn after the trace, they join
    // the scene as a second instance: in this frame's water reflections, then (moved to the next frame's
    // camera) in its shadows, sky occlusion and reflections (Rosalina is one: the game draws her after the capture).
    std::vector<RtDraw> late_draws_;
    std::uint32_t late_triangles_ = 0u;
    std::size_t late_flushed_draws_ = 0u;
    std::uint32_t late_flushed_triangles_ = 0u;
    bool late_open_ = false;
    bool late_incomplete_ = false;
    bool late_pending_ = false;            // flushed late triangles not yet in late_blas_
    std::uint32_t late_built_ = 0u;        // triangles in late_blas_ (0: none)
    std::uint64_t late_scene_ = 0u;        // scene_serial_ of the trace they followed
    std::array<float, 12> late_camera_{};          // the eye space of late_blas_
    bool late_camera_known_ = false;
    std::array<float, 12> late_next_camera_{};     // the eye space of the late draws being noted
    bool late_next_camera_known_ = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> late_positions_;
    Microsoft::WRL::ComPtr<ID3D12Resource> late_surfaces_;
    Microsoft::WRL::ComPtr<ID3D12Resource> late_blas_;
    Microsoft::WRL::ComPtr<ID3D12Resource> late_scratch_;
    D3D12_RESOURCE_STATES late_positions_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES late_surfaces_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    // The traced sky (physical lighting): the game's sky lists' draws (kRtDrawSky) in a structure of their own, which
    // the sky probe traces from the camera each frame into the sky's light by direction (sky_probe_map_:
    // rt_lighting.hlsl SkyProbeCS) and its spherical harmonics (SkyTraceShCS).
    static constexpr std::uint32_t kSkyMaxTriangles = 64u * 1024u;
    static constexpr std::uint32_t kSkyMapSize = 64u;   // rt_lighting.hlsl kSkyMapSize
    std::vector<RtDraw> sky_draws_;
    // Per sky draw (RtDraw.flags bits 16-31): its texture view, flags (bit 0: the vertices' colour is the material;
    // bits 1-10: RtDrawInfo.blend), material colour and alpha test (rt_lighting.hlsl sky_layer).
    std::vector<std::array<std::uint32_t, 4>> sky_records_;
    // Per draw of the scene and of the sky: its TEV record (RtTevMaterial, its maps' views) and the stages after them
    // (rt_tev.hlsli g_tev).
    std::vector<RtTevMaterial> tev_records_;
    std::vector<RtTevStage> tev_stages_;
    std::vector<RtTevMaterial> sky_tev_records_;
    std::vector<RtTevStage> sky_tev_stages_;
    void note_tev(const RtDrawInfo& info, std::vector<RtTevMaterial>& records, std::vector<RtTevStage>& stages,
                  std::uint32_t delight = 0xFFFFFFFFu);
    // The records, then the stages (each record's stage_offset in bytes), for g_tev; 0 for none.
    D3D12_GPU_VIRTUAL_ADDRESS upload_tev(const std::vector<RtTevMaterial>& records, const std::vector<RtTevStage>& stages,
                                         Allocate allocate, void* allocate_context, RtUpload* uploaded = nullptr,
                                         std::uint64_t* uploaded_bytes = nullptr);
    // The trace's per-draw tables (draws, their textures, TEV records) again in the recording renderer frame's upload
    // ring, for the water trace (ray_tracing_water.cpp).
    void refresh_draw_tables(Allocate allocate, void* allocate_context);
    std::uint32_t sky_triangles_ = 0u;
    std::size_t sky_flushed_draws_ = 0u;
    std::uint32_t sky_flushed_triangles_ = 0u;
    bool sky_open_ = false;
    bool sky_incomplete_ = false;
    std::uint32_t sky_built_ = 0u;   // triangles in sky_blas_ this scene (0: none)
    std::array<float, 3> scene_up_{0.0f, 1.0f, 0.0f};
    std::uint64_t sky_scratch_tlas_ = 0u;   // the TLAS build's offset in sky_scratch_
    Microsoft::WRL::ComPtr<ID3D12Resource> sky_positions_;
    Microsoft::WRL::ComPtr<ID3D12Resource> sky_surfaces_;
    Microsoft::WRL::ComPtr<ID3D12Resource> sky_uv_;
    Microsoft::WRL::ComPtr<ID3D12Resource> sky_blas_;
    Microsoft::WRL::ComPtr<ID3D12Resource> sky_tlas_;
    Microsoft::WRL::ComPtr<ID3D12Resource> sky_scratch_;
    Microsoft::WRL::ComPtr<ID3D12Resource> sky_probe_map_;
    D3D12_RESOURCE_STATES sky_positions_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES sky_surfaces_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES sky_uv_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES sky_probe_map_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> sky_probe_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> sky_trace_sh_pso_;
    // GALAXY_RT_SKY_DUMP=<frame> (diagnostics): the traced sky's map at that frame, as sky_probe_<frame>.ppm (in units
    // of the stage's full light, display-encoded, 4x) in the snapshot directory, and its harmonics in the log.
    std::uint64_t sky_dump_at_ = 0u;
    std::uint64_t sky_dump_taken_ = 0u;
    Microsoft::WRL::ComPtr<ID3D12Resource> sky_dump_readback_;
    // Per slot the traced frame's eye position from a sub-frame's (3 rows), copied in from a
    // ring of upload entries before each sub-frame's replay.
    Microsoft::WRL::ComPtr<ID3D12Resource> reproject_;
    Microsoft::WRL::ComPtr<ID3D12Resource> reproject_upload_;
    std::byte* reproject_mapped_ = nullptr;
    unsigned reproject_entry_ = 0;
    D3D12_RESOURCE_STATES reproject_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
    std::array<float, 12> camera_{};
    std::array<float, 12> history_camera_{};
    std::array<float, 12> picture_camera_{};
    // Their projections (each frame's own jitter).
    std::array<float, 4> history_projection_{};
    // Physical: the stage's own full light on a surface facing it as the picture shows it (the eye adaptation's
    // highlight limit, rt_lighting.hlsl SkyStateCS).
    float physical_highlight_ = 1.0f;
    float sun_strength_ = 1.25f;  // a stage sun's light over the stage's key (GALAXY_RT_SUN_STRENGTH)
    std::array<float, 2> history_jitter_{};
    std::array<float, 4> picture_projection_{};
    std::uint32_t light_samples_ = 0;
    std::uint32_t history_width_ = 0;
    std::uint32_t history_height_ = 0;
    std::uint32_t history_factor_ = 0;
    std::uint32_t picture_width_ = 0;
    std::uint32_t picture_height_ = 0;
    std::uint32_t picture_factor_ = 0;
    std::uint32_t rays_ = 2;
    bool light_textured_ = false;  // the lighting's trace reads alpha maps from the descriptor heap (SM 6.6)
    bool low_quality_ = false;   // config rt_quality low: a third of the internal resolution, single rays
    unsigned quality_ = 1u;       // config rt_quality when it started
    bool high_quality_ = true;  // config rt_quality: high (every pixel, more rays) or medium
    std::uint32_t factor_override_ = 0;
    std::uint32_t history_frames_ = 16;
    std::uint32_t light_frame_ = 0;
    unsigned light_parity_ = 0;
    unsigned next_light_slot_ = 0;
    int light_slot_ = -1;
    float range_ = 1600.0f;
    float strength_ = 1.0f;
    float contact_ = 0.3f;
    float contact_range_ = 250.0f;
    float sky_saturation_ = 0.12f;
    float fallback_ = 0.55f;
    float emissive_ = 3.0f;
    std::array<float, 3> sun_direction_{0.35f, 1.0f, 0.3f};   // world space, towards the sun
    std::array<float, 3> sun_color_{1.0f, 0.93f, 0.82f};
    float sun_size_ = 1.0f;     // degrees
    bool sun_override_ = false;
    float sun_max_elevation_ = 65.0f;  // GALAXY_RT_SUN_MAX_ELEVATION: degrees no key light (no sun shown) rises above
    float key_min_elevation_ = 50.0f;  // GALAXY_RT_KEY_MIN_ELEVATION: degrees a key light (no sun shown) rises to
    float sun_min_elevation_ = 30.0f;  // GALAXY_RT_SUN_MIN_ELEVATION: degrees a sun the sky shows rises to
    const RtStageSun* stage_sun_ = nullptr;
    bool sun_held_ = false;
    bool key_valid_ = false;    // set_key_light
    bool key_shown_ = false;
    bool sun_shown_ = false;   // this trace's sun is one the sky shows (the camera's shafts come from it)
    std::array<float, 3> sun_visible_eye_{};   // where the sky shows it (eye space; the light may stand higher)
    // The light's direction as last traced (world), eased toward each trace's (hold_light): its source (0 the sun the
    // sky shows, 1 the table's moon, 2 the key light), the stage's sun row and when it was traced.
    std::array<float, 3> light_world_{};
    bool light_world_valid_ = false;
    int light_world_source_ = -1;
    const RtStageSun* light_world_stage_ = nullptr;
    std::chrono::steady_clock::time_point light_world_time_{};
    std::array<float, 3> key_direction_{};
    std::array<float, 3> key_color_{};
    float key_strength_ = 0.0f;
    // The sky's sun: the bins a capture fills (cleared from sky_zero_), their readbacks (capture
    // frame and reset generation), the map they build and the sun it shows.
    static constexpr unsigned kSkyRing = 4u;
    Microsoft::WRL::ComPtr<ID3D12Resource> sky_bins_;
    D3D12_RESOURCE_STATES sky_bins_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
    Microsoft::WRL::ComPtr<ID3D12Resource> sky_zero_;
    Microsoft::WRL::ComPtr<ID3D12Resource> sky_readback_;
    struct SkyRead {
        std::uint64_t frame = 0;
        std::uint32_t generation = 0;
        bool pending = false;
    };
    std::array<SkyRead, kSkyRing> sky_reads_{};
    unsigned sky_next_ = 0;
    std::uint32_t sky_generation_ = 0;
    SkyMap sky_map_{};
    SkySun sky_sun_{};
    SkyKey sky_key_{};
    std::uint64_t sky_reads_done_ = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> sky_radiance_;
    Microsoft::WRL::ComPtr<ID3D12Resource> sky_sh_;
    D3D12_RESOURCE_STATES sky_radiance_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES sky_sh_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    std::uint32_t sky_radiance_generation_ = ~0u;   // sky_generation_ when the map was last cleared
    std::uint64_t sky_cycles_ = 0;
    void read_sky();
    float key_size_ = 1.5f;     // GALAXY_RT_KEY_SIZE: degrees
    float point_light_ = 2.0f;      // GALAXY_RT_POINT_LIGHT: weight of the game's point lights
    float point_radius_ = 800.0f;   // GALAXY_RT_POINT_RADIUS: half-light distance without GX attenuation
    float point_saturation_ = 0.4f; // GALAXY_RT_POINT_SATURATION: 0 white .. 1 the light's own colour
    float split_ = 0.0f;            // GALAXY_RT_SPLIT: left of this fraction the game's own picture
    float night_ = 1.0f;            // GALAXY_RT_NIGHT: the sky and bounce light under a black sky (relative)
    float daylight_ = 0.35f;        // GALAXY_RT_DAYLIGHT: the sky's luminance that gives it in full
    float glow_boost_ = 0.4f;       // GALAXY_RT_GLOW_BOOST: extra light of a glowing surface
    // GALAXY_RT_GLOW_EXPOSURE: colour scale of the glows themselves. 1 draws them as the game does: the
    // exposure already matches the lit picture to the game's, and a scale on every additive draw washes
    // planets' atmospheres and halos out to white.
    float glow_exposure_ = 1.0f;
    float glow_spill_ = 0.7f;       // GALAXY_RT_GLOW_SPILL: share of a glow's excess that turns it white
    float glow_model_exposure_ = 1.0f;  // GALAXY_RT_GLOW_MODEL_EXPOSURE: glowing models (a star's surface)
    float glow_model_spill_ = 1.0f;     // GALAXY_RT_GLOW_MODEL_SPILL
    float sky_distance_ = 60000.0f; // GALAXY_RT_SKY_DISTANCE: farther surfaces are the sky (backdrops)
    float fill_ = 0.08f;            // GALAXY_RT_FILL: the light every surface keeps (more at night)
    // The material table's emitters glowing from their own surfaces (GALAXY_RT_SELF_AREA lava and
    // magma, GALAXY_RT_SELF_POINT Lumas, star bits, stars, per unit of intensity) and an area
    // light's radiance on what sees it (GALAXY_RT_AREA_RADIANCE, times its colour and intensity).
    float self_area_ = 1.2f;
    float self_point_ = 1.0f;
    float area_radiance_ = 3.0f;
    // Contact occlusion (rt_lighting.hlsl contact_occlusion): reach in eye units, strength on the art by
    // day, strength on the sky and bounce light at night.
    float ao_reach_ = 150.0f;
    float ao_day_ = 0.8f;
    float ao_night_ = 1.0f;
    // Night model (stages without daylight: rt-suns day 0, or a dark sky): the open art's level
    // (GALAXY_RT_NIGHT_LEVEL), the traced sky occlusion's depth (_AO), the cast shadows' depth (_SHADOW),
    // the traced lights' gain (_LIGHTS) and the light they add at most, softly (_LIGHTS_CAP).
    float night_level_ = 0.7f;
    float night_occlusion_ = 0.75f;
    float night_shadow_ = 0.6f;
    float night_lights_ = 2.0f;
    float night_lights_cap_ = 1.0f;
    float sun_cap_ = 2.5f;          // GALAXY_RT_SUN_CAP: the night lights' soft cap where the star core's light rules
    // The night key (moon, a stage's red star) adds its light on the faces it reaches (GALAXY_RT_NIGHT_KEY,
    // its strength per stage in rt-suns.tsv); by day the environment's art level (GALAXY_RT_DAY_ENVIRONMENT,
    // map geometry only).
    float night_key_ = 0.5f;
    float day_environment_ = 0.92f;
    float sky_ibl_ = 0.8f;          // GALAXY_RT_SKY_IBL: rays reaching the sky take the light of their
                                    // direction (0: the sky's mean colour from every direction)
    float relief_ = 3.0f;           // GALAXY_RT_RELIEF: height (eye units) of full texture brightness
    bool gi_ = true;                // GALAXY_RT_GI=0: no sky occlusion or bounce (shadows and lights stay)
    // GALAXY_RT_CORE_RADIUS: the star core light's half distance at scale 1, over 1.2 (it reaches the plaza)
    float core_radius_ = 1667.0f;
    float core_light_ = 8.0f;       // GALAXY_RT_CORE_LIGHT: its light at full revival (by its ball's surface)
    // GALAXY_RT_EXPOSURE_DAY / _NIGHT: the mean lighting the exposure holds under a bright / black
    // sky (the light moves into lit areas and out of shadows instead of dimming the picture);
    // GALAXY_RT_EXPOSURE=0 turns it off.
    float exposure_day_ = 1.0f;
    float exposure_night_ = 1.0f;
    bool exposure_ = true;
    float surface_emission_ = 3.0f; // GALAXY_RT_SURFACE_EMISSION: a glowing surface's light per unit
                                    // of its colour and eye-space area (0: none)
    float emission_ = 40.0f;        // GALAXY_RT_EMISSION: a glow's light per unit it adds to the picture
                                    // and unit of eye-space area (0: no glow lights)
    float point_size_ = 600.0f;     // GALAXY_RT_POINT_SIZE: radius of the geometry around one
    float sun_light_ = 1.2f;        // GALAXY_RT_SUN_LIGHT: the key light's strength
    float sky_light_ = 0.6f;
    bool lighting_enabled_ = true;
    bool ab_ = false;               // GALAXY_RT_AB
    bool ab_original_ = false;
    bool scene_untraced_ = false;
    bool show_lighting_ = false;
    std::uint32_t show_term_ = 0;   // GALAXY_RT_VIEW=2 sun, 3 sky light
    std::uint32_t view_cycle_ = 0;  // GALAXY_RT_VIEW_CYCLE: frames per view, 0 off
    bool camera_known_ = false;
    bool history_camera_known_ = false;
    bool history_valid_ = false;
    bool picture_valid_ = false;
    bool picture_camera_known_ = false;

    // [rt-stats] accumulation over the report window.
    struct Window {
        std::uint64_t scenes = 0;
        std::uint64_t draws = 0;
        std::uint64_t triangles = 0;
        std::uint64_t max_triangles = 0;
        double build_ms = 0.0;
        double validate_ms = 0.0;
        double light_ms = 0.0;
        std::uint64_t composites = 0;
        std::uint64_t point_lights = 0;
        std::uint64_t captures = 0;
        std::uint64_t timed = 0;
        std::array<std::uint64_t, kStatsWords> stats{};
        std::uint64_t validated = 0;
        std::uint64_t skipped = 0;
        std::uint64_t glow_draws = 0;
        std::uint64_t glow_triangles = 0;
        std::uint64_t late_triangles = 0;
        std::uint64_t water_triangles = 0;
        std::uint64_t water_dropped = 0;   // water draws past kWaterMaxTriangles
        std::array<double, kPassCount> pass_ms{};
        std::array<std::uint64_t, kPassCount> passes{};
        std::uint64_t passes_dropped = 0;
    } window_{};
};

}  // namespace galaxy::gx
