// SPDX-License-Identifier: GPL-3.0-only
// The renderer on Metal (docs/APPLE.md), with the interface RenderCore uses on RendererD3D12 and RendererVk.
//
// The scene target is a colour texture (RGB10A2) and a depth texture (Depth32Float_Stencil8) drawn through one render
// command encoder, the scene pass; a scene target copy, screen probe or present ends that encoder, and the next scene
// target operation begins another, loading what the last stored. kFramesInFlight frames are recorded ahead of the GPU.
// A frame is a row of command buffers committed in order: the frame's texture uploads (a blit encoder), then its
// draws; a pass recorded on several threads (parallel_record_*) runs inside the scene pass on a tile-based GPU, else in a
// command buffer of its own in that row.
//
// Draw bindings follow gpu::Encoder (core/gpu_encoder.h, mtl/shader_msl.h). Addresses (gpu::Address) name a buffer
// and an offset (mtl::Context::resolve). Metal's clip space is Direct3D's: nothing is turned over.
//
// Plain C++: Metal's objects are mtl::Object here (mtl_context.h), and the Objective-C++ sources turn them back.
#pragma once

#include "galaxy/content_viewport.h"
#include "galaxy/render/ambient_occlusion_objects.h"
#include "galaxy/render/display_modes.h"
#include "galaxy/render/frame_pass.h"
#include "galaxy/render/gpu_types.h"
#include "galaxy/render/gpu_upload_ring.h"
#include "galaxy/render/host_window.h"
#include "galaxy/launch_args.h"
#include "galaxy/render/mtl/draw_types.h"
#include "galaxy/render/mtl/fx_mtl.h"
#include "galaxy/render/mtl/mtl_context.h"
#include "galaxy/render/rt/rt_backend.h"
#include "galaxy/render/rt/rt_denoiser.h"
#include "galaxy/render/mtl/temporal_upscaler_mtl.h"
#include "galaxy/render/mtl/utility_msl.h"
#include "galaxy/render/post_process_settings.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/renderer_common.h"
#include "galaxy/render/rt_types.h"
#include "galaxy/render/window_owner.h"
#include "galaxy/render_api.h"
#include "render/core/draw_record.h"
#include "render/core/gpu_encoder.h"

#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace galaxy::render {

class TextureStore;
struct TextureHandle;

class RendererMtl final {
public:
    using SubmissionCallback = void (*)(void* context, std::uint64_t fence_value);

    bool initialize(int window_width, int window_height, const FrameMap& frame);
    // Why initialize failed, when the device said.
    [[nodiscard]] std::string startup_failure() const { return !init_error_.empty() ? init_error_ : mtl::Context::get().failure(); }
    std::string init_error_;
    // What outlives this renderer when a lost device is left behind (RenderCore::restart_renderer): the window.
    struct Kept {
        host_window::NativeWindow window = nullptr;
    };
    void set_window_owner(WindowOwner& owner) noexcept { window_owner_ = &owner; }
    [[nodiscard]] Kept take_apart_lost_device();
    void keep(const Kept& kept, bool) noexcept { window_ = kept.window; }
    void shutdown();
    [[nodiscard]] UploadBuffer create_upload_buffer(std::size_t bytes, const char* name);

