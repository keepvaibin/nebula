#pragma once

// RendererD3D12: device, swap chain, scene target/display picture render targets, per-frame upload
// rings, draw submission, and presentation.
//
// Frame pipelining: kFramesInFlight frames are recorded ahead of the GPU;
// begin_frame() waits only on the fence of frame N - kFramesInFlight. A full
// CPU/GPU sync happens only in shutdown() and on resize.
//
// All draws target the offscreen scene target (colour R10G10B10A2 + depth
// D32_FLOAT_S8X24_UINT, reversed Z). present()
// aspect-fits a display picture into the swap-chain backbuffer.
//
// begin_frame() is the sole PSO-publication point: it drains the
// PipelineCache completion queue before any command-list recording starts.

#include "galaxy/windows_api.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>

#include "galaxy/render/fsr_frame_generation.h"
#include "galaxy/render/utility_shaders.h"
#include "galaxy/render/frame_pass.h"
#include "galaxy/render/temporal_upscaler.h"
#include "galaxy/render/gpu_upload_ring.h"
#include "galaxy/render/post_process.h"
#include "galaxy/render/display_modes.h"
#include "galaxy/render/ambient_occlusion.h"
#include "galaxy/render/scene_camera.h"
#include "galaxy/render/game_bloom.h"
#include "galaxy/render/rt_types.h"
#include "galaxy/render/rt/rt_backend.h"
#include "galaxy/render/gpu_placed_textures.h"
#include "galaxy/render/texture_sampler.h"
#include "galaxy/render/gpu_types.h"
#include "galaxy/render/renderer_common.h"
#include "render/core/gpu_encoder.h"
#include "render/core/draw_record.h"
#include "galaxy/render/render_config.h"
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

class TextureStore;
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
// Scene depth: 32-bit float with stencil (the shadow volumes); reversed Z keeps its precision across the range.
inline constexpr DXGI_FORMAT kSceneDepthResourceFormat = DXGI_FORMAT_R32G8X24_TYPELESS;
inline constexpr DXGI_FORMAT kSceneDepthFormat = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
inline constexpr DXGI_FORMAT kSceneDepthSrvFormat = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
inline constexpr double kSceneDepthBytes = 8.0;  // per pixel and sample
inline constexpr DXGI_FORMAT kBackbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

// Builds the graphics root signature used by RendererD3D12. Root
// CBV b0 is visible to both VS and GS because line and point expansion
// reads its scale-aware raster constants in the geometry stage.
[[nodiscard]] HRESULT create_draw_root_signature(
    ID3D12Device* device,
    ID3D12RootSignature** root_signature) noexcept;

[[nodiscard]] D3D12_SAMPLER_DESC build_sampler_desc_from_key(
    const SamplerKey& key,
    const RenderConfig& cfg) noexcept;

// The one shader-visible CBV/SRV/UAV heap, bound once per command list:
//   [texture region][persistent descriptors][one segment per frame in flight]
// The texture region holds the draws' texture views (slot n = TextureStore index n, the last slot the null view) and is the
// only part that grows: resize_textures builds a larger heap and the others move with it. Everything else is written into a
// CPU-only shadow heap ([persistent][segments][null block]) and copied to the heap from there: a descriptor is never copied
// out of the shader-visible heap, and a replaced heap is rebuilt from the shadow. A persistent descriptor is named by its
// index in the persistent region, so it keeps its name when the heap is replaced; gpu_at turns it into a handle of the
// current heap. A transient table's handle belongs to the heap that was current when it was allocated.
class DescriptorRing {
public:
    // The most descriptors one shader-visible CBV/SRV/UAV heap holds on every resource binding tier. Tiers 1 and 2 stop there; tier 3
    // names no limit (the renderer passes a larger heap_limit and the heap doubles until the device refuses one).
    static constexpr unsigned kMaxHeapDescriptors = 1000000u;

    // Caller must have retired all GPU references to this heap.
    void reset() noexcept;
    // `texture_descriptors` slots first (the last one the caller's null view), then the persistent descriptors, then one
    // segment of `descriptors_per_frame` per frame in flight.
    // `heap_limit`: the most descriptors the device's shader-visible heap takes (see kMaxHeapDescriptors).
    bool initialize(
        ID3D12Device* device,
        D3D12_DESCRIPTOR_HEAP_TYPE type,
        unsigned descriptors_per_frame,
        unsigned frames_in_flight,
        unsigned persistent_descriptors = 0,
        unsigned texture_descriptors = 0,
        unsigned heap_limit = kMaxHeapDescriptors);
    [[nodiscard]] unsigned heap_limit() const noexcept { return heap_limit_; }
    // The sampler heap that goes with this heap in bind().
    void set_sampler_heap(ID3D12DescriptorHeap* heap) noexcept { sampler_heap_ = heap; }
    void begin_frame(unsigned frame_slot);

    // A table of this frame's segment: `cpu` is its place in the shadow heap (write views there), `gpu` its place in the
    // heap. Unwritten slots hold the null SRV. flush() copies the frame's tables into the heap.
    struct Table {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    };
    // Reserves `count` consecutive slots; the caller writes views at `cpu`.
    Table allocate(unsigned count);
    // Copies this frame's tables from the shadow heap into the heap (before the frame's command list is submitted).
    void flush();

    // A table of the persistent region: written at `cpu`, then commit() copies it into the heap. Never rewound; the
    // descriptors must stay unchanged while the GPU reads them.
    struct Persistent {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
        std::uint32_t index = 0u;  // in the persistent region
    };
    static constexpr std::uint32_t kNoPersistent = ~0u;
    // `count` consecutive persistent descriptors. When the region is full it doubles: a new heap and a new persistent shadow
    // replace the old (the descriptors keep their indices; the frame segments move up, and this frame's tables so far are
    // copied into the new heap). Hold the index, not `cpu`: persistent_cpu(index) is the current place in the shadow. The replaced
    // heap goes to take_replaced().
    Persistent allocate_persistent(unsigned count);
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE persistent_cpu(std::uint32_t index) const noexcept;
    // Called when a frame segment's growth (allocate) has put a new heap in place of the current one: its texture
    // region is empty, and whoever fills that region fills it again.
    void on_frames_grown(void (*fn)(void*), void* owner) noexcept {
        replaced_fn_ = fn;
        replaced_owner_ = owner;
    }
    // The heaps growth replaced, for the renderer to free once the GPU is past the commands that may read them.
    [[nodiscard]] std::vector<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>> take_replaced() noexcept { return std::move(replaced_); }
    // The most a frame's segment and the persistent region have held (diagnostics). A segment that a frame fills doubles, as the
    // persistent region does (grow_frames); its first size is kSrvPerFrame.
    [[nodiscard]] unsigned frame_high_water() const noexcept { return frame_high_water_; }
    [[nodiscard]] unsigned persistent_used() const noexcept { return persistent_cursor_; }
    [[nodiscard]] unsigned persistent_capacity() const noexcept { return persistent_descriptors_; }
    // Copies `count` descriptors from `table` into the heap.
    void commit(const Persistent& table, unsigned count);
    // The handle of persistent descriptor `index` in the current heap.
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE gpu_at(std::uint32_t index) const noexcept;

