// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// RendererVk: the GX renderer on Vulkan (docs/VULKAN.md), the same interface GxBackend uses on RendererD3D12.
//
// The EFB is a colour target (RGBA8) and a depth target (D24 where the device has it, else D32 float) drawn through
// one render pass; an EFB copy, peek or present ends that pass, and the next EFB operation begins it again. Between
// command buffers the EFB is always in its attachment layouts. kFramesInFlight frames are recorded ahead of the GPU,
// each in two command buffers submitted together: the frame's texture uploads, then its draws (TextureCache never
// writes a texture after a draw of the same frame read it, so the uploads may run first).
//
// Draw bindings mirror the D3D12 root signature: set 0 holds b0 and b1 (dynamic uniform buffers) and the matrix
// palette (a dynamic storage buffer), set 1 the eight textures, set 2 the eight samplers. Addresses (gpu::Address)
// name a buffer and offset (vk::Context::resolve).
//
// Normal rendering only so far: the HUD split, temporal upscaling, interpolated redraw and the post-process chain
// report themselves unavailable.

#include "galaxy/gx/efb_copy.h"
#include "galaxy/gx/gpu_types.h"
#include "galaxy/gx/renderer_common.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx/ui_canvas.h"
#include "galaxy/gx/upload_ring.h"
#include "galaxy/gx/vk/shader_compile_vk.h"
#include "galaxy/gx/vk/vk_context.h"
#include "galaxy/gx/host_window.h"
#include "galaxy/gx/window_owner.h"
#include "galaxy/gx_d3d12.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace galaxy::gx {

class TextureCache;
struct TextureHandle;

namespace vk {

// What PipelineCacheVk builds GX pipelines against (RendererVk::root_signature()).
struct GxLayout {
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::array<VkDescriptorSetLayout, 3> sets{};
    VkRenderPass efb_pass = VK_NULL_HANDLE;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    bool geometry_shaders = false;
    bool dual_source_blend = false;
    bool logic_op = false;
    bool depth_clamp = false;
    bool depth_clip_enable = false;  // VK_EXT_depth_clip_enable
};

}  // namespace vk

class RendererVk final : public UiCanvas {
public:
    using SubmissionCallback = void (*)(void* context, std::uint64_t fence_value);
    static constexpr std::size_t kAsyncPeekFieldSlots = 3u;
    static constexpr std::size_t kAsyncPeekFieldChannels = 4u;

    bool initialize(int window_width, int window_height, unsigned efb_scale);
    // Why initialize failed, when the device said.
    [[nodiscard]] std::string startup_failure() const { return vk::Context::get().failure(); }
    void create_first_use_resources();
    void shutdown();
    [[nodiscard]] UploadBuffer create_upload_buffer(std::size_t bytes, const char* name);
    [[nodiscard]] gpu::Table texture_table_for(const TextureHandle* handles, unsigned count, const TextureCache& cache);
    void attach_texture_uploads(TextureCache& cache);
    void drain_gpu();
    void pump_messages() {}
    [[nodiscard]] bool quit_requested() const noexcept;

    void begin_frame(bool present_swap_chain = true);
    std::uint64_t end_frame(bool present_swap_chain = true, SubmissionCallback on_submitted = nullptr,
                            void* submission_context = nullptr);
    [[nodiscard]] std::uint64_t completed_fence_value() const;
    void wait_for_fence_value(std::uint64_t fence_value);

    void bind_efb(const float xf_viewport[6]);
    void clear_efb(std::uint32_t clear_ar, std::uint32_t clear_gb, std::uint32_t clear_z24, bool color_enable,
                   bool alpha_enable, bool depth_enable, unsigned rect_x = 0, unsigned rect_y = 0, unsigned rect_w = 0,
                   unsigned rect_h = 0, std::uint8_t pixel_format = 0);
    void draw(const DrawCall& call);
    void copy_efb_to_xfb(const EfbCopyParams& params, XfbTexture& dest);
    [[nodiscard]] bool copy_efb_to_texture(const EfbCopyParams& params, TextureCache& textures, bool is_depth_copy);
    bool present(const XfbTexture* source, bool capture_backbuffer = false);

