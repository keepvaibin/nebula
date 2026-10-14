#pragma once

// RendererD3D12: device, swap chain, EFB/XFB render targets, per-frame upload
// rings, draw submission, and presentation.
//
// Frame pipelining: kFramesInFlight frames are recorded ahead of the GPU;
// begin_frame() waits only on the fence of frame N - kFramesInFlight. A full
// CPU/GPU sync happens only in shutdown() and on resize.
//
// All GX draws target the offscreen EFB (color RGBA8 + depth
// D24_UNORM_S8_UINT, which keeps 24-bit Z parity with the Wii). present()
// aspect-fits an XFB into the swap-chain backbuffer; that blit is where
// post-processing and ray tracing would be injected.
//
// begin_frame() is also the sole PSO-publication point: it drains the
// PipelineCache completion queue before any command-list recording starts.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include "galaxy/gx/efb_copy.h"
#include "galaxy/gx/temporal_upscaler.h"
#include "galaxy/gx/immutable_upload.h"
#include "galaxy/gx/interp_gpu.h"
#include "galaxy/gx/post_process.h"
#include "galaxy/gx/ray_tracing.h"
#include "galaxy/gx/placed_texture_allocator.h"
#include "galaxy/gx/gx_bitfields.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx/window_owner.h"
#include "galaxy/gx_d3d12.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <future>
#include <optional>
#include <string>
#include <span>
#include <atomic>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace galaxy::gx {

class TextureCache;

// Deterministic, device-free validation used by native tests so changes to the
// runtime-compiled EFB conversion shader cannot ship with HLSL syntax errors.
[[nodiscard]] bool validate_efb_conversion_shader(
    std::string& diagnostics) noexcept;

inline constexpr unsigned kFramesInFlight = 2;
// Matrix-ring segments in temporal mode (see RendererD3D12::begin_frame):
// two more than the frames in flight.
inline constexpr unsigned kRingSegments = kFramesInFlight + 2u;
// Interpolation mode: one segment of every ring, the descriptor ring and the
// sampler heaps per game frame, for the frame being recorded, the completed
// log waiting for its present and the log whose sub-frames play. A segment
// is reused only after its fence (the oldest log is then invalidated).
inline constexpr unsigned kInterpSegments = 3u;
inline constexpr unsigned kSamplerHeapSlots = 2048;

namespace detail {

// One shared CPU-only conversion RTV. Its contents are the identity being
// cached, not any individual destination's last use of that descriptor.
class ConversionRtvSlot {
public:
    // True when a descriptor write was required. All slot writes go through
    // this method; reset before replacing its device or descriptor heap.
    [[nodiscard]] bool bind(
        ID3D12Device* device, ID3D12Resource* resource,
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor);
    void reset() noexcept {
        resource_.Reset();
        descriptor_ = {};
    }

private:
    // Retain identity so freed resource-pointer reuse cannot fake a match.
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor_{};
};

}  // namespace detail

struct SamplerTableKey {
    std::array<std::uint32_t, 8> samplers{};
    std::uint32_t config = 0;
    bool operator==(const SamplerTableKey&) const = default;
};
struct SamplerTableKeyHasher {
    [[nodiscard]] std::size_t operator()(const SamplerTableKey& key) const noexcept;
};
[[nodiscard]] SamplerTableKey make_sampler_table_key(
    const std::uint32_t keys[8], const RenderConfig& cfg) noexcept;

// One cache describes one physical sampler heap. reset(config) starts a new
// frame only after that heap's GPU fence completes. Unchanged tables survive;
// every lookup pins its index for this frame, including pending draw batches.
// At capacity, only a table not used this frame may be replaced. Thus history
// cannot exhaust a legal frame, and no recorded draw's descriptors are changed.
// A replacement reports created=true so the caller writes all eight samplers.
// Configuration changes and unconditional reset invalidate all retained tables.
class SamplerTableCache {
public:
    struct Allocation { unsigned index; bool created; };
    [[nodiscard]] Allocation get_or_allocate(const SamplerTableKey& key);
    void reset(std::uint32_t config_key) noexcept {
        if (config_key != config_key_ || frame_ == UINT64_MAX) {
            reset();
            config_key_ = config_key;
        }
        ++frame_;
    }
    // Unconditional clear for teardown paths, where the heap contents must not
    // be assumed valid again.
    void reset() noexcept {
        tables_.clear();
        cursor_ = 8u;
        config_key_ = 0u;
        frame_ = 0u;
        eviction_cursor_ = 0u;
    }
private:
    std::unordered_map<SamplerTableKey, unsigned, SamplerTableKeyHasher> tables_;
    unsigned cursor_ = 8u;
    std::uint32_t config_key_ = 0u;
    static constexpr unsigned kDynamicTables = (kSamplerHeapSlots - 8u) / 8u;
    // A bounded clock scan avoids repeatedly scanning all previously pinned
    // entries for each miss. Reverse keys allow exception-safe map replacement.
    std::array<SamplerTableKey, kDynamicTables> slot_keys_{};
    std::array<std::uint64_t, kDynamicTables> slot_frames_{};
    std::uint64_t frame_ = 0u;
    unsigned eviction_cursor_ = 0u;
};
inline constexpr unsigned kEfbWidth = 640;
inline constexpr unsigned kEfbHeight = 528;
inline constexpr DXGI_FORMAT kEfbColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
// Motion vectors for temporal upscaling: render pixels, current -> previous.
inline constexpr DXGI_FORMAT kMotionVectorFormat = DXGI_FORMAT_R16G16_FLOAT;
// Reactive mask for temporal upscaling: blended draws' alpha, max-blended.
inline constexpr DXGI_FORMAT kReactiveMaskFormat = DXGI_FORMAT_R8_UNORM;
inline constexpr DXGI_FORMAT kEfbDepthResourceFormat =
    DXGI_FORMAT_R24G8_TYPELESS;
inline constexpr DXGI_FORMAT kEfbDepthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
inline constexpr DXGI_FORMAT kEfbDepthSrvFormat =
    DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
inline constexpr DXGI_FORMAT kBackbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

struct EfbClearValues {
    std::array<float, 4> color{};
    float reversed_depth = 0.0f;
};
[[nodiscard]] EfbClearValues make_efb_clear_values(
    std::uint32_t ar, std::uint32_t gb, std::uint32_t z24,
    std::uint8_t pixel_format) noexcept;

// Builds the exact GX graphics root signature used by RendererD3D12. Root
// CBV b0 is visible to both VS and GS because native GX line/point expansion
// reads its scale-aware raster constants in the geometry stage.
[[nodiscard]] HRESULT create_gx_root_signature(
    ID3D12Device* device,
    ID3D12RootSignature** root_signature) noexcept;

// Exact 16-dword root-constant payload consumed by the EFB conversion shader.
// Exposed so device-only validation can exercise the production shader and
// the production clamp/coefficient setup without creating a game window.
struct EfbConversionShaderConstants {
    std::array<float, 16> values{};
};

[[nodiscard]] std::string_view efb_conversion_shader_source() noexcept;
[[nodiscard]] EfbConversionShaderConstants make_efb_conversion_constants(
    const EfbCopyParams& params,
    bool is_depth_copy,
    bool intensity,
    bool keep_alpha,
    std::uint8_t copy_format,
    float src_x,
    float src_y,
    float step_x,
    float step_y,
    float filter_row_offset,
    unsigned efb_scale,
    unsigned logical_efb_height,
    unsigned reduction_width = 0u) noexcept;

// Builds the production 1x1 GXPeek conversion payload. Color peeks box-reduce
// the complete scaled logical-pixel block; depth peeks address the exact
// point-selected physical texel recorded in EfbPeekSamplingGeometry.
[[nodiscard]] EfbConversionShaderConstants make_efb_peek_conversion_constants(
    const EfbPeekSamplingGeometry& sampling,
    EfbPeekKind kind) noexcept;

[[nodiscard]] D3D12_SAMPLER_DESC build_sampler_desc_from_key(
    std::uint32_t key,
    const RenderConfig& cfg) noexcept;

// Linear per-frame allocator over one persistently mapped UPLOAD resource,
// partitioned into kFramesInFlight segments so frame N never overwrites
// memory the GPU is still reading for frame N-1.
class UploadRing {
public:
    // Caller must have retired all GPU references to this resource.
    void reset() noexcept;
    struct Allocation {
        std::byte* cpu = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
        std::size_t offset = 0;     // within the whole resource
    };

    // `capacity`: bytes reserved beyond bytes_per_frame * frames_in_flight, so later
    // resegment() calls up to it need no new buffer.
    bool initialize(
        ID3D12Device* device,
        const char* name,
        std::size_t bytes_per_frame,
        unsigned frames_in_flight,
        std::size_t capacity = 0);
    // A new segmentation inside the existing buffer (the caller has retired every GPU
    // reference to its contents); false when it does not fit. Immutable uploads start over.
    bool resegment(std::size_t bytes_per_frame, unsigned frames_in_flight);
    void begin_frame(unsigned frame_slot);

    // Bump-allocates within the current frame segment. Exhaustion is a GxFatalError:
    // geometry is never dropped silently.
    // Alignment must be a nonzero power of two. Rejected requests do not
    // consume bytes or advance the segment's content generation.
    Allocation allocate(std::size_t size, std::size_t alignment);

    Allocation upload_immutable(
        const void* source, std::size_t size, std::size_t alignment,
        ImmutableUploadToken& token);
    [[nodiscard]] std::uint64_t reused_bytes() const { return reused_bytes_; }
    [[nodiscard]] std::uint64_t copied_bytes() const { return copied_bytes_; }
    // Number of upload_immutable() calls this frame. reused_bytes() and copied_bytes()
    // are only meaningful relative to this count: both are zero for a frame in which
    // the immutable path was never entered, which looks the same on the timing line as
    // a frame in which it was entered and classified every byte as a copy. The call
    // count is the field that separates "the reuse path is not reached" from "the
    // counters are not observed". The increment costs one add on a path that already
    // does a memcpy or a map lookup.
    [[nodiscard]] std::uint64_t immutable_upload_calls() const {
        return immutable_upload_calls_;
    }

    // Peak bytes used within a single frame segment, and the segment size. These show
    // whether the ring's configured size is resident memory or only reserved address
    // space. The rings are D3D12_HEAP_TYPE_UPLOAD committed buffers mapped for the life
    // of the process, so the reservation is bytes_per_frame * frames_in_flight (240 MiB
    // by default). Physical pages are committed only for the parts that were written,
    // so the physical cost tracks the peak cursor rather than the reservation. Exhaustion
    // is fatal, so the floor for any reduction is peak_bytes_per_frame().
    [[nodiscard]] std::size_t peak_bytes_per_frame() const {
        return peak_bytes_per_frame_;
    }
    // The peak since the last call (diagnostics windows).
    [[nodiscard]] std::size_t take_peak_bytes_per_frame() noexcept {
        const std::size_t peak = peak_bytes_per_frame_;
        peak_bytes_per_frame_ = 0u;
        return peak;
    }
    [[nodiscard]] std::size_t bytes_per_frame() const { return bytes_per_frame_; }
    [[nodiscard]] std::size_t total_bytes() const { return bytes_per_frame_ * frames_in_flight_; }
    // CPU view of a GPU address inside this ring; null outside it.
    [[nodiscard]] const std::byte* cpu_at(D3D12_GPU_VIRTUAL_ADDRESS va) const {
        return va >= gpu_base_ && va < gpu_base_ + total_bytes() ? mapped_ + (va - gpu_base_) : nullptr;
    }
    [[nodiscard]] std::size_t cursor() const { return cursor_; }

