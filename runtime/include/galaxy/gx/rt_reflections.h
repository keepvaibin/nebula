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

// The lights the lighting traced (rt_lighting.hlsl RtLight, eye space), for specular highlights.
struct RtReflectLights {
    std::array<float, 4> sun{};               // towards the sun (xyz), tangent of its radius (w)
    std::array<float, 4> sun_light{};         // sun light (rgb), how far its shadow rays reach (w)
    // Per point light: position, reach; colour, 1 / its half-light distance squared.
    std::array<std::array<float, 4>, 16> points{};
    std::uint32_t point_count = 0;
    float point_light = 0.0f;                 // the point lights' weight
    float point_size = 0.0f;                  // radius of the emitter around a point light
    D3D12_GPU_VIRTUAL_ADDRESS emitters = 0;   // glow lights (rt_lighting.hlsl g_emitters), 0 for none
};

// A traced scene, as the reflections read it. They take the lighting's samples: one per factor x
// factor EFB pixels, each with the triangle the lighting's camera ray met.
struct RtReflectScene {
    std::array<float, 4> projection{};      // GX projection A-D
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
};

class RtReflections {
public:
    // Constants of one trace, in an upload allocation the caller makes (256-byte aligned).
    static constexpr std::size_t kConstantsBytes = 720u;
    // Results kept for the sub-frames of the game frames being interpolated (the lighting's slots).
    static constexpr unsigned kSlots = 4u;

    RtReflections() = default;
    RtReflections(const RtReflections&) = delete;
    RtReflections& operator=(const RtReflections&) = delete;
    ~RtReflections() { shutdown(); }

    // GALAXY_RT_REFLECT=0 leaves it off. False when a pipeline cannot be created.
    bool initialize(ID3D12Device5* device, std::uint64_t timestamp_frequency, DXGI_FORMAT color_format);
    void shutdown() noexcept;
    [[nodiscard]] bool ready() const noexcept { return ready_; }

    // Start of a renderer frame: statistics of frames the GPU has finished are collected.
    void begin_frame(std::uint64_t frame_index);
    // Traces `scene`'s reflections into result slot `slot` (the lighting's). The EFB colour and
    // depth are two consecutive SRVs at `color_depth` (NON_PIXEL_SHADER_RESOURCE).
    void trace(ID3D12GraphicsCommandList* list, const RtReflectScene& scene, unsigned slot, void* constants_cpu,
               D3D12_GPU_VIRTUAL_ADDRESS constants_gpu, D3D12_GPU_DESCRIPTOR_HANDLE color_depth);
    // The lighting of `slot` was not traced: neither are its reflections.
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
    // `scene_srv`: the frame with the game's water drawn (a copy; 0: the water keeps the game's look).
    void composite_water(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE target,
                         D3D12_GPU_DESCRIPTOR_HANDLE depth_srv, std::uint32_t width, std::uint32_t height,
                         unsigned slot, D3D12_GPU_VIRTUAL_ADDRESS reproject, float split,
                         D3D12_GPU_DESCRIPTOR_HANDLE scene_srv = {}, D3D12_GPU_DESCRIPTOR_HANDLE opaque_srv = {});

private:
    static constexpr unsigned kResultSlots = 16u;
    static constexpr std::uint64_t kResultDelay = 8u;
    // Words 0-15 the trace's, 16-31 the water's.
    static constexpr unsigned kStatsWords = 32u;
    // Trace, composite, water trace, water composite: begin and end each.
    static constexpr unsigned kTimestampsPerSlot = 8u;
    static constexpr unsigned kClasses = 8u;
    // Class of the translucent water's surface (rt_reflections.hlsl kWaterSurface).
    static constexpr unsigned kWaterClass = 5u;
    // Environment memory: kMemorySize^2 world directions (octahedral; rt_reflections.hlsl kProbeSize).
    static constexpr unsigned kMemorySize = 64u;
    static constexpr unsigned kMemoryTexels = kMemorySize * kMemorySize;

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

    bool create_pipelines(DXGI_FORMAT color_format);
    bool ensure_buffers(std::uint32_t samples, std::uint32_t tiles);
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
    Microsoft::WRL::ComPtr<ID3D12PipelineState> temporal_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> filter_a_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> filter_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> memory_pso_;
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
    // reflections on every other traced frame (A/B pairs), 5 the classes on every other frame.
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
    float metal_sky_ = 0.3f;    // GALAXY_RT_REFLECT_METAL_SKY: metal's share of reflected sky over its env map
    float local_gain_ = 2.0f;   // GALAXY_RT_REFLECT_LOCAL_GAIN: the point and glow lights' highlights
    bool memory_reset_ = false;
    D3D12_GPU_DESCRIPTOR_HANDLE water_scene_{};  // composite_water's scene copy while it draws
    D3D12_GPU_DESCRIPTOR_HANDLE water_opaque_{}; // ... and its copy of the frame before the water
    float water_cover_ = 0.75f;        // GALAXY_RT_WATER_COVER: share of the game's drawn water low-passed
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
