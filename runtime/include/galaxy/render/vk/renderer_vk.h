// SPDX-License-Identifier: GPL-3.0-only
// The renderer on Vulkan (docs/VULKAN.md), with the interface RenderCore uses on RendererD3D12.
//
// The scene target is a colour target (RGBA8) and a depth target (D24 where the device has it, else D32 float) drawn through
// one render pass; a scene target copy, screen probe or present ends that pass, and the next scene target operation begins it again. Between
// command buffers the scene target is always in its attachment layouts. kFramesInFlight frames are recorded ahead of the GPU,
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

#include "galaxy/content_viewport.h"
#include "galaxy/render/frame_pass.h"
#include "galaxy/render/gpu_types.h"
#include "galaxy/render/interp_gpu.h"
#include "galaxy/render/interp_types.h"
#include "galaxy/render/renderer_common.h"
#include "galaxy/render/rt_materials.h"
#include "galaxy/render/rt_sky_sun.h"
#include "galaxy/render/rt_types.h"
#include "galaxy/render/transform_history.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/render_state_key.h"
#include "galaxy/render/ui_canvas.h"
#include "galaxy/render/gpu_upload_ring.h"
#include "galaxy/render/vk/ambient_occlusion_vk.h"
#include "galaxy/render/vk/temporal_upscaler_vk.h"
#include "galaxy/render/vk/fsr_frame_generation_vk.h"
#include "galaxy/render/vk/scene_camera_vk.h"
#include "galaxy/render/vk/game_bloom_vk.h"
#include "galaxy/render/vk/post_process_vk.h"
#include "galaxy/render/vk/shader_compile_vk.h"
#include "galaxy/render/vk/utility_spirv.h"
#include "galaxy/render/vk/vk_context.h"
#include "galaxy/render/host_window.h"
#include "galaxy/render/window_owner.h"
#include "galaxy/render_api.h"

#include <array>
#include <chrono>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace galaxy::render {

class TextureCache;
struct TextureHandle;

namespace vk {

// What PipelineCacheVk builds pipelines against (RendererVk::root_signature()).
struct RenderLayout {
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::array<VkDescriptorSetLayout, 3> sets{};
    VkRenderPass scene_pass = VK_NULL_HANDLE;
    // The same pass clearing both targets as it begins (a whole-scene target copy-clear): nothing loaded from memory, which a
    // tiled GPU otherwise reads back at the full internal resolution. Compatible with scene_pass (load ops only).
    VkRenderPass scene_pass_clear = VK_NULL_HANDLE;
    // MSAA: the scene target passes over multisampled targets (index: log2 of the samples per pixel, 1 to 3), the colour
    // resolved into the single-sample target as each pass ends. Index 0 is scene_pass / scene_pass_clear.
    std::array<VkRenderPass, 4> ms_passes{};
    std::array<VkRenderPass, 4> ms_passes_clear{};
    // The same passes over the wide scene's colour format (kSceneVariantHdr), by samples.
    std::array<VkRenderPass, 4> wide_passes{};
    std::array<VkRenderPass, 4> wide_passes_clear{};
    // A temporal scene's passes: the scene target and depth with one more target, single-sample (DrawCall::aux_target: 1 the
    // motion vectors, 2 the reactive mask), over the 8-bit or the wide scene's colour.
    std::array<std::array<VkRenderPass, 2>, 2> aux_passes{};
    [[nodiscard]] VkRenderPass aux_pass_for(unsigned samples_log2, unsigned aux) const noexcept {
        return aux_passes[(samples_log2 & kSceneVariantHdr) != 0u ? 1u : 0u][aux - 1u];
    }
    [[nodiscard]] VkRenderPass pass_for(unsigned samples_log2, bool clear) const noexcept {
        if ((samples_log2 & kSceneVariantHdr) != 0u) {
            const unsigned samples = samples_log2 & kSceneVariantSamplesMask;
            return clear ? wide_passes_clear[samples] : wide_passes[samples];
        }
        if (samples_log2 == 0u) {
            return clear ? scene_pass_clear : scene_pass;
        }
        return clear ? ms_passes_clear[samples_log2] : ms_passes[samples_log2];
    }
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    bool dual_source_blend = false;
    bool logic_op = false;
    bool depth_clamp = false;
    bool depth_clip_enable = false;  // VK_EXT_depth_clip_enable
};

}  // namespace vk

class RendererVk final : public UiCanvas {
public:
    using SubmissionCallback = void (*)(void* context, std::uint64_t fence_value);

    bool initialize(int window_width, int window_height, const FrameMap& frame);
    // Why initialize failed, when the device said.
    [[nodiscard]] std::string startup_failure() const { return !init_error_.empty() ? init_error_ : vk::Context::get().failure(); }
    std::string init_error_;  // why initialize failed
    void create_first_use_resources();
    void shutdown();
    [[nodiscard]] UploadBuffer create_upload_buffer(std::size_t bytes, const char* name);