    [[nodiscard]] ID3D12DescriptorHeap* heap() const { return heap_.Get(); }
    // The bytes of the shader-visible heap and of the heaps growth replaced that are not yet freed (video memory).
    [[nodiscard]] std::uint64_t heap_bytes() const noexcept;
    // The texture region.
    [[nodiscard]] std::uint32_t texture_slots() const noexcept { return texture_descriptors_; }
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE texture_cpu(std::uint32_t slot) const noexcept;
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE texture_gpu_start() const noexcept;
    // Descriptors outside the texture region.
    [[nodiscard]] unsigned ring_descriptors() const noexcept {
        return persistent_descriptors_ + descriptors_per_frame_ * frames_in_flight_;
    }
    // A heap with `texture_descriptors` texture slots takes the place of the current one: the persistent descriptors and the
    // frame's tables so far are copied into it, and the texture slots are the caller's to fill (the earlier slots move with
    // the textures' own CPU views). Returns the replaced heap, which commands already recorded still read: free it once the
    // GPU is past them. Throws past kMaxHeapDescriptors; an empty result when the count is not larger.
    [[nodiscard]] Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> resize_textures(unsigned texture_descriptors);

    // Sets the heap (and the sampler heap given to set_sampler_heap) on `list` unless this list already has them. A new
    // frame or a new heap asks again; forget_bound() after code that sets heaps of its own (a library's dispatch).
    void bind(ID3D12GraphicsCommandList* list);
    void forget_bound() noexcept { bound_list_ = nullptr; }

private:
    void grow_persistent(unsigned persistent);
    void grow_frames(unsigned per_frame);
    void (*replaced_fn_)(void*) = nullptr;
    void* replaced_owner_ = nullptr;
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE shadow_at(unsigned index) const noexcept;
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE heap_cpu_at(unsigned index) const noexcept;  // a ring index, past the textures
    void fill_null(unsigned index, unsigned count);
    void copy_in(unsigned index, unsigned count);  // shadow -> heap, both by ring index
    ID3D12Device* device_ = nullptr;
    D3D12_DESCRIPTOR_HEAP_TYPE type_{};
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> shadow_;             // CPU only: [frame segments][null block]
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> persistent_shadow_;  // CPU only: [persistent]; replaced when it grows
    std::vector<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>> replaced_;
    unsigned heap_limit_ = kMaxHeapDescriptors;
    unsigned frame_slot_ = 0;
    unsigned frame_high_water_ = 0;
    ID3D12DescriptorHeap* sampler_heap_ = nullptr;
    ID3D12GraphicsCommandList* bound_list_ = nullptr;
    ID3D12DescriptorHeap* bound_heap_ = nullptr;
    bool frame_ready_ = false;
    unsigned texture_descriptors_ = 0;
    unsigned persistent_descriptors_ = 0;
    unsigned persistent_cursor_ = 0;
    unsigned descriptors_per_frame_ = 0;
    unsigned frame_base_ = 0;  // ring index of this frame's segment
    unsigned cursor_ = 0;
    unsigned increment_ = 0;
    unsigned frames_in_flight_ = 0;
};

class RendererD3D12 final {
public:
    // Creates the Win32 window, device, queue, swap chain, scene target targets at
    // at the frame's size, the shared root signature, the
    // present-blit pipeline, and all rings.
    bool initialize(int window_width, int window_height, const FrameMap& frame);
    [[nodiscard]] std::string startup_failure() const { return init_error_; }
    std::string init_error_;  // why initialize failed
    // The window stays; everything else is made again. False: no device yet.
    // What outlives this renderer when a lost card is left behind (RenderCore::restart_renderer).
    struct Kept {
        HWND window = nullptr;
        LUID card{};
#if defined(NEBULA_DEV)
        Microsoft::WRL::ComPtr<ID3D12DebugDevice> debug_device;
#endif
    };
#if defined(NEBULA_DEV)
    static void list_live_objects(Kept& kept);
#endif
    void set_window_owner(WindowOwner& owner) noexcept { window_owner_ = &owner; }
    [[nodiscard]] Kept take_apart_lost_device();
    void keep(const Kept& kept, bool same_card) noexcept {
        window_ = kept.window;
        card_ = kept.card;
        same_card_only_ = same_card;
    }
    void shutdown();                // full GPU drain, then release
    // A persistently mapped upload buffer; empty when it cannot be created.
    [[nodiscard]] UploadBuffer create_upload_buffer(std::size_t bytes, const char* name);
    // A texture's place in the draws' texture array: its view's index; the null view's place for an empty handle.
    [[nodiscard]] std::uint32_t texture_slot(const TextureHandle& handle) const noexcept;
    // A sampler's place in the sampler heap, made the first time it is asked for (until the heap starts again:
    // sampler_generation changes).
    [[nodiscard]] std::uint32_t sampler_slot(const SamplerKey& key);
    // `count` pairs {texture slot, sampler slot} into this frame's texture lists: the buffer they went to and the first
    // pair's place in it (DrawRoot::table).
    void write_texture_list(const std::uint32_t* pairs, std::uint32_t count, gpu::Address& lists, std::uint32_t& first);
    // The texture cache records its uploads into this frame's command list.
    void attach_texture_uploads(TextureStore& cache);
    // The adapter's local video memory in use by this process and the OS's budget for it (DXGI QueryVideoMemoryInfo), read at
    // a frame boundary. False: not known.
    [[nodiscard]] bool video_memory(std::uint64_t& usage, std::uint64_t& budget) const noexcept;
    // The upload rings' sizes and the most one frame took (--diagnostics memory).
    void trace_rings() const;
    // What the render core holds that the breakdown line (log_video_memory) adds to the renderer's own figures; bytes unless noted.
    struct VideoMemoryOutside {
        std::uint64_t texture_heap_bytes = 0u, texture_used_bytes = 0u, texture_heaps = 0u, texture_committed = 0u;
        std::uint64_t staging_bytes = 0u;                       // TextureStore's staging chunks
        std::uint64_t mesh_page_bytes = 0u, mesh_live_bytes = 0u, slot_bytes = 0u;
        std::uint64_t mesh_local_bytes = 0u;                    // of mesh_page_bytes, in video memory
        std::uint32_t mesh_pages = 0u, mesh_empty_pages = 0u, slots = 0u;
        std::uint64_t picture_bytes = 0u;                       // DisplayPictures' textures
        std::uint32_t picture_count = 0u;
        std::uint64_t pipelines = 0u;                           // pipelines made
    };
    // The 600-frame `[vram] breakdown` line: the process's local video memory as the OS reads it, then the renderer's own
    // accounting of each kind (resource allocation sizes and heap sizes, not estimates) and what is left. Render thread.
    // `usage` is the OS figure.
    void log_video_memory(const VideoMemoryOutside& outside);
    // The size of `resource`'s allocation (GetResourceAllocationInfo); zero for none.
    [[nodiscard]] std::uint64_t allocation_bytes(ID3D12Resource* resource) const;
    void pump_messages();           // no-op: window_owner_ pumps the window's messages
    [[nodiscard]] bool quit_requested() const noexcept;
    // The last present said the whole window is covered (DXGI_STATUS_OCCLUDED); probe_occlusion asks again without drawing
    // (a test present) and clears it when the window shows. Render thread for the probe, any thread for the flag.
    [[nodiscard]] bool occluded() const noexcept { return occluded_.load(std::memory_order_relaxed); }
    void probe_occlusion();

