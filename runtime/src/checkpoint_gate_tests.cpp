#include "galaxy/checkpoint_gate.h"
#include "galaxy/native_api.h"
#include "galaxy/scope_exit.h"

#include <atomic>
#include <array>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

using galaxy::checkpoint::GateController;
using galaxy::checkpoint::PointerView;
using galaxy::checkpoint::RequestedMode;

static_assert(std::is_nothrow_destructible_v<GateController::FullScope>);
static_assert(std::is_nothrow_move_constructible_v<GateController::FullScope>);
static_assert(std::is_nothrow_destructible_v<GateController::InlineScope>);
static_assert(std::is_nothrow_move_constructible_v<GateController::InlineScope>);
static_assert(std::is_nothrow_destructible_v<GateController>);

bool expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

struct CleanupTransfer {
    galaxy::PpcContext* context;
    std::uint32_t pc;
    std::uint64_t generation;
};

struct CleanupEvidence {
    std::array<unsigned, 6> order{};
    unsigned count{}, flags{}, completed_handlers{};
    std::uint32_t msr{0x9032u};
    bool valid{true};
};

void cleanup_boundary(bool guarded, unsigned depth, unsigned outcome,
    CleanupEvidence& evidence, GateController& gate,
    galaxy::PpcContext& context, std::atomic<std::uint32_t>& pending) {
    auto full = gate.full_scope();
    (void)full;
    const unsigned previous_flags = evidence.flags;
    const std::uint32_t previous_msr = evidence.msr;
    evidence.flags |= 1u << depth;
    evidence.msr &= ~0x8000u;
    const auto cleanup = [&]() noexcept {
        evidence.valid &= gate.full_scope_depth() == depth &&
            !gate.inline_published() && pending.load() == 0x15u &&
            evidence.count < evidence.order.size();
        if (evidence.count < evidence.order.size())
            evidence.order[evidence.count++] = depth;
        evidence.flags = previous_flags;
        evidence.msr = previous_msr;
    };
    const auto body = [&]() {
        if (depth < 6u) {
            cleanup_boundary(guarded, depth + 1u, outcome,
                evidence, gate, context, pending);
        } else {
            context.pc = 0x804A812Cu;
            context.gpr[3] = 0x80F8EF70u;
            if (outcome == 1u)
                throw CleanupTransfer{&context, context.pc, 0x100000002ull};
            if (outcome == 2u) throw std::runtime_error("required failure");
            if (outcome == 3u) throw 17;
        }
    };
    if (guarded) {
        const galaxy::ScopeExit scope(cleanup);
        body();
    } else {
        try { body(); } catch (...) { cleanup(); throw; }
        cleanup();
    }
    ++evidence.completed_handlers;
}

bool cleanup_guards_preserve_unwind_and_owned_state() {
    bool passed = true;
    for (unsigned outcome = 0u; outcome < 4u; ++outcome) {
        std::array<CleanupEvidence, 2> evidence{};
        std::array<galaxy::PpcContext, 2> contexts{};
        for (unsigned variant = 0u; variant < 2u; ++variant) {
            galaxy::NativeServicesV1 services{};
            std::uint64_t counter = 9u, threshold = 13u;
            GateController gate(RequestedMode::Inline);
            gate.bind({&services.checkpoint_counter,
                &services.checkpoint_next_slow}, &counter, &threshold);
            gate.activate();
            std::atomic<std::uint32_t> pending{0x15u};
            unsigned observed = 0u;
            try {
                cleanup_boundary(variant != 0u, 1u, outcome,
                    evidence[variant], gate, contexts[variant], pending);
            } catch (const CleanupTransfer& transfer) {
                observed = 1u;
                passed &= expect(transfer.context == &contexts[variant] &&
                    transfer.pc == 0x804A812Cu &&
                    transfer.generation == 0x100000002ull,
                    "cleanup retains exact transfer identity and resume payload");
            } catch (const std::runtime_error& error) {
                observed = 2u;
                passed &= expect(std::string_view(error.what()) == "required failure",
                    "cleanup retains required runtime failure");
            } catch (int value) { observed = 3u; passed &= expect(value == 17,
                    "cleanup retains unknown exception payload"); }
            passed &= expect(observed == outcome && evidence[variant].valid &&
                evidence[variant].count == 6u && evidence[variant].flags == 0u &&
                evidence[variant].msr == 0x9032u &&
                evidence[variant].completed_handlers == (outcome == 0u ? 6u : 0u) &&
                pending.load() == 0x15u && gate.full_scope_depth() == 0u &&
                gate.inline_published() && services.checkpoint_counter == &counter &&
                services.checkpoint_next_slow == &threshold,
                "all nested owner cleanup precedes gate restoration without acknowledging events");
            gate.unbind();
        }
        passed &= expect(evidence[0].order == evidence[1].order &&
            std::memcmp(&contexts[0], &contexts[1], sizeof(galaxy::PpcContext)) == 0,
            "RAII and catch/rethrow preserve complete context and cleanup order");
    }
    using Guard = galaxy::ScopeExit<decltype([]() noexcept {})>;
    static_assert(std::is_nothrow_destructible_v<Guard>);
    static_assert(!std::is_copy_constructible_v<Guard> &&
        !std::is_move_constructible_v<Guard>);
    return passed;
}

