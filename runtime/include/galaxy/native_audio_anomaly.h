#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace galaxy::host {

enum class NativeAudioAnomalyKind : std::uint8_t {
    AiState,
    Accepted,
    Submitted,
    PlaybackStarted,
    QueueEmpty,
    EmptyRecovered,
    VoiceStart,
    HostPause,
};

// Zero denotes an unobserved/inapplicable field. Host nanoseconds use the
// process steady clock; the host-pause QPC/CPU samples retain their own units.
// This is diagnostics only and must never participate in playback decisions.
struct NativeAudioAnomalyRecord {
    NativeAudioAnomalyKind kind{};
    std::uint64_t host_ns{};
    std::uint64_t buffer_sequence{};
    std::uint64_t accepted_ns{};
    std::uint64_t dequeued_ns{};
    std::uint64_t submit_begin_ns{};
    std::uint64_t previous_submit_ns{};
    std::uint64_t empty_since_ns{};
    std::uint64_t guest_ticks{};
    std::uint64_t duration_ticks{};
    std::uint64_t deadline_ticks{};
    std::uint64_t completed{};
    std::uint64_t qpc_begin{};
    std::uint64_t qpc_end{};
    std::uint64_t qpc_frequency{};
    std::uint64_t cpu_begin_100ns{};
    std::uint64_t cpu_end_100ns{};
    std::uint64_t vi{};
    std::uint64_t last_guest_ticks{};
    std::uint64_t removed_ticks{};
    std::uint64_t prepare_ticks{};
    std::uint32_t address{};
    std::uint32_t bytes{};
    std::uint32_t sample_rate{};
    std::uint32_t queued{};
    std::uint32_t pending{};
    std::uint32_t blocks{};
    std::uint32_t reason{};
    std::uint32_t proof{};
    std::uint32_t boundary_phase{};
};

// Append-only MPSC capture. A producer owns one unique slot and release-
// publishes only after copying every field. Readers never touch an incomplete
// slot, slots are never reused, and overflow never overwrites earlier evidence.
// No allocation, lock, formatting or I/O occurs here. Dump after all producers
// stop; read() is also safe during publication for tests/diagnostic snapshots.
template <std::size_t Capacity>
class NativeAudioAnomalyLedger {
public:
    static_assert(Capacity > 0u);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    static_assert(std::atomic_bool::is_always_lock_free);

    bool record(const NativeAudioAnomalyRecord& record) noexcept {
        const std::uint64_t index = attempted_.fetch_add(
            1u, std::memory_order_relaxed);
        if (index >= Capacity) {
            return false;
        }
        Slot& slot = slots_[static_cast<std::size_t>(index)];
        slot.record = record;
        slot.ready.store(true, std::memory_order_release);
        return true;
    }

    bool read(std::size_t index, NativeAudioAnomalyRecord& result) const noexcept {
        if (index >= Capacity ||
            !slots_[index].ready.load(std::memory_order_acquire)) {
            return false;
        }
        result = slots_[index].record;
        return true;
    }

    std::uint64_t attempted() const noexcept {
        return attempted_.load(std::memory_order_acquire);
    }

    static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    struct Slot {
        NativeAudioAnomalyRecord record{};
        std::atomic_bool ready{false};
    };
    std::array<Slot, Capacity> slots_{};
    std::atomic_uint64_t attempted_{};
};

}  // namespace galaxy::host
