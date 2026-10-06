#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <type_traits>

namespace galaxy::cadence {

// This diagnostic is bounded and allocation-free while frames
// are running. Text is produced only by dump_report(), after the benchmark has
// drained or during shutdown.
inline constexpr std::uint32_t kReportSchemaVersion = 4u;
inline constexpr std::size_t kViRecordCapacity = 8'192u;
inline constexpr std::size_t kGuestThreadAttributionCapacity = 128u;
inline constexpr std::size_t kGuestPcAttributionCapacity = 8'192u;
inline constexpr std::size_t kGuestParentAttributionCapacity = 1'024u;

enum class FrameTokenKind : std::uint8_t {
    None,
    EventFree,
    EventBearing,
};

enum class TimingPhase : std::uint8_t {
    ProducerQueueWait,
    LiveMemoryReadBarrierWait,
    RenderBegin,
    RenderCacheInvalidation,
    RenderFifoParse,
    RenderPostParseFlush,
    PeFenceWait,
    PeConsumerWait,
    GuestCheckpointCallback,
    DspMramServiceWake,
    DspCheckpointPreflightPoll,
    DspCheckpointPublicationPoll,
    DspCheckpointPreflightYield,
    DspCheckpointPublicationYield,
    WorkerDependencyScan,
    WorkerSnapshotCopy,
    RenderTextureInvalidation,
    RenderParserLockWait,
    RenderDisplayListInvalidation,
    RenderDecodedVertexInvalidation,
    GuestTransferUnwind,
    EfbPeekSynchronization,
    EfbPeekReadback,
    Count,
};

// Internal host diagnostic origin; neither this enum nor its counters are part
// of the generated game-module ABI.
enum class DspPollOrigin : std::uint8_t {
    Other,
    CheckpointPreflight,
    CheckpointPublication,
};

[[nodiscard]] constexpr TimingPhase dsp_poll_timing_phase(
    DspPollOrigin origin, bool yield) noexcept {
    switch (origin) {
    case DspPollOrigin::CheckpointPreflight:
        return yield ? TimingPhase::DspCheckpointPreflightYield
                     : TimingPhase::DspCheckpointPreflightPoll;
    case DspPollOrigin::CheckpointPublication:
        return yield ? TimingPhase::DspCheckpointPublicationYield
                     : TimingPhase::DspCheckpointPublicationPoll;
    case DspPollOrigin::Other:
        return TimingPhase::Count;
    }
    return TimingPhase::Count;
}

inline constexpr std::array<TimingPhase, 4u> kDspPollTimingPhases{
    TimingPhase::DspCheckpointPreflightPoll,
    TimingPhase::DspCheckpointPublicationPoll,
    TimingPhase::DspCheckpointPreflightYield,
    TimingPhase::DspCheckpointPublicationYield};

// Cumulative completed-scope totals, captured by the simulation owner at frame
// boundaries. A yield is included in its corresponding poll;
// do not add the two wall totals. No histogram is copied into each frame record.
struct DspPollTimingSnapshot {
    std::array<std::uint64_t, kDspPollTimingPhases.size()> counts{};
    std::array<std::uint64_t, kDspPollTimingPhases.size()> total_ns{};
    bool valid{};
};

inline constexpr std::size_t kTimingPhaseCount =
    static_cast<std::size_t>(TimingPhase::Count);

// Inclusive upper bounds in nanoseconds. The final bucket is overflow.
inline constexpr std::array<std::uint64_t, 11>
    kTimingHistogramUpperBoundsNs{
        1'000u,
        4'000u,
        16'000u,
        64'000u,
        256'000u,
        1'000'000u,
        2'000'000u,
        4'000'000u,
        8'000'000u,
        16'000'000u,
        33'000'000u,
    };
inline constexpr std::size_t kTimingHistogramBucketCount =
    kTimingHistogramUpperBoundsNs.size() + 1u;

enum class InlineCheckpointScheduleCause : std::uint8_t {
    ForcedNext,
    PeriodicViPoll,
    ViReadinessProbe,
    ViDeadline,
    InputDeadline,
    RuntimeFailure,
    DspDeadline,
    Decrementer,
    AudioDeadline,
    AudioFallback,
    HostPump,
    IpcPending,
    DspInterfacePending,
    PePending,
    Count,
};

inline constexpr std::size_t kInlineCheckpointScheduleCauseCount =
    static_cast<std::size_t>(InlineCheckpointScheduleCause::Count);
static_assert(kInlineCheckpointScheduleCauseCount <= 32u);

[[nodiscard]] constexpr std::uint32_t inline_checkpoint_schedule_cause_bit(
    InlineCheckpointScheduleCause cause) noexcept {
    return std::uint32_t{1u} << static_cast<std::uint32_t>(cause);
}

inline constexpr std::size_t kInlineCheckpointGapBucketCount = 7u;

[[nodiscard]] constexpr std::size_t inline_checkpoint_gap_bucket(
    std::uint64_t gap) noexcept {
    if (gap <= 1u) {
        return 0u;
    }
    if (gap <= 4u) {
        return 1u;
    }
    if (gap <= 16u) {
        return 2u;
    }
    if (gap <= 256u) {
        return 3u;
    }
    if (gap <= 2'047u) {
        return 4u;
    }
    if (gap <= 8'192u) {
        return 5u;
    }
    return 6u;
}

[[nodiscard]] constexpr std::size_t timing_histogram_bucket_for_ns(
    std::uint64_t elapsed_ns) noexcept {
    for (std::size_t index = 0;
         index < kTimingHistogramUpperBoundsNs.size();
         ++index) {
        if (elapsed_ns <= kTimingHistogramUpperBoundsNs[index]) {
            return index;
        }
    }
    return kTimingHistogramUpperBoundsNs.size();
}

struct ViRecord {
    std::uint64_t boundary_serial{};
    std::uint64_t deadline_sequence{};
    std::uint64_t deadline_ticks{};
    std::uint64_t deadline_actual_ticks{};