bool exception_cleanup_owns_only_incomplete_boundary() {
    bool passed = true;
    // Model the whole invocation transaction: translated body, context
    // commit, then optional observation. Each stage can fail after earlier
    // stages have already returned. Rollback owns all three, and no later
    // caller failure may cancel a normally committed transaction.
    for (unsigned failure_stage = 0u; failure_stage <= 3u; ++failure_stage) {
        for (unsigned outcome = 1u; outcome <= 3u; ++outcome) {
            galaxy::PpcContext context{};
            context.pc = 0x804A812Cu;
            unsigned completed_stage = 0u, aborts = 0u, caught = 0u;
            bool decoder_active = true, sample_active = true, profiler_active = true;
            std::array<unsigned, 3> abort_order{};
            const auto rollback = [&]() noexcept {
                abort_order[0] = 1u; decoder_active = false;
                abort_order[1] = 2u; sample_active = false;
                abort_order[2] = 3u; profiler_active = false;
                ++aborts;
            };
            try {
                galaxy::run_with_exception_cleanup([&]() {
                    for (unsigned stage = 1u; stage <= 3u; ++stage) {
                        context.gpr[stage] = stage * 17u;
                        completed_stage = stage;
                        if (stage == failure_stage) {
                            if (outcome == 1u)
                                throw CleanupTransfer{&context, context.pc, 0x100000002ull};
                            if (outcome == 2u) throw std::runtime_error("commit failure");
                            throw 17;
                        }
                    }
                }, rollback);
                // An unrelated failure after the invocation must not abort
                // the decoder/sample that is now ready for its normal commit.
                if (failure_stage == 0u) throw 23;
            } catch (const CleanupTransfer& transfer) {
                caught = 1u;
                passed &= expect(transfer.context == &context &&
                    transfer.pc == 0x804A812Cu && transfer.generation == 0x100000002ull,
                    "exception-only cleanup retains exact transfer and context");
            } catch (const std::runtime_error& error) {
                caught = 2u;
                passed &= expect(std::string_view(error.what()) == "commit failure",
                    "a post-body commit failure retains its original diagnostic");
            } catch (int value) {
                caught = failure_stage == 0u ? 4u : 3u;
                passed &= expect(value == (failure_stage == 0u ? 23 : 17),
                    "cleanup neither suppresses nor replaces unknown exceptions");
            }
            if (failure_stage == 0u) {
                passed &= expect(caught == 4u && completed_stage == 3u && aborts == 0u &&
                    decoder_active && sample_active && profiler_active,
                    "later caller failure cannot abort a completed invocation");
            } else {
                passed &= expect(caught == outcome && completed_stage == failure_stage &&
                    aborts == 1u && !decoder_active && !sample_active && !profiler_active &&
                    abort_order == std::array<unsigned, 3>{1u, 2u, 3u},
                    "every incomplete stage aborts all owned observations exactly once in order");
            }
            for (unsigned stage = 1u; stage <= completed_stage; ++stage) {
                passed &= expect(context.gpr[stage] == stage * 17u,
                    "observational rollback must not revert guest register effects");
            }
        }
    }
    return passed;
}

struct OptionalHandlerEvidence {
    std::array<unsigned, 32> destruction_order{}, handler_order{};
    std::array<unsigned, 4> owner_aborts{};
    unsigned destroyed{}, handled{}, completed{};
    std::uint32_t flags{};
    bool valid{true};
};