    // Not on Vulkan yet: the HUD split, temporal upscaling and interpolated redraw.
    [[nodiscard]] bool begin_output_efb() noexcept { return false; }
    void end_output_efb() noexcept {}
    [[nodiscard]] bool output_efb_active() const noexcept { return false; }
    [[nodiscard]] bool scene_temporal_begin(float&, float&) noexcept { return false; }
    [[nodiscard]] bool begin_motion_scene() noexcept { return false; }
    void record_motion_debug_view() noexcept {}
    void reset_temporal_history() noexcept {}
    [[nodiscard]] unsigned matrix_history_frames() const noexcept { return 0u; }
    [[nodiscard]] std::uint64_t matrix_segment_serial() const noexcept { return 0u; }
    [[nodiscard]] bool replay_available() const noexcept { return false; }
    bool start_replay_playback() noexcept { return false; }
    [[nodiscard]] bool replay_playing() const noexcept { return false; }
    [[nodiscard]] bool interpolation_mode() const noexcept { return false; }
    [[nodiscard]] std::uint64_t replay_serial() const noexcept { return 0u; }
    [[nodiscard]] std::uint32_t replay_xfb_addr() const noexcept { return 0u; }
    void note_replay_scene() noexcept {}
    [[nodiscard]] unsigned replay_failure() const noexcept { return replay_failure_; }
    void note_replay_failure(unsigned reason) noexcept { replay_failure_ = reason; }
    void set_subframe_capture(bool) noexcept {}
    bool present_replay_subframe(std::span<const gpu::Address>) noexcept { return false; }
    void take_subframe_gpu_ms(double& total_ms, std::uint64_t& samples) noexcept {
        total_ms = 0.0;
        samples = 0u;
    }
    void set_scene_camera(float, float, float) noexcept {}

