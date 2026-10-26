// SPDX-License-Identifier: GPL-3.0-only
// The GX renderer on Vulkan (docs/VULKAN.md), with the interface GxBackend uses on RendererD3D12.
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
// Temporal upscaling, ambient occlusion and ray tracing are D3D12 only: their entry points here report themselves
// unavailable.
#pragma once

#include "galaxy/gx/efb_copy.h"
#include "galaxy/gx/gpu_types.h"
#include "galaxy/gx/interp_gpu.h"
#include "galaxy/gx/interp_types.h"
#include "galaxy/gx/renderer_common.h"
#include "galaxy/gx/rt_materials.h"
#include "galaxy/gx/rt_sky_sun.h"
#include "galaxy/gx/rt_types.h"
#include "galaxy/gx/transform_history.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx/ui_canvas.h"
#include "galaxy/gx/upload_ring.h"
#include "galaxy/gx/vk/game_bloom_vk.h"
#include "galaxy/gx/vk/post_process_vk.h"
#include "galaxy/gx/vk/shader_compile_vk.h"
#include "galaxy/gx/vk/vk_context.h"
#include "galaxy/gx/host_window.h"
#include "galaxy/gx/window_owner.h"
#include "galaxy/gx_d3d12.h"

#include <array>
#include <atomic>
#include <bit>
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
    // The same pass clearing both targets as it begins (a whole-EFB copy-clear): nothing loaded from memory, which a
    // tiled GPU otherwise reads back at the full internal resolution. Compatible with efb_pass (load ops only).
    VkRenderPass efb_pass_clear = VK_NULL_HANDLE;
    // MSAA: the EFB passes over multisampled targets (index: log2 of the samples per pixel, 1 to 3), the colour
    // resolved into the single-sample target as each pass ends. Index 0 is efb_pass / efb_pass_clear.
    std::array<VkRenderPass, 4> ms_passes{};
    std::array<VkRenderPass, 4> ms_passes_clear{};
    [[nodiscard]] VkRenderPass pass_for(unsigned samples_log2, bool clear) const noexcept {
        if (samples_log2 == 0u) {
            return clear ? efb_pass_clear : efb_pass;
        }
        return clear ? ms_passes_clear[samples_log2] : ms_passes[samples_log2];
    }
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    bool geometry_shaders = false;
    bool tessellation = false;  // curved surfaces' hull and domain stages
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

    // Interpolated sub-frames (renderer_vk_replay.cpp): each game frame's draws, clears and copies are logged;
    // a sub-frame replays the log with the constants evaluated at its t (interp_eval.h) into a private EFB and
    // presents its display copy. The interface is RendererD3D12's. Scene AA and ray tracing are inert here.
    using ReplaySequence = gx::ReplaySequence;
    using SubframeTime = gx::SubframeTime;
    using CopyRedirectStats = gx::CopyRedirectStats;
    using StripeVertex = gx::StripeVertex;
    [[nodiscard]] std::uint64_t playing_serial() const noexcept { return run_log_.serial; }
    void prepare_vertex_blend(std::span<const std::int32_t> previous_record);
    [[nodiscard]] std::span<const std::int32_t> ps_record_pairs() const noexcept { return run_ps_previous_; }
    // SMAA or FXAA of the 3D scene in the EFB before the 2D layer draws (PostProcessorVk).
    void record_scene_aa();
    // Ambient occlusion (D3D12's ambient_occlusion.h) is not on Vulkan yet.
    [[nodiscard]] bool ambient_occlusion_ready() const noexcept { return false; }
    void set_scene_ao_view(const RtView&) noexcept {}
    void record_scene_ao() noexcept {}
    // The game's bloom through the renderer (galaxy/motion_signals.h kMarkerBloom; game_bloom_vk.h): until its
    // passes have compiled, the game draws its own chains.
    [[nodiscard]] bool game_bloom_ready() const noexcept { return bloom_.ready(); }
    void game_bloom(std::uint16_t phase, std::uint32_t a, std::uint32_t b);
    [[nodiscard]] unsigned replay_reject() const noexcept { return replay_reject_; }
    void reset_subframe_gpu_ema() noexcept { subframe_gpu_ema_ms_ = 0.0; }
    [[nodiscard]] double subframe_gpu_ema_ms() const noexcept { return subframe_gpu_ema_ms_; }
    void suspend_temporal_interpolation(double) {}
    [[nodiscard]] bool temporal_interpolation() const noexcept { return false; }
    bool begin_replay_sequence(const ReplaySequence& sequence);
    bool present_replay_subframe(const SubframeTime& time, const Affine34& camera_t, const Affine34& camera_prev);
    void note_stripe_vertices(std::span<const StripeVertex> vertices);
    [[nodiscard]] CopyRedirectStats take_copy_redirect_stats() noexcept { return std::exchange(redirect_stats_, {}); }
    void set_subframe_copy_redirect(int) noexcept {}
    [[nodiscard]] std::uint64_t take_log_gap_ops() noexcept { return 0u; }
    [[nodiscard]] std::uint64_t take_vertex_blend_draws() noexcept { return 0u; }
    void take_sequence_record_ms(double& total_ms, std::uint64_t& samples, std::uint64_t& replays) noexcept {
        total_ms = std::exchange(sequence_record_ms_, 0.0);
        samples = std::exchange(sequence_record_samples_, 0u);
        replays = std::exchange(replay_records_, 0u);
    }
    [[nodiscard]] std::uint64_t take_subframe_guarded() noexcept { return 0u; }
    void take_subframe_path_gpu_ms(std::array<double, 3>& total_ms, std::array<std::uint64_t, 3>& samples) noexcept {
        total_ms = {};
        samples = {};
    }
    [[nodiscard]] std::array<double, 3> take_subframe_phase_ms() noexcept { return {}; }
    void take_subframe_record_ms(double& total_ms, std::uint64_t& samples) noexcept {
        total_ms = std::exchange(subframe_record_ms_, 0.0);
        samples = std::exchange(subframe_record_samples_, 0u);
    }
    void set_capture_waits(bool) noexcept {}
    // The resident decoded-vertex buffer (blend pairs may live in it); 0 for none.
    void set_resident_vertices(gpu::Address base, std::uint64_t bytes) noexcept {
        resident_vertex_base_ = base;
        resident_vertex_bytes_ = bytes;
    }
    void set_scene_view(const std::array<float, 12>*) noexcept {}
    [[nodiscard]] bool rt_active() const noexcept { return false; }
    [[nodiscard]] bool rt_scene_open() const noexcept { return false; }
    [[nodiscard]] bool rt_glow_open() const noexcept { return false; }
    [[nodiscard]] bool rt_water_open() const noexcept { return false; }
    [[nodiscard]] bool rt_late_open() const noexcept { return false; }
    [[nodiscard]] bool rt_sky_open() const noexcept { return false; }
    void rt_begin_sky() noexcept {}
    void rt_set_scene_up(const std::array<float, 3>&) noexcept {}
    [[nodiscard]] bool rt_lighting() const noexcept { return false; }
    [[nodiscard]] bool rt_ab_original() const noexcept { return false; }
    void rt_set_ab_original(bool) noexcept {}
    [[nodiscard]] bool rt_reflections_active() const noexcept { return false; }
    [[nodiscard]] bool rt_engine() const noexcept { return false; }
    [[nodiscard]] bool rt_engine_clear() const noexcept { return false; }
    [[nodiscard]] bool rt_log_seq() const noexcept { return false; }
    [[nodiscard]] bool rt_scene_untraced() const noexcept { return false; }
    void rt_set_scene_untraced(bool) noexcept {}
    void begin_hdr_scene() noexcept {}
    void end_hdr_scene() noexcept {}
    void rt_begin_scene() {}
    void rt_capture_picture() {}
    void rt_trace_scene() {}
    void rt_trace_water() {}
    void rt_hold_sun(bool) noexcept {}
    void rt_reset_sky() noexcept {}
    void rt_set_camera(const std::array<float, 12>*) noexcept {}
    void rt_set_view(const RtView&) noexcept {}
    [[nodiscard]] bool rt_physical() const noexcept { return false; }
    void rt_set_presents(std::uint64_t) noexcept {}
    void rt_set_key_light(bool, const std::array<float, 3>&, const std::array<float, 3>&, float, bool) noexcept {}
    void rt_set_stage_sun(const RtStageSun*) noexcept {}
    [[nodiscard]] const RtStageSun* rt_stage_sun() const noexcept { return nullptr; }
    [[nodiscard]] bool rt_sky_key(std::array<float, 3>&, float&) const noexcept { return false; }
    [[nodiscard]] bool rt_sky_sun(std::array<float, 3>&, std::array<float, 3>&) const noexcept { return false; }
    [[nodiscard]] bool rt_sun_direction(std::array<float, 3>&) const noexcept { return false; }
    void rt_note_draw(const DrawCall&, const RtDrawInfo&) {}
    [[nodiscard]] float rt_glow_exposure(bool = false) const noexcept { return 1.0f; }
    [[nodiscard]] const void* rt_light_texture() const noexcept { return nullptr; }
    [[nodiscard]] bool rt_light_texture_ready() const noexcept { return false; }
    [[nodiscard]] float rt_light_texture_scale() const noexcept { return 1.0f; }
    // The game-frame ring segment while interpolating (a log's resources live until it comes round), ~0u else.
    [[nodiscard]] unsigned replay_hold_segment() const noexcept {
        return ring_mode_ == RingMode::Interp ? ring_segment_ : ~0u;
    }
    // Diagnostics (GALAXY_INTERP_VERIFY): the next sub-frame's evaluated output, read back once its fence passed.
    void request_eval_readback() noexcept { eval_readback_requested_ = true; }
    [[nodiscard]] bool take_eval_readback(std::vector<std::uint32_t>& words, float& t);
    [[nodiscard]] bool temporal_mode() const { return false; }
    // The 3D image's scale over the Wii's 640 x 456 as it is shown (no temporal upscaler here: the EFB's).
    [[nodiscard]] unsigned scene_display_scale() const noexcept { return efb_scale_; }
    // A copy right after the display copy, from the output EFB the 2D layer drew in (the EFB itself without one).
    [[nodiscard]] bool copy_output_efb_to_texture(const EfbCopyParams& params, TextureCache& textures);

    [[nodiscard]] gpu::Table texture_table_for(const TextureHandle* handles, unsigned count, const TextureCache& cache,
                                               bool lit_table = false);  // no ray tracing on Vulkan yet
    void attach_texture_uploads(TextureCache& cache);
    void drain_gpu();
    void pump_messages() {}
    [[nodiscard]] bool quit_requested() const noexcept;

    void begin_frame(bool present_swap_chain = true);
    // D3D12 waits for its swap chain's frame latency here (a frame begun without a present that presents after all);
    // Vulkan paces at the image acquire in present().
    void wait_for_frame_latency_before_present() {}
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
    // `black`: the frame is presented black (the pass clears; the XFB is not drawn).
    bool present(const XfbTexture* source, bool capture_backbuffer = false, bool black = false);

    // The 2D layer at the window's resolution (hud_output_resolution): at the scene's end the EFB is resampled into
    // an EFB at the window's scale, which the 2D layer and the display copy then use, until end_output_efb.
    [[nodiscard]] bool begin_output_efb();
    void end_output_efb();
    [[nodiscard]] bool output_efb_active() const noexcept { return output_efb_active_; }
    // Not on Vulkan yet: temporal upscaling.
    [[nodiscard]] bool scene_temporal_begin(float&, float&) noexcept { return false; }
    [[nodiscard]] bool begin_motion_scene() noexcept { return false; }
    void record_motion_debug_view() noexcept {}
    void reset_temporal_history() noexcept {}
    [[nodiscard]] unsigned matrix_history_frames() const noexcept { return 0u; }
    [[nodiscard]] std::uint64_t matrix_segment_serial() const noexcept { return 0u; }
    // A completed log waits (interpolation mode); start_replay_playback makes it the playing one.
    [[nodiscard]] bool replay_available() const noexcept { return ready_valid_; }
    void set_present_game_frame(bool) noexcept {}
    bool start_replay_playback();
    [[nodiscard]] bool replay_playing() const noexcept { return run_valid_; }
    // A present rate above 60.
    [[nodiscard]] bool interpolation_mode() const;
    [[nodiscard]] std::uint64_t replay_serial() const noexcept { return replay_serial_; }
    [[nodiscard]] std::uint32_t replay_xfb_addr() const noexcept { return replay_xfb_addr_; }
    void note_replay_scene() noexcept { ++replay_scene_count_; }
    [[nodiscard]] unsigned replay_failure() const noexcept { return replay_failure_; }
    void note_replay_failure(unsigned reason) noexcept { replay_failure_ = reason; }
    void set_subframe_capture(bool capture) noexcept { subframe_capture_ = capture; }
    void take_subframe_gpu_ms(double& total_ms, std::uint64_t& samples) noexcept {
        total_ms = std::exchange(subframe_gpu_ms_, 0.0);
        samples = std::exchange(subframe_gpu_samples_, 0u);
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
    void set_debug_capture_paths(std::string efb_ppm_path, std::string backbuffer_ppm_path);

    [[nodiscard]] gpu::Device* device() const noexcept { return vk::Context::get().device(); }
    [[nodiscard]] unsigned efb_scale() const noexcept { return efb_scale_; }
    // log2 of the EFB's samples per pixel (MSAA; the output EFB has one): the GX pipelines' sample count.
    [[nodiscard]] std::uint8_t efb_samples_log2() const noexcept {
        return static_cast<std::uint8_t>(std::countr_zero(efb_.samples));
    }
    // The pipelines' EFB variant (shader_keys.h RenderStateKey::samples_log2): no HDR scene EFB here.
    [[nodiscard]] std::uint8_t efb_variant() const noexcept { return efb_samples_log2(); }
    [[nodiscard]] const vk::GxLayout* root_signature() const noexcept { return &gx_layout_; }
    [[nodiscard]] double display_refresh_hz() const noexcept { return display_refresh_hz_; }
    // The display's vblanks (host_window::vblank_grid): the latest and the interval, on galaxy::qpc_now()'s clock.
    [[nodiscard]] bool vblank_grid(std::int64_t& vblank, std::int64_t& period) const noexcept;
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
    bool create_efb_targets(unsigned scale, unsigned samples);
    // The MSAA sample count the settings ask for and the device has for the EFB's formats (1: none).
    [[nodiscard]] unsigned wanted_msaa_samples() const;
    // Before the EFB's single-sample depth is read: the multisampled depth drawn since the last resolve, resolved
    // into it (each pixel's sample 0). The colour resolves as each EFB pass ends.
    void resolve_efb_depth(VkCommandBuffer commands);
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
                                                     bool depth_write,
                                                     VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
    [[nodiscard]] VkShaderModule utility_module(std::string_view hlsl, vk::ShaderStage stage, bool conversion);

    // Frame recording.
    // `clear`: begins with both targets cleared to it (efb_pass_clear).
    void begin_efb_pass(const EfbClearValues* clear = nullptr);
    void end_efb_pass();
    void reset_gx_bindings() noexcept;
    [[nodiscard]] VkDescriptorSet allocate_frame_set(VkDescriptorSetLayout layout);
    // A post-process draw's set: the replay list's while one is recorded, else the frame's.
    [[nodiscard]] VkDescriptorSet allocate_post_set(VkDescriptorSetLayout layout);
    // Presentation post-processing: SMAA or FXAA of the scene, FSR 1 or the box resample, RCAS.
    vk::PostProcessorVk post_;
    vk::GameBloomVk bloom_;
    void sync_post_process();
    // The picture through the post chain before a turned swap chain's blit (pre-rotated images).
    gpu::TextureRef oriented_;
    void retire(gpu::TextureRef texture);
    [[nodiscard]] VkDescriptorSet allocate_persistent_set(VkDescriptorSetLayout layout);
    void refresh_default_sampler_table();
    // Set 0 for b0, b1, a palette binding of `palette_range` bytes from `palette_offset` (VK_WHOLE_SIZE: to the
    // buffer's end; b0, b1 and the palette take the draw's dynamic offsets), vertex blending's earlier vertices
    // (`earlier`, whole) and the verdicts.
    [[nodiscard]] VkDescriptorSet constants_set(VkBuffer b0, VkBuffer b1, VkBuffer palette, VkDeviceSize palette_offset,
                                                VkDeviceSize palette_range, VkBuffer earlier);
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
    Frame* frame_ = &frames_[0];  // the submission being recorded: the frame slot's, or a sub-frame's
    VkCommandBuffer commands_ = VK_NULL_HANDLE;  // where GX work records: the frame's, or a replay's
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
    // An EFB: its targets, their framebuffer and the conversion inputs reading them. The live one, and the private
    // one sub-frames replay into (the chunks of the game frame being drawn keep the live one between them).
    struct EfbSet {
        gpu::TextureRef color;
        gpu::TextureRef depth;  // its sampled view is the depth aspect alone
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkDescriptorSet color_source = VK_NULL_HANDLE;
        VkDescriptorSet depth_source = VK_NULL_HANDLE;
        // MSAA (samples above one): the multisampled targets the EFB pass draws into, color and depth above being
        // their resolves; resolve_framebuffer (efb_pass) holds color and depth for the depth resolve.
        unsigned samples = 1u;
        gpu::TextureRef ms_color;
        gpu::TextureRef ms_depth;
        VkFramebuffer resolve_framebuffer = VK_NULL_HANDLE;
        VkDescriptorSet ms_depth_source = VK_NULL_HANDLE;
        bool depth_dirty = false;
    };
    EfbSet efb_;
    // The output EFB (begin_output_efb) and its scale; they trade places with efb_ and efb_scale_ while it is active.
    EfbSet alt_efb_;
    unsigned alt_efb_scale_ = 0;
    bool output_efb_active_ = false;
    [[nodiscard]] unsigned output_efb_scale_for_window() const;
    bool enter_output_efb(unsigned scale);
    void swap_efb_sets();
    gpu::TextureRef efb_carry_color_;  // the previous targets, read once after a resolution change
    gpu::TextureRef efb_carry_depth_;
    bool efb_carry_clear_ = false;  // the new targets start cleared (MSAA before or after the change)
    bool efb_pass_open_ = false;
    bool present_black_ = false;  // present()'s `black`, for present_xfb
    bool efb_seeded_ = false;

    // Pipelines the renderer owns.
    VkPipeline conversion_pipeline_ = VK_NULL_HANDLE;
    // The renderer's own draws into the EFB pass, per EFB sample count (log2), made at first use: masked clears,
    // and whole-colour and depth clears as draws (DeviceQuirks::clear_with_draw only; null otherwise).
    enum class EfbUtility : unsigned { ClearRgb, ClearAlpha, ClearRgba, ClearDepth, Count };
    std::array<std::array<VkPipeline, 4>, static_cast<std::size_t>(EfbUtility::Count)> efb_utility_pipelines_{};
    [[nodiscard]] VkPipeline efb_utility_pipeline(EfbUtility kind);
    unsigned msaa_max_samples_ = 1u;  // create_layouts: the most the EFB's formats render and sample
    VkPipeline depth_resolve_pipeline_ = VK_NULL_HANDLE;
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
        VkBuffer b0, b1, palette, earlier;
        bool whole_palette;  // the palette binding reaches the buffer's end (palettes addressed from its base)
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
        VkBuffer earlier = VK_NULL_HANDLE;   // vertex blending's earlier vertices (set 0)
        std::array<std::uint32_t, 5> blend{};  // vertex blending's push constants
        bool blend_valid = false;
    } bound_;

    // EFB copies.
    std::unordered_map<std::uint64_t, EfbCopyDest> efb_copy_dests_;
    std::uint64_t efb_copy_dests_bytes_ = 0;
    std::uint64_t efb_copy_dests_tick_ = 0;

    // Peeks.
    gpu::TextureRef peek_target_;  // 1x1, and the 640x528 pointer field below
    gpu::TextureRef field_target_;
    // Each peek target's linear twin (DeviceQuirks::linear_readback), made on first use.
    std::unordered_map<const vk::Texture*, gpu::TextureRef> linear_twins_;
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

    // Interpolated sub-frames (renderer_vk_replay.cpp).
    // Ring mode: in Interp the upload rings, texture-table descriptors and retired textures are segmented per game
    // frame (kInterpSegments), so a logged frame's data lives until its segment comes round again.
    enum class RingMode : std::uint8_t { Normal, Interp };
    struct Segment {
        VkDescriptorPool descriptors = VK_NULL_HANDLE;
        std::vector<VkDescriptorPool> overflow;
        std::vector<gpu::TextureRef> retired;
        std::vector<gpu::TextureRef> snapshots;  // its logs' copy snapshots, reused when it comes round
        std::uint64_t fence = 0;  // the last submission that read the segment
    };
    void update_ring_mode();
    void advance_ring_segment();
    [[nodiscard]] VkDescriptorSet allocate_table_set(VkDescriptorSetLayout layout);
    RingMode ring_mode_ = RingMode::Normal;
    // An upload ring could not grow (out of upload memory): no further attempts.
    bool ring_growth_failed_ = false;
    std::array<Segment, kInterpSegments> segments_{};
    unsigned ring_segment_ = 0;
    bool ring_advance_pending_ = false;

    // The log: one game frame's GX work in order (a display copy closes it).
    // OutputEfbBegin: the scene resampled into the output EFB at the op's scale; SwapEfbSets: the EFB and the output
    // EFB trade places.
    enum class ReplayKind : std::uint8_t { Draw, BindEfb, Clear, Copy, SceneAa, OutputEfbBegin, SwapEfbSets, Bloom };
    struct ReplayOp {
        ReplayKind kind = ReplayKind::Draw;
        std::uint32_t index = 0;
    };
    struct ReplayViewport {
        bool valid = false;
        float values[6]{};
    };
    struct ReplayClear {
        std::uint32_t ar = 0, gb = 0, z24 = 0;
        bool color = false, alpha = false, depth = false;
        unsigned x = 0, y = 0, w = 0, h = 0;
        std::uint8_t format = 0;
    };
    struct ReplayCopy {
        EfbCopyParams params{};
        bool depth = false;
        bool display = false;            // the display copy (closes the log)
        std::uint64_t dest_key = 0;      // a texture copy's destination (efb_copy_dests_)
        unsigned width = 0, height = 0;  // the destination's size
        float geometry[5]{};             // sampling: src x, y, step x, y, filter row offset
        bool exact = false;              // an identity copy (vkCmdCopyImage)
        unsigned box_left = 0, box_top = 0;
        VkImageView dest_view = VK_NULL_HANDLE;  // the destination's view as the log's tables name it
    };
    // The game's bloom marker (its phase and values).
    struct ReplayBloom {
        std::uint16_t phase = 0;
        std::uint32_t a = 0, b = 0;
    };
    void record_game_bloom(const ReplayBloom& bloom);
    struct ReplayLog {
        std::vector<ReplayOp> ops;
        std::vector<DrawCall> draws;
        std::vector<ReplayViewport> viewports;
        std::vector<ReplayClear> clears;
        std::vector<ReplayCopy> copies;
        std::vector<ReplayBloom> blooms;
        // The views of each texture table its draws bound (stand-ins replace copy destinations in them).
        std::unordered_map<std::uint64_t, std::array<VkImageView, kMaxTextureMaps>> tables;
        unsigned segment = 0;
        std::uint64_t serial = 0;
        float surround = 0.0f;  // the display copy's XFB: its surround and movie flag (the sub-frame XFB's)
        bool movie = false;
        // Copy textures this log read before rewriting them, as they were at its start (the replay reads these).
        std::vector<std::pair<VkImageView, gpu::TextureRef>> snapshots;
        std::vector<VkImageView> written;  // copy destinations its copies wrote
        // JPA stripe particles (note_stripe_vertices): each stripe draw's vertices, by draw index.
        struct StripeDraw {
            std::uint32_t draw = 0, first = 0, count = 0;
        };
        std::vector<StripeDraw> stripe_draws;
        std::vector<StripeVertex> stripe_vertices;
        void clear();
    };
    void begin_replay_record();
    // Copy textures read before the frame rewrites them (feedback: blur trails, cut covers), with the logs that read
    // them lately: each log's start snapshots them.
    struct Feedback {
        VkImageView view = VK_NULL_HANDLE;
        gpu::TextureRef texture;
        std::uint64_t serial = 0;  // the last log that read it so
    };
    std::vector<Feedback> feedback_;
    void note_feedback_reads(const std::array<VkImageView, kMaxTextureMaps>& views);
    void take_log_snapshots();
    // The display copy closes the log: one 3D scene makes it replayable.
    void close_replay_log(std::uint32_t xfb_addr);
    // GX work recorded now goes into the log (not while a replay records, nor inside a logged operation's own
    // clears and rebinds: LogPause).
    [[nodiscard]] bool logging() const noexcept { return recording_log_valid_ && !replaying_ && !log_paused_; }
    struct LogPause {
        RendererVk& renderer;
        bool was;
        explicit LogPause(RendererVk& r) : renderer(r), was(r.log_paused_) { r.log_paused_ = true; }
        ~LogPause() { renderer.log_paused_ = was; }
    };
    bool log_paused_ = false;
    // A copy-clear right after the display copy (outside any log) starts the next log.
    ReplayClear start_clear_{};
    bool start_clear_valid_ = false;
    ReplayLog recording_log_;  // being recorded
    ReplayLog ready_log_;      // complete, waiting
    ReplayLog run_log_;        // playing
    ReplayLog prev_log_;       // the one played before it (draw pairing)
    bool recording_log_valid_ = false;
    bool ready_valid_ = false;
    bool run_valid_ = false;
    bool prev_valid_ = false;
    std::vector<std::uint32_t> pairing_scratch_;
    unsigned replay_scene_count_ = 0;
    unsigned replay_reject_ = 0;
    std::uint64_t replay_serial_ = 0;
    std::uint32_t replay_xfb_addr_ = 0;
    std::vector<std::int32_t> run_ps_previous_;
    // Vertex blending (prepare_vertex_blend): per playing draw its earlier vertices and verdict slot; the pairs the
    // GPU checks (current offset, earlier offset, vertices, buffers); stripe vertices gathered from the log before.
    struct BlendSource {
        gpu::Address vertices = 0;
        std::uint32_t slot = 0;
    };
    std::vector<BlendSource> run_blend_vertices_;
    std::vector<std::array<std::uint32_t, 4>> blend_pairs_;
    std::vector<std::array<std::uint32_t, 2>> stripe_gather_;  // per gathered vertex: byte offset, buffer
    std::vector<std::pair<std::uint64_t, std::uint32_t>> stripe_keys_;
    std::vector<std::int32_t> stripe_matched_;
    std::vector<std::int32_t> stripe_of_draw_;
    std::vector<std::int32_t> stripe_of_prev_draw_;
    bool blend_validate_pending_ = false;
    gpu::Address resident_vertex_base_ = 0;
    std::uint64_t resident_vertex_bytes_ = 0;
    std::shared_ptr<vk::Buffer> stripe_earlier_;  // the gathered earlier stripe vertices (144 bytes each)
    bool ensure_blend_pipelines();
    void record_blend_validation(VkCommandBuffer commands);
    VkDescriptorSetLayout blend_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout blend_layout_ = VK_NULL_HANDLE;
    VkPipeline blend_validate_pipeline_ = VK_NULL_HANDLE;
    VkPipeline stripe_gather_pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool blend_pool_ = VK_NULL_HANDLE;
    bool blend_failed_ = false;

    // A sequence: the playing log's sub-frames between two game frames.
    struct SequenceState {
        bool ready = false;
        bool first = true;
        bool validated = false;  // its blend pairs were checked (the first sub-frame of a blending replay)
        std::uint64_t serial = 0;
        ReplaySequence overrides{};
        gpu::Address program = 0;  // copies, ops and payload, in the constant ring
        std::uint32_t program_bytes = 0;
        std::uint32_t copies = 0, ops = 0;
        std::uint32_t copies_offset = 0, ops_offset = 0, payload_offset = 0;
    };
    bool ensure_interp_eval();
    bool ensure_interp_output(std::uint64_t bytes);
    void record_interp_eval(VkCommandBuffer commands, const SubframeTime& time, const Affine34& camera_t,
                            const Affine34& camera_prev);
    SequenceState sequence_;
    std::uint64_t sequence_serial_ = 0;
    std::vector<interp_gpu::Copy> interp_copies_;
    std::vector<std::uint8_t> interp_copied_;
    VkDescriptorSetLayout eval_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout eval_layout_ = VK_NULL_HANDLE;
    VkPipeline eval_copy_pipeline_ = VK_NULL_HANDLE;
    VkPipeline eval_pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool eval_pool_ = VK_NULL_HANDLE;
    bool eval_failed_ = false;
    // A sequence's program (copies, ops, payload), in one of three buffers; each sub-frame's evaluation constants
    // (InterpEvalFrame) at its slot's 256 bytes of eval_frames_.
    struct ProgramBuffer {
        std::shared_ptr<vk::Buffer> buffer;
        std::uint64_t fence = 0;
        VkDescriptorSet set = VK_NULL_HANDLE;
        std::array<VkBuffer, 5> bound{};  // what `set` names: program, constant ring, matrix ring, output, verdicts
        // The sequence's blend pairs and stripe sources, and the sets of the passes that read them.
        std::shared_ptr<vk::Buffer> pairs;
        std::shared_ptr<vk::Buffer> sources;
        VkDescriptorSet validate_set = VK_NULL_HANDLE;
        VkDescriptorSet gather_set = VK_NULL_HANDLE;
        std::array<VkBuffer, 4> validate_bound{};
        std::array<VkBuffer, 4> gather_bound{};
    };
    std::array<ProgramBuffer, 3> program_buffers_{};
    unsigned program_index_ = 0;
    std::shared_ptr<vk::Buffer> eval_frames_;
    std::shared_ptr<vk::Buffer> eval_readback_;
    std::uint64_t eval_readback_fence_ = 0;
    float eval_readback_t_ = 0.0f;
    bool eval_readback_requested_ = false;
    bool eval_readback_recorded_ = false;
    [[nodiscard]] VkDescriptorSet eval_set(ProgramBuffer& program);
    std::shared_ptr<vk::Buffer> interp_output_;  // storage (written) and uniform (read by the replay's draws)
    gpu::Address interp_output_address_ = 0;
    std::shared_ptr<vk::Buffer> blend_verdicts_;
    std::vector<std::pair<std::shared_ptr<vk::Buffer>, std::uint64_t>> interp_retired_;  // with their last fence

    // Replay lists: the playing log recorded once per sequence into a reusable command buffer, with its own
    // descriptor pool, private EFB and stand-ins for its copies' destinations.
    struct StandIn {
        std::uint64_t dest_key = 0;
        gpu::TextureRef texture;
    };
    struct ReplayList {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer commands = VK_NULL_HANDLE;
        VkDescriptorPool descriptors = VK_NULL_HANDLE;
        std::vector<StandIn> stand_ins;
        std::uint64_t sequence = 0;
        std::uint64_t fence = 0;
        unsigned segment = 0;
        bool blend = false;          // its draws read the log before's vertices
        unsigned blend_segment = 0;  // that log's segment
        std::vector<gpu::TextureRef> held;  // the post-process targets its scene AA reads
    };
    bool record_replay_list(ReplayList& list);
    // A log's texture table with the replay's stand-ins in place of the copy destinations it names.
    [[nodiscard]] std::uint64_t patched_table(ReplayList& list, std::uint64_t table);
    std::unordered_map<VkImageView, VkImageView> redirect_;  // copy destination -> stand-in (this replay)
    ReplayList* replay_list_ = nullptr;  // the list being recorded
    std::vector<std::pair<std::array<VkBuffer, 3>, VkDescriptorSet>> replay_constant_sets_;  // its output-palette sets
    std::unordered_map<std::uint64_t, std::uint64_t> patched_;  // texture table -> patched (this replay)
    void replay_copy(const ReplayCopy& copy, vk::Texture& target);
    std::array<ReplayList, 3> replay_lists_{};
    EfbSet replay_efb_;
    unsigned replay_efb_scale_ = 0;
    EfbSet replay_alt_efb_;  // the replay's output EFB
    unsigned replay_alt_scale_ = 0;
    bool create_efb_set(EfbSet& set, unsigned scale, unsigned samples = 1u);
    void destroy_efb_set(EfbSet& set);
    XfbTexture subframe_xfb_;
    bool replaying_ = false;

    // Sub-frame submissions: the evaluation and present command buffers, their descriptor pool and acquire
    // semaphore (Frame), independent of the frame slots.
    std::array<Frame, 3> subframes_{};
    unsigned subframe_next_ = 0;
    // Each sub-frame's GPU time (its evaluation's start to its present's end): the interpolation's GPU governor.
    VkQueryPool subframe_timestamps_ = VK_NULL_HANDLE;
    std::array<bool, 3> subframe_timestamp_pending_{};
    bool subframe_capture_ = false;
    double subframe_gpu_ema_ms_ = 0.0;
    double subframe_gpu_ms_ = 0.0;
    std::uint64_t subframe_gpu_samples_ = 0;
    double subframe_record_ms_ = 0.0;
    std::uint64_t subframe_record_samples_ = 0;
    double sequence_record_ms_ = 0.0;
    std::uint64_t sequence_record_samples_ = 0;
    std::uint64_t replay_records_ = 0;
    CopyRedirectStats redirect_stats_{};
    void release_replay_resources() noexcept;
    // Presents the acquired image (swap_image_) after its submission: end_frame's and a sub-frame's.
    void present_swapchain_image();
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
    // VK_GOOGLE_display_timing (Android), as Swappy paces: each frame gets the retrace slot after the previous
    // frame's on the display's vsync grid (host_window::present_grid), two retraces after the latest retrace at
    // first (one of pipelining) and later only when the frame itself is late, and asks for half a vsync before it,
    // so the compositor shows it at that vsync for exactly one retrace period. The past presentation times report
    // how many were ([present-timing], every 600 frames).
    struct PresentTiming {
        std::uint32_t next_id = 0;
        std::int64_t last_slot = 0;
        std::int64_t vsync_ns = 0;  // the display's vsync interval (Choreographer)
        std::array<std::int64_t, 64> requested{};  // each presentID's slot, by presentID % 64
        std::int64_t refresh_ns = 0;
        std::int64_t last_actual = 0;
        unsigned frames = 0;
        unsigned on_slot = 0;
        unsigned late = 0;
        unsigned unrequested = 0;
        unsigned even = 0;  // shown one retrace period after the frame before
    };
    PresentTiming present_timing_;
    void report_present_timing();
    // Every 600 frames ([vk-gpu]): the GPU's frame time (timestamps), and how often the EFB pass reopened (each
    // reopening loads and stores the whole EFB on a tiled GPU) and the EFB was copied.
    struct GpuWindow {
        double ms = 0.0;
        double max_ms = 0.0;
        unsigned frames = 0;
        std::uint64_t passes = 0;
        std::uint64_t copies = 0;
    };
    GpuWindow gpu_window_;
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
