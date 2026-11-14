// SPDX-License-Identifier: GPL-3.0-only
// The renderer on Direct3D 11 (feature level 11_0 and up), with the interface RenderCore uses on RendererD3D12,
// RendererVk and RendererMtl. The executable NebulaNativeD11 is built with it (NEBULA_GPU_D3D11); the Direct3D 12
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
#include "galaxy/launch_args.h"
#include "galaxy/render/ambient_occlusion_objects.h"
#include "galaxy/render/d3d11/d3d11_context.h"
#include "galaxy/render/d3d11/fx_d3d11.h"
#include "galaxy/render/display_modes.h"
#include "galaxy/render/frame_pass.h"
#include "galaxy/render/gpu_types.h"
#include "galaxy/render/gpu_upload_ring.h"
#include "galaxy/render/host_window.h"
#include "galaxy/render/post_process_settings.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/renderer_common.h"
#include "galaxy/render/rt/rt_backend.h"
#include "galaxy/render/rt/rt_denoiser.h"
#include "galaxy/render/rt_types.h"
#include "galaxy/render/window_owner.h"
#include "galaxy/render_api.h"
#include "render/core/draw_record.h"
#include "render/core/gpu_encoder.h"

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
    void set_scene_ao_view(const RtView& view) noexcept { scene_ao_view_ = view; }
    void set_scene_ao_objects(std::vector<ao::ObjectOccluder>& objects) noexcept { scene_ao_objects_.swap(objects); }
    void record_scene_ao();
    // The game's bloom (d3d11::GameBloom): `kind` a NEBULA_PASS_BLOOM_*, `a` and `b` its values.
    [[nodiscard]] bool game_bloom_ready() const noexcept { return bloom_.ready(); }
    void game_bloom(std::uint32_t kind, std::uint32_t a, std::uint32_t b);
    // No frame generation or temporal upscaler on Direct3D 11: the interpolation and upscaling calls say there is none.
    void reset_generated_gpu_ema() noexcept {}
    [[nodiscard]] double generated_gpu_ema_ms() const noexcept { return 0.0; }
    [[nodiscard]] double interpolation_gpu_cost_ms() const noexcept { return native_scene_gpu_ema_ms_; }
    void suspend_temporal_interpolation(double) {}
    [[nodiscard]] bool temporal_interpolation() const noexcept { return false; }
    [[nodiscard]] unsigned temporal_output_width() const noexcept { return 0u; }
    [[nodiscard]] double gpu_ms_per_tick() const noexcept { return 0.0; }
    // Smoothed GPU time of a frame that drew the scene (0 until measured).
    [[nodiscard]] double scene_gpu_ema_ms() const noexcept { return native_scene_gpu_ema_ms_; }
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
    void end_wide_scene(unsigned finish = 0u);  // finish: the traced relight's camera (always 0: nothing traces here)
    [[nodiscard]] bool temporal_mode() const { return false; }
    [[nodiscard]] bool motion_pass_only() const { return false; }
    [[nodiscard]] bool has_layer_targets() const noexcept { return false; }
    // The draws' textures: a draw's own tables of up to table_slots() maps (no arrays on Direct3D 11).
    [[nodiscard]] bool bindless() const noexcept { return false; }
    [[nodiscard]] std::uint32_t texture_slot(const TextureHandle&) const noexcept { return 0u; }
    [[nodiscard]] std::uint32_t sampler_slot(const SamplerKey&) { return 0u; }
    void write_texture_list(const std::uint32_t*, std::uint32_t, gpu::Address& lists, std::uint32_t& first) {
        lists = 0u;
        first = 0u;
    }
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
    // The video memory in use and its budget (IDXGIAdapter3::QueryVideoMemoryInfo), read at a frame boundary.
    [[nodiscard]] bool video_memory(std::uint64_t& usage, std::uint64_t& budget) const noexcept;
    void wait_for_gpu_idle();
    void pump_messages() {}
    [[nodiscard]] bool quit_requested() const noexcept;
    // A covered or minimised window: the swap chain's word (DXGI_STATUS_OCCLUDED from a test present).
    [[nodiscard]] bool occluded() const noexcept { return occluded_; }
    void probe_occlusion() noexcept;

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
    [[nodiscard]] static std::uint64_t allocation_bytes(const d3d11::Texture* texture) noexcept {
        return texture != nullptr ? texture->allocation_bytes : 0u;
    }
    // GPU time by pass (render_api.h GpuPassTime): any thread.
    void set_gpu_timing(bool on) noexcept { gpu_timing_wanted_.store(on, std::memory_order_relaxed); }
    [[nodiscard]] bool gpu_timing_on() const noexcept { return gpu_timing_wanted_.load(std::memory_order_relaxed); }
    // What the ray tracer records through on the other renderers: there is no tracer on Direct3D 11 (feature level 11
    // hardware has no ray tracing), so the render core never comes here; they say there is nothing.
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
    // `black`: the frame is presented black (the pass clears; the display picture is not drawn).
    bool present(const DisplayTexture* source, bool capture_backbuffer = false, bool black = false);

    [[nodiscard]] bool begin_output_scene(bool resolve_msaa_only = false);
    [[nodiscard]] bool begin_target_pass(std::uint64_t key, std::uint32_t width, std::uint32_t height);
    void end_target_pass(std::uint8_t channels, TextureStore& textures);
    [[nodiscard]] bool target_picture(std::uint64_t key, TextureHandle& out) const;
    [[nodiscard]] bool target_pass_active() const noexcept { return target_pass_active_; }
    void end_output_scene();
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
    // A present rate above 60.
    [[nodiscard]] bool interpolation_mode() const { return false; }
    void take_generated_gpu_ms(double& total_ms, std::uint64_t& samples) noexcept {
        total_ms = 0.0;
        samples = 0u;
    }
    void set_scene_camera(float near_plane, float far_plane, float fov_y) noexcept {
        scene_lens_[0] = near_plane;
        scene_lens_[1] = far_plane;
        scene_lens_[2] = fov_y;
    }

    [[nodiscard]] double measured_present_hz() const noexcept { return measured_present_hz_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t frame_index() const noexcept { return frame_index_; }

    // Each point's depth (0 near, 1 far) into slot `slot`'s readback; screen_probes_finished names the values once that
    // frame's fence has passed. `compared` probes are answered by a depth test of each point (1 seen, 0 hidden).
    [[nodiscard]] bool record_screen_probes(std::size_t slot, std::span<const ScreenProbe> probes, bool compared);
    [[nodiscard]] const float* screen_probes_finished(std::size_t slot);

    // Test captures: the scene target and the presented picture as PPM files (paths from set_debug_capture_paths).
    bool debug_copy_scene_to_readback();
    void debug_log_readback();
    void debug_log_backbuffer_readback();
    bool debug_copy_selected_display_to_readback(const DisplayTexture& source);
    void debug_log_display_readback();
    void set_debug_capture_paths(std::string scene_ppm_path, std::string backbuffer_ppm_path);
    // The pictures handed to an extension, taken from the GPU and written (render thread; RenderCore::finish_pictures,
    // before a process that ends without the shutdown).
    void finish_pictures();

    [[nodiscard]] gpu::Device* device() const noexcept { return d3d11::Context::get().device(); }
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
    enum class GpuPass : std::uint8_t {
        Scene, Resolve, FramePass, DisplayCopy, Bloom, SceneAa, SceneAo, Present, Camera, Count
    };
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
    };
    struct FramePassTarget {
        gpu::TextureRef texture;
        unsigned width = 0;
        unsigned height = 0;
    };
    struct Readback {
        d3d11::ComPtr<ID3D11Texture2D> staging;  // D3D11_USAGE_STAGING, CPU read
        std::uint64_t fence = 0;                 // 0: idle
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
    enum class Utility : std::uint8_t { Blit, Clear, ClearDepth, DepthResolve, Convert, DisplayFilter, Count };
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
    [[nodiscard]] std::uint64_t target_idle_frames() const noexcept { return kTargetIdleFrames; }

    // Frame recording.
    void begin_scene_pass(const char* caller = __builtin_FUNCTION());
    void end_scene_pass(const char* caller = __builtin_FUNCTION());
    // The recorded draws run: the upload memory they read reaches the GPU, then the ops replay on the immediate context.
    void flush_draws();
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
    // One of the renderer's own full-screen triangles into the bound targets: `rect` cuts it, `write_mask` the channels it
    // writes (D3D11_COLOR_WRITE_ENABLE_*), `source` its t0.
    void utility_draw(Utility shader, ID3D11ShaderResourceView* source, const void* constants, std::size_t constant_bytes,
                      const D3D11_VIEWPORT& viewport, const gpu::Rect& rect, std::uint8_t write_mask = 0xFu,
                      DepthUse depth = DepthUse::None);
    void refresh_display_rate();
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
    TextureStore* mirrored_store_ = nullptr;

    // The swap chain.
    d3d11::ComPtr<IDXGISwapChain3> swap_chain_;
    HANDLE frame_latency_waitable_ = nullptr;
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
    d3d11::ComPtr<ID3D11Buffer> root_constants_;     // b3: the draw's DrawRoot, written per change (dynamic)
    d3d11::ComPtr<ID3D11Buffer> utility_constants_;  // the renderer's own draws' b0 (dynamic)
    std::array<d3d11::ComPtr<ID3D11PixelShader>, static_cast<std::size_t>(Utility::Count)> utility_ps_{};
    d3d11::ComPtr<ID3D11VertexShader> full_screen_vs_;
    d3d11::ComPtr<ID3D11ComputeShader> probe_cs_;
    d3d11::ComPtr<ID3D11RasterizerState> utility_raster_;
    std::array<d3d11::ComPtr<ID3D11BlendState>, 16> write_masks_{};  // by D3D11_COLOR_WRITE_ENABLE_* mask
    std::array<d3d11::ComPtr<ID3D11DepthStencilState>, static_cast<std::size_t>(DepthUse::Count)> depth_uses_{};

    // The scene target.
    FrameMap scene_map_{};
    SceneSet scene_;
    SceneSet wide_scene_;
    SceneSet alt_scene_;
    FrameMap alt_scene_map_{};
    std::unordered_map<std::uint64_t, Picture> pictures_;
    static constexpr std::uint64_t kTargetIdleFrames = 600u;  // frames at the game's 60 a second
    std::unordered_map<std::uint64_t, TargetSlot> target_slots_;
    void retire_target_slot(TargetSlot& slot);
    void sweep_target_slots();
    std::uint64_t target_key_ = 0;
    SceneSet target_scene_;
    FrameMap target_scene_map_{};
    bool target_pass_active_ = false;
    bool output_scene_active_ = false;
    bool scene_pass_open_ = false;
    const char* scene_pass_closed_by_ = "frame start";
    std::array<float, 12> scene_view_{};
    bool scene_view_known_ = false;
    float scene_lens_[3]{1.0f, 100000.0f, 1.0f};
    bool present_black_ = false;
    bool present_game_frame_ = false;
    std::uint64_t wide_scene_used_frame_ = 0u;
    std::uint64_t output_scene_used_frame_ = 0u;
    std::int64_t camera_metered_qpc_ = 0;

    // The renderer's parts (fx_d3d11.h).
    d3d11::PostProcessor post_;
    d3d11::GameBloom bloom_;
    d3d11::AmbientOcclusion ao_;
    d3d11::SceneCamera scene_camera_;
    RtView scene_ao_view_{};
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

    // Screen probes: a slot's points (written here) and values (the dispatch writes, read back), fenced with its frame.
    struct ProbeSlot {
        d3d11::ComPtr<ID3D11Buffer> points;  // the frame pixels to read (uint2), dynamic
        d3d11::ComPtr<ID3D11ShaderResourceView> points_view;
        d3d11::ComPtr<ID3D11Buffer> values;  // written by the probe dispatch
        d3d11::ComPtr<ID3D11UnorderedAccessView> values_view;
        d3d11::ComPtr<ID3D11Buffer> readback;  // D3D11_USAGE_STAGING
        std::uint64_t capacity = 0;  // points they hold
        std::uint64_t fence = 0;     // 0: idle; kProbesInFrame: recorded in the frame being built
        std::uint32_t count = 0;
        std::vector<float> answers;
    };
    static constexpr std::uint64_t kProbesInFrame = ~std::uint64_t{0};
    std::vector<ProbeSlot> probe_slots_;

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
    Readback display_capture_;
    std::string display_capture_pending_path_;
    unsigned display_capture_width_ = 0;
    unsigned display_capture_height_ = 0;
    std::atomic<double> measured_present_hz_{0.0};
    double display_refresh_hz_ = 0.0;
    bool variable_refresh_ = false;
    unsigned display_refresh_checks_ = 0u;
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
    double native_scene_gpu_ema_ms_ = 0.0;
    bool scene_drawn_ = false;
    GpuPass current_pass_ = GpuPass::Scene;
    std::atomic<bool> gpu_timing_wanted_{false};
    std::atomic<float> frame_gpu_ema_ms_{0.0f};
    bool pass_timing_on_ = false;  // this frame marks its passes
    std::array<std::atomic<float>, static_cast<std::size_t>(GpuPass::Count)> pass_ema_ms_{};
    std::array<double, static_cast<std::size_t>(GpuPass::Count)> telemetry_pass_ms_{};
    void read_pass_times(Frame& frame, UINT64 frequency, UINT64 frame_end);
};

using Renderer = RendererD3D11;

}  // namespace galaxy::render
