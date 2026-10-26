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
// aspect-fits an XFB into the swap-chain backbuffer.
//
// begin_frame() is the sole PSO-publication point: it drains the
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
#include "galaxy/gx/upload_ring.h"
#include "galaxy/gx/interp_gpu.h"
#include "galaxy/gx/post_process.h"
#include "galaxy/gx/ambient_occlusion.h"
#include "galaxy/gx/game_bloom.h"
#include "galaxy/gx/interp_types.h"
#include "galaxy/gx/ray_tracing.h"
#include "galaxy/gx/placed_texture_allocator.h"
#include "galaxy/gx/gx_bitfields.h"
#include "galaxy/gx/gpu_types.h"
#include "galaxy/gx/renderer_common.h"
#include "galaxy/gx/shader_keys.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx/ui_canvas.h"
#include "galaxy/gx/window_owner.h"
#include "galaxy/gx_d3d12.h"

#include <array>
#include <bit>
#include <chrono>
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
struct TextureHandle;

// Compiles the runtime EFB conversion shader without a device (tests).
[[nodiscard]] bool validate_efb_conversion_shader(
    std::string& diagnostics) noexcept;
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

inline constexpr DXGI_FORMAT kEfbColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
// The ray tracer's HDR scene EFB (RendererD3D12::begin_hdr_scene): unclamped traced light, sRGB-encoded above 1 as
// below, until the camera pass at the scene's end resolves it into the RGBA8 EFB.
inline constexpr DXGI_FORMAT kEfbHdrColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
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

// Builds the GX graphics root signature used by RendererD3D12. Root
// CBV b0 is visible to both VS and GS because native GX line/point expansion
// reads its scale-aware raster constants in the geometry stage.
[[nodiscard]] HRESULT create_gx_root_signature(
    ID3D12Device* device,
    ID3D12RootSignature** root_signature) noexcept;

[[nodiscard]] D3D12_SAMPLER_DESC build_sampler_desc_from_key(
    std::uint32_t key,
    const RenderConfig& cfg) noexcept;

// Shader-visible descriptor allocator, segmented per frame in flight like the upload rings.
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
    // The heap index of a view this ring handed out (~0 for none of its own).
    [[nodiscard]] std::uint32_t index_of(D3D12_GPU_DESCRIPTOR_HANDLE handle) const noexcept {
        if (increment_ == 0u || handle.ptr < gpu_base_.ptr) {
            return 0xFFFFFFFFu;
        }
        const std::uint64_t index = (handle.ptr - gpu_base_.ptr) / increment_;
        const std::uint64_t size = static_cast<std::uint64_t>(persistent_descriptors_) +
                                   static_cast<std::uint64_t>(descriptors_per_frame_) * frames_in_flight_;
        return index < size ? static_cast<std::uint32_t>(index) : 0xFFFFFFFFu;
    }

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

class RendererD3D12 final : public UiCanvas {
public:
    // Creates the Win32 window, device, queue, swap chain, EFB targets at
    // kEfbWidth x kEfbHeight x efb_scale, the shared GX root signature, the
    // present-blit pipeline, and all rings.
    bool initialize(int window_width, int window_height, unsigned efb_scale);
    [[nodiscard]] std::string startup_failure() const { return {}; }
    // Creates the EFB-peek and pointer-depth resources up front, so the first frame
    // that shows the pointer does not stall creating them.
    void create_first_use_resources();
    void shutdown();                // full GPU drain, then release
    // A persistently mapped upload buffer; empty when it cannot be created.
    [[nodiscard]] UploadBuffer create_upload_buffer(std::size_t bytes, const char* name);
    // The frame's table of `count` texture descriptors for the draw's maps (null SRVs where a handle is empty).
    // `lit_table`: map 7 is the ray tracer's light texture (lit translucent draws).
    [[nodiscard]] gpu::Table texture_table_for(
        const TextureHandle* handles, unsigned count, const TextureCache& cache, bool lit_table = false);
    // The texture cache records its uploads into this frame's command list.
    void attach_texture_uploads(TextureCache& cache);
    void pump_messages();           // no-op: window_owner_ pumps the window's messages
    [[nodiscard]] bool quit_requested() const noexcept;

