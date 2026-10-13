#pragma once

// GxBackend: the orchestrator. Owns every subsystem and implements the per-frame loop behind
// the frozen galaxy::gx public API (gx_d3d12.h).
//
// The SIM thread calls render_frame() once per VI retrace. It only snapshots the frame's FIFO
// bytes into a bounded queue (depth 1 plus one in flight, so at most one frame of latency) and
// returns. A dedicated RENDER thread consumes chunks in order and runs the pipeline:
//
//   render_frame_on_thread(chunk)            [render thread]
//     -> renderer.begin_frame()           (drains PSO completions first)
//     -> pipeline_cache.drain_completions()
//     -> parser.run(fifo, ...)            (this object is the FifoSink)
//          register loads  -> state_
//          on_draw         -> flush dirty state to keys/constants,
//                             vertex_loader into the rings,
//                             texture_cache lookups, renderer.draw()
//          on_efb_copy     -> renderer copy to XFB / texture, copy-clear
//          on_pe_*         -> collect game-visible FINISH/TOKEN events
//     -> renderer.present(efb_copies.latest())
//     -> renderer.end_frame() returns the successful submission fence
//     -> bind collected events to that exact native queue fence
//
// Back-pressure: a full queue blocks the producer after the configured one to three retrace
// chunks. There is no PowerPC exception or CP breakpoint emulation. The AOT cadence (one chunk
// per retrace, parsed in submission order) is the synchronization. Guest-memory reads during
// the parse (textures, vertex arrays) run a bounded number of frames behind the simulation,
// the same window real GP hardware has while the game respects GXDrawDone.
//
// A render-thread failure is stashed and rethrown from the next sim-thread render_frame()
// call. It is a hard failure, never a silently frozen display.
//
// The legacy entry points in gx_d3d12.h are a thin shim over one GxBackend instance, so
// native_runtime.cpp is unchanged.

#include "galaxy/gx/efb_copy.h"
#include "galaxy/gx/fifo_parser.h"
#include "galaxy/gx/gx_state.h"
#include "galaxy/gx/pipeline_cache.h"
#include "galaxy/gx/renderer_d3d12.h"
#include "galaxy/gx/resident_vertex_arena.h"
#include "galaxy/gx/shader_keys.h"
#include "galaxy/gx/texture_cache.h"
#include "galaxy/gx/transform_history.h"
#include "galaxy/gx/uber_constants.h"
#include "galaxy/gx/vs_constants.h"
#include "galaxy/gx/vertex_loader.h"
#include "galaxy/gx_d3d12.h"
#include "galaxy/native_api.h"
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
#include <vector>

namespace galaxy::gx {
class DependencyEventSink;

struct GuestMemoryRange {
    std::uint32_t guest_base = 0;
    std::uint32_t size = 0;
};

class GxBackendMemorySnapshotTestAccess;

// Exact guest-visible PE event identity emitted by the lightweight FIFO preclassifier. It is
// not a byte-pattern scan. The classifier uses FifoParser, so partial commands, draw payloads,
// BP masks and CALL_DL recursion have the same command-boundary semantics as the renderer.
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
    // event_only selects the parser's classify-only mode. With GALAXY_PE_CLASSIFY_VERIFY=1, a
    // full-mode classifier runs on the same bytes and every difference is reported.
    explicit FramePeEventClassifier(bool event_only = true);
    void reset();
    void classify(
        const std::byte* fifo_data,
        std::size_t fifo_size,
        GuestMemoryV1* memory,
        std::vector<FramePeEventSignature>& events,
        bool frozen_regions = false);
    std::size_t invalidate_display_list_cache_range(
        std::uint32_t guest_addr,
        std::uint32_t size);

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
    bool event_only_ = true;
    std::unique_ptr<FramePeEventClassifier> verifier_;
    std::vector<FramePeEventSignature> verifier_events_;
    std::uint64_t verified_chunks_ = 0;
    std::uint64_t verify_mismatches_ = 0;
    std::vector<std::byte> pending_fifo_;
    std::vector<FramePeEventSignature>* active_events_ = nullptr;
    // Memo for draw_payload_size(). The parser calls it once per draw to check that the whole
    // payload is present before on_draw, and this classifier repeats that pass over the same
    // stream. The vertex layout is fully described by CP VCD_LO/VCD_HI and the per-format VAT
    // A/B/C registers. This sink shares the parser's GxState, so comparing those five words is an
    // exact test of whether the remembered stride still applies.
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

[[nodiscard]] std::uint32_t pack_sampler_key(
    const TexMode& mode,
    bool generated_mips,
    std::uint8_t mip_levels) noexcept;

// FNV-1a step over one 64-bit value. It consumes a whole word per step instead of one byte.
// The byte-wise form ran 8 xor-multiply rounds per 8-byte value, and the binding table hashes
// about 41 such values per call. These hashes only feed unordered_map buckets; each key's
// operator== decides identity. The caller seeds the accumulator with kFnvOffsetBasis. mix64
// never inspects the accumulator, so a zero value in the middle of a key cannot restart the chain.
inline constexpr std::uint64_t kMix64OffsetBasis = 1469598103934665603ull;
inline constexpr std::uint64_t kMix64Prime = 1099511628211ull;

[[nodiscard]] inline std::uint64_t mix64(std::uint64_t hash, std::uint64_t value) noexcept {
    return (hash ^ value) * kMix64Prime;
}

class GxBackend final : public FifoSink {
public:
    GxBackend() = default;
    GxBackend(const GxBackend&) = delete;
    GxBackend& operator=(const GxBackend&) = delete;
    ~GxBackend() noexcept override;

    // --- Frozen public surface (mirrors gx_d3d12.h) -------------------------

