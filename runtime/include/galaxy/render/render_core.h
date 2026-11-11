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
    double rt_build_rate_ = 0.0;      // triangles a GPU millisecond of the timed builds (5.3 G-3; 0: none timed yet)
    std::uint64_t rt_budget_ = 0u;    // this frame's build budget, in triangles
    void rt_start();
    void rt_stop() noexcept;
    // The setting changed while the game runs (plan 5.3 G-6): the tracer starts or stops at the start of a walk.
    void rt_follow_setting(const RenderConfig& render);
    int rt_setting_ = -1;  // what the tracer last followed (-1: the device's start)
    std::uint32_t rt_image_failures_ = 0u;  // walks running whose light images could not be made
    bool rt_gave_up_ = false;               // stopped for want of memory, until the setting is turned on again
    std::uint32_t rt_toggle_walks_ = 0u;
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
    // 5.3 G-2: the alpha-tested meshes that stand still and asked this frame for an opacity micromap (each mesh once),
    // made after the frame's tables by a pass over the item that asked (rt_micromaps).
    struct RtMicromapWant {
        std::uint64_t key = 0u;
        std::uint32_t item = 0u, tag = 0u, triangles = 0u;
    };
    std::vector<RtMicromapWant> rt_micromap_wants_;
    std::unordered_set<std::uint64_t> rt_micromap_asked_;
    [[nodiscard]] rt::Micromapped rt_micromap_for(std::uint64_t key, const NebulaFrameItem& item, std::uint32_t i, std::uint32_t kind,
                                                  std::uint32_t mask, std::uint32_t triangles);
    void rt_micromaps();
    void rt_scene(const NebulaFramePass& begin);
    // The walk's one traced scene: its begin pass (rt_pick_scene), NEBULA_NONE32 when none; a scene pass not picked
    // draws untraced (rt_untraced).
    [[nodiscard]] std::uint32_t rt_pick_scene() const;
    void rt_untraced();
    std::uint32_t rt_traced_begin_ = NEBULA_NONE32;
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
    RtRing rt_rings_[4];  // early, opaque, blended; and the game's code-drawn water (c10)
    std::uint32_t rt_water_ring_item_ = ~0u;  // the water ring's first item this frame: its words are water's
    // The game's drop shadows, traced in place of their stencil volumes (the light pass, rt_light.hlsl). A drop is
    // traced when the scene holds its caster's draws; its volume's draws are then left out of the walk.
    struct RtCaster {
        float low[3], high[3];  // the eye-space box of a slot's instances
    };
    struct RtDrop {
        float base[3], radius;
        float direction[3], length;
        std::uint32_t caster, reserved[3];
    };
    std::unordered_map<std::uint32_t, RtCaster> rt_casters_;
    std::vector<RtDrop> rt_drops_;
    std::unordered_set<std::uint32_t> rt_drop_volumes_;  // the slots of the volumes' draws left out this walk
    std::uint32_t rt_scene_pass_ = NEBULA_NONE32;        // this walk's scene begin pass, once its scene is built
    std::uint32_t rt_scene_view_ = NEBULA_NONE32;        // and the scene's view (the one its opaque end names)
    std::vector<std::uint32_t> rt_tiles_, rt_tile_entries_;
    std::vector<std::uint32_t> rt_tile_counts_;
    std::uint32_t rt_tiles_across_ = 0u, rt_tiles_down_ = 0u;
    void rt_caster(std::uint32_t slot, const float centre[3], float radius);
    void rt_drops_collect(const NebulaFramePass& begin);
    // The drops by the 16-pixel screen tiles their reach covers: rt_tiles_ (two words a tile: its first entry, how
    // many) and rt_tile_entries_ (a drop each).
    void rt_drop_bins(unsigned width, unsigned height, const float ray[4]);
    // The stage's shadow sun (RT-MASTER-PLAN 7.1): one direction every sun shadow of the scene comes from, in the
    // scene camera's eye space; false: the scene has none. The stage's row (rt_stages.tsv `sun`) decides; by default
    // the visible sun where the stage draws one, else its key light, none where the key is off (under 0.05). A key
    // the game moves with the camera is latched in the world when the zone's light changes (and at a cut) and held,
    // so shadows do not swing as the player looks around; a change of source or zone eases over 30 frames. It stands
    // at least 30 degrees above the player's horizon (decision D2), the player's up easing where it turns faster than
    // a degree a frame (a jump between planets).
    [[nodiscard]] bool rt_sun(float eye_direction[3]);
    // The lit programs' key light from the sun's direction: light 0 of every light set an area lights (in the scene
    // camera's space) is placed far along it, in this walk's rows, before the scene's first draw. Lit faces and the
    // sun's shadows then agree.
    void rt_sun_keys(const float eye_direction[3]);
    float rt_sun_eye_[3]{};
    bool rt_sun_now_ = false;  // this walk's scene has its sun
    float rt_sun_world_[3]{};
    bool rt_sun_known_ = false;
    float rt_sun_from_[3]{};            // an ease's start, in the world
    float rt_sun_to_[3]{};              // the direction it eases to (a latched key's, held)
    std::uint32_t rt_sun_ease_ = 0u;    // frames of the ease left
    std::uint64_t rt_sun_light_ = 0u;   // the key latched (its world light's id), 0: none
    std::uint8_t rt_sun_source_ = 0u;   // 0 none, 1 the visible sun, 2 the key, 3 the row's direction, 4 overhead
    float rt_sun_up_[3]{};              // the player's up as the lift uses it, in the world
    bool rt_sun_up_known_ = false;
    std::uint32_t rt_sun_frame_ = 0u;   // the game frame the sun's eases last stepped on (l57)
    [[nodiscard]] std::uint32_t rt_scene_image(rt::ImageFormat format, unsigned width, unsigned height);
    // The traced light. Before the scene's draws, its opaque surfaces go into depth and the item target (the prepass:
    // every scene item that core::prepass_draws takes, under the scene's camera); the frame's passes give each pixel
    // what light reaches it (rt_light: the surfaces, the sun, the local lights, the shares the programs read, the
    // filters); the scene pass then draws those items over their own depth, the lit programs in their traced form.
    void rt_prepass(const NebulaFramePass& begin);
    void rt_light();
    void rt_light_end() noexcept;
    void rt_reactive();  // the reactive mask where the added light changed (plan 16.4)
    bool rt_physical_ = false;   // the Physical style (plan 14.6, view_flags bit 64)
    bool rt_lamps_own_ = false;  // local lights off (plan 19.1): the programs light by their own lamp (view_flags bit 32)
    // The second layer, at the opaque phases' end: the scene's blended surfaces (water, glass, crystal) drawn into the
    // item target and the light pass run over them, into the picture the opaque draws are done reading.
    void rt_blended(const NebulaFramePass& end);
    [[nodiscard]] bool rt_prepass_draws(const NebulaFrameItem& item, std::uint32_t state) const;
    void walk_draw_pass_direct(const NebulaFramePass& pass);
    std::vector<std::uint8_t> rt_prepassed_;  // an item of the list: 1 drawn in this walk's prepass, 2 in its second
    bool rt_prepass_walk_ = false;            // walk_draw_items draws the prepass
    bool rt_blended_walk_ = false;            // and it is the second layer's
    // The frame's traced light as rt_tables set it up, for every pass and both layers: the bindings every pass shares
    // (the scene, the frame's buffers and mesh memory, the depth and the item map, the frame's constants at b0), the
    // picture's size and groups (across 0: no light this walk), the eye's terms, the frame's turn (the noise's, and
    // which of a pair of images is this frame's), the layer, the layer's b1, and whether a frame before is read.
    struct RtFrame {
        rt::PassBinds binds{};
        unsigned width = 0u, height = 0u;
        std::uint32_t across = 0u, down = 0u;
        float ray[4]{}, depth[4]{}, viewport[4]{};
        std::uint32_t turn = 0u;
        std::uint32_t layer = 0u;  // 0 the opaque surfaces; 1 the nearest blended surface a pixel (rt_blended)
        rt::BufferView pass{};
        bool history = false;
    };
    RtFrame rt_light_frame_{};
    // The passes of the frame, in their order (rt_walk.cpp): its tables and constants; the surfaces; the sun; the local
    // lights; the sky and bounce light; the shares the programs read with what the surfaces mirror; the reflections;
    // the light volume; then the filters, and the report (--diagnostics rt-inputs).
    void rt_tables(const Affine34& world_from_now);
    [[nodiscard]] rt::BufferView rt_pass_constants(std::uint32_t layer, std::uint32_t step, std::uint32_t last, std::uint32_t free = 0u);
    // The blue noise into its image once it is made (rt_sample.hlsli), and the sun's filter (plan 7.3).
    void rt_noise_fill();
    void rt_sun_filter();
    bool rt_noise_filled_ = false;
    [[nodiscard]] rt::PassBinds rt_pass_binds() const;
    [[nodiscard]] std::uint32_t rt_layer_guide() const noexcept;
    void rt_pass_surface();
    void rt_pass_sun();
    void rt_pass_lights();
    void rt_pass_light();
    void rt_filters();
    void rt_report();
    [[nodiscard]] bool rt_measure(unsigned width);
    // The passes of their own files (rt_pass_indirect.cpp, rt_pass_reflect.cpp, rt_pass_volume.cpp): whether each runs
    // this frame (its images are made while it does), and the pass.
    [[nodiscard]] bool rt_pass_indirect_on() const;
    void rt_pass_indirect();
    [[nodiscard]] bool rt_pass_reflect_on() const;
    void rt_pass_reflect();
    // What is seen through glass and water, bent (rt_pass_transmit.cpp; plan 13.2/13.3 High): picture 6.
    [[nodiscard]] bool rt_pass_transmit_on() const;
    void rt_pass_transmit();
    // T is wanted (its image kept): High, not under rt-added or rt-motion; under the reference its own (FB, 18).
    [[nodiscard]] bool rt_transmit_wanted() const;
    void rt_pass_transmit_reference();
    [[nodiscard]] bool rt_pass_volume_on() const;
    void rt_pass_volume();
    bool rt_light_on_ = false;                // this walk's scene has its traced light: its views carry it
    std::uint32_t rt_light_slot_ = 0u;        // the light image's place among the textures
    std::uint32_t rt_scene_camera_ = NEBULA_NONE32;
    // The traced light's images (rt_walk.cpp kRtImageKinds: name, format, size, the passes that need one). A pair A, B
    // is this frame's and the frame before's by turns (rt_now, rt_before). The surface images (rt_common.hlsli):
    // Guide, Normal, Motion, Material, Base. Light: the sun's and the sky's shares, then the filter's last picture,
    // the one the programs read; Added: the local lights' (what glows near each pixel), the same way; Mirror: what
    // each pixel mirrors.
    enum class RtImage : std::uint8_t {
        GuideA, GuideB, Normal, Motion, Material, Base, SunRaw, Light, LightStep, LightAverageA, LightAverageB, Lights,
        LightsAverageA, LightsAverageB, LightsMomentsA, LightsMomentsB, Added, ReservoirsA, ReservoirsB, Mirror,
        BlueNoise, SunTiles, SunAverageA, SunAverageB, SunDistanceA, SunDistanceB, SunBlur, SunOut, LightsSpec,
        BlendGuideA, BlendGuideB, BlendReservoirsA, BlendReservoirsB, BlendSunA, BlendSunB, BlendDistanceA, BlendDistanceB,
        BlendLightsA, BlendLightsB, BlendMomentsA, BlendMomentsB, Facing, BlendBody,
        IndirectRaw, IndirectAverageA, IndirectAverageB, IndirectMomentsA, IndirectMomentsB, Indirect, GiReservoirsA,
        GiReservoirsB, GiWeightsA, GiWeightsB, IndirectGuideA, IndirectGuideB, IndirectMotion, IndirectStep, ReflectRaw, ReflectAverageA, ReflectAverageB, Reflect, Transmit, VolumeA,
        VolumeB, Scatter, ReflectMoved, RrDiffuse, RrSpecular, RrNormalRoughness, RrHit, Count
    };
    // The passes an image is made for.
    static constexpr std::uint32_t kRtPassLight = 1u, kRtPassIndirect = 2u, kRtPassReflect = 4u, kRtPassVolume = 8u,
                                   kRtPassTransmit = 16u, kRtPassBlend = 32u, kRtPassGuides = 64u;
    // The second layer's history images are made once a frame has had blended things to trace (rt_blended).
    std::uint32_t rt_light_wanted_ = 0u;
    bool rt_blend_seen_ = false;
    std::uint64_t rt_blend_walk_before_ = 0u;
    [[nodiscard]] rt::BufferView rt_layer_constants(std::uint32_t step, std::uint32_t last);
    void rt_blend_filters();
    static constexpr std::size_t kRtImages = static_cast<std::size_t>(RtImage::Count);
    [[nodiscard]] static constexpr std::array<std::uint32_t, kRtImages> rt_no_images() noexcept {
        std::array<std::uint32_t, kRtImages> none{};
        for (std::uint32_t& image : none) {
            image = rt::kNoImage;
        }
        return none;
    }
    std::array<std::uint32_t, kRtImages> rt_images_ = rt_no_images();
    [[nodiscard]] bool rt_images(unsigned width, unsigned height, std::uint32_t wanted);
    [[nodiscard]] std::uint32_t rt_image(RtImage image) const noexcept { return rt_images_[static_cast<std::size_t>(image)]; }
    [[nodiscard]] std::uint32_t rt_now(RtImage a) const noexcept {
        return rt_images_[static_cast<std::size_t>(a) + (rt_light_frame_.turn & 1u)];
    }
    [[nodiscard]] std::uint32_t rt_before(RtImage a) const noexcept {
        return rt_images_[static_cast<std::size_t>(a) + 1u - (rt_light_frame_.turn & 1u)];
    }
    std::uint32_t rt_mirror_slot_ = 0u;  // the mirror picture's place among the textures
    std::uint32_t rt_added_slot_ = 0u;  // the added light's place among the textures
    std::uint32_t rt_facing_slot_ = 0u;  // the facing picture's (how a painted surface faces the sun)
    // The stage's ambient (A) and key (K) colours the programs' painted composite takes (ViewRows::stage_a/k,
    // RT-MASTER-PLAN 3.4); rt_stage_a_[3] is kShadowTint.
    float rt_stage_a_[4]{}, rt_stage_k_[4]{};
    float rt_stage_air_ = 0.0f;  // the stage row's air (rt_stages.tsv), per unit; 0: none (rt_air_density)
    // What glows in the scene: a draw of a mesh whose material glows (nebula_model.h NebulaModelHit.glow, from the
    // cooker's table of glowing models and from surfaces added to what is behind them). One a block; the light pass
    // adds each one's light to the surfaces within its reach, less what stands between.
    // A light source of the scene. Collected with its intensity (colour times candela; 100 units a metre) and its own
    // size; rt_emitters_finish turns that into what the light pass reads, by the stage's exposure.
    struct RtEmitter {
        float centre[3], radius;  // in the scene camera's eye space; the source's own radius
        float colour[3], reach;   // collected: colour x candela; finished: the share of a surface's colour at the source. Where its light ends
        std::uint32_t slot, kind, reserved[2];  // kind: 0 a point light the game asked, 1 a glowing mesh, 2 a particle;
                                                // reserved[1]: what it is from one frame to the next (rt_emitter_id)
    };
    bool rt_emitters_finished_ = false;
    float rt_exposure_ = 150.0f;  // the stage's exposure the emitters were finished by (lux)
    void rt_emitters_finish(float exposure_lux);
    std::vector<RtEmitter> rt_emitters_;
    // For the reservoirs: the frame before's emitters by what they are, where each is in this frame's list, and this
    // frame's list by what they are; the frame before's emitters' centres and reaches (4 floats each, as ids_before).
    std::vector<std::uint32_t> rt_emitter_ids_before_, rt_emitter_remap_;
    std::vector<float> rt_emitter_places_before_;
    std::unordered_map<std::uint32_t, std::uint32_t> rt_emitter_now_;
    std::unordered_map<std::uint32_t, std::uint32_t> rt_emitter_blocks_;
    // A byte an item of this walk's list: 1 for a glowing mesh too large to be one light (over 6 m in two axes, L-3):
    // the item table flags it (word 1, kItemEmissiveArea) and the indirect pass lights by its emission.
    std::vector<std::uint8_t> rt_emissive_areas_;
    // --diagnostics rt-inputs: the lights' sources over a report's frames (kind << 32 | name hash: 1 a glowing mesh's
    // material, 2 a particle light's effect), and how many lights each gave (c11, the emitter census a stop).
    std::unordered_map<std::uint64_t, std::uint32_t> rt_emitter_sources_;
    std::vector<std::uint32_t> rt_glow_tiles_, rt_glow_entries_, rt_glow_counts_;
    void rt_emitter(const NebulaFrameItem& item, const core::GpuMesh& mesh, const float centre[3], float radius,
                    const float (*place)[4] = nullptr);
    // A particle draw as a light: every effect that is added to the picture and is a light by its kind
    // (render/rt/rt_effect_lights.inc, from the census of the game's effects), with the particles' own place, size and
    // colour this frame. `state`: its pipeline's; `camera`: the scene's.
    void rt_particle_emitter(const NebulaFrameItem& item, std::uint32_t state, std::uint32_t camera);
    void rt_emitter_bins(unsigned width, unsigned height, const float ray[4]);
    unsigned rt_light_width_ = 0u, rt_light_height_ = 0u;
    // The frame before, for the filter: its camera, its eye rays, the walk it was (history counts from the walk just
    // before, with no cut between).
    Affine34 rt_light_view_before_{};
    float rt_light_ray_before_[4]{};
    std::uint64_t rt_light_walk_before_ = 0u;
    std::int64_t rt_light_ticks_before_ = 0;  // when the light pass ran last (qpc)
    float rt_frame_rate_ = 60.0f;             // the rate it runs at, smoothed: the filters' histories follow it
    // Plan 19.2 (rt_measure): settled frames in a row, the sun pass's times taken, done (measured, or not to be).
    static constexpr unsigned kRtMeasureFrames = 31u;
    unsigned rt_measure_settled_ = 0u;
    unsigned rt_measure_count_ = 0u;
    float rt_measure_ms_[kRtMeasureFrames]{};
    bool rt_measure_done_ = false;
    bool rt_light_history_ = false;
    std::vector<std::pair<std::uint64_t, std::uint32_t>> rt_light_retired_;  // frame, image
    std::vector<std::uint32_t> rt_item_words_;
    std::vector<float> rt_set_words_;
    std::uint64_t rt_light_sum_[10]{};  // [6] cutout tests, [7] cutouts the pass could not read, [8] mirror rays,
                                        // [9] refracted pixels
    std::uint32_t rt_light_arenas_ = 0u, rt_light_unplaced_ = 0u;  // this walk: mesh arenas bound, items left without
    std::uint32_t rt_light_skinned_ = 0u;  // this walk: mesh items whose vertices name their own matrices (no mesh words)
    std::uint32_t rt_light_frames_ = 0u;
    std::uint32_t rt_light_number_ = 0u;      // the noise's turn: counts the light passes
    // The tracer's views (--diagnostics rt-view, rt-check): the scene drawn by rays over the game's opaque scene.
    void rt_view(const NebulaFramePass& pass);
    std::uint32_t rt_view_image_ = rt::kNoImage;
    unsigned rt_view_width_ = 0u, rt_view_height_ = 0u;
    rt::ImageFormat rt_view_format_ = rt::ImageFormat::Rgb10a2;
    // The light that travels (rt_bounce.cpp, rt_pass_indirect.cpp): hit shading's frame (rt_shade.hlsli
    // RtShadeFrame, field for field), the world light grid, the reference path tracer (--diagnostics rt-reference)
    // and the signals as pictures (rt-signals).
    struct RtShadeFrame {
        float ambient[4], key[4], fill[4], sky[4], gains[4], axis[3][4], look[4], authored[4];
        std::uint32_t grid[4], spare[5][4];
    };
    void rt_shade_frame(RtShadeFrame& frame) const;
    void rt_world_grid();
    [[nodiscard]] bool rt_shade_binds(rt::PassBinds& binds, std::uint32_t& at);
    [[nodiscard]] rt::BufferView rt_bounce_constants(std::uint32_t y, std::uint32_t z, std::uint32_t w, const float (&values)[3][4]);
    [[nodiscard]] std::uint32_t rt_bounce_image(std::uint32_t& image, rt::ImageFormat format, unsigned width, unsigned height,
                                                const char* name);
    bool rt_dome_read();
    void rt_pass_reference();
    void rt_reference_publish();
    void rt_signals();
    [[nodiscard]] bool rt_holds() const noexcept;
    void rt_hold(ListEntry& entry, const core::WalkTime& time, const std::string& picture);
    void rt_bounce_stop() noexcept;
    unsigned rt_quality_ = 1u;  // render.rt_quality: 0 medium, 1 high, 2 low (3 reference, 4 custom: FA's l32)
    // Each part's level, in FA's RtPart order (sun, lights, indirect, reflect, transmit, volume): 0 off, 1 low,
    // 2 medium, 3 high (begin_walk_frame).
    std::uint8_t rt_levels_[6] = {2u, 2u, 2u, 2u, 2u, 2u};
    static constexpr unsigned kFbIndirect = 2u, kFbReflect = 3u, kFbVolume = 5u;
    // The reference draws the picture: --diagnostics rt-reference, or the preset (3).
    [[nodiscard]] bool rt_fb_reference() const noexcept;
    // The shading frame and grid as uploaded for the frame of turn rt_shade_turn_ - 1 (0: none yet).
    std::uint32_t rt_shade_turn_ = 0u, rt_shade_at_ = 0u;
    rt::BufferView rt_shade_view_{};
    std::vector<std::uint32_t> rt_grid_words_;
    struct RtGridRange {
        int low[3], high[3];
    };
    std::vector<RtGridRange> rt_grid_ranges_;
    std::uint32_t rt_grid_entries_ = 0u;
    float rt_grid_axis_[3][4]{};
    unsigned rt_bounce_width_ = 0u, rt_bounce_height_ = 0u;
    std::vector<std::pair<std::uint64_t, std::uint32_t>> rt_bounce_retired_;  // frame, image
    float rt_dome_mean_ = 0.0f;  // the sky dome's mean luminance as the bounce rays see it (11.1); 0 not yet known
    // The reference's sums, V D I R this walk's and the walk before's by turns (rt_ref_now_: this walk's first), and
    // what starts them again.
    std::uint32_t rt_ref_images_[8] = {rt::kNoImage, rt::kNoImage, rt::kNoImage, rt::kNoImage,
                                       rt::kNoImage, rt::kNoImage, rt::kNoImage, rt::kNoImage};
    unsigned rt_ref_now_ = 0u;
    bool rt_ref_ready_ = false;  // this walk's reference is traced: rt_reference_publish puts it out
    std::uint32_t rt_ref_serial_ = 0u;
    const NebulaFrameList* rt_ref_list_ = nullptr;
    Affine34 rt_ref_camera_{};
    float rt_ref_time_ = 0.0f;
    bool rt_ref_restart_ = false;  // this walk's reference started its sums again (its T follows: FB, 18)
    // The reference's T sums by turns (rt_pass_transmit_reference), and the size they were made at.
    std::uint32_t rt_ref_t_images_[2] = {rt::kNoImage, rt::kNoImage};
    unsigned rt_ref_t_width_ = 0u, rt_ref_t_height_ = 0u;
    // A kept picture held (rt_hold): the walks left (0: none), of how many, and its folder and number.
    unsigned rt_hold_left_ = 0u, rt_hold_walks_ = 0u;
    std::string rt_hold_folder_, rt_hold_number_;
    bool rt_signal_waiting_ = false;
    // The light volume (rt_pass_volume.cpp): drawn this frame; the turn after the last frame that drew it; its picture's
    // place among the textures and its sampler's (program.hlsli traced_air). The light in the air (rt_scatter.hlsl):
    // integrated this frame, its picture's place.
    [[nodiscard]] bool rt_volume_reads(const NebulaFrameItem& item, std::uint32_t state) const;
    [[nodiscard]] float rt_air_density() const;
    bool rt_volume_drawn_ = false, rt_scatter_drawn_ = false;
    bool rt_indirect_shown_ = false;     // the indirect ratio is picture 7 this frame (FB 22.10: the bounce's colour)
    std::uint32_t rt_indirect_slot_ = 0u;
    // The reflections' change was written this frame (rt_pass_reflect.cpp): the reactive mask reads it (rt_reactive).
    bool rt_reflect_moved_ = false;
    bool rt_transmit_drawn_ = false;        // the transmit pass wrote T this walk (ViewRows::transmit)
    std::uint32_t rt_transmit_slot_ = 0u;   // T's place among the textures (picture 6)
    // The guide blended draws compare with (FB, 15.2; ViewRows::blend_guide): 0 none this walk, 1 the second layer's
    // (picture 9), 2 the opaque layer's (picture 8).
    std::uint32_t rt_blend_guide_mode_ = 0u, rt_blend_guide_slot_ = 0u, rt_opaque_guide_slot_ = 0u;
    // An outside denoiser (rt_denoiser.h; rt_pass_reflect.cpp): the signals go to it raw this frame (g_more.z bit
    // 2048); its guides were written this frame.
    bool rt_raw_ = false, rt_guides_drawn_ = false;
    void rt_pass_guides();
    void rt_guides_publish();
    std::uint32_t rt_volume_turn_ = 0u, rt_volume_slot_ = 0u, rt_volume_sampler_ = 0u, rt_scatter_slot_ = 0u;
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
    std::byte* walk_rows_upload_ = nullptr;  // the same rows in the frame's upload memory
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
