// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// RtReflections: ray-traced reflections of the traced scene's metal, glossy and water surfaces
// (rt_reflections.hlsl), part of SceneRayTracer. Traced after the lighting at the scene's first
// translucent phase from the EFB as the opaque scene left it; composited after the lighting
// composite, on interpolated sub-frames too. Translucent water (seas, lakes) is traced and
// composited once the translucent phases have drawn it. Render thread only.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace galaxy::gx {

// Point lights the lighting traces per frame (rt_lighting.hlsl and rt_reflections.hlsl g_points: two float4 each).
inline constexpr std::uint32_t kRtPointLights = 16u;

// The lights the lighting traced (rt_lighting.hlsl RtLight, eye space), for specular highlights and the light
// on high quality's shaded hits.
struct RtReflectLights {
    std::array<float, 4> sun{};               // towards the sun (xyz), tangent of its radius (w)
    std::array<float, 4> sun_light{};         // sun light (rgb), how far its shadow rays reach (w)
    // Per point light: position, reach; colour, 1 / its half-light distance squared.
    std::array<std::array<float, 4>, 2u * kRtPointLights> points{};
    std::uint32_t point_count = 0;
    float point_light = 0.0f;                 // the point lights' weight
    float point_size = 0.0f;                  // radius of the emitter around a point light
    D3D12_GPU_VIRTUAL_ADDRESS emitters = 0;   // glow lights (rt_lighting.hlsl g_emitters), 0 for none
    // The night model's point light (rt_lighting.hlsl g_night.w, g_ao.w, g_sun_cap, g_core_point): their weight,
    // the soft cap of what they add, its rise where the star core's light rules, the core's index (~0: none).
    float night_lights = 0.0f;
    float night_cap = 1.0f;
    float sun_cap = 1.0f;
    std::uint32_t core_point = ~0u;
};

// Physical light (GALAXY_RT_PHYSICAL) at the hits a reflection ray finds off screen: lit as the lighting lights the
// scene, from the lighting's own inputs. Its buffers are raw views in the frame's descriptor heap (high quality's
// textured hits; ~0: none).
struct RtReflectPhysical {
    bool on = false;
    std::array<float, 4> sky_radiance{};   // rt_lighting.hlsl g_sky_radiance (w: an overcast sky)
    std::array<float, 4> full_light{};     // g_full_light (w 1: the traced sky's map holds the sky)
    std::array<float, 4> up{};             // g_up: the player's up in eye space (w the de-light maps' relief; 0 off)
    std::array<float, 4> light_fit{};      // g_light_fit (x: the camera's exposure; 0: physical_exposure's own)
    std::uint32_t tev_view = ~0u;          // the scene's TEV records (rt_tev.hlsli g_tev)
    std::uint32_t tev_count = 0u;
    std::uint32_t emit_view = ~0u;         // the emitting triangles (EmitTriangleCS, EmitScanCS)
    std::uint32_t emit_count = 0u;
    std::uint32_t emit_scene = 0u;         // the scene's among them (the rest: glow emitters, in world space)
    std::uint32_t sky_view = ~0u;          // the traced sky's map and its spherical harmonics (SkyProbeCS)
};

