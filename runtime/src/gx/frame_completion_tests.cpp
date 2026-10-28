// Tests of FramePeCompletionContract.
#include "galaxy/gx/frame_completion.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using galaxy::gx::FramePeCompletionContract;
using galaxy::gx::FramePeCompletionStats;
using galaxy::gx::FramePeCompletionToken;
using galaxy::gx::FramePeWaitState;

bool expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

template <typename Function>
bool expect_logic_error(Function&& function, std::string_view message) {
    try {
        function();
    } catch (const std::logic_error&) {
        return true;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << message << " (wrong exception: "
                  << error.what() << ")\n";
        return false;
    }
    std::cerr << "FAIL: " << message << " (no exception)\n";
    return false;
}

struct FakeFence {
    std::uint64_t completed = 0;
};

enum class DeterministicWaitResult {
    Complete,
    Timeout,
};

DeterministicWaitResult deterministic_wait(
    FramePeCompletionContract& contract,
    FramePeCompletionToken token,
    const FakeFence& fence,
    unsigned observation_budget) {
    for (unsigned observation = 0; observation < observation_budget;
         ++observation) {
        (void)contract.observe_completed_fence(fence.completed);
        if (contract.probe_wait(token) == FramePeWaitState::Complete) {
            contract.consume_wait(token);
            return DeterministicWaitResult::Complete;
        }
    }
    return DeterministicWaitResult::Timeout;
}

void deterministic_wait_or_throw(
    FramePeCompletionContract& contract,
    FramePeCompletionToken token,
    const FakeFence& fence,
    unsigned observation_budget) {
    if (deterministic_wait(contract, token, fence, observation_budget) ==
        DeterministicWaitResult::Timeout) {
        throw std::runtime_error(
            "deterministic frame PE completion wait timed out");
    }
}

bool conservation_holds(const FramePeCompletionStats& stats) {
    return stats.issued == stats.completed + stats.unresolved +
            stats.submission_ready + stats.fence_pending &&
        stats.consumed <= stats.completed &&
        stats.completed <= stats.resolved &&
        stats.resolved <= stats.issued;
}

bool ordered_fake_fence_completion() {
    FramePeCompletionContract contract;
    contract.reset();
    const FramePeCompletionToken first = contract.issue();
    const FramePeCompletionToken second = contract.issue();
    const FramePeCompletionToken third = contract.issue();
    contract.resolve_to_fence(first, 41u);
    contract.resolve_to_fence(second, 42u);
    contract.resolve_to_fence(third, 43u);

    FakeFence fence{42u};
    if (!expect(
            deterministic_wait(contract, first, fence, 1u) ==
                DeterministicWaitResult::Complete,
            "first frame completes at its exact fake fence") ||
        !expect(
            deterministic_wait(contract, second, fence, 1u) ==
                DeterministicWaitResult::Complete,
            "second frame completes at its exact fake fence") ||
        !expect(
            contract.probe_wait(third) == FramePeWaitState::Pending,
            "later frame remains pending") ||
        !expect(conservation_holds(contract.stats()),
                "partial completion conserves every token")) {
        return false;
    }

    fence.completed = 43u;
    return expect(
               deterministic_wait(contract, third, fence, 1u) ==
                   DeterministicWaitResult::Complete,
               "third frame completes after its own fence") &&
        expect(contract.stats().completed == 3u &&
                   contract.stats().consumed == 3u,
               "all three completions are consumed exactly once") &&
        expect(conservation_holds(contract.stats()),
               "completed queue conserves every token");
}

bool deterministic_timeout_does_not_forge_completion() {
    FramePeCompletionContract contract;
    contract.reset();
    const FramePeCompletionToken token = contract.issue();
    contract.resolve_to_fence(token, 9u);
    const FakeFence fence{8u};
    bool timeout_hard_failed = false;
    try {
        deterministic_wait_or_throw(contract, token, fence, 4u);
    } catch (const std::runtime_error&) {
        timeout_hard_failed = true;
    }
    const FramePeCompletionStats stats = contract.stats();
    return expect(timeout_hard_failed,
                  "bounded fake wait hard-fails on timeout") &&
        expect(stats.completed == 0u && stats.consumed == 0u &&
                   stats.fence_pending == 1u,
               "timeout neither completes nor consumes the token") &&
        expect(conservation_holds(stats),
               "timeout preserves token conservation");
}

