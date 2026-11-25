// SPDX-License-Identifier: GPL-3.0-only
// The renderer on Direct3D 11 (feature level 11_0 and up), with the interface RenderCore uses on RendererD3D12,
// RendererVk and RendererMtl. The executable NebulaD11 is built with it (NEBULA_GPU_D3D11); the Direct3D 12
// executable starts it when no adapter passes its probe (renderer_d3d12.cpp try_adapter) or the settings ask for it.
//
// The scene target is a colour texture (RGB10A2; the wide scene RGBA16F) and a depth texture (D32_FLOAT_S8X24), with MSAA
// a multisampled pair resolved as on Direct3D 12. A frame is recorded on the immediate context, kFramesInFlight ahead of
// the GPU (Context::signal and wait). A scene pass's draws are recorded as ops (gpu::Encoder) and replayed after the
// upload memory they read has reached the GPU (flush_draws), when the pass ends or the renderer draws in it itself.
// The swap chain is a flip-model one (FLIP_DISCARD, kSwapChainBuffers, a waitable object, tearing where offered); the
// picture is drawn into its buffer by the post chain's last pass or the plain blit, never copied.
//
// The renderer's parts (d3d11/fx_d3d11.h): the post chain, the game's bloom, ambient occlusion and the scene camera, each
// called on the immediate context with no scene pass open.
#pragma once

#include "galaxy/content_viewport.h"
#include "galaxy/platform_time.h"
#include "galaxy/launch_args.h"
#include "galaxy/render/ambient_occlusion_objects.h"
#include "galaxy/render/d3d11/constants_watch_d3d11.h"
#include "galaxy/render/d3d11/d3d11_context.h"
#include "galaxy/render/d3d11/fx_d3d11.h"
#include "galaxy/render/display_modes.h"
#include "galaxy/render/dxgi_memory.h"
#include "galaxy/render/dynamic_resolution.h"
#include "galaxy/render/frame_pass.h"
#include "galaxy/render/gpu_pass_times.h"
#include "galaxy/render/gpu_types.h"
#include "galaxy/render/gpu_upload_ring.h"
#include "galaxy/render/host_window.h"
#include "galaxy/render/mod_post.h"
#include "galaxy/render/post_process_settings.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/frame_schedule.h"
#include "galaxy/render/present_slots.h"
#include "galaxy/render/renderer_common.h"
#include "galaxy/render/scene_view.h"
#include "galaxy/render/window_owner.h"
#include "galaxy/render_api.h"
#include "galaxy/render/idle_clock.h"
#include "render/core/draw_record.h"
#include "render/core/retire_ring.h"
#include "render/core/gpu_encoder.h"

#include <nebula_frame_list.h>

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <deque>
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

class RendererD3D11 final {
public:
    using SubmissionCallback = void (*)(void* context, std::uint64_t fence_value);

    bool initialize(int window_width, int window_height, const FrameMap& frame);
    // Why initialize failed, when the device said.
    [[nodiscard]] std::string startup_failure() const {
        return !init_error_.empty() ? init_error_ : d3d11::Context::get().failure();
    }
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