// A traced scene, as the reflections read it. They take the lighting's samples: one per factor x
// factor EFB pixels, each with the triangle the lighting's camera ray met.
struct RtReflectScene {
    std::array<float, 4> projection{};      // GX projection A-D
    // Physical light: the open sky's ambient as the picture shows it (display-encoded, at the camera's exposure). A
    // reflection sees the sky at least this bright, the fill the lighting gives every matte surface; without it chrome
    // in space mirrors black.
    std::array<float, 3> sky_fill{};
    std::array<float, 4> depth{};           // D3D projection [2][2], [2][3]; viewport min, max depth
    std::array<float, 4> viewport{};        // EFB pixels
    float near_plane = 0.0f;
    std::array<float, 12> camera{};         // world to eye (3x4 rows)
    bool camera_known = false;
    std::uint32_t width = 0;                // EFB pixels
    std::uint32_t height = 0;
    std::uint32_t sample_width = 0;
    std::uint32_t sample_height = 0;
    std::uint32_t factor = 0;
    D3D12_GPU_VIRTUAL_ADDRESS tlas = 0;
    D3D12_GPU_VIRTUAL_ADDRESS positions = 0;    // per triangle its eye-space vertices (36 B)
    D3D12_GPU_VIRTUAL_ADDRESS surfaces = 0;     // per triangle its vertex normals and draw flags (16 B)
    D3D12_GPU_VIRTUAL_ADDRESS primary = 0;      // per sample triangle + 1 and barycentrics (UAV)
    D3D12_GPU_VIRTUAL_ADDRESS light = 0;        // per sample the lighting result (uint3)
    // The previous picture's sky and scene sums (rt_lighting.hlsl CaptureCS); 0 for none.
    D3D12_GPU_VIRTUAL_ADDRESS sums = 0;
    float sky_distance = 0.0f;              // farther surfaces are the sky (backdrops); 0 none
    // The lighting's sky map (rt_lighting.hlsl SkyLightMapCS: 32x32 float4 by world direction); 0 none.
    D3D12_GPU_VIRTUAL_ADDRESS sky_map = 0;
    RtReflectLights lights{};
    // Per draw (rt_lighting.hlsl g_draws, 32 bytes): the material table's roughness and metalness;
    // 0: none (the classes' own).
    D3D12_GPU_VIRTUAL_ADDRESS draws = 0;
    std::uint32_t draw_count = 0;
    // The translucent water draws (trace_water): their TLAS, eye-space vertices and surface records.
    D3D12_GPU_VIRTUAL_ADDRESS water_tlas = 0;
    D3D12_GPU_VIRTUAL_ADDRESS water_positions = 0;
    D3D12_GPU_VIRTUAL_ADDRESS water_surfaces = 0;
    // Their texture coordinates (per triangle as surface_uv: the wave map's, the water texture's) and per draw its waves
    // (rt_reflections.hlsl water_wave); whether any has the game's waves (the composite's own waves then rest).
    D3D12_GPU_VIRTUAL_ADDRESS water_uv = 0;
    D3D12_GPU_VIRTUAL_ADDRESS water_waves = 0;
    bool water_game_waves = false;
    // The ray tracing engine traced the clear glass and crystal (rt_engine.hlsl EngineClearCS): the water pass leaves
    // them (rt_reflections.hlsl kReflectEngineClear).
    bool engine_clear = false;
    // High quality: per triangle texture coordinates and colours, per draw its texture view and colour
    // (0: none, the hits off screen stay unknown); the art's level at a hit (day share, night level, day
    // level, texture detail per eye unit of distance).
    D3D12_GPU_VIRTUAL_ADDRESS surface_uv = 0;
    D3D12_GPU_VIRTUAL_ADDRESS draw_textures = 0;
    std::uint32_t texture_count = 0;
    std::array<float, 4> hit{};
    RtReflectPhysical physical{};
};

class RtReflections {
public:
    // Constants of one trace, in an upload allocation the caller makes (256-byte aligned).
    static constexpr std::size_t kConstantsBytes = 1168u;
    // Results kept for the sub-frames of the game frames being interpolated (the lighting's slots).
    static constexpr unsigned kSlots = 4u;

    RtReflections() = default;
    RtReflections(const RtReflections&) = delete;
    RtReflections& operator=(const RtReflections&) = delete;
    ~RtReflections() { shutdown(); }

