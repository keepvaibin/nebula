#include "galaxy/decrementer_deadline.h"
#include "galaxy/runtime_timeline.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

using galaxy::timing::DecrementerDeadlineArm;
using galaxy::timing::DecrementerDeadlineTracker;
using galaxy::timing::kDecrementerTicksPerSecond;
using galaxy::timing::kDecrementerWrapTicks;

static_assert(kDecrementerTicksPerSecond == 60'750'000ull);
static_assert(
    kDecrementerTicksPerSecond == galaxy::timing::kTimelineTicksPerSecond);
static_assert(kDecrementerWrapTicks == 0x1'0000'0000ull);

bool expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

template <typename Exception, typename Callback>
bool expect_exception(Callback&& callback, std::string_view message) {
    try {
        std::forward<Callback>(callback)();
    } catch (const Exception&) {
        return true;
    } catch (...) {
    }
    return expect(false, message);
}

bool raw_timeline_drives_value_and_wrap() {
    constexpr std::uint64_t kRawStart = 1'000u;
    constexpr std::uint64_t kUnrelatedGuestTbOffset = 0x1234'5678ull;
    DecrementerDeadlineTracker tracker;
    const DecrementerDeadlineArm arm = tracker.initialize(kRawStart, 2u);

    bool passed = expect(
        arm == DecrementerDeadlineArm{kRawStart + 3u, 1u},
        "initial arm is value-plus-one raw ticks");
    passed &= expect(
        tracker.current_value(kRawStart) == 2u &&
            tracker.current_value(kRawStart + 1u) == 1u &&
            tracker.current_value(kRawStart + 2u) == 0u &&
            tracker.current_value(kRawStart + 3u) == 0xffff'ffffu,
        "DEC subtracts the low 32 bits of raw elapsed ticks");
    passed &= expect(
        tracker.current_value(kRawStart + kDecrementerWrapTicks) == 2u,
        "DEC value wraps exactly every 2^32 raw ticks");
    passed &= expect(
        tracker.current_value(kRawStart) !=
            tracker.current_value(
                kRawStart + kUnrelatedGuestTbOffset),
        "only the supplied raw tick, never a hidden guest TB offset, affects DEC");
    passed &= expect(
        !tracker.pending_request() && tracker.arm().has_value() &&
            *tracker.arm() == arm,
        "initialization does not fabricate an interrupt request");
    return passed;
}

bool zero_value_deadline_is_next_tick() {
    DecrementerDeadlineTracker tracker;
    const DecrementerDeadlineArm first = tracker.initialize(50u, 0u);
    bool passed = expect(
        first == DecrementerDeadlineArm{51u, 1u},
        "zero DEC transitions on the immediately following raw tick");
    passed &= expect(
        tracker.current_value(50u) == 0u &&
            tracker.current_value(51u) == 0xffff'ffffu,
        "zero crosses from nonnegative to negative at its deadline");
    const DecrementerDeadlineArm next =
        tracker.on_deadline(51u, first.generation);
    passed &= expect(
        next == DecrementerDeadlineArm{51u + kDecrementerWrapTicks, 2u} &&
            tracker.pending_request(),
        "zero deadline latches and rearms one exact wrap later");
    return passed;
}

