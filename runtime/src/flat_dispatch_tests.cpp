#include "galaxy/flat_dispatch.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

using galaxy::scheduler::ContextTransferToken;
using galaxy::scheduler::FlatContinuationStopReason;
using galaxy::scheduler::GuestExceptionIdentityDisposition;
using galaxy::scheduler::GuestExceptionIdentityEvidence;
using galaxy::scheduler::GuestSaveContextIdentityDisposition;
using galaxy::scheduler::GuestSaveContextIdentityEvidence;
using galaxy::scheduler::GuestSetCurrentContextDisposition;
using galaxy::scheduler::GuestSetCurrentContextEvidence;
using galaxy::scheduler::GuestSetCurrentContextRoleKind;
using galaxy::scheduler::GuestSetCurrentContextToken;
using galaxy::scheduler::GuestSleepIdentityDisposition;
using galaxy::scheduler::GuestSleepIdentityEvidence;
using galaxy::scheduler::GuestSleepQueueDisposition;
using galaxy::scheduler::PendingContextTransfer;
using galaxy::scheduler::ScopedContextTransfer;
using galaxy::scheduler::PendingGuestSetCurrentContexts;

bool expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

bool rfi_uses_only_srr_pair() {
    galaxy::PpcContext context{};
    for (std::uint32_t i = 0u; i < 32u; ++i) {
        context.gpr[i] = 0xC0000000u + i;
        context.fpr_bits[i] = 0x1000000000000000ull + i;
        context.ps1_bits[i] = 0x2000000000000000ull + i;
    }
    context.cr = 0xC1000001u;
    context.lr = 0xC1000002u;
    context.ctr = 0xC1000003u;
    context.xer = 0xC1000004u;
    context.msr = 0xDEADBEEFu;
    context.pc = 0xC1000006u;
    for (std::uint32_t i = 0u; i < 8u; ++i) {
        context.gqr[i] = 0xC2000000u + i;
    }

    context.fpr_bits[7] = 0x1122334455667788ull;
    context.ps1_bits[9] = 0x8877665544332211ull;
    context.fpscr = 0x01020304u;
    context.hid2 = 0x05060708u;
    context.segment_registers[3] = 0x09101112u;
    context.spr[26] = 0x13141516u;
    context.time_base_offset = 0x1718192021222324ull;
    context.decrementer_start_ticks = 0x2526272829303132ull;
    context.decrementer_start_value = 0x33343536u;
    context.reserved_address = 0x37383940u;
    context.reserved_value = 0x41424344u;
    context.spr[26] = 0x80001237u;
    context.spr[27] = 0x00009032u;
    const galaxy::PpcContext before = context;

    const std::uint32_t resume_pc = galaxy::scheduler::apply_rfi(context);

    bool passed = expect(
        resume_pc == 0x80001237u && context.pc == resume_pc,
        "RFI transfers the exact SRR0 value to the next-instruction address");
    passed &= expect(
        context.msr == 0x58299032u,
        "RFI merges Broadway's SRR1 mask, preserves other MSR bits, and clears MSR[13]");
    for (std::uint32_t i = 0u; i < 32u; ++i) {
        passed &= expect(
            context.gpr[i] == before.gpr[i] &&
                context.fpr_bits[i] == before.fpr_bits[i] &&
                context.ps1_bits[i] == before.ps1_bits[i],
            "RFI does not reload GPR/FPR state from an unrelated OSContext");
    }
    passed &= expect(
        context.cr == before.cr && context.lr == before.lr &&
            context.ctr == before.ctr && context.xer == before.xer &&
            context.fpscr == before.fpscr,
        "RFI preserves control state other than PC and MSR");
    for (std::uint32_t i = 0u; i < 8u; ++i) {
        passed &= expect(
            context.gqr[i] == before.gqr[i],
            "RFI does not reload GQR state from OSContext memory");
    }
    passed &= expect(
        context.hid2 == before.hid2 &&
            context.segment_registers[3] == before.segment_registers[3] &&
            context.spr[26] == before.spr[26] &&
            context.spr[27] == before.spr[27] &&
            context.time_base_offset == before.time_base_offset &&
            context.decrementer_start_ticks ==
                before.decrementer_start_ticks &&
            context.decrementer_start_value == before.decrementer_start_value &&
            context.reserved_address == before.reserved_address &&
            context.reserved_value == before.reserved_value,
        "RFI leaves non-SRR architectural and host-owned state unchanged");
    return passed;
}

// The emitted continuations this fixture treats as statically translated.
//
// At namespace scope on purpose. It used to be a local `constexpr std::array`
// inside `physical_rfi_aliases_bind_only_to_exact_static_continuations`, read by
// the capture-less lambda `has_exact`. A lambda with no capture-default can only
// name such a variable if it is not odr-used, which is an ODR subtlety MSVC
// accepts and the pinned clang-cl (the compiler that actually builds this tree)
// rejects with "variable 'translated' cannot be implicitly captured in a lambda
// with no capture-default specified". A variable at namespace scope is reachable
// from a capture-less lambda under every compiler, so hoisting it removes the
// ambiguity without changing what the fixture tests.
constexpr std::array kTranslatedContinuations{
    0x004A8CD4u,
    0x804A8CD4u,
    0x90001000u,
};

bool physical_rfi_aliases_bind_only_to_exact_static_continuations() {
    constexpr std::uint32_t kMem1Size = 0x01800000u;
    constexpr std::uint32_t kMem2Size = 0x04000000u;
    const auto has_exact = [](std::uint32_t address) {
        for (const std::uint32_t candidate : kTranslatedContinuations) {
            if (candidate == address) {
                return true;
            }
        }
        return false;
    };
    const auto bind = [&](std::uint32_t address) {
        return galaxy::scheduler::resolve_static_executable_address(
            address, kMem1Size, kMem2Size, has_exact);
    };

    bool passed = expect(
        bind(0x004A8CD4u) == 0x004A8CD4u,
        "an exact physical continuation takes priority over its cached alias");

    const auto cached_only = [](std::uint32_t address) {
        return address == 0x804A8CD4u || address == 0x90001000u;
    };
    passed &= expect(
        galaxy::scheduler::resolve_static_executable_address(
            0x004A8CD4u, kMem1Size, kMem2Size, cached_only) ==
            0x804A8CD4u,
        "a physical MEM1 SRR0 binds to an emitted cached continuation");
    passed &= expect(
        galaxy::scheduler::resolve_static_executable_address(
            0x10001000u, kMem1Size, kMem2Size, cached_only) ==
            0x90001000u,
        "a physical MEM2 SRR0 binds to an emitted cached continuation");
    passed &= expect(
        galaxy::scheduler::resolve_static_executable_address(
            0x004A8CD7u, kMem1Size, kMem2Size, cached_only) ==
            0x804A8CD4u,
        "static dispatch clears non-instruction low bits before exact lookup");
    passed &= expect(
        bind(0x804A8CD4u) == 0x804A8CD4u,
        "an already-cached continuation remains unchanged");

    const auto missing = [](std::uint32_t) { return false; };
    passed &= expect(
        galaxy::scheduler::resolve_static_executable_address(
            0x00002000u, kMem1Size, kMem2Size, missing) == 0x00002000u,
        "an unknown physical MEM1 address is not fabricated into success");
    passed &= expect(
        galaxy::scheduler::resolve_static_executable_address(
            0x14000000u, kMem1Size, kMem2Size, cached_only) == 0x14000000u,
        "the byte immediately beyond physical MEM2 is never aliased");
    return passed;
}