    // At `quality` (RenderConfig::rt_quality). GALAXY_RT_REFLECT=0 leaves it off. False when a pipeline cannot be
    // created.
    bool initialize(ID3D12Device5* device, std::uint64_t timestamp_frequency, DXGI_FORMAT color_format,
                    unsigned quality);
    // The pipelines of `quality` made into the ray tracing pipeline library and dropped: a start that prepares the
    // library makes every quality's (rt_prepare.h).
    static bool prepare_pipelines(ID3D12Device5* device, DXGI_FORMAT color_format, unsigned quality);
    // Keeps the environment memory and the captured sky (the stage's; reset_memory clears them): started again on
    // the same device, the reflections continue from what the stage has shown.
    void shutdown() noexcept;
    [[nodiscard]] bool ready() const noexcept { return ready_; }
    // The water pass's buffers for a sample grid of `samples` in `tiles` 8 x 8 tiles (what trace_water makes on its
    // first water under the engine): made by the tracer's starting worker, not by the first stage with water.
    bool prepare_water(std::uint32_t samples, std::uint32_t tiles) {
        return ensure_buffers(samples, tiles, false) && ensure_water_buffers();
    }
    // GALAXY_RT_LOG_SEQ's frames (the ray tracer's).
    void set_log_seq(bool on) noexcept { log_seq_ = on; }

    // Start of a renderer frame: statistics of frames the GPU has finished are collected.
    void begin_frame(std::uint64_t frame_index);
    // Traces `scene`'s reflections into result slot `slot` (the lighting's). The EFB colour and
    // depth are two consecutive SRVs at `color_depth` (NON_PIXEL_SHADER_RESOURCE).
    void trace(ID3D12GraphicsCommandList* list, const RtReflectScene& scene, unsigned slot, void* constants_cpu,
               D3D12_GPU_VIRTUAL_ADDRESS constants_gpu, D3D12_GPU_DESCRIPTOR_HANDLE color_depth);
    // The lighting of `slot` is not traced: neither are its reflections.
    void invalidate(unsigned slot) noexcept;
    // A new stage: the environment memory starts over at the next trace.
    void reset_memory() noexcept { memory_reset_ = true; }
    // Blends slot `slot`'s reflections into the EFB colour `target` at the depth `depth_srv` holds
    // (PIXEL_SHADER_RESOURCE, in the bound heap). `reproject`: per slot the traced frame's eye
    // position from the sub-frame's (3 float4 rows), 0 in the traced frame. Pixels left of `split`
    // (EFB x; negative for none) keep the game's own picture. Leaves its own root signature and
    // pipeline bound.
    void composite(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE target,
                   D3D12_GPU_DESCRIPTOR_HANDLE depth_srv, std::uint32_t width, std::uint32_t height, unsigned slot,
                   D3D12_GPU_VIRTUAL_ADDRESS reproject, float split);
    // The translucent water of the scene traced into `slot` (scene.water_*), from the EFB colour and
    // depth as the translucent phases left them (NON_PIXEL_SHADER_RESOURCE at `color_depth`).
    void trace_water(ID3D12GraphicsCommandList* list, const RtReflectScene& scene, unsigned slot,
                     void* constants_cpu, D3D12_GPU_VIRTUAL_ADDRESS constants_gpu,
                     D3D12_GPU_DESCRIPTOR_HANDLE color_depth);
    // As composite(), the water's reflections over it.
    // `scene_srv`: the frame with the game's water drawn (a copy; 0: the water keeps the game's look); `layer_srv`:
    // the effects layer drawn over the water (its colour and transmittance, two views; 0: none).
    void composite_water(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE target,
                         D3D12_GPU_DESCRIPTOR_HANDLE depth_srv, std::uint32_t width, std::uint32_t height,
                         unsigned slot, D3D12_GPU_VIRTUAL_ADDRESS reproject, float split,
                         D3D12_GPU_DESCRIPTOR_HANDLE scene_srv = {}, D3D12_GPU_DESCRIPTOR_HANDLE opaque_srv = {},
                         D3D12_GPU_DESCRIPTOR_HANDLE layer_srv = {});

private:
    bool log_seq_ = false;
    static constexpr unsigned kResultSlots = 16u;
    static constexpr std::uint64_t kResultDelay = 8u;
    // Words 0-15 the trace's, 16-27 the water's; 28, 29 the sky map's share (trace, water); 30, 31 the
    // trace's hits lit by their draw's colour and its captured sky.
    static constexpr unsigned kStatsWords = 32u;
    // Trace, composite, water trace, water composite: begin and end each.
    static constexpr unsigned kTimestampsPerSlot = 8u;
    // Surface classes (rt_surfaces.h), the reflections' bare metal (rt_reflections.hlsl kBareMetalClass) and falling
    // water (kFallingWater).
    static constexpr unsigned kClasses = 10u;
    static constexpr unsigned kBareMetalClass = 8u;
    // Class of the translucent water's surface (rt_reflections.hlsl kWaterSurface).
    static constexpr unsigned kWaterClass = 5u;
    // Environment memory: kMemorySize^2 world directions (octahedral; rt_reflections.hlsl kProbeSize).
    static constexpr unsigned kMemorySize = 64u;
    static constexpr unsigned kMemoryTexels = kMemorySize * kMemorySize;
    // Captured sky: kSkySize^2 world directions (rt_reflections.hlsl kSkySize).
    static constexpr unsigned kSkySize = 1024u;
    // Draws whose mean lit colour the trace keeps (RtDraw.flags holds 16 bits of the index).
    static constexpr std::uint32_t kDrawLightEntries = 65536u;

