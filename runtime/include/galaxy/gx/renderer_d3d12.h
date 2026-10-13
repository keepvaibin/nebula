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
#include "galaxy/gx/post_process.h"
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
// Ring segments in temporal and interpolation modes (see RendererD3D12::
// begin_frame): two more than the frames in flight.
inline constexpr unsigned kRingSegments = kFramesInFlight + 2u;
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

    bool initialize(
        ID3D12Device* device,
        const char* name,
        std::size_t bytes_per_frame,
        unsigned frames_in_flight);
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
    [[nodiscard]] std::size_t bytes_per_frame() const { return bytes_per_frame_; }
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
    // swaps) are logged. A sub-frame re-records the log into a continuation
    // frame: its own command allocator, no ring rewind (it appends to the
    // recorded frame's segments, which stay alive until the slot is reused),
    // draws with a record index take that record's interpolated VS constants,
    // and the display copy lands in a renderer-owned XFB that is presented.
    void begin_replay_record();
    void end_replay_record(bool keep);
    [[nodiscard]] bool replay_recording() const noexcept { return replay_recording_; }
    [[nodiscard]] bool replay_available() const noexcept { return replay_valid_; }
    // Moves the completed log to playback (sub-frames then replay it); false
    // when none is ready.
    bool start_replay_playback();
    [[nodiscard]] bool replay_playing() const noexcept { return run_valid_; }
    // Interpolation mode: present rate above 60, temporal upscaler off,
    // GALAXY_EXPERIMENTAL_INTERPOLATION=1. Rings then keep one segment per
    // game frame (advanced at the first frame after a display copy) and the
    // log spans every render chunk of the game frame up to its display copy.
    [[nodiscard]] bool interpolation_mode() const;
    // Completed logs, counted; the guest XFB the latest one copied to.
    [[nodiscard]] std::uint64_t replay_serial() const noexcept { return replay_serial_; }
    [[nodiscard]] std::uint32_t replay_xfb_addr() const noexcept { return replay_xfb_addr_; }
    // A 3D scene began inside the log being recorded (one is replayable).
    void note_replay_scene() noexcept { ++replay_scene_count_; }
    // Last reason a sub-frame was not recorded (diagnostics).
    [[nodiscard]] unsigned replay_failure() const noexcept { return replay_failure_; }
    void note_replay_failure(unsigned reason) noexcept { replay_failure_ = reason; }
    // The next sub-frame's backbuffer is read back to the debug capture path.
    void set_subframe_capture(bool capture) noexcept { subframe_capture_ = capture; }
    // False (nothing recorded) when the log is gone or has no display copy.
    // `record_constants[i]` replaces the VS constants of draws of record i.
    bool present_replay_subframe(std::span<const D3D12_GPU_VIRTUAL_ADDRESS> record_constants);
    // Swap-chain presents per second over the last second (any source).
    [[nodiscard]] double measured_present_hz() const noexcept {
        return measured_present_hz_.load(std::memory_order_relaxed);
    }
    // GPU milliseconds of completed sub-frames since the last call.
    void take_subframe_gpu_ms(double& total_ms, std::uint64_t& samples) noexcept {
        total_ms = subframe_gpu_ms_;
        samples = subframe_gpu_samples_;
        subframe_gpu_ms_ = 0.0;
        subframe_gpu_samples_ = 0u;
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
    [[nodiscard]] bool try_complete_async_peek_field(EfbPeekKind kind);
    [[nodiscard]] const std::uint8_t* async_peek_field_rgba(
        EfbPeekKind kind, std::size_t slot) const noexcept;
    [[nodiscard]] bool async_peek_field_pending(EfbPeekKind kind) const noexcept {
        return async_peek_fields_[async_peek_field_index(kind)].fence != 0u;
    }
    [[nodiscard]] bool any_async_peek_field_pending() const noexcept {
        return async_peek_fields_[0].fence != 0u || async_peek_fields_[1].fence != 0u;
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
    [[nodiscard]] bool temporal_mode() const;
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
    enum class ReplayKind : std::uint8_t { Draw, BindEfb, Clear, Copy, SwapEfbSets, ClearDepth };
    struct ReplayOp {
        ReplayKind kind = ReplayKind::Draw;
        std::uint32_t index = 0;
    };
    void present_to_swap_chain();
    void execute_replay_copy(const ReplayCopy& copy);
    bool ensure_subframe_resources(unsigned xfb_width, unsigned xfb_height);
    // The log being recorded; a completed log moves to play_* (swapped), so
    // the next game frame records while the previous one is interpolated.
    std::vector<ReplayOp> replay_ops_;
    std::vector<DrawCall> replay_draws_;
    std::vector<ReplayViewport> replay_viewports_;
    std::vector<ReplayClear> replay_clears_;
    std::vector<ReplayCopy> replay_copies_;
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
    bool replay_has_swaps_ = false;
    unsigned replay_failure_ = 0;
    bool subframe_capture_ = false;
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
    std::array<std::uint64_t, kRingSegments> ring_segment_fence_{};
    bool ring_advance_pending_ = true;
    // Sub-frames render into their own EFB so a later frame's partial EFB
    // (its chunks interleave with the sub-frames) stays intact.
    EfbTargetSet replay_efb_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> replay_efb_rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> replay_efb_dsv_heap_;
    bool create_efb_set(EfbTargetSet& set, Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& rtv_heap,
                        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& dsv_heap, unsigned scale);
    void swap_efb_with(EfbTargetSet& other);
    bool replay_playing_ = false;
    bool replay_start_output_active_ = false;
    unsigned replay_source_slot_ = 0;
    XfbTexture subframe_xfb_{};
    static constexpr unsigned kSubframeAllocators = 4;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, kSubframeAllocators> subframe_allocators_;
    std::array<std::uint64_t, kSubframeAllocators> subframe_fences_{};
    std::array<bool, kSubframeAllocators> subframe_timestamp_pending_{};
    unsigned subframe_next_ = 0;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> subframe_query_heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> subframe_readback_;
    double subframe_gpu_ms_ = 0.0;
    std::uint64_t subframe_gpu_samples_ = 0;
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
    std::array<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>, kRingSegments>
        sampler_heaps_;
    // Sampler heap in use: the frame slot, or the game-frame segment in
    // interpolation mode.
    unsigned sampler_slot_ = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE default_sampler_table_{};
    std::array<std::uint32_t, kRingSegments> default_sampler_config_keys_{};
    std::array<bool, kRingSegments> default_sampler_valid_{};
    // Each cache describes tables actually written in its physical heap.
    // Configuration invalidation happens only after that slot's fence; tables
    // retained in another heap cannot establish a hit for the active heap.
    std::array<SamplerTableCache, kRingSegments> sampler_tables_;

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