bool guest_stack_spans_accept_exact_mem2_without_weakening_identity() {
    using galaxy::scheduler::guest_stack_span_contains;
    using galaxy::scheduler::is_cached_guest_ram_stack_span;
    constexpr std::uint32_t kMem1Size = 0x01800000u;
    constexpr std::uint32_t kMem2Size = 0x04000000u;
    constexpr std::uint32_t kObservedBase = 0x90EA5504u;
    constexpr std::uint32_t kObservedEnd = 0x90E9D504u;
    constexpr std::uint32_t kObservedR1 = 0x90EA54B8u;

    bool passed = expect(
        is_cached_guest_ram_stack_span(
            kObservedBase, kObservedEnd, kMem1Size, kMem2Size),
        "the observed 0x8000-byte JASDvd stack is a valid cached MEM2 span");
    passed &= expect(
        guest_stack_span_contains(
            kObservedBase, kObservedEnd, kObservedR1),
        "the observed JASDvd r1 belongs to its exact recorded MEM2 stack");
    passed &= expect(
        !guest_stack_span_contains(
            kObservedBase, kObservedEnd, 0x90E9D500u),
        "a foreign frame below stackEnd never acquires this thread identity");
    passed &= expect(
        !guest_stack_span_contains(
            kObservedBase, kObservedEnd, kObservedBase),
        "the high-exclusive stackBase is not accepted as a live frame");
    passed &= expect(
        !is_cached_guest_ram_stack_span(
            0x95008000u, 0x95000000u, kMem1Size, kMem2Size),
        "an unmapped address range is not accepted as cached MEM2");
    passed &= expect(
        !is_cached_guest_ram_stack_span(
            0x90001000u, 0x817FF000u, kMem1Size, kMem2Size),
        "a span crossing from cached MEM1 into cached MEM2 is rejected");
    passed &= expect(
        !is_cached_guest_ram_stack_span(
            kObservedEnd, kObservedBase, kMem1Size, kMem2Size),
        "reversed stack bounds are rejected");
    passed &= expect(
        !is_cached_guest_ram_stack_span(
            kObservedBase, kObservedBase, kMem1Size, kMem2Size),
        "an empty stack span is rejected");
    passed &= expect(
        is_cached_guest_ram_stack_span(
            0x817FFFF0u, 0x817FE000u, kMem1Size, kMem2Size),
        "a mapped cached MEM1 stack remains valid");
    return passed;
}

bool pending_osload_is_consumed_exactly_once() {
    PendingContextTransfer pending;
    const ContextTransferToken transfer{0x809C3060u, 0x804A381Cu};
    const ContextTransferToken wrong_target{0x809D3420u, 0x804A381Cu};

    bool passed = expect(
        pending.begin(transfer),
        "the first OSLoadContext owns its pending transfer token");
    passed &= expect(
        !pending.begin(wrong_target),
        "recursive OSLoadContext entry is rejected");
    passed &= expect(
        pending.pending() != nullptr &&
            *pending.pending() == transfer,
        "the RFI boundary can inspect the original token without consuming it");
    passed &= expect(
        !pending.consume_exact(wrong_target) &&
            pending.pending() != nullptr,
        "a mismatched RFI target fails closed and preserves ownership");
    passed &= expect(
        pending.consume_exact(transfer),
        "the matching OSLoadContext boundary consumes its token");
    passed &= expect(
        pending.pending() == nullptr &&
            !pending.consume_exact(transfer),
        "normal and exceptional cleanup cannot consume twice");
    return passed;
}

bool scoped_osload_cleanup_preserves_unwind_and_failure() {
    static_assert(!std::is_copy_constructible_v<ScopedContextTransfer>);
    static_assert(!std::is_move_constructible_v<ScopedContextTransfer>);
    static_assert(std::is_nothrow_destructible_v<ScopedContextTransfer>);
    const ContextTransferToken a{0x80650878u, 0x804A381Cu};
    const ContextTransferToken b{0x809A00D8u, 0x804A381Cu};
    struct ControlTransfer { std::uint32_t address; };
    bool passed = true;
    {
        PendingContextTransfer pending;
        passed &= expect(pending.begin(a), "scoped owner begins");
        std::vector<unsigned> order;
        struct InnerFrame {
            PendingContextTransfer& pending;
            ContextTransferToken token;
            std::vector<unsigned>& order;
            bool& observed;
            ~InnerFrame() noexcept {
                observed = pending.pending() != nullptr && *pending.pending() == token;
                order.push_back(1u);
            }
        };
        order.reserve(2u);
        bool inner_saw_owner = false;
        try {
            ScopedContextTransfer owner{pending, a};
            InnerFrame frame{pending, a, order, inner_saw_owner};
            throw ControlTransfer{0x804AB30Cu};
        } catch (const ControlTransfer& transfer) {
            order.push_back(2u);
            passed &= expect(transfer.address == 0x804AB30Cu,
                "control transfer payload propagates unchanged");
            passed &= expect(pending.pending() == nullptr &&
                pending.cleanup_failure() == nullptr,
                "owner is consumed before continuation catch");
        }
        passed &= expect(inner_saw_owner && order == std::vector<unsigned>({1u,2u}),
            "inner frame destruction observes token before owner cleanup");
        passed &= expect(pending.begin(a), "clean next transfer is allowed");
        {
            ScopedContextTransfer owner{pending, a};
            passed &= expect(owner.finish() && !owner.finish(),
                "normal/missing-body cleanup consumes exactly once");
        }
        passed &= expect(!pending.pending() && !pending.cleanup_failure(),
            "destructor after explicit finish cannot fail or consume twice");
    }
    {
        PendingContextTransfer pending;
        passed &= expect(pending.begin(a), "generic failure owner begins");
        try {
            ScopedContextTransfer owner{pending, a};
            if (!pending.begin(b)) throw std::runtime_error("recursive transfer rejected");
        } catch (const std::runtime_error& error) {
            passed &= expect(std::string_view(error.what()) == "recursive transfer rejected",
                "ordinary error retains original exception after cleanup");
        }
        passed &= expect(!pending.pending() && !pending.cleanup_failure(),
            "recursive rejection does not consume the wrong token");
    }
    for (const bool different_token : {false, true}) {
        PendingContextTransfer pending;
        passed &= expect(pending.begin(a), "malformed cleanup fixture begins");
        try {
            ScopedContextTransfer owner{pending, a};
            passed &= expect(pending.consume_exact(a), "fixture removes original token");
            if (different_token) passed &= expect(pending.begin(b), "fixture installs unrelated token");
            throw ControlTransfer{0x804AB30Cu};
        } catch (const ControlTransfer&) {
            const auto* failed = pending.cleanup_failure();
            passed &= expect(failed && *failed == a,
                "failed unwind cleanup latches exact original ownership");
            // The production retained/flat/FPU/control-exit gates inspect this
            // same latch before any callback or continuation can be accepted.
            bool continuation_ran = false;
            if (failed == nullptr) continuation_ran = true;
            passed &= expect(!continuation_ran, "failed cleanup rejects continuation");
        }
        passed &= expect(different_token ? pending.pending() && *pending.pending() == b : !pending.pending(),
            "failed owner never erases an unrelated token");
        if (different_token) passed &= expect(pending.consume_exact(b), "unrelated token remains independently owned");
        passed &= expect(!pending.begin(a), "sticky cleanup failure blocks a new transfer even when empty");
        {
            ScopedContextTransfer repeated_failure{pending, b};
        }
        passed &= expect(pending.cleanup_failure() && *pending.cleanup_failure() == a,
            "later cleanup failures cannot replace the first failure identity");
    }
    return passed;
}

bool valid_guest_sleep_always_runs_translated_scheduler() {
    using galaxy::scheduler::classify_guest_sleep_queue;

    bool passed = expect(
        classify_guest_sleep_queue(0x806A2E60u, true) ==
            GuestSleepQueueDisposition::RunTranslatedScheduler,
        "a valid MEM1 sleep queue enters translated OSSleepThread");
    passed &= expect(
        classify_guest_sleep_queue(0x933E00ACu, true) ==
            GuestSleepQueueDisposition::RunTranslatedScheduler,
        "a valid MEM2 sleep queue enters translated OSSleepThread");
    passed &= expect(
        classify_guest_sleep_queue(0u, true) ==
            GuestSleepQueueDisposition::RejectNullQueue,
        "a null sleep queue hard-fails instead of becoming a host wakeup");
    passed &= expect(
        classify_guest_sleep_queue(0x806A2E61u, true) ==
            GuestSleepQueueDisposition::RejectMisalignedQueue,
        "a misaligned sleep queue hard-fails instead of becoming a host wakeup");
    passed &= expect(
        classify_guest_sleep_queue(0x806A2E60u, false) ==
            GuestSleepQueueDisposition::RejectUnmappedQueue,
        "an unmapped sleep queue hard-fails instead of becoming a host wakeup");
    return passed;
}

GuestSleepIdentityEvidence valid_sleep_identity() {
    constexpr std::uint32_t kThread = 0x80650878u;
    return GuestSleepIdentityEvidence{
        kThread,
        kThread & 0x3FFFFFFFu,
        kThread,
        kThread,
        kThread,
        2u,
        true,
        true,
        true};
}