    [[nodiscard]] double measured_present_hz() const noexcept {
        return measured_present_hz_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t frame_index() const noexcept { return frame_index_; }

    // GXPeek and the pointer's depth field (separate submissions; they read the EFB as last submitted).
    [[nodiscard]] bool peek_efb(std::uint16_t x, std::uint16_t y, EfbPeekKind kind, std::uint32_t& value);
    [[nodiscard]] bool capture_pointer_depth(std::span<std::uint32_t> pixels);
    [[nodiscard]] std::uint64_t submit_pointer_depth_capture();
    [[nodiscard]] bool record_pointer_depth_capture_in_frame();
    [[nodiscard]] std::uint64_t pointer_depth_capture_fence() const noexcept { return pointer_depth_fence_; }
    [[nodiscard]] bool try_complete_pointer_depth_capture_in_place(const std::uint8_t*& rgba);
    // Asynchronous EFB fields, one per channel (GxBackend binds a channel to a peek kind and draw-sync token).
    [[nodiscard]] std::uint64_t submit_async_peek_field(std::size_t channel, EfbPeekKind kind, std::size_t slot);
    // The same capture recorded in the open frame where the stream is now (a draw-sync token); fenced at end_frame.
    [[nodiscard]] bool record_async_peek_field_in_frame(std::size_t channel, EfbPeekKind kind, std::size_t slot);
    [[nodiscard]] bool try_complete_async_peek_field(std::size_t channel);
    [[nodiscard]] const std::uint8_t* async_peek_field_rgba(std::size_t channel, std::size_t slot) const noexcept;
    [[nodiscard]] bool async_peek_field_pending(std::size_t channel) const noexcept {
        return async_fields_[channel].fence != 0u;
    }
    [[nodiscard]] bool any_async_peek_field_pending() const noexcept {
        for (const AsyncField& field : async_fields_) {
            if (field.fence != 0u) return true;
        }
        return false;
    }

    // Test captures: the EFB and the presented backbuffer as PPM files (paths from set_debug_capture_paths).
    bool debug_copy_efb_to_readback();
    void debug_log_readback();
    void debug_log_backbuffer_readback();
    bool debug_copy_selected_xfb_to_readback(const XfbTexture&) noexcept { return false; }
    void debug_log_selected_xfb_readback() noexcept {}
    void set_debug_capture_paths(std::string efb_ppm_path, std::string backbuffer_ppm_path);

    [[nodiscard]] gpu::Device* device() const noexcept { return vk::Context::get().device(); }
    [[nodiscard]] unsigned efb_scale() const noexcept { return efb_scale_; }
    [[nodiscard]] const vk::GxLayout* root_signature() const noexcept { return &gx_layout_; }
    [[nodiscard]] double display_refresh_hz() const noexcept { return display_refresh_hz_; }
    [[nodiscard]] UploadRing& vertex_ring() noexcept { return vertex_ring_; }
    [[nodiscard]] UploadRing& index_ring() noexcept { return index_ring_; }
    [[nodiscard]] UploadRing& constant_ring() noexcept { return constant_ring_; }
    [[nodiscard]] UploadRing& matrix_ring() noexcept { return matrix_ring_; }
    [[nodiscard]] unsigned frame_slot() const noexcept { return frame_slot_; }
    [[nodiscard]] std::uint64_t efb_copy_dest_bytes() const noexcept { return efb_copy_dests_bytes_; }
    [[nodiscard]] static std::uint64_t efb_copy_dest_budget_bytes() { return gx::efb_copy_dest_budget_bytes(); }
    [[nodiscard]] std::uint64_t last_latency_wait_us() const noexcept { return 0u; }
    [[nodiscard]] std::uint64_t last_frame_slot_wait_us() const noexcept { return last_frame_slot_wait_us_; }
    [[nodiscard]] std::uint64_t last_timestamp_read_us() const noexcept { return 0u; }
    [[nodiscard]] std::uint64_t last_begin_reset_us() const noexcept { return last_begin_reset_us_; }
    [[nodiscard]] gpu::Table default_sampler_table() const noexcept { return default_sampler_table_; }
    [[nodiscard]] gpu::Table sampler_table_for(const std::uint32_t keys[8]);

    // UiCanvas: the settings overlay, the HOME Menu and the start hint, drawn into the backbuffer.
    void ensure_native_settings_corner_texture_loaded() override;
    void draw_ui_textured_quad(int x, int y, int w, int h, float u0, float v0, float u1, float v1,
                               const std::array<float, 4>& tint) override;
    void draw_ui_solid_quad(int x, int y, int w, int h, const std::array<float, 4>& color) override;
    void draw_ui_glyph(int x, int y, int scale, std::uint64_t bitmap, const std::array<float, 4>& color) override;

private:
    struct Frame {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer commands = VK_NULL_HANDLE;
        VkCommandBuffer uploads = VK_NULL_HANDLE;
        VkDescriptorPool descriptors = VK_NULL_HANDLE;
        std::vector<VkDescriptorPool> overflow_descriptors;  // when a frame outgrows its pool
        VkSemaphore acquired = VK_NULL_HANDLE;
        std::uint64_t fence = 0;  // the submission that used this slot last
        std::vector<gpu::TextureRef> retired;  // kept until the slot's fence passes
    };
    struct Swapchain {
        VkSwapchainKHR handle = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkExtent2D extent{};  // the window as it is displayed
        // Pre-rotation: on a display turned from its natural orientation (Android), the images keep the natural
        // orientation (`image`, extent's sides swapped for a quarter turn) and the present pass turns the picture
        // into them by `transform`, so the compositor shows them as they are.
        VkExtent2D image{};
        VkSurfaceTransformFlagBitsKHR transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
        bool copyable = false;  // transfer source (test snapshots of the backbuffer)
        std::vector<VkImage> images;
        std::vector<VkImageView> views;
        std::vector<VkFramebuffer> framebuffers;
        std::vector<VkSemaphore> rendered;  // per image: the frame's commands finished writing it
    };
    struct EfbCopyDest {
        gpu::TextureRef texture;
        unsigned width = 0;
        unsigned height = 0;
        std::uint64_t bytes = 0;
        std::uint64_t last_used = 0;
    };
    struct Readback {
        std::shared_ptr<vk::Buffer> buffer;  // host-visible, mapped
        std::uint64_t fence = 0;             // 0: idle
    };
    struct AsyncField {
        gpu::TextureRef target;
        std::array<Readback, kAsyncPeekFieldSlots> readbacks;
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer commands = VK_NULL_HANDLE;
        std::uint64_t fence = 0;
        std::size_t pending_slot = 0;
    };

    // Device objects.
    bool create_surface_and_device();
    bool create_swapchain();
    void destroy_swapchain() noexcept;
    // The window's surface withdrawn (host_window::surface_withdrawn) or lost, and made again once it is back.
    void release_surface() noexcept;
    [[nodiscard]] bool restore_surface();
    [[nodiscard]] bool surface_changed() const;
    bool create_layouts();
    bool create_efb_targets(unsigned scale);
    [[nodiscard]] bool efb_scale_fits_memory(unsigned scale) const;
    void apply_pending_efb_scale();
    void carry_over_efb_contents();
    // Records `record` into a one-time command buffer, submits it and waits (start and resolution changes only).
    void run_one_time(const std::function<void(VkCommandBuffer)>& record);
    bool create_utility_pipelines();
    // A pipeline of the renderer's own passes: no vertex input, `strip` four-vertex quads or full-screen triangles,
    // blended over the target when `blend`, depth written (always passing) when `depth_write`.
    [[nodiscard]] VkPipeline create_utility_pipeline(VkRenderPass pass, VkPipelineLayout layout,
                                                     std::string_view vs_source, std::string_view ps_source,
                                                     VkColorComponentFlags write_mask, bool blend, bool strip,
                                                     bool depth_write);
    [[nodiscard]] VkShaderModule utility_module(std::string_view hlsl, vk::ShaderStage stage, bool conversion);

    // Frame recording.
    void begin_efb_pass();
    void end_efb_pass();
    void reset_gx_bindings() noexcept;
    [[nodiscard]] VkDescriptorSet allocate_frame_set(VkDescriptorSetLayout layout);
    [[nodiscard]] VkDescriptorSet allocate_persistent_set(VkDescriptorSetLayout layout);
    void refresh_default_sampler_table();
    [[nodiscard]] VkDescriptorSet constants_set(VkBuffer b0, VkBuffer b1, VkBuffer palette);
    [[nodiscard]] VkSampler sampler_for(std::uint32_t key, const RenderConfig& cfg);
    void record_conversion_blit(vk::Texture& dest, unsigned dest_w, unsigned dest_h, const EfbCopyParams& params,
                                bool is_depth_copy, bool intensity, bool keep_alpha, std::uint8_t copy_format,
                                float src_x, float src_y, float step_x, float step_y, float filter_row_offset);
    void record_peek_field(VkCommandBuffer commands, vk::Texture& target, EfbPeekKind kind,
                           const EfbConversionShaderConstants& constants, unsigned width, unsigned height,
                           VkBuffer readback);
    void transition_efb_for_sampling(VkCommandBuffer commands, bool depth);
    void transition_efb_to_attachment(VkCommandBuffer commands, bool depth);
    [[nodiscard]] VkFramebuffer framebuffer_for(vk::Texture& texture);
    void refresh_display_rate();
    void follow_window_and_resize();
    void update_gpu_budget();
    void report_present_stats();
    [[nodiscard]] std::uint64_t submit_one_time(VkCommandBuffer commands);

    WindowOwner window_owner_;
    host_window::NativeWindow window_ = nullptr;
    bool window_shown_ = false;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    Swapchain swapchain_;
    bool swapchain_dirty_ = false;
    bool surface_lost_ = false;  // the driver reported it: made again from the window at the next frame
    std::uint32_t swap_image_ = UINT32_MAX;  // acquired for this frame's present
    // host_window::surface_use, from acquiring this frame's image until it is presented.
    std::unique_lock<std::timed_mutex> surface_lock_;

    std::array<Frame, kFramesInFlight> frames_{};
    unsigned frame_slot_ = 0;
    std::uint64_t frame_index_ = 0;
    std::uint64_t last_submitted_ = 0;

    // Layouts and passes.
    vk::GxLayout gx_layout_{};
    VkDescriptorSetLayout utility_set_layout_ = VK_NULL_HANDLE;  // t0, s0 (linear clamp)
    VkDescriptorSetLayout conversion_set_layout_ = VK_NULL_HANDLE;  // t0, s0 (linear), s1 (point)
    VkPipelineLayout utility_layout_ = VK_NULL_HANDLE;      // utility set + 64 bytes of push constants
    VkPipelineLayout conversion_layout_ = VK_NULL_HANDLE;   // conversion set + 64 bytes of push constants
    VkRenderPass color_pass_ = VK_NULL_HANDLE;    // one RGBA8 target, ends shader-readable
    VkRenderPass present_pass_ = VK_NULL_HANDLE;  // the swapchain image, cleared, ends presentable
    VkSampler linear_clamp_ = VK_NULL_HANDLE;
    VkSampler point_clamp_ = VK_NULL_HANDLE;
    VkDescriptorPool persistent_pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> persistent_overflow_;

    // The EFB.
    unsigned efb_scale_ = 1;
    VkFormat depth_format_ = VK_FORMAT_UNDEFINED;
    bool depth_linear_ = true;  // depth_format_ can be sampled with linear filtering
    gpu::TextureRef efb_color_;
    gpu::TextureRef efb_depth_;  // its sampled view is the depth aspect alone
    VkFramebuffer efb_framebuffer_ = VK_NULL_HANDLE;
    gpu::TextureRef efb_carry_color_;  // the previous targets, read once after a resolution change
    gpu::TextureRef efb_carry_depth_;
    VkDescriptorSet efb_color_source_ = VK_NULL_HANDLE;  // conversion inputs
    VkDescriptorSet efb_depth_source_ = VK_NULL_HANDLE;
    bool efb_pass_open_ = false;
    bool efb_seeded_ = false;

    // Pipelines the renderer owns.
    VkPipeline conversion_pipeline_ = VK_NULL_HANDLE;
    VkPipeline clear_rgb_pipeline_ = VK_NULL_HANDLE;
    VkPipeline clear_alpha_pipeline_ = VK_NULL_HANDLE;
    // Whole-colour and depth clears as draws (DeviceQuirks::clear_with_draw only).
    VkPipeline clear_rgba_pipeline_ = VK_NULL_HANDLE;
    VkPipeline clear_depth_pipeline_ = VK_NULL_HANDLE;
    VkPipeline blit_pipeline_ = VK_NULL_HANDLE;
    VkPipeline ui_solid_pipeline_ = VK_NULL_HANDLE;
    VkPipeline ui_textured_pipeline_ = VK_NULL_HANDLE;
    VkPipeline depth_carry_pipeline_ = VK_NULL_HANDLE;
    VkPipeline color_carry_pipeline_ = VK_NULL_HANDLE;
    gpu::TextureRef corner_texture_;
    VkDescriptorSet corner_set_ = VK_NULL_HANDLE;
    bool corner_texture_tried_ = false;

    // GX draws.
    UploadRing vertex_ring_;
    UploadRing index_ring_;
    UploadRing constant_ring_;
    UploadRing matrix_ring_;
    struct ConstantsSetKey {
        VkBuffer b0, b1, palette;
        bool operator==(const ConstantsSetKey&) const = default;
    };
    struct ConstantsSetKeyHash {
        std::size_t operator()(const ConstantsSetKey& key) const noexcept;
    };
    std::unordered_map<ConstantsSetKey, VkDescriptorSet, ConstantsSetKeyHash> constants_sets_;
    std::unordered_map<std::uint64_t, VkSampler> samplers_;  // sampler key << 32 | config key
    std::unordered_map<SamplerTableKey, VkDescriptorSet, SamplerTableKeyHasher> sampler_tables_;
    gpu::Table default_sampler_table_{};
    std::uint32_t default_sampler_config_ = 0;
    TextureCache* textures_ = nullptr;
    struct Bound {
        gpu::Pipeline* pipeline = nullptr;
        std::array<VkBuffer, 3> buffers{};  // b0, b1, the palette
        std::array<std::uint32_t, 3> offsets{};
        bool constants_valid = false;
        std::uint64_t texture_table = 0;
        std::uint64_t sampler_table = 0;
        gpu::Address vertex_address = ~gpu::Address{0};
        gpu::Address index_address = ~gpu::Address{0};
        VkViewport viewport{};
        bool viewport_valid = false;
        VkRect2D scissor{};
        bool scissor_valid = false;
        gpu::Rect scissor_input{};
        bool scissor_input_valid = false;
    } bound_;

    // EFB copies.
    std::unordered_map<std::uint64_t, EfbCopyDest> efb_copy_dests_;
    std::uint64_t efb_copy_dests_bytes_ = 0;
    std::uint64_t efb_copy_dests_tick_ = 0;

    // Peeks.
    gpu::TextureRef peek_target_;  // 1x1, and the 640x528 pointer field below
    gpu::TextureRef field_target_;
    Readback peek_readback_;
    static constexpr std::size_t kPointerDepthRing = 3u;
    std::array<Readback, kPointerDepthRing> pointer_depth_ring_;
    std::size_t pointer_depth_next_ = 0;
    const std::uint8_t* pointer_depth_rgba_ = nullptr;
    const vk::Buffer* pointer_depth_readback_ = nullptr;
    std::uint64_t pointer_depth_fence_ = 0;
    bool pointer_depth_in_frame_ = false;
    VkCommandPool utility_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer utility_commands_ = VK_NULL_HANDLE;
    std::array<AsyncField, kAsyncPeekFieldChannels> async_fields_{};
    // AsyncField::fence of a field recorded in the open frame.
    static constexpr std::uint64_t kAsyncPeekInFrame = ~std::uint64_t{0};
    void ensure_async_field(AsyncField& field);

    // Captures and telemetry.
    std::string efb_capture_path_;
    std::string backbuffer_capture_path_;
    Readback efb_capture_;
    std::string efb_capture_pending_path_;
    unsigned efb_capture_width_ = 0;
    unsigned efb_capture_height_ = 0;
    Readback backbuffer_capture_;
    std::string backbuffer_capture_pending_path_;
    unsigned backbuffer_capture_width_ = 0;
    unsigned backbuffer_capture_height_ = 0;
    bool backbuffer_capture_bgra_ = false;
    std::atomic<double> measured_present_hz_{0.0};
    double display_refresh_hz_ = 0.0;
    std::uint64_t display_refresh_checks_ = 0;
    std::int64_t measured_present_window_qpc_ = 0;
    std::uint64_t measured_present_count_ = 0;
    std::uint64_t last_frame_slot_wait_us_ = 0;
    std::uint64_t last_begin_reset_us_ = 0;
    unsigned replay_failure_ = 0;
    // Match Output: the window and picture shape the scale was last derived for, and the GPU-time cap
    // (RenderConfig::gpu_budget).
    std::uint32_t follow_window_width_ = 0;
    std::uint32_t follow_window_height_ = 0;
    double follow_window_aspect_ = 0.0;
    std::uint64_t follow_window_retry_ms_ = 0;
    unsigned gpu_budget_cap_ = 0;  // 0: none
    double gpu_budget_ms_ = 0.0;
    std::int64_t gpu_budget_window_qpc_ = 0;
    unsigned gpu_budget_over_windows_ = 0;
    PresentPacer present_pacer_;
    struct PresentStats {
        std::int64_t window_start_qpc = 0;
        std::int64_t last_present_qpc = 0;
        std::uint64_t presents = 0;
        double interval_ms = 0.0;
        double interval_max_ms = 0.0;
        std::uint64_t intervals = 0;
        double pace_wait_ms = 0.0;
        double pace_wait_max_ms = 0.0;
        double present_ms = 0.0;
        double present_max_ms = 0.0;
        double gpu_ms = 0.0;
        std::uint64_t gpu_samples = 0;
    } stats_;
    VkQueryPool timestamps_ = VK_NULL_HANDLE;
    std::array<bool, kFramesInFlight> timestamp_pending_{};
    double timestamp_period_ns_ = 0.0;
};

using Renderer = RendererVk;

}  // namespace galaxy::gx