    // Interpolated sub-frames (renderer_vk_replay.cpp): each game frame's draws, clears and copies are logged;
    // a sub-frame replays the log with the constants evaluated at its t (interp_eval.h) into a private scene target and
    // presents its display copy. The interface is RendererD3D12's. Scene AA and ray tracing are inert here.
    using ReplaySequence = render::ReplaySequence;
    using SubframeTime = render::SubframeTime;
    using CopyRedirectStats = render::CopyRedirectStats;
    using StripeVertex = render::StripeVertex;
    [[nodiscard]] std::uint64_t playing_serial() const noexcept { return run_log_.serial; }
    void prepare_vertex_blend(std::span<const std::int32_t> previous_record);
    [[nodiscard]] std::span<const std::int32_t> ps_record_pairs() const noexcept { return run_ps_previous_; }
    // SMAA or FXAA of the 3D scene in the scene target before the 2D layer draws (PostProcessorVk).
    void record_scene_aa();
    // Ambient occlusion of the opaque scene (ambient_occlusion_vk.h).
    [[nodiscard]] bool ambient_occlusion_ready() const noexcept { return ao_.ready(); }
    void set_scene_ao_view(const RtView& view) noexcept { scene_ao_view_ = view; }
    // The scene's models (ambient_occlusion_objects.h); a replay reads the last game frame's.
    void set_scene_ao_objects(std::vector<ao::ObjectOccluder>& objects) noexcept { scene_ao_objects_.swap(objects); }
    void record_scene_ao();
    // The game's bloom through the renderer (galaxy/motion_signals.h kMarkerBloom; game_bloom_vk.h): until its
    // passes have compiled, the game draws its own chains.
    [[nodiscard]] bool game_bloom_ready() const noexcept { return bloom_.ready(); }
    void game_bloom(std::uint16_t phase, std::uint32_t a, std::uint32_t b);
    [[nodiscard]] unsigned replay_reject() const noexcept { return replay_reject_; }
    void reset_subframe_gpu_ema() noexcept { subframe_gpu_ema_ms_ = 0.0; }
    [[nodiscard]] double subframe_gpu_ema_ms() const noexcept { return subframe_gpu_ema_ms_; }
    void suspend_temporal_interpolation(double ms_per_frame);
    [[nodiscard]] bool temporal_interpolation() const noexcept { return temporal_interp_; }
    [[nodiscard]] unsigned temporal_output_width() const noexcept { return temporal_output_w_; }
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
    void set_scene_view(const std::array<float, 12>* view) noexcept {
        scene_view_known_ = view != nullptr;
        if (view != nullptr) {
            scene_view_ = *view;
        }
    }
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
    [[nodiscard]] bool rt_mist_open() const noexcept { return false; }
    // The effects layer opens over ray-traced water (RendererD3D12::begin_layer_window), which this renderer has not.
    void begin_layer_window() noexcept {}
    [[nodiscard]] bool layer_window() const noexcept { return false; }
    [[nodiscard]] std::int64_t last_present_qpc() const noexcept { return swap_present_qpc_; }
    void rt_note_mist(const float*, float, float, float) noexcept {}
    [[nodiscard]] bool rt_log_seq() const noexcept { return false; }
    [[nodiscard]] bool rt_scene_untraced() const noexcept { return false; }
    void rt_set_scene_untraced(bool) noexcept {}
    // The 3D scene in a wide target while `wanted` (the enhanced lighting), its camera at the end (scene_camera_vk.h);
    // sub-frames replay both.
    void begin_wide_scene(bool wanted);
    void end_wide_scene();
    void rt_begin_scene() {}
    void rt_capture_picture() {}
    void rt_trace_scene() {}
    void rt_trace_water() {}
    void rt_hold_sun(bool) noexcept {}
    void rt_reset_sky() noexcept {}
    void rt_set_camera(const std::array<float, 12>*) noexcept {}
    [[nodiscard]] bool rt_physical() const noexcept { return false; }
    void rt_set_presents(std::uint64_t) noexcept {}
    void rt_set_key_light(bool, const std::array<float, 3>&, const std::array<float, 3>&, float, bool) noexcept {}
    void rt_set_stage_sun(const RtStageSun*) noexcept {}
    [[nodiscard]] const RtStageSun* rt_stage_sun() const noexcept { return nullptr; }
    [[nodiscard]] bool rt_sky_key(std::array<float, 3>&, float&) const noexcept { return false; }
    [[nodiscard]] bool rt_sky_sun(std::array<float, 3>&, std::array<float, 3>&) const noexcept { return false; }
    [[nodiscard]] bool rt_sun_direction(std::array<float, 3>&) const noexcept { return false; }
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
    [[nodiscard]] bool temporal_mode() const;
    // A copy right after the display copy, from the output scene target the 2D layer drew in (the scene target itself without one).
    [[nodiscard]] bool draw_frame_pass_from_output(const FramePassRequest& params, TextureCache& textures);

    [[nodiscard]] gpu::Table texture_table_for(const TextureHandle* handles, unsigned count, const TextureCache& cache);
    void attach_texture_uploads(TextureCache& cache);
    void drain_gpu();
    void pump_messages() {}
    [[nodiscard]] bool quit_requested() const noexcept;

    // Test only (GALAXY_TEST_GPU_LOST_FRAME): as a lost device reports itself.
    [[noreturn]] void test_lose_device();
    void begin_frame(bool present_swap_chain = true);
    // D3D12 waits for its swap chain's frame latency here (a frame begun without a present that presents after all);
    // Vulkan paces at the image acquire in present().
    void wait_for_frame_latency_before_present() {}
    std::uint64_t end_frame(bool present_swap_chain = true, SubmissionCallback on_submitted = nullptr,
                            void* submission_context = nullptr);
    [[nodiscard]] std::uint64_t completed_fence_value() const;
    void wait_for_fence_value(std::uint64_t fence_value);