bool guest_sleep_identity_is_strict_and_non_repairing() {
    using galaxy::scheduler::classify_guest_sleep_identity;

    GuestSleepIdentityEvidence evidence = valid_sleep_identity();
    bool passed = expect(
        classify_guest_sleep_identity(evidence) ==
            GuestSleepIdentityDisposition::Valid,
        "a running thread with matching D4/C0/E4/r1 ownership is valid");

    evidence = valid_sleep_identity();
    evidence.current_context = 0x80908A20u;
    evidence.physical_context = evidence.current_context & 0x3FFFFFFFu;
    evidence.context_owner_thread = evidence.current_thread;
    passed &= expect(
        classify_guest_sleep_identity(evidence) ==
            GuestSleepIdentityDisposition::Valid,
        "a stack-allocated exception context owned by E4 remains valid");

    evidence = valid_sleep_identity();
    evidence.current_context = 0u;
    passed &= expect(
        classify_guest_sleep_identity(evidence) ==
            GuestSleepIdentityDisposition::RejectNullCurrentContext,
        "a null D4 is rejected before any ownership inference");

    evidence = valid_sleep_identity();
    evidence.current_context_is_mapped = false;
    passed &= expect(
        classify_guest_sleep_identity(evidence) ==
            GuestSleepIdentityDisposition::RejectUnmappedCurrentContext,
        "D4 must name a fully mapped OSContext");

    evidence = valid_sleep_identity();
    evidence.physical_context ^= 4u;
    passed &= expect(
        classify_guest_sleep_identity(evidence) ==
            GuestSleepIdentityDisposition::RejectPhysicalContextAlias,
        "a stale C0 alias is rejected instead of repaired");

    evidence = valid_sleep_identity();
    evidence.current_thread = 0u;
    passed &= expect(
        classify_guest_sleep_identity(evidence) ==
            GuestSleepIdentityDisposition::RejectNullCurrentThread,
        "the scheduler idle E4 value cannot enter translated OSSleepThread");

    evidence = valid_sleep_identity();
    evidence.current_thread_is_mapped = false;
    passed &= expect(
        classify_guest_sleep_identity(evidence) ==
            GuestSleepIdentityDisposition::RejectUnmappedCurrentThread,
        "E4 must map the complete OSThread fields consumed by sleep");

    evidence = valid_sleep_identity();
    evidence.current_thread_is_active = false;
    passed &= expect(
        classify_guest_sleep_identity(evidence) ==
            GuestSleepIdentityDisposition::RejectInactiveCurrentThread,
        "E4 must name a member of the exact active-thread queue");

    evidence = valid_sleep_identity();
    evidence.current_thread_state = 4u;
    passed &= expect(
        classify_guest_sleep_identity(evidence) ==
            GuestSleepIdentityDisposition::RejectNonRunningCurrentThread,
        "the caller must still be running before translated sleep marks it waiting");

    evidence = valid_sleep_identity();
    evidence.current_context = 0x80908A20u;
    evidence.physical_context = evidence.current_context & 0x3FFFFFFFu;
    evidence.context_owner_thread = 0x807ACCA0u;
    passed &= expect(
        classify_guest_sleep_identity(evidence) ==
            GuestSleepIdentityDisposition::RejectContextOwnerMismatch,
        "a foreign D4 context is rejected instead of stamped onto E4");

    evidence = valid_sleep_identity();
    evidence.r1_owner_thread = 0x807ACCA0u;
    passed &= expect(
        classify_guest_sleep_identity(evidence) ==
            GuestSleepIdentityDisposition::RejectStackOwnerMismatch,
        "an r1 owned by another thread is rejected instead of healing identity");

    evidence = valid_sleep_identity();
    evidence.r1_owner_thread = 0u;
    passed &= expect(
        classify_guest_sleep_identity(evidence) ==
            GuestSleepIdentityDisposition::RejectStackOwnerMismatch,
        "an unowned r1 cannot silently bypass the strict sleep boundary");
    return passed;
}

GuestExceptionIdentityEvidence valid_exception_identity() {
    constexpr std::uint32_t kThread = 0x80650878u;
    return GuestExceptionIdentityEvidence{
        kThread,
        kThread,
        kThread & 0x3FFFFFFFu,
        kThread,
        kThread,
        kThread,
        2u,
        true,
        false,
        true,
        true,
        false};
}

bool guest_exception_identity_is_strict_and_preserves_idle() {
    using galaxy::scheduler::classify_guest_exception_identity;
    using galaxy::scheduler::resolve_guest_stack_owner;

    constexpr std::uint32_t kThread = 0x80650878u;
    constexpr std::uint32_t kForeignThread = 0x807ACCA0u;
    constexpr std::uint32_t kStackContext = 0x816FFE00u;
    constexpr std::uint32_t kIdleContext = 0x80650C90u;
    GuestExceptionIdentityEvidence evidence = valid_exception_identity();
    bool passed = expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::ValidThread,
        "an exception may save the exact running thread context");

    evidence = valid_exception_identity();
    evidence.requested_context = kStackContext;
    evidence.current_context = kStackContext;
    evidence.physical_context = kStackContext & 0x3FFFFFFFu;
    evidence.context_owner_thread = kThread;
    passed &= expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::ValidThread,
        "a stack-local exception OSContext owned by E4 is valid");

    constexpr std::uint32_t kFiberFrame = 0x80661160u;
    constexpr std::uint32_t kThreadFrame = 0x816FFD00u;
    const std::uint32_t fiber_owner = resolve_guest_stack_owner(
        kFiberFrame,
        [](std::uint32_t frame) {
            return frame == kThreadFrame ? kThread : 0u;
        },
        [](std::uint32_t frame, std::uint32_t& previous) {
            if (frame != kFiberFrame) {
                return false;
            }
            previous = kThreadFrame;
            return true;
        });
    evidence = valid_exception_identity();
    evidence.r1_owner_thread = fiber_owner;
    passed &= expect(
        fiber_owner == kThread &&
            classify_guest_exception_identity(evidence) ==
                GuestExceptionIdentityDisposition::ValidThread,
        "an exact OSSwitchFiber backchain preserves exception ownership");

    evidence = GuestExceptionIdentityEvidence{
        kIdleContext,
        kIdleContext,
        kIdleContext & 0x3FFFFFFFu,
        0u,
        0u,
        kForeignThread,
        0u,
        true,
        true,
        false,
        false,
        false};
    passed &= expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::ValidIdle,
        "SelectThread's exact D4=IdleContext/E4=null window remains valid");

    evidence.current_thread = kThread;
    passed &= expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::RejectIdleCurrentThread,
        "IdleContext cannot be paired with a fabricated current thread");

    evidence = valid_exception_identity();
    evidence.requested_context = kStackContext;
    passed &= expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::RejectCurrentContextChanged,
        "the requested exception target must remain the observed D4 value");

    evidence = valid_exception_identity();
    evidence.current_context = 0u;
    passed &= expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::RejectNullCurrentContext,
        "a null architectural D4 hard-fails");

    evidence = valid_exception_identity();
    evidence.current_context += 4u;
    evidence.requested_context += 4u;
    evidence.physical_context = evidence.current_context & 0x3FFFFFFFu;
    passed &= expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::
                RejectMisalignedCurrentContext,
        "an unaligned architectural OSContext hard-fails");

    evidence = valid_exception_identity();
    evidence.current_context_is_mapped = false;
    passed &= expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::RejectUnmappedCurrentContext,
        "D4 must map a complete OSContext");

    evidence = valid_exception_identity();
    evidence.physical_context ^= 4u;
    passed &= expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::RejectPhysicalContextAlias,
        "C0 must remain D4's exact physical alias");

    evidence = valid_exception_identity();
    evidence.current_thread = 0u;
    passed &= expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::RejectNullCurrentThread,
        "only the named IdleContext permits E4=null");

    evidence = valid_exception_identity();
    evidence.current_thread_is_mapped = false;
    passed &= expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::RejectUnmappedCurrentThread,
        "E4 must map a complete OSThread");

    evidence = valid_exception_identity();
    evidence.current_thread_is_active = false;
    passed &= expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::RejectInactiveCurrentThread,
        "E4 must remain on the active-thread queue");

    evidence = valid_exception_identity();
    evidence.current_thread_state = 1u;
    passed &= expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::RejectNonRunningCurrentThread,
        "an enabled exception cannot stamp a non-running thread");

    evidence = valid_exception_identity();
    evidence.current_context = kStackContext;
    evidence.requested_context = kStackContext;
    evidence.physical_context = kStackContext & 0x3FFFFFFFu;
    evidence.context_owner_thread = kForeignThread;
    passed &= expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::RejectContextOwnerMismatch,
        "a foreign stack-local D4 hard-fails instead of being repaired");

    evidence = valid_exception_identity();
    evidence.r1_owner_thread = kForeignThread;
    passed &= expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::RejectStackOwnerMismatch,
        "a foreign executing stack hard-fails instead of healing D4/E4");

    evidence = valid_exception_identity();
    evidence.r1_owner_thread = 0u;
    passed &= expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::RejectStackOwnerMismatch,
        "an unprovable non-idle stack owner fails closed");
    return passed;
}

