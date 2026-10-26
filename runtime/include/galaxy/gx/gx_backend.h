#pragma once

// GxBackend: owns the GX pipeline's subsystems and runs the per-frame loop behind the galaxy::gx
// API (gx_d3d12.h, through gx_shim.cpp).
//
// The simulation thread calls render_frame() once per VI retrace; it copies the frame's FIFO bytes
// into a bounded queue and returns. The render thread consumes the chunks in order:
//
//   render_frame_on_thread(chunk)
//     -> renderer.begin_frame()            (drains PSO completions first)
//     -> pipeline_cache.drain_completions()
//     -> parser.run(fifo, ...)             (this object is the FifoSink)
//          register loads  -> state_
//          on_draw         -> dirty state to keys and constants, vertex_loader into the rings,
//                             texture_cache lookups, renderer.draw()
//          on_efb_copy     -> copy to an XFB or texture, copy-clear
//          on_pe_*         -> collect the game-visible FINISH/TOKEN events
//     -> renderer.present(efb_copies.latest())
//     -> renderer.end_frame() returns the submission fence
//     -> the collected events bind to that fence
//
// A full queue (render_queue_depth chunks) blocks the producer. One chunk per retrace, parsed in
// submission order, is the only synchronization: guest-memory reads during the parse (textures,
// vertex arrays) run a bounded number of frames behind the simulation, the window real GP
// hardware has while the game respects GXDrawDone.
//
// A render-thread failure is rethrown from the next render_frame() call rather than leaving a
// frozen display.

#include "galaxy/gx/actor_lights.h"
#include "galaxy/gx/efb_copy.h"
#include "galaxy/gx/fifo_parser.h"
#include "galaxy/gx/gx_state.h"
#include "galaxy/gx/movie_replacement.h"
#if defined(NEBULA_GPU_VULKAN)
#include "galaxy/gx/vk/pipeline_cache_vk.h"
#include "galaxy/gx/vk/renderer_vk.h"
#else
#include "galaxy/gx/pipeline_cache.h"
#include "galaxy/gx/renderer_d3d12.h"
#endif
#include "galaxy/gx/resident_vertex_arena.h"
#include "galaxy/gx/renderer_common.h"
#include "galaxy/gx/rt_sky_sun.h"
#include "galaxy/gx/rt_types.h"
#include "galaxy/gx/shader_keys.h"
#include "galaxy/gx/texture_cache.h"
#include "galaxy/gx/frame_interpolation.h"
#include "galaxy/gx/interp_gpu.h"
#include "galaxy/gx/transform_history.h"
#include "galaxy/gx/uber_constants.h"
#include "galaxy/gx/vs_constants.h"
#include "galaxy/gx/vertex_loader.h"
#include "galaxy/gx_d3d12.h"
#include "galaxy/guest_memory.h"
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
#include <mutex>
#include <span>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace galaxy::gx {

struct GuestMemoryRange {
    std::uint32_t guest_base = 0;
    std::uint32_t size = 0;
};

// A game-visible PE event found by the FIFO preclassifier. The classifier runs FifoParser, so
// partial commands, draw payloads, BP masks and CALL_DL recursion split exactly as for the renderer.
struct FramePeEventSignature {
    enum class Kind : std::uint8_t {
        Finish,
        Token,
    };

    Kind kind = Kind::Finish;
    std::uint16_t token = 0;
    bool interrupt = false;

    [[nodiscard]] bool operator==(
        const FramePeEventSignature&) const = default;
};

class FramePeEventClassifier final : private FifoSink {
public:
    FramePeEventClassifier();
    void reset();
    void classify(
        const std::byte* fifo_data,
        std::size_t fifo_size,
        GuestMemory* memory,
        std::vector<FramePeEventSignature>& events,
        bool frozen_regions = false);

private:
    [[nodiscard]] std::size_t draw_payload_size(
        std::uint8_t vtxfmt,
        std::uint16_t vertex_count) const override;
    void on_draw(
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        FifoCursor& cursor) override;
    [[nodiscard]] bool on_cached_simple_draw_run(
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        std::span<const std::byte> bytes,
        std::size_t base_offset,
        std::span<const CachedDrawPacket> packets) override;
    [[nodiscard]] bool on_cached_prepared_draw_run(
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        std::span<const std::byte> prepared_payload,
        std::size_t base_offset,
        std::size_t local_opcode_offset,
        std::uint8_t opcode,
        std::size_t source_draw_count) override;
    [[nodiscard]] bool on_cached_packet_draw_run(
        std::uint64_t cache_token,
        std::size_t packet_run_index,
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        std::span<const std::byte> bytes,
        std::size_t base_offset,
        std::span<const CachedDrawPacket> packets,
        std::uint32_t total_vertices,
        std::uint32_t total_indices,
        std::span<const std::uint16_t> precomputed_indices) override;
    void on_efb_copy(std::uint32_t exec_command) override;
    void on_pe_finish() override;
    void on_pe_token(std::uint16_t token, bool interrupt) override;
    void on_invalidate_textures() override;
    void on_tlut_load() override;
    void on_invalidate_vertex_cache() override;

    GxState state_{};
    FifoParser parser_{};
    std::vector<std::byte> pending_fifo_;
    std::vector<FramePeEventSignature>* active_events_ = nullptr;
    // draw_payload_size() per vertex format, valid while CP VCD_LO/HI and VAT_A/B/C are unchanged.
    struct VtxPayloadStrideMemo {
        bool valid = false;
        std::uint32_t vcd_lo = 0u;
        std::uint32_t vcd_hi = 0u;
        std::uint32_t vat_a = 0u;
        std::uint32_t vat_b = 0u;
        std::uint32_t vat_c = 0u;
        std::size_t stride = 0u;
    };
    mutable std::array<VtxPayloadStrideMemo, 8> vtx_payload_stride_memo_{};
};

// lod_shift_x16: a texture-pack replacement 2^(shift / 16) times the guest size. The guest's LOD
// range moves by the shift (its base level is the replacement's level `shift`), except that a guest
// range starting at its base level starts at the replacement's (close up, the guest magnifies its
// base level: the replacement shows its own); a guest sampler without mips uses the replacement's
// levels down to the guest size. An arbitrary replacement (_arb) pairs its levels with the guest's
// by index, as Dolphin does: the bias moves its LOD back to the guest's at the console's resolution
// (efb_scale_log2_x32: 32 x log2 of the internal resolution scale), over its whole chain, without
// anisotropy (which would push its levels away); mip mode 3 in the key is linear without anisotropy.
[[nodiscard]] std::uint32_t pack_sampler_key(
    const TexMode& mode,
    bool generated_mips,
    std::uint8_t mip_levels,
    std::uint8_t lod_shift_x16 = 0u,
    bool arbitrary = false,
    std::uint8_t efb_scale_log2_x32 = 0u) noexcept;

// FNV-1a step over a whole 64-bit word (seed with kMix64OffsetBasis). Only bucket selection:
// each key's operator== decides identity.
inline constexpr std::uint64_t kMix64OffsetBasis = 1469598103934665603ull;
inline constexpr std::uint64_t kMix64Prime = 1099511628211ull;

[[nodiscard]] inline std::uint64_t mix64(std::uint64_t hash, std::uint64_t value) noexcept {
    return (hash ^ value) * kMix64Prime;
}

class GxBackend final : public FifoSink {
public:
    GxBackend() = default;