    // All host timestamps use steady-clock nanoseconds from an arbitrary
    // process-local origin. Only differences are meaningful.
    std::uint64_t edge_consumed_ns{};
    std::uint64_t capture_ns{};
    std::uint64_t submit_begin_ns{};
    std::uint64_t submit_end_ns{};
    std::uint64_t token_complete_ns{};
    std::uint64_t vi_latched_ns{};
    std::uint64_t finalize_ns{};
    std::uint64_t guest_interval_ns{};

    std::uint64_t checkpoint_begin{};
    std::uint64_t checkpoint_end{};
    std::uint64_t fifo_bytes{};
    std::uint64_t xfb_copy_serial_before{};
    std::uint64_t xfb_copy_serial_after{};
    std::uint64_t token_epoch{};
    std::uint64_t token_value{};
    std::uint64_t token_wait_ns{};

    std::uint32_t selected_xfb_addr{};
    std::uint32_t jut_manager{};
    std::array<std::uint32_t, 3> jut_slot_addrs{};
    std::int16_t jut_displaying_slot{-1};
    std::uint16_t jut_buffer_count{};
    std::uint32_t token_wait_slices{};
    FrameTokenKind token_kind{FrameTokenKind::None};
    bool token_completed{};
    // Completion may be observed after IRQ24 RFI; token_wait_ns remains zero.
    bool token_detached{};
    bool rendered_retrace{};
};

static_assert(std::is_trivially_copyable_v<ViRecord>);
static_assert(std::is_standard_layout_v<ViRecord>);

struct TimingSnapshot {
    std::uint64_t count{};
    std::uint64_t total_ns{};
    std::uint64_t max_ns{};
    std::array<std::uint64_t, kTimingHistogramBucketCount> buckets{};
};

struct TokenSnapshot {
    std::uint64_t event_free_issued{};
    std::uint64_t event_bearing_issued{};
    std::uint64_t consumer_completed{};
    std::uint64_t consumer_wait_slices{};
};

struct GuestThroughputSummary {
    std::uint64_t samples{};
    std::uint64_t checkpoint_delta{};
    std::uint64_t event_pending_samples{};
    std::uint64_t dsp_pending_samples{};
    std::uint64_t inline_gate_samples{};
    std::uint64_t full_gate_samples{};
    std::uint64_t root_callback_samples{};
    std::uint64_t nested_callback_samples{};
    std::uint64_t root_full_gate_samples{};
    std::uint64_t nested_full_gate_samples{};
    std::uint64_t maximum_enclosing_callback_depth{};
    std::uint64_t maximum_gate_full_scope_depth{};
    std::uint64_t maximum_gate_inline_scope_depth{};
    std::uint64_t checkpoint_regressions{};
    std::uint64_t thread_table_overflows{};
    std::uint64_t pc_table_overflows{};
    std::uint64_t parent_table_overflows{};
    std::uint64_t thread_entries{};
    std::uint64_t pc_entries{};
    std::uint64_t parent_entries{};
};

struct GuestThreadAttribution {
    std::uint64_t checkpoints{};
    std::uint64_t samples{};
    std::uint32_t thread{};
    std::uint32_t run_queue_bits{};
    bool occupied{};
};

struct GuestPcAttribution {
    std::uint64_t checkpoints{};
    std::uint64_t samples{};
    std::uint32_t pc{};
    bool occupied{};
};

struct GuestParentAttribution {
    std::uint64_t samples{};
    std::uint32_t parent_pc{};
    bool occupied{};
};

struct InlineCheckpointScheduleSummary {
    std::uint64_t samples{};
    std::uint64_t unattributed_samples{};
    std::array<std::uint64_t, kInlineCheckpointGapBucketCount> gap_buckets{};
    std::array<std::uint64_t, kInlineCheckpointScheduleCauseCount>
        cause_samples{};
};

class TimingAccumulator {
public:
    void record(std::uint64_t elapsed_ns) noexcept;
    void reset() noexcept;
    [[nodiscard]] TimingSnapshot snapshot() const noexcept;

private:
    std::atomic_uint64_t count_{0};
    std::atomic_uint64_t total_ns_{0};
    std::atomic_uint64_t max_ns_{0};
    std::array<std::atomic_uint64_t, kTimingHistogramBucketCount> buckets_{};
};

class Session {
public:
    explicit Session(bool enabled = true) noexcept : enabled_(enabled) {}

    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    void reset() noexcept;