    // Advances the frame ring: waits on the fence of frame N-kFramesInFlight
    // only, resets that slot's command allocator, opens the command list,
    // and rewinds the slot's ring segments. Swap-chain latency is waited in
    // present(), after scene target work is recorded but before the backbuffer is
    // selected, so gameplay work is not paced by DWM too early.
    // Test only (GALAXY_TEST_DEVICE_LOST): the device removed, as a driver reset leaves it.
    void lose_device();
    void begin_frame();
    // Called after the queue signal, before pacing and Present; the context is not retained.
    using SubmissionCallback = void (*)(void* context, std::uint64_t fence_value);
    // Closes, submits and signals the frame's fence. Presenting (optional) comes after
    // the signal so display pacing cannot delay command-allocator and upload-ring reuse.
    std::uint64_t end_frame(
        bool present_swap_chain = true,
        SubmissionCallback on_submitted = nullptr,
        void* submission_context = nullptr,
        bool generated = false);
    [[nodiscard]] std::uint64_t completed_fence_value() const;
    [[nodiscard]] std::uint64_t submitted_fence_value() const noexcept { return next_fence_value_; }
    // GPU time by pass (render_api.h GpuPassTime): any thread. Off: no query is recorded or read.
    void set_gpu_timing(bool on) noexcept { gpu_timing_wanted_.store(on, std::memory_order_relaxed); }
    [[nodiscard]] bool gpu_timing_on() const noexcept { return gpu_timing_wanted_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::size_t gpu_pass_times(GpuPassTime* out, std::size_t capacity) const noexcept;
    // The latest scene's jitter: its index in the sequence and the sequence's length (0 phases: no temporal scene yet).
    void scene_jitter_info(unsigned& index, unsigned& phases) const noexcept {
        index = scene_jitter_index_;
        phases = scene_jitter_phases_;
    }
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
    [[nodiscard]] bool begin_output_scene(bool resolve_msaa_only = false);
    // A pass into texture target `key` (E-07): its target, `width` x `height` game units at the scene target's scale, takes
    // the scene target's place until end_target_pass, so the pass's draws and clears go to it. Each key keeps its target
    // between passes (made again only when its size or the scene target's scale changes); the pass is drawn once a
    // game frame. False when the target cannot be made or a pass is open.
    [[nodiscard]] bool begin_target_pass(std::uint64_t key, std::uint32_t width, std::uint32_t height);
    // Ends it: the scene target comes back and the target's picture is what draws sampling `key` at that size read (at full
    // precision, in any format, through `channels`: NebulaTargetChannels).
    void end_target_pass(std::uint8_t channels, TextureStore& textures);
    // The picture of target `key` when it is `width` x `height`: its texture and view.
    [[nodiscard]] bool target_picture(std::uint64_t key, TextureHandle& out) const;
    [[nodiscard]] bool target_pass_active() const noexcept { return target_pass_active_; }
    void end_output_scene();
    // The scene's anti-aliasing (post_process.h) in the render scene target at the end of the 3D scene,
    // before the 2D layer and begin_output_scene. A temporal upscaler antialiases the scene itself.
    void record_scene_aa();
    // The 3D scene in a wide target while `wanted` (the enhanced lighting), its camera at the end (scene_camera.h).
    bool begin_wide_scene(bool wanted, const SceneClearValues* initial_clear = nullptr);
    void end_wide_scene();
    // Ambient occlusion (ambient_occlusion.h) of the scene drawn so far, from the view `set_scene_ao_view` gave:
    // after the opaque phases, before the translucent ones.
    [[nodiscard]] bool ambient_occlusion_ready() const noexcept { return ambient_occlusion_.ready(); }
    void set_scene_ao_view(const RtView& view) noexcept { scene_ao_view_ = view; }
    // The scene's models (ambient_occlusion_objects.h).
    void set_scene_ao_objects(std::vector<ao::ObjectOccluder>& objects) noexcept { scene_ao_objects_.swap(objects); }
    void record_scene_ao();
    // What the tracer records through (rt_backend.h Backend::begin: the frame's commands, no pass open, and where a
    // pass's views go), timed as the ray tracing pass until rt_commands_end.
    struct RtCommands {
        void* commands = nullptr;
        void* descriptors = nullptr;
    };
    [[nodiscard]] RtCommands rt_commands_begin();
    void rt_commands_end();
    // The scene's depth as a pass reads it, until rt_depth_end, with its size and the format an image must have to be
    // copied over the scene's colour (rt_copy_to_scene). False when no pass can read it now (a multisampled scene).
    [[nodiscard]] bool rt_depth_begin(rt::TextureView& depth, unsigned& width, unsigned& height, rt::ImageFormat& color);
    void rt_depth_end();
    // An image of the tracer's (Backend::image_to_copy) over the scene's colour: the tracer's views.
    void rt_copy_to_scene(void* image);
    // The game's bloom (game_bloom.h): `kind` a NEBULA_PASS_BLOOM_*, `a` and `b` its values.
    [[nodiscard]] bool game_bloom_ready() const noexcept { return game_bloom_.ready(); }
    void game_bloom(std::uint32_t kind, std::uint32_t a, std::uint32_t b);
    [[nodiscard]] bool output_scene_active() const noexcept { return output_scene_active_; }
    // Whether the targets for `scale` fit the adapter's video-memory budget
    // next to everything already resident.
    [[nodiscard]] bool scene_fits_memory(const FrameMap& map) const;

    // A temporal upscaler (FSR 3 or DLSS) is selected and available.
    [[nodiscard]] bool temporal_mode() const;
    // Every upscaler here reads the reactive mask, so the scene's draws keep their own aux targets.
    [[nodiscard]] bool motion_pass_only() const noexcept { return false; }
    // The effects layer's targets (begin_layer_window).
    [[nodiscard]] bool has_layer_targets() const noexcept { return true; }
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
    // RendererVk's: a clear here opens no render pass to keep.
    [[nodiscard]] bool begin_cleared_motion_scene(std::uint32_t clear_rgba) noexcept {
        (void)clear_rgba;
        return false;
    }
    [[nodiscard]] bool defer_scene_depth_clear(std::uint32_t clear_rgba) noexcept {
        (void)clear_rgba;
        return false;
    }
    // The game's effects begin (the scene's ShadowSurface draw type, after its translucent lists) in a scene whose
    // blended draws write the reactive mask: the effects layer opens (cleared: nothing added, everything let through)
    // and its draws record what they blend over the scene target until the water's composite (kRenderStateLayerTarget).
    void begin_layer_window();
    [[nodiscard]] bool layer_window() const noexcept { return layer_window_; }
    // GALAXY_MOTION_DEBUG: at the scene's end, replaces the scene target color with
    // the motion field (grey = still; red/green = +x/+y towards the previous
    // position, 8 pixels to full scale).
    // The temporal history does not match the picture (scene cut).
    void reset_temporal_history() noexcept { temporal_history_valid_ = false; }

    // When the swap chain last took a present (QPC; 0 none).
    [[nodiscard]] std::int64_t last_present_qpc() const noexcept { return swap_present_qpc_; }
    [[nodiscard]] std::uint64_t frame_index() const noexcept { return frame_index_; }

// The next present is a game frame shown without frames between while interpolating (a movie, no scene):
    // a whole game frame (present_sync_interval).
    // The next present is a game frame shown without generated frames while interpolating (a movie, no scene): it holds for
    // a whole game frame (present_sync_interval).
    void set_present_game_frame(bool game_frame) noexcept { present_game_frame_ = game_frame; }
// Interpolation mode: a present rate above 60 (with a temporal upscaler, while suspend_temporal_interpolation
    // has not stopped it). Lists walk at the times the display wants.
    // Interpolation mode: a present rate above 60 (with a temporal upscaler,
    // while suspend_temporal_interpolation has not stopped it). Rings then keep
    // one segment per game frame (advanced at the first frame after a display
    // copy) and the log spans every render chunk of the game frame up to its
    // display copy.
    [[nodiscard]] bool interpolation_mode() const;
// Every presented frame is upscaled while interpolating with a temporal upscaler.
    [[nodiscard]] bool temporal_interpolation() const { return interpolation_mode() && temporal_mode(); }
    // Swap-chain presents per second over the last second (any source).
    [[nodiscard]] double measured_present_hz() const noexcept {
        return measured_present_hz_.load(std::memory_order_relaxed);
    }
    // Smoothed GPU time of one generated frame (0 until measured), for the budget.
    [[nodiscard]] double generated_gpu_ema_ms() const noexcept { return generated_gpu_ema_ms_; }
    [[nodiscard]] double interpolation_gpu_cost_ms() const noexcept {
        return temporal_interpolation() ? generated_gpu_ema_ms_ : native_scene_gpu_ema_ms_;
    }
    // The GPU cannot afford a generated frame per presented frame: the game frames take
    // the temporal upscaler (60 presents) until the display or upscaler
    // settings change.
    void suspend_temporal_interpolation(double ms_per_frame);
    void reset_generated_gpu_ema() noexcept { generated_gpu_ema_ms_ = 0.0; }
    void take_generated_gpu_ms(double& total_ms, std::uint64_t& samples) noexcept {
        total_ms = generated_gpu_ms_;
        samples = generated_gpu_samples_;
        generated_gpu_ms_ = 0.0;
        generated_gpu_samples_ = 0u;
    }
// The scene camera's clip planes and vertical field of view (radians).
    // Generated frame draws drawn with blended vertices since the last call.
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

// What the walker draws through. Aux targets beside the scene target: 0 none, 1 motion, 2 reactive, 3 reactive and layer.
    [[nodiscard]] gpu::Encoder& encoder() noexcept { return encoder_; }
    void set_draw_aux(unsigned aux);
    void note_scene_draw(std::uint32_t state) noexcept {
        scene_drawn_ = true;
        if (scene_msaa_.samples > 1u) {
            constexpr std::uint32_t depth_write = (1u << 10u) | (1u << 14u);
            scene_msaa_.depth_dirty |= (state & depth_write) == depth_write;
            scene_msaa_.color_dirty |= (state & ((1u << 17u) | (1u << 18u) | (1u << 19u))) != 0u;
        }
    }
    // A buffer of `bytes` zeros, for streams a mesh lacks.
    [[nodiscard]] gpu::Address zero_stream(std::uint32_t bytes);
    // The frame so far is the frame's picture now: `fresh` (or a new target) takes its place.
    [[nodiscard]] gpu::TextureRef take_frame_picture(gpu::TextureRef fresh);
    // What presents with `dest` when frame generation wants the scene without its 2D layer.
    void copy_fg_hudless_display(const DisplayTexture& dest);
    // A target whose id is freed: every picture it has goes.
    void free_target(std::uint32_t id, TextureStore& textures);
    // Moves when every sampler table is written again.
    [[nodiscard]] std::uint32_t sampler_generation() const noexcept { return sampler_generation_; }

    // The frame so far becomes picture `key`; the frame goes on in `fresh`, which starts empty. `released` is the
    // picture `key` had.
    [[nodiscard]] bool can_take_scene() const noexcept;
    bool take_scene(std::uint64_t key, gpu::TextureRef fresh, TextureStore& textures, gpu::TextureRef& released);
    // The frame, and its depth, as pictures for the next target pass (a key of 0: not wanted); end_target_pass gives
    // them back, or close_frame_for_pass when the pass could not begin.
    void open_frame_for_pass(std::uint64_t color_key, std::uint8_t channels, std::uint64_t depth_key, TextureStore& textures);
    void close_frame_for_pass(TextureStore& textures);
    [[nodiscard]] bool draw_frame_pass(
        const FramePassRequest& params,
        TextureStore& textures);

    // Stretch-blit `source` to the backbuffer. A null/invalid source is a
    // runtime bug: callers must keep the last valid swap-chain image instead
    // of submitting a black frame. `black` presents black with the overlays
    // (black output); `source` then only sets the layout.
    // `capture_backbuffer` records the final post-blit swap-chain image.
    // Returns whether an optional backbuffer capture was recorded. Busy
    // diagnostic buffers never suppress the required blit/presentation.
    bool present(const DisplayTexture* source, bool capture_backbuffer = false, bool black = false);

    // Render-owner only. Each point's depth (0 near, 1 far) into slot `slot`'s readback, in the open frame where the
    // stream is now; screen_probes_finished names the values, one float a point, once that frame's fence has passed.
    // `compared` probes are answered with their depth too: the read breaks no render pass here.
    [[nodiscard]] bool record_screen_probes(std::size_t slot, std::span<const ScreenProbe> probes, bool compared);
    [[nodiscard]] const float* screen_probes_finished(std::size_t slot);  // throws when the device is gone
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
    [[nodiscard]] ID3D12RootSignature* pipeline_root() const { return root_signature_.Get(); }
    [[nodiscard]] void* pipeline_backend() const noexcept { return const_cast<RendererD3D12*>(this); }
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
    // The draw heaps, every texture and sampler, and the texture lists in `lists`.
    void bind_draw_textures(gpu::Address lists);
    // What bind_draw_textures would bind, as plain values (the recorder keeps them for a chunk's list).
    [[nodiscard]] gpu::TextureArrays draw_texture_arrays(gpu::Address lists);

    // Parallel recording of a draw pass (draw_walker.cpp): the pass was recorded as ops in chunks; each chunk is replayed into a
    // command list of its own, on any thread, and the lists run in order after the commands recorded so far. begin closes the
    // frame's list at this point (false: not possible now, nothing changed) and says each chunk's draw targets; chunk replays
    // chunk `i`; end carries the frame on in a new list after the chunks'. replay_draws replays one chunk on the frame's list.
    [[nodiscard]] bool parallel_record_begin(const std::uint8_t* chunk_aux, unsigned chunk_count);
    void parallel_record_chunk(unsigned chunk, const gpu::DrawOp* ops, std::size_t count);
    void parallel_record_end();
    void replay_draws(std::uint8_t aux, const gpu::DrawOp* ops, std::size_t count);

private:
    void invalidate_draw_bindings();
    // VSYNC ON presents each frame for round(refresh / rate) refreshes when
    // the refresh is a multiple of the present rate (60 at 120 Hz, 30 at
    // 60 Hz), so every frame stays on screen equally long; other rates keep
    // one refresh.
    [[nodiscard]] UINT present_sync_interval(unsigned configured, float max_fps);
    void refresh_display_rate();
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
    void write_scene_conversion_views(const DescriptorRing::Persistent& table);
// The colour target becomes a picture and `fresh`, or a new target, takes its place.
    [[nodiscard]] gpu::TextureRef swap_scene_color(gpu::TextureRef fresh);
    // The conversion views after a swap, several in a frame: a table of its own, as the GPU reads views when it
    // runs, used again once the frames that could read it are done.
    std::uint64_t frame_pass_color_key_ = 0u;
    std::uint64_t frame_pass_depth_key_ = 0u;
    struct ConvSceneTable {
        DescriptorRing::Persistent table;
        std::uint64_t free_from = 0u;
    };
    std::vector<ConvSceneTable> conv_scene_tables_;
    std::size_t conv_scene_table_ = ~std::size_t{0};
    // Presents so far.
    std::uint64_t presented_frames_ = 0;
    bool create_root_signature();
    bool create_sampler_heap();
    void reset_sampler_frame(); // only after this slot's fence completes
    bool create_conversion_pipeline();  // the capture pipelines and the screen probes' compute pipeline
    bool create_clear_pipeline();

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

    WindowOwner* window_owner_ = nullptr;
    HWND window_ = nullptr;
    bool keep_window_ = false;
    bool device_gone_ = false;
    bool same_card_only_ = false;
    LUID card_{};
#if defined(NEBULA_DEV)
    Microsoft::WRL::ComPtr<ID3D12DebugDevice> debug_device_;
#endif
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    // The device's enhanced-barrier interface when it has them (BarrierPath::device points at it).
    Microsoft::WRL::ComPtr<ID3D12Device10> barrier_device_;
    // The selected adapter, for video-memory budget queries.
    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3_;
    // The OS signals this event when it changes the process's video memory budget (RegisterVideoMemoryBudgetChangeNotificationEvent;
    // auto-reset; null where not registered); video_memory then reads again at once, else at most every 100 ms.
    HANDLE budget_event_ = nullptr;
    DWORD budget_cookie_ = 0u;
    mutable std::int64_t memory_read_ms_ = 0;
    mutable std::uint64_t memory_usage_ = 0u;
    mutable std::uint64_t memory_budget_ = 0u;
    void release_budget_notification() noexcept;
    std::atomic<bool> occluded_{false};
    // The swap chain's frame latency waitable object (create_swap_chain): a present waits on it, bounded, so a frame is queued
    // when the display can take it. Owned (closed with the chain); null where the chain has none.
    HANDLE latency_wait_ = nullptr;
    unsigned latency_timeouts_ = 0u;
    void release_latency_wait() noexcept;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swap_chain_;
    Microsoft::WRL::ComPtr<IDXGIFactory4> dxgi_factory_;
    // FSR 3 frame generation: its pacing swap chain while it is chosen.
    FsrFrameGeneration fsr_generation_;
    bool fsr_swap_chain_ = false;
    // The swap chain was made with DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING (the system allows it): a present with VSync off
    // may then tear and does not wait for the display, as Vulkan's immediate mode.
    bool tearing_allowed_ = false;
    bool fg_hudless_ready_ = false;
    unsigned fsr_rect_[4]{};
    [[nodiscard]] bool fsr_generation_wanted() const;
    bool create_swap_chain(bool fsr);
    void recreate_swap_chain(bool fsr);
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>,
        kFramesInFlight> allocators_;
    // Parallel recording: a command list and allocator for each chunk of each recorded pass of a frame (by frame slot; the
    // allocators restart with the slot), the frame's list so far and the lists it was cut into, which run before command_list_.
    struct RecordList {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
    };
    void set_render_targets(ID3D12GraphicsCommandList* list, unsigned aux) const;
    std::array<std::vector<RecordList>, kFramesInFlight> record_lists_;
    std::array<unsigned, kFramesInFlight> record_used_{};
    unsigned record_base_ = 0u;
    std::vector<std::uint8_t> record_aux_;
    std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>> segment_lists_;
    unsigned segments_used_ = 0u;
    std::vector<ID3D12CommandList*> frame_lists_;
    std::vector<ID3D12CommandList*> submit_lists_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    std::array<std::uint64_t, kFramesInFlight> fence_values_{};
    HANDLE fence_event_ = nullptr;
    // The display the window and swap chain were last brought to.
    struct AppliedDisplay {
        bool valid = false;
        unsigned mode = 0;
        std::array<char, 64> monitor{};
        unsigned window_width = 0;
        unsigned window_height = 0;
    };
    AppliedDisplay applied_display_{};
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> timestamp_query_heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> timestamp_readback_;
    std::array<bool, kFramesInFlight> timestamp_pending_{};
    std::uint64_t timestamp_frequency_ = 0;
    // --diagnostics gpu-passes: GPU time by pass for present-stats' gpu-pass-ms. A mark starts a pass on the open command
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
    GpuPass rt_outer_pass_ = GpuPass::Scene;
    bool pass_list_open_ = false;
    std::array<double, static_cast<std::size_t>(GpuPass::Count)> telemetry_pass_ms_{};
    // The settings page's timing: asked for by gpu_timing_wanted_, on for the frames begun while it is (or the diagnostics
    // record passes); each pass's running average for the page.
    std::atomic<bool> gpu_timing_wanted_{false};
    bool diagnostic_passes_ = false;
    bool pass_timing_on_ = false;
    std::array<std::atomic<float>, static_cast<std::size_t>(GpuPass::Count)> pass_ema_ms_{};
    void ensure_pass_queries();
    // --diagnostics gpu-passes also counts each command list's pipeline statistics (shader invocations, primitives) for
    // present-stats' gpu-stats.
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> stats_query_heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> stats_readback_;
    std::array<bool, kFramesInFlight> stats_pending_{};
    D3D12_QUERY_DATA_PIPELINE_STATISTICS telemetry_stats_{};


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
        std::optional<DescriptorRing::Persistent> depth_srv;  // the multisampled depth, read by the depth resolve
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
        std::optional<DescriptorRing::Persistent> conv_table;
        // The conversion views' persistent indices (srv_ring_.gpu_at turns one into a handle of the current heap).
        std::uint32_t conv_color_index = DescriptorRing::kNoPersistent;
        std::uint32_t conv_depth_index = DescriptorRing::kNoPersistent;
    };
    SceneTargetSet alt_scene_;
    // The open target pass's set (begin_target_pass) while the scene target holds it, and each key's set between passes
    // ( its colour shared with its picture), dropped after kTargetIdleFrames frames unused. A picture is sampled
    // through its own view slot (TextureStore::allocate_view) reading the pass's channels.
    struct TargetSlot {
        SceneTargetSet set;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsv_heap;
        std::uint64_t used = 0;
        std::uint32_t width = 0, height = 0;  // frame units
    };
    // The pictures draws sample by key (target passes' and captures'), each through its own view slot
    // (TextureStore::allocate_view) reading its channels; a capture's `bytes` is its footprint at the key, over which a
    // later capture replaces it.
    struct Picture {
        std::uint32_t view = ~0u;
        gpu::TextureRef viewed;
        std::uint32_t width = 0, height = 0;  // frame units, the size draws sample it at
        std::uint8_t channels = 0;
    };
    std::unordered_map<std::uint64_t, Picture> pictures_;
    // `texture` becomes picture `key` (`width` x `height` game units, read through `channels`): true when draws
    // must look it up again (a new view).
    bool publish_picture(std::uint64_t key, std::uint32_t width, std::uint32_t height, gpu::TextureRef texture,
                         std::uint8_t channels, TextureStore& textures);
    bool publish_frame_pass(const FramePassRequest& params, gpu::TextureRef texture, TextureStore& textures);
    static constexpr std::uint64_t kTargetIdleFrames = 600u;  // frames at the game's 60 a second
    // The same idle time at the rate frames are being presented (a 240 Hz picture counts four frames per game frame).
    [[nodiscard]] std::uint64_t target_idle_frames() const noexcept {
        const double rate = measured_present_hz_.load(std::memory_order_relaxed);
        return kTargetIdleFrames * std::max<std::uint64_t>(1u, static_cast<std::uint64_t>(rate / kGameStepRate + 0.5));
    }
    std::unordered_map<std::uint64_t, TargetSlot> target_slots_;
    // A slot's targets go to the open frame's retired list (the frames in flight may still draw into or read them).
    void retire_target_slot(TargetSlot& slot);
    // Slots no pass has used for target_idle_frames() give their targets back; also from begin_frame, so that a scene without
    // target passes does not keep the last scene's.
    void sweep_target_slots();
    std::uint64_t target_key_ = 0;
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
    std::uint64_t fg_hudless_used_frame_ = 0u;   // the last frame frame generation ran
    std::uint64_t hdr_capture_used_frame_ = 0u;  // the last frame a capture drew the SDR picture
    std::uint64_t motion_used_frame_ = 0u;       // the last frame a scene asked for the motion, reactive and layer targets
    std::uint64_t output_scene_used_frame_ = 0u; // the last frame an output scene target was asked for (alt_scene_)
    std::uint64_t wide_scene_used_frame_ = 0u;   // the last frame a scene asked for the wide colour targets (hdr_scene_scene_)
    // Once a frame: hands the frame generation and temporal targets back after 600 frames unused, at the frame's fence.
    void trim_idle_effect_targets();
    void take_fg_hudless_scene();
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
    void present_to_swap_chain();
    // An upload ring could not grow (out of upload memory): no further attempts.
    bool ring_growth_failed_ = false;
    bool present_game_frame_ = false;
    // The wide scene's colour targets (begin_wide_scene), over the scene target's own depth.
    SceneTargetSet hdr_scene_scene_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> hdr_scene_rtv_heap_;
    // The live scene target is the wide set (swapped with the set that held it).
    bool scene_hdr_ = false;
    bool ensure_wide_set(SceneTargetSet& set, Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& rtv_heap);
    void record_hdr_begin();
    void record_hdr_end();
    SceneCamera camera_pass_;
    std::chrono::steady_clock::time_point camera_metered_{};
    // persistent_views: the conversion-source views live in the descriptor
    // ring's persistent prefix (else the caller writes them per use).
    // unordered_access: the colour target takes compute writes (the temporal
    // upscaler's output, as the live output scene target).
    bool create_scene_set(SceneTargetSet& set, Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& rtv_heap,
                        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& dsv_heap, const FrameMap& map,
                        bool persistent_views = true, bool unordered_access = false, unsigned samples = 1u,
                        bool hdr = false, unsigned width = kFrameUnits, unsigned height = kFrameUnits, bool target = false);
    void write_scene_set_views(SceneTargetSet& set, const DescriptorRing::Persistent& table);
    void swap_scene_with(SceneTargetSet& other);
    // The last frame's start (the hitch log's intervals, begin to begin).
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
    double generated_gpu_ms_ = 0.0;
    std::uint64_t generated_gpu_samples_ = 0;
    double generated_gpu_ema_ms_ = 0.0;
    double native_scene_gpu_ema_ms_ = 0.0;
    bool scene_drawn_ = false;
    std::array<bool, kFramesInFlight> frame_scene_drawn_{};
    std::array<bool, kFramesInFlight> frame_generated_{};
    std::atomic<double> measured_present_hz_{0.0};
    std::int64_t measured_present_window_qpc_ = 0;
    std::uint32_t measured_present_count_ = 0;
    // Render target 1 bound next to the scene target (0 none, 1 the motion target, 2 the reactive mask).
    std::uint8_t bound_rt_aux_ = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> reactive_target_;
    D3D12_CPU_DESCRIPTOR_HANDLE reactive_rtv_{};
// The vertex and matrix rings: a frame reads the frame before's vertices and rows, so they advance past a frame in flight.
    unsigned history_segment_ = 0;
    std::array<std::uint64_t, kRingSegments> history_segment_fence_{};
    std::size_t history_begin_vertex_ = 0, history_begin_matrix_ = 0;
    bool history_used_ = true;

