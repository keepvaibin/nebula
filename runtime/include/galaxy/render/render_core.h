#pragma once

// RenderCore: runs the frame behind the galaxy::render API (render_api.h, through render_api.cpp). The game's thread hands over
// one frame list at a time; the render thread walks it once per presented frame (core/draw_walker.cpp): at its own time, and
// between lists at the times the display wants. A full queue blocks the producer. A render-thread failure is rethrown from the
// render_frame() call rather than leaving a frozen display.

#include "galaxy/platform_time.h"
#include "galaxy/render/ambient_occlusion_objects.h"
#include "galaxy/render/frame_pass.h"
#include "galaxy/render/movie_playback.h"
#if defined(NEBULA_GPU_VULKAN)
#include "galaxy/render/vk/renderer_vk.h"
#elif defined(NEBULA_GPU_METAL)
#include "galaxy/render/mtl/renderer_mtl.h"
#elif defined(NEBULA_GPU_D3D11)
#include "galaxy/render/d3d11/renderer_d3d11.h"
#else
#include "galaxy/render/renderer_d3d12.h"
#endif
#include "galaxy/render/renderer_common.h"
#include "galaxy/render/scene_view.h"
#include "galaxy/render/texture_store.h"
#include "galaxy/render/frame_interpolation.h"
#include "galaxy/render/frame_schedule.h"
#include "galaxy/render_api.h"
#include "galaxy/frame_telemetry.h"
#include "render/core/draw_record.h"
#include "render/core/frame_core.h"
#include "render/core/pipelines.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <memory>
#include <optional>
#include <mutex>
#include <span>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace galaxy::render {

// Replacement LOD adjustments are floating-point mip levels. `arbitrary`: a replacement whose levels pair with the game's
// by index; `authored`: the game's own levels drawn by hand (TextureHandle::authored_levels). Both choose the level the
// game showed at its authored lines (sampler_frame_bias) without the settings' bias (kSamplerAuthoredLevels).
// The key holds the texture's own state only: what follows the frame is the programs' (sampler_frame_bias).
[[nodiscard]] SamplerKey make_sampler_key(const TextureSampler& mode, std::uint32_t mip_levels, float lod_shift = 0.0f,
    bool arbitrary = false, bool authored = false) noexcept;

// The part of a sampler's level bias that follows the frame, which the programs add to the sampler's own
// (program.hlsli BIAS, from core::ViewRows::map_bias): none (no levels); the scene's, an upscaler's, for the game's
// levels; or, for levels drawn by hand, the frame's lines over the authored lines, so they show the level the game
// showed. Out of the key, dynamic resolution's steps, the window's height and the upscaler make no new samplers.
enum class SamplerFrameBias : std::uint32_t { none = 0u, scene = 1u, lines = 2u };
[[nodiscard]] SamplerFrameBias sampler_frame_bias(const TextureSampler& mode, bool arbitrary, bool authored) noexcept;

void note_layer_2d_lines(unsigned lines) noexcept;


class RenderCore final {
public:
    RenderCore() = default;

    bool initialize(int width, int height);
    [[nodiscard]] std::string startup_failure() const { return renderer_.startup_failure(); }
    void render_frame(bool present_swap_chain = true, bool black = false);
    void present_cached_display(
        bool black,
        bool present_swap_chain = true);
    void wait_for_render_idle();
    // At a host's exit, which ends the process without the renderer's shutdown: every picture handed to an extension
    // (present_snapshot_path) taken from the GPU and written, on the render thread. False when it did not answer
    // within `limit` (stopped, or its device gone).
    bool finish_pictures(std::chrono::milliseconds limit);
    void set_frame_end_presents(bool enabled) noexcept {
        frame_end_presents_.store(enabled, std::memory_order_relaxed);
    }
    [[nodiscard]] unsigned bloom_route() const noexcept {
        return bloom_route_.load(std::memory_order_relaxed);
    }
    // Services, on the game's thread.
    [[nodiscard]] core::Registries& registries() noexcept { return registries_; }
    [[nodiscard]] PipelineProgress pipeline_progress() const noexcept {
        const std::uint32_t queued = pipelines_queued_.load(std::memory_order_relaxed);
        // Registered counts from the moment the game names a pipeline: the cook's list, registered at the start, is
        // still to make before the render thread has been handed it (the title's wait asks in those first frames).
        const std::uint32_t count = registries_.registered_pipeline_count();
        return {static_cast<std::uint32_t>(pipelines_.created()), pipelines_.pending() + (count > queued ? count - queued : 0u),
                pipelines_.warm()};
    }
    [[nodiscard]] TextureStore& textures() noexcept { return texture_store_; }
    // The list the next render_frame hands over.
    void attach_frame_list(NebulaFrameList* list) noexcept { producer_list_ = list; }
    // Simulation thread: the step clock's moment for the step now starting (render_api.h set_step_moment).
    void set_step_moment(std::int64_t qpc) noexcept { producer_step_moment_qpc_.store(qpc, std::memory_order_relaxed); }

