// Tests of FramePeCompletionContract and the FIFO PE-event preclassifier (FramePeEventClassifier).
#include "galaxy/gx/frame_completion.h"
#include "galaxy/gx/gx_backend.h"

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
using galaxy::gx::FramePeEventClassifier;
using galaxy::gx::FramePeEventSignature;
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

bool fifo_preclassifier_is_boundary_and_identity_exact() {
    FramePeEventClassifier classifier;
    classifier.reset();
    std::vector<FramePeEventSignature> events;

    const std::array<std::byte, 5> event_free_bp{
        std::byte{0x61}, std::byte{0x40}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x17}};
    classifier.classify(
        event_free_bp.data(), event_free_bp.size(), nullptr, events);
    if (!expect(events.empty(),
                "ordinary BP write is classified event-free")) {
        return false;
    }

    const std::array<std::byte, 15> draw_with_pe_shaped_payload{
        std::byte{0x08}, std::byte{galaxy::gx::cp::kVcdLo},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00},
        std::byte{0x90}, std::byte{0x00}, std::byte{0x03},
        std::byte{0x61}, std::byte{0x45}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x02}, std::byte{0x7F}};
    classifier.classify(
        draw_with_pe_shaped_payload.data(),
        draw_with_pe_shaped_payload.size(),
        nullptr,
        events);
    if (!expect(
            events.empty(),
            "PE-shaped draw payload bytes cannot forge a completion barrier")) {
        return false;
    }

    const std::array<std::byte, 3> split_token_head{
        std::byte{0x61}, std::byte{0x48}, std::byte{0x00}};
    classifier.classify(
        split_token_head.data(), split_token_head.size(), nullptr, events);
    if (!expect(events.empty(),
                "partial PE command cannot forge a completion barrier")) {
        return false;
    }
    const std::array<std::byte, 2> split_token_tail{
        std::byte{0x12}, std::byte{0x34}};
    classifier.classify(
        split_token_tail.data(), split_token_tail.size(), nullptr, events);
    if (!expect(
            events == std::vector<FramePeEventSignature>{
                FramePeEventSignature{
                    FramePeEventSignature::Kind::Token,
                    0x1234u,
                    true}},
            "split PE token retains exact value and interrupt identity")) {
        return false;
    }

    const std::array<std::byte, 15> pe_sequence{
        std::byte{0x61}, std::byte{0x45}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x02},
        std::byte{0x61}, std::byte{0x47}, std::byte{0x00},
        std::byte{0xAB}, std::byte{0xCD},
        std::byte{0x61}, std::byte{0x48}, std::byte{0x00},
        std::byte{0x56}, std::byte{0x78}};
    classifier.classify(
        pe_sequence.data(), pe_sequence.size(), nullptr, events);
    return expect(
        events == std::vector<FramePeEventSignature>{
            FramePeEventSignature{
                FramePeEventSignature::Kind::Finish,
                0u,
                false},
            FramePeEventSignature{
                FramePeEventSignature::Kind::Token,
                0xABCDu,
                false},
            FramePeEventSignature{
                FramePeEventSignature::Kind::Token,
                0x5678u,
                true}},
        "classifier preserves ordered FINISH/TOKEN identities");
}

