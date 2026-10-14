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

#include <array>
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
inline constexpr std::uint32_t kRtDrawResident = 1u;
inline constexpr std::uint32_t kRtDrawIndexed = 2u;
inline constexpr std::uint32_t kRtDrawWideIndices = 4u;

// Diagnostics for GALAXY_RT_DUMP: the draw's GX projection (A-F), viewport depth range, cull
// mode and SceneFunction phase.
struct RtDrawInfo {
    std::array<float, 6> projection{};
    float min_depth = 0.0f;
    float max_depth = 1.0f;
    std::uint32_t cull = 0;
    std::uint32_t phase = 0;
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
    // scene has none. The occlusion history follows it from frame to frame.
    void set_camera(const std::array<float, 12>* view) noexcept;
    [[nodiscard]] bool ao_active() const noexcept { return ready_ && ao_enabled_; }
    // Ambient occlusion of the last traced scene: its result slot for composite(), -1 for none.
    // The interpolated sub-frames of that game frame composite the same slot.
    [[nodiscard]] int ao_slot() const noexcept { return ao_slot_; }
    // Multiplies the EFB colour `target` (width x height, the size the slot was traced at) by
    // occlusion result `slot` at the depth `depth_srv` holds (PIXEL_SHADER_RESOURCE, in the bound
    // descriptor heap). `reproject`: the depth is an interpolated sub-frame's, whose camera
    // write_reprojection gave. Leaves its own root signature and pipeline bound.
    void composite(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE target,
                   D3D12_GPU_DESCRIPTOR_HANDLE depth_srv, std::uint32_t width, std::uint32_t height, int slot,
                   bool reproject);
    // Before a sub-frame's replay: its camera (world to eye at t), for the composites it runs.
    void write_reprojection(ID3D12GraphicsCommandList* list, const std::array<float, 12>& camera_t);

private:
    static constexpr unsigned kResultSlots = 16u;
    static constexpr std::uint64_t kResultDelay = 8u;
    static constexpr unsigned kStatsWords = 16u;
    static constexpr std::uint32_t kMaxTriangles = 4u * 1024u * 1024u;
    // Occlusion results kept for the sub-frames of the game frames being interpolated.
    static constexpr unsigned kAoSlots = 4u;

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

    bool ensure_capacity(ID3D12GraphicsCommandList* list, std::uint32_t triangles);
    bool fill_constants(const RtSources& sources, Allocate allocate, void* allocate_context, std::uint32_t draws,
                        std::uint32_t triangle_base, std::uint32_t triangles, std::uint32_t width,
                        std::uint32_t height, std::uint32_t flags, RtUpload& frame);
    void write_dump();
    bool create_ao_pipelines(DXGI_FORMAT color_format);
    bool ensure_ao_buffers(std::uint32_t samples);
    void trace_ao(ID3D12GraphicsCommandList* list, Allocate allocate, void* allocate_context,
                  D3D12_GPU_DESCRIPTOR_HANDLE depth_srv, std::uint32_t width, std::uint32_t height);
    static void to_state(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES& state,
                         D3D12_RESOURCE_STATES wanted);
    bool create_buffer(std::uint64_t bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state,
                       D3D12_RESOURCE_FLAGS flags, Microsoft::WRL::ComPtr<ID3D12Resource>& out, const wchar_t* name);
    void retire(Microsoft::WRL::ComPtr<ID3D12Resource>& resource);
    void report();