    // SMAA or FXAA of the 3D scene in the scene target before the 2D layer draws (d3d11::PostProcessor).
    void record_scene_aa();
    // Ambient occlusion of the opaque scene (d3d11::AmbientOcclusion).
    [[nodiscard]] bool ambient_occlusion_ready() const noexcept { return ao_.ready(); }
    void set_scene_ao_view(const SceneView& view) noexcept { scene_ao_view_ = view; }
    void set_scene_ao_objects(std::vector<ao::ObjectOccluder>& objects) noexcept { scene_ao_objects_.swap(objects); }
    void record_scene_ao();
    // The game's bloom (d3d11::GameBloom): `kind` a NEBULA_PASS_BLOOM_*, `a` and `b` its values.
    [[nodiscard]] bool game_bloom_ready() const noexcept { return bloom_.ready(); }
    void game_bloom(std::uint32_t kind, std::uint32_t a, std::uint32_t b);
    // Interpolation re-walks the newest list for each picture between game steps (RenderCore); no frame generation or
    // temporal upscaler on Direct3D 11, so a generated picture costs what a scene picture costs.
    void reset_generated_gpu_ema() noexcept { frame_gpu_.reset_generated_ema(); }
    [[nodiscard]] double generated_gpu_ema_ms() const noexcept { return frame_gpu_.generated_ema_ms(); }
    [[nodiscard]] double interpolation_gpu_cost_ms() const noexcept { return frame_gpu_.scene_ema_ms(); }
    void suspend_temporal_interpolation(double) {}
    [[nodiscard]] bool temporal_interpolation() const noexcept { return false; }
    [[nodiscard]] unsigned temporal_output_width() const noexcept { return 0u; }
    void set_scene_view(const std::array<float, 12>* view) noexcept {
        scene_view_known_ = view != nullptr;
        if (view != nullptr) {
            scene_view_ = *view;
        }
    }
    [[nodiscard]] bool layer_window() const noexcept { return false; }
    void begin_layer_window() {}
    [[nodiscard]] std::int64_t last_present_qpc() const noexcept { return swap_present_qpc_; }
    // The 3D scene in a wide target while `wanted` (the enhanced lighting), its camera at the end (d3d11::SceneCamera).
    bool begin_wide_scene(bool wanted, const SceneClearValues* initial_clear = nullptr);
    void end_wide_scene();
    [[nodiscard]] bool temporal_mode() const { return false; }
    [[nodiscard]] bool motion_pass_only() const { return false; }
    [[nodiscard]] bool has_layer_targets() const noexcept { return false; }
    // The draws' textures: a draw's own tables of up to table_slots() maps (no arrays on Direct3D 11).
    [[nodiscard]] bool bindless() const noexcept { return false; }
    [[nodiscard]] std::uint32_t texture_slot(const TextureHandle&) const noexcept { return 0u; }
    [[nodiscard]] std::uint32_t sampler_slot(const SamplerKey&) { return 0u; }
    bool make_texture_list_chunk(std::uint32_t, std::uint32_t*&, gpu::Address&) { return false; }
    void bind_draw_textures(gpu::Address) {}
    [[nodiscard]] std::uint32_t table_slots() const noexcept { return d3d11::kBoundTableSlots; }
    [[nodiscard]] gpu::Table texture_table_for(const TextureHandle* handles, unsigned count, const TextureStore& cache);
    // A table that lives until replaced: `old` is given back once the GPU is past it.
    [[nodiscard]] gpu::Table persistent_texture_table(const TextureHandle* handles, unsigned count, const TextureStore& cache,
                                                      gpu::Table old);
    void retire_texture_table(gpu::Table table);
    // A target whose id is freed: every picture it has goes.
    void free_target(std::uint32_t id, TextureStore& textures);
    // Moves when every sampler table is made again.
    [[nodiscard]] std::uint32_t sampler_generation() const noexcept { return sampler_generation_; }
    void trace_rings() const;
    void copy_fg_hudless_display(const DisplayTexture&) {}
    void attach_texture_uploads(TextureStore& cache);
    // The video memory in use and its budget (dxgi_memory.h: asked at most every 100 ms, at once when the budget moved).
    [[nodiscard]] bool video_memory(std::uint64_t& usage, std::uint64_t& budget) const noexcept;
    void wait_for_gpu_idle();
    void pump_messages() {}
    [[nodiscard]] bool quit_requested() const noexcept;
    // A covered or minimised window: the swap chain's word (DXGI_STATUS_OCCLUDED from a test present).
    [[nodiscard]] bool occluded() const noexcept { return occluded_; }
    void probe_occlusion() noexcept;