    // SMAA or FXAA of the 3D scene in the scene target before the 2D layer draws (mtl::PostProcessor).
    void record_scene_aa();
    // Ambient occlusion of the opaque scene (mtl::AmbientOcclusion).
    [[nodiscard]] bool ambient_occlusion_ready() const noexcept { return ao_.ready(); }
    void set_scene_ao_view(const RtView& view) noexcept { scene_ao_view_ = view; }
    void set_scene_ao_objects(std::vector<ao::ObjectOccluder>& objects) noexcept { scene_ao_objects_.swap(objects); }
    void record_scene_ao();
    // The game's bloom (mtl::GameBloom): `kind` a NEBULA_PASS_BLOOM_*, `a` and `b` its values.
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
    [[nodiscard]] double gpu_ms_per_tick() const noexcept { return 0.0; }  // the tracer's measure: none yet
    // Smoothed GPU time of a frame that drew the scene (0 until measured): the tracer's build budget (5.3 G-3).
    [[nodiscard]] double scene_gpu_ema_ms() const noexcept { return native_scene_gpu_ema_ms_; }
    void set_scene_view(const std::array<float, 12>* view) noexcept {
        scene_view_known_ = view != nullptr;
        if (view != nullptr) {
            scene_view_ = *view;
        }
    }
    // The effects layer: while its window is open, blended draws that never read the scene also write its colour and
    // transmittance targets (aux 3).
    [[nodiscard]] bool layer_window() const noexcept { return layer_window_; }
    void begin_layer_window();
    [[nodiscard]] std::int64_t last_present_qpc() const noexcept { return swap_present_qpc_; }
    // The 3D scene in a wide target while `wanted` (the enhanced lighting), its camera at the end (mtl::SceneCamera).
    bool begin_wide_scene(bool wanted, const SceneClearValues* initial_clear = nullptr);
    void end_wide_scene();
    [[nodiscard]] bool temporal_mode() const;
    // The upscaler reads the motion but no reactive mask (SGSR 2): every draw of the scene shares the motion pass.
    [[nodiscard]] bool motion_pass_only() const;
    [[nodiscard]] bool has_layer_targets() const noexcept { return draw_layout_.layer_targets; }
    // The draws' textures: with arrays (bindless()) every texture sits in one array at its slot and every sampler in
    // another, and a draw names its maps by their places in a list of {texture slot, sampler slot} pairs.
    [[nodiscard]] bool bindless() const noexcept { return bindless_; }
    [[nodiscard]] std::uint32_t texture_slot(const TextureHandle& handle) const noexcept;
    [[nodiscard]] std::uint32_t sampler_slot(const SamplerKey& key);
    // `count` pairs into this frame's texture lists: the buffer they went to and the first pair's place in it
    // (DrawRoot::table).
    void write_texture_list(const std::uint32_t* pairs, std::uint32_t count, gpu::Address& lists, std::uint32_t& first);
    // The arrays and the lists, for the draws that follow.
    void bind_draw_textures(gpu::Address lists);
    // Without arrays: a draw's own tables of up to table_slots() maps.
    [[nodiscard]] std::uint32_t table_slots() const noexcept { return mtl::kBoundTableSlots; }
    [[nodiscard]] gpu::Table texture_table_for(const TextureHandle* handles, unsigned count, const TextureStore& cache);
    // A table that lives until replaced: `old` is given back once the GPU is past it.
    [[nodiscard]] gpu::Table persistent_texture_table(const TextureHandle* handles, unsigned count, const TextureStore& cache,
                                                      gpu::Table old);
    void retire_texture_table(gpu::Table table);
    // A target whose id is freed: every picture it has goes.
    void free_target(std::uint32_t id, TextureStore& textures);
    // Moves when every sampler table is made again, or the texture array's null slot moved.
    [[nodiscard]] std::uint32_t sampler_generation() const noexcept;
    void trace_rings() const;
    void copy_fg_hudless_display(const DisplayTexture&) {}
    void attach_texture_uploads(TextureStore& cache);
    // The video memory in use and its budget, read at a frame boundary. False: not known.
    [[nodiscard]] bool video_memory(std::uint64_t& usage, std::uint64_t& budget) const noexcept;
    void wait_for_gpu_idle();
    void pump_messages() {}
    [[nodiscard]] bool quit_requested() const noexcept;
    // A covered or minimised window is the host's word (it stops the game's frames).
    [[nodiscard]] bool occluded() const noexcept { return false; }
    void probe_occlusion() noexcept {}

    void lose_device();
    void begin_frame();
    std::uint64_t end_frame(bool present_swap_chain = true, SubmissionCallback on_submitted = nullptr,
                            void* submission_context = nullptr, bool generated = false);
    [[nodiscard]] std::uint64_t completed_fence_value() const;
    [[nodiscard]] std::uint64_t submitted_fence_value() const noexcept { return last_submitted_; }
    // What the render core holds that the breakdown line (log_video_memory) adds to the renderer's own figures.
    struct VideoMemoryOutside {
        std::uint64_t staging_bytes = 0u;
        std::uint64_t mesh_page_bytes = 0u, mesh_live_bytes = 0u, slot_bytes = 0u;
        std::uint64_t mesh_local_bytes = 0u;
        std::uint32_t mesh_pages = 0u, mesh_empty_pages = 0u, slots = 0u;
        std::uint64_t picture_bytes = 0u;
        std::uint32_t picture_count = 0u;
        std::uint64_t pipelines = 0u;
    };
    void log_video_memory(const VideoMemoryOutside& outside);
    [[nodiscard]] static std::uint64_t allocation_bytes(const mtl::Texture* texture) noexcept {
        return texture != nullptr ? texture->allocation_bytes : 0u;
    }
    // GPU time by pass (render_api.h GpuPassTime): any thread.
    void set_gpu_timing(bool on) noexcept { gpu_timing_wanted_.store(on, std::memory_order_relaxed); }
    [[nodiscard]] bool gpu_timing_on() const noexcept { return gpu_timing_wanted_.load(std::memory_order_relaxed); }
    // What the ray tracer records through on the other renderers (renderer_vk.h). There is no tracer on Metal yet
    // (mtl/rt_backend_mtl.cpp: it never starts), so the render core never comes here; they say there is nothing.
    struct RtCommands {
        void* commands = nullptr;
        void* descriptors = nullptr;
    };
    [[nodiscard]] RtCommands rt_commands_begin() { return {}; }
    void rt_commands_end() {}
    [[nodiscard]] bool rt_depth_begin(rt::TextureView&, unsigned&, unsigned&, rt::ImageFormat&) { return false; }
    void rt_depth_end() {}
    void rt_copy_to_scene(void*) {}
    [[nodiscard]] bool rt_items_begin() { return false; }
    [[nodiscard]] bool rt_light_begin(rt::TextureView&, rt::TextureView&, unsigned&, unsigned&) { return false; }
    void rt_light_end() {}
    [[nodiscard]] bool rt_reactive_begin(rt::TextureView&) { return false; }
    void rt_reactive_end() {}
    [[nodiscard]] rt::Denoiser* rt_denoiser() noexcept { return nullptr; }
    [[nodiscard]] std::uint32_t rt_publish_light(void*, TextureStore&, unsigned = 0u) { return 0u; }
    void rt_unpublish_lights(TextureStore&) {}
    void rt_wait_idle() { wait_for_gpu_idle(); }
    [[nodiscard]] std::size_t gpu_pass_times(GpuPassTime* out, std::size_t capacity) const noexcept;
    void scene_jitter_info(unsigned& index, unsigned& phases) const noexcept {
        index = scene_jitter_index_;
        phases = scene_jitter_phases_;
    }
    void wait_for_fence_value(std::uint64_t fence_value);