    bool initialize(int width, int height);
    [[nodiscard]] FramePeCompletionToken render_frame(
        const std::byte* fifo_data,
        std::size_t fifo_size,
        GuestMemoryV1* memory,
        const NativeServicesV1* services,
        bool present_swap_chain = true,
        std::uint32_t displayed_xfb_addr = 0);
    void present_cached_xfb(
        std::uint32_t displayed_xfb_addr,
        bool present_swap_chain = true);
    void wait_for_render_idle();
    void wait_for_render_fifo_effects_idle();
    void wait_for_frame_pe_completion(FramePeCompletionToken token);
    [[nodiscard]] bool wait_for_frame_pe_completion_for(
        FramePeCompletionToken token,
        std::uint32_t timeout_ms);
    [[nodiscard]] bool render_queue_has_space();
    [[nodiscard]] bool capture_pointer_depth(std::span<std::uint32_t> pixels);
    [[nodiscard]] bool enqueue_pointer_depth_capture(const std::shared_ptr<PointerDepthCapture>& capture);
    // Simulation thread: the next render_frame submission records this pointer depth field when its
    // parser reaches `fifo_offset` bytes into the submitted FIFO (the offset of the current gather
    // tail at the scene boundary). The render thread owns the capture until it publishes.
    void attach_pointer_depth_capture(
        const std::shared_ptr<PointerDepthCapture>& capture, std::size_t fifo_offset);
    // Simulation thread: the next render_frame submission handles `marker`
    // when its parser reaches `fifo_offset` bytes into the submitted FIFO.
    void attach_fifo_marker(FifoMarker marker, std::size_t fifo_offset) noexcept {
        if (producer_fifo_marker_count_ < producer_fifo_markers_.size()) {
            const std::uint8_t index = producer_fifo_marker_count_++;
            producer_fifo_markers_[index] = {fifo_offset, marker};
            producer_fifo_marker_views_[index] = producer_scene_view_;
            if (producer_scene_view_valid_ && marker == FifoMarker::SceneBegin) {
                producer_fifo_marker_view_mask_ |= static_cast<std::uint8_t>(1u << index);
            } else {
                producer_fifo_marker_view_mask_ &= static_cast<std::uint8_t>(~(1u << index));
            }
        }
        producer_scene_view_valid_ = false;
    }
    // Simulation thread: the game camera for the next SceneBegin marker.
    void attach_scene_view(const float view[12]) noexcept {
        for (unsigned i = 0; i < 12u; ++i) producer_scene_view_[i] = view[i];
        producer_scene_view_valid_ = true;
    }
    void set_prerecorded_movie_content(bool movie) noexcept { producer_movie_content_=movie; }
    void set_safety_scene_surround(float intensity) noexcept { producer_safety_surround_=intensity; }
    void set_pointer_response(PointerResponseStamp stamp) noexcept {
        if (stamp.generation==producer_pointer_response_.generation &&
            stamp.x_bits==producer_pointer_response_.x_bits &&
            stamp.y_bits==producer_pointer_response_.y_bits) return;
        producer_pointer_response_=stamp;
    }
    [[nodiscard]] bool peek_efb(
        std::uint16_t x,
        std::uint16_t y,
        EfbPeekKind kind,
        std::uint32_t& value);
    // Simulation thread: answers a depth or color peek from the latest published asynchronous EFB
    // field of that kind. The request is recorded so the render thread keeps producing fields
    // while peeks continue.
    [[nodiscard]] bool peek_efb_published(
        std::uint16_t x,
        std::uint16_t y,
        EfbPeekKind kind,
        std::uint32_t& value);
    void scan_pe_events_on_sim_thread(
        const std::byte* fifo_data,
        std::size_t fifo_size,
        GuestMemoryV1* memory,
        const NativeServicesV1* services);
    void install_guest_dirty_tracker(GuestMemoryV1* memory) noexcept;
    void notify_guest_memory_write(
        std::uint32_t address,
        std::uint32_t size) noexcept;
    void pump_messages();
    [[nodiscard]] bool quit_requested() const noexcept;
    void shutdown();
    [[nodiscard]] std::uint64_t xfb_copy_count() const noexcept;
    [[nodiscard]] std::uint64_t xfb_causal_trace_frame_index() const noexcept;
    [[nodiscard]] XfbPresentStats xfb_present_stats() const noexcept;
    [[nodiscard]] XfbMeasurementCadenceStats
    xfb_measurement_cadence_stats() const noexcept;
    void reset_xfb_measurement_window_stats() noexcept;

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

    // --- FifoSink -----------------------------------------------------------

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

    // --- Per-draw state flush ------------------------------------------------

    // Consumes state_ dirty bits: rebuilds shader/render-state keys, writes
    // GxVsConstants/GxPsConstants into the constant ring, snapshots the XF
    // matrix palette into the matrix ring, and refreshes the texture table
    // for the enabled TEV maps.  Returns the resolved pipeline for the draw.
    ID3D12PipelineState* flush_draw_state(
        D3D12_PRIMITIVE_TOPOLOGY_TYPE primitive_topology_type);

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
    };

    [[nodiscard]] static bool draw_calls_batch_compatible(
        const DrawCall& lhs,
        const DrawCall& rhs,
        bool lhs_indexed,
        bool rhs_indexed) noexcept;
    void flush_pending_draw_batch();
    // Routine, clock-free shader and PSO cache-coverage report. Prints only when the on-disk
    // coverage changed. Called once per frame from the end-of-frame cache-persistence block.
    void ReportPipelineCoverage();
    std::size_t last_reported_shader_records_ = 0;
    std::size_t last_reported_pso_records_ = 0;
    void on_cached_draw_run_draw(
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        FifoCursor& cursor);

    // --- Render thread --------------------------------------------------------

    // One queued frame: a snapshot of the FIFO bytes the sim produced for this retrace, plus the
    // memory and services pointers the parser callbacks need. When memory_snapshot_active is true,
    // memory points at memory_snapshot, so delayed parsing sees the dynamic arrays and textures that
    // existed when the sim submitted the frame.
    struct MemorySnapshotStorage {
        std::byte* source = nullptr;
        std::uint32_t size = 0;
        bool used = false;
        bool copied = false;
        std::vector<std::byte> bytes;
    };

    struct MemorySnapshot {
        GuestMemoryV1 memory{};
        std::vector<GuestMemoryRegionV1> regions;
        std::vector<MemorySnapshotStorage> storage;
        // Rebuilt lazily for this capture only. Entries are slot indices, so
        // storage-vector growth and queue moves cannot leave dangling keys.
        std::vector<std::size_t> storage_lookup;
        std::size_t storage_used_count = 0;
        bool storage_lookup_active = false;
        // Production-gated compact snapshots can share immutable range resources across frames. A dirty
        // range gets a new resource, so an older queued chunk never aliases mutable guest bytes.
        std::vector<std::shared_ptr<std::vector<std::byte>>> immutable_ranges;
        std::uint64_t copied_bytes = 0;
        std::uint64_t reused_bytes = 0;
        bool immutable_range_ownership = false;
        // True only after every requested byte has been copied and every
        // callback/source pointer into live guest state has been detached.
        // Region host pointers then refer exclusively to `storage` bytes.
        bool sealed = false;
        // Certificate for these exact, frozen region bytes; never implies
        // that generic/live guest views have sorted or disjoint mappings.
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