    // One simulation-thread writer owns the VI ring. Callers must dump only
    // after the exact benchmark drain or after guest execution has stopped.
    void push_vi_record(const ViRecord& record) noexcept;
    [[nodiscard]] std::uint64_t total_vi_records() const noexcept {
        return total_vi_records_;
    }
    [[nodiscard]] std::size_t retained_vi_records() const noexcept;
    [[nodiscard]] std::uint64_t overwritten_vi_records() const noexcept;
    [[nodiscard]] const ViRecord& retained_vi_record(
        std::size_t chronological_index) const;

    // One simulation writer updates an already retained exact receipt. This
    // records readiness observation after VI finalization, never a wait time.
    [[nodiscard]] bool complete_detached_vi_token(
        std::uint64_t serial, std::uint64_t epoch, std::uint64_t value,
        std::uint64_t completed_ns) noexcept;

    void record_phase(TimingPhase phase, std::uint64_t elapsed_ns) noexcept;
    [[nodiscard]] TimingSnapshot phase_snapshot(
        TimingPhase phase) const noexcept;
    void record_token_issued(FrameTokenKind kind) noexcept;
    void record_consumer_completion(std::uint32_t wait_slices) noexcept;
    [[nodiscard]] TokenSnapshot token_snapshot() const noexcept;