    // Backbuffers + present blit.
    static constexpr unsigned kSwapChainBuffers = 3;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,
        kSwapChainBuffers> backbuffers_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> blit_pipeline_;
    // Presentation image quality; present() keeps the plain blit while it is
    // not ready.
    PostProcessor post_process_;
    AmbientOcclusion ambient_occlusion_;
    GameBloom game_bloom_;
    unsigned scene_jitter_index_ = 0u, scene_jitter_phases_ = 0u;
    // A scene colour that could not take the scene's place, kept until the GPU is done with it.
    struct OldSceneTarget {
        gpu::TextureRef texture;
        std::uint64_t free_from = 0u;
    };
    std::vector<OldSceneTarget> old_scene_targets_;
    RtView scene_ao_view_{};
    std::vector<ao::ObjectOccluder> scene_ao_objects_;
    // scene target-copy destination textures (render targets) are placed in shared
    // heaps rather than committed one by one (gpu_placed_textures.h).
    PlacedTextureAllocator frame_pass_heap_{
        D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES, 128ull << 20};

    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> sampler_heap_;
    std::uint32_t sampler_config_ = ~0u;
    std::uint32_t sampler_generation_ = 1u;
    SamplerSlotCache sampler_slots_;
    // The texture region is the first part of srv_ring_'s heap: a view's slot is its TextureStore index and the last slot
    // (null_texture_slot_) holds the null view (an empty map). When the region grows (install_texture_heap,
    // grow_texture_heap) the ring builds a larger heap; the replaced heap is kept until the GPU is past the frames that
    // recorded commands with it.
    std::uint32_t null_texture_slot_ = 0u;
    struct RetiredHeap {
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
        std::uint64_t fence = 0u;
    };
    std::vector<RetiredHeap> retired_heaps_;
    void install_texture_heap(TextureStore& cache, std::uint32_t slots);
    [[nodiscard]] DescriptorRing::Persistent allocate_persistent_descriptors(unsigned count);
    void fill_sampler_heap(const RenderConfig& cfg);
    void grow_texture_heap(std::uint32_t index);
    // The descriptor ring grew a frame's segment into a new heap: the texture views go into it again.
    void texture_heap_replaced();
    const void* mirrored_backend_ = nullptr;
    TextureStore* mirrored_store_ = nullptr;
    // The texture lists (t18): one upload buffer per frame in flight, written from the start each frame; a full one is
    // replaced by one twice its size, the old kept until the GPU is past it.
    struct TextureListBuffer {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        std::uint32_t* mapped = nullptr;
        std::uint32_t capacity = 0u;  // pairs
    };
    std::array<TextureListBuffer, kFramesInFlight> texture_lists_{};
    std::uint32_t texture_lists_used_ = 0u;
    struct RetiredBuffer {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        std::uint64_t fence = 0u;
    };
    std::vector<RetiredBuffer> retired_texture_lists_;
    gpu::Encoder encoder_;
    gpu::UploadMemory zero_stream_{};
    std::uint32_t zero_stream_bytes_ = 0u;

