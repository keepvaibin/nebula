// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// RtReflections: ray-traced reflections of the traced scene's metal, glossy and water surfaces
// (rt_reflections.hlsl), part of SceneRayTracer. Traced after the lighting at the scene's first
// translucent phase from the EFB as the opaque scene left it; composited after the lighting
// composite, on interpolated sub-frames too. Render thread only.

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
};

class RtReflections {
public:
    // Constants of one trace, in an upload allocation the caller makes (256-byte aligned).
    static constexpr std::size_t kConstantsBytes = 384u;
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
    // Blends slot `slot`'s reflections into the EFB colour `target` at the depth `depth_srv` holds
    // (PIXEL_SHADER_RESOURCE, in the bound heap). `reproject`: per slot the traced frame's eye
    // position from the sub-frame's (3 float4 rows), 0 in the traced frame. Pixels left of `split`
    // (EFB x; negative for none) keep the game's own picture. Leaves its own root signature and
    // pipeline bound.
    void composite(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE target,
                   D3D12_GPU_DESCRIPTOR_HANDLE depth_srv, std::uint32_t width, std::uint32_t height, unsigned slot,
                   D3D12_GPU_VIRTUAL_ADDRESS reproject, float split);

private:
    static constexpr unsigned kResultSlots = 16u;
    static constexpr std::uint64_t kResultDelay = 8u;
    static constexpr unsigned kStatsWords = 16u;
    static constexpr unsigned kTimestampsPerSlot = 4u;
    static constexpr unsigned kClasses = 8u;
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
    };
    struct Pending {
        std::uint64_t frame = 0;
        bool pending = false;
        bool composite_timed = false;
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
    bool create_buffer(std::uint64_t bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state,
                       D3D12_RESOURCE_FLAGS flags, Microsoft::WRL::ComPtr<ID3D12Resource>& out, const wchar_t* name);
    void retire(Microsoft::WRL::ComPtr<ID3D12Resource>& resource);
    static void to_state(ID3D12GraphicsCommandList* list, Buffer& buffer, D3D12_RESOURCE_STATES wanted);
    void report();

    Microsoft::WRL::ComPtr<ID3D12Device5> device_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> trace_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> temporal_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> filter_a_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> filter_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> memory_pso_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> composite_root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> composite_pso_;
    // Per sample: surface (both frames), reflected light, accumulated light (both frames), and per
    // result slot the composite's input and its tiles of 8x8 samples; the environment memory.
    std::array<Buffer, 2> info_{};
    Buffer raw_{};
    std::array<Buffer, 2> history_{};
    std::array<Buffer, kSlots> final_{};
    std::array<Buffer, kSlots> tiles_{};
    Buffer memory_{};
    Buffer stats_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> stats_zero_;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> timestamps_;
    std::vector<Retired> retired_;
    std::array<Slot, kSlots> slots_{};
    std::array<Pending, kResultSlots> pending_{};
    std::array<Material, kClasses> materials_{};
    std::array<float, 12> history_camera_{};
    std::uint64_t timestamp_frequency_ = 0;
    std::uint64_t frame_index_ = 0;
    std::uint64_t serial_ = 0;
    int composite_timing_ = -1;   // result slot whose composite is timed next
    std::uint32_t samples_ = 0;
    std::uint32_t tiles_capacity_ = 0;
    std::uint32_t history_width_ = 0;
    std::uint32_t history_height_ = 0;
    std::uint32_t history_factor_ = 0;
    std::uint32_t frame_ = 0;
    unsigned parity_ = 0;
    bool history_valid_ = false;
    bool history_camera_known_ = false;
    bool ready_ = false;
    // GALAXY_RT_REFLECT_VIEW: 1 the reflections alone, 2 the surface classes, 3 their weight, 4
    // reflections on every other traced frame (A/B pairs).
    std::uint32_t view_ = 0;
    std::uint32_t history_frames_ = 24;
    float range_ = 50000.0f;
    float lobe_ = 0.85f;
    float fallback_ = 0.0f;   // GALAXY_RT_REFLECT_FALLBACK: confidence of the scene's average light
    float memory_fade_ = 0.99f;

    // [rt-reflect] accumulation over the report window.
    struct Window {
        std::uint64_t traces = 0;
        std::uint64_t timed = 0;
        std::uint64_t composites = 0;
        std::uint64_t composites_timed = 0;
        double trace_ms = 0.0;
        double composite_ms = 0.0;
        std::array<std::uint64_t, kStatsWords> stats{};
        std::uint64_t counted = 0;
    } window_{};
};

}  // namespace galaxy::gx
