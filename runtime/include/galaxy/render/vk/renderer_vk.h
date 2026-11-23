// SPDX-License-Identifier: GPL-3.0-only
// The renderer on Vulkan, with the interface RenderCore uses on RendererD3D12.
//
// The scene target is a colour target (A2B10G10R10) and a depth target (D32 float with stencil; D24S8 where the device lacks it or
// its D32F clears are broken, DeviceQuirks::depth_d24) drawn through
// one render pass; a scene target copy or present ends that pass, and the next scene target operation begins it again. Between
// command buffers the scene target is always in its attachment layouts. kFramesInFlight frames are recorded ahead of the GPU,
// each in two command buffers submitted together: the frame's texture uploads, then its draws (TextureStore never
// writes a texture after a draw of the same frame read it, so the uploads may run first).
//
// Draw bindings follow gpu::Encoder: set 0 holds b0, b1 and b2 (dynamic uniform buffers), the composed rows and the instance
// table, set 1 the sixteen textures, set 2 the sixteen samplers, push constants b3. Addresses (gpu::Address) name a buffer
// and offset (vk::Context::resolve).
#pragma once

#include "galaxy/content_viewport.h"
#include "galaxy/platform_time.h"
#include "galaxy/render/temporal_hold.h"
#include "galaxy/render/frame_pass.h"
#include "galaxy/render/gpu_pass_times.h"
#include "galaxy/render/gpu_types.h"
#include "galaxy/render/display_modes.h"
#include "galaxy/render/present_slots.h"
#include "galaxy/render/renderer_common.h"
#include "galaxy/render/scene_view.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/present_slots.h"
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
#include "galaxy/render/unseen_window.h"
#include "galaxy/render/window_owner.h"
#include "galaxy/render_api.h"
#include "galaxy/render/mod_post.h"
#include "galaxy/render/idle_clock.h"
#include "galaxy/render/dynamic_resolution.h"
#include <nebula_frame_list.h>

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
    // The same depth clears where the opening replaces the whole colour (discard_color_passes' colour, never loaded).
    std::array<std::array<std::array<VkRenderPass, 4>, 2>, 2> discard_depth_clear_passes{};
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
    // The pictures handed to an extension, taken from the GPU and written (render thread; RenderCore::finish_pictures,
    // at a host's exit that skips shutdown).
    void finish_pictures();
    [[nodiscard]] UploadBuffer create_upload_buffer(std::size_t bytes, const char* name);

    // SMAA or FXAA of the 3D scene in the scene target before the 2D layer draws (PostProcessorVk).
    void record_scene_aa();
    // Ambient occlusion of the opaque scene (ambient_occlusion_vk.h).
    [[nodiscard]] bool ambient_occlusion_ready() const noexcept { return ao_.ready(); }
    void set_scene_ao_view(const SceneView& view) noexcept { scene_ao_view_ = view; }
    // The scene's models (ambient_occlusion_objects.h).
    void set_scene_ao_objects(std::vector<ao::ObjectOccluder>& objects) noexcept { scene_ao_objects_.swap(objects); }
    void record_scene_ao();
    // The game's bloom (game_bloom_vk.h): `kind` a NEBULA_PASS_BLOOM_*, `a` and `b` its values.
    [[nodiscard]] bool game_bloom_ready() const noexcept { return bloom_.ready(); }
    void game_bloom(std::uint32_t kind, std::uint32_t a, std::uint32_t b);
    // draw.post (mod_post.h, mod_post_vk.cpp): `program` full screen over `picture` (the frame so far, from
    // take_frame_picture) into the scene target, in a pass of its own; `depth_known` the walker could give the depth terms.
    void record_mod_post(const ModPostProgram& program, ModPostConstants constants, const gpu::TextureRef& picture,
                         bool depth_known);
    // draw.native (luma_draw_native.h): `call` with the device and the command buffer between passes, the scene target
    // (with its depth at a scene point); the projection's y row turned for Vulkan's clip space.
    void record_native_call(const NebulaCall& call, LumaDrawNativeFrame& frame, bool scene_point);
    void reset_generated_gpu_ema() noexcept { frame_gpu_.reset_generated_ema(); }
    [[nodiscard]] double generated_gpu_ema_ms() const noexcept { return frame_gpu_.generated_ema_ms(); }
    [[nodiscard]] double interpolation_gpu_cost_ms() const noexcept {
        return temporal_interp_ ? frame_gpu_.generated_ema_ms() : frame_gpu_.scene_ema_ms();
    }
    void suspend_temporal_interpolation(double ms_per_frame);
    [[nodiscard]] bool temporal_interpolation() const noexcept { return temporal_interp_; }
    [[nodiscard]] unsigned temporal_output_width() const noexcept { return temporal_output_w_; }
    // Milliseconds a GPU timestamp tick (0: the device has no compute timestamps).
    [[nodiscard]] double gpu_ms_per_tick() const noexcept { return timestamp_period_ns_ / 1'000'000.0; }
    // Smoothed GPU time of a frame that drew the scene (0 until measured).
    [[nodiscard]] double scene_gpu_ema_ms() const noexcept { return frame_gpu_.scene_ema_ms(); }
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
    void end_wide_scene();
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
    // A chunk of the texture lists (core::TextureLists, FRAME-MODEL 2.10): `pairs` pairs of mapped memory the programs
    // read at `address`, kept until the renderer is taken down. False when it cannot be made.
    bool make_texture_list_chunk(std::uint32_t pairs, std::uint32_t*& mapped, gpu::Address& address);
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

    // An extension's lose_gpu_device: the next submission reports a lost device (vk::Context::simulate_device_loss).
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
        if ((state & depth_write) == depth_write && scene_.samples > 1u) {
            scene_.depth_dirty = true;
        }
    }
    // A buffer of `bytes` zeros, for streams a mesh lacks.
    [[nodiscard]] gpu::Address zero_stream(std::uint32_t bytes);
    // The frame so far is the frame's picture now: `fresh` (or a new target) takes its place.
    [[nodiscard]] gpu::TextureRef take_frame_picture(gpu::TextureRef fresh);
    // The frame so far becomes picture `key`; the frame goes on in `fresh`, which starts empty. `released` is the
    // picture `key` had.
    [[nodiscard]] bool can_take_scene() const noexcept;
    // The scene target's colour image may trade places with another of its size, for a pass that then writes all of it
    // that is read (the frame, or dynamic resolution's rectangle): can_take_scene less the game's whole-frame need.
    [[nodiscard]] bool can_trade_scene() const noexcept;
    bool take_scene(std::uint64_t key, gpu::TextureRef fresh, TextureStore& textures, gpu::TextureRef& released);
    // The frame, and its depth, as pictures for the next target pass (a key of 0: not wanted); end_target_pass gives
    // them back, or close_frame_for_pass when the pass could not begin.
    void open_frame_for_pass(std::uint64_t color_key, std::uint8_t channels, std::uint64_t depth_key, TextureStore& textures);
    void close_frame_for_pass(TextureStore& textures);
    [[nodiscard]] bool draw_frame_pass(const FramePassRequest& params, TextureStore& textures);
    // A frame copy made before the list writes the frame: the frame is still the picture shown last (`shown`), as a
    // console's frame buffer still holds it, never what the display copy left in the frame's target. Copied into the
    // frame's target, or the output scene target's when it is the picture's size, for this copy; a picture that fits
    // neither (a size or format changed since it was shown) leaves the frame as it is (draw_frame_pass).
    [[nodiscard]] bool draw_shown_frame_pass(const FramePassRequest& params, const DisplayTexture& shown, TextureStore& textures);
    // `black`: the frame is presented black (the pass clears; the display picture is not drawn).
    bool present(const DisplayTexture* source, bool capture_backbuffer = false, bool black = false);

    // The 2D layer at the window's resolution (render_hud_at_output): at the scene's end the scene target is resampled into
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
    // HDR output's 2D layer (RendererD3D12::begin_layer_2d): the HUD and menus in the scene's 10-bit format over the float
    // scene, the pixels they drew written back into it at the end.
    void begin_layer_2d();
    void end_layer_2d();
    [[nodiscard]] bool output_scene_active() const noexcept { return output_scene_active_; }
    // Temporal upscaling (RendererD3D12's).
    [[nodiscard]] bool scene_temporal_begin(float& jitter_x, float& jitter_y);
    [[nodiscard]] bool begin_motion_scene();
    // The whole frame cleared (`clear_rgba`, far, stencil 0) just before a scene the temporal path will run: the motion
    // pass opens with the scene target, its depth and the motion vectors cleared as it begins, where the clear's own pass
    // would end at begin_motion_scene for a transfer clear of the motion targets and the motion pass load them all again.
    // False when the scene will not run temporal here (clear_scene clears).
    [[nodiscard]] bool begin_cleared_motion_scene(std::uint32_t clear_rgba);
    // The walk's late depth clear (plan_late_depth_clears) is cleared where it stands: nothing holds the scene pass open
    // past it (the held depth probes went with S0.2.6).
    [[nodiscard]] bool defer_scene_depth_clear(std::uint32_t) noexcept { return false; }
    // Test builds: the motion vectors as colour over the 8-bit scene target (RendererD3D12's); nothing otherwise.
    void reset_temporal_history() noexcept { temporal_history_valid_ = false; }
    void set_present_game_frame(bool) noexcept {}
    // A present rate above 60.
    [[nodiscard]] bool interpolation_mode() const;
    void take_generated_gpu_ms(double& total_ms, std::uint64_t& samples) noexcept {
        frame_gpu_.take_generated(total_ms, samples);
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

    // Test captures: the presented backbuffer as PPM files (paths from set_debug_capture_paths). Writes every capture
    // whose frame the GPU has finished.
    void debug_log_backbuffer_readback();
    // The presented display picture itself (before the post chain), copied in the present's command buffer to
    // <backbuffer path>.display.ppm, as the Direct3D 12 renderer's selected-display capture.
    bool debug_copy_selected_display_to_readback(const DisplayTexture& source);
    void set_debug_capture_paths(std::string backbuffer_ppm_path);

    [[nodiscard]] gpu::Device* device() const noexcept { return vk::Context::get().device(); }
    [[nodiscard]] const FrameMap& frame_map() const noexcept { return scene_map_; }
    // The bound scene target's allocated size: the render size while dynamic resolution draws the scene in a part of it
    // (frame_map, the step's rectangle), else frame_map itself. Pictures for the game and target passes take this size.
    [[nodiscard]] const FrameMap& frame_extent() const noexcept { return frame_extent_; }
    // The 3D scene's pass opens (its clear, or walk_scene_begin): dynamic resolution decides this picture's rectangle
    // (once a frame) and the main scene target is drawn in it from here to the end of the frame.
    void begin_scene_rectangle();
    // The rectangle the last scene was drawn in (the render size while dynamic resolution is off).
    [[nodiscard]] const FrameMap& scene_rectangle() const noexcept { return scene_rectangle_; }
    // log2 of the scene target's samples per pixel (MSAA; the output scene target has one): the pipelines' sample count.
    [[nodiscard]] std::uint8_t scene_samples_log2() const noexcept {
        return static_cast<std::uint8_t>(std::countr_zero(scene_.samples));
    }
    // The scene target passes by samples (log2), wide (float) or not; null where the device has none.
    [[nodiscard]] std::array<VkRenderPass, 4> scene_passes(bool wide) const noexcept {
        std::array<VkRenderPass, 4> passes{};
        for (unsigned samples_log2 = 0u; samples_log2 < 4u; ++samples_log2) {
            passes[samples_log2] = draw_layout_.pass_for(samples_log2 | (wide ? kSceneVariantHdr : 0u), false);
        }
        return passes;
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
    // What the swap chain keeps of the pictures presented (present_slots.h): its present mode's.
    [[nodiscard]] SwapQueue swap_queue() const noexcept {
        return swapchain_.mode == VK_PRESENT_MODE_FIFO_KHR      ? SwapQueue::EveryPicture
             : swapchain_.mode == VK_PRESENT_MODE_MAILBOX_KHR   ? SwapQueue::NewestPicture
                                                                : SwapQueue::Tearing;
    }
    // The moment the next present shows, on galaxy::qpc_now()'s clock (RenderCore's interpolation slot, a vblank); 0
    // when it has none. With presentation times (VK_GOOGLE_display_timing) the picture is shown at that moment and a
    // pipeline depth (PresentTiming).
    void set_present_moment(std::int64_t qpc) noexcept { present_moment_ = qpc; }
    // True once when the presentation times lower the depth by one picture fewer: the next slot is skipped
    // (PresentSlots::take_skip).
    [[nodiscard]] bool take_present_skip() noexcept { return present_timing_.slots.take_skip(); }
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
        // When a frame outgrows `descriptors`: the slot's further pools, kept and reset with it; a frame takes them in order
        // and makes one (twice the last's sets) only when every one is full.
        std::vector<VkDescriptorPool> overflow_descriptors;
        std::uint32_t overflow_sets = 0;  // sets of the last overflow pool made
        std::size_t overflow_used = 0;    // pools of overflow_descriptors this frame has taken
        std::size_t overflow_peak = 0;    // the most taken by a frame since overflow_peak_step
        std::uint64_t overflow_peak_step = 0;  // idle_clock() when pools past the peak were last trimmed
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
        // Made for present timing (Context::present_timing and the surface's support): its presents carry present
        // id 2 (waited for with present wait 2) and ask when they reach the display at `timing_stage`, in the time
        // domain `time_domain_id`: the runtime's clock (QueryPerformanceCounter, or CLOCK_MONOTONIC) where the swap
        // chain offers it, else its own (present stage or swap chain local, in nanoseconds), mapped to the runtime's
        // clock by the two read together (vkGetCalibratedTimestampsKHR: `calibration_local` with `calibration_qpc`).
        bool ids2 = false;
        bool timed = false;
        std::uint64_t time_domain_id = 0;
        VkTimeDomainKHR time_domain = VK_TIME_DOMAIN_DEVICE_KHR;
        bool local_clock = false;
        std::uint64_t calibration_local = 0;
        std::int64_t calibration_qpc = 0;
        VkPresentStageFlagsEXT timing_stage = 0;
        std::int64_t refresh_qpc = 0;               // the swap chain's refresh duration (0: variable or unknown)
        std::uint64_t timing_properties_counter = 0;
        std::uint32_t timing_outstanding = 0;       // presents asked for timing whose results are not read yet
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
                                                     VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
                                                     bool color_attachment = true);
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
    SceneView scene_ao_view_{};
    std::vector<ao::ObjectOccluder> scene_ao_objects_;
    void sync_post_process();
    // DLSS Frame Generation (RendererD3D12's fg_hudless_*): the frame without its 2D layer, and whether a frame's Streamline
    // markers are open.
    gpu::TextureRef fg_hudless_scene_color_;
    gpu::TextureRef fg_hudless_display_;
    gpu::TextureRef fg_hudless_target_;
    bool fg_hudless_scene_ = false;       // this frame's scene was taken
    bool fg_hudless_taken_ = false;       // a picture's HUD-less copy is waiting
    std::uint32_t fg_hudless_slot_ = 0u;  // the picture it went with
    std::uint64_t fg_hudless_used_step_ = 0u;  // idle_clock() when frame generation ran
    std::uint64_t hdr_capture_used_step_ = 0u;  // idle_clock() when a capture drew the SDR picture
    std::uint64_t motion_used_frame_ = 0u;      // the last frame a scene asked for the motion, reactive and layer targets
    std::uint64_t motion_used_step_ = 0u;        // idle_clock() at that frame (the idle trim's stamp)
    std::uint64_t output_scene_used_step_ = 0u;  // idle_clock() when an output scene set was asked for (alt_scene_)
    std::uint64_t wide_scene_used_step_ = 0u;    // idle_clock() when a scene asked for the wide colour set (wide_scene_)
    // Once a frame, after the slot's retired images are freed: hands the frame generation and temporal targets back after
    // kIdleSteps game steps unused (idle_clock.h), behind the fence of the slot's frame.
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
    // HDR mode, the post chain can write the last pass for it (pre-rotated or not; never under frame generation); the
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
    // The game's last wish for Windows HDR on the window's display (os_hdr.h).
    bool os_hdr_asked_ = false;
    bool os_hdr_wanted_ = false;
    HMONITOR os_hdr_monitor_ = nullptr;
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
    [[nodiscard]] VkFramebuffer scene_framebuffer();
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
    UnseenWindow unseen_;  // while the window cannot be seen, no image is acquired or presented
    bool keep_window_ = false;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    Swapchain swapchain_;
    bool swapchain_dirty_ = false;
    // The window's size as its last resize message said (follow_window_and_resize; 0 before any): a surface that reports no
    // size (Wayland) is made at it (swapchain_extent.h). Render thread only.
    std::uint32_t window_width_ = 0u;
    std::uint32_t window_height_ = 0u;
    bool surface_lost_ = false;  // the driver reported it: made again from the window at the next frame
    bool driver_confirmed_ = false;  // a frame was shown: an imported driver works (launch_args gpu_driver)
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
    VkRenderPass color_pass_wide_ = VK_NULL_HANDLE;  // the same at RGBA16F: the frame shown with float_scene_
    VkRenderPass present_pass_ = VK_NULL_HANDLE;  // the swapchain image, cleared, ends presentable
    VkSampler linear_clamp_ = VK_NULL_HANDLE;
    VkDescriptorPool persistent_pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> persistent_overflow_;
    std::uint32_t persistent_overflow_sets_ = 0;  // sets of the last overflow pool made (the next has twice as many)

    // The scene target.
    FrameMap scene_map_{};  // the bound scene target's map (E-08): the part of it drawn in
    FrameMap frame_extent_{};  // the bound scene target's allocated size (frame_extent)
    FrameMap scene_extent_{};  // the size the main scene targets are made at (the render size)
    // Dynamic resolution (dynamic_resolution.h): its rectangle is the main scene's scene_map_ from begin_scene_rectangle
    // to the frame's end.
    DynamicResolution dynamic_resolution_;
    FrameMap scene_rectangle_{};
    std::uint64_t rectangle_frame_ = ~std::uint64_t{0};  // the frame whose rectangle was decided last
    // begin_frame: the frame starts with the main scene target whole.
    void apply_dynamic_resolution();
    // The smallest share of the render size (sixteenths) the running temporal upscaler takes; 8 without one.
    [[nodiscard]] unsigned dynamic_resolution_floor() const;
    // The bound target's drawn part, within its images (the viewport, scissor and render area of its passes).
    [[nodiscard]] VkExtent2D scene_part() const noexcept {
        return {std::min(scene_map_.width, scene_.color->width), std::min(scene_map_.height, scene_.color->height)};
    }
    // A depth picture for the game at the extent's size holding the whole frame (dynamic resolution's rectangle
    // resampled, point sampled), for a frame pass over the frame; its colour is draw_frame_pass's.
    bool publish_scene_depth_copy(std::uint64_t key, TextureStore& textures);
    VkRenderPass depth_copy_pass_ = VK_NULL_HANDLE;  // one depth_copy_format_ target, ends shader-readable
    VkPipeline depth_copy_pipeline_ = VK_NULL_HANDLE;
    // R32 float where the device filters it linearly (a material may sample the picture so), else 16-bit float (linear
    // filtering required of every device).
    VkFormat depth_copy_format_ = VK_FORMAT_R32_SFLOAT;
    VkFormat depth_format_ = VK_FORMAT_UNDEFINED;
    // A scene target: its targets, their framebuffer and the conversion input reading its colour.
    struct SceneSet {
        gpu::TextureRef color;
        gpu::TextureRef depth;  // its sampled view is the depth aspect alone
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkDescriptorSet color_source = VK_NULL_HANDLE;
        // MSAA (samples above one): the multisampled targets the scene target pass draws into, color and depth above being
        // their resolves (the depth's by resolve_scene_depth, a pass over depth alone).
        unsigned samples = 1u;
        gpu::TextureRef ms_color;
        gpu::TextureRef ms_depth;
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
    // The wide set is the live scene target (begin_wide_scene to end_wide_scene; scene_.wide is the live set's format).
    bool wide_live_ = false;
    // HDR output: the scene target, the output set and the frame shown are RGBA16F (SceneSet::wide), and the post chain's
    // pictures too (PostProcessorVk::set_wide), so the scene's light past white reaches the last write, which fits it
    // into the display's range once. apply_window_size makes the sets again when it changes. The wide set and its camera
    // are not used then: the scene is already wide, and HDR wants no shoulder before the last write.
    bool float_scene_ = false;
    bool wide_base_sampled_ = false;
    // A set over the live scene target's depth at its size and samples, in `format` (the wide set's, or the 2D layer's).
    bool ensure_wide_set(SceneSet& set, VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT);
    void record_wide_begin(const SceneClearValues* initial_clear);
    void record_wide_end();
    vk::SceneCameraVk scene_camera_;
    std::chrono::steady_clock::time_point camera_metered_{};
    std::uint64_t scene_depth_serial_ = 0;
    VkDescriptorSet turn_source_ = VK_NULL_HANDLE;
    const vk::Texture* turn_source_image_ = nullptr;
    std::uint64_t turn_source_frame_ = ~std::uint64_t{0};
    // The output scene target (begin_output_scene) and its size; they trade places with scene_ and scene_target_scale_ while it is active.
    SceneSet alt_scene_;
    FrameMap alt_scene_map_{};
    FrameMap alt_scene_extent_{};  // its allocated size (alt_scene_map_: the part drawn), traded with frame_extent_
    // The open target pass's set (begin_target_pass) while the scene target holds it, and each key's set between passes
    // ( its colour shared with its picture), dropped after kIdleSteps game steps (idle_clock.h) unused. A picture is sampled
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
    std::unordered_map<std::uint64_t, TargetSlot> target_slots_;
    static constexpr std::uint64_t kTargetSweepSteps = 60u;  // begin_frame's sweep: once a second of the game
    std::uint64_t target_swept_step_ = 0u;                    // idle_clock() at the last sweep
    std::vector<VkDescriptorSet> target_free_sources_;  // dropped slots' colour inputs, free to reuse
    // A slot's targets go to the open frame's retired list; its colour input waits for the fence (retired_target_sources_).
    void retire_target_slot(TargetSlot& slot);
    // Slots no pass has used for kIdleSteps game steps give their targets back; also from begin_frame, so that a scene without
    // target passes does not keep the last scene's.
    void sweep_target_slots();
    std::uint64_t target_key_ = 0;
    SceneSet target_scene_;
    FrameMap target_scene_map_{};
    FrameMap target_scene_extent_{};  // traded with frame_extent_ as target_scene_map_ with scene_map_
    bool target_pass_active_ = false;
    bool output_scene_active_ = false;
    // The scene target's colour, which the output scene's first draw sampled, waits in the sampling layout until
    // end_output_scene gives it back its attachment layout.
    bool output_source_sampled_ = false;
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
    // draw.post's layout, its colour-only passes (by format and samples) and the programs' pipelines.
    struct ModPostPass {
        VkFormat format = VK_FORMAT_UNDEFINED;
        unsigned samples = 1u;
        VkRenderPass pass = VK_NULL_HANDLE;
    };
    struct ModPostPipelineVk {
        std::uint32_t program = 0u;
        VkFormat format = VK_FORMAT_UNDEFINED;
        unsigned samples = 1u;
        VkPipeline pipeline = VK_NULL_HANDLE;  // null: the device refused it
    };
    VkSampler mod_post_samplers_[2]{};
    VkDescriptorSetLayout mod_post_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout mod_post_layout_ = VK_NULL_HANDLE;
    bool mod_post_failed_ = false;
    std::vector<ModPostPass> mod_post_passes_;
    std::vector<ModPostPipelineVk> mod_post_pipelines_;
    std::uint64_t mod_post_epoch_ = 0u;
    bool ensure_mod_post_layout();
    [[nodiscard]] VkRenderPass mod_post_pass(VkFormat format, unsigned samples);
    [[nodiscard]] VkPipeline mod_post_pipeline(const ModPostProgram& program, VkFormat format, unsigned samples,
                                               VkRenderPass pass);
    void release_mod_post();
    [[nodiscard]] bool ensure_motion_targets();
    void clear_motion_targets(bool motion = true);  // the reactive mask too where it is read
    // The layer's targets to nothing added (zero colour) and everything let through (transmittance one).
    void clear_layer_targets();
    bool layer_targets_ok_ = false;  // create_layouts: the format and the attachment count are there
    bool layer_window_ = false;
    bool reactive_scene_ = false;  // this scene's blended draws write the reactive mask (begin_motion_scene)
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
    bool temporal_wide_ = false;  // made for the float scene (HDR output): colour past white
    unsigned temporal_quality_ = 0u;
    bool temporal_history_valid_ = false;
    bool scene_temporal_ = false;
    bool scene_motion_ = false;
    bool motion_cleared_ = false;  // begin_cleared_motion_scene opened the motion pass cleared for the next scene
    void clear_depth_in_pass(float reversed_depth, const VkRect2D& rect);
    // The single-sample depth target alone (resolve_scene_depth): depth cleared, stencil loaded and stored.
    VkRenderPass depth_alone_pass_ = VK_NULL_HANDLE;
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
    TemporalInterpHold temporal_interp_hold_;
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
    // The same into an RGBA16F picture (color_pass_wide_: the frame shown with float_scene_).
    VkPipeline conversion_pipeline_wide_ = VK_NULL_HANDLE;
    VkPipeline display_filter_pipeline_wide_ = VK_NULL_HANDLE;
    // HDR output's 2D layer (begin_layer_2d): its set while the float set is live and the float set while it is, the
    // layer as it started (layer2d_base_, single-sample: what the merge compares with), and the merge's layout (t0 the
    // layer as drawn, t1 its start) and pipelines into the float scene pass, by log2 samples.
    SceneSet layer2d_set_;
    gpu::TextureRef layer2d_base_;
    VkDescriptorSetLayout layer2d_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout layer2d_layout_ = VK_NULL_HANDLE;
    std::array<VkPipeline, 4> layer2d_merge_pipelines_{};
    bool layer2d_live_ = false;
    bool layer2d_failed_ = false;  // the merge could not be made: the 2D layer is drawn in the float scene
    std::uint64_t layer2d_used_step_ = 0u;  // idle_clock() when a frame last drew a 2D layer
    [[nodiscard]] VkPipeline layer2d_merge_pipeline(unsigned samples_log2);
    // The renderer's own draws into the scene target pass, per scene target sample count (log2), made at first use: masked clears,
    // and whole-colour and depth clears as draws (DeviceQuirks::clear_with_draw only; null otherwise), and the output scene's
    // first draw, the scene resampled as conversion_pipeline_ and display_filter_pipeline_ do (enter_output_scene).
    enum class SceneUtility : unsigned { ClearRgb, ClearAlpha, ClearRgba, ClearDepth, Conversion, DisplayFilter, Count };
    std::array<std::array<VkPipeline, kSceneVariantCount>, static_cast<std::size_t>(SceneUtility::Count)> scene_utility_pipelines_{};
    [[nodiscard]] VkPipeline scene_utility_pipeline(SceneUtility kind);
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
    std::vector<VkDescriptorSet> free_sampler_tables_;  // sets of tables of an older sampler configuration, written again
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
    // The driver takes a variable-length set's room from its layout's upper bound, not from the length asked for (probed
    // when the layouts are made): the layout is kBindlessTextureSlotsWhole long and every image set has its full length.
    bool texture_array_whole_ = false;
    const void* mirrored_backend_ = nullptr;
    TextureStore* mirrored_store_ = nullptr;
    TextureStore* uploads_store_ = nullptr;  // the store whose uploads the transfer queue may take (attach_texture_uploads)
    // The texture lists' chunks (draw_tables, set 0 binding 5; core::TextureLists): host-visible storage buffers.
    std::vector<std::shared_ptr<vk::Buffer>> texture_list_chunks_;
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

    VkCommandPool utility_pool_ = VK_NULL_HANDLE;  // one-time submissions (run_one_time)

    // Captures and telemetry.
    std::string backbuffer_capture_path_;
    // The pictures copied out (the presented picture and the display picture), each its own host-visible buffer.
    using Captures = PictureCaptures<std::shared_ptr<vk::Buffer>>;
    Captures captures_;
    // A buffer for a capture of `bytes`: a spare one, or a new one.
    [[nodiscard]] std::shared_ptr<vk::Buffer> capture_buffer(std::uint64_t bytes, const char* name);
    std::atomic<double> measured_present_hz_{0.0};
    double display_refresh_hz_ = 0.0;
    // The display varies its refresh with the presents (next_variable_refresh): display_refresh_hz_ is then the cap that keeps
    // it in its range, and presents are spaced at the cap.
    bool variable_refresh_ = false;
    // The display's rate is read again twice a second on the clock, whatever the present rate (an unlimited one presents
    // thousands a second).
    galaxy::ClockInterval display_refresh_{500, true};
    std::int64_t measured_present_window_qpc_ = 0;
    std::int64_t swap_present_qpc_ = 0;   // every present's (last_present_qpc)
    // VK_KHR_present_id / present_wait: the id of each present on a FIFO swap chain (rising for the renderer's life), and the
    // last one and the one before it that went in on this swap chain (0: none, nothing to wait for); present_swapchain_image
    // waits for one of them to show before queueing the next frame, 100 ms at most.
    std::uint64_t present_id_counter_ = 0;
    // The pacing recorder's display reports from present timing (VK_EXT_present_timing).
    void report_present_times();
    // The swap chain's own present clock read with the runtime's (local_clock); false where the device cannot.
    bool calibrate_present_clock();
    std::uint64_t last_present_id_ = 0;
    std::uint64_t present_id_before_last_ = 0;
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
    FrameGpuTime frame_gpu_;  // the frame's GPU time: the governor's and the interpolation's measures
    bool scene_drawn_ = false;
    std::array<bool, kFramesInFlight> frame_scene_drawn_{};
    // Presents the acquired image (swap_image_) after its submission.
    void present_swapchain_image();
    [[nodiscard]] vk::TemporalUpscaleInputs temporal_inputs(const vk::Texture& color, const vk::Texture& depth, const vk::Texture& motion,
                                                            const vk::Texture& reactive, const vk::Texture& output) const;
    // A depth or colour picture of the open scene target through open_frame_for_pass, until close_frame_for_pass.
    std::uint64_t frame_pass_color_key_ = 0u;
    std::uint64_t frame_pass_depth_key_ = 0u;
    std::uint64_t window_size_refused_ms_ = 0;
    PresentPacer present_pacer_;
    // VK_GOOGLE_display_timing (Android), as Swappy paces: each present asks for its slot (PresentSlots: the vsync
    // a pipeline depth after its moment or frame start) half a vsync early, so the compositor shows it at that vsync.
    // The past presentation times set the depth and report how the presents went ([present-timing], every ten seconds).
    std::int64_t present_moment_ = 0;
    struct PresentTiming {
        std::uint32_t next_id = 0;
        std::int64_t vsync_ns = 0;  // the display's vsync interval (Choreographer)
        std::array<std::uint32_t, 64> requested_id{};
        std::array<std::int64_t, 64> requested_period{};
        std::array<std::int64_t, 64> requested{};  // each presentID's slot, by presentID % 64
        std::array<std::int64_t, 64> called{};     // when each was presented (CLOCK_MONOTONIC)
        PresentSlots slots;
        galaxy::ClockInterval report{10'000};  // [present-timing] every ten seconds
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
    // Every ten seconds ([vk-gpu]): the GPU's frame time (timestamps), and how often the scene target pass reopened (each
    // reopening loads and stores the whole scene target on a tiled GPU) and the scene target was copied.
    galaxy::ClockInterval cpu_report_{10'000};  // the report every ten seconds
    struct GpuWindow {
        double ms = 0.0;
        double max_ms = 0.0;
        unsigned frames = 0;     // pictures with GPU times read
        unsigned recorded = 0;   // pictures recorded since the last report
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
    using GpuPass = ::galaxy::render::GpuPass;
    static constexpr unsigned kGpuPassSlots = 512;
    static constexpr unsigned kNoPassMark = ~0u;
    void gpu_pass(GpuPass pass);
    [[nodiscard]] unsigned gpu_pass_mark(GpuPass pass, bool pass_begins);
    void gpu_pass_restore(GpuPass outer);
    void read_gpu_passes();
    void prepare_gpu_pass_queries();
    bool profile_frame_ = false;
    class GpuPassScope {
    public:
        GpuPassScope(RendererVk& renderer, GpuPass pass) : renderer_(renderer), outer_(renderer.current_pass_) {
            renderer_.gpu_pass(pass);
        }
        ~GpuPassScope() { renderer_.gpu_pass_restore(outer_); }
        GpuPassScope(const GpuPassScope&) = delete;
        GpuPassScope& operator=(const GpuPassScope&) = delete;
    private:
        RendererVk& renderer_;
        GpuPass outer_;
    };
    VkQueryPool pass_queries_ = VK_NULL_HANDLE;
    unsigned open_pass_mark_ = kNoPassMark;  // the mark written as the open scene pass began
    // The frame's shader invocations (gpu-stats): vertex, clipped
    // primitives, fragment and compute, in VkQueryPipelineStatisticFlagBits' order.
    VkQueryPool stats_queries_ = VK_NULL_HANDLE;
    std::array<bool, kFramesInFlight> stats_pending_{};
    std::array<double, 4> stats_sum_{};
    std::array<std::array<GpuPass, kGpuPassSlots>, kFramesInFlight> pass_marks_{};
    std::array<unsigned, kFramesInFlight> pass_mark_count_{};
    GpuPass current_pass_ = GpuPass::Scene;
    bool pass_recording_ = false;  // between begin_frame and end_frame
    // The settings page's timing: asked for by gpu_timing_wanted_ (every frame is profiled while it is); the page's
    // averages and the logs' window (window_frames: the frames read, which gpu-stats is divided by too).
    std::atomic<bool> gpu_timing_wanted_{false};
    GpuPassTimes pass_times_;
};

using Renderer = RendererVk;

}  // namespace galaxy::render