    // Simulation thread: the movie frame later submissions carry (movie_playback.h).
    void attach_movie_frame(const char* name, std::int32_t frame, float frame_rate, const void* data, std::uint64_t size,
                            std::uint32_t width, std::uint32_t height) noexcept {
        MovieFrameMark& mark = producer_movie_frame_;
        if (frame == MovieFrameMark::kNone) {
            mark = {};
            return;
        }
        if (name != nullptr) {
            mark.name = {};
            for (std::size_t i = 0; name[i] != 0 && i + 1u < mark.name.size(); ++i) mark.name[i] = name[i];
        }
        mark.frame = frame;
        mark.frame_rate = frame_rate;
        if (data != nullptr) {
            mark.data = static_cast<const std::uint8_t*>(data);
            mark.size = size;
        }
        mark.width = width;
        mark.height = height;
    }
    void pump_messages();
    [[nodiscard]] bool quit_requested() const noexcept;
    // Out of sight (render_api.h set_window_hidden): no presents of its own, longer waits. Any thread.
    void set_window_hidden(bool hidden) noexcept { window_hidden_.store(hidden, std::memory_order_relaxed); }
    // Open but not in front (render_api.h set_window_background): the game's own frames only, at the game's rate. Any thread.
    void set_window_background(bool background) noexcept { window_background_.store(background, std::memory_order_relaxed); }
    [[nodiscard]] bool window_occluded() const noexcept { return initialized_ && renderer_.occluded(); }
    // Minimised (the host's word) or covered (the OS's): nothing of its own is presented and no generated frame is made.
    [[nodiscard]] bool out_of_sight() const noexcept { return window_hidden_.load(std::memory_order_relaxed) || window_occluded(); }
    // GPU time by pass (render_api.h); any thread.
    void set_gpu_timing(bool on) noexcept { renderer_.set_gpu_timing(on); }
    [[nodiscard]] bool gpu_timing_on() const noexcept { return renderer_.gpu_timing_on(); }
    [[nodiscard]] std::size_t gpu_pass_times(GpuPassTime* out, std::size_t capacity) const noexcept {
        return renderer_.gpu_pass_times(out, capacity);
    }
    [[nodiscard]] std::uint64_t pictures_made() const noexcept;
    [[nodiscard]] bool video_memory(std::uint64_t& usage, std::uint64_t& budget) const noexcept {
        return renderer_.video_memory(usage, budget);
    }
    [[nodiscard]] DisplayPresentStats display_present_stats() const noexcept;

private:

    // One queued frame: its records and what the game said about it.
    struct FrameChunk {
        std::uint64_t device_epoch = 0u;  // the card its work was made for
        core::RegistryOps registry_ops;
        std::shared_ptr<TextureStore::Ops> texture_ops;
        PointerResponseStamp pointer_response{};
        bool prerecorded_movie{};
        MovieFrameMark movie_frame{};
        bool black = false;  // the output shows black (no picture)
        bool present_swap_chain = true;
        bool present_only = false;
        // When the game asked for this frame's present (QPC, its thread): the frame's arrival for the interpolation's
        // clock, which a take on the render thread would see late by whatever that thread was drawing.
        std::int64_t present_asked_qpc = 0;
        // The moment of the step that made it on the step clock (set_step_moment); 0 none.
        std::int64_t step_moment_qpc = 0;
        NebulaFrameList* list = nullptr;
    };

    void reset_session_state();