    bool initialize(int width, int height);
    [[nodiscard]] std::string startup_failure() const { return renderer_.startup_failure(); }
    [[nodiscard]] FramePeCompletionToken render_frame(
        const std::byte* fifo_data,
        std::size_t fifo_size,
        GuestMemory* memory,
        const GxServices* services,
        bool present_swap_chain = true,
        std::uint32_t displayed_xfb_addr = 0);
    void present_cached_xfb(
        std::uint32_t displayed_xfb_addr,
        bool present_swap_chain = true);
    void wait_for_render_idle();
    [[nodiscard]] bool wait_for_frame_pe_completion_for(
        FramePeCompletionToken token,
        std::uint32_t timeout_ms);
    // Scene boundaries one render chunk carries (3D and 2D, two frames).
    static constexpr std::size_t kFifoMarkersPerChunk = 8u;
    // A stage name as the game reports it (MR::getCurrentStageName), NUL-terminated.
    using SceneStage = std::array<char, 48>;
    // Simulation thread: the next render_frame submission handles `marker`
    // when its parser reaches `fifo_offset` bytes into the submitted FIFO.
    void attach_fifo_marker(FifoMarker marker, std::size_t fifo_offset) noexcept {
        if (producer_fifo_marker_count_ < producer_fifo_markers_.size()) {
            const std::uint8_t index = producer_fifo_marker_count_++;
            producer_fifo_markers_[index] = {fifo_offset, marker};
            producer_fifo_marker_views_[index] = producer_scene_view_;
            producer_fifo_marker_sky_up_[index] = producer_scene_sky_up_;
            if (producer_scene_view_valid_ && marker == FifoMarker::SceneBegin) {
                producer_fifo_marker_view_mask_ |= static_cast<std::uint8_t>(1u << index);
            } else {
                producer_fifo_marker_view_mask_ &= static_cast<std::uint8_t>(~(1u << index));
            }
            if (producer_scene_view_valid_ && producer_scene_sky_up_known_ && marker == FifoMarker::SceneBegin) {
                producer_fifo_marker_sky_up_mask_ |= static_cast<std::uint8_t>(1u << index);
            } else {
                producer_fifo_marker_sky_up_mask_ &= static_cast<std::uint8_t>(~(1u << index));
            }
            producer_fifo_marker_stages_[index] = producer_scene_stage_;
        }
        producer_scene_view_valid_ = false;
    }
    [[nodiscard]] bool rt_shadow_casters_wanted() const noexcept {
        return rt_shadow_casters_wanted_.load(std::memory_order_relaxed);
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
    // One particle of a JPA stripe at `stream_offset` of the live GX stream
    // (its two strip vertices follow); carried by the next submission.
    struct StripeParticleMark {
        std::size_t offset = 0;
        std::uint32_t particle = 0;
        std::uint32_t age_lifetime = 0;   // mAge << 16 | mLifeTime
        std::uint8_t strip = 0;           // 0 JPADrawStripe, 1-2 JPADrawStripeX
    };
    // Simulation thread.
    void attach_stripe_particle(const StripeParticleMark& mark) {
        if (producer_stripe_particles_.size() < 65536u) producer_stripe_particles_.push_back(mark);
    }
    // Simulation thread: the movie frame later submissions carry (movie_replacement.h).
    void attach_movie_frame(const char* name, std::int32_t frame, float frame_rate, std::uint32_t y, std::uint32_t u,
                            std::uint32_t v, std::uint32_t width, std::uint32_t height) noexcept {
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
    void set_safety_scene_surround(float intensity) noexcept { producer_safety_surround_=intensity; }
    [[nodiscard]] bool peek_efb(
        std::uint16_t x,
        std::uint16_t y,
        EfbPeekKind kind,
        std::uint32_t& value);
    // Simulation thread: answers a depth or color peek from an asynchronous EFB field. A peek made in
    // a draw-sync token's callback (`token` nonzero) reads the field captured where the stream
    // reached that token (the console stopped the GPU there with a breakpoint while the callback
    // peeked); the field a frame earlier, as the capture completes asynchronously. Until that
    // channel has a field, and for token 0, the latest field of the kind answers. The request is
    // recorded so the render thread keeps producing fields while peeks continue.
    [[nodiscard]] bool peek_efb_published(
        std::uint16_t x,
        std::uint16_t y,
        EfbPeekKind kind,
        std::uint16_t token,
        std::uint32_t& value);
    void scan_pe_events_on_sim_thread(
        const std::byte* fifo_data,
        std::size_t fifo_size,
        GuestMemory* memory,
        const GxServices* services);
    void notify_guest_memory_write(
        std::uint32_t address,
        std::uint32_t size) noexcept;
    void pump_messages();
    [[nodiscard]] bool quit_requested() const noexcept;
    [[nodiscard]] std::uint64_t xfb_copy_count() const noexcept;
    [[nodiscard]] XfbPresentStats xfb_present_stats() const noexcept;

    struct DependencyDrawRunRangeCacheKey {
        std::uint64_t cache_token = 0;
        std::size_t packet_run_index = 0;
        PrimitiveClass primitive = PrimitiveClass::Points;
        std::uint8_t vtxfmt = 0;
        std::uint64_t state_hash = 0;

        GxState::DependencyShapeWords shape_words{};

        [[nodiscard]] bool operator==(
            const DependencyDrawRunRangeCacheKey&) const = default;
    };

    struct DependencyDrawRunRangeCacheKeyHash {
        [[nodiscard]] std::size_t operator()(
            const DependencyDrawRunRangeCacheKey& key) const noexcept {
            std::uint64_t hash = 1469598103934665603ull;
            const auto mix = [&hash](std::uint64_t value) noexcept {
                for (unsigned byte = 0; byte < 8; ++byte) {
                    hash ^= (value >> (byte * 8u)) & 0xFFu;
                    hash *= 1099511628211ull;
                }
            };
            mix(key.cache_token);
            mix(static_cast<std::uint64_t>(key.packet_run_index));
            mix(static_cast<std::uint8_t>(key.primitive));
            mix(key.vtxfmt);
            mix(key.state_hash);
            return static_cast<std::size_t>(hash);
        }
    };

    struct DependencyDrawRunRangeCacheEntry {
        DependencyDrawRunRangeCacheKey key{};
        std::uint64_t last_used = 0;
        std::size_t byte_size = 0;
        std::vector<GuestMemoryRange> ranges;
    };

private:
    friend class GxBackendMemorySnapshotTestAccess;

    struct VertexLayoutCacheEntry {
        bool valid = false;
        std::uint32_t vcd_lo = 0;
        std::uint32_t vcd_hi = 0;
        std::uint32_t vat_a = 0;
        std::uint32_t vat_b = 0;
        std::uint32_t vat_c = 0;
        VertexDescriptor desc{};
        std::size_t source_size = 0;
    };

    [[nodiscard]] const VertexLayoutCacheEntry& cached_vertex_layout(
        std::uint8_t vtxfmt) const;

    [[nodiscard]] std::size_t draw_payload_size(
        std::uint8_t vtxfmt,
        std::uint16_t vertex_count) const override;
    void on_draw(
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        FifoCursor& cursor) override;
    [[nodiscard]] bool begin_cached_draw_run(
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        std::size_t draw_count) override;
    void end_cached_draw_run() override;
    [[nodiscard]] bool on_cached_simple_draw_run(
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        std::span<const std::byte> bytes,
        std::size_t base_offset,
        std::span<const CachedDrawPacket> packets) override;
    [[nodiscard]] bool on_cached_prepared_draw_run(
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        std::span<const std::byte> prepared_payload,
        std::size_t base_offset,
        std::size_t local_opcode_offset,
        std::uint8_t opcode,
        std::size_t source_draw_count) override;
    [[nodiscard]] bool on_cached_packet_draw_run(
        std::uint64_t cache_token,
        std::size_t packet_run_index,
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        std::span<const std::byte> bytes,
        std::size_t base_offset,
        std::span<const CachedDrawPacket> packets,
        std::uint32_t total_vertices,
        std::uint32_t total_indices,
        std::span<const std::uint16_t> precomputed_indices) override;
    void on_efb_copy(std::uint32_t exec_command) override;
    void on_pe_finish() override;
    void on_pe_token(std::uint16_t token, bool interrupt) override;
    void on_invalidate_textures() override;
    void on_invalidate_vertex_cache() override;
    void on_tlut_load() override;
    void on_host_marker(std::uint8_t kind, std::uint16_t aux, std::uint32_t a,
                        std::uint32_t b) override;
    // Motion-signal markers (galaxy/motion_signals.h kinds 5-8).
    void on_motion_marker(std::uint8_t kind, std::uint16_t aux, std::uint32_t a, std::uint32_t b);

    // Consumes state_'s dirty bits: rebuilds the shader and render-state keys, writes
    // GxVsConstants/GxPsConstants into the constant ring, snapshots the XF matrix palette into the
    // matrix ring and refreshes the texture tables of the sampled maps. Returns the draw's pipeline,
    // or nullptr when none is ready (the draw is skipped).
    gpu::Pipeline* flush_draw_state(
        gpu::TopologyType primitive_topology_type, std::uint8_t vtxfmt);
    // Whether a triangle draw with vertex format `vtxfmt` is a curved surface (curved_surfaces.h): a perspective scene
    // draw of a model the game registered, its positions indexed, not bump mapped, while the setting is on.
    [[nodiscard]] bool curved_draw(std::uint8_t vtxfmt) const;
    // The topology a draw records: a curved surface's patches while its specialized pipeline draws it.
    [[nodiscard]] gpu::Topology draw_topology(gpu::Topology topology) const noexcept {
        return current_curved_ && !current_pipeline_is_uber_ ? gpu::kTopologyTrianglePatches : topology;
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
        RtDrawInfo rt_info{};
        std::uint64_t debake_model = 0u;   // an undecided model whose vertex colours this batch samples
    };

    [[nodiscard]] static bool draw_calls_batch_compatible(
        const DrawCall& lhs,
        const DrawCall& rhs,
        bool lhs_indexed,
        bool rhs_indexed) noexcept;
    void flush_pending_draw_batch();
    // Shader and PSO cache coverage, printed once per frame when the on-disk coverage changed.
    void ReportPipelineCoverage();
    std::size_t last_reported_shader_records_ = 0;
    std::size_t last_reported_pso_records_ = 0;
    void on_cached_draw_run_draw(
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        FifoCursor& cursor);

    struct MemorySnapshotStorage {
        std::byte* source = nullptr;
        std::uint32_t size = 0;
        bool used = false;
        bool copied = false;
        std::vector<std::byte> bytes;
    };

    struct MemorySnapshot {
        GuestMemory memory{};
        std::vector<GuestMemoryRegion> regions;
        std::vector<MemorySnapshotStorage> storage;
        // Rebuilt lazily per capture; entries are slot indices, so growth and moves keep them valid.
        std::vector<std::size_t> storage_lookup;
        std::size_t storage_used_count = 0;
        bool storage_lookup_active = false;
        // Every requested byte is copied and no pointer into live guest memory remains: the regions
        // point into `storage` only.
        bool sealed = false;
        // These regions (not guest views in general) are sorted and disjoint.
        bool regions_disjoint_sorted = false;
    };

    struct GuestWriteRange {
        std::uint32_t guest_addr = 0;
        std::uint32_t size = 0;
    };

    struct PeEvent {
        enum class Kind : std::uint8_t {
            Finish,
            Token,
        };

        Kind kind = Kind::Finish;
        std::uint16_t token = 0;
        bool interrupt = false;
    };

    struct EfbPeekRequest {
        std::uint16_t x = 0;
        std::uint16_t y = 0;
        EfbPeekKind kind = EfbPeekKind::Color;
        std::span<std::uint32_t> depth_pixels;
        std::uint32_t value = 0;
        bool success = false;
        bool done = false;
        std::exception_ptr error;
        std::mutex mutex;
        std::condition_variable cv;
    };

    // One queued frame: the FIFO bytes the simulation produced for a retrace and the memory and
    // services the parser callbacks need. With memory_snapshot_active, `memory` points at
    // memory_snapshot, so delayed parsing sees the arrays and textures of submission time.
    struct FrameChunk {
        PointerResponseStamp pointer_response{};
        bool prerecorded_movie{};
        float safety_surround{};
        MovieFrameMark movie_frame{};
        std::vector<std::byte> fifo;
        std::vector<GuestWriteRange> dirty_ranges;
        std::vector<GuestMemoryRange> memory_ranges;
        std::vector<PeEvent> pe_events;
        std::vector<FramePeEventSignature> preclassified_pe_events;
        GuestMemory* memory = nullptr;
        const GxServices* services = nullptr;
        FramePeCompletionToken pe_completion_token{};
        std::uint64_t producer_wait_ns = 0;
        std::uint64_t pe_classification_ns = 0;
        std::uint32_t displayed_xfb_addr = 0;
        bool present_swap_chain = true;
        bool present_only = false;
        bool memory_snapshot_active = false;
        bool memory_snapshot_strict = false;
        bool cpu_reads_pending = false;
        bool fifo_effects_pending = false;
        bool pe_events_preclassified = false;
        bool pe_event_free_receipt = false;
        bool pe_completion_requires_resolution = false;
        bool pe_events_scanned_on_sim_thread = false;
        bool efb_peek_only = false;
        EfbPeekRequest* efb_peek_request = nullptr;
        std::shared_ptr<PointerDepthCapture> pointer_depth_capture;
        // In-frame pointer field: recorded when the parser reaches this
        // offset of the chunk's FIFO (the scene boundary).
        std::size_t pointer_depth_capture_fifo_offset = 0;
        bool pointer_depth_in_frame = false;
        // Scene boundaries in this chunk's FIFO, in stream order (3D and 2D
        // scenes of up to two frames).
        std::array<std::pair<std::size_t, FifoMarker>, kFifoMarkersPerChunk> fifo_markers{};
        std::uint8_t fifo_marker_count = 0;
        // Game camera (world to eye) per SceneBegin marker; bit i of the
        // mask marks marker i's view valid.
        std::array<std::array<float, 12>, kFifoMarkersPerChunk> fifo_marker_views{};
        std::uint8_t fifo_marker_view_mask = 0;
        // The player's up per SceneBegin marker (GxVsConstants::sky_up); bit i of the mask marks it known.
        std::array<std::uint32_t, kFifoMarkersPerChunk> fifo_marker_sky_up{};
        std::uint8_t fifo_marker_sky_up_mask = 0;
        // The stage per SceneBegin marker (empty when the game did not say).
        std::array<SceneStage, kFifoMarkersPerChunk> fifo_marker_stages{};
        // JPA stripe particles in this chunk's FIFO (attach_stripe_particle).
        std::vector<StripeParticleMark> stripe_particles;
        std::uint64_t dependency_scan_ns = 0;
        std::uint64_t snapshot_ns = 0;
        std::uint64_t snapshot_bytes = 0;
        std::uint64_t snapshot_range_bytes = 0;
        std::uint64_t dirty_bytes = 0;
        std::size_t snapshot_range_count = 0;
        std::size_t dirty_range_count = 0;
        bool dependency_cache_hit = false;
        MemorySnapshot memory_snapshot;
    };

    struct PendingFrameEffects {
        FramePeCompletionToken pe_completion_token{};
        std::uint64_t fence_value = 0;
        std::uint64_t diagnostic_fence_bound_ns = 0;
        std::uint64_t diagnostic_fence_observed_ns = 0;
        const GxServices* services = nullptr;
        std::vector<PeEvent> pe_events;
    };

    void drain_guest_memory_writes(std::vector<GuestWriteRange>& out) noexcept;
    static void coalesce_dirty_ranges(
        std::vector<GuestWriteRange>& ranges) noexcept;
    static std::uint64_t dirty_range_bytes(
        const std::vector<GuestWriteRange>& ranges) noexcept;
    std::size_t invalidate_dirty_texture_ranges(
        const std::vector<GuestWriteRange>& ranges);
    // Starts one fresh GX command-stream session. Every parser, state and tail mirror resets
    // together. Resetting only some of them can classify a receipt as event-free after shutdown
    // or reinitialize.
    void reset_fifo_session_state();

    void render_thread_main();
    void render_frame_on_thread(FrameChunk& chunk);
    void execute_efb_peek_on_thread(FrameChunk& chunk);
    void poll_completed_pointer_depth_capture();
    void drain_pending_pointer_depth_capture();
    // Render owner: completes an in-flight asynchronous depth field into the
    // next publication slot, and submits a new one after a rendered chunk
    // while the simulation thread keeps peeking.
    void poll_async_peek_fields();
    void submit_async_peek_fields_if_requested();
    void mark_frame_cpu_reads_done(FrameChunk& chunk);
    void mark_frame_fifo_effects_done(FrameChunk& chunk);
    void wait_for_render_cpu_reads_idle();
    void queue_frame_fifo_effects_fence(
        FrameChunk& chunk,
        std::uint64_t fence_value);
    void poll_completed_frame_effects();
    static void deliver_pe_event(
        const GxServices* services,
        const PeEvent& event);
    void present_cached_xfb_on_thread(
        std::uint32_t displayed_xfb_addr,
        bool sleep_if_missing,
        bool count_game_xfb_present = true);
    void present_latest_frame_on_thread();
    // The VI shows black (displayed_xfb_addr kNoDisplayedXfbAddr): one black present per black
    // run, repeated while the settings overlay is open. `in_frame`: inside a render chunk's
    // begin_frame/end_frame.
    void present_vi_black(bool in_frame);
    bool vi_black_shown_ = false;
    PointerResponseStamp producer_pointer_response_{}, render_pointer_response_{}, last_response_{};
    std::shared_ptr<PointerDepthCapture> producer_pointer_depth_capture_;
    std::size_t producer_pointer_depth_capture_offset_ = 0;
    std::array<std::pair<std::size_t, FifoMarker>, kFifoMarkersPerChunk> producer_fifo_markers_{};
    std::uint8_t producer_fifo_marker_count_ = 0;
    std::array<std::array<float, 12>, kFifoMarkersPerChunk> producer_fifo_marker_views_{};
    std::uint8_t producer_fifo_marker_view_mask_ = 0;
    std::array<std::uint32_t, kFifoMarkersPerChunk> producer_fifo_marker_sky_up_{};
    std::uint8_t producer_fifo_marker_sky_up_mask_ = 0;
    std::array<float, 12> producer_scene_view_{};
    bool producer_scene_view_valid_ = false;
    std::uint32_t producer_scene_sky_up_ = 0u;
    bool producer_scene_sky_up_known_ = false;
    std::array<SceneStage, kFifoMarkersPerChunk> producer_fifo_marker_stages_{};
    SceneStage producer_scene_stage_{};
    // Render thread: the stage the game last reported, and whether it ever did.
    SceneStage scene_stage_{};
    bool scene_stage_known_ = false;
    // Render thread: the scene's sky up changed; the next draw uploads its VS constants.
    bool sky_up_dirty_ = false;
    std::vector<StripeParticleMark> producer_stripe_particles_;
    // Render thread: the chunk's stripe marks while its FIFO is parsed (log
    // recording), and the stripe vertices of the pending draw batch.
    std::span<const StripeParticleMark> parse_stripes_{};
    std::size_t parse_stripe_next_ = 0;
    std::vector<StripeVertex> pending_stripe_vertices_;
    std::vector<StripeVertex> draw_stripe_vertices_;
    void take_attached_stripe_particles(FrameChunk& chunk, std::size_t fifo_size);
    // Render thread, after a chunk's parse: trace of the stripe-particle marks.
    void note_stripe_particles(const FrameChunk& chunk);
    // Render thread: inside GameScene::draw3D (between the scene markers).
    bool scene_phase_ = false;
    // Render thread -> simulation thread: emit draw-identity markers.
    std::atomic<bool> draw_identity_wanted_{false};
    std::atomic<unsigned> bloom_route_{0u};
    // RenderConfig::rt_water this render chunk: water draws join the traced water (rt_water_draw).
    bool rt_water_wanted_ = true;
    std::atomic<bool> rt_shadow_casters_wanted_{false};
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
    // A render chunk began (FIFO streaming cuts frames mid-scene): its first
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
    // FIFO offset of the span the parser is running (run_stream), so marker
    // offsets map to the chunk's FIFO.
    std::size_t parse_span_base_ = 0;
    std::size_t parse_span_prefix_ = 0;
    std::uint64_t motion_category_markers_ = 0;
    std::uint64_t motion_image_effect_markers_ = 0;
    std::uint64_t motion_scene_markers_ = 0;
    std::uint64_t motion_light_markers_ = 0;
    std::uint64_t motion_mirror_markers_ = 0;
    std::uint64_t motion_stripe_marks_ = 0;
    // GameScene::draw2D began (FifoMarker::Scene2DBegin); 2D layers in the report window.
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
    // Root SRV for draws: the palette snapshot, or the matrix ring base in
    // temporal scenes (palettes then addressed by mtx_palette_base).
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
    // Ray tracing: the 3D scene's SceneFunction phase (motion::ScenePhase, from the phase
    // markers), whether the draw state is traceable geometry (unblended, colour- and
    // depth-writing perspective triangles; flush_draw_state) and whether the scene was traced.
    std::uint8_t rt_phase_ = 0xFFu;
    bool rt_state_eligible_ = false;
    // The same, alpha-tested: traced as foliage cards where the material table names foliage.
    bool rt_card_eligible_ = false;
    // Additive blended triangles (glows) under a perspective projection.
    bool rt_glow_eligible_ = false;
    // The glow exposure the PS constants carry: 0 none, 1 sprite, 2 model.
    std::uint8_t glow_kind_ = 0u;
    // Blended scene draws of the translucent phases under ray-traced lighting take its texture in
    // map 7 (output_flags bit 6); the texture binding key marks them in map 7's TEX_MODE0 word.
    bool lit_translucent_ = false;
    static constexpr std::uint32_t kLitTranslucentKey = 0x4C495431u;
    bool rt_water_eligible_ = false;
    bool rt_sprite_eligible_ = false;   // alpha-blended translucent triangles (light sprites, rt_sprite_light_draw)
    bool lit_late_ = false;             // a lit late opaque draw (output_flags bit 7: its light at most 1.15)
    bool rt_scene_traced_ = true;
    bool rt_view_set_ = false;
    // Ambient occlusion (RenderConfig::ambient_occlusion, without traced lighting), sampled at each frame start; the
    // scene's view is set (note_scene_ao_view) and the occlusion drawn (apply_scene_ao).
    bool ambient_occlusion_ = false;
    bool ao_view_set_ = false;
    bool ao_scene_applied_ = true;
    [[nodiscard]] bool rt_geometry_draw() const noexcept;
    [[nodiscard]] bool rt_card_draw() const noexcept;
    [[nodiscard]] bool rt_alpha_draw() const noexcept;
    // Texture map `map` of the bound table as the ray tracer copies it (none for an empty map).
    [[nodiscard]] RtTextureSource rt_texture_source(unsigned map) const noexcept;
    // The loaded stage (note_rt_stage) and its fixed key (aim_rt_scene_lights): decided once per stage.
    std::uint64_t rt_stage_hash_ = 0u;
    std::uint64_t rt_fixed_key_stage_ = ~0ull;
    std::uint32_t rt_fixed_key_scenes_ = 0u;
    bool rt_fixed_key_on_ = false;
    [[nodiscard]] bool rt_glow_draw() const noexcept;
    [[nodiscard]] bool rt_late_draw() const noexcept;
    [[nodiscard]] bool rt_water_draw() const noexcept;
    [[nodiscard]] bool rt_ocean_water_draw() const noexcept;
    [[nodiscard]] bool rt_sprite_light_draw() const noexcept;
    void note_rt_phase(std::uint8_t phase);
    // The view of a perspective scene draw for ambient occlusion: its projection with the upscaler's jitter, depth
    // range and viewport (the ray tracer's RtView).
    [[nodiscard]] RtView scene_depth_view(const ScaledGxViewport& viewport) const;
    void note_scene_ao_view(const DrawCall& call);
    // The scene's occlusion, once, before its first translucent phase (or at its end).
    void apply_scene_ao();
    void trace_rt_scene();
    void trace_rt_water();
    // GALAXY_RT_MATERIALS: the census line of a scene draw (rt_surfaces.h).
    void note_rt_material();
    // A stage was loaded: its name and its row of the sun table (rt_materials.h) for the ray tracer.
    void note_rt_stage();
    // The current draw is a light effect the traced lighting replaces (rt_materials.h hide).
    [[nodiscard]] bool rt_hidden_draw() const noexcept;
    // The draw is a clear surface the engine traces (the table's `traced`): not shown, still captured.
    [[nodiscard]] bool rt_traced_clear_draw() const noexcept;
    // A traced clear solid's draw (the table's `traced solid`): unseen scene geometry, its game draw hidden.
    [[nodiscard]] bool rt_traced_solid_draw() const noexcept;
    // The current object's name (NameObj::mName) into `out`; its guest address, 0 for none.
    std::uint32_t rt_object_name(char* out, std::size_t size);
    [[nodiscard]] static std::uint16_t rt_material_of(std::uint32_t map_mask,
                                                      const std::array<TextureHandle, kMaxTextureMaps>& handles,
                                                      unsigned& map) noexcept;
    std::uint32_t rt_object_ = 0u;   // the last NameObj marker's object
    bool rt_object_fake_shadow_ = false;  // it is one of the game's shadow drawers (rt_fake_shadow_object)
    bool rt_mirror_pass_ = false;         // the game's mirror pass is drawing (motion::PassMarker)
    bool rt_trace_only_ = false;          // trace-only draws (motion::kMarkerTraceOnly): traced, never drawn
    std::unordered_map<std::uint32_t, bool> rt_fake_shadow_cache_;
    bool rt_fake_shadow_object(std::uint32_t object);
    std::uint16_t current_rt_material_ = 0u;  // the draw's material table entry (rt_materials.h)
    // Its parts: the bound textures' entry, the J3D material's (motion::kMarkerMaterial; 0: none) over it, and for an
    // emitter's particles its effect's (motion::kMarkerEffect; 0: none) over the textures'.
    std::uint16_t texture_rt_material_ = 0u;
    std::uint16_t rt_material_identity_ = 0u;
    // The J3D material identity (FNV-1a "model/material") whose shapes draw now, 0 none (GALAXY_RT_LOG_GLOWS).
    std::uint32_t rt_identity_seen_ = 0u;
    std::uint16_t rt_effect_material_ = 0u;
    // The traced draws' identity from frame to frame (RtDrawInfo.light_key): the last object, model shape, emitter or
    // particle marker (a particle by its pool node and lifetime), the J3D material's after it, the draws since.
    std::uint64_t rt_light_object_ = 0u;
    std::uint32_t rt_light_material_ = 0u;
    std::uint32_t rt_light_draws_ = 0u;
    [[nodiscard]] std::uint32_t rt_light_key() noexcept;
    void refresh_rt_material() noexcept {
        current_rt_material_ = rt_material_identity_ != 0u ? rt_material_identity_
                               : rt_effect_material_ != 0u ? rt_effect_material_
                                                           : texture_rt_material_;
    }
    unsigned current_rt_map_ = 0xFFu;  // its texture map (else the first bound one; 0xFF none)
    std::uint32_t rt_draw_buffer_ = 0xFFFFu;   // the last draw buffer marker's type (bit 16: translucent)
    bool rt_sky_buffer_ = false;  // the draws are the game's sky lists' (kRtDrawSky) until the next category marker
    bool rt_sun_buffer_ = false;   // the game's sun list (Sun 3): neither sky nor scene under physical light
    // The draws are the game's own shadows (CategoryList::execute of MR::DrawType ShadowSurface 0x26, ShadowVolume 0x27,
    // AlphaShadow 0x29) until the next category marker: hidden under traced lighting (rt_hidden_draw).
    bool rt_fake_shadow_list_ = false;
    // The texture generator of the first TEV stage sampling texture map `map` (0xFF: none).
    [[nodiscard]] unsigned rt_texgen_of_map(unsigned map) const noexcept;
    // A draw of the game's sky lists: its record for the traced sky (kRtDrawSky).
    void begin_rt_sky_batch(RtDrawInfo& info);
    // The draw's TEV program, registers, colour channels (lit ones at `facing` of their lights) and textures for ray
    // hits (RtDrawInfo.tev); its two texture slots.
    void fill_rt_tev(RtDrawInfo& info, float facing);
    // The program and its state alone (stages, registers, konstants, colour channels, alpha compare).
    void build_rt_tev(RtTevMaterial& tev, RtTevStage* stages, float facing) const;
    // debake_channels (bits 0-3) with each channel's neutral light (bits 8-15, 16-23: rt_tev_neutral_light), cached by
    // the TEV state.
    [[nodiscard]] std::uint32_t debake_state();
    std::unordered_map<std::uint64_t, std::uint32_t> debake_levels_;
    // De-baking by model (apply_debake_model): a model's vertex colours (its CP colour arrays CLR0 / CLR1), sampled the
    // first frame it draws, decide whether they hold baked light (de-baked from the next frame) or paint (kept).
    struct DebakeModel {
        std::uint64_t frame = 0u;   // the frame (frame_index_ + 1) it first drew in
        bool decided = false;
        bool baked = false;
        std::uint8_t level = 0u;    // its brightest vertex's value (0-255): the colour it is de-baked to
        std::uint32_t vertices = 0u;
        std::unordered_set<std::uint32_t> colours;   // distinct RGB8, at most kDebakeModelColours
        // The light baked into its colours as a function of the surface's facing (fit_debake_light): its sampled
        // vertices with a normal (object space) and their colours, at most kDebakeModelSamples.
        std::vector<std::array<float, 6>> samples;
        std::array<float, 27> sh{};      // the fitted light, nine spherical harmonics a channel (r, g, b)
        std::array<float, 3> peak{};     // the fitted light at the most lit sampled facing
        float fit_r2 = 0.0f;             // the share of the colours' luminance variance the facing explains
        bool fitted = false;
    };
    static constexpr std::size_t kDebakeModelSamples = 4096u;
    // Its baked light as a function of the facing (model.sh, peak, fit_r2) from its samples.
    static void fit_debake_light(DebakeModel& model);
    static constexpr std::size_t kDebakeModelColours = 4096u;
    std::unordered_map<std::uint64_t, DebakeModel> debake_models_;
    std::uint32_t debake_models_stage_ = 0u;
    std::uint64_t debake_sample_key_ = 0u;   // the current state's undecided model: its batches are sampled
    std::uint8_t debake_model_level_ = 0u;   // the current state's baked model's level (0: the program's neutral)
    // The current state's model's facing fit (GxVsConstants::debake_sh, debake_peak): the shaders de-bake by it.
    bool debake_fit_active_ = false;
    float debake_fit_sh_[7][4]{};
    float debake_fit_peak_[4]{};
    std::vector<std::uint64_t> debake_model_order_;   // GALAXY_RT_DEBAKE_CYCLE: the baked models in decision order
    // The unlit channels (bits 0-1) of `channels` kept only where the model's colours are baked light.
    [[nodiscard]] std::uint32_t apply_debake_model(std::uint32_t channels);
    // A finished batch's vertex colours into its undecided model's sample.
    void sample_debake_model(std::uint64_t key, const DrawCall& call);
    // Physical ray tracing: the colour channels whose colour carries the art's baked light, in an opaque scene phase,
    // outside the sky lists and the material table's emitters (whose colour is their light): unlit and taken from the
    // vertices (bits 0-1: channel 0, 1), and lit (bits 2-3: their light at full, their vertex colour's shading).
    // GALAXY_RT_DEBAKE=0: none.
    [[nodiscard]] std::uint32_t debake_channels() const;
    // Per object name (its guest address) the class its name gives (rt_named_class), + 1.
    std::unordered_map<std::uint32_t, std::uint8_t> rt_named_;
    // Whether a batch starting with `call` is traced geometry; the scene's first one also gives
    // the camera (projection and viewport) for rays through EFB pixels.
    [[nodiscard]] bool begin_rt_batch(const DrawCall& call, RtDrawInfo& info);
    // Actor lights (actor_lights.h): the open scene's aim and its serial; the scene's key light in
    // world space (direction towards it), how far the lights are moved to it, its source (1 the
    // game's visible sun, 2 the sun its sky shows, 3 a world-fixed light) and, for 2 and 3, its
    // colour (RGBA8); whether the scene is followed (the ray tracer lights it and its camera is
    // known) and that camera; GX lights 0 and 1: those the hooked light loads left (known) and of
    // them those following the camera, those whose loaded position words (rt_light_words_) the next
    // draw takes; the moved lights of the palette snapshot in use and of the flushed draw (serial
    // << 8 | light bits).
    ActorLightAim rt_light_aim_{};
    std::uint32_t rt_light_serial_ = 0;
    std::array<float, 3> rt_light_world_{};
    float rt_light_blend_ = 0.0f;
    std::uint8_t rt_key_source_ = 0;
    std::uint32_t rt_light_key_color_ = 0;
    bool rt_light_tracking_ = false;
    std::array<float, 12> rt_scene_view_{};
    std::uint8_t rt_known_lights_ = 0;
    std::uint8_t rt_follow_lights_ = 0;
    std::uint8_t rt_light_pending_ = 0;
    std::array<std::array<std::uint32_t, 3>, 2> rt_light_words_{};
    std::uint32_t rt_palette_lights_ = 0;
    std::uint32_t rt_draw_lights_ = 0;
    // The open scene's candidate key lights: the world-fixed GX light 0 of its lit draws, by world
    // direction and colour, with the draws using each and the sum of their ambient registers (0-1)
    // over those taking it from the register (traced).
    struct RtWorldKey {
        std::array<float, 3> direction{};
        std::uint32_t color = 0;
        std::uint32_t draws = 0;
        std::array<float, 3> ambient{};
        std::uint32_t ambient_draws = 0;
    };
    std::array<RtWorldKey, 8> rt_world_keys_{};
    std::uint32_t rt_world_key_count_ = 0;
    // The stage's last world key (none: no draws), until a stage load.
    RtWorldKey rt_world_held_{};
    // The game's player light rig as its draws load it (XF words, RGB): ambient, lights 0 and 1; per scene
    // (aim_rt_scene_lights). rt_player_rig_draw: the draw is lit by it.
    std::array<std::uint32_t, 3> rt_player_rig_{};
    bool rt_player_rig_known_ = false;
    [[nodiscard]] bool rt_player_rig_draw() const noexcept;
    void note_rt_world_key() noexcept;
    // A camera cut since the last scene's aim (and the most objects one reported); the game's sun
    // count at the last cut, and whether one arrived since (the ray tracer holds it).
    bool rt_scene_cut_ = false;
    std::uint32_t rt_cut_objects_ = 0;
    std::uint64_t rt_sun_cut_updates_ = 0;
    bool rt_sun_held_ = false;
    // GALAXY_TRACE_LIGHTS: snapshots with moved lights, snapshots added for them, TSC cycles of
    // the actor-light work and its timed regions, and palette snapshots with the cycles of their
    // allocation and copy.
    std::uint64_t rt_light_uploads_ = 0;
    std::uint64_t rt_light_forced_ = 0;
    std::uint64_t rt_light_cycles_ = 0;
    std::uint64_t rt_light_regions_ = 0;
    std::uint64_t rt_snapshot_count_ = 0;
    std::uint64_t rt_snapshot_cycles_ = 0;
    void aim_rt_scene_lights(const std::array<float, 12>* view);
    [[nodiscard]] std::uint32_t rt_aimed_lights() const noexcept;
    void write_rt_lights(std::uint32_t* palette) noexcept;
    bool scene_3d_recorded_ = false;           // this frame's 3D scene took records
    bool mirror_opened_scene_ = false;         // this frame's InterpScene began at its mirror pass
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
    void set_identity_marker(std::uint8_t kind, std::uint16_t aux, std::uint32_t a, std::uint32_t tag);
    // Interpolated redraw (present rate above 60, temporal upscaler off):
    // this render chunk records a replay log and per-record VS constants.
    bool interp_recording_ = false;
    // Render thread: the chunk being rendered made a display (XFB) copy into an XFB a VI has shown, and the copy
    // serial last presented when its chunk ended (present_on_copy_enabled).
    bool xfb_copied_in_chunk_ = false;
    std::uint64_t copy_presented_serial_ = 0u;
    // The guest XFBs the VI has named (the game's display buffers), so a copy elsewhere (a capture) never presents.
    std::array<std::uint32_t, 4> vi_named_xfbs_{};
    std::size_t vi_named_xfb_next_ = 0u;
    [[nodiscard]] bool is_vi_named_xfb(std::uint32_t guest_addr) const noexcept;
    // Sub-frames show between game frames now (interpolation on and its governors allowing at least one).
    [[nodiscard]] bool subframes_shown() const;
    void note_vi_named_xfb(std::uint32_t guest_addr) noexcept;
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
        std::vector<GxVsConstants> constants;   // per record, as uploaded
        std::vector<std::int32_t> prev_index;   // matched record of the previous scene
        std::vector<std::int32_t> palette;      // per record: shadow index or -1
        std::vector<std::uint8_t> per_vertex;   // per-vertex palette draw
        std::vector<std::uint32_t> palettes;    // kMatrixPaletteWords per shadow
        std::size_t palette_count = 0;
        // Pixel-stage constants per upload in the scene (draws carry the index).
        std::vector<GxPsConstants> ps_constants;
        Affine34 camera{};
        // The game camera (world to eye) of the scene.
        Affine34 view{};
        bool view_known = false;
        bool valid = false;
        std::uint64_t serial = 0;               // replay serial of its log (0: none)
        // Per record: draw phase and category (galaxy/motion_signals.h).
        std::vector<std::uint32_t> category;
        // The game's camera epoch (scene marker); a different epoch in the
        // next scene is a hard cut.
        std::uint32_t camera_epoch = 0;
        bool camera_epoch_known = false;
        // Record ranges of the frame's layers (GxBackend::kSceneLayer,
        // kScreenLayer, kMirrorLayer). Every prev_index is in the scene
        // before's index space (GxBackend::end_record_layer).
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
            palette.clear(); per_vertex.clear(); palette_count = 0; valid = false; serial = 0;
            ps_constants.clear();
            category.clear(); camera_epoch_known = false;
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
        std::uint32_t xfb_addr = 0;
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
    float interp_camera_move_ = 0.0f;     // eye movement into the scene before (cut test)
    struct InterpRecordMotion {
        std::uint32_t record = 0;
        std::int32_t prev = -1;           // matched record of the scene before
        std::int32_t palette = -1;        // interp_palette_motions_ index
        EyeMotion position{};             // inline position matrix
        bool position_moves = false;
        bool tex_moves = false;           // inline texture or post matrices
        bool colour_moves = false;        // channel ambient or material colours
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
        std::uint8_t light_mask = 0;      // GX lights its records evaluate
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
    std::string interp_snapshot_path_;
    [[nodiscard]] std::string interp_snapshot_path(const char* suffix) const;
    void run_pending_interp_start();
    InterpSequence interp_sequence_{};
    // A recorded frame whose present arrives as a separate VI present.
    bool interp_log_waiting_ = false;
    std::uint32_t interp_log_xfb_ = 0;
    bool interp_stopping_ = false;        // render thread draining for a stop
    // RendererD3D12::replay_hold_segment() seen at the last render chunk.
    unsigned resident_hold_segment_ = ~0u;
    std::uint64_t interp_held_vertex_blocks_ = 0;   // released while interpolating
    // Present slot clock: next slot and slot length (with VSync, on the display's vblanks when they are timed).
    std::int64_t interp_slot_due_qpc_ = 0;
    std::int64_t interp_slot_period_qpc_ = 0;
    // Presents by the vblanks since the one before (0, 1, 2, 3 or more), against the display's vblank grid, and the
    // range of the time from a present's slot (the moment it shows) to the vblank that shows it.
    std::array<std::uint64_t, 4> interp_vblank_steps_{};
    std::int64_t interp_last_present_vblank_ = 0;
    double interp_slot_to_vblank_min_ms_ = 0.0;
    double interp_slot_to_vblank_max_ms_ = 0.0;
    std::uint64_t interp_slot_to_vblank_samples_ = 0;
    void note_present_vblank(std::int64_t slot);
    // GALAXY_INTERP_TIMELINE=<file>: one line per present while interpolating (pacing checks).
    void note_interp_timeline(std::int64_t slot, double t, bool real);
    // Smoothed game-frame arrival clock (start_interp_sequence).
    std::int64_t interp_arrival_qpc_ = 0;
    std::int64_t interp_frame_period_qpc_ = 0;
    std::uint64_t interp_late_slots_ = 0;   // slots a stall skipped
    std::uint64_t interp_superseded_ = 0;   // frames replaced before their own slot
    void supersede_interp_sequence();
    void start_interp_sequence(std::uint32_t xfb_addr);
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
    // The game thread's time blocked on the render thread (queue space, drains, PE completions, EFB peeks), in QPC
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
    std::vector<std::int32_t> interp_ps_slot_;             // per pixel-constant record
    std::vector<std::int32_t> interp_palette_slot_;        // per interp_palette_motions_ entry
    std::vector<std::int32_t> interp_palette_entry_;       // per palette slot: its entry
    std::vector<std::uint32_t> interp_palette_source_;     // per palette slot: mtx_palette_base
    bool prepare_interp_sequence(const InterpScene& scene, const InterpScene& before_scene, bool before_valid);
    // Pixel-constant records of the playing scene whose colours change
    // (record, matched record of the scene before), and their sub-frame uploads.
    std::vector<std::array<std::uint32_t, 2>> interp_ps_moving_;
    std::uint64_t interp_ps_moving_total_ = 0;   // summed per sub-frame
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
    std::uint64_t interp_present_skipped_ = 0;  // VI presents while a sequence played
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
    float producer_safety_surround_{},render_safety_surround_{};
    std::array<std::uint64_t,512> pointer_latency_bins_{};
    std::uint64_t pointer_response_count_{}, pointer_latency_total_{}, pointer_latency_max_{}, pointer_report_ns_{};
    std::array<std::uint64_t,512> pointer_acquisition_age_bins_{};
    std::uint64_t pointer_acquisition_age_count_{}, pointer_acquisition_age_total_{}, pointer_acquisition_age_max_{};
    void record_pointer_response(const XfbTexture& texture) noexcept;
    void record_presented_xfb_copy(const XfbTexture& texture) noexcept;
    void record_skipped_duplicate_xfb_present(
        const XfbTexture& texture) noexcept;
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
    static void record_monotonic_cadence_sample(
        MonotonicCadenceAccumulator& accumulator) noexcept;
    static void reset_monotonic_cadence(
        MonotonicCadenceAccumulator& accumulator) noexcept;
    static MemorySnapshotStorage* snapshot_storage_for(
        MemorySnapshot& snapshot,
        std::byte* source,
        std::uint32_t size);
    static void copy_memory_snapshot_storage(MemorySnapshot& snapshot);
    static std::uint64_t memory_snapshot_used_bytes(
        const MemorySnapshot& snapshot) noexcept;
    void capture_memory_snapshot(
        GuestMemory* source,
        MemorySnapshot& snapshot,
        const std::vector<GuestMemoryRange>& ranges,
        bool strict_ranges);
    void collect_memory_dependency_ranges(
        const std::byte* fifo_data,
        std::size_t fifo_size,
        GuestMemory* memory,
        std::vector<GuestMemoryRange>& out,
        bool& cache_hit);
    void invalidate_dependency_range_cache(
        const std::vector<GuestWriteRange>& ranges) noexcept;
    std::size_t invalidate_dirty_decoded_packet_run_ranges(
        const std::vector<GuestWriteRange>& ranges) noexcept;
    GxState state_;
    FifoParser parser_;
    std::mutex parser_cache_mutex_;
    // Exclusively owned by the selected snapshot producer (CPU or worker).
    // Renderer compilation/recording must not lock dependency discovery.
    FifoParser dependency_parser_;
    FifoParser event_parser_;
    FramePeEventClassifier pe_event_classifier_;
    VertexLoader vertex_loader_;
    PipelineCache pipeline_cache_;
    TextureCache texture_cache_;
    EfbCopyManager efb_copies_;
    Renderer renderer_;
    // A VI retrace is not a GX command boundary. Preserve only an incomplete
    // top-level command tail and prepend the next capture chunk.
    // (Render-thread state, like everything below it down to the stats.)
    std::vector<std::byte> pending_fifo_;
    GxState event_state_;
    std::vector<std::byte> event_pending_fifo_;
    GxState dependency_state_;
    std::vector<std::byte> dependency_pending_fifo_;

    // Valid only inside render_frame_on_thread (parser callbacks need them).
    GuestMemory* frame_memory_ = nullptr;
    bool frame_memory_regions_disjoint_sorted_ = false;
    const GxServices* frame_services_ = nullptr;
    std::vector<PeEvent>* frame_pe_events_ = nullptr;
    bool frame_pe_callbacks_enabled_ = true;

    // Frame queue: the sim thread is the producer, the render thread the consumer. The producer may
    // run a small bounded number of frames ahead, then blocks on queue_space_cv_. This is the
    // frame-granular analogue of Dolphin's SyncGPU max-distance throttle. fifo_pool_ recycles chunk
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
    std::vector<std::vector<std::byte>> fifo_pool_;
    std::vector<std::vector<GuestMemoryRange>> memory_range_pool_;
    // Dirty-range vectors travel with the chunk; recycled like the FIFO so
    // the producer does not grow a fresh vector every frame.
    std::vector<std::vector<GuestWriteRange>> dirty_range_pool_;
    // Producer-thread scratch for drain_guest_memory_writes.
    std::vector<GuestWriteRange> drain_scratch_ranges_;
    std::vector<MemorySnapshot> memory_snapshot_pool_;
    std::deque<PendingFrameEffects> pending_frame_effects_;
    // Only the render owner accesses this record outside queue_mutex_. All
    // mutations hold that mutex for simulation-thread idle predicates.
    struct PendingPointerDepthCapture {
        std::shared_ptr<PointerDepthCapture> capture;
        std::uint64_t fence_value = 0;
        std::chrono::steady_clock::time_point started{};
    } pending_pointer_depth_capture_;
    void take_attached_pointer_depth_capture(FrameChunk& chunk, std::size_t fifo_size);
    void record_pointer_depth_capture_in_frame(FrameChunk& chunk);
    void take_attached_fifo_markers(FrameChunk& chunk, std::size_t fifo_size) noexcept;
    // `sky_up`: the scene's GxVsConstants::sky_up when `sky_up_known`; `stage` the game's stage (SceneBegin).
    void on_fifo_marker(FifoMarker marker, const std::array<float, 12>* view = nullptr, std::uint32_t sky_up = 0u,
                        bool sky_up_known = false, const SceneStage* stage = nullptr);
    // A stage was loaded (another sky and other lights): the tracer's stage state starts over.
    void begin_rt_stage();
    // Temporal scene: jitter, palette base, transform record and motion
    // fields of a VS constant upload (`upload` is write-combined memory).
    void write_scene_vs_constants(const GxVsConstants& cpu, GxVsConstants& upload);
    // Writes the jittered projection of a perspective draw into `upload`;
    // `jitter_ndc` receives the offset (zero for orthographic draws).
    void apply_scene_jitter(const GxVsConstants& cpu, GxVsConstants& upload,
                            float jitter_ndc[2]);
    void adopt_pointer_depth_fence_if_needed();
    // Asynchronous EFB fields for simulation-thread GXPeekZ/GXPeekARGB, one per channel. A channel
    // answers one peek kind at one draw-sync token (`key`, bound by the simulation thread at the
    // first such peek; the least recently used channel is rebound). Three slots: the render owner
    // fills the slot after the published one, then publishes it. Readers only load the published
    // slot. `published` packs the binding generation (high word) and slot + 1, so a field
    // captured for an earlier binding never answers the new one.
    static constexpr std::uint32_t kAsyncPeekUnbound = 0xFFFFFFFFu;
    struct AsyncPeekFieldState {
        // Mapped readback bytes of each slot (RGBA8, 640x528), written by the
        // render thread before the slot is published.
        std::array<const std::uint8_t*, 3> rgba{};
        // Slot the in-flight field writes and the binding it was captured for; render thread only.
        int pending_slot = -1;
        std::uint32_t pending_generation = 0;
        std::atomic<std::uint32_t> key{kAsyncPeekUnbound};
        std::atomic<std::uint32_t> generation{0};
        std::atomic<std::uint64_t> published{0};
        std::atomic<bool> requested{false};
        std::atomic<std::uint64_t> published_answers{0};
        std::uint64_t last_request = 0;  // simulation thread
        std::uint64_t completed = 0;
    };
    static constexpr std::uint32_t async_peek_key(EfbPeekKind kind, std::uint16_t token) noexcept {
        return (kind == EfbPeekKind::Depth ? 0u : 0x10000u) | token;
    }
    static constexpr EfbPeekKind async_peek_key_kind(std::uint32_t key) noexcept {
        return (key & 0x10000u) == 0u ? EfbPeekKind::Depth : EfbPeekKind::Color;
    }
    [[nodiscard]] std::size_t async_peek_channel(EfbPeekKind kind, std::uint16_t token);
    [[nodiscard]] static bool read_async_peek_field(
        AsyncPeekFieldState& state, std::uint16_t x, std::uint16_t y, EfbPeekKind kind, std::uint32_t& value);
    std::array<AsyncPeekFieldState, Renderer::kAsyncPeekFieldChannels> async_peek_{};
    std::uint64_t async_peek_requests_ = 0;  // simulation thread
    // Render owner: records the fields of channels bound to `token` where the stream reaches it.
    void record_async_peek_fields_at_token(std::uint16_t token);
    FramePeCompletionContract frame_pe_completions_;
    std::size_t render_chunks_in_flight_ = 0;
    std::size_t present_chunks_in_flight_ = 0;
    std::size_t utility_chunks_in_flight_ = 0;
    std::size_t render_cpu_reads_in_flight_ = 0;
    std::size_t render_fifo_effects_in_flight_ = 0;
    // Sim-thread only: dirty guest ranges observed on retraces with no FIFO.
    // They do not need a full render chunk, but the next real render must
    // invalidate texture/display-list caches before parsing.
    std::vector<GuestWriteRange> pending_async_dirty_ranges_;
    struct DependencyRangeCacheEntry {
        bool valid = false;
        std::uint64_t fifo_hash = 0;
        std::uint64_t state_hash = 0;
        GxState::DependencyShapeWords shape_words{};
        std::uint32_t pending_bp_write_mask = 0x00FFFFFFu;
        std::uint64_t last_used = 0;
        std::vector<std::byte> fifo;
        std::vector<GuestMemoryRange> ranges;
        std::vector<GuestMemoryRange> shape_ranges;
        GxState final_state;
    };
    std::array<DependencyRangeCacheEntry, 64> dependency_range_cache_{};
    std::uint64_t dependency_range_cache_tick_ = 0;
    std::unordered_map<
        DependencyDrawRunRangeCacheKey,
        DependencyDrawRunRangeCacheEntry,
        DependencyDrawRunRangeCacheKeyHash>
        dependency_draw_run_range_cache_{};
    std::uint64_t dependency_draw_run_range_cache_tick_ = 0;
    std::size_t dependency_draw_run_range_cache_bytes_ = 0;

    struct DecodedPacketRunCacheKey {
        std::uint64_t cache_token = 0;
        std::size_t packet_run_index = 0;
        PrimitiveClass primitive = PrimitiveClass::Points;
        std::uint8_t vtxfmt = 0;
        std::uint32_t vcd_lo = 0;
        std::uint32_t vcd_hi = 0;
        std::uint32_t vat_a = 0;
        std::uint32_t vat_b = 0;
        std::uint32_t vat_c = 0;
        std::uint32_t matrix_index_a = 0;
        std::uint32_t matrix_index_b = 0;
        std::array<std::uint32_t, 24> array_base_stride{};

        [[nodiscard]] bool operator==(
            const DecodedPacketRunCacheKey&) const = default;
    };

    struct DecodedPacketRunCacheKeyHash {
        [[nodiscard]] std::size_t operator()(
            const DecodedPacketRunCacheKey& key) const noexcept;
    };

    struct DecodedPacketRunCacheEntry {
        // Every indirect vertex-array byte a decode read, merged into sorted, disjoint ranges (the same byte
        // set as the per-attribute reads) whose copies sit back to back in `bytes`.
        struct GuestDependencySnapshots {
            struct Range {
                std::uint32_t guest_base = 0;
                std::uint32_t size = 0;
                std::size_t offset = 0;  // into `bytes`
            };
            std::vector<Range> ranges;
            std::vector<std::byte> bytes;

            [[nodiscard]] bool empty() const noexcept { return ranges.empty(); }
            [[nodiscard]] std::size_t size() const noexcept { return ranges.size(); }
            [[nodiscard]] std::span<const std::byte> range_bytes(
                std::size_t index) const noexcept {
                const Range& range = ranges[index];
                return {bytes.data() + range.offset, range.size};
            }
            void clear() noexcept {
                ranges.clear();
                bytes.clear();
            }
        };

        DecodedPacketRunCacheKey key{};
        std::uint32_t total_indices = 0;
        std::uint64_t last_used = 0;
        std::size_t byte_size = 0;
        std::vector<GxVertexOut> vertices;
        ImmutableUploadToken upload_token;
        // Block of `resident_vertices_` holding a copy of `vertices` that draws
        // reference directly. Valid only while `resident_epoch` equals the
        // arena epoch (0 = no block).
        std::uint64_t resident_epoch = 0;
        std::uint32_t resident_offset = 0;  // in vertices
        std::uint32_t resident_units = 0;
        std::vector<VertexDecodeGuestRange> guest_array_reads;
        // A copy of every byte the decode read, so a missed or coalesced write notification cannot replay
        // stale geometry. A hit compares it once per write generation (decoded_packet_run_generation_),
        // not once per replay.
        GuestDependencySnapshots guest_array_snapshots;
        // The decoded_packet_run_generation_ at which guest_array_snapshots last matched guest memory.
        std::uint64_t validated_generation = 0;
    };
    // Bumped once per batch of guest-write notifications, before the frame's first replay: an entry validated
    // at the current value needs no compare. Never reset to a value an entry holds.
    std::uint64_t decoded_packet_run_generation_ = 1;

    [[nodiscard]] DecodedPacketRunCacheKey decoded_packet_run_cache_key(
        std::uint64_t cache_token,
        std::size_t packet_run_index,
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        const VertexDescriptor& desc) const;
    // Sorts and merges overlapping/adjacent reads (never across a gap, and
    // never into a span that is not one contiguous host mapping), then copies
    // each merged range. `captured_bytes` is the merged byte count, which is
    // what the budget bounds. On failure `snapshots` is empty.
    [[nodiscard]] static bool capture_decoded_packet_run_dependencies(
        GuestMemory* memory,
        const std::vector<VertexDecodeGuestRange>& dependencies,
        std::size_t byte_budget,
        DecodedPacketRunCacheEntry::GuestDependencySnapshots& snapshots,
        std::size_t& captured_bytes,
        bool regions_disjoint_sorted = false);
    // Returns true when the snapshots still describe live guest memory.
    // `entry_generation` is compared against `current_generation`: when they are
    // equal and non-zero, this entry was already validated after the most recent
    // guest write batch and the byte compare is skipped. `new_generation`
    // receives the non-zero generation the caller may record on success.
    [[nodiscard]] static bool decoded_packet_run_dependencies_match(
        GuestMemory* memory,
        std::uint64_t entry_generation,
        std::uint64_t current_generation,
        const DecodedPacketRunCacheEntry::GuestDependencySnapshots& snapshots,
        std::uint64_t& new_generation,
        bool regions_disjoint_sorted = false) noexcept;
    void prune_decoded_packet_run_cache();

    static constexpr std::size_t kDecodedPacketRunCacheMaxBytes =
        64u * 1024u * 1024u;
    static constexpr std::size_t kDecodedPacketRunCacheMaxEntryBytes =
        8u * 1024u * 1024u;
    std::unordered_map<
        DecodedPacketRunCacheKey,
        DecodedPacketRunCacheEntry,
        DecodedPacketRunCacheKeyHash>
        decoded_packet_run_cache_{};
    std::uint64_t decoded_packet_run_cache_tick_ = 0;
    std::size_t decoded_packet_run_cache_bytes_ = 0;
    // GPU-resident decoded vertices (render thread only). One persistently
    // mapped UPLOAD buffer, created lazily per device; see
    // resident_vertex_arena.h for the block retirement rule.
    static constexpr std::uint32_t kResidentDecodedVertexCapacity =
        static_cast<std::uint32_t>(
            kDecodedPacketRunCacheMaxBytes / sizeof(GxVertexOut));
    ResidentVertexArena resident_vertices_;
    UploadBuffer resident_vertex_buffer_;
    std::byte* resident_vertex_mapped_ = nullptr;
    gpu::Address resident_vertex_gpu_ = 0;
    gpu::Device* resident_vertex_device_ = nullptr;
    bool resident_vertex_buffer_failed_ = false;
    std::uint64_t frame_resident_vertex_hits_ = 0;
    std::uint64_t frame_resident_vertex_bytes_ = 0;
    [[nodiscard]] bool ensure_resident_vertex_buffer();
    void release_resident_vertex_buffer() noexcept;
    // Returns the entry's block to the arena (deferred reuse). Every removal or
    // replacement of a decoded_packet_run_cache_ entry must call this.
    void release_decoded_entry_residency(DecodedPacketRunCacheEntry& entry) noexcept;
    // Gives `entry` a resident block holding its vertices; false when out of space or the
    // buffer could not be created (the ring path is used).
    [[nodiscard]] bool make_decoded_entry_resident(DecodedPacketRunCacheEntry& entry);
    // Render thread: decode scratch for runs outside the decoded cache (capacity bounded by
    // kDecodedPacketRunCacheMaxEntryBytes).
    std::vector<GxVertexOut> decoded_packet_vertex_scratch_;
    bool stop_render_thread_ = false;
    std::exception_ptr render_thread_error_;
    std::uint64_t present_epoch_ = 0;
    XfbSerialAccountingState xfb_serial_accounting_{};
    MonotonicCadenceAccumulator xfb_copy_production_cadence_{};
    MonotonicCadenceAccumulator xfb_first_serial_present_cadence_{};
    std::atomic<std::uint64_t> last_presented_xfb_stamp_{
        ~std::uint64_t{0}};
    std::atomic<std::uint64_t> last_presented_xfb_serial_{
        ~std::uint64_t{0}};
    std::atomic<std::uint32_t> last_presented_xfb_addr_{0};
    // Present rates below the game's 60 Hz show the newest frame on their own cadence. A 30 Hz present
    // shows every other frame at full game speed.
    [[nodiscard]] bool skip_for_present_rate();
    std::int64_t next_rate_present_qpc_ = 0;

    // GX texture base registers store physical addresses (MEM1 0x00000000 and
    // MEM2 0x10000000), while the CPU can write through cached/uncached
    // aliases. Dirty notifications are normalized to this physical window.
    static constexpr std::uint32_t kMem1PhysicalBase = 0x00000000u;
    // The native game's memory (docs/NATIVE_PORT.md): MEM1 and MEM2 enlarged for 8-byte pointers; the console's
    // 24 and 64 MB fit inside.
    static constexpr std::uint32_t kMem1Size = 0x04000000u;
    static constexpr std::uint32_t kMem2PhysicalBase = 0x10000000u;
    static constexpr std::uint32_t kMem2Size = 0x10000000u;
    static constexpr std::uint32_t kDirtyTrackedBase = 0x00000000u;
    static constexpr std::uint32_t kDirtyTrackedSize = 0x20000000u;
    // GX copy destinations and tiled rows use 32-byte units. Coarser pages
    // falsely retire GPU-only EFB contents when adjacent heap data is written.
    static constexpr std::uint32_t kDirtyPageShift = 5u;
    static constexpr std::uint32_t kDirtyPageSize = 1u << kDirtyPageShift;
    static constexpr std::uint32_t kDirtyPageCount =
        kDirtyTrackedSize >> kDirtyPageShift;
    static constexpr std::size_t kDirtyPageWordCount =
        (kDirtyPageCount + 63u) / 64u;
    // Two-level bitmap. `dirty_page_words_` holds one bit per 32-byte guest
    // page (1 MiB of bitmap); `dirty_word_summary_` holds one bit per word of
    // that bitmap. Producers set the summary bit whenever they publish a page
    // bit, so end-of-frame draining can skip entire groups of empty words
    // instead of loading every one of the 131,072 bitmap words.
    static constexpr std::size_t kDirtyWordSummaryCount =
        (kDirtyPageWordCount + 63u) / 64u;
    static constexpr std::uint64_t kDirtyFullWord = ~std::uint64_t{0};
    std::array<std::atomic_uint64_t, kDirtyPageWordCount> dirty_page_words_{};
    std::array<std::atomic_uint64_t, kDirtyWordSummaryCount>
        dirty_word_summary_{};

    // Cached per-draw bindings, rebuilt when the matching dirty bit fires.
    PixelShaderKey ps_key_{};
    VertexShaderKey vs_key_{};
    RenderStateKey render_state_key_{};
    std::uint64_t current_vs_hash_{};
    std::uint64_t current_ps_hash_{};
    bool shader_key_hashes_valid_ = false;
    // RenderConfig::per_pixel_lighting, sampled at each frame start.
    bool per_pixel_lighting_ = true;
    // RenderConfig::curved_surfaces where the renderer tessellates, sampled at each frame start; whether the last
    // flushed draw is a curved surface (its keys carry kVsKeyCurved and the patch topology).
    bool curved_surfaces_ = false;
    bool current_curved_ = false;
    // RenderConfig::enhanced_lighting, sampled at each frame start; the last draw's eligibility and
    // material word (enhanced_lighting.h), kept until its keys or eligibility change.
    bool enhanced_lighting_ = false;
    bool enhanced_gate_ = false;
    std::uint32_t enhanced_material_ = 0u;
    // uber_ps_header[3] flags of enhanced pipelines this frame (shader_keys.h kEnhancedRtReflections).
    std::uint32_t enhanced_ps_flags_ = 0u;
    // The material table bits (enhanced_table_material) the last uploaded PS constants carry.
    std::uint32_t enhanced_material_bits_ = 0u;
    std::uint16_t glow_material_ = 0u;  // the material whose glow gain the PS constants hold (output_flags bit 5)
    PsoKey current_pso_key_{};
    bool current_pso_key_valid_ = false;
    gpu::Pipeline* current_pipeline_ = nullptr;
    // Frame-scoped memo of PsoKey -> pipeline resolutions: a frame resolves the same few hundred keys many
    // times. PipelineCache::published_ changes only on this thread (get(), and drain_completions() in
    // begin_frame()), so a result holds for the rest of the frame; cleared at begin_frame(). Direct-mapped
    // on the key hash; a collision falls through to the full resolution.
    static constexpr std::size_t kPipelineMemoSlots = 512u;
    static_assert(
        (kPipelineMemoSlots & (kPipelineMemoSlots - 1u)) == 0u,
        "the memo index masks the low hash bits, so the slot count is a power of two");
    static constexpr std::size_t kPipelineMemoMask = kPipelineMemoSlots - 1u;
    struct PipelineMemoEntry {
        std::uint64_t hash = 0u;
        gpu::Pipeline* pipeline = nullptr;
        PsoKey key{};
    };
    std::array<PipelineMemoEntry, kPipelineMemoSlots> pipeline_memo_{};
    // Hash of `current_pso_key_`, kept beside it so the memo probe never
    // re-derives the fold for a key it already hashed once.
    std::uint64_t current_pso_key_hash_ = 0u;
    gpu::Address current_vs_constants_ = 0;
    VsConstantsCpuCache cpu_vs_constants_{};
    // current_pipeline_ is an uber pipeline standing in for a specialization
    // that is still compiling: resolve the key again on the next draw.
    bool current_pipeline_is_uber_ = false;
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
    // The 32-word chunks of the XF matrix palette already snapshot into this frame slot's matrix ring
    // (cleared on the slot rewind and on session reset). When nothing moved, flush_draw_state reuses
    // current_matrix_palette_; a new allocation still copies the whole palette (GxState::xf_palette_dirty()).
    static constexpr std::uint64_t xf_palette_chunk_mask =
        (std::uint64_t{1}
         << GxState::kMatrixPaletteChunkCount) - 1u;
    static_assert(
        GxState::kMatrixPaletteChunkCount < 64u,
        "palette dirty bitmap is one 64-bit word");
    static_assert(
        GxState::kMatrixPaletteWords % GxState::kMatrixPaletteChunkWords == 0u,
        "palette chunks must tile the palette exactly");
    std::uint64_t xf_palette_valid_ = 0u;
    // Upload/descriptor-ring handles are frame-slot-local. Rebuild them on
    // the first draw after begin_frame(), even when GX register state did not
    // change. EFB copies can also replace a guest-address texture alias
    // without rewriting its BP texture registers.
    bool frame_bindings_dirty_ = true;
    bool texture_bindings_dirty_ = true;
    std::uint8_t current_texture_map_mask_ = 0;
    // sampled_texture_map_mask(state_) reads GenMode, the TEV order, indirect-command and IND_REF registers,
    // all of which set kDirtyTev; invalidated wherever the BP register file resets.
    bool current_texture_map_mask_valid_ = false;
    mutable std::array<VertexLayoutCacheEntry, 8> vertex_layout_cache_{};

    struct TextureBindingKey {
        std::array<std::uint32_t, kMaxTextureMaps * 5u> regs{};
        std::uint8_t map_mask = 0;

        // Field-wise, like TextureBindingKeyHash: the 3 bytes of tail padding after map_mask must not take part.
        [[nodiscard]] bool operator==(const TextureBindingKey& other) const
            noexcept {
            return regs == other.regs && map_mask == other.map_mask;
        }
    };
    struct TextureHandleKey {
        std::uint32_t guest_addr = 0;
        std::uint16_t width = 0;
        std::uint16_t height = 0;
        std::uint8_t format = 0;
        std::uint8_t tlut_format = 0;
        std::uint16_t tlut_offset = 0;
        std::uint8_t levels = 1;
        std::uint8_t generated_mips = 0;

        [[nodiscard]] bool operator==(const TextureHandleKey&) const =
            default;
    };
    [[nodiscard]] TextureBindingKey capture_texture_binding_key(
        std::uint8_t map_mask) const;
    [[nodiscard]] TextureHandleKey texture_handle_key(
        const TexImage& image,
        const TexMode& mode,
        const TlutRef& tlut) const;
    [[nodiscard]] std::array<std::uint32_t, kMaxTextureMaps> capture_sampler_keys(
        std::uint8_t map_mask,
        const std::array<TextureHandle, kMaxTextureMaps>& handles) const;
    void mark_texture_binding_alias_dirty(const EfbCopyParams& params);
    void prune_dirty_texture_binding_tables();
    void clear_frame_texture_binding_table_cache();
    void clear_texture_binding_table_cache();
    TextureBindingKey current_texture_binding_key_{};
    bool current_texture_binding_key_valid_ = false;
    // Sampled map mask that produced current_texture_binding_key_. A map-mask
    // change alone must force a new capture even when no texture BP register
    // moved, so the key stays an exact function of its inputs.
    std::uint8_t current_texture_binding_key_mask_ = 0;

    struct TextureBindingDependency {
        std::uint32_t guest_addr = 0;
        std::uint16_t width = 0;
        std::uint16_t height = 0;
        std::uint8_t format = 0;
        // Exact decoded mip-chain or strided EFB alias footprint.
        std::uint32_t guest_byte_size = 0;

        [[nodiscard]] bool operator==(
            const TextureBindingDependency&) const = default;
    };
    void invalidate_texture_handle_cache_dependency(
        const TextureBindingDependency& dependency);
    void invalidate_texture_binding_cache_range(
        std::uint32_t guest_addr,
        std::uint32_t size);
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
    // Layout guards for both keys, tail padding included; the padding is why they compare field-wise rather
    // than with key_bytes_equal like the padding-free shader keys.
    static_assert(sizeof(TextureBindingKey) ==
                  kMaxTextureMaps * 5u * sizeof(std::uint32_t) + 4u);
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
            mix(key.guest_addr);
            mix(key.width);
            mix(key.height);
            mix(key.format);
            mix(key.tlut_format);
            mix(key.tlut_offset);
            mix(key.levels);
            mix(key.generated_mips);
            return static_cast<std::size_t>(hash);
        }
    };
    struct TextureBindingKeyHash {
        [[nodiscard]] std::size_t operator()(
            const TextureBindingKey& key) const noexcept {
            std::uint64_t hash = mix64(kMix64OffsetBasis, key.map_mask);
            for (const std::uint32_t reg : key.regs) {
                hash = mix64(hash, reg);
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
    std::vector<TextureCache::GuestRange> dirty_texture_ranges_scratch_;

    std::uint64_t frame_index_ = 0;
    // frame_index_ published once per frame for the producer's sampling decision; it orders nothing.
    std::atomic<std::uint64_t> timing_frame_index_{0};
    cadence::Session* cadence_diagnostics_ = nullptr;
    bool initialized_ = false;
    // The per-draw clock reads run this frame: on stall-traced frames and on frames sampled by
    // GALAXY_GX_FRAME_TIMING_SAMPLE.
    bool frame_time_detail_ = false;
    bool frame_time_sample_ = false;
    // render_start of the last emitted [gx-frame-timing] line, for frame-interval-us.
    std::chrono::steady_clock::time_point last_frame_timing_emit_{};
    bool last_frame_timing_emit_valid_ = false;
    // One-time EFB seed clear; GX clears only through the copy command's clear bit (on_efb_copy).
    bool efb_seeded_ = false;

    // The last display copy came from the output EFB and nothing has drawn or cleared since: an EFB-to-texture
    // copy now takes that picture (RendererD3D12::copy_output_efb_to_texture), not the render EFB's scene.
    bool display_copy_on_output_ = false;

    // Bring-up stats, reported every 300 frames.
    std::uint64_t stat_draws_issued_ = 0;
    std::uint64_t stat_draws_no_pso_ = 0;
    std::uint64_t frame_draws_issued_ = 0;
    std::uint64_t frame_draw_batches_ = 0;
    std::uint64_t frame_draws_no_pso_ = 0;
    std::uint64_t frame_xfb_copies_ = 0;
    std::uint64_t frame_tex_copies_ = 0;
    std::uint64_t frame_efb_alias_updates_ = 0;
    std::uint64_t frame_efb_alias_noops_ = 0;
    std::uint64_t frame_texture_binding_cache_hits_ = 0;
    std::uint64_t frame_texture_binding_cache_misses_ = 0;
    std::uint64_t frame_texture_binding_cache_prunes_ = 0;
    std::uint64_t frame_nontri_skips_ = 0;
    std::uint64_t frame_scissor_skips_ = 0;
    std::uint64_t frame_flush_us_ = 0;
    std::uint64_t frame_flush_pso_us_ = 0;
    // PSO-resolution thread cycles this frame; valid only when every resolution was measured.
    std::uint64_t frame_flush_pso_cycles_ = 0;
    bool frame_flush_pso_cycles_valid_ = false;
    bool pso_cycles_enabled = false;
    // Sampled wall time of whole flush_draw_state calls (ScopedFlushTiming) and the sample count; unlike
    // frame_flush_us_ it needs no per-draw instrumentation.
    std::uint64_t frame_flush_total_us_ = 0;
    std::uint64_t frame_flush_timed_samples_ = 0;
    std::uint64_t frame_flush_timed_calls_ = 0;
    // Sub-slices of frame_flush_pso_us_: memo hits (no PipelineCache call), the PipelineCache::get() call,
    // and the PsoKey hash and compare work.
    std::uint64_t frame_pso_memo_hits_ = 0;
    std::uint64_t frame_pso_get_calls_ = 0;
    std::uint64_t frame_pso_get_us_ = 0;
    std::uint64_t frame_pso_get_max_us_ = 0;
    // Cached display-list replay path per batch: the parser offers the two batched paths before per-packet
    // replay.
    std::uint64_t frame_replay_prepared_batches_ = 0;
    std::uint64_t frame_replay_packet_batches_ = 0;
    std::uint64_t frame_replay_simple_batches_ = 0;
    std::uint64_t frame_replay_generic_draws_ = 0;
    std::uint64_t frame_flush_matrix_us_ = 0;
    // Bytes written to the matrix upload ring this frame, always counted.
    std::uint64_t frame_matrix_upload_bytes_ = 0;
    // Per-frame deltas of PipelineCache::CompileStats against frame_compile_stats_baseline_: driver builds
    // (pso-compiles), builds from the D3D12 pipeline library (pso-library-loads), FXC-generated shader pairs
    // and pairs whose DXBC came from disk.
    std::uint64_t frame_pso_compiles_ = 0;
    std::uint64_t frame_pso_library_loads_ = 0;
    std::uint64_t frame_shader_pair_compiles_ = 0;
    // FXC microseconds this frame.
    std::uint64_t frame_blob_compile_us_ = 0;
    std::uint64_t frame_blobs_from_cache_ = 0;
    // Baseline for the deltas above, captured in the per-frame reset block.
    PipelineCache::CompileStats frame_compile_stats_baseline_{};
    std::uint64_t frame_flush_constants_us_ = 0;
    std::uint64_t frame_flush_texture_us_ = 0;
    std::uint64_t frame_vertex_load_us_ = 0;
    std::uint64_t frame_cached_vertex_validation_us_ = 0;
    std::uint64_t frame_texture_decode_join_us_ = 0;
    std::uint64_t frame_cached_vertex_upload_us_ = 0;
    std::uint64_t frame_draw_record_us_ = 0;
    std::uint64_t frame_efb_copy_us_ = 0;
    std::uint64_t frame_decoded_vertex_cache_hits_ = 0;
    std::uint64_t frame_decoded_vertex_cache_misses_ = 0;
    std::uint64_t frame_decoded_vertex_cache_evictions_ = 0;
    std::uint64_t frame_decoded_vertex_cache_stale_rejects_ = 0;
    // Always-counted structural counters (the timing fields above need frame_time_detail_). The memo and
    // get() routes are frame_pso_memo_hits_ and frame_pso_get_calls_.
    std::uint64_t frame_flush_calls_ = 0;
    std::uint64_t frame_pso_resolutions_ = 0;
    std::uint64_t frame_pso_route_early_ = 0;
    PendingDrawBatch pending_draw_batch_{};
    bool cached_draw_run_active_ = false;
    bool cached_draw_run_scissor_reject_ = false;
    bool cached_draw_run_primitive_indexed_ = false;
    bool cached_draw_run_no_pso_ = false;
    bool cached_draw_run_batch_compatible_known_ = false;
    PrimitiveClass cached_draw_run_primitive_ = PrimitiveClass::Points;
    std::uint8_t cached_draw_run_vtxfmt_ = 0;
    DrawCall cached_draw_run_state_call_{};
    std::vector<std::byte> cached_simple_draw_scratch_;
    FifoParserProfile frame_fifo_profile_{};
    std::atomic<std::uint64_t> stat_xfb_copies_{0};
    std::unique_ptr<galaxy::telemetry::Publisher> frame_telemetry_;
    std::atomic<std::uint64_t> stat_xfb_present_requests_{0};
    std::atomic<std::uint64_t> stat_xfb_present_presented_{0};
    std::atomic<std::uint64_t> stat_xfb_first_serial_presentations_{0};
    std::atomic<std::uint64_t> stat_xfb_present_misses_{0};
    std::atomic<std::uint64_t> stat_xfb_present_stale_fallbacks_{0};
    std::atomic<std::uint64_t> stat_xfb_present_missing_fallbacks_{0};
    std::atomic<std::uint64_t> stat_xfb_present_stale_preserved_{0};
    std::atomic<std::uint64_t> stat_xfb_present_missing_preserved_{0};
    std::atomic<std::uint64_t>
        stat_xfb_repeated_or_stale_serial_requests_{0};
    std::atomic<std::uint64_t> stat_xfb_present_skipped_duplicate_presents_{0};
    std::atomic<std::uint64_t> stat_xfb_present_duplicate_run_max_{0};
    std::atomic<std::uint64_t> stat_xfb_out_of_order_serial_requests_{0};
    std::atomic<std::uint64_t> stat_xfb_present_serial_delta_zero_{0};
    std::atomic<std::uint64_t> stat_xfb_present_serial_delta_one_{0};
    std::atomic<std::uint64_t> stat_xfb_present_serial_delta_multi_{0};
    std::atomic<std::uint64_t>
        stat_xfb_highest_first_presented_copy_serial_{0};

};

}  // namespace galaxy::gx
