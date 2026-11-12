// SPDX-License-Identifier: GPL-3.0-only
// The renderer on Vulkan (docs/VULKAN.md), with the interface RenderCore uses on RendererD3D12.
//
// The scene target is a colour target (A2B10G10R10) and a depth target (D32 float with stencil; D24S8 where the device lacks it or
// its D32F clears are broken, DeviceQuirks::depth_d24) drawn through
// one render pass; a scene target copy, screen probe or present ends that pass, and the next scene target operation begins it again. Between
// command buffers the scene target is always in its attachment layouts. kFramesInFlight frames are recorded ahead of the GPU,
// each in two command buffers submitted together: the frame's texture uploads, then its draws (TextureStore never
// writes a texture after a draw of the same frame read it, so the uploads may run first).
//
// Draw bindings follow gpu::Encoder: set 0 holds b0, b1 and b2 (dynamic uniform buffers), the composed rows and the instance
// table, set 1 the sixteen textures, set 2 the sixteen samplers, push constants b3. Addresses (gpu::Address) name a buffer
// and offset (vk::Context::resolve).
#pragma once

#include "galaxy/content_viewport.h"
#include "galaxy/render/frame_pass.h"
#include "galaxy/render/gpu_types.h"
#include "galaxy/render/display_modes.h"
#include "galaxy/render/renderer_common.h"
#include "galaxy/render/rt_types.h"
#include "galaxy/render/rt/rt_backend.h"
#include "galaxy/render/rt/rt_denoiser.h"
#include "galaxy/render/render_config.h"
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
#include "render/core/gpu_encoder.h"
#include "render/core/draw_record.h"
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

class TextureStore;
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
    // Full-screen colour replacements preserve depth but never load old colour.
    std::array<std::array<VkRenderPass, 4>, 2> discard_color_passes{};
    // A whole-target depth clear opening the pass: the colour loaded, the depth cleared, the stencil loaded ([0]: as the
    // depth-clearing draw keeps it) or cleared ([1]: as vkCmdClearAttachments clears it).
    std::array<std::array<std::array<VkRenderPass, 4>, 2>, 2> depth_clear_passes{};
    // A texture target's pass (kSceneVariantTarget): RGBA8, one sample.
    VkRenderPass target_pass = VK_NULL_HANDLE;
    VkRenderPass target_pass_clear = VK_NULL_HANDLE;
    // A temporal scene's passes: the scene target and depth with one more target, single-sample (aux 1 the
    // motion vectors, 2 the reactive mask, 3 the reactive mask and the effects layer's colour and transmittance, which are
    // attachments 3 and 4), over the 8-bit or the wide scene's colour. Aux 3 is null where the device cannot make it.
    std::array<std::array<VkRenderPass, 3>, 2> aux_passes{};
    // The same, every attachment cleared as the pass begins (the scene's first clear; the layer's two load): compatible with
    // aux_passes.
    std::array<std::array<VkRenderPass, 3>, 2> aux_passes_clear{};
    // The tracer's prepass (aux kAuxPrepass): the item target (R32_UINT) and the scene's depth, single-sample; the
    // clearing form clears the item target as the pass begins and loads the depth.
    VkRenderPass item_pass = VK_NULL_HANDLE;
    VkRenderPass item_pass_clear = VK_NULL_HANDLE;
    [[nodiscard]] VkRenderPass aux_pass_for(unsigned samples_log2, unsigned aux, bool clear = false) const noexcept {
        const unsigned wide = (samples_log2 & kSceneVariantHdr) != 0u ? 1u : 0u;
        return clear ? aux_passes_clear[wide][aux - 1u] : aux_passes[wide][aux - 1u];
    }
    [[nodiscard]] VkRenderPass pass_for(unsigned samples_log2, bool clear) const noexcept {
        if ((samples_log2 & kSceneVariantTarget) != 0u) {
            return clear ? target_pass_clear : target_pass;
        }
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
    bool depth_clamp = false;
    bool depth_clip_enable = false;  // VK_EXT_depth_clip_enable
};

}  // namespace vk

class RendererVk final {
public:
    using SubmissionCallback = void (*)(void* context, std::uint64_t fence_value);

    bool initialize(int window_width, int window_height, const FrameMap& frame);
    // Why initialize failed, when the device said.
    [[nodiscard]] std::string startup_failure() const { return !init_error_.empty() ? init_error_ : vk::Context::get().failure(); }
    std::string init_error_;  // why initialize failed
    // The window stays; everything else is made again. False: no device yet.
    // What outlives this renderer when a lost card is left behind (RenderCore::restart_renderer).
    struct Kept {
        host_window::NativeWindow window = nullptr;
    };
    void set_window_owner(WindowOwner& owner) noexcept { window_owner_ = &owner; }
    [[nodiscard]] Kept take_apart_lost_device();
    void keep(const Kept& kept, bool) noexcept { window_ = kept.window; }
    void shutdown();
    [[nodiscard]] UploadBuffer create_upload_buffer(std::size_t bytes, const char* name);