void optional_handler_boundary(bool specialized, unsigned level, unsigned depth,
    unsigned owners, unsigned outcome, OptionalHandlerEvidence& evidence,
    GateController& gate, galaxy::PpcContext& context) {
    auto full = gate.full_scope();
    (void)full;
    const auto previous_flags = evidence.flags;
    evidence.flags |= 1u << (level - 1u);
    const galaxy::ScopeExit mandatory_cleanup([&]() noexcept {
        evidence.valid &= gate.full_scope_depth() == level && !gate.inline_published();
        evidence.destruction_order[evidence.destroyed++] = level;
        evidence.flags = previous_flags;
    });
    const auto body = [&]() {
        context.gpr[level % 32u] = level * 17u;
        if (level < depth) {
            optional_handler_boundary(specialized, level + 1u, depth, owners,
                outcome, evidence, gate, context);
        } else {
            context.pc = 0x804A812Cu;
            if (outcome == 1u)
                throw CleanupTransfer{&context, context.pc, 0x100000002ull};
            if (outcome == 2u) throw std::runtime_error("dispatch failure");
            if (outcome == 3u) throw 17;
        }
    };
    const auto handler = [&]() {
        if (owners == 0u) return;
        evidence.valid &= evidence.destroyed == depth - level &&
            gate.full_scope_depth() == level;
        evidence.handler_order[evidence.handled++] = level;
        for (unsigned owner = 0; owner < 4u; ++owner)
            if ((owners & (1u << owner)) != 0u) ++evidence.owner_aborts[owner];
    };
    if (specialized) {
        galaxy::run_with_optional_exception_handler(owners != 0u, body, handler);
    } else {
        try { body(); } catch (...) { handler(); throw; }
    }
    ++evidence.completed;
}

bool empty_handler_specialization_preserves_ownership_and_unwinding() {
    bool passed = true;
    for (unsigned depth = 1u; depth <= 32u; ++depth) {
        for (unsigned owners = 0u; owners < 16u; ++owners) {
            for (unsigned outcome = 0u; outcome < 4u; ++outcome) {
                std::array<OptionalHandlerEvidence, 2> evidence{};
                std::array<galaxy::PpcContext, 2> contexts{};
                for (unsigned variant = 0u; variant < 2u; ++variant) {
                    galaxy::NativeServicesV1 services{};
                    std::uint64_t counter = 9u, threshold = 13u;
                    GateController gate(RequestedMode::Inline);
                    gate.bind({&services.checkpoint_counter, &services.checkpoint_next_slow},
                        &counter, &threshold);
                    gate.activate();
                    unsigned caught = 0u;
                    try {
                        optional_handler_boundary(variant != 0u, 1u, depth, owners,
                            outcome, evidence[variant], gate, contexts[variant]);
                    } catch (const CleanupTransfer& transfer) {
                        caught = 1u;
                        passed &= expect(transfer.context == &contexts[variant] &&
                            transfer.pc == 0x804A812Cu && transfer.generation == 0x100000002ull,
                            "optional handler retains exact transfer context and generation");
                    } catch (const std::runtime_error& error) {
                        caught = 2u;
                        passed &= expect(std::string_view(error.what()) == "dispatch failure",
                            "optional handler retains required failure message");
                    } catch (int value) {
                        caught = 3u;
                        passed &= expect(value == 17, "optional handler retains unknown payload");
                    }
                    const auto& e = evidence[variant];
                    passed &= expect(caught == outcome && e.valid && e.destroyed == depth &&
                        e.handled == (outcome != 0u && owners != 0u ? depth : 0u) &&
                        e.completed == (outcome == 0u ? depth : 0u) && e.flags == 0u &&
                        gate.full_scope_depth() == 0u && gate.inline_published() &&
                        services.checkpoint_counter == &counter &&
                        services.checkpoint_next_slow == &threshold,
                        "all ordinary destructors and gate restoration run with or without a handler");
                    for (unsigned i = 0u; i < depth; ++i) {
                        passed &= expect(e.destruction_order[i] == depth - i,
                            "mandatory cleanup retains innermost-first order");
                        if (e.handled != 0u)
                            passed &= expect(e.handler_order[i] == depth - i,
                                "owned failure handler runs before its mandatory destructor");
                    }
                    for (unsigned owner = 0u; owner < 4u; ++owner)
                        passed &= expect(e.owner_aborts[owner] ==
                            (outcome != 0u && (owners & (1u << owner)) != 0u ? depth : 0u),
                            "every independently owned postlude runs exactly once per failed layer");
                    gate.unbind();
                }
                passed &= expect(evidence[0].destruction_order == evidence[1].destruction_order &&
                    evidence[0].handler_order == evidence[1].handler_order &&
                    evidence[0].owner_aborts == evidence[1].owner_aborts &&
                    std::memcmp(&contexts[0], &contexts[1], sizeof(galaxy::PpcContext)) == 0,
                    "handler selection preserves complete context and all ownership evidence");
            }
        }
    }
    for (const bool required : {false, true}) {
        unsigned handled = 0u;
        try {
            galaxy::run_with_optional_exception_handler(required,
                []() { throw 17; }, [&]() { ++handled; throw 23; });
        } catch (int value) {
            passed &= expect(value == (required ? 23 : 17) && handled == (required ? 1u : 0u),
                "required fallible diagnostic handler retains replacement behavior");
        }
    }
    return passed;
}