bool guest_exception_identity_accepts_only_exact_idle_temporary_lineage() {
    using galaxy::scheduler::classify_guest_exception_identity;

    constexpr std::uint32_t kThread = 0x80650878u;
    constexpr std::uint32_t kForeignThread = 0x807ACCA0u;
    constexpr std::uint32_t kViTemporaryContext = 0x806BDBD8u;
    GuestExceptionIdentityEvidence evidence{
        kViTemporaryContext,
        kViTemporaryContext,
        kViTemporaryContext & 0x3FFFFFFFu,
        0u,
        kThread,
        kThread,
        0u,
        true,
        false,
        false,
        false,
        true};

    bool passed = expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::ValidIdleTemporary,
        "VI's exact stack-local context remains valid over the scheduler idle window");
    passed &= expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::ValidIdleTemporary,
        "the identical post-RFI retry tuple remains valid without state repair");

    evidence.current_context_has_idle_temporary_lineage = false;
    passed &= expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::RejectNullCurrentThread,
        "E4=null cannot bypass identity without exact idle-temporary lineage");

    evidence.current_context_has_idle_temporary_lineage = true;
    evidence.current_thread = kThread;
    passed &= expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::
                RejectIdleTemporaryCurrentThread,
        "idle-temporary lineage cannot be paired with a fabricated E4 thread");

    evidence.current_thread = 0u;
    evidence.context_owner_thread = 0u;
    passed &= expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::RejectContextOwnerMismatch,
        "an idle-temporary context must resolve to a nonzero active-stack owner");

    evidence.context_owner_thread = kThread;
    evidence.r1_owner_thread = kForeignThread;
    passed &= expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::RejectStackOwnerMismatch,
        "idle-temporary D4 and executing r1 must resolve to the same owner");

    evidence.r1_owner_thread = 0u;
    passed &= expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::RejectStackOwnerMismatch,
        "an unowned idle-temporary executing stack fails closed");

    evidence.r1_owner_thread = kThread;
    evidence.physical_context ^= 4u;
    passed &= expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::RejectPhysicalContextAlias,
        "idle-temporary lineage never bypasses the exact C0 alias");

    evidence.physical_context = kViTemporaryContext & 0x3FFFFFFFu;
    evidence.current_context_is_mapped = false;
    passed &= expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::RejectUnmappedCurrentContext,
        "idle-temporary lineage never bypasses complete OSContext mapping");

    evidence.current_context_is_mapped = true;
    evidence.requested_context += 8u;
    passed &= expect(
        classify_guest_exception_identity(evidence) ==
            GuestExceptionIdentityDisposition::RejectCurrentContextChanged,
        "idle-temporary lineage never permits the requested D4 identity to change");
    return passed;
}

GuestSaveContextIdentityEvidence valid_save_context_identity() {
    constexpr std::uint32_t kThread = 0x80650878u;
    return GuestSaveContextIdentityEvidence{
        kThread,
        kThread,
        kThread & 0x3FFFFFFFu,
        kThread,
        kThread,
        0u,
        true,
        true,
        true};
}

bool ossave_context_identity_is_exact_and_non_repairing() {
    using galaxy::scheduler::classify_guest_save_context_identity;
    using galaxy::scheduler::resolve_guest_stack_owner;

    constexpr std::uint32_t kThread = 0x80650878u;
    constexpr std::uint32_t kForeignThread = 0x807ACCA0u;
    GuestSaveContextIdentityEvidence evidence = valid_save_context_identity();
    const std::uint32_t architectural_r3 = evidence.requested_context;
    bool passed = expect(
        classify_guest_save_context_identity(evidence) ==
                GuestSaveContextIdentityDisposition::Valid &&
            evidence.requested_context == architectural_r3,
        "SelectThread's exact D4=E4=r3 save target is accepted unchanged");

    constexpr std::uint32_t kFiberFrame = 0x80661160u;
    constexpr std::uint32_t kThreadFrame = 0x816FFD00u;
    evidence = valid_save_context_identity();
    evidence.r1_owner_thread = resolve_guest_stack_owner(
        kFiberFrame,
        [](std::uint32_t frame) {
            return frame == kThreadFrame ? kThread : 0u;
        },
        [](std::uint32_t frame, std::uint32_t& previous) {
            if (frame != kFiberFrame) {
                return false;
            }
            previous = kThreadFrame;
            return true;
        });
    passed &= expect(
        classify_guest_save_context_identity(evidence) ==
            GuestSaveContextIdentityDisposition::Valid,
        "OSSaveContext accepts an exact OSSwitchFiber backchain owner");

    evidence = valid_save_context_identity();
    evidence.requested_context = 0u;
    passed &= expect(
        classify_guest_save_context_identity(evidence) ==
            GuestSaveContextIdentityDisposition::RejectNullRequestedContext,
        "OSSaveContext rejects a null architectural r3");

    evidence = valid_save_context_identity();
    evidence.requested_context_is_mapped = false;
    passed &= expect(
        classify_guest_save_context_identity(evidence) ==
            GuestSaveContextIdentityDisposition::RejectUnmappedRequestedContext,
        "OSSaveContext r3 must map a complete OSContext");

    evidence = valid_save_context_identity();
    evidence.current_context = kForeignThread;
    passed &= expect(
        classify_guest_save_context_identity(evidence) ==
            GuestSaveContextIdentityDisposition::RejectCurrentContextMismatch,
        "SelectThread's D4 must exactly equal the requested save target");

    evidence = valid_save_context_identity();
    evidence.physical_context ^= 4u;
    passed &= expect(
        classify_guest_save_context_identity(evidence) ==
            GuestSaveContextIdentityDisposition::RejectPhysicalContextAlias,
        "OSSaveContext rejects a stale C0 alias");

    evidence = valid_save_context_identity();
    evidence.current_thread = kForeignThread;
    passed &= expect(
        classify_guest_save_context_identity(evidence) ==
            GuestSaveContextIdentityDisposition::RejectCurrentThreadMismatch,
        "SelectThread's E4 must exactly equal architectural r3");

    evidence = valid_save_context_identity();
    evidence.current_thread_is_mapped = false;
    passed &= expect(
        classify_guest_save_context_identity(evidence) ==
            GuestSaveContextIdentityDisposition::RejectUnmappedCurrentThread,
        "the OSSaveContext target must map a complete OSThread");

    evidence = valid_save_context_identity();
    evidence.current_thread_is_active = false;
    passed &= expect(
        classify_guest_save_context_identity(evidence) ==
            GuestSaveContextIdentityDisposition::RejectInactiveCurrentThread,
        "the OSSaveContext target must remain active");

    evidence = valid_save_context_identity();
    evidence.r1_owner_thread = kForeignThread;
    passed &= expect(
        classify_guest_save_context_identity(evidence) ==
            GuestSaveContextIdentityDisposition::RejectStackOwnerMismatch,
        "OSSaveContext rejects a proven foreign executing stack");

    evidence = valid_save_context_identity();
    evidence.r1_owner_thread = 0u;
    passed &= expect(
        classify_guest_save_context_identity(evidence) ==
            GuestSaveContextIdentityDisposition::RejectStackOwnerMismatch,
        "OSSaveContext fails closed when r1 ownership is unprovable");

    evidence = valid_save_context_identity();
    evidence.requested_context_state = 2u;
    passed &= expect(
        classify_guest_save_context_identity(evidence) ==
            GuestSaveContextIdentityDisposition::RejectExceptionState,
        "SelectThread cannot call OSSaveContext with exception state set");
    return passed;
}