    void bind_scene();
    [[nodiscard]] gpu::TextureRef swap_scene_color(gpu::TextureRef fresh);
    void clear_scene(std::uint32_t clear_rgba, bool color_enable, bool alpha_enable, bool depth_enable, unsigned rect_x = 0,
                     unsigned rect_y = 0, unsigned rect_w = 0, unsigned rect_h = 0, std::uint8_t pixel_format = 0);
    // What the walker draws through. Aux targets beside the scene target: 0 none, 1 motion, 2 reactive, 3 reactive and
    // the effects layer, 4 the motion pass for a draw that writes no motion.
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
    [[nodiscard]] bool can_take_scene() const noexcept;
    bool take_scene(std::uint64_t key, gpu::TextureRef fresh, TextureStore& textures, gpu::TextureRef& released);
    void open_frame_for_pass(std::uint64_t color_key, std::uint8_t channels, std::uint64_t depth_key, TextureStore& textures);
    void close_frame_for_pass(TextureStore& textures);
    [[nodiscard]] bool draw_frame_pass(const FramePassRequest& params, TextureStore& textures);
    // `black`: the frame is presented black (the pass clears; the display picture is not drawn).
    bool present(const DisplayTexture* source, bool capture_backbuffer = false, bool black = false);

    [[nodiscard]] bool begin_output_scene(bool resolve_msaa_only = false);
    [[nodiscard]] bool begin_target_pass(std::uint64_t key, std::uint32_t width, std::uint32_t height);
    void end_target_pass(std::uint8_t channels, TextureStore& textures);
    [[nodiscard]] bool target_picture(std::uint64_t key, TextureHandle& out) const;
    [[nodiscard]] bool target_pass_active() const noexcept { return target_pass_active_; }
    void end_output_scene();
    [[nodiscard]] bool output_scene_active() const noexcept { return output_scene_active_; }
    // Temporal upscaling.
    [[nodiscard]] bool scene_temporal_begin(float& jitter_x, float& jitter_y);
    [[nodiscard]] bool begin_motion_scene();
    [[nodiscard]] bool begin_cleared_motion_scene(std::uint32_t clear_rgba);
    [[nodiscard]] bool defer_scene_depth_clear(std::uint32_t clear_rgba);
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

