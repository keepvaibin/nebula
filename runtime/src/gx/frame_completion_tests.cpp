#include "galaxy/gx/frame_completion.h"
#include "galaxy/gx/fifo_submission.h"
#include "galaxy/vi_boundary.h"
#include "galaxy/gx/gx_backend.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using galaxy::gx::FramePeCompletionContract;
using galaxy::gx::FramePeCompletionStats;
using galaxy::gx::FramePeCompletionToken;
using galaxy::gx::FramePeEventClassifier;
using galaxy::gx::FramePeEventSignature;
using galaxy::gx::FramePeWaitState;
using galaxy::gx::GxBackend;
using galaxy::gx::frame_pe_cooperative_wait_step;

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

    galaxy::GuestMemoryV1 memory{};
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
    if (!expect(
            events == std::vector<FramePeEventSignature>{
                FramePeEventSignature{
                    FramePeEventSignature::Kind::Token,
                    0xCAFEu,
                    true}},
            "cached CALL_DL draw fast path preserves following PE identity")) {
        return false;
    }

    guest[kDisplayListOffset + 6u] = std::byte{0xBE};
    guest[kDisplayListOffset + 7u] = std::byte{0xEF};
    const std::size_t invalidated =
        classifier.invalidate_display_list_cache_range(
            0x80000106u,
            2u);
    classifier.classify(
        call_dl.data(), call_dl.size(), &memory, events);
    return expect(invalidated == 1u,
                  "dirty CALL_DL bytes invalidate one classifier cache entry") &&
        expect(
            events == std::vector<FramePeEventSignature>{
                FramePeEventSignature{
                    FramePeEventSignature::Kind::Token,
                    0xBEEFu,
                    true}},
            "classifier observes changed CALL_DL event identity after invalidation");
}

struct BackendPeProbe {
    std::atomic_uint64_t finishes{0u};
    std::atomic_uint64_t tokens{0u};
};

void backend_pe_finish(void* user) {
    static_cast<BackendPeProbe*>(user)->finishes.fetch_add(
        1u, std::memory_order_relaxed);
}

void backend_pe_token(void* user, std::uint16_t, bool) {
    static_cast<BackendPeProbe*>(user)->tokens.fetch_add(
        1u, std::memory_order_relaxed);
}

