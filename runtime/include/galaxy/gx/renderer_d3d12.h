#pragma once

// RendererD3D12: device, swap chain, EFB/XFB render targets, per-frame upload
// rings, draw submission, and presentation.
//
// Frame pipelining: kFramesInFlight frames are recorded ahead of the GPU;
// begin_frame() waits only on the fence of frame N - kFramesInFlight.  A full
// CPU/GPU sync happens exclusively in shutdown() and on resize.
//
// All GX draws target the offscreen EFB (color RGBA8 + depth
// D24_UNORM_S8_UINT — 24-bit Z parity with the Wii is a binding review
// amendment).  present() aspect-fits an XFB into the swap-chain backbuffer;
// that blit is the future post-processing / ray-tracing injection point.
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
#include "galaxy/gx/immutable_upload.h"
#include "galaxy/gx/gx_bitfields.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx/window_owner.h"
#include "galaxy/gx_d3d12.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <future>
#include <string>
#include <span>
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

    // Bump-allocates within the current frame segment.  Exhaustion is a
    // GxFatalError (sized for worst observed frame x margin, never silently
    // dropped geometry).
    // Alignment must be a nonzero power of two. Rejected requests do not
    // consume bytes or advance the segment's content generation.
    Allocation allocate(std::size_t size, std::size_t alignment);

    Allocation upload_immutable(
        const void* source, std::size_t size, std::size_t alignment,
        ImmutableUploadToken& token);
    [[nodiscard]] std::uint64_t reused_bytes() const { return reused_bytes_; }
    [[nodiscard]] std::uint64_t copied_bytes() const { return copied_bytes_; }
    // Number of upload_immutable() calls this frame. `reused_bytes()` and
    // `copied_bytes()` are only meaningful *relative to this count*: both are
    // zero for a frame in which the immutable path was never entered, which is
    // indistinguishable on the timing line from a frame in which it was entered
    // and classified every byte as a copy. Retained recordings showed
    // `decoded-vtx-cache-hits=235` with both byte counters at 0 on the same frame,
    // and the call count is the one field that separates "the reuse path is not
    // reached" from "the counters are not observed". The increment is one add on
    // a path that already does a memcpy or a map lookup, so it is not worth
    // gating behind a trace flag of its own.
    [[nodiscard]] std::uint64_t immutable_upload_calls() const {
        return immutable_upload_calls_;
    }

    // Peak bytes used within a single frame segment, and the segment size. These
    // decide whether the ring's configured size is real resident memory or just
    // reserved address space. The rings are D3D12_HEAP_TYPE_UPLOAD
    // CreateCommittedResource buffers that are mapped for the life of the
    // process, so the *reservation* is bytes_per_frame * frames_in_flight --
    // 240 MiB by default. But a mapped upload heap only has physical pages
    // committed for the parts that were actually written, so the physical cost
    // tracks the working set (peak cursor) rather than the reservation. Without
    // this number, `bytes_per_frame * frames_in_flight` is an upper bound that
    // reads like a measurement, and on the low-end target that difference decides
    // whether trimming the defaults is worth anything. Exhaustion is fatal and
    // loud, so the floor for any reduction is `peak_bytes_per_frame()`, not a
    // guess.
    [[nodiscard]] std::size_t peak_bytes_per_frame() const {
        return peak_bytes_per_frame_;
    }
    [[nodiscard]] std::size_t bytes_per_frame() const { return bytes_per_frame_; }

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
    // This is for descriptors whose lifetime is tied to explicit GX texture
    // invalidation instead of the current frame slot.
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
};

class RendererD3D12 {
public:
    // Creates the Win32 window, device, queue, swap chain, EFB targets at
    // kEfbWidth x kEfbHeight x efb_scale, the shared GX root signature, the
    // present-blit pipeline, and all rings.
    bool initialize(int window_width, int window_height, unsigned efb_scale);
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
    std::uint64_t end_frame(bool present_swap_chain = true);
    [[nodiscard]] std::uint64_t completed_fence_value() const;
    // Bounded wait for one exact queue signal. Used only by the explicitly
    // synchronous renderer path; normal async rendering never drains the GPU.
    void wait_for_fence_value(std::uint64_t fence_value);

    // Binds EFB RTV+DSV and applies the viewport from the XF viewport regs.
    void bind_efb(const float xf_viewport[6]);
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

    // EFB copy paths (record into the current command list).  Both run the
    // conversion blit (intensity / gamma / 3-tap vertical copy filter /
    // half-scale box / XFB y-scale stretch / depth-source packing) instead
    // of a raw CopyTextureRegion — Dolphin TextureConverterShaderGen
    // semantics.  `is_depth_copy` = PE pixel format is Z24 at copy time
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
    void present(const XfbTexture* source, bool capture_backbuffer = false);