GuestSetCurrentContextEvidence set_context_evidence(
    std::uint32_t return_lr,
    std::uint32_t requested_context,
    std::uint32_t current_context,
    std::uint32_t r1) {
    constexpr std::uint32_t kThread = 0x80650878u;
    return GuestSetCurrentContextEvidence{
        return_lr,
        requested_context,
        current_context,
        current_context & 0x3FFFFFFFu,
        kThread,
        r1,
        2u,
        true,
        true,
        true,
        true,
        false};
}

bool osset_role_table_is_exhaustive_and_exact() {
    using galaxy::scheduler::find_guest_set_current_context_role;
    using galaxy::scheduler::guest_set_current_context_role_table_is_exact;
    using galaxy::scheduler::kRmge01SetCurrentContextRoles;

    constexpr std::array<std::array<std::uint32_t, 3u>, 30u> kExpected{{
        {{0x804965ECu, 0x8u, 0x804968E4u}},
        {{0x804968E4u, 0u, 0x804965ECu}},
        {{0x804A2548u, 0x8u, 0x804A256Cu}},
        {{0x804A256Cu, 0u, 0x804A2548u}},
        {{0x804A3B54u, 0x8u, 0x804A3C28u}},
        {{0x804A3C28u, 0u, 0x804A3B54u}},
        {{0x804A6C58u, 0x80650540u, 0u}},
        {{0x804AAC88u, 0x80650878u, 0u}},
        {{0x804AB354u, 0x80650C90u, 0u}},
        {{0x804AB410u, 0u, 0u}},
        {{0x804AF370u, 0x8u, 0x804AF394u}},
        {{0x804AF394u, 0u, 0x804AF370u}},
        {{0x804AF58Cu, 0x8u, 0x804AF5B0u}},
        {{0x804AF5B0u, 0u, 0x804AF58Cu}},
        {{0x804AF64Cu, 0x8u, 0x804AF678u}},
        {{0x804AF678u, 0u, 0x804AF64Cu}},
        {{0x804B16E4u, 0x10u, 0x804B1748u}},
        {{0x804B1748u, 0u, 0x804B16E4u}},
        {{0x804B1768u, 0x10u, 0x804B1B88u}},
        {{0x804B1B88u, 0u, 0x804B1768u}},
        {{0x804B8424u, 0x8u, 0x804B8440u}},
        {{0x804B8440u, 0u, 0x804B8424u}},
        {{0x804BA2C8u, 0x8u, 0x804BA2E8u}},
        {{0x804BA2E8u, 0u, 0x804BA2C8u}},
        {{0x804BA39Cu, 0x8u, 0x804BA3B8u}},
        {{0x804BA3B8u, 0u, 0x804BA39Cu}},
        {{0x804C817Cu, 0x8u, 0x804C81D4u}},
        {{0x804C81D4u, 0u, 0x804C817Cu}},
        {{0x804D213Cu, 0x8u, 0x804D2160u}},
        {{0x804D2160u, 0u, 0x804D213Cu}},
    }};

    std::size_t installs = 0u;
    std::size_t restores = 0u;
    std::size_t specials = 0u;
    bool passed = expect(
        kRmge01SetCurrentContextRoles.size() == 30u &&
            guest_set_current_context_role_table_is_exact(),
        "RMGE01's OSSetCurrentContext role table has exactly 30 unique calls");
    for (std::size_t i = 0u;
         i < kRmge01SetCurrentContextRoles.size();
         ++i) {
        const auto& role = kRmge01SetCurrentContextRoles[i];
        passed &= expect(
            find_guest_set_current_context_role(role.return_lr) == &role,
            "every OSSetCurrentContext return LR resolves to its exact role");
        passed &= expect(
            role.return_lr == kExpected[i][0] &&
                role.argument == kExpected[i][1] &&
                role.paired_return_lr == kExpected[i][2],
            "every generated return LR, operand, and pair matches RMGE01");
        if (role.kind ==
            GuestSetCurrentContextRoleKind::TemporaryInstall) {
            ++installs;
        } else if (role.kind ==
                   GuestSetCurrentContextRoleKind::TemporaryRestore) {
            ++restores;
        } else {
            ++specials;
        }
    }
    passed &= expect(
        installs == 13u && restores == 13u && specials == 4u,
        "the role table preserves 13 callback pairs and four SDK specials");

    const auto* ai = find_guest_set_current_context_role(0x804C817Cu);
    const auto* vi_position =
        find_guest_set_current_context_role(0x804B16E4u);
    const auto* vi_retrace =
        find_guest_set_current_context_role(0x804B1768u);
    passed &= expect(
        ai != nullptr && ai->argument == 0x8u &&
            ai->paired_return_lr == 0x804C81D4u,
        "AI's generated r1+8 context has one exact restore LR");
    passed &= expect(
        vi_position != nullptr && vi_position->argument == 0x10u &&
            vi_position->paired_return_lr == 0x804B1748u &&
            vi_retrace != nullptr && vi_retrace->argument == 0x10u &&
            vi_retrace->paired_return_lr == 0x804B1B88u,
        "both VI branches preserve their generated r1+0x10 contexts");
    passed &= expect(
        find_guest_set_current_context_role(0x804A3734u) == nullptr,
        "the callee address is not mistaken for a generated return role");
    return passed;
}

bool osset_temporary_contexts_are_nested_and_non_repairing() {
    using galaxy::scheduler::classify_guest_set_current_context;

    constexpr std::uint32_t kThread = 0x80650878u;
    constexpr std::uint32_t kOuterR1 = 0x90001000u;
    constexpr std::uint32_t kOuterContext = kOuterR1 + 0x8u;
    constexpr std::uint32_t kInnerR1 = 0x91002000u;
    constexpr std::uint32_t kInnerContext = kInnerR1 + 0x10u;
    PendingGuestSetCurrentContexts pending;

    GuestSetCurrentContextEvidence evidence = set_context_evidence(
        0x804C817Cu, kOuterContext, kThread, kOuterR1);
    const std::uint32_t architectural_r3 = evidence.requested_context;
    bool passed = expect(
        classify_guest_set_current_context(evidence, pending.top()) ==
                GuestSetCurrentContextDisposition::ValidTemporaryInstall &&
            evidence.requested_context == architectural_r3,
        "AI accepts its exact alternate-stack context without rewriting r3");
    const GuestSetCurrentContextToken outer{
        kOuterContext, kThread, 0x804C817Cu, 0x804C81D4u};
    passed &= expect(
        pending.push(outer) && pending.size() == 1u,
        "the first temporary context owns one LIFO token");

    evidence = set_context_evidence(
        0x804B16E4u, kInnerContext, kOuterContext, kInnerR1);
    passed &= expect(
        classify_guest_set_current_context(evidence, pending.top()) ==
            GuestSetCurrentContextDisposition::ValidTemporaryInstall,
        "VI may nest its exact r1+0x10 context over AI's temporary context");
    const GuestSetCurrentContextToken inner{
        kInnerContext, kOuterContext, 0x804B16E4u, 0x804B1748u};
    passed &= expect(
        pending.push(inner) && pending.size() == 2u,
        "a nested temporary context receives a distinct top token");

    evidence = set_context_evidence(
        0x804B1748u, kOuterContext, kInnerContext, kInnerR1);
    passed &= expect(
        classify_guest_set_current_context(evidence, pending.top()) ==
            GuestSetCurrentContextDisposition::ValidTemporaryRestore,
        "VI restores the exact stack-local context that was current before it");
    passed &= expect(
        pending.consume_exact(inner) && pending.size() == 1u,
        "VI consumes only its own restore token");

    evidence = set_context_evidence(
        0x804C81D4u, kThread, kOuterContext, kOuterR1);
    passed &= expect(
        classify_guest_set_current_context(evidence, pending.top()) ==
            GuestSetCurrentContextDisposition::ValidTemporaryRestore,
        "AI restores its original context without any thread-owner inference");
    passed &= expect(
        pending.consume_exact(outer) && pending.empty(),
        "the outer restore consumes the final token exactly once");

    PendingGuestSetCurrentContexts out_of_order;
    passed &= expect(
        out_of_order.push(outer) && out_of_order.push(inner),
        "the out-of-order proof starts with two valid nested tokens");
    evidence = set_context_evidence(
        0x804C81D4u, kThread, kOuterContext, kOuterR1);
    passed &= expect(
        classify_guest_set_current_context(evidence, out_of_order.top()) ==
            GuestSetCurrentContextDisposition::RejectRestoreRole,
        "an outer restore cannot skip a nested temporary context");
    evidence = set_context_evidence(
        0x804B1748u, kThread, kInnerContext, kInnerR1);
    passed &= expect(
        classify_guest_set_current_context(evidence, out_of_order.top()) ==
            GuestSetCurrentContextDisposition::RejectRestoreTarget,
        "a restore cannot substitute the executing thread for the saved target");
    evidence = set_context_evidence(
        0x804B1748u, kOuterContext, kOuterContext, kInnerR1);
    passed &= expect(
        classify_guest_set_current_context(evidence, out_of_order.top()) ==
            GuestSetCurrentContextDisposition::RejectRestoreCurrentContext,
        "a restore requires D4 to still name its exact temporary context");
    return passed;
}

