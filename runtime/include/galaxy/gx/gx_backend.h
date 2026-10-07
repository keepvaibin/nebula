#pragma once

// GxBackend: the orchestrator.  Owns every subsystem and implements the
// per-frame loop behind the frozen galaxy::gx public API (gx_d3d12.h).
//
// Dual-core model (Dolphin Fifo.cpp deterministic-GPU-thread shape, collapsed
// for the AOT cadence): the SIM thread calls render_frame() once per VI
// retrace, which only snapshots the frame's FIFO bytes into a bounded queue
// (depth 1 + one in flight = at most one frame of latency) and returns; a
// dedicated RENDER thread consumes chunks in order and runs the actual
// pipeline:
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
// Back-pressure is a native producer/consumer contract: a full queue blocks
// the producer after the configured one-to-three retrace chunks.
// No PowerPC exception or CP breakpoint emulation — the AOT cadence (one chunk
// per retrace, parsed in submission order) is the synchronization. Guest-memory
// reads during the parse (textures, vertex arrays) run a bounded number of
// frames behind the simulation, the same kind of window real GP hardware has
// while the game respects GXDrawDone.
//
// A render-thread failure is stashed and rethrown from the next sim-thread
// render_frame() call — hard-fail, never a silently frozen display.
//
// The legacy entry points in gx_d3d12.h become a thin shim over a single
// GxBackend instance, so runtime integration (native_runtime.cpp) is
// untouched.

#include "galaxy/gx/efb_copy.h"
#include "galaxy/gx/fifo_parser.h"
#include "galaxy/gx/gx_state.h"
#include "galaxy/gx/pipeline_cache.h"
#include "galaxy/gx/renderer_d3d12.h"
#include "galaxy/gx/shader_keys.h"
#include "galaxy/gx/texture_cache.h"
#include "galaxy/gx/uber_constants.h"
#include "galaxy/gx/vertex_loader.h"
#include "galaxy/gx_d3d12.h"
#include "galaxy/native_api.h"
#include "galaxy/frame_telemetry.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
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

// Exact guest-visible PE event identity emitted by the lightweight FIFO
// preclassifier. This is deliberately not a byte-pattern scan: the classifier
// uses FifoParser so partial commands, draw payloads, BP masks, and CALL_DL
// recursion have the same command-boundary semantics as the renderer.
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
    void reset();
    void classify(
        const std::byte* fifo_data,
        std::size_t fifo_size,
        GuestMemoryV1* memory,
        std::vector<FramePeEventSignature>& events);
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
    std::vector<std::byte> pending_fifo_;
    std::vector<FramePeEventSignature>* active_events_ = nullptr;
    // Memo for draw_payload_size(). This classifier runs a second full pass over
    // the same command stream, and the parser calls draw_payload_size once per
    // draw to validate that the whole payload is present before it invokes
    // on_draw — on a replayed display list that is tens of thousands of calls
    // per frame into a ~30-arm switch walk. The vertex layout is fully described
    // by CP VCD_LO/VCD_HI and the per-format VAT A/B/C registers, and this sink
    // shares the parser's GxState, so comparing those five words is an exact
    // test of whether the remembered stride still applies.
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

