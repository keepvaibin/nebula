#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace galaxy::scheduler {

// Passive diagnostic state for RMGE01's translated FunctionAsyncExecutor worker.
// None of this state is consulted by guest scheduling or game-visible memory.
enum class FunctionAsyncEvent : std::uint32_t {
    WorkerDequeued,
    ExecuteBegin,
    ExecuteReturned,
    DoneMessagePosted,
    EndFlagPublished,
    Reaped,
};

enum class FunctionAsyncPhase : std::uint32_t {
    Empty,
    WorkerDequeued,
    ExecuteBegun,
    ExecuteReturned,
    DoneMessagePosted,
    EndFlagPublished,
    Reaped,
};

enum class FunctionAsyncFailure : std::uint32_t {
    None,
    NullExecInfo,
    NullWorker,
    CapacityExceeded,
    DuplicateExecInfo,
    MissingExecInfo,
    UnexpectedPhase,
    WorkerMismatch,
    DoneMessageRejected,
    EndFlagNotPublished,
};

[[nodiscard]] constexpr const char* function_async_event_name(
    FunctionAsyncEvent event) noexcept {
    switch (event) {
        case FunctionAsyncEvent::WorkerDequeued: return "worker-dequeued";
        case FunctionAsyncEvent::ExecuteBegin: return "execute-begin";
        case FunctionAsyncEvent::ExecuteReturned: return "execute-returned";
        case FunctionAsyncEvent::DoneMessagePosted: return "done-message-posted";
        case FunctionAsyncEvent::EndFlagPublished: return "end-flag-published";
        case FunctionAsyncEvent::Reaped: return "reaped";
    }
    return "unknown";
}

[[nodiscard]] constexpr const char* function_async_failure_name(
    FunctionAsyncFailure failure) noexcept {
    switch (failure) {
        case FunctionAsyncFailure::None: return "none";
        case FunctionAsyncFailure::NullExecInfo: return "null-exec-info";
        case FunctionAsyncFailure::NullWorker: return "null-worker";
        case FunctionAsyncFailure::CapacityExceeded: return "capacity-exceeded";
        case FunctionAsyncFailure::DuplicateExecInfo: return "duplicate-exec-info";
        case FunctionAsyncFailure::MissingExecInfo: return "missing-exec-info";
        case FunctionAsyncFailure::UnexpectedPhase: return "unexpected-phase";
        case FunctionAsyncFailure::WorkerMismatch: return "worker-mismatch";
        case FunctionAsyncFailure::DoneMessageRejected: return "done-message-rejected";
        case FunctionAsyncFailure::EndFlagNotPublished: return "end-flag-not-published";
    }
    return "unknown";
}

struct FunctionAsyncObservation {
    bool accepted{};
    FunctionAsyncFailure failure{FunctionAsyncFailure::None};
    FunctionAsyncEvent event{};
    FunctionAsyncPhase phase_before{FunctionAsyncPhase::Empty};
    FunctionAsyncPhase phase_after{FunctionAsyncPhase::Empty};
    std::uint64_t flight_sequence{};
    std::uint32_t exec_info{};
    std::uint32_t worker{};
};

struct FunctionAsyncSummary {
    std::uint64_t observations{};
    std::uint64_t completed_flights{};
    std::uint64_t active_flights{};
    std::uint64_t protocol_failures{};
    FunctionAsyncFailure last_failure{FunctionAsyncFailure::None};

    [[nodiscard]] constexpr bool migration_ready() const noexcept {
        return completed_flights != 0u && protocol_failures == 0u;
    }
};

// Two translated workers exist in RMGE01. A larger fixed table makes address
// reuse explicit while remaining allocation-free and deterministic under
// diagnostics. Reaped entries are immediately reusable with a new sequence.
class FunctionAsyncFlightTracker final {
public:
    static constexpr std::size_t kCapacity = 64u;