    struct FrameChunk {
        PointerResponseStamp pointer_response{};
        bool prerecorded_movie{};
        float safety_surround{};
        std::vector<std::byte> fifo;
        std::vector<GuestWriteRange> dirty_ranges;
        std::vector<GuestMemoryRange> memory_ranges;
        std::vector<PeEvent> pe_events;
        std::vector<FramePeEventSignature> preclassified_pe_events;
        GuestMemoryV1* memory = nullptr;
        const NativeServicesV1* services = nullptr;
        FramePeCompletionToken pe_completion_token{};
        // Shared with the blocked producer until the worker has classified
        // sealed memory and issued the exact receipt. queue_mutex_ publishes
        // it before releasing cpu_reads_pending, including on queue moves.
        std::shared_ptr<FramePeCompletionToken> worker_submission_token;
        std::uint64_t producer_wait_ns = 0;
        std::uint64_t pe_classification_ns = 0;
        std::uint32_t displayed_xfb_addr = 0;
        bool present_swap_chain = true;
        bool present_only = false;
        bool memory_snapshot_active = false;
        // Opt-in worker seals owned dependencies while the CPU owner waits,
        // then releases live reads before GPU command recording starts.
        bool worker_memory_snapshot = false;
        bool memory_snapshot_strict = false;
        bool immutable_range_ownership = false;
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
        // Scene boundaries in this chunk's FIFO, in stream order.
        std::array<std::pair<std::size_t, FifoMarker>, 4> fifo_markers{};
        std::uint8_t fifo_marker_count = 0;
        // Game camera (world to eye) per SceneBegin marker; bit i of the
        // mask marks marker i's view valid.
        std::array<std::array<float, 12>, 4> fifo_marker_views{};
        std::uint8_t fifo_marker_view_mask = 0;
        std::uint64_t dependency_scan_ns = 0;
        std::uint64_t snapshot_ns = 0;
        std::uint64_t snapshot_bytes = 0;
        std::uint64_t snapshot_reused_bytes = 0;
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
        const NativeServicesV1* services = nullptr;
        std::vector<PeEvent> pe_events;
    };