    UploadRing vertex_ring_;
    UploadRing index_ring_;
    UploadRing constant_ring_;
    UploadRing matrix_ring_;        // palette snapshots (structured)
    DescriptorRing srv_ring_;

    unsigned frame_slot_ = 0;
    std::uint64_t frame_index_ = 0;
    std::uint64_t next_fence_value_ = 0;


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
    std::uint64_t telemetry_new_pictures_start_ = 0;
    std::uint64_t telemetry_pictures_made_start_ = 0;
    PresentPacer present_pacer_;
    bool window_shown_ = false;
    std::uint64_t telemetry_present_count_ = 0;
    std::uint64_t telemetry_interval_count_ = 0;
    double telemetry_interval_ms_ = 0.0;
    double telemetry_interval_max_ms_ = 0.0;
    double telemetry_present_ms_ = 0.0;
    double telemetry_present_max_ms_ = 0.0;
    double telemetry_pace_wait_ms_ = 0.0;
    double telemetry_pace_wait_max_ms_ = 0.0;
    double telemetry_gpu_ms_ = 0.0;
    // GPU time by a command list's place in its frame (0: the first list after a present; the last place collects
    // the rest), for present-stats' gpu-by-list-ms.
    static constexpr unsigned kGpuListPlaces = 6;
    std::array<std::uint8_t, kFramesInFlight> timestamp_list_place_{};
    unsigned list_in_frame_ = 0;
    std::array<double, kGpuListPlaces> telemetry_gpu_by_place_{};
    std::array<unsigned, kGpuListPlaces> telemetry_gpu_by_place_count_{};
    std::uint64_t telemetry_gpu_samples_ = 0;
    // Refresh rate of the swap chain's output (EnumDisplaySettings) and, per
    // telemetry window, how many refreshes each displayed frame stayed on
    // screen (DXGI frame statistics; index 4 counts 4 or more).
    double display_refresh_hz_ = 0.0;
    // The display varies its refresh with the presents (next_variable_refresh): display_refresh_hz_ is then the cap that keeps
    // it in its range (variable_refresh_cap), the sync interval is the configured one, and presents are spaced at the cap.
    bool variable_refresh_ = false;
    std::uint64_t display_refresh_presents_ = 0;
    // The output's colour space and luminances as last read (read_display_output; read_output_hdr), and whether it has been read.
    OutputHdr display_reading_{};
    bool display_read_ = false;
    void read_display_output();
    // HDR10 output: the swap chain is R10G10B10A2 in the ST 2084 colour space while hdr_active_, and the post chain's last
    // write encodes for it with hdr_output_'s levels. reconcile_hdr_output follows the setting, the display and frame
    // generation at a frame boundary; set_swap_chain_hdr changes the buffers' format (ResizeBuffers) and colour space.
    bool hdr_active_ = false;
    bool hdr_failed_ = false;  // the last switch to HDR10 failed: not tried again until the setting or the display changes
    PostOutput hdr_output_{};
    void reconcile_hdr_output();
    void publish_display_hdr();
    bool set_swap_chain_hdr(bool hdr, const PostOutput& levels);
    void apply_hdr_metadata(const PostOutput& levels);
    // The SDR picture of the frame for a capture while the swap chain is HDR: the post chain's last write again, into an
    // R8G8B8A8 target of the swap chain's size (kept). Null when the target cannot be made.
    [[nodiscard]] ID3D12Resource* hdr_capture_picture(const DisplayTexture& source, const D3D12_VIEWPORT& viewport);
    Microsoft::WRL::ComPtr<ID3D12Resource> hdr_capture_target_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> hdr_capture_rtv_heap_;
    UINT last_stats_present_count_ = 0;
    UINT last_stats_present_refresh_count_ = 0;
    std::array<std::uint64_t, 5> telemetry_shown_refreshes_{};