bool submission_completion_stays_behind_older_fence() {
    FramePeCompletionContract contract;
    contract.reset();
    const FramePeCompletionToken first = contract.issue();
    const FramePeCompletionToken second = contract.issue();
    contract.resolve_to_fence(first, 5u);
    contract.resolve_on_submission(second);
    (void)contract.observe_completed_fence(4u);
    if (!expect(contract.stats().completed == 0u,
                "newer submission completion cannot pass an older GPU fence")) {
        return false;
    }
    (void)contract.observe_completed_fence(5u);
    return expect(contract.stats().completed == 2u,
                  "ordered frontier releases both ready frames") &&
        expect(conservation_holds(contract.stats()),
               "mixed CPU/GPU resolution conserves tokens");
}

bool preclassified_event_free_receipts_do_not_serialize() {
    FramePeCompletionContract contract;
    contract.reset();
    const FramePeCompletionToken first = contract.issue_event_free();
    const FramePeCompletionToken second = contract.issue_event_free();
    if (!expect(
            contract.probe_wait(first) == FramePeWaitState::Complete,
            "first preclassified event-free receipt is immediately ready")) {
        return false;
    }
    contract.consume_wait(first);
    if (!expect(
            contract.probe_wait(second) == FramePeWaitState::Complete,
            "second preclassified event-free receipt is independently ready")) {
        return false;
    }
    contract.consume_wait(second);
    const FramePeCompletionStats stats = contract.stats();
    return expect(
               stats.issued == 2u && stats.resolved == 2u &&
                   stats.completed == 2u && stats.consumed == 2u,
               "event-free receipts preserve exact conservation") &&
        expect(conservation_holds(stats),
               "event-free receipt queue satisfies conservation audit");
}

bool event_free_receipt_cannot_pass_older_pe_fence() {
    FramePeCompletionContract contract;
    contract.reset();
    const FramePeCompletionToken pe = contract.issue();
    const FramePeCompletionToken event_free = contract.issue_event_free();
    const FramePeCompletionToken later_pe = contract.issue();

    if (!expect(
            contract.probe_wait(pe) == FramePeWaitState::Pending,
            "unresolved PE token blocks the completion frontier") ||
        !expect_logic_error(
            [&] { (void)contract.probe_wait(event_free); },
            "wait order cannot skip the older PE token")) {
        return false;
    }

    contract.resolve_to_fence(pe, 7u);
    contract.resolve_to_fence(later_pe, 8u);
    (void)contract.observe_completed_fence(7u);
    if (!expect(
            contract.probe_wait(pe) == FramePeWaitState::Complete,
            "older PE token completes at its exact fence")) {
        return false;
    }
    contract.consume_wait(pe);
    if (!expect(
            contract.probe_wait(event_free) == FramePeWaitState::Complete,
            "ready event-free receipt follows the older PE fence")) {
        return false;
    }
    contract.consume_wait(event_free);
    if (!expect(
            contract.probe_wait(later_pe) == FramePeWaitState::Pending,
            "later PE token still requires its own fence")) {
        return false;
    }
    (void)contract.observe_completed_fence(8u);
    contract.consume_wait(later_pe);
    return expect(conservation_holds(contract.stats()),
                  "mixed preclassified/fenced sequence conserves tokens");
}

bool duplicate_stale_and_out_of_order_fail() {
    FramePeCompletionContract contract;
    contract.reset();
    const FramePeCompletionToken first = contract.issue();
    const FramePeCompletionToken second = contract.issue();
    if (!expect_logic_error(
            [&] { contract.resolve_to_fence(second, 2u); },
            "out-of-order resolve hard-fails")) {
        return false;
    }
    contract.resolve_to_fence(first, 1u);
    if (!expect_logic_error(
            [&] { contract.resolve_to_fence(second, 1u); },
            "duplicate GPU fence binding hard-fails")) {
        return false;
    }
    if (!expect_logic_error(
            [&] { contract.resolve_to_fence(first, 2u); },
            "duplicate resolve hard-fails")) {
        return false;
    }
    contract.resolve_to_fence(second, 2u);
    if (!expect_logic_error(
            [&] { (void)contract.probe_wait(second); },
            "out-of-order wait hard-fails") ||
        !expect_logic_error(
            [&] {
                (void)contract.probe_wait(FramePeCompletionToken{});
            },
            "invalid stale token hard-fails")) {
        return false;
    }

    (void)contract.observe_completed_fence(2u);
    contract.consume_wait(first);
    if (!expect_logic_error(
            [&] { contract.consume_wait(first); },
            "duplicate wait consumption hard-fails") ||
        !expect_logic_error(
            [&] { (void)contract.observe_completed_fence(1u); },
            "regressing fake fence hard-fails")) {
        return false;
    }
    contract.consume_wait(second);
    return expect(conservation_holds(contract.stats()),
                  "failure probes do not corrupt conservation");
}