bool osset_idle_temporary_lineage_is_complete_and_role_validated() {
    constexpr std::uint32_t kIdle = 0x80650C90u;
    constexpr std::uint32_t kThread = 0x80650878u;
    constexpr std::uint32_t kViPositionContext = 0x806BDBD8u;
    constexpr std::uint32_t kViRetraceContext = 0x806BD8E8u;
    constexpr std::uint32_t kNestedContext = 0x806BD7A8u;
    const GuestSetCurrentContextToken vi_position{
        kViPositionContext, kIdle, 0x804B16E4u, 0x804B1748u};
    const GuestSetCurrentContextToken vi_retrace{
        kViRetraceContext, kIdle, 0x804B1768u, 0x804B1B88u};
    const GuestSetCurrentContextToken nested{
        kNestedContext, kViRetraceContext, 0x804C817Cu, 0x804C81D4u};

    PendingGuestSetCurrentContexts empty;
    bool passed = expect(
        !empty.has_exact_lineage(kViRetraceContext, kIdle),
        "an empty token stack cannot fabricate idle-temporary lineage");

    PendingGuestSetCurrentContexts direct_position;
    passed &= expect(
        direct_position.push(vi_position) &&
            direct_position.has_exact_lineage(kViPositionContext, kIdle),
        "VI's position callback role pair proves its exact idle-rooted lineage");

    PendingGuestSetCurrentContexts direct_retrace;
    passed &= expect(
        direct_retrace.push(vi_retrace) &&
            direct_retrace.has_exact_lineage(kViRetraceContext, kIdle),
        "VI's retrace callback role pair proves the VI33 idle-rooted lineage");
    passed &= expect(
        !direct_retrace.has_exact_lineage(kViRetraceContext + 8u, kIdle),
        "the observed D4 must equal the top temporary context exactly");
    passed &= expect(
        !direct_retrace.has_exact_lineage(kViRetraceContext, kThread),
        "a chain rooted at IdleContext cannot be relabeled as thread-rooted");

    PendingGuestSetCurrentContexts two_level;
    passed &= expect(
        two_level.push(vi_retrace) && two_level.push(nested) &&
            two_level.has_exact_lineage(kNestedContext, kIdle),
        "nested temporary contexts must form one uninterrupted LIFO chain to IdleContext");

    PendingGuestSetCurrentContexts discontinuous;
    passed &= expect(
        discontinuous.push(vi_retrace) &&
            discontinuous.push(GuestSetCurrentContextToken{
                kNestedContext,
                kViPositionContext,
                0x804C817Cu,
                0x804C81D4u}) &&
            !discontinuous.has_exact_lineage(kNestedContext, kIdle),
        "a discontinuous previous-context link cannot prove idle lineage");

    PendingGuestSetCurrentContexts unknown_install;
    passed &= expect(
        unknown_install.push(GuestSetCurrentContextToken{
            kViRetraceContext, kIdle, 0xDEADBEEFu, 0x804B1B88u}) &&
            !unknown_install.has_exact_lineage(kViRetraceContext, kIdle),
        "an unknown install LR invalidates the entire lineage");

    PendingGuestSetCurrentContexts wrong_restore;
    passed &= expect(
        wrong_restore.push(GuestSetCurrentContextToken{
            kViRetraceContext,
            kIdle,
            0x804B1768u,
            0x804B1748u}) &&
            !wrong_restore.has_exact_lineage(kViRetraceContext, kIdle),
        "an install token paired with the wrong restore LR is rejected");

    PendingGuestSetCurrentContexts thread_rooted;
    passed &= expect(
        thread_rooted.push(GuestSetCurrentContextToken{
            kViRetraceContext,
            kThread,
            0x804B1768u,
            0x804B1B88u}) &&
            !thread_rooted.has_exact_lineage(kViRetraceContext, kIdle),
        "a normal thread-rooted temporary context cannot use the idle exception rule");

    PendingGuestSetCurrentContexts residual_below_idle;
    passed &= expect(
        residual_below_idle.push(GuestSetCurrentContextToken{
            kIdle, kThread, 0x804C817Cu, 0x804C81D4u}) &&
            residual_below_idle.push(vi_retrace) &&
            !residual_below_idle.has_exact_lineage(
                kViRetraceContext, kIdle),
        "an apparent idle root with a residual lower token fails closed");

    PendingGuestSetCurrentContexts cyclic;
    passed &= expect(
        cyclic.push(GuestSetCurrentContextToken{
            kViRetraceContext,
            kViRetraceContext,
            0x804B1768u,
            0x804B1B88u}) &&
            !cyclic.has_exact_lineage(kViRetraceContext, kIdle),
        "a self-referential token cannot satisfy lineage");

    PendingGuestSetCurrentContexts misaligned;
    passed &= expect(
        misaligned.push(GuestSetCurrentContextToken{
            kViRetraceContext + 4u,
            kIdle,
            0x804B1768u,
            0x804B1B88u}) &&
            !misaligned.has_exact_lineage(kViRetraceContext + 4u, kIdle),
        "misaligned temporary contexts cannot satisfy lineage");
    return passed;
}

bool osset_rejects_unknown_or_malformed_transitions() {
    using galaxy::scheduler::classify_guest_set_current_context;

    constexpr std::uint32_t kThread = 0x80650878u;
    constexpr std::uint32_t kR1 = 0x90001000u;
    GuestSetCurrentContextEvidence evidence =
        set_context_evidence(0xDEADBEEFu, kR1 + 0x8u, kThread, kR1);
    bool passed = expect(
        classify_guest_set_current_context(evidence, nullptr) ==
            GuestSetCurrentContextDisposition::RejectUnknownRole,
        "an unknown OSSetCurrentContext caller hard-fails");

    evidence = set_context_evidence(0x804C817Cu, 0u, kThread, kR1);
    passed &= expect(
        classify_guest_set_current_context(evidence, nullptr) ==
            GuestSetCurrentContextDisposition::RejectNullRequestedContext,
        "a null OSSetCurrentContext target hard-fails");
    evidence = set_context_evidence(
        0x804C817Cu, kR1 + 0x4u, kThread, kR1);
    passed &= expect(
        classify_guest_set_current_context(evidence, nullptr) ==
            GuestSetCurrentContextDisposition::
                RejectMisalignedRequestedContext,
        "a misaligned OSContext target hard-fails");
    evidence = set_context_evidence(
        0x804C817Cu, kR1 + 0x8u, kThread, kR1);
    evidence.requested_context_is_mapped = false;
    passed &= expect(
        classify_guest_set_current_context(evidence, nullptr) ==
            GuestSetCurrentContextDisposition::RejectUnmappedRequestedContext,
        "the complete requested OSContext must be mapped");
    evidence = set_context_evidence(
        0x804C817Cu, kR1 + 0x8u, kThread, kR1);
    evidence.physical_context ^= 4u;
    passed &= expect(
        classify_guest_set_current_context(evidence, nullptr) ==
            GuestSetCurrentContextDisposition::RejectPhysicalContextAlias,
        "the pre-call D4/C0 pair must already be architecturally coherent");
    evidence = set_context_evidence(
        0x804C817Cu, kR1 + 0x8u, kThread, kR1);
    evidence.current_context_is_mapped = false;
    passed &= expect(
        classify_guest_set_current_context(evidence, nullptr) ==
            GuestSetCurrentContextDisposition::RejectUnmappedCurrentContext,
        "a non-bootstrap transition requires a mapped previous D4");
    evidence = set_context_evidence(
        0x804C817Cu, kR1 + 0x18u, kThread, kR1);
    passed &= expect(
        classify_guest_set_current_context(evidence, nullptr) ==
            GuestSetCurrentContextDisposition::RejectTemporaryAddress,
        "a temporary install must use its generated r1 offset exactly");
    evidence = set_context_evidence(
        0x804C817Cu, kR1 + 0x8u, kThread, kR1);
    evidence.token_stack_is_full = true;
    passed &= expect(
        classify_guest_set_current_context(evidence, nullptr) ==
            GuestSetCurrentContextDisposition::RejectTokenOverflow,
        "temporary-context nesting cannot overflow silently");
    evidence = set_context_evidence(
        0x804C81D4u, kThread, kR1 + 0x8u, kR1);
    passed &= expect(
        classify_guest_set_current_context(evidence, nullptr) ==
            GuestSetCurrentContextDisposition::RejectRestoreWithoutToken,
        "a restore without its exact install token hard-fails");

    PendingGuestSetCurrentContexts full;
    for (std::size_t i = 0u;
         i < PendingGuestSetCurrentContexts::kCapacity;
         ++i) {
        const GuestSetCurrentContextToken token{
            static_cast<std::uint32_t>(0x90000000u + i * 8u),
            kThread,
            static_cast<std::uint32_t>(0x80000000u + i * 8u),
            static_cast<std::uint32_t>(0x81000000u + i * 8u)};
        passed &= expect(full.push(token), "every in-capacity token is stored");
    }
    passed &= expect(
        full.full() &&
            !full.push(GuestSetCurrentContextToken{
                0x91000000u, kThread, 0x82000000u, 0x83000000u}),
        "the fixed token stack fails closed at its exact capacity");
    return passed;
}