    void render_thread_main();
    void render_loop();
    // A lost card, with `error` from the render loop: true when the game goes on with a new one.
    [[nodiscard]] bool bear_device_loss(std::exception_ptr error);
    [[nodiscard]] bool restart_device();
    [[nodiscard]] bool restart_renderer(const FrameMap& frame, bool same_card);
    void drop_device_state();
    void render_frame_on_thread(FrameChunk& chunk);
    // Presents black, a named picture (`slot`) or the newest.
    void present_cached_display_on_thread(
        bool black,
        bool count_game_display_present = true,
        std::optional<std::uint32_t> slot = std::nullopt);
    // The output shows black (FrameChunk::black): one black present per black
    // run, repeated while the settings overlay is open. `in_frame`: inside a render chunk's
    // begin_frame/end_frame.
    void present_black_picture(bool in_frame);
    bool black_shown_ = false;
    PointerResponseStamp producer_pointer_response_{}, render_pointer_response_{}, last_response_{};
    // Render thread: inside GameScene::draw3D (between the scene markers).
    bool scene_phase_ = false;
    std::atomic<unsigned> bloom_route_{0u};
    // Temporal upscaling for the current scene: perspective projections carry
    // this sub-pixel jitter (render pixels).
    bool temporal_scene_ = false;
    // The temporal scene writes motion vectors (the motion target is live).
    bool motion_scene_ = false;
    // Ambient occlusion (RenderConfig::ambient_occlusion), sampled at each frame start.
    bool ambient_occlusion_ = false;
    // The view of a perspective scene draw for ambient occlusion: its projection with the upscaler's jitter, depth
    // range and viewport.
    [[nodiscard]] SceneView scene_depth_view(const float terms[6], const ScaledViewport& viewport) const;
    // A list and what came with it, from its arrival until nothing can read it.
    struct ListEntry {
        core::ListData data;
        ListEntry* before = nullptr;
        PointerResponseStamp pointer_response{};
        bool prerecorded_movie = false;
        MovieFrameMark movie_frame{};
        // What the services queued before this list, applied at its first walk.
        core::RegistryOps registry_ops;
        std::shared_ptr<TextureStore::Ops> texture_ops;
        bool ops_waiting = false;
    };
    struct WalkState {
        core::ListData* now = nullptr;
        core::ListData* before = nullptr;
        core::WalkTime time{};
        std::uint64_t serial = 0u;
        std::uint32_t rows_base = 0u;
        gpu::Address rows_address = 0u, instances = 0u, ring_vertices = 0u, ring_indices = 0u;
        bool between = false, same_list_ring = false;
        float mip_lines = 0.0f;  // log2 of the frame's lines over the authored lines, taken once per walk (every draw's table reads it)
    };
    struct WalkView {
        std::uint64_t stamp = 0u;
        gpu::Address address = 0u;
        gpu::Rect scissor{};
        gpu::ViewportRect viewport{};
        float frame_height = 0.0f;
        bool empty = false;
    };
    // A generated frame's target has a copy of its own, named by this bit.
    static constexpr std::uint64_t kGeneratedPicture = 1ull << 40u;