PointerView service_pointers(galaxy::NativeServicesV1& services) noexcept {
    return PointerView{
        &services.checkpoint_counter,
        &services.checkpoint_next_slow};
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

bool activation_is_requested_and_deferred() {
    galaxy::NativeServicesV1 services{};
    std::uint64_t counter = 4u;
    std::uint64_t next_slow = 9u;
    GateController gate(RequestedMode::Inline);
    gate.bind(service_pointers(services), &counter, &next_slow);

    bool passed = expect(
        services.checkpoint_counter == nullptr &&
            services.checkpoint_next_slow == nullptr,
        "binding does not implicitly activate the inline gate");
    {
        auto outer = gate.full_scope();
        gate.activate();
        passed &= expect(
            gate.activated() && services.checkpoint_counter == nullptr &&
                services.checkpoint_next_slow == nullptr,
            "activation is deferred for the entire outer full scope");
        {
            auto nested = gate.full_scope();
            passed &= expect(
                gate.full_scope_depth() == 2u &&
                    services.checkpoint_counter == nullptr &&
                    services.checkpoint_next_slow == nullptr,
                "nested full scope cannot republish inline pointers");
        }
        passed &= expect(
            gate.full_scope_depth() == 1u &&
                services.checkpoint_counter == nullptr,
            "nested exit leaves the outer full scope in force");
    }

    const auto& telemetry = gate.telemetry();
    passed &= expect(
        services.checkpoint_counter == &counter &&
            services.checkpoint_next_slow == &next_slow,
        "outer exit publishes both inline pointers together");
    passed &= expect(
        telemetry.activations == 1u && telemetry.full_scope_entries == 2u &&
            telemetry.maximum_full_scope_depth == 2u &&
            telemetry.transitions_to_full == 0u &&
            telemetry.transitions_to_inline == 1u,
        "deferred activation telemetry is exact");
    gate.activate();
    passed &= expect(
        gate.telemetry().activations == 1u,
        "repeated activation is idempotent");
    gate.unbind();
    return passed;
}

struct GuestControlSentinel final {};

bool unwinding_restores_exactly_once() {
    galaxy::NativeServicesV1 services{};
    std::uint64_t counter{};
    std::uint64_t next_slow = 1u;
    GateController gate(RequestedMode::Inline);
    gate.bind(service_pointers(services), &counter, &next_slow);
    gate.activate();

    bool saw_sentinel = false;
    try {
        auto outer = gate.full_scope();
        auto nested = gate.full_scope();
        if (services.checkpoint_counter != nullptr ||
            services.checkpoint_next_slow != nullptr) {
            return expect(false, "nested sentinel tree must remain fully gated");
        }
        throw GuestControlSentinel{};
    } catch (const GuestControlSentinel&) {
        saw_sentinel = true;
    }

    bool passed = expect(saw_sentinel, "guest control sentinel propagated") &&
        expect(
            services.checkpoint_counter == &counter &&
                services.checkpoint_next_slow == &next_slow,
            "sentinel unwinding restores inline pointers");
    passed &= expect(
        gate.telemetry().transitions_to_full == 1u &&
            gate.telemetry().transitions_to_inline == 2u,
        "two sentinel scope destructors restore inline mode exactly once");

    try {
        auto scope = gate.full_scope();
        throw std::runtime_error("ordinary callback failure");
    } catch (const std::runtime_error&) {
    }
    passed &= expect(
        services.checkpoint_counter == &counter &&
            gate.telemetry().transitions_to_full == 2u &&
            gate.telemetry().transitions_to_inline == 3u,
        "ordinary exception unwinding restores exactly once");

    {
        auto original = gate.full_scope();
        auto moved = std::move(original);
        passed &= expect(
            gate.full_scope_depth() == 1u,
            "moving a full scope transfers one reference");
    }
    passed &= expect(
        gate.full_scope_depth() == 0u &&
            gate.telemetry().transitions_to_full == 3u &&
            gate.telemetry().transitions_to_inline == 4u,
        "moved-from scope does not restore a second time");
    passed &= expect(
        gate.telemetry().full_scope_entries == 4u &&
            gate.telemetry().maximum_full_scope_depth == 2u,
        "scope entry and maximum-depth telemetry remain exact");
    gate.unbind();
    return passed;
}

bool translated_inline_scope_is_lexical_and_nested() {
    galaxy::NativeServicesV1 services{};
    std::uint64_t counter{};
    std::uint64_t next_slow = 1u;
    GateController gate(RequestedMode::Inline);
    gate.bind(service_pointers(services), &counter, &next_slow);
    gate.activate();

    bool passed = true;
    {
        auto outer_full = gate.full_scope();
        passed &= expect(
            gate.full_scope_depth() == 1u &&
                services.checkpoint_counter == nullptr,
            "outer host callback begins in full mode");
        {
            auto translated = gate.inline_scope();
            passed &= expect(
                translated.active() && gate.full_scope_depth() == 0u &&
                    gate.inline_scope_depth() == 1u &&
                    services.checkpoint_counter == &counter &&
                    services.checkpoint_next_slow == &next_slow,
                "translated execution suspends and preserves the enclosing full scope");
            {
                auto nested_full = gate.full_scope();
                passed &= expect(
                    gate.full_scope_depth() == 1u &&
                        services.checkpoint_counter == nullptr,
                    "nested host callback conservatively restores full mode");
            }
            passed &= expect(
                gate.full_scope_depth() == 0u &&
                    services.checkpoint_counter == &counter,
                "nested host callback returns to translated inline mode");
            {
                auto nested_translated = gate.inline_scope();
                passed &= expect(
                    nested_translated.active() &&
                        gate.inline_scope_depth() == 2u &&
                        services.checkpoint_counter == &counter,
                    "nested translated scope is lexical without redundant transitions");
            }
            passed &= expect(
                gate.inline_scope_depth() == 1u &&
                    services.checkpoint_counter == &counter,
                "nested translated scope restores its parent exactly");
        }
        passed &= expect(
            gate.full_scope_depth() == 1u &&
                gate.inline_scope_depth() == 0u &&
                services.checkpoint_counter == nullptr,
            "translated scope restores the suspended outer full mode");
    }
    passed &= expect(
        gate.full_scope_depth() == 0u &&
            services.checkpoint_counter == &counter &&
            services.checkpoint_next_slow == &next_slow,
        "outer callback exit restores persistent inline mode");

    const auto& telemetry = gate.telemetry();
    passed &= expect(
        telemetry.full_scope_entries == 2u &&
            telemetry.maximum_full_scope_depth == 1u &&
            telemetry.inline_scope_entries == 2u &&
            telemetry.maximum_inline_scope_depth == 2u &&
            telemetry.transitions_to_full == 3u &&
            telemetry.transitions_to_inline == 4u,
        "lexical translated-scope telemetry is exact");

    try {
        auto outer_full = gate.full_scope();
        auto translated = gate.inline_scope();
        throw GuestControlSentinel{};
    } catch (const GuestControlSentinel&) {
    }
    passed &= expect(
        gate.full_scope_depth() == 0u &&
            gate.inline_scope_depth() == 0u &&
            services.checkpoint_counter == &counter,
        "exception unwinding restores translated and host scopes in LIFO order");
    gate.unbind();
    return passed;
}

bool translated_inline_scope_preserves_recursive_full_depth() {
    galaxy::NativeServicesV1 services{};
    std::uint64_t counter{};
    std::uint64_t next_slow = 1u;
    GateController gate(RequestedMode::Inline);
    gate.bind(service_pointers(services), &counter, &next_slow);
    gate.activate();

    bool passed = true;
    {
        auto callback_f0 = gate.full_scope();
        auto translated_i0 = gate.inline_scope();
        {
            auto callback_f1 = gate.full_scope();
            {
                auto callback_f2 = gate.full_scope();
                passed &= expect(
                    gate.full_scope_depth() == 2u &&
                        gate.inline_scope_depth() == 1u &&
                        services.checkpoint_counter == nullptr,
                    "recursive callbacks accumulate full depth inside translated execution");
                {
                    auto translated_i1 = gate.inline_scope();
                    auto moved_i1 = std::move(translated_i1);
                    passed &= expect(
                        !translated_i1.active() && moved_i1.active() &&
                            gate.full_scope_depth() == 0u &&
                            gate.inline_scope_depth() == 2u &&
                            services.checkpoint_counter == &counter,
                        "recursive translated scope suspends two full scopes and transfers ownership on move");
                    {
                        auto callback_f3 = gate.full_scope();
                        passed &= expect(
                            gate.full_scope_depth() == 1u &&
                                services.checkpoint_counter == nullptr,
                            "callback nested in recursive translated scope is full-gated");
                    }
                    passed &= expect(
                        gate.full_scope_depth() == 0u &&
                            services.checkpoint_counter == &counter,
                        "nested callback returns to recursive translated inline mode");
                }
                passed &= expect(
                    gate.full_scope_depth() == 2u &&
                        gate.inline_scope_depth() == 1u &&
                        services.checkpoint_counter == nullptr,
                    "recursive translated unwind restores both suspended callback depths");
            }
            passed &= expect(
                gate.full_scope_depth() == 1u &&
                    services.checkpoint_counter == nullptr,
                "inner recursive callback unwind preserves its parent");
        }
        passed &= expect(
            gate.full_scope_depth() == 0u &&
                services.checkpoint_counter == &counter,
            "recursive callback tree returns to its outer translated scope");
    }
    passed &= expect(
        gate.full_scope_depth() == 0u &&
            gate.inline_scope_depth() == 0u &&
            services.checkpoint_counter == &counter,
        "recursive translated tree restores persistent inline mode");
    passed &= expect(
        gate.telemetry().maximum_full_scope_depth == 2u &&
            gate.telemetry().maximum_inline_scope_depth == 2u,
        "recursive translated tree records both exact nesting maxima");
    gate.unbind();
    return passed;
}

bool inactive_inline_scope_contract() {
    galaxy::NativeServicesV1 services{};
    std::uint64_t counter{};
    std::uint64_t next_slow = 1u;
    GateController gate(RequestedMode::Inline);

    auto unbound = gate.inline_scope();
    bool passed = expect(
        !unbound.active() && gate.inline_scope_depth() == 0u,
        "unbound inline scope is an inactive no-op");
    gate.bind(service_pointers(services), &counter, &next_slow);
    auto pre_activation = gate.inline_scope();
    passed &= expect(
        !pre_activation.active() && gate.inline_scope_depth() == 0u &&
            gate.telemetry().inline_scope_entries == 0u,
        "pre-activation inline scope is an inactive no-op");
    gate.unbind();
    return passed;
}

struct CheckpointProbe {
    std::uint64_t callbacks{};
    std::uint32_t last_guest_pc{};
    std::uint32_t last_context_pc{};
};

void capture_checkpoint(
    void* user,
    std::uint32_t guest_pc,
    galaxy::PpcContext* context,
    galaxy::GuestMemoryV1*) {
    auto& probe = *static_cast<CheckpointProbe*>(user);
    ++probe.callbacks;
    probe.last_guest_pc = guest_pc;
    probe.last_context_pc = context != nullptr ? context->pc : 0u;
}

bool pending_event_bypass_stays_in_generated_code() {
    CheckpointProbe probe{};
    galaxy::NativeServicesV1 services{};
    services.user = &probe;
    services.branch_checkpoint = &capture_checkpoint;
    std::atomic_uint32_t pending_mask{0u};
    services.pending_event_mask = &pending_mask;

    std::uint64_t counter{};
    std::uint64_t next_slow = 100u;
    GateController gate(RequestedMode::Inline);
    gate.bind(service_pointers(services), &counter, &next_slow);
    gate.activate();

    galaxy::PpcContext context{};
    galaxy::GuestMemoryV1 memory{};
    galaxy::branch_checkpoint_taken(
        &services, 0x80001000u, 0x80001004u, &context, &memory);
    bool passed = expect(
        counter == 1u && probe.callbacks == 0u,
        "inline generated checkpoint skips host below its threshold");

    pending_mask.store(1u, std::memory_order_release);
    galaxy::branch_checkpoint_taken(
        &services, 0x80002000u, 0x80002004u, &context, &memory);
    passed &= expect(
        counter == 2u && probe.callbacks == 1u &&
            context.pc == 0x80002004u,
        "pending event bypasses inline threshold in generated code");

    pending_mask.store(0u, std::memory_order_release);
    {
        auto full = gate.full_scope();
        galaxy::branch_checkpoint_taken(
            &services, 0x80003000u, 0x80003004u, &context, &memory);
        passed &= expect(
            counter == 2u && probe.callbacks == 2u,
            "full scope calls host without mutating the inline counter");
    }
    gate.unbind();
    return passed;
}

bool call_return_checkpoint_services_only_published_events() {
    CheckpointProbe probe{};
    galaxy::NativeServicesV1 services{};
    services.user = &probe;
    services.branch_checkpoint = &capture_checkpoint;
    std::atomic_uint32_t pending_mask{0u};
    services.pending_event_mask = &pending_mask;

    std::uint64_t counter{};
    std::uint64_t next_slow = 100u;
    GateController gate(RequestedMode::Inline);
    gate.bind(service_pointers(services), &counter, &next_slow);
    gate.activate();

    galaxy::PpcContext context{};
    context.pc = 0xDEADBEECu;
    galaxy::GuestMemoryV1 memory{};
    galaxy::call_return_checkpoint(
        &services, 0x80004000u, 0x80004004u, &context, &memory);
    bool passed = expect(
        counter == 0u && probe.callbacks == 0u &&
            context.pc == 0xDEADBEECu,
        "call-return checkpoint has no counter/callback cost without a published event");

    pending_mask.store(1u, std::memory_order_release);
    galaxy::call_return_checkpoint(
        &services, 0x80005000u, 0x80005004u, &context, &memory);
    passed &= expect(
        counter == 1u && probe.callbacks == 1u &&
            probe.last_guest_pc == 0x80005000u &&
            probe.last_context_pc == 0x80005004u &&
            context.pc == 0x80005004u,
        "call-return checkpoint bypasses the inline threshold and publishes the exact continuation");

    gate.unbind();
    return passed;
}

bool binding_contract_fails_closed() {
    galaxy::NativeServicesV1 services{};
    std::uint64_t counter{};
    std::uint64_t next_slow = 1u;
    GateController gate(RequestedMode::Inline);
    gate.bind(service_pointers(services), &counter, &next_slow);

    bool passed = expect_exception<std::logic_error>(
        [&]() { gate.bind(service_pointers(services), &counter, &next_slow); },
        "binding an already-bound controller must fail");
    {
        auto full = gate.full_scope();
        passed &= expect_exception<std::logic_error>(
            [&]() { gate.unbind(); },
            "unbinding a live full scope must fail");
    }
    gate.activate();
    {
        auto translated = gate.inline_scope();
        passed &= expect(translated.active(), "activated inline scope is live");
        passed &= expect_exception<std::logic_error>(
            [&]() { gate.unbind(); },
            "unbinding a live inline scope must fail");
    }
    passed &= expect_exception<std::logic_error>(
        [&]() { gate.set_requested_mode(RequestedMode::Full); },
        "mode changes after activation must fail");

    services.checkpoint_counter = nullptr;
    passed &= expect_exception<std::logic_error>(
        [&]() {
            auto full = gate.full_scope();
            (void)full;
        },
        "external pointer-slot takeover must fail");
    services.checkpoint_counter = &counter;
    gate.unbind();

    galaxy::NativeServicesV1 occupied{};
    occupied.checkpoint_counter = &counter;
    GateController other(RequestedMode::Inline);
    passed &= expect_exception<std::logic_error>(
        [&]() { other.bind(service_pointers(occupied), &counter, &next_slow); },
        "binding occupied service slots must fail");
    passed &= expect_exception<std::invalid_argument>(
        [&]() {
            GateController invalid(static_cast<RequestedMode>(0xFFu));
            (void)invalid;
        },
        "invalid requested mode must fail");
    return passed;
}

bool full_requested_mode_never_publishes() {
    galaxy::NativeServicesV1 services{};
    std::uint64_t counter{};
    std::uint64_t next_slow = 1u;
    GateController gate(RequestedMode::Full);
    gate.bind(service_pointers(services), &counter, &next_slow);
    gate.activate();
    auto translated = gate.inline_scope();
    {
        auto full = gate.full_scope();
        (void)full;
    }
    const bool passed = expect(
        !translated.active() && !gate.activated() &&
            !gate.inline_published() &&
            services.checkpoint_counter == nullptr &&
            services.checkpoint_next_slow == nullptr &&
            gate.telemetry().activations == 0u &&
            gate.telemetry().inline_scope_entries == 0u &&
            gate.telemetry().transitions_to_inline == 0u &&
            gate.telemetry().transitions_to_full == 0u,
        "full requested mode never exposes inline pointers");
    gate.unbind();
    return passed;
}

}  // namespace