    struct Retired {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        std::uint64_t frame = 0;
    };
    struct Buffer {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    };
    struct Slot {
        bool valid = false;
        std::uint64_t serial = 0;
        std::array<float, 4> projection{};
        std::array<float, 4> depth{};
        std::array<float, 4> viewport{};
        float near_plane = 0.0f;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t sample_width = 0;
        std::uint32_t sample_height = 0;
        std::uint32_t factor = 0;
        std::uint32_t tile_width = 0;
        std::array<float, 4> sun{};   // the water's caustics: towards the sun (eye space), 0 without one
        bool game_waves = false;      // its water moved by the game's own waves (water_wave)
    };
    struct Pending {
        std::uint64_t frame = 0;
        bool pending = false;
        bool composite_timed = false;
        bool water_timed = false;
        bool water_composite_timed = false;
    };
    // Per sample buffers of one pass (reflections or water): surface (both frames), accumulated light
    // (both frames); per result slot the composite's input and its tiles of 8x8 samples.
    struct Pass {
        std::array<Buffer, 2> info{};
        std::array<Buffer, 2> history{};
        std::array<Buffer, kSlots> final{};
        std::array<Buffer, kSlots> tiles{};
        std::array<float, 12> history_camera{};
        std::uint32_t history_width = 0;
        std::uint32_t history_height = 0;
        std::uint32_t history_factor = 0;
        unsigned parity = 0;
        bool history_valid = false;
        bool history_camera_known = false;
    };
    // Per surface class (rt_surfaces.h): perceptual roughness, F0, strength, metal (hue of the
    // surface) and cover (share of the surface's colour the reflection replaces; 0 adds light only).
    struct Material {
        float roughness = 0.0f;
        float f0 = 0.0f;
        float strength = 0.0f;
        float metal = 0.0f;
        float cover = 0.0f;
    };