    // SMAA or FXAA of the 3D scene in the scene target before the 2D layer draws (PostProcessorVk).
    void record_scene_aa();
    // Ambient occlusion of the opaque scene (ambient_occlusion_vk.h).
    [[nodiscard]] bool ambient_occlusion_ready() const noexcept { return ao_.ready(); }
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
    // copied over the scene's colour (rt_copy_to_scene). With `color_view`, the scene's colour as drawn so far for the
    // pass to read as well. False when no pass can read them now (a multisampled scene).
    [[nodiscard]] bool rt_depth_begin(rt::TextureView& depth, unsigned& width, unsigned& height, rt::ImageFormat& color,
                                      rt::TextureView* color_view = nullptr);
    void rt_depth_end();
    // An image of the tracer's (Backend::image_to_copy) over the scene's colour: the tracer's views.
    void rt_copy_to_scene(void* image);
    // The tracer's prepass: the item target (each pixel's item of the list, from 1; 0 none) beside the scene's depth,
    // made at the scene's size and cleared by the first draw's pass. Draws of aux kAuxPrepass write it. False when
    // there can be none (a multisampled scene, a texture target).
    [[nodiscard]] bool rt_items_begin();
    // The scene's depth and the item target as a pass reads them, until rt_light_end.
    [[nodiscard]] bool rt_light_begin(rt::TextureView& depth, rt::TextureView& items, unsigned& width, unsigned& height);
    void rt_light_end();
    // The reactive mask as the tracer writes it (plan 16.4), at the scene's end: false without one this frame or where
    // the device cannot. Until rt_reactive_end.
    [[nodiscard]] bool rt_reactive_begin(rt::TextureView& reactive);
    void rt_reactive_end();
    // The tracer's outside denoiser this frame (rt_denoiser.h: DLSS Ray Reconstruction), null for the built-in filters.
    [[nodiscard]] rt::Denoiser* rt_denoiser() noexcept { return temporal_ ? temporal_->denoiser() : nullptr; }
    // An image of the tracer's (Backend::image_to_sample) as a texture the draws' programs read: its place among the
    // textures (the view constants' view_traced). `which`: 0 the traced light, 1 the added light.
    [[nodiscard]] std::uint32_t rt_publish_light(void* image, TextureStore& textures, unsigned which = 0u);
    // The tracer stops while the game runs: its pictures leave the draws (their views retire), after the GPU is idle.
    void rt_unpublish_lights(TextureStore& textures);
    void rt_wait_idle() { wait_for_gpu_idle(); }
    // The game's bloom (game_bloom_vk.h): `kind` a NEBULA_PASS_BLOOM_*, `a` and `b` its values.
    [[nodiscard]] bool game_bloom_ready() const noexcept { return bloom_.ready(); }
    void game_bloom(std::uint32_t kind, std::uint32_t a, std::uint32_t b);
    void reset_generated_gpu_ema() noexcept { generated_gpu_ema_ms_ = 0.0; }
    [[nodiscard]] double generated_gpu_ema_ms() const noexcept { return generated_gpu_ema_ms_; }
    [[nodiscard]] double interpolation_gpu_cost_ms() const noexcept {
        return temporal_interp_ ? generated_gpu_ema_ms_ : native_scene_gpu_ema_ms_;
    }
    void suspend_temporal_interpolation(double ms_per_frame);
    [[nodiscard]] bool temporal_interpolation() const noexcept { return temporal_interp_; }
    [[nodiscard]] unsigned temporal_output_width() const noexcept { return temporal_output_w_; }
    // Milliseconds a GPU timestamp tick (0: the device has no compute timestamps).
    [[nodiscard]] double gpu_ms_per_tick() const noexcept { return timestamp_period_ns_ / 1'000'000.0; }
    // Smoothed GPU time of a frame that drew the scene (0 until measured): the tracer's build budget (5.3 G-3).
    [[nodiscard]] double scene_gpu_ema_ms() const noexcept { return native_scene_gpu_ema_ms_; }
    void set_scene_view(const std::array<float, 12>* view) noexcept {
        scene_view_known_ = view != nullptr;
        if (view != nullptr) {
            scene_view_ = *view;
        }
    }
    // The effects layer (RendererD3D12's): while its window is open, blended draws that never read the scene also write
    // its colour and transmittance targets (aux 3).
    [[nodiscard]] bool layer_window() const noexcept { return layer_window_; }
    void begin_layer_window();
    [[nodiscard]] std::int64_t last_present_qpc() const noexcept { return swap_present_qpc_; }
    // The 3D scene in a wide target while `wanted` (the enhanced lighting), its camera at the end (scene_camera_vk.h).
    bool begin_wide_scene(bool wanted, const SceneClearValues* initial_clear = nullptr);
    void end_wide_scene(bool finish = false);  // finish: the traced relight's camera (scene_camera.hlsl g_finish)
    [[nodiscard]] bool temporal_mode() const;
    // The upscaler reads the motion but no reactive mask (SGSR 2): every draw of the scene shares the motion pass, the
    // ones without motion leaving it as it is, so the pass is not closed and reopened between them.
    [[nodiscard]] bool motion_pass_only() const;
    // True where the device renders the layer's format with blending and four colour attachments.
    [[nodiscard]] bool has_layer_targets() const noexcept { return layer_targets_ok_; }
    // The draws' textures, as the Direct3D 12 renderer offers them (renderer_d3d12.h): with descriptor indexing (bindless())
    // every texture view sits in one array at its index and every sampler in another, and a draw names its maps by their
    // places in a list of {texture slot, sampler slot} pairs.
    [[nodiscard]] bool bindless() const noexcept { return bindless_; }
    [[nodiscard]] std::uint32_t texture_slot(const TextureHandle& handle) const noexcept;
    [[nodiscard]] std::uint32_t sampler_slot(const SamplerKey& key);
    // `count` pairs into this frame's texture lists: the buffer they went to and the first pair's place in it
    // (DrawRoot::table).
    void write_texture_list(const std::uint32_t* pairs, std::uint32_t count, gpu::Address& lists, std::uint32_t& first);
    // The arrays and the lists, for the draws that follow.
    void bind_draw_textures(gpu::Address lists);
    // Without descriptor indexing: a draw's own tables of up to table_slots() maps.
    [[nodiscard]] std::uint32_t table_slots() const noexcept { return vk::kBoundTableSlots; }
    [[nodiscard]] gpu::Table texture_table_for(const TextureHandle* handles, unsigned count, const TextureStore& cache);
    // A table that lives until replaced: `old` is given back once the GPU is past it.
    [[nodiscard]] gpu::Table persistent_texture_table(const TextureHandle* handles, unsigned count, const TextureStore& cache, gpu::Table old);
    // A persistent table nothing names any more.
    void retire_texture_table(gpu::Table table);
    // A target whose id is freed: every picture it has goes.
    void free_target(std::uint32_t id, TextureStore& textures);
    // Moves when every sampler table is made again.
    [[nodiscard]] std::uint32_t sampler_generation() const noexcept { return sampler_generation_; }
    void trace_rings() const;
    void copy_fg_hudless_display(const DisplayTexture& dest);
    void attach_texture_uploads(TextureStore& cache);
    // The device-local memory in use and its budget (VK_EXT_memory_budget when the device has it, else the allocator's
    // estimate), read at a frame boundary. False: not known.
    [[nodiscard]] bool video_memory(std::uint64_t& usage, std::uint64_t& budget) const noexcept;
    void wait_for_gpu_idle();
    void pump_messages() {}
    [[nodiscard]] bool quit_requested() const noexcept;
    // Vulkan on Windows says nothing of a covered window (a minimised one is the host's word): never occluded.
    [[nodiscard]] bool occluded() const noexcept { return false; }
    void probe_occlusion() noexcept {}