// FNV-1a step over one 64-bit value. The 32-bit FNV prime is *not* invertible
// modulo 2^64, so a per-byte recurrence cannot be folded into this; a
// multi-byte step is a different (still FNV-1a shaped) hash, not a rewrite of
// the same one. That is fine because these hash operators feed unordered_map
// buckets only: the keys' operator== stays the sole arbiter of identity, and
// hashing is transparent to the containers. The reason to prefer it is cost:
// the byte-at-a-time form ran 8 iterations of {xor, 64-bit multiply} for every
// 8-byte value, and the binding table hashes ~41 such values, i.e. ~330
// dependent multiply iterations per call.
//
// mix64 keeps the classic FNV-1a constants but consumes a whole word per step.
// The caller seeds the accumulator with kFnvOffsetBasis; mix64 never inspects
// the accumulator, so a zero value in the middle of a key cannot restart the
// chain.
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
    [[nodiscard]] bool capture_pointer_depth(std::span<std::uint32_t> pixels);
    [[nodiscard]] bool enqueue_pointer_depth_capture(const std::shared_ptr<PointerDepthCapture>& capture);
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
    };

    [[nodiscard]] static bool draw_calls_batch_compatible(
        const DrawCall& lhs,
        const DrawCall& rhs,
        bool lhs_indexed,
        bool rhs_indexed) noexcept;
    void flush_pending_draw_batch();
    // Routine, clock-free shader/PSO cache-coverage report; prints only when the
    // on-disk coverage actually changed. Called once per frame from the
    // end-of-frame cache-persistence block. See the definition for why it exists.
    void ReportPipelineCoverage();
    std::size_t last_reported_shader_records_ = 0;
    std::size_t last_reported_pso_records_ = 0;
    void on_cached_draw_run_draw(
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        FifoCursor& cursor);

    // --- Render thread --------------------------------------------------------

    // One queued frame: a snapshot of the FIFO bytes the sim produced this
    // retrace plus the memory/services pointers the parser callbacks need.
    // When memory_snapshot_active is true, memory points at memory_snapshot so
    // delayed render-thread parsing sees the same dynamic arrays/textures that
    // existed when the sim submitted this frame.
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
        // Production-gated compact snapshots can share immutable range
        // resources across frames. A dirty replacement allocates/copies a new
        // resource, so an older queued chunk never aliases mutable guest bytes
        // or a newer version of the same guest range.
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
    // Starts one logically fresh GX command-stream session. This must reset
    // every parser/state/tail mirror together: retaining even one stream
    // while resetting the PE preclassifier can falsely classify a receipt as
    // event-free after shutdown/reinitialize.
    void reset_fifo_session_state();

    void render_thread_main();
    void render_frame_on_thread(FrameChunk& chunk);
    void execute_efb_peek_on_thread(FrameChunk& chunk);
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

    // Frame queue (sim thread = producer, render thread = consumer). The
    // producer may run a small bounded number of frames ahead of the
    // consumer and then blocks on queue_space_cv_ — the frame-granular analogue
    // of Dolphin's SyncGPU max-distance throttle. fifo_pool_ recycles chunk
    // buffers so the steady state performs no allocation.
    std::thread render_thread_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;        // consumer: work or stop
    std::condition_variable queue_space_cv_;  // producer: slot free
    std::deque<FrameChunk> frame_queue_;
    // Recycled per-frame chunk buffers. The point of these pools is to avoid
    // re-allocating a buffer every frame, which a *bounded* number of retained
    // buffers does just as well as an unbounded one: the queue depth is capped
    // by the in-flight accounting above, so only a handful can ever be handed
    // out at once. Retaining more than that keeps each rejected buffer's peak
    // capacity alive for the rest of the session, and those peaks are large --
    // a delta snapshot's `storage` holds one heap block per scanned dependency
    // range, and a single scanned frame can request well over a megabyte. An
    // uncapped pool therefore grows with frames *processed*, not with frames in
    // flight: the Arc recording's working set climbed from 1.0 GB to 6.3 GB
    // over one 372 s session while the RTX 5090 session of the same content
    // stayed near 1.3 GB.
    //
    // The cap is deliberately generous relative to the queue depth -- the
    // largest legitimate steady-state pool is bounded by
    // `GxTimingConfig::render_queue_depth` plus the one chunk the consumer is
    // holding, so a handful of retained buffers is already more than can be
    // handed out at once and this only drops buffers that could never be reused
    // anyway.
    //
    // `render_queue_depth` defaults to 3 but is operator-selectable up to 8
    // (`GALAXY_RENDER_QUEUE_DEPTH`; see resolve_gx_timing_config_from_environment).
    // At depth 8 the worst case is 8 queued plus 1 held by the render thread = 9
    // outstanding, so a cap of 8 would silently discard the ninth buffer and put
    // the producer back on a fresh allocation every frame -- the exact behaviour
    // this bound exists to avoid, without the unbounded growth. 2x the maximum
    // selectable depth (8) plus the consumer's held chunk (1) is 17, so 24 keeps
    // a deliberate margin above that. The pool is still bounded, so it cannot
    // regrow with frames processed.
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
    // The pools themselves. `gx_backend.cpp` clears them in teardown, pops a
    // recycled buffer when building a chunk, and pushes one back after the
    // consumer releases it (three sites, all guarded by
    // kChunkPoolRetainedBuffers). These declarations are load-bearing: their
    // removal while those uses remain is a compile error, which is how this was
    // caught.
    std::vector<std::vector<std::byte>> fifo_pool_;
    std::vector<std::vector<GuestMemoryRange>> memory_range_pool_;
    std::vector<MemorySnapshot> memory_snapshot_pool_;
    static constexpr std::size_t kImmutableRangeCacheMaxEntries = 2048u;
    static constexpr std::size_t kImmutableRangeCacheMaxBytes =
        128u * 1024u * 1024u;
    std::vector<ImmutableRangeCacheEntry> immutable_range_cache_;
    std::uint64_t immutable_range_cache_tick_ = 0;
    std::size_t immutable_range_cache_bytes_ = 0;
    std::deque<PendingFrameEffects> pending_frame_effects_;
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
        struct GuestDependencySnapshot {
            std::uint32_t guest_base = 0;
            std::vector<std::byte> bytes;
        };

        DecodedPacketRunCacheKey key{};
        std::uint32_t total_indices = 0;
        std::uint64_t last_used = 0;
        std::size_t byte_size = 0;
        std::vector<GxVertexOut> vertices;
        ImmutableUploadToken upload_token;
        std::vector<VertexDecodeGuestRange> guest_array_reads;
        // Dirty-page invalidation is the cheap first line of defence. Keep an
        // immutable copy of every indirect vertex-array byte as well so a
        // missed/coalesced notification can never replay stale decoded
        // geometry. A cache hit compares these bytes before uploading vertices
        // -- but only once per dirty-notification generation, not once per hit.
        // A key replayed thousands of times in one frame would otherwise pay the
        // full snapshot compare thousands of times, and no guest write can be
        // observed between the generation bump (which happens before the first
        // replay of the frame) and the end of the frame.
        std::vector<GuestDependencySnapshot> guest_array_snapshots;
        // `decoded_packet_run_generation_` value at which `guest_array_snapshots`
        // was last proven equal to live guest memory. See the match function.
        std::uint64_t validated_generation = 0;
    };
    // Bumped once per batch of guest-write notifications, before any display
    // list is replayed for that frame. An entry whose `validated_generation`
    // equals the current value has already had its snapshots compared against
    // live guest memory since the last write was applied, so repeating the byte
    // compare on every replay of the same key in that frame cannot change the
    // answer. Never reset to the value an existing entry holds.
    std::uint64_t decoded_packet_run_generation_ = 1;

    [[nodiscard]] DecodedPacketRunCacheKey decoded_packet_run_cache_key(
        std::uint64_t cache_token,
        std::size_t packet_run_index,
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        const VertexDescriptor& desc) const;
    [[nodiscard]] static bool capture_decoded_packet_run_dependencies(
        GuestMemoryV1* memory,
        const std::vector<VertexDecodeGuestRange>& dependencies,
        std::size_t byte_budget,
        std::vector<DecodedPacketRunCacheEntry::GuestDependencySnapshot>&
            snapshots,
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
        const std::vector<
            DecodedPacketRunCacheEntry::GuestDependencySnapshot>& snapshots,
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
    PsoKey current_pso_key_{};
    bool current_pso_key_valid_ = false;
    ID3D12PipelineState* current_pipeline_ = nullptr;
    // Frame-scoped memo of recent (PsoKey -> pipeline) resolutions. Busy SMG
    // frames run 1k-36k draws through flush_draw_state, and a busy frame's key
    // set runs to hundreds of distinct material variants, so the same keys are
    // resolved many times per frame. PipelineCache::published_ is mutated only
    // on this render thread (from get() here, and from drain_completions() in
    // begin_frame(), which runs before any draw), so a result resolved inside
    // this frame cannot change for the rest of the frame. This is therefore a
    // pure redundant-lookup elision: it never substitutes a different pipeline
    // and never skips a draw. Invalidated wholesale at begin_frame().
    //
    // Direct-mapped on the memoized key hash rather than a linear scan. The
    // probe is the frame's hottest single operation -- one per PSO resolution,
    // tens of thousands per frame -- so its cost has to be a constant, and the
    // table has to be big enough to still hold the frame's working set. An
    // 8-entry LRU scan was neither: it walked up to 8 entries (valid test, null
    // test, 32-byte key compare each) and evicted almost everything a busy
    // frame touches. Indexing by `MemoizedPsoKey::hash` makes the common case
    // one indexed read, one 64-bit compare and one exact 32-byte confirm, so a
    // collision still falls through to the full resolution rather than
    // returning a wrong pipeline.
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
    D3D12_GPU_VIRTUAL_ADDRESS current_ps_constants_ = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE current_texture_table_{};
    D3D12_GPU_DESCRIPTOR_HANDLE current_sampler_table_{};
    D3D12_GPU_VIRTUAL_ADDRESS current_matrix_palette_ = 0;
    bool matrix_palette_stale_ = true;
    bool inline_matrices_stale_ = true;
    // Which 32-word chunks of the XF matrix palette have already been
    // snapshotted into this frame slot's matrix ring. Armed exactly like
    // `matrix_palette_stale_` (cleared on the frame-slot rewind and on the
    // session reset) and filled in as each snapshot lands.
    //
    // Its one consumer is the reuse decision in `flush_draw_state`: if the
    // palette has not moved since the snapshot, the existing
    // `current_matrix_palette_` address is reused and no ring allocation is
    // made. It does **not** limit how much is copied -- a fresh allocation needs
    // the complete palette, head and tail included, because the ring segment it
    // lands in has no inherited contents. See `GxState::xf_palette_dirty()`.
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
    // `sampled_texture_map_mask(state_)` is a pure function of GenMode, the
    // per-stage TEV order registers and the indirect-command / IND_REF
    // registers. Every BP write that can move any of those sets kDirtyTev
    // (GxState::load_bp: GenMode, TEV order, IND_CMD, and RAS1_SS0..IND_REF all
    // do), so the result only needs recomputing when a flush's dirty mask
    // contains kDirtyTev. A busy replayed display list re-uploads matrices per
    // object with kDirtyXfMatrices alone, which cannot change the mask.
    // Invalidated wherever the guest BP register file is reset.
    bool current_texture_map_mask_valid_ = false;
    mutable std::array<VertexLayoutCacheEntry, 8> vertex_layout_cache_{};

    struct TextureBindingKey {
        std::array<std::uint32_t, kMaxTextureMaps * 5u> regs{};
        std::uint8_t map_mask = 0;

        // Field-wise comparison, deliberately NOT the byte-wise
        // key_bytes_equal form the shader keys use.
        //
        // The decisive reason is the hash, not the padding. TextureBindingKeyHash
        // below is FIELD-WISE: it mixes `map_mask` and then each element of
        // `regs`. Equality must partition the same way the hash does. This struct
        // is 164 bytes with 4-byte alignment and ends in a 1-byte `map_mask`, so
        // it carries 3 bytes of trailing padding that no member covers; a
        // byte-wise `==` folds those padding bytes into the answer while the hash
        // does not. Two keys with identical members can then hash equal and
        // compare UNEQUAL, which is a broken std::unordered_map contract, not
        // merely a missed cache: the entry becomes unreachable and the lookup
        // degrades to a miss (or an unnecessary descriptor-table rebuild) for a
        // key that is already present.
        //
        // The previous comment justified the byte form on construction-path
        // grounds ("every member is value-initialised, so padding is zero"). That
        // is true of the aggregate-initialisation path and is still not enough:
        // it makes the padding *usually* zero rather than *always* determinate,
        // and it does nothing about the hash mismatch above. Field-wise equality
        // needs neither argument.
        //
        // Cost is unchanged in practice: `std::array::operator==` is element-wise
        // and the compiler lowers it to the same linear vector compare the memcmp
        // produced, not the dependent compare-and-branch chain the original
        // comment described.
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

        // Field-wise, for exactly the reason given on TextureBindingKey above:
        // TextureTableKeyHash is field-wise (map_mask then every element of
        // resources/srv_indices/widths/heights), so equality must be too. This
        // struct is 136 bytes with 8-byte alignment and ends in a 1-byte
        // `map_mask`, so it carries 7 bytes of trailing padding that a byte-wise
        // `==` would fold into the answer while the hash does not.
        [[nodiscard]] bool operator==(const TextureTableKey& other) const
            noexcept {
            return resources == other.resources &&
                srv_indices == other.srv_indices &&
                widths == other.widths &&
                heights == other.heights &&
                map_mask == other.map_mask;
        }
    };
    // Both keys above compare FIELD-WISE, matching their field-wise hashes. The
    // size assertions below pin the exact layout — including the trailing
    // `+4`/`+8` tail padding after `map_mask` that the layout necessarily
    // produces — so a future field cannot silently change the struct these keys
    // are looked up by. They are layout guards, not a licence to compare bytes.
    //
    // NOTE: `std::has_unique_object_representations_v` is deliberately NOT
    // asserted here, and cannot be. That trait is false for *any* type with
    // padding, and both of these keys have trailing padding by construction: the
    // `+4`/`+8` in the size assertions below is exactly that padding. The trait
    // holds in `shader_keys.h` only because `PixelShaderKey`, `VertexShaderKey`,
    // `RenderStateKey` and `PsoKey` are explicitly padding-free there.
    //
    // That asymmetry is precisely why these two keys must not use the shader
    // keys' byte-wise `key_bytes_equal`: with padding present it folds bytes the
    // field-wise hash never saw into the equality answer, so two equal keys can
    // hash equal and compare unequal. The trait failing here was the compiler
    // saying so, and the earlier `key_bytes_equal` form did not compile.
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
            // 41 values over a 161-byte key: the byte-at-a-time form this
            // replaces ran ~330 dependent {xor, multiply} iterations here, on
            // every candidate compare during a draw flush.
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
    // There was a second, identically-typed `persistent_texture_binding_tables_`
    // here. It was never inserted into or read -- every one of its uses was
    // clear/erase/prune -- so it was removed. It could not have worked: the
    // tables it would have stored hold CPU descriptors valid only for the frame
    // that built them, which is why `frame_texture_binding_tables_` above is the
    // owner. Cross-frame amortization is provided by `texture_handle_cache_`.
    std::vector<DirtyTextureAlias> dirty_texture_aliases_;
    std::vector<TextureCache::GuestRange> dirty_texture_ranges_scratch_;

    std::uint64_t frame_index_ = 0;
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
    // Single per-frame decision for the ~8-12 per-draw clock reads. Computed
    // once at frame start and read as a member by every draw-path site, so the
    // gate is no longer a Meyers-singleton-gated function call repeated
    // thousands of times per frame.
    //
    // Semantics are unchanged from `frame_microprofile_enabled_ ||
    // trace_gx_stalls_enabled()`: fully on for an intrusive run, fully off
    // otherwise. GALAXY_GX_FRAME_TIMING_SAMPLE >= 2 additionally turns it on for
    // a bounded window of every Nth frame, which is what makes one routine
    // recording able to answer both "what does an uninstrumented frame cost" and
    // "where does the time go" instead of being all-or-nothing.
    bool frame_time_detail_ = false;
    bool frame_time_sample_ = false;
    // State for the rendered `frame-interval-us`: the render_start of the last
    // emitted [gx-frame-timing] line. Quantifies whether this thread is idle
    // between frames (interval >> total-us) or saturated (interval ~= total-us),
    // which the per-phase waits on that line cannot answer when they all read 0.
    std::chrono::steady_clock::time_point last_frame_timing_emit_{};
    bool last_frame_timing_emit_valid_ = false;
    // One-time EFB seed clear (GX never clears at frame start — only the
    // copy command's clear bit does, see on_efb_copy).
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
    // Always-on, sampled (1 call in kFlushTimingSampleStride) wall time of the
    // whole `flush_draw_state` call, plus the sample count behind the estimate.
    // `frame_flush_us_` above is the same quantity but is populated only under
    // per-draw instrumentation, so it cannot rank the next fix; this pair can,
    // in an uninstrumented run. See ScopedFlushTiming in gx_backend.cpp for why
    // the sampling stride exists.
    std::uint64_t frame_flush_total_us_ = 0;
    std::uint64_t frame_flush_timed_samples_ = 0;
    std::uint64_t frame_flush_timed_calls_ = 0;
    // Sub-slices of frame_flush_pso_us_ so a slow interval can be attributed
    // without guessing: memo hits (no PipelineCache call at all), the
    // PipelineCache::get() call itself, and the PsoKey hash/compare work.
    std::uint64_t frame_pso_memo_hits_ = 0;
    std::uint64_t frame_pso_get_calls_ = 0;
    std::uint64_t frame_pso_get_us_ = 0;
    std::uint64_t frame_pso_get_max_us_ = 0;
    // Which cached-display-list replay path actually ran, counted in batches
    // (each batch covers many draws). The parser offers the two batched paths
    // first and falls back to per-packet replay; without these the split is
    // invisible in a routine recording and a silently-dead fast path looks
    // exactly like a slow one.
    std::uint64_t frame_replay_prepared_batches_ = 0;
    std::uint64_t frame_replay_packet_batches_ = 0;
    std::uint64_t frame_replay_simple_batches_ = 0;
    std::uint64_t frame_replay_generic_draws_ = 0;
    std::uint64_t frame_flush_matrix_us_ = 0;
    // Bytes pushed into the matrix upload ring this frame. Always-on and
    // clock-free: `frame_flush_matrix_us_` above says how long the uploads took,
    // this says how much was written. The two are independent -- a frame can be
    // slow because the upload is large or because the ring's next slot is cold
    // -- and on a host whose adapter backs GPU memory from system DRAM the byte
    // count is the number that predicts the cost, not the call count. Nothing
    // in either supplied recording could report it.
    std::uint64_t frame_matrix_upload_bytes_ = 0;
    // Per-frame deltas of PipelineCache::CompileStats, captured against
    // `frame_compile_stats_baseline_` at the end of the frame. These decide
    // whether a slow frame is *compiling* or *waiting on a compile somebody else
    // started*:
    //
    //   pso-compiles      driver builds performed this frame
    //   pso-library-loads builds answered from the D3D12 pipeline library
    //   shader-pairs      FXC-generated (VS, PS) pairs
    //   blobs-from-cache  pairs whose DXBC came off disk
    //
    // `[pso-cache-coverage]` reports the cumulative totals, but a cumulative
    // counter cannot be matched against one frame's `flush-pso-us`, and that is
    // the open question: 9 of 30 Arc frames carry 84 % of the render thread's
    // time with `flush-pso-us` at 89-99.7 % of it over as few as 3-4 draws, and
    // the arithmetic (frame 21110: 115226 us across 4 draws at the observed
    // ~22 ms per miss = 5.2 compiles) says those frames wait on a small integer
    // number of compiles rather than drawing. Four subtractions per frame on
    // already-tracked integers; no clock, no lock, nothing added to the per-draw
    // path. `pso-compiles = 0` on a tail frame would falsify that reading and
    // move the cause onto the library-load or memo path instead.
    std::uint64_t frame_pso_compiles_ = 0;
    std::uint64_t frame_pso_library_loads_ = 0;
    std::uint64_t frame_shader_pair_compiles_ = 0;
    // FXC microseconds this frame. The only per-frame figure for shader
    // compilation: `blob-us=` on a miss line is per request and gated on that
    // single request crossing the stall threshold, so a frame compiling several
    // shaders below the threshold reports nothing at all there.
    std::uint64_t frame_blob_compile_us_ = 0;
    std::uint64_t frame_blobs_from_cache_ = 0;
    // Baseline for the deltas above, captured in the per-frame reset block.
    PipelineCache::CompileStats frame_compile_stats_baseline_{};
    std::uint64_t frame_flush_constants_us_ = 0;
    std::uint64_t frame_flush_texture_us_ = 0;
    std::uint64_t frame_vertex_load_us_ = 0;
    std::uint64_t frame_cached_vertex_validation_us_ = 0;
    std::uint64_t frame_cached_vertex_upload_us_ = 0;
    std::uint64_t frame_draw_record_us_ = 0;
    std::uint64_t frame_efb_copy_us_ = 0;
    std::uint64_t frame_decoded_vertex_cache_hits_ = 0;
    std::uint64_t frame_decoded_vertex_cache_misses_ = 0;
    std::uint64_t frame_decoded_vertex_cache_evictions_ = 0;
    std::uint64_t frame_decoded_vertex_cache_stale_rejects_ = 0;
    // Clock-free structural counters. Plain integer increments: never atomic,
    // never timed, so they cost exactly the same in normal play as under a
    // diagnostic run. Every *timing* field above is populated only when
    // frame_microprofile_enabled_ or trace_gx_stalls_enabled() is set -- both
    // opt-in instrumentation -- so a timing-only profile cannot distinguish
    // real work from the cost of being measured. These counts settle that:
    // they say whether the flush-pso-us block runs 3 times per frame or 36000.
    // route: only the *early-out* count needs its own member. The memo and
    // get() routes are exactly frame_pso_memo_hits_ and frame_pso_get_calls_,
    // and the report derives them rather than storing a second copy that could
    // drift from them and become an overlapping (duplicate) timing field.
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
    // Trace samples are collected by the render thread, but formatted only
    // after that thread has stopped.  Writing a diagnostic line directly from
    // a real-time render callback can block on the redirected stderr handle
    // long enough to invalidate the exact VI cadence we are trying to
    // measure.  The fixed bound deliberately makes the trace observational:
    // no allocation, I/O, or lock is introduced on the render path.
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
        // Clock-free attribution, sampled from representative frames rather than
        // only from over-threshold ones. Every field here is a plain increment on
        // a per-frame member, so adding them costs no clock read and cannot
        // inflate the very quantity under investigation -- which is the failure
        // mode that made `flush-pso-us` unattributable in the retained
        // recordings: all 16 of their `[gx-frame-timing]` lines came from frames
        // that had already crossed the stall threshold, so the per-draw
        // `steady_clock` calls they carried were themselves part of the number.
        //
        // `render_ns` above is accumulated only under trace_gx_stats/stalls and
        // therefore reads 0 on a routine sample; the fields below are the ones
        // that stay valid, and they are what separate the two hypotheses:
        //   us_per_flush   = flush_pso_ns / flush_calls
        //   draws_per_call = draws / flush_calls
        // A frame with thousands of calls at ~3 us each and a frame with ~24
        // calls at ~17 ms each have the same total and imply opposite fixes.
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
    // Total samples offered to the ring, including those it did not retain. This
    // is reservoir sampling's N: the ring keeps a uniform draw over the whole
    // session rather than the first `capacity` rows, so the retained rows describe
    // the run instead of only its opening. `dropped` still counts non-retained
    // samples so the report can state the retention ratio.
    std::uint64_t deferred_gx_stats_seen_ = 0;
    // xorshift64* state for the reservoir replacement index. Per-backend so there
    // is no shared state on the render thread, and deterministic so the same
    // session shape yields the same rows.
    std::uint64_t deferred_gx_stats_rng_ = 0x9e3779b97f4a7c15ull;
    // Frame index of the first dropped sample, so the shutdown report can state
    // which part of the session the ring stopped covering. Zero means none.
    std::uint64_t deferred_gx_stats_first_drop_frame_ = 0;
    // The causal XFB trace is intentionally separate from the periodic GX
    // statistics: it captures a caller-selected contiguous render-frame
    // window and writes it only after the render thread has joined.
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
