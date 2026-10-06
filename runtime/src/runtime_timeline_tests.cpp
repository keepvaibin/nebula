#include "galaxy/ai_dma_timing.h"
#include "galaxy/runtime_timeline.h"
#include "galaxy/relative_vi_window.h"
#include "galaxy/psmtx_guard_trace.h"
#include "galaxy/vi_boundary.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <intrin.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <exception>
#include <limits>
#include <sstream>
#include <string>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

namespace {

template <class Ready>
bool await_handshake(Ready ready, std::atomic_bool& clean,
                     std::chrono::steady_clock::time_point deadline) {
    while (!ready()) {
        if (!clean.load(std::memory_order_acquire)) return false;
        if (std::chrono::steady_clock::now() >= deadline) {
            clean.store(false, std::memory_order_release);
            return false;
        }
        std::this_thread::yield();
    }
    return clean.load(std::memory_order_acquire);
}

bool run_test(const char* name, bool (*test)()) noexcept {
    try { return test(); }
    catch (const std::exception& failure) {
        std::cerr << name << ": exception: " << failure.what() << '\n';
    } catch (...) {
        std::cerr << name << ": unknown exception\n";
    }
    return false;
}

struct ManualCounter {
    std::uint64_t value{};
    std::atomic_uint64_t reads{};
};

std::uint64_t read_manual_counter(void* user) noexcept {
    auto& counter = *static_cast<ManualCounter*>(user);
    counter.reads.fetch_add(1u, std::memory_order_relaxed);
    return counter.value;
}

struct AtomicManualCounter {
    std::atomic_uint64_t value{};
};

std::uint64_t read_atomic_manual_counter(void* user) noexcept {
    return static_cast<AtomicManualCounter*>(user)->value.load(
        std::memory_order_acquire);
}

struct WorkerPriorityCounter {
    DWORD owner_thread_id{};
    std::atomic_int observed_worker_priority{THREAD_PRIORITY_ERROR_RETURN};
    std::atomic_bool worker_observed{false};
    std::uint64_t value{};
};

std::uint64_t read_worker_priority_counter(void* user) noexcept {
    auto& counter = *static_cast<WorkerPriorityCounter*>(user);
    if (GetCurrentThreadId() != counter.owner_thread_id) {
        const int priority = GetThreadPriority(GetCurrentThread());
        counter.observed_worker_priority.store(
            priority, std::memory_order_release);
        counter.worker_observed.store(true, std::memory_order_release);
    }
    return counter.value;
}

bool expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

bool counter_scale_matches_wide_integer_oracle() {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    const auto check = [](std::uint64_t delta, std::uint64_t frequency) {
        bool expected_saturation = false;
        std::uint64_t expected = maximum;
        if (frequency == 0 || frequency > 1'000'000'000ull) {
            expected_saturation = true;
        } else {
            std::uint64_t high = 0, remainder = 0;
            const auto low = _umul128(delta,
                galaxy::timing::kTimelineTicksPerSecond, &high);
            if (high >= frequency) {
                expected_saturation = true;
            } else {
                expected = _udiv128(high, low, frequency, &remainder);
            }
        }
        bool saturated = !expected_saturation;
        return expect(galaxy::timing::scale_counter_delta(
                delta, frequency, &saturated) == expected &&
            saturated == expected_saturation &&
            galaxy::timing::scale_counter_delta(
                delta, frequency, nullptr) == expected,
            "counter conversion matches independent 128-bit floor and saturation");
    };
    constexpr std::array<std::uint64_t, 11> frequencies{
        0, 1, 40, 243, 9'999'999, 10'000'000, 10'000'001,
        60'750'000, 1'000'000'000, 1'000'000'001, maximum};
    constexpr std::array<std::uint64_t, 13> deltas{
        0, 1, 39, 40, 41, 9'999'999, 10'000'000, 36'000'000'000,
        maximum / 243 - 1, maximum / 243, maximum / 243 + 1,
        maximum - 1, maximum};
    for (const auto frequency : frequencies) {
        for (const auto delta : deltas) {
            if (!check(delta, frequency)) return false;
        }
    }
    std::uint64_t seed = 0xa2847cd9750213ull;
    for (std::uint64_t index = 0; index < 100'000; ++index) {
        seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
        const auto frequency = index % 3 == 0 ? 10'000'000ull :
            frequencies[static_cast<std::size_t>(index % frequencies.size())];
        if (!check(seed, frequency)) return false;
    }
    return true;
}

bool timeline_exactness() {
    constexpr std::uint64_t kCounterFrequency = 10'000'000ull;
    ManualCounter counter{1234};
    galaxy::timing::RuntimeTimeline timeline(
        {&read_manual_counter, &counter, kCounterFrequency});

    counter.value += kCounterFrequency;
    std::uint64_t sampled_counter = 0u;
    bool passed = expect(
        timeline.now_ticks(&sampled_counter) ==
            galaxy::timing::kTimelineTicksPerSecond &&
            sampled_counter == counter.value &&
            counter.reads.load(std::memory_order_relaxed) == 2u,
        "one counter second is exactly 60,750,000 timeline ticks");

    counter.value = 1234 + kCounterFrequency * 60ull * 60ull;
    passed &= expect(
        timeline.now_ticks() ==
            galaxy::timing::kTimelineTicksPerSecond * 60ull * 60ull,
        "one hour has no timeline conversion drift");

    counter.value = 1;
    passed &= expect(
        timeline.now_ticks(&sampled_counter) ==
            galaxy::timing::kTimelineTicksPerSecond * 60ull * 60ull &&
            sampled_counter == 1u,
        "raw diagnostic counter retains regression while guest time stays monotonic");
    passed &= expect(!timeline.saturated(), "ordinary timeline does not saturate");
    return passed;
}

bool timeline_rebase_excludes_verified_host_suspension() {
    ManualCounter counter{};
    galaxy::timing::RuntimeTimeline timeline(
        {&read_manual_counter,
         &counter,
         galaxy::timing::kTimelineTicksPerSecond});

    counter.value = 10'000u;
    const std::uint64_t last_guest_ticks = timeline.now_ticks();
    timeline.commit_guest_observation(last_guest_ticks);
    counter.value += galaxy::timing::kViPeriodTicks * 8u;
    const std::uint64_t observed_host_ticks = timeline.now_ticks();

    bool passed = expect(
        observed_host_ticks - last_guest_ticks ==
            galaxy::timing::kViPeriodTicks * 8u,
        "unrebased timeline accounts for every host-counter tick");
    passed &= expect(
        timeline.rebase_after_host_suspension(
            last_guest_ticks, observed_host_ticks),
        "verified host suspension rebases the shared timeline");
    passed &= expect(
        timeline.now_ticks() == last_guest_ticks &&
            timeline.committed_guest_ticks() == last_guest_ticks,
        "rebase restores the last guest-visible tick without a regression");

    counter.value += galaxy::timing::kViPeriodTicks;
    passed &= expect(
        timeline.now_ticks() ==
            last_guest_ticks + galaxy::timing::kViPeriodTicks,
        "timeline resumes at the original exact periodic phase after rebase");
    return passed;
}

bool timeline_rejects_recovery_after_device_observation() {
    ManualCounter counter{};
    galaxy::timing::RuntimeTimeline timeline(
        {&read_manual_counter, &counter,
         galaxy::timing::kTimelineTicksPerSecond});
    // The control capture's production/delivery pair: acquisition stalled
    // after the sample was produced, and recovery was deferred until after
    // IOS delivery had committed the later timestamp.
    constexpr std::uint64_t kProduction = 4'293'302'676u;
    constexpr std::uint64_t kDelivery = 4'294'676'228u;
    counter.value = kProduction;
    timeline.commit_guest_observation(timeline.now_ticks());
    counter.value = kDelivery;
    const auto delivery = timeline.now_ticks();
    timeline.commit_guest_observation(delivery);
    timeline.commit_guest_observation(delivery);

    bool passed = expect(
        !timeline.can_rebase_after_host_suspension(kProduction, delivery) &&
            !timeline.rebase_after_host_suspension(kProduction, delivery) &&
            timeline.now_ticks() == delivery &&
            timeline.committed_guest_ticks() == delivery,
        "a committed HID delivery rejects a recovery anchored at its earlier production without rewinding time");
    bool regression_rejected = false;
    try {
        timeline.commit_guest_observation(kProduction);
    } catch (const std::runtime_error&) {
        regression_rejected = true;
    }
    passed &= expect(
        regression_rejected && timeline.committed_guest_ticks() == delivery,
        "device observation regression hard-fails without changing the committed clock");

    counter.value += galaxy::timing::kViPeriodTicks;
    const auto unexposed_observation = timeline.now_ticks();
    passed &= expect(
        timeline.can_rebase_after_host_suspension(delivery, unexposed_observation) &&
            timeline.rebase_after_host_suspension(delivery, unexposed_observation) &&
            timeline.now_ticks() == delivery,
        "an unexposed later suspension can still recover to the exact committed boundary");
    ++counter.value;
    const auto resumed = timeline.now_ticks();
    timeline.commit_guest_observation(resumed);
    passed &= expect(
        resumed == delivery + 1u && timeline.committed_guest_ticks() == resumed,
        "accepted recovery resumes monotonically without inventing device progress");
    return passed;
}

bool host_suspension_candidate_interval_boundaries() {
    using galaxy::timing::is_host_suspension_candidate_interval;
    constexpr std::uint64_t kMinimum =
        galaxy::timing::kHostSuspensionCandidateMinimumTicks;
    constexpr std::uint64_t kMaximumCpu =
        galaxy::timing::kHostSuspensionCandidateMaximumCpuTime100ns;

    // kHostSuspensionCandidateMinimumTicks is exactly one VI period (lowered
    // from 1.5 -- see the comment on its definition in runtime_timeline.h):
    // a stall just over one period with near-zero CPU time used is already
    // sufficient evidence of an OS scheduling gap.
    static_assert(kMinimum == galaxy::timing::kViPeriodTicks);
    static_assert(kMinimum > 0u);
    static_assert(kMaximumCpu < std::numeric_limits<std::uint64_t>::max());

    bool passed = expect(
        !is_host_suspension_candidate_interval(kMinimum - 1u, 0u),
        "host-suspension candidate rejects one tick below 1 VI period");
    passed &= expect(
        is_host_suspension_candidate_interval(kMinimum, 0u),
        "host-suspension candidate accepts the exact threshold with no CPU advance");
    passed &= expect(
        is_host_suspension_candidate_interval(kMinimum, kMaximumCpu),
        "host-suspension candidate accepts the exact CPU evidence boundary");
    passed &= expect(
        !is_host_suspension_candidate_interval(
            kMinimum, kMaximumCpu + 1u),
        "host-suspension candidate rejects CPU evidence above the boundary");
    passed &= expect(
        !is_host_suspension_candidate_interval(
            kMinimum * 8u, kMaximumCpu + 1u),
        "a long wall interval cannot override excess simulation-thread CPU");
    return passed;
}

bool host_suspension_proof_selection_preserves_boundary_ownership() {
    using galaxy::timing::HostSuspensionRecoveryProof;
    using galaxy::timing::PeriodicConsumeStatus;
    using galaxy::timing::select_host_suspension_recovery_proof;

    galaxy::vi::BoundarySnapshot boundary{};
    boundary.serial = 131u;
    boundary.phase = galaxy::vi::BoundaryPhase::AwaitViRfi;
    boundary.edge.kind = galaxy::timing::EventKind::VideoInterface;
    boundary.edge.sequence = 131u;
    boundary.edge.deadline_ticks = 203'195'672u;
    boundary.interrupted_resume_pc = 0x804AB358u;
    boundary.interrupted_context = 0x80650C90u;
    constexpr std::uint64_t last_guest_ticks = 203'200'731u;
    galaxy::timing::PeriodicDeadlineObservation observation{};
    observation.status = PeriodicConsumeStatus::Backlog;
    observation.expected_sequence = boundary.edge.sequence + 1u;
    observation.expected_deadline_ticks =
        boundary.edge.deadline_ticks + galaxy::timing::kViPeriodTicks;
    observation.pending_edges = 11u;
    observation.scheduled_due_edges = 11u;
    observation.lateness_ticks = galaxy::timing::kViPeriodTicks * 10u;

    const auto select_active_backlog = [&](bool enabled) {
        const bool successor_owned =
            galaxy::vi::active_boundary_owns_successor_during_backlog(
                boundary, observation, last_guest_ticks);
        return select_host_suspension_recovery_proof(
            observation.status, true, true, false, successor_owned, enabled);
    };
    // This is the production selector. The former status-first branch chose
    // ViBacklog here, which its validator rejects for every active boundary.
    bool passed = expect(
        select_active_backlog(true) ==
            HostSuspensionRecoveryProof::BacklogWithOwnedActiveViSuccessor,
        "active AwaitViRfi backlog selects the active successor proof");
    passed &= expect(
        !select_active_backlog(false).has_value(),
        "disabled active-backlog recovery cannot fall back to the inactive proof");
    ++observation.expected_sequence;
    passed &= expect(
        !select_active_backlog(true).has_value(),
        "active backlog with a different successor remains ineligible");
    --observation.expected_sequence;
    boundary.phase = galaxy::vi::BoundaryPhase::TokenPending;
    boundary.token = galaxy::gx::FramePeCompletionToken{11u, 29u};
    boundary.token_consumed = false;
    passed &= expect(
        select_active_backlog(true) ==
            HostSuspensionRecoveryProof::BacklogWithOwnedActiveViSuccessor,
        "a live pending renderer token selects the active successor proof");
    boundary.token_consumed = true;
    passed &= expect(
        !select_active_backlog(true).has_value(),
        "a consumed renderer token cannot select backlog recovery");

    passed &= expect(
        select_host_suspension_recovery_proof(
            PeriodicConsumeStatus::Backlog, false, false, false, false, false) ==
            HostSuspensionRecoveryProof::ViBacklog,
        "inactive backlog recovery remains available independently of the active flag");
    for (const bool boundary_active : {false, true}) {
        passed &= expect(
            !select_host_suspension_recovery_proof(
                PeriodicConsumeStatus::Backlog,
                boundary_active, !boundary_active, true, true, true).has_value(),
            "partial VI ownership cannot select either backlog proof");
    }
    passed &= expect(
        select_host_suspension_recovery_proof(
            PeriodicConsumeStatus::Ready, true, true, true, false, false) ==
            HostSuspensionRecoveryProof::AiOverrunWithOwnedActiveViSuccessor &&
            !select_host_suspension_recovery_proof(
                PeriodicConsumeStatus::Ready,
                true, true, false, true, true).has_value(),
        "Ready retains the existing AI-crossing path without borrowing backlog evidence");
    for (const auto status : {
             PeriodicConsumeStatus::NotDue,
             PeriodicConsumeStatus::AwaitingPublication,
             PeriodicConsumeStatus::EarlyPublication,
             PeriodicConsumeStatus::ArithmeticOverflow}) {
        passed &= expect(
            !select_host_suspension_recovery_proof(
                status, true, true, true, true, true).has_value(),
            "invalid or not-yet-due observations never select a recovery proof");
    }
    return passed;
}

bool host_suspension_device_crossing_boundaries() {
    using galaxy::timing::host_suspension_crossed_device_deadline_by_period;
    constexpr std::uint64_t kDeadline = 407'724'979u;
    constexpr std::uint64_t kPeriod = 1'063'125u;

    bool passed = expect(
        host_suspension_crossed_device_deadline_by_period(
            kDeadline - 1u, kDeadline + kPeriod, kDeadline, kPeriod),
        "device crossing accepts the exact one-period boundary");
    passed &= expect(
        !host_suspension_crossed_device_deadline_by_period(
            kDeadline, kDeadline + kPeriod, kDeadline, kPeriod),
        "device crossing must begin strictly before the deadline");
    passed &= expect(
        !host_suspension_crossed_device_deadline_by_period(
            kDeadline - 1u,
            kDeadline + kPeriod - 1u,
            kDeadline,
            kPeriod),
        "device crossing rejects one tick below a complete period");
    passed &= expect(
        !host_suspension_crossed_device_deadline_by_period(
            kDeadline - 1u, kDeadline - 1u, kDeadline, kPeriod),
        "device crossing rejects an observation before the deadline");
    passed &= expect(
        !host_suspension_crossed_device_deadline_by_period(
            0u, kDeadline + kPeriod, kDeadline, kPeriod) &&
            !host_suspension_crossed_device_deadline_by_period(
                kDeadline - 1u, 0u, kDeadline, kPeriod) &&
            !host_suspension_crossed_device_deadline_by_period(
                kDeadline - 1u, kDeadline + kPeriod, 0u, kPeriod) &&
            !host_suspension_crossed_device_deadline_by_period(
                kDeadline - 1u, kDeadline + kPeriod, kDeadline, 0u),
        "zero or unarmed device identities fail closed");
    passed &= expect(
        !host_suspension_crossed_device_deadline_by_period(
            std::numeric_limits<std::uint64_t>::max() - 1u,
            std::numeric_limits<std::uint64_t>::max(),
            std::numeric_limits<std::uint64_t>::max(),
            1u),
        "maximum uint64 ordering fails closed without subtraction overflow");
    return passed;
}

bool host_suspension_ai_arm_identity_boundaries() {
    using galaxy::host::ai_dma_timing::
        exact_host_suspension_arm_identity;
    constexpr std::uint64_t kDeadline = 407'724'979u;
    constexpr std::uint64_t kGeneration = 317u;
    constexpr std::uint64_t kDuration = 1'063'125u;
    const auto exact = [](bool playing,
                          bool interrupt_pending,
                          bool armed,
                          std::uint64_t deadline,
                          std::uint64_t generation,
                          std::uint64_t pending,
                          std::uint64_t local,
                          std::uint64_t completion,
                          std::uint64_t duration) constexpr {
        return exact_host_suspension_arm_identity(
            playing,
            interrupt_pending,
            armed,
            deadline,
            generation,
            pending,
            local,
            completion,
            duration);
    };

    bool passed = expect(
        exact(
            true,
            false,
            true,
            kDeadline,
            kGeneration,
            kGeneration,
            0u,
            kDeadline,
            kDuration),
        "AI recovery accepts the exact pending armed generation");
    passed &= expect(
        exact(
            true,
            false,
            true,
            kDeadline,
            kGeneration,
            0u,
            kGeneration,
            kDeadline,
            kDuration),
        "AI recovery accepts the exact locally taken armed generation");
    passed &= expect(
        !exact(
            true,
            false,
            true,
            kDeadline,
            kGeneration,
            0u,
            0u,
            kDeadline,
            kDuration) &&
            !exact(
                true,
                false,
                true,
                kDeadline,
                kGeneration,
                kGeneration + 1u,
                0u,
                kDeadline,
                kDuration) &&
            !exact(
                true,
                false,
                true,
                kDeadline,
                kGeneration,
                kGeneration,
                kGeneration + 1u,
                kDeadline,
                kDuration),
        "AI recovery rejects missing or contradictory publication identity");
    passed &= expect(
        !exact(
            false,
            false,
            true,
            kDeadline,
            kGeneration,
            kGeneration,
            0u,
            kDeadline,
            kDuration) &&
            !exact(
                true,
                true,
                true,
                kDeadline,
                kGeneration,
                kGeneration,
                0u,
                kDeadline,
                kDuration) &&
            !exact(
                true,
                false,
                false,
                kDeadline,
                kGeneration,
                kGeneration,
                0u,
                kDeadline,
                kDuration),
        "AI recovery rejects stopped, interrupted, or unarmed device state");
    passed &= expect(
        !exact(
            true,
            false,
            true,
            kDeadline,
            0u,
            0u,
            0u,
            kDeadline,
            kDuration) &&
            !exact(
                true,
                false,
                true,
                kDeadline,
                kGeneration,
                kGeneration,
                0u,
                kDeadline + 1u,
                kDuration) &&
            !exact(
                true,
                false,
                true,
                kDeadline,
                kGeneration,
                kGeneration,
                0u,
                kDeadline,
                0u),
        "AI recovery rejects zero generation, deadline mismatch, or zero duration");
    return passed;
}

bool native_input_ai_preemption_policy_boundaries() {
    using galaxy::host::ai_dma_timing::NativeInputAiServiceResult;
    using galaxy::host::ai_dma_timing::ScopedExactWordBitClear;
    using galaxy::host::ai_dma_timing::ai_device_transaction_owned;
    using galaxy::host::ai_dma_timing::ai_probe_precedes_native_input;
    using galaxy::host::ai_dma_timing::
        native_input_must_wait_for_ai_owner;
    using galaxy::host::ai_dma_timing::
        service_native_input_with_ai_priority;

    bool passed = expect(
        ai_probe_precedes_native_input(true, false, false),
        "an active unowned AI device is probed before native input service");
    passed &= expect(
        !ai_probe_precedes_native_input(false, false, false),
        "an inactive AI device does not require a probe");
    passed &= expect(
        !ai_probe_precedes_native_input(true, true, false),
        "nested input service cannot recursively preempt itself");
    passed &= expect(
        !ai_probe_precedes_native_input(true, false, true),
        "an active AI handler retains ownership of its device transaction");

    const bool scoped_owner = ai_device_transaction_owned(
        true, false, 0u, false, false);
    const bool selected_owner = ai_device_transaction_owned(
        false, true, 5u, false, false);
    const bool selection_call_owner = ai_device_transaction_owned(
        false, true, 0xFFFFFFFFu, false, true);
    passed &= expect(
        scoped_owner && selected_owner && selection_call_owner,
        "AI ownership covers scoped, durably selected, and selection-call edges");
    passed &= expect(
        !ai_device_transaction_owned(false, true, 5u, true, false) &&
            !ai_device_transaction_owned(false, true, 24u, false, false) &&
            !ai_device_transaction_owned(false, false, 5u, false, false),
        "completed, non-AI, and inactive dispatch records do not own AI");
    passed &= expect(
        native_input_must_wait_for_ai_owner(false, scoped_owner) &&
            native_input_must_wait_for_ai_owner(false, selected_owner) &&
            native_input_must_wait_for_ai_owner(false, selection_call_owner),
        "input waits for every AI owner even before the next publication");
    passed &= expect(
        !native_input_must_wait_for_ai_owner(true, scoped_owner) &&
            !native_input_must_wait_for_ai_owner(false, false),
        "input deferral requires an owner outside recursive input service");

    constexpr std::uint32_t kEe = 0x00008000u;
    std::uint32_t enabled_msr = 0xA5A5F5A5u;
    const std::uint32_t enabled_original = enabled_msr;
    {
        ScopedExactWordBitClear mask(&enabled_msr, kEe);
        passed &= expect(
            enabled_msr == (enabled_original & ~kEe),
            "AI probe clears only EE while its scope is active");
    }
    passed &= expect(
        enabled_msr == enabled_original,
        "AI probe restores an EE-enabled MSR exactly");

    std::uint32_t masked_msr = 0x00002032u;
    const std::uint32_t masked_original = masked_msr;
    try {
        ScopedExactWordBitClear mask(&masked_msr, kEe);
        throw std::runtime_error("exercise exceptional mask restoration");
    } catch (const std::runtime_error&) {
    }
    passed &= expect(
        masked_msr == masked_original,
        "AI probe restores an already-masked MSR after an exception");
    {
        ScopedExactWordBitClear mask(nullptr, kEe);
    }

    std::vector<char> order;
    bool owner = false;
    const auto service_result = service_native_input_with_ai_priority(
        false,
        [&] {
            order.push_back('o');
            return owner;
        },
        [&] { order.push_back('a'); },
        [&] {
            order.push_back('p');
            return true;
        },
        [&] { order.push_back('i'); });
    passed &= expect(
        service_result == NativeInputAiServiceResult::InputServiceAttempted &&
            order == std::vector<char>({'o', 'a', 'o', 'p', 'i', 'a'}),
        "AI/input orchestration is pre-probe, input, post-probe");

    order.clear();
    owner = true;
    const auto owner_result = service_native_input_with_ai_priority(
        false,
        [&] {
            order.push_back('o');
            return owner;
        },
        [&] { order.push_back('a'); },
        [&] {
            order.push_back('p');
            return true;
        },
        [&] { order.push_back('i'); });
    passed &= expect(
        owner_result == NativeInputAiServiceResult::DeferredForAiOwner &&
            order == std::vector<char>({'o'}),
        "an AI owner defers input before either device is mutated");

    order.clear();
    owner = false;
    const auto race_result = service_native_input_with_ai_priority(
        false,
        [&] {
            order.push_back('o');
            return owner;
        },
        [&] {
            order.push_back('a');
            owner = true;
        },
        [&] {
            order.push_back('p');
            return true;
        },
        [&] { order.push_back('i'); });
    passed &= expect(
        race_result == NativeInputAiServiceResult::DeferredForAiOwner &&
            order == std::vector<char>({'o', 'a', 'o'}),
        "ownership acquired during the pre-probe defers input");

    order.clear();
    const auto recursive_result = service_native_input_with_ai_priority(
        true,
        [&] {
            order.push_back('o');
            return false;
        },
        [&] { order.push_back('a'); },
        [&] {
            order.push_back('p');
            return true;
        },
        [&] { order.push_back('i'); });
    passed &= expect(
        recursive_result == NativeInputAiServiceResult::RecursiveInput &&
            order.empty(),
        "recursive input performs no owner, AI, or input work");
    return passed;
}

bool stall_accounting_is_honest() {
    constexpr std::uint64_t kStallTicks = 16'220'250ull;  // exactly 267 ms
    galaxy::timing::PeriodicDeadline vi(
        galaxy::timing::kViPeriodTicks,
        galaxy::timing::kViPeriodTicks);
    const auto edges = vi.collect_due(
        galaxy::timing::EventKind::VideoInterface, kStallTicks);
    const std::uint64_t ai_samples_48khz =
        (kStallTicks * 48'000ull) /
        galaxy::timing::kTimelineTicksPerSecond;
    return expect(edges.size() == 16u, "267 ms accounts for all 16 VI edges") &&
        expect(edges.front().sequence == 1u, "first retained VI edge is sequence 1") &&
        expect(edges.back().sequence == 16u, "last retained VI edge is sequence 16") &&
        expect(ai_samples_48khz == 12'816u,
               "267 ms advances AISCNT by exactly 12,816 samples") &&
        expect(vi.next_deadline_ticks() ==
                   galaxy::timing::kViPeriodTicks * 17ull,
               "next VI deadline is edge 17 rather than now plus one period");
}

bool vi_one_hour_never_skips() {
    galaxy::timing::PeriodicDeadline vi(
        galaxy::timing::kViPeriodTicks,
        galaxy::timing::kViPeriodTicks);
    const auto edges = vi.collect_due(
        galaxy::timing::EventKind::VideoInterface,
        galaxy::timing::kTimelineTicksPerSecond * 60ull * 60ull);
    return expect(edges.size() == 216'000u, "one hour retains exactly 216,000 VI edges") &&
        expect(edges.back().sequence == 216'000u,
               "one-hour VI sequence has no accumulated drift") &&
        expect(edges.back().deadline_ticks ==
                   galaxy::timing::kTimelineTicksPerSecond * 60ull * 60ull,
               "one-hour final VI deadline is exact");
}

bool ai_deadline_is_exact() {
    constexpr std::uint64_t kFrames = 105ull * 32ull / 4ull;
    constexpr std::uint64_t kAiDuration =
        kFrames * galaxy::timing::kTimelineTicksPerSecond / 48'000ull;
    static_assert(kFrames == 840ull);
    static_assert(kAiDuration == 1'063'125ull);

    galaxy::timing::PeriodicDeadline ai(kAiDuration, kAiDuration);
    bool passed = expect(
        ai.collect_due(
              galaxy::timing::EventKind::AudioDma,
              kAiDuration - 1u)
            .empty(),
        "AI DMA is not due one tick early");
    const auto due = ai.collect_due(
        galaxy::timing::EventKind::AudioDma, kAiDuration);
    passed &= expect(due.size() == 1u, "AI DMA is due exactly once at its deadline");
    passed &= expect(
        !due.empty() && due.front().deadline_ticks == kAiDuration,
        "AI DMA retains its exact absolute deadline");
    return passed;
}

bool schedule_order_is_stable() {
    galaxy::timing::DeadlineSchedule schedule;
    schedule.set_periodic(
        galaxy::timing::EventKind::InputReport, 100u, 100u);
    schedule.set_periodic(
        galaxy::timing::EventKind::VideoInterface, 100u, 100u);
    schedule.schedule_once(galaxy::timing::EventKind::AudioDma, 100u);
    const auto due = schedule.collect_due(100u);
    return expect(due.size() == 3u, "simultaneous schedule retains every event") &&
        expect(due[0].kind == galaxy::timing::EventKind::VideoInterface,
               "simultaneous VI has stable first ordering") &&
        expect(due[1].kind == galaxy::timing::EventKind::AudioDma,
               "simultaneous AI has stable second ordering") &&
        expect(due[2].kind == galaxy::timing::EventKind::InputReport,
               "simultaneous input has stable third ordering");
}

bool deadline_failures_preserve_owners() {
    using namespace galaxy::timing;
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    bool passed = true;
    PeriodicDeadline backlog(0u, 1u);
    bool overflow = false;
    try { (void)backlog.collect_due(EventKind::VideoInterface, maximum); }
    catch (const std::overflow_error&) { overflow = true; }
    passed &= expect(overflow && backlog.next_deadline_ticks() == 0u &&
        backlog.next_sequence() == 1u,
        "maximum periodic backlog fails before advancing its owner");

    PeriodicDeadline sequence(100u, 100u, maximum);
    overflow = false;
    try { (void)sequence.collect_due(EventKind::VideoInterface, 100u); }
    catch (const std::overflow_error&) { overflow = true; }
    passed &= expect(overflow && sequence.next_deadline_ticks() == 100u &&
        sequence.next_sequence() == maximum,
        "sequence overflow cannot partially advance the periodic deadline");
    bool invalid = false;
    try { (void)sequence.collect_due(static_cast<EventKind>(255u), 0u); }
    catch (const std::invalid_argument&) { invalid = true; }
    passed &= expect(invalid, "all out-of-range event kinds fail closed");

    DeadlineSchedule counted;
    counted.schedule_once(EventKind::InputReport, 100u, maximum);
    unsigned rejected = 0u;
    for (unsigned attempt = 0; attempt < 2u; ++attempt) {
        try { counted.schedule_once(EventKind::InputReport, 200u); }
        catch (const std::overflow_error&) { ++rejected; }
    }
    const auto retained = counted.collect_due(200u);
    passed &= expect(rejected == 2u && retained.size() == 1u &&
        retained.front().sequence == maximum,
        "failed automatic sequences never wrap and become reusable");

    DeadlineSchedule replaceable;
    replaceable.replace_once(EventKind::AudioDma, 100u, 1u);
    unsigned mixed = 0u;
    try { replaceable.schedule_once(EventKind::AudioDma, 200u); }
    catch (const std::logic_error&) { ++mixed; }
    try { replaceable.set_periodic(EventKind::AudioDma, 200u, 100u); }
    catch (const std::logic_error&) { ++mixed; }
    const auto owned = replaceable.collect_due(100u);
    passed &= expect(mixed == 2u && owned.size() == 1u &&
        owned.front().replaceable && owned.front().sequence == 1u,
        "counted and periodic insertion cannot overwrite a replaceable owner");
    return passed;
}

bool replaceable_schedule_keeps_only_latest_generation() {
    using galaxy::timing::DeadlineSchedule;
    using galaxy::timing::EventKind;

    DeadlineSchedule schedule;
    schedule.replace_once(EventKind::AudioDma, 100u, 1u);
    schedule.replace_once(EventKind::AudioDma, 200u, 2u);
    bool passed = expect(
        schedule.collect_due(199u).empty(),
        "replaced AI one-shot cannot publish its obsolete deadline");
    const auto due = schedule.collect_due(200u);
    passed &= expect(
        due.size() == 1u && due.front().kind == EventKind::AudioDma &&
            due.front().sequence == 2u &&
            due.front().deadline_ticks == 200u &&
            due.front().replaceable,
        "replaceable AI one-shot retains only its latest generation");

    schedule.replace_once(EventKind::AudioDma, 300u, 3u);
    schedule.cancel_once(EventKind::AudioDma, 4u);
    passed &= expect(
        schedule.collect_due(1'000u).empty(),
        "cancelled replaceable AI one-shot cannot publish later");

    bool generation_failed = false;
    try {
        schedule.replace_once(EventKind::AudioDma, 400u, 4u);
    } catch (const std::invalid_argument&) {
        generation_failed = true;
    }
    passed &= expect(
        generation_failed,
        "replaceable deadline generations must increase across cancellation");
    return passed;
}

bool replaceable_schedule_restores_only_current_generation() {
    using galaxy::timing::DeadlineSchedule;
    using galaxy::timing::EventKind;

    DeadlineSchedule schedule;
    schedule.replace_once(EventKind::AudioDma, 100u, 7u);
    const auto published = schedule.collect_due(100u);
    bool passed = expect(
        published.size() == 1u && published.front().sequence == 7u,
        "initial replaceable deadline publishes its owned generation");

    schedule.restore_replaceable_after_host_suspension(
        EventKind::AudioDma, 100u, 7u);
    passed &= expect(
        schedule.collect_due(99u).empty(),
        "restored replaceable deadline does not publish early");
    const auto restored = schedule.collect_due(100u);
    passed &= expect(
        restored.size() == 1u && restored.front().kind == EventKind::AudioDma &&
            restored.front().sequence == 7u && restored.front().replaceable,
        "host-suspension restore preserves the existing device generation");

    schedule.cancel_once(EventKind::AudioDma, 8u);
    bool stale_restore_rejected = false;
    try {
        schedule.restore_replaceable_after_host_suspension(
            EventKind::AudioDma, 200u, 7u);
    } catch (const std::invalid_argument&) {
        stale_restore_rejected = true;
    }
    passed &= expect(
        stale_restore_rejected,
        "host-suspension restore cannot revive a cancelled generation");
    return passed;
}

bool broker_rebases_and_restores_deadlines_atomically() {
    using galaxy::timing::EventKind;

    AtomicManualCounter counter{};
    galaxy::timing::RuntimeTimeline timeline(
        {&read_atomic_manual_counter,
         &counter,
         galaxy::timing::kTimelineTicksPerSecond});
    galaxy::timing::PendingEventLatch latch;
    galaxy::timing::DeadlineBroker broker(timeline, latch);
    constexpr std::uint64_t kFirstVi = 100u;
    constexpr std::uint64_t kAudioDeadline = 200u;
    counter.value.store(50u, std::memory_order_release);
    const std::uint64_t last_guest_ticks = timeline.now_ticks();
    broker.set_periodic(EventKind::VideoInterface, kFirstVi, 100u, 1u);
    broker.replace_once(EventKind::AudioDma, kAudioDeadline, 1u);
    broker.start();

    counter.value.store(950u, std::memory_order_release);
    const auto publish_timeout =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while ((latch.pending(EventKind::VideoInterface) == 0u ||
            latch.pending_replaceable_generation(EventKind::AudioDma) == 0u) &&
           !broker.failed() && std::chrono::steady_clock::now() < publish_timeout) {
        std::this_thread::yield();
    }
    const std::uint64_t observed_host_ticks = timeline.now_ticks();
    bool passed = expect(
        latch.pending(EventKind::VideoInterface) != 0u &&
            latch.pending_replaceable_generation(EventKind::AudioDma) == 1u,
        "pre-rebase worker publications are present for atomic restoration");
    const std::vector<galaxy::timing::PeriodicDeadlineRestore> periodic{
        {EventKind::VideoInterface, kFirstVi, 100u, 1u}};
    const std::vector<galaxy::timing::ReplaceableDeadlineRestore> replaceable{
        {EventKind::AudioDma, kAudioDeadline, 1u}};
    passed &= expect(
        broker.rebase_and_restore_after_host_suspension(
            last_guest_ticks,
            observed_host_ticks,
            periodic,
            replaceable),
        "atomic host-suspension restoration succeeds");
    passed &= expect(
        timeline.now_ticks() == last_guest_ticks &&
            latch.pending(EventKind::VideoInterface) == 0u &&
            latch.pending_replaceable_generation(EventKind::AudioDma) == 0u,
        "atomic restoration resets time and clears stale publications together");

    counter.value.fetch_add(50u, std::memory_order_acq_rel);
    const auto restore_timeout =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (latch.pending(EventKind::VideoInterface) == 0u &&
           !broker.failed() && std::chrono::steady_clock::now() < restore_timeout) {
        std::this_thread::yield();
    }
    passed &= expect(
        latch.pending(EventKind::VideoInterface) == 1u &&
            latch.pending_replaceable_generation(EventKind::AudioDma) == 0u,
        "restored schedule publishes only the exact next VI after guest time resumes");
    broker.stop();
    try {
        broker.throw_if_failed();
    } catch (const std::exception& error) {
        std::cerr << "FAILED: atomic host-suspension broker failed: "
                  << error.what() << '\n';
        passed = false;
    }
    return passed;
}

bool broker_rejects_wrong_restore_owner_transactionally() {
    using namespace galaxy::timing;
    AtomicManualCounter counter{};
    RuntimeTimeline timeline({&read_atomic_manual_counter, &counter,
        kTimelineTicksPerSecond});
    PendingEventLatch latch;
    DeadlineBroker broker(timeline, latch);
    broker.set_periodic(EventKind::VideoInterface, 1'000u, 100u, 10u);
    broker.replace_once(EventKind::AudioDma, 1'200u, 7u);
    counter.value.store(950u, std::memory_order_release);
    const auto observed = timeline.now_ticks();
    bool passed = expect(
        latch.publish_periodic({EventKind::VideoInterface, 9u, 900u}) &&
        latch.publish_replaceable(EventKind::AudioDma, 7u),
        "invalid-owner recovery fixture has exact pending publications");
    bool rejected = false;
    try {
        (void)broker.rebase_and_restore_after_host_suspension(500u, observed,
            {{EventKind::VideoInterface, 100u, 100u, 1u}},
            {{EventKind::AudioDma, 200u, 6u}});
    } catch (const std::invalid_argument&) { rejected = true; }
    passed &= expect(rejected && timeline.now_ticks() == observed &&
        latch.pending(EventKind::VideoInterface) == 1u &&
        latch.pending_replaceable_generation(EventKind::AudioDma) == 7u,
        "late invalid restore owner leaves the timeline and earlier latch intact");
    const auto retained = latch.take_all_periodic(EventKind::VideoInterface);
    passed &= expect(retained.first_sequence == 9u &&
        retained.first_deadline_ticks == 900u,
        "failed transaction preserves the exact original pending identity");
    latch.clear_replaceable(EventKind::AudioDma);
    broker.start();
    counter.value.store(1'100u, std::memory_order_release);
    const auto timeout = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (latch.pending(EventKind::VideoInterface) < 2u && !broker.failed() &&
           std::chrono::steady_clock::now() < timeout) { std::this_thread::yield(); }
    broker.stop();
    const auto next = latch.take_all_periodic(EventKind::VideoInterface);
    passed &= expect(next.count == 2u && next.first_sequence == 10u &&
        next.first_deadline_ticks == 1'000u && next.last_sequence == 11u &&
        next.last_deadline_ticks == 1'100u,
        "invalid restoration cannot replace an earlier periodic schedule");
    broker.throw_if_failed();
    return passed;
}

bool broker_rejects_observed_recovery_without_mutating_owners() {
    using galaxy::timing::DeadlineEvent;
    using galaxy::timing::EventKind;
    AtomicManualCounter counter{};
    galaxy::timing::RuntimeTimeline timeline(
        {&read_atomic_manual_counter, &counter,
         galaxy::timing::kTimelineTicksPerSecond});
    galaxy::timing::PendingEventLatch latch;
    galaxy::timing::DeadlineBroker broker(timeline, latch);
    broker.set_periodic(EventKind::VideoInterface, 1'000u, 100u, 10u);
    broker.set_periodic(EventKind::InputReport, 1'100u, 190u, 20u);
    broker.replace_once(EventKind::AudioDma, 1'200u, 7u);
    counter.value.store(950u, std::memory_order_release);
    const auto observed_ticks = timeline.now_ticks();
    timeline.commit_guest_observation(observed_ticks);

    bool passed = expect(
        latch.publish_periodic(DeadlineEvent{EventKind::VideoInterface, 9u, 900u}) &&
            latch.publish_periodic(DeadlineEvent{EventKind::InputReport, 19u, 910u}) &&
            latch.publish_replaceable(EventKind::AudioDma, 7u),
        "rejected-recovery fixture retains exact pending VI, HID, and AI owners");
    const std::vector<galaxy::timing::PeriodicDeadlineRestore> periodic{
        {EventKind::VideoInterface, 100u, 100u, 1u},
        {EventKind::InputReport, 200u, 100u, 1u}};
    const std::vector<galaxy::timing::ReplaceableDeadlineRestore> replaceable{
        {EventKind::AudioDma, 300u, 7u}};
    passed &= expect(
        !broker.rebase_and_restore_after_host_suspension(
            500u, observed_ticks, periodic, replaceable) &&
            timeline.now_ticks() == observed_ticks &&
            latch.pending(EventKind::VideoInterface) == 1u &&
            latch.pending(EventKind::InputReport) == 1u &&
            latch.pending_replaceable_generation(EventKind::AudioDma) == 7u,
        "a stale anchor rejects before any clock, pending publication, or AI generation mutation");
    const auto retained_vi = latch.take_all_periodic(EventKind::VideoInterface);
    const auto retained_input = latch.take_all_periodic(EventKind::InputReport);
    passed &= expect(
        retained_vi.count == 1u && retained_vi.first_sequence == 9u &&
            retained_vi.first_deadline_ticks == 900u &&
            retained_input.count == 1u && retained_input.first_sequence == 19u &&
            retained_input.first_deadline_ticks == 910u,
        "rejected recovery preserves the exact pending identities and deadlines");
    latch.clear_replaceable(EventKind::AudioDma);

    broker.start();
    counter.value.store(1'100u, std::memory_order_release);
    const auto timeout = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(100);
    while ((latch.pending(EventKind::VideoInterface) < 2u ||
            latch.pending(EventKind::InputReport) == 0u) && !broker.failed() &&
           std::chrono::steady_clock::now() < timeout) {
        std::this_thread::yield();
    }
    broker.stop();
    const auto next_vi = latch.take_all_periodic(EventKind::VideoInterface);
    const auto next_input = latch.take_all_periodic(EventKind::InputReport);
    passed &= expect(
        next_vi.count == 2u && next_vi.first_sequence == 10u &&
            next_vi.first_deadline_ticks == 1'000u &&
            next_vi.last_sequence == 11u && next_vi.last_deadline_ticks == 1'100u &&
            next_input.count == 1u && next_input.first_sequence == 20u &&
            next_input.first_deadline_ticks == 1'100u &&
            latch.pending_replaceable_generation(EventKind::AudioDma) == 0u,
        "rejected recovery leaves future VI, HID, and AI schedules unchanged");
    try {
        broker.throw_if_failed();
    } catch (const std::exception& error) {
        std::cerr << "FAILED: rejected-recovery broker failed: "
                  << error.what() << '\n';
        passed = false;
    }
    return passed;
}

bool pending_latch_retains_counts() {
    galaxy::timing::PendingEventLatch latch;
    const auto vi = galaxy::timing::EventKind::VideoInterface;
    const auto ai = galaxy::timing::EventKind::AudioDma;
    bool passed = expect(latch.publish(vi, 5u), "VI backlog publication succeeds");
    passed &= expect(latch.publish(ai), "AI publication succeeds");
    passed &= expect(latch.pending(vi) == 5u, "VI pending count retains all edges");
    passed &= expect(
        latch.mask() ==
            (galaxy::timing::event_bit(vi) | galaxy::timing::event_bit(ai)),
        "pending mask coalesces event kinds without losing counts");
    passed &= expect(latch.take_one(vi), "one VI edge can be consumed");
    passed &= expect(latch.pending(vi) == 4u, "taking one VI preserves the backlog");
    passed &= expect(latch.take_all(vi) == 4u, "remaining VI backlog is returned exactly");
    passed &= expect(
        latch.mask() == galaxy::timing::event_bit(ai),
        "clearing VI leaves simultaneous AI pending");
    passed &= expect(latch.take_all(ai) == 1u, "AI event is consumed exactly once");
    passed &= expect(latch.mask() == 0u, "pending mask clears after all events");
    return passed;
}

bool replaceable_latch_validates_generation_identity() {
    using galaxy::timing::EventKind;
    using galaxy::timing::PendingEventLatch;

    PendingEventLatch latch;
    const EventKind ai = EventKind::AudioDma;
    bool passed = expect(
        latch.publish_replaceable(ai, 7u),
        "replaceable AI generation publication succeeds");
    passed &= expect(
        latch.pending_replaceable_generation(ai) == 7u &&
            (latch.mask() & galaxy::timing::event_bit(ai)) != 0u,
        "replaceable AI generation sets its inline wake bit");

    const auto future = latch.take_replaceable(ai, 6u);
    passed &= expect(
        future.status ==
                PendingEventLatch::ReplaceableTakeStatus::Future &&
            future.observed_generation == 7u &&
            latch.pending_replaceable_generation(ai) == 7u,
        "a newer AI generation is preserved rather than consumed");
    const auto exact = latch.take_replaceable(ai, 7u);
    passed &= expect(
        exact.status ==
                PendingEventLatch::ReplaceableTakeStatus::Taken &&
            exact.observed_generation == 7u && latch.mask() == 0u,
        "the exact AI generation is consumed once and clears its wake bit");

    passed &= expect(
        latch.publish_replaceable(ai, 8u),
        "second replaceable AI generation publication succeeds");
    const auto stale = latch.take_replaceable(ai, 9u);
    passed &= expect(
        stale.status ==
                PendingEventLatch::ReplaceableTakeStatus::Stale &&
            stale.observed_generation == 8u && latch.mask() == 0u,
        "an obsolete AI generation is identified and discarded");

    passed &= expect(
        latch.publish_replaceable(ai, 10u),
        "replaceable AI publication succeeds before cancellation");
    latch.clear_replaceable(ai);
    passed &= expect(
        latch.pending_replaceable_generation(ai) == 0u && latch.mask() == 0u,
        "replaceable AI cancellation clears its generation and wake bit");
    const auto contention = latch.lock_contention_stats(ai);
    passed &= expect(
        contention.contended_acquisitions == 0u &&
            contention.total_spins == 0u && contention.max_spins == 0u,
        "uncontended replaceable operations leave contention telemetry clear");
    return passed;
}

bool pending_latch_publication_drain_race_leaves_no_stale_mask() {
    using galaxy::timing::EventKind;
    using galaxy::timing::PendingEventLatch;

    // The consumer deliberately polls the payload rather than the mask so it
    // can race a producer at the publication boundary. Once publish returns
    // and the payload has been drained, the corresponding wake bit must be
    // clear. This exercises the former payload-store/mask-fetch_or window.
    constexpr std::uint32_t kRounds = 20'000u;

    PendingEventLatch counted_latch;
    constexpr EventKind counted_kind = EventKind::InputReport;
    std::atomic_uint32_t counted_requested{};
    std::atomic_uint32_t counted_returned{};
    std::atomic_uint32_t counted_consumed{};
    std::atomic_bool counted_clean{true};
    const auto counted_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    std::thread counted_producer([&] {
        for (std::uint32_t round = 1u; round <= kRounds; ++round) {
            if (!await_handshake([&] { return counted_requested.load(std::memory_order_acquire) >= round; },
                                 counted_clean, counted_deadline)) return;
            if (!counted_latch.publish(counted_kind)) {
                counted_clean.store(false, std::memory_order_release);
            }
            counted_returned.store(round, std::memory_order_release);
            if (!await_handshake([&] { return counted_consumed.load(std::memory_order_acquire) >= round; },
                                 counted_clean, counted_deadline)) return;
        }
    });
    for (std::uint32_t round = 1u; round <= kRounds; ++round) {
        counted_requested.store(round, std::memory_order_release);
        if (!await_handshake([&] { return counted_latch.pending(counted_kind) != 0u; },
                             counted_clean, counted_deadline)) break;
        if (!counted_latch.take_one(counted_kind)) {
            counted_clean.store(false, std::memory_order_release);
        }
        counted_consumed.store(round, std::memory_order_release);
        if (!await_handshake([&] { return counted_returned.load(std::memory_order_acquire) >= round; },
                             counted_clean, counted_deadline)) break;
        if (counted_latch.pending(counted_kind) != 0u ||
            (counted_latch.mask() & galaxy::timing::event_bit(counted_kind)) !=
                0u) {
            counted_clean.store(false, std::memory_order_release);
        }
    }
    counted_producer.join();

    PendingEventLatch replaceable_latch;
    constexpr EventKind replaceable_kind = EventKind::AudioDma;
    std::atomic_uint32_t replaceable_requested{};
    std::atomic_uint32_t replaceable_returned{};
    std::atomic_uint32_t replaceable_consumed{};
    std::atomic_bool replaceable_clean{true};
    const auto replaceable_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    std::thread replaceable_producer([&] {
        for (std::uint32_t round = 1u; round <= kRounds; ++round) {
            if (!await_handshake([&] { return replaceable_requested.load(std::memory_order_acquire) >= round; },
                                 replaceable_clean, replaceable_deadline)) return;
            if (!replaceable_latch.publish_replaceable(
                    replaceable_kind, round)) {
                replaceable_clean.store(false, std::memory_order_release);
            }
            replaceable_returned.store(round, std::memory_order_release);
            if (!await_handshake([&] { return replaceable_consumed.load(std::memory_order_acquire) >= round; },
                                 replaceable_clean, replaceable_deadline)) return;
        }
    });
    for (std::uint32_t round = 1u; round <= kRounds; ++round) {
        replaceable_requested.store(round, std::memory_order_release);
        if (!await_handshake([&] { return replaceable_latch.pending_replaceable_generation(
                   replaceable_kind) != 0u; },
                             replaceable_clean, replaceable_deadline)) break;
        const auto taken =
            replaceable_latch.take_replaceable(replaceable_kind, round);
        if (taken.status !=
                PendingEventLatch::ReplaceableTakeStatus::Taken ||
            taken.observed_generation != round) {
            replaceable_clean.store(false, std::memory_order_release);
        }
        replaceable_consumed.store(round, std::memory_order_release);
        if (!await_handshake([&] { return replaceable_returned.load(std::memory_order_acquire) >= round; },
                             replaceable_clean, replaceable_deadline)) break;
        if (replaceable_latch.pending_replaceable_generation(
                replaceable_kind) != 0u ||
            (replaceable_latch.mask() &
             galaxy::timing::event_bit(replaceable_kind)) != 0u) {
            replaceable_clean.store(false, std::memory_order_release);
        }
    }
    replaceable_producer.join();

    return expect(
               counted_clean.load(std::memory_order_acquire),
               "counted publication/drain races leave no quiescent wake bit") &&
        expect(
               replaceable_clean.load(std::memory_order_acquire),
               "replaceable publication/drain races leave no quiescent wake bit");
}

bool periodic_batch_identity_is_atomic_and_nonmixable() {
    using galaxy::timing::DeadlineEvent;
    using galaxy::timing::EventKind;
    using galaxy::timing::PendingEventLatch;

    constexpr std::uint64_t kFirstSequence = 41u;
    constexpr std::uint64_t kFirstDeadline = 700'000u;
    constexpr std::uint64_t kPeriod = 607'500u;
    bool passed = true;

    // Race a second publication against the drain. The per-kind lock permits
    // exactly two linearizations: the first batch alone followed by the
    // second batch, or one two-edge batch. Neither may pair count from one
    // outcome with identity from the other.
    for (std::uint64_t round = 0u; round < 128u; ++round) {
        PendingEventLatch latch;
        const DeadlineEvent first{
            EventKind::InputReport,
            kFirstSequence,
            kFirstDeadline,
            false};
        const DeadlineEvent second{
            EventKind::InputReport,
            kFirstSequence + 1u,
            kFirstDeadline + kPeriod,
            false};
        passed &= expect(
            latch.publish_periodic(first),
            "first exact input publication succeeds");
        std::atomic_bool go{false};
        std::atomic_bool publisher_ok{false};
        std::thread publisher([&] {
            while (!go.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            publisher_ok.store(
                latch.publish_periodic(second),
                std::memory_order_release);
        });
        go.store(true, std::memory_order_release);
        const PendingEventLatch::PeriodicBatch drained =
            latch.take_all_periodic(EventKind::InputReport);
        publisher.join();
        passed &= expect(
            publisher_ok.load(std::memory_order_acquire),
            "racing exact input publication succeeds");

        if (drained.count == 1u) {
            passed &= expect(
                drained.has_identity() &&
                    drained.first_sequence == kFirstSequence &&
                    drained.last_sequence == kFirstSequence &&
                    drained.first_deadline_ticks == kFirstDeadline &&
                    drained.last_deadline_ticks == kFirstDeadline,
                "drain-before-publish retains the first immutable identity");
            const PendingEventLatch::PeriodicBatch remainder =
                latch.take_all_periodic(EventKind::InputReport);
            passed &= expect(
                remainder.count == 1u && remainder.has_identity() &&
                    remainder.first_sequence == kFirstSequence + 1u &&
                    remainder.last_sequence == kFirstSequence + 1u &&
                    remainder.first_deadline_ticks ==
                        kFirstDeadline + kPeriod &&
                    remainder.last_deadline_ticks ==
                        kFirstDeadline + kPeriod,
                "post-drain publication remains a separate exact batch");
        } else {
            passed &= expect(
                drained.count == 2u && drained.has_identity() &&
                    drained.first_sequence == kFirstSequence &&
                    drained.last_sequence == kFirstSequence + 1u &&
                    drained.first_deadline_ticks == kFirstDeadline &&
                    drained.last_deadline_ticks ==
                        kFirstDeadline + kPeriod,
                "publish-before-drain retains one exact two-edge range");
        }
        passed &= expect(
            latch.pending(EventKind::InputReport) == 0u &&
                (latch.mask() &
                 galaxy::timing::event_bit(EventKind::InputReport)) == 0u,
            "periodic publication/drain race leaves no count or wake bit");
    }

    PendingEventLatch anonymous_first;
    passed &= expect(
        anonymous_first.publish(EventKind::InputReport),
        "anonymous input publication remains available to generic users");
    passed &= expect(
        !anonymous_first.publish_periodic(
            DeadlineEvent{
                EventKind::InputReport, 1u, 100u, false}),
        "exact publication cannot inherit an anonymous pending count");
    const PendingEventLatch::PeriodicBatch anonymous =
        anonymous_first.take_all_periodic(EventKind::InputReport);
    passed &= expect(
        anonymous.count == 1u && !anonymous.has_identity(),
        "exact drain exposes rather than fabricates anonymous identity");

    PendingEventLatch exact_first;
    passed &= expect(
        exact_first.publish_periodic(
            DeadlineEvent{
                EventKind::InputReport, 7u, 900u, false}),
        "exact input publication can start an empty slot");
    passed &= expect(
        !exact_first.publish(EventKind::InputReport),
        "anonymous publication cannot contaminate an exact pending batch");
    const PendingEventLatch::PeriodicBatch exact =
        exact_first.take_all_periodic(EventKind::InputReport);
    passed &= expect(
        exact.count == 1u && exact.has_identity() &&
            exact.first_sequence == 7u && exact.last_sequence == 7u &&
            exact.first_deadline_ticks == 900u &&
            exact.last_deadline_ticks == 900u,
        "failed anonymous mixing leaves exact identity unchanged");
    return passed;
}

bool exact_periodic_consumer_accepts_one_on_time_edge() {
    using galaxy::timing::EventKind;
    using galaxy::timing::ExactPeriodicDeadlineConsumer;
    using galaxy::timing::PendingEventLatch;
    using galaxy::timing::PeriodicConsumeStatus;

    PendingEventLatch latch;
    ExactPeriodicDeadlineConsumer consumer(1'000u, 100u, 7u);
    bool passed = expect(
        latch.publish(EventKind::VideoInterface),
        "on-time VI publication succeeds");
    const auto before =
        consumer.inspect(latch, EventKind::VideoInterface, 1'000u);
    passed &= expect(
        before.status == PeriodicConsumeStatus::Ready,
        "one on-time VI edge is ready");
    const auto event = consumer.consume_one_or_throw(
        latch, EventKind::VideoInterface, 1'000u);
    passed &= expect(event.has_value(), "one on-time VI edge is consumed");
    passed &= expect(
        event.has_value() && event->sequence == 7u &&
            event->deadline_ticks == 1'000u,
        "on-time VI retains its exact origin sequence and deadline");
    passed &= expect(
        latch.pending(EventKind::VideoInterface) == 0u,
        "on-time VI consumes exactly one pending edge");

    const auto& stats = consumer.stats();
    passed &= expect(
        stats.locked_rate_valid,
        "one on-time VI preserves locked-rate validity");
    passed &= expect(
        stats.scheduled_edges == 1u && stats.delivered_edges == 1u,
        "one on-time VI accounts one scheduled and delivered edge");
    passed &= expect(
        stats.last_scheduled_sequence == 7u &&
            stats.last_delivered_sequence == 7u,
        "on-time VI audit records the exact sequence");
    passed &= expect(
        stats.max_pending_backlog == 1u &&
            stats.max_lateness_ticks == 0u,
        "on-time VI has singleton backlog and zero lateness");
    return passed;
}

bool exact_periodic_consumer_accepts_one_late_edge_within_period() {
    using galaxy::timing::EventKind;
    using galaxy::timing::ExactPeriodicDeadlineConsumer;
    using galaxy::timing::PendingEventLatch;

    PendingEventLatch latch;
    ExactPeriodicDeadlineConsumer consumer(1'000u, 100u);
    bool passed = expect(
        latch.publish(EventKind::VideoInterface),
        "late singleton VI publication succeeds");
    const auto event = consumer.consume_one_or_throw(
        latch, EventKind::VideoInterface, 1'099u);
    passed &= expect(
        event.has_value() && event->deadline_ticks == 1'000u,
        "late singleton VI retains the scheduled deadline");
    const auto& stats = consumer.stats();
    passed &= expect(
        stats.locked_rate_valid,
        "a singleton consumed before the next period remains valid");
    passed &= expect(
        stats.max_lateness_ticks == 99u,
        "late singleton VI records exact lateness");
    passed &= expect(
        stats.missed_or_coalesced_edges == 0u,
        "late singleton VI does not invent a missed edge");
    return passed;
}

bool exact_periodic_consumer_rejects_267ms_backlog() {
    using galaxy::timing::EventKind;
    using galaxy::timing::ExactPeriodicDeadlineConsumer;
    using galaxy::timing::PendingEventLatch;
    using galaxy::timing::PeriodicConsumeStatus;

    constexpr std::uint64_t kStallTicks = 16'220'250ull;
    PendingEventLatch latch;
    ExactPeriodicDeadlineConsumer consumer(
        galaxy::timing::kViPeriodTicks,
        galaxy::timing::kViPeriodTicks);
    bool passed = expect(
        latch.publish(EventKind::VideoInterface, 16u),
        "all 16 stalled VI publications are retained");
    const auto observation =
        consumer.inspect(latch, EventKind::VideoInterface, kStallTicks);
    passed &= expect(
        observation.status == PeriodicConsumeStatus::Backlog,
        "267 ms is represented as an invalid VI backlog");
    passed &= expect(
        observation.scheduled_due_edges == 16u &&
            observation.scheduled_edges_elapsed == 16u,
        "267 ms exposes all 16 scheduled VI edges");
    passed &= expect(
        observation.pending_edges == 16u &&
            observation.missed_or_coalesced_edges == 15u,
        "267 ms exposes 16 pending and 15 coalesced VI edges");

    bool backlog_failed = false;
    try {
        (void)consumer.consume_one_or_throw(
            latch, EventKind::VideoInterface, kStallTicks);
    } catch (const std::runtime_error&) {
        backlog_failed = true;
    }
    passed &= expect(
        backlog_failed,
        "267 ms VI backlog hard-fails strict consumption");
    passed &= expect(
        latch.pending(EventKind::VideoInterface) == 16u,
        "backlog failure consumes no VI edge");
    const auto& stats = consumer.stats();
    passed &= expect(
        !stats.locked_rate_valid,
        "267 ms VI backlog invalidates locked-rate proof");
    passed &= expect(
        stats.scheduled_edges == 16u && stats.delivered_edges == 0u,
        "backlog audit distinguishes scheduled from delivered VI edges");
    passed &= expect(
        stats.max_pending_backlog == 16u &&
            stats.missed_or_coalesced_edges == 15u,
        "backlog audit preserves its maximum and loss count");
    return passed;
}

bool level_periodic_consumer_coalesces_only_complete_due_identity() {
    using galaxy::timing::DeadlineEvent;
    using galaxy::timing::EventKind;
    using galaxy::timing::ExactPeriodicDeadlineConsumer;
    using galaxy::timing::PendingEventLatch;

    constexpr EventKind kKind = EventKind::VideoInterface;
    PendingEventLatch latch;
    ExactPeriodicDeadlineConsumer consumer(1'000u, 100u, 7u);
    bool passed = true;

    passed &= expect(
        latch.publish_periodic(DeadlineEvent{kKind, 7u, 1'000u}),
        "first level-triggered VI identity publishes");
    passed &= expect(
        consumer.inspect(latch, kKind, 1'099u).status ==
            galaxy::timing::PeriodicConsumeStatus::Ready,
        "VI is ready before time crosses the following publication deadline");
    const auto incomplete = consumer.consume_one_or_coalesce_level_or_throw(
        latch, kKind, 1'100u);
    passed &= expect(
        !incomplete.has_value() && latch.pending(kKind) == 1u &&
            consumer.stats().locked_rate_valid &&
            consumer.stats().delivered_edges == 0u &&
            consumer.stats().delivered_interrupts == 0u,
        "a prior Ready observation may defer without losing an edge when the next period elapses");

    passed &= expect(
        latch.publish_periodic(DeadlineEvent{kKind, 8u, 1'100u}) &&
            latch.publish_periodic(DeadlineEvent{kKind, 9u, 1'200u}),
        "remaining contiguous level-triggered VI identities publish");
    const auto coalesced = consumer.consume_one_or_coalesce_level_or_throw(
        latch, kKind, 1'200u);
    passed &= expect(
        coalesced.has_value() && coalesced->sequence == 9u &&
            coalesced->deadline_ticks == 1'200u &&
            latch.pending(kKind) == 0u,
        "three elapsed VI levels collapse to their latest exact identity");

    passed &= expect(
        latch.publish_periodic(DeadlineEvent{kKind, 10u, 1'300u}),
        "next singleton VI identity publishes after coalescing");
    const auto singleton = consumer.consume_one_or_coalesce_level_or_throw(
        latch, kKind, 1'300u);
    passed &= expect(
        singleton.has_value() && singleton->sequence == 10u &&
            singleton->deadline_ticks == 1'300u,
        "singleton delivery resumes at the exact successor identity");

    const auto& stats = consumer.stats();
    passed &= expect(
        stats.locked_rate_valid && stats.scheduled_edges == 4u &&
            stats.delivered_edges == 4u &&
            stats.last_delivered_sequence == 10u,
        "level coalescing preserves the immutable 100-tick schedule");
    passed &= expect(
        stats.delivered_interrupts == 2u &&
            stats.coalesced_level_edges == 2u &&
            stats.level_coalescing_events == 1u &&
            stats.missed_or_coalesced_edges == 2u,
        "level coalescing remains explicit in acceptance telemetry");
    return passed;
}

bool periodic_prefix_take_preserves_concurrent_successor() {
    using galaxy::timing::DeadlineEvent;
    using galaxy::timing::EventKind;
    using galaxy::timing::PendingEventLatch;

    constexpr EventKind kKind = EventKind::VideoInterface;
    PendingEventLatch latch;
    bool passed = true;
    passed &= expect(
        latch.publish_periodic(DeadlineEvent{kKind, 7u, 1'000u}) &&
            latch.publish_periodic(DeadlineEvent{kKind, 8u, 1'100u}) &&
            latch.publish_periodic(DeadlineEvent{kKind, 9u, 1'200u}),
        "three contiguous periodic identities publish before prefix take");
    const auto take = latch.take_periodic_prefix_if_matches(
        kKind, 7u, 1'000u, 100u, 2u);
    passed &= expect(
        take.status == PendingEventLatch::PeriodicPrefixTakeStatus::Taken &&
            take.observed_count == 3u && take.batch.count == 2u &&
            take.batch.first_sequence == 7u &&
            take.batch.last_sequence == 8u &&
            take.batch.first_deadline_ticks == 1'000u &&
            take.batch.last_deadline_ticks == 1'100u,
        "periodic prefix take returns exactly the requested due identities");
    passed &= expect(
        latch.pending(kKind) == 1u &&
            (latch.mask() & galaxy::timing::event_bit(kKind)) != 0u,
        "periodic prefix take retains a concurrent successor and its wake bit");
    const auto suffix = latch.take_all_periodic(kKind);
    passed &= expect(
        suffix.count == 1u && suffix.first_sequence == 9u &&
            suffix.last_sequence == 9u &&
            suffix.first_deadline_ticks == 1'200u &&
            suffix.last_deadline_ticks == 1'200u,
        "retained successor preserves its original periodic identity");

    PendingEventLatch incomplete;
    passed &= expect(
        incomplete.publish_periodic(
            DeadlineEvent{kKind, 20u, 2'000u}),
        "incomplete periodic prefix fixture publishes");
    const auto incomplete_take = incomplete.take_periodic_prefix_if_matches(
        kKind, 20u, 2'000u, 100u, 2u);
    passed &= expect(
        incomplete_take.status ==
                PendingEventLatch::PeriodicPrefixTakeStatus::Incomplete &&
            incomplete_take.observed_count == 1u &&
            incomplete.pending(kKind) == 1u,
        "incomplete periodic prefix consumes nothing");
    const auto invalid_take = incomplete.take_periodic_prefix_if_matches(
        kKind, 21u, 2'000u, 100u, 1u);
    passed &= expect(
        invalid_take.status ==
                PendingEventLatch::PeriodicPrefixTakeStatus::Invalid &&
            incomplete.pending(kKind) == 1u,
        "mismatched periodic prefix consumes nothing");

    PendingEventLatch irregular;
    passed &= expect(
        irregular.publish_periodic(
            DeadlineEvent{kKind, 30u, 3'000u}) &&
            irregular.publish_periodic(
                DeadlineEvent{kKind, 31u, 3'050u}) &&
            !irregular.publish_periodic(
                DeadlineEvent{kKind, 32u, 3'200u}),
        "periodic publication rejects an irregular interior deadline step");
    const auto wrong_period = irregular.take_periodic_prefix_if_matches(
        kKind, 30u, 3'000u, 100u, 2u);
    passed &= expect(
        wrong_period.status ==
                PendingEventLatch::PeriodicPrefixTakeStatus::Invalid &&
            irregular.pending(kKind) == 2u,
        "prefix validation cannot fabricate an unpublished interior deadline");
    const auto regular_pair = irregular.take_periodic_prefix_if_matches(
        kKind, 30u, 3'000u, 50u, 2u);
    passed &= expect(
        regular_pair.status ==
                PendingEventLatch::PeriodicPrefixTakeStatus::Taken &&
            regular_pair.batch.last_deadline_ticks == 3'050u &&
            irregular.pending(kKind) == 0u,
        "the actually published regular period remains consumable");

    PendingEventLatch anonymous;
    passed &= expect(
        anonymous.publish(kKind),
        "anonymous singleton fixture publishes");
    const auto anonymous_take = anonymous.take_periodic_prefix_if_matches(
        kKind, 40u, 4'000u, 100u, 1u);
    passed &= expect(
        anonymous_take.status ==
                PendingEventLatch::PeriodicPrefixTakeStatus::Invalid &&
            anonymous.pending(kKind) == 1u &&
            (anonymous.mask() & galaxy::timing::event_bit(kKind)) != 0u,
        "periodic prefix rejects an anonymous singleton without consuming it");

    PendingEventLatch retained;
    passed &= expect(
        retained.publish_periodic(
            DeadlineEvent{kKind, 50u, 5'000u}) &&
            retained.publish_periodic(
                DeadlineEvent{kKind, 51u, 5'100u}) &&
            retained.publish_periodic(
                DeadlineEvent{kKind, 52u, 5'200u}),
        "retained-period fixture publishes");
    const auto retained_prefix = retained.take_periodic_prefix_if_matches(
        kKind, 50u, 5'000u, 100u, 2u);
    passed &= expect(
        retained_prefix.status ==
                PendingEventLatch::PeriodicPrefixTakeStatus::Taken &&
            retained.pending(kKind) == 1u &&
            !retained.publish_periodic(
                DeadlineEvent{kKind, 53u, 5'350u}) &&
            retained.publish_periodic(
                DeadlineEvent{kKind, 53u, 5'300u}),
        "a singleton suffix retains its established deadline period");
    return passed;
}

bool level_periodic_consumer_rejects_anonymous_singleton() {
    using galaxy::timing::EventKind;
    using galaxy::timing::ExactPeriodicDeadlineConsumer;
    using galaxy::timing::PendingEventLatch;

    constexpr EventKind kKind = EventKind::VideoInterface;
    PendingEventLatch latch;
    ExactPeriodicDeadlineConsumer consumer(1'000u, 100u, 7u);
    bool passed = expect(
        latch.publish(kKind),
        "anonymous level-triggered VI fixture publishes");
    bool rejected = false;
    try {
        static_cast<void>(consumer.consume_one_or_coalesce_level_or_throw(
            latch, kKind, 1'000u));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    passed &= expect(
        rejected && latch.pending(kKind) == 1u &&
            !consumer.stats().locked_rate_valid &&
            consumer.stats().delivered_edges == 0u,
        "level-triggered consumption fails closed on anonymous identity");
    return passed;
}

bool periodic_prefix_concurrent_orders_converge() {
    using galaxy::timing::DeadlineEvent;
    using galaxy::timing::EventKind;
    using galaxy::timing::ExactPeriodicDeadlineConsumer;
    using galaxy::timing::PendingEventLatch;

    constexpr EventKind kKind = EventKind::VideoInterface;
    bool passed = true;
    for (std::uint64_t iteration = 0u; iteration < 128u; ++iteration) {
        PendingEventLatch latch;
        ExactPeriodicDeadlineConsumer consumer(1'000u, 100u, 7u);
        passed &= expect(
            latch.publish_periodic(
                DeadlineEvent{kKind, 7u, 1'000u}),
            "concurrent prefix fixture publishes its due identity");
        std::atomic_bool start{false};
        bool successor_published = false;
        std::optional<DeadlineEvent> consumed;
        std::exception_ptr publisher_error;
        std::exception_ptr consumer_error;
        // jthread cancellation also releases the first waiter if constructing
        // the second thread throws. Worker exceptions are reported after join.
        std::jthread publisher([&](std::stop_token stop) {
            while (!start.load(std::memory_order_acquire)) {
                if (stop.stop_requested()) return;
                std::this_thread::yield();
            }
            try {
                successor_published = latch.publish_periodic(
                    DeadlineEvent{kKind, 8u, 1'100u});
            } catch (...) { publisher_error = std::current_exception(); }
        });
        std::jthread consumer_thread([&](std::stop_token stop) {
            while (!start.load(std::memory_order_acquire)) {
                if (stop.stop_requested()) return;
                std::this_thread::yield();
            }
            try {
                consumed = consumer.consume_one_or_coalesce_level_or_throw(
                    latch, kKind, 1'099u);
            } catch (...) { consumer_error = std::current_exception(); }
        });
        start.store(true, std::memory_order_release);
        publisher.join();
        consumer_thread.join();
        const auto suffix = latch.take_all_periodic(kKind);
        passed &= expect(
            !publisher_error && !consumer_error &&
                successor_published && consumed.has_value() &&
                consumed->sequence == 7u &&
                consumed->deadline_ticks == 1'000u &&
                suffix.count == 1u && suffix.first_sequence == 8u &&
                suffix.last_sequence == 8u &&
                suffix.first_deadline_ticks == 1'100u &&
                suffix.last_deadline_ticks == 1'100u && latch.mask() == 0u,
            "publisher-first and consumer-first prefix orders converge");
    }
    return passed;
}

bool level_periodic_consumer_preserves_future_published_suffix() {
    using galaxy::timing::DeadlineEvent;
    using galaxy::timing::EventKind;
    using galaxy::timing::ExactPeriodicDeadlineConsumer;
    using galaxy::timing::PendingEventLatch;

    constexpr EventKind kKind = EventKind::VideoInterface;
    PendingEventLatch latch;
    ExactPeriodicDeadlineConsumer consumer(1'000u, 100u, 7u);
    bool passed = expect(
        latch.publish_periodic(DeadlineEvent{kKind, 7u, 1'000u}) &&
            latch.publish_periodic(DeadlineEvent{kKind, 8u, 1'100u}),
        "due VI and concurrently published successor retain identities");
    const auto first = consumer.consume_one_or_coalesce_level_or_throw(
        latch, kKind, 1'099u);
    passed &= expect(
        first.has_value() && first->sequence == 7u &&
            first->deadline_ticks == 1'000u && latch.pending(kKind) == 1u,
        "stale clock sample consumes only its due VI and preserves successor");
    const auto second = consumer.consume_one_or_coalesce_level_or_throw(
        latch, kKind, 1'100u);
    passed &= expect(
        second.has_value() && second->sequence == 8u &&
            second->deadline_ticks == 1'100u && latch.pending(kKind) == 0u,
        "preserved successor is consumed at its own deadline");
    const auto& stats = consumer.stats();
    passed &= expect(
        stats.locked_rate_valid && stats.scheduled_edges == 2u &&
            stats.delivered_edges == 2u &&
            stats.delivered_interrupts == 2u &&
            stats.coalesced_level_edges == 0u &&
            stats.level_coalescing_events == 0u,
        "future successor preservation does not invent coalesced VI loss");

    PendingEventLatch backlog_latch;
    ExactPeriodicDeadlineConsumer backlog_consumer(2'000u, 100u, 20u);
    passed &= expect(
        backlog_latch.publish_periodic(
            DeadlineEvent{kKind, 20u, 2'000u}) &&
            backlog_latch.publish_periodic(
                DeadlineEvent{kKind, 21u, 2'100u}) &&
            backlog_latch.publish_periodic(
                DeadlineEvent{kKind, 22u, 2'200u}),
        "two due VIs and one concurrently published successor retain identities");
    const auto backlog =
        backlog_consumer.consume_one_or_coalesce_level_or_throw(
            backlog_latch, kKind, 2'100u);
    passed &= expect(
        backlog.has_value() && backlog->sequence == 21u &&
            backlog->deadline_ticks == 2'100u &&
            backlog_latch.pending(kKind) == 1u,
        "due VI backlog coalesces without draining its future successor");
    const auto backlog_successor =
        backlog_consumer.consume_one_or_coalesce_level_or_throw(
            backlog_latch, kKind, 2'200u);
    passed &= expect(
        backlog_successor.has_value() &&
            backlog_successor->sequence == 22u &&
            backlog_successor->deadline_ticks == 2'200u &&
            backlog_latch.pending(kKind) == 0u,
        "successor after due backlog remains available at its own deadline");
    const auto& backlog_stats = backlog_consumer.stats();
    passed &= expect(
        backlog_stats.locked_rate_valid &&
            backlog_stats.scheduled_edges == 3u &&
            backlog_stats.delivered_edges == 3u &&
            backlog_stats.delivered_interrupts == 2u &&
            backlog_stats.coalesced_level_edges == 1u &&
            backlog_stats.level_coalescing_events == 1u,
        "due-prefix coalescing accounts only elapsed identities");
    return passed;
}

bool exact_periodic_consumer_handles_early_and_empty_observations() {
    using galaxy::timing::EventKind;
    using galaxy::timing::ExactPeriodicDeadlineConsumer;
    using galaxy::timing::PendingEventLatch;
    using galaxy::timing::PeriodicConsumeStatus;

    PendingEventLatch empty_latch;
    ExactPeriodicDeadlineConsumer empty_consumer(1'000u, 100u);
    bool passed = expect(
        empty_consumer
                .inspect(empty_latch, EventKind::VideoInterface, 999u)
                .status == PeriodicConsumeStatus::NotDue,
        "an empty VI latch before the deadline is not due");
    passed &= expect(
        !empty_consumer
             .consume_one_or_throw(
                 empty_latch, EventKind::VideoInterface, 999u)
             .has_value(),
        "empty early VI observation consumes nothing");
    passed &= expect(
        empty_consumer
                .inspect(empty_latch, EventKind::VideoInterface, 1'000u)
                .status == PeriodicConsumeStatus::AwaitingPublication,
        "an elapsed singleton deadline may await broker publication");
    passed &= expect(
        !empty_consumer
             .consume_one_or_throw(
                 empty_latch, EventKind::VideoInterface, 1'000u)
             .has_value(),
        "awaiting broker publication consumes nothing without failing");

    PendingEventLatch early_latch;
    ExactPeriodicDeadlineConsumer early_consumer(1'000u, 100u);
    passed &= expect(
        early_latch.publish(EventKind::VideoInterface),
        "early VI publication is retained for diagnosis");
    passed &= expect(
        early_consumer
                .inspect(early_latch, EventKind::VideoInterface, 999u)
                .status == PeriodicConsumeStatus::EarlyPublication,
        "publication before the absolute VI deadline is explicit");
    bool early_failed = false;
    try {
        (void)early_consumer.consume_one_or_throw(
            early_latch, EventKind::VideoInterface, 999u);
    } catch (const std::runtime_error&) {
        early_failed = true;
    }
    passed &= expect(
        early_failed,
        "early VI publication hard-fails strict consumption");
    passed &= expect(
        early_latch.pending(EventKind::VideoInterface) == 1u,
        "early publication failure consumes no VI edge");
    passed &= expect(
        !early_consumer.stats().locked_rate_valid &&
            early_consumer.stats().early_publication_edges == 1u,
        "early publication invalidates and annotates the cadence audit");
    return passed;
}

bool exact_periodic_consumer_fails_closed_on_overflow() {
    using galaxy::timing::EventKind;
    using galaxy::timing::ExactPeriodicDeadlineConsumer;
    using galaxy::timing::PendingEventLatch;
    using galaxy::timing::PeriodicConsumeStatus;

    PendingEventLatch due_count_latch;
    ExactPeriodicDeadlineConsumer due_count_consumer(0u, 1u);
    const auto due_count_observation = due_count_consumer.inspect(
        due_count_latch,
        EventKind::VideoInterface,
        std::numeric_limits<std::uint64_t>::max());
    bool passed = expect(
        due_count_observation.status ==
            PeriodicConsumeStatus::ArithmeticOverflow,
        "unrepresentable elapsed-edge count is explicit");
    bool due_count_failed = false;
    try {
        (void)due_count_consumer.consume_one_or_throw(
            due_count_latch,
            EventKind::VideoInterface,
            std::numeric_limits<std::uint64_t>::max());
    } catch (const std::overflow_error&) {
        due_count_failed = true;
    }
    passed &= expect(
        due_count_failed &&
            due_count_consumer.stats().arithmetic_failures == 1u,
        "elapsed-edge overflow hard-fails and is audited");

    PendingEventLatch origin_latch;
    ExactPeriodicDeadlineConsumer origin_consumer(
        std::numeric_limits<std::uint64_t>::max(), 2u);
    passed &= expect(
        origin_latch.publish(EventKind::VideoInterface),
        "maximum-tick first deadline can be published");
    passed &= expect(
        origin_consumer
            .consume_one_or_throw(
                origin_latch,
                EventKind::VideoInterface,
                std::numeric_limits<std::uint64_t>::max())
            .has_value(),
        "maximum-tick first deadline remains representable");
    passed &= expect(
        origin_latch.publish(EventKind::VideoInterface),
        "second maximum-origin edge is retained before audit");
    passed &= expect(
        origin_consumer
                .inspect(
                    origin_latch,
                    EventKind::VideoInterface,
                    std::numeric_limits<std::uint64_t>::max())
                .status == PeriodicConsumeStatus::ArithmeticOverflow,
        "origin-plus-period overflow is explicit");
    return passed;
}

bool exact_periodic_consumer_closes_publication_observation_race() {
    using galaxy::timing::EventKind;
    using galaxy::timing::ExactPeriodicDeadlineConsumer;
    using galaxy::timing::PendingEventLatch;
    using galaxy::timing::PeriodicConsumeStatus;

    PendingEventLatch latch;
    ExactPeriodicDeadlineConsumer consumer(1'000u, 100u);
    bool passed = expect(
        latch.publish(EventKind::VideoInterface),
        "first racing VI publication succeeds");
    passed &= expect(
        consumer
                .inspect(latch, EventKind::VideoInterface, 1'000u)
                .status == PeriodicConsumeStatus::Ready,
        "first observation sees one ready VI edge");

    // This publication models the broker winning the race after a consumer's
    // non-mutating observation. Strict consumption must observe two atomically,
    // retain both, and invalidate the cadence.
    passed &= expect(
        latch.publish(EventKind::VideoInterface),
        "second racing VI publication succeeds");
    bool race_failed = false;
    try {
        (void)consumer.consume_one_or_throw(
            latch, EventKind::VideoInterface, 1'000u);
    } catch (const std::runtime_error&) {
        race_failed = true;
    }
    passed &= expect(
        race_failed,
        "publication between observations becomes a backlog failure");
    passed &= expect(
        latch.pending(EventKind::VideoInterface) == 2u,
        "racing publication failure leaves both edges observable");
    passed &= expect(
        consumer.stats().max_pending_backlog == 2u &&
            consumer.stats().missed_or_coalesced_edges == 1u,
        "racing publication is counted rather than silently coalesced");
    return passed;
}

bool exact_periodic_consumer_repolls_after_prior_phase() {
    using galaxy::timing::EventKind;
    using galaxy::timing::ExactPeriodicDeadlineConsumer;
    using galaxy::timing::PendingEventLatch;

    constexpr std::uint64_t kFirstDeadline = 1'000u;
    constexpr std::uint64_t kPeriod = 100u;
    PendingEventLatch latch;
    ExactPeriodicDeadlineConsumer consumer(kFirstDeadline, kPeriod);

    // Model one checkpoint's initial VI poll followed by decrementer/scheduler
    // work. The edge did not exist at the initial poll and is published before
    // the prior phase returns, so only a fresh post-phase read can see it.
    const bool stale_poll_due =
        latch.pending(EventKind::VideoInterface) != 0u;
    bool passed = expect(
        !stale_poll_due,
        "VI is absent from the checkpoint's pre-decrementer snapshot");
    passed &= expect(
        latch.publish(EventKind::VideoInterface),
        "decrementer-phase VI publication succeeds");
    const bool fresh_poll_due =
        latch.pending(EventKind::VideoInterface) != 0u;
    passed &= expect(
        fresh_poll_due,
        "fresh post-decrementer VI poll observes the published edge");

    if (fresh_poll_due) {
        const auto event = consumer.consume_one_or_throw(
            latch, EventKind::VideoInterface, kFirstDeadline + kPeriod - 1u);
        passed &= expect(
            event.has_value() && event->sequence == 1u &&
                event->deadline_ticks == kFirstDeadline,
            "post-decrementer poll consumes the exact scheduled VI edge");
    }
    const auto& stats = consumer.stats();
    passed &= expect(
        latch.pending(EventKind::VideoInterface) == 0u &&
            stats.locked_rate_valid && stats.scheduled_edges == 1u &&
            stats.delivered_edges == 1u && stats.max_pending_backlog == 1u,
        "post-decrementer repoll preserves singleton locked cadence");

    PendingEventLatch backlog_latch;
    ExactPeriodicDeadlineConsumer backlog_consumer(
        kFirstDeadline, kPeriod);
    passed &= expect(
        backlog_latch.publish(EventKind::VideoInterface, 2u),
        "two-edge post-decrementer backlog is retained");
    bool backlog_failed = false;
    try {
        (void)backlog_consumer.consume_one_or_throw(
            backlog_latch,
            EventKind::VideoInterface,
            kFirstDeadline + kPeriod);
    } catch (const std::runtime_error&) {
        backlog_failed = true;
    }
    passed &= expect(
        backlog_failed &&
            backlog_latch.pending(EventKind::VideoInterface) == 2u &&
            !backlog_consumer.stats().locked_rate_valid &&
            backlog_consumer.stats().scheduled_edges == 2u &&
            backlog_consumer.stats().delivered_edges == 0u &&
            backlog_consumer.stats().max_pending_backlog == 2u &&
            backlog_consumer.stats().missed_or_coalesced_edges == 1u,
        "post-decrementer repoll still hard-fails a two-edge VI backlog");
    return passed;
}

bool broker_only_latches_host_events() {
    ManualCounter counter{};
    galaxy::timing::RuntimeTimeline timeline(
        {&read_manual_counter, &counter, 1'000'000ull});
    galaxy::timing::PendingEventLatch latch;
    galaxy::timing::DeadlineBroker broker(timeline, latch);
    broker.set_periodic(
        galaxy::timing::EventKind::VideoInterface,
        0u,
        galaxy::timing::kViPeriodTicks);
    broker.start();
    const auto timeout =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (latch.pending(galaxy::timing::EventKind::VideoInterface) == 0u &&
           std::chrono::steady_clock::now() < timeout) {
        std::this_thread::yield();
    }
    broker.stop();
    broker.throw_if_failed();
    const galaxy::timing::DeadlinePublicationStats publication =
        broker.publication_stats(
            galaxy::timing::EventKind::VideoInterface);
    return expect(
               latch.pending(
                   galaxy::timing::EventKind::VideoInterface) == 1u,
               "deadline broker latches one due VI event without invoking guest code") &&
        expect(
            publication.publications == 1u &&
                publication.last_sequence == 1u &&
                publication.last_deadline_ticks == 0u &&
                publication.last_prepublication_ticks == 0u &&
                publication.last_postpublication_ticks == 0u &&
                publication.last_prepublication_lateness_ticks == 0u &&
                publication.last_postpublication_lateness_ticks == 0u &&
                publication.max_prepublication_lateness_ticks == 0u &&
                publication.max_postpublication_lateness_ticks == 0u,
            "deadline broker records exact pre/post publication timing bounds");
}

bool broker_periodic_rephase_discards_old_latch() {
    using galaxy::timing::EventKind;

    AtomicManualCounter counter{};
    galaxy::timing::RuntimeTimeline timeline(
        {&read_atomic_manual_counter,
         &counter,
         galaxy::timing::kTimelineTicksPerSecond});
    galaxy::timing::PendingEventLatch latch;
    galaxy::timing::DeadlineBroker broker(timeline, latch);
    broker.set_periodic(EventKind::InputReport, 0u, 10'000u);
    broker.start();

    const auto old_phase_timeout =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (latch.pending(EventKind::InputReport) == 0u && !broker.failed() &&
           std::chrono::steady_clock::now() < old_phase_timeout) {
        std::this_thread::yield();
    }

    bool passed = expect(
        latch.pending(EventKind::InputReport) == 1u,
        "broker publishes the old input phase before rephasing");

    // The guest's report-mode command owns the new phase. Replacing the
    // periodic schedule must invalidate an already-published edge from the
    // process-start phase before the replacement deadline can become due.
    broker.set_periodic(EventKind::InputReport, 5'000u, 10'000u, 1u);
    passed &= expect(
        latch.pending(EventKind::InputReport) == 0u &&
            (latch.mask() & galaxy::timing::event_bit(EventKind::InputReport)) ==
                0u,
        "broker rephase clears the old input edge and wake bit linearly");

    counter.value.store(5'000u, std::memory_order_release);
    const auto new_phase_timeout =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (latch.pending(EventKind::InputReport) == 0u && !broker.failed() &&
           std::chrono::steady_clock::now() < new_phase_timeout) {
        std::this_thread::yield();
    }
    passed &= expect(
        latch.pending(EventKind::InputReport) == 1u,
        "broker publishes exactly one edge at the replacement input phase");

    broker.stop();
    try {
        broker.throw_if_failed();
    } catch (const std::exception& error) {
        std::cerr << "FAILED: rephased broker failed: " << error.what() << '\n';
        passed = false;
    }
    return passed;
}

bool broker_rephase_starts_at_next_unconsumed_input_deadline() {
    using galaxy::timing::EventKind;

    constexpr std::uint64_t kDeviceOrigin = 5'000u;
    constexpr std::uint64_t kDevicePeriod = 10'000u;
    constexpr std::uint64_t kNextUnconsumedDeadline =
        kDeviceOrigin + kDevicePeriod;
    AtomicManualCounter counter{};
    galaxy::timing::RuntimeTimeline timeline(
        {&read_atomic_manual_counter,
         &counter,
         galaxy::timing::kTimelineTicksPerSecond});
    galaxy::timing::PendingEventLatch latch;
    galaxy::timing::DeadlineBroker broker(timeline, latch);
    broker.set_periodic(EventKind::InputReport, 0u, kDevicePeriod);
    broker.start();

    const auto startup_timeout =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (latch.pending(EventKind::InputReport) == 0u && !broker.failed() &&
           std::chrono::steady_clock::now() < startup_timeout) {
        std::this_thread::yield();
    }
    bool passed = expect(
        latch.pending(EventKind::InputReport) == 1u,
        "the process-start input service phase publishes its bootstrap edge");

    // Device service has already consumed the newly armed origin sample at
    // kDeviceOrigin. Rephase from the cadence's exact next unconsumed slot,
    // which must clear the stale bootstrap edge without publishing origin a
    // second time.
    counter.value.store(kDeviceOrigin, std::memory_order_release);
    broker.set_periodic(
        EventKind::InputReport,
        kNextUnconsumedDeadline,
        kDevicePeriod);
    passed &= expect(
        latch.pending(EventKind::InputReport) == 0u,
        "input rephase clears the stale bootstrap edge after inline origin consumption");

    const auto early_observation_end =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(5);
    while (std::chrono::steady_clock::now() < early_observation_end &&
           !broker.failed()) {
        if (latch.pending(EventKind::InputReport) != 0u) {
            break;
        }
        std::this_thread::yield();
    }
    passed &= expect(
        latch.pending(EventKind::InputReport) == 0u,
        "the broker cannot republish an already-consumed input origin");

    counter.value.store(kNextUnconsumedDeadline, std::memory_order_release);
    const auto next_deadline_timeout =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (latch.pending(EventKind::InputReport) == 0u && !broker.failed() &&
           std::chrono::steady_clock::now() < next_deadline_timeout) {
        std::this_thread::yield();
    }
    passed &= expect(
        latch.pending(EventKind::InputReport) == 1u,
        "the broker publishes exactly one edge at the next unconsumed input deadline");

    broker.stop();
    try {
        broker.throw_if_failed();
    } catch (const std::exception& error) {
        std::cerr << "FAILED: next-input-deadline broker failed: "
                  << error.what() << '\n';
        passed = false;
    }
    return passed;
}

bool broker_configures_worker_priority_before_timeline_reads() {
    // This checks the explicit Win32 fallback, not the later dynamic MMCSS
    // priority. Games registration can legitimately change GetThreadPriority
    // after configure_deadline_worker_scheduling has verified ABOVE_NORMAL.
    // Override only this process during this test and restore its prior value.
    struct RestoreMmcssEnvironment {
        std::wstring previous;
        bool existed{};
        bool active{};
        bool restore() noexcept {
            if (!active) return true;
            if (!SetEnvironmentVariableW(L"GALAXY_RUNTIME_MMCSS",
                    existed ? previous.c_str() : nullptr)) return false;
            active = false;
            return true;
        }
        ~RestoreMmcssEnvironment() { static_cast<void>(restore()); }
    } restore_mmcss;
    SetLastError(ERROR_SUCCESS);
    const DWORD required = GetEnvironmentVariableW(L"GALAXY_RUNTIME_MMCSS", nullptr, 0u);
    if (required != 0u) {
        restore_mmcss.existed = true;
        restore_mmcss.previous.resize(required);
        const DWORD read = GetEnvironmentVariableW(L"GALAXY_RUNTIME_MMCSS",
            restore_mmcss.previous.data(), required);
        if (read == 0u || read >= required) throw std::runtime_error("failed to preserve test MMCSS environment");
        restore_mmcss.previous.resize(read);
    } else {
        const DWORD error = GetLastError();
        if (error == ERROR_SUCCESS) restore_mmcss.existed = true;
        else if (error != ERROR_ENVVAR_NOT_FOUND)
            throw std::runtime_error("failed to query test MMCSS environment");
    }
    if (!SetEnvironmentVariableW(L"GALAXY_RUNTIME_MMCSS", L"0"))
        throw std::runtime_error("failed to configure fallback-priority test environment");
    restore_mmcss.active = true;
    WorkerPriorityCounter counter{};
    counter.owner_thread_id = GetCurrentThreadId();
    galaxy::timing::RuntimeTimeline timeline(
        {&read_worker_priority_counter, &counter, 1'000'000ull});
    galaxy::timing::PendingEventLatch latch;
    galaxy::timing::DeadlineBroker broker(timeline, latch);
    broker.schedule_once(galaxy::timing::EventKind::VideoInterface, 0u);
    broker.start();

    const auto timeout =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (!counter.worker_observed.load(std::memory_order_acquire) &&
           !broker.failed() &&
           std::chrono::steady_clock::now() < timeout) {
        std::this_thread::yield();
    }
    broker.stop();

    bool passed = true;
    try {
        broker.throw_if_failed();
    } catch (const std::exception& error) {
        std::cerr << "FAILED: deadline broker priority setup failed: "
                  << error.what() << '\n';
        passed = false;
    }
    const bool worker_observed =
        counter.worker_observed.load(std::memory_order_acquire);
    const int worker_priority =
        counter.observed_worker_priority.load(std::memory_order_acquire);
    passed &= expect(
        worker_observed,
        "deadline broker worker reads the timeline during startup");
    passed &= expect(
        worker_observed && worker_priority != THREAD_PRIORITY_ERROR_RETURN &&
            worker_priority >= THREAD_PRIORITY_ABOVE_NORMAL,
        "deadline broker raises its own priority before reading the timeline");
    passed &= expect(
        latch.pending(galaxy::timing::EventKind::VideoInterface) == 1u,
        "priority-configured deadline broker still publishes the due edge");
    passed &= expect(restore_mmcss.restore(), "fallback-priority test restores its process environment");
    return passed;
}

bool broker_replaces_and_cancels_one_shots_linearly() {
    using galaxy::timing::EventKind;

    ManualCounter counter{};
    galaxy::timing::RuntimeTimeline timeline(
        {&read_manual_counter, &counter, 1'000'000ull});
    galaxy::timing::PendingEventLatch latch;
    galaxy::timing::DeadlineBroker broker(timeline, latch);
    broker.replace_once(EventKind::AudioDma, 10'000u, 1u);
    broker.start();

    // The second generation is immediately due. Replacement must remove the
    // sleeping first generation and publish identity 2, never a bare count.
    broker.replace_once(EventKind::AudioDma, 0u, 2u);
    const auto timeout =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (latch.pending_replaceable_generation(EventKind::AudioDma) == 0u &&
           !broker.failed() && std::chrono::steady_clock::now() < timeout) {
        std::this_thread::yield();
    }
    bool passed = expect(
        latch.pending_replaceable_generation(EventKind::AudioDma) == 2u,
        "broker publishes only the latest replaceable AI generation");

    broker.cancel_once(EventKind::AudioDma, 3u);
    passed &= expect(
        latch.pending_replaceable_generation(EventKind::AudioDma) == 0u &&
            (latch.mask() &
             galaxy::timing::event_bit(EventKind::AudioDma)) == 0u,
        "broker cancellation linearly clears an already-published AI edge");
    broker.stop();
    try {
        broker.throw_if_failed();
    } catch (const std::exception& error) {
        std::cerr << "FAILED: replaceable broker failed: " << error.what()
                  << '\n';
        passed = false;
    }
    return passed;
}

bool invalid_and_overflow_paths_fail_closed() {
    bool invalid_frequency_failed = false;
    try {
        ManualCounter counter{};
        galaxy::timing::RuntimeTimeline timeline(
            {&read_manual_counter, &counter, 0});
        (void)timeline;
    } catch (const std::invalid_argument&) {
        invalid_frequency_failed = true;
    }

    bool backlog_failed = false;
    try {
        galaxy::timing::PeriodicDeadline deadline(1u, 1u);
        (void)deadline.collect_due(
            galaxy::timing::EventKind::VideoInterface, 10u, 9u);
    } catch (const std::overflow_error&) {
        backlog_failed = true;
    }

    bool saturated = false;
    const std::uint64_t scaled = galaxy::timing::scale_counter_delta(
        std::numeric_limits<std::uint64_t>::max(), 1u, &saturated);
    return expect(invalid_frequency_failed, "zero-frequency timeline hard-fails") &&
        expect(backlog_failed, "deadline backlog limit hard-fails instead of skipping") &&
        expect(saturated && scaled == std::numeric_limits<std::uint64_t>::max(),
               "counter conversion overflow saturates and reports failure state");
}

bool relative_vi_windows_are_exact_and_fail_closed() {
    bool passed = expect(
        !galaxy::timing::relative_vi_window_active(
            false, 100u, 150u, 40u, 60u),
        "relative VI window stays closed until its exact anchor is observed");
    passed &= expect(
        !galaxy::timing::relative_vi_window_active(
            true, 100u, 99u, 0u, 60u),
        "relative VI window rejects a current VI preceding its anchor");
    passed &= expect(
        !galaxy::timing::relative_vi_window_active(
            true, 100u, 139u, 40u, 60u) &&
            galaxy::timing::relative_vi_window_active(
                true, 100u, 140u, 40u, 60u) &&
            galaxy::timing::relative_vi_window_active(
                true, 100u, 160u, 40u, 60u) &&
            !galaxy::timing::relative_vi_window_active(
                true, 100u, 161u, 40u, 60u),
        "relative VI window includes both exact offset boundaries only");
    passed &= expect(
        !galaxy::timing::relative_vi_window_active(
            true, 100u, 150u, 60u, 40u),
        "relative VI window fails closed on reversed offsets");
    passed &= expect(
        galaxy::timing::relative_vi_window_active(
            true,
            std::numeric_limits<std::uint64_t>::max() - 10u,
            std::numeric_limits<std::uint64_t>::max(),
            10u,
            10u),
        "relative VI window remains exact at the uint64 boundary");
    return passed;
}

bool ios_completion_wake_preserves_other_devices_and_followup() {
    using galaxy::timing::EventKind;
    using galaxy::timing::PendingEventLatch;
    PendingEventLatch latch;
    std::atomic_uint stage{0u};
    std::atomic_bool published{true};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    std::thread worker([&] {
        published.store(latch.publish(EventKind::IosCompletion),
            std::memory_order_release);
        stage.store(1u, std::memory_order_release);
        if (!await_handshake([&] { return stage.load(std::memory_order_acquire) == 2u; },
                             published, deadline)) return;
        if (!latch.publish(EventKind::IosCompletion)) {
            published.store(false, std::memory_order_release);
        }
    });
    if (!await_handshake([&] { return stage.load(std::memory_order_acquire) == 1u; },
                         published, deadline)) {
        worker.join();
        return expect(false, "IOS completion worker handshake failed or timed out");
    }
    bool passed = true;
    passed &= expect(latch.publish(EventKind::Dsp) &&
        latch.publish(EventKind::InputReport, 2u),
        "unrelated device work can remain pending beside storage completion");
    passed &= expect(latch.pending(EventKind::RuntimeFailure) == 0u &&
        (latch.mask() & galaxy::timing::event_bit(EventKind::IosCompletion)) != 0u,
        "normal storage completion wakes checkpoints without becoming a runtime failure");
    passed &= expect(latch.take_all(EventKind::IosCompletion) == 1u,
        "the first completion wake is retired before servicing its owned result");
    // Completion service can admit a new transaction. Its worker may finish
    // before that service returns, so the second wake must survive untouched.
    stage.store(2u, std::memory_order_release);
    worker.join();
    passed &= expect(published.load(std::memory_order_acquire) &&
        latch.take_all(EventKind::IosCompletion) == 1u &&
        latch.pending(EventKind::IosCompletion) == 0u,
        "a followup completion published during service is consumed exactly once later");
    passed &= expect(latch.take_all(EventKind::Dsp) == 1u &&
        latch.take_all(EventKind::InputReport) == 2u && latch.mask() == 0u,
        "storage wake consumption preserves independent DSP and input publications");
    return passed;
}

bool psmtx_guard_summary_capture_is_bounded_and_deferred() {
    using Capture = galaxy::diagnostics::PsmtxGuardTrace;
    Capture capture;
    bool passed = true;
    passed &= expect(!capture.capture(nullptr) && !capture.capture("ordinary trace"),
        "unrelated trace messages remain available to normal filtering");
    std::ostringstream empty;
    capture.dump(empty);
    passed &= expect(empty.str().empty(), "a missing flush never claims zero guard calls");
    const std::string prefix = "[psmtx-local-guard] ";
    const std::string exact = prefix + std::string(Capture::kTextCapacity - prefix.size() - 1u, 'x');
    passed &= expect(capture.capture(exact.c_str()) && capture.truncated() == 0u,
        "exact-capacity text preserves its terminator without truncation");
    const std::string oversized = exact + "not-retained";
    passed &= expect(capture.capture(oversized.c_str()) && capture.truncated() == 1u,
        "oversize summary is bounded and truncation is explicit");
    passed &= expect(capture.capture(
        "[psvec-normalize-local-guard] eligible=1"),
        "normalization summaries bypass ordinary Trace filtering too");
    for (std::size_t index = capture.retained(); index < Capture::kCapacity; ++index) {
        passed &= expect(capture.capture((prefix + "eligible=1").c_str()),
            "recognized summaries are retained before window filtering");
    }
    passed &= expect(capture.capture((prefix + "overflow-only").c_str()) &&
        capture.retained() == Capture::kCapacity && capture.overflow() == 1u,
        "capacity overflow consumes the message without overwriting earlier evidence");
    std::ostringstream output;
    capture.dump(output);
    passed &= expect(output.str().find("psmtxSeen=16 psvecNormalizeSeen=1") !=
            std::string::npos &&
        output.str().find("overflow=1 truncated=1") != std::string::npos &&
        output.str().find(exact) != std::string::npos &&
        output.str().find("[psvec-normalize-local-guard] eligible=1") !=
            std::string::npos &&
        output.str().find("not-retained") == std::string::npos &&
        output.str().find("overflow-only") == std::string::npos,
        "terminal dump reports loss and safely prints only retained bounded text");
    return passed;
}

}  // namespace

int main() {
    bool passed = true;
    passed &= run_test("ios_completion_wake_preserves_other_devices_and_followup", ios_completion_wake_preserves_other_devices_and_followup);
    passed &= run_test("psmtx_guard_summary_capture_is_bounded_and_deferred", psmtx_guard_summary_capture_is_bounded_and_deferred);
    passed &= run_test("counter_scale_matches_wide_integer_oracle", counter_scale_matches_wide_integer_oracle);
    passed &= run_test("timeline_exactness", timeline_exactness);
    passed &= run_test("timeline_rebase_excludes_verified_host_suspension", timeline_rebase_excludes_verified_host_suspension);
    passed &= run_test("timeline_rejects_recovery_after_device_observation", timeline_rejects_recovery_after_device_observation);
    passed &= run_test("host_suspension_candidate_interval_boundaries", host_suspension_candidate_interval_boundaries);
    passed &= run_test("host_suspension_proof_selection_preserves_boundary_ownership", host_suspension_proof_selection_preserves_boundary_ownership);
    passed &= run_test("host_suspension_device_crossing_boundaries", host_suspension_device_crossing_boundaries);
    passed &= run_test("host_suspension_ai_arm_identity_boundaries", host_suspension_ai_arm_identity_boundaries);
    passed &= run_test("native_input_ai_preemption_policy_boundaries", native_input_ai_preemption_policy_boundaries);
    passed &= run_test("stall_accounting_is_honest", stall_accounting_is_honest);
    passed &= run_test("vi_one_hour_never_skips", vi_one_hour_never_skips);
    passed &= run_test("ai_deadline_is_exact", ai_deadline_is_exact);
    passed &= run_test("schedule_order_is_stable", schedule_order_is_stable);
    passed &= run_test("deadline_failures_preserve_owners", deadline_failures_preserve_owners);
    passed &= run_test("replaceable_schedule_keeps_only_latest_generation", replaceable_schedule_keeps_only_latest_generation);
    passed &= run_test("replaceable_schedule_restores_only_current_generation", replaceable_schedule_restores_only_current_generation);
    passed &= run_test("broker_rebases_and_restores_deadlines_atomically", broker_rebases_and_restores_deadlines_atomically);
    passed &= run_test("broker_rejects_wrong_restore_owner_transactionally", broker_rejects_wrong_restore_owner_transactionally);
    passed &= run_test("broker_rejects_observed_recovery_without_mutating_owners", broker_rejects_observed_recovery_without_mutating_owners);
    passed &= run_test("pending_latch_retains_counts", pending_latch_retains_counts);
    passed &= run_test("replaceable_latch_validates_generation_identity", replaceable_latch_validates_generation_identity);
    passed &= run_test("pending_latch_publication_drain_race_leaves_no_stale_mask", pending_latch_publication_drain_race_leaves_no_stale_mask);
    passed &= run_test("periodic_batch_identity_is_atomic_and_nonmixable", periodic_batch_identity_is_atomic_and_nonmixable);
    passed &= run_test("exact_periodic_consumer_accepts_one_on_time_edge", exact_periodic_consumer_accepts_one_on_time_edge);
    passed &= run_test("exact_periodic_consumer_accepts_one_late_edge_within_period", exact_periodic_consumer_accepts_one_late_edge_within_period);
    passed &= run_test("exact_periodic_consumer_rejects_267ms_backlog", exact_periodic_consumer_rejects_267ms_backlog);
    passed &= run_test("level_periodic_consumer_coalesces_only_complete_due_identity", level_periodic_consumer_coalesces_only_complete_due_identity);
    passed &= run_test("periodic_prefix_take_preserves_concurrent_successor", periodic_prefix_take_preserves_concurrent_successor);
    passed &= run_test("level_periodic_consumer_rejects_anonymous_singleton", level_periodic_consumer_rejects_anonymous_singleton);
    passed &= run_test("periodic_prefix_concurrent_orders_converge", periodic_prefix_concurrent_orders_converge);
    passed &= run_test("level_periodic_consumer_preserves_future_published_suffix", level_periodic_consumer_preserves_future_published_suffix);
    passed &= run_test("exact_periodic_consumer_handles_early_and_empty_observations", exact_periodic_consumer_handles_early_and_empty_observations);
    passed &= run_test("exact_periodic_consumer_fails_closed_on_overflow", exact_periodic_consumer_fails_closed_on_overflow);
    passed &= run_test("exact_periodic_consumer_closes_publication_observation_race", exact_periodic_consumer_closes_publication_observation_race);
    passed &= run_test("exact_periodic_consumer_repolls_after_prior_phase", exact_periodic_consumer_repolls_after_prior_phase);
    passed &= run_test("broker_only_latches_host_events", broker_only_latches_host_events);
    passed &= run_test("broker_periodic_rephase_discards_old_latch", broker_periodic_rephase_discards_old_latch);
    passed &= run_test("broker_rephase_starts_at_next_unconsumed_input_deadline", broker_rephase_starts_at_next_unconsumed_input_deadline);
    passed &= run_test("broker_configures_worker_priority_before_timeline_reads", broker_configures_worker_priority_before_timeline_reads);
    passed &= run_test("broker_replaces_and_cancels_one_shots_linearly", broker_replaces_and_cancels_one_shots_linearly);
    passed &= run_test("invalid_and_overflow_paths_fail_closed", invalid_and_overflow_paths_fail_closed);
    passed &= run_test("relative_vi_windows_are_exact_and_fail_closed", relative_vi_windows_are_exact_and_fail_closed);
    return passed ? 0 : 1;
}