    // Test only (GALAXY_TEST_DEVICE_LOST): the next submission reports a lost device (vk::Context::simulate_device_loss).
    void lose_device();
    void begin_frame();
    // D3D12 waits for its swap chain's frame latency here (a frame begun without a present that presents after all);
    // Vulkan paces at the image acquire in present().
    std::uint64_t end_frame(bool present_swap_chain = true, SubmissionCallback on_submitted = nullptr,
                            void* submission_context = nullptr, bool generated = false);
    [[nodiscard]] std::uint64_t completed_fence_value() const;
    [[nodiscard]] std::uint64_t submitted_fence_value() const noexcept { return last_submitted_; }
    // What the render core holds that the breakdown line (log_video_memory) adds to the renderer's own figures; bytes unless noted.
    struct VideoMemoryOutside {
        std::uint64_t staging_bytes = 0u;                       // TextureStore's staging chunks
        std::uint64_t mesh_page_bytes = 0u, mesh_live_bytes = 0u, slot_bytes = 0u;
        std::uint64_t mesh_local_bytes = 0u;                    // of mesh_page_bytes, in video memory
        std::uint32_t mesh_pages = 0u, mesh_empty_pages = 0u, slots = 0u;
        std::uint64_t picture_bytes = 0u;                       // DisplayPictures' textures
        std::uint32_t picture_count = 0u;
        std::uint64_t pipelines = 0u;                           // pipelines made
    };
    // The 600-frame `[vram] breakdown` line: the device-local heap as the driver reads it, the allocator's own count of the
    // blocks and allocations in it, then the images the renderer holds (allocation sizes) and what is left. Render thread.
    void log_video_memory(const VideoMemoryOutside& outside);
    // The size of `texture`'s allocation; zero for none.
    [[nodiscard]] static std::uint64_t allocation_bytes(const vk::Texture* texture) noexcept {
        return texture != nullptr ? texture->allocation_bytes : 0u;
    }
    // GPU time by pass (render_api.h GpuPassTime): any thread. Off: no query is recorded or read.
    void set_gpu_timing(bool on) noexcept { gpu_timing_wanted_.store(on, std::memory_order_relaxed); }
    [[nodiscard]] bool gpu_timing_on() const noexcept { return gpu_timing_wanted_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::size_t gpu_pass_times(GpuPassTime* out, std::size_t capacity) const noexcept;
    // The latest scene's jitter: its index in the sequence and the sequence's length (0 phases: no temporal scene yet).
    void scene_jitter_info(unsigned& index, unsigned& phases) const noexcept {
        index = scene_jitter_index_;
        phases = scene_jitter_phases_;
    }
    void wait_for_fence_value(std::uint64_t fence_value);

    void bind_scene();
    [[nodiscard]] gpu::TextureRef swap_scene_color(gpu::TextureRef fresh);
    void clear_scene(std::uint32_t clear_rgba, bool color_enable,
                   bool alpha_enable, bool depth_enable, unsigned rect_x = 0, unsigned rect_y = 0, unsigned rect_w = 0,
                   unsigned rect_h = 0, std::uint8_t pixel_format = 0);
    // What the walker draws through. Aux targets beside the scene target: 0 none, 1 motion, 2 reactive, 3 reactive and the
    // effects layer, 4 the motion pass for a draw that writes no motion.
    [[nodiscard]] gpu::Encoder& encoder() noexcept { return encoder_; }
    void set_draw_aux(unsigned aux);
    void note_scene_draw(std::uint32_t state) {
        scene_drawn_ = true;
        constexpr std::uint32_t depth_write = (1u << 10u) | (1u << 14u);
        if (pending_depth_clear_ != 0u &&
            (((state & (1u << 10u)) != 0u && (((state >> 11u) & 7u) != 7u || (state & (1u << 14u)) != 0u)) ||
             ((state >> 20u) & 3u) != 0u)) {
            settle_depth_clear();  // a draw that tests or writes depth or stencil sees it cleared
        }
        if ((state & depth_write) == depth_write) {
            // Held probes read the depth as it was when they were asked: before this draw changes it.
            if (!held_probe_slots_.empty()) settle_screen_probes();
            if (scene_.samples > 1u) scene_.depth_dirty = true;
        }
    }
    // A buffer of `bytes` zeros, for streams a mesh lacks.
    [[nodiscard]] gpu::Address zero_stream(std::uint32_t bytes);
    // The frame so far is the frame's picture now: `fresh` (or a new target) takes its place.
    [[nodiscard]] gpu::TextureRef take_frame_picture(gpu::TextureRef fresh);
    // The frame so far becomes picture `key`; the frame goes on in `fresh`, which starts empty. `released` is the
    // picture `key` had.
    [[nodiscard]] bool can_take_scene() const noexcept;
    bool take_scene(std::uint64_t key, gpu::TextureRef fresh, TextureStore& textures, gpu::TextureRef& released);
    // The frame, and its depth, as pictures for the next target pass (a key of 0: not wanted); end_target_pass gives
    // them back, or close_frame_for_pass when the pass could not begin.
    void open_frame_for_pass(std::uint64_t color_key, std::uint8_t channels, std::uint64_t depth_key, TextureStore& textures);
    void close_frame_for_pass(TextureStore& textures);
    [[nodiscard]] bool draw_frame_pass(const FramePassRequest& params, TextureStore& textures);
    // `black`: the frame is presented black (the pass clears; the display picture is not drawn).
    bool present(const DisplayTexture* source, bool capture_backbuffer = false, bool black = false);

    // The 2D layer at the window's resolution (hud_output_resolution): at the scene's end the scene target is resampled into
    // an scene target at the window's scale, which the 2D layer and the display copy then use, until end_output_scene.
    [[nodiscard]] bool begin_output_scene(bool resolve_msaa_only = false);
    // A pass into texture target `key` (E-07): its target, `width` x `height` game units at the scene target's scale, takes
    // the scene target's place until end_target_pass, so the pass's draws and clears go to it. Each key keeps its target
    // between passes (made again only when its size or the scene target's scale changes); the pass is drawn once a game
    // frame. False when the target cannot be made or a pass is open.
    [[nodiscard]] bool begin_target_pass(std::uint64_t key, std::uint32_t width, std::uint32_t height);
    // Ends it: the scene target comes back and the target's picture is what draws sampling `key` at that size read (at full
    // precision, in any format, through `channels`: NebulaTargetChannels).
    void end_target_pass(std::uint8_t channels, TextureStore& textures);
    // The picture of target `key` when it is `width` x `height`: its texture and view.
    [[nodiscard]] bool target_picture(std::uint64_t key, TextureHandle& out) const;
    [[nodiscard]] bool target_pass_active() const noexcept { return target_pass_active_; }
    void end_output_scene();
    [[nodiscard]] bool output_scene_active() const noexcept { return output_scene_active_; }
    // Temporal upscaling (RendererD3D12's).
    [[nodiscard]] bool scene_temporal_begin(float& jitter_x, float& jitter_y);
    [[nodiscard]] bool begin_motion_scene();
    // The whole frame cleared (`clear_rgba`, far, stencil 0) just before a scene the temporal path will run: the motion
    // pass opens with the scene target, its depth and the motion vectors cleared as it begins, where the clear's own pass
    // would end at begin_motion_scene for a transfer clear of the motion targets and the motion pass load them all again.
    // False when the scene will not run temporal here (clear_scene clears).
    [[nodiscard]] bool begin_cleared_motion_scene(std::uint32_t clear_rgba);
    // A whole clear of the scene's depth (`clear_rgba`'s) that nothing later in the list reads or writes, asked while
    // held probes keep the scene pass open: the pass goes on, where the clear would end it to read them first (a store
    // and a load of the whole target on a tiled GPU). The depth is cleared as the next scene pass opens, or before
    // anything reads or tests it (settle_depth_clear). False when the clear is clear_scene's.
    [[nodiscard]] bool defer_scene_depth_clear(std::uint32_t clear_rgba);
    // Test builds: the motion vectors as colour over the 8-bit scene target (RendererD3D12's); nothing otherwise.
    void reset_temporal_history() noexcept { temporal_history_valid_ = false; }
    void set_present_game_frame(bool) noexcept {}
    // A present rate above 60.
    [[nodiscard]] bool interpolation_mode() const;
    void take_generated_gpu_ms(double& total_ms, std::uint64_t& samples) noexcept {
        total_ms = std::exchange(generated_gpu_ms_, 0.0);
        samples = std::exchange(generated_gpu_samples_, 0u);
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

    // Each point's depth (0 near, 1 far) into slot `slot`'s readback, in the open frame where the stream is now;
    // screen_probes_finished names the values, one float a point, once that frame's fence has passed. `compared` probes
    // asked inside the scene target pass are answered there, by a depth test of each point (1 seen, 0 hidden).
    [[nodiscard]] bool record_screen_probes(std::size_t slot, std::span<const ScreenProbe> probes, bool compared);
    [[nodiscard]] const float* screen_probes_finished(std::size_t slot);

    // Test captures: the scene target and the presented backbuffer as PPM files (paths from set_debug_capture_paths).
    bool debug_copy_scene_to_readback();
    void debug_log_readback();
    void debug_log_backbuffer_readback();
    // The presented display picture itself (before the post chain), copied in the present's command buffer to
    // <backbuffer path>.display.ppm, as the Direct3D 12 renderer's selected-display capture.
    bool debug_copy_selected_display_to_readback(const DisplayTexture& source);
    void debug_log_display_readback();
    void set_debug_capture_paths(std::string scene_ppm_path, std::string backbuffer_ppm_path);

    [[nodiscard]] gpu::Device* device() const noexcept { return vk::Context::get().device(); }
    [[nodiscard]] const FrameMap& frame_map() const noexcept { return scene_map_; }
    // log2 of the scene target's samples per pixel (MSAA; the output scene target has one): the pipelines' sample count.
    [[nodiscard]] std::uint8_t scene_samples_log2() const noexcept {
        return static_cast<std::uint8_t>(std::countr_zero(scene_.samples));
    }
    // The pipelines' scene target variant (render_state_key.h RenderStateKey::samples_log2): no HDR scene scene target here.
    [[nodiscard]] std::uint8_t scene_variant() const noexcept {
        return static_cast<std::uint8_t>(scene_samples_log2() | (scene_.wide ? kSceneVariantHdr : 0u) |
                                         (target_pass_active_ ? kSceneVariantTarget : 0u));
    }
    [[nodiscard]] void* pipeline_root() noexcept { return &draw_layout_; }
    [[nodiscard]] void* pipeline_backend() noexcept { return this; }
    [[nodiscard]] double display_refresh_hz() const noexcept { return display_refresh_hz_; }
    // The display's vblanks (host_window::vblank_grid): the latest and the interval, on galaxy::qpc_now()'s clock.
    [[nodiscard]] bool vblank_grid(std::int64_t& vblank, std::int64_t& period) const noexcept;
    [[nodiscard]] UploadRing& vertex_ring() noexcept { return vertex_ring_; }
    [[nodiscard]] UploadRing& index_ring() noexcept { return index_ring_; }
    [[nodiscard]] UploadRing& constant_ring() noexcept { return constant_ring_; }
    [[nodiscard]] UploadRing& matrix_ring() noexcept { return matrix_ring_; }
    [[nodiscard]] unsigned frame_slot() const noexcept { return frame_slot_; }
    void bind_draw_tables(gpu::Table, gpu::Table) noexcept {}
    // What bind_draw_textures would bind, as plain values (the recorder keeps them for a chunk's command buffer).
    [[nodiscard]] gpu::TextureArrays draw_texture_arrays(gpu::Address lists);

    // Parallel recording of a draw pass (draw_walker.cpp): the pass was recorded as ops in chunks; each chunk is replayed into a
    // secondary command buffer of its own, on any thread, and run in a render pass of its draw targets (a run of chunks with the
    // same targets shares one) after the commands recorded so far. begin says each chunk's draw targets (false: not possible now,
    // nothing changed); chunk replays chunk `i`; end runs them. replay_draws replays one chunk inline.
    [[nodiscard]] bool parallel_record_begin(const std::uint8_t* chunk_aux, unsigned chunk_count);
    void parallel_record_chunk(unsigned chunk, const gpu::DrawOp* ops, std::size_t count);
    void parallel_record_end();
    void replay_draws(std::uint8_t aux, const gpu::DrawOp* ops, std::size_t count);
    [[nodiscard]] gpu::Table sampler_table_for(const SamplerKey* keys, unsigned count);

private:
    // Parallel recording: a pool and a secondary command buffer for each chunk of each recorded pass of a frame (by frame slot).
    struct RecordCommands {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer commands = VK_NULL_HANDLE;
    };
    std::array<std::vector<RecordCommands>, kFramesInFlight> record_commands_;
    std::array<unsigned, kFramesInFlight> record_used_{};
    unsigned record_base_ = 0u;
    std::vector<std::uint8_t> record_aux_;
    std::uint8_t record_variant_ = 0u;
    gpu::Encoder::Layout encoding_layout_{};
    std::mutex constants_mutex_;  // the chunks' encoders share the frame's descriptor sets (constants_set)
    std::atomic<bool> recording_parallel_{false};
    struct Frame {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer commands = VK_NULL_HANDLE;
        VkCommandBuffer uploads = VK_NULL_HANDLE;
        VkCommandBuffer upscale = VK_NULL_HANDLE;  // a generated frame's temporal upscale
        VkDescriptorPool descriptors = VK_NULL_HANDLE;
        std::vector<VkDescriptorPool> overflow_descriptors;  // when a frame outgrows its pool
        std::uint32_t overflow_sets = 0;  // sets of the last overflow pool made: a frame's first overflow pool has this many, each further one in the frame twice the last
        VkSemaphore acquired = VK_NULL_HANDLE;
        std::uint64_t fence = 0;  // the submission that used this slot last
        std::vector<gpu::TextureRef> retired;  // kept until the slot's fence passes
        std::vector<gpu::BufferRef> retired_buffers;
        std::vector<VkDescriptorPool> retired_pools;  // image arrays replaced by larger ones, kept until the slot's fence passes
    };
    struct Swapchain {
        VkSwapchainKHR handle = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkColorSpaceKHR color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        VkExtent2D extent{};  // the window as it is displayed
        // Pre-rotation: on a display turned from its natural orientation (Android), the images keep the natural
        // orientation (`image`, extent's sides swapped for a quarter turn) and the present pass turns the picture
        // into them by `transform`, so the compositor shows them as they are.
        VkExtent2D image{};
        VkSurfaceTransformFlagBitsKHR transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        // The surface's own transform when the swap chain was made (`transform` stays identity under AMD's frame
        // generation, which reads the pictures and motion in one orientation; the compositor turns them then).
        VkSurfaceTransformFlagBitsKHR surface_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
        bool vsync = true;      // wants_vsync() when it was made (FIFO may be all a surface offers with VSync off)
        bool hdr_asked = false; // wants_hdr() when it was made (the surface may not offer the format: then no HDR10 and no remake)
        bool hdr = false;       // the images are HDR10 (a 10-bit format in the ST 2084 colour space)
        bool relaxed = false;   // made with {FIFO, FIFO_RELAXED}: a late present may ask for the second (present_swapchain_image)
        bool copyable = false;  // transfer source (test snapshots of the backbuffer)
        std::vector<VkImage> images;
        std::vector<VkImageView> views;
        std::vector<VkFramebuffer> framebuffers;
        std::vector<VkSemaphore> rendered;  // per image: the frame's commands finished writing it
    };
    std::uint32_t logged_swap_images_ = 0u;  // create_swapchain's "[vk] swap chain" line, written when these change
    VkPresentModeKHR logged_swap_mode_ = VK_PRESENT_MODE_MAX_ENUM_KHR;
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
    [[nodiscard]] bool surface_has_no_area() const;
    // The surface still offers the swap chain's format, colour space and present mode (they can differ per monitor).
    [[nodiscard]] bool surface_offers_swapchain() const;
    void destroy_swapchain() noexcept;
    // The window's surface withdrawn (host_window::surface_withdrawn) or lost, and made again once it is back.
    void release_surface() noexcept;
    [[nodiscard]] bool restore_surface();
    [[nodiscard]] bool surface_changed() const;
    bool create_layouts();
    // The adapter as the settings and the launcher see it (RenderFeatureCaps, rtx_adapter, adapter.ini), as the D3D12
    // renderer reports its own. After create_layouts (the scene target's sample counts).
    void report_adapter();
    bool create_scene_targets(const FrameMap& map, unsigned samples);
    // The MSAA sample count the settings ask for and the device has for the scene target's formats (1: none).
    [[nodiscard]] unsigned wanted_msaa_samples() const;
    // Before the scene target's single-sample depth is read: the multisampled depth drawn since the last resolve, resolved
    // into it (each pixel's sample 0). The colour resolves as each scene target pass ends.
    void resolve_scene_depth(VkCommandBuffer commands);
    [[nodiscard]] bool scene_fits_memory(const FrameMap& map) const;
    void apply_window_size();
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
    // `caller` (the calling function's name) counts why the scene target was opened again (GpuWindow::opens).
    void begin_scene_pass(const SceneClearValues* clear = nullptr, const char* caller = __builtin_FUNCTION());
    // The same with a temporal scene's second target (`aux`: 1 the motion target, 2 the reactive mask, 3 the reactive mask and the effects layer); a pass open with another one ends.
    // `secondary`: the pass runs secondary command buffers only (parallel recording): no viewport or scissor is set inline.
    void begin_scene_pass_with(unsigned aux, const SceneClearValues* clear = nullptr, bool discard_color = false,
                               const char* caller = __builtin_FUNCTION(),
                               unsigned clear_depth = 0u, bool secondary = false);
    // The draw targets a walker's aux names (set_draw_aux): aux 4 draws in the motion pass, aux 3 in the effects layer's only
    // while its window is open.
    [[nodiscard]] unsigned effective_draw_aux(unsigned aux) const noexcept {
        return aux == 4u ? 1u : aux == 3u ? (layer_window_ ? 3u : 2u) : aux;
    }
    void end_scene_pass(const char* caller = __builtin_FUNCTION());
    const char* scene_pass_closed_by_ = "frame start";
    void reset_draw_bindings() noexcept;
    [[nodiscard]] VkDescriptorSet allocate_frame_set(VkDescriptorSetLayout layout);
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
    std::uint64_t fg_hudless_used_frame_ = 0u;  // the last frame frame generation ran
    std::uint64_t hdr_capture_used_frame_ = 0u;  // the last frame a capture drew the SDR picture
    std::uint64_t motion_used_frame_ = 0u;      // the last frame a scene asked for the motion, reactive and layer targets
    std::uint64_t output_scene_used_frame_ = 0u;  // the last frame an output scene set was asked for (alt_scene_)
    std::uint64_t wide_scene_used_frame_ = 0u;    // the last frame a scene asked for the wide colour set (wide_scene_)
    // Once a frame, after the slot's retired images are freed: hands the frame generation and temporal targets back after
    // 600 frames unused, behind the fence of the slot's frame.
    void trim_idle_effect_targets();
    bool streamline_frame_open_ = false;
    // FSR 3 frame generation: its pacing swap chain while it is chosen, and the swap chain calls in use.
    vk::FsrFrameGenerationVk fsr_generation_;
    vk::SwapchainCalls swapchain_calls_{};
    bool swapchain_fsr_ = false;
    bool fg_hudless_ready_ = false;
    galaxy::ContentViewport fsr_rect_{};
    [[nodiscard]] bool fsr_generation_wanted() const;
    void take_fg_hudless_scene();
    void present_fg_hudless(const DisplayTexture& source, const galaxy::ContentViewport& content);
    [[nodiscard]] bool wants_vsync() const;
    // HDR10 output (E-17): the swap chain is made with a 10-bit ST 2084 format while the settings ask for HDR, the display is in
    // HDR mode, the post chain can write the last pass for it (never on a pre-rotated surface or under frame generation); the
    // last write then encodes with hdr_output_'s levels. read_display_hdr publishes what the display offers (the settings page
    // shows its HDR rows from it) at the first frame, after a move to another monitor and when the post chain becomes ready.
    [[nodiscard]] bool wants_hdr() const;
    void read_display_hdr();
    void apply_hdr_metadata(const PostOutput& levels);
    // A capture while the swap chain is HDR10 is the SDR picture, drawn again into hdr_capture_ and copied out.
    [[nodiscard]] bool capture_sdr_picture(VkCommandBuffer commands, const vk::Texture& picture, const galaxy::ContentViewport& content,
                                           const vk::PostProcessorVk::SetAllocator& allocate);
    bool hdr_read_ = false;
    bool hdr_post_ready_ = false;
    PostOutput hdr_output_{};
    VkFormat present_pass_format_ = VK_FORMAT_UNDEFINED;  // the swap chain's format present_pass_ was made for
    gpu::TextureRef hdr_capture_;
#if defined(_WIN32)
    OutputHdr hdr_reading_{};
#endif
    void retire(gpu::TextureRef texture);
    [[nodiscard]] VkDescriptorSet allocate_persistent_set(VkDescriptorSetLayout layout);
    // At each frame's start: the sampler array (or the default table) starts again when the settings changed or the array
    // is nearly full.
    void reset_sampler_frame();
    void write_sampler(unsigned slot, VkSampler sampler);
    void fill_sampler_array(VkSampler sampler);
    // Slots [first, first + count) of binding 0 of `set` take `info`: one write per chunk of kFillChunk infos, taken from
    // fill_infos_, the one array every fill reuses (no allocation the size of the set).
    void fill_slots(VkDescriptorSet set, VkDescriptorType type, std::uint32_t first, std::uint32_t count,
                    const VkDescriptorImageInfo& info);
    static constexpr std::uint32_t kFillChunk = 4096u;
    std::vector<VkDescriptorImageInfo> fill_infos_;
    // The image array: a set of `slots` (the null slot last) over the store's views, made when the store is first attached
    // and again, twice as large, when a view index reaches the end (the earlier views are copied; the frames that read
    // the old set finish before its pool is destroyed).
    void install_texture_array(TextureStore& cache, std::uint32_t slots);
    void grow_texture_array(std::uint32_t index);
    void destroy_retired_pools(Frame& frame) noexcept;
    // Set 0 for the three constants' buffers, the rows and the instances: one per list of them in a frame.
    [[nodiscard]] VkDescriptorSet constants_set(const VkBuffer* buffers, gpu::Address rows, gpu::Address instances,
                                                gpu::Address lists);
    [[nodiscard]] VkSampler sampler_for(const SamplerKey& key, const RenderConfig& cfg);
    void record_conversion_blit(vk::Texture& dest, unsigned dest_w, unsigned dest_h,
                                std::uint8_t copy_format, float src_x, float src_y, float step_x, float step_y);
    // `color_image`: the colour image read (none: the scene target's).
    void transition_scene_for_sampling(VkCommandBuffer commands, bool depth, VkImage color_image = VK_NULL_HANDLE);
    void transition_scene_to_attachment(VkCommandBuffer commands, bool depth, VkImage color_image = VK_NULL_HANDLE);
    // The scene target's framebuffer and colour conversion input: the set's own, or for an image taken into the
    // scene, the image's framebuffer (vk::Texture::scene_framebuffer, with the scene target's depth) and a set in the
    // frame's descriptors.
    [[nodiscard]] VkFramebuffer scene_framebuffer(bool resolved = false);
    [[nodiscard]] VkDescriptorSet scene_color_source();
    [[nodiscard]] VkFramebuffer framebuffer_for(vk::Texture& texture);
    void refresh_display_rate();
    void follow_window_and_resize();
#if defined(_WIN32)
    // The window's mode (windowed or borderless fullscreen), monitor and windowed size from the settings, at a frame
    // boundary: as RendererD3D12::reconcile_display_state. The swap chain follows through the window's size message.
    void reconcile_display_state();
    struct AppliedDisplay {
        bool valid = false;
        unsigned mode = 0;
        std::array<char, 64> monitor{};
        unsigned window_width = 0;
        unsigned window_height = 0;
    };
    AppliedDisplay applied_display_{};
#endif
    void report_present_stats();

    WindowOwner* window_owner_ = nullptr;
    host_window::NativeWindow window_ = nullptr;
    bool window_shown_ = false;
    bool keep_window_ = false;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    Swapchain swapchain_;
    bool swapchain_dirty_ = false;
    bool surface_lost_ = false;  // the driver reported it: made again from the window at the next frame
    std::uint32_t swap_image_ = UINT32_MAX;  // acquired for this frame's present
    // host_window::surface_use, from acquiring this frame's image until it is presented.
    std::unique_lock<std::timed_mutex> surface_lock_;

    std::array<Frame, kFramesInFlight> frames_{};
    unsigned frame_slot_ = 0;
    Frame* frame_ = &frames_[0];  // the submission being recorded
    VkCommandBuffer commands_ = VK_NULL_HANDLE;  // where work records
    std::uint64_t frame_index_ = 0;
    std::uint64_t last_submitted_ = 0;

    // Layouts and passes.
    vk::RenderLayout draw_layout_{};
    VkDescriptorSetLayout utility_set_layout_ = VK_NULL_HANDLE;  // t0, s0 (linear clamp)
    VkPipelineLayout utility_layout_ = VK_NULL_HANDLE;      // utility set + 64 bytes of push constants
    VkRenderPass color_pass_ = VK_NULL_HANDLE;    // one target at the scene's format, ends shader-readable
    VkRenderPass rgba8_pass_ = VK_NULL_HANDLE;    // the same at RGBA8: a window-format picture
    VkRenderPass present_pass_ = VK_NULL_HANDLE;  // the swapchain image, cleared, ends presentable
    VkSampler linear_clamp_ = VK_NULL_HANDLE;
    VkDescriptorPool persistent_pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> persistent_overflow_;
    std::uint32_t persistent_overflow_sets_ = 0;  // sets of the last overflow pool made (the next has twice as many)

    // The scene target.
    FrameMap scene_map_{};  // the bound scene target's map (E-08)
    VkFormat depth_format_ = VK_FORMAT_UNDEFINED;
    // A scene target: its targets, their framebuffer and the conversion input reading its colour.
    struct SceneSet {
        gpu::TextureRef color;
        gpu::TextureRef depth;  // its sampled view is the depth aspect alone
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkDescriptorSet color_source = VK_NULL_HANDLE;
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
    // The wide scene's colour targets (begin_wide_scene).
    SceneSet wide_scene_;
    bool wide_base_sampled_ = false;
    bool ensure_wide_set(SceneSet& set);
    void record_wide_begin(const SceneClearValues* initial_clear);
    void record_wide_end(bool finish = false);
    vk::SceneCameraVk scene_camera_;
    std::chrono::steady_clock::time_point camera_metered_{};
    std::uint64_t scene_depth_serial_ = 0;
    VkDescriptorSet turn_source_ = VK_NULL_HANDLE;
    const vk::Texture* turn_source_image_ = nullptr;
    std::uint64_t turn_source_frame_ = ~std::uint64_t{0};
    // The output scene target (begin_output_scene) and its size; they trade places with scene_ and scene_target_scale_ while it is active.
    SceneSet alt_scene_;
    FrameMap alt_scene_map_{};
    // The open target pass's set (begin_target_pass) while the scene target holds it, and each key's set between passes
    // ( its colour shared with its picture), dropped after kTargetIdleFrames frames unused. A picture is sampled
    // through its own view slot (TextureStore::allocate_view) reading the pass's channels.
    struct TargetSlot {
        SceneSet set;
        FrameMap map{};
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
        VkImageView sampled = VK_NULL_HANDLE;  // the view reading `channels` (none: the texture's own view)
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
    std::vector<VkDescriptorSet> target_free_sources_;  // dropped slots' colour inputs, free to reuse
    // A slot's targets go to the open frame's retired list; its colour input waits for the fence (retired_target_sources_).
    void retire_target_slot(TargetSlot& slot);
    // Slots no pass has used for target_idle_frames() give their targets back; also from begin_frame, so that a scene without
    // target passes does not keep the last scene's.
    void sweep_target_slots();
    std::uint64_t target_key_ = 0;
    SceneSet target_scene_;
    FrameMap target_scene_map_{};
    bool target_pass_active_ = false;
    bool output_scene_active_ = false;
    [[nodiscard]] FrameMap output_frame_for_window() const;
    bool enter_output_scene(const FrameMap& output);
    void swap_scene_sets();
    void swap_target_set();
    bool scene_pass_open_ = false;
    unsigned scene_pass_aux_ = 0u;
    // The temporal scene (RendererD3D12's scene_temporal_, its targets and its upscaler).
    struct MotionTargets {
        gpu::TextureRef motion;
        gpu::TextureRef reactive;
        gpu::TextureRef layer_color;  // the effects layer: what its draws added, R16G16B16A16F
        gpu::TextureRef layer_trans;  // and the product of their destination factors
        std::array<VkFramebuffer, 3> framebuffers{};
        VkImageView framebuffer_color = VK_NULL_HANDLE;
        VkImageView framebuffer_depth = VK_NULL_HANDLE;
        FrameMap map{};
    };
    MotionTargets motion_;
    [[nodiscard]] bool ensure_motion_targets();
    void clear_motion_targets(bool motion = true);  // the reactive mask too where it is read
    // The layer's targets to nothing added (zero colour) and everything let through (transmittance one).
    void clear_layer_targets();
    bool layer_targets_ok_ = false;  // create_layouts: the format and the attachment count are there
    bool layer_window_ = false;
    bool reactive_scene_ = false;  // this scene's blended draws write the reactive mask (begin_motion_scene)
    bool reactive_storable_ = false;  // the tracer writes it too (a storage image: R8 stores here and the device traces)
    void destroy_motion_framebuffers();
    [[nodiscard]] VkFramebuffer motion_framebuffer(unsigned aux);
    void prepare_temporal_upscaler();
    [[nodiscard]] bool record_temporal_upscale(const FrameMap& output);
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
    bool temporal_rr_ = false;  // DLSS made as Ray Reconstruction (the tracer running)
    unsigned temporal_quality_ = 0u;
    bool temporal_history_valid_ = false;
    bool scene_temporal_ = false;
    bool scene_motion_ = false;
    bool motion_cleared_ = false;  // begin_cleared_motion_scene opened the motion pass cleared for the next scene
    // defer_scene_depth_clear's: the depth attachment, its clear, and the depth clear pass it opens with (1 keeps the
    // stencil, 2 clears it; 0 none waiting).
    const vk::Texture* pending_depth_ = nullptr;
    SceneClearValues pending_depth_values_{};
    unsigned pending_depth_clear_ = 0u;
    [[nodiscard]] const vk::Texture* scene_depth_attachment() const noexcept {
        return scene_.samples > 1u ? scene_.ms_depth.get() : scene_.depth.get();
    }
    [[nodiscard]] bool depth_clear_pending() const noexcept {
        return pending_depth_clear_ != 0u && !target_pass_active_ && pending_depth_ == scene_depth_attachment();
    }
    void settle_depth_clear();
    void clear_depth_in_pass(float reversed_depth, const VkRect2D& rect);
    // The upscaler takes the generated frames (and the frame at t = 1) instead of the game frames.
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
    unsigned scene_jitter_index_ = 0u, scene_jitter_phases_ = 0u;
    float scene_lens_[3]{1.0f, 100000.0f, 1.0f};
    std::int64_t temporal_last_upscale_us_ = 0;
    bool scene_set_storage_ = false;  // create_scene_set: its colour takes the upscaler's output
    bool alt_scene_storage_ = false;
    bool present_black_ = false;  // present()'s `black`, for present_display

    // Pipelines the renderer owns.
    VkPipeline conversion_pipeline_ = VK_NULL_HANDLE;
    VkPipeline display_filter_pipeline_ = VK_NULL_HANDLE;   // 1:1 display copies without gamma
    // Screen probes (ScreenProbeCs): set {t0 the scene's depth, t1 the pixels, u0 the values}, the count pushed.
    VkDescriptorSetLayout probe_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout probe_layout_ = VK_NULL_HANDLE;
    VkPipeline probe_pipeline_ = VK_NULL_HANDLE;
    // The renderer's own draws into the scene target pass, per scene target sample count (log2), made at first use: masked clears,
    // and whole-colour and depth clears as draws (DeviceQuirks::clear_with_draw only; null otherwise).
    enum class SceneUtility : unsigned { ClearRgb, ClearAlpha, ClearRgba, ClearDepth, Count };
    std::array<std::array<VkPipeline, kSceneVariantCount>, static_cast<std::size_t>(SceneUtility::Count)> scene_utility_pipelines_{};
    [[nodiscard]] VkPipeline scene_utility_pipeline(SceneUtility kind);
    // A compared probe's test in the open scene target pass (by the pass's aux target, 0 to 3, and scene variant): the
    // depth clear's shader at the point's depth, tested GREATER_OR_EQUAL against the reversed depth on the first sample,
    // writing nothing, counted by an occlusion query.
    std::array<std::array<VkPipeline, kSceneVariantCount>, 4> probe_test_pipelines_{};
    [[nodiscard]] VkPipeline probe_test_pipeline();
    unsigned msaa_max_samples_ = 1u;  // create_layouts: the most the scene target's formats render and sample
    VkPipeline depth_resolve_pipeline_ = VK_NULL_HANDLE;
    VkPipeline blit_pipeline_ = VK_NULL_HANDLE;

    // draws.
    UploadRing vertex_ring_;
    UploadRing index_ring_;
    UploadRing constant_ring_;
    UploadRing matrix_ring_;
    std::unordered_map<SamplerCacheKey, VkSampler, SamplerCacheKeyHasher> samplers_;  // sampler key << 32 | config key
    std::unordered_map<SamplerTableKey, VkDescriptorSet, SamplerTableKeyHasher> sampler_tables_;
    std::uint32_t sampler_config_ = 0;
    gpu::Table default_sampler_table_{};
    // Descriptor indexing (vk::Context::bindless): the image array (set 1) and the sampler array (set 2) every draw reads,
    // updated while bound. A texture's slot is its view's index (TextureBackend::mirror_into), the last slot the null view;
    // a sampler's slot comes from the cache, slot 0 the default sampler. The sampler set is fixed at kBindlessSamplerSlots
    // (bindless_pool_). The image set has the length the views need (bindless_images_pool_, texture_array_slots_ slots,
    // the null slot included, never above texture_array_limit_) and is replaced by one twice as large when a view index
    // reaches its end (grow_texture_array); the old one's pool goes to the frame's retired_pools. A new image set holds the
    // null view in every slot and the sampler array the default sampler in every slot before a slot is named.
    bool bindless_ = false;
    VkDescriptorPool bindless_pool_ = VK_NULL_HANDLE;
    VkDescriptorPool bindless_images_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet bindless_images_ = VK_NULL_HANDLE;
    VkDescriptorSet bindless_samplers_ = VK_NULL_HANDLE;
    SamplerSlotCache sampler_slots_;
    bool sampler_slots_ready_ = false;
    std::uint32_t null_texture_slot_ = 0u;
    std::uint32_t texture_array_slots_ = 0u;
    std::uint32_t texture_array_limit_ = 0u;
    const void* mirrored_backend_ = nullptr;
    TextureStore* mirrored_store_ = nullptr;
    TextureStore* uploads_store_ = nullptr;  // the store whose uploads the transfer queue may take (attach_texture_uploads)
    // The texture lists (draw_tables, set 0 binding 5): a host-visible storage buffer per frame in flight, written from its
    // start each frame; a full one is replaced by one twice its size, the old kept until the frame's fence passes.
    struct TextureListBuffer {
        std::shared_ptr<vk::Buffer> buffer;
        std::uint32_t capacity = 0u;  // pairs
    };
    std::array<TextureListBuffer, kFramesInFlight> texture_lists_{};
    std::uint32_t texture_lists_used_ = 0u;
    std::uint32_t zero_stream_bytes_ = 0u;
    gpu::Address zero_stream_address_ = 0u;
    unsigned history_segment_ = 0u;
    std::array<std::uint64_t, kRingSegments> history_segment_fence_{};
    bool history_used_ = true;
    std::size_t history_begin_vertex_ = 0u, history_begin_matrix_ = 0u;
    void grow_rings();
    // Viewport and scissor of the renderer's own passes, set again after one.
    struct Bound {
        VkViewport viewport{};
        bool viewport_valid = false;
        VkRect2D scissor{};
        bool scissor_valid = false;
    } bound_;

    // scene target copies.
    std::unordered_map<std::uint64_t, FramePassTarget> frame_pass_targets_;

    // Screen probes: a slot's pixels (written here) and values (the dispatch writes, read back), fenced with its frame.
    struct ProbeSlot {
        std::shared_ptr<vk::Buffer> pixels;  // host-visible, coherent
        std::shared_ptr<vk::Buffer> values;  // readback, a storage buffer
        std::uint64_t capacity = 0;          // points both hold
        std::uint64_t fence = 0;             // 0: idle
        std::uint32_t held = 0;              // points asked while the scene pass was open, read when it ends
        // Compared points tested in the scene target pass: a query each, the answers once the fence has passed.
        VkQueryPool queries = VK_NULL_HANDLE;
        std::uint32_t query_capacity = 0;
        std::uint32_t tested = 0;            // points, 0 when the values buffer answers
        std::vector<float> answers;          // 1 seen, 0 hidden; the points no draw tests are set as they are asked
        std::vector<std::uint8_t> drawn;     // the points whose query decides
    };
    std::vector<ProbeSlot> probe_slots_;
    // Depth probes (compared ones are tested in the pass) asked while the scene target pass is open wait for its end, which comes anyway (a tiled GPU stores and loads
    // the whole target for each break): read then, or before the next draw or clear that writes depth, whichever is
    // first, they see the depth they were asked about.
    std::vector<std::size_t> held_probe_slots_;
    void dispatch_screen_probes(std::size_t slot, std::uint32_t count);
    [[nodiscard]] bool test_screen_probes(ProbeSlot& out, std::span<const ScreenProbe> probes);
    void read_held_probes();
    // Ends the scene pass for the held probes and opens it again as it was.
    void settle_screen_probes();
    // ProbeSlot::fence of probes recorded in the open frame.
    static constexpr std::uint64_t kProbesInFrame = ~std::uint64_t{0};

    VkCommandPool utility_pool_ = VK_NULL_HANDLE;  // one-time submissions (run_one_time)

    // Captures and telemetry.
    std::string scene_capture_path_;
    std::string backbuffer_capture_path_;
    Readback scene_capture_;
    std::string scene_capture_pending_path_;
    unsigned scene_capture_width_ = 0;
    unsigned scene_capture_height_ = 0;
    bool scene_capture_wide_ = false;  // the copy is of the wide (RGBA16F) scene target: 8 bytes a pixel (FB b83)
    Readback backbuffer_capture_;
    std::string backbuffer_capture_pending_path_;
    unsigned backbuffer_capture_width_ = 0;
    unsigned backbuffer_capture_height_ = 0;
    bool backbuffer_capture_bgra_ = false;
    Readback display_capture_;
    std::string display_capture_pending_path_;
    unsigned display_capture_width_ = 0;
    unsigned display_capture_height_ = 0;
    bool display_capture_ten_bit_ = false;
    std::atomic<double> measured_present_hz_{0.0};
    double display_refresh_hz_ = 0.0;
    // The display varies its refresh with the presents (next_variable_refresh): display_refresh_hz_ is then the cap that keeps
    // it in its range, and presents are spaced at the cap.
    bool variable_refresh_ = false;
    std::uint64_t display_refresh_checks_ = 0;
    std::int64_t measured_present_window_qpc_ = 0;
    std::int64_t swap_present_qpc_ = 0;   // every present's (last_present_qpc)
    // VK_KHR_present_id / present_wait: the id of each present on a FIFO swap chain (rising for the renderer's life), and the
    // last one that went in on this swap chain (0: none yet, nothing to wait for); present_swapchain_image waits for it to show
    // before queueing the next frame, 100 ms at most.
    std::uint64_t present_id_counter_ = 0;
    std::uint64_t last_present_id_ = 0;
    unsigned present_wait_timeouts_ = 0u;
    std::uint64_t measured_present_count_ = 0;

    // A table, a target's picture: what the GPU may still read until a fence passes.
    struct RetiredTable {
        VkDescriptorSet set = VK_NULL_HANDLE;
        std::uint64_t fence = 0;
    };
    std::vector<RetiredTable> retired_tables_;
    std::vector<RetiredTable> retired_target_sources_;  // dropped slots' colour inputs until the GPU is past their last read
    std::vector<VkDescriptorSet> free_tables_;
    void write_table(VkDescriptorSet set, const TextureHandle* handles, unsigned count, const TextureStore& cache);
    bool ring_growth_failed_ = false;
    gpu::Encoder encoder_;
    std::uint32_t sampler_generation_ = 1u;
    std::shared_ptr<vk::Buffer> zero_vertices_;
    struct ConstantsKey {
        std::array<VkBuffer, 3> buffers{};
        gpu::Address rows = 0u, instances = 0u, lists = 0u;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };
    std::vector<ConstantsKey> constants_sets_;
    bool create_scene_set(SceneSet& set, const FrameMap& map, unsigned samples = 1u, unsigned width = kFrameUnits,
                        unsigned height = kFrameUnits, bool target = false);
    void destroy_scene_set(SceneSet& set);
    // The frame's GPU time (timestamps) of frames that presented a generated picture: the interpolation's GPU governor.
    std::array<bool, kFramesInFlight> frame_generated_{};
    double generated_gpu_ema_ms_ = 0.0;
    double native_scene_gpu_ema_ms_ = 0.0;
    bool scene_drawn_ = false;
    std::array<bool, kFramesInFlight> frame_scene_drawn_{};
    double generated_gpu_ms_ = 0.0;
    std::uint64_t generated_gpu_samples_ = 0;
    // Presents the acquired image (swap_image_) after its submission.
    void present_swapchain_image();
    [[nodiscard]] vk::TemporalUpscaleInputs temporal_inputs(const vk::Texture& color, const vk::Texture& depth, const vk::Texture& motion,
                                                            const vk::Texture& reactive, const vk::Texture& output) const;
    // A depth or colour picture of the open scene target through open_frame_for_pass, until close_frame_for_pass.
    std::uint64_t frame_pass_color_key_ = 0u;
    std::uint64_t frame_pass_depth_key_ = 0u;
    std::uint64_t window_size_refused_ms_ = 0;
    PresentPacer present_pacer_;
    // VK_GOOGLE_display_timing (Android), as Swappy paces: each frame gets the present slot after the previous
    // frame's on the display's vsync grid (host_window::present_grid), two frame periods after the latest frame start at
    // first (one of pipelining) and later only when the frame itself is late, and asks for half a vsync before it,
    // so the compositor shows it at that vsync for exactly one frame period. The past presentation times report
    // how many were ([present-timing], every 600 frames).
    struct PresentTiming {
        std::uint32_t next_id = 0;
        std::int64_t last_slot = 0;
        std::int64_t vsync_ns = 0;  // the display's vsync interval (Choreographer)
        std::array<std::uint32_t, 64> requested_id{};
        std::array<std::int64_t, 64> requested_period{};
        std::array<std::int64_t, 64> requested{};  // each presentID's slot, by presentID % 64
        std::array<std::int64_t, 64> called{};     // when each was presented (CLOCK_MONOTONIC)
        // How long after its present a frame can first be shown: the GPU's work and the compositor's latch, as this
        // device takes them. Learned from frames that missed their slot (their earliestPresentTime is when they could
        // first be shown; a frame shown on time reports the slot it waited for), up a vsync at most per late frame and
        // easing down a quarter vsync every 240 frames on time. A slot is asked for no sooner than this after the present.
        std::int64_t lead_ns = 0;
        std::int64_t lead_max = 0;  // the highest a late frame needed in the reporting window
        unsigned on_time_run = 0;
        std::int64_t refresh_ns = 0;
        std::int64_t last_actual = 0;
        unsigned frames = 0;
        unsigned on_slot = 0;
        unsigned late = 0;
        unsigned unrequested = 0;
        unsigned duplicates = 0;
        unsigned available_late = 0;
        unsigned delayed_late = 0;
        unsigned intervals = 0;
        std::int64_t interval_sum = 0, interval_max = 0, margin_sum = 0;
        unsigned even = 0;  // shown one frame period after the frame before
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
        // Each scene target pass by what closed the one before it and what opened it: on a tiled GPU every opening
        // loads the colour, depth and stencil from memory and its end stores them again.
        struct Open {
            const char* closed = nullptr;
            const char* opened = nullptr;
            bool cleared = false;
            std::uint64_t count = 0;
        };
        std::array<Open, 24> opens{};
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
    // --diagnostics gpu-passes: GPU time by pass for present-stats' gpu-pass-ms (RendererD3D12's). A mark starts a pass in
    // the frame's commands; the time to the next mark (or the frame's end) is that pass's. The curved surfaces' sort,
    // in the upload commands, takes the two queries after the frame's marks.
    enum class GpuPass : std::uint8_t {
        Scene, Resolve, FramePass, DisplayCopy, Bloom, SceneAa, SceneAo, Upscale, RayTracing, Present, Camera,
        AoDistance, AoOcclusion, AoDenoise, AoUpsample, AoApply, Count
    };
    static constexpr unsigned kGpuPassSlots = 512;
    void gpu_pass(GpuPass pass);
    void read_gpu_passes();
    void prepare_gpu_pass_queries();
    bool profile_frame_ = false;
    unsigned profile_samples_ = 0;
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
    std::array<bool, kFramesInFlight> stats_pending_{};
    std::array<double, 4> stats_sum_{};
    std::array<std::array<GpuPass, kGpuPassSlots>, kFramesInFlight> pass_marks_{};
    std::array<unsigned, kFramesInFlight> pass_mark_count_{};
    GpuPass current_pass_ = GpuPass::Scene;
    GpuPass rt_outer_pass_ = GpuPass::Scene;
    bool rt_color_read_ = false;  // rt_depth_begin put the scene's colour in a pass's hands
    rt::Backend::TextureTable rt_textures_{};  // what rt_commands_begin hands the tracer: the image array's set and layout
    // The tracer's item target (rt_items_begin) and its framebuffer with the scene's depth.
    gpu::TextureRef item_target_;
    VkFramebuffer item_framebuffer_ = VK_NULL_HANDLE;
    VkImageView item_framebuffer_depth_ = VK_NULL_HANDLE;
    bool item_clear_pending_ = false;  // the prepass's next pass clears the item target
    [[nodiscard]] VkFramebuffer item_framebuffer();
    void destroy_item_framebuffer();
    static constexpr std::uint64_t kRtLightPicture = 1ull << 62u | 1u;  // the traced light's key among the pictures
    static constexpr unsigned kRtLightPictures = 10u;  // its pictures, kRtLightPicture + 0 to 9 (rt_publish_light's which)
    bool pass_recording_ = false;  // between begin_frame and end_frame
    std::array<double, static_cast<std::size_t>(GpuPass::Count)> pass_ms_{};
    // The settings page's timing: asked for by gpu_timing_wanted_ (every frame is profiled while it is), and each pass's running
    // average for the page.
    std::atomic<bool> gpu_timing_wanted_{false};
    std::array<std::atomic<float>, static_cast<std::size_t>(GpuPass::Count)> pass_ema_ms_{};
};

using Renderer = RendererVk;

}  // namespace galaxy::render