    void drain_guest_memory_writes(std::vector<GuestWriteRange>& out) noexcept;
    static void coalesce_dirty_ranges(
        std::vector<GuestWriteRange>& ranges) noexcept;
    static std::uint64_t dirty_range_bytes(
        const std::vector<GuestWriteRange>& ranges) noexcept;
    std::size_t invalidate_dirty_texture_ranges(
        const std::vector<GuestWriteRange>& ranges);
    static std::size_t invalidate_dirty_display_list_ranges(
        FifoParser& parser,
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
    void prepare_worker_memory_snapshot(FrameChunk& chunk);
    void mark_frame_cpu_reads_done(FrameChunk& chunk);
    void mark_frame_fifo_effects_done(FrameChunk& chunk);
    void wait_for_render_cpu_reads_idle();
    void queue_frame_fifo_effects_fence(
        FrameChunk& chunk,
        std::uint64_t fence_value);
    void poll_completed_frame_effects();
    static void deliver_pe_event(
        const NativeServicesV1* services,
        const PeEvent& event);
    void present_cached_xfb_on_thread(
        std::uint32_t displayed_xfb_addr,
        bool sleep_if_missing,
        bool count_game_xfb_present = true);
    void present_latest_frame_on_thread();
    PointerResponseStamp producer_pointer_response_{}, render_pointer_response_{}, last_response_{};
    std::shared_ptr<PointerDepthCapture> producer_pointer_depth_capture_;
    std::size_t producer_pointer_depth_capture_offset_ = 0;
    std::array<std::pair<std::size_t, FifoMarker>, 4> producer_fifo_markers_{};
    std::uint8_t producer_fifo_marker_count_ = 0;
    std::array<std::array<float, 12>, 4> producer_fifo_marker_views_{};
    std::uint8_t producer_fifo_marker_view_mask_ = 0;
    std::array<float, 12> producer_scene_view_{};
    bool producer_scene_view_valid_ = false;
    // Render thread: inside GameScene::draw3D (between the scene markers).
    bool scene_phase_ = false;
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
    D3D12_GPU_VIRTUAL_ADDRESS draw_matrix_palette_ = 0;
    D3D12_GPU_VIRTUAL_ADDRESS temporal_vs_palette_va_ = 0;
    TransformHistory transform_history_;
    // The scene records transforms (temporal upscaling or interpolation).
    bool history_scene_ = false;
    // Interpolated redraw (present rate above 60, temporal upscaler off):
    // this render chunk records a replay log and per-record VS constants.
    bool interp_recording_ = false;
    unsigned interp_frames_ = 0;          // interpolated frames per game frame
    unsigned interp_scenes_in_chunk_ = 0;
    std::int32_t current_replay_record_ = -1;
    std::int32_t current_palette_shadow_ = -1;
    // One history scene's interpolation inputs, kept for three scenes: the
    // one recording, the one being interpolated (its log completed), and its
    // predecessor (previous palettes and texture matrices).
    struct InterpScene {
        std::vector<GxVsConstants> constants;   // per record, as uploaded
        std::vector<Affine34> from;             // previous transform (inline)
        std::vector<Affine34> current;          // current inline transform
        std::vector<std::int32_t> prev_index;   // matched record of the previous scene
        std::vector<std::int32_t> palette;      // per record: shadow index or -1
        std::vector<std::uint8_t> per_vertex;   // per-vertex palette draw
        std::vector<std::uint32_t> palettes;    // kMatrixPaletteWords per shadow
        std::size_t palette_count = 0;
        Affine34 camera{};
        bool valid = false;
        void clear() noexcept {
            constants.clear(); from.clear(); current.clear(); prev_index.clear();
            palette.clear(); per_vertex.clear(); palette_count = 0; valid = false;
        }
    };
    // Recording, ready (log completed), playing, and the playing scene's
    // predecessor (previous palettes and texture matrices).
    std::array<InterpScene, 4> interp_scenes_{};
    unsigned interp_scene_ = 0;           // scene being recorded
    unsigned interp_log_scene_ = 0;       // scene of the completed log
    std::uint64_t interp_seen_serial_ = 0;
    std::uint64_t interp_started_serial_ = 0;
    void note_interp_log_completion();
    struct InterpSequence {
        bool active = false;
        std::uint32_t xfb_addr = 0;
        unsigned subframes = 0;
        unsigned next = 0;
        std::int64_t due_qpc = 0;
        std::int64_t step_qpc = 0;
        // Set inside a render chunk (no frame may be recorded there): the
        // render loop presents the real frame before anything else.
        bool flush_requested = false;
        unsigned scene = 0;               // interp_scenes_ index being interpolated
    };
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
    void start_interp_sequence(std::uint32_t xfb_addr);
    std::vector<D3D12_GPU_VIRTUAL_ADDRESS> interp_record_vas_;
    std::uint64_t interp_presented_ = 0;
    std::uint64_t interp_dropped_ = 0;
    std::uint64_t interp_failed_ = 0;     // sub-frames that could not be recorded
    double interp_cpu_ms_ = 0.0;
    std::int64_t interp_stats_qpc_ = 0;
    // Render thread: steps the interpolation sequence of the latest frame
    // (one sub-frame or the real frame when due; `flush` presents the real
    // frame now and drops the remaining sub-frames).
    void advance_interp_sequence(bool flush);
    bool render_interp_subframe(float t);
    [[nodiscard]] std::int64_t interp_wait_ms() const;
    std::uint64_t motion_trace_scenes_ = 0;
    bool scene_camera_captured_ = false;
    float scene_jitter_px_[2]{};
    std::uint64_t scene_markers_seen_ = 0;
    bool producer_movie_content_{},render_movie_content_{};
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
    [[nodiscard]] static XfbMonotonicCadenceStats
    snapshot_monotonic_cadence(
        const MonotonicCadenceAccumulator& accumulator) noexcept;
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
        GuestMemoryV1* source,
        MemorySnapshot& snapshot,
        const std::vector<GuestMemoryRange>& ranges,
        bool strict_ranges,
        bool immutable_range_ownership = false);
    struct ImmutableRangeCacheEntry {
        std::uint32_t guest_base = 0;
        std::uint32_t size = 0;
        const std::byte* source_identity = nullptr;
        std::shared_ptr<std::vector<std::byte>> bytes;
        std::uint64_t last_used = 0;
        bool valid = false;
    };
    std::shared_ptr<std::vector<std::byte>> acquire_immutable_range(
        std::uint32_t guest_base,
        const std::byte* source,
        std::uint32_t size,
        bool& reused);
    void invalidate_immutable_range_cache(
        const std::vector<GuestWriteRange>& ranges) noexcept;
    void prune_immutable_range_cache(std::size_t incoming_bytes);
    void collect_memory_dependency_ranges(
        const std::byte* fifo_data,
        std::size_t fifo_size,
        GuestMemoryV1* memory,
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
    RendererD3D12 renderer_;
    // A VI retrace is not a GX command boundary. Preserve only an incomplete
    // top-level command tail and prepend the next capture chunk.
    // (Render-thread state, like everything below it down to the stats.)
    std::vector<std::byte> pending_fifo_;
    GxState event_state_;
    std::vector<std::byte> event_pending_fifo_;
    GxState dependency_state_;
    std::vector<std::byte> dependency_pending_fifo_;

    // Valid only inside render_frame_on_thread (parser callbacks need them).
    GuestMemoryV1* frame_memory_ = nullptr;
    bool frame_memory_regions_disjoint_sorted_ = false;
    DependencyEventSink* frame_dependency_event_sink_ = nullptr;
    const NativeServicesV1* frame_services_ = nullptr;
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
    std::uint64_t stalled_queue_admissions_ = 0;
    std::size_t stalled_queue_max_depth_ = 0;
    static constexpr std::uint64_t kRenderStallNs = 20'000'000u;
    static constexpr std::size_t kStalledRenderQueueDepth = 8u;
    // Recycled per-frame chunk buffers, so a frame does not reallocate. The retained count is bounded:
    // a rejected buffer keeps its peak capacity alive, and a scanned delta snapshot can hold well over
    // a megabyte. Without a cap the pool would grow with frames processed rather than frames in flight.
    //
    // The cap is set well above the working need. The largest steady-state pool is
    // GxTimingConfig::render_queue_depth plus the chunk the consumer holds. render_queue_depth defaults
    // to 3 and can be set up to 8 (GALAXY_RENDER_QUEUE_DEPTH). At depth 8 there are up to 9 buffers
    // outstanding, so a cap of 8 would discard the ninth and reallocate every frame. Twice the maximum
    // depth (8) plus the consumer's held chunk is 17, so 24 leaves a margin.
    static constexpr std::size_t kChunkPoolRetainedBuffers = 24u;
    // Maximum depth an operator may select. Kept here, next to the pool it
    // sizes, so the two cannot drift apart silently: raising one without the
    // other is exactly the bug this assert was written to catch.
    static constexpr std::size_t kMaxSelectableRenderQueueDepth = 8u;
    static_assert(
        kChunkPoolRetainedBuffers >= 2u * kMaxSelectableRenderQueueDepth + 1u,
        "chunk pool must retain at least twice the maximum selectable "
        "GALAXY_RENDER_QUEUE_DEPTH plus the consumer's held chunk, or a legal "
        "queue depth silently forces a re-allocation every frame");
    // The pools themselves. gx_backend.cpp clears them in teardown, pops a recycled buffer when building
    // a chunk, and pushes one back after the consumer releases it (three sites, all bounded by
    // kChunkPoolRetainedBuffers). Removing these declarations while those uses remain is a compile error.
    std::vector<std::vector<std::byte>> fifo_pool_;
    std::vector<std::vector<GuestMemoryRange>> memory_range_pool_;
    // Dirty-range vectors travel with the chunk; recycled like the FIFO so
    // the producer does not grow a fresh vector every frame.
    std::vector<std::vector<GuestWriteRange>> dirty_range_pool_;
    // Producer-thread scratch for drain_guest_memory_writes.
    std::vector<GuestWriteRange> drain_scratch_ranges_;
    std::vector<MemorySnapshot> memory_snapshot_pool_;
    static constexpr std::size_t kImmutableRangeCacheMaxEntries = 2048u;
    static constexpr std::size_t kImmutableRangeCacheMaxBytes =
        128u * 1024u * 1024u;
    std::vector<ImmutableRangeCacheEntry> immutable_range_cache_;
    std::uint64_t immutable_range_cache_tick_ = 0;
    std::size_t immutable_range_cache_bytes_ = 0;
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
    void on_fifo_marker(FifoMarker marker, const std::array<float, 12>* view = nullptr);
    // Temporal scene: jitter, palette base, transform record and motion
    // fields of a VS constant upload (`upload` is write-combined memory).
    void write_scene_vs_constants(const GxVsConstants& cpu, GxVsConstants& upload);
    // Writes the jittered projection of a perspective draw into `upload`;
    // `jitter_ndc` receives the offset (zero for orthographic draws).
    void apply_scene_jitter(const GxVsConstants& cpu, GxVsConstants& upload,
                            float jitter_ndc[2]);
    void adopt_pointer_depth_fence_if_needed();
    // Asynchronous EFB depth field for simulation-thread GXPeekZ. Three slots: the render owner fills the
    // slot after the published one, then publishes it. Readers only load the published slot. One set per
    // peek kind: [0] depth, [1] color (async_peek_kind_index).
    struct AsyncPeekFieldState {
        // Mapped readback bytes of each slot (RGBA8, 640x528), written by the
        // render thread before the slot is published.
        std::array<const std::uint8_t*, 3> rgba{};
        // Slot the in-flight field writes; render thread only.
        int pending_slot = -1;
        std::atomic<int> published{-1};
        std::atomic<bool> requested{false};
        std::atomic<std::uint64_t> published_answers{0};
        std::uint64_t completed = 0;
    };
    static constexpr std::size_t async_peek_kind_index(EfbPeekKind kind) noexcept {
        return kind == EfbPeekKind::Depth ? 0u : 1u;
    }
    static constexpr EfbPeekKind async_peek_kind_at(std::size_t index) noexcept {
        return index == 0u ? EfbPeekKind::Depth : EfbPeekKind::Color;
    }
    std::array<AsyncPeekFieldState, 2> async_peek_{};
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
    static constexpr std::size_t kDependencyDrawRunRangeCacheMaxBytes =
        8u * 1024u * 1024u;
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
        // Every indirect vertex-array byte a decode read, as sorted, disjoint ranges whose copies sit back to
        // back in one buffer. The decoder records reads per attribute in read order, so non-monotonic index
        // streams produce many overlapping chunks of one array. Merging overlapping and adjacent reads covers
        // the same byte set, so one byte compare over the merged ranges accepts and rejects exactly the same
        // guest memory.
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
        // Disjoint coarse masks prove that no dependency can overlap a dirty
        // range. All collisions still use the original exact range checks.
        std::uint64_t guest_array_page_mask = ~std::uint64_t{0};
        // Eager dirty-range invalidation is optional. A complete immutable copy of every indirect vertex-array
        // byte means a missed or coalesced notification cannot replay stale decoded geometry. A cache hit
        // compares these bytes before uploading vertices, but only once per dirty-notification generation, not
        // once per hit. A key replayed thousands of times in a frame would otherwise pay the full compare each
        // time. No guest write can land between the generation bump, which happens before the frame's first
        // replay, and the end of the frame.
        GuestDependencySnapshots guest_array_snapshots;
        // `decoded_packet_run_generation_` value at which `guest_array_snapshots`
        // was last proven equal to live guest memory. See the match function.
        std::uint64_t validated_generation = 0;
    };
    // Bumped once per batch of guest-write notifications, before any display list is replayed for the frame.
    // An entry whose validated_generation equals the current value has already been compared against live
    // guest memory since the last write, so repeating the compare on each replay in that frame cannot change
    // the answer. Never reset to a value an existing entry holds.
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
        GuestMemoryV1* memory,
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
        GuestMemoryV1* memory,
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
    Microsoft::WRL::ComPtr<ID3D12Resource> resident_vertex_buffer_;
    std::byte* resident_vertex_mapped_ = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS resident_vertex_gpu_ = 0;
    ID3D12Device* resident_vertex_device_ = nullptr;
    bool resident_vertex_buffer_failed_ = false;
    std::uint64_t frame_resident_vertex_hits_ = 0;
    std::uint64_t frame_resident_vertex_bytes_ = 0;
    [[nodiscard]] bool ensure_resident_vertex_buffer();
    void release_resident_vertex_buffer() noexcept;
    // Returns the entry's block to the arena (deferred reuse). Every removal or
    // replacement of a decoded_packet_run_cache_ entry must call this.
    void release_decoded_entry_residency(DecodedPacketRunCacheEntry& entry) noexcept;
    // Gives `entry` a resident block holding its vertices; false when disabled,
    // out of space or the buffer could not be created (the ring path is used).
    [[nodiscard]] bool make_decoded_entry_resident(DecodedPacketRunCacheEntry& entry);
    // Render-owner cache-off packet staging, bounded by the existing 8MiB cap.
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
    static constexpr std::uint32_t kMem1Size = 0x01800000u;
    static constexpr std::uint32_t kMem2PhysicalBase = 0x10000000u;
    static constexpr std::uint32_t kMem2Size = 0x04000000u;
    static constexpr std::uint32_t kDirtyTrackedBase = 0x00000000u;
    static constexpr std::uint32_t kDirtyTrackedSize = 0x14000000u;
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
    PsoKey current_pso_key_{};
    bool current_pso_key_valid_ = false;
    ID3D12PipelineState* current_pipeline_ = nullptr;
    // Frame-scoped memo of recent (PsoKey -> pipeline) resolutions. Busy frames run 1k-36k draws through
    // flush_draw_state and reuse a few hundred material variants, so the same keys resolve many times per
    // frame. PipelineCache::published_ changes only on this render thread (in get(), and in
    // drain_completions() from begin_frame(), which runs before any draw), so a result resolved this frame
    // cannot change for the rest of it. The memo never substitutes a different pipeline and never skips a
    // draw. It is invalidated at begin_frame().
    //
    // Direct-mapped on the memoized key hash, not a linear scan. The probe runs once per PSO resolution, so
    // its cost must be constant, and the table must hold the frame's working set. A hash collision falls
    // through to the full resolution, so it never returns a wrong pipeline.
    static constexpr std::size_t kPipelineMemoSlots = 512u;
    static_assert(
        (kPipelineMemoSlots & (kPipelineMemoSlots - 1u)) == 0u,
        "the memo index masks the low hash bits, so the slot count is a power of two");
    static constexpr std::size_t kPipelineMemoMask = kPipelineMemoSlots - 1u;
    struct PipelineMemoEntry {
        std::uint64_t hash = 0u;
        ID3D12PipelineState* pipeline = nullptr;
        PsoKey key{};
    };
    std::array<PipelineMemoEntry, kPipelineMemoSlots> pipeline_memo_{};
    // Hash of `current_pso_key_`, kept beside it so the memo probe never
    // re-derives the fold for a key it already hashed once.
    std::uint64_t current_pso_key_hash_ = 0u;
    D3D12_GPU_VIRTUAL_ADDRESS current_vs_constants_ = 0;
    VsConstantsCpuCache cpu_vs_constants_{};
    // current_pipeline_ is an uber pipeline standing in for a specialization
    // that is still compiling: resolve the key again on the next draw.
    bool current_pipeline_is_uber_ = false;
    D3D12_GPU_VIRTUAL_ADDRESS current_ps_constants_ = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE current_texture_table_{};
    D3D12_GPU_DESCRIPTOR_HANDLE current_sampler_table_{};
    D3D12_GPU_VIRTUAL_ADDRESS current_matrix_palette_ = 0;
    bool matrix_palette_stale_ = true;
    // Requirement of the last resolved VS. A palette-free draw can reuse its
    // bindings while retaining stale palette debt for a later palette user.
    bool current_palette_required_ = true;
    bool inline_matrices_stale_ = true;
    // Which 32-word chunks of the XF matrix palette have already been snapshotted into this frame slot's
    // matrix ring. Armed like matrix_palette_stale_ (cleared on the frame-slot rewind and on session reset)
    // and filled in as each snapshot lands.
    //
    // Its one consumer is the reuse decision in flush_draw_state: if the palette has not moved since the
    // snapshot, the existing current_matrix_palette_ address is reused and no ring allocation is made. It
    // does not limit how much is copied. A fresh allocation needs the complete palette, head and tail
    // included, because the ring segment it lands in has no inherited contents. See
    // GxState::xf_palette_dirty().
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
    // sampled_texture_map_mask(state_) depends only on GenMode, the per-stage TEV order registers, and the
    // indirect-command and IND_REF registers. Every BP write that can change them sets kDirtyTev, so the
    // result is recomputed only when a flush's dirty mask includes kDirtyTev. Per-object matrix uploads
    // with kDirtyXfMatrices alone cannot change it. Invalidated wherever the guest BP register file resets.
    bool current_texture_map_mask_valid_ = false;
    mutable std::array<VertexLayoutCacheEntry, 8> vertex_layout_cache_{};