    bool create_pipelines(DXGI_FORMAT color_format, unsigned quality);
    // `opaque`: the opaque pass's buffers too (the engine skips that pass; the water pass needs the rest).
    bool ensure_buffers(std::uint32_t samples, std::uint32_t tiles, bool opaque);
    bool ensure_water_buffers();
    bool allocate_pass(Pass& pass, std::uint64_t samples, std::uint32_t tiles, const wchar_t* name);
    void release_pass(Pass& pass);
    bool create_buffer(std::uint64_t bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state,
                       D3D12_RESOURCE_FLAGS flags, Microsoft::WRL::ComPtr<ID3D12Resource>& out, const wchar_t* name);
    void retire(Microsoft::WRL::ComPtr<ID3D12Resource>& resource);
    static void to_state(ID3D12GraphicsCommandList* list, Buffer& buffer, D3D12_RESOURCE_STATES wanted);
    // The trace constants of `scene` for `pass` (its camera history), with `flags` added.
    void fill_constants(const RtReflectScene& scene, const Pass& pass, std::uint32_t flags, void* constants_cpu);
    // Binds the trace's root parameters for `pass` writing result slot `slot`.
    void bind_trace(ID3D12GraphicsCommandList* list, const RtReflectScene& scene, Pass& pass, unsigned slot,
                    D3D12_GPU_VIRTUAL_ADDRESS constants_gpu, D3D12_GPU_DESCRIPTOR_HANDLE color_depth);
    // After the trace: temporal accumulation and the two filters into result slot `slot`.
    void accumulate(ID3D12GraphicsCommandList* list, Pass& pass, unsigned slot, UINT groups_x, UINT groups_y);
    // The pass's history moves to this trace and its result slot `slot` describes it.
    void finish_trace(Pass& pass, std::array<Slot, kSlots>& slots, const RtReflectScene& scene, unsigned slot,
                      UINT groups_x);
    void draw_composite(ID3D12GraphicsCommandList* list, Pass& pass, ID3D12PipelineState* pso,
                        D3D12_CPU_DESCRIPTOR_HANDLE target, D3D12_GPU_DESCRIPTOR_HANDLE depth_srv,
                        std::uint32_t width, std::uint32_t height, unsigned slot, const Slot& info,
                        D3D12_GPU_VIRTUAL_ADDRESS reproject, float split);
    void report();

