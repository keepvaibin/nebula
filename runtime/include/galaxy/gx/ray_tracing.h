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
#include "galaxy/gx/rt_reflections.h"
#include "galaxy/gx/rt_sky_sun.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace galaxy::gx {

// One draw's geometry, located in the renderer's buffers (byte offsets from each base).
struct RtDraw {
    std::uint32_t vertex_offset = 0;
    std::uint32_t flags = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t index_offset = 0;
    std::uint32_t first_triangle = 0;
    std::uint32_t triangle_count = 0;
    std::uint32_t constants_offset = 0;
    std::uint32_t palette_offset = 0;
};
static_assert(sizeof(RtDraw) == 32u);
// Decoded vertex stride (GxVertexOut: position at 0, matrix indices at 120).
inline constexpr std::uint32_t kRtVertexStride = 144u;
// RtDraw.flags: these bits; surface classes in bits 8-15; the draw's index in its scene in bits
// 16-31 (SceneRayTracer::add_draw).
inline constexpr std::uint32_t kRtDrawResident = 1u;
inline constexpr std::uint32_t kRtDrawIndexed = 2u;
inline constexpr std::uint32_t kRtDrawWideIndices = 4u;
// The draw's colour is lit by GX lights (characters, lit objects): its shading already turns
// towards its lights.
inline constexpr std::uint32_t kRtDrawLit = 8u;
// A lit draw whose GX light 0 the actor lights moved towards the traced sun (actor_lights.h):
// its shading's key light is the sun.
inline constexpr std::uint32_t kRtDrawSunKey = 16u;
// It takes GX light 4, the slot LightFunction::loadPointLightInfo loads the game's point light into.
inline constexpr std::uint32_t kRtDrawPointLit = 32u;
// An additive translucent draw (a glow: lamps, cores, Lumas, sparks): an emitter, not in the BVH.
inline constexpr std::uint32_t kRtDrawGlow = 64u;
// A blended translucent draw of the water class (seas, lakes): its reflections are traced once the
// translucent phases drew it (SceneRayTracer::trace_water); not in the BVH.
inline constexpr std::uint32_t kRtDrawWater = 128u;
// A lit draw whose colour is its material alone (the enhanced path under traced lighting: the game's
// lights and ambient removed, shader_gen.cpp): the traced light is its whole light.
inline constexpr std::uint32_t kRtDrawDelit = 1u << 12u;
// A water draw the material table marks as falling water (rt_reflections.hlsl kWaterfallFlag).
inline constexpr std::uint32_t kRtDrawWaterfall = 1u << 13u;
// Bits 8-11: the draw's surface class (rt_surfaces.h).
inline constexpr std::uint32_t kRtDrawClassShift = 8u;
inline constexpr std::uint32_t kRtDrawClassMask = 0xFu << kRtDrawClassShift;

// Diagnostics for GALAXY_RT_DUMP: the draw's GX projection (A-F), viewport depth range, cull
// mode and SceneFunction phase; colour channel 0 (XF control word, ambient and material RGBA8)
// and the first two lights of its mask (index, eye-space position and direction, RGBA8).
struct RtDrawInfo {
    std::array<float, 6> projection{};
    float min_depth = 0.0f;
    float max_depth = 1.0f;
    std::uint32_t cull = 0;
    std::uint32_t phase = 0;
    std::uint32_t channel = 0;
    std::uint32_t ambient = 0;
    std::uint32_t material = 0;
    std::array<std::uint32_t, 2> light_index{0xFFu, 0xFFu};
    std::array<std::array<float, 6>, 2> light{};
    std::array<std::uint32_t, 2> light_color{};
    // Added to RtDraw.flags: kRtDrawLit, kRtDrawSunKey, kRtDrawPointLit or kRtDrawGlow; surface classes
    // in bits 8-15.
    std::uint32_t surface = 0;
    // TEV stages, texgens, indirect stages and surface features (bits 0, 8, 16, 24); the environment
    // map's colour combiner and stage (bits 24); the colour channel controls 0 and 1.
    std::array<std::uint32_t, 4> shading{};
    // Its material table entry (rt_materials.h; 0: none).
    std::uint16_t rt_material = 0;
};

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