    struct TextureBindingKey {
        std::array<std::uint32_t, kMaxTextureMaps * 5u> regs{};
        std::uint8_t map_mask = 0;

        // Field-wise comparison, not the byte-wise key_bytes_equal used by the shader keys. TextureBindingKeyHash
        // is field-wise (map_mask, then each element of regs), so equality must partition the same way. This
        // struct is 164 bytes with 4-byte alignment and ends in a 1-byte map_mask, so it has 3 bytes of trailing
        // padding that no member covers. A byte-wise == would fold those bytes into the answer while the hash
        // ignores them. Equal keys could then hash equal and compare unequal, leaving the entry unreachable.
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

        // Field-wise, as for TextureBindingKey: TextureTableKeyHash is field-wise (map_mask, then every element
        // of resources, srv_indices, widths and heights), so equality must match. This struct is 136 bytes,
        // 8-byte aligned, and ends in a 1-byte map_mask, so it has 7 bytes of trailing padding that a byte-wise
        // == would fold into the answer.
        [[nodiscard]] bool operator==(const TextureTableKey& other) const
            noexcept {
            return resources == other.resources &&
                srv_indices == other.srv_indices &&
                widths == other.widths &&
                heights == other.heights &&
                map_mask == other.map_mask;
        }
    };
    // Both keys above compare field-wise, matching their hashes. The size assertions pin the exact layout,
    // including the tail padding after map_mask, so a new field cannot silently change the struct these keys
    // are looked up by. They are layout guards, not permission to compare bytes.
    //
    // std::has_unique_object_representations_v is not asserted. It is false for any type with padding, and
    // both keys have padding by construction. It holds in shader_keys.h only because PixelShaderKey,
    // VertexShaderKey, RenderStateKey and PsoKey have no padding there. Those shader keys use byte-wise
    // key_bytes_equal; these two must not, since padding would make equal keys compare unequal.
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
            // 41 values over a 161-byte key. The byte-at-a-time form ran about 330 dependent xor-multiply rounds
            // here, on every candidate compare during a draw flush.
            std::uint64_t hash = mix64(kMix64OffsetBasis, key.map_mask);
            for (const std::uint32_t reg : key.regs) {
                hash = mix64(hash, reg);
            }
            return static_cast<std::size_t>(hash);
        }
    };
    struct TextureBindingTables {
        D3D12_GPU_DESCRIPTOR_HANDLE texture_table{};
        D3D12_GPU_DESCRIPTOR_HANDLE sampler_table{};
        std::array<TextureBindingDependency, kMaxTextureMaps>
            dependencies{};
        std::uint8_t dependency_count = 0;
        // Non-owning, frame-fence-valid handles used to maintain decoded LRU.
        std::array<TextureHandle, kMaxTextureMaps> texture_handles{};
        // Exact resources staged into this table. Used only by opt-in ordered
        // capture when this frame-local table is reused without TextureCache::get.
        std::array<std::uintptr_t, kMaxTextureMaps> alias_resources{};
    };
    std::unordered_map<
        TextureTableKey,
        D3D12_GPU_DESCRIPTOR_HANDLE,
        TextureTableKeyHash>
        frame_texture_tables_;
    std::unordered_map<
        TextureHandleKey,
        TextureHandle,
        TextureHandleKeyHash>
        texture_handle_cache_;
    std::uint64_t seen_texture_retirement_revision_ = 0;
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
    // Frame-local binding tables are owned by frame_texture_binding_tables_ above, not by a cross-frame map.
    // Their CPU descriptors are valid only for the frame that built them. Cross-frame reuse comes from
    // texture_handle_cache_.
    std::vector<DirtyTextureAlias> dirty_texture_aliases_;
    std::vector<TextureCache::GuestRange> dirty_texture_ranges_scratch_;

    std::uint64_t frame_index_ = 0;
    // One relaxed publication per completed frame for producer diagnostic gates.
    // This ordinal does not synchronize guest-memory reads or GPU completion.
    std::atomic<std::uint64_t> timing_frame_index_{0};
    // Published only for the opt-in causal XFB diagnostic. The simulation
    // thread must not read the renderer-owned frame_index_ directly.
    std::atomic<std::uint64_t> causal_trace_frame_index_{0};
    cadence::Session* cadence_diagnostics_ = nullptr;
    // Captured-frame draw log (GALAXY_GX_CAPTURE_FRAME opt-in; render thread).
    bool capture_draws_ = false;
    bool capture_composites_ = false;
    bool capture_all_textures_ = false;
    unsigned capture_draw_idx_ = 0;
    unsigned capture_readback_draw_ = ~0u;
    bool capture_readback_recorded_ = false;
    bool initialized_ = false;
    bool frame_microprofile_enabled_ = false;
    // Per-frame decision for the 8-12 per-draw clock reads, computed once at frame start and read as a member.
    // It is on for an intrusive run and off otherwise. GALAXY_GX_FRAME_TIMING_SAMPLE >= 2 also enables it for a
    // bounded window of every Nth frame, so one recording can show both the uninstrumented frame cost and where
    // the time goes.
    bool frame_time_detail_ = false;
    bool frame_time_sample_ = false;
    // Last emitted [gx-frame-timing] render_start, for frame-interval-us. An interval much larger than total-us
    // means the thread is idle between frames; an interval near total-us means it is saturated.
    std::chrono::steady_clock::time_point last_frame_timing_emit_{};
    bool last_frame_timing_emit_valid_ = false;
    // One-time EFB seed clear. GX does not clear at frame start; only the copy command's clear bit does
    // (see on_efb_copy).
    bool efb_seeded_ = false;

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
    // Explicit diagnostic counter aggregate. Zero can mean disabled or invalid;
    // the validity field distinguishes both from a complete measured zero.
    std::uint64_t frame_flush_pso_cycles_ = 0;
    bool frame_flush_pso_cycles_valid_ = false;
    bool pso_cycles_enabled = false;
    // Sampled wall time (1 call in kFlushTimingSampleStride) of the whole flush_draw_state call, with the
    // sample count behind the estimate. frame_flush_us_ above is the same quantity, but only under per-draw
    // instrumentation. This pair works in an uninstrumented run. See ScopedFlushTiming in gx_backend.cpp for
    // the sampling stride.
    std::uint64_t frame_flush_total_us_ = 0;
    std::uint64_t frame_flush_timed_samples_ = 0;
    std::uint64_t frame_flush_timed_calls_ = 0;
    // Sub-slices of frame_flush_pso_us_: memo hits (no PipelineCache call), the PipelineCache::get() call,
    // and the PsoKey hash and compare work.
    std::uint64_t frame_pso_memo_hits_ = 0;
    std::uint64_t frame_pso_get_calls_ = 0;
    std::uint64_t frame_pso_get_us_ = 0;
    std::uint64_t frame_pso_get_max_us_ = 0;
    // Which cached display-list replay path ran, counted in batches. The parser offers the two batched paths
    // first and falls back to per-packet replay. Without these counts a silently dead fast path looks like a
    // slow one.
    std::uint64_t frame_replay_prepared_batches_ = 0;
    std::uint64_t frame_replay_packet_batches_ = 0;
    std::uint64_t frame_replay_simple_batches_ = 0;
    std::uint64_t frame_replay_generic_draws_ = 0;
    std::uint64_t frame_flush_matrix_us_ = 0;
    // Bytes written to the matrix upload ring this frame. Clock-free and always on. frame_flush_matrix_us_
    // gives the time the uploads took; this gives the amount written. A frame can be slow because the upload
    // is large or because the ring slot is cold. On an adapter that backs GPU memory with system DRAM, the byte
    // count predicts the cost better than the call count.
    std::uint64_t frame_matrix_upload_bytes_ = 0;
    // Per-frame deltas of PipelineCache::CompileStats, taken against frame_compile_stats_baseline_ at the end
    // of the frame. They show whether a slow frame is compiling or waiting on a compile another thread started:
    //
    //   pso-compiles      driver builds performed this frame
    //   pso-library-loads builds answered from the D3D12 pipeline library
    //   shader-pairs      FXC-generated (VS, PS) pairs
    //   blobs-from-cache  pairs whose DXBC came from disk
    //
    // [pso-cache-coverage] reports cumulative totals, which cannot be matched to one frame's flush-pso-us.
    // These are four subtractions per frame on already-tracked integers: no clock, no lock, nothing added to
    // the per-draw path. pso-compiles = 0 on a slow frame points the cause at the library-load or memo path.
    std::uint64_t frame_pso_compiles_ = 0;
    std::uint64_t frame_pso_library_loads_ = 0;
    std::uint64_t frame_shader_pair_compiles_ = 0;
    // FXC microseconds this frame, the only per-frame shader compile figure. blob-us= on a miss line is per
    // request and printed only when that one request crosses the stall threshold, so a frame compiling several
    // shaders below the threshold reports nothing.
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
    // Clock-free structural counters: plain integer increments, never atomic or timed, so they cost the same in
    // normal play as in a diagnostic run. Every timing field above is filled only under
    // frame_microprofile_enabled_ or trace_gx_stalls_enabled(). Those timings cannot separate real work from the
    // cost of being measured, so these counts show whether flush-pso-us runs 3 times per frame or 36000.
    //
    // Only the early-out count needs its own member. The memo and get() routes are frame_pso_memo_hits_ and
    // frame_pso_get_calls_, and the report derives them instead of storing a copy that could drift.
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
    std::uint64_t stat_tex_copies_ = 0;
    std::atomic<std::uint64_t> stat_queue_wait_ns_{0};
    std::atomic<std::uint64_t> stat_render_ns_{0};
    std::atomic<std::uint64_t> stat_dependency_scan_ns_{0};
    std::atomic<std::uint64_t> stat_dependency_cache_hits_{0};
    std::atomic<std::uint64_t> stat_dependency_cache_misses_{0};
    std::atomic<std::uint64_t> stat_dependency_cache_invalidations_{0};
    std::atomic<std::uint64_t> stat_dependency_draw_run_cache_hits_{0};
    std::atomic<std::uint64_t> stat_dependency_draw_run_cache_misses_{0};
    std::atomic<std::uint64_t> stat_dependency_draw_run_cache_evictions_{0};
    std::atomic<std::uint64_t> stat_snapshot_ns_{0};
    std::atomic<std::uint64_t> stat_snapshot_bytes_{0};
    std::atomic<std::uint64_t> stat_snapshot_ranges_{0};
    std::atomic<std::uint64_t> stat_dirty_ranges_{0};
    std::atomic<std::uint64_t> stat_dirty_bytes_{0};
    std::atomic<std::uint64_t> stat_dirty_texture_evictions_{0};
    std::atomic<std::uint64_t> stat_dirty_display_list_invalidations_{0};
    std::uint64_t stat_palette_uploads_ = 0;
    std::uint64_t stat_inline_matrix_uploads_ = 0;
    // Trace samples are collected on the render thread but formatted only after it has stopped. Writing a
    // diagnostic line from a real-time render callback can block on redirected stderr long enough to distort
    // the VI cadence being measured. The fixed bound keeps the trace observational: no allocation, I/O or lock
    // on the render path.
    struct DeferredGxStatsSample {
        std::uint64_t frame_index = 0;
        std::uint64_t total_draws = 0;
        std::uint64_t total_no_pso = 0;
        std::uint64_t frame_draws = 0;
        std::uint64_t frame_no_pso = 0;
        std::uint64_t xfb_copies = 0;
        std::uint64_t tex_copies = 0;
        std::uint64_t specialized_hits = 0;
        std::uint64_t uber_fallbacks = 0;
        bool has_latest_xfb = false;
        XfbPresentStats present{};
        std::uint64_t stale_fallbacks = 0;
        std::uint64_t missing_fallbacks = 0;
        std::uint64_t stale_preserved = 0;
        std::uint64_t missing_preserved = 0;
        std::uint64_t render_ns = 0;
        std::uint64_t queue_wait_ns = 0;
        std::uint64_t dependency_scan_ns = 0;
        std::uint64_t dependency_cache_hits = 0;
        std::uint64_t dependency_cache_misses = 0;
        std::uint64_t dependency_cache_invalidations = 0;
        std::uint64_t dependency_draw_run_cache_hits = 0;
        std::uint64_t dependency_draw_run_cache_misses = 0;
        std::uint64_t dependency_draw_run_cache_evictions = 0;
        std::uint64_t snapshot_ns = 0;
        std::uint64_t snapshot_bytes = 0;
        std::uint64_t snapshot_ranges = 0;
        std::uint64_t dirty_ranges = 0;
        std::uint64_t dirty_bytes = 0;
        std::uint64_t dirty_texture_evictions = 0;
        std::uint64_t dirty_display_list_invalidations = 0;
        std::uint64_t palette_uploads = 0;
        std::uint64_t inline_matrix_uploads = 0;
        // Clock-free attribution, sampled from representative frames, not only over-threshold ones. Each field is a
        // plain increment on a per-frame member, so it adds no clock read and cannot inflate the quantity measured.
        // Frames that cross the stall threshold carry their own per-draw clock reads, which would inflate
        // flush-pso-us if they were the only samples.
        //
        // render_ns is accumulated only under trace_gx_stats or trace_gx_stalls, so it reads 0 on a routine sample.
        // The fields below stay valid and separate the two hypotheses:
        //   us_per_flush   = flush_pso_ns / flush_calls
        //   draws_per_call = draws / flush_calls
        // Thousands of calls at ~3 us each and ~24 calls at ~17 ms each have the same total but imply opposite fixes.
        std::uint64_t draws = 0;
        std::uint64_t draw_batches = 0;
        std::uint64_t flush_calls = 0;
        std::uint64_t pso_resolutions = 0;
        std::uint64_t pso_route_early = 0;
        std::uint64_t pso_route_memo = 0;
        std::uint64_t pso_route_get = 0;
        std::uint64_t vertex_load_ns = 0;
        std::uint64_t cached_vertex_upload_ns = 0;
        std::uint64_t draw_record_ns = 0;
        std::uint64_t flush_pso_ns = 0;
    };
    static constexpr std::size_t kDeferredGxStatsCapacity = 128;
    std::array<DeferredGxStatsSample, kDeferredGxStatsCapacity>
        deferred_gx_stats_{};
    std::size_t deferred_gx_stats_count_ = 0;
    std::uint64_t deferred_gx_stats_dropped_ = 0;
    // Total samples offered to the ring, including those it did not retain. This is reservoir sampling's N. The
    // ring keeps a uniform draw over the whole session rather than the first `capacity` rows, so the retained
    // rows describe the whole run. `dropped` still counts non-retained samples so the report can state the
    // retention ratio.
    std::uint64_t deferred_gx_stats_seen_ = 0;
    // xorshift64* state for the reservoir replacement index. It is per backend, so the render thread shares no
    // state, and it is deterministic, so one session shape gives the same rows.
    std::uint64_t deferred_gx_stats_rng_ = 0x9e3779b97f4a7c15ull;
    // Frame index of the first dropped sample, so the shutdown report can say which part of the session the ring
    // stopped covering. Zero means none.
    std::uint64_t deferred_gx_stats_first_drop_frame_ = 0;
    // The causal XFB trace is separate from the periodic GX statistics. It captures a caller-selected contiguous
    // render-frame window and writes it only after the render thread has joined.
    struct DeferredXfbCausalRecord {
        std::uint64_t frame_index = 0;
        std::size_t fifo_bytes = 0;
        std::uint64_t copy_exec_count = 0;
        std::uint64_t xfb_copy_count = 0;
        std::uint64_t texture_copy_count = 0;
        bool fifo_complete = false;
        bool present_only = false;
        bool present_request = false;
        bool present_submitted = false;
        std::uint32_t selected_xfb = 0;
        std::uint64_t selected_serial = 0;
        std::uint64_t global_xfb_copies = 0;
        std::size_t pe_classified_events = 0;
        std::size_t pe_rendered_events = 0;
    };
    static constexpr std::size_t kDeferredXfbCausalCapacity = 128;
    std::array<DeferredXfbCausalRecord, kDeferredXfbCausalCapacity>
        deferred_xfb_causal_{};
    std::size_t deferred_xfb_causal_count_ = 0;
    std::uint64_t deferred_xfb_causal_dropped_ = 0;
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

    void emit_deferred_gx_stats();
    void emit_deferred_xfb_causal_trace();
};

}  // namespace galaxy::gx