    // Bring-up diagnostics: record an EFB→readback copy into the current
    // command list (call while recording). Readback and PPM encoding are
    // polled asynchronously after end_frame(); normal rendering never drains
    // the GPU for an opt-in capture.
    [[nodiscard]] bool capture_pointer_depth(std::span<std::uint32_t> pixels);
    [[nodiscard]] bool peek_efb(
        std::uint16_t x,
        std::uint16_t y,
        EfbPeekKind kind,
        std::uint32_t& value);
    void debug_copy_efb_to_readback();
    void debug_log_readback();
    void debug_log_backbuffer_readback();
    void debug_copy_selected_xfb_to_readback(const XfbTexture& source);
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
    [[nodiscard]] UploadRing& vertex_ring() { return vertex_ring_; }
    [[nodiscard]] UploadRing& index_ring() { return index_ring_; }
    [[nodiscard]] UploadRing& constant_ring() { return constant_ring_; }
    [[nodiscard]] UploadRing& matrix_ring() { return matrix_ring_; }
    [[nodiscard]] DescriptorRing& srv_ring() { return srv_ring_; }
    [[nodiscard]] unsigned frame_slot() const { return frame_slot_; }
    // Live bytes held by the EFB-copy destination cache, in SCALED bytes (each
    // destination is allocated at the internal EFB extent, so its `bytes` grows
    // with efb_scale^2 while the 192 MiB budget stays fixed). Maintained but
    // previously unreported, which left the one resolution-scaled GPU pool that
    // is not a render target invisible to every recording: this runtime has three
    // such pools (this one, the decoded texture map, and the fixed render
    // targets) and only the fixed ones were observable. Report it against its
    // budget so a run can say whether it is saturating rather than leaving the
    // largest resolution-dependent allocation unmeasured.
    [[nodiscard]] std::uint64_t efb_copy_dest_bytes() const {
        return efb_copy_dests_bytes_;
    }
    // The copy-destination byte budget this session is running with (192 MiB
    // default, GALAXY_EFB_COPY_DEST_BUDGET_MB overridable). Reported next to the
    // live bytes so a recording states whether the pool is saturating rather than
    // requiring the reader to know the default.
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

    // Lazily creates the solid-fill quad root signature/PSO. Unlike the
    // corner texture, this never depends on game assets, so it always
    // succeeds once the device exists. Draw-call based: this deliberately
    // avoids ID3D12GraphicsCommandList::ClearRenderTargetView with a
    // non-null pRects, which reproducibly faults inside nvwgf2umx.dll
    // (STATUS_STACK_BUFFER_OVERRUN, confirmed via Application-log Event
    // 1000) on at least one real driver build when used for the native
    // settings overlay's panel/text rects. Full-target clears (pRects ==
    // nullptr) are unaffected and still used elsewhere.
    void ensure_native_settings_solid_pipeline_loaded();
    // Draws one flat-colored quad (screen pixel coordinates) via a real
    // draw call rather than a rect-limited clear. No-op (and does not
    // create device resources) until first called after the device exists.
    void draw_ui_solid_quad(
        int x, int y, int w, int h,
        const std::array<float, 4>& color);

private:
    void invalidate_gx_bindings();
    void pace_present_if_needed(unsigned vsync_interval, float max_fps);
    void wait_for_frame_latency_if_needed(bool collect_present_telemetry);
    void wait_for_gpu();            // shutdown/resize only
    // Applies a pending live window resize, if one is recorded (see
    // the coherent extent mailbox in renderer_d3d12.cpp). No-op when none is pending.
    // Only ever called from begin_frame(), i.e. between frames with no
    // in-flight command-list recording on this renderer.
    void apply_pending_resize();
    void reap_debug_capture_writers(bool wait_for_all);
    void queue_debug_ppm_capture(
        const char* tag,
        const std::string& path,
        const std::uint8_t* source,
        unsigned width,
        unsigned height,
        unsigned row_pitch);
    bool create_efb_targets(unsigned efb_scale);
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

    // Backbuffers + present blit.
    static constexpr unsigned kSwapChainBuffers = 3;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,
        kSwapChainBuffers> backbuffers_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> blit_pipeline_;