    // Advances the frame ring: waits on the fence of frame N-kFramesInFlight
    // only, resets that slot's command allocator, opens the command list,
    // and rewinds the slot's ring segments. Swap-chain latency is waited in
    // present(), after GX/EFB work is recorded but before the backbuffer is
    // selected, so gameplay work is not paced by DWM too early.
    void begin_frame(bool present_swap_chain = true);
    // The frame-latency wait begin_frame(true) takes, for a frame begun without a present that presents after all (a
    // game frame shown when its display copy is made).
    void wait_for_frame_latency_before_present();
    // Called after the queue signal, before pacing and Present; the context is not retained.
    using SubmissionCallback = void (*)(void* context, std::uint64_t fence_value);
    // Closes, submits and signals the frame's fence. Presenting (optional) comes after
    // the signal so display pacing cannot delay command-allocator and upload-ring reuse.
    std::uint64_t end_frame(
        bool present_swap_chain = true,
        SubmissionCallback on_submitted = nullptr,
        void* submission_context = nullptr);
    [[nodiscard]] std::uint64_t completed_fence_value() const;
    // Bounded wait for one queue signal (the synchronous rendering path only).
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
    // An EFB-to-texture copy from the output EFB after end_output_efb: the picture its display copy took (the
    // scene with the 2D layer), which the console's single EFB still holds until something draws.
    [[nodiscard]] bool copy_output_efb_to_texture(const EfbCopyParams& params, TextureCache& textures);
    // The scene's anti-aliasing (post_process.h) in the render EFB at the end of the 3D scene,
    // before the 2D layer and begin_output_efb. A temporal upscaler antialiases the scene itself.
    void record_scene_aa();
    // Ambient occlusion (ambient_occlusion.h) of the scene drawn so far, from the view `set_scene_ao_view` gave:
    // after the opaque phases, before the translucent ones; sub-frames replay it.
    [[nodiscard]] bool ambient_occlusion_ready() const noexcept { return ambient_occlusion_.ready(); }
    void set_scene_ao_view(const RtView& view) noexcept { scene_ao_view_ = view; }
    void record_scene_ao();
    // The game's bloom (game_bloom.h) at a kMarkerBloom marker (galaxy/motion_signals.h: `phase` its aux, `a` and `b`
    // its values); sub-frames replay it.
    [[nodiscard]] bool game_bloom_ready() const noexcept { return game_bloom_.ready(); }
    void game_bloom(std::uint16_t phase, std::uint32_t a, std::uint32_t b);
    [[nodiscard]] bool output_efb_active() const noexcept { return output_efb_active_; }
    // Whether the targets for `scale` fit the adapter's video-memory budget
    // next to everything already resident.
    [[nodiscard]] bool efb_scale_fits_memory(unsigned scale) const;

