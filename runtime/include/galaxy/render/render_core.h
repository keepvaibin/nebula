#pragma once

// RenderCore: runs the frame behind the galaxy::render API (render_api.h, through render_api.cpp).
//
// The game's thread attaches records (shapes, materials, passes, scene boundaries) and hands them over with render_frame;
// the render thread consumes them in order:
//
//   render_frame_on_thread(chunk)
//     -> renderer.begin_frame()
//     -> pipeline_cache.drain_completions()
//     -> on_shape_record for each record   (drawing state, draws, target passes, captures)
//     -> renderer.present()
//     -> renderer.end_frame() returns the submission fence
//
// A full queue (render_queue_depth chunks) blocks the producer. A render-thread failure is rethrown from the next
// render_frame() call rather than leaving a frozen display.

#include "galaxy/render/actor_lights.h"
#include "galaxy/render/frame_pass.h"
#include "galaxy/frame_cadence_diagnostics.h"
#include "galaxy/render/drawing_state.h"
#include "galaxy/render/draw_matrices.h"
#include "galaxy/render/movie_replacement.h"
#include "galaxy/render/native_shapes.h"
#if defined(NEBULA_GPU_VULKAN)
#include "galaxy/render/vk/pipeline_cache_vk.h"
#include "galaxy/render/vk/renderer_vk.h"
#else
#include "galaxy/render/pipeline_cache.h"
#include "galaxy/render/renderer_d3d12.h"
#endif
#include "galaxy/render/resident_vertex_arena.h"
#include "galaxy/render/renderer_common.h"
#include "galaxy/render/rt_sky_sun.h"
#include "galaxy/render/rt_types.h"
#include "galaxy/render/pso_key.h"
#include "galaxy/render/texture_store.h"
#include "galaxy/render/frame_interpolation.h"
#include "galaxy/render/interp_gpu.h"
#include "galaxy/render/transform_history.h"
#include "galaxy/render/draw_constants.h"
#include "galaxy/render/draw_constants_fill.h"
#include "galaxy/render/render_vertex.h"
#include "galaxy/render_api.h"
#include "galaxy/render/render_services.h"
#include "galaxy/frame_telemetry.h"

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

// lod_shift_x16: a texture-pack replacement 2^(shift / 16) times the source size. The game's LOD
// range moves by the shift (its base level is the replacement's level `shift`), except that a game
// range starting at its base level starts at the replacement's (close up, the game magnifies its
// base level: the replacement shows its own); a game sampler without mips uses the replacement's
// levels down to the source size. An arbitrary replacement (_arb) pairs its levels with the game's
// by index: the bias moves its LOD back to the one its levels were painted for
// (mip_lines_log2_x32: 32 x log2 of the frame's lines over kAuthoredLines), over its whole chain, without
// anisotropy (which would push its levels away); mip mode 3 in the key is linear without anisotropy.
[[nodiscard]] std::uint32_t pack_sampler_key(
    const TextureSampler& mode,
    bool generated_mips,
    std::uint8_t mip_levels,
    std::uint8_t lod_shift_x16 = 0u,
    bool arbitrary = false,
    std::uint8_t mip_lines_log2_x32 = 0u) noexcept;

// FNV-1a step over a whole 64-bit word (seed with kMix64OffsetBasis). Only bucket selection:
// each key's operator== decides identity.
inline constexpr std::uint64_t kMix64OffsetBasis = 1469598103934665603ull;
inline constexpr std::uint64_t kMix64Prime = 1099511628211ull;

[[nodiscard]] inline std::uint64_t mix64(std::uint64_t hash, std::uint64_t value) noexcept {
    return (hash ^ value) * kMix64Prime;
}

class RenderCore final {
public:
    RenderCore() = default;

    bool initialize(int width, int height);
    [[nodiscard]] std::string startup_failure() const { return renderer_.startup_failure(); }
    void render_frame(
        const RenderServices* services,
        bool present_swap_chain = true,
        bool black = false);
    void present_cached_display(
        bool black,
        bool present_swap_chain = true);
    void wait_for_render_idle();
    void set_frame_end_presents(bool enabled) noexcept {
        frame_end_presents_.store(enabled, std::memory_order_relaxed);
    }
    // A stage name as the game reports it (MR::getCurrentStageName), NUL-terminated.
    using SceneStage = std::array<char, 48>;
    // Simulation thread: a scene boundary, a record in the stream like the draws around it.
    void attach_scene_marker(SceneMarker marker) {
        shapes::Record record;
        record.kind = shapes::Record::Kind::Scene;
        record.key = static_cast<std::uint64_t>(static_cast<int>(marker));
        record.native_index = static_cast<std::uint32_t>(producer_record_pool_.scenes.size());
        shapes::SceneInfo& info = producer_record_pool_.scenes.emplace_back();
        if (marker == SceneMarker::SceneBegin && producer_scene_view_valid_) {
            info.view = producer_scene_view_;
            info.view_valid = true;
            info.sky_up = producer_scene_sky_up_;
            info.sky_up_valid = producer_scene_sky_up_known_;
        }
        if (marker == SceneMarker::SceneOpaqueEnd) {
            info.view = producer_scene_opaque_;
            info.view_valid = true;
            attach_shape_record(std::move(record));
            return;
        }
        info.stage = producer_scene_stage_;
        producer_scene_view_valid_ = false;
        attach_shape_record(std::move(record));
    }
    void attach_scene_opaque(const float view[12]) noexcept {
        for (unsigned i = 0; i < 12u; ++i) producer_scene_opaque_[i] = view[i];
    }
    [[nodiscard]] bool draw_identity_wanted() const noexcept {
        return draw_identity_wanted_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] unsigned bloom_route() const noexcept {
        return bloom_route_.load(std::memory_order_relaxed);
    }
    // Simulation thread: the game camera for the next SceneBegin marker, the player's up in the world
    // (null when unknown), kept in that camera's eye space for the enhanced sky, and the stage being
    // played (null: unknown, an empty name in the marker).
    void attach_scene_view(const float view[12], const float* up = nullptr, const char* stage = nullptr) noexcept {
        for (unsigned i = 0; i < 12u; ++i) producer_scene_view_[i] = view[i];
        producer_scene_view_valid_ = true;
        producer_scene_stage_ = {};
        for (std::size_t i = 0; stage != nullptr && i + 1u < producer_scene_stage_.size() && stage[i] != 0; ++i) {
            producer_scene_stage_[i] = stage[i];
        }
        producer_scene_sky_up_known_ = false;
        if (up != nullptr) {
            const float eye[3] = {view[0] * up[0] + view[1] * up[1] + view[2] * up[2],
                                  view[4] * up[0] + view[5] * up[1] + view[6] * up[2],
                                  view[8] * up[0] + view[9] * up[1] + view[10] * up[2]};
            const float length2 = eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2];
            producer_scene_sky_up_known_ = std::isfinite(length2) && length2 > 1e-12f;
            producer_scene_sky_up_ = producer_scene_sky_up_known_ ? encode_octahedral(eye) : 0u;
        }
    }
    // One particle of a JPA stripe at `stream_offset` of the live stream
    // (its two strip vertices follow); carried by the next submission.
    struct StripeParticleMark {
        std::size_t offset = 0;
        std::uint64_t particle = 0;
        std::uint32_t age_lifetime = 0;   // mAge << 16 | mLifeTime
        std::uint8_t strip = 0;           // 0 JPADrawStripe, 1-2 JPADrawStripeX
    };
    // Simulation thread.
    void attach_stripe_particle(const StripeParticleMark& mark) {
        if (producer_stripe_particles_.size() < 65536u) producer_stripe_particles_.push_back(mark);
    }
    // Simulation thread: a native shape record (native_shapes.h) the next submission carries, run when its parser
    // reaches the record's offset.
    void attach_shape_record(shapes::Record&& record) {
        producer_shape_records_.push_back(std::move(record));
    }
    // Simulation thread: room for a record's matrices and state ops in the pool the next submission carries.
    [[nodiscard]] shapes::Matrix* reserve_record_matrices(shapes::Record& record, std::uint32_t count) {
        record.matrix_first = static_cast<std::uint32_t>(producer_record_pool_.matrices.size());
        record.matrix_count = count;
        producer_record_pool_.matrices.resize(producer_record_pool_.matrices.size() + count);
        return producer_record_pool_.matrices.data() + record.matrix_first;
    }
    [[nodiscard]] state::Op* reserve_record_ops(shapes::Record& record, std::uint32_t count) {
        record.op_first = static_cast<std::uint32_t>(producer_record_pool_.ops.size());
        record.op_count = count;
        producer_record_pool_.ops.resize(producer_record_pool_.ops.size() + count);
        return producer_record_pool_.ops.data() + record.op_first;
    }
    [[nodiscard]] std::uint32_t reserve_record_native(const void* native, std::size_t bytes) {
        const auto index = static_cast<std::uint32_t>(producer_record_pool_.natives.size());
        producer_record_pool_.natives.emplace_back();
        std::memcpy(&producer_record_pool_.natives.back(), native, bytes);
        return index;
    }
    [[nodiscard]] ScreenProbe* reserve_record_probes(shapes::Record& record, std::uint32_t count) {
        record.vertex_first = static_cast<std::uint32_t>(producer_record_pool_.probes.size());
        record.vertex_count = count;
        producer_record_pool_.probes.resize(producer_record_pool_.probes.size() + count);
        return producer_record_pool_.probes.data() + record.vertex_first;
    }
    [[nodiscard]] RenderVertex* reserve_record_vertices(shapes::Record& record, std::uint32_t count) {
        record.vertex_first = static_cast<std::uint32_t>(producer_record_pool_.vertices.size());
        record.vertex_count = count;
        producer_record_pool_.vertices.resize(producer_record_pool_.vertices.size() + count);
        return producer_record_pool_.vertices.data() + record.vertex_first;
    }
    // Simulation thread: a record's matrices while the pool is still being filled.
    [[nodiscard]] const shapes::Matrix* producer_record_matrices(const shapes::Record& record) const noexcept {
        return producer_record_pool_.matrices.data() + record.matrix_first;
    }
    // Simulation thread: the movie frame later submissions carry (movie_replacement.h).
    void attach_movie_frame(const char* name, std::int32_t frame, float frame_rate, std::uint64_t y, std::uint64_t u,
                            std::uint64_t v, std::uint32_t width, std::uint32_t height) noexcept {
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
        mark.y = y;
        mark.u = u;
        mark.v = v;
        mark.width = static_cast<std::uint16_t>(width);
        mark.height = static_cast<std::uint16_t>(height);
    }
    // Simulation thread: channel `channel`'s latest finished probes, at most `capacity`; how many.
    [[nodiscard]] std::uint32_t screen_probe_results(std::uint32_t channel, ScreenProbe* probes, std::uint32_t capacity);
    void pump_messages();
    [[nodiscard]] bool quit_requested() const noexcept;
    [[nodiscard]] std::uint64_t display_copy_count() const noexcept;
    [[nodiscard]] DisplayPresentStats display_present_stats() const noexcept;

