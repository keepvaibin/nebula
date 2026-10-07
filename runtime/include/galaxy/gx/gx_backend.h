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
};

[[nodiscard]] std::uint32_t pack_sampler_key(
    const TexMode& mode,
    bool generated_mips,
    std::uint8_t mip_levels) noexcept;

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
        // geometry. Cache hits compare these bytes before uploading vertices.
        std::vector<GuestDependencySnapshot> guest_array_snapshots;
    };

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
        std::size_t& captured_bytes);
    [[nodiscard]] static bool decoded_packet_run_dependencies_match(
        GuestMemoryV1* memory,
        const std::vector<
            DecodedPacketRunCacheEntry::GuestDependencySnapshot>& snapshots)
        noexcept;
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
    std::array<std::atomic_uint64_t, kDirtyPageWordCount> dirty_page_words_{};

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
    D3D12_GPU_VIRTUAL_ADDRESS current_vs_constants_ = 0;
    D3D12_GPU_VIRTUAL_ADDRESS current_ps_constants_ = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE current_texture_table_{};
    D3D12_GPU_DESCRIPTOR_HANDLE current_sampler_table_{};
    D3D12_GPU_VIRTUAL_ADDRESS current_matrix_palette_ = 0;
    bool matrix_palette_stale_ = true;
    bool inline_matrices_stale_ = true;
    // Upload/descriptor-ring handles are frame-slot-local. Rebuild them on
    // the first draw after begin_frame(), even when GX register state did not
    // change. EFB copies can also replace a guest-address texture alias
    // without rewriting its BP texture registers.
    bool frame_bindings_dirty_ = true;
    bool texture_bindings_dirty_ = true;
    std::uint8_t current_texture_map_mask_ = 0;
    mutable std::array<VertexLayoutCacheEntry, 8> vertex_layout_cache_{};

    struct TextureBindingKey {
        std::array<std::uint32_t, kMaxTextureMaps * 5u> regs{};
        std::uint8_t map_mask = 0;

        [[nodiscard]] bool operator==(const TextureBindingKey&) const =
            default;
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

        [[nodiscard]] bool operator==(const TextureTableKey&) const = default;
    };
    struct TextureTableKeyHash {
        [[nodiscard]] std::size_t operator()(
            const TextureTableKey& key) const noexcept {
            std::uint64_t hash = 1469598103934665603ull;
            const auto mix = [&hash](std::uint64_t value) {
                for (unsigned byte = 0; byte < 8; ++byte) {
                    hash ^= (value >> (byte * 8u)) & 0xFFu;
                    hash *= 1099511628211ull;
                }
            };
            mix(key.map_mask);
            for (unsigned index = 0; index < kMaxTextureMaps; ++index) {
                mix(static_cast<std::uint64_t>(key.resources[index]));
                mix(key.srv_indices[index]);
                mix(key.widths[index]);
                mix(key.heights[index]);
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
            std::uint64_t hash = 1469598103934665603ull;
            const auto mix = [&hash](std::uint64_t value) {
                for (unsigned byte = 0; byte < 8; ++byte) {
                    hash ^= (value >> (byte * 8u)) & 0xFFu;
                    hash *= 1099511628211ull;
                }
            };
            mix(key.map_mask);
            for (const std::uint32_t reg : key.regs) {
                mix(reg);
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
    std::unordered_map<
        TextureBindingKey,
        TextureBindingTables,
        TextureBindingKeyHash>
        persistent_texture_binding_tables_;
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
    std::uint64_t frame_flush_matrix_us_ = 0;
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
    };
    static constexpr std::size_t kDeferredGxStatsCapacity = 128;
    std::array<DeferredGxStatsSample, kDeferredGxStatsCapacity>
        deferred_gx_stats_{};
    std::size_t deferred_gx_stats_count_ = 0;
    std::uint64_t deferred_gx_stats_dropped_ = 0;
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