    ListEntry& list_arrived(FrameChunk& chunk);
    void release_lists_before(const ListEntry& entry) noexcept;
    void begin_walk_frame(ListEntry& entry);
    std::vector<std::uint32_t> freed_textures_;
    std::vector<gpu::Table> retired_tables_;
    // Every draw's texture list, for the life of its table (FRAME-MODEL 2.10); the chunks are the renderer's.
    core::TextureLists texture_lists_;
    std::vector<core::TextureLists::Place> released_lists_;
    void give_up_released_lists();
    void walk_frame(ListEntry& entry, const core::WalkTime& time, bool present);
    void flush_waiting_list();
    void scan_list(core::ListData& now);
    void prepare_walk(core::ListData& now, core::ListData* before, const core::WalkTime& time);
    void walk_list(core::ListData& now, core::ListData* before, const core::WalkTime& time);
    void walk_pass(core::ListData& now, std::uint32_t index, std::uint32_t& target_channels);
    void set_scene_sky(const NebulaFramePass& scene_begin);
    void walk_draw_pass(const NebulaFramePass& pass);
    // The pass's draws, resolved in order and handed to `encoder` (draw_walker.cpp: drawn at once, or recorded for chunks).
    template <class Draws>
    void walk_draw_items(const NebulaFramePass& pass, Draws& encoder);
    [[nodiscard]] unsigned recording_workers(const NebulaFramePass& pass) const;
    void submit_recorded(unsigned workers);
    gpu::DrawRecorder draw_record_;
    std::vector<std::uint8_t> record_chunk_aux_;
    std::unique_ptr<core::RecordPool> record_pool_;
    const WalkView& walk_view(std::uint32_t index, const NebulaFramePass& pass);
    const core::StaticSlot& walk_material(std::uint32_t index);
    // Makes a frame constants entry's rows as this walk uploads them: the list's own, or at t (kept in walk_lerped_).
    void walk_material_settle(std::uint32_t index);
    // A settled entry's rows. Reads only: a pointer from it stays valid until the next walk_material_settle.
    [[nodiscard]] const float* walk_material_rows(std::uint32_t index) const;
    // One material at this walk's t: one entry, one registered slot, or rows the same to the byte.
    [[nodiscard]] bool walk_same_material(std::uint32_t a, std::uint32_t b);
    // The ring draws from `first` that are one draw with it; their indices in `index_count`.
    [[nodiscard]] std::uint32_t merge_ring_run(std::uint32_t first, std::uint32_t end, std::uint32_t matrix_at,
                                               std::uint32_t& index_count);
    // The table's texture list for this walk: the buffer and its first pair (DrawRoot::table). Direct3D 12, and Vulkan with
    // descriptor indexing (renderer_.bindless()).
    void walk_tables(std::uint32_t id, gpu::Address& lists, std::uint32_t& first);
    std::vector<TextureHandle> walk_handles_;
    std::vector<std::uint32_t> walk_pairs_;
#if defined(NEBULA_GPU_VULKAN) || defined(NEBULA_GPU_METAL) || defined(NEBULA_GPU_D3D11)
    // Vulkan without descriptor indexing: the table's descriptor sets, as many maps as a bound table holds.
    void walk_bound_tables(std::uint32_t id, gpu::Table& textures, gpu::Table& samplers, std::uint32_t& map_bias);
    std::vector<SamplerKey> walk_keys_;
#endif
    [[nodiscard]] bool item_culled(const NebulaFrameItem& item, const NebulaFramePass& pass, std::uint32_t state,
                                   const core::GpuMesh* mesh) noexcept;
    [[nodiscard]] gpu::Address ring_item_before(std::uint32_t index, gpu::Address own) const noexcept;
    void walk_scene_begin(const NebulaFramePass& pass);
    void walk_opaque_end(const NebulaFramePass& pass);
    void walk_scene_end();
    void walk_target_begin(const NebulaFramePass& pass);
    void walk_target_end(std::uint32_t channels);
    void walk_show();
    // draw.post (docs/modding/reference/services/draw.md): a mod's post program full screen into a new picture, the frame from there.
    void walk_post(const NebulaFrameList& list, const NebulaFramePass& pass);
    // draw.native (docs/modding/reference/services/draw.md): a mod's own GPU commands at a draw point, on this thread.
    void walk_native(const NebulaFrameList& list, const NebulaFramePass& pass);
    [[nodiscard]] bool generated_written(std::uint32_t texture) const noexcept;
    void note_generated_written(std::uint32_t texture);

    DeviceGate device_gate_;
    bool device_lost_ = false;
    std::uint32_t device_losses_ = 0u;
    // Game frames (list serials), not pictures: the pictures a game frame is shown in vary with the rate.
    std::uint64_t table_sweep_at_ = 0u;  // the game frame the next texture table sweep is due
    std::uint64_t table_log_at_ = 0u;    // and --diagnostics memory's table line
    std::uint64_t vram_log_at_ = 0u;  // the game frame the next look at whether a video memory line is due
    std::uint64_t vram_report_at_ = 0u;  // the game frame the line is due whatever changed
    std::uint64_t vram_logged_budget_mb_ = 0u;
    std::uint64_t vram_logged_left_out_mb_ = 0u;
    std::atomic<bool> window_hidden_{false};
    std::atomic<bool> window_background_{false};
    std::int64_t occlusion_probe_due_qpc_ = 0;  // render thread: the next look at whether a covered window shows again
    // The frame boundary's read of the adapter's video memory: past its budget the texture store gives textures back.
    void enforce_video_memory();
    std::uint64_t asked_loss_frame_ = 0u;  // lose_gpu_device: the frame of the loss being recovered from (0: none)
    std::int64_t last_device_loss_qpc_ = 0;
    NebulaFrameList* in_hand_list_ = nullptr;  // popped from the queue, not yet a ListEntry