    void bind_scene();
    void clear_scene(std::uint32_t clear_rgba, bool color_enable,
                   bool alpha_enable, bool depth_enable, unsigned rect_x = 0, unsigned rect_y = 0, unsigned rect_w = 0,
                   unsigned rect_h = 0, std::uint8_t pixel_format = 0);
    void draw(const DrawCall& call);
    // The resident decoded vertices (RendererD3D12::create_resident_vertices): written once, read every frame, so in
    // device memory where the device maps it (vk::BufferPlace::DeviceMapped).
    [[nodiscard]] UploadBuffer create_resident_vertices(std::size_t bytes, const char* name);
    [[nodiscard]] std::byte* stage_resident_vertices(std::uint64_t offset, std::uint64_t) noexcept {
        return resident_mapped_ + offset;
    }
    void copy_scene_to_display(const FramePassRequest& params, DisplayTexture& dest);
    // Native presentation (E-10, RendererD3D12's): at default settings the display copy takes the scene target's colour image
    // instead of copying it (the display's vertical filter is applied when the frame is presented), and the scene target
    // continues on `fresh`, an idle picture of its size (made when null). The handed picture stays owed to the scene target
    // until a clear covers it: any other use copies it back (settle_owed_scene), at the latest when the frame ends.
    [[nodiscard]] bool can_hand_over_display_copy(const FramePassRequest& params, const DisplayTexture& dest) const;
    [[nodiscard]] gpu::TextureRef hand_over_scene_color(gpu::TextureRef fresh);
    // The frame so far becomes picture `key`; the frame goes on in `fresh`, not owed its contents. `released` is the
    // picture `key` had.
    [[nodiscard]] bool can_take_scene() const noexcept;
    bool take_scene(std::uint64_t key, gpu::TextureRef fresh, TextureCache& textures, gpu::TextureRef& released);
    // The frame, and its depth, as pictures for the next target pass (a key of 0: not wanted); end_target_pass gives
    // them back, or close_frame_for_pass when the pass could not begin.
    void open_frame_for_pass(std::uint64_t color_key, std::uint8_t channels, std::uint64_t depth_key, TextureCache& textures);
    void close_frame_for_pass(TextureCache& textures);
    [[nodiscard]] bool draw_frame_pass(const FramePassRequest& params, TextureCache& textures);
    // `black`: the frame is presented black (the pass clears; the display picture is not drawn).
    bool present(const DisplayTexture* source, bool capture_backbuffer = false, bool black = false);

    // The 2D layer at the window's resolution (hud_output_resolution): at the scene's end the scene target is resampled into
    // an scene target at the window's scale, which the 2D layer and the display copy then use, until end_output_scene.
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
    [[nodiscard]] bool output_scene_active() const noexcept { return output_scene_active_; }
    // Temporal upscaling (RendererD3D12's).
    [[nodiscard]] bool scene_temporal_begin(float& jitter_x, float& jitter_y);
    [[nodiscard]] bool begin_motion_scene();
    void record_motion_debug_view() noexcept {}
    void reset_temporal_history() noexcept { temporal_history_valid_ = false; }
    // Matrix-ring segment advances back whose palettes are still alive while this frame executes.
    [[nodiscard]] unsigned matrix_history_frames() const noexcept { return matrix_segments_ - kFramesInFlight; }
    // The matrix ring's rewinds (renderer_d3d12.h).
    [[nodiscard]] std::uint64_t matrix_segment_serial() const noexcept { return matrix_ring_.epoch(); }
    // A completed log waits (interpolation mode); start_replay_playback makes it the playing one.
    [[nodiscard]] bool replay_available() const noexcept { return ready_valid_; }
    void set_present_game_frame(bool) noexcept {}
    bool start_replay_playback();
    [[nodiscard]] bool replay_playing() const noexcept { return run_valid_; }
    // A present rate above 60.
    [[nodiscard]] bool interpolation_mode() const;
    [[nodiscard]] std::uint64_t replay_serial() const noexcept { return replay_serial_; }
    [[nodiscard]] std::uint32_t replay_picture_slot() const noexcept { return replay_picture_slot_; }
    void note_replay_scene() noexcept { ++replay_scene_count_; }
    // As RendererD3D12: a frame that draws no 3D scene, recorded as its screen layer alone.
    void note_replay_screen_frame() noexcept { replay_screen_frame_ = true; }
    void cancel_replay_screen_frame() noexcept { replay_screen_frame_ = false; }
    [[nodiscard]] unsigned replay_failure() const noexcept { return replay_failure_; }
    void note_replay_failure(unsigned reason) noexcept { replay_failure_ = reason; }
    void set_subframe_capture(bool capture) noexcept { subframe_capture_ = capture; }
    void take_subframe_gpu_ms(double& total_ms, std::uint64_t& samples) noexcept {
        total_ms = std::exchange(subframe_gpu_ms_, 0.0);
        samples = std::exchange(subframe_gpu_samples_, 0u);
    }
    void set_scene_camera(float near_plane, float far_plane, float fov_y) noexcept {
        scene_lens_[0] = near_plane;
        scene_lens_[1] = far_plane;
        scene_lens_[2] = fov_y;
    }

