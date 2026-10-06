#include "galaxy/function_async_flight.h"

#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

using Event = galaxy::scheduler::FunctionAsyncEvent;
using Failure = galaxy::scheduler::FunctionAsyncFailure;
using Tracker = galaxy::scheduler::FunctionAsyncFlightTracker;

bool complete_flight(
    Tracker& tracker,
    std::uint32_t info,
    std::uint32_t worker) {
    bool passed = true;
    passed &= tracker.observe(Event::WorkerDequeued, info, worker).accepted;
    passed &= tracker.observe(Event::ExecuteBegin, info, worker).accepted;
    // A scheduler transfer may unwind every native frame here. The tracker is
    // deliberately durable and accepts the exact translated return boundary
    // when the worker continuation is entered later.
    passed &= tracker.observe(Event::ExecuteReturned, info, worker).accepted;
    passed &= tracker.observe(Event::DoneMessagePosted, info, worker, true).accepted;
    passed &= tracker.observe(Event::EndFlagPublished, info, worker, true).accepted;
    const auto reaped = tracker.observe(Event::Reaped, info, 0u);
    passed &= reaped.accepted && reaped.exec_info == info &&
              reaped.worker == worker;
    return passed;
}

}  // namespace

int main() {
    bool passed = true;

    passed &= expect(
        std::string_view(galaxy::scheduler::function_async_event_name(
            Event::DoneMessagePosted)) == "done-message-posted" &&
            std::string_view(galaxy::scheduler::function_async_failure_name(
                Failure::EndFlagNotPublished)) == "end-flag-not-published",
        "proof labels are stable and machine-readable");

    {
        Tracker tracker;
        passed &= expect(
            complete_flight(tracker, 0x81001000u, 0x809C3060u),
            "one exact worker lifecycle survives a native-stack unwind");
        const auto summary = tracker.summary();
        passed &= expect(
            summary.migration_ready() && summary.completed_flights == 1u &&
                summary.active_flights == 0u && summary.observations == 6u,
            "a complete translated lifecycle satisfies the migration gate");
    }

    {
        Tracker tracker;
        passed &= expect(
            tracker.observe(
                Event::WorkerDequeued, 0x81002000u, 0x809C3060u).accepted &&
                tracker.observe(
                    Event::ExecuteBegin, 0x81002000u, 0x809C3060u).accepted,
            "the tracker accepts the exact lifecycle prefix");
        const auto out_of_order = tracker.observe(
            Event::DoneMessagePosted, 0x81002000u, 0x809C3060u, true);
        passed &= expect(
            !out_of_order.accepted &&
                out_of_order.failure == Failure::UnexpectedPhase &&
                !tracker.summary().migration_ready(),
            "skipping the translated execute-return boundary fails closed");
    }

    {
        Tracker tracker;
        constexpr std::uint32_t worker_a = 0x809C3060u;
        constexpr std::uint32_t worker_b = 0x809D3420u;
        constexpr std::uint32_t info_a = 0x81002100u;
        constexpr std::uint32_t info_b = 0x81002200u;
        passed &= expect(
            tracker.observe(Event::WorkerDequeued, info_a, worker_a).accepted &&
                tracker.observe(Event::ExecuteBegin, info_a, worker_a).accepted &&
                tracker.observe(Event::WorkerDequeued, info_b, worker_b).accepted &&
                tracker.observe(Event::ExecuteBegin, info_b, worker_b).accepted &&
                tracker.observe(Event::ExecuteReturned, info_a, worker_a).accepted &&
                tracker.observe(Event::ExecuteReturned, info_b, worker_b).accepted &&
                tracker.observe(Event::DoneMessagePosted, info_b, worker_b).accepted &&
                tracker.observe(Event::EndFlagPublished, info_b, worker_b).accepted &&
                tracker.observe(Event::Reaped, info_b, 0u).accepted &&
                tracker.observe(Event::DoneMessagePosted, info_a, worker_a).accepted &&
                tracker.observe(Event::EndFlagPublished, info_a, worker_a).accepted &&
                tracker.observe(Event::Reaped, info_a, 0u).accepted,
            "interleaved translated workers complete by identity, not LIFO order");
        const auto summary = tracker.summary();
        passed &= expect(
            summary.completed_flights == 2u &&
                summary.active_flights == 0u &&
                summary.protocol_failures == 0u &&
                summary.migration_ready(),
            "interleaved exact lifecycles satisfy the migration gate");
    }

    {
        Tracker tracker;
        static_cast<void>(tracker.observe(
            Event::WorkerDequeued, 0x81003000u, 0x809C3060u));
        const auto wrong_worker = tracker.observe(
            Event::ExecuteBegin, 0x81003000u, 0x809D3420u);
        passed &= expect(
            !wrong_worker.accepted &&
                wrong_worker.failure == Failure::WorkerMismatch,
            "a different worker cannot inherit another worker's job");
    }

    {
        Tracker tracker;
        static_cast<void>(tracker.observe(
            Event::WorkerDequeued, 0x81004000u, 0x809C3060u));
        static_cast<void>(tracker.observe(
            Event::ExecuteBegin, 0x81004000u, 0x809C3060u));
        static_cast<void>(tracker.observe(
            Event::ExecuteReturned, 0x81004000u, 0x809C3060u));
        const auto rejected_send = tracker.observe(
            Event::DoneMessagePosted, 0x81004000u, 0x809C3060u, false);
        passed &= expect(
            !rejected_send.accepted &&
                rejected_send.failure == Failure::DoneMessageRejected,
            "a rejected nonblocking done-message post cannot count as completion");
    }

    {
        Tracker tracker;
        static_cast<void>(tracker.observe(
            Event::WorkerDequeued, 0x81005000u, 0x809C3060u));
        static_cast<void>(tracker.observe(
            Event::ExecuteBegin, 0x81005000u, 0x809C3060u));
        static_cast<void>(tracker.observe(
            Event::ExecuteReturned, 0x81005000u, 0x809C3060u));
        static_cast<void>(tracker.observe(
            Event::DoneMessagePosted, 0x81005000u, 0x809C3060u));
        const auto missing_end = tracker.observe(
            Event::EndFlagPublished, 0x81005000u, 0x809C3060u, false);
        passed &= expect(
            !missing_end.accepted &&
                missing_end.failure == Failure::EndFlagNotPublished,
            "the proof reads back mIsEnd instead of inferring its store");
    }

    {
        Tracker tracker;
        constexpr std::uint32_t info = 0x81005800u;
        constexpr std::uint32_t worker = 0x809C3060u;

        passed &= expect(!tracker.has_active_exec_info(info),
                         "inactive exec-info query starts false");
        const auto dequeued =
            tracker.observe(Event::WorkerDequeued, info, worker);
        passed &= expect(dequeued.accepted &&
                             tracker.has_active_exec_info(info),
                         "dequeued exec-info query is active");
        passed &= expect(
            tracker.observe(Event::ExecuteBegin, info, worker).accepted &&
                tracker.observe(Event::ExecuteReturned, info, worker)
                    .accepted &&
                tracker.observe(Event::DoneMessagePosted, info, worker)
                    .accepted &&
                tracker.observe(Event::EndFlagPublished, info, worker)
                    .accepted,
            "active query lifecycle reaches the reap boundary");
        const auto reaped = tracker.observe(Event::Reaped, info, 0u);
        passed &= expect(reaped.accepted &&
                             !tracker.has_active_exec_info(info),
                         "reaped exec-info query is inactive");
        const auto reused =
            tracker.observe(Event::WorkerDequeued, info, worker);
        passed &= expect(reused.accepted && reused.flight_sequence == 2u &&
                             tracker.has_active_exec_info(info),
                         "reused exec-info query tracks the new flight");
    }

    {
        Tracker tracker;
        passed &= expect(
            complete_flight(tracker, 0x81006000u, 0x809C3060u),
            "the first reused-address lifecycle completes");
        const auto reused = tracker.observe(
            Event::WorkerDequeued, 0x81006000u, 0x809D3420u);
        passed &= expect(
            reused.accepted && reused.flight_sequence == 2u,
            "a reaped exec-info address starts a distinct durable flight");
    }

    if (!passed) {
        return 1;
    }
    std::cout << "FunctionAsync flight tests passed\n";
    return 0;
}