    [[nodiscard]] double measured_present_hz() const noexcept { return measured_present_hz_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t frame_index() const noexcept { return frame_index_; }

    // Each point's depth (0 near, 1 far) into slot `slot`'s readback, in the open frame where the stream is now;
    // screen_probes_finished names the values once that frame's fence has passed. `compared` probes asked inside the
    // scene pass are answered there, by a depth test of each point (1 seen, 0 hidden).
    [[nodiscard]] bool record_screen_probes(std::size_t slot, std::span<const ScreenProbe> probes, bool compared);
    [[nodiscard]] const float* screen_probes_finished(std::size_t slot);

    // Test captures: the scene target and the presented picture as PPM files (paths from set_debug_capture_paths).
    bool debug_copy_scene_to_readback();
    void debug_log_readback();
    void debug_log_backbuffer_readback();
    bool debug_copy_selected_display_to_readback(const DisplayTexture& source);
    void debug_log_display_readback();
    void set_debug_capture_paths(std::string scene_ppm_path, std::string backbuffer_ppm_path);

    [[nodiscard]] gpu::Device* device() const noexcept { return static_cast<gpu::Device*>(mtl::Context::get().device()); }
    [[nodiscard]] const FrameMap& frame_map() const noexcept { return scene_map_; }
    [[nodiscard]] std::uint8_t scene_samples_log2() const noexcept {
        return static_cast<std::uint8_t>(std::countr_zero(scene_.samples));
    }
    [[nodiscard]] std::uint8_t scene_variant() const noexcept {
        return static_cast<std::uint8_t>(scene_samples_log2() | (scene_.wide ? kSceneVariantHdr : 0u) |
                                         (target_pass_active_ ? kSceneVariantTarget : 0u));
    }
    [[nodiscard]] void* pipeline_root() noexcept { return &draw_layout_; }
    [[nodiscard]] void* pipeline_backend() noexcept { return this; }
    [[nodiscard]] double display_refresh_hz() const noexcept { return display_refresh_hz_; }
    [[nodiscard]] bool vblank_grid(std::int64_t& vblank, std::int64_t& period) const noexcept;
    [[nodiscard]] UploadRing& vertex_ring() noexcept { return vertex_ring_; }
    [[nodiscard]] UploadRing& index_ring() noexcept { return index_ring_; }
    [[nodiscard]] UploadRing& constant_ring() noexcept { return constant_ring_; }
    [[nodiscard]] UploadRing& matrix_ring() noexcept { return matrix_ring_; }
    [[nodiscard]] unsigned frame_slot() const noexcept { return frame_slot_; }
    void bind_draw_tables(gpu::Table, gpu::Table) noexcept {}
    [[nodiscard]] gpu::TextureArrays draw_texture_arrays(gpu::Address lists);

    // Parallel recording of a draw pass (draw_walker.cpp): the pass was recorded as ops in chunks; each chunk is
    // replayed into a render command encoder of its own, on any thread, and the chunks run in order after the commands
    // recorded so far (a run of chunks with the same targets is one parallel render pass, in a command buffer of its
    // own). begin says each chunk's draw targets (false: not possible now, nothing changed); chunk replays chunk `i`;
    // end closes them. replay_draws replays one chunk inline.
    [[nodiscard]] bool parallel_record_begin(const std::uint8_t* chunk_aux, unsigned chunk_count);
    void parallel_record_chunk(unsigned chunk, const gpu::DrawOp* ops, std::size_t count);
    void parallel_record_end();
    void replay_draws(std::uint8_t aux, const gpu::DrawOp* ops, std::size_t count);
    [[nodiscard]] gpu::Table sampler_table_for(const SamplerKey* keys, unsigned count);

    // For the renderer's own parts (post chain, bloom, occlusion, camera): the command buffer being recorded, with no
    // encoder open after end_scene_pass; and what to call after encoding with one of their own.
    enum class GpuPass : std::uint8_t {
        Scene, Resolve, FramePass, DisplayCopy, Bloom, SceneAa, SceneAo, Upscale, RayTracing, Present, Camera,
        AoDistance, AoOcclusion, AoDenoise, AoUpsample, AoApply, Count
    };

private:
    struct Frame {
        // id<MTLCommandBuffer>s in the order they are committed; the last is the one being recorded (commands_).
        std::vector<mtl::Object> lists;
        mtl::Object uploads = nullptr;      // id<MTLCommandBuffer>: the frame's texture uploads, committed first
        mtl::Object upload_blit = nullptr;  // id<MTLBlitCommandEncoder> open on it
        std::uint64_t fence = 0;            // the submission that used this slot last
        std::vector<gpu::TextureRef> retired;  // kept until the slot's fence passes
        std::vector<gpu::BufferRef> retired_buffers;
        std::vector<mtl::Object> retired_objects;  // Metal objects (views) released when the slot's fence passes
        // A draw's own tables this frame (no arrays): reused each frame.
        std::deque<mtl::BoundTextures> tables;
        std::size_t tables_used = 0;
        // Visibility results for points tested in the frame's scene passes: one buffer a pass that tested any.
        std::vector<std::shared_ptr<mtl::Buffer>> visibility;
        std::size_t visibility_used = 0;
        // GPU start and end times of the frame's command buffers, written by their completion (seconds; 0: none).
        std::shared_ptr<std::array<std::atomic<double>, 2>> gpu_time;
        // With the passes timed (profile_frame_): the GPU's seconds on the frame's command buffers by the pass each was
        // begun for, summed by their completions; the last entry is the frame's texture uploads.
        std::shared_ptr<std::array<std::atomic<double>, static_cast<std::size_t>(GpuPass::Count) + 1u>> pass_time;
        bool pass_timed = false;
    };
    struct FramePassTarget {
        gpu::TextureRef texture;
        unsigned width = 0;
        unsigned height = 0;
    };
    struct Readback {
        std::shared_ptr<mtl::Buffer> buffer;  // shared memory
        std::uint64_t fence = 0;              // 0: idle
    };
    // A scene target: its colour and depth, and with MSAA the multisampled targets the pass draws into (the colour
    // resolves into `color` as each pass ends; the depth is resolved on demand, resolve_scene_depth).
    struct SceneSet {
        gpu::TextureRef color;
        gpu::TextureRef depth;
        unsigned samples = 1u;
        gpu::TextureRef ms_color;
        gpu::TextureRef ms_depth;
        bool depth_dirty = false;
        std::uint64_t depth_serial = 0;  // its depth target's
        bool wide = false;               // the wide scene's colour (kSceneVariantHdr), over the scene target's own depth
    };
    struct TargetSlot {
        SceneSet set;
        FrameMap map{};
        std::uint64_t used = 0;
        std::uint32_t width = 0, height = 0;  // frame units
    };
    // The pictures draws sample by key (target passes' and captures'), each through its own view slot.
    struct Picture {
        std::uint32_t view = ~0u;
        gpu::TextureRef viewed;
        std::uint32_t width = 0, height = 0;  // frame units, the size draws sample it at
        std::uint8_t channels = 0;
        mtl::Object sampled = nullptr;  // id<MTLTexture>: the view reading `channels` (none: the texture itself)
    };
    struct MotionTargets {
        gpu::TextureRef motion;
        gpu::TextureRef reactive;
        gpu::TextureRef layer_color;  // the effects layer: what its draws added, RGBA16F
        gpu::TextureRef layer_trans;  // and the product of their destination factors
        FrameMap map{};
    };
    // The window's CAMetalLayer as the renderer set it.
    struct Layer {
        mtl::Object layer = nullptr;  // CAMetalLayer*, the window's (not retained)
        std::uint32_t width = 0, height = 0;  // the drawables' size in pixels
        mtl::Format format = mtl::Format::Bgra8;
        bool vsync = true;
        bool hdr_asked = false;
        bool hdr = false;  // extended range: RGBA16F drawables in a linear extended colour space
    };
    // A pipeline of the renderer's own passes: a full-screen triangle (no vertex input) into `colors` (and depth).
    struct UtilityKey {
        mtl::UtilityShader vertex = mtl::UtilityShader::BlitVs;
        mtl::UtilityShader fragment = mtl::UtilityShader::BlitPs;
        std::array<mtl::Format, 4> colors{};
        std::uint8_t color_count = 0;
        std::uint8_t write_mask = 0;  // of the first colour (MTLColorWriteMask); the others are never written
        std::uint8_t samples = 1;
        bool depth = false;           // a depth and stencil target is attached
        friend bool operator==(const UtilityKey&, const UtilityKey&) = default;
    };
    struct UtilityKeyHasher {
        std::size_t operator()(const UtilityKey& key) const noexcept;
    };
    enum class DepthUse : std::uint8_t { None, Write, TestNearer, Count };

    bool create_device();
    bool create_layouts();
    void report_adapter();
    bool configure_layer();
    bool create_scene_targets(const FrameMap& map, unsigned samples);
    bool create_scene_set(SceneSet& set, const FrameMap& map, unsigned samples = 1u, unsigned width = kFrameUnits,
                          unsigned height = kFrameUnits, bool target = false);
    void destroy_scene_set(SceneSet& set);
    [[nodiscard]] unsigned wanted_msaa_samples() const;
    void resolve_scene_depth();
    [[nodiscard]] bool scene_fits_memory(const FrameMap& map) const;
    void apply_window_size();
    [[nodiscard]] mtl::Object utility_pipeline(const UtilityKey& key);          // id<MTLRenderPipelineState>
    [[nodiscard]] mtl::Object utility_function(mtl::UtilityShader shader);     // id<MTLFunction>
    // The pipeline of one of the renderer's own draws in the open scene pass (its targets and samples).
    [[nodiscard]] mtl::Object scene_pass_pipeline(mtl::UtilityShader fragment, std::uint8_t write_mask);
    // A draw of three vertices with `pipeline` in `encoder` (an id<MTLRenderCommandEncoder>): the depth use, the
    // viewport over `width` x `height`, `scissor`, `constants` pushed to both functions, and `texture` at index 0 with
    // the linear clamp sampler. `viewport`: the viewport when it is not the whole target.
    void utility_draw(mtl::Object encoder, mtl::Object pipeline, DepthUse depth, std::uint32_t width, std::uint32_t height,
                      const gpu::Rect& scissor, const void* constants, std::size_t constant_bytes, mtl::Object texture,
                      const gpu::Rect* viewport = nullptr);
    // A render pass of the frame's command buffer drawing into `color` whole (its old contents dropped, or cleared to
    // black when `clear`): the id<MTLRenderCommandEncoder>, the caller's to end with end_own_pass.
    [[nodiscard]] mtl::Object begin_color_pass(mtl::Object color, bool clear, const char* name);
    void end_own_pass(mtl::Object encoder);
    // The frame's next command buffer: the one being recorded is done (the scene pass ended).
    // With the passes timed, the buffer's GPU time counts as `pass`'s.
    void next_command_buffer(GpuPass pass);

    // Frame recording.
    void begin_scene_pass(const SceneClearValues* clear = nullptr, const char* caller = __builtin_FUNCTION());
    // With parallel_scene_pass_ the pass is a parallel encoder and pass_ its encoder for the draws in order.
    void begin_scene_pass_with(unsigned aux, const SceneClearValues* clear = nullptr, bool discard_color = false,
                               const char* caller = __builtin_FUNCTION(), unsigned clear_depth = 0u);
    // pass_, new in the open scene pass: the textures its draws read, the walker's encoder, the scene's rectangles.
    void attach_scene_encoder();
    // The chunks as encoders of the open parallel scene pass, between the draws before them and those after.
    [[nodiscard]] bool parallel_record_in_pass(const std::uint8_t* chunk_aux, unsigned chunk_count);
    // The scene pass's descriptor (an MTLRenderPassDescriptor*, the caller's to release) for the live set.
    [[nodiscard]] mtl::Object scene_pass_descriptor(unsigned aux, const SceneClearValues* clear, bool discard_color,
                                                    unsigned clear_depth, mtl::Object visibility);
    [[nodiscard]] unsigned effective_draw_aux(unsigned aux) const noexcept {
        return aux == 4u ? 1u : aux == 3u ? (layer_window_ ? 3u : 2u) : aux;
    }
    void end_scene_pass(const char* caller = __builtin_FUNCTION());
    const char* scene_pass_closed_by_ = "frame start";
    void reset_draw_bindings() noexcept;
    void sync_post_process();
    void trim_idle_effect_targets();
    [[nodiscard]] bool wants_vsync() const;
    [[nodiscard]] bool wants_hdr() const;
    void read_display_hdr();
    void retire(gpu::TextureRef texture);
    void retire_object(mtl::Object object);
    void reset_sampler_frame();
    void write_sampler(unsigned slot, mtl::Object sampler);
    void fill_sampler_array(mtl::Object sampler);
    [[nodiscard]] mtl::Object sampler_for(const SamplerKey& key, const RenderConfig& cfg);  // id<MTLSamplerState>
    void write_table(mtl::BoundTextures& table, const TextureHandle* handles, unsigned count, const TextureStore& cache);
    void record_conversion_blit(mtl::Texture& dest, unsigned dest_w, unsigned dest_h, std::uint8_t copy_format, float src_x,
                                float src_y, float step_x, float step_y);
    void refresh_display_rate();
    void follow_window_and_resize();
    void report_present_stats();
    void grow_rings();

    WindowOwner* window_owner_ = nullptr;
    host_window::NativeWindow window_ = nullptr;
    bool window_shown_ = false;
    bool keep_window_ = false;
    Layer layer_;
    bool layer_dirty_ = false;
    mtl::Object drawable_ = nullptr;  // id<CAMetalDrawable> taken for this frame's present
    std::unique_lock<std::timed_mutex> surface_lock_;

    std::array<Frame, kFramesInFlight> frames_{};
    unsigned frame_slot_ = 0;
    Frame* frame_ = &frames_[0];
    mtl::Object commands_ = nullptr;  // id<MTLCommandBuffer> where work records (frame_->lists.back())
    mtl::Object pass_ = nullptr;      // id<MTLRenderCommandEncoder>: the open scene pass
    // id<MTLParallelRenderCommandEncoder>: the open scene pass when it is a parallel one (pass_ is then one of its
    // encoders). A tile-based GPU's scene passes are, once a walk has recorded a pass in chunks: the chunks then run in
    // the pass, where a pass of their own stored the targets and loaded them again before and after.
    mtl::Object scene_parallel_ = nullptr;
    bool parallel_scene_pass_ = false;
    std::uint64_t frame_index_ = 0;
    std::uint64_t last_submitted_ = 0;

    // What pipelines are built against, and the renderer's own.
    mtl::RenderLayout draw_layout_{};
    mtl::Object linear_clamp_ = nullptr;  // id<MTLSamplerState>
    std::array<mtl::Object, static_cast<std::size_t>(DepthUse::Count)> depth_uses_{};  // id<MTLDepthStencilState>
    std::unordered_map<UtilityKey, mtl::Object, UtilityKeyHasher> utility_pipelines_;
    std::array<mtl::Object, static_cast<std::size_t>(mtl::UtilityShader::Count)> utility_functions_{};
    mtl::Object probe_pipeline_ = nullptr;  // id<MTLComputePipelineState>

    // The scene target.
    FrameMap scene_map_{};
    SceneSet scene_;
    SceneSet wide_scene_;
    bool ensure_wide_set(SceneSet& set);
    void record_wide_begin(const SceneClearValues* initial_clear);
    void record_wide_end();
    std::chrono::steady_clock::time_point camera_metered_{};
    std::uint64_t scene_depth_serial_ = 0;
    SceneSet alt_scene_;
    FrameMap alt_scene_map_{};
    std::unordered_map<std::uint64_t, Picture> pictures_;
    bool publish_picture(std::uint64_t key, std::uint32_t width, std::uint32_t height, gpu::TextureRef texture,
                         std::uint8_t channels, TextureStore& textures);
    bool publish_frame_pass(const FramePassRequest& params, gpu::TextureRef texture, TextureStore& textures);
    void drop_picture(Picture& picture, TextureStore& textures);
    static constexpr std::uint64_t kTargetIdleFrames = 600u;  // frames at the game's 60 a second
    [[nodiscard]] std::uint64_t target_idle_frames() const noexcept {
        const double rate = measured_present_hz_.load(std::memory_order_relaxed);
        return kTargetIdleFrames * std::max<std::uint64_t>(1u, static_cast<std::uint64_t>(rate / kGameStepRate + 0.5));
    }
    std::unordered_map<std::uint64_t, TargetSlot> target_slots_;
    void retire_target_slot(TargetSlot& slot);
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
    std::uint32_t pass_width_ = 0u, pass_height_ = 0u;  // the open scene pass's targets
    // The temporal scene.
    MotionTargets motion_;
    [[nodiscard]] bool ensure_motion_targets();
    void clear_motion_targets(bool motion = true);
    void clear_layer_targets();
    // `count` textures cleared whole to `values` (four floats each) in one pass of the frame's command buffer.
    void clear_textures(const mtl::Texture* const* textures, const float (*values)[4], unsigned count);
    bool layer_window_ = false;
    bool reactive_scene_ = false;
    void prepare_temporal_upscaler();
    [[nodiscard]] bool record_temporal_upscale(const FrameMap& output);
    [[nodiscard]] bool ensure_output_set(const FrameMap& output, bool storage);
    std::unique_ptr<mtl::TemporalUpscaler> temporal_;
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
    bool motion_cleared_ = false;
    // defer_scene_depth_clear's: the depth attachment and its clear (1: waiting; the stencil is kept).
    const mtl::Texture* pending_depth_ = nullptr;
    SceneClearValues pending_depth_values_{};
    unsigned pending_depth_clear_ = 0u;
    [[nodiscard]] const mtl::Texture* scene_depth_attachment() const noexcept {
        return scene_.samples > 1u ? scene_.ms_depth.get() : scene_.depth.get();
    }
    [[nodiscard]] bool depth_clear_pending() const noexcept {
        return pending_depth_clear_ != 0u && !target_pass_active_ && pending_depth_ == scene_depth_attachment();
    }
    void settle_depth_clear();
    void clear_depth_in_pass(float reversed_depth, const gpu::Rect& rect);
    bool temporal_interp_ = false;
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
    bool present_black_ = false;
    std::uint64_t motion_used_frame_ = 0u;
    std::uint64_t output_scene_used_frame_ = 0u;
    std::uint64_t wide_scene_used_frame_ = 0u;
    std::uint64_t capture_used_frame_ = 0u;

    // The renderer's parts (fx_mtl.h).
    mtl::PostProcessor post_;
    mtl::GameBloom bloom_;
    mtl::AmbientOcclusion ao_;
    mtl::SceneCamera scene_camera_;
    RtView scene_ao_view_{};
    std::vector<ao::ObjectOccluder> scene_ao_objects_;
    bool hdr_read_ = false;
    bool hdr_post_ready_ = false;
    PostOutput hdr_output_{};

    // Draws.
    UploadRing vertex_ring_;
    UploadRing index_ring_;
    UploadRing constant_ring_;
    UploadRing matrix_ring_;
    std::unordered_map<SamplerCacheKey, mtl::Object, SamplerCacheKeyHasher> samplers_;
    std::unordered_map<SamplerTableKey, std::unique_ptr<mtl::BoundSamplers>, SamplerTableKeyHasher> sampler_tables_;
    std::uint32_t sampler_config_ = 0;
    gpu::Table default_sampler_table_{};
    // With arrays: the texture array is the store's backend's (TextureBackend::texture_array, a slot each view); the
    // sampler array is here, sampler_array_slots_ resource ids, slot 0 the default sampler and every unnamed slot it.
    bool bindless_ = false;
    std::shared_ptr<mtl::Buffer> sampler_array_;
    std::uint32_t sampler_array_slots_ = 0u;
    SamplerSlotCache sampler_slots_;
    bool sampler_slots_ready_ = false;
    TextureStore* mirrored_store_ = nullptr;
    // The texture lists: a shared buffer per frame in flight, written from its start each frame; a full one is
    // replaced by one twice its size, the old kept until the frame's fence passes.
    struct TextureListBuffer {
        std::shared_ptr<mtl::Buffer> buffer;
        std::uint32_t capacity = 0u;  // pairs
    };
    std::array<TextureListBuffer, kFramesInFlight> texture_lists_{};
    std::uint32_t texture_lists_used_ = 0u;
    std::uint32_t zero_stream_bytes_ = 0u;
    gpu::Address zero_stream_address_ = 0u;
    gpu::BufferRef zero_vertices_;
    unsigned history_segment_ = 0u;
    std::array<std::uint64_t, kRingSegments> history_segment_fence_{};
    bool history_used_ = true;
    std::size_t history_begin_vertex_ = 0u, history_begin_matrix_ = 0u;
    bool ring_growth_failed_ = false;
    // Viewport and scissor of the scene (bind_scene), set again after one of the renderer's own draws.
    struct Bound {
        gpu::ViewportRect viewport{};
        bool viewport_valid = false;
        gpu::Rect scissor{};
        bool scissor_valid = false;
    } bound_;
    gpu::Encoder encoder_;
    gpu::Encoder::Layout encoding_layout_{};
    std::uint32_t sampler_generation_ = 1u;

    // Tables that live across frames (no arrays), and what the GPU may still read until a fence passes.
    struct RetiredTable {
        mtl::BoundTextures* table = nullptr;
        std::uint64_t fence = 0;
    };
    std::vector<std::unique_ptr<mtl::BoundTextures>> persistent_tables_;
    std::vector<RetiredTable> retired_tables_;
    std::vector<mtl::BoundTextures*> free_tables_;

    // Parallel recording: the frame's command buffers each run of chunks got, and each chunk's encoder.
    struct RecordRun {
        mtl::Object commands = nullptr;  // id<MTLCommandBuffer>
        mtl::Object parallel = nullptr;  // id<MTLParallelRenderCommandEncoder>
    };
    std::vector<RecordRun> record_runs_;
    std::vector<mtl::Object> record_encoders_;  // id<MTLRenderCommandEncoder> by chunk
    std::vector<std::uint8_t> record_aux_;
    std::uint32_t record_width_ = 0u, record_height_ = 0u;
    unsigned record_passes_ = 0u;  // parallel passes this frame

    // Scene target copies.
    std::unordered_map<std::uint64_t, FramePassTarget> frame_pass_targets_;
    std::uint64_t frame_pass_color_key_ = 0u;
    std::uint64_t frame_pass_depth_key_ = 0u;

    // Screen probes: a slot's pixels (written here) and values (the dispatch writes, read back), fenced with its frame.
    struct ProbeSlot {
        std::shared_ptr<mtl::Buffer> pixels;
        std::shared_ptr<mtl::Buffer> values;
        std::uint64_t capacity = 0;  // points both hold
        std::uint64_t fence = 0;     // 0: idle
        std::uint32_t held = 0;      // points asked while the scene pass was open, read when it ends
        // Compared points tested in the scene pass: a visibility result each (eight bytes), read once the fence passed.
        std::shared_ptr<mtl::Buffer> results;
        std::uint64_t results_offset = 0;
        std::uint32_t tested = 0;         // points, 0 when the values buffer answers
        std::vector<float> answers;       // 1 seen, 0 hidden; the points no draw tests are set as they are asked
        std::vector<std::uint8_t> drawn;  // the points whose result decides
    };
    std::vector<ProbeSlot> probe_slots_;
    std::vector<std::size_t> held_probe_slots_;
    // The open scene pass's visibility results: the buffer and how many it has taken.
    std::shared_ptr<mtl::Buffer> pass_visibility_;
    std::uint32_t pass_visibility_used_ = 0u;
    static constexpr std::uint32_t kPassVisibilityResults = 1024u;
    void dispatch_screen_probes(std::size_t slot, std::uint32_t count);
    [[nodiscard]] bool test_screen_probes(ProbeSlot& out, std::span<const ScreenProbe> probes);
    void read_held_probes();
    void settle_screen_probes();
    static constexpr std::uint64_t kProbesInFrame = ~std::uint64_t{0};

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
    gpu::TextureRef capture_target_;  // the presented picture drawn again for a capture (RGBA8)
    Readback display_capture_;
    std::string display_capture_pending_path_;
    unsigned display_capture_width_ = 0;
    unsigned display_capture_height_ = 0;
    bool display_capture_ten_bit_ = false;
    // The picture as the window shows it, drawn into `target` (an id<MTLTexture> of the layer's size and `format`):
    // black, the picture in its content rectangle through the post chain's last pass or the plain blit.
    void draw_presented_picture(mtl::Object target, mtl::Format format, const mtl::Texture& picture,
                                const galaxy::ContentViewport& content, bool window_pass);
    std::atomic<double> measured_present_hz_{0.0};
    double display_refresh_hz_ = 0.0;
    bool variable_refresh_ = false;
    std::uint64_t display_refresh_checks_ = 0;
    std::int64_t measured_present_window_qpc_ = 0;
    std::int64_t swap_present_qpc_ = 0;
    std::uint64_t measured_present_count_ = 0;
    std::uint64_t window_size_refused_ms_ = 0;
    PresentPacer present_pacer_;
    std::array<bool, kFramesInFlight> frame_generated_{};
    double generated_gpu_ema_ms_ = 0.0;
    double native_scene_gpu_ema_ms_ = 0.0;
    bool scene_drawn_ = false;
    std::array<bool, kFramesInFlight> frame_scene_drawn_{};
    double generated_gpu_ms_ = 0.0;
    std::uint64_t generated_gpu_samples_ = 0;
    // Every 600 frames ([metal-gpu]): the GPU's frame time, and how often the scene pass reopened (each reopening
    // loads and stores the whole scene target on a tile-based GPU) and the scene target was copied.
    struct GpuWindow {
        double ms = 0.0;
        double max_ms = 0.0;
        unsigned frames = 0;
        std::uint64_t passes = 0;
        std::uint64_t copies = 0;
        struct Open {
            const char* closed = nullptr;
            const char* opened = nullptr;
            bool cleared = false;
            std::uint64_t count = 0;
        };
        std::array<Open, 24> opens{};
    };
    GpuWindow gpu_window_;
    // With present-stats asked for: when each picture reached the screen (the drawable's presented time, which the
    // display reports), so the trace says how long each stayed up, in the display's refreshes, and how long after it
    // was handed over it appeared. The handlers come on Metal's threads.
    struct Shown {
        std::mutex mutex;
        double last = 0.0;
        std::array<std::uint64_t, 5> refreshes{};  // pictures up for 0 (two in one refresh), 1, 2, 3, 4 or more
        std::uint64_t never = 0;                   // dropped: never shown
        double latency = 0.0;
        double latency_max = 0.0;
        std::uint64_t count = 0;
    };
    std::shared_ptr<Shown> shown_ = std::make_shared<Shown>();
    void choose_first_start_scale();
    // The layer's line of pictures (present): a frame that waited this long for a drawable waited for a refresh, the
    // line full; whether the last frame's picture was held back for it; and how many have been.
    static constexpr double kDrawableWaitMs = 1.5;
    bool held_last_ = false;
    std::uint64_t pictures_held_ = 0;
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
        double handed_phase_ms = 0.0;
        double drawable_wait_ms = 0.0;
        double drawable_wait_max_ms = 0.0;
        double gpu_ms = 0.0;
        std::uint64_t gpu_samples = 0;
    } stats_;
    void gpu_pass(GpuPass pass);
    class GpuPassScope {
    public:
        GpuPassScope(RendererMtl& renderer, GpuPass pass) : renderer_(renderer), outer_(renderer.current_pass_) {
            renderer_.gpu_pass(pass);
        }
        ~GpuPassScope() { renderer_.gpu_pass(outer_); }
        GpuPassScope(const GpuPassScope&) = delete;
        GpuPassScope& operator=(const GpuPassScope&) = delete;

    private:
        RendererMtl& renderer_;
        GpuPass outer_;
    };
    GpuPass current_pass_ = GpuPass::Scene;
    // Passes are timed only on request (--diagnostics gpu-passes, or the settings page's GPU TIMING): the frame's
    // commands are then split into a command buffer a pass, whose GPU start and end times Metal reports on every GPU
    // (an encoder is never split: a tile-based GPU would load and store the target again). list_pass_: the pass the
    // buffer being recorded counts as.
    bool profile_frame_ = false;
    GpuPass list_pass_ = GpuPass::Scene;
    std::array<double, static_cast<std::size_t>(GpuPass::Count) + 1u> pass_window_ms_{};
    unsigned pass_window_frames_ = 0;
    std::atomic<bool> gpu_timing_wanted_{false};
    std::array<std::atomic<float>, static_cast<std::size_t>(GpuPass::Count)> pass_ema_ms_{};
};

using Renderer = RendererMtl;

}  // namespace galaxy::render