    core::Registries registries_;
    core::Pairing pairing_;
    // --diagnostics present-stats ([interp-coverage]): each list drawn between steps (not a cut), its items by category
    // (NEBULA_ITEM_CATEGORY_*): those shown at the newest step only, because no item or block of the list before is
    // theirs (new; missed: the list before held an item of the same slot, so it was there but did not pair), because
    // their ring vertices have no pair of the same shape (ring-at-b), or because the game marks their vertices as having
    // no identity (discrete). Run totals; each category's first misses are logged with what differed ([interp-miss]).
    struct CoverageCount {
        std::uint64_t items = 0u, new_items = 0u, missed = 0u, ring_at_b = 0u, discrete = 0u;
    };
    std::array<CoverageCount, 256> coverage_{};
    std::uint64_t coverage_lists_ = 0u;
    std::array<std::uint8_t, 256> coverage_misses_logged_{};  // per category
    std::vector<std::uint32_t> coverage_slots_;  // the list before's item slots, sorted
    void count_coverage(const core::ListData& now, const core::ListData& before) noexcept;
    core::Pipelines pipelines_;
    NebulaFrameList* producer_list_ = nullptr;
    std::deque<std::unique_ptr<ListEntry>> lists_;
    std::vector<std::unique_ptr<ListEntry>> free_lists_;
    std::uint32_t list_serial_ = 0u;
    std::atomic<std::uint32_t> pipelines_queued_{0u};
    std::uint32_t pipelines_settings_ = ~0u;
    bool pixel_lit_ = false;

    WalkState walk_{};
    std::uint64_t walk_serial_ = 0u;
    // Counts the frame captures and takes that published a target's picture in another image (a new view): a table naming
    // a target, written earlier in the walk, names it again after one.
    std::uint64_t pictures_moved_ = 0u;
    std::vector<Affine34> walk_cameras_;
    std::vector<std::uint32_t> walk_block_rows_, walk_block_world_, walk_previous_;
    std::vector<std::uint32_t> walk_light_rows_;  // per light set: its header row in this walk's rows
    std::vector<float> walk_rows_, walk_joints_, walk_lerped_;
    std::vector<float> walk_skin_;            // a weighted block's joint products (core::ComposeInput::skin)
    std::vector<std::uint8_t> walk_skin_made_;
    std::vector<std::array<float, 6>> walk_view_terms_, walk_view_previous_;
    std::vector<std::array<float, 16>> walk_view_planes_;  // item_culled's four side planes a view: normal, length
    std::vector<WalkView> walk_views_;
    std::vector<core::StaticSlot> walk_constants_;
    std::vector<std::uint8_t> walk_constants_known_;
    std::vector<std::uint32_t> walk_material_at_;  // per entry: its rows' place in walk_lerped_, the list's, or not yet
    std::vector<std::byte> walk_ring_stage_;        // the ring's vertices with positions moved at t, before they are copied out
    std::byte* walk_ring_cpu_ = nullptr;            // this walk's copies of the ring's vertices and indices
    std::uint32_t* walk_index_cpu_ = nullptr;
    std::uint64_t walk_view_stamp_ = 0u;
    gpu::Address walk_rows_ring_ = 0u, walk_vertex_ring_ = 0u, walk_ring_previous_ = 0u;
    const float (*walk_rows_cpu_)[4] = nullptr;
    std::byte* walk_rows_upload_ = nullptr;  // the same rows in the frame's upload memory
    std::unordered_map<std::uint32_t, std::uint64_t> generated_written_;
    float sky_up_[3]{0.0f, 1.0f, 0.0f};
    std::uint32_t view_flags_ = 0u;  // ViewRows::view_flags
    bool target_open_ = false;
    std::uint64_t stat_items_culled_ = 0u, stat_draws_no_pipeline_ = 0u;

    // Render thread: the chunk being rendered ended a game frame (a new picture).
    bool picture_made_in_chunk_ = false;
    std::atomic<bool> frame_end_presents_{true};  // set_frame_end_presents
    bool interp_on_ = false;       // this list is interpolated towards
    // The schedule's view of the settings, the display and the GPU (render_core.cpp).
    class ScheduleView final : public ScheduleDisplay {
    public:
        explicit ScheduleView(RenderCore& core) noexcept : core_(core) {}
        [[nodiscard]] ScheduleSettings settings() const override;
        [[nodiscard]] bool vblank_grid(std::int64_t& vblank, std::int64_t& period) const override;
        [[nodiscard]] double generated_gpu_ms() const override;
        [[nodiscard]] bool temporal_upscaling() const override;
        void suspend_temporal_upscaling(double generated_ms) override;
        void reset_generated_gpu_ms() override;
        [[nodiscard]] bool benchmark() const override;
        [[nodiscard]] bool tracing() const override;

    private:
        RenderCore& core_;
    };
    ScheduleView schedule_view_{*this};
    // When each picture between two game frames is shown (frame_schedule.h).
    FrameSchedule schedule_{schedule_view_, galaxy::qpc_frequency()};
    // The game frame the playing sequence ends on.
    ListEntry* interp_entry_ = nullptr;