    // Called only at the translated branch gate's naturally selected slow
    // callbacks. The fixed tables attribute the checkpoint delta ending at
    // each sample without allocating, reading a clock, or emitting text.
    void record_guest_checkpoint(
        std::uint64_t checkpoint,
        std::uint32_t current_thread,
        std::uint32_t run_queue_bits,
        std::uint32_t guest_pc,
        bool event_pending,
        bool dsp_pending,
        bool inline_gate_published,
        std::uint64_t enclosing_callback_depth,
        std::uint32_t parent_guest_pc,
        std::uint64_t gate_full_scope_depth,
        std::uint64_t gate_inline_scope_depth) noexcept;
    [[nodiscard]] GuestThroughputSummary guest_throughput_summary() const noexcept;
    void record_inline_checkpoint_schedule(
        std::uint64_t scheduled_gap,
        std::uint32_t limiting_cause_mask) noexcept;
    [[nodiscard]] InlineCheckpointScheduleSummary
    inline_checkpoint_schedule_summary() const noexcept;

    void dump_report(std::ostream& output, const char* tag) const;

private:
    bool enabled_{};
    std::array<ViRecord, kViRecordCapacity> vi_records_{};
    std::uint64_t total_vi_records_{};
    std::array<TimingAccumulator, kTimingPhaseCount> phases_{};
    std::atomic_uint64_t event_free_issued_{0};
    std::atomic_uint64_t event_bearing_issued_{0};
    std::atomic_uint64_t consumer_completed_{0};
    std::atomic_uint64_t consumer_wait_slices_{0};
    std::array<GuestThreadAttribution, kGuestThreadAttributionCapacity>
        guest_thread_attribution_{};
    std::array<GuestPcAttribution, kGuestPcAttributionCapacity>
        guest_pc_attribution_{};
    std::array<GuestParentAttribution, kGuestParentAttributionCapacity>
        guest_parent_attribution_{};
    GuestThroughputSummary guest_throughput_{};
    InlineCheckpointScheduleSummary inline_checkpoint_schedule_{};
    std::uint64_t last_guest_checkpoint_{};
    bool last_guest_checkpoint_valid_{};
};

using MonotonicReadFn = std::uint64_t (*)(void*) noexcept;

// A null/disabled session returns zero without calling the clock reader.
[[nodiscard]] std::uint64_t sample_now_ns_if_enabled(
    const Session* session,
    MonotonicReadFn reader,
    void* user) noexcept;
[[nodiscard]] std::uint64_t steady_now_ns_if_enabled(
    const Session* session) noexcept;
// Disabled/null sessions return an invalid zero snapshot without clock reads
// or counter updates. The four measured phases have one simulation writer.
[[nodiscard]] DspPollTimingSnapshot snapshot_dsp_poll_timing(
    const Session* session) noexcept;

class ScopedPhaseTimer {
public:
    ScopedPhaseTimer(Session* session, TimingPhase phase) noexcept;
    ScopedPhaseTimer(const ScopedPhaseTimer&) = delete;
    ScopedPhaseTimer& operator=(const ScopedPhaseTimer&) = delete;
    ~ScopedPhaseTimer() noexcept;

private:
    Session* session_{};
    TimingPhase phase_{TimingPhase::ProducerQueueWait};
    std::uint64_t start_ns_{};
};

// Configured exactly once by NebulaRuntime before GX initialization. The
// disabled path publishes nullptr so render hot paths perform no clock read,
// atomic update, allocation, or I/O.
void configure_global_session(bool enabled) noexcept;
[[nodiscard]] Session* global_session_if_enabled() noexcept;

[[nodiscard]] const char* timing_phase_name(TimingPhase phase) noexcept;
[[nodiscard]] const char* frame_token_kind_name(FrameTokenKind kind) noexcept;
[[nodiscard]] const char* inline_checkpoint_schedule_cause_name(
    InlineCheckpointScheduleCause cause) noexcept;

}  // namespace galaxy::cadence
