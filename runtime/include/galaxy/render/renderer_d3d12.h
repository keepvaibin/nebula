#pragma once

// RendererD3D12: device, swap chain, scene target/display picture render targets, per-frame upload
// rings, draw submission, and presentation.
//
// Frame pipelining: kFramesInFlight frames are recorded ahead of the GPU;
// begin_frame() waits only on the fence of frame N - kFramesInFlight. A full
// CPU/GPU sync happens only in shutdown() and on resize.
//
// All draws target the offscreen scene target (color RGBA8 + depth
// D24_UNORM_S8_UINT, with 24-bit depth). present()
// aspect-fits an display picture into the swap-chain backbuffer.
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

#include "galaxy/render/fsr_frame_generation.h"
#include "galaxy/render/utility_shaders.h"
#include "galaxy/render/frame_pass.h"
#include "galaxy/render/temporal_upscaler.h"
#include "galaxy/render/gpu_upload_ring.h"
#include "galaxy/render/interp_gpu.h"
#include "galaxy/render/post_process.h"
#include "galaxy/render/ambient_occlusion.h"
#include "galaxy/render/scene_camera.h"
#include "galaxy/render/game_bloom.h"
#include "galaxy/render/interp_types.h"
#include "galaxy/render/rt_types.h"
#include "galaxy/render/gpu_placed_textures.h"
#include "galaxy/render/draw_types.h"
#include "galaxy/render/gpu_types.h"
#include "galaxy/render/renderer_common.h"
#include "galaxy/render/pso_key.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/ui_canvas.h"
#include "galaxy/render/window_owner.h"
#include "galaxy/render_api.h"

#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <span>
#include <atomic>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace galaxy::render {

class TextureCache;
struct TextureHandle;

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

// The scene: the art's own blending, in gamma, at 10 bits a channel. No shader reads its alpha.
inline constexpr DXGI_FORMAT kSceneColorFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
// A texture target's colour: the game keeps masks in its alpha.
inline constexpr DXGI_FORMAT kTargetColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
// The wide 3D scene's target (RendererD3D12::begin_wide_scene): light past white stays, display-encoded above 1 as below,
// until the camera at the scene's end brings it into the RGBA8 scene target.
inline constexpr DXGI_FORMAT kSceneHdrColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
// Motion vectors for temporal upscaling: render pixels, current -> previous.
inline constexpr DXGI_FORMAT kMotionVectorFormat = DXGI_FORMAT_R16G16_FLOAT;
// Reactive mask for temporal upscaling: blended draws' alpha, max-blended.
inline constexpr DXGI_FORMAT kReactiveMaskFormat = DXGI_FORMAT_R8_UNORM;
// The effects layer (kRenderStateLayerTarget): its colour (what the effects added, as they blended into the scene target) and
// transmittance (the product of their destination factors), in the scene target's own encoding.
inline constexpr DXGI_FORMAT kLayerFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
inline constexpr DXGI_FORMAT kSceneDepthResourceFormat =
    DXGI_FORMAT_R24G8_TYPELESS;
inline constexpr DXGI_FORMAT kSceneDepthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
inline constexpr DXGI_FORMAT kSceneDepthSrvFormat =
    DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
inline constexpr DXGI_FORMAT kBackbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

// Builds the graphics root signature used by RendererD3D12. Root
// CBV b0 is visible to both VS and GS because line and point expansion
// reads its scale-aware raster constants in the geometry stage.
[[nodiscard]] HRESULT create_draw_root_signature(
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

// scene target resources and these views share the renderer's initialize/shutdown
// lifetime. The source resources must outlive every submitted use of the heap.
// Returns color then depth; consumes exactly two persistent descriptors.
[[nodiscard]] std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 2>
create_scene_conversion_source_views(
    ID3D12Device* device, ID3D12Resource* color, ID3D12Resource* depth,
    DescriptorRing& ring);

}  // namespace detail

class RendererD3D12 final : public UiCanvas {
public:
    // Creates the Win32 window, device, queue, swap chain, scene target targets at
    // at the frame's size, the shared root signature, the
    // present-blit pipeline, and all rings.
    bool initialize(int window_width, int window_height, const FrameMap& frame);
    [[nodiscard]] std::string startup_failure() const { return init_error_; }
    std::string init_error_;  // why initialize failed
    // Creates the screen probes' resources up front, so the first frame that asks does not stall creating them.
    void create_first_use_resources();
    void shutdown();                // full GPU drain, then release
    // A persistently mapped upload buffer; empty when it cannot be created.
    [[nodiscard]] UploadBuffer create_upload_buffer(std::size_t bytes, const char* name);
    // The frame's table of `count` texture descriptors for the draw's maps (null SRVs where a handle is empty).
    [[nodiscard]] gpu::Table texture_table_for(const TextureHandle* handles, unsigned count, const TextureCache& cache);
    // The texture cache records its uploads into this frame's command list.
    void attach_texture_uploads(TextureCache& cache);
    // The upload rings' sizes and the most one frame took (GALAXY_TRACE_MEMORY).
    void trace_rings() const;
    void pump_messages();           // no-op: window_owner_ pumps the window's messages
    [[nodiscard]] bool quit_requested() const noexcept;

    // Advances the frame ring: waits on the fence of frame N-kFramesInFlight
    // only, resets that slot's command allocator, opens the command list,
    // and rewinds the slot's ring segments. Swap-chain latency is waited in
    // present(), after scene target work is recorded but before the backbuffer is
    // selected, so gameplay work is not paced by DWM too early.
    // Test only (GALAXY_TEST_GPU_LOST_FRAME): the device removed, as a driver reset leaves it.
    void test_lose_device();
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

    // Binds scene target RTV+DSV and applies the viewport from the draw.
    void bind_scene();