bool osset_special_roles_are_exact() {
    using galaxy::scheduler::classify_guest_set_current_context;

    constexpr std::uint32_t kThread = 0x80650878u;
    constexpr std::uint32_t kIdle = 0x80650C90u;
    GuestSetCurrentContextEvidence evidence = set_context_evidence(
        0x804A6C58u, 0x80650540u, kThread, 0x806BDF00u);
    bool passed = expect(
        classify_guest_set_current_context(evidence, nullptr) ==
            GuestSetCurrentContextDisposition::ValidFatalContext,
        "OSFatal installs only RMGE01's exact static FatalContext");
    evidence.requested_context = 0x80650548u;
    passed &= expect(
        classify_guest_set_current_context(evidence, nullptr) ==
            GuestSetCurrentContextDisposition::RejectSpecialContext,
        "OSFatal cannot substitute a nearby mapped context");

    evidence = set_context_evidence(
        0x804AAC88u, kThread, 0u, 0x816FFFF0u);
    evidence.physical_context = 0u;
    evidence.current_thread = 0u;
    evidence.current_thread_state = 0u;
    evidence.current_context_is_mapped = false;
    evidence.current_thread_is_mapped = false;
    evidence.current_thread_is_active = false;
    passed &= expect(
        classify_guest_set_current_context(evidence, nullptr) ==
            GuestSetCurrentContextDisposition::ValidBootstrapThread,
        "__OSThreadInit installs DefaultThread from the zeroed bootstrap state");
    evidence.current_context = kIdle;
    evidence.physical_context = kIdle & 0x3FFFFFFFu;
    evidence.current_context_is_mapped = true;
    passed &= expect(
        classify_guest_set_current_context(evidence, nullptr) ==
            GuestSetCurrentContextDisposition::
                RejectBootstrapPreviousContext,
        "the bootstrap role cannot be replayed over a live context");

    evidence = set_context_evidence(
        0x804AB354u, kIdle, kThread, 0x806BDF00u);
    evidence.current_thread = 0u;
    evidence.current_thread_state = 0u;
    evidence.current_thread_is_mapped = false;
    evidence.current_thread_is_active = false;
    passed &= expect(
        classify_guest_set_current_context(evidence, nullptr) ==
            GuestSetCurrentContextDisposition::ValidSchedulerIdle,
        "SelectThread's exact IdleContext/E4-null window remains valid");
    evidence.current_thread = kThread;
    passed &= expect(
        classify_guest_set_current_context(evidence, nullptr) ==
            GuestSetCurrentContextDisposition::RejectIdleCurrentThread,
        "IdleContext cannot be installed while E4 names a thread");

    evidence = set_context_evidence(
        0x804AB410u, kThread, kIdle, 0x806BDF00u);
    passed &= expect(
        classify_guest_set_current_context(evidence, nullptr) ==
            GuestSetCurrentContextDisposition::ValidSchedulerThread,
        "SelectThread installs the exact running active E4 target");
    evidence.requested_context = 0x807ACCA0u;
    passed &= expect(
        classify_guest_set_current_context(evidence, nullptr) ==
            GuestSetCurrentContextDisposition::RejectThreadMismatch,
        "the scheduler target must equal E4 exactly");
    evidence = set_context_evidence(
        0x804AB410u, kThread, kIdle, 0x806BDF00u);
    evidence.current_thread_is_mapped = false;
    passed &= expect(
        classify_guest_set_current_context(evidence, nullptr) ==
            GuestSetCurrentContextDisposition::RejectUnmappedCurrentThread,
        "the selected E4 target must map a complete OSThread");
    evidence = set_context_evidence(
        0x804AB410u, kThread, kIdle, 0x806BDF00u);
    evidence.current_thread_is_active = false;
    passed &= expect(
        classify_guest_set_current_context(evidence, nullptr) ==
            GuestSetCurrentContextDisposition::RejectInactiveCurrentThread,
        "the selected E4 target must remain on the active queue");
    evidence = set_context_evidence(
        0x804AB410u, kThread, kIdle, 0x806BDF00u);
    evidence.current_thread_state = 1u;
    passed &= expect(
        classify_guest_set_current_context(evidence, nullptr) ==
            GuestSetCurrentContextDisposition::RejectNonRunningCurrentThread,
        "SelectThread may install only a running target");
    return passed;
}

bool alternate_stack_backchain_preserves_exact_owner() {
    using galaxy::scheduler::resolve_guest_stack_owner;

    constexpr std::uint32_t kThread = 0x80650878u;
    constexpr std::uint32_t kFiberFrame = 0x80661160u;
    constexpr std::uint32_t kFiberRoot = 0x80661178u;
    constexpr std::uint32_t kOwnedThreadFrame = 0x806BDF40u;
    const auto direct_owner = [](std::uint32_t frame) {
        return frame == kOwnedThreadFrame ? kThread : 0u;
    };
    const auto read_previous = [](std::uint32_t frame, std::uint32_t& previous) {
        if (frame == kFiberFrame) {
            previous = kFiberRoot;
            return true;
        }
        if (frame == kFiberRoot) {
            previous = kOwnedThreadFrame;
            return true;
        }
        return false;
    };

    bool passed = expect(
        resolve_guest_stack_owner(
            kFiberFrame, direct_owner, read_previous) == kThread,
        "the exact OSSwitchFiber backchain resolves to the displaced thread");
    passed &= expect(
        resolve_guest_stack_owner(
            kOwnedThreadFrame, direct_owner, read_previous) == kThread,
        "a normal thread-stack r1 resolves without walking a backchain");
    passed &= expect(
        resolve_guest_stack_owner(
            kFiberFrame + 4u, direct_owner, read_previous) == 0u,
        "a misaligned r1 cannot acquire ownership from a plausible frame");

    std::uint32_t cycle_reads = 0u;
    const auto cyclic_backchain =
        [&cycle_reads](std::uint32_t frame, std::uint32_t& previous) {
            ++cycle_reads;
            previous = frame == 0x1000u ? 0x2000u : 0x1000u;
            return true;
        };
    passed &= expect(
        resolve_guest_stack_owner(
            0x1000u,
            [](std::uint32_t) { return 0u; },
            cyclic_backchain) == 0u &&
            cycle_reads == 256u,
        "a cyclic alternate stack fails closed at the fixed iteration bound");
    return passed;
}