    // A temporal upscaler (FSR 3 or DLSS) is selected and available.
    [[nodiscard]] bool temporal_mode() const;
    // The 3D image's scale over the Wii's 640 x 456 as it is shown: the temporal upscaler's output, else the EFB's.
    [[nodiscard]] unsigned scene_display_scale() const;
    // At the start of the guest's 3D scene: whether the temporal path runs for
    // it and, if so, the scene's sub-pixel jitter in render pixels (applied to
    // perspective projections).
    [[nodiscard]] bool scene_temporal_begin(float& jitter_x, float& jitter_y);
    // After scene_temporal_begin, in a temporal scene or one the ray tracing
    // engine lights (its histories follow moving surfaces by the motion; no
    // jitter, no upscale): clears the motion target for the scene. False
    // otherwise or when it could not be created (no motion writers).
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
    [[nodiscard]] bool rt_water_open() const noexcept { return ray_tracer_.water_open(); }
    [[nodiscard]] bool rt_late_open() const noexcept { return ray_tracer_.late_open(); }
    [[nodiscard]] bool rt_sky_open() const noexcept { return ray_tracer_.sky_open(); }
    void rt_begin_sky() noexcept { ray_tracer_.begin_sky(); }
    void rt_set_scene_up(const std::array<float, 3>& up) noexcept { ray_tracer_.set_scene_up(up); }
    [[nodiscard]] float rt_glow_exposure(bool model = false) const noexcept {
        return ray_tracer_.ready() ? ray_tracer_.glow_exposure(model) : 1.0f;
    }
    // The last traced scene's lighting as a texture for lit translucents (texture 7).
    [[nodiscard]] bool rt_light_texture_ready() const noexcept {
        return ray_tracer_.ready() && ray_tracer_.light_texture_ready();
    }
    [[nodiscard]] ID3D12Resource* rt_light_texture() const noexcept { return ray_tracer_.light_texture(); }
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE rt_light_texture_srv() const noexcept {
        return ray_tracer_.light_texture_srv();
    }
    [[nodiscard]] float rt_light_texture_scale() const noexcept { return ray_tracer_.light_texture_scale(); }
    void rt_begin_scene();
    void rt_set_view(const RtView& view) noexcept { ray_tracer_.set_view(view); }
    void rt_set_camera(const std::array<float, 12>* view) noexcept { ray_tracer_.set_camera(view); }
    [[nodiscard]] bool rt_lighting() const noexcept { return ray_tracer_.lighting_active(); }
    [[nodiscard]] bool rt_physical() const noexcept { return ray_tracer_.physical_active(); }
    // The ray tracing engine runs (its clear-surface pass traces the table's `traced` glass).
    [[nodiscard]] bool rt_engine() const noexcept { return ray_tracer_.engine_active(); }
    [[nodiscard]] bool rt_engine_clear() const noexcept { return ray_tracer_.engine_clear_active(); }
    [[nodiscard]] bool rt_log_seq() const noexcept { return ray_tracer_.log_seq(); }
    [[nodiscard]] bool rt_ab_original() const noexcept { return ray_tracer_.ab_original(); }
    [[nodiscard]] bool rt_scene_untraced() const noexcept { return ray_tracer_.scene_untraced(); }
    void rt_set_ab_original(bool original) noexcept { ray_tracer_.set_ab_original(original); }
    void rt_set_scene_untraced(bool untraced) noexcept { ray_tracer_.set_scene_untraced(untraced); }
    void rt_set_presents(std::uint64_t presents) noexcept { ray_tracer_.set_presents(presents); }
    // The traced reflections on the scene's surfaces (SceneRayTracer::surface_reflections_active).
    [[nodiscard]] bool rt_reflections_active() const noexcept { return ray_tracer_.surface_reflections_active(); }
    [[nodiscard]] bool rt_sun_direction(std::array<float, 3>& world) const noexcept {
        return ray_tracer_.sun_direction(world);
    }
    void rt_hold_sun(bool held) noexcept { ray_tracer_.hold_sun(held); }
    void rt_set_key_light(bool valid, const std::array<float, 3>& world, const std::array<float, 3>& color,
                          float strength, bool shown) noexcept {
        ray_tracer_.set_key_light(valid, world, color, strength, shown);
    }
    [[nodiscard]] bool rt_sky_sun(std::array<float, 3>& world, std::array<float, 3>& color) const noexcept {
        return ray_tracer_.sky_sun(world, color);
    }
    [[nodiscard]] bool rt_sky_key(std::array<float, 3>& world, float& strength) const noexcept {
        return ray_tracer_.sky_key(world, strength);
    }
    void rt_reset_sky() noexcept { ray_tracer_.reset_sky(); }
    void rt_set_stage_sun(const RtStageSun* sun) noexcept { ray_tracer_.set_stage_sun(sun); }
    [[nodiscard]] const RtStageSun* rt_stage_sun() const noexcept { return ray_tracer_.stage_sun(); }
    void rt_note_draw(const DrawCall& call, const RtDrawInfo& info);
    void rt_trace_scene();
    // The scene's end: its finished picture becomes the light the next frame's rays read.
    void rt_capture_picture();
    // The scene layer's beginning while ray tracing runs: the HDR scene EFB stands in for the EFB (its contents so
    // far carried over), until end_hdr_scene's camera pass (rt_capture_picture, or the display copy of a scene that
    // never ended) writes its picture to the EFB and restores it. Sub-frames replay both.
    void begin_hdr_scene();
    void end_hdr_scene();
    // After the translucent phases: the water drawn since the trace reflects (its composite is
    // recorded for the sub-frames' replays).
    void rt_trace_water();
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
    [[nodiscard]] bool replay_recording() const noexcept { return replay_recording_; }
    [[nodiscard]] bool replay_available() const noexcept { return replay_valid_; }
    // The next present is a game frame shown without sub-frames while interpolating (a movie, no scene): it holds for
    // a whole game frame (present_sync_interval).
    void set_present_game_frame(bool game_frame) noexcept { present_game_frame_ = game_frame; }
    // Moves the completed log to playback (sub-frames then replay it); false
    // when none is ready.
    bool start_replay_playback();
    [[nodiscard]] bool replay_playing() const noexcept { return run_valid_; }
    // Replay serial (game frame) of the playing log.
    [[nodiscard]] std::uint64_t playing_serial() const noexcept { return run_serial_; }
    // Interpolation mode: a present rate above 60 (with a temporal upscaler,
    // while suspend_temporal_interpolation has not stopped it). Rings then keep
    // one segment per game frame (advanced at the first frame after a display
    // copy) and the log spans every render chunk of the game frame up to its
    // display copy.
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
    using ReplaySequence = gx::ReplaySequence;  // interp_types.h
    bool begin_replay_sequence(const ReplaySequence& sequence);
    // A stripe particle's vertex in the draw logged next (JPA stripes, two
    // vertices per particle): interpolated per particle between game frames.
    using StripeVertex = gx::StripeVertex;  // renderer_common.h
    void note_stripe_vertices(std::span<const StripeVertex> vertices);
    // A presented frame of the sequence: t, the t of the frame presented before
    // it (motion vectors) and its jitter in render pixels (temporal upscaling).
    using SubframeTime = gx::SubframeTime;  // interp_types.h
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
    using CopyRedirectStats = gx::CopyRedirectStats;  // interp_types.h
    [[nodiscard]] CopyRedirectStats take_copy_redirect_stats() noexcept {
        return std::exchange(copy_redirect_stats_, CopyRedirectStats{});
    }
    // Diagnostics (A/B in one run): 1 redirect, 0 guard, 2 neither for the
    // next sub-frames; -1 (the default) redirects.
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
    // Render thread, at each scene's start: its camera (world to eye), or null when unknown.
    void set_scene_view(const std::array<float, 12>* view) noexcept {
        scene_view_known_ = view != nullptr;
        if (view != nullptr) {
            scene_view_ = *view;
        }
    }
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
    // of submitting a black frame. `black` presents black with the overlays
    // (the VI's own black output); `source` then only sets the layout.
    // `capture_backbuffer` records the final post-blit swap-chain image.
    // Returns whether an optional backbuffer capture was recorded. Busy
    // diagnostic buffers never suppress the required blit/presentation.
    bool present(const XfbTexture* source, bool capture_backbuffer = false, bool black = false);