bool queue_depths_one_through_three_are_independent() {
    for (std::uint64_t depth = 1u; depth <= 3u; ++depth) {
        FramePeCompletionContract contract;
        contract.reset();
        FramePeCompletionToken tokens[3]{};
        for (std::uint64_t i = 0; i < depth; ++i) {
            tokens[i] = contract.issue();
            contract.resolve_to_fence(tokens[i], 100u + i);
        }
        (void)contract.observe_completed_fence(99u + depth);
        for (std::uint64_t i = 0; i < depth; ++i) {
            if (!expect(
                    contract.probe_wait(tokens[i]) ==
                        FramePeWaitState::Complete,
                    "configured queue-depth token completes in order")) {
                return false;
            }
            contract.consume_wait(tokens[i]);
        }
        if (!expect(contract.stats().completed == depth &&
                        contract.stats().consumed == depth,
                    "q1/q2/q3 retain exact completion counts") ||
            !expect(conservation_holds(contract.stats()),
                    "q1/q2/q3 conserve tokens")) {
            return false;
        }
    }
    return true;
}

bool reset_changes_identity_and_rejects_stale_epoch() {
    FramePeCompletionContract contract;
    contract.reset();
    const FramePeCompletionToken old_token = contract.issue();
    contract.resolve_on_submission(old_token);
    contract.consume_wait(old_token);
    contract.audit_drained();

    contract.reset();
    const FramePeCompletionToken new_token = contract.issue();
    if (!expect(old_token.epoch != new_token.epoch,
                "reset advances the token epoch") ||
        !expect(old_token.value == new_token.value,
                "epoch protects reused per-session sequence values") ||
        !expect_logic_error(
            [&] { (void)contract.probe_wait(old_token); },
            "pre-reset token cannot alias a post-reset wait") ||
        !expect_logic_error(
            [&] { contract.resolve_on_submission(old_token); },
            "pre-reset token cannot resolve a post-reset frame")) {
        return false;
    }
    contract.resolve_on_submission(new_token);
    contract.consume_wait(new_token);
    contract.audit_drained();
    return true;
}

bool reset_and_drain_audit_reject_pending_or_unconsumed() {
    FramePeCompletionContract contract;
    contract.reset();
    const FramePeCompletionToken token = contract.issue();
    if (!expect_logic_error(
            [&] { contract.audit_drained(); },
            "drain audit rejects an unresolved token") ||
        !expect_logic_error(
            [&] { contract.reset(); },
            "reset cannot erase an unresolved token")) {
        return false;
    }
    contract.resolve_on_submission(token);
    if (!expect_logic_error(
            [&] { contract.audit_drained(); },
            "drain audit rejects a completed but unconsumed token")) {
        return false;
    }
    contract.consume_wait(token);
    contract.audit_drained();
    return true;
}

}  // namespace

int main() {
    bool ok = true;
    ok = ordered_fake_fence_completion() && ok;
    ok = deterministic_timeout_does_not_forge_completion() && ok;
    ok = submission_completion_stays_behind_older_fence() && ok;
    ok = preclassified_event_free_receipts_do_not_serialize() && ok;
    ok = event_free_receipt_cannot_pass_older_pe_fence() && ok;
    ok = duplicate_stale_and_out_of_order_fail() && ok;
    ok = queue_depths_one_through_three_are_independent() && ok;
    ok = reset_changes_identity_and_rejects_stale_epoch() && ok;
    ok = reset_and_drain_audit_reject_pending_or_unconsumed() && ok;
    if (!ok) {
        return 1;
    }
    std::cout << "frame PE completion contract tests passed\n";
    return 0;
}