    Microsoft::WRL::ComPtr<ID3D12Device5> device_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> geometry_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> validate_pso_;
    Microsoft::WRL::ComPtr<ID3D12Resource> positions_;
    Microsoft::WRL::ComPtr<ID3D12Resource> blas_;
    Microsoft::WRL::ComPtr<ID3D12Resource> tlas_;
    Microsoft::WRL::ComPtr<ID3D12Resource> scratch_;
    Microsoft::WRL::ComPtr<ID3D12Resource> instance_;
    Microsoft::WRL::ComPtr<ID3D12Resource> stats_;
    Microsoft::WRL::ComPtr<ID3D12Resource> stats_zero_;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> timestamps_;
    std::vector<Retired> retired_;
    // GALAXY_RT_DUMP=<scene serial>: that scene's per-pixel validation is written as PPM
    // images to GALAXY_RT_DUMP_DIR (class, raster depth, traced depth).
    Microsoft::WRL::ComPtr<ID3D12Resource> dump_;
    Microsoft::WRL::ComPtr<ID3D12Resource> dump_readback_;
    std::uint64_t dump_scene_ = 0;
    std::uint64_t dump_frame_ = 0;
    std::uint32_t dump_width_ = 0;
    std::uint32_t dump_height_ = 0;
    bool dump_pending_ = false;
    std::vector<RtDraw> dump_draws_;
    std::vector<RtDrawInfo> infos_;
    std::vector<RtDrawInfo> dump_infos_;
    std::vector<RtDraw> draws_;
    std::array<ResultSlot, kResultSlots> slots_{};
    RtView view_{};
    D3D12_RESOURCE_STATES positions_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
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

    // Ambient occlusion (rt_ao.hlsl). GALAXY_RT_AO=0 turns it off; GALAXY_RT_AO_RAYS (per sample),
    // _RADIUS (eye units), _STRENGTH (0-1), _HISTORY (frames), _FACTOR (EFB pixels per sample
    // side, 1-4) and _VIEW=1 (the occlusion alone) tune it.
    struct AoSlot {
        RtView view{};
        std::array<float, 12> camera{};
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t ao_width = 0;
        std::uint32_t ao_height = 0;
        float near_plane = 0.0f;
        bool camera_known = false;
    };
    Microsoft::WRL::ComPtr<ID3D12RootSignature> ao_root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> ao_trace_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> ao_temporal_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> ao_spatial_pso_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> composite_root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> composite_pso_;
    // Per sample: eye z (this frame and the previous), packed normal, raw occlusion,
    // accumulated (occlusion, frames) for both frames; per result slot (occlusion, eye z).
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> ao_z_;
    Microsoft::WRL::ComPtr<ID3D12Resource> ao_normal_;
    Microsoft::WRL::ComPtr<ID3D12Resource> ao_raw_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> ao_history_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kAoSlots> ao_final_;
    std::array<D3D12_RESOURCE_STATES, 2> ao_z_state_{};
    std::array<D3D12_RESOURCE_STATES, 2> ao_history_state_{};
    std::array<D3D12_RESOURCE_STATES, kAoSlots> ao_final_state_{};
    std::array<AoSlot, kAoSlots> ao_slots_{};
    // Per slot the traced frame's eye position from a sub-frame's (3 rows), copied in from a
    // ring of upload entries before each sub-frame's replay.
    Microsoft::WRL::ComPtr<ID3D12Resource> reproject_;
    Microsoft::WRL::ComPtr<ID3D12Resource> reproject_upload_;
    std::byte* reproject_mapped_ = nullptr;
    unsigned reproject_entry_ = 0;
    D3D12_RESOURCE_STATES reproject_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
    std::array<float, 12> camera_{};
    std::array<float, 12> history_camera_{};
    std::uint32_t ao_samples_ = 0;
    std::uint32_t ao_history_width_ = 0;
    std::uint32_t ao_history_height_ = 0;
    std::uint32_t ao_rays_ = 2;
    std::uint32_t ao_factor_ = 2;
    std::uint32_t ao_history_frames_ = 12;
    std::uint32_t ao_frame_ = 0;
    unsigned ao_parity_ = 0;
    unsigned ao_next_slot_ = 0;
    int ao_slot_ = -1;
    float ao_radius_ = 300.0f;
    float ao_strength_ = 0.85f;
    bool ao_enabled_ = true;
    bool ao_view_ = false;
    bool camera_known_ = false;
    bool history_camera_known_ = false;
    bool ao_history_valid_ = false;

    // [rt-stats] accumulation over the report window.
    struct Window {
        std::uint64_t scenes = 0;
        std::uint64_t draws = 0;
        std::uint64_t triangles = 0;
        std::uint64_t max_triangles = 0;
        double build_ms = 0.0;
        double validate_ms = 0.0;
        double ao_ms = 0.0;
        std::uint64_t timed = 0;
        std::array<std::uint64_t, kStatsWords> stats{};
        std::uint64_t validated = 0;
        std::uint64_t skipped = 0;
    } window_{};
};

}  // namespace galaxy::gx