    [[nodiscard]] FunctionAsyncObservation observe(
        FunctionAsyncEvent event,
        std::uint32_t exec_info,
        std::uint32_t worker,
        bool boundary_succeeded = true) noexcept {
        ++observations_;

        if (exec_info == 0u) {
            return reject(
                event,
                FunctionAsyncFailure::NullExecInfo,
                exec_info,
                worker,
                FunctionAsyncPhase::Empty);
        }

        if (event == FunctionAsyncEvent::WorkerDequeued) {
            if (worker == 0u) {
                return reject(
                    event,
                    FunctionAsyncFailure::NullWorker,
                    exec_info,
                    worker,
                    FunctionAsyncPhase::Empty);
            }
            if (const Entry* existing = find_active(exec_info)) {
                return reject(
                    event,
                    FunctionAsyncFailure::DuplicateExecInfo,
                    exec_info,
                    worker,
                    existing->phase,
                    existing->sequence);
            }
            Entry* entry = find_reusable();
            if (entry == nullptr) {
                return reject(
                    event,
                    FunctionAsyncFailure::CapacityExceeded,
                    exec_info,
                    worker,
                    FunctionAsyncPhase::Empty);
            }
            if (next_sequence_ == 0u) {
                ++next_sequence_;
            }
            *entry = Entry{
                true,
                next_sequence_++,
                exec_info,
                worker,
                FunctionAsyncPhase::WorkerDequeued};
            return accept(
                event,
                *entry,
                FunctionAsyncPhase::Empty,
                FunctionAsyncPhase::WorkerDequeued);
        }

        Entry* entry = find_active(exec_info);
        if (entry == nullptr) {
            return reject(
                event,
                FunctionAsyncFailure::MissingExecInfo,
                exec_info,
                worker,
                FunctionAsyncPhase::Empty);
        }

        if (event != FunctionAsyncEvent::Reaped && worker != entry->worker) {
            return reject(
                event,
                FunctionAsyncFailure::WorkerMismatch,
                exec_info,
                worker,
                entry->phase,
                entry->sequence);
        }

        const FunctionAsyncPhase expected = expected_phase(event);
        if (entry->phase != expected) {
            return reject(
                event,
                FunctionAsyncFailure::UnexpectedPhase,
                exec_info,
                worker,
                entry->phase,
                entry->sequence);
        }
        if (event == FunctionAsyncEvent::DoneMessagePosted &&
            !boundary_succeeded) {
            return reject(
                event,
                FunctionAsyncFailure::DoneMessageRejected,
                exec_info,
                worker,
                entry->phase,
                entry->sequence);
        }
        if (event == FunctionAsyncEvent::EndFlagPublished &&
            !boundary_succeeded) {
            return reject(
                event,
                FunctionAsyncFailure::EndFlagNotPublished,
                exec_info,
                worker,
                entry->phase,
                entry->sequence);
        }

        const FunctionAsyncPhase before = entry->phase;
        entry->phase = resulting_phase(event);
        if (event == FunctionAsyncEvent::Reaped) {
            ++completed_flights_;
        }
        return accept(event, *entry, before, entry->phase);
    }

    [[nodiscard]] bool has_active_exec_info(
        std::uint32_t exec_info) const noexcept {
        return find_active(exec_info) != nullptr;
    }

    [[nodiscard]] FunctionAsyncSummary summary() const noexcept {
        std::uint64_t active = 0u;
        for (const Entry& entry : entries_) {
            if (entry.occupied && entry.phase != FunctionAsyncPhase::Reaped) {
                ++active;
            }
        }
        return FunctionAsyncSummary{
            observations_,
            completed_flights_,
            active,
            protocol_failures_,
            last_failure_};
    }

private:
    struct Entry {
        bool occupied{};
        std::uint64_t sequence{};
        std::uint32_t exec_info{};
        std::uint32_t worker{};
        FunctionAsyncPhase phase{FunctionAsyncPhase::Empty};
    };