    [[nodiscard]] double measured_present_hz() const noexcept {
        return measured_present_hz_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t frame_index() const noexcept { return frame_index_; }

    // The picture's value at each point into slot `slot`'s readback, in the open frame where the stream is now;
    // screen_probes_finished names its bytes (RGBA8 per point) once that frame's fence has passed.
    static constexpr std::size_t kScreenProbeSlots = 12u;
    [[nodiscard]] bool record_screen_probes(std::size_t slot, ScreenProbeKind kind, std::span<const ScreenProbe> probes);
    [[nodiscard]] const std::uint8_t* screen_probes_finished(std::size_t slot);

    // Test captures: the scene target and the presented backbuffer as PPM files (paths from set_debug_capture_paths).
    bool debug_copy_scene_to_readback();
    void debug_log_readback();
    void debug_log_backbuffer_readback();
    bool debug_copy_selected_display_to_readback(const DisplayTexture&) noexcept { return false; }
    void set_debug_capture_paths(std::string scene_ppm_path, std::string backbuffer_ppm_path);

    [[nodiscard]] gpu::Device* device() const noexcept { return vk::Context::get().device(); }
    [[nodiscard]] const FrameMap& frame_map() const noexcept { return scene_map_; }
    // log2 of the scene target's samples per pixel (MSAA; the output scene target has one): the pipelines' sample count.
    [[nodiscard]] std::uint8_t scene_samples_log2() const noexcept {
        return static_cast<std::uint8_t>(std::countr_zero(scene_.samples));
    }
    // The pipelines' scene target variant (render_state_key.h RenderStateKey::samples_log2): no HDR scene scene target here.
    [[nodiscard]] std::uint8_t scene_variant() const noexcept {
        return static_cast<std::uint8_t>(scene_samples_log2() | (scene_.wide ? kSceneVariantHdr : 0u));
    }
    [[nodiscard]] const vk::RenderLayout* root_signature() const noexcept { return &draw_layout_; }
    [[nodiscard]] double display_refresh_hz() const noexcept { return display_refresh_hz_; }
    // The display's vblanks (host_window::vblank_grid): the latest and the interval, on galaxy::qpc_now()'s clock.
    [[nodiscard]] bool vblank_grid(std::int64_t& vblank, std::int64_t& period) const noexcept;
    [[nodiscard]] UploadRing& vertex_ring() noexcept { return vertex_ring_; }
    [[nodiscard]] UploadRing& index_ring() noexcept { return index_ring_; }
    [[nodiscard]] UploadRing& constant_ring() noexcept { return constant_ring_; }
    [[nodiscard]] UploadRing& matrix_ring() noexcept { return matrix_ring_; }
    [[nodiscard]] unsigned frame_slot() const noexcept { return frame_slot_; }
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
        VkCommandBuffer upscale = VK_NULL_HANDLE;  // a sub-frame's temporal upscale
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
    struct FramePassTarget {
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
    // Device objects.
    bool create_surface_and_device();
    bool create_swapchain();
    void destroy_swapchain() noexcept;
    // The window's surface withdrawn (host_window::surface_withdrawn) or lost, and made again once it is back.
    void release_surface() noexcept;
    [[nodiscard]] bool restore_surface();
    [[nodiscard]] bool surface_changed() const;
    bool create_layouts();
    bool create_scene_targets(const FrameMap& map, unsigned samples);
    // The MSAA sample count the settings ask for and the device has for the scene target's formats (1: none).
    [[nodiscard]] unsigned wanted_msaa_samples() const;
    // Before the scene target's single-sample depth is read: the multisampled depth drawn since the last resolve, resolved
    // into it (each pixel's sample 0). The colour resolves as each scene target pass ends.
    void resolve_scene_depth(VkCommandBuffer commands);
    [[nodiscard]] bool scene_fits_memory(const FrameMap& map) const;
    void apply_window_size();
    void carry_over_scene_contents();
    // Records `record` into a one-time command buffer, submits it and waits (start and resolution changes only).
    void run_one_time(const std::function<void(VkCommandBuffer)>& record);
    bool create_utility_pipelines();
    // A pipeline of the renderer's own passes: no vertex input, `strip` four-vertex quads or full-screen triangles,
    // blended over the target when `blend`, depth written (always passing) when `depth_write`.
    [[nodiscard]] VkPipeline create_utility_pipeline(VkRenderPass pass, VkPipelineLayout layout,
                                                     vk::UtilitySpirv vs_shader, vk::UtilitySpirv ps_shader,
                                                     VkColorComponentFlags write_mask, bool blend, bool strip,
                                                     bool depth_write,
                                                     VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
    [[nodiscard]] VkShaderModule utility_module(vk::UtilitySpirv shader);

    // Frame recording.
    // `clear`: begins with both targets cleared to it (scene_pass_clear).
    void begin_scene_pass(const SceneClearValues* clear = nullptr);
    // The same with a temporal scene's second target (DrawCall::aux_target); a pass open with another one ends.
    void begin_scene_pass_with(unsigned aux, const SceneClearValues* clear);
    void end_scene_pass();
    void reset_draw_bindings() noexcept;
    [[nodiscard]] VkDescriptorSet allocate_frame_set(VkDescriptorSetLayout layout);
    // A post-process draw's set: the replay list's while one is recorded, else the frame's.
    [[nodiscard]] VkDescriptorSet allocate_post_set(VkDescriptorSetLayout layout);
    // Presentation post-processing: SMAA or FXAA of the scene, FSR 1 or the box resample, RCAS.
    vk::PostProcessorVk post_;
    vk::GameBloomVk bloom_;
    vk::AmbientOcclusionVk ao_;
    RtView scene_ao_view_{};
    std::vector<ao::ObjectOccluder> scene_ao_objects_;
    void sync_post_process();
    // The picture through the post chain before a turned swap chain's blit (pre-rotated images).
    gpu::TextureRef oriented_;
    // DLSS Frame Generation (RendererD3D12's fg_hudless_*): the frame without its 2D layer, and whether a frame's Streamline
    // markers are open.
    gpu::TextureRef fg_hudless_scene_color_;
    gpu::TextureRef fg_hudless_display_;
    gpu::TextureRef fg_hudless_target_;
    bool fg_hudless_scene_ = false;       // this frame's scene was taken
    bool fg_hudless_taken_ = false;       // a picture's HUD-less copy is waiting
    std::uint32_t fg_hudless_slot_ = 0u;  // the picture it went with
    bool streamline_frame_open_ = false;
    // FSR 3 frame generation: its pacing swap chain while it is chosen, and the swap chain calls in use.
    vk::FsrFrameGenerationVk fsr_generation_;
    vk::SwapchainCalls swapchain_calls_{};
    bool swapchain_fsr_ = false;
    bool fg_hudless_ready_ = false;
    galaxy::ContentViewport fsr_rect_{};
    [[nodiscard]] bool fsr_generation_wanted() const;
    void take_fg_hudless_scene();
    void copy_fg_hudless_display(const FramePassGeometry& sampling, const DisplayTexture& dest);
    void present_fg_hudless(const DisplayTexture& source, const galaxy::ContentViewport& content);
    [[nodiscard]] bool wants_vsync() const;
    void retire(gpu::TextureRef texture);
    [[nodiscard]] VkDescriptorSet allocate_persistent_set(VkDescriptorSetLayout layout);
    void refresh_default_sampler_table();
    // Set 0 for b0, b1, a palette binding of `palette_range` bytes from `palette_offset` (VK_WHOLE_SIZE: to the
    // buffer's end; b0, b1 and the palette take the draw's dynamic offsets), vertex blending's earlier vertices
    // (`earlier`, whole) and the verdicts.
    [[nodiscard]] VkDescriptorSet constants_set(VkBuffer b0, VkBuffer b1, VkBuffer palette, VkDeviceSize palette_offset,
                                                VkDeviceSize palette_range, VkBuffer earlier);
    [[nodiscard]] VkSampler sampler_for(std::uint32_t key, const RenderConfig& cfg);
    void record_conversion_blit(vk::Texture& dest, unsigned dest_w, unsigned dest_h,
                                std::uint8_t copy_format, float src_x, float src_y, float step_x, float step_y);
    // `color_image`: the colour image read (none: the scene target's).
    void transition_scene_for_sampling(VkCommandBuffer commands, bool depth, VkImage color_image = VK_NULL_HANDLE);
    void transition_scene_to_attachment(VkCommandBuffer commands, bool depth, VkImage color_image = VK_NULL_HANDLE);
    // The hand-over (E-10). The scene target's framebuffer and colour conversion input: the set's own, or for an image a
    // hand-over gave it, the image's framebuffer (vk::Texture::scene_framebuffer, with the scene target's depth) and a set
    // in the frame's descriptors.
    [[nodiscard]] VkFramebuffer scene_framebuffer();
    [[nodiscard]] VkDescriptorSet scene_color_source();
    // The owed picture copied back into the scene target, all of it or the bands around `kept` (a clear covers `kept`).
    void restore_owed_scene(const VkRect2D* kept);
    void settle_owed_scene() {
        if (scene_owed_ && !replaying_) [[unlikely]] {
            restore_owed_scene(nullptr);
        }
    }
    // A handed-over frame through the display's vertical filter into a picture of its size (present).
    [[nodiscard]] VkFramebuffer framebuffer_for(vk::Texture& texture);
    void refresh_display_rate();
    void follow_window_and_resize();
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
    VkCommandBuffer commands_ = VK_NULL_HANDLE;  // where work records: the frame's, or a replay's
    std::uint64_t frame_index_ = 0;
    std::uint64_t last_submitted_ = 0;

    // Layouts and passes.
    vk::RenderLayout draw_layout_{};
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

    // The scene target.
    FrameMap scene_map_{};  // the bound scene target's map (E-08)
    VkFormat depth_format_ = VK_FORMAT_UNDEFINED;
    bool depth_linear_ = true;  // depth_format_ can be sampled with linear filtering
    // An scene target: its targets, their framebuffer and the conversion inputs reading them. The live one, and the private
    // one sub-frames replay into (the chunks of the game frame being drawn keep the live one between them).
    struct SceneSet {
        gpu::TextureRef color;
        gpu::TextureRef depth;  // its sampled view is the depth aspect alone
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkDescriptorSet color_source = VK_NULL_HANDLE;
        VkDescriptorSet depth_source = VK_NULL_HANDLE;
        // MSAA (samples above one): the multisampled targets the scene target pass draws into, color and depth above being
        // their resolves; resolve_framebuffer (scene_pass) holds color and depth for the depth resolve.
        unsigned samples = 1u;
        gpu::TextureRef ms_color;
        gpu::TextureRef ms_depth;
        VkFramebuffer resolve_framebuffer = VK_NULL_HANDLE;
        VkDescriptorSet ms_depth_source = VK_NULL_HANDLE;
        bool depth_dirty = false;
        // Its colour is an image a hand-over gave it (E-10): framebuffer and colour input per image (scene_framebuffer).
        bool turned = false;
        std::uint64_t depth_serial = 0;  // its depth target's (create_scene_set): the per-image framebuffers' key
        bool wide = false;  // the wide scene's colour (kSceneVariantHdr), over the scene target's own depth
        bool wide_read = false;  // its colour is in the sampling layout, since the last camera
    };
    SceneSet scene_;
    // The wide scene's colour targets (begin_wide_scene): the live frames' and the sub-frames'.
    SceneSet wide_scene_;
    SceneSet replay_wide_scene_;
    bool ensure_wide_set(SceneSet& set);
    void record_wide_begin();
    void record_wide_end();
    vk::SceneCameraVk scene_camera_;
    std::chrono::steady_clock::time_point camera_metered_{};
    std::uint64_t scene_depth_serial_ = 0;
    // E-10: the picture the scene target owes (hand_over_scene_color), the image the scene target had at the last frame submission while a
    // hand-over in the open frame replaced it, the open frame's input set for a turned scene target colour, the separate
    // submissions' colour input and the present's filtered picture.
    gpu::TextureRef scene_owed_;
    std::uint64_t frame_pass_color_key_ = 0u;
    std::uint64_t frame_pass_depth_key_ = 0u;
    std::weak_ptr<vk::Texture> scene_submitted_color_;
    VkImage scene_shown_image_ = VK_NULL_HANDLE;  // that picture while it stays owed across a submission
    bool scene_turned_in_frame_ = false;
    VkDescriptorSet turn_source_ = VK_NULL_HANDLE;
    const vk::Texture* turn_source_image_ = nullptr;
    std::uint64_t turn_source_frame_ = ~std::uint64_t{0};
    // The output scene target (begin_output_scene) and its size; they trade places with scene_ and scene_target_scale_ while it is active.
    SceneSet alt_scene_;
    FrameMap alt_scene_map_{};
    // The open target pass's set (begin_target_pass) while the scene target holds it, and each key's set between passes
    // ( its colour shared with its picture), dropped after kTargetIdleFrames frames unused. A picture is sampled
    // through its own view slot (TextureCache::allocate_view) reading the pass's channels.
    struct TargetSlot {
        SceneSet set;
        FrameMap map{};
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
        VkImageView sampled = VK_NULL_HANDLE;  // the view reading `channels` (none: the texture's own view)
    };
    std::unordered_map<std::uint64_t, Picture> pictures_;
    // `texture` becomes picture `key` (`width` x `height` game units, read through `channels`): true when draws
    // must look it up again (a new view, or a capture replacing others).
    bool publish_picture(std::uint64_t key, std::uint32_t width, std::uint32_t height, gpu::TextureRef texture,
                         std::uint8_t channels, std::uint32_t bytes, TextureCache& textures);
    bool publish_frame_pass(const FramePassRequest& params, gpu::TextureRef texture, TextureCache& textures);
    static constexpr std::uint64_t kTargetIdleFrames = 600u;
    std::unordered_map<std::uint64_t, TargetSlot> target_slots_;
    std::vector<std::pair<VkDescriptorSet, VkDescriptorSet>> target_free_sources_;  // dropped slots' inputs
    std::uint64_t target_key_ = 0;
    bool target_log_paused_ = false;  // log_paused_ before the pass
    SceneSet target_scene_;
    FrameMap target_scene_map_{};
    bool target_pass_active_ = false;
    bool output_scene_active_ = false;
    [[nodiscard]] FrameMap output_frame_for_window() const;
    bool enter_output_scene(const FrameMap& output);
    void swap_scene_sets();
    void swap_target_set();
    gpu::TextureRef scene_carry_color_;  // the previous targets, read once after a resolution change
    gpu::TextureRef scene_carry_depth_;
    bool scene_carry_clear_ = false;  // the new targets start cleared (MSAA before or after the change)
    bool scene_pass_open_ = false;
    unsigned scene_pass_aux_ = 0u;
    // The temporal scene (RendererD3D12's scene_temporal_, its targets and its upscaler).
    struct MotionTargets {
        gpu::TextureRef motion;
        gpu::TextureRef reactive;
        std::array<VkFramebuffer, 2> framebuffers{};
        VkImageView framebuffer_color = VK_NULL_HANDLE;
        VkImageView framebuffer_depth = VK_NULL_HANDLE;
        FrameMap map{};
    };
    MotionTargets motion_;
    MotionTargets replay_motion_;  // the sub-frames' (the game frame's are live meanwhile)
    [[nodiscard]] bool ensure_motion_targets();
    void clear_motion_targets();
    void destroy_motion_framebuffers();
    [[nodiscard]] VkFramebuffer motion_framebuffer(unsigned aux);
    void prepare_temporal_upscaler();
    [[nodiscard]] bool record_temporal_upscale(const FrameMap& output);
    void dump_temporal_inputs(const vk::Texture& color, const vk::Texture& motion, std::uint64_t index);
    [[nodiscard]] bool ensure_output_set(const FrameMap& output, bool storage);
    std::unique_ptr<vk::TemporalUpscaler> temporal_;
    std::future<std::unique_ptr<vk::TemporalUpscaler>> temporal_pending_;
    FrameMap upscale_output_{};
    unsigned temporal_render_w_ = 0u;
    unsigned temporal_render_h_ = 0u;
    unsigned temporal_output_w_ = 0u;
    unsigned temporal_output_h_ = 0u;
    unsigned temporal_frame_ = 0u;
    unsigned temporal_kind_ = 0u;
    unsigned temporal_quality_ = 0u;
    bool temporal_history_valid_ = false;
    bool scene_temporal_ = false;
    bool scene_motion_ = false;
    // The upscaler takes the sub-frames (and the frame at t = 1) instead of the game frames.
    bool temporal_interp_ = false;
    // suspend_temporal_interpolation, and the settings it was decided at.
    struct TemporalInterpKey {
        unsigned upscaler = 0u;
        unsigned quality = 0u;
        FrameMap scene_map{};
        FrameMap output_map{};
        float max_fps = 0.0f;
        unsigned vsync = 0u;
        double refresh_hz = 0.0;
        friend bool operator==(const TemporalInterpKey&, const TemporalInterpKey&) = default;
    };
    [[nodiscard]] TemporalInterpKey temporal_interp_key() const;
    bool temporal_interp_suspended_ = false;
    TemporalInterpKey temporal_interp_key_{};
    std::array<float, 12> scene_view_{};
    bool scene_view_known_ = false;
    float scene_jitter_[2]{};
    float scene_lens_[3]{1.0f, 100000.0f, 1.0f};
    std::int64_t temporal_last_upscale_us_ = 0;
    bool scene_set_storage_ = false;  // create_scene_set: its colour takes the upscaler's output
    bool alt_scene_storage_ = false;
    unsigned matrix_segments_ = kFramesInFlight;
    unsigned matrix_segment_ = 0u;
    bool matrix_segment_used_ = false;
    bool matrix_segment_started_ = false;
    std::size_t matrix_begin_cursor_ = 0u;
    bool present_black_ = false;  // present()'s `black`, for present_display
    bool scene_seeded_ = false;

    // Pipelines the renderer owns.
    VkPipeline conversion_pipeline_ = VK_NULL_HANDLE;
    VkPipeline display_filter_pipeline_ = VK_NULL_HANDLE;   // 1:1 display copies without gamma
    // The renderer's own draws into the scene target pass, per scene target sample count (log2), made at first use: masked clears,
    // and whole-colour and depth clears as draws (DeviceQuirks::clear_with_draw only; null otherwise).
    enum class SceneUtility : unsigned { ClearRgb, ClearAlpha, ClearRgba, ClearDepth, Count };
    std::array<std::array<VkPipeline, 8>, static_cast<std::size_t>(SceneUtility::Count)> scene_utility_pipelines_{};
    [[nodiscard]] VkPipeline scene_utility_pipeline(SceneUtility kind);
    unsigned msaa_max_samples_ = 1u;  // create_layouts: the most the scene target's formats render and sample
    VkPipeline depth_resolve_pipeline_ = VK_NULL_HANDLE;
    VkPipeline blit_pipeline_ = VK_NULL_HANDLE;
    VkPipeline ui_solid_pipeline_ = VK_NULL_HANDLE;
    VkPipeline ui_textured_pipeline_ = VK_NULL_HANDLE;
    VkPipeline depth_carry_pipeline_ = VK_NULL_HANDLE;
    VkPipeline color_carry_pipeline_ = VK_NULL_HANDLE;
    gpu::TextureRef corner_texture_;
    VkDescriptorSet corner_set_ = VK_NULL_HANDLE;
    bool corner_texture_tried_ = false;

    // draws.
    std::byte* resident_mapped_ = nullptr;
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

    // scene target copies.
    std::unordered_map<std::uint64_t, FramePassTarget> frame_pass_targets_;

    // Screen probes: one pixel of the target per point, each slot's readback fenced with its frame.
    gpu::TextureRef probe_target_;
    gpu::TextureRef probe_target_linear_;  // its linear twin (DeviceQuirks::linear_readback), made on first use
    std::array<Readback, kScreenProbeSlots> probe_slots_;
    // Readback::fence of probes recorded in the open frame.
    static constexpr std::uint64_t kProbesInFrame = ~std::uint64_t{0};

    VkCommandPool utility_pool_ = VK_NULL_HANDLE;  // one-time submissions (run_one_time)

    // Captures and telemetry.
    std::string scene_capture_path_;
    std::string backbuffer_capture_path_;
    Readback scene_capture_;
    std::string scene_capture_pending_path_;
    unsigned scene_capture_width_ = 0;
    unsigned scene_capture_height_ = 0;
    Readback backbuffer_capture_;
    std::string backbuffer_capture_pending_path_;
    unsigned backbuffer_capture_width_ = 0;
    unsigned backbuffer_capture_height_ = 0;
    bool backbuffer_capture_bgra_ = false;
    std::atomic<double> measured_present_hz_{0.0};
    double display_refresh_hz_ = 0.0;
    std::uint64_t display_refresh_checks_ = 0;
    std::int64_t measured_present_window_qpc_ = 0;
    std::int64_t swap_present_qpc_ = 0;   // every present's (last_present_qpc)
    std::uint64_t measured_present_count_ = 0;
    std::uint64_t last_frame_slot_wait_us_ = 0;
    std::uint64_t last_begin_reset_us_ = 0;
    unsigned replay_failure_ = 0;

    // Interpolated sub-frames (renderer_vk_replay.cpp).
    // Ring mode: in Interp the upload rings, texture-table descriptors and retired textures are segmented per game
    // frame (kInterpSegments), so a logged frame's data lives until its segment comes round again.
    enum class RingMode : std::uint8_t { Normal, Temporal, Interp };
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

    // The log: one game frame's work in order (a display copy closes it).
    // OutputSceneBegin: the scene resampled into the output scene target at the op's scale; SwapSceneSets: the scene target and the output
    // scene target trade places.
    enum class ReplayKind : std::uint8_t {
        Draw, BindScene, Clear, Copy, SceneAa, SceneAo, OutputSceneBegin, SwapSceneSets, Bloom, WideBegin, WideEnd
    };
    struct ReplayOp {
        ReplayKind kind = ReplayKind::Draw;
        std::uint32_t index = 0;
    };
    struct ReplayClear {
        std::uint32_t rgba = 0;
        bool color = false, alpha = false, depth = false;
        unsigned x = 0, y = 0, w = 0, h = 0;
        std::uint8_t format = 0;
    };
    struct ReplayCopy {
        FramePassRequest params{};
        bool display = false;            // the display copy (closes the log)
        std::uint64_t dest_key = 0;      // a texture copy's destination (frame_pass_targets_)
        unsigned width = 0, height = 0;  // the destination's size
        float geometry[4]{};  // sampling: src x, y, step x, y
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
        std::vector<ReplayClear> clears;
        std::vector<ReplayCopy> copies;
        std::vector<ReplayBloom> blooms;
        // The views of each texture table its draws bound (stand-ins replace copy destinations in them).
        std::unordered_map<std::uint64_t, std::array<VkImageView, kMaxTextureMaps>> tables;
        unsigned segment = 0;
        std::uint64_t serial = 0;
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
    void close_replay_log(std::uint32_t picture_slot);
    // work recorded now goes into the log (not while a replay records, nor inside a logged operation's own
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
    bool replay_screen_frame_ = false;
    unsigned replay_reject_ = 0;
    std::uint64_t replay_serial_ = 0;
    std::uint32_t replay_picture_slot_ = 0;
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
    // descriptor pool, private scene target and stand-ins for its copies' destinations.
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
        // A temporal upscale splits it at the output switch: what follows is in `commands_after`, the upscale of each
        // presented frame between the two.
        VkCommandBuffer commands_after = VK_NULL_HANDLE;
        bool split = false;
        struct Upscale {
            gpu::TextureRef color, depth, motion, reactive, output;
            unsigned render_width = 0u, render_height = 0u;
            unsigned output_width = 0u, output_height = 0u;
        } upscale;
    };
    bool record_replay_list(ReplayList& list);
    void record_subframe_upscale(const ReplayList& list, const SubframeTime& time);
    [[nodiscard]] vk::TemporalUpscaleInputs temporal_inputs(const vk::Texture& color, const vk::Texture& depth,
                                                            const vk::Texture& motion, const vk::Texture& reactive,
                                                            const vk::Texture& output) const;
    // A log's texture table with the replay's stand-ins in place of the copy destinations it names.
    [[nodiscard]] std::uint64_t patched_table(ReplayList& list, std::uint64_t table);
    std::unordered_map<VkImageView, VkImageView> redirect_;  // copy destination -> stand-in (this replay)
    ReplayList* replay_list_ = nullptr;  // the list being recorded
    std::vector<std::pair<std::array<VkBuffer, 3>, VkDescriptorSet>> replay_constant_sets_;  // its output-palette sets
    std::unordered_map<std::uint64_t, std::uint64_t> patched_;  // texture table -> patched (this replay)
    void replay_copy(const ReplayCopy& copy, vk::Texture& target);
    std::array<ReplayList, 3> replay_lists_{};
    SceneSet replay_scene_;
    FrameMap replay_scene_map_{};
    SceneSet replay_alt_scene_;  // the replay's output scene target
    FrameMap replay_alt_scale_{};
    bool replay_alt_storage_ = false;
    bool create_scene_set(SceneSet& set, const FrameMap& map, unsigned samples = 1u, unsigned width = kFrameUnits,
                        unsigned height = kFrameUnits);
    void destroy_scene_set(SceneSet& set);
    DisplayTexture subframe_display_;
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
    std::uint64_t window_size_refused_ms_ = 0;
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
    // Every 600 frames ([vk-gpu]): the GPU's frame time (timestamps), and how often the scene target pass reopened (each
    // reopening loads and stores the whole scene target on a tiled GPU) and the scene target was copied.
    struct GpuWindow {
        double ms = 0.0;
        double max_ms = 0.0;
        unsigned frames = 0;
        std::uint64_t passes = 0;
        std::uint64_t copies = 0;
        // Display copies handed over (E-10), and how their owed pictures ended: covered by a clear, copied back in
        // bands around a clear, or copied back whole.
        std::uint64_t handovers = 0;
        std::uint64_t owed_free = 0;
        std::uint64_t owed_banded = 0;
        std::uint64_t owed_restored = 0;
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
    // GALAXY_GPU_PASSES=1: GPU time by pass for present-stats' gpu-pass-ms (RendererD3D12's). A mark starts a pass in
    // the frame's commands; the time to the next mark (or the frame's end) is that pass's. The curved surfaces' sort,
    // in the upload commands, takes the two queries after the frame's marks.
    enum class GpuPass : std::uint8_t {
        Scene, Resolve, FramePass, DisplayCopy, Bloom, SceneAa, SceneAo, Upscale, RayTracing, Present, Camera, Count
    };
    static constexpr unsigned kGpuPassSlots = 512;
    void gpu_pass(GpuPass pass);
    void read_gpu_passes();
    class GpuPassScope {
    public:
        GpuPassScope(RendererVk& renderer, GpuPass pass) : renderer_(renderer), outer_(renderer.current_pass_) {
            renderer_.gpu_pass(pass);
        }
        ~GpuPassScope() { renderer_.gpu_pass(outer_); }
        GpuPassScope(const GpuPassScope&) = delete;
        GpuPassScope& operator=(const GpuPassScope&) = delete;
    private:
        RendererVk& renderer_;
        GpuPass outer_;
    };
    VkQueryPool pass_queries_ = VK_NULL_HANDLE;
    // The frame's shader invocations (gpu-stats): vertex, clipped
    // primitives, fragment and compute, in VkQueryPipelineStatisticFlagBits' order.
    VkQueryPool stats_queries_ = VK_NULL_HANDLE;
    // GALAXY_GPU_DRAWS=1: each draw between two timestamps, its time summed by pipeline for present-stats'
    // [gpu-draws] lines (RendererD3D12's; the timestamps serialize the draws: the total grows, the shares hold).
    static constexpr unsigned kGpuDrawSlots = 16384;
    struct GpuDrawMark {
        gpu::Pipeline* pipeline = nullptr;
        std::uint32_t triangles = 0;
    };
    struct GpuDrawCost {
        double ms = 0.0;
        std::uint64_t draws = 0;
        std::uint64_t triangles = 0;
    };
    VkQueryPool draw_queries_ = VK_NULL_HANDLE;
    std::array<std::vector<GpuDrawMark>, kFramesInFlight> draw_marks_{};
    std::unordered_map<gpu::Pipeline*, GpuDrawCost> draw_cost_;
    std::array<bool, kFramesInFlight> stats_pending_{};
    std::array<double, 4> stats_sum_{};
    std::array<std::array<GpuPass, kGpuPassSlots>, kFramesInFlight> pass_marks_{};
    std::array<unsigned, kFramesInFlight> pass_mark_count_{};
    GpuPass current_pass_ = GpuPass::Scene;
    bool pass_recording_ = false;  // between begin_frame and end_frame
    std::array<double, static_cast<std::size_t>(GpuPass::Count)> pass_ms_{};
};

using Renderer = RendererVk;

}  // namespace galaxy::render
