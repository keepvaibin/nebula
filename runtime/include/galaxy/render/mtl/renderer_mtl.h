// SPDX-License-Identifier: GPL-3.0-only
// The renderer on Metal, with the interface RenderCore uses on RendererD3D12 and RendererVk.
//
// The scene target is a colour texture (RGB10A2) and a depth texture (Depth32Float_Stencil8) drawn through one render
// command encoder, the scene pass; a scene target copy or present ends that encoder, and the next scene
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

#include "galaxy/platform_time.h"
#include "galaxy/content_viewport.h"
#include "galaxy/render/temporal_hold.h"
#include "galaxy/render/ambient_occlusion_objects.h"
#include "galaxy/render/display_modes.h"
#include "galaxy/render/frame_pass.h"
#include "galaxy/render/gpu_pass_times.h"
#include "galaxy/render/gpu_types.h"
#include "galaxy/render/gpu_upload_ring.h"
#include "galaxy/render/host_window.h"
#include "galaxy/render/unseen_window.h"
#include "galaxy/launch_args.h"
#include "galaxy/render/mtl/draw_types.h"
#include "galaxy/render/mtl/fx_mtl.h"
#include "galaxy/render/mtl/mtl_context.h"
#include "galaxy/render/mtl/temporal_upscaler_mtl.h"
#include "galaxy/render/mtl/utility_msl.h"
#include "galaxy/render/post_process_settings.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/frame_schedule.h"
#include "galaxy/render/present_slots.h"
#include "galaxy/render/renderer_common.h"
#include "galaxy/render/scene_view.h"
#include "galaxy/render/window_owner.h"
#include "galaxy/render_api.h"
#include "galaxy/render/mod_post.h"
#include "galaxy/render/idle_clock.h"
#include "galaxy/render/dynamic_resolution.h"
#include <nebula_frame_list.h>
#include "render/core/draw_record.h"
#include "render/core/retire_ring.h"
#include "render/core/gpu_encoder.h"