    [[nodiscard]] static constexpr FunctionAsyncPhase expected_phase(
        FunctionAsyncEvent event) noexcept {
        switch (event) {
            case FunctionAsyncEvent::ExecuteBegin:
                return FunctionAsyncPhase::WorkerDequeued;
            case FunctionAsyncEvent::ExecuteReturned:
                return FunctionAsyncPhase::ExecuteBegun;
            case FunctionAsyncEvent::DoneMessagePosted:
                return FunctionAsyncPhase::ExecuteReturned;
            case FunctionAsyncEvent::EndFlagPublished:
                return FunctionAsyncPhase::DoneMessagePosted;
            case FunctionAsyncEvent::Reaped:
                return FunctionAsyncPhase::EndFlagPublished;
            case FunctionAsyncEvent::WorkerDequeued:
                return FunctionAsyncPhase::Empty;
        }
        return FunctionAsyncPhase::Empty;
    }

    [[nodiscard]] static constexpr FunctionAsyncPhase resulting_phase(
        FunctionAsyncEvent event) noexcept {
        switch (event) {
            case FunctionAsyncEvent::WorkerDequeued:
                return FunctionAsyncPhase::WorkerDequeued;
            case FunctionAsyncEvent::ExecuteBegin:
                return FunctionAsyncPhase::ExecuteBegun;
            case FunctionAsyncEvent::ExecuteReturned:
                return FunctionAsyncPhase::ExecuteReturned;
            case FunctionAsyncEvent::DoneMessagePosted:
                return FunctionAsyncPhase::DoneMessagePosted;
            case FunctionAsyncEvent::EndFlagPublished:
                return FunctionAsyncPhase::EndFlagPublished;
            case FunctionAsyncEvent::Reaped:
                return FunctionAsyncPhase::Reaped;
        }
        return FunctionAsyncPhase::Empty;
    }

    [[nodiscard]] Entry* find_active(std::uint32_t exec_info) noexcept {
        for (Entry& entry : entries_) {
            if (entry.occupied && entry.exec_info == exec_info &&
                entry.phase != FunctionAsyncPhase::Reaped) {
                return &entry;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const Entry* find_active(
        std::uint32_t exec_info) const noexcept {
        for (const Entry& entry : entries_) {
            if (entry.occupied && entry.exec_info == exec_info &&
                entry.phase != FunctionAsyncPhase::Reaped) {
                return &entry;
            }
        }
        return nullptr;
    }

    [[nodiscard]] Entry* find_reusable() noexcept {
        for (Entry& entry : entries_) {
            if (!entry.occupied || entry.phase == FunctionAsyncPhase::Reaped) {
                return &entry;
            }
        }
        return nullptr;
    }

    [[nodiscard]] FunctionAsyncObservation accept(
        FunctionAsyncEvent event,
        const Entry& entry,
        FunctionAsyncPhase before,
        FunctionAsyncPhase after) const noexcept {
        return FunctionAsyncObservation{
            true,
            FunctionAsyncFailure::None,
            event,
            before,
            after,
            entry.sequence,
            entry.exec_info,
            entry.worker};
    }

    [[nodiscard]] FunctionAsyncObservation reject(
        FunctionAsyncEvent event,
        FunctionAsyncFailure failure,
        std::uint32_t exec_info,
        std::uint32_t worker,
        FunctionAsyncPhase before,
        std::uint64_t sequence = 0u) noexcept {
        ++protocol_failures_;
        last_failure_ = failure;
        return FunctionAsyncObservation{
            false,
            failure,
            event,
            before,
            before,
            sequence,
            exec_info,
            worker};
    }

    std::array<Entry, kCapacity> entries_{};
    std::uint64_t next_sequence_{1u};
    std::uint64_t observations_{};
    std::uint64_t completed_flights_{};
    std::uint64_t protocol_failures_{};
    FunctionAsyncFailure last_failure_{FunctionAsyncFailure::None};
};

}  // namespace galaxy::scheduler