    // scene target->texture copy destinations, keyed by (game dest address, copy format,
    // depth-source). SMG reuses one address with different copy formats, and each format
    // needs its own converted texture. The resource is reused across copies and recreated
    // only on size change; the TextureStore alias is refreshed per copy (cheap when unchanged).
    struct FramePassTarget {
        Microsoft::WRL::ComPtr<ID3D12Resource> texture;
        UINT width = 0;
        UINT height = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        bool rtv_valid = false;
    };
    // One texture per capture key at the frame's extent in kSceneColorFormat, kept until the game frees the texture
    // (free_target) or the frame's size or samples change.
    std::unordered_map<std::uint64_t, FramePassTarget> frame_pass_targets_;
    // Render-owner-only aggregate work counts, reported once at shutdown.
    // Payload bytes are copied pixels, not a device allocation-size estimate.
    std::uint64_t frame_pass_requests_ = 0;
    std::uint64_t frame_passes_drawn_ = 0;
    using RetiredResourceList =
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>;
    std::array<RetiredResourceList, kFramesInFlight>
        retired_targets_;
    // Interpolation: copy textures retired during a game-frame ring segment.

    // Conversion blit pipeline (its root signature is a module static beside the
    // present blit's) and CPU-only descriptors for conversion destinations. RTV
    // descriptors are consumed at record time, so every copy rebinds the same
    // CPU-only scratch slot. scene target source SRVs occupy an immutable prefix of the
    // active shader-visible heap.
    Microsoft::WRL::ComPtr<ID3D12PipelineState> conv_pipeline_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> display_filter_pipeline_;  // 1:1 display copies without gamma
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> conv_rtv_heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE conv_scratch_rtv_{};
    detail::ConversionRtvSlot conv_scratch_rtv_slot_;
    std::optional<DescriptorRing::Persistent> conv_scene_source_table_;
    std::uint32_t conv_scene_color_index_ = DescriptorRing::kNoPersistent;
    std::uint32_t conv_scene_depth_index_ = DescriptorRing::kNoPersistent;
    // The masked scene target clears (color / alpha only): shaders and pipelines, made at first use.
    UtilityShaderBytes clear_vs_;
    UtilityShaderBytes clear_ps_;
    // Per scene target variant (scene_variant: samples, then HDR).
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kSceneVariantCount> clear_rgb_pipelines_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kSceneVariantCount> clear_alpha_pipelines_;

    // Screen probes (ScreenProbeCs): the values a slot's dispatch writes, copied to its readback, fenced with its frame.
    Microsoft::WRL::ComPtr<ID3D12RootSignature> probe_root_signature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> probe_pipeline_;
    struct ScreenProbeSlot {
        Microsoft::WRL::ComPtr<ID3D12Resource> values;    // default heap, unordered access between dispatches
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        const float* mapped = nullptr;
        std::uint64_t bytes = 0;
        std::uint64_t fence = 0;
    };
    std::vector<ScreenProbeSlot> probe_slots_;
    // ScreenProbeSlot::fence of probes recorded in the open frame.
    static constexpr std::uint64_t kProbesInFrame = ~std::uint64_t{0};
    static const std::uint8_t* pin_readback_mapping(ID3D12Resource* readback);

};

// The renderer RenderCore builds against (renderer_vk.h defines it for Vulkan).
using Renderer = RendererD3D12;

}  // namespace galaxy::render