    // Native settings overlay: the game's own window-frame corner texture
    // (extracted read-only from files/LayoutData/FileSelect.arc -- see
    // galaxy/gx/game_asset_extract.h), drawn at all four panel corners so
    // the overlay visually reads as part of Super Mario Galaxy's own UI
    // instead of a flat placeholder rectangle. Loaded lazily and once, the
    // first time the overlay is shown; every part of this stays optional --
    // if the asset can't be read/decoded (e.g. a different game dump), the
    // overlay silently keeps its existing solid-color corners.
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
    std::array<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>, kFramesInFlight>
        sampler_heaps_;
    D3D12_GPU_DESCRIPTOR_HANDLE default_sampler_table_{};
    std::array<std::uint32_t, kFramesInFlight> default_sampler_config_keys_{};
    std::array<bool, kFramesInFlight> default_sampler_valid_{};
    // Each cache describes tables actually written in its physical heap.
    // Configuration invalidation happens only after that slot's fence; tables
    // retained in another heap cannot establish a hit for the active heap.
    std::array<SamplerTableCache, kFramesInFlight> sampler_tables_;

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

    // EFB->texture copy destinations, keyed by (guest dest address, copy
    // format, depth-source) — SMG reuses one address with different copy
    // formats, and each format needs its own converted texture.  The
    // resource is reused across copies and recreated only on size change;
    // the TextureCache alias is refreshed per copy (cheap when unchanged).
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
    // BOUNDED. This map holds one committed D3D12 texture per destination key.
    // Each is that copy's SCALED destination extent at kEfbColorFormat (RGBA8),
    // so the size varies with both the copy's logical extent and the internal
    // scale. At the full 640x456 EFB extent: 1.11 MB at 1x, 17.81 MB at 4x,
    // 40.08 MB at 6x. (An earlier note here said "~5 MiB at 4x and ~11 MiB at
    // 6x", which understates a full-extent entry by ~3.5x — enough to mislead
    // anyone choosing the default budget downward.) The map used to be unbounded
    // — only a failed allocation (and full shutdown) ever removed an entry — so
    // every distinct (addr, format, depth, bloom) the game had ever copied to
    // stayed resident for the life of the process.
    //
    // Budgeted in BYTES, not entries: the per-entry cost varies with the scale
    // AND with the copy extent, so an entry count cannot bound residency. The
    // accounting charges the driver's own `GetResourceAllocationInfo` size rather
    // than a computed w*h*bpp, so it follows the resource if the format changes.
    // The default is deliberately well above any plausible per-scene working set
    // (SMG double-buffers a handful of XFBs across a couple of formats plus one
    // bloom workspace), so this should not thrash a legitimate set; it stops
    // unbounded accumulation across scene transitions. Override with
    // GALAXY_EFB_COPY_DEST_BUDGET_MB; 0 disables the bound.
    //
    // Note for the low-end target: the adapter there reports `vram-mb=128`, but on
    // Intel integrated graphics that value is a FICTITIOUS compatibility constant
    // emitted for applications that assume a discrete GPU — those parts have no
    // separate memory bank, so a DEFAULT-heap texture is system memory either way.
    // The reason to bound this map is that it was unbounded, not that it
    // oversubscribes a 128 MB pool; there is no such pool.
    std::unordered_map<std::uint64_t, EfbCopyDest> efb_copy_dests_;
    std::uint64_t efb_copy_dests_tick_ = 0;
    std::uint64_t efb_copy_dests_bytes_ = 0;
    using RetiredResourceList =
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>;
    std::array<RetiredResourceList, kFramesInFlight>
        retired_efb_copy_textures_;

    // Conversion blit pipeline (root sig lives in a module static next to
    // the present-blit one) + cached CPU-only descriptors for conversion
    // sources/destinations. Non-shader-visible RTV descriptors are consumed
    // at record time, so every copy rebinds the same CPU-only scratch slot.
    Microsoft::WRL::ComPtr<ID3D12PipelineState> conv_pipeline_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> conv_rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> conv_srv_heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE conv_scratch_rtv_{};
    detail::ConversionRtvSlot conv_scratch_rtv_slot_;
    D3D12_CPU_DESCRIPTOR_HANDLE conv_efb_color_srv_{};
    D3D12_CPU_DESCRIPTOR_HANDLE conv_efb_depth_srv_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> clear_rgb_pipeline_;
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
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> peek_srv_heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE peek_rtv_{};
    D3D12_CPU_DESCRIPTOR_HANDLE peek_srv_{};
    D3D12_GPU_DESCRIPTOR_HANDLE peek_srv_gpu_{};
    UINT peek_readback_row_pitch_ = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> pointer_depth_target_;
    Microsoft::WRL::ComPtr<ID3D12Resource> pointer_depth_readback_;
    bool read_efb_pixels(std::uint16_t x, std::uint16_t y, EfbPeekKind kind,
        std::uint32_t& value, std::span<std::uint32_t> depth_pixels);

};

}  // namespace galaxy::gx
