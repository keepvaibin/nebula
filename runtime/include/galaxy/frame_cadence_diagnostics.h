// Phase timers and frame-token counters for the GX pipeline (gx_backend.cpp). They record into the active Session;
// the host installs none, so each timer costs a null check.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace galaxy::cadence {

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
    WorkerDependencyScan,
    WorkerSnapshotCopy,
    RenderTextureInvalidation,
    RenderParserLockWait,
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
    std::array<TimingAccumulator, kTimingPhaseCount> phases_{};
    std::atomic_uint64_t event_free_issued_{0};
    std::atomic_uint64_t event_bearing_issued_{0};
};

// Steady-clock nanoseconds from an arbitrary origin when `session` records, else 0.
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