    // HUD split: from the end of the game's 3D scene to the display copy the
    // scene target is a second target at the window's integer scale, so the 2D layer
    // renders at display resolution over the resampled scene. The two target
    // sets share layout and resource states and are swapped as a whole, so
    // every scene target path (draws, clears, copies, probes) works unchanged on either.
    // begin_output_scene returns false when the split is off or the scene target already
    // covers the window.
    [[nodiscard]] bool begin_output_scene();
    // A pass into texture target `key` (E-07): its target, `width` x `height` game units at the scene target's scale, takes
    // the scene target's place until end_target_pass, so the pass's draws and clears go to it. Each key keeps its target
    // between passes (made again only when its size or the scene target's scale changes); the pass is drawn once, never
    // replayed by sub-frames. False when the target cannot be made or a pass is open.
    [[nodiscard]] bool begin_target_pass(std::uint64_t key, std::uint32_t width, std::uint32_t height);
    // Ends it: the scene target comes back and the target's picture is what draws sampling `key` at that size read (at full
    // precision, in any format, through `channels`: NebulaTargetChannels).
    void end_target_pass(std::uint8_t channels, TextureCache& textures);
    // The picture of target `key` when it is `width` x `height`: its texture and view.
    [[nodiscard]] bool target_picture(std::uint64_t key, TextureHandle& out) const;
    [[nodiscard]] bool target_pass_active() const noexcept { return target_pass_active_; }
    void end_output_scene();
    // An scene target-to-texture copy from the output scene target after end_output_scene: the picture its display copy took (the
    // scene with the 2D layer), which the single scene target still holds until something draws.
    [[nodiscard]] bool draw_frame_pass_from_output(const FramePassRequest& params, TextureCache& textures);
    // The scene's anti-aliasing (post_process.h) in the render scene target at the end of the 3D scene,
    // before the 2D layer and begin_output_scene. A temporal upscaler antialiases the scene itself.
    void record_scene_aa();
    // The 3D scene in a wide target while `wanted` (the enhanced lighting), its camera at the end (scene_camera.h);
    // sub-frames replay both.
    void begin_wide_scene(bool wanted);
    void end_wide_scene();
    // Ambient occlusion (ambient_occlusion.h) of the scene drawn so far, from the view `set_scene_ao_view` gave:
    // after the opaque phases, before the translucent ones; sub-frames replay it.
    [[nodiscard]] bool ambient_occlusion_ready() const noexcept { return ambient_occlusion_.ready(); }
    void set_scene_ao_view(const RtView& view) noexcept { scene_ao_view_ = view; }
    // The scene's models (ambient_occlusion_objects.h); a replay reads the last game frame's.
    void set_scene_ao_objects(std::vector<ao::ObjectOccluder>& objects) noexcept { scene_ao_objects_.swap(objects); }
    void record_scene_ao();
    // The game's bloom (game_bloom.h) at a kMarkerBloom marker (galaxy/motion_signals.h: `phase` its aux, `a` and `b`
    // its values); sub-frames replay it.
    [[nodiscard]] bool game_bloom_ready() const noexcept { return game_bloom_.ready(); }
    void game_bloom(std::uint16_t phase, std::uint32_t a, std::uint32_t b);
    [[nodiscard]] bool output_scene_active() const noexcept { return output_scene_active_; }
    // Whether the targets for `scale` fit the adapter's video-memory budget
    // next to everything already resident.
    [[nodiscard]] bool scene_fits_memory(const FrameMap& map) const;

    // A temporal upscaler (FSR 3 or DLSS) is selected and available.
    [[nodiscard]] bool temporal_mode() const;
    // At the start of the game's 3D scene: whether the temporal path runs for
    // it and, if so, the scene's sub-pixel jitter in render pixels (applied to
    // perspective projections).
    [[nodiscard]] bool scene_temporal_begin(float& jitter_x, float& jitter_y);
    [[nodiscard]] unsigned temporal_output_width() const noexcept { return temporal_output_w_; }
    // After scene_temporal_begin, in a temporal scene or one the ray tracing
    // engine lights (its histories follow moving surfaces by the motion; no
    // jitter, no upscale): clears the motion target for the scene. False
    // otherwise or when it could not be created (no motion writers).
    [[nodiscard]] bool begin_motion_scene();
    // The game's effects begin (the scene's ShadowSurface draw type, after its translucent lists) in a scene whose
    // blended draws write the reactive mask: the effects layer opens (cleared: nothing added, everything let through)
    // and its draws record what they blend over the scene target until the water's composite (kRenderStateLayerTarget).
    void begin_layer_window();
    [[nodiscard]] bool layer_window() const noexcept { return layer_window_; }
    // GALAXY_MOTION_DEBUG: at the scene's end, replaces the scene target color with
    // the motion field (grey = still; red/green = +x/+y towards the previous
    // position, 8 pixels to full scale).
    void record_motion_debug_view();
    // The temporal history does not match the picture (scene cut).
    void reset_temporal_history() noexcept { temporal_history_valid_ = false; }

    // When the swap chain last took a present (QPC; 0 none).
    [[nodiscard]] std::int64_t last_present_qpc() const noexcept { return swap_present_qpc_; }
    // Matrix-ring segment advances back whose palettes are still alive while
    // this frame executes (0 outside temporal mode). In temporal mode the
    // segment advances only after frames that wrote palettes, so present and
    // utility frames do not age the previous scene's palettes.
    [[nodiscard]] unsigned matrix_history_frames() const noexcept {
        return matrix_segments_ - kFramesInFlight;
    }
    // Counts the matrix ring's rewinds (every frame in normal mode, segment advances in temporal mode): a scene's
    // palettes are addressable while the count has moved by at most matrix_history_frames().
    [[nodiscard]] std::uint64_t matrix_segment_serial() const noexcept {
        return matrix_ring_.epoch();
    }
    [[nodiscard]] std::uint64_t frame_index() const noexcept { return frame_index_; }