private:

    // Resolves draw state once for a run of `draw_count` draws (a native shape's); end_cached_draw_run follows
    // only when this returns true.
    [[nodiscard]] bool begin_cached_draw_run(
        Primitive primitive,
        std::uint8_t vtxfmt,
        std::size_t draw_count);
    void end_cached_draw_run();
    // A copy (scene target to texture or display) with the pixel format it reads.
    void on_frame_output(const FramePassRequest& params);
    // The frame clears' format byte (make_scene_clear_values): 1 a frame keeping alpha, 0 one without.
    [[nodiscard]] std::uint8_t frame_format() const noexcept { return draw_state_.frame_alpha ? 1u : 0u; }
    // A light op's words (the state), into matrix memory.
    void load_light(const state::Op& op);
    void on_host_marker(std::uint8_t kind, std::uint16_t aux, std::uint64_t a, std::uint64_t b);
    // Motion-signal markers (galaxy/motion_signals.h kinds 5-8).
    void on_motion_marker(std::uint8_t kind, std::uint16_t aux, std::uint64_t a, std::uint64_t b);

    // Consumes state_'s dirty bits: rebuilds the shader and render-state keys, writes
    // VertexConstants/PixelConstants into the constant ring, snapshots the matrix palette into the
    // matrix ring and refreshes the texture tables of the sampled maps. Returns the draw's pipeline,
    // or nullptr when none is ready (the draw is skipped).
    gpu::Pipeline* flush_draw_state(
        gpu::TopologyType primitive_topology_type, std::uint8_t vtxfmt);
    // Whether a triangle draw with vertex format `vtxfmt` is a curved surface (curved_surfaces.h): a perspective scene
    // draw of a model the game registered, its positions indexed, not bump mapped, while the setting is on.
    [[nodiscard]] bool curved_draw(std::uint8_t vtxfmt) const;
    // The topology a draw records: a curved surface's patches while its specialized pipeline draws it.
    [[nodiscard]] gpu::Topology draw_topology(gpu::Topology topology) const noexcept {
        return current_curved_ ? gpu::kTopologyTrianglePatches : topology;
    }

    struct PendingDrawBatch {
        bool active = false;
        DrawCall call{};
        bool indexed = false;
        std::uint32_t base_vertex = 0;
        std::uint32_t vertex_count = 0;
        std::size_t vertex_byte_end = 0;
        std::uint32_t first_index = 0;
        std::uint32_t index_count = 0;
        std::uint32_t draw_count = 0;
        // Vertices live in resident_vertex_buffer_ instead of the frame vertex
        // ring; vertex_byte_end/base_vertex are offsets in that buffer. Only
        // another resident run can extend such a batch.
        bool resident = false;
        // Ray-traced scene geometry (begin_rt_batch when the batch began).
        bool rt = false;
        // A light effect the game fakes (rt_materials.h hide) while the traced lighting replaces it.
        bool hidden = false;
        // Hidden but traced: a clear surface the engine traces (the table's `traced`), captured though not drawn.
        bool traced = false;
        // Its indices are a native shape's own, apart from the frame's index ring: nothing extends it.
        bool sealed = false;
    };

    [[nodiscard]] static bool draw_calls_batch_compatible(
        const DrawCall& lhs,
        const DrawCall& rhs,
        bool lhs_indexed,
        bool rhs_indexed) noexcept;
    void flush_pending_draw_batch();




    // One queued frame: its records and what the game said about it.
    struct FrameChunk {
        PointerResponseStamp pointer_response{};
        bool prerecorded_movie{};
        MovieFrameMark movie_frame{};
        const RenderServices* services = nullptr;
        std::uint64_t producer_wait_ns = 0;
        bool black = false;  // the output shows black (no picture)
        bool present_swap_chain = true;
        bool present_only = false;
        // JPA stripe particles in this chunk (attach_stripe_particle).
        std::vector<StripeParticleMark> stripe_particles;
        // The chunk's records, in the order the game made them (attach_shape_record).
        std::vector<shapes::Record> shape_records;
        shapes::RecordPool record_pool;
    };

    struct PendingFrameEffects {
        std::uint64_t fence_value = 0;
        std::uint64_t diagnostic_fence_bound_ns = 0;
        std::uint64_t diagnostic_fence_observed_ns = 0;
        const RenderServices* services = nullptr;
    };

    // Starts one fresh command-stream session. Every parser, state and tail mirror resets
    // together. Resetting only some of them can classify a receipt as event-free after shutdown
    // or reinitialize.
    void reset_session_state();

    void render_thread_main();
    void render_frame_on_thread(FrameChunk& chunk);
    // Render owner: completes an in-flight asynchronous depth field into the
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
    void present_vi_black(bool in_frame);
    bool black_shown_ = false;
    PointerResponseStamp producer_pointer_response_{}, render_pointer_response_{}, last_response_{};
    std::array<float, 12> producer_scene_view_{};
    std::array<float, 12> producer_scene_opaque_{};
    bool producer_scene_view_valid_ = false;
    std::uint32_t producer_scene_sky_up_ = 0u;
    bool producer_scene_sky_up_known_ = false;
    SceneStage producer_scene_stage_{};
    // Render thread: the stage the game last reported, and whether it ever did.
    SceneStage scene_stage_{};
    bool scene_stage_known_ = false;
    // Render thread: the scene's sky up changed; the next draw uploads its VS constants.
    bool sky_up_dirty_ = false;
    std::vector<StripeParticleMark> producer_stripe_particles_;
    // Render thread: the chunk's stripe marks while its records run (log
    // recording), and the stripe vertices of the pending draw batch.
    std::span<const StripeParticleMark> parse_stripes_{};
    std::size_t parse_stripe_next_ = 0;
    std::vector<StripeVertex> pending_stripe_vertices_;
    std::vector<StripeVertex> draw_stripe_vertices_;
    void take_attached_stripe_particles(FrameChunk& chunk);
    // Render thread, after a chunk's parse: trace of the stripe-particle marks.
    void note_stripe_particles(const FrameChunk& chunk);
    // Render thread: inside GameScene::draw3D (between the scene markers).
    bool scene_phase_ = false;
    // Render thread -> simulation thread: emit draw-identity markers.
    std::atomic<bool> draw_identity_wanted_{false};
    std::atomic<unsigned> bloom_route_{0u};
    // Draw identity (on_host_marker): the key of the marker in force in the
    // open scene and the matrix-write count at it. A transform record's
    // identity is that key with the matrix writes since. Repeated markers of
    // one scene get distinct keys through their occurrence counts. A scene
    // uses identities when the scene before it had markers (a module without
    // them keeps signature matching).
    std::uint64_t identity_marker_ = 0;
    std::uint32_t identity_matrix_base_ = 0;
    // Records with the same marker and matrix writes (texgen or color changes
    // between them) are told apart by their order.
    std::uint64_t identity_last_base_ = 0;
    std::uint32_t identity_sub_index_ = 0;
    // A render chunk began (streaming cuts frames mid-scene): its first
    // VS upload re-states the bindings and may continue the open record.
    bool scene_chunk_restart_ = false;
    std::uint64_t interp_chunk_continues_ = 0;   // records continued across a chunk start
    std::uint64_t identity_scene_index_ = 0;
    std::uint64_t identity_scene_markers_ = 0;
    bool identity_scene_ = false;
    struct IdentityOccurrence {
        std::uint64_t key = 0;
        std::uint64_t scene = 0;
        std::uint32_t count = 0;
    };
    std::vector<IdentityOccurrence> identity_occurrences_;
    std::uint32_t identity_occurrences_used_ = 0;
    [[nodiscard]] std::uint32_t note_identity_occurrence(std::uint64_t key);
    [[nodiscard]] std::uint64_t record_identity() noexcept;
    void begin_identity_scene();
    std::array<std::uint64_t, 13> identity_markers_by_kind_{};  // motion::kMarkerKinds
    std::uint64_t identity_records_ = 0;
    std::uint64_t identity_matched_ = 0;
    std::uint64_t identity_rejected_ = 0;
    std::uint64_t identity_scene_records_ = 0;
    // Motion signals (galaxy/motion_signals.h). The marker in force names a
    // particle: its age is the record's continuity (checked on match, not
    // part of the key).
    std::uint32_t identity_continuity_ = 0;
    bool identity_continuity_particle_ = false;
    // Category in force (draw phase and the latest CategoryList call).
    std::uint32_t identity_category_ = 0x00FF0000u;
    std::uint64_t motion_category_markers_ = 0;
    std::uint64_t motion_image_effect_markers_ = 0;
    std::uint64_t motion_scene_markers_ = 0;
    std::uint64_t motion_light_markers_ = 0;
    std::uint64_t motion_mirror_markers_ = 0;
    std::uint64_t motion_stripe_marks_ = 0;
    // GameScene::draw2D began (SceneMarker::Scene2DBegin); 2D layers in the report window.
    void on_scene_2d_begin();
    std::uint64_t window_2d_scenes_ = 0;
    std::uint64_t identity_signature_rejected_ = 0;
    std::uint64_t identity_continuity_rejected_ = 0;
    // Matched particle keys by age step: <0, 0, 1, 2, >2.
    std::array<std::uint64_t, 5> particle_age_steps_{};
    std::uint64_t interp_game_cuts_ = 0;        // cuts from the game's camera epoch
    std::uint64_t interp_threshold_cuts_ = 0;   // cuts from the turn/move thresholds only
    // Temporal upscaling for the current scene: perspective projections carry
    // this sub-pixel jitter (render pixels).
    bool temporal_scene_ = false;
    // The temporal scene writes motion vectors (the motion target is live).
    bool motion_scene_ = false;
    // The live keys carry the motion bits (cleared once after a scene).
    bool motion_bits_set_ = false;
    // Render target 1 of the resolved pipeline (DrawCall::aux_target).
    std::uint8_t current_pipeline_aux_ = 0;
    // The matrix rows' binding for draws: the matrix ring from its start (draws name their blocks' rows from it).
    gpu::Address draw_matrix_palette_ = 0;
    gpu::Address temporal_vs_palette_va_ = 0;
    TransformHistory transform_history_;
    // The scene records transforms (temporal upscaling or interpolation).
    bool history_scene_ = false;
    // An interpolated frame's records come in layers (InterpScene::layers): its
    // 3D scene (transform_history_), the screen layer after it (everything up
    // to the display copy: HUD, layouts, star pointer, wipes) and the mirror
    // pass before it (GameScene::drawMirror, its own camera). The side layers
    // match against the previous frame's same layer by their own identities
    // and move without the scene camera's world split.
    static constexpr unsigned kSceneLayer = 0u;
    static constexpr unsigned kScreenLayer = 1u;
    static constexpr unsigned kMirrorLayer = 2u;
    TransformHistory screen_history_;
    TransformHistory mirror_history_;
    unsigned side_layer_ = 0u;                 // kScreenLayer or kMirrorLayer while one records
    // The glow exposure the PS constants carry: 0 none, 1 sprite, 2 model.
    std::uint8_t glow_kind_ = 0u;
    // Blended scene draws of the translucent phases under ray-traced lighting take its texture in
    // map 7 (output_flags bit 6); the texture binding key marks them in map 7's TEX_MODE0 word.
    bool lit_translucent_ = false;
    static constexpr std::uint32_t kLitTranslucentKey = 0x4C495431u;
    bool lit_late_ = false;             // a lit late opaque draw (output_flags bit 7: its light at most 1.15)
    // Ambient occlusion (RenderConfig::ambient_occlusion, without traced lighting), sampled at each frame start; the
    bool ambient_occlusion_ = false;
    // The view of a perspective scene draw for ambient occlusion: its projection with the upscaler's jitter, depth
    // range and viewport (the ray tracer's RtView).
    [[nodiscard]] RtView scene_depth_view(const float terms[6], const ScaledViewport& viewport) const;
    // The scene's occlusion, once, before its first translucent phase (or at its end).
    void apply_scene_ao(const std::array<float, 12>& view);
    // The open scene's candidate key lights: the world-fixed light 0 of its lit draws, by world
    // direction and colour, with the draws using each and the sum of their ambient registers (0-1)
    // over those taking it from the register (traced).
    struct RtWorldKey {
        std::array<float, 3> direction{};
        std::uint32_t color = 0;
        std::uint32_t draws = 0;
        std::array<float, 3> ambient{};
        std::uint32_t ambient_draws = 0;
    };
    bool scene_3d_recorded_ = false;           // this frame's 3D scene took records
    bool mirror_opened_scene_ = false;         // this frame's InterpScene began at its mirror pass
    // A run of frames that draw no 3D scene (the pause menu over its captured picture): each is recorded as a screen
    // layer from its first draw (begin_screen_frame), so its layouts move in sub-frames as a scene's screen layer does.
    // frame_scene_seen_: a 3D scene began since the last display copy; screen_run_: the frame before drew none;
    // screen_frame_open_: this frame's screen-only record is open (a 3D scene beginning in it drops the record).
    bool frame_scene_seen_ = false;
    bool screen_run_ = false;
    bool screen_frame_open_ = false;
    void begin_screen_frame();
    void cancel_screen_frame();
    std::array<std::uint64_t, 3> layer_serials_{};
    std::array<std::uint64_t, 3> layer_identity_markers_{};
    std::array<bool, 3> layer_identity_{};     // the layer's previous occurrence carried markers
    // Per layer: records recorded and matches dropped because the scene before
    // did not hold the previous occurrence of the layer.
    std::array<std::uint64_t, 3> layer_records_{};
    std::array<std::uint64_t, 3> layer_unmapped_{};
    void open_interp_scene();
    void begin_side_layer(unsigned layer);
    void end_side_layer();
    void end_record_layer(unsigned layer);
    void set_identity_marker(std::uint8_t kind, std::uint16_t aux, std::uint64_t a, std::uint64_t tag);
    // Interpolated redraw (present rate above 60, temporal upscaler off):
    // this render chunk records a replay log and per-record VS constants.
    bool interp_recording_ = false;
    // Render thread: the chunk being rendered ended a game frame (a new picture), and the picture serial last
    // presented when its chunk ended (present_on_copy_enabled).
    bool display_copied_in_chunk_ = false;
    std::atomic<bool> frame_end_presents_{true};  // set_frame_end_presents
    std::uint64_t copy_presented_serial_ = 0u;
    unsigned interp_frames_ = 0;          // interpolated frames per game frame
    unsigned interp_scenes_in_chunk_ = 0;
    std::int32_t current_replay_record_ = -1;
    // The scene's current pixel-constant record (InterpScene::ps_constants), -1 for none.
    std::int32_t current_ps_record_ = -1;
    // The first draw of a recorded scene re-uploads its pixel constants so it has a record.
    bool ps_record_restart_ = false;
    std::int32_t current_palette_shadow_ = -1;
    // One history scene's interpolation inputs, kept for three scenes: the
    // one recording, the one being interpolated (its log completed), and its
    // predecessor (previous palettes and texture matrices).
    struct InterpScene {
        std::vector<VertexConstants> constants;   // per record, as uploaded
        std::vector<std::int32_t> prev_index;   // matched record of the previous scene
        std::vector<std::int32_t> palette;      // per record: shadow index or -1
        std::vector<std::uint8_t> per_vertex;   // per-vertex palette draw
        // Per record, from its drawing state: texgen i's bit when it maps position or normal (its matrices lerp
        // whole), and the lights its colour channels and emboss texgens evaluate.
        std::vector<std::uint8_t> spatial_texgens;
        std::vector<std::uint8_t> light_masks;
        // The palettes (shadows) as rows: per shadow a table of draw_rows::kRows indices into `palette_rows` (4
        // words a row). A shadow appends only the kinds that changed since the one before it; the rest it shares.
        std::vector<std::uint32_t> palette_tables;
        std::vector<std::uint32_t> palette_rows;
        std::size_t palette_count = 0;
        // Shadow `shadow`'s palette (valid until the scene appends rows).
        [[nodiscard]] RowView palette_view(std::size_t shadow) const noexcept {
            return {palette_tables.data() + shadow * draw_rows::kRows, palette_rows.data()};
        }
        // Pixel-stage constants per upload in the scene (draws carry the index).
        Affine34 camera{};
        // The game camera (world to eye) of the scene.
        Affine34 view{};
        bool view_known = false;
        // The projection the ray tracer took the scene's view from
        // and its viewport's size in scene target pixels: sub-frames interpolate it (SubframeTime::projection).
        std::array<float, 6> rt_projection{};
        float rt_viewport[2]{};
        bool rt_projection_known = false;
        bool valid = false;
        std::uint64_t serial = 0;               // replay serial of its log (0: none)
        // Per record: draw phase and category (galaxy/motion_signals.h).
        std::vector<std::uint32_t> category;
        // The game's camera epoch (scene marker); a different epoch in the
        // next scene is a hard cut.
        std::uint32_t camera_epoch = 0;
        bool camera_epoch_known = false;
        // Record ranges of the frame's layers (RenderCore::kSceneLayer,
        // kScreenLayer, kMirrorLayer). Every prev_index is in the scene
        // before's index space (RenderCore::end_record_layer).
        struct LayerRange {
            std::size_t first = 0;
            std::size_t count = 0;
            std::uint64_t serial = 0;   // 0: not recorded
        };
        std::array<LayerRange, 3> layers{};
        [[nodiscard]] bool in_layer(unsigned layer, std::size_t r) const noexcept {
            return r - layers[layer].first < layers[layer].count;
        }
        void clear() noexcept {
            constants.clear(); prev_index.clear();
            palette.clear(); per_vertex.clear(); spatial_texgens.clear(); light_masks.clear(); palette_tables.clear(); palette_rows.clear(); palette_count = 0;
            valid = false; serial = 0;
            category.clear(); camera_epoch_known = false;
            rt_projection_known = false;
            layers = {};
        }
    };
    // Recording, ready (log completed), playing, and the playing scene's
    // predecessor (previous palettes and texture matrices).
    std::array<InterpScene, 4> interp_scenes_{};
    unsigned interp_scene_ = 0;           // scene being recorded
    unsigned interp_log_scene_ = 0;       // scene of the completed log
    std::uint64_t interp_seen_serial_ = 0;
    void note_interp_log_completion();
    struct InterpSequence {
        bool active = false;
        std::uint32_t picture_slot = 0;  // the game frame it ends on
        unsigned subframes = 0;
        unsigned next = 0;
        std::int64_t frame_qpc = 0;       // the frame's arrival (its present)
        double lead = 0.0;                // t of a slot at the arrival (see start)
        // Set inside a render chunk (no frame may be recorded there): the
        // render loop presents the real frame before anything else.
        bool flush_requested = false;
        unsigned scene = 0;               // interp_scenes_ index being interpolated
        bool classified = false;          // interp_moving_ holds this scene's records
        bool ready = false;               // the replay and its program are recorded
        // The scene interpolated from: the one before, or the one shown last
        // when the one before was never shown (its log replaced unplayed);
        // `chained`: records then match through the skipped scene
        // (interp_chain_prev_).
        unsigned before = 0;
        bool chained = false;
    };
    // The interp_scenes_ index of the last frame on screen (a sub-frame or the
    // real frame of a sequence), -1 when unknown (a frame presented alone).
    std::int32_t interp_shown_scene_ = -1;
    // Its sequence and t (1: the real frame): the frame before of the next
    // replay's motion vectors.
    std::uint64_t interp_shown_serial_ = 0;
    float interp_shown_t_ = 0.0f;
    std::vector<std::int32_t> interp_chain_prev_;
    std::uint64_t interp_chained_sequences_ = 0;  // started from the scene shown last
    std::uint64_t interp_unchained_skips_ = 0;    // a never-shown scene before, not chained
    // The playing scene, classified once per sequence (render_interp_subframe):
    // the game camera between the scene before and it, every record whose
    // constants change in a sub-frame, and every palette snapshot pair.
    CameraMotion interp_camera_{};
    bool interp_camera_game_cut_ = false;  // the sequence cut by the game's own camera epoch (timeline)
    float interp_camera_move_ = 0.0f;     // eye movement into the scene before (cut test)
    struct InterpRecordMotion {
        std::uint32_t record = 0;
        std::int32_t prev = -1;           // matched record of the scene before
        std::int32_t palette = -1;        // interp_palette_motions_ index
        EyeMotion position{};             // inline position matrix
        bool position_moves = false;
        bool tex_moves = false;           // inline texture or post matrices
        bool projection_moves = false;
    };
    std::vector<InterpRecordMotion> interp_moving_;
    std::vector<InterpRecordMotion> interp_pending_;  // classification scratch
    struct InterpPaletteMotion {
        std::int32_t shadow = -1;         // palette shadow of the playing scene
        std::int32_t before = -1;         // of the scene before, or -1
        std::int32_t next = -1;           // next entry of the same shadow
        std::uint16_t position_mask = 0;  // PNMTX slots its records read
        std::uint16_t normal_mask = 0;    // normal matrices its records read
        std::uint8_t light_mask = 0;      // lights its records evaluate
        bool orthographic = false;
        std::uint8_t layer = 0;           // of the records that read it
        PaletteMotion motion{};
    };
    // The camera of screen-layer records: none (no split, no cut). Mirror-pass
    // records have no split either, and cut with the scene camera.
    static constexpr CameraMotion kScreenCamera{};
    CameraMotion interp_mirror_camera_{};
    [[nodiscard]] const CameraMotion& layer_camera(unsigned layer) const noexcept {
        return layer == kScreenLayer ? kScreenCamera : layer == kMirrorLayer ? interp_mirror_camera_ : interp_camera_;
    }
    std::vector<InterpPaletteMotion> interp_palette_motions_;
    std::vector<std::int32_t> interp_palette_first_;  // per shadow: first entry
    std::uint64_t interp_moving_records_ = 0;        // summed per sub-frame
    std::uint64_t interp_palette_decompositions_ = 0;
    std::uint64_t interp_camera_cuts_ = 0;
    // A ready frame's present arrived inside a render chunk: the render loop
    // finishes the playing sequence, then starts this one.
    bool interp_start_pending_ = false;
    std::uint64_t interp_sequence_serial_ = 0;
    // GALAXY_INTERP_SNAPSHOT_DIR/_FIRST/_LAST: sequences whose sub-frames and
    // real frame are read back (seq_<n>_sub<i>.ppm, seq_<n>_real.ppm).
    void run_pending_interp_start();
    InterpSequence interp_sequence_{};
    // A recorded frame whose present arrives as a separate present.
    bool interp_log_waiting_ = false;
    std::uint32_t interp_log_slot_ = 0;
    bool interp_stopping_ = false;        // render thread draining for a stop
    // RendererD3D12::replay_hold_segment() seen at the last render chunk.
    unsigned resident_hold_segment_ = ~0u;
    std::uint64_t interp_held_vertex_blocks_ = 0;   // released while interpolating
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
    // GALAXY_INTERP_TIMELINE=<file>: one line per present while interpolating (pacing checks).
    // Smoothed game-frame arrival clock (start_interp_sequence).
    std::int64_t interp_arrival_qpc_ = 0;
    std::int64_t interp_frame_period_qpc_ = 0;
    std::uint64_t interp_late_slots_ = 0;   // slots a stall skipped
    std::uint64_t interp_superseded_ = 0;   // frames replaced before their own slot
    void supersede_interp_sequence();
    void start_interp_sequence(std::uint32_t picture_slot);
    void select_interp_before();
    // Sub-frames per game frame within the GPU budget.
    unsigned interp_gpu_governor(double& rate);
    unsigned interp_gpu_cap_ = 7u;
    std::uint64_t interp_gpu_cap_serial_ = 0;
    std::uint64_t interp_gpu_checked_ = 0;
    // Sequences after a rate or upscaler change whose GPU times are not used (pipeline,
    // upscaler and target creation).
    std::uint64_t interp_gpu_warm_until_ = 0;
    // A new sub-frame configuration (interp_frames_ changed): both governors start over.
    void restart_interp_governors();
    std::uint64_t interp_gpu_capped_ = 0;   // sequences with fewer sub-frames
    // Game-frame cadence governor (note_game_frame_cadence).
    void note_game_frame_cadence(std::int64_t now_qpc);
    // The present rate a sub-frame count gives, and the counts of the next lower and higher rates.
    [[nodiscard]] double interp_rate_with(unsigned subframes) const;
    [[nodiscard]] unsigned interp_step_down(unsigned subframes) const;
    [[nodiscard]] unsigned interp_step_up(unsigned subframes) const;
    unsigned interp_cadence_cap_ = 7u;
    unsigned interp_cadence_interval_s_ = 2u;
    std::int64_t interp_cadence_window_qpc_ = 0;
    std::int64_t interp_cadence_last_qpc_ = 0;
    std::uint32_t interp_cadence_misses_ = 0;   // in the window: the sub-frames'
    std::uint32_t interp_cadence_own_ = 0;      // in the window: the game's own
    std::int64_t interp_cadence_change_qpc_ = 0;
    std::int64_t interp_cadence_try_qpc_ = 0;
    std::uint32_t interp_cadence_frames_ = 0;
    std::int64_t interp_cadence_grace_until_qpc_ = 0;   // misses after a change are not counted
    std::uint64_t interp_cadence_drops_ = 0;
    double interp_cadence_fps_ = 0.0;
    double interp_cadence_own_misses_ = 0.0;   // the game's own misses per second: their lowest recent rate
    // The game thread's time blocked on the render thread (queue space, drains, completions, scene target peeks), in QPC
    // ticks: the sub-frames' share of it tells their misses from the game's own.
    std::atomic<std::uint64_t> game_wait_qpc_{0};
    std::uint64_t interp_cadence_wait_seen_ = 0;
    std::uint64_t interp_cadence_wait_qpc_ = 0;   // in the window
    // The render thread's time rendering sub-frames (advance_interp_sequence), in QPC ticks.
    std::uint64_t interp_subframe_qpc_ = 0;
    std::uint64_t interp_cadence_subframe_seen_ = 0;
    // A probe of the game's own misses: seconds of them in a row above the lowest rate, the cap a probe lowered and
    // their rate before it, and the earliest next probe.
    unsigned interp_cadence_own_windows_ = 0;
    bool interp_cadence_probe_ = false;
    unsigned interp_cadence_probe_restore_ = 0;
    double interp_cadence_probe_misses_ = 0.0;
    std::int64_t interp_cadence_probe_after_qpc_ = 0;
    // The playing sequence's evaluation program and output slots (prepare_interp_sequence).
    interp_gpu::Program interp_program_;
    interp_gpu::Layout interp_layout_{};
    std::vector<std::int32_t> interp_vs_slot_;             // per record
    std::vector<std::int32_t> interp_record_palette_slot_;  // per record
    std::vector<std::int32_t> interp_palette_slot_;        // per interp_palette_motions_ entry
    std::vector<std::int32_t> interp_palette_entry_;       // per palette slot: its entry
    std::vector<std::array<std::uint32_t, 5>> interp_palette_source_;  // per palette slot: its draw's blocks
    bool prepare_interp_sequence(const InterpScene& scene, const InterpScene& before_scene, bool before_valid);
    // Pixel-constant records of the playing scene whose colours change
    // (record, matched record of the scene before), and their sub-frame uploads.
    std::uint64_t interp_presented_ = 0;
    std::uint64_t interp_dropped_ = 0;
    std::uint64_t interp_failed_ = 0;     // sub-frames that could not be recorded
    std::uint64_t interp_history_resets_ = 0;  // presented frames that start the temporal upscaler's history again
    std::uint64_t interp_history_carried_ = 0; // first sub-frames whose frame before ended the pair before
    double interp_cpu_ms_ = 0.0;          // wall time per sub-frame, waits included
    double interp_prepare_ms_ = 0.0;      // classification, program and replay per sequence
    std::array<double, 6> interp_prepare_phase_ms_{};
    std::uint64_t interp_prepared_ = 0;   // sequences prepared
    std::uint64_t interp_program_ops_ = 0;
    std::uint64_t interp_program_words_ = 0;
    std::uint64_t interp_sequences_ = 0;
    std::uint64_t interp_snapped_ = 0;    // records shown at their own transform
    std::uint64_t interp_started_ = 0;
    std::uint64_t interp_last_played_serial_ = 0;
    std::uint64_t interp_logs_skipped_ = 0;     // completed logs replaced before playing
    std::uint64_t interp_present_skipped_ = 0;  // presents while a sequence played
    // Game frames presented without sub-frames, by reason: overlay, no 3D
    // scene, several scenes, (unused), log already gone, unmatched.
    std::array<std::uint64_t, 6> interp_real_only_{};
    bool interp_presenting_real_ = false;
    void note_interp_real_present();
    void report_interp_stats();
    std::int64_t interp_stats_qpc_ = 0;
    // Render thread: steps the interpolation sequence of the latest frame
    // (one sub-frame or the real frame when due; `flush` presents the real
    // frame now and drops the remaining sub-frames).
    void advance_interp_sequence(bool flush);
    bool render_interp_subframe(float t);
    [[nodiscard]] std::int64_t interp_remaining_us() const;  // to the next slot
    [[nodiscard]] double interp_slot_t() const;
    static void interp_sleep_us(std::int64_t us);
    bool scene_camera_captured_ = false;
    float scene_jitter_px_[2]{};
    bool producer_movie_content_{},render_movie_content_{};
    MovieFrameMark producer_movie_frame_{}, render_movie_frame_{};
    // HD movie replacements: the decoder (render thread) and the folders last applied.
    MovieReplacements movie_replacements_;
    std::uint64_t movie_mods_generation_ = ~0ull;
    std::uint64_t movie_stats_tick_ = 0;
    std::array<std::uint64_t,512> pointer_latency_bins_{};
    std::uint64_t pointer_response_count_{}, pointer_latency_total_{}, pointer_latency_max_{}, pointer_report_ns_{};
    std::array<std::uint64_t,512> pointer_acquisition_age_bins_{};
    std::uint64_t pointer_acquisition_age_count_{}, pointer_acquisition_age_total_{}, pointer_acquisition_age_max_{};
    void record_pointer_response(const DisplayTexture& texture) noexcept;
    void record_presented_display_copy(const DisplayTexture& texture) noexcept;
    void record_skipped_duplicate_display_present(
        const DisplayTexture& texture) noexcept;
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
    // The matrices and lights the game loads (E-05 replaces it with a per-frame arena).
    DrawMatrices matrices_;
    // The drawing state the game's records set, as typed values (drawing_state.h).
    NativeDrawState draw_state_;
    // The blend the pixel engine performs (effective_blend: one that writes the source unchanged is none).
    [[nodiscard]] BlendMode draw_blend() const {
        return effective_blend(draw_state_.blend, draw_state_.frame_alpha);
    }
    PipelineCache pipeline_cache_;
    TextureCache texture_cache_;
    DisplayPictures display_pictures_;
    Renderer renderer_;

    const RenderServices* frame_services_ = nullptr;

    // Frame queue: the sim thread is the producer, the render thread the consumer. The producer may
    // run a small bounded number of frames ahead, then blocks on queue_space_cv_. This is the
    // a frame-granular throttle. chunk_pool_ recycles chunk
    // buffers, so the steady state does not allocate.
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
    // Recycled chunk buffers. The pool is capped, as each kept buffer holds its peak capacity (a
    // snapshot can exceed a megabyte); the cap covers the deepest selectable queue (render_queue_depth
    // up to 8) plus the chunk the consumer holds, twice over.
    static constexpr std::size_t kChunkPoolRetainedBuffers = 24u;
    static constexpr std::size_t kMaxSelectableRenderQueueDepth = 8u;
    static_assert(
        kChunkPoolRetainedBuffers >= 2u * kMaxSelectableRenderQueueDepth + 1u,
        "the chunk pool must cover twice the deepest render queue plus the consumer's chunk");
    // `sky_up`: the scene's VertexConstants::sky_up when `sky_up_known`; `stage` the game's stage (SceneBegin).
    void on_scene_marker(SceneMarker marker, const std::array<float, 12>* view = nullptr, std::uint32_t sky_up = 0u,
                        bool sky_up_known = false, const SceneStage* stage = nullptr);
    // Temporal scene: jitter, palette base, transform record and motion
    // fields of a VS constant upload (`upload` is write-combined memory).
    void write_scene_vs_constants(const VertexConstants& cpu, VertexConstants& upload);
    // Writes the jittered projection of a perspective draw into `upload`;
    // `jitter_ndc` receives the offset (zero for orthographic draws).
    void apply_scene_jitter(const VertexConstants& cpu, VertexConstants& upload,
                            float jitter_ndc[2]);
    // Probes in flight, oldest first; the answers of each channel. Only the answers are shared with the simulation thread.
    struct ScreenProbeFlight {
        std::uint32_t channel = 0, count = 0;
        std::size_t slot = 0;
        ScreenProbeKind kind = ScreenProbeKind::Alpha;
        std::array<ScreenProbe, kScreenProbesMax> probes{};
    };
    std::array<ScreenProbeFlight, Renderer::kScreenProbeSlots> probe_flights_{};
    std::size_t probe_flight_first_ = 0, probe_flights_active_ = 0;
    void record_screen_probes(const shapes::Record& record);
    static constexpr std::uint32_t kScreenProbeChannels = 8u;
    struct ScreenProbeAnswers {
        std::uint32_t count = 0;
        std::array<ScreenProbe, kScreenProbesMax> probes{};
    };
    mutable std::mutex probe_answers_lock_;
    std::array<ScreenProbeAnswers, kScreenProbeChannels> probe_answers_{};
    std::size_t render_chunks_in_flight_ = 0;
    std::size_t present_chunks_in_flight_ = 0;
    std::size_t utility_chunks_in_flight_ = 0;

    // GPU-resident decoded vertices (render thread only). One persistently
    // mapped UPLOAD buffer, created lazily per device; see
    // resident_vertex_arena.h for the block retirement rule.
    static constexpr std::uint32_t kResidentDecodedVertexCapacity =
        static_cast<std::uint32_t>(
            64u * 1024u * 1024u / sizeof(RenderVertex));
    ResidentVertexArena resident_vertices_;
    UploadBuffer resident_vertex_buffer_;
    gpu::Address resident_vertex_gpu_ = 0;
    gpu::Device* resident_vertex_device_ = nullptr;
    bool resident_vertex_buffer_failed_ = false;
    // Curved surface triangles drawn plain (no corner bends) and as patches, and the draws split between them.
    [[nodiscard]] bool ensure_resident_vertex_buffer();
    void release_resident_vertex_buffer() noexcept;
    // `triangles` (indices into `vertices`) with those no corner bends first, into `split`; how many indices those
    // are. Empty `split` when an index is out of range.
    static std::uint32_t split_curved_triangles(std::span<const RenderVertex> vertices,
                                                std::span<const std::uint16_t> triangles,
                                                std::vector<std::uint16_t>& split);

    // Native shapes (native_shapes.h): the game's arrays and groups by key as its records registered them (a key
    // registered again is replaced: the game made something new at that address), and per group the vertices last
    // expanded for it, resident while the arrays they came from are the same.
    struct ShapeArrayEntry {
        std::shared_ptr<const shapes::Array> array;
        std::uint64_t version = 0;
    };
    std::unordered_map<std::uint64_t, ShapeArrayEntry> shape_arrays_;
    std::uint64_t shape_array_versions_ = 0;
    struct RecordShape {
        std::shared_ptr<const shapes::Geometry> geometry;
        std::array<std::uint64_t, 12> versions{};  // of the arrays its vertices came from
        std::uint8_t position_matrix = 0;
        std::shared_ptr<const curved::Model> curved;
        bool expanded = false;
        std::uint64_t resident_epoch = 0;
        std::uint32_t resident_offset = 0;
        std::uint32_t resident_units = 0;     // the block: its vertices, then its indices
        std::uint32_t resident_vertices = 0;  // the vertices alone
        std::vector<std::uint16_t> split_indices;
        std::uint32_t split_flat_indices = 0;
        bool split_built = false;
        ImmutableUploadToken split_upload_token;
    };
    std::unordered_map<std::uint64_t, RecordShape> record_shapes_;
    std::vector<RenderVertex> expand_scratch_;  // a group's vertices on their way to the resident buffer
    std::vector<shapes::Record> producer_shape_records_;
    shapes::RecordPool producer_record_pool_;
    const shapes::RecordPool* current_record_pool_ = nullptr;
    void take_attached_shape_records(FrameChunk& chunk);
    // Render thread: a record at its place in the stream (an array or group registered, or a group drawn).
    void on_shape_record(const shapes::Record& record);
    void draw_record_shape(const shapes::Record& record);
    // Vertices the game sent as it drew them, through the frame's rings.
    void draw_record_immediate(const shapes::Record& record);
    // A material record into the drawing state (native_material.h).
    // A native material (E-04): its state into the drawing state; its pipeline and constants for
    // the draws until end_native_material (the next material or drawing-state change).
    void apply_native_material(const material::Native* native);
    void end_native_material();
    void begin_target_pass(const shapes::Record& record);
    void end_target_pass(const shapes::Record& record);
    // The open target pass's size (game units), for its closing copy.
    std::array<std::uint32_t, 2> target_pass_size_{};
    // A draw record's matrices and current matrices, into the matrix state its draw reads.
    void apply_record_matrices(const shapes::Record& record);
    // Gives back a group's resident block.
    void release_record_shape(RecordShape& shape) noexcept;
    // The vertex inputs of the draw being made when a native shape record draws (its corners' matrix slots, its
    // model's arrays), in place of the vertex description the stream's draws carry.
    struct RecordVertexInputs {
        std::uint32_t matrix_bits = 0;  // bit 0 position matrix per corner, bits 1-8 texture matrices 0-7
        std::uint64_t positions = 0;    // the position array's key
        std::uint64_t colors = 0;       // the colour arrays' keys, combined
        bool colored = false;
        // Positions and colours the group's own values (geometry the game builds): drawn as direct vertices were.
        bool own_positions = false;
        const curved::Model* curved = nullptr;
    };
    const RecordVertexInputs* record_inputs_ = nullptr;
    // draw_record_immediate's vertices and indices before they go to the rings.
    std::vector<std::uint16_t> immediate_indices_;
    // The vertex inputs the last keys were built with.
    std::uint64_t vertex_source_ = 0;
    bool stop_render_thread_ = false;
    std::exception_ptr render_thread_error_;
    std::uint64_t present_epoch_ = 0;
    DisplaySerialAccountingState display_serial_accounting_{};
    MonotonicCadenceAccumulator display_copy_production_cadence_{};
    MonotonicCadenceAccumulator display_first_serial_present_cadence_{};
    std::atomic<std::uint64_t> last_presented_display_stamp_{
        ~std::uint64_t{0}};
    std::atomic<std::uint64_t> last_presented_display_serial_{
        ~std::uint64_t{0}};
    std::atomic<std::uint32_t> last_presented_slot_{0};
    // Present rates below the game's 60 Hz show the newest frame on their own cadence. A 30 Hz present
    // shows every other frame at full game speed.
    [[nodiscard]] bool skip_for_present_rate();
    std::int64_t next_rate_present_qpc_ = 0;

    // Cached per-draw bindings, rebuilt when the matching dirty bit fires.
    RenderStateKey render_state_key_{};
    std::uint64_t current_vs_hash_{};
    // The native material the draws use (null: the state's), whether its constants are not yet uploaded, and the
    // texture maps it binds.
    material::Native native_material_storage_{};
    const material::Native* native_material_ = nullptr;
    std::uint32_t native_texmtx_ = 0x80000000u;  // the header words as last uploaded (bit 31: a draw's)
    bool native_constants_dirty_ = false;
    std::uint8_t native_texture_mask_ = 0u;
    std::uint64_t current_ps_hash_{};
    bool shader_key_hashes_valid_ = false;
    // RenderConfig::per_pixel_lighting, sampled at each frame start.
    bool per_pixel_lighting_ = false;
    // RenderConfig::curved_surfaces where the renderer tessellates, sampled at each frame start; whether the last
    // flushed draw is a curved surface (its keys carry kVsKeyCurved and the patch topology).
    bool curved_surfaces_ = false;
    bool current_curved_ = false;
    // The last curved draw's pipeline without the patch stages, for its flat triangles (DrawCall::flat_pipeline);
    // null where the order of a draw's triangles could show (blending, a logic op, no depth test or write) or while
    // it compiles.
    gpu::Pipeline* current_flat_pipeline_ = nullptr;
    // RenderConfig::enhanced_lighting, sampled at each frame start; the last draw's eligibility and
    // material word (enhanced_lighting.h), kept until its keys or eligibility change.
    bool enhanced_lighting_ = false;
    bool enhanced_gate_ = false;
    std::uint32_t enhanced_material_ = 0u;
    // The material table bits (enhanced_table_material) the last uploaded PS constants carry.
    std::uint32_t enhanced_material_bits_ = 0u;
    // The material whose glow gain (output_flags bit 5) or falling water (bit 6) the PS constants hold.
    std::uint16_t glow_material_ = 0u;
    // The share of a clear ice or crystal draw's tint the PS constants clear (rt_materials.h kRtIceTintKept; 0 none).
    float ps_tint_cleared_ = 0.0f;
    PsoKey current_pso_key_{};
    bool current_pso_key_valid_ = false;
    gpu::Pipeline* current_pipeline_ = nullptr;
    // Hash of `current_pso_key_`, kept beside it so the memo probe never
    // re-derives the fold for a key it already hashed once.
    std::uint64_t current_pso_key_hash_ = 0u;
    gpu::Address current_vs_constants_ = 0;
    VsConstantsCpuCache cpu_vs_constants_{};
    gpu::Address current_ps_constants_ = 0;
    gpu::Table current_texture_table_{};
    // The textures current_texture_table_ holds (texture cache handles; empty maps have no resource).
    std::array<TextureHandle, kMaxTextureMaps> current_texture_handles_{};
    gpu::Table current_sampler_table_{};
    gpu::Address current_matrix_palette_ = 0;
    bool matrix_palette_stale_ = true;
    // Requirement of the last resolved VS. A palette-free draw can reuse its
    // bindings while retaining stale palette debt for a later palette user.
    bool current_palette_required_ = true;
    bool inline_matrices_stale_ = true;
    // The matrix blocks (E-05): where each kind's rows were last appended in this frame slot's matrix ring (float4
    // index from the ring's base; positions, textures, normals, post-transform matrices, lights), and the light
    // block the draw reads (the game's, or its moved lights'). They hold once the slot's first draw appended every
    // kind (matrix_blocks_valid_, cleared on the slot rewind and on session reset).
    std::array<std::uint32_t, 5> matrix_block_base_{};
    std::uint32_t draw_light_base_ = 0;
    bool matrix_bases_changed_ = true;
    // The recording scene's index (InterpScene::palette_rows) of each row's current value (draw_rows numbering); its
    // first shadow appends every row.
    std::array<std::uint32_t, draw_rows::kRows> scene_row_index_{};
    // Kinds changed since the recording scene's last shadow (uploads between shadows clear the matrices' own mask).
    std::uint32_t scene_kinds_pending_ = 0;
    bool matrix_blocks_valid_ = false;
    std::uint64_t matrix_blocks_epoch_ = 0;  // the matrix ring's epoch() the blocks were appended in
    bool matrix_blocks_forced_ = true;
    // Upload/descriptor-ring handles are frame-slot-local. Rebuild them on
    // the first draw after begin_frame(), even when register state did not
    // change. scene target copies can also replace a game-address texture alias
    // without rewriting its texture state.
    bool frame_bindings_dirty_ = true;
    bool texture_bindings_dirty_ = true;
    std::uint8_t current_texture_map_mask_ = 0;
    // sampled_texture_map_mask(draw_state_) reads DrawCounts, the material order, indirect-command and IND_REF registers,
    // all of which set kDirtyTev; invalidated wherever the state resets.
    bool current_texture_map_mask_valid_ = false;

    struct TextureBindingKey {
        // The sampled maps; kLitTranslucentKey when the lit-translucent path takes map 7.
        std::array<TextureMap, kMaxTextureMaps> maps{};
        std::uint32_t lit_translucent = 0;
        std::uint8_t map_mask = 0;

        [[nodiscard]] bool operator==(const TextureBindingKey& other) const noexcept = default;
    };
    struct TextureHandleKey {
        std::uint64_t address = 0;
        std::uint16_t width = 0;
        std::uint16_t height = 0;
        std::uint8_t format = 0;
        std::uint8_t levels = 1;
        std::uint8_t generated_mips = 0;

        [[nodiscard]] bool operator==(const TextureHandleKey&) const =
            default;
    };
    [[nodiscard]] TextureBindingKey capture_texture_binding_key(
        std::uint8_t map_mask) const;
    [[nodiscard]] TextureHandleKey texture_handle_key(std::uint64_t texture, const TextureSampler& mode) const;
    [[nodiscard]] std::array<std::uint32_t, kMaxTextureMaps> capture_sampler_keys(
        std::uint8_t map_mask,
        const std::array<TextureHandle, kMaxTextureMaps>& handles) const;
    void mark_texture_binding_alias_dirty(const FramePassRequest& params);
    void prune_dirty_texture_binding_tables();
    void clear_frame_texture_binding_table_cache();
    void clear_texture_binding_table_cache();
    TextureBindingKey current_texture_binding_key_{};
    bool current_texture_binding_key_valid_ = false;
    // Sampled map mask that produced current_texture_binding_key_. A map-mask
    // change alone must force a new capture even when no texture state
    // moved, so the key stays an exact function of its inputs.
    std::uint8_t current_texture_binding_key_mask_ = 0;

    struct TextureBindingDependency {
        std::uint64_t address = 0;
        std::uint16_t width = 0;
        std::uint16_t height = 0;
        std::uint8_t format = 0;
        // Exact decoded mip-chain or strided scene target alias footprint.
        std::uint32_t source_bytes = 0;

        [[nodiscard]] bool operator==(
            const TextureBindingDependency&) const = default;
    };
    void invalidate_texture_handle_cache_dependency(
        const TextureBindingDependency& dependency);
    void invalidate_texture_binding_cache_key(std::uint64_t key);
    struct DirtyTextureAlias {
        TextureBindingDependency dependency{};
    };

    struct TextureTableKey {
        std::array<std::uintptr_t, kMaxTextureMaps> resources{};
        std::array<std::uint32_t, kMaxTextureMaps> srv_indices{};
        std::array<std::uint16_t, kMaxTextureMaps> widths{};
        std::array<std::uint16_t, kMaxTextureMaps> heights{};
        std::uint8_t map_mask = 0;

        // Field-wise, like TextureTableKeyHash: the 7 bytes of tail padding after map_mask must not take part.
        [[nodiscard]] bool operator==(const TextureTableKey& other) const
            noexcept {
            return resources == other.resources &&
                srv_indices == other.srv_indices &&
                widths == other.widths &&
                heights == other.heights &&
                map_mask == other.map_mask;
        }
    };
    // Layout guard, tail padding included; the padding is why the keys compare field-wise rather than with
    // key_bytes_equal like the padding-free shader keys (TextureBindingKey's typed maps compare and hash by field).
    static_assert(sizeof(TextureTableKey) ==
                  kMaxTextureMaps *
                          (sizeof(std::uintptr_t) + sizeof(std::uint32_t) +
                           sizeof(std::uint16_t) + sizeof(std::uint16_t)) +
                      8u);
    struct TextureTableKeyHash {
        [[nodiscard]] std::size_t operator()(
            const TextureTableKey& key) const noexcept {
            std::uint64_t hash = mix64(kMix64OffsetBasis, key.map_mask);
            for (unsigned index = 0; index < kMaxTextureMaps; ++index) {
                hash = mix64(hash, key.resources[index]);
                hash = mix64(hash, key.srv_indices[index]);
                hash = mix64(hash, key.widths[index]);
                hash = mix64(hash, key.heights[index]);
            }
            return static_cast<std::size_t>(hash);
        }
    };
    struct TextureHandleKeyHash {
        [[nodiscard]] std::size_t operator()(
            const TextureHandleKey& key) const noexcept {
            std::uint64_t hash = 1469598103934665603ull;
            const auto mix = [&hash](std::uint64_t value) {
                for (unsigned byte = 0; byte < 8; ++byte) {
                    hash ^= (value >> (byte * 8u)) & 0xFFu;
                    hash *= 1099511628211ull;
                }
            };
            mix(key.address);
            mix(key.width);
            mix(key.height);
            mix(key.format);
            mix(key.levels);
            mix(key.generated_mips);
            return static_cast<std::size_t>(hash);
        }
    };
    struct TextureBindingKeyHash {
        [[nodiscard]] std::size_t operator()(
            const TextureBindingKey& key) const noexcept {
            std::uint64_t hash = mix64(kMix64OffsetBasis, key.map_mask);
            hash = mix64(hash, key.lit_translucent);
            for (const TextureMap& t : key.maps) {
                const TextureSampler& m = t.mode;
                hash = mix64(hash, t.texture);
                hash = mix64(hash, static_cast<std::uint64_t>(m.wrap_s) | static_cast<std::uint64_t>(m.wrap_t) << 4u |
                                       static_cast<std::uint64_t>(m.mag_linear) << 8u |
                                       static_cast<std::uint64_t>(min_filter_code(m)) << 12u |
                                       static_cast<std::uint64_t>(static_cast<std::uint16_t>(lod_bias_x32(m.lod_bias))) << 16u |
                                       static_cast<std::uint64_t>(m.anisotropy) << 32u |
                                       static_cast<std::uint64_t>(lod_x16(m.min_lod)) << 40u |
                                       static_cast<std::uint64_t>(lod_x16(m.max_lod)) << 48u |
                                       static_cast<std::uint64_t>(t.edge_lod) << 56u |
                                       static_cast<std::uint64_t>(t.bias_clamp) << 57u);
            }
            return static_cast<std::size_t>(hash);
        }
    };
    struct TextureBindingTables {
        gpu::Table texture_table{};
        gpu::Table sampler_table{};
        std::array<TextureBindingDependency, kMaxTextureMaps>
            dependencies{};
        std::uint8_t dependency_count = 0;
        // Non-owning, frame-fence-valid handles used to maintain decoded LRU.
        std::array<TextureHandle, kMaxTextureMaps> texture_handles{};
    };
    std::unordered_map<
        TextureTableKey,
        gpu::Table,
        TextureTableKeyHash>
        frame_texture_tables_;
    std::unordered_map<
        TextureHandleKey,
        TextureHandle,
        TextureHandleKeyHash>
        texture_handle_cache_;
    std::uint64_t seen_texture_retirement_revision_ = 0;
    // Texture-pack settings last applied (render config and the mods' folders, polled at frame begin).
    TexturePackSettings texture_pack_settings_{};
    bool texture_pack_settings_applied_ = false;
    std::uint64_t texture_pack_mods_generation_ = ~0ull;
    std::vector<std::filesystem::path> texture_pack_mod_folders_;
    std::vector<std::uint32_t> texture_pack_mod_categories_off_;
    std::unordered_map<
        TextureHandleKey,
        TextureHandle,
        TextureHandleKeyHash>
        frame_texture_handle_cache_;
    std::unordered_map<
        TextureBindingKey,
        TextureBindingTables,
        TextureBindingKeyHash>
        frame_texture_binding_tables_;
    std::vector<DirtyTextureAlias> dirty_texture_aliases_;

    std::uint64_t frame_index_ = 0;
    // frame_index_ published once per frame for the producer's sampling decision; it orders nothing.
    cadence::Session* cadence_diagnostics_ = nullptr;
    bool initialized_ = false;
    // The per-draw clock reads run this frame: on stall-traced frames and on frames sampled by
    bool scene_seeded_ = false;

    // The last display copy came from the output scene target and nothing has drawn or cleared since: an scene target-to-texture
    // copy now takes that picture (RendererD3D12::draw_frame_pass_from_output), not the render scene target's scene.
    bool display_copy_on_output_ = false;

    // Bring-up stats, reported every 300 frames.
    std::uint64_t stat_draws_issued_ = 0;
    std::uint64_t stat_draws_no_pso_ = 0;
    // PSO-resolution thread cycles this frame; valid only when every resolution was measured.
    // Sampled wall time of whole flush_draw_state calls (ScopedFlushTiming) and the sample count; unlike
    // frame_flush_us_ it needs no per-draw instrumentation.
    // Sub-slices of frame_flush_pso_us_: memo hits (no PipelineCache call), the PipelineCache::get() call,
    // and the PsoKey hash and compare work.
    // Cached display-list replay path per batch: the parser offers the two batched paths before per-packet
    // replay.
    // Bytes written to the matrix upload ring this frame, always counted.
    // Per-frame deltas of PipelineCache::CompileStats against frame_compile_stats_baseline_: driver builds
    // (pso-compiles), builds from the D3D12 pipeline library (pso-library-loads), FXC-generated shader pairs
    // and pairs whose DXBC came from disk.
    // FXC microseconds this frame.
    std::uint64_t frame_texture_decode_join_us_ = 0;
    // Always-counted structural counters (the timing fields above need frame_time_detail_). The memo and
    // get() routes are frame_pso_memo_hits_ and frame_pso_get_calls_.
    PendingDrawBatch pending_draw_batch_{};
    bool cached_draw_run_active_ = false;
    bool cached_draw_run_scissor_reject_ = false;
    bool cached_draw_run_primitive_indexed_ = false;
    bool cached_draw_run_no_pso_ = false;
    bool cached_draw_run_batch_compatible_known_ = false;
    Primitive cached_draw_run_primitive_ = Primitive::Points;
    std::uint8_t cached_draw_run_vtxfmt_ = 0;
    DrawCall cached_draw_run_state_call_{};
    std::atomic<std::uint64_t> stat_display_copies_{0};
    std::unique_ptr<galaxy::telemetry::Publisher> frame_telemetry_;
    std::atomic<std::uint64_t> stat_display_present_requests_{0};
    std::atomic<std::uint64_t> stat_display_present_presented_{0};
    std::atomic<std::uint64_t> stat_display_first_serial_presentations_{0};
    std::atomic<std::uint64_t>
        stat_display_repeated_or_stale_serial_requests_{0};
    std::atomic<std::uint64_t> stat_display_present_skipped_duplicate_presents_{0};
    std::atomic<std::uint64_t> stat_display_present_duplicate_run_max_{0};
    std::atomic<std::uint64_t> stat_display_out_of_order_serial_requests_{0};
    std::atomic<std::uint64_t> stat_display_present_serial_delta_zero_{0};
    std::atomic<std::uint64_t> stat_display_present_serial_delta_one_{0};
    std::atomic<std::uint64_t> stat_display_present_serial_delta_multi_{0};
    std::atomic<std::uint64_t>
        stat_display_highest_first_presented_copy_serial_{0};

};

}  // namespace galaxy::render
