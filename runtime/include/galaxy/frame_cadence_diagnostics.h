#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <type_traits>

namespace galaxy::cadence {

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

inline constexpr std::size_t kInlineCheckpointGapBucketCount = 7u;

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

    void record_phase(TimingPhase phase, std::uint64_t elapsed_ns) noexcept;
    void record_token_issued(FrameTokenKind kind) noexcept;

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

[[nodiscard]] std::uint64_t sample_now_ns_if_enabled(
    const Session* session,
    MonotonicReadFn reader,
    void* user) noexcept;
[[nodiscard]] std::uint64_t steady_now_ns_if_enabled(
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

[[nodiscard]] Session* global_session_if_enabled() noexcept;

}  // namespace galaxy::cadence
