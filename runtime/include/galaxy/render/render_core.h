#pragma once

// RenderCore: runs the frame behind the galaxy::render API (render_api.h, through render_api.cpp). The game's thread hands over
// one frame list at a time; the render thread walks it once per presented frame (core/draw_walker.cpp): at its own time, and
// between lists at the times the display wants. A full queue blocks the producer. A render-thread failure is rethrown from the
// render_frame() call rather than leaving a frozen display.

#include "galaxy/render/ambient_occlusion_objects.h"
#include "galaxy/render/frame_pass.h"
#include "galaxy/render/movie_playback.h"
#if defined(NEBULA_GPU_VULKAN)
#include "galaxy/render/vk/renderer_vk.h"
#elif defined(NEBULA_GPU_METAL)
#include "galaxy/render/mtl/renderer_mtl.h"
#else
#include "galaxy/render/renderer_d3d12.h"
#endif
#include "galaxy/render/renderer_common.h"
#include "galaxy/render/rt/rt_backend.h"
#include "galaxy/render/rt_types.h"
#include "galaxy/render/texture_store.h"
#include "galaxy/render/frame_interpolation.h"
#include "galaxy/render_api.h"
#include "galaxy/frame_telemetry.h"
#include "render/core/draw_record.h"
#include "render/core/frame_core.h"
#include "render/core/pipelines.h"
#include "render/core/world_lights.h"
#include "render/rt/rt_geometry.h"
#include "render/rt/rt_scene.h"

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