bool first_deadline_covers_sign_boundaries() {
    constexpr std::uint64_t kStart = 500u;
    constexpr std::array<std::uint32_t, 5> kValues{
        0u, 1u, 0x7fff'ffffu, 0x8000'0000u, 0xffff'ffffu};
    bool passed = true;
    for (const std::uint32_t value : kValues) {
        DecrementerDeadlineTracker tracker;
        const DecrementerDeadlineArm arm = tracker.initialize(kStart, value);
        const std::uint64_t expected_deadline =
            kStart + static_cast<std::uint64_t>(value) + 1u;
        passed &= expect(
            arm.deadline_ticks == expected_deadline,
            "every sign-boundary value uses exact value-plus-one timing");
        passed &= expect(
            tracker.current_value(expected_deadline - 1u) == 0u &&
                tracker.current_value(expected_deadline) == 0xffff'ffffu,
            "every first arm identifies the exact architectural MSB edge");
    }
    return passed;
}

bool same_value_write_rearms_from_write_tick() {
    DecrementerDeadlineTracker tracker;
    const DecrementerDeadlineArm first = tracker.initialize(100u, 10u);
    const DecrementerDeadlineArm second = tracker.on_write(105u, 10u);
    return expect(
               first == DecrementerDeadlineArm{111u, 1u},
               "first same-value fixture arm is exact") &&
        expect(
            second == DecrementerDeadlineArm{116u, 2u},
            "same-value store replaces the counter at its exact write tick") &&
        expect(
            tracker.current_value(105u) == 10u &&
                tracker.current_value(115u) == 0u &&
                !tracker.pending_request(),
            "same-value replacement restarts DEC without a false transition");
}

bool store_transition_latches_only_zero_to_one() {
    DecrementerDeadlineTracker positive;
    (void)positive.initialize(1'000u, 100u);
    const DecrementerDeadlineArm negative_arm =
        positive.on_write(1'001u, 0xffff'ffffu);
    bool passed = expect(
        positive.pending_request(),
        "positive-to-negative DEC store latches a request immediately");
    passed &= expect(
        negative_arm == DecrementerDeadlineArm{
                            1'001u + kDecrementerWrapTicks, 2u},
        "negative store still schedules its next natural zero-to-one edge");
    passed &= expect(
        positive.take_pending_request() &&
            !positive.take_pending_request(),
        "store-transition request is acknowledged exactly once");

    DecrementerDeadlineTracker negative;
    (void)negative.initialize(2'000u, 0xffff'fff0u);
    (void)negative.on_write(2'001u, 0xffff'ffffu);
    passed &= expect(
        !negative.pending_request(),
        "negative-to-negative DEC store does not invent a request");
    (void)negative.on_write(2'002u, 0x7fff'ffffu);
    passed &= expect(
        !negative.pending_request(),
        "negative-to-positive DEC store is not a zero-to-one transition");
    return passed;
}

bool writes_preserve_and_coalesce_requests() {
    DecrementerDeadlineTracker tracker;
    (void)tracker.initialize(100u, 5u);
    (void)tracker.on_write(101u, 0x8000'0000u);
    const DecrementerDeadlineArm replacement = tracker.on_write(102u, 0u);
    bool passed = expect(
        tracker.pending_request(),
        "a later write never clears an existing DEC request");

    const DecrementerDeadlineArm recurring = tracker.on_deadline(
        replacement.deadline_ticks, replacement.generation);
    passed &= expect(
        tracker.pending_request() &&
            recurring.generation == replacement.generation + 1u,
        "natural edges coalesce behind an already-pending request");
    passed &= expect(
        tracker.take_pending_request() && !tracker.pending_request() &&
            !tracker.take_pending_request(),
        "coalesced requests require one acknowledgement");

    DecrementerDeadlineTracker overdue;
    (void)overdue.initialize(10u, 0u);
    (void)overdue.on_write(12u, 20u);
    passed &= expect(
        overdue.pending_request(),
        "a write cannot erase a natural edge already elapsed before service");
    return passed;
}

bool deadline_observation_is_exact_and_strict() {
    DecrementerDeadlineTracker tracker;
    const DecrementerDeadlineArm first = tracker.initialize(100u, 9u);
    bool passed = expect_exception<std::runtime_error>(
        [&] { (void)tracker.on_deadline(109u, first.generation); },
        "pre-deadline observation must hard-fail");
    passed &= expect(
        !tracker.pending_request() && tracker.arm() == first,
        "early observation cannot mutate the arm or request latch");
    passed &= expect_exception<std::invalid_argument>(
        [&] { (void)tracker.on_deadline(110u, first.generation + 1u); },
        "mismatched replaceable generation must hard-fail");
    passed &= expect(
        !tracker.pending_request() && tracker.arm() == first,
        "generation mismatch cannot consume the active arm");

    const DecrementerDeadlineArm second =
        tracker.on_deadline(110u, first.generation);
    passed &= expect(
        tracker.pending_request() &&
            second == DecrementerDeadlineArm{
                          110u + kDecrementerWrapTicks, 2u},
        "exact deadline latches and advances from the scheduled edge");
    return passed;
}

bool recurrence_never_drifts_from_original_phase() {
    DecrementerDeadlineTracker tracker;
    DecrementerDeadlineArm arm = tracker.initialize(7u, 2u);
    constexpr std::uint64_t kFirstDeadline = 10u;
    bool passed = expect(
        arm.deadline_ticks == kFirstDeadline,
        "recurrence fixture has the expected origin");
    for (std::uint64_t sequence = 1u; sequence <= 3u; ++sequence) {
        arm = tracker.on_deadline(arm.deadline_ticks, arm.generation);
        passed &= expect(
            arm.deadline_ticks ==
                    kFirstDeadline + sequence * kDecrementerWrapTicks &&
                arm.generation == sequence + 1u,
            "each recurrence stays locked to the original deadline phase");
        (void)tracker.take_pending_request();
    }
    return passed;
}

bool late_observation_skips_cycles_without_drift() {
    DecrementerDeadlineTracker tracker;
    const DecrementerDeadlineArm first = tracker.initialize(10u, 0u);
    constexpr std::uint64_t kElapsedCompleteWraps = 3u;
    constexpr std::uint64_t kLateTicks = 123u;
    const std::uint64_t now = first.deadline_ticks +
        kElapsedCompleteWraps * kDecrementerWrapTicks + kLateTicks;
    const DecrementerDeadlineArm next =
        tracker.on_deadline(now, first.generation);
    return expect(
               next.deadline_ticks == first.deadline_ticks +
                       (kElapsedCompleteWraps + 1u) *
                           kDecrementerWrapTicks,
               "late observation skips every elapsed edge to the next exact phase") &&
        expect(
            next.deadline_ticks > now && next.generation == 2u &&
                tracker.pending_request(),
            "late multi-cycle service emits one coalesced request and one future arm");
}

bool invalid_and_overflow_paths_fail_closed() {
    constexpr std::uint64_t kMax =
        std::numeric_limits<std::uint64_t>::max();
    DecrementerDeadlineTracker invalid_generation;
    bool passed = expect_exception<std::invalid_argument>(
        [&] { (void)invalid_generation.initialize(0u, 0u, 0u); },
        "zero replaceable generation must hard-fail");
    passed &= expect(
        !invalid_generation.initialized() &&
            !invalid_generation.arm().has_value(),
        "failed initialization leaves no partial arm");

    DecrementerDeadlineTracker first_overflow;
    passed &= expect_exception<std::overflow_error>(
        [&] { (void)first_overflow.initialize(kMax, 0u); },
        "unrepresentable first transition must hard-fail");
    passed &= expect(
        !first_overflow.initialized(),
        "first-deadline overflow leaves the tracker uninitialized");

    DecrementerDeadlineTracker recurring_overflow;
    const DecrementerDeadlineArm last =
        recurring_overflow.initialize(kMax - 1u, 0u);
    passed &= expect(last.deadline_ticks == kMax, "last tick can be armed");
    passed &= expect_exception<std::overflow_error>(
        [&] {
            (void)recurring_overflow.on_deadline(
                last.deadline_ticks, last.generation);
        },
        "unrepresentable recurring transition must hard-fail");
    passed &= expect(
        recurring_overflow.arm() == last &&
            !recurring_overflow.pending_request(),
        "recurrence overflow cannot leave a partially advanced state");

    DecrementerDeadlineTracker write_overflow;
    const DecrementerDeadlineArm before_write =
        write_overflow.initialize(0u, 0u);
    passed &= expect_exception<std::overflow_error>(
        [&] { (void)write_overflow.on_write(kMax, 0u); },
        "unrepresentable replacement deadline must hard-fail");
    passed &= expect(
        write_overflow.arm() == before_write &&
            !write_overflow.pending_request(),
        "write overflow preserves the prior arm and request state");

    DecrementerDeadlineTracker generation_overflow;
    const DecrementerDeadlineArm terminal_generation =
        generation_overflow.initialize(0u, 0u, kMax);
    passed &= expect_exception<std::overflow_error>(
        [&] { (void)generation_overflow.on_write(0u, 1u); },
        "write cannot wrap a replaceable generation to zero");
    passed &= expect(
        generation_overflow.arm() == terminal_generation,
        "generation overflow cannot mutate the active arm");
    passed &= expect_exception<std::overflow_error>(
        [&] {
            (void)generation_overflow.on_deadline(
                terminal_generation.deadline_ticks,
                terminal_generation.generation);
        },
        "deadline recurrence cannot wrap a generation to zero");

    DecrementerDeadlineTracker backwards;
    (void)backwards.initialize(100u, 10u);
    passed &= expect_exception<std::invalid_argument>(
        [&] { (void)backwards.current_value(99u); },
        "raw timeline regression on read must hard-fail");
    passed &= expect_exception<std::invalid_argument>(
        [&] { (void)backwards.on_write(99u, 10u); },
        "raw timeline regression on write must hard-fail");
    passed &= expect_exception<std::logic_error>(
        [&] { (void)backwards.initialize(100u, 10u); },
        "tracker cannot be initialized twice");

    DecrementerDeadlineTracker post_deadline_regression;
    const DecrementerDeadlineArm regression_arm =
        post_deadline_regression.initialize(0u, 0u);
    (void)post_deadline_regression.on_deadline(
        100u, regression_arm.generation);
    passed &= expect_exception<std::invalid_argument>(
        [&] { (void)post_deadline_regression.current_value(99u); },
        "read cannot regress behind a prior late deadline observation");
    passed &= expect_exception<std::invalid_argument>(
        [&] { (void)post_deadline_regression.on_write(99u, 1u); },
        "write cannot regress behind a prior late deadline observation");
    return passed;
}

}  // namespace

int main() {
    bool passed = true;
    passed &= raw_timeline_drives_value_and_wrap();
    passed &= zero_value_deadline_is_next_tick();
    passed &= first_deadline_covers_sign_boundaries();
    passed &= same_value_write_rearms_from_write_tick();
    passed &= store_transition_latches_only_zero_to_one();
    passed &= writes_preserve_and_coalesce_requests();
    passed &= deadline_observation_is_exact_and_strict();
    passed &= recurrence_never_drifts_from_original_phase();
    passed &= late_observation_skips_cycles_without_drift();
    passed &= invalid_and_overflow_paths_fail_closed();
    return passed ? 0 : 1;
}