bool backend_reinitialize_resets_all_fifo_parser_mirrors() {
    // Keep this lifecycle regression noninteractive. The timing configuration
    // is startup-only and has not been resolved by the pure tests above.
    if (_putenv_s("GALAXY_HIDE_WINDOW", "1") != 0 ||
        _putenv_s("GALAXY_ASYNC_RENDER_THREAD", "1") != 0 ||
        _putenv_s("GALAXY_PE_EVENTS_GPU_FENCE", "1") != 0 ||
        _putenv_s("GALAXY_RENDER_LIVE_MEMORY_WAIT", "1") != 0) {
        return expect(false, "GX lifecycle test environment is configurable");
    }

    BackendPeProbe probe;
    galaxy::NativeServicesV1 services{};
    services.user = &probe;
    services.gx_pe_finish = &backend_pe_finish;
    services.gx_pe_token = &backend_pe_token;

    // GxBackend deliberately owns large fixed-capacity caches; keep it off the
    // test process's small default stack, matching the product singleton.
    auto backend = std::make_unique<GxBackend>();
    bool initialized = false;
    const auto shutdown = [&]() {
        if (!initialized) {
            return true;
        }
        try {
            backend->shutdown();
            initialized = false;
            return true;
        } catch (const std::exception& error) {
            initialized = false;
            std::cerr << "FAIL: GX lifecycle shutdown: " << error.what()
                      << '\n';
            return false;
        }
    };

    try {
        initialized = backend->initialize(320, 240);
        if (!expect(initialized, "first hidden GX backend initializes")) {
            return false;
        }

        // Session one changes VCD_LO so format zero has a two-byte vertex,
        // then ends halfway through an interrupting PE_TOKEN BP write. Both
        // the main parser and preclassifier must retain that tail only until
        // this session is shut down.
        const std::array<std::byte, 9> state_and_partial_token{
            std::byte{0x08}, std::byte{galaxy::gx::cp::kVcdLo},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x02},
            std::byte{0x00}, std::byte{0x61}, std::byte{0x48},
            std::byte{0x00}};
        const FramePeCompletionToken first = backend->render_frame(
            state_and_partial_token.data(),
            state_and_partial_token.size(),
            nullptr,
            &services,
            false,
            0u);
        if (!expect(static_cast<bool>(first),
                    "partial FIFO capture receives a conserved receipt")) {
            (void)shutdown();
            return false;
        }
        backend->wait_for_frame_pe_completion(first);
        backend->wait_for_render_idle();
        if (!expect(
                probe.finishes.load(std::memory_order_relaxed) == 0u &&
                    probe.tokens.load(std::memory_order_relaxed) == 0u,
                "partial PE command emits no callback in session one")) {
            (void)shutdown();
            return false;
        }
        if (!shutdown()) {
            return false;
        }

        initialized = backend->initialize(320, 240);
        if (!expect(initialized, "reinitialized hidden GX backend starts")) {
            return false;
        }

        // With a fresh parser, the two leading NOPs remain NOPs, the default
        // zero-stride draw has no payload, and the following BP write is one
        // FINISH. If either the old three-byte tail or VCD_LO survives, the
        // renderer instead sees TOKEN, no event, or different boundaries and
        // the classifier/render identity check hard-fails.
        const std::array<std::byte, 11> fresh_session_fifo{
            std::byte{0x00}, std::byte{0x00},
            std::byte{0x90}, std::byte{0x00}, std::byte{0x03},
            std::byte{0x61}, std::byte{0x45}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x02}, std::byte{0x00}};
        const FramePeCompletionToken second = backend->render_frame(
            fresh_session_fifo.data(),
            fresh_session_fifo.size(),
            nullptr,
            &services,
            false,
            0u);
        if (!expect(static_cast<bool>(second),
                    "fresh-session FIFO receives a conserved receipt")) {
            (void)shutdown();
            return false;
        }
        backend->wait_for_frame_pe_completion(second);
        backend->wait_for_render_idle();
        const bool callback_identity_exact = expect(
            probe.finishes.load(std::memory_order_relaxed) == 1u &&
                probe.tokens.load(std::memory_order_relaxed) == 0u,
            "reinitialize clears stale state and partial FIFO identity");
        const bool shutdown_ok = shutdown();
        return callback_identity_exact && shutdown_ok;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: GX reinitialize parser regression: "
                  << error.what() << '\n';
        (void)shutdown();
        return false;
    }
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