    [[nodiscard]] ID3D12Resource* resource() const { return buffer_.Get(); }
    [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS gpu_base() const { return gpu_base_; }

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer_;
    const char* name_ = "";
    std::byte* mapped_ = nullptr;
    // This committed buffer keeps its address until it is replaced by initialize.
    D3D12_GPU_VIRTUAL_ADDRESS gpu_base_ = 0;
    std::size_t bytes_per_frame_ = 0;
    std::size_t capacity_ = 0;
    std::size_t segment_base_ = 0;
    std::size_t cursor_ = 0;
    unsigned frames_in_flight_ = 0;
    unsigned active_slot_ = 0;
    bool frame_ready_ = false;
    bool segment_allocation_pending_ = true;
    std::uint64_t resource_identity_ = 0;
    std::vector<std::uint64_t> slot_generations_;
    std::uint64_t reused_bytes_ = 0;
    std::uint64_t copied_bytes_ = 0;
    std::uint64_t immutable_upload_calls_ = 0;
    std::size_t peak_bytes_per_frame_ = 0;
};

// Shader-visible descriptor allocator with the same per-frame segmentation.
class DescriptorRing {
public:
    // Caller must have retired all GPU references to this heap.
    void reset() noexcept;
    bool initialize(
        ID3D12Device* device,
        D3D12_DESCRIPTOR_HEAP_TYPE type,
        unsigned descriptors_per_frame,
        unsigned frames_in_flight,
        unsigned persistent_descriptors = 0);
    void begin_frame(unsigned frame_slot);

    struct Table {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    };
    // Reserves `count` consecutive slots; the caller copies SRVs in via
    // ID3D12Device::CopyDescriptors.
    Table allocate(unsigned count);
    // Reserves from a non-rewound prefix of the same shader-visible heap.
    // These descriptors must remain unchanged until all referencing GPU work
    // completes; frame-slot reuse never rewinds or overwrites this prefix.
    Table allocate_persistent(unsigned count);