    // A ready frame's present arrived inside a render chunk: the render loop
    // finishes the playing sequence, then starts this one.
    bool interp_start_pending_ = false;
    void run_pending_interp_start();
    // A recorded frame whose present arrives as a separate present.
    bool interp_list_waiting_ = false;
    ListEntry* interp_waiting_entry_ = nullptr;
    bool interp_stopping_ = false;        // render thread draining for a stop
    // A native overlay (the HOME Menu, the settings) up while no interpolated frames run, at a present rate above
    // 60 Hz (the interpolation's slots, or none at an unlimited rate): the latest frame presents again at each slot
    // without a present of its own (overlay_idle_due_qpc_: the next; 0 outside).
    [[nodiscard]] bool overlay_idle_presents() const noexcept;
    std::int64_t overlay_idle_due_qpc_ = 0;
    // Presents by the vblanks since the one before (0, 1, 2, 3 or more), against the display's vblank grid, and the
    // range of the time from a present's slot (the moment it shows) to the vblank that shows it.
    std::array<std::uint64_t, 4> interp_vblank_steps_{};
    std::int64_t interp_last_present_vblank_ = 0;
    double interp_slot_to_vblank_min_ms_ = 0.0;
    double interp_slot_to_vblank_max_ms_ = 0.0;
    std::uint64_t interp_slot_to_vblank_samples_ = 0;
    void note_present_vblank(std::int64_t slot);
    void skip_slot_for_depth();
    // The starting frame's present_asked_qpc and step moment (FrameChunk::step_moment_qpc), for start_interp_sequence.
    std::int64_t interp_present_asked_qpc_ = 0;
    std::int64_t interp_step_moment_qpc_ = 0;
    void supersede_interp_sequence();
    void start_interp_sequence();
    // The game thread's time blocked on the render thread (queue space, drains, completions, scene target peeks), in QPC
    // ticks: the generated frames' share of it tells their misses from the game's own.
    std::atomic<std::uint64_t> game_wait_qpc_{0};
    // The first start's resolution calibration (render_core.cpp calibrate_first_start): the GPU's time at two internal
    // heights in the first 3D scene, fitted and turned into the height kept in config.ini.
    struct FirstStartCalibration {
        unsigned stage = 0u;          // 0 off, 1 waiting for a scene with the start's pipelines made, 2 and 3 timing heights[0], [1]
        unsigned heights[2]{};
        unsigned start = 0u;          // the table's height, kept when nothing can be measured or the CPU is the limit
        unsigned frames = 0u;         // frames of the current height
        unsigned width = 0u, lines = 0u;
        bool timing_was_on = false;
        std::vector<double> pixels, ms;
        double cpu_over = 0.0;        // samples with the walk near the frame time or the game late on its own
        std::int64_t list_qpc = 0;
        std::uint64_t list_wait = 0u;
        // The chosen settings as the calibration last saw them. It owns internal_height alone: a change there or to the
        // upscaler ends it with nothing set; a change to any other field is taken in and the heights timed again.
        RenderConfig armed{};
        bool changed = false;         // a field changed since arming: no PERFORMANCE preset at the end, only the height
        bool first_height = true;     // heights[0] is the height set (a restart sets it again before the timing)
        unsigned restarts = 0u;       // timings started again for a changed setting
    };
    FirstStartCalibration calibration_;
    void calibrate_first_start(bool scene, double walk_ms);
    void end_first_start_calibration(unsigned height, const char* why, bool performance = false);
    std::uint64_t interp_presented_ = 0;        // since the last stats report
    std::uint64_t interp_dropped_ = 0;
    std::uint64_t interp_failed_ = 0;     // generated frames that could not be recorded
    double interp_cpu_ms_ = 0.0;          // wall time per generated frame, waits included
    std::uint64_t interp_sequences_ = 0;
    std::uint64_t interp_lists_skipped_ = 0;     // completed logs replaced before playing
    std::uint64_t interp_present_skipped_ = 0;  // presents while a sequence played
    // Game frames presented without generated frames: an overlay, anything else.
    std::array<std::uint64_t, 2> interp_real_only_{};
    bool interp_presenting_real_ = false;
    void note_interp_real_present();
    void report_interp_stats();
    std::int64_t interp_stats_qpc_ = 0;
    // Render thread: steps the interpolation sequence of the latest frame
    // (one generated frame or the real frame when due; `flush` presents the real
    // frame now and drops the remaining generated frames).
    void advance_interp_sequence(bool flush);
    bool render_generated_frame(float t);
    // A walk of `entry` at t: its motion from the picture the temporal history holds (the one walked last), else a new
    // history. `count`: a presented picture, counted in the interpolation stats.
    core::WalkTime interp_walk_time(const ListEntry& entry, float t, bool count);
    // `entry` walked at t (presented or not): the picture the temporal history now holds.
    void note_interp_walked(const ListEntry& entry, float t) noexcept;
    // Render thread: until a present slot's moment (qpc_now() ticks), precisely (precise_wait.h).
    static void interp_wait_until(std::int64_t due_qpc);
    float scene_jitter_px_[2]{};
    // The walked list's scene camera, kept at its scene's end for the passes after it (draw.post's depth, draw.native's
    // matrices): the view (3x4 rows, the game's), its projection terms and viewport; `post_scene_known_` false before a
    // scene in this list.
    bool post_scene_known_ = false;
    SceneView post_scene_view_{};
    std::array<float, 12> post_scene_camera_{};
    bool post_scene_ortho_ = false;
    float post_scene_target_[2]{};  // the scene target's pixels the viewport was placed in
    // draw.native's `picture`: which picture of the list this walk draws (0 the first, interpolated ones after).
    const NebulaFrameList* walk_picture_list_ = nullptr;
    std::uint32_t walk_picture_frame_ = 0u;
    std::uint32_t walk_picture_ = 0u;
    // A present-only chunk's times (FrameChunk::present_asked_qpc, step_moment_qpc) for the pacing recorder's
    // description of the picture it shows again (present_cached_display_on_thread).
    std::int64_t cached_present_asked_qpc_ = 0;
    std::int64_t cached_present_step_qpc_ = 0;
    bool producer_movie_content_{},render_movie_content_{};
    MovieFrameMark producer_movie_frame_{}, render_movie_frame_{};
    // The step clock's moment for the step now running (set_step_moment); 0 none.
    std::atomic<std::int64_t> producer_step_moment_qpc_{0};
    // That moment for a present asked at `asked`, 0 when there is none or it has not come.
    [[nodiscard]] std::int64_t step_moment_for(std::int64_t asked) const noexcept {
        const std::int64_t moment = producer_step_moment_qpc_.load(std::memory_order_relaxed);
        return moment > 0 && moment <= asked ? moment : 0;
    }
    // The game's movies: the cooked movie's decoder (a worker), HD replacements (render thread), the folders last applied.
    MoviePlayback movie_playback_;
    std::uint64_t movie_mods_generation_ = ~0ull;
    galaxy::ClockInterval movie_stats_report_{4'000};  // [movie] every four seconds while one plays
    std::array<std::uint64_t,512> pointer_latency_bins_{};
    std::uint64_t pointer_response_count_{}, pointer_latency_total_{}, pointer_latency_max_{}, pointer_report_ns_{};
    std::array<std::uint64_t,512> pointer_acquisition_age_bins_{};
    std::uint64_t pointer_acquisition_age_count_{}, pointer_acquisition_age_total_{}, pointer_acquisition_age_max_{};
    void record_pointer_response(const DisplayTexture& texture) noexcept;
    void note_picture_shown(const DisplayTexture& texture) noexcept;
    struct MonotonicCadenceAccumulator {
        static constexpr std::size_t kMaxRetainedIntervals = 20'000u;

        // Render-owner only, fixed-size window tails for normal play monitoring.
        std::array<std::uint64_t,512> monitor_bins{};
        std::uint64_t monitor_intervals{},monitor_total_ns{},monitor_max_ns{};
        bool has_last_sample = false;
        std::uint64_t last_sample_ns = 0;
        std::array<std::uint64_t, kMaxRetainedIntervals> interval_samples{};
        std::size_t retained_interval_samples = 0;
        bool interval_samples_overflowed = false;
        std::atomic<std::uint64_t> samples{0};
        std::atomic<std::uint64_t> intervals{0};
        std::atomic<std::uint64_t> total_interval_ns{0};
        std::atomic<std::uint64_t> min_interval_ns{~std::uint64_t{0}};
        std::atomic<std::uint64_t> max_interval_ns{0};
        std::atomic<std::uint64_t> published_first_sample_ns{0};
        std::atomic<std::uint64_t> published_last_sample_ns{0};
    };
    std::uint64_t frame_tail_report_ns_{};
    void report_frame_tail_window() noexcept;
    void trace_memory();
    static void record_monotonic_cadence_sample(
        MonotonicCadenceAccumulator& accumulator) noexcept;
    static void reset_monotonic_cadence(
        MonotonicCadenceAccumulator& accumulator) noexcept;
    TextureStore texture_store_;
    DisplayPictures display_pictures_;
    WindowOwner window_owner_;
    Renderer renderer_;

    // Frame queue: the simulation thread is the producer, the render thread the consumer. The producer may
    // run a small bounded number of frames ahead, then blocks on queue_space_cv_.
    std::thread render_thread_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;        // consumer: work or stop
    std::condition_variable queue_space_cv_;  // producer: slot free
    std::deque<FrameChunk> frame_queue_;
    // When the render thread's current piece of work began (steady clock; 0 between pieces): a frame's list, an
    // interpolation step, a present. While one has run for kRenderStallNs (a burst of first-use textures or pipelines,
    // a mod's draw.native function holding the thread), the producer may queue up to kStalledRenderQueueDepth chunks
    // (render_queue.h queue_place). The game then reaches its own GPU wait, where its audio thread keeps running.
    std::atomic<std::uint64_t> render_work_started_ns_{0};
    void render_work_begins() noexcept;
    void render_work_ends() noexcept;
    static constexpr std::uint64_t kRenderStallNs = 20'000'000u;
    static constexpr std::size_t kStalledRenderQueueDepth = 8u;
    std::size_t render_chunks_in_flight_ = 0;
    std::size_t present_chunks_in_flight_ = 0;

    ao::ObjectCollector ao_objects_;
    bool collect_ao_objects_ = false;
    bool stop_render_thread_ = false;
    // finish_pictures: asked (the render thread's next turn writes them), answered under queue_mutex_.
    std::atomic<bool> pictures_finish_asked_{false};
    void finish_pictures_here();  // the render thread's turn: the renderer's finish_pictures, then the answer
    bool pictures_finished_ = false;
    std::condition_variable pictures_finished_cv_;
    std::exception_ptr render_thread_error_;
    MonotonicCadenceAccumulator picture_made_cadence_{};
    MonotonicCadenceAccumulator new_picture_shown_cadence_{};
    std::atomic<std::uint64_t> last_presented_display_stamp_{
        ~std::uint64_t{0}};
    std::atomic<std::uint64_t> last_presented_picture_{
        ~std::uint64_t{0}};
    std::atomic<std::uint32_t> last_presented_slot_{0};
    // Present rates below the game's 60 Hz show the newest frame on their own cadence. A 30 Hz present
    // shows every other frame at full game speed.
    [[nodiscard]] bool skip_for_present_rate();
    std::int64_t next_rate_present_qpc_ = 0;

    // A temporal scene's texture level shift in 32nds, from its render scale to the output's (0 outside one).
    float scene_mip_bias_ = 0.0f;
    // RenderConfig::curved_surfaces, sampled at each frame start.
    bool curved_refine_ = false;
    // RenderConfig::enhanced_lighting, sampled at each frame start; the last draw's eligibility and
    // material word (enhanced_lighting.h), kept until its keys or eligibility change.
    bool enhanced_lighting_ = false;
    // scene_wide (render_config.h) for this frame, sampled at each frame start: the scene's target and pipelines.
    bool wide_scene_ = false;
    // Texture-pack settings last applied (render config and the mods' folders, polled at frame begin).
    TexturePackSettings texture_pack_settings_{};
    bool texture_pack_settings_applied_ = false;
    // The most a texture pack's replacements may hold on this device (0: no limit known): a share of the memory budget its
    // back end reports, taken at start and after a device change (cap_texture_packs). texture_pack_budget_mb applies below it.
    std::uint64_t texture_pack_cap_bytes_ = 0u;
    void cap_texture_packs();
    std::uint64_t texture_pack_mods_generation_ = ~0ull;
    std::vector<std::filesystem::path> texture_pack_mod_folders_;
    std::vector<std::uint32_t> texture_pack_mod_categories_off_;
    std::uint64_t frame_index_ = 0;
    bool initialized_ = false;
    std::atomic<std::uint64_t> stat_pictures_made_{0};
    std::unique_ptr<galaxy::telemetry::Publisher> frame_telemetry_;  // --diagnostics telemetry
    std::atomic<std::uint64_t> stat_new_pictures_shown_{0};
    std::atomic<std::uint64_t> stat_presents_{0};
    std::atomic<std::uint64_t> stat_pictures_shown_again_{0};

};

}  // namespace galaxy::render