bool cooperative_wait_policy_is_bounded_and_exact() {
    const auto first = frame_pe_cooperative_wait_step(0u);
    const auto last = frame_pe_cooperative_wait_step(1'999u);
    const auto expired = frame_pe_cooperative_wait_step(2'000u);
    const auto clipped = frame_pe_cooperative_wait_step(8u, 10u, 4u);
    return expect(first.wait_ms == 1u && !first.timed_out,
                  "cooperative wait starts with one bounded slice") &&
        expect(last.wait_ms == 1u && !last.timed_out,
               "last in-budget millisecond remains serviceable") &&
        expect(expired.wait_ms == 0u && expired.timed_out,
               "cooperative wait hard-fails at its exact budget") &&
        expect(clipped.wait_ms == 2u && !clipped.timed_out,
               "final custom slice is clipped to remaining budget") &&
        expect_logic_error(
            [] { (void)frame_pe_cooperative_wait_step(0u, 0u, 1u); },
            "zero cooperative timeout hard-fails") &&
        expect_logic_error(
            [] { (void)frame_pe_cooperative_wait_step(0u, 1u, 0u); },
            "zero cooperative slice hard-fails");
}

bool off_vi_capture_survives_guest_transfers() {
    using galaxy::gx::FifoSubmissionPhase;
    galaxy::gx::PendingFifoSubmissions pending;
    FramePeCompletionContract backend;
    backend.reset();
    struct GuestTransfer {};
    const std::vector<std::byte> original{
        std::byte{0x61}, std::byte{0x45}, std::byte{0}, std::byte{0}, std::byte{2}};
    const std::vector<std::byte> successor{std::byte{0x10}, std::byte{0x20}};
    std::vector<std::byte> producer = original;
    std::vector<std::byte> backend_copy;
    std::uint64_t serial = 0;
    unsigned submissions = 0;
    try {
        serial = pending.capture(std::move(producer),
            std::chrono::steady_clock::time_point{std::chrono::milliseconds{10}});
        producer = successor;
        throw GuestTransfer{};
    } catch (const GuestTransfer&) {
    }
    if (!expect(pending.front() != nullptr &&
                    pending.front()->phase == FifoSubmissionPhase::Captured &&
                    pending.front()->fifo == original && producer == successor,
                "transfer after capture retains old FIFO and new producer bytes")) {
        return false;
    }
    try {
        const auto* entry = pending.next_to_submit();
        backend_copy = entry->fifo;
        const auto token = backend.issue();
        ++submissions;
        if (!expect(pending.mark_submitted(entry->serial, token),
                    "record backend token before guest transfer")) {
            return false;
        }
        throw GuestTransfer{};
    } catch (const GuestTransfer&) {
    }
    const auto token = pending.front()->token;
    if (!expect(pending.next_to_submit() == nullptr && submissions == 1u &&
                    pending.front()->fifo.empty() && backend_copy == original,
                "transfer after submit resumes token instead of replaying FIFO")) {
        return false;
    }
    backend.resolve_to_fence(token, 10u);
    (void)backend.observe_completed_fence(9u);
    if (!expect(backend.probe_wait(token) == FramePeWaitState::Pending &&
                    pending.front()->phase == FifoSubmissionPhase::Submitted,
                "owned submission does not forge fence readiness")) {
        return false;
    }
    (void)backend.observe_completed_fence(10u);
    try {
        backend.consume_wait(token);
        if (!expect(pending.mark_consumed(serial, token),
                    "persist exact token consumption before later service")) {
            return false;
        }
        throw GuestTransfer{};
    } catch (const GuestTransfer&) {
    }
    if (!expect(pending.front()->phase == FifoSubmissionPhase::Consumed &&
                    !pending.mark_consumed(serial, token),
                "transfer after consume does not consume backend token twice") ||
        !expect(pending.retire_consumed(serial) && pending.empty() &&
                    !pending.retire_consumed(serial),
                "only consumed capture can retire exactly once") ||
        !expect(producer == successor && backend_copy == original &&
                    submissions == 1u && backend.stats().consumed == 1u,
                "capture retirement cannot erase later producer bytes")) {
        return false;
    }
    backend.audit_drained();
    pending.audit_drained();
    return true;
}

bool off_vi_captures_preserve_interleaved_vi_token_order() {
    galaxy::gx::PendingFifoSubmissions pending;
    FramePeCompletionContract backend;
    backend.reset();
    std::vector<std::byte> producer{std::byte{0xA1}};
    const auto first_serial = pending.capture(std::move(producer),
        std::chrono::steady_clock::time_point{std::chrono::milliseconds{10}});
    const auto first = backend.issue();
    if (!expect(pending.mark_submitted(first_serial, first),
                "first off-VI capture submitted")) {
        return false;
    }
    producer = {std::byte{0xB1}, std::byte{0xB2}};
    std::vector<std::byte> vi_capture;
    vi_capture.swap(producer);
    const auto vi = backend.issue();
    producer = {std::byte{0xC1}};
    const auto second_serial = pending.capture(std::move(producer),
        std::chrono::steady_clock::time_point{std::chrono::milliseconds{20}});
    const auto second = backend.issue();
    if (!expect(pending.mark_submitted(second_serial, second) &&
                    second.value == first.value + 2u && pending.size() == 2u,
                "off-VI token identities permit an interleaved VI capture")) {
        return false;
    }
    backend.resolve_to_fence(first, 10u);
    backend.resolve_to_fence(vi, 20u);
    backend.resolve_to_fence(second, 30u);
    (void)backend.observe_completed_fence(30u);
    if (!expect_logic_error([&] { backend.consume_wait(vi); },
                            "VI cannot pass the earlier off-VI token") ||
        !expect(!pending.mark_consumed(second_serial, second),
                "later off-VI capture cannot pass its front owner")) {
        return false;
    }
    backend.consume_wait(first);
    if (!expect(pending.mark_consumed(first_serial, first) &&
                    pending.retire_consumed(first_serial),
                "oldest off-VI owner drains before VI") ||
        !expect_logic_error([&] { backend.consume_wait(second); },
                            "later off-VI token cannot pass interleaved VI")) {
        return false;
    }
    backend.consume_wait(vi);
    backend.consume_wait(second);
    if (!expect(pending.mark_consumed(second_serial, second) &&
                    pending.retire_consumed(second_serial) && pending.empty(),
                "multiple off-VI captures drain in global backend token order") ||
        !expect(vi_capture == std::vector<std::byte>{
                    std::byte{0xB1}, std::byte{0xB2}},
                "off-VI retirement leaves separate VI capture intact")) {
        return false;
    }
    backend.audit_drained();
    return true;
}

bool off_vi_invalid_transitions_preserve_owned_work() {
    galaxy::gx::PendingFifoSubmissions pending;
    std::vector<std::byte> empty;
    if (!expect_logic_error([&] {
            (void)pending.capture(std::move(empty),
                std::chrono::steady_clock::time_point{});
        },
                            "empty capture cannot acquire phantom token owner")) {
        return false;
    }
    const std::vector<std::byte> bytes{std::byte{0xA5}};
    std::vector<std::byte> first_bytes = bytes;
    std::vector<std::byte> second_bytes{std::byte{0x5A}};
    const auto first_serial = pending.capture(std::move(first_bytes),
        std::chrono::steady_clock::time_point{std::chrono::milliseconds{10}});
    const auto second_serial = pending.capture(std::move(second_bytes),
        std::chrono::steady_clock::time_point{std::chrono::milliseconds{20}});
    if (!expect_logic_error([&] { pending.audit_drained(); },
                            "shutdown audit cannot discard captured FIFO")) {
        return false;
    }
    const FramePeCompletionToken first{
        7u, 10u, galaxy::cadence::FrameTokenKind::EventBearing};
    const FramePeCompletionToken second{
        7u, 12u, galaxy::cadence::FrameTokenKind::EventFree};
    if (!expect(!pending.mark_submitted(second_serial, second) &&
                    !pending.mark_submitted(first_serial, {}) &&
                    !pending.mark_consumed(first_serial, first) &&
                    !pending.retire_consumed(first_serial) &&
                    pending.front()->fifo == bytes,
                "invalid capture transitions leave immutable FIFO available") ||
        !expect(pending.mark_submitted(first_serial, first) &&
                    !pending.mark_submitted(first_serial, first) &&
                    !pending.mark_submitted(second_serial, first) &&
                    !pending.mark_submitted(second_serial,
                        {8u, 12u, galaxy::cadence::FrameTokenKind::EventFree}) &&
                    !pending.mark_submitted(second_serial,
                        {7u, 9u, galaxy::cadence::FrameTokenKind::EventFree}),
                "duplicate reversed or changed-epoch token cannot submit") ||
        !expect(pending.mark_submitted(second_serial, second) &&
                    !pending.mark_consumed(first_serial, second) &&
                    !pending.mark_consumed(first_serial,
                        {7u, 10u, galaxy::cadence::FrameTokenKind::EventFree}) &&
                    !pending.retire_consumed(second_serial),
                "consumption requires exact serial token and kind identity") ||
        !expect(pending.mark_consumed(first_serial, first) &&
                    !pending.mark_consumed(first_serial, first) &&
                    !pending.mark_consumed(second_serial, second) &&
                    pending.retire_consumed(first_serial) &&
                    pending.mark_consumed(second_serial, second) &&
                    pending.retire_consumed(second_serial) && pending.empty(),
                "front retirement preserves ordered multiple-capture ownership")) {
        return false;
    }
    pending.audit_drained();
    return true;
}

bool off_vi_timeout_origin_belongs_to_each_capture() {
    galaxy::gx::PendingFifoSubmissions pending;
    FramePeCompletionContract backend;
    backend.reset();
    using Clock = std::chrono::steady_clock;
    const Clock::time_point first_start{std::chrono::milliseconds{100}};
    const Clock::time_point second_start{std::chrono::milliseconds{1600}};
    const Clock::time_point now{std::chrono::milliseconds{2200}};
    std::vector<std::byte> first_bytes{std::byte{1}};
    std::vector<std::byte> second_bytes{std::byte{2}};
    const auto first_serial = pending.capture(std::move(first_bytes), first_start);
    const auto first = backend.issue();
    if (!expect(pending.mark_submitted(first_serial, first),
                "old capture keeps its independent timeout origin")) {
        return false;
    }
    const auto second_serial = pending.capture(std::move(second_bytes), second_start);
    const auto second = backend.issue();
    if (!expect(pending.mark_submitted(second_serial, second),
                "new capture records its later timeout origin")) {
        return false;
    }
    backend.resolve_to_fence(first, 10u);
    backend.resolve_to_fence(second, 20u);
    (void)backend.observe_completed_fence(10u);
    backend.consume_wait(first);
    if (!expect(pending.mark_consumed(first_serial, first) &&
                    pending.retire_consumed(first_serial),
                "completed old capture retires without resetting younger age") ||
        !expect(pending.front()->serial == second_serial &&
                    pending.front()->started == second_start &&
                    now - first_start >= std::chrono::milliseconds{2000} &&
                    now - pending.front()->started < std::chrono::milliseconds{2000} &&
                    backend.probe_wait(second) == FramePeWaitState::Pending,
                "younger pending token must not inherit expired queue age")) {
        return false;
    }
    (void)backend.observe_completed_fence(20u);
    backend.consume_wait(second);
    if (!expect(pending.mark_consumed(second_serial, second) &&
                    pending.retire_consumed(second_serial),
                "younger capture later completes with original identity")) {
        return false;
    }
    backend.audit_drained();
    pending.audit_drained();
    return true;
}

bool draw_done_policy_accepts_owned_snapshot_without_live_aliases() {
    using galaxy::gx::draw_done_submission_policy_valid;
    if (!expect(
            draw_done_submission_policy_valid(
                true, true, true, false, false, false),
            "draw-done submission accepts the legacy live-memory barrier") ||
        !expect(
            draw_done_submission_policy_valid(
                true, true, false, true, false, false),
            "draw-done submission accepts a sealed owned-memory snapshot") ||
        !expect(
            !draw_done_submission_policy_valid(
                true, true, false, false, false, false),
            "draw-done submission rejects unowned render memory") ||
        !expect(
            !draw_done_submission_policy_valid(
                false, true, false, true, false, false),
            "draw-done submission still requires real PE fences") ||
        !expect(
            !draw_done_submission_policy_valid(
                true, false, false, true, false, false),
            "draw-done submission still requires the async renderer") ||
        !expect(
            !draw_done_submission_policy_valid(
                true, true, false, true, true, false),
            "draw-done submission rejects simulation-thread PE scanning") ||
        !expect(
            !draw_done_submission_policy_valid(
                true, true, false, true, false, true),
            "draw-done submission rejects direct WGPIPE PE events")) {
        return false;
    }
    return true;
}

bool detached_vi_receipts_share_global_fifo_order() {
    galaxy::gx::PendingFifoSubmissions pending;
    FramePeCompletionContract backend;
    backend.reset();
    const auto started = std::chrono::steady_clock::time_point{};
    std::vector<std::byte> bytes{std::byte{0x61}};
    const auto a_serial = pending.capture(std::move(bytes), started);
    const auto a = backend.issue();
    if (!expect(pending.mark_submitted(a_serial, a), "off-VI token submits first"))
        return false;
    const auto b = backend.issue();
    const auto b_serial = pending.adopt_submitted_vi(b, 17u, started, 9u);
    const auto c = backend.issue_event_free();
    const auto c_serial = pending.adopt_submitted_vi(c, 5u, started, 10u);
    galaxy::vi::PendingBoundary vi;
    galaxy::vi::BoundaryCapture capture{};
    capture.serial = 9u;
    capture.edge = {galaxy::timing::EventKind::VideoInterface, 8u, 100u, false};
    capture.interrupted_resume_pc = 0x80300010u;
    capture.interrupted_context = 0x80650878u;
    capture.fifo = {std::byte{0x61}};
    capture.render_completion_independent = true;
    if (!expect(vi.capture(std::move(capture)) &&
                    vi.mark_submitted(b, true) &&
                    vi.detach_token_to_receipt(b, b_serial) &&
                    vi.mark_vi_level_latched() && vi.await_vi_rfi() &&
                    vi.confirm_vi_rfi(9u, 24u),
                "exact IRQ24 progresses with a separately owned PE fence"))
        return false;
    galaxy::vi::BoundarySnapshot completed_vi{};
    if (!expect(vi.finish(completed_vi) && !completed_vi.token_consumed &&
                    completed_vi.token == b &&
                    completed_vi.detached_receipt_serial == b_serial,
                "IRQ24 RFI neither consumes nor abandons the GPU receipt"))
        return false;
    // An empty later VI has no new CPU-read obligation/token. The older
    // receipt still owns its exact fence after another IRQ24 RFI.
    auto empty_capture = galaxy::vi::BoundaryCapture{};
    empty_capture.serial = 10u;
    empty_capture.edge = {galaxy::timing::EventKind::VideoInterface, 9u, 200u, false};
    empty_capture.interrupted_resume_pc = 0x80300010u;
    empty_capture.interrupted_context = 0x80650878u;
    empty_capture.render_completion_independent = true;
    if (!expect(vi.capture(std::move(empty_capture)) &&
                    vi.mark_submitted({}, true) && vi.begin_pe_drain() &&
                    vi.mark_vi_level_latched() && vi.await_vi_rfi() &&
                    vi.confirm_vi_rfi(10u, 24u) && vi.finish(completed_vi) &&
                    !completed_vi.token && pending.size() == 3u,
                "empty VI neither invents a token nor consumes older GPU work"))
        return false;
    backend.resolve_to_fence(a, 11u);
    backend.resolve_to_fence(b, 12u);
    (void)backend.observe_completed_fence(11u);
    backend.consume_wait(a);
    if (!expect(pending.mark_consumed(a_serial, a) &&
                    pending.retire_consumed(a_serial), "older off-VI owner retires") ||
        !expect(pending.front()->source_vi_serial == 9u &&
                    pending.front()->fifo_size == 17u &&
                    pending.front()->fifo.empty() &&
                    backend.probe_wait(b) == FramePeWaitState::Pending,
                "IRQ24 may finish while exact detached GPU receipt remains"))
        return false;
    if (!expect_logic_error([&] { backend.consume_wait(c); },
                            "event-free later receipt cannot pass older PE fence") ||
        !expect_logic_error([&] {
                    (void)pending.adopt_submitted_vi(b, 1u, started, 11u);
                }, "duplicate token adoption fails without modifying ledger") ||
        !expect(pending.size() == 2u, "invalid adoption preserves both owners"))
        return false;
    (void)backend.observe_completed_fence(12u);
    backend.consume_wait(b);
    if (!expect(pending.mark_consumed(b_serial, b) &&
                    pending.retire_consumed(b_serial), "detached fence retires once"))
        return false;
    backend.consume_wait(c);
    if (!expect(pending.mark_consumed(c_serial, c) &&
                    pending.retire_consumed(c_serial), "later event-free receipt retires"))
        return false;
    backend.audit_drained();
    pending.audit_drained();
    return true;
}

bool independent_vi_rejects_unowned_or_synthetic_completion() {
    using galaxy::gx::independent_vi_pe_policy_valid;
    bool ok = expect(independent_vi_pe_policy_valid(true, true, true, false, false),
                     "exact live-read release and real fences admit the boundary");
    ok &= expect(!independent_vi_pe_policy_valid(false, true, true, false, false),
                 "missing GPU fences cannot admit independent delivery");
    ok &= expect(!independent_vi_pe_policy_valid(true, false, true, false, false),
                 "synchronous renderer cannot admit independent delivery");
    ok &= expect(!independent_vi_pe_policy_valid(true, true, false, false, false),
                 "unproved live-read ownership cannot admit independent delivery");
    ok &= expect(!independent_vi_pe_policy_valid(true, true, true, true, false) &&
                     !independent_vi_pe_policy_valid(true, true, true, false, true),
                 "synthetic PE publication cannot admit independent delivery");
    ok &= expect(independent_vi_pe_policy_valid(true, true, false, false, false, true),
                 "sealed detached memory admits independent delivery with actual fences");
    ok &= expect(!independent_vi_pe_policy_valid(false, true, false, false, false, true) &&
                 !independent_vi_pe_policy_valid(true, false, false, false, false, true) &&
                 !independent_vi_pe_policy_valid(true, true, false, true, false, true) &&
                 !independent_vi_pe_policy_valid(true, true, false, false, true, true),
                 "snapshot ownership cannot replace GPU fences or admit synthetic PE events");
    return ok;
}

bool completed_drawn_protocol_requires_real_finish_and_distinct_ram() {
    using galaxy::gx::independent_vi_completed_drawn_valid;
    FramePeCompletionContract backend;
    backend.reset();
    const auto prior_draw = backend.issue();
    backend.resolve_to_fence(prior_draw, 15u);
    std::array<std::byte, 32> prior_buffer{};
    std::array<std::byte, 32> next_buffer{};
    std::uint8_t sdk_finished = 0u;
    bool ok = expect(backend.probe_wait(prior_draw) == FramePeWaitState::Pending &&
                         !independent_vi_completed_drawn_valid(
                             sdk_finished, 1, 2, prior_buffer.data(),
                             next_buffer.data()),
                     "uncompleted prior drawing cannot become an admitted drawn slot");
    (void)backend.observe_completed_fence(15u);
    if (!expect(backend.probe_wait(prior_draw) == FramePeWaitState::Complete,
                "only the actual fence completes prior drawing")) return false;
    backend.consume_wait(prior_draw);
    // Model the unchanged translated finish handler's exact byte publication.
    sdk_finished = 1u;
    ok &= expect(independent_vi_completed_drawn_valid(
                     sdk_finished, 1, 2, prior_buffer.data(), next_buffer.data()),
                 "completed drawn may be selected while a distinct drawing is active");
    ok &= expect(!independent_vi_completed_drawn_valid(
                     sdk_finished, 1, 1, prior_buffer.data(), next_buffer.data()) &&
                     !independent_vi_completed_drawn_valid(
                         sdk_finished, 1, 2, prior_buffer.data(), prior_buffer.data()),
                 "same slot and aliased RAM retain original completion transaction");
    ok &= expect(!independent_vi_completed_drawn_valid(
                     sdk_finished, -1, 2, prior_buffer.data(), next_buffer.data()) &&
                     !independent_vi_completed_drawn_valid(
                         sdk_finished, 1, 3, prior_buffer.data(), next_buffer.data()) &&
                     !independent_vi_completed_drawn_valid(
                         sdk_finished, 1, 2, nullptr, next_buffer.data()) &&
                     !independent_vi_completed_drawn_valid(
                         2u, 1, 2, prior_buffer.data(), next_buffer.data()),
                 "unknown slot, unmapped buffer or nonretail finish state is rejected");
    sdk_finished = 0u; // A subsequent GXSetDrawDone/GXDrawDone clears its own byte.
    ok &= expect(!independent_vi_completed_drawn_valid(
                     sdk_finished, 1, 2, prior_buffer.data(), next_buffer.data()),
                 "next pending finish revokes the completed-drawn admission");
    backend.audit_drained();
    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok = ordered_fake_fence_completion() && ok;
    ok = detached_vi_receipts_share_global_fifo_order() && ok;
    ok = independent_vi_rejects_unowned_or_synthetic_completion() && ok;
    ok = completed_drawn_protocol_requires_real_finish_and_distinct_ram() && ok;
    ok = deterministic_timeout_does_not_forge_completion() && ok;
    ok = submission_completion_stays_behind_older_fence() && ok;
    ok = preclassified_event_free_receipts_do_not_serialize() && ok;
    ok = event_free_receipt_cannot_pass_older_pe_fence() && ok;
    ok = fifo_preclassifier_is_boundary_and_identity_exact() && ok;
    ok = fifo_preclassifier_follows_display_lists() && ok;
    ok = backend_reinitialize_resets_all_fifo_parser_mirrors() && ok;
    ok = duplicate_stale_and_out_of_order_fail() && ok;
    ok = queue_depths_one_through_three_are_independent() && ok;
    ok = reset_changes_identity_and_rejects_stale_epoch() && ok;
    ok = reset_and_drain_audit_reject_pending_or_unconsumed() && ok;
    ok = cooperative_wait_policy_is_bounded_and_exact() && ok;
    ok = off_vi_capture_survives_guest_transfers() && ok;
    ok = off_vi_captures_preserve_interleaved_vi_token_order() && ok;
    ok = off_vi_invalid_transitions_preserve_owned_work() && ok;
    ok = off_vi_timeout_origin_belongs_to_each_capture() && ok;
    ok = draw_done_policy_accepts_owned_snapshot_without_live_aliases() && ok;
    if (!ok) {
        return 1;
    }
    std::cout << "frame PE completion contract tests passed\n";
    return 0;
}