    [[nodiscard]] ID3D12DescriptorHeap* heap() const { return heap_.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_base_{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_base_{};
    bool frame_ready_ = false;
    unsigned persistent_descriptors_ = 0;
    unsigned persistent_cursor_ = 0;
    unsigned descriptors_per_frame_ = 0;
    unsigned segment_base_ = 0;
    unsigned cursor_ = 0;
    unsigned increment_ = 0;
    unsigned frames_in_flight_ = 0;
};

namespace detail {

// EFB resources and these views share the renderer's initialize/shutdown
// lifetime. The source resources must outlive every submitted use of the heap.
// Returns color then depth; consumes exactly two persistent descriptors.
[[nodiscard]] std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 2>
create_efb_conversion_source_views(
    ID3D12Device* device, ID3D12Resource* color, ID3D12Resource* depth,
    DescriptorRing& ring);

// Exact color/alpha identity only: rejects filtering, conversion, resampling
// and any source rectangle that would require shader clamp behavior.
[[nodiscard]] std::optional<D3D12_BOX> efb_identity_copy_box(
    const EfbCopyParams& params, bool is_depth_copy, unsigned efb_scale,
    unsigned dest_width, unsigned dest_height) noexcept;
// Source enters/leaves RENDER_TARGET; destination enters/leaves PSR. Neither
// the copy nor its barriers changes any graphics binding on the command list.
void record_efb_identity_copy(
    ID3D12GraphicsCommandList* commands, ID3D12Resource* source,
    ID3D12Resource* destination, const D3D12_BOX& source_box);

}  // namespace detail

// One fully resolved draw.  The renderer sets PSO / root arguments only when
// they differ from the previous draw (cheap CPU-side batching).
struct DrawCall {
    ID3D12PipelineState* pipeline = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS vs_constants = 0;   // root CBV b0
    D3D12_GPU_VIRTUAL_ADDRESS ps_constants = 0;   // root CBV b1
    D3D12_GPU_DESCRIPTOR_HANDLE texture_table{};  // t0-t7
    D3D12_GPU_DESCRIPTOR_HANDLE sampler_table{};  // s0-s7
    D3D12_GPU_VIRTUAL_ADDRESS matrix_palette = 0; // root SRV t0,space1
    D3D12_VERTEX_BUFFER_VIEW vertex_view{};
    D3D12_INDEX_BUFFER_VIEW index_view{};
    std::uint32_t index_count = 0;
    D3D12_PRIMITIVE_TOPOLOGY topology =
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    D3D12_RECT scissor{};
    // Raw XF viewport registers 0x101A-0x101F in hardware order:
    // [0]=wd (w/2), [1]=ht (-h/2), [2]=zRange ((f-n)*(2^24-1)),
    // [3]=xOrig (cx+342), [4]=yOrig (cy+342), [5]=farZ (f*(2^24-1)).
    // has_viewport=false (XF regs not yet written) falls back to full EFB.
    float xf_viewport[6]{};
    bool  has_viewport = false;
    // Render target 1 in temporal scenes: 1 the motion target (opaque
    // motion writers), 2 the reactive mask (blended draws), 0 none.
    std::uint8_t aux_target = 0;
    // Interpolated redraw: the scene transform record whose VS constants a
    // sub-frame replaces, -1 for draws outside the 3D scene.
    std::int32_t replay_record = -1;
    // Sub-frames only: the matching draw's vertices in the frame before,
    // blended with this draw's (root SRV t1, space1); 0 for none. The pair's
    // GPU verdict (same geometry or not) is at `blend_slot`.
    D3D12_GPU_VIRTUAL_ADDRESS blend_vertices = 0;
    std::uint32_t blend_slot = 0;
    // Interpolated redraw: the scene's pixel-constant record, -1 for none.
    std::int32_t ps_record = -1;
};

class RendererD3D12 {
public:
    // Creates the Win32 window, device, queue, swap chain, EFB targets at
    // kEfbWidth x kEfbHeight x efb_scale, the shared GX root signature, the
    // present-blit pipeline, and all rings.
    bool initialize(int window_width, int window_height, unsigned efb_scale);
    // Creates the EFB-peek and pointer-depth resources up front. Creating them on first
    // use stalled the first frame that showed the pointer.
    void create_first_use_resources();
    void shutdown();                // full GPU drain, then release
    void drain_gpu();               // wait while keeping renderer resources alive
    void pump_messages();           // compatibility hook; owner pumps independently
    [[nodiscard]] bool quit_requested() const noexcept;

    // Advances the frame ring: waits on the fence of frame N-kFramesInFlight
    // only, resets that slot's command allocator, opens the command list,
    // and rewinds the slot's ring segments. Swap-chain latency is waited in
    // present(), after GX/EFB work is recorded but before the backbuffer is
    // selected, so gameplay work is not paced by DWM too early.
    void begin_frame(bool present_swap_chain = true);
    // Closes, submits, and signals once the GPU command stream has accepted
    // this frame. Swap-chain presentation is optional and intentionally comes
    // after the resource fence so DWM/display pacing cannot block command
    // allocator and upload-ring reuse.
    // Called synchronously after successful queue signal/readback binding,
    // before optional pacing/Present. The context is never retained.
    using SubmissionCallback = void (*)(void* context, std::uint64_t fence_value);
    std::uint64_t end_frame(
        bool present_swap_chain = true,
        SubmissionCallback on_submitted = nullptr,
        void* submission_context = nullptr);
    [[nodiscard]] std::uint64_t completed_fence_value() const;
    // Bounded wait for one exact queue signal. Used only by the explicitly
    // synchronous renderer path; normal async rendering never drains the GPU.
    void wait_for_fence_value(std::uint64_t fence_value);

    // Binds EFB RTV+DSV and applies the viewport from the XF viewport regs.
    void bind_efb(const float xf_viewport[6]);

    // HUD split: from the end of the guest's 3D scene to the display copy the
    // EFB is a second target at the window's integer scale, so the 2D layer
    // renders at display resolution over the resampled scene. The two target
    // sets share layout and resource states and are swapped as a whole, so
    // every EFB path (draws, clears, copies, peeks) works unchanged on either.
    // begin_output_efb returns false when the split is off or the EFB already
    // covers the window.
    [[nodiscard]] bool begin_output_efb();
    void end_output_efb();
    [[nodiscard]] bool output_efb_active() const noexcept { return output_efb_active_; }
    // Whether the targets for `scale` fit the adapter's video-memory budget
    // next to everything already resident.
    [[nodiscard]] bool efb_scale_fits_memory(unsigned scale) const;

    // Temporal upscaling (upscaler mode 2). At the start of the guest's 3D
    // scene: whether the temporal path runs for it and, if so, the scene's
    // sub-pixel jitter in render pixels (applied to perspective projections).
    // The temporal upscaler (FSR 3) is selected and available.
    [[nodiscard]] bool temporal_mode() const;
    [[nodiscard]] bool scene_temporal_begin(float& jitter_x, float& jitter_y);
    // After a temporal scene_temporal_begin: clears the motion target for
    // the scene. False when it could not be created (no motion writers).
    [[nodiscard]] bool begin_motion_scene();
    // GALAXY_MOTION_DEBUG: at the scene's end, replaces the EFB color with
    // the motion field (grey = still; red/green = +x/+y towards the previous
    // position, 8 pixels to full scale).
    void record_motion_debug_view();
    // The temporal history does not match the picture (scene cut).
    void reset_temporal_history() noexcept { temporal_history_valid_ = false; }

    // Ray tracing (galaxy/gx/ray_tracing.h): the 3D scene's opaque draws, noted as they are
    // drawn, become the traced scene when the game's translucent phases begin.
    [[nodiscard]] bool rt_active() const noexcept { return ray_tracer_.ready(); }
    // Starts or stops the ray tracer at a frame boundary to follow the RAY TRACING setting.
    void reconcile_ray_tracing();
    [[nodiscard]] bool rt_scene_open() const noexcept { return ray_tracer_.scene_open(); }
    [[nodiscard]] bool rt_glow_open() const noexcept { return ray_tracer_.glow_open(); }
    [[nodiscard]] float rt_glow_exposure() const noexcept {
        return ray_tracer_.ready() ? ray_tracer_.glow_exposure() : 1.0f;
    }
    [[nodiscard]] float rt_glow_spill() const noexcept { return ray_tracer_.glow_spill(); }
    void rt_begin_scene() noexcept { ray_tracer_.begin_scene(); }
    void rt_set_view(const RtView& view) noexcept { ray_tracer_.set_view(view); }
    void rt_set_camera(const std::array<float, 12>* view) noexcept { ray_tracer_.set_camera(view); }
    [[nodiscard]] bool rt_lighting() const noexcept { return ray_tracer_.lighting_active(); }
    [[nodiscard]] bool rt_sun_direction(std::array<float, 3>& world) const noexcept {
        return ray_tracer_.sun_direction(world);
    }
    [[nodiscard]] float rt_point_radius() const noexcept { return ray_tracer_.point_radius(); }
    void rt_note_draw(const DrawCall& call, const RtDrawInfo& info);
    void rt_trace_scene();
    // The scene's end: its finished picture becomes the light the next frame's rays read.
    void rt_capture_picture();
    // Matrix-ring segment advances back whose palettes are still alive while
    // this frame executes (0 outside temporal mode). In temporal mode the
    // segment advances only after frames that wrote palettes, so present and
    // peek frames do not age the previous scene's palettes.
    [[nodiscard]] unsigned matrix_history_frames() const noexcept {
        return matrix_segments_ - kFramesInFlight;
    }
    [[nodiscard]] std::uint64_t matrix_segment_serial() const noexcept {
        return matrix_segment_serial_;
    }
    [[nodiscard]] std::uint64_t frame_index() const noexcept { return frame_index_; }

    // Interpolated redraw (present rates above 60). While recording, a render
    // chunk's EFB operations (draws, EFB bindings, clears, copies, EFB set
    // swaps) are logged. The completed log is replayed by the sub-frames of
    // its sequence (begin_replay_sequence, present_replay_subframe).
    void begin_replay_record();
    void end_replay_record(bool keep);
    [[nodiscard]] bool replay_recording() const noexcept { return replay_recording_; }
    [[nodiscard]] bool replay_available() const noexcept { return replay_valid_; }
    // Moves the completed log to playback (sub-frames then replay it); false
    // when none is ready.
    bool start_replay_playback();
    [[nodiscard]] bool replay_playing() const noexcept { return run_valid_; }
    // Replay serial (game frame) of the playing log.
    [[nodiscard]] std::uint64_t playing_serial() const noexcept { return run_serial_; }
    // Interpolation mode: present rate above 60, temporal upscaler off, not
    // GALAXY_INTERPOLATION=0. Rings then keep one segment per
    // game frame (advanced at the first frame after a display copy) and the
    // log spans every render chunk of the game frame up to its display copy.
    [[nodiscard]] bool interpolation_mode() const;
    // The game-frame ring segment in interpolation mode (resources a log
    // names must outlive it: it comes round again only after its fence),
    // ~0u otherwise.
    [[nodiscard]] unsigned replay_hold_segment() const noexcept {
        return ring_mode_ == RingMode::Interp ? ring_segment_ : ~0u;
    }
    // Completed logs, counted; the guest XFB the latest one copied to.
    [[nodiscard]] std::uint64_t replay_serial() const noexcept { return replay_serial_; }
    [[nodiscard]] std::uint32_t replay_xfb_addr() const noexcept { return replay_xfb_addr_; }
    // A 3D scene began inside the log being recorded (one is replayable).
    void note_replay_scene() noexcept { ++replay_scene_count_; }
    // Why the latest closed log is not replayable: 0 it is, 1 no 3D scene,
    // 2 more than one.
    [[nodiscard]] unsigned replay_reject() const noexcept { return replay_reject_; }
    // Last reason a sub-frame was not recorded (diagnostics).
    [[nodiscard]] unsigned replay_failure() const noexcept { return replay_failure_; }
    void note_replay_failure(unsigned reason) noexcept { replay_failure_ = reason; }
    // The next sub-frame's backbuffer is read back to the debug capture path.
    void set_subframe_capture(bool capture) noexcept { subframe_capture_ = capture; }
    // Captures wait for the previous readback instead of skipping (sequences).
    void set_capture_waits(bool waits) noexcept { capture_waits_ = waits; }
    // The playing log's sub-frames. begin_replay_sequence records the replay
    // once per sequence: draws of moving records read their constants from the
    // evaluation output (interp_gpu.h), draws matched by prepare_vertex_blend
    // blend their vertices with the frame before's, and the display copy lands
    // in a renderer-owned XFB. present_replay_subframe evaluates the sequence's
    // program at t and executes that replay. False when the log is gone or has
    // no display copy (the sequence then shows its real frame only).
    struct ReplaySequence {
        interp_gpu::Layout layout{};
        std::span<const std::int32_t> record_vs_slot;       // per scene record, -1 none
        std::span<const std::int32_t> record_palette_slot;  // per scene record, -1 none
        std::span<const std::int32_t> ps_slot;              // per pixel-constant record, -1 none
        std::span<const std::uint32_t> palette_source;      // per palette slot: float4 offset in the matrix ring
        const interp_gpu::Program* program = nullptr;
        bool vertex_blend = false;
    };
    bool begin_replay_sequence(const ReplaySequence& sequence);
    // A stripe particle's vertex in the draw logged next (JPA stripes, two
    // vertices per particle): interpolated per particle between game frames.
    struct StripeVertex {
        std::uint32_t vertex = 0;        // in the draw's vertex buffer
        std::uint32_t particle = 0;      // JPABaseParticle
        std::uint32_t age_lifetime = 0;  // age << 16 | lifetime
        std::uint8_t strip = 0;          // JPADrawStripe 0, JPADrawStripeX passes 1-2
        std::uint8_t side = 0;           // the particle's first or second vertex
    };
    void note_stripe_vertices(std::span<const StripeVertex> vertices);
    // A presented frame of the sequence: t, the t of the frame presented before
    // it (motion vectors) and its jitter in render pixels (temporal upscaling).
    struct SubframeTime {
        float t = 0.0f;
        float t_prev = 0.0f;
        float jitter_x = 0.0f;
        float jitter_y = 0.0f;
        bool reset = false;   // the upscaler's history starts again (cut, gap)
    };
    bool present_replay_subframe(const SubframeTime& time, const Affine34& camera_t, const Affine34& camera_prev);
    // The temporal upscaler upscales the replays (every presented frame), not the game frames.
    [[nodiscard]] bool temporal_interpolation() const noexcept { return temporal_interp_; }
    // GALAXY_INTERP_EVAL_CHECK: the evaluation output of the next sub-frame is
    // read back; take_eval_readback returns it once complete (empty until then).
    void request_eval_readback() noexcept { eval_readback_requested_ = true; }
    [[nodiscard]] bool take_eval_readback(std::vector<std::uint32_t>& words, float& t);
    // CPU time of begin_replay_sequence and replay recordings since the last
    // call; sequences begun and replays recorded.
    void take_sequence_record_ms(double& total_ms, std::uint64_t& samples, std::uint64_t& replays) noexcept {
        total_ms = std::exchange(sequence_record_ms_, 0.0);
        samples = std::exchange(sequence_record_samples_, 0u);
        replays = std::exchange(replay_records_, 0u);
    }
    // Once per sequence: pairs each draw of the playing log with the draw of
    // the log before it that has the same position among its record's draws
    // (`previous_record[i]`: the record of the scene before matched to record
    // i, or -1) and as many vertices in a different buffer, so CPU-written
    // geometry (shadow volumes, particle strips, trails) moves per sub-frame.
    void prepare_vertex_blend(std::span<const std::int32_t> previous_record);
    // After prepare_vertex_blend: per pixel-constant record of the playing
    // log, the record of the log before that its paired draws used (-1 none,
    // -2 paired draws disagree).
    [[nodiscard]] std::span<const std::int32_t> ps_record_pairs() const noexcept {
        return run_ps_previous_;
    }
    // The resident decoded-vertex buffer (blend pairs may live in it); 0 for none.
    void set_resident_vertices(D3D12_GPU_VIRTUAL_ADDRESS base, std::uint64_t bytes) noexcept {
        resident_vertex_base_ = base;
        resident_vertex_bytes_ = bytes;
    }
    // Swap-chain presents per second over the last second (any source).
    [[nodiscard]] double measured_present_hz() const noexcept {
        return measured_present_hz_.load(std::memory_order_relaxed);
    }
    // GPU milliseconds of completed sub-frames since the last call.
    void take_subframe_path_gpu_ms(std::array<double, 3>& total_ms,
                                   std::array<std::uint64_t, 3>& samples) noexcept {
        total_ms = std::exchange(subframe_path_gpu_ms_, {});
        samples = std::exchange(subframe_path_gpu_samples_, {});
    }
    // Smoothed GPU time of one sub-frame (0 until measured), for the budget.
    [[nodiscard]] double subframe_gpu_ema_ms() const noexcept { return subframe_gpu_ema_ms_; }
    // The GPU cannot afford a replay per presented frame: the game frames take
    // the temporal upscaler (60 presents) until the display or upscaler
    // settings change.
    void suspend_temporal_interpolation(double ms_per_frame);
    void reset_subframe_gpu_ema() noexcept { subframe_gpu_ema_ms_ = 0.0; }
    // GPU milliseconds of completed sub-frames by phase (evaluation, replay,
    // present) since the last call.
    [[nodiscard]] std::array<double, 3> take_subframe_phase_ms() noexcept {
        return std::exchange(subframe_phase_ms_, {});
    }
    void take_subframe_gpu_ms(double& total_ms, std::uint64_t& samples) noexcept {
        total_ms = subframe_gpu_ms_;
        samples = subframe_gpu_samples_;
        subframe_gpu_ms_ = 0.0;
        subframe_gpu_samples_ = 0u;
    }
    // Render-thread milliseconds recording and submitting sub-frames (no
    // fence, latency or present waits) since the last call.
    void take_subframe_record_ms(double& total_ms, std::uint64_t& samples) noexcept {
        total_ms = subframe_record_ms_;
        samples = subframe_record_samples_;
        subframe_record_ms_ = 0.0;
        subframe_record_samples_ = 0u;
    }
    // Copy textures saved and restored around sub-frames since the last call.
    [[nodiscard]] std::uint64_t take_subframe_guarded() noexcept {
        return std::exchange(subframe_guarded_total_, 0u);
    }
    // A texture table the backend just filled while a log records, naming
    // EFB-copy textures (`copies[m]`, null for other slots). The log keeps CPU
    // copies of its kReplayTableSlots descriptors (`sources`, valid now) so
    // sub-frames can point the copy slots elsewhere.
    static constexpr unsigned kReplayTableSlots = 8u;
    void note_replay_texture_table(D3D12_GPU_DESCRIPTOR_HANDLE table,
                                   const D3D12_CPU_DESCRIPTOR_HANDLE* sources,
                                   ID3D12Resource* const* copies);
    // Sub-frame copy redirection since the last call: copies written to
    // redirect targets, tables patched, read-before-write snapshots taken by
    // real frames, logs that fell back to the guard (a table was not kept).
    struct CopyRedirectStats {
        std::uint64_t redirected = 0;
        std::uint64_t patched_tables = 0;
        std::uint64_t snapshots = 0;
        std::uint64_t fallback_logs = 0;
    };
    [[nodiscard]] CopyRedirectStats take_copy_redirect_stats() noexcept {
        return std::exchange(copy_redirect_stats_, CopyRedirectStats{});
    }
    // Diagnostics (A/B in one run): 1 redirect, 0 guard, 2 neither for the
    // next sub-frames; -1 follows GALAXY_INTERP_COPY_REDIRECT.
    void set_subframe_copy_redirect(int mode) noexcept { subframe_copy_redirect_ = mode; }
    [[nodiscard]] std::uint64_t take_log_gap_ops() noexcept {
        return std::exchange(log_gap_ops_, 0u);
    }
    // Sub-frame draws drawn with blended vertices since the last call.
    [[nodiscard]] std::uint64_t take_vertex_blend_draws() noexcept {
        return std::exchange(vertex_blend_draws_, 0u);
    }
    // The scene camera's clip planes and vertical field of view (radians).
    void set_scene_camera(float near_plane, float far_plane, float fov_y) noexcept;
    // GX copy-clear: color from the latched AR/GB regs, depth from z24
    // (converted z24 / 0xFFFFFF into the D24 target).  The clear is scoped to
    // the given rect in unscaled EFB pixels (w==0 -> full target), matching
    // hardware: PE_COPY_EXECUTE's clear bit wipes the copy's src rect only
    // (Dolphin BPStructs ClearScreen(srcRect)).
    void clear_efb(
        std::uint32_t clear_ar,
        std::uint32_t clear_gb,
        std::uint32_t clear_z24,
        bool color_enable,
        bool alpha_enable,
        bool depth_enable,
        unsigned rect_x = 0,
        unsigned rect_y = 0,
        unsigned rect_w = 0,
        unsigned rect_h = 0,
        std::uint8_t pixel_format = 0);

    void draw(const DrawCall& call);
    // GALAXY_VALIDATE_DRAW_ADDRESSES=1: logs draws whose GPU addresses lie
    // outside every buffer the renderer owns (device-removal triage).
    void check_draw_addresses(const DrawCall& call);

    // EFB copy paths (record into the current command list). Both run the
    // conversion blit (intensity / gamma / 3-tap vertical copy filter /
    // half-scale box / XFB y-scale stretch / depth-source packing) instead
    // of a raw CopyTextureRegion, following Dolphin TextureConverterShaderGen
    // semantics. `is_depth_copy` is true when the PE pixel format is Z24 at copy time
    // (Dolphin BPStructs BPMEM_TRIGGER_EFB_COPY).
    void copy_efb_to_xfb(const EfbCopyParams& params, XfbTexture& dest);
    [[nodiscard]] bool copy_efb_to_texture(
        const EfbCopyParams& params,
        TextureCache& textures,
        bool is_depth_copy);

    // Stretch-blit `source` to the backbuffer. A null/invalid source is a
    // runtime bug: callers must keep the last valid swap-chain image instead
    // of submitting a black frame.
    // `capture_backbuffer` records the final post-blit swap-chain image.
    // Returns whether an optional backbuffer capture was recorded. Busy
    // diagnostic buffers never suppress the required blit/presentation.
    bool present(const XfbTexture* source, bool capture_backbuffer = false);

    // Bring-up diagnostics: record an EFB-to-readback copy into the current
    // command list (call while recording). Readback and PPM encoding are
    // polled asynchronously after end_frame(); normal rendering never drains
    // the GPU for an opt-in capture.
    [[nodiscard]] bool capture_pointer_depth(std::span<std::uint32_t> pixels);
    // Render-owner only. Keeps shared GXPeek utility resources owned until the
    // entire exact field has been copied by try_complete_pointer_depth_capture.
    [[nodiscard]] std::uint64_t submit_pointer_depth_capture();
    // Records the pointer depth field capture into the frame's own command
    // list at the current point of the stream (the simulation thread marks the
    // FIFO offset of the scene boundary). The readback completes at this
    // frame's fence; try_complete_pointer_depth_capture publishes it. False
    // while another pointer field is still owned.
    [[nodiscard]] bool record_pointer_depth_capture_in_frame();
    [[nodiscard]] std::uint64_t pointer_depth_capture_fence() const noexcept {
        return pointer_depth_capture_fence_;
    }
    // In place: on completion `rgba` names the mapped field (null when the
    // readback could not be mapped; the span overload converts instead).
    [[nodiscard]] bool try_complete_pointer_depth_capture_in_place(
        const std::uint8_t*& rgba);
    [[nodiscard]] bool try_complete_pointer_depth_capture(
        std::span<std::uint32_t> pixels);
    // Render-owner only. Asynchronous 640x528 EFB fields (depth for GXPeekZ,
    // color for GXPeekARGB) for the simulation thread, each on private
    // resources so they never contend with the pointer field above or with
    // each other. Returns the fence value, or 0 when that field is in flight.
    static constexpr std::size_t kAsyncPeekFieldSlots = 3u;
    // Captures the field into readback `slot` (0..2). Completion leaves it
    // there; async_peek_field_rgba names the mapped bytes (RGBA8, 640x528).
    [[nodiscard]] std::uint64_t submit_async_peek_field(EfbPeekKind kind, std::size_t slot);
    // The same field recorded at this point of the open frame (the PE token that follows the
    // game's alpha marks); end_frame fences it.
    [[nodiscard]] bool record_async_peek_field_in_frame(EfbPeekKind kind, std::size_t slot);
    [[nodiscard]] bool try_complete_async_peek_field(EfbPeekKind kind);
    [[nodiscard]] const std::uint8_t* async_peek_field_rgba(
        EfbPeekKind kind, std::size_t slot) const noexcept;
    [[nodiscard]] bool async_peek_field_pending(EfbPeekKind kind) const noexcept {
        const AsyncPeekField& field = async_peek_fields_[async_peek_field_index(kind)];
        return field.fence != 0u || field.in_frame;
    }
    [[nodiscard]] bool any_async_peek_field_pending() const noexcept {
        return async_peek_field_pending(EfbPeekKind::Depth) || async_peek_field_pending(EfbPeekKind::Color);
    }
    static constexpr std::size_t async_peek_field_index(EfbPeekKind kind) noexcept {
        return kind == EfbPeekKind::Depth ? 0u : 1u;
    }
    [[nodiscard]] bool peek_efb(
        std::uint16_t x,
        std::uint16_t y,
        EfbPeekKind kind,
        std::uint32_t& value);
    bool debug_copy_efb_to_readback();
    void debug_log_readback();
    void debug_log_backbuffer_readback();
    bool debug_copy_selected_xfb_to_readback(const XfbTexture& source);
    void debug_log_selected_xfb_readback();
    // The backend binds the exact output paths to the frame before recording
    // its optional readbacks. Paths are retained with the pending readback so
    // a later nonblocking completion cannot write to a subsequent frame's
    // diagnostic file.
    void set_debug_capture_paths(
        std::string efb_ppm_path,
        std::string backbuffer_ppm_path);

    [[nodiscard]] ID3D12Device* device() const { return device_.Get(); }
    [[nodiscard]] unsigned efb_scale() const noexcept { return efb_scale_; }
    [[nodiscard]] ID3D12GraphicsCommandList* command_list() const {
        return command_list_.Get();
    }
    [[nodiscard]] ID3D12RootSignature* root_signature() const {
        return root_signature_.Get();
    }
    [[nodiscard]] double display_refresh_hz() const noexcept { return display_refresh_hz_; }
    [[nodiscard]] UploadRing& vertex_ring() { return vertex_ring_; }
    [[nodiscard]] UploadRing& index_ring() { return index_ring_; }
    [[nodiscard]] UploadRing& constant_ring() { return constant_ring_; }
    [[nodiscard]] UploadRing& matrix_ring() { return matrix_ring_; }
    [[nodiscard]] DescriptorRing& srv_ring() { return srv_ring_; }
    [[nodiscard]] unsigned frame_slot() const { return frame_slot_; }
    // Live bytes held by the EFB-copy destination cache, in SCALED bytes (each
    // destination is allocated at the internal EFB extent, so its `bytes` grows with
    // efb_scale^2 while the 192 MiB budget stays fixed). Reported against its budget so
    // a run can show whether the pool is saturating. It is a resolution-scaled GPU pool
    // that is not a render target.
    [[nodiscard]] std::uint64_t efb_copy_dest_bytes() const {
        return efb_copy_dests_bytes_;
    }
    // The copy-destination byte budget this session is running with (192 MiB default,
    // GALAXY_EFB_COPY_DEST_BUDGET_MB overridable). Reported next to the live bytes so a
    // recording states whether the pool is saturating.
    [[nodiscard]] static std::uint64_t efb_copy_dest_budget_bytes();
    [[nodiscard]] std::uint64_t last_latency_wait_us() const {
        return last_latency_wait_us_;
    }
    [[nodiscard]] std::uint64_t last_frame_slot_wait_us() const {
        return last_frame_slot_wait_us_;
    }
    [[nodiscard]] std::uint64_t last_timestamp_read_us() const {
        return last_timestamp_read_us_;
    }
    [[nodiscard]] std::uint64_t last_begin_reset_us() const {
        return last_begin_reset_us_;
    }
    // Current frame's shared s0-s7 table for draws without dynamic samplers.
    // Exhaustion is explicit; defaults never replace a requested GX table.
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE default_sampler_table() const {
        return default_sampler_table_;
    }

    // Returns (building on first use) an 8-slot s0-s7 sampler table for the
    // given packed per-map sampler keys (see pack_sampler_key in
    // gx_backend.cpp). Tables are exact-key cached within this frame and remain
    // GPU-valid until its slot fence. Never retain a handle across begin_frame.
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE sampler_table_for(
        const std::uint32_t keys[8]);

    // Lazily loads and uploads the native-settings-overlay corner texture
    // (see the member comment below). Safe to call every frame -- after
    // the first attempt (success or failure) it is a no-op. Called by the
    // free-function draw_native_settings_overlay() (see renderer_d3d12.cpp)
    // once it knows the panel layout, hence public rather than a private
    // detail of this class alone.
    void ensure_native_settings_corner_texture_loaded();
    // Draws one textured quad (screen pixel coordinates) using the
    // extracted-game-asset texture, with UV min/max so the same texture can
    // be flipped/mirrored for each panel corner without four separate
    // textures. No-op if the corner texture failed to load.
    void draw_ui_textured_quad(
        int x, int y, int w, int h,
        float u0, float v0, float u1, float v1,
        const std::array<float, 4>& tint);

    // Lazily creates the solid-fill quad root signature/PSO. Unlike the corner texture,
    // this never depends on game assets, so it always succeeds once the device exists.
    // Draw-call based on purpose: ClearRenderTargetView with a non-null pRects faulted
    // inside nvwgf2umx.dll (STATUS_STACK_BUFFER_OVERRUN) on one driver build for the
    // overlay's panel and text rects. Full-target clears (pRects == nullptr) are
    // unaffected and still used elsewhere.
    void ensure_native_settings_solid_pipeline_loaded();
    // Draws one flat-colored quad (screen pixel coordinates) via a real
    // draw call rather than a rect-limited clear. No-op (and does not
    // create device resources) until first called after the device exists.
    void draw_ui_solid_quad(
        int x, int y, int w, int h,
        const std::array<float, 4>& color);
    // Packed row-major 5x7 bitmap, drawn with one instanced submission.
    void draw_ui_glyph(
        int x, int y, int scale, std::uint64_t bitmap,
        const std::array<float, 4>& color);

private:
    void invalidate_gx_bindings();
    void pace_present_if_needed(unsigned vsync_interval, float max_fps);
    // VSYNC ON presents each frame for round(refresh / rate) refreshes when
    // the refresh is a multiple of the present rate (60 at 120 Hz, 30 at
    // 60 Hz), so every frame stays on screen equally long; other rates keep
    // one refresh.
    [[nodiscard]] UINT present_sync_interval(unsigned configured, float max_fps);
    void refresh_display_rate();
    void wait_for_frame_latency_if_needed(bool collect_present_telemetry);
    void wait_for_gpu();            // shutdown/resize only
    // Applies a pending live window resize, if one is recorded (see
    // the coherent extent mailbox in renderer_d3d12.cpp). No-op when none is pending.
    // Only ever called from begin_frame(), i.e. between frames with no
    // in-flight command-list recording on this renderer.
    void apply_pending_resize();
    // Resizes the swap-chain buffers; `force` also when the size is unchanged
    // (required after a fullscreen transition with the flip model).
    void resize_swap_chain(UINT width, UINT height, bool force);
    // Brings the window and swap chain to the configured display (monitor,
    // windowed/borderless/exclusive fullscreen, mode, window size) at a frame
    // boundary.
    void reconcile_display_state();
    [[nodiscard]] bool enter_exclusive_fullscreen(
        const std::wstring& monitor_device, unsigned width, unsigned height,
        unsigned refresh_hz);
    void leave_exclusive_fullscreen();
    void reap_debug_capture_writers(bool wait_for_all);
    void queue_debug_ppm_capture(
        const char* tag,
        const std::string& path,
        const std::uint8_t* source,
        unsigned width,
        unsigned height,
        unsigned row_pitch);
    bool create_efb_targets(unsigned efb_scale);
    void carry_over_efb_contents();
    [[nodiscard]] unsigned output_efb_scale_for_window() const;
    [[nodiscard]] unsigned temporal_render_scale(unsigned output_scale) const;
    [[nodiscard]] bool ensure_motion_target();
    [[nodiscard]] bool record_temporal_upscale(unsigned output_scale);
    [[nodiscard]] bool ensure_output_efb(unsigned scale);
    void swap_efb_sets();
    // Live internal-resolution change, applied between frames by begin_frame
    // when get_render_config().efb_scale differs from the live targets.
    void apply_pending_efb_scale();
    // With internal_follow_window, re-derive efb_scale from the settled
    // window size (see RenderConfig::internal_follow_window).
    void follow_window_internal_resolution();
    // Hands changed presentation settings (overlay or config) to the
    // post-processor at the frame boundary.
    void sync_post_process_settings();
    UINT follow_window_width_ = 0;
    UINT follow_window_height_ = 0;
    double follow_window_aspect_ = 0.0;
    // A scale refused for the memory budget is tried again at this tick.
    std::uint64_t follow_window_retry_ms_ = 0;
    // (Re)creates the EFB color/depth conversion SRVs in their fixed slots.
    void refresh_efb_conversion_source_views();
    bool create_root_signature();
    bool create_sampler_heap();
    void reset_sampler_frame(); // only after this slot's fence completes
    bool create_conversion_pipeline();  // EFB-copy conversion blit PSO
    bool create_clear_pipeline();
    bool ensure_peek_resources();

    // Records the conversion blit: fullscreen triangle into `dest` (RTV),
    // sampling the EFB color (or depth) SRV.  (src_x, src_y) is the source
    // origin and (step_x, step_y) the source texels advanced per dest pixel,
    // all in scaled EFB texels.  Caller handles dest resource transitions;
    // EFB transitions happen inside.  Restores GX root signature/EFB
    // binding before returning.
    void record_conversion_blit(
        ID3D12Resource* dest,
        unsigned dest_w,
        unsigned dest_h,
        D3D12_CPU_DESCRIPTOR_HANDLE dest_rtv,
        const EfbCopyParams& params,
        bool is_depth_copy,
        bool intensity,
        bool keep_alpha,
        std::uint8_t copy_format,
        float src_x,
        float src_y,
        float step_x,
        float step_y,
        float filter_row_offset);
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE allocate_conversion_rtv(
        ID3D12Resource* resource);

    WindowOwner window_owner_;
    HWND window_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    // The selected adapter, for video-memory budget queries.
    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>,
        kFramesInFlight> allocators_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    std::array<std::uint64_t, kFramesInFlight> fence_values_{};
    HANDLE fence_event_ = nullptr;
    HANDLE frame_latency_waitable_ = nullptr;
    bool exclusive_fullscreen_active_ = false;
    // The display the window and swap chain were last brought to.
    struct AppliedDisplay {
        bool valid = false;
        unsigned mode = 0;
        std::array<char, 64> monitor{};
        unsigned fullscreen_width = 0;
        unsigned fullscreen_height = 0;
        unsigned fullscreen_refresh_hz = 0;
        unsigned window_width = 0;
        unsigned window_height = 0;
    };
    AppliedDisplay applied_display_{};
    std::uint64_t display_checks_ = 0;
    bool tearing_supported_ = false;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> timestamp_query_heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> timestamp_readback_;
    std::array<bool, kFramesInFlight> timestamp_pending_{};
    std::uint64_t timestamp_frequency_ = 0;

    // EFB.
    Microsoft::WRL::ComPtr<ID3D12Resource> efb_color_;
    Microsoft::WRL::ComPtr<ID3D12Resource> efb_depth_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsv_heap_;
    unsigned efb_scale_ = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE efb_rtv_{};
    D3D12_CPU_DESCRIPTOR_HANDLE efb_dsv_{};
    // The inactive EFB target set (the output-resolution one between frames).
    struct EfbTargetSet {
        Microsoft::WRL::ComPtr<ID3D12Resource> color;
        Microsoft::WRL::ComPtr<ID3D12Resource> depth;
        unsigned scale = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
        std::optional<DescriptorRing::Table> conv_table;
        D3D12_GPU_DESCRIPTOR_HANDLE conv_color_srv{};
        D3D12_GPU_DESCRIPTOR_HANDLE conv_depth_srv{};
    };
    EfbTargetSet alt_efb_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> alt_efb_rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> alt_efb_dsv_heap_;
    bool output_efb_active_ = false;
    std::unique_ptr<TemporalUpscaler> temporal_;
    unsigned temporal_kind_ = 0;       // upscaler setting it was made for (2 FSR 3, 3 DLSS)
    unsigned temporal_quality_ = 0;
    unsigned temporal_render_w_ = 0;
    unsigned temporal_render_h_ = 0;
    unsigned temporal_output_w_ = 0;
    unsigned temporal_output_h_ = 0;
    std::uint64_t temporal_frame_ = 0;
    bool temporal_history_valid_ = false;
    bool scene_temporal_ = false;
    float scene_jitter_[2]{};
    float scene_camera_[3]{1.0f, 100000.0f, 1.0f};
    Microsoft::WRL::ComPtr<ID3D12Resource> motion_target_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> motion_rtv_heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE motion_rtv_{};
    unsigned motion_target_scale_ = 0;
    // Interpolated redraw state (see begin_replay_record).
    struct ReplayCopy {
        ID3D12Resource* dest = nullptr;
        unsigned width = 0;
        unsigned height = 0;
        EfbCopyParams params{};
        bool depth = false;
        bool intensity = false;
        bool keep_alpha = false;
        std::uint8_t format = 0;
        float geometry[5]{};  // src x, y, step x, y, filter row offset
        bool identity = false;
        D3D12_BOX box{};
        std::uint8_t mode = 0;  // 0 texture, 1 display copy, 2 output-EFB resample
    };
    struct ReplayClear {
        std::uint32_t ar = 0, gb = 0, z24 = 0;
        bool color = false, alpha = false, depth = false;
        unsigned x = 0, y = 0, w = 0, h = 0;
        std::uint8_t format = 0;
    };
    struct ReplayViewport {
        float values[6]{};
        bool valid = false;
    };
    // RtComposite: the ambient occlusion of ray-tracer result slot `index`.
    enum class ReplayKind : std::uint8_t { Draw, BindEfb, Clear, Copy, SwapEfbSets, ClearDepth, RtComposite };
    struct ReplayOp {
        ReplayKind kind = ReplayKind::Draw;
        std::uint32_t index = 0;
    };
    void present_to_swap_chain();
    // `target`: write a texture copy (mode 0) there instead of copy.dest.
    void execute_replay_copy(const ReplayCopy& copy, ID3D12Resource* target = nullptr);
    bool ensure_subframe_resources(unsigned xfb_width, unsigned xfb_height);
    // The log being recorded; a completed log moves to play_* (swapped), so
    // the next game frame records while the previous one is interpolated.
    std::vector<ReplayOp> replay_ops_;
    std::vector<DrawCall> replay_draws_;
    std::vector<ReplayViewport> replay_viewports_;
    std::vector<ReplayClear> replay_clears_;
    std::vector<ReplayCopy> replay_copies_;
    // The display copy's clear runs after the copy closed the log; the next
    // log starts with it so sub-frames begin from the same EFB as the frame.
    ReplayClear log_start_clear_{};
    bool log_start_clear_valid_ = false;
    // Draws and texture copies between a log's display copy and the next log
    // (missing from both logs; expected 0).
    std::uint64_t log_gap_ops_ = 0;
    std::vector<ReplayOp> play_ops_;
    std::vector<DrawCall> play_draws_;
    std::vector<ReplayViewport> play_viewports_;
    std::vector<ReplayClear> play_clears_;
    std::vector<ReplayCopy> play_copies_;
    bool play_start_output_active_ = false;
    void publish_replay_log();
    // The log being interpolated (moved from play_* by start_replay_playback),
    // so a newer completed log can wait while sub-frames of this one run.
    std::vector<ReplayOp> run_ops_;
    std::vector<DrawCall> run_draws_;
    std::vector<ReplayViewport> run_viewports_;
    std::vector<ReplayClear> run_clears_;
    std::vector<ReplayCopy> run_copies_;
    bool run_start_output_active_ = false;
    bool run_valid_ = false;
    unsigned run_segment_ = 0;
    bool replay_recording_ = false;
    bool replay_valid_ = false;
    unsigned replay_reject_ = 0;
    unsigned replay_failure_ = 0;
    bool subframe_capture_ = false;
    bool capture_waits_ = false;
    unsigned replay_scene_count_ = 0;
    unsigned replay_segment_ = 0;
    std::uint64_t replay_serial_ = 0;
    std::uint32_t replay_xfb_addr_ = 0;
    // Ring segmentation (begin_frame): Normal = frame slots; Temporal = the
    // matrix ring advances after palette-writing frames; Interp = every ring
    // and the sampler heap advance once per game frame.
    enum class RingMode : std::uint8_t { Normal, Temporal, Interp };
    RingMode ring_mode_ = RingMode::Normal;
    unsigned ring_segment_ = 0;
    std::array<std::uint64_t, kInterpSegments> ring_segment_fence_{};
    bool ring_advance_pending_ = true;
    // Sub-frames render into their own EFB so a later frame's partial EFB
    // (its chunks interleave with the sub-frames) stays intact.
    EfbTargetSet replay_efb_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> replay_efb_rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> replay_efb_dsv_heap_;
    // The output-resolution EFB of a sub-frame whose log switches to it.
    EfbTargetSet replay_output_efb_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> replay_output_efb_rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> replay_output_efb_dsv_heap_;
    // persistent_views: the conversion-source views live in the descriptor
    // ring's persistent prefix (else the caller writes them per use).
    // unordered_access: the colour target takes compute writes (the temporal
    // upscaler's output, as the live output EFB).
    bool create_efb_set(EfbTargetSet& set, Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& rtv_heap,
                        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& dsv_heap, unsigned scale,
                        bool persistent_views = true, bool unordered_access = false);
    void write_efb_set_views(EfbTargetSet& set, const DescriptorRing::Table& table);
    void swap_efb_with(EfbTargetSet& other);
    bool replay_playing_ = false;
    bool replay_start_output_active_ = false;
    unsigned replay_source_slot_ = 0;
    XfbTexture subframe_xfb_{};
    // EFB-copy textures are shared with the live frames: a later game frame's
    // chunks may already have rewritten a texture the sub-frame's log copies
    // to. Such textures are saved to scratch before the sub-frame and restored
    // after it. copy_writes_: the latest replay serial each copy texture was
    // written under (Interp mode, real copies only).
    struct CopyWrite {
        ID3D12Resource* dest = nullptr;
        std::uint64_t serial = 0;
    };
    std::vector<CopyWrite> copy_writes_;
    void note_copy_write(ID3D12Resource* dest);
    std::uint64_t play_serial_ = 0;       // replay serial of the ready log
    std::uint64_t run_serial_ = 0;        // replay serial of the playing log
    // The log played before run_* (its vertices stay in its ring segment
    // until the segment comes round again) and, per run_* draw, its matched
    // draw's vertices (prepare_vertex_blend; 0 for none).
    std::vector<DrawCall> prev_run_draws_;
    std::vector<ReplayCopy> prev_run_copies_;
    std::uint64_t moving_copies_ = 0;     // paired texture copies whose source moved (prepare_vertex_blend)
    unsigned prev_run_segment_ = 0;
    std::uint64_t prev_run_serial_ = 0;
    bool prev_run_valid_ = false;
    struct BlendSource {
        D3D12_GPU_VIRTUAL_ADDRESS vertices = 0;
        std::uint32_t slot = 0;
    };
    std::vector<BlendSource> run_blend_vertices_;
    std::vector<std::int32_t> run_ps_previous_;
    // Candidate pairs of the playing log (current offset, previous offset,
    // vertices, flags: bit 0 current / bit 1 previous in the resident buffer),
    // checked on the GPU before the sequence's first sub-frame draws: a
    // verdict per pair (vertices, positions unchanged, largest move, extent).
    std::vector<std::array<std::uint32_t, 4>> blend_pairs_;
    bool blend_validate_pending_ = false;
    bool blend_validate_failed_ = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> blend_verdicts_;
    D3D12_RESOURCE_STATES blend_verdicts_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> blend_validate_root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> blend_validate_pso_;
    SceneRayTracer ray_tracer_;
    // The ray tracer could not start on this device: not retried every frame.
    bool rt_start_failed_ = false;
    static RtUpload rt_allocate(void* renderer, std::size_t bytes, std::size_t alignment);
    [[nodiscard]] RtSources rt_sources() const;
    // The occlusion of result `slot` onto the EFB colour; `reproject` on a sub-frame's replay.
    void record_rt_composite(int slot, bool reproject);
    [[nodiscard]] bool temporal_inputs_match(const TemporalUpscaleInputs& in) const;
    D3D12_GPU_VIRTUAL_ADDRESS resident_vertex_base_ = 0;
    std::uint64_t resident_vertex_bytes_ = 0;
    static constexpr std::uint32_t kMaxBlendPairs = 8192u;
    // Never a pair's verdict (stays zero, which passes): stripe draws, paired
    // per particle, blend without the geometry check.
    static constexpr std::uint32_t kStripeVerdict = kMaxBlendPairs - 1u;
    struct StripeDraw {
        std::uint32_t draw = 0;   // index in its log's draws
        std::uint32_t first = 0;  // in its log's stripe vertices
        std::uint32_t count = 0;
    };
    struct StripeLog {
        std::vector<StripeDraw> draws;
        std::vector<StripeVertex> vertices;
        void clear() noexcept {
            draws.clear();
            vertices.clear();
        }
    };
    StripeLog replay_stripes_;
    StripeLog play_stripes_;
    StripeLog run_stripes_;
    StripeLog prev_run_stripes_;
    // The playing sequence's stripe draws: per output vertex the source of its
    // earlier position (byte offset, buffer: 0 ring, 1 resident), gathered
    // into stripe_earlier_ before the first sub-frame (kStripeGatherCs).
    std::vector<std::array<std::uint32_t, 2>> stripe_gather_;
    std::vector<std::int32_t> stripe_of_draw_;
    std::vector<std::int32_t> stripe_of_prev_draw_;
    std::vector<std::pair<std::uint64_t, std::uint32_t>> stripe_keys_;
    std::vector<std::int32_t> stripe_matched_;  // per vertex of a stripe draw: its earlier vertex, or -1
    Microsoft::WRL::ComPtr<ID3D12Resource> stripe_earlier_;
    std::uint64_t stripe_earlier_bytes_ = 0;
    D3D12_RESOURCE_STATES stripe_earlier_state_ = D3D12_RESOURCE_STATE_COMMON;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> stripe_gather_root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> stripe_gather_pso_;
    bool stripe_gather_failed_ = false;
    bool ensure_stripe_gather(std::uint64_t bytes);
    void record_stripe_gather();
    static constexpr std::uint32_t kFrameValuesElement = kMaxBlendPairs;
    static constexpr std::uint32_t kZeroValuesElement = kMaxBlendPairs + 1u;
    void ensure_frame_values();
    bool ensure_blend_validation();
    void record_blend_validation();
    std::vector<std::uint32_t> blend_scratch_;
    std::uint64_t vertex_blend_draws_ = 0;
    // Sequence replay (begin_replay_sequence) and its evaluation pass. The
    // replay list is recorded once per sequence and executed by every
    // sub-frame between a small evaluation list and a small present list.
    bool ensure_interp_eval();
    bool ensure_interp_output(std::uint64_t bytes);
    void record_interp_eval(const SubframeTime& time, const Affine34& camera_t, const Affine34& camera_prev);
    void transition_interp_output(D3D12_RESOURCE_STATES state);
    void transition_blend_verdicts(D3D12_RESOURCE_STATES state);
    Microsoft::WRL::ComPtr<ID3D12RootSignature> interp_eval_root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> interp_copy_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> interp_eval_pso_;
    bool interp_eval_failed_ = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> interp_output_;
    std::uint64_t interp_output_bytes_ = 0;
    D3D12_RESOURCE_STATES interp_output_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    struct RetiredResource {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        std::uint64_t fence = 0;
    };
    std::vector<RetiredResource> interp_retired_;
    // Inputs of a split replay's temporal upscale (its output switch).
    struct ReplayUpscale {
        ID3D12Resource* color = nullptr;
        ID3D12Resource* depth = nullptr;
        ID3D12Resource* motion = nullptr;
        ID3D12Resource* reactive = nullptr;
        ID3D12Resource* output = nullptr;
        unsigned render_width = 0;
        unsigned render_height = 0;
        unsigned output_width = 0;
        unsigned output_height = 0;
        std::uint32_t copy = 0;   // the output switch's copy (resample fallback)
    };
    struct ReplayList {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
        // Temporal upscaling: the log after its output switch; the switch
        // itself is recorded per presented frame.
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list_after;
        bool split = false;
        ReplayUpscale upscale{};
        std::uint64_t fence = 0;        // the last submission that executed it
        std::uint64_t sequence = 0;     // SequenceState::serial it replays, 0 none
        bool blend = false;
        bool redirect = false;
        std::uint64_t blended_draws = 0;
        std::uint64_t guarded = 0;
        unsigned alloc_segment = 0;     // ring segment of its descriptor tables
        unsigned blend_segment = 0;     // ring segment of the log before's vertices
    };
    static constexpr unsigned kReplayLists = 4;
    std::array<ReplayList, kReplayLists> replay_lists_{};
    bool record_replay_list(ReplayList& entry);
    void record_subframe_upscale(const ReplayList& entry, const SubframeTime& time);
    struct ReplayMotionTargets {
        Microsoft::WRL::ComPtr<ID3D12Resource> target;
        Microsoft::WRL::ComPtr<ID3D12Resource> reactive;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        D3D12_CPU_DESCRIPTOR_HANDLE reactive_rtv{};
        unsigned scale = 0;
    };
    ReplayMotionTargets replay_motion_{};
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> replay_motion_rtv_heap_;
    bool ensure_replay_motion_targets();
    void swap_motion_targets();
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> subframe_upscale_list_;
    // GALAXY_INTERP_GPU_PROFILE: timestamps around the copies and clears of a
    // sampled replay (report_replay_profile).
    static constexpr std::uint32_t kReplayProfileQueries = 2048u;
    struct ReplayProfileSegment {
        std::uint32_t kind = 0;    // 0 draws, 1-3 copy (mode 0 texture, 1 display, 2 output EFB), 4 clear
        std::uint32_t detail = 0;  // the op's copy or clear index
    };
    struct ReplayProfile {
        Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        std::vector<ReplayProfileSegment> segments;
        const ReplayList* list = nullptr;
        std::uint64_t fence = 0;
        bool armed = false;
    };
    ReplayProfile replay_profile_{};
    std::uint64_t replay_profile_recordings_ = 0;
    bool ensure_replay_profile();
    void report_replay_profile();
    std::array<double, 3> subframe_phase_ms_{};  // evaluation, replay, present
    // The temporal upscaler upscales the sub-frames (and the frame at t = 1)
    // instead of the game frames (GALAXY_TEMPORAL_INTERPOLATION).
    bool temporal_interp_ = false;
    // suspend_temporal_interpolation, and the settings it was decided at.
    struct TemporalInterpKey {
        unsigned upscaler = 0;
        unsigned quality = 0;
        unsigned efb_scale = 0;
        unsigned output_scale = 0;
        float max_fps = 0.0f;
        unsigned vsync = 0;
        double refresh_hz = 0.0;
        friend bool operator==(const TemporalInterpKey&, const TemporalInterpKey&) = default;
    };
    [[nodiscard]] TemporalInterpKey temporal_interp_key() const;
    bool temporal_interp_suspended_ = false;
    TemporalInterpKey temporal_interp_key_{};
    std::uint64_t sequence_serial_ = 0;
    std::uint64_t replay_records_ = 0;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> subframe_eval_list_;
    struct SequenceState {
        bool ready = false;
        bool first = false;             // the next sub-frame copies the base blocks
        bool validated = false;         // the blend pairs were checked on the GPU
        std::uint64_t serial = 0;
        unsigned output_scale = 0;
        ReplaySequence overrides{};
        unsigned alloc_segment = 0;     // ring segment of the sequence's program
        D3D12_GPU_VIRTUAL_ADDRESS program = 0;
        std::uint32_t program_bytes = 0;
        std::uint32_t ops = 0;
        std::uint32_t copies = 0;
        std::uint32_t ops_offset = 0;
        std::uint32_t copies_offset = 0;
        std::uint32_t payload_offset = 0;
    };
    SequenceState sequence_{};
    std::vector<interp_gpu::Copy> interp_copies_;
    std::vector<std::uint8_t> interp_copied_;
    double sequence_record_ms_ = 0.0;
    std::uint64_t sequence_record_samples_ = 0;
    bool eval_readback_requested_ = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> eval_readback_;
    std::uint64_t eval_readback_fence_ = 0;
    std::uint64_t eval_readback_bytes_ = 0;
    float eval_readback_t_ = 0.0f;
    struct SubframeScratch {
        Microsoft::WRL::ComPtr<ID3D12Resource> texture;
        D3D12_RESOURCE_DESC desc{};
        std::uint64_t used = 0;           // subframe_counter_ of its last use
    };
    std::vector<SubframeScratch> subframe_scratch_;
    std::vector<std::pair<ID3D12Resource*, ID3D12Resource*>> subframe_guarded_;  // dest, scratch
    std::vector<D3D12_RESOURCE_BARRIER> subframe_barriers_;
    std::uint64_t subframe_counter_ = 0;
    std::uint64_t subframe_guarded_total_ = 0;
    ID3D12Resource* acquire_subframe_scratch(ID3D12Resource* dest);
    void record_subframe_guard(bool save);
    // Sub-frame copy redirection (GALAXY_INTERP_COPY_REDIRECT, default on;
    // 0 falls back to the save/restore guard above). Real frames own the
    // copy textures. A sub-frame's texture copies land in redirect targets,
    // and its draws that sample a redirected texture get a table patched in
    // the current descriptor segment. A copy texture a log samples before
    // writing it (an earlier frame's capture: feedback blur, camera cover) is
    // snapshotted by the recording frame, because later real frames rewrite
    // it while the log's sub-frames still run.
    struct ReplayTextureTable {
        std::uint64_t gpu = 0;                      // DrawCall::texture_table
        std::uint32_t store = 0;                    // first CPU copy in the log's store
        bool snapshots_checked = false;             // recording: sources handled
        std::array<ID3D12Resource*, kReplayTableSlots> copies{};  // EFB-copy texture per slot
    };
    struct ReplayTextures {
        std::vector<ReplayTextureTable> tables;
        std::unordered_map<std::uint64_t, std::uint32_t> by_gpu;
        // Read-before-write sources and their snapshot (copy_pool_ index).
        std::vector<std::pair<ID3D12Resource*, std::uint32_t>> snapshots;
        // Copy destinations the log wrote so far (recording).
        std::vector<ID3D12Resource*> written;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> store;  // CPU-only
        D3D12_CPU_DESCRIPTOR_HANDLE store_base{};
        std::uint32_t store_used = 0;
        // A table or snapshot could not be kept: sub-frames use the guard.
        bool incomplete = false;
        void clear() noexcept {
            tables.clear();
            by_gpu.clear();
            snapshots.clear();
            written.clear();
            store_used = 0u;
            incomplete = false;
        }
    };
    static constexpr std::uint32_t kReplayStoreDescriptors = 16384u;
    ReplayTextures replay_textures_;
    ReplayTextures play_textures_;
    ReplayTextures run_textures_;
    // Redirect targets (one per destination per sub-frame) and snapshots.
    struct CopyTexture {
        Microsoft::WRL::ComPtr<ID3D12Resource> texture;
        D3D12_RESOURCE_DESC desc{};
        std::uint32_t srv = 0;                      // slot in copy_srv_heap_
        std::uint64_t used = 0;                     // target: subframe_counter_ of last use
        unsigned segment = ~0u;                     // snapshot: owning log's segment (~0u free)
        bool snapshot = false;
    };
    static constexpr std::uint32_t kCopySrvCapacity = 256u;
    std::vector<CopyTexture> copy_pool_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> copy_srv_heap_;  // CPU-only
    D3D12_CPU_DESCRIPTOR_HANDLE copy_srv_base_{};
    std::uint32_t copy_srv_used_ = 0;
    UINT srv_increment_ = 0;
    // The playing sub-frame: copy texture -> copy_pool_ index standing in for it,
    // and tables patched under the current redirections (epoch-tagged).
    std::unordered_map<ID3D12Resource*, std::uint32_t> redirect_;
    std::unordered_map<std::uint64_t, std::pair<std::uint64_t, std::uint64_t>> patched_tables_;
    std::uint64_t redirect_epoch_ = 0;
    CopyRedirectStats copy_redirect_stats_{};
    int subframe_copy_redirect_ = -1;
    [[nodiscard]] static bool copy_redirect_enabled();
    bool ensure_replay_store(ReplayTextures& log);
    // -1 when no texture can be made. `snapshot`: owned by the recording log.
    std::int32_t acquire_copy_texture(ID3D12Resource* like, bool snapshot);
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE copy_texture_srv(std::uint32_t index) const;
    void take_replay_snapshots(std::uint64_t table);
    D3D12_GPU_DESCRIPTOR_HANDLE patch_replay_table(D3D12_GPU_DESCRIPTOR_HANDLE table);
    void release_copy_snapshots(unsigned segment);
    static constexpr unsigned kSubframeAllocators = 4;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, kSubframeAllocators> subframe_allocators_;
    std::array<std::uint64_t, kSubframeAllocators> subframe_fences_{};
    std::array<bool, kSubframeAllocators> subframe_timestamp_pending_{};
    unsigned subframe_next_ = 0;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> subframe_query_heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> subframe_readback_;
    double subframe_gpu_ms_ = 0.0;
    std::uint64_t subframe_gpu_samples_ = 0;
    double subframe_gpu_ema_ms_ = 0.0;
    // The same by copy path (0: guard, 1: redirected, 2: neither), for A/B runs.
    std::array<std::uint8_t, kSubframeAllocators> subframe_timestamp_redirect_{};
    std::array<double, 3> subframe_path_gpu_ms_{};
    std::array<std::uint64_t, 3> subframe_path_gpu_samples_{};
    double subframe_record_ms_ = 0.0;
    std::uint64_t subframe_record_samples_ = 0;
    std::atomic<double> measured_present_hz_{0.0};
    std::int64_t measured_present_window_qpc_ = 0;
    std::uint32_t measured_present_count_ = 0;
    // Render target 1 bound next to the EFB (DrawCall::aux_target values).
    std::uint8_t bound_rt_aux_ = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> reactive_target_;
    D3D12_CPU_DESCRIPTOR_HANDLE reactive_rtv_{};
    // Matrix ring segments: kFramesInFlight, plus two in temporal mode so the
    // previous render frame's palettes outlive the frame that reads them.
    unsigned matrix_segments_ = kFramesInFlight;
    unsigned matrix_segment_ = 0;
    std::uint64_t matrix_segment_serial_ = 0;
    std::array<std::uint64_t, kFramesInFlight + 2u> matrix_segment_fence_{};
    std::size_t matrix_begin_cursor_ = 0;
    bool matrix_segment_used_ = false;
    bool matrix_segment_started_ = false;

    // Backbuffers + present blit.
    static constexpr unsigned kSwapChainBuffers = 3;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,
        kSwapChainBuffers> backbuffers_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> blit_pipeline_;
    // Targets replaced by an internal-resolution change, until begin_frame
    // has copied their contents into the new ones.
    Microsoft::WRL::ComPtr<ID3D12Resource> efb_carry_color_;
    Microsoft::WRL::ComPtr<ID3D12Resource> efb_carry_depth_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> efb_carry_depth_pso_;
    // Presentation image quality; present() keeps the plain blit while it is
    // not ready.
    PostProcessor post_process_;
    // EFB-copy destination textures (render targets) are placed in shared
    // heaps rather than committed one by one (placed_texture_allocator.h).
    PlacedTextureAllocator efb_copy_heap_{
        D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES, 128ull << 20};

    // Native settings overlay: the game's own window-frame corner texture (extracted
    // read-only from files/LayoutData/FileSelect.arc; see galaxy/gx/game_asset_extract.h),
    // drawn at all four panel corners so the overlay reads as part of Super Mario
    // Galaxy's own UI instead of a flat placeholder rectangle. Loaded lazily and once,
    // the first time the overlay is shown. Every part of this is optional: if the asset
    // cannot be read or decoded (for example, a different game dump), the overlay keeps
    // its solid-color corners.
    Microsoft::WRL::ComPtr<ID3D12RootSignature> ui_texture_root_sig_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> ui_texture_pipeline_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> ui_texture_srv_heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> ui_corner_texture_;
    Microsoft::WRL::ComPtr<ID3D12Resource> ui_corner_upload_;
    bool ui_corner_texture_load_attempted_ = false;
    bool ui_corner_texture_ready_ = false;
    unsigned ui_corner_texture_width_ = 0;
    unsigned ui_corner_texture_height_ = 0;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> ui_solid_root_sig_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> ui_solid_pipeline_;
    bool ui_solid_pipeline_load_attempted_ = false;
    bool ui_solid_pipeline_ready_ = false;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> sampler_heap_;
    std::array<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>, kInterpSegments>
        sampler_heaps_;
    // Sampler heap in use: the frame slot, or the game-frame segment in
    // interpolation mode.
    unsigned sampler_slot_ = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE default_sampler_table_{};
    std::array<std::uint32_t, kInterpSegments> default_sampler_config_keys_{};
    std::array<bool, kInterpSegments> default_sampler_valid_{};
    // Each cache describes tables actually written in its physical heap.
    // Configuration invalidation happens only after that slot's fence; tables
    // retained in another heap cannot establish a hit for the active heap.
    std::array<SamplerTableCache, kInterpSegments> sampler_tables_;

    UploadRing vertex_ring_;
    UploadRing index_ring_;
    UploadRing constant_ring_;
    UploadRing matrix_ring_;        // XF palette snapshots (structured)
    DescriptorRing srv_ring_;

    unsigned frame_slot_ = 0;
    std::uint64_t frame_index_ = 0;
    std::uint64_t next_fence_value_ = 0;
    std::uint64_t last_latency_wait_us_ = 0;
    std::uint64_t last_frame_slot_wait_us_ = 0;
    std::uint64_t last_timestamp_read_us_ = 0;
    std::uint64_t last_begin_reset_us_ = 0;

    // Redundant-state elision for draw(). Every one of these is re-armed by
    // invalidate_gx_bindings(), which begin_frame() calls after the upload rings
    // for the new frame slot have been rewound and before any list is recorded.
    // The vertex view is part of the set: its GPU VA comes from the per-frame
    // vertex ring, so a stale entry can never survive that rewind.
    ID3D12PipelineState* bound_pipeline_ = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS bound_vs_constants_ = 0;
    D3D12_GPU_VIRTUAL_ADDRESS bound_ps_constants_ = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE bound_texture_table_{};
    D3D12_GPU_DESCRIPTOR_HANDLE bound_sampler_table_{};
    D3D12_GPU_VIRTUAL_ADDRESS bound_matrix_palette_ = 0;
    D3D12_GPU_VIRTUAL_ADDRESS bound_blend_vertices_ = 0;
    std::uint32_t bound_blend_slot_ = 0;
    bool bound_vertex_blend_valid_ = false;
    D3D12_PRIMITIVE_TOPOLOGY bound_topology_ =
        D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    D3D12_VERTEX_BUFFER_VIEW bound_vertex_view_{};
    bool bound_vertex_view_valid_ = false;
    // The index view is a CPU-side 20-byte descriptor whose BufferLocation comes
    // from the per-frame index ring, so like the vertex view it must be re-armed
    // by invalidate_gx_bindings() after the ring for the new slot is rewound.
    D3D12_INDEX_BUFFER_VIEW bound_index_view_{};
    bool bound_index_view_valid_ = false;
    D3D12_RECT bound_scissor_{};
    // Successful nonempty GX input that produced the currently bound scissor.
    D3D12_RECT bound_scissor_input_{};
    unsigned bound_scissor_input_scale_ = 0;
    bool bound_scissor_input_valid_ = false;
    D3D12_VIEWPORT bound_viewport_{};
    bool bound_scissor_valid_ = false;
    bool bound_viewport_valid_ = false;

    // Bring-up EFB readback (debug_copy_efb_to_readback / debug_log_readback).
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_buf_;
    bool readback_pending_ = false;
    std::uint64_t readback_fence_value_ = 0;
    std::string next_efb_capture_path_;
    std::string readback_capture_path_;
    Microsoft::WRL::ComPtr<ID3D12Resource> backbuffer_readback_buf_;
    bool backbuffer_readback_pending_ = false;
    std::uint64_t backbuffer_readback_fence_value_ = 0;
    bool backbuffer_readback_capture_ = false;
    bool backbuffer_readback_luma_trace_ = false;
    UINT backbuffer_readback_width_ = 0;
    UINT backbuffer_readback_height_ = 0;
    UINT backbuffer_readback_row_pitch_ = 0;
    std::string next_backbuffer_capture_path_;
    std::string backbuffer_readback_capture_path_;
    std::uint64_t backbuffer_readback_frame_ = 0;
    std::uint32_t backbuffer_readback_xfb_addr_ = 0;
    std::uint64_t backbuffer_readback_xfb_stamp_ = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> selected_xfb_readback_buf_;
    Microsoft::WRL::ComPtr<ID3D12Resource> selected_xfb_readback_source_;
    bool selected_xfb_readback_pending_ = false;
    std::uint64_t selected_xfb_readback_fence_ = 0;
    UINT selected_xfb_readback_width_ = 0;
    UINT selected_xfb_readback_height_ = 0;
    UINT selected_xfb_readback_pitch_ = 0;
    std::string selected_xfb_readback_path_;
    struct DebugPpmWriter {
        std::string tag;
        std::string path;
        std::future<void> completion;
    };
    std::vector<DebugPpmWriter> debug_ppm_writers_;

    // Presentation telemetry: successful DXGI submissions, distinct XFB
    // updates, frame-latency waits, Present blocking, and GPU timestamps.
    LARGE_INTEGER qpc_frequency_{};
    std::int64_t telemetry_window_start_qpc_ = 0;
    std::int64_t last_present_qpc_ = 0;
    std::uint64_t telemetry_first_serial_start_ = 0;
    std::uint64_t telemetry_xfb_copy_start_ = 0;
    std::int64_t last_paced_present_qpc_ = 0;
    bool has_presented_swap_chain_ = false;
    bool window_shown_ = false;
    std::uint64_t telemetry_present_count_ = 0;
    std::uint64_t telemetry_interval_count_ = 0;
    std::uint64_t telemetry_adjacent_serial_transition_count_ = 0;
    std::uint64_t last_presented_xfb_serial_ = ~std::uint64_t{0};
    double telemetry_interval_ms_ = 0.0;
    double telemetry_interval_max_ms_ = 0.0;
    double telemetry_present_ms_ = 0.0;
    double telemetry_present_max_ms_ = 0.0;
    double telemetry_pace_wait_ms_ = 0.0;
    double telemetry_pace_wait_max_ms_ = 0.0;
    double telemetry_latency_wait_ms_ = 0.0;
    double telemetry_latency_wait_max_ms_ = 0.0;
    double telemetry_gpu_ms_ = 0.0;
    std::uint64_t telemetry_gpu_samples_ = 0;
    std::uint64_t telemetry_no_xfb_present_count_ = 0;
    std::uint64_t telemetry_backbuffer_clear_count_ = 0;
    // Refresh rate of the swap chain's output (EnumDisplaySettings) and, per
    // telemetry window, how many refreshes each displayed frame stayed on
    // screen (DXGI frame statistics; index 4 counts 4 or more).
    double display_refresh_hz_ = 0.0;
    std::uint64_t display_refresh_presents_ = 0;
    UINT last_stats_present_count_ = 0;
    UINT last_stats_present_refresh_count_ = 0;
    std::array<std::uint64_t, 5> telemetry_shown_refreshes_{};

    // EFB->texture copy destinations, keyed by (guest dest address, copy format,
    // depth-source). SMG reuses one address with different copy formats, and each format
    // needs its own converted texture. The resource is reused across copies and recreated
    // only on size change; the TextureCache alias is refreshed per copy (cheap when unchanged).
    struct EfbCopyDest {
        Microsoft::WRL::ComPtr<ID3D12Resource> texture;
        UINT width = 0;
        UINT height = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        bool rtv_valid = false;
        // Last copy that used this destination, for the bound below. A copy
        // destination is a pure function of its key, so dropping an entry can
        // only cost a later re-creation; it can never change a presented pixel.
        std::uint64_t last_used = 0;
        // Resident bytes of `texture`, for the byte budget below.
        std::uint64_t bytes = 0;
    };
    // Bounded. This map holds one committed D3D12 texture per destination key.
    // Each is that copy's SCALED destination extent at kEfbColorFormat (RGBA8),
    // so the size varies with both the copy's logical extent and the internal
    // scale. At the full 640x456 EFB extent: 1.11 MB at 1x, 17.81 MB at 4x,
    // 40.08 MB at 6x.
    //
    // Budgeted in BYTES, not entries: the per-entry cost varies with the scale
    // AND with the copy extent, so an entry count cannot bound residency. The
    // accounting charges the driver's own `GetResourceAllocationInfo` size rather
    // than a computed w*h*bpp, so it follows the resource if the format changes.
    // The default is well above the expected per-scene working set (a handful of
    // XFBs across a couple of formats plus one bloom workspace), so a legitimate
    // set should not thrash. Override with GALAXY_EFB_COPY_DEST_BUDGET_MB; 0 disables the bound.
    //
    // Without a bound, only a failed allocation or full shutdown removed an entry, so
    // every distinct (addr, format, depth, bloom) the game copied to stayed resident for
    // the life of the process.
    //
    // The adapter's reported vram-mb value on integrated graphics is a compatibility
    // constant, not a separate memory pool: a default-heap texture is system memory
    // either way. The bound exists because the map was unbounded, not because it
    // oversubscribes a reported 128 MB pool; there is no such pool.
    std::unordered_map<std::uint64_t, EfbCopyDest> efb_copy_dests_;
    std::uint64_t efb_copy_dests_tick_ = 0;
    std::uint64_t efb_copy_dests_bytes_ = 0;
    // Render-owner-only aggregate work counts, reported once at shutdown.
    // Payload bytes are copied pixels, not a device allocation-size estimate.
    std::uint64_t efb_copy_requests_ = 0;
    std::uint64_t efb_shader_copies_ = 0;
    std::uint64_t efb_identity_copies_ = 0;
    std::uint64_t efb_identity_payload_bytes_ = 0;
    using RetiredResourceList =
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>;
    std::array<RetiredResourceList, kFramesInFlight>
        retired_efb_copy_textures_;
    // Interpolation: copy textures retired during a game-frame ring segment.
    std::array<RetiredResourceList, kInterpSegments> retired_segment_textures_;

    // Conversion blit pipeline (root sig lives in a module static next to
    // the present-blit one) + cached CPU-only descriptors for conversion
    // destinations. Non-shader-visible RTV descriptors are consumed at record
    // time, so every copy rebinds the same CPU-only scratch slot. EFB source
    // SRVs occupy an immutable prefix of the active shader-visible heap.
    Microsoft::WRL::ComPtr<ID3D12PipelineState> conv_pipeline_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> conv_rtv_heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE conv_scratch_rtv_{};
    detail::ConversionRtvSlot conv_scratch_rtv_slot_;
    std::optional<DescriptorRing::Table> conv_efb_source_table_;
    D3D12_GPU_DESCRIPTOR_HANDLE conv_efb_color_srv_{};
    D3D12_GPU_DESCRIPTOR_HANDLE conv_efb_depth_srv_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> clear_rgb_pipeline_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> motion_debug_root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> motion_debug_pipeline_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> clear_alpha_pipeline_;

    // Synchronous GXPeekARGB/GXPeekZ bridge. Peeks execute on the render
    // thread through a tiny 1x1 conversion target and readback buffer, so
    // CPU EFB access observes the native D3D12 EFB without mapping the EFB
    // aperture as fake RAM.
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> peek_allocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> peek_command_list_;
    Microsoft::WRL::ComPtr<ID3D12Resource> peek_target_;
    Microsoft::WRL::ComPtr<ID3D12Resource> peek_readback_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> peek_rtv_heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE peek_rtv_{};
    UINT peek_readback_row_pitch_ = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> pointer_depth_target_;
    Microsoft::WRL::ComPtr<ID3D12Resource> pointer_depth_readback_;
    std::uint64_t pointer_depth_capture_fence_ = 0;
    // Pointer depth fields rotate through mapped readbacks so a published
    // field stays readable while the next ones are captured (the consumer
    // reads at most the two newest; four leave margin).
    static constexpr std::size_t kPointerDepthRing = 4u;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kPointerDepthRing> pointer_depth_ring_;
    std::array<const std::uint8_t*, kPointerDepthRing> pointer_depth_ring_rgba_{};
    std::size_t pointer_depth_ring_next_ = 0;
    const std::uint8_t* pointer_depth_pending_rgba_ = nullptr;
    bool pointer_depth_in_frame_pending_ = false;
    bool ensure_pointer_depth_resources();
    struct AsyncPeekField {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list;
        Microsoft::WRL::ComPtr<ID3D12Resource> target;
        // One mapped readback per published slot of GxBackend's field ring.
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kAsyncPeekFieldSlots> readbacks;
        std::array<const std::uint8_t*, kAsyncPeekFieldSlots> rgba{};
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        std::uint64_t fence = 0;
        // Recorded on the open frame's command list; end_frame assigns its fence.
        bool in_frame = false;
    };
    // [0] depth, [1] color (async_peek_field_index).
    std::array<AsyncPeekField, 2> async_peek_fields_{};
    bool ensure_async_peek_field_resources(AsyncPeekField& field, EfbPeekKind kind);
    bool read_efb_pixels(std::uint16_t x, std::uint16_t y, EfbPeekKind kind,
        std::uint32_t& value, std::span<std::uint32_t> depth_pixels,
        bool defer_pointer_depth = false);
    static const std::uint8_t* pin_readback_mapping(ID3D12Resource* readback);
    void select_pointer_depth_destination();
    void copy_efb_readback_pixels(ID3D12Resource* readback, UINT width,
        UINT height, UINT row_pitch, bool is_depth, std::uint32_t& value,
        std::span<std::uint32_t> depth_pixels);

};

}  // namespace galaxy::gx