    // Reads the EFB's 640x528 depth field into `pixels`.
    [[nodiscard]] bool capture_pointer_depth(std::span<std::uint32_t> pixels);
    // Render-owner only. Keeps the shared GXPeek resources owned until
    // try_complete_pointer_depth_capture_in_place has taken the field.
    [[nodiscard]] std::uint64_t submit_pointer_depth_capture();
    // Records the pointer depth field capture into the frame's own command
    // list at the current point of the stream (the simulation thread marks the
    // FIFO offset of the scene boundary). The readback completes at this
    // frame's fence; try_complete_pointer_depth_capture_in_place publishes it.
    // False while another pointer field is still owned.
    [[nodiscard]] bool record_pointer_depth_capture_in_frame();
    [[nodiscard]] std::uint64_t pointer_depth_capture_fence() const noexcept {
        return pointer_depth_capture_fence_;
    }
    // On completion `rgba` names the mapped field (null when the readback
    // could not be mapped).
    [[nodiscard]] bool try_complete_pointer_depth_capture_in_place(
        const std::uint8_t*& rgba);
    // Render-owner only. Asynchronous 640x528 EFB fields (depth for GXPeekZ,
    // color for GXPeekARGB) for the simulation thread, one per channel
    // (GxBackend binds a channel to a peek kind and draw-sync token), each on
    // private resources so they never contend with the pointer field above or
    // with each other. Returns the fence value, or 0 when that channel is in
    // flight.
    static constexpr std::size_t kAsyncPeekFieldSlots = 3u;
    static constexpr std::size_t kAsyncPeekFieldChannels = 4u;
    // Captures the field into readback `slot` (0..2). Completion leaves it
    // there; async_peek_field_rgba names the mapped bytes (RGBA8, 640x528).
    [[nodiscard]] std::uint64_t submit_async_peek_field(
        std::size_t channel, EfbPeekKind kind, std::size_t slot);
    // The same capture recorded in the open frame where the stream is now
    // (a draw-sync token); fenced when the frame is submitted.
    [[nodiscard]] bool record_async_peek_field_in_frame(
        std::size_t channel, EfbPeekKind kind, std::size_t slot);
    [[nodiscard]] bool try_complete_async_peek_field(std::size_t channel);
    [[nodiscard]] const std::uint8_t* async_peek_field_rgba(
        std::size_t channel, std::size_t slot) const noexcept;
    [[nodiscard]] bool async_peek_field_pending(std::size_t channel) const noexcept {
        return async_peek_fields_[channel].fence != 0u;
    }
    [[nodiscard]] bool any_async_peek_field_pending() const noexcept {
        for (const AsyncPeekField& field : async_peek_fields_) {
            if (field.fence != 0u) return true;
        }
        return false;
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
    // The EFB's samples per pixel as a power of two (MSAA; 0 for one sample).
    [[nodiscard]] std::uint8_t efb_samples_log2() const noexcept {
        return static_cast<std::uint8_t>(std::countr_zero(efb_msaa_.samples));
    }
    // The live EFB's variant, which its draws' pipelines share (shader_keys.h RenderStateKey::samples_log2): its
    // samples, and kEfbVariantHdr while the HDR scene EFB stands in.
    [[nodiscard]] std::uint8_t efb_variant() const noexcept {
        return static_cast<std::uint8_t>(efb_samples_log2() | (efb_hdr_ ? kEfbVariantHdr : 0u));
    }
    [[nodiscard]] ID3D12RootSignature* root_signature() const {
        return root_signature_.Get();
    }
    [[nodiscard]] double display_refresh_hz() const noexcept { return display_refresh_hz_; }
    // The display's vblanks (host_window::vblank_grid): the latest and the interval, on galaxy::qpc_now()'s clock.
    [[nodiscard]] bool vblank_grid(std::int64_t& vblank, std::int64_t& period) const noexcept;
    [[nodiscard]] UploadRing& vertex_ring() { return vertex_ring_; }
    [[nodiscard]] UploadRing& index_ring() { return index_ring_; }
    [[nodiscard]] UploadRing& constant_ring() { return constant_ring_; }
    [[nodiscard]] UploadRing& matrix_ring() { return matrix_ring_; }
    [[nodiscard]] unsigned frame_slot() const { return frame_slot_; }
    // Bytes held by the EFB-copy destination cache. Destinations are allocated at the
    // internal resolution, so this grows with efb_scale^2 against a fixed budget.
    [[nodiscard]] std::uint64_t efb_copy_dest_bytes() const {
        return efb_copy_dests_bytes_;
    }
    // The copy-destination byte budget (192 MiB), reported next to the live bytes.
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

    // Loads and uploads the settings overlay's corner texture on the first call;
    // later calls do nothing.
    void ensure_native_settings_corner_texture_loaded() override;
    // Draws one quad of the corner texture (screen pixel coordinates); the UV
    // range mirrors it for each panel corner. No-op if the texture failed to load.
    void draw_ui_textured_quad(
        int x, int y, int w, int h,
        float u0, float v0, float u1, float v1,
        const std::array<float, 4>& tint) override;

    // Creates the solid-fill quad root signature and PSO on first use. Solid quads
    // are draws, not rect-limited clears: ClearRenderTargetView with non-null pRects
    // faults inside nvwgf2umx.dll (STATUS_STACK_BUFFER_OVERRUN) on some NVIDIA drivers.
    void ensure_native_settings_solid_pipeline_loaded();
    // Draws one flat-colored quad (screen pixel coordinates).
    void draw_ui_solid_quad(
        int x, int y, int w, int h,
        const std::array<float, 4>& color) override;
    // Packed row-major 5x7 bitmap, drawn with one instanced submission.
    void draw_ui_glyph(
        int x, int y, int scale, std::uint64_t bitmap,
        const std::array<float, 4>& color) override;

private:
    void invalidate_gx_bindings();
    // VSYNC ON presents each frame for round(refresh / rate) refreshes when
    // the refresh is a multiple of the present rate (60 at 120 Hz, 30 at
    // 60 Hz), so every frame stays on screen equally long; other rates keep
    // one refresh.
    [[nodiscard]] UINT present_sync_interval(unsigned configured, float max_fps);
    void refresh_display_rate();
    void wait_for_frame_latency_if_needed(bool collect_present_telemetry);
    void wait_for_gpu();            // shutdown/resize only
    // Applies a pending window resize (the extent mailbox in renderer_d3d12.cpp).
    // Called only from begin_frame(), between command-list recordings.
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
    // The MSAA sample count the settings ask for and the device has for the EFB's formats: 1 without MSAA, and
    // with a temporal upscaler or ray tracing, which read the EFB's single-sample targets directly.
    [[nodiscard]] unsigned wanted_msaa_samples() const;
    // The most samples per pixel the device has for both EFB formats (1: none), asked once.
    [[nodiscard]] unsigned msaa_max_samples() const;
    // Before the EFB's single-sample color (and `depth`) are read: the multisampled targets drawn since the last
    // resolve, resolved into them.
    void resolve_efb(bool depth, bool color = true);
    // The resolve itself into `list` (a separately submitted list resolves what was submitted before it). The depth
    // resolve leaves `list` with the blit root signature and the single-sample depth bound.
    void record_efb_resolve(ID3D12GraphicsCommandList* list, bool color, bool depth);
    // resolve_efb for a reader recording into `list`: another list resolves (what the frame's list submitted before
    // it) without clearing the frame's pending resolve, which can hold draws not submitted yet.
    void resolve_efb_on(ID3D12GraphicsCommandList* list, bool color, bool depth);
    [[nodiscard]] unsigned output_efb_scale_for_window() const;
    [[nodiscard]] bool ensure_motion_target();
    [[nodiscard]] bool record_temporal_upscale(unsigned output_scale);
    [[nodiscard]] bool ensure_output_efb(unsigned scale);
    void swap_efb_sets();
    // Live internal-resolution change, applied between frames by begin_frame
    // when get_render_config().efb_scale differs from the live targets.
    void apply_pending_efb_scale();
    // Creates, replaces or releases the temporal upscaler for the live render
    // and output scales, from begin_frame before its command list records.
    void prepare_temporal_upscaler();
    // The INTERNAL RESOLUTION row's scale, taken by apply_pending_efb_scale: the
    // temporal upscaler's output (its quality mode renders below it).
    unsigned upscale_output_scale_ = 0;
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
    // Match Output's GPU budget: the frame command lists' measured GPU time over 2 s windows. When it stays above
    // kGpuBudgetBusy of the wall time for two windows, the internal resolution Match Output may pick is capped one step
    // lower for the session, so a GPU too slow for the window's resolution keeps every frame.
    void update_gpu_budget();
    double gpu_budget_ms_ = 0.0;
    std::chrono::steady_clock::time_point gpu_budget_window_start_{};
    unsigned gpu_budget_over_windows_ = 0;
    unsigned gpu_budget_cap_ = 0;  // 0: none
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
    // MSAA: an EFB's draws and clears go to these multisampled targets (the set's rtv and dsv name them); its color
    // and depth, which copies, peeks and readbacks read, are their resolves (resolve_efb).
    struct EfbMsaa {
        Microsoft::WRL::ComPtr<ID3D12Resource> color;
        Microsoft::WRL::ComPtr<ID3D12Resource> depth;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsv_heap;  // the multisampled depth, then the resolve's target
        std::optional<DescriptorRing::Table> depth_srv;  // the multisampled depth, read by the depth resolve
        unsigned samples = 1;
        bool color_dirty = false;
        bool depth_dirty = false;
    };
    // The set's multisampled targets for `samples` (none for one), with `rtv` and `dsv` pointed at them.
    bool create_efb_msaa(EfbMsaa& msaa, ID3D12Resource* depth, unsigned scale, unsigned samples,
                         D3D12_CPU_DESCRIPTOR_HANDLE& rtv, D3D12_CPU_DESCRIPTOR_HANDLE& dsv);
    void write_msaa_depth_view(EfbMsaa& msaa);
    [[nodiscard]] ID3D12PipelineState* clear_pipeline(bool color);
    EfbMsaa efb_msaa_;
    mutable unsigned msaa_max_samples_ = 0u;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> depth_resolve_pso_;
    struct EfbTargetSet {
        Microsoft::WRL::ComPtr<ID3D12Resource> color;
        Microsoft::WRL::ComPtr<ID3D12Resource> depth;
        EfbMsaa msaa;
        unsigned scale = 0;
        bool hdr = false;   // colour kEfbHdrColorFormat (else kEfbColorFormat)
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
    // An FSR 3 context being created off the render thread (its pipelines take seconds on an
    // integrated GPU), for the sizes and mode recorded below.
    std::future<std::unique_ptr<TemporalUpscaler>> temporal_pending_;
    unsigned temporal_kind_ = 0;       // upscaler setting it was made for (2 FSR 3, 3 DLSS)
    unsigned temporal_quality_ = 0;
    // A presented frame's Streamline markers have begun (begin_frame) and not yet ended (present).
    bool streamline_frame_open_ = false;
    // DLSS Frame Generation's HUD-less picture: the upscaled scene before the 2D layer (taken at
    // the output EFB switch), copied as the display copy copies the frame and presented as the
    // frame is, into a backbuffer-sized target.
    EfbTargetSet fg_hudless_efb_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> fg_hudless_rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> fg_hudless_dsv_heap_;
    XfbTexture fg_hudless_xfb_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> fg_hudless_target_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> fg_hudless_target_rtv_heap_;
    bool fg_hudless_scene_ = false;              // this frame's scene was taken
    std::uint32_t fg_hudless_xfb_addr_ = 0u;     // the display copy it went with (0: none)
    void take_fg_hudless_scene();
    void copy_fg_hudless_xfb(const EfbCopyParams& params, const XfbTexture& dest);
    void present_fg_hudless(const XfbTexture& source, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor);
    unsigned temporal_render_w_ = 0;
    unsigned temporal_render_h_ = 0;
    unsigned temporal_output_w_ = 0;
    unsigned temporal_output_h_ = 0;
    std::uint64_t temporal_frame_ = 0;
    bool temporal_history_valid_ = false;
    bool scene_temporal_ = false;
    // The scene writes the motion target: a temporal scene, or the ray tracing engine's without a temporal upscaler.
    bool scene_motion_ = false;
    float scene_jitter_[2]{};
    float scene_camera_[3]{1.0f, 100000.0f, 1.0f};
    std::array<float, 12> scene_view_{};
    bool scene_view_known_ = false;
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
    enum class ReplayKind : std::uint8_t {
        Draw, BindEfb, Clear, Copy, SwapEfbSets, ClearDepth, RtComposite, RtWater, SceneAa, SceneAo,
        HdrBegin, HdrEnd, Bloom
    };
    // Bloom: a game_bloom marker.
    struct ReplayBloom {
        std::uint16_t phase = 0;
        std::uint32_t a = 0, b = 0;
    };
    void record_game_bloom(const ReplayBloom& bloom);
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
    std::vector<ReplayBloom> replay_blooms_;
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
    std::vector<ReplayBloom> play_blooms_;
    bool play_start_output_active_ = false;
    void publish_replay_log();
    // The log being interpolated (moved from play_* by start_replay_playback),
    // so a newer completed log can wait while sub-frames of this one run.
    std::vector<ReplayOp> run_ops_;
    std::vector<DrawCall> run_draws_;
    std::vector<ReplayViewport> run_viewports_;
    std::vector<ReplayClear> run_clears_;
    std::vector<ReplayCopy> run_copies_;
    std::vector<ReplayBloom> run_blooms_;
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
    // An upload ring could not grow (out of upload memory): no further attempts.
    bool ring_growth_failed_ = false;
    bool present_game_frame_ = false;
    unsigned ring_segment_ = 0;
    std::array<std::uint64_t, kInterpSegments> ring_segment_fence_{};
    bool ring_advance_pending_ = true;
    // Sub-frames render into their own EFB so a later frame's partial EFB
    // (its chunks interleave with the sub-frames) stays intact.
    EfbTargetSet replay_efb_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> replay_efb_rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> replay_efb_dsv_heap_;
    // The HDR scene EFB (begin_hdr_scene) of the live frames and of the sub-frames (a later frame's scene may be open
    // while they play).
    EfbTargetSet hdr_scene_efb_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> hdr_scene_rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> hdr_scene_dsv_heap_;
    EfbTargetSet replay_hdr_efb_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> replay_hdr_rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> replay_hdr_dsv_heap_;
    // The live EFB is an HDR set (swapped with the set that held it).
    bool efb_hdr_ = false;
    void record_hdr_begin();
    void record_hdr_end();
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
                        bool persistent_views = true, bool unordered_access = false, unsigned samples = 1u,
                        bool hdr = false);
    void write_efb_set_views(EfbTargetSet& set, const DescriptorRing::Table& table);
    void swap_efb_with(EfbTargetSet& other);
    bool replay_playing_ = false;
    bool replay_start_output_active_ = false;
    unsigned replay_source_slot_ = 0;
    XfbTexture subframe_xfb_{};
    // EFB-copy textures are shared with the live frames: a later game frame's
    // chunks may already have rewritten a texture the sub-frame's log copies
    // to. Such textures are saved to scratch before the sub-frame and restored
    // after it (record_subframe_guard). note_copy_write records a real copy's
    // destination in the log being recorded (Interp mode).
    void note_copy_write(ID3D12Resource* dest);
    std::uint64_t play_serial_ = 0;       // replay serial of the ready log
    std::uint64_t run_serial_ = 0;        // replay serial of the playing log
    // The log played before run_* (its vertices stay in its ring segment
    // until the segment comes round again) and, per run_* draw, its matched
    // draw's vertices (prepare_vertex_blend; 0 for none).
    std::vector<DrawCall> prev_run_draws_;
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
    // The EFB colour as the ray-traced composite reads it (it draws into the EFB): a copy, and its
    // view in this frame's descriptor ring.
    D3D12_GPU_DESCRIPTOR_HANDLE rt_composite_color();
    D3D12_GPU_DESCRIPTOR_HANDLE rt_copy_color(Microsoft::WRL::ComPtr<ID3D12Resource>& target, const wchar_t* name);
    D3D12_GPU_DESCRIPTOR_HANDLE rt_game_picture();
    Microsoft::WRL::ComPtr<ID3D12Resource> rt_color_copy_;
    Microsoft::WRL::ComPtr<ID3D12Resource> rt_water_copy_;   // the frame with the game's water (water composite)
    Microsoft::WRL::ComPtr<ID3D12Resource> rt_opaque_copy_;  // the frame before the translucent phases
    D3D12_GPU_DESCRIPTOR_HANDLE rt_opaque_srv_{};             // its view this frame (0: none)
    void record_rt_water(int slot, bool reproject);
    void log_rt_seq(const char* step);
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
    std::array<double, 3> subframe_phase_ms_{};  // evaluation, replay, present
    // The temporal upscaler upscales the sub-frames (and the frame at t = 1)
    // instead of the game frames.
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
    // Sub-frame copy redirection (set_subframe_copy_redirect can select the
    // save/restore guard above instead). Real frames own the copy textures. A
    // sub-frame's texture copies land in redirect targets, and its draws that
    // sample a redirected texture get a table patched in the current
    // descriptor segment. A copy texture a log samples before
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
    // The same by copy path (0: guard, 1: redirected, 2: neither).
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
    AmbientOcclusion ambient_occlusion_;
    GameBloom game_bloom_;
    RtView scene_ao_view_{};
    // EFB-copy destination textures (render targets) are placed in shared
    // heaps rather than committed one by one (placed_texture_allocator.h).
    PlacedTextureAllocator efb_copy_heap_{
        D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES, 128ull << 20};

    // Settings overlay: the game's window-frame corner texture (read from
    // files/LayoutData/FileSelect.arc, galaxy/gx/game_asset_extract.h), drawn at the
    // panel's four corners. Loaded once, when the overlay is first shown; without it
    // the overlay keeps solid-color corners.
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

    // Debug EFB readback (debug_copy_efb_to_readback / debug_log_readback).
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_buf_;
    bool readback_pending_ = false;
    std::uint64_t readback_fence_value_ = 0;
    std::string next_efb_capture_path_;
    std::string readback_capture_path_;
    Microsoft::WRL::ComPtr<ID3D12Resource> backbuffer_readback_buf_;
    bool backbuffer_readback_pending_ = false;
    std::uint64_t backbuffer_readback_fence_value_ = 0;
    UINT backbuffer_readback_width_ = 0;
    UINT backbuffer_readback_height_ = 0;
    UINT backbuffer_readback_row_pitch_ = 0;
    std::string next_backbuffer_capture_path_;
    std::string backbuffer_readback_capture_path_;
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
    PresentPacer present_pacer_;
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
    // GPU time by a command list's place in its frame (0: the first list after a present; the last place collects
    // the rest), for present-stats' gpu-by-list-ms.
    static constexpr unsigned kGpuListPlaces = 6;
    std::array<std::uint8_t, kFramesInFlight> timestamp_list_place_{};
    unsigned list_in_frame_ = 0;
    std::array<double, kGpuListPlaces> telemetry_gpu_by_place_{};
    std::array<unsigned, kGpuListPlaces> telemetry_gpu_by_place_count_{};
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
        // Last copy that used this destination, for the bound below. A destination
        // is a pure function of its key, so evicting one only costs a re-creation.
        std::uint64_t last_used = 0;
        // Resident bytes of `texture`, for the byte budget below.
        std::uint64_t bytes = 0;
    };
    // One texture per destination key, at the copy's scaled extent in kEfbColorFormat
    // (a full 640x456 copy: 1.11 MB at 1x, 17.81 MB at 4x, 40.08 MB at 6x). Bounded in
    // bytes, not entries, since an entry's cost varies with the copy extent and the
    // internal scale; sizes are the driver's GetResourceAllocationInfo. The 192 MiB budget
    // is well above a scene's working set (a few XFBs across a couple of formats plus one
    // bloom workspace).
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