bool exact_lr_alias_chain_is_iterative() {
    constexpr std::array aliases{0x00000100u, 0x00000200u, 0x00000300u};
    std::vector<std::uint32_t> invoked;
    const auto normalize = [](std::uint32_t address) {
        return address & 0xFFFFFFFCu;
    };
    const auto lookup = [&](std::uint32_t address) {
        for (const auto& alias : aliases) {
            if (alias == address) {
                return &alias;
            }
        }
        return static_cast<const std::uint32_t*>(nullptr);
    };
    const auto invoke = [&](std::uint32_t address, const std::uint32_t* token) {
        invoked.push_back(address);
        if (token == nullptr || *token != address) {
            return 0xDEAD0000u;
        }
        switch (address) {
            case 0x00000100u:
                return 0x00000203u;  // bclr masks LR's low two bits.
            case 0x00000200u:
                return 0x00000300u;
            default:
                return 0x00000407u;
        }
    };
    const auto stop = galaxy::scheduler::run_flat_continuation_chain(
        0x00000103u,
        normalize,
        lookup,
        invoke,
        [](std::uint32_t) { return false; });

    return expect(
               invoked == std::vector<std::uint32_t>(
                   {0x00000100u, 0x00000200u, 0x00000300u}),
               "flat dispatch invokes exact LR aliases in architectural order") &&
        expect(
            stop.reason ==
                    FlatContinuationStopReason::MissingExactContinuation &&
                stop.initial_pc == 0x00000100u &&
                stop.previous_pc == 0x00000300u &&
                stop.next_pc == 0x00000404u &&
                stop.returned_lr == 0x00000407u &&
                stop.completed_hops == 3u,
            "flat dispatch reports the first missing exact alias without fallback");
}

bool missing_initial_alias_never_invokes() {
    std::uint32_t invocations = 0u;
    const auto stop = galaxy::scheduler::run_flat_continuation_chain(
        0x00001237u,
        [](std::uint32_t address) { return address & 0xFFFFFFFCu; },
        [](std::uint32_t) {
            return static_cast<const std::uint32_t*>(nullptr);
        },
        [&](std::uint32_t, const std::uint32_t*) {
            ++invocations;
            return 0u;
        },
        [](std::uint32_t) { return false; });
    return expect(
               invocations == 0u,
               "a missing initial exact alias cannot run an enclosing owner") &&
        expect(
            stop.reason ==
                    FlatContinuationStopReason::MissingExactContinuation &&
                stop.initial_pc == 0x00001234u &&
                stop.previous_pc == 0u && stop.next_pc == 0x00001234u &&
                stop.completed_hops == 0u,
            "initial exact-alias failure is distinguishable from a returned LR");
}

bool stack_top_sentinel_is_not_success() {
    constexpr std::uint32_t alias = 0x00000100u;
    const auto stop = galaxy::scheduler::run_flat_continuation_chain(
        alias,
        [](std::uint32_t address) { return address & 0xFFFFFFFCu; },
        [&](std::uint32_t address) {
            return address == alias
                ? &alias
                : static_cast<const std::uint32_t*>(nullptr);
        },
        [](std::uint32_t, const std::uint32_t*) { return 1u; },
        [](std::uint32_t returned_lr) { return returned_lr <= 1u; });
    return expect(
        stop.reason == FlatContinuationStopReason::StackTopSentinel &&
            stop.previous_pc == alias && stop.next_pc == 0u &&
            stop.returned_lr == 1u && stop.completed_hops == 1u,
        "a validated stack-top sentinel remains a loud terminal condition");
}

struct SyntheticGuestTransfer final {
    std::uint32_t address{};
};

bool architectural_transfer_propagates_before_next_target() {
    constexpr std::array aliases{0x00000100u, 0x00000300u};
    std::vector<std::uint32_t> invoked;
    bool frame_unwound = false;
    volatile bool transfer_requested = true;
    struct FrameGuard {
        bool& unwound;
        ~FrameGuard() {
            unwound = true;
        }
    };

    try {
        (void)galaxy::scheduler::run_flat_continuation_chain(
            aliases[0],
            [](std::uint32_t address) { return address & 0xFFFFFFFCu; },
            [&](std::uint32_t address) {
                for (const auto& alias : aliases) {
                    if (alias == address) {
                        return &alias;
                    }
                }
                return static_cast<const std::uint32_t*>(nullptr);
            },
            [&](std::uint32_t address, const std::uint32_t*) -> std::uint32_t {
                invoked.push_back(address);
                FrameGuard guard{frame_unwound};
                if (transfer_requested) {
                    throw SyntheticGuestTransfer{aliases[1]};
                }
                return 0u;
            },
            [](std::uint32_t) { return false; });
    } catch (const SyntheticGuestTransfer& transfer) {
        return expect(
                   transfer.address == aliases[1],
                   "the architectural transfer target propagates unchanged") &&
            expect(
                frame_unwound,
                "the abandoned translated frame unwinds before top-level restart") &&
            expect(
                invoked == std::vector<std::uint32_t>({aliases[0]}),
                "the flat chain never recursively invokes the transfer target");
    } catch (...) {
    }
    return expect(false, "architectural transfer must escape the LR-hop helper");
}

bool long_self_lr_chain_uses_constant_native_stack() {
    constexpr std::uint32_t alias = 0x00000100u;
    constexpr std::uint64_t kHops = 65'536u;
    std::uint64_t invocations = 0u;
    const auto stop = galaxy::scheduler::run_flat_continuation_chain(
        alias,
        [](std::uint32_t address) { return address & 0xFFFFFFFCu; },
        [&](std::uint32_t address) {
            return address == alias
                ? &alias
                : static_cast<const std::uint32_t*>(nullptr);
        },
        [&](std::uint32_t, const std::uint32_t*) {
            ++invocations;
            return invocations < kHops ? alias : 0x00000200u;
        },
        [](std::uint32_t) { return false; });
    return expect(
        invocations == kHops && stop.completed_hops == kHops &&
            stop.previous_pc == alias && stop.next_pc == 0x00000200u,
        "65,536 LR hops complete iteratively without recursive native frames");
}

}  // namespace

bool bounded_stack_owner_memo_preserves_order_and_invalidation() {
    using Memo = galaxy::scheduler::GuestStackOwnerMemo;
    Memo memo;
    memo.append(1, 0x80002000u, 0x80001000u);
    memo.append(2, 0x80003000u, 0x80001800u);
    if (memo.complete()) return false;
    memo.finish();
    if (!memo.complete() || memo.owner(0x80001900u) != 1u ||
        memo.owner(0x80002800u) != 2u || memo.owner(0x90000000u) != 0u)
        return false;
    memo.disable();
    memo.finish();
    if (memo.complete()) return false;
    // A fresh invocation must not inherit the previous roster's ownership.
    Memo next;
    for (std::uint32_t i = 0; i < 64; ++i)
        next.append(100u + i, 0x90001000u + i * 0x1000u,
                    0x90000000u + i * 0x1000u);
    next.finish();
    for (std::uint32_t i = 0; i < 64; ++i)
        if (next.owner(0x90000008u + i * 0x1000u) != 100u + i)
            return false;
    return true;
}

int main() {
    bool passed = true;
    passed &= rfi_uses_only_srr_pair();
    passed &= physical_rfi_aliases_bind_only_to_exact_static_continuations();
    passed &= guest_stack_spans_accept_exact_mem2_without_weakening_identity();
    passed &= pending_osload_is_consumed_exactly_once();
    passed &= scoped_osload_cleanup_preserves_unwind_and_failure();
    passed &= valid_guest_sleep_always_runs_translated_scheduler();
    passed &= guest_sleep_identity_is_strict_and_non_repairing();
    passed &= guest_exception_identity_is_strict_and_preserves_idle();
    passed &=
        guest_exception_identity_accepts_only_exact_idle_temporary_lineage();
    passed &= ossave_context_identity_is_exact_and_non_repairing();
    passed &= osset_role_table_is_exhaustive_and_exact();
    passed &= osset_temporary_contexts_are_nested_and_non_repairing();
    passed &= osset_idle_temporary_lineage_is_complete_and_role_validated();
    passed &= osset_rejects_unknown_or_malformed_transitions();
    passed &= osset_special_roles_are_exact();
    passed &= alternate_stack_backchain_preserves_exact_owner();
    passed &= bounded_stack_owner_memo_preserves_order_and_invalidation();
    passed &= exact_lr_alias_chain_is_iterative();
    passed &= missing_initial_alias_never_invokes();
    passed &= stack_top_sentinel_is_not_success();
    passed &= architectural_transfer_propagates_before_next_target();
    passed &= long_self_lr_chain_uses_constant_native_stack();
    if (!passed) {
        return 1;
    }
    std::cout << "flat scheduler dispatch contracts passed\n";
    return 0;
}