    // Interpolated redraw (present rates above 60). While recording, a render
    // chunk's scene target operations (draws, scene target bindings, clears, copies, scene target set
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
    // Completed logs, counted; the game display picture the latest one copied to.
    [[nodiscard]] std::uint64_t replay_serial() const noexcept { return replay_serial_; }
    [[nodiscard]] std::uint32_t replay_picture_slot() const noexcept { return replay_picture_slot_; }
    // A 3D scene began inside the log being recorded (one is replayable).
    void note_replay_scene() noexcept { ++replay_scene_count_; }
    // The log being recorded is a frame that draws no 3D scene, recorded as its screen layer alone (RenderCore::
    // begin_screen_frame): replayable without a scene; dropped again when a scene begins in it after all.
    void note_replay_screen_frame() noexcept { replay_screen_frame_ = true; }
    void cancel_replay_screen_frame() noexcept { replay_screen_frame_ = false; }
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
    // in a renderer-owned display picture. present_replay_subframe evaluates the sequence's
    // program at t and executes that replay. False when the log is gone or has
    // no display copy (the sequence then shows its real frame only).
    using ReplaySequence = render::ReplaySequence;  // interp_types.h
    bool begin_replay_sequence(const ReplaySequence& sequence);
    // vertices per particle): interpolated per particle between game frames.
    // A presented frame of the sequence: t, the t of the frame presented before
    // it (motion vectors) and its jitter in render pixels (temporal upscaling).
    using SubframeTime = render::SubframeTime;  // interp_types.h
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
        if (base == 0u) {
            resident_upload_.Reset();
            resident_upload_mapped_ = nullptr;
            resident_local_ = nullptr;
            resident_copies_.clear();
            for (ResidentStaging& staging : resident_staging_) {
                staging = {};
            }
        }
    }
    // The resident decoded vertices (RenderCore): the GPU reads them from local memory at the returned address (GPU
    // reads of upload memory measured 2.7x slower on an AMD APU). Where local memory cannot be had they live in upload
    // memory instead.
    [[nodiscard]] UploadBuffer create_resident_vertices(std::size_t bytes, const char* name);
    // Where to write the bytes at `offset`: staging memory that lives while this frame does, the copy running before
    // the command list being recorded.
    [[nodiscard]] std::byte* stage_resident_vertices(std::uint64_t offset, std::uint64_t bytes);
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
    // scene target-copy textures (`copies[m]`, null for other slots). The log keeps CPU
    // copies of its kReplayTableSlots descriptors (`sources`, valid now) so
    // sub-frames can point the copy slots elsewhere.
    static constexpr unsigned kReplayTableSlots = 8u;
    void note_replay_texture_table(D3D12_GPU_DESCRIPTOR_HANDLE table,
                                   const D3D12_CPU_DESCRIPTOR_HANDLE* sources,
                                   ID3D12Resource* const* copies);
    // Sub-frame copy redirection since the last call: copies written to
    // redirect targets, tables patched, read-before-write snapshots taken by
    // real frames, logs that fell back to the guard (a table was not kept).
    using CopyRedirectStats = render::CopyRedirectStats;  // interp_types.h
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
    // The scene camera's clip planes and vertical field of view (radians).
    void set_scene_camera(float near_plane, float far_plane, float fov_y) noexcept;
    // Render thread, at each scene's start: its camera (world to eye), or null when unknown.
    void set_scene_view(const std::array<float, 12>* view) noexcept {
        scene_view_known_ = view != nullptr;
        if (view != nullptr) {
            scene_view_ = *view;
        }
    }
    // Colour from the caller, depth to far, within the rect in target pixels (w == 0: the whole target).
    void clear_scene(
        std::uint32_t clear_rgba,
        bool color_enable,
        bool alpha_enable,
        bool depth_enable,
        unsigned rect_x = 0,
        unsigned rect_y = 0,
        unsigned rect_w = 0,
        unsigned rect_h = 0,
        std::uint8_t pixel_format = 0);

    void draw(const DrawCall& call);

    // Passes over the frame into a picture, recorded into the current command list.
    void copy_scene_to_display(const FramePassRequest& params, DisplayTexture& dest);
    // Native presentation: whether this display copy can take the scene target's colour target instead of copying it
    // (hand_over_scene_color): an exact copy of the scene target's top rows (the identity filter, no gamma or scaling) on the
    // plain path (one sample a pixel, no HDR scene, ray tracing, temporal upscaler, output-resolution 2D layer,
    // interpolation or frame generation, post-processing ready).
    [[nodiscard]] bool can_hand_over_display_copy(const FramePassRequest& params, const DisplayTexture& dest) const;
    // Gives the scene target's colour target (the picture of this display copy, as an display picture keeps it) to the caller and continues
    // the scene target on `fresh` (a released texture of the scene target's description, or null for a new one). The picture is owed to
    // the scene target until a clear covers it (settle_owed_scene).
    [[nodiscard]] gpu::TextureRef hand_over_scene_color(gpu::TextureRef fresh);
    // Pictures offered for a hand-over from before a settings change, until the GPU is past them.
    struct OldSceneTarget {
        gpu::TextureRef texture;
        std::uint64_t free_from = 0u;
    };
    std::vector<OldSceneTarget> old_scene_targets_;
    // The frame so far becomes picture `key`; the frame goes on in `fresh`, not owed its contents. `released` is the
    // picture `key` had.
    [[nodiscard]] bool can_take_scene() const noexcept;
    bool take_scene(std::uint64_t key, gpu::TextureRef fresh, TextureCache& textures, gpu::TextureRef& released);
    // The frame, and its depth, as pictures for the next target pass (a key of 0: not wanted); end_target_pass gives
    // them back, or close_frame_for_pass when the pass could not begin.
    void open_frame_for_pass(std::uint64_t color_key, std::uint8_t channels, std::uint64_t depth_key, TextureCache& textures);
    void close_frame_for_pass(TextureCache& textures);
    [[nodiscard]] bool draw_frame_pass(
        const FramePassRequest& params,
        TextureCache& textures);

    // Stretch-blit `source` to the backbuffer. A null/invalid source is a
    // runtime bug: callers must keep the last valid swap-chain image instead
    // of submitting a black frame. `black` presents black with the overlays
    // (black output); `source` then only sets the layout.
    // `capture_backbuffer` records the final post-blit swap-chain image.
    // Returns whether an optional backbuffer capture was recorded. Busy
    // diagnostic buffers never suppress the required blit/presentation.
    bool present(const DisplayTexture* source, bool capture_backbuffer = false, bool black = false);

    // Render-owner only. The picture's value at each point into slot `slot`'s readback, in the open frame where the
    // stream is now; screen_probes_finished names its bytes (RGBA8 per point) once that frame's fence has passed.
    static constexpr std::size_t kScreenProbeSlots = 12u;
    [[nodiscard]] bool record_screen_probes(std::size_t slot, std::span<const ScreenProbe> probes);
    [[nodiscard]] const std::uint8_t* screen_probes_finished(std::size_t slot) noexcept;
    bool debug_copy_scene_to_readback();
    void debug_log_readback();
    void debug_log_backbuffer_readback();
    bool debug_copy_selected_display_to_readback(const DisplayTexture& source);
    void debug_log_selected_display_readback();
    // The backend binds the exact output paths to the frame before recording
    // its optional readbacks. Paths are retained with the pending readback so
    // a later nonblocking completion cannot write to a subsequent frame's
    // diagnostic file.
    void set_debug_capture_paths(
        std::string scene_ppm_path,
        std::string backbuffer_ppm_path);

    [[nodiscard]] ID3D12Device* device() const { return device_.Get(); }
    [[nodiscard]] const FrameMap& frame_map() const noexcept { return frame_; }
    // The scene target's samples per pixel as a power of two (MSAA; 0 for one sample).
    [[nodiscard]] std::uint8_t scene_samples_log2() const noexcept {
        return static_cast<std::uint8_t>(std::countr_zero(scene_msaa_.samples));
    }
    // The live scene target's variant, which its draws' pipelines share (render_state_key.h RenderStateKey::samples_log2): its
    // samples, and kSceneVariantHdr while the HDR scene scene target stands in.
    [[nodiscard]] std::uint8_t scene_variant() const noexcept {
        return static_cast<std::uint8_t>(scene_samples_log2() | (scene_hdr_ ? kSceneVariantHdr : 0u) |
                                         (target_pass_active_ ? kSceneVariantTarget : 0u));
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
    // Bytes held by the scene target-copy destination cache. Destinations are allocated at the
    // internal resolution, so this grows with the frame's area against a fixed budget.
    // The copy-destination byte budget (192 MiB), reported next to the live bytes.
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
    // Exhaustion is explicit; defaults never replace a requested table.
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE default_sampler_table() const {
        return default_sampler_table_;
    }

    // Returns (building on first use) an 8-slot s0-s7 sampler table for the
    // given packed per-map sampler keys (see pack_sampler_key in
    // render_core.cpp). Tables are exact-key cached within this frame and remain
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
    void invalidate_draw_bindings();
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
    // windowed or borderless, mode, window size) at a frame
    // boundary.
    void reconcile_display_state();
    void reap_debug_capture_writers(bool wait_for_all);
    void queue_debug_ppm_capture(
        const char* tag,
        const std::string& path,
        const std::uint8_t* source,
        unsigned width,
        unsigned height,
        unsigned row_pitch,
        bool ten_bit = false);
    bool create_scene_targets(const FrameMap& map);
    void carry_over_scene_contents();
    // The MSAA sample count the settings ask for and the device has for the scene target's formats: 1 without MSAA, and
    // with a temporal upscaler or ray tracing, which read the scene target's single-sample targets directly.
    [[nodiscard]] unsigned wanted_msaa_samples() const;
    // The most samples per pixel the device has for both scene target formats (1: none), asked once.
    [[nodiscard]] unsigned msaa_max_samples() const;
    // Before the scene target's single-sample color (and `depth`) are read: the multisampled targets drawn since the last
    // resolve, resolved into them.
    void resolve_scene(bool depth, bool color = true);
    // The resolve itself into `list` (a separately submitted list resolves what was submitted before it). The depth
    // resolve leaves `list` with the blit root signature and the single-sample depth bound.
    void record_scene_resolve(ID3D12GraphicsCommandList* list, bool color, bool depth);
    // resolve_scene for a reader recording into `list`: another list resolves (what the frame's list submitted before
    // it) without clearing the frame's pending resolve, which can hold draws not submitted yet.
    void resolve_scene_on(ID3D12GraphicsCommandList* list, bool color, bool depth);
    [[nodiscard]] FrameMap output_frame_for_window() const;
    [[nodiscard]] bool ensure_motion_target();
    [[nodiscard]] bool record_temporal_upscale(const FrameMap& output);
    void dump_temporal_inputs(ID3D12Resource* color, ID3D12Resource* motion, std::uint64_t index);
    [[nodiscard]] bool ensure_output_scene(const FrameMap& map);
    void swap_scene_sets();
    void swap_target_set();
    // Live internal-resolution change, applied between frames by begin_frame
    // when the frame's size differs from the live targets.
    void apply_window_size();
    // Creates, replaces or releases the temporal upscaler for the live render
    // and output scales, from begin_frame before its command list records.
    void prepare_temporal_upscaler();
    // The INTERNAL RESOLUTION row's scale, taken by apply_window_size: the
    // temporal upscaler's output (its quality mode renders below it).
    FrameMap upscale_output_{};
    // Hands changed presentation settings (overlay or config) to the
    // post-processor at the frame boundary.
    void sync_post_process_settings();
    // A scale refused for the memory budget is tried again at this tick.
    std::uint64_t window_size_refused_ms_ = 0;
    // Match Output's GPU budget: the frame command lists' measured GPU time over 2 s windows. When it stays above
    // kGpuBudgetBusy of the wall time for two windows, the internal resolution Match Output may pick is capped one step
    // lower for the session, so a GPU too slow for the window's resolution keeps every frame.
    // (Re)creates the scene target color/depth conversion SRVs in their fixed slots.
    void refresh_scene_conversion_source_views();
    // The scene target's conversion views (colour and depth) into `table`.
    void write_scene_conversion_views(const DescriptorRing::Table& table);
    // The picture a handed-over scene target still owes (hand_over_scene_color): anything but a clear that covers it copies it
    // back before reading or writing the scene target's colour.
    Microsoft::WRL::ComPtr<ID3D12Resource> scene_owed_;
    // Inside the frame's command list; between lists the picture stays owed (create_scene_targets carries from it).
    void settle_owed_scene() {
        if (scene_owed_ && pass_list_open_) [[unlikely]] {
            restore_owed_scene(nullptr);
        }
    }
    // Copies the owed picture back, all of it or (`kept`) all but that rect, which a clear is about to cover.
    void restore_owed_scene(const D3D12_RECT* kept);
    // The conversion views after a handover, several in a frame: a table of its own, as the GPU reads views when it
    // runs, used again once the frames that could read it are done.
    std::uint64_t frame_pass_color_key_ = 0u;
    std::uint64_t frame_pass_depth_key_ = 0u;
    struct ConvSceneTable {
        DescriptorRing::Table table;
        std::uint64_t free_from = 0u;
    };
    std::vector<ConvSceneTable> conv_scene_tables_;
    std::size_t conv_scene_table_ = ~std::size_t{0};
    // Presents so far (the native-presentation A/B alternates by them); handovers, and how their pictures were settled:
    // a clear covered all of it, a clear left bands to copy back, or something read or wrote the scene target first.
    std::uint64_t presented_frames_ = 0;
    std::uint64_t scene_handovers_ = 0;
    std::uint64_t scene_handovers_free_ = 0;
    std::uint64_t scene_handovers_banded_ = 0;
    std::uint64_t scene_handovers_restored_ = 0;
    bool create_root_signature();
    bool create_sampler_heap();
    void reset_sampler_frame(); // only after this slot's fence completes
    bool create_conversion_pipeline();  // scene target-copy conversion blit PSO
    bool create_clear_pipeline();
    bool ensure_screen_probe_resources();

    // Records the conversion blit: fullscreen triangle into `dest` (RTV),
    // sampling the scene target color (or depth) SRV.  (src_x, src_y) is the source
    // origin and (step_x, step_y) the source texels advanced per dest pixel,
    // all in scaled scene target texels.  Caller handles dest resource transitions;
    // scene target transitions happen inside.  Restores root signature/scene target
    // binding before returning.
    void record_conversion_blit(
        ID3D12Resource* dest,
        unsigned dest_w,
        unsigned dest_h,
        D3D12_CPU_DESCRIPTOR_HANDLE dest_rtv,
        std::uint8_t copy_format,
        float src_x,
        float src_y,
        float step_x,
        float step_y);
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE allocate_conversion_rtv(
        ID3D12Resource* resource);

    WindowOwner window_owner_;
    HWND window_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    // The selected adapter, for video-memory budget queries.
    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swap_chain_;
    Microsoft::WRL::ComPtr<IDXGIFactory4> dxgi_factory_;
    // FSR 3 frame generation: its pacing swap chain while it is chosen.
    FsrFrameGeneration fsr_generation_;
    bool fsr_swap_chain_ = false;
    bool fg_hudless_ready_ = false;
    unsigned fsr_rect_[4]{};
    [[nodiscard]] bool fsr_generation_wanted() const;
    bool create_swap_chain(bool fsr);
    void recreate_swap_chain(bool fsr);
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>,
        kFramesInFlight> allocators_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    std::array<std::uint64_t, kFramesInFlight> fence_values_{};
    HANDLE fence_event_ = nullptr;
    HANDLE frame_latency_waitable_ = nullptr;
    // The display the window and swap chain were last brought to.
    struct AppliedDisplay {
        bool valid = false;
        unsigned mode = 0;
        std::array<char, 64> monitor{};
        unsigned window_width = 0;
        unsigned window_height = 0;
    };
    AppliedDisplay applied_display_{};
    std::uint64_t display_checks_ = 0;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> timestamp_query_heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> timestamp_readback_;
    std::array<bool, kFramesInFlight> timestamp_pending_{};
    std::uint64_t timestamp_frequency_ = 0;
    // GALAXY_GPU_PASSES=1: GPU time by pass for present-stats' gpu-pass-ms. A mark starts a pass on the open command
    // list; the time to the next mark (or the list's end) is that pass's.
    enum class GpuPass : std::uint8_t {
        Scene, Resolve, FramePass, DisplayCopy, Bloom, SceneAa, SceneAo, Upscale, RayTracing, Present, Camera, Count
    };
    static constexpr unsigned kGpuPassSlots = 512;
    void gpu_pass(GpuPass pass);
    class GpuPassScope {
    public:
        GpuPassScope(RendererD3D12& renderer, GpuPass pass) : renderer_(renderer), outer_(renderer.current_pass_) {
            renderer_.gpu_pass(pass);
        }
        ~GpuPassScope() { renderer_.gpu_pass(outer_); }
        GpuPassScope(const GpuPassScope&) = delete;
        GpuPassScope& operator=(const GpuPassScope&) = delete;
    private:
        RendererD3D12& renderer_;
        GpuPass outer_;
    };
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> pass_query_heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> pass_readback_;
    std::array<std::array<GpuPass, kGpuPassSlots>, kFramesInFlight> pass_marks_{};
    std::array<unsigned, kFramesInFlight> pass_mark_count_{};
    GpuPass current_pass_ = GpuPass::Scene;
    bool pass_list_open_ = false;
    std::array<double, static_cast<std::size_t>(GpuPass::Count)> telemetry_pass_ms_{};
    // GALAXY_GPU_DRAWS=1: each draw between two timestamps, its time summed by pipeline for present-stats'
    // [gpu-draws] lines (the timestamps serialize the draws: the total grows, the shares hold).
    static constexpr unsigned kGpuDrawSlots = 16384;
    struct GpuDrawMark {
        ID3D12PipelineState* pipeline = nullptr;
        std::uint32_t triangles = 0;
    };
    struct GpuDrawCost {
        double ms = 0.0;
        std::uint64_t draws = 0;
        std::uint64_t triangles = 0;
    };
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> draw_query_heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> draw_readback_;
    std::array<std::vector<GpuDrawMark>, kFramesInFlight> draw_marks_{};
    std::unordered_map<ID3D12PipelineState*, GpuDrawCost> telemetry_draw_cost_;
    // GALAXY_GPU_PASSES=1 also counts each command list's pipeline statistics (shader invocations, primitives) for
    // present-stats' gpu-stats.
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> stats_query_heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> stats_readback_;
    std::array<bool, kFramesInFlight> stats_pending_{};
    D3D12_QUERY_DATA_PIPELINE_STATISTICS telemetry_stats_{};

    // The command list run before each main one when it has work: resident vertex copies.
    bool ensure_pre_list();
    void record_pre_list();
    struct ResidentCopy {
        ID3D12Resource* source;
        std::uint64_t source_offset;
        std::uint64_t offset;
        std::uint64_t bytes;
    };
    // A frame's staging: chunks kept while frames keep staging, gone after a frame that staged nothing (the usual
    // state: new vertices come with a scene).
    struct ResidentStagingChunk {
        UploadBuffer memory;
        std::uint64_t size = 0;
    };
    struct ResidentStaging {
        std::vector<ResidentStagingChunk> chunks;
        std::size_t current = 0;
        std::uint64_t used = 0;
        bool staged = false;
    };
    static constexpr std::uint64_t kResidentStagingChunk = 4ull << 20u;
    std::array<ResidentStaging, kFramesInFlight> resident_staging_;
    Microsoft::WRL::ComPtr<ID3D12Resource> resident_upload_;  // only where local memory cannot be had
    std::byte* resident_upload_mapped_ = nullptr;
    ID3D12Resource* resident_local_ = nullptr;
    std::vector<ResidentCopy> resident_copies_;
    bool pre_list_failed_ = false;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> pre_list_;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, kFramesInFlight> pre_allocators_;
    std::array<bool, kFramesInFlight> pre_list_used_{};
    bool pre_list_recorded_ = false;

    // scene target.
    Microsoft::WRL::ComPtr<ID3D12Resource> scene_color_;
    Microsoft::WRL::ComPtr<ID3D12Resource> scene_depth_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsv_heap_;
    FrameMap frame_{};  // the bound scene target's map (E-08)
    D3D12_CPU_DESCRIPTOR_HANDLE scene_rtv_{};
    D3D12_CPU_DESCRIPTOR_HANDLE scene_dsv_{};
    // The inactive scene target target set (the output-resolution one between frames).
    // MSAA: an scene target's draws and clears go to these multisampled targets (the set's rtv and dsv name them); its color
    // and depth, which copies and probes read, are their resolves (resolve_scene).
    struct SceneMsaa {
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
    bool create_scene_msaa(SceneMsaa& msaa, ID3D12Resource* color, ID3D12Resource* depth, unsigned samples,
                         D3D12_CPU_DESCRIPTOR_HANDLE& rtv, D3D12_CPU_DESCRIPTOR_HANDLE& dsv);
    void write_msaa_depth_view(SceneMsaa& msaa);
    [[nodiscard]] ID3D12PipelineState* clear_pipeline(bool color);
    SceneMsaa scene_msaa_;
    mutable unsigned msaa_max_samples_ = 0u;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> depth_resolve_pso_;
    struct SceneTargetSet {
        Microsoft::WRL::ComPtr<ID3D12Resource> color;
        Microsoft::WRL::ComPtr<ID3D12Resource> depth;
        SceneMsaa msaa;
        FrameMap map{};
        bool hdr = false;   // colour kSceneHdrColorFormat (else kSceneColorFormat, or kTargetColorFormat for a target's)
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
        std::optional<DescriptorRing::Table> conv_table;
        D3D12_GPU_DESCRIPTOR_HANDLE conv_color_srv{};
        D3D12_GPU_DESCRIPTOR_HANDLE conv_depth_srv{};
    };
    SceneTargetSet alt_scene_;
    // The open target pass's set (begin_target_pass) while the scene target holds it, and each key's set between passes
    // ( its colour shared with its picture), dropped after kTargetIdleFrames frames unused. A picture is sampled
    // through its own view slot (TextureCache::allocate_view) reading the pass's channels.
    struct TargetSlot {
        SceneTargetSet set;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsv_heap;
        std::uint64_t used = 0;
        std::uint32_t width = 0, height = 0;  // frame units
    };
    // The pictures draws sample by key (target passes' and captures'), each through its own view slot
    // (TextureCache::allocate_view) reading its channels; a capture's `bytes` is its footprint at the key, over which a
    // later capture replaces it.
    struct Picture {
        std::uint32_t view = ~0u;
        gpu::TextureRef viewed;
        std::uint32_t width = 0, height = 0;  // frame units, the size draws sample it at
        std::uint8_t channels = 0;
        std::uint32_t bytes = 0;
    };
    std::unordered_map<std::uint64_t, Picture> pictures_;
    // `texture` becomes picture `key` (`width` x `height` game units, read through `channels`): true when draws
    // must look it up again (a new view, or a capture replacing others).
    bool publish_picture(std::uint64_t key, std::uint32_t width, std::uint32_t height, gpu::TextureRef texture,
                         std::uint8_t channels, std::uint32_t bytes, TextureCache& textures);
    bool publish_frame_pass(const FramePassRequest& params, gpu::TextureRef texture, TextureCache& textures);
    static constexpr std::uint64_t kTargetIdleFrames = 600u;
    std::unordered_map<std::uint64_t, TargetSlot> target_slots_;
    std::uint64_t target_key_ = 0;
    bool target_recording_ = false;  // replay_recording_ before the pass
    SceneTargetSet target_scene_;
    bool target_pass_active_ = false;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> alt_scene_rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> alt_scene_dsv_heap_;
    bool output_scene_active_ = false;
    std::unique_ptr<TemporalUpscaler> temporal_;
    // An FSR 3 context being created off the render thread (its pipelines take seconds on an
    // integrated GPU), for the sizes and mode recorded below.
    std::future<std::unique_ptr<TemporalUpscaler>> temporal_pending_;
    unsigned temporal_kind_ = 0;       // upscaler setting it was made for (2 FSR 3, 3 DLSS)
    unsigned temporal_quality_ = 0;
    std::int64_t temporal_last_upscale_us_ = 0;
    // A presented frame's Streamline markers have begun (begin_frame) and not yet ended (present).
    bool streamline_frame_open_ = false;
    // DLSS Frame Generation's HUD-less picture: the upscaled scene before the 2D layer (taken at
    // the output scene target switch), copied as the display copy copies the frame and presented as the
    // frame is, into a backbuffer-sized target.
    SceneTargetSet fg_hudless_scene_target_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> fg_hudless_rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> fg_hudless_dsv_heap_;
    DisplayTexture fg_hudless_display_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> fg_hudless_target_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> fg_hudless_target_rtv_heap_;
    bool fg_hudless_scene_ = false;              // this frame's scene was taken
    bool fg_hudless_taken_ = false;              // a picture's HUD-less copy is waiting
    std::uint32_t fg_hudless_slot_ = 0u;         // the picture it went with
    void take_fg_hudless_scene();
    void copy_fg_hudless_display(const FramePassRequest& params, const DisplayTexture& dest);
    void present_fg_hudless(const DisplayTexture& source, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor);
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
    FrameMap motion_target_map_{};
    // Interpolated redraw state (see begin_replay_record).
    struct ReplayCopy {
        ID3D12Resource* dest = nullptr;
        unsigned width = 0;
        unsigned height = 0;
        FramePassRequest params{};
        std::uint8_t format = 0;
        float geometry[4]{};  // src x, y, step x, y
        std::uint8_t mode = 0;  // 0 texture, 1 display copy, 2 output-scene target resample
    };
    struct ReplayClear {
        std::uint32_t rgba = 0;
        bool color = false, alpha = false, depth = false;
        unsigned x = 0, y = 0, w = 0, h = 0;
        std::uint8_t format = 0;
    };
    // RtComposite: the ambient occlusion of ray-tracer result slot `index`.
    enum class ReplayKind : std::uint8_t {
        Draw, BindScene, Clear, Copy, SwapSceneSets, ClearDepth, RtComposite, RtWater, SceneAa, SceneAo,
        HdrBegin, HdrEnd, Bloom, LayerBegin, RtMist
    };
    // RtWater's and RtMist's index: the light slot, kReplayWaterLayer when the effects layer was open.
    static constexpr std::uint32_t kReplayWaterLayer = 0x80000000u;
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
    bool ensure_subframe_resources(unsigned display_picture_width, unsigned display_picture_height);
    // The log being recorded; a completed log moves to play_* (swapped), so
    // the next game frame records while the previous one is interpolated.
    std::vector<ReplayOp> replay_ops_;
    std::vector<DrawCall> replay_draws_;
    std::vector<ReplayClear> replay_clears_;
    std::vector<ReplayCopy> replay_copies_;
    std::vector<ReplayBloom> replay_blooms_;
    // The display copy's clear runs after the copy closed the log; the next
    // log starts with it so sub-frames begin from the same scene target as the frame.
    ReplayClear log_start_clear_{};
    bool log_start_clear_valid_ = false;
    // Draws and texture copies between a log's display copy and the next log
    // (missing from both logs; expected 0).
    std::uint64_t log_gap_ops_ = 0;
    std::vector<ReplayOp> play_ops_;
    std::vector<DrawCall> play_draws_;
    std::vector<ReplayClear> play_clears_;
    std::vector<ReplayCopy> play_copies_;
    std::vector<ReplayBloom> play_blooms_;
    bool play_start_output_active_ = false;
    void publish_replay_log();
    // The log being interpolated (moved from play_* by start_replay_playback),
    // so a newer completed log can wait while sub-frames of this one run.
    std::vector<ReplayOp> run_ops_;
    std::vector<DrawCall> run_draws_;
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
    bool replay_screen_frame_ = false;
    unsigned replay_segment_ = 0;
    std::uint64_t replay_serial_ = 0;
    std::uint32_t replay_picture_slot_ = 0;
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
    // Sub-frames render into their own scene target so a later frame's partial scene target
    // (its chunks interleave with the sub-frames) stays intact.
    SceneTargetSet replay_scene_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> replay_scene_rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> replay_scene_dsv_heap_;
    // The wide scene's colour targets (begin_wide_scene), over the scene target's own depth: the live frames' and the
    // sub-frames'.
    SceneTargetSet hdr_scene_scene_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> hdr_scene_rtv_heap_;
    SceneTargetSet replay_hdr_scene_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> replay_hdr_rtv_heap_;
    // The live scene target is the wide set (swapped with the set that held it).
    bool scene_hdr_ = false;
    bool ensure_wide_set(SceneTargetSet& set, Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& rtv_heap);
    void record_hdr_begin();
    void record_hdr_end();
    SceneCamera camera_pass_;
    std::chrono::steady_clock::time_point camera_metered_{};
    // The output-resolution scene target of a sub-frame whose log switches to it.
    SceneTargetSet replay_output_scene_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> replay_output_scene_rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> replay_output_scene_dsv_heap_;
    // persistent_views: the conversion-source views live in the descriptor
    // ring's persistent prefix (else the caller writes them per use).
    // unordered_access: the colour target takes compute writes (the temporal
    // upscaler's output, as the live output scene target).
    bool create_scene_set(SceneTargetSet& set, Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& rtv_heap,
                        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& dsv_heap, const FrameMap& map,
                        bool persistent_views = true, bool unordered_access = false, unsigned samples = 1u,
                        bool hdr = false, unsigned width = kFrameUnits, unsigned height = kFrameUnits, bool target = false);
    void write_scene_set_views(SceneTargetSet& set, const DescriptorRing::Table& table);
    void swap_scene_with(SceneTargetSet& other);
    bool replay_playing_ = false;
    bool replay_start_output_active_ = false;
    unsigned replay_source_slot_ = 0;
    DisplayTexture subframe_display_{};
    // scene target-copy textures are shared with the live frames: a later game frame's
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
    std::vector<std::int32_t> run_ps_previous_;
    // Candidate pairs of the playing log (current offset, previous offset,
    // vertices, flags: bit 0 current / bit 1 previous in the resident buffer),
    // checked on the GPU before the sequence's first sub-frame draws: a
    // verdict per pair (vertices, positions unchanged, largest move, extent).
    // The last frame's start (the hitch log's intervals, begin to begin).
    std::chrono::steady_clock::time_point frame_begun_{};
    // The effects layer: its colour (what the effects added, as they blended into the scene target) and transmittance (what of
    // the scene target behind them they let through), render targets 2 and 3 of a layer draw; whether this scene's is open.
    Microsoft::WRL::ComPtr<ID3D12Resource> layer_color_;
    Microsoft::WRL::ComPtr<ID3D12Resource> layer_trans_;
    D3D12_CPU_DESCRIPTOR_HANDLE layer_color_rtv_{};
    D3D12_CPU_DESCRIPTOR_HANDLE layer_trans_rtv_{};
    bool layer_window_ = false;
    void record_layer_begin();
    bool reactive_scene_ = false;   // this scene's blended draws write the reactive mask (begin_motion_scene)
    [[nodiscard]] bool temporal_inputs_match(const TemporalUpscaleInputs& in) const;
    D3D12_GPU_VIRTUAL_ADDRESS resident_vertex_base_ = 0;
    std::uint64_t resident_vertex_bytes_ = 0;
    // Sequence replay (begin_replay_sequence) and its evaluation pass. The
    // replay list is recorded once per sequence and executed by every
    // sub-frame between a small evaluation list and a small present list.
    bool ensure_interp_eval();
    bool ensure_interp_output(std::uint64_t bytes);
    void record_interp_eval(const SubframeTime& time, const Affine34& camera_t, const Affine34& camera_prev);
    void transition_interp_output(D3D12_RESOURCE_STATES state);
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
        bool redirect = false;
        std::uint64_t guarded = 0;
        unsigned alloc_segment = 0;     // ring segment of its descriptor tables
    };
    static constexpr unsigned kReplayLists = 4;
    std::array<ReplayList, kReplayLists> replay_lists_{};
    bool record_replay_list(ReplayList& entry);
    void record_subframe_upscale(const ReplayList& entry, const SubframeTime& time);
    struct ReplayMotionTargets {
        Microsoft::WRL::ComPtr<ID3D12Resource> target;
        Microsoft::WRL::ComPtr<ID3D12Resource> reactive;
        Microsoft::WRL::ComPtr<ID3D12Resource> layer_color;
        Microsoft::WRL::ComPtr<ID3D12Resource> layer_trans;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        D3D12_CPU_DESCRIPTOR_HANDLE reactive_rtv{};
        D3D12_CPU_DESCRIPTOR_HANDLE layer_color_rtv{};
        D3D12_CPU_DESCRIPTOR_HANDLE layer_trans_rtv{};
        FrameMap map{};
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
        FrameMap scene_map{};
        FrameMap output_map{};
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
        std::uint64_t serial = 0;
        FrameMap output_map{};
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
        std::array<ID3D12Resource*, kReplayTableSlots> copies{};  // scene target-copy texture per slot
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
    // Render target 1 bound next to the scene target (DrawCall::aux_target values).
    std::uint8_t bound_rt_aux_ = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> reactive_target_;
    D3D12_CPU_DESCRIPTOR_HANDLE reactive_rtv_{};
    // Matrix ring segments: kFramesInFlight, plus two in temporal mode so the
    // previous render frame's palettes outlive the frame that reads them.
    unsigned matrix_segments_ = kFramesInFlight;
    unsigned matrix_segment_ = 0;
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
    Microsoft::WRL::ComPtr<ID3D12Resource> scene_carry_color_;
    // The carried colour is a handed-over picture, kept as a shader resource (its display picture reads it too).
    bool scene_carry_color_readable_ = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> scene_carry_depth_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> scene_carry_depth_pso_;
    // Presentation image quality; present() keeps the plain blit while it is
    // not ready.
    PostProcessor post_process_;
    AmbientOcclusion ambient_occlusion_;
    GameBloom game_bloom_;
    RtView scene_ao_view_{};
    std::vector<ao::ObjectOccluder> scene_ao_objects_;
    // scene target-copy destination textures (render targets) are placed in shared
    // heaps rather than committed one by one (gpu_placed_textures.h).
    PlacedTextureAllocator frame_pass_heap_{
        D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES, 128ull << 20};

    // Settings overlay: the game's window-frame corner texture (read from
    // files/LayoutData/FileSelect.arc, galaxy/render/game_asset_extract.h), drawn at the
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
    UploadRing matrix_ring_;        // palette snapshots (structured)
    DescriptorRing srv_ring_;

    unsigned frame_slot_ = 0;
    std::uint64_t frame_index_ = 0;
    std::uint64_t next_fence_value_ = 0;
    std::uint64_t last_latency_wait_us_ = 0;
    std::uint64_t last_frame_slot_wait_us_ = 0;
    std::uint64_t last_timestamp_read_us_ = 0;
    std::uint64_t last_begin_reset_us_ = 0;

    // Redundant-state elision for draw(). Every one of these is re-armed by
    // invalidate_draw_bindings(), which begin_frame() calls after the upload rings
    // for the new frame slot have been rewound and before any list is recorded.
    // The vertex view is part of the set: its GPU VA comes from the per-frame
    // vertex ring, so a stale entry can never survive that rewind.
    ID3D12PipelineState* bound_pipeline_ = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS bound_vs_constants_ = 0;
    D3D12_GPU_VIRTUAL_ADDRESS bound_ps_constants_ = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE bound_texture_table_{};
    D3D12_GPU_DESCRIPTOR_HANDLE bound_sampler_table_{};
    D3D12_GPU_VIRTUAL_ADDRESS bound_matrix_rows_ = 0;
    D3D12_PRIMITIVE_TOPOLOGY bound_topology_ =
        D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    D3D12_VERTEX_BUFFER_VIEW bound_vertex_view_{};
    bool bound_vertex_view_valid_ = false;
    D3D12_VERTEX_BUFFER_VIEW bound_extra_view_{};
    bool bound_extra_view_valid_ = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> zero_vertices_;
    // The index view is a CPU-side 20-byte descriptor whose BufferLocation comes
    // from the per-frame index ring, so like the vertex view it must be re-armed
    // by invalidate_draw_bindings() after the ring for the new slot is rewound.
    D3D12_INDEX_BUFFER_VIEW bound_index_view_{};
    bool bound_index_view_valid_ = false;
    D3D12_RECT bound_scissor_{};
    // Successful nonempty input that produced the currently bound scissor.
    D3D12_RECT bound_scissor_input_{};
    FrameMap bound_scissor_input_map_{};
    bool bound_scissor_input_valid_ = false;
    D3D12_VIEWPORT bound_viewport_{};
    bool bound_scissor_valid_ = false;
    bool bound_viewport_valid_ = false;

    // Debug scene target readback (debug_copy_scene_to_readback / debug_log_readback).
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_buf_;
    bool readback_pending_ = false;
    std::uint64_t readback_fence_value_ = 0;
    std::string next_scene_capture_path_;
    std::string readback_capture_path_;
    Microsoft::WRL::ComPtr<ID3D12Resource> backbuffer_readback_buf_;
    bool backbuffer_readback_pending_ = false;
    std::uint64_t backbuffer_readback_fence_value_ = 0;
    UINT backbuffer_readback_width_ = 0;
    UINT backbuffer_readback_height_ = 0;
    UINT backbuffer_readback_row_pitch_ = 0;
    std::string next_backbuffer_capture_path_;
    std::string backbuffer_readback_capture_path_;
    Microsoft::WRL::ComPtr<ID3D12Resource> selected_display_readback_buf_;
    Microsoft::WRL::ComPtr<ID3D12Resource> selected_display_readback_source_;
    bool selected_display_readback_pending_ = false;
    bool selected_display_readback_ten_bit_ = false;
    std::uint64_t selected_display_readback_fence_ = 0;
    UINT selected_display_readback_width_ = 0;
    UINT selected_display_readback_height_ = 0;
    UINT selected_display_readback_pitch_ = 0;
    std::string selected_display_readback_path_;
    struct DebugPpmWriter {
        std::string tag;
        std::string path;
        std::future<void> completion;
    };
    std::vector<DebugPpmWriter> debug_ppm_writers_;

    // Presentation telemetry: successful DXGI submissions, distinct display picture
    // updates, frame-latency waits, Present blocking, and GPU timestamps.
    LARGE_INTEGER qpc_frequency_{};
    std::int64_t telemetry_window_start_qpc_ = 0;
    std::int64_t last_present_qpc_ = 0;
    std::int64_t swap_present_qpc_ = 0;   // every present's, telemetry or not (last_present_qpc)
    std::uint64_t telemetry_first_serial_start_ = 0;
    std::uint64_t telemetry_display_copy_start_ = 0;
    PresentPacer present_pacer_;
    bool has_presented_swap_chain_ = false;
    bool window_shown_ = false;
    std::uint64_t telemetry_present_count_ = 0;
    std::uint64_t telemetry_interval_count_ = 0;
    std::uint64_t telemetry_adjacent_serial_transition_count_ = 0;
    std::uint64_t last_presented_display_serial_ = ~std::uint64_t{0};
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
    std::uint64_t telemetry_no_display_present_count_ = 0;
    std::uint64_t telemetry_backbuffer_clear_count_ = 0;
    // Refresh rate of the swap chain's output (EnumDisplaySettings) and, per
    // telemetry window, how many refreshes each displayed frame stayed on
    // screen (DXGI frame statistics; index 4 counts 4 or more).
    double display_refresh_hz_ = 0.0;
    std::uint64_t display_refresh_presents_ = 0;
    UINT last_stats_present_count_ = 0;
    UINT last_stats_present_refresh_count_ = 0;
    std::array<std::uint64_t, 5> telemetry_shown_refreshes_{};

    // scene target->texture copy destinations, keyed by (game dest address, copy format,
    // depth-source). SMG reuses one address with different copy formats, and each format
    // needs its own converted texture. The resource is reused across copies and recreated
    // only on size change; the TextureCache alias is refreshed per copy (cheap when unchanged).
    struct FramePassTarget {
        Microsoft::WRL::ComPtr<ID3D12Resource> texture;
        UINT width = 0;
        UINT height = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        bool rtv_valid = false;
    };
    // One texture per destination key, at the copy's scaled extent in kSceneColorFormat
    // (a full frame copy). Bounded in
    // bytes, not entries, since an entry's cost varies with the copy extent and the
    // internal scale; sizes are the driver's GetResourceAllocationInfo. The 192 MiB budget
    // is well above a scene's working set (a few pictures across a couple of formats plus one
    // bloom workspace).
    std::unordered_map<std::uint64_t, FramePassTarget> frame_pass_targets_;
    // Render-owner-only aggregate work counts, reported once at shutdown.
    // Payload bytes are copied pixels, not a device allocation-size estimate.
    std::uint64_t frame_pass_requests_ = 0;
    std::uint64_t frame_passes_drawn_ = 0;
    using RetiredResourceList =
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>;
    std::array<RetiredResourceList, kFramesInFlight>
        retired_frame_pass_targets_;
    // Interpolation: copy textures retired during a game-frame ring segment.
    std::array<RetiredResourceList, kInterpSegments> retired_segment_textures_;

    // Conversion blit pipeline (its root signature is a module static beside the
    // present blit's) and CPU-only descriptors for conversion destinations. RTV
    // descriptors are consumed at record time, so every copy rebinds the same
    // CPU-only scratch slot. scene target source SRVs occupy an immutable prefix of the
    // active shader-visible heap.
    Microsoft::WRL::ComPtr<ID3D12PipelineState> conv_pipeline_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> probe_pipeline_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> display_filter_pipeline_;  // 1:1 display copies without gamma
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> conv_rtv_heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE conv_scratch_rtv_{};
    detail::ConversionRtvSlot conv_scratch_rtv_slot_;
    std::optional<DescriptorRing::Table> conv_scene_source_table_;
    D3D12_GPU_DESCRIPTOR_HANDLE conv_scene_color_srv_{};
    D3D12_GPU_DESCRIPTOR_HANDLE conv_scene_depth_srv_{};
    // The masked scene target clears (color / alpha only): shaders and pipelines, made at first use.
    UtilityShaderBytes clear_vs_;
    UtilityShaderBytes clear_ps_;
    // Per scene target variant (scene_variant: samples, then HDR).
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 8> clear_rgb_pipelines_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 8> clear_alpha_pipelines_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> motion_debug_root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> motion_debug_pipeline_;

    // Screen probes: one pixel of the target per point, each slot's readback fenced with its frame.
    Microsoft::WRL::ComPtr<ID3D12Resource> probe_target_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> probe_rtv_heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE probe_rtv_{};
    struct ScreenProbeSlot {
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        const std::uint8_t* rgba = nullptr;
        std::uint64_t fence = 0;
    };
    std::array<ScreenProbeSlot, kScreenProbeSlots> probe_slots_{};
    // ScreenProbeSlot::fence of probes recorded in the open frame.
    static constexpr std::uint64_t kProbesInFrame = ~std::uint64_t{0};
    static const std::uint8_t* pin_readback_mapping(ID3D12Resource* readback);

};

// The renderer RenderCore builds against (renderer_vk.h defines it for Vulkan).
using Renderer = RendererD3D12;

}  // namespace galaxy::render