// Replacement and scene LOD adjustments are floating-point mip levels.
[[nodiscard]] SamplerKey make_sampler_key(const TextureSampler& mode, bool generated_mips, std::uint32_t mip_levels,
    float lod_shift = 0.0f, bool arbitrary = false, float mip_lines_log2 = 0.0f, float scene_bias = 0.0f) noexcept;

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
        return {static_cast<std::uint32_t>(pipelines_.created()), pipelines_.pending() + (count > queued ? count - queued : 0u)};
    }
    [[nodiscard]] TextureStore& textures() noexcept { return texture_store_; }
    // The list the next render_frame hands over.
    void attach_frame_list(NebulaFrameList* list) noexcept { producer_list_ = list; }

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
    // Simulation thread: channel `channel`'s latest finished probes, at most `capacity`; how many.
    [[nodiscard]] std::uint32_t screen_probe_results(std::uint32_t channel, ScreenProbe* probes, std::uint32_t capacity);
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
    // Render owner: the probes whose frames have finished become their channels' answers.
    void poll_screen_probes();
    // Presents black, a named picture (`slot`) or the newest.
    void present_cached_display_on_thread(
        bool black,
        bool sleep_if_missing,
        bool count_game_display_present = true,
        std::optional<std::uint32_t> slot = std::nullopt);
    void present_latest_frame_on_thread();
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
    // Ambient occlusion (RenderConfig::ambient_occlusion, without traced lighting), sampled at each frame start.
    bool ambient_occlusion_ = false;
    // The view of a perspective scene draw for ambient occlusion: its projection with the upscaler's jitter, depth
    // range and viewport (the ray tracer's RtView).
    [[nodiscard]] RtView scene_depth_view(const float terms[6], const ScaledViewport& viewport) const;
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
    void walk_frame(ListEntry& entry, const core::WalkTime& time, bool present);
    void flush_waiting_list();
    void scan_list(core::ListData& now);
    void prepare_walk(core::ListData& now, core::ListData* before, const core::WalkTime& time);
    void walk_list(core::ListData& now, core::ListData* before, const core::WalkTime& time);
    void plan_early_targets(core::ListData& now);
    void plan_early_probes(core::ListData& now);
    void plan_late_depth_clears(core::ListData& now);
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
    // A frame constants entry's rows as this walk uploads them: the list's own, or at t.
    [[nodiscard]] const float* walk_material_rows(std::uint32_t index);
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
#if defined(NEBULA_GPU_VULKAN) || defined(NEBULA_GPU_METAL)
    // Vulkan without descriptor indexing: the table's descriptor sets, as many maps as a bound table holds.
    void walk_bound_tables(std::uint32_t id, gpu::Table& textures, gpu::Table& samplers);
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
    [[nodiscard]] bool generated_written(std::uint32_t texture) const noexcept;
    void note_generated_written(std::uint32_t texture);

    DeviceGate device_gate_;
    bool device_lost_ = false;
    std::uint32_t device_losses_ = 0u;
    std::uint64_t table_sweep_at_ = 0u;  // the frame the next texture table sweep and (test builds) its log line are due
    std::uint64_t table_log_at_ = 0u;
    std::uint64_t vram_log_at_ = 0u;  // the frame the next look at whether a video memory line is due
    std::uint64_t vram_report_at_ = 0u;  // the frame the line is due whatever changed
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
    core::WorldLightStats world_light_stats_;  // --diagnostics rt-inputs
    // Ray tracing (render/rt): the device's builder and the structures of the meshes one matrix places. Started with the
    // device when the setting is on and the device can trace; render thread.
    rt::Backend rt_backend_;
    rt::Geometry rt_geometry_{rt_backend_};
    std::uint64_t rt_log_at_ = 0u;
    bool rt_building_ = false;  // meshes wait for their structures
    void rt_start();
    void rt_stop() noexcept;
    void rt_want(std::uint32_t id, const core::GpuMesh& mesh);
    void rt_want_form(std::uint32_t id, const core::GpuMesh& mesh, bool refined);
    void rt_frame(const NebulaFrameList& list);
    // The scene a frame traces: its instances from the list's scene passes, at the walk's time.
    rt::Scene rt_scene_{rt_backend_};
    rt::SceneCensus rt_census_;  // --diagnostics rt-inputs
    struct RtPlace {
        std::uint64_t mesh = 0u;
        std::uint32_t block = 0u, matrix = 0u;
        [[nodiscard]] bool operator==(const RtPlace&) const noexcept = default;
    };
    struct RtPlaceHash {
        [[nodiscard]] std::size_t operator()(const RtPlace& place) const noexcept {
            return static_cast<std::size_t>((place.mesh * 0x9E3779B97F4A7C15ull) ^ (std::uint64_t{place.block} << 32u | place.matrix));
        }
    };
    std::unordered_map<RtPlace, std::uint32_t, RtPlaceHash> rt_seen_;  // a place's instance in this frame's scene
    void rt_scene(const NebulaFramePass& begin);
    // A draw whose vertices carry their own matrices: skinned by a pass into the tracer's vertex buffer each frame.
    struct RtSkin {
        std::uint64_t key = 0u;
        gpu::Address positions = 0u, matrices = 0u, rows = 0u;
        std::uint32_t vertices = 0u, rows_bytes = 0u;
        std::uint32_t item = 0u, mask = 0u;
        rt::Faces faces = rt::Faces::Both;
        std::uint32_t kind = 0u;
        std::uint64_t out = 0u;  // its vertices' byte in the buffer
        float root[3][4]{};      // the eye's space from the root's: its instance's matrix
        float root_inverse[3][4]{};
        rt::TriangleInput input{};
    };
    std::vector<RtSkin> rt_skins_;
    std::uint32_t rt_skin_buffer_ = rt::kNoBuffer;
    std::uint64_t rt_skin_bytes_ = 0u;
    std::vector<std::pair<std::uint64_t, std::uint32_t>> rt_skin_retired_;  // frame, buffer
    void rt_skin();
    // The frame's ring draws (vertices made this frame), by what they are to a ray: their triangles in the eye's space,
    // one structure a kind, built each frame.
    struct RtRing {
        std::vector<float> positions;  // three a vertex, three vertices a triangle
        std::uint32_t item = 0u;
        bool regenerated = false;      // a draw whose vertices are not last frame's moved
    };
    RtRing rt_rings_[3];
    // The tracer's views (--diagnostics rt-view, rt-check): the scene drawn by rays over the game's opaque scene.
    void rt_view(const NebulaFramePass& pass);
    std::uint32_t rt_view_image_ = rt::kNoImage;
    unsigned rt_view_width_ = 0u, rt_view_height_ = 0u;
    rt::ImageFormat rt_view_format_ = rt::ImageFormat::Rgb10a2;
    std::vector<std::pair<std::uint64_t, std::uint32_t>> rt_view_retired_;  // frame, image
    std::uint64_t rt_check_sum_[8]{};
    std::uint32_t rt_check_frames_ = 0u;
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
    std::vector<Affine34> walk_cameras_;
    std::vector<std::uint32_t> walk_block_rows_, walk_block_world_, walk_previous_;
    std::vector<std::uint32_t> walk_light_rows_;  // per light set: its header row in this walk's rows
    std::vector<float> walk_rows_, walk_joints_, walk_lerped_;
    std::vector<std::array<float, 6>> walk_view_terms_, walk_view_previous_;
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
    std::unordered_map<std::uint32_t, std::uint64_t> generated_written_;
    float sky_up_[3]{0.0f, 1.0f, 0.0f};
    std::uint32_t view_flags_ = 0u;  // ViewRows::view_flags
    bool target_open_ = false;
    std::uint64_t stat_items_culled_ = 0u, stat_draws_no_pipeline_ = 0u;

    // Render thread: the chunk being rendered ended a game frame (a new picture).
    bool picture_made_in_chunk_ = false;
    std::atomic<bool> frame_end_presents_{true};  // set_frame_end_presents
    unsigned interp_frames_ = 0;          // interpolated frames per game frame
    bool interp_on_ = false;       // this list is interpolated towards
    struct InterpSequence {
        bool active = false;
        std::uint32_t picture_slot = 0;  // the game frame it ends on
        unsigned generated = 0;
        unsigned next = 0;
        std::int64_t frame_qpc = 0;       // the frame's arrival (its present)
        double lead = 0.0;                // t of a slot at the arrival (see start)
        // Set inside a render chunk (no frame may be recorded there): the
        // render loop presents the real frame before anything else.
        bool flush_requested = false;
        ListEntry* entry = nullptr;
    };
    const ListEntry* interp_shown_list_ = nullptr;
    std::uint64_t interp_shown_serial_ = 0;
    float interp_shown_t_ = 0.0f;

    // A ready frame's present arrived inside a render chunk: the render loop
    // finishes the playing sequence, then starts this one.
    bool interp_start_pending_ = false;
    std::uint64_t interp_sequence_serial_ = 0;
    void run_pending_interp_start();
    InterpSequence interp_sequence_{};
    // A recorded frame whose present arrives as a separate present.
    bool interp_list_waiting_ = false;
    ListEntry* interp_waiting_entry_ = nullptr;
    bool interp_stopping_ = false;        // render thread draining for a stop
    // Present slot clock: next slot and slot length (with VSync, on the display's vblanks when they are timed).
    std::int64_t interp_slot_due_qpc_ = 0;
    std::int64_t interp_slot_period_qpc_ = 0;
    // A native overlay (the HOME Menu, the settings) up while no interpolated frames run, at a display faster than
    // 60 Hz (the interpolation's slots): the latest frame presents again at each slot without a present of its own
    // (overlay_idle_due_qpc_: the next; 0 outside).
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
    // Smoothed game-frame arrival clock (start_interp_sequence).
    std::int64_t interp_arrival_qpc_ = 0;
    std::int64_t interp_frame_period_qpc_ = 0;
    std::uint64_t interp_late_slots_ = 0;   // slots a stall skipped
    std::uint64_t interp_superseded_ = 0;   // frames replaced before their own slot
    void supersede_interp_sequence();
    void start_interp_sequence();
    // Generated frames per game frame within the GPU budget.
    unsigned interp_gpu_governor(double& rate);
    unsigned interp_gpu_cap_ = 7u;
    std::uint64_t interp_gpu_cap_serial_ = 0;
    std::uint64_t interp_gpu_checked_ = 0;
    // After a rate or upscaler change, the time (QPC) before which GPU times are not used (pipeline,
    // upscaler and target creation).
    std::int64_t interp_gpu_warm_until_qpc_ = 0;
    // A new generated frame configuration (interp_frames_ changed): both governors start over.
    void restart_interp_governors();
    std::uint64_t interp_gpu_capped_ = 0;   // sequences with fewer generated frames
    // Game-frame cadence governor (note_game_frame_cadence).
    void note_game_frame_cadence(std::int64_t now_qpc);
    // The present rate a generated frame count gives, and the counts of the next lower and higher rates.
    [[nodiscard]] double interp_rate_with(unsigned generated) const;
    [[nodiscard]] unsigned interp_step_down(unsigned generated) const;
    [[nodiscard]] unsigned interp_step_up(unsigned generated) const;
    unsigned interp_cadence_cap_ = 7u;
    unsigned interp_cadence_interval_s_ = 2u;
    std::int64_t interp_cadence_window_qpc_ = 0;
    std::int64_t interp_cadence_last_qpc_ = 0;
    std::uint32_t interp_cadence_misses_ = 0;   // in the window: the generated frames'
    std::uint32_t interp_cadence_own_ = 0;      // in the window: the game's own
    std::int64_t interp_cadence_change_qpc_ = 0;
    std::int64_t interp_cadence_try_qpc_ = 0;
    std::uint32_t interp_cadence_frames_ = 0;
    std::int64_t interp_cadence_grace_until_qpc_ = 0;   // misses after a change are not counted
    std::uint64_t interp_cadence_drops_ = 0;
    double interp_cadence_fps_ = 0.0;
    double interp_cadence_own_misses_ = 0.0;   // the game's own misses per second: their lowest recent rate
    // The game thread's time blocked on the render thread (queue space, drains, completions, scene target peeks), in QPC
    // ticks: the generated frames' share of it tells their misses from the game's own.
    std::atomic<std::uint64_t> game_wait_qpc_{0};
    std::uint64_t interp_cadence_wait_seen_ = 0;
    std::uint64_t interp_cadence_wait_qpc_ = 0;   // in the window
    // The render thread's time rendering generated frames (advance_interp_sequence), in QPC ticks.
    std::uint64_t interp_generated_qpc_ = 0;
    std::uint64_t interp_cadence_generated_seen_ = 0;
    // A probe of the game's own misses: seconds of them in a row above the lowest rate, the cap a probe lowered and
    // their rate before it, and the earliest next probe.
    unsigned interp_cadence_own_windows_ = 0;
    bool interp_cadence_probe_ = false;
    unsigned interp_cadence_probe_restore_ = 0;
    double interp_cadence_probe_misses_ = 0.0;
    std::int64_t interp_cadence_probe_after_qpc_ = 0;
    std::uint64_t interp_presented_ = 0;
    std::uint64_t interp_dropped_ = 0;
    std::uint64_t interp_failed_ = 0;     // generated frames that could not be recorded
    std::uint64_t interp_history_resets_ = 0;  // presented frames that start the temporal upscaler's history again
    std::uint64_t interp_history_carried_ = 0; // first generated frames whose frame before ended the pair before
    double interp_cpu_ms_ = 0.0;          // wall time per generated frame, waits included
    std::uint64_t interp_sequences_ = 0;
    std::uint64_t interp_started_ = 0;
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
    [[nodiscard]] std::int64_t interp_remaining_us() const;  // to the next slot
    [[nodiscard]] double interp_slot_t() const;
    static void interp_sleep_us(std::int64_t us);
    float scene_jitter_px_[2]{};
    bool producer_movie_content_{},render_movie_content_{};
    MovieFrameMark producer_movie_frame_{}, render_movie_frame_{};
    // The game's movies: the cooked movie's decoder (a worker), HD replacements (render thread), the folders last applied.
    MoviePlayback movie_playback_;
    std::uint64_t movie_mods_generation_ = ~0ull;
    std::uint64_t movie_stats_tick_ = 0;
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
    // Steady-clock time the render thread has spent on its current chunk (0 between chunks), written
    // under queue_mutex_. While one chunk has run for kRenderStallNs (a burst of first-use textures or
    // pipelines), the producer may queue up to kStalledRenderQueueDepth chunks. The game then reaches
    // its own GPU wait, where its audio thread keeps running, instead of blocking here.
    std::uint64_t render_chunk_started_ns_ = 0;
    static constexpr std::uint64_t kRenderStallNs = 20'000'000u;
    static constexpr std::size_t kStalledRenderQueueDepth = 8u;
    // Probes in flight, oldest first; the answers of each channel. Only the answers are shared with the simulation thread.
    struct ScreenProbeFlight {
        std::uint32_t channel = 0, count = 0;
        std::size_t slot = 0;
        std::vector<ScreenProbe> probes;
    };
    std::deque<ScreenProbeFlight> probe_flights_;
    std::vector<std::size_t> free_probe_slots_;
    std::size_t next_probe_slot_ = 0;
    // `compared`: NEBULA_PROBE_COMPARED, the game only compares each value with its user_z.
    void record_screen_probes(std::uint32_t channel, const ScreenProbe* probes, std::uint32_t count, bool compared);
    struct ScreenProbeAnswers {
        std::uint32_t count = 0;
        std::vector<ScreenProbe> probes;
    };
    mutable std::mutex probe_answers_lock_;
    std::unordered_map<std::uint32_t, ScreenProbeAnswers> probe_answers_;
    std::size_t render_chunks_in_flight_ = 0;
    std::size_t present_chunks_in_flight_ = 0;

    ao::ObjectCollector ao_objects_;
    bool collect_ao_objects_ = false;
    bool stop_render_thread_ = false;
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
    // Texture-pack settings last applied (render config and the mods' folders, polled at frame begin).
    TexturePackSettings texture_pack_settings_{};
    bool texture_pack_settings_applied_ = false;
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