#include <algorithm>
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
    // The pictures handed to an extension, taken from the GPU and written (render thread; RenderCore::finish_pictures,
    // at a host's exit that skips shutdown).
    void finish_pictures();
    [[nodiscard]] UploadBuffer create_upload_buffer(std::size_t bytes, const char* name);

    // SMAA or FXAA of the 3D scene in the scene target before the 2D layer draws (mtl::PostProcessor).
    void record_scene_aa();
    // Ambient occlusion of the opaque scene (mtl::AmbientOcclusion).
    [[nodiscard]] bool ambient_occlusion_ready() const noexcept { return ao_.ready(); }
    void set_scene_ao_view(const SceneView& view) noexcept { scene_ao_view_ = view; }
    void set_scene_ao_objects(std::vector<ao::ObjectOccluder>& objects) noexcept { scene_ao_objects_.swap(objects); }
    void record_scene_ao();
    // The game's bloom (mtl::GameBloom): `kind` a NEBULA_PASS_BLOOM_*, `a` and `b` its values.
    [[nodiscard]] bool game_bloom_ready() const noexcept { return bloom_.ready(); }
    void game_bloom(std::uint32_t kind, std::uint32_t a, std::uint32_t b);
    void reset_generated_gpu_ema() noexcept { frame_gpu_.reset_generated_ema(); }
    [[nodiscard]] double generated_gpu_ema_ms() const noexcept { return frame_gpu_.generated_ema_ms(); }
    [[nodiscard]] double interpolation_gpu_cost_ms() const noexcept {
        return temporal_interp_ ? frame_gpu_.generated_ema_ms() : frame_gpu_.scene_ema_ms();
    }
    void suspend_temporal_interpolation(double ms_per_frame);
    [[nodiscard]] bool temporal_interpolation() const noexcept { return temporal_interp_; }
    [[nodiscard]] unsigned temporal_output_width() const noexcept { return temporal_output_w_; }
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
    // A chunk of the texture lists (core::TextureLists, FRAME-MODEL 2.10): `pairs` pairs of mapped memory the programs
    // read at `address`, kept until the renderer is taken down. False when it cannot be made.
    bool make_texture_list_chunk(std::uint32_t pairs, std::uint32_t*& mapped, gpu::Address& address);
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
        if ((state & depth_write) == depth_write && scene_.samples > 1u) {
            scene_.depth_dirty = true;
        }
    }
    // A buffer of `bytes` zeros, for streams a mesh lacks.
    [[nodiscard]] gpu::Address zero_stream(std::uint32_t bytes);
    // The frame so far is the frame's picture now: `fresh` (or a new target) takes its place.
    [[nodiscard]] gpu::TextureRef take_frame_picture(gpu::TextureRef fresh);
    // draw.post and draw.native (mod_post.h, luma_draw_native.h): not on Metal yet. No compiler is installed, so every
    // post program is refused at post_register ("no compiler for post programs on this renderer") and no list names
    // one; draw.native's backend values are D3D12 and Vulkan only, so no native call reaches here.
    void record_mod_post(const ModPostProgram&, ModPostConstants, const gpu::TextureRef&, bool) {}
    void record_native_call(const NebulaCall&, LumaDrawNativeFrame&, bool) {}
    [[nodiscard]] bool can_take_scene() const noexcept;
    // The scene target's colour may trade places with another of its size, for a pass that then writes all of it that
    // is read (the frame, or dynamic resolution's rectangle): can_take_scene less the game's whole-frame need.
    [[nodiscard]] bool can_trade_scene() const noexcept;
    bool take_scene(std::uint64_t key, gpu::TextureRef fresh, TextureStore& textures, gpu::TextureRef& released);
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

    [[nodiscard]] bool begin_output_scene(bool resolve_msaa_only = false);
    [[nodiscard]] bool begin_target_pass(std::uint64_t key, std::uint32_t width, std::uint32_t height);
    void end_target_pass(std::uint8_t channels, TextureStore& textures);
    [[nodiscard]] bool target_picture(std::uint64_t key, TextureHandle& out) const;
    [[nodiscard]] bool target_pass_active() const noexcept { return target_pass_active_; }
    void end_output_scene();
    // HDR output's 2D layer (RendererD3D12::begin_layer_2d): Metal draws no float scene.
    void begin_layer_2d() noexcept {}
    [[nodiscard]] bool output_scene_active() const noexcept { return output_scene_active_; }
    // Temporal upscaling.
    [[nodiscard]] bool scene_temporal_begin(float& jitter_x, float& jitter_y);
    [[nodiscard]] bool begin_motion_scene();
    [[nodiscard]] bool begin_cleared_motion_scene(std::uint32_t clear_rgba);
    // The walk's late depth clear (plan_late_depth_clears) is cleared where it stands: nothing holds the scene pass open
    // past it (the held depth probes went with S0.2.6).
    [[nodiscard]] bool defer_scene_depth_clear(std::uint32_t) noexcept { return false; }
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

    [[nodiscard]] double measured_present_hz() const noexcept { return measured_present_hz_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t frame_index() const noexcept { return frame_index_; }

    // Test captures: the presented picture as PPM files (paths from set_debug_capture_paths). Writes every capture
    // whose frame the GPU has finished.
    void debug_log_backbuffer_readback();
    bool debug_copy_selected_display_to_readback(const DisplayTexture& source);
    void set_debug_capture_paths(std::string backbuffer_ppm_path);

    [[nodiscard]] gpu::Device* device() const noexcept { return static_cast<gpu::Device*>(mtl::Context::get().device()); }
    [[nodiscard]] const FrameMap& frame_map() const noexcept { return scene_map_; }
    // The bound scene target's allocated size: the render size while dynamic resolution draws the scene in a part of it
    // (frame_map, the step's rectangle), else frame_map itself. Pictures for the game and target passes take this size.
    [[nodiscard]] const FrameMap& frame_extent() const noexcept { return frame_extent_; }
    // The 3D scene's pass opens (its clear, or walk_scene_begin): dynamic resolution decides this picture's rectangle
    // (once a frame) and the main scene target is drawn in it from here to the end of the frame.
    void begin_scene_rectangle();
    // The rectangle the last scene was drawn in (the render size while dynamic resolution is off).
    [[nodiscard]] const FrameMap& scene_rectangle() const noexcept { return scene_rectangle_; }
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
    // What the swap chain keeps of the pictures presented (present_slots.h): every one with VSync; VSync off tears here.
    [[nodiscard]] SwapQueue swap_queue() const noexcept {
        return get_render_config().vsync_interval != 0u ? SwapQueue::EveryPicture : SwapQueue::Tearing;
    }
    // The moment the next present shows (RenderCore's interpolation slots): this API has no presentation times, so
    // the presents pace themselves (PresentPacer, the swap chain's vblanks).
    void set_present_moment(std::int64_t) noexcept {}
    [[nodiscard]] bool take_present_skip() noexcept { return false; }
    // No display model yet (G2.3.1 gives Direct3D's; this platform's comes in its own landing): the schedule's slots
    // are the moments the pictures start.
    [[nodiscard]] bool display_time(std::int64_t, std::int64_t& shown) const noexcept {
        shown = 0;
        return false;
    }
    [[nodiscard]] bool take_shown(ShownPicture&) noexcept { return false; }
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
    using GpuPass = ::galaxy::render::GpuPass;

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
        // GPU start and end times of the frame's command buffers, written by their completion (seconds; 0: none).
        std::shared_ptr<std::array<std::atomic<double>, 2>> gpu_time;
        // With the passes timed (profile_frame_): the GPU's seconds on the frame's command buffers by the pass each was
        // begun for, summed by their completions; GpuPass::Uploads is the frame's texture uploads.
        std::shared_ptr<std::array<std::atomic<double>, kGpuPassCount>> pass_time;
        bool pass_timed = false;
    };
    struct FramePassTarget {
        gpu::TextureRef texture;
        unsigned width = 0;
        unsigned height = 0;
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
    // Every sample count a scene target can have on this device (1 and each it supports up to draw_layout_.max_samples):
    // the passes' pipelines are made for each at the start.
    [[nodiscard]] std::vector<unsigned> scene_sample_counts() const;
    // The renderer's own pipelines every frame can ask for, made at the start on a worker (initialize).
    void prepare_utility_pipelines();
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
                                                    unsigned clear_depth);
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
    void replace_samplers(core::RetireRing& ring, unsigned slot, const RenderConfig& cfg, std::uint32_t config, bool all);
    [[nodiscard]] std::shared_ptr<mtl::Buffer> make_sampler_array() const;
    void write_sampler(unsigned slot, mtl::Object sampler);
    void fill_sampler_array(mtl::Object sampler);
    [[nodiscard]] mtl::Object sampler_for(const SamplerKey& key, const RenderConfig& cfg);  // id<MTLSamplerState>
    void write_table(mtl::BoundTextures& table, const TextureHandle* handles, unsigned count, const TextureStore& cache);
    void record_conversion_blit(mtl::Texture& dest, unsigned dest_w, unsigned dest_h, std::uint8_t copy_format, float src_x,
                                float src_y, float step_x, float step_y);
    void refresh_display_rate(bool ask);
    void follow_window_and_resize();
    void report_present_stats();
    void grow_rings();

    WindowOwner* window_owner_ = nullptr;
    host_window::NativeWindow window_ = nullptr;
    bool window_shown_ = false;
    UnseenWindow unseen_;  // while the window cannot be seen, no drawable is taken or presented
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

    // The scene target.
    FrameMap scene_map_{};     // the bound scene target's map: the part of it drawn in
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
    // The bound target's drawn part, within its textures (the viewport and scissor of its passes).
    [[nodiscard]] FrameMap scene_part() const noexcept {
        return scene_.color ? FrameMap(std::min(scene_map_.width, scene_.color->width),
                                       std::min(scene_map_.height, scene_.color->height))
                            : scene_map_;
    }
    // A depth picture for the game at the extent's size holding the whole frame (dynamic resolution's rectangle
    // resampled, point sampled), for a frame pass over the frame; its colour is draw_frame_pass's.
    bool publish_scene_depth_copy(std::uint64_t key, TextureStore& textures);
    // R32 float where the device filters it (a material may sample the picture so), else 16-bit float.
    mtl::Format depth_copy_format_ = mtl::Format::R32f;
    SceneSet scene_;
    SceneSet wide_scene_;
    bool ensure_wide_set(SceneSet& set);
    void record_wide_begin(const SceneClearValues* initial_clear);
    void record_wide_end();
    std::chrono::steady_clock::time_point camera_metered_{};
    std::uint64_t scene_depth_serial_ = 0;
    SceneSet alt_scene_;
    FrameMap alt_scene_map_{};
    FrameMap alt_scene_extent_{};  // its allocated size (alt_scene_map_: the part drawn), traded with frame_extent_
    std::unordered_map<std::uint64_t, Picture> pictures_;
    bool publish_picture(std::uint64_t key, std::uint32_t width, std::uint32_t height, gpu::TextureRef texture,
                         std::uint8_t channels, TextureStore& textures);
    bool publish_frame_pass(const FramePassRequest& params, gpu::TextureRef texture, TextureStore& textures);
    void drop_picture(Picture& picture, TextureStore& textures);
    std::unordered_map<std::uint64_t, TargetSlot> target_slots_;
    static constexpr std::uint64_t kTargetSweepSteps = 60u;  // begin_frame's sweep: once a second of the game
    std::uint64_t target_swept_step_ = 0u;                    // idle_clock() at the last sweep
    void retire_target_slot(TargetSlot& slot);
    void sweep_target_slots();
    std::uint64_t target_key_ = 0;
    SceneSet target_scene_;
    FrameMap target_scene_map_{};
    FrameMap target_scene_extent_{};  // traded with frame_extent_ as target_scene_map_ with scene_map_
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
    bool present_black_ = false;
    std::uint64_t motion_used_step_ = 0u;
    std::uint64_t output_scene_used_step_ = 0u;
    std::uint64_t wide_scene_used_step_ = 0u;
    std::uint64_t capture_used_step_ = 0u;

    // The renderer's parts (fx_mtl.h).
    mtl::PostProcessor post_;
    mtl::GameBloom bloom_;
    mtl::AmbientOcclusion ao_;
    mtl::SceneCamera scene_camera_;
    SceneView scene_ao_view_{};
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
    // The texture lists' chunks (core::TextureLists): shared buffers.
    std::vector<std::shared_ptr<mtl::Buffer>> texture_list_chunks_;
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

    // Captures and telemetry.
    std::string backbuffer_capture_path_;
    gpu::TextureRef capture_target_;  // the presented picture drawn again for a capture (RGBA8)
    // The pictures copied out (the presented picture and the display picture), each its own shared buffer.
    using Captures = PictureCaptures<std::shared_ptr<mtl::Buffer>>;
    Captures captures_;
    // A buffer for a capture of `bytes`: a spare one, or a new one.
    [[nodiscard]] std::shared_ptr<mtl::Buffer> capture_buffer(std::uint64_t bytes, const char* name);
    // The picture as the window shows it, drawn into `target` (an id<MTLTexture> of the layer's size and `format`):
    // black, the picture in its content rectangle through the post chain's last pass or the plain blit.
    void draw_presented_picture(mtl::Object target, mtl::Format format, const mtl::Texture& picture,
                                const galaxy::ContentViewport& content, bool window_pass);
    std::atomic<double> measured_present_hz_{0.0};
    double display_refresh_hz_ = 0.0;
    // The display varies its refresh with the presents (refresh_answer_, the system's answer): display_refresh_hz_ is then
    // the cap that keeps it in its range.
    bool variable_refresh_ = false;
    RefreshAnswer refresh_answer_;
    galaxy::ClockInterval display_refresh_{500, true};  // refresh_display_rate twice a second on the clock
    std::int64_t measured_present_window_qpc_ = 0;
    std::int64_t swap_present_qpc_ = 0;
    std::uint64_t measured_present_count_ = 0;
    std::uint64_t window_size_refused_ms_ = 0;
    PresentPacer present_pacer_;
    std::array<bool, kFramesInFlight> frame_generated_{};
    FrameGpuTime frame_gpu_;  // the frame's GPU time: the governor's and the interpolation's measures
    bool scene_drawn_ = false;
    std::array<bool, kFramesInFlight> frame_scene_drawn_{};
    // Every ten seconds ([metal-gpu]): the GPU's frame time, and how often the scene pass reopened (each reopening
    // loads and stores the whole scene target on a tile-based GPU) and the scene target was copied.
    galaxy::ClockInterval gpu_report_{10'000};  // the report every ten seconds
    struct GpuWindow {
        double ms = 0.0;
        double max_ms = 0.0;
        unsigned frames = 0;     // pictures with GPU times read
        unsigned recorded = 0;   // pictures recorded since the last report
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
    std::uint64_t pacing_present_id_ = 0;  // the pacing recorder's id for each present
    void choose_first_start_scale();
    // The layer's line of pictures (present): the drawables handed over and not yet presented (up at presentDrawable,
    // down in the presented handler on Metal's threads, a presented time of 0 included); a picture whose drawable comes
    // while two are in the line is held back; and how many have been.
    std::shared_ptr<std::atomic<int>> drawables_out_ = std::make_shared<std::atomic<int>>(0);
    std::uint64_t pictures_held_ = 0;
    // The Metal display link (host_window::metal_link_layer): the layer it runs on (null: none), and whether
    // in-between pictures fill the display (-1: not yet told).
    void* link_layer_ = nullptr;
    int display_fills_ = -1;
    // A test's late render thread (NEBULA_TEST_RENDER_DELAY_MS, I-006's proof): that many milliseconds slept before
    // every 30th picture's drawable is asked.
    unsigned test_delay_ms_ = 0u;
    std::uint64_t test_delay_count_ = 0;
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
    std::atomic<bool> gpu_timing_wanted_{false};
    GpuPassTimes pass_times_;  // the page's averages and the [metal-gpu] window
    // What frames in flight may still read when it is given up, per frame slot (core::RetireRing; its first users are the
    // sampler arrays, samplers and sampler tables a start again leaves behind). Last, so it is emptied before the rest goes.
    core::RetireRing retire_ring_{kFramesInFlight};
};

using Renderer = RendererMtl;

}  // namespace galaxy::render