// The scene camera, for rays through EFB pixels and depth reconstruction: GX projection A-D,
// the D3D projection's [2][2] and [2][3], the viewport in EFB pixels and its depth range.
struct RtView {
    std::array<float, 4> projection{};
    float z_scale = 0.0f;
    float z_offset = 0.0f;
    std::array<float, 4> viewport{};
    float min_depth = 0.0f;
    float max_depth = 1.0f;
};

struct RtUpload {
    void* cpu = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
};

class SceneRayTracer {
public:
    using Allocate = RtUpload (*)(void* context, std::size_t bytes, std::size_t alignment);

    SceneRayTracer() = default;
    SceneRayTracer(const SceneRayTracer&) = delete;
    SceneRayTracer& operator=(const SceneRayTracer&) = delete;
    ~SceneRayTracer() { shutdown(); }

    // False without DXR 1.1 and shader model 6.5, or when a pipeline cannot be created.
    // `validate` adds the primary-ray check against the raster depth ([rt-stats]);
    // `color_format` is the EFB colour format the occlusion composite draws into.
    bool initialize(ID3D12Device* device, std::uint64_t timestamp_frequency, bool validate,
                    DXGI_FORMAT color_format);
    void shutdown() noexcept;
    [[nodiscard]] bool ready() const noexcept { return ready_; }