    Microsoft::WRL::ComPtr<ID3D12Device5> device_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> trace_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> water_trace_pso_;
    // The traces sample the hits' textures (shader model 6.6, the descriptor heap indexed directly).
    bool textured_ = false;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> temporal_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> filter_a_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> filter_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> memory_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> sky_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> draw_clear_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> draw_light_pso_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> composite_root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> composite_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> water_composite_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> underwater_pso_;
    // The reflections' and the water's per sample buffers; reflected light per sample (both passes,
    // one after the other); the environment memory.
    Pass main_{};
    Pass water_{};
    Buffer raw_{};
    // Per result slot, per sample the picture under the translucent phases (the water's refraction),
    // then the water pass's state (rt_reflections.hlsl kUnderState entries).
    std::array<Buffer, kSlots> under_{};
    static constexpr std::uint32_t kUnderState = 13u;
    static constexpr std::uint32_t kUnderTint = 9u;   // rt_reflections.hlsl kUnderTint (4 entries)
    Buffer memory_{};
    Buffer sky_{};          // the captured sky (rt_reflections.hlsl SkyCaptureCS)
    Microsoft::WRL::ComPtr<ID3D12Device5> history_device_;   // the device memory_ and sky_ were made on
    Buffer draw_light_{};   // per draw its mean lit colour this frame (DrawLightCS)
    Buffer stats_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> stats_zero_;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> timestamps_;
    std::vector<Retired> retired_;
    std::array<Slot, kSlots> slots_{};
    std::array<Slot, kSlots> water_slots_{};
    std::array<Pending, kResultSlots> pending_{};
    std::array<Material, kClasses> materials_{};
    std::uint64_t timestamp_frequency_ = 0;
    std::uint64_t frame_index_ = 0;
    std::uint64_t serial_ = 0;
    int composite_timing_ = -1;         // result slot whose composite is timed next
    int water_result_ = -1;             // result slot of the last trace, for the water's stats
    int water_composite_timing_ = -1;   // result slot whose water composite is timed next
    std::uint32_t samples_ = 0;
    std::uint32_t tiles_capacity_ = 0;
    std::uint32_t water_samples_ = 0;   // the water's buffers, allocated at its first trace
    std::uint32_t frame_ = 0;
    bool ready_ = false;
    // GALAXY_RT_REFLECT_VIEW: 1 the reflections alone, 2 the surface classes, 3 their weight, 4
    // reflections on every other traced frame (A/B pairs), 5 the classes on every other frame, 6 per
    // traced frame in turn: the composite, none, the reflected light alone, its weight k (red) and
    // the highlights' luminance (green), marked at the middle of the right edge white, -, red, green
    // (GALAXY_PRESENT_SNAPSHOT_PAIR=3 keeps each set of four), 7 each draw's material table row (its
    // roughness word in hexadecimal digits, one per channel), 8 where each reflection's light came from, 9 per
    // traced frame in turn (no accumulation): the composite, 7, 8 and the reflected light alone, marked white,
    // red, green, blue.
    std::uint32_t view_ = 0;
    std::uint32_t history_frames_ = 24;
    float range_ = 50000.0f;
    float lobe_ = 0.85f;
    float fallback_ = 0.0f;   // GALAXY_RT_REFLECT_FALLBACK: confidence of the scene's average light
    float memory_fade_ = 0.99f;
    float ripple_ = 0.0f;     // GALAXY_RT_WATER_RIPPLE: the water normal's slope per luminance slope
    float probe_ = 0.6f;      // GALAXY_RT_REFLECT_PROBE: confidence of the memory along unknown rays
    // GALAXY_RT_WATER_ABSORB (r,g,b per 1000 eye units), _CLEAR (share of the clear water shown),
    // _CAUSTICS (gain, 0 none).
    std::array<float, 3> absorb_{};
    float clear_ = 1.0f;
    float caustics_ = 1.0f;
    float shafts_ = 1.0f;     // GALAXY_RT_WATER_SHAFTS: sunlight scattered under the water (0 none)
    float sky_fade_ = 0.9995f;  // GALAXY_RT_REFLECT_SKY_FADE: the memory of the sky kept per frame
    float sky_average_ = 0.15f; // GALAXY_RT_REFLECT_SKY_AVERAGE: confidence of the sky's average colour
    float metal_sky_ = 0.5f;    // GALAXY_RT_REFLECT_METAL_SKY: metal's share of a remembered or average sky
    float local_gain_ = 2.0f;   // GALAXY_RT_REFLECT_LOCAL_GAIN: the point and glow lights' highlights
    float draw_trust_ = 0.75f;  // GALAXY_RT_REFLECT_DRAW_TRUST: confidence of a hidden hit's draw colour (0 off)
    float sky_trust_ = 1.0f;    // GALAXY_RT_REFLECT_SKY_TRUST: confidence of the captured sky (0 off)
    bool memory_reset_ = false;
    D3D12_GPU_DESCRIPTOR_HANDLE water_scene_{};  // composite_water's scene copy while it draws
    D3D12_GPU_DESCRIPTOR_HANDLE water_opaque_{}; // ... and its copy of the frame before the water
    D3D12_GPU_DESCRIPTOR_HANDLE effects_layer_{};  // ... and the effects layer drawn over the water
    float water_cover_ = 0.0f;         // GALAXY_RT_WATER_COVER: share of the game's drawn water low-passed
    float foam_ = 0.6f;               // GALAXY_RT_WATER_FOAM: shore foam
    float shallow_caustics_ = 0.6f;   // GALAXY_RT_WATER_SHALLOW_CAUSTICS
    float waves_ = 1.0f;          // GALAXY_RT_WAVES: slope of the water's procedural waves (0: none)
    float rough_cutoff_ = 0.65f; // GALAXY_RT_REFLECT_ROUGH_CUTOFF: the material table's roughness above
                                 // which a surface traces no reflection

    // [rt-reflect] accumulation over the report window.
    struct Window {
        std::uint64_t traces = 0;
        std::uint64_t timed = 0;
        std::uint64_t composites = 0;
        std::uint64_t composites_timed = 0;
        double trace_ms = 0.0;
        double composite_ms = 0.0;
        std::uint64_t waters = 0;
        std::uint64_t water_timed = 0;
        std::uint64_t water_composites_timed = 0;
        double water_ms = 0.0;
        double water_composite_ms = 0.0;
        std::array<std::uint64_t, kStatsWords> stats{};
        std::uint64_t counted = 0;
    } window_{};
};

}  // namespace galaxy::gx