bool frozen_preclassifier_preserves_order_and_partial_calls() {
    std::array<std::array<std::byte, 32>, 64> bytes{};
    std::array<galaxy::GuestMemoryRegion, 64> regions{};
    std::vector<std::byte> fifo;
    std::vector<FramePeEventSignature> expected;
    for (std::uint32_t i = 0; i < regions.size(); ++i) {
        regions[i] = {0x1000u + i * 64u, 32u, bytes[i].data()};
        bytes[i][0] = std::byte{0x61}; bytes[i][1] = std::byte{0x48};
        bytes[i][4] = static_cast<std::byte>(i);
        const auto address = regions[i].guest_base;
        fifo.push_back(std::byte{0x40});
        for (unsigned shift = 0; shift < 4; ++shift)
            fifo.push_back(static_cast<std::byte>(address >> (24u - 8u * shift)));
        fifo.insert(fifo.end(), {std::byte{0},std::byte{0},std::byte{0},std::byte{32}});
        expected.push_back({FramePeEventSignature::Kind::Token, static_cast<std::uint16_t>(i), true});
    }
    galaxy::GuestMemory memory{}; memory.regions = regions.data();
    memory.region_count = static_cast<std::uint32_t>(regions.size());
    for (std::size_t split = 0; split <= 9u; ++split) {
        FramePeEventClassifier generic, frozen;
        for (unsigned pass = 0; pass < 2u; ++pass) {
            std::vector<FramePeEventSignature> a,b,all_a,all_b;
            generic.classify(fifo.data(), split, &memory, a);
            frozen.classify(fifo.data(), split, &memory, b, true);
            all_a = a; all_b = b;
            generic.classify(fifo.data()+split, fifo.size()-split, &memory, a);
            frozen.classify(fifo.data()+split, fifo.size()-split, &memory, b, true);
            all_a.insert(all_a.end(),a.begin(),a.end()); all_b.insert(all_b.end(),b.begin(),b.end());
            if (!expect(all_a == expected && all_b == expected,
                        "frozen classifier preserves cold/replay/split token order")) return false;
        }
    }
    return true;
}

bool fifo_preclassifier_follows_display_lists() {
    FramePeEventClassifier classifier;
    classifier.reset();
    std::array<std::byte, 512> guest{};
    constexpr std::size_t kDisplayListOffset = 0x100u;
    guest[kDisplayListOffset + 0u] = std::byte{0x90};
    guest[kDisplayListOffset + 1u] = std::byte{0x00};
    guest[kDisplayListOffset + 2u] = std::byte{0x00};
    guest[kDisplayListOffset + 3u] = std::byte{0x61};
    guest[kDisplayListOffset + 4u] = std::byte{0x48};
    guest[kDisplayListOffset + 5u] = std::byte{0x00};
    guest[kDisplayListOffset + 6u] = std::byte{0xCA};
    guest[kDisplayListOffset + 7u] = std::byte{0xFE};

    galaxy::GuestMemory memory{};
    memory.fast_regions[8].host_base = guest.data();
    memory.fast_regions[8].size =
        static_cast<std::uint32_t>(guest.size());
    const std::array<std::byte, 9> call_dl{
        std::byte{0x40},
        std::byte{0x80}, std::byte{0x00},
        std::byte{0x01}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x20}};
    std::vector<FramePeEventSignature> events;
    classifier.classify(
        call_dl.data(), call_dl.size(), &memory, events);
    if (!expect(
            events == std::vector<FramePeEventSignature>{
                FramePeEventSignature{
                    FramePeEventSignature::Kind::Token,
                    0xCAFEu,
                    true}},
            "classifier follows CALL_DL instead of scanning only top-level bytes")) {
        return false;
    }

    classifier.classify(
        call_dl.data(), call_dl.size(), &memory, events);
    return expect(
        events == std::vector<FramePeEventSignature>{
            FramePeEventSignature{
                FramePeEventSignature::Kind::Token,
                0xCAFEu,
                true}},
        "cached CALL_DL draw fast path preserves following PE identity");
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
    if (!frozen_preclassifier_preserves_order_and_partial_calls()) return 1;
    bool ok = true;
    ok = ordered_fake_fence_completion() && ok;
    ok = deterministic_timeout_does_not_forge_completion() && ok;
    ok = submission_completion_stays_behind_older_fence() && ok;
    ok = preclassified_event_free_receipts_do_not_serialize() && ok;
    ok = event_free_receipt_cannot_pass_older_pe_fence() && ok;
    ok = fifo_preclassifier_is_boundary_and_identity_exact() && ok;
    ok = fifo_preclassifier_follows_display_lists() && ok;
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