    // Start of a renderer frame: results of frames the GPU has finished are collected.
    void begin_frame(std::uint64_t frame_index);
    void begin_scene() noexcept;
    void add_draw(const RtDraw& draw, const RtDrawInfo& info);
    void set_view(const RtView& view) noexcept;
    [[nodiscard]] bool scene_open() const noexcept { return scene_open_; }
    // From the trace to the scene's end: glow draws (kRtDrawGlow) are noted.
    [[nodiscard]] bool glow_open() const noexcept { return glow_open_; }
    // From the trace to the water's trace: translucent water draws (kRtDrawWater) are noted.
    [[nodiscard]] bool water_open() const noexcept { return water_open_; }
    [[nodiscard]] bool accepts(const RtDrawInfo& info) const noexcept {
        return (info.surface & kRtDrawGlow) != 0u ? glow_open_
               : (info.surface & kRtDrawWater) != 0u ? water_open_
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

    // TLAS of the last traced scene (valid on the GPU timeline after trace_scene).
    [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS scene_tlas() const noexcept;

    // The game camera's view matrix (3x4 rows, world to eye) of the open scene; null when the
    // scene has none. The lighting history follows it from frame to frame.
    void set_camera(const std::array<float, 12>* view) noexcept;
    [[nodiscard]] bool lighting_active() const noexcept { return ready_ && lighting_enabled_ && !ab_original_; }
    // GALAXY_RT_AB=1 (test runs): every other present shows the game's own picture (no traced
    // lighting, reflections or finish); the traced frames carry a mark in the EFB's top-left corner.
    [[nodiscard]] bool ab_original() const noexcept { return ab_original_; }
    // The frame about to be drawn (GxBackend at its scene's start: the present it will become).
    void set_ab_original(bool original) noexcept { ab_original_ = ab_ && original; }
    // The traced sun's world direction (towards it: GALAXY_RT_SUN, else the game's visible sun)
    // and the distance of the game's sun model (0: none); false without a sun.
    [[nodiscard]] bool sun_direction(std::array<float, 3>& world, float* distance = nullptr) const noexcept;
    // Held: the game's sun stays where it last arrived however long ago (GxBackend: it arrived
    // since the last camera cut); else it lasts 2 s.
    void hold_sun(bool held) noexcept { sun_held_ = held; }
    // The open scene's key light while the game shows no sun (GxBackend: the world-fixed light its
    // lit draws take): world direction towards it, colour (0-1), how far it has come in (0-1) and
    // whether the sky shows it. It lights the scene as the sun does; one the sky does not show has
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
    // Another sky (GxBackend: a stage was loaded): the map starts over.
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
    // After trace_scene, before composite(): the last traced scene's reflections (rt_reflections.h)
    // from the EFB colour and depth as the opaque scene left them (NON_PIXEL_SHADER_RESOURCE, two
    // consecutive SRVs at `color_depth_table` in the bound heap).
    void trace_reflections(ID3D12GraphicsCommandList* list, Allocate allocate, void* allocate_context,
                           D3D12_GPU_DESCRIPTOR_HANDLE color_depth_table, std::uint32_t width, std::uint32_t height);
    // Multiplies the EFB colour `target` (width x height, the size the slot was traced at) by
    // lighting result `slot` at the depth `depth_srv` holds (PIXEL_SHADER_RESOURCE, in the bound
    // descriptor heap), then blends in the slot's reflections. `reproject`: the depth is an
    // interpolated sub-frame's, whose camera write_reprojection gave. Leaves its own root
    // signature and pipeline bound.
    void composite(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE target,
                   D3D12_GPU_DESCRIPTOR_HANDLE depth_srv, D3D12_GPU_DESCRIPTOR_HANDLE color_srv, std::uint32_t width,
                   std::uint32_t height, int slot, bool reproject);
    // Before a sub-frame's replay: its camera (world to eye at t), for the composites it runs.
    void write_reprojection(ID3D12GraphicsCommandList* list, const std::array<float, 12>& camera_t);
    // The end of the scene: its finished picture (EFB colour and depth, NON_PIXEL_SHADER_RESOURCE,
    // as two consecutive SRVs at `color_depth_table` in the bound heap) becomes the light the
    // next frame's rays read where they hit.
    // The glows drawn since the trace become lights: their eye-space triangles (geometry pass on the
    // same sources) and what they added to the picture since snapshot_glow_base().
    // `game_srv`: the game's own opaque picture (the lighting composite's source; 0: none), whose light
    // past the film shoulder joins the bloom.
    void capture_picture(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                         void* allocate_context, D3D12_GPU_DESCRIPTOR_HANDLE color_depth_table, std::uint32_t width,
                         std::uint32_t height, D3D12_GPU_DESCRIPTOR_HANDLE game_srv = {});
    // The glows' bloom of the scene just captured (-1: none) and its composite (additive) onto the
    // EFB; sub-frames replay the composite of their frame's slot.
    [[nodiscard]] int bloom_slot() const noexcept { return bloom_slot_; }
    void bloom(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE target, std::uint32_t width,
               std::uint32_t height, int slot);
    // The cinematic finish at the scene's end (rt_post.hlsl): post_prepare reads the EFB colour and
    // depth (NON_PIXEL_SHADER_RESOURCE, two consecutive SRVs at `color_depth_table`); post_finish
    // writes over the EFB colour (render target) reading the depth (PIXEL_SHADER_RESOURCE at
    // `depth_table`). A sub-frame's replay passes `replay` (its camera from write_reprojection).
    [[nodiscard]] bool post_active() const noexcept { return lighting_active() && post_; }
    // The lighting of the last traced scene as a texture (RGBA16F, one texel per sample) for the
    // translucent draws after it: written after the composite, PIXEL_SHADER_RESOURCE otherwise.
    void write_light_texture(ID3D12GraphicsCommandList* list, int slot);
    [[nodiscard]] bool light_texture_ready() const noexcept { return light_tex_ready_; }
    [[nodiscard]] ID3D12Resource* light_texture() const noexcept { return light_tex_.Get(); }
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE light_texture_srv() const noexcept { return light_tex_srv_; }
    [[nodiscard]] float light_texture_scale() const noexcept { return light_tex_scale_; }
    void post_prepare(ID3D12GraphicsCommandList* list, D3D12_GPU_DESCRIPTOR_HANDLE color_depth_table,
                      std::uint32_t width, std::uint32_t height, bool replay);
    void post_finish(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE target,
                     D3D12_GPU_DESCRIPTOR_HANDLE depth_table, std::uint32_t width, std::uint32_t height);
    // Transforms the water draws noted since the last flush (before a render chunk's list closes,
    // as flush_geometry).
    void flush_water(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
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
    // `scene_srv`: the frame with the game's water drawn (a copy), which the water composite low-passes.
    void composite_water(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE target,
                         D3D12_GPU_DESCRIPTOR_HANDLE depth_srv, std::uint32_t width, std::uint32_t height, int slot,
                         bool reproject, D3D12_GPU_DESCRIPTOR_HANDLE scene_srv = {},
                         D3D12_GPU_DESCRIPTOR_HANDLE opaque_srv = {});
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
    [[nodiscard]] float glow_spill(bool model) const noexcept { return model ? glow_model_spill_ : glow_spill_; }

private:
    static constexpr unsigned kResultSlots = 16u;
    static constexpr std::uint64_t kResultDelay = 8u;
    static constexpr std::uint32_t kEmitLogLights = 64u;
    static constexpr std::size_t kAuthoredLights = 512u;
    // A light in the glow lights' lists (rt_lighting.hlsl kEmitStride): position, radius; power,
    // frames unseen or cumulative power; spot direction, cosine of its cone (0 0 0 for a point light).
    static constexpr std::uint32_t kEmitStride = 48u;
    static constexpr std::uint64_t kEmitLogBytes = 16u + kEmitLogLights * kEmitStride;
    static constexpr std::uint64_t kEmitLogSums = 64u;   // the picture sums after them
    static constexpr unsigned kStatsWords = 16u;
    static constexpr std::uint32_t kMaxTriangles = 4u * 1024u * 1024u;
    // Translucent water triangles of one scene.
    static constexpr std::uint32_t kWaterMaxTriangles = 128u * 1024u;
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
        kPassCount
    };
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
    bool create_lighting_pipelines(DXGI_FORMAT color_format);
    // The geometry pass over `count` draws (triangles from `triangle_base`) into `positions` and
    // `surfaces`.
    bool transform_draws(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                         void* allocate_context, const RtDraw* draws, std::size_t count, std::uint32_t triangle_base,
                         std::uint32_t triangles, ID3D12Resource* positions, D3D12_RESOURCE_STATES& positions_state,
                         ID3D12Resource* surfaces, D3D12_RESOURCE_STATES& surfaces_state);
    // The reflections' view of the scene traced into lighting slot `slot`.
    [[nodiscard]] RtReflectScene reflect_scene(unsigned slot) const;
    bool ensure_water_buffers();
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
    Microsoft::WRL::ComPtr<ID3D12Resource> instance_;
    Microsoft::WRL::ComPtr<ID3D12Resource> stats_;
    Microsoft::WRL::ComPtr<ID3D12Resource> stats_zero_;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> timestamps_;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> pass_timestamps_;
    Microsoft::WRL::ComPtr<ID3D12Resource> pass_readback_;
    std::array<PassEntry, kPassEntries> pass_entries_{};
    unsigned next_pass_entry_ = 0;
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
    // register (RGBA8), flags (kDrawTableTint), a spare word.
    std::vector<std::array<std::uint32_t, 8>> draw_colors_;
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
        std::array<float, 3> sun_light{};  // its light
    };
    Microsoft::WRL::ComPtr<ID3D12RootSignature> light_root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> trace_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> temporal_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> spatial_a_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> spatial_m_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> spatial_b_pso_;
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
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> light_history_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kLightSlots> light_final_;
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
    // The cinematic finish (rt_post.hlsl): the picture before it (RGBA8), its bright part, blur and
    // light shafts at a quarter of the pixels, the focus and core radius over time.
    Microsoft::WRL::ComPtr<ID3D12RootSignature> post_root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> post_copy_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> post_bright_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> post_blur_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> post_shafts_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> post_focus_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> post_lum_pso_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> post_finish_root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> post_finish_pso_;
    // scene, bright, tmp, bloom, shafts, state, region luminance, its blur's temporary
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 8> post_buffers_{};
    std::array<D3D12_RESOURCE_STATES, 8> post_states_{};
    std::uint64_t post_pixels_ = 0u;
    std::uint64_t post_quarter_ = 0u;
    std::array<std::uint32_t, 40> post_constants_{};
    std::array<float, 12> post_camera_{};
    bool post_camera_known_ = false;
    std::chrono::steady_clock::time_point post_epoch_ = std::chrono::steady_clock::now();
    // Glow bloom (rt_lighting.hlsl BloomDownCS, BloomBlurCS, BloomPS): per light slot, what the
    // glows added at a quarter of the samples, blurred.
    Microsoft::WRL::ComPtr<ID3D12RootSignature> bloom_root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> bloom_down_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> bloom_blur_pso_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> bloom_composite_root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> bloom_composite_pso_;
    Microsoft::WRL::ComPtr<ID3D12Resource> bloom_temp_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 4> bloom_{};
    std::array<D3D12_RESOURCE_STATES, 4> bloom_state_{};
    D3D12_RESOURCE_STATES bloom_temp_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    std::array<std::array<std::uint32_t, 4>, 4> bloom_size_{};   // width, height, EFB width, EFB height
    std::uint64_t bloom_entries_ = 0u;
    int bloom_slot_ = -1;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> glow_emit_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> emit_merge_pso_;
    D3D12_RESOURCE_STATES glow_base_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES glow_added_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES glow_positions_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
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
        float range;
        float pulse;
        std::uint32_t flags;
        float cone;
    };
    static_assert(sizeof(AuthoredLight) == 32u);
    std::vector<AuthoredLight> authored_;
    // Per glow draw (glow_draws_) for GlowEmitCS, in AuthoredLight's layout: its triangles, the material
    // table's colour and intensity for its texture where the table names it (flags kGlowTable).
    std::vector<AuthoredLight> glow_lights_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> authored_emit_pso_;
    float authored_power_ = 40000.0f;   // GALAXY_RT_AUTHORED_POWER: a Luma's light (intensity 1)
    std::uint64_t authored_frames_ = 0u;
    std::uint32_t glow_triangles_ = 0u;
    bool glow_open_ = false;
    bool glow_base_valid_ = false;
    std::chrono::steady_clock::time_point emit_last_time_{};
    std::array<float, 3> emit_last_eye_{};
    bool emit_seen_ = false;
    bool log_emitters_ = false;      // GALAXY_RT_LOG_EMITTERS=1: [rt-emit] every 120 frames
    float glow_cutoff_ = 0.004f;     // GALAXY_RT_GLOW_CUTOFF: a glow light's reach ends where its light is this
    float glow_reach_ = 6000.0f;     // GALAXY_RT_GLOW_REACH: the longest reach (eye units)
    float glow_light_ = 1.0f;        // GALAXY_RT_GLOW_LIGHT: the glow lights' gain on surfaces
    float surface_light_ = 1.0f;     // GALAXY_RT_SURFACE_LIGHT: glowing surfaces' (lava) light on what sees them
    bool emit_log_pending_ = false;
    std::uint64_t emit_log_frame_ = 0u;
    std::array<float, 3> emit_log_eye_{};
    std::array<float, 6> emit_log_sun_{};   // sun light rgb, sky weight, keyed, strength
    // The logged scene's material-table point lights, glow draws and the glow draws the table names.
    std::array<std::uint32_t, 3> emit_log_draws_{};
    std::array<D3D12_RESOURCE_STATES, 2> light_z_state_{};
    std::array<D3D12_RESOURCE_STATES, 2> light_history_state_{};
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
    std::uint32_t light_samples_ = 0;
    std::uint32_t history_width_ = 0;
    std::uint32_t history_height_ = 0;
    std::uint32_t history_factor_ = 0;
    std::uint32_t picture_width_ = 0;
    std::uint32_t picture_height_ = 0;
    std::uint32_t picture_factor_ = 0;
    std::uint32_t rays_ = 2;
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
    float sun_size_ = 2.5f;     // degrees
    bool sun_override_ = false;
    float sun_min_elevation_ = 30.0f;  // GALAXY_RT_SUN_MIN_ELEVATION: degrees a game sun rises to at least
    const RtStageSun* stage_sun_ = nullptr;
    bool sun_held_ = false;
    bool key_valid_ = false;    // set_key_light
    bool key_shown_ = false;
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
    float key_size_ = 6.0f;     // GALAXY_RT_KEY_SIZE: degrees
    float point_light_ = 2.0f;      // GALAXY_RT_POINT_LIGHT: weight of the game's point lights
    float point_radius_ = 800.0f;   // GALAXY_RT_POINT_RADIUS: half-light distance without GX attenuation
    float point_saturation_ = 0.4f; // GALAXY_RT_POINT_SATURATION: 0 white .. 1 the light's own colour
    float split_ = 0.0f;            // GALAXY_RT_SPLIT: left of this fraction the game's own picture
    float night_ = 1.0f;            // GALAXY_RT_NIGHT: the sky and bounce light under a black sky (relative)
    float daylight_ = 0.35f;        // GALAXY_RT_DAYLIGHT: the sky's luminance that gives it in full
    float glow_boost_ = 0.4f;       // GALAXY_RT_GLOW_BOOST: extra light of a glowing surface
    // GALAXY_RT_GLOW_EXPOSURE: colour scale of the glows themselves. 1 (as the game draws them): the
    // exposure matches the lit picture to the game's, and a scale on every additive draw blew planets'
    // atmospheres and halos out to white (measured: Gateway, the Observatory).
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
    float sky_ibl_ = 0.8f;          // GALAXY_RT_SKY_IBL: rays reaching the sky take the light of their
                                    // direction (0: the sky's mean colour from every direction)
    bool post_ = true;              // GALAXY_RT_POST: the cinematic finish
    // GALAXY_RT_POST_BLOOM: bloom of the picture's bright part. 0: the 8-bit picture holds no light
    // beyond white, so a threshold blooms white sand, snow and clouds; the glows' own bloom (what
    // the emitters added) runs under the finish instead.
    float post_bloom_ = 0.0f;
    float post_threshold_ = 0.9f;   // GALAXY_RT_POST_THRESHOLD: luminance where bloom starts
    float post_shafts_ = 1.0f;      // GALAXY_RT_POST_SHAFTS
    float post_dof_ = 0.0f;         // GALAXY_RT_POST_DOF: largest far blur (EFB pixels at 1x; 0 none)
    float post_grade_ = 1.0f;       // GALAXY_RT_POST_GRADE
    float post_rim_ = 0.0f;         // GALAXY_RT_POST_RIM: rim light at silhouettes (outlined everything)
    float relief_ = 3.0f;           // GALAXY_RT_RELIEF: height (eye units) of full texture brightness
    float post_local_bright_ = 0.75f;  // GALAXY_RT_LOCAL_BRIGHT: contrast scale of regions above mid grey
    float post_local_dark_ = 0.92f;    // GALAXY_RT_LOCAL_DARK: of regions below it (1: no local exposure)
    float post_haze_ = 0.12f;       // GALAXY_RT_POST_HAZE: distance haze's largest share (bright skies)
    float post_haze_distance_ = 20000.0f;  // GALAXY_RT_POST_HAZE_DISTANCE: where it reaches 63 % of it
    float post_vignette_ = 0.1f;    // GALAXY_RT_POST_VIGNETTE: darkening towards the corners
    bool post_view_ = false;        // GALAXY_RT_POST_VIEW: the finished luminance in false colour
    bool gi_ = true;                // GALAXY_RT_GI=0: no sky occlusion or bounce (shadows and lights stay)
    float core_radius_ = 400.0f;    // GALAXY_RT_CORE_RADIUS: the star core's radius at scale 1 (first guess;
                                    // the finish measures it from depth)
    float core_light_ = 0.8f;       // GALAXY_RT_CORE_LIGHT: its light (a point light; grows with it)
    float bloom_strength_ = 1.4f;   // GALAXY_RT_BLOOM: the glows' bloom (0: none)
    float bloom_threshold_ = 0.15f; // GALAXY_RT_BLOOM_THRESHOLD: glow luminance that starts to bloom
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
    float sun_light_ = 1.5f;        // GALAXY_RT_SUN_LIGHT: the key light's strength
    float sky_light_ = 0.7f;
    bool lighting_enabled_ = true;
    bool ab_ = false;               // GALAXY_RT_AB
    bool ab_original_ = false;
    bool show_lighting_ = false;
    std::uint32_t show_term_ = 0;   // GALAXY_RT_VIEW=2 sun, 3 sky light
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
        std::array<double, kPassCount> pass_ms{};
        std::array<std::uint64_t, kPassCount> passes{};
        std::uint64_t passes_dropped = 0;
    } window_{};
};

}  // namespace galaxy::gx