    // Conversion blit pipeline (its root signature is a module static beside the
    // present blit's) and CPU-only descriptors for conversion destinations. RTV
    // descriptors are consumed at record time, so every copy rebinds the same
    // CPU-only scratch slot. EFB source SRVs occupy an immutable prefix of the
    // active shader-visible heap.
    Microsoft::WRL::ComPtr<ID3D12PipelineState> conv_pipeline_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> conv_rtv_heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE conv_scratch_rtv_{};
    detail::ConversionRtvSlot conv_scratch_rtv_slot_;
    std::optional<DescriptorRing::Table> conv_efb_source_table_;
    D3D12_GPU_DESCRIPTOR_HANDLE conv_efb_color_srv_{};
    D3D12_GPU_DESCRIPTOR_HANDLE conv_efb_depth_srv_{};
    // The masked EFB clears (color / alpha only): shaders and pipelines, made at first use.
    Microsoft::WRL::ComPtr<ID3DBlob> clear_vs_;
    Microsoft::WRL::ComPtr<ID3DBlob> clear_ps_;
    // Per EFB variant (efb_variant: samples, then HDR).
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 8> clear_rgb_pipelines_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 8> clear_alpha_pipelines_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> motion_debug_root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> motion_debug_pipeline_;

    // Synchronous GXPeekARGB/GXPeekZ: the render thread copies the pixel through
    // a 1x1 conversion target into a readback buffer.
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
    std::array<AsyncPeekField, kAsyncPeekFieldChannels> async_peek_fields_{};
    // AsyncPeekField::fence of a field recorded in the open frame.
    static constexpr std::uint64_t kAsyncPeekInFrame = ~std::uint64_t{0};
    bool ensure_async_peek_field_resources(AsyncPeekField& field, EfbPeekKind kind);
    void record_async_peek_field_commands(
        ID3D12GraphicsCommandList* list, AsyncPeekField& field, EfbPeekKind kind, std::size_t slot);
    bool read_efb_pixels(std::uint16_t x, std::uint16_t y, EfbPeekKind kind,
        std::uint32_t& value, std::span<std::uint32_t> depth_pixels,
        bool defer_pointer_depth = false);
    static const std::uint8_t* pin_readback_mapping(ID3D12Resource* readback);
    void select_pointer_depth_destination();
    void copy_efb_readback_pixels(ID3D12Resource* readback, UINT width,
        UINT height, UINT row_pitch, bool is_depth, std::uint32_t& value,
        std::span<std::uint32_t> depth_pixels);

};

// The renderer GxBackend builds against (renderer_vk.h defines it for Vulkan).
using Renderer = RendererD3D12;

}  // namespace galaxy::gx