int main() {
    bool passed = true;
    passed &= cleanup_guards_preserve_unwind_and_owned_state();
    passed &= exception_cleanup_owns_only_incomplete_boundary();
    passed &= empty_handler_specialization_preserves_ownership_and_unwinding();
    const std::array<std::uint64_t, 4> future_deadlines{101u, 102u, 103u, 104u};
    passed &= expect(galaxy::checkpoint::quiet_architectural_checkpoint(
        100u, future_deadlines, 0u, false, 2u, 3u, true),
        "eligible architectural boundary can return when all devices are quiet");
    for (std::size_t device = 0; device < future_deadlines.size(); ++device) {
        auto due = future_deadlines;
        due[device] = 100u;
        passed &= expect(!galaxy::checkpoint::quiet_architectural_checkpoint(
            100u, due, 0u, false, 2u, 3u, true),
            "an exact VI/DEC/AI/HID edge forces service even before broker publication");
        due[device] = 99u;
        passed &= expect(!galaxy::checkpoint::quiet_architectural_checkpoint(
            100u, due, 0u, false, 2u, 3u, true),
            "a late unpublished edge also forces service");
    }
    passed &= expect(!galaxy::checkpoint::quiet_architectural_checkpoint(
        100u, future_deadlines, 1u, false, 2u, 3u, true) &&
        !galaxy::checkpoint::quiet_architectural_checkpoint(
        100u, future_deadlines, 0u, true, 2u, 3u, true) &&
        !galaxy::checkpoint::quiet_architectural_checkpoint(
        100u, future_deadlines, 0u, false, 3u, 3u, true) &&
        !galaxy::checkpoint::quiet_architectural_checkpoint(
        100u, future_deadlines, 0u, false, 2u, 3u, false),
        "pending work, retained IRQ levels, periodic threshold and ownership all prevent return");
    // Restore EE against the same latched level: suppressing an ineligible
    // scheduling hint must not consume the event. Scanning is independent.
    for (const bool latched : {false, true}) {
        for (const bool scan : {false, true}) {
            passed &= expect(
                galaxy::checkpoint::pe_service_due(false, false, latched, scan) ==
                    (latched || scan),
                "disabled experiment preserves the existing PE schedule");
            passed &= expect(
                galaxy::checkpoint::pe_service_due(true, false, latched, scan) == scan,
                "masked PE level does not force a callback; FIFO scanning still does");
            passed &= expect(
                galaxy::checkpoint::pe_service_due(true, true, latched, scan) ==
                    (latched || scan),
                "restoring EE makes the retained PE level immediately serviceable");
        }
    }
    passed &= activation_is_requested_and_deferred();
    passed &= unwinding_restores_exactly_once();
    passed &= translated_inline_scope_is_lexical_and_nested();
    passed &= translated_inline_scope_preserves_recursive_full_depth();
    passed &= inactive_inline_scope_contract();
    passed &= pending_event_bypass_stays_in_generated_code();
    passed &= call_return_checkpoint_services_only_published_events();
    passed &= binding_contract_fails_closed();
    passed &= full_requested_mode_never_publishes();
    return passed ? 0 : 1;
}