    // An extension's lose_gpu_device, on the render thread: the next present reports a removed device.
    void lose_device();
    // A GPU allocation inside the frame failed: thrown as a device loss when the device is gone (the renderer is made
    // again), as memory run out otherwise (P-062).
    [[noreturn]] void allocation_failed(const char* what) const;
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
    [[nodiscard]] static std::uint64_t allocation_bytes(const d3d11::Texture* texture) noexcept {
        return texture != nullptr ? texture->allocation_bytes : 0u;
    }
    // GPU time by pass (render_api.h GpuPassTime): any thread.
    void set_gpu_timing(bool on) noexcept { gpu_timing_wanted_.store(on, std::memory_order_relaxed); }
    [[nodiscard]] bool gpu_timing_on() const noexcept { return gpu_timing_wanted_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::size_t gpu_pass_times(GpuPassTime* out, std::size_t capacity) const noexcept;
    void scene_jitter_info(unsigned& index, unsigned& phases) const noexcept {
        index = 0u;
        phases = 0u;
    }
    void wait_for_fence_value(std::uint64_t fence_value);

    void bind_scene();
    [[nodiscard]] gpu::TextureRef swap_scene_color(gpu::TextureRef fresh);
    void clear_scene(std::uint32_t clear_rgba, bool color_enable, bool alpha_enable, bool depth_enable, unsigned rect_x = 0,
                     unsigned rect_y = 0, unsigned rect_w = 0, unsigned rect_h = 0, std::uint8_t pixel_format = 0);
    // What the walker draws through. Aux targets beside the scene target: none on Direct3D 11 (no temporal upscaler).
    [[nodiscard]] gpu::Encoder& encoder() noexcept { return encoder_; }
    // Opens the scene pass when no pass is open (aux is always 0 here).
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
    [[nodiscard]] bool can_take_scene() const noexcept;
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
    // draw.post (mod_post.h, mod_post_d3d11.cpp): `program` full screen over `picture` (the frame so far, from
    // take_frame_picture) into the scene target; `depth_known` the walker could give the depth terms. No motion here.
    void record_mod_post(const ModPostProgram& program, ModPostConstants constants, const gpu::TextureRef& picture,
                         bool depth_known);
    // draw.native (luma_draw_native.h): `call` with the device and the immediate context between passes, the scene
    // target (with its depth at a scene point), on a context state of its own (the renderer's comes back after it).
    void record_native_call(const NebulaCall& call, LumaDrawNativeFrame& frame, bool scene_point);

    [[nodiscard]] bool begin_output_scene(bool resolve_msaa_only = false);
    [[nodiscard]] bool begin_target_pass(std::uint64_t key, std::uint32_t width, std::uint32_t height);
    void end_target_pass(std::uint8_t channels, TextureStore& textures);
    [[nodiscard]] bool target_picture(std::uint64_t key, TextureHandle& out) const;
    [[nodiscard]] bool target_pass_active() const noexcept { return target_pass_active_; }
    void end_output_scene();
    // HDR output's 2D layer (RendererD3D12::begin_layer_2d): Direct3D 11 draws no float scene.
    void begin_layer_2d() noexcept {}
    [[nodiscard]] bool output_scene_active() const noexcept { return output_scene_active_; }
    // Temporal upscaling: none on Direct3D 11.
    [[nodiscard]] bool scene_temporal_begin(float& jitter_x, float& jitter_y) {
        jitter_x = jitter_y = 0.0f;
        return false;
    }
    [[nodiscard]] bool begin_motion_scene() { return false; }
    [[nodiscard]] bool begin_cleared_motion_scene(std::uint32_t) { return false; }
    [[nodiscard]] bool defer_scene_depth_clear(std::uint32_t) { return false; }
    void reset_temporal_history() noexcept {}
    void set_present_game_frame(bool game_frame) noexcept { present_game_frame_ = game_frame; }
    // A present rate above 60: pictures between game steps.
    [[nodiscard]] bool interpolation_mode() const;
    // The generated pictures' GPU time since the last call (--diagnostics present-stats).
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
    // The presented display picture itself (before it is scaled to the window), copied to <backbuffer path>.display.ppm,
    // as the other renderers' display capture.
    bool debug_copy_selected_display_to_readback(const DisplayTexture& source);
    void set_debug_capture_paths(std::string backbuffer_ppm_path);
    // The pictures handed to an extension, taken from the GPU and written (render thread; RenderCore::finish_pictures,
    // before a process that ends without the shutdown).
    void finish_pictures();

    [[nodiscard]] gpu::Device* device() const noexcept { return d3d11::Context::get().device(); }
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
    // The refresh the next present is drawn for (RenderCore's interpolation slot); 0 when it has none. This API asks no
    // time: the display model (display_time) tells when a present shows, and the frame latency object holds a present
    // that is early until the one before it is shown.
    void set_present_moment(std::int64_t qpc) noexcept { present_moment_ = qpc; }
    [[nodiscard]] bool take_present_skip() noexcept { return false; }
    // The display model (frame_schedule.h DisplayModel), fed by every present's frame statistics (dxgi_pacing.h).
    [[nodiscard]] bool display_time(std::int64_t present, std::int64_t& shown) const noexcept {
        return display_model_.display_time(present, shown);
    }
    [[nodiscard]] bool take_shown(ShownPicture& picture) noexcept { return display_model_.take_shown(picture); }
    [[nodiscard]] UploadRing& vertex_ring() noexcept { return vertex_ring_; }
    [[nodiscard]] UploadRing& index_ring() noexcept { return index_ring_; }
    [[nodiscard]] UploadRing& constant_ring() noexcept { return constant_ring_; }
    [[nodiscard]] UploadRing& matrix_ring() noexcept { return matrix_ring_; }
    [[nodiscard]] unsigned frame_slot() const noexcept { return frame_slot_; }
    void bind_draw_tables(gpu::Table, gpu::Table) noexcept {}
    [[nodiscard]] gpu::TextureArrays draw_texture_arrays(gpu::Address) { return {}; }

    // Parallel recording: one immediate context, so a pass is always recorded on the render thread (begin says false).
    [[nodiscard]] bool parallel_record_begin(const std::uint8_t*, unsigned) { return false; }
    void parallel_record_chunk(unsigned, const gpu::DrawOp*, std::size_t) {}
    void parallel_record_end() {}
    void replay_draws(std::uint8_t aux, const gpu::DrawOp* ops, std::size_t count);
    [[nodiscard]] gpu::Table sampler_table_for(const SamplerKey* keys, unsigned count);

    // For the renderer's parts (post chain, bloom, occlusion, camera): the immediate context with no scene pass open
    // and every recorded draw run (flush_draws, end_scene_pass); a part leaves its own state bound, and the renderer
    // binds its own again (encoder_.reset, bound_).
    [[nodiscard]] ID3D11DeviceContext1* context() const noexcept { return d3d11::Context::get().immediate(); }
    using GpuPass = ::galaxy::render::GpuPass;
    static constexpr unsigned kGpuPassSlots = 256u;

private:
    struct Frame {
        std::uint64_t fence = 0;               // the submission that used this slot last
        std::vector<gpu::TextureRef> retired;  // kept until the slot's fence passes (pictures the store may reuse)
        std::vector<gpu::BufferRef> retired_buffers;
        // A draw's own tables this frame: reused each frame.
        std::deque<d3d11::BoundTable> tables;
        std::size_t tables_used = 0;
        // The frame's GPU time: a disjoint query and the timestamps at its start and end, read once the fence passed.
        d3d11::ComPtr<ID3D11Query> disjoint;
        std::array<d3d11::ComPtr<ID3D11Query>, 2> frame_time{};
        // GPU time by pass (--diagnostics gpu-passes, the settings page): a timestamp where the pass changes (gpu_pass);
        // the time to the next one, or to the frame's end, is that pass's.
        std::vector<d3d11::ComPtr<ID3D11Query>> pass_queries;
        std::array<GpuPass, kGpuPassSlots> pass_marks{};
        unsigned pass_mark_count = 0;
        bool timed = false;
        bool scene_drawn = false;
        bool generated = false;  // a picture between game steps (end_frame's `generated`)
    };
    struct FramePassTarget {
        gpu::TextureRef texture;
        unsigned width = 0;
        unsigned height = 0;
    };
    // A scene target: its colour and depth, and with MSAA the multisampled targets the pass draws into (the colour
    // resolves into `color` when read; the depth is resolved on demand, resolve_scene_depth).
    struct SceneSet {
        gpu::TextureRef color;
        gpu::TextureRef depth;
        unsigned samples = 1u;
        gpu::TextureRef ms_color;
        gpu::TextureRef ms_depth;
        bool depth_dirty = false;
        bool color_dirty = false;  // the multisampled colour holds draws its resolve has not taken
        bool wide = false;         // the wide scene's colour (kSceneVariantHdr), over the scene target's own depth
        // The views the scene pass draws into: the multisampled ones with MSAA.
        [[nodiscard]] ID3D11RenderTargetView* rtv() const noexcept {
            return samples > 1u && ms_color ? ms_color->target.Get() : color ? color->target.Get() : nullptr;
        }
        [[nodiscard]] ID3D11DepthStencilView* dsv() const noexcept {
            return samples > 1u && ms_depth ? ms_depth->depth.Get() : depth ? depth->depth.Get() : nullptr;
        }
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
        // The picture with its channels as the other renderers' views give them (a copy: Direct3D 11 views cannot
        // repeat a channel); null when the texture's own channels are the picture's.
        gpu::TextureRef sampled;
    };
    // The renderer's own full-screen draws (utility_shader_table.inc compiled for shader model 5.0, and the depth clear).
    enum class Utility : std::uint8_t { Blit, Clear, ClearDepth, DepthResolve, Convert, DisplayFilter, DepthCopy, Count };
    // How one of them treats depth and stencil.
    enum class DepthUse : std::uint8_t { None, Clear, Resolve, Count };
    class GpuPassScope {
    public:
        GpuPassScope(RendererD3D11& renderer, GpuPass pass) : renderer_(renderer), outer_(renderer.current_pass_) {
            renderer_.gpu_pass(pass);
        }
        ~GpuPassScope() { renderer_.gpu_pass(outer_); }
        GpuPassScope(const GpuPassScope&) = delete;
        GpuPassScope& operator=(const GpuPassScope&) = delete;

    private:
        RendererD3D11& renderer_;
        GpuPass outer_;
    };

    bool create_device();
    bool create_states();
    bool create_swap_chain();
    bool resize_swap_chain(std::uint32_t width, std::uint32_t height);
    void report_adapter();
    bool create_scene_targets(const FrameMap& map, unsigned samples);
    bool create_scene_set(SceneSet& set, const FrameMap& map, unsigned samples = 1u, unsigned width = kFrameUnits,
                          unsigned height = kFrameUnits, bool target = false);
    void destroy_scene_set(SceneSet& set);
    [[nodiscard]] unsigned wanted_msaa_samples() const;
    void resolve_scene_color();
    void resolve_scene_depth();
    void apply_window_size();
    [[nodiscard]] FrameMap output_frame_for_window() const;
    void swap_scene_sets();
    void swap_target_set();
    bool publish_picture(std::uint64_t key, std::uint32_t width, std::uint32_t height, gpu::TextureRef texture,
                         std::uint8_t channels, TextureStore& textures);
    void drop_picture(Picture& picture, TextureStore& textures);
    void record_conversion_blit(d3d11::Texture& dest, unsigned dest_w, unsigned dest_h, std::uint8_t copy_format, float src_x,
                                float src_y, float step_x, float step_y);
    // `source` drawn into `dest` through the conversion shader (the scene target's copies, a picture's channels).
    void conversion_draw(ID3D11ShaderResourceView* source, d3d11::Texture& dest, unsigned dest_w, unsigned dest_h,
                         std::uint8_t copy_format, float src_x, float src_y, float step_x, float step_y);
    // The output scene target at `output`'s size (single-sample), made when it is not that already.
    [[nodiscard]] bool ensure_output_set(const FrameMap& output);
    // The open scene pass's targets, viewport and scissor bound again (after one of the renderer's own draws or a part).
    void bind_pass_targets();

    // Frame recording.
    void begin_scene_pass(const char* caller = __builtin_FUNCTION());
    void end_scene_pass(const char* caller = __builtin_FUNCTION());
    // The recorded draws run: the upload memory they read reaches the GPU, then the ops replay on the immediate context.
    void flush_draws();
    // The roots of pass_ops_ from `from` on into one piece of the constant ring, as many as a piece holds; returns the
    // first op not written.
    std::size_t write_roots(std::size_t from, bool carried);
    void bind_constants(unsigned slot, gpu::Address address, bool vertex, bool pixel);
    void bind_rows(gpu::Address rows, gpu::Address instances);
    void reset_draw_bindings() noexcept;
    void sync_post_process();
    [[nodiscard]] bool wants_vsync() const;
    [[nodiscard]] bool wants_hdr() const;
    [[nodiscard]] unsigned present_sync_interval(unsigned configured, float max_fps);
    void retire(gpu::TextureRef texture);
    void write_table(d3d11::BoundTable& table, const TextureHandle* handles, unsigned count, const TextureStore& cache);
    [[nodiscard]] ID3D11SamplerState* sampler_for(const SamplerKey& key, const RenderConfig& cfg);
    void reset_sampler_frame();
    void replace_samplers(core::RetireRing& ring, unsigned slot, std::uint32_t config, bool all);
    // One of the renderer's own full-screen triangles into the bound targets: `rect` cuts it, `write_mask` the channels it
    // writes (D3D11_COLOR_WRITE_ENABLE_*), `source` its t0.
    void utility_draw(Utility shader, ID3D11ShaderResourceView* source, const void* constants, std::size_t constant_bytes,
                      const D3D11_VIEWPORT& viewport, const gpu::Rect& rect, std::uint8_t write_mask = 0xFu,
                      DepthUse depth = DepthUse::None);
    void refresh_display_rate(bool ask);
    void follow_window_and_resize();
    void report_present_stats(double pace_wait_ms, double present_ms);
    void grow_rings();
    void gpu_pass(GpuPass pass);
    // The picture as the window shows it, drawn into `target`: black, the picture in its content rectangle through the
    // post chain or the plain blit.
    void draw_presented_picture(ID3D11RenderTargetView* target, const d3d11::Texture& picture, unsigned width, unsigned height,
                                const galaxy::ContentViewport& content);

    WindowOwner* window_owner_ = nullptr;
    host_window::NativeWindow window_ = nullptr;
    bool window_shown_ = false;
    bool keep_window_ = false;
    bool occluded_ = false;
    // The device's adapter's video memory (video_memory); render thread.
    mutable DxgiMemory memory_;
    bool loss_asked_ = false;  // lose_device, until the present that reports it
    TextureStore* mirrored_store_ = nullptr;

    // The swap chain.
    d3d11::ComPtr<IDXGISwapChain3> swap_chain_;
    HANDLE frame_latency_waitable_ = nullptr;
    // The refresh the next present is drawn for (set_present_moment), and the display model its statistics feed.
    std::int64_t present_moment_ = 0;
    DisplayModel display_model_{galaxy::qpc_frequency()};
    d3d11::ComPtr<ID3D11RenderTargetView> backbuffer_rtv_;  // buffer 0 (flip model: always the one drawn next)
    std::uint32_t swap_width_ = 0u, swap_height_ = 0u;
    DXGI_FORMAT swap_format_ = DXGI_FORMAT_R8G8B8A8_UNORM;
    bool swap_vsync_ = false;
    bool tearing_ = false;
    bool swap_dirty_ = false;
    bool presented_ = false;  // the backbuffer holds this frame's picture (present drew it)
    std::unique_lock<std::timed_mutex> surface_lock_;
    static constexpr unsigned kSwapChainBuffers = 3u;

    std::array<Frame, kFramesInFlight> frames_{};
    unsigned frame_slot_ = 0;
    Frame* frame_ = &frames_[0];
    std::uint64_t frame_index_ = 0;
    std::uint64_t last_submitted_ = 0;

    // What pipelines are built against, and the renderer's own.
    d3d11::RenderLayout draw_layout_{};
    d3d11::ComPtr<ID3D11SamplerState> linear_clamp_;
    d3d11::ComPtr<ID3D11SamplerState> point_clamp_;
    // b3: the draws' DrawRoots in the constant ring (d3d11::Context::begin_constants), 256 bytes each, written ahead of
    // the draws in runs of one piece each, each draw bound to its own by offset (flush_draws, P-060).
    d3d11::ComPtr<ID3D11Buffer> root_buffer_;  // the ring the last run went to
    std::vector<UINT> root_first_;             // per pass op: where its Root was written (in 16-byte constants)
    gpu::DrawRoot root_carried_{};             // the last root written: the next pass starts with it bound
    UINT root_carried_first_ = 0u;             // where this pass's copy of it lies
    std::array<d3d11::ComPtr<ID3D11PixelShader>, static_cast<std::size_t>(Utility::Count)> utility_ps_{};
    d3d11::ComPtr<ID3D11VertexShader> full_screen_vs_;
    // --diagnostics constants-watch: once a frame, a compute read of a ring piece checked against what was written
    // (P-332); null without it.
    std::unique_ptr<d3d11::ConstantsWatch> constants_watch_;
    d3d11::ComPtr<ID3D11RasterizerState> utility_raster_;
    std::array<d3d11::ComPtr<ID3D11BlendState>, 16> write_masks_{};  // by D3D11_COLOR_WRITE_ENABLE_* mask
    std::array<d3d11::ComPtr<ID3D11DepthStencilState>, static_cast<std::size_t>(DepthUse::Count)> depth_uses_{};

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
    // The bound target's drawn part, within its textures (the viewport and scissor of its draws).
    [[nodiscard]] FrameMap scene_part() const noexcept {
        return scene_.color ? FrameMap(std::min(scene_map_.width, scene_.color->width),
                                       std::min(scene_map_.height, scene_.color->height))
                            : scene_map_;
    }
    // A depth picture for the game at the extent's size holding the whole frame (dynamic resolution's rectangle
    // resampled, point sampled), for a frame pass over the frame; its colour is draw_frame_pass's.
    bool publish_scene_depth_copy(std::uint64_t key, TextureStore& textures);
    // R32 float where the device filters it (a material may sample the picture so), else 16-bit float.
    DXGI_FORMAT depth_copy_format_ = DXGI_FORMAT_R32_FLOAT;
    SceneSet scene_;
    SceneSet wide_scene_;
    SceneSet alt_scene_;
    FrameMap alt_scene_map_{};
    FrameMap alt_scene_extent_{};  // its allocated size (alt_scene_map_: the part drawn), traded with frame_extent_
    std::unordered_map<std::uint64_t, Picture> pictures_;
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
    bool scene_pass_open_ = false;
    const char* scene_pass_closed_by_ = "frame start";
    std::array<float, 12> scene_view_{};
    bool scene_view_known_ = false;
    float scene_lens_[3]{1.0f, 100000.0f, 1.0f};
    bool present_black_ = false;
    bool present_game_frame_ = false;
    std::uint64_t wide_scene_used_step_ = 0u;
    std::uint64_t output_scene_used_step_ = 0u;
    std::int64_t camera_metered_qpc_ = 0;

    // The renderer's parts (fx_d3d11.h).
    d3d11::PostProcessor post_;
    d3d11::GameBloom bloom_;
    d3d11::AmbientOcclusion ao_;
    d3d11::SceneCamera scene_camera_;
    SceneView scene_ao_view_{};
    std::vector<ao::ObjectOccluder> scene_ao_objects_;
    PostOutput hdr_output_{};

    // Draws.
    UploadRing vertex_ring_;
    UploadRing index_ring_;
    UploadRing constant_ring_;
    UploadRing matrix_ring_;
    std::unordered_map<SamplerCacheKey, d3d11::ComPtr<ID3D11SamplerState>, SamplerCacheKeyHasher> samplers_;
    std::unordered_map<SamplerTableKey, std::unique_ptr<d3d11::BoundTable>, SamplerTableKeyHasher> sampler_tables_;
    std::uint32_t sampler_config_ = 0;
    std::uint32_t sampler_generation_ = 1u;
    bool sampler_ready_ = false;
    gpu::Table default_sampler_table_{};
    std::uint32_t zero_stream_bytes_ = 0u;
    gpu::Address zero_stream_address_ = 0u;
    gpu::BufferRef zero_vertices_;
    bool ring_growth_failed_ = false;
    // The open pass's recorded draws (gpu::Encoder writes them; flush_draws replays them).
    std::vector<gpu::DrawOp> pass_ops_;
    // Viewport and scissor of the scene (bind_scene), set again after one of the renderer's own draws.
    struct Bound {
        gpu::ViewportRect viewport{};
        bool viewport_valid = false;
        gpu::Rect scissor{};
        bool scissor_valid = false;
    } bound_;
    gpu::Encoder encoder_;

    // Tables that live across frames, and what the GPU may still read until a fence passes.
    struct RetiredTable {
        d3d11::BoundTable* table = nullptr;
        std::uint64_t fence = 0;
    };
    std::vector<std::unique_ptr<d3d11::BoundTable>> persistent_tables_;
    std::vector<RetiredTable> retired_tables_;
    std::vector<d3d11::BoundTable*> free_tables_;

    // Scene target copies.
    std::unordered_map<std::uint64_t, FramePassTarget> frame_pass_targets_;
    std::uint64_t frame_pass_color_key_ = 0u;
    std::uint64_t frame_pass_depth_key_ = 0u;

    // Captures and telemetry.
    std::string backbuffer_capture_path_;
    // The pictures copied out (the presented picture and the display picture), each its own staging texture.
    using Captures = PictureCaptures<d3d11::ComPtr<ID3D11Texture2D>>;
    Captures captures_;
    // A staging texture for a capture of `width` x `height` `format` pixels: a spare one, or a new one.
    [[nodiscard]] d3d11::ComPtr<ID3D11Texture2D> capture_staging(unsigned width, unsigned height, DXGI_FORMAT format);
    std::atomic<double> measured_present_hz_{0.0};
    double display_refresh_hz_ = 0.0;
    // Variable refresh, as on Direct3D 12 (renderer_d3d12.h): the answer, and the probe rate while the presents' proof runs.
    bool variable_refresh_ = false;
    RefreshAnswer refresh_answer_;
    double refresh_probe_hz_ = 0.0;
    double display_mode_hz_ = 0.0;  // the mode's own rate, read when the display changes
    // The display's rate is read again twice a second on the clock, whatever the present rate (an unlimited one presents
    // thousands a second).
    galaxy::ClockInterval display_refresh_{500, true};
    galaxy::ClockInterval budget_warning_{10'000, true};  // a scale over the video-memory budget: one line in ten seconds
    unsigned latency_timeouts_ = 0u;
    std::int64_t swap_present_qpc_ = 0;
    std::int64_t measured_present_window_qpc_ = 0;
    std::uint64_t measured_present_count_ = 0;
    struct PresentStats {
        std::int64_t window_start_qpc = 0;
        std::int64_t last_present_qpc = 0;
        std::uint64_t presents = 0, intervals = 0, gpu_samples = 0;
        double interval_ms = 0.0, interval_max_ms = 0.0, pace_wait_ms = 0.0, pace_wait_max_ms = 0.0;
        double present_ms = 0.0, present_max_ms = 0.0, gpu_ms = 0.0;
    } stats_;
    PresentPacer present_pacer_;
    FrameGpuTime frame_gpu_;  // the frame's GPU time: the governor's and the interpolation's measures
    bool scene_drawn_ = false;
    GpuPass current_pass_ = GpuPass::Scene;
    std::atomic<bool> gpu_timing_wanted_{false};
    std::atomic<float> frame_gpu_ema_ms_{0.0f};
    GpuPassTimes pass_times_;  // timed(): this frame marks its passes
    void read_pass_times(Frame& frame, UINT64 frequency, UINT64 frame_end);

    // draw.post's stages by program (mod_post_d3d11.cpp), dropped when the program leaves (mod_post_epoch); a stage the
    // driver refused stays null, so its pass draws nothing and the log says why once.
    struct ModPostShaders {
        std::uint32_t program = 0u;
        d3d11::ComPtr<ID3D11VertexShader> vertex;
        d3d11::ComPtr<ID3D11PixelShader> pixel;
    };
    std::vector<ModPostShaders> mod_post_shaders_;
    std::uint64_t mod_post_epoch_ = 0u;
    // draw.native's context state: a mod's function binds what it likes there, and the renderer's own state is swapped
    // back in after it (ID3D11DeviceContext1::SwapDeviceContextState).
    d3d11::ComPtr<ID3DDeviceContextState> native_state_;
    bool native_state_failed_ = false;
    [[nodiscard]] const ModPostShaders* mod_post_stages(const ModPostProgram& program);
    void release_mod_post();
    // What frames in flight may still read when it is given up, per frame slot (core::RetireRing; its first users are the
    // samplers and sampler tables a start again leaves behind). Last, so it is emptied before the rest goes.
    core::RetireRing retire_ring_{kFramesInFlight};
};

using Renderer = RendererD3D11;

}  // namespace galaxy::render
