#include "galaxy/dsp_timing_runtime.h"
#include "galaxy/dsp_timing_replay.h"
#include "galaxy/dsp_instruction_provenance.h"
#include "galaxy/gx/frame_completion.h"
#include "galaxy/native_dsp.h"
#include "galaxy/runtime_timeline.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <type_traits>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

galaxy::DspGeneratedInstructionIdentity standalone_provenance_identity(
    std::uint16_t fetch_address = 0x0123u) {
    galaxy::DspGeneratedInstructionIdentity identity{};
    identity.fetch_address = fetch_address;
    identity.opcode_word = 0x02bfu;
    identity.second_word = 0x4567u;
    identity.has_second_word = true;
    return identity;
}

galaxy::DspGeneratedInstructionIdentity parallel_provenance_identity(
    std::uint16_t fetch_address = 0x8123u) {
    galaxy::DspGeneratedInstructionIdentity identity{};
    identity.fetch_address = fetch_address;
    identity.opcode_word = 0x8401u;
    identity.bundle = galaxy::DspGeneratedInstructionBundleKind::Parallel;
    identity.parallel_primary_word = 0x8400u;
    identity.parallel_extension = 0x01u;
    identity.parallel_extension_mask = 0xffu;
    return identity;
}

bool raw_instruction_provenance_captures_exact_prestate_and_identity() {
    std::array<galaxy::DspRawInstructionBoundaryRecord, 4> storage{};
    galaxy::DspRawInstructionBoundaryObserver observer{storage};
    galaxy::DspContext context{};
    galaxy::DspLogicalStackDepthTracker logical_depths{};
    if (!expect(
            galaxy::dsp_logical_stack_depth_begin_trusted_reset_owner_epoch(
                context, logical_depths),
            "raw provenance starts from an exact logical-stack reset epoch")) {
        return false;
    }
    std::atomic<std::uint16_t> external_vector{0x000eu};
    context.raw_instruction_boundary_observer = &observer;
    context.host_external_interrupt = &external_vector;
    context.sr = 0x63a0u;
    context.cr = 0x00f1u;
    context.ar = {0x0001u, 0x0fffu, 0x1000u, 0xffffu};
    context.ix = {1, -1, 2, -2};
    context.wr = {0xffffu, 0x0fffu, 0x00ffu, 0x000fu};
    for (std::size_t stack = 0u; stack < 4u; ++stack) {
        constexpr std::array<std::uint8_t, 4> depths{0u, 1u, 0x1fu, 7u};
        for (std::uint8_t depth = 0u; depth < depths[stack]; ++depth) {
            galaxy::dsp_stack_push(context, stack, 0u);
        }
    }
    context.st = {0x1111u, 0x2222u, 0x3333u, 0x4444u};

    const auto standalone = standalone_provenance_identity();
    {
        galaxy::DspRawInstructionBoundaryScope scope{
            context.raw_instruction_boundary_observer,
            context,
            standalone};
        galaxy::dsp_raw_instruction_boundary_require_ready(context, scope);
        context.sr = 0u;
        context.cr = 0u;
        context.ar.fill(0u);
        context.ix.fill(0);
        context.wr.fill(0u);
        for (std::size_t stack = 0u; stack < 4u; ++stack) {
            while (logical_depths.depths[stack] != 0u) {
                (void)galaxy::dsp_stack_pop(context, stack);
            }
        }
        context.st.fill(0u);
        external_vector.store(
            galaxy::kDspNoPendingExternalInterrupt,
            std::memory_order_release);
        galaxy::dsp_raw_instruction_boundary_commit(context, scope);
    }

    bool passed = expect(
        observer.committed_count() == 1u && observer.records().size() == 1u,
        "raw instruction provenance publishes exactly once on explicit commit");
    const auto& first = observer.records()[0];
    passed &= expect(
        first.sequence == 1u && first.identity == standalone &&
            first.pre_state.status_register == 0x63a0u &&
            first.pre_state.control_register == 0x00f1u &&
            first.pre_state.address_registers ==
                std::array<std::uint16_t, 4>{
                    0x0001u, 0x0fffu, 0x1000u, 0xffffu} &&
            first.pre_state.index_registers ==
                std::array<std::int16_t, 4>{1, -1, 2, -2} &&
            first.pre_state.wrap_registers ==
                std::array<std::uint16_t, 4>{
                    0xffffu, 0x0fffu, 0x00ffu, 0x000fu} &&
            first.pre_state.stacks[0] ==
                galaxy::DspRawStackCursorState{0x1111u, 0u} &&
            first.pre_state.stacks[1] ==
                galaxy::DspRawStackCursorState{0x2222u, 1u} &&
            first.pre_state.stacks[2] ==
                galaxy::DspRawStackCursorState{0x3333u, 0x1fu} &&
            first.pre_state.stacks[3] ==
                galaxy::DspRawStackCursorState{0x4444u, 7u} &&
            first.pre_state.logical_stack_depths ==
                std::array<galaxy::DspRawLogicalStackDepthState, 4>{
                    galaxy::DspRawLogicalStackDepthState{
                        galaxy::DspRawLogicalStackDepthKind::Exact, 0u},
                    galaxy::DspRawLogicalStackDepthState{
                        galaxy::DspRawLogicalStackDepthKind::Exact, 1u},
                    galaxy::DspRawLogicalStackDepthState{
                        galaxy::DspRawLogicalStackDepthKind::Exact, 0x1fu},
                    galaxy::DspRawLogicalStackDepthState{
                        galaxy::DspRawLogicalStackDepthKind::Exact, 7u}} &&
            first.pre_state.logical_stack_epoch == 1u &&
            first.pre_state.external_vector ==
                galaxy::DspRawExternalVectorState{
                    galaxy::DspRawExternalVectorKind::PendingVector,
                    0x000eu} &&
            first.pre_state.accelerator ==
                galaxy::DspRawAcceleratorState::Unavailable,
        "raw instruction provenance freezes SR/CR/AR/IX/WR, stack tops, raw cursors, exact reset-epoch logical depths, external vector, and unavailable accelerator state at begin");

    const auto no_pending = standalone_provenance_identity(0x0125u);
    {
        galaxy::DspRawInstructionBoundaryScope scope{&observer, context, no_pending};
        galaxy::dsp_raw_instruction_boundary_require_ready(context, scope);
        galaxy::dsp_raw_instruction_boundary_commit(context, scope);
    }
    context.host_external_interrupt = nullptr;
    const auto unavailable = parallel_provenance_identity();
    {
        galaxy::DspRawInstructionBoundaryScope scope{
            &observer, context, unavailable};
        galaxy::dsp_raw_instruction_boundary_require_ready(context, scope);
        galaxy::dsp_raw_instruction_boundary_commit(context, scope);
    }
    passed &= expect(
        observer.records().size() == 3u &&
            observer.records()[1].sequence == 2u &&
            observer.records()[1].pre_state.external_vector ==
                galaxy::DspRawExternalVectorState{
                    galaxy::DspRawExternalVectorKind::NoPendingVector,
                    galaxy::kDspNoPendingExternalInterrupt} &&
            observer.records()[2].sequence == 3u &&
            observer.records()[2].identity == unavailable &&
            observer.records()[2].pre_state.external_vector ==
                galaxy::DspRawExternalVectorState{
                    galaxy::DspRawExternalVectorKind::Unavailable,
                    0u},
        "raw instruction provenance distinguishes pending, no-pending, and unavailable external-vector state and accepts exact parallel identity");

    auto zero_second_word = standalone_provenance_identity(0x0126u);
    zero_second_word.second_word = 0u;
    {
        galaxy::DspRawInstructionBoundaryScope scope{
            &observer, context, zero_second_word};
        galaxy::dsp_raw_instruction_boundary_require_ready(context, scope);
        galaxy::dsp_raw_instruction_boundary_commit(context, scope);
    }
    passed &= expect(
        observer.records().size() == 4u &&
            observer.records()[3].identity == zero_second_word &&
            observer.records()[3].identity.has_second_word &&
            observer.records()[3].identity.second_word == 0u,
        "raw instruction provenance preserves second-word presence independently from a zero value");

    galaxy::DspContext null_context{};
    null_context.stack_pointers[0] = 0xffu;
    galaxy::DspRawInstructionBoundaryScope null_scope{
        nullptr, null_context, standalone};
    galaxy::dsp_raw_instruction_boundary_require_ready(
        null_context, null_scope);
    galaxy::dsp_raw_instruction_boundary_commit(null_context, null_scope);
    passed &= expect(
        null_context.raw_instruction_boundary_observer == nullptr &&
            null_context.stack_pointers[0] == 0xffu &&
            observer.committed_count() == 4u,
        "null product observer is a strict no-op that does not capture or validate raw state");
    return passed;
}

bool sbset_eie_post_retirement_interrupt_stacks_fallthrough_once() {
    constexpr std::uint16_t instruction_address = 0x0123u;
    constexpr std::uint16_t fallthrough_address = 0x0124u;
    constexpr std::uint16_t interrupt_vector = 0x000eu;

    std::array<galaxy::DspRawInstructionBoundaryRecord, 1> storage{};
    galaxy::DspRawInstructionBoundaryObserver observer{storage};
    galaxy::DspContext context{};
    galaxy::DspLogicalStackDepthTracker logical_depths{};
    if (!expect(
            galaxy::dsp_logical_stack_depth_begin_trusted_reset_owner_epoch(
                context, logical_depths),
            "SBSET EIE regression starts from an exact logical-stack reset epoch")) {
        return false;
    }

    std::atomic<std::uint16_t> pending_interrupt{interrupt_vector};
    context.raw_instruction_boundary_observer = &observer;
    context.host_external_interrupt = &pending_interrupt;
    context.pc = instruction_address;

    galaxy::DspGeneratedInstructionIdentity identity{};
    identity.fetch_address = instruction_address;
    identity.opcode_word = 0x1305u;

    bool interrupt_accepted = false;
    try {
        galaxy::DspRawInstructionBoundaryScope scope{
            context.raw_instruction_boundary_observer, context, identity};
        galaxy::dsp_raw_instruction_boundary_require_ready(context, scope);
        context.sr = static_cast<std::uint16_t>(
            context.sr | galaxy::kDspSrExtIntEnable);
        galaxy::dsp_raw_instruction_boundary_commit(context, scope);
        context.pc = fallthrough_address;
        galaxy::dsp_accept_pending_external_interrupt(context);
    } catch (const galaxy::DspExternalInterruptRequested&) {
        interrupt_accepted = true;
    }

    const bool entry_is_exact =
        interrupt_accepted && context.pc == interrupt_vector &&
        context.st[0] == fallthrough_address &&
        (context.st[1] & galaxy::kDspSrExtIntEnable) != 0u &&
        logical_depths.depths[0] == 1u &&
        logical_depths.depths[1] == 1u && observer.committed_count() == 1u &&
        observer.records().size() == 1u && observer.records()[0].sequence == 1u &&
        observer.records()[0].identity == identity &&
        (observer.records()[0].pre_state.status_register &
         galaxy::kDspSrExtIntEnable) == 0u &&
        observer.records()[0].pre_state.external_vector ==
            galaxy::DspRawExternalVectorState{
                galaxy::DspRawExternalVectorKind::PendingVector,
                interrupt_vector} &&
        observer.failure() == galaxy::DspRawInstructionBoundaryFailure::None &&
        pending_interrupt.load(std::memory_order_acquire) ==
            galaxy::kDspNoPendingExternalInterrupt;
    if (!expect(
            entry_is_exact,
            "retired SBSET EIE commits once and PIINT stacks its exact fallthrough PC")) {
        return false;
    }

    context.sr = galaxy::dsp_stack_pop(context, 1u);
    context.pc = galaxy::dsp_stack_pop(context, 0u);
    return expect(
        context.pc == fallthrough_address &&
            (context.sr & galaxy::kDspSrExtIntEnable) != 0u &&
            logical_depths.depths[0] == 0u &&
            logical_depths.depths[1] == 0u &&
            observer.committed_count() == 1u,
        "RTI restores the SBSET EIE fallthrough without a second retirement");
}

bool raw_instruction_memory_operands_are_decoder_planned_and_frozen() {
    std::array<galaxy::DspRawInstructionBoundaryRecord, 3> storage{};
    galaxy::DspRawInstructionBoundaryObserver observer{storage};
    galaxy::DspContext context{};
    galaxy::DspLogicalStackDepthTracker logical_depths{};
    if (!expect(
            galaxy::dsp_logical_stack_depth_begin_trusted_reset_owner_epoch(
                context, logical_depths),
            "raw memory provenance starts from an exact logical-stack reset epoch")) {
        return false;
    }
    context.ar = {0x2201u, 0x1234u, 0x9001u, 0x23feu};
    context.cr = 0x00f1u;

    auto dual_identity = parallel_provenance_identity();
    dual_identity.opcode_word = 0x80d7u;
    dual_identity.parallel_primary_word = 0x8000u;
    dual_identity.parallel_extension = 0xd7u;
    galaxy::DspGeneratedMemoryOperandPlanSet dual_plan{};
    dual_plan.operand_count = 2u;
    dual_plan.operands[0] = {
        galaxy::DspGeneratedMemorySlot::ParallelPrimary,
        galaxy::DspGeneratedMemorySpace::Data,
        galaxy::DspGeneratedMemoryDirection::Read,
        galaxy::DspGeneratedMemoryAddressSourceKind::AddressRegister,
        0u};
    dual_plan.operands[1] = {
        galaxy::DspGeneratedMemorySlot::ParallelSecondary,
        galaxy::DspGeneratedMemorySpace::Data,
        galaxy::DspGeneratedMemoryDirection::Read,
        galaxy::DspGeneratedMemoryAddressSourceKind::AddressRegister,
        3u};
    {
        galaxy::DspRawInstructionBoundaryScope scope{
            &observer, context, dual_identity, dual_plan};
        galaxy::dsp_raw_instruction_boundary_require_ready(context, scope);
        context.ar[0] = 0u;
        context.ar[3] = 0xffffu;
        galaxy::dsp_raw_instruction_boundary_commit(context, scope);
    }

    auto direct_identity = standalone_provenance_identity(0x0124u);
    direct_identity.opcode_word = 0x2f55u;
    direct_identity.second_word = 0u;
    direct_identity.has_second_word = false;
    const galaxy::DspGeneratedMemoryOperandPlanSet direct_plan{
        {
            galaxy::DspGeneratedMemoryOperandPlan{
                galaxy::DspGeneratedMemorySlot::Standalone,
                galaxy::DspGeneratedMemorySpace::Data,
                galaxy::DspGeneratedMemoryDirection::Write,
                galaxy::DspGeneratedMemoryAddressSourceKind::DirectPage,
                0x55u},
            galaxy::DspGeneratedMemoryOperandPlan{},
            galaxy::DspGeneratedMemoryOperandPlan{},
        },
        static_cast<std::uint8_t>(1)};
    {
        galaxy::DspRawInstructionBoundaryScope scope{
            &observer, context, direct_identity, direct_plan};
        galaxy::dsp_raw_instruction_boundary_require_ready(context, scope);
        context.cr = 0u;
        galaxy::dsp_raw_instruction_boundary_commit(context, scope);
    }

    auto instruction_identity = standalone_provenance_identity(0x0125u);
    instruction_identity.opcode_word = 0x0212u;
    instruction_identity.second_word = 0u;
    instruction_identity.has_second_word = false;
    context.ar[2] = 0x9001u;
    galaxy::DspGeneratedMemoryOperandPlanSet instruction_plan{};
    instruction_plan.operand_count = 1u;
    instruction_plan.operands[0] = {
        galaxy::DspGeneratedMemorySlot::Standalone,
        galaxy::DspGeneratedMemorySpace::Instruction,
        galaxy::DspGeneratedMemoryDirection::Read,
        galaxy::DspGeneratedMemoryAddressSourceKind::AddressRegister,
        2u};
    {
        galaxy::DspRawInstructionBoundaryScope scope{
            &observer, context, instruction_identity, instruction_plan};
        galaxy::dsp_raw_instruction_boundary_require_ready(context, scope);
        context.ar[2] = 0x0001u;
        galaxy::dsp_raw_instruction_boundary_commit(context, scope);
    }

    bool passed = expect(
        observer.records().size() == 3u &&
            observer.records()[0].memory_operand_count == 2u &&
            observer.records()[0].memory_operands[0] ==
                galaxy::DspRawResolvedMemoryOperand{
                    galaxy::DspGeneratedMemorySlot::ParallelPrimary,
                    galaxy::DspGeneratedMemorySpace::Data,
                    galaxy::DspGeneratedMemoryDirection::Read,
                    0x2201u} &&
            observer.records()[0].memory_operands[1] ==
                galaxy::DspRawResolvedMemoryOperand{
                    galaxy::DspGeneratedMemorySlot::ParallelSecondary,
                    galaxy::DspGeneratedMemorySpace::Data,
                    galaxy::DspGeneratedMemoryDirection::Read,
                    0x23feu},
        "same-bank coalesced dual loads retain two decoder slots and freeze both distinct pre-state addresses before semantics");
    passed &= expect(
        observer.records()[1].pre_state.control_register == 0x00f1u &&
            observer.records()[1].memory_operand_count == 1u &&
            observer.records()[1].memory_operands[0].address == 0xf155u &&
            observer.records()[1].memory_operands[0].direction ==
                galaxy::DspGeneratedMemoryDirection::Write,
        "direct-page memory provenance freezes CR:imm8 from immutable pre-state");
    passed &= expect(
        observer.records()[2].memory_operand_count == 1u &&
            observer.records()[2].memory_operands[0].space ==
                galaxy::DspGeneratedMemorySpace::Instruction &&
            observer.records()[2].memory_operands[0].address == 0x9001u,
        "instruction-memory provenance retains the raw unmapped address from the decoded AR source");

    std::array<galaxy::DspRawInstructionBoundaryRecord, 1> trap_storage{};
    galaxy::DspRawInstructionBoundaryObserver trap_observer{trap_storage};
    bool trapped = false;
    try {
        galaxy::DspRawInstructionBoundaryScope scope{
            &trap_observer, context, instruction_identity, instruction_plan};
        galaxy::dsp_raw_instruction_boundary_require_ready(context, scope);
        galaxy::dsp_hard_trap_at_current(
            context, "raw memory provenance trap path");
    } catch (const galaxy::DspHardTrap&) {
        trapped = true;
    }
    passed &= expect(
        trapped && trap_observer.records().empty() && !trap_observer.failed(),
        "trap unwind never publishes a resolved memory operand");

    auto invalid_write = instruction_plan;
    invalid_write.operands[0].direction =
        galaxy::DspGeneratedMemoryDirection::Write;
    std::array<galaxy::DspRawInstructionBoundaryRecord, 1> invalid_storage{};
    galaxy::DspRawInstructionBoundaryObserver invalid_observer{invalid_storage};
    galaxy::DspRawInstructionBoundaryScope invalid_scope{
        &invalid_observer, context, instruction_identity, invalid_write};
    passed &= expect(
        !invalid_scope.ready() && invalid_observer.failure() ==
                galaxy::DspRawInstructionBoundaryFailure::
                    InvalidMemoryOperandPlan,
        "explicit instruction-memory writes are structurally unrepresentable");

    galaxy::DspGeneratedMemoryOperandPlanSet secondary_only{};
    secondary_only.operand_count = 1u;
    secondary_only.operands[0] = dual_plan.operands[1];
    std::array<galaxy::DspRawInstructionBoundaryRecord, 1>
        secondary_only_storage{};
    galaxy::DspRawInstructionBoundaryObserver secondary_only_observer{
        secondary_only_storage};
    galaxy::DspRawInstructionBoundaryScope secondary_only_scope{
        &secondary_only_observer,
        context,
        dual_identity,
        secondary_only};
    passed &= expect(
        !secondary_only_scope.ready() &&
            secondary_only_observer.failure() ==
                galaxy::DspRawInstructionBoundaryFailure::
                    InvalidMemoryOperandPlan,
        "parallel secondary memory metadata cannot omit its canonical primary slot");

    galaxy::DspContext null_context{};
    null_context.stack_pointers[0] = 0xffu;
    galaxy::DspRawInstructionBoundaryScope null_scope{
        nullptr, null_context, instruction_identity, invalid_write};
    passed &= expect(
        null_scope.ready() && null_scope.commit() &&
            null_context.stack_pointers[0] == 0xffu,
        "null product observer never resolves or validates generated memory metadata");
    return passed;
}

bool raw_instruction_provenance_lifecycle_is_owned_and_explicit() {
    const auto identity = standalone_provenance_identity();
    galaxy::DspContext context{};
    galaxy::DspLogicalStackDepthTracker logical_depths{};
    if (!expect(
            galaxy::dsp_logical_stack_depth_begin_trusted_reset_owner_epoch(
                context, logical_depths),
            "raw provenance lifecycle starts from a known reset epoch")) {
        return false;
    }
    const auto pre_state =
        galaxy::dsp_capture_raw_instruction_pre_state(context);
    bool passed = true;

    {
        std::array<galaxy::DspRawInstructionBoundaryRecord, 1> storage{};
        galaxy::DspRawInstructionBoundaryObserver observer{storage};
        galaxy::DspRawInstructionBoundaryScope scope{
            &observer, context, identity};
        passed &= expect(
            scope.ready() && scope.abort() &&
                observer.committed_count() == 0u && !observer.failed() &&
                !observer.active(),
            "explicit provenance abort cancels without publication or failure");
    }

    {
        std::array<galaxy::DspRawInstructionBoundaryRecord, 1> storage{};
        galaxy::DspRawInstructionBoundaryObserver observer{storage};
        bool trapped = false;
        try {
            galaxy::DspRawInstructionBoundaryScope scope{
                &observer, context, identity};
            galaxy::dsp_raw_instruction_boundary_require_ready(context, scope);
            galaxy::dsp_hard_trap_at_current(
                context, "provenance unwind test");
        } catch (const galaxy::DspHardTrap&) {
            trapped = true;
        }
        passed &= expect(
            trapped && observer.committed_count() == 0u &&
                !observer.active() && !observer.failed(),
            "hard-trap unwind cancels active provenance without publication");
    }

    {
        std::array<galaxy::DspRawInstructionBoundaryRecord, 1> storage{};
        galaxy::DspRawInstructionBoundaryObserver observer{storage};
        {
            galaxy::DspRawInstructionBoundaryScope scope{
                &observer, context, identity};
            passed &= expect(
                scope.ready(),
                "forgotten-commit provenance scope begins successfully");
        }
        passed &= expect(
            observer.committed_count() == 0u && !observer.active() &&
                observer.failure() ==
                    galaxy::DspRawInstructionBoundaryFailure::
                        UncommittedInstruction,
            "normal scope exit without explicit commit is sticky failure and never publishes");
    }

    {
        std::array<galaxy::DspRawInstructionBoundaryRecord, 2> storage{};
        galaxy::DspRawInstructionBoundaryObserver observer{storage};
        const auto first = observer.begin(identity, pre_state);
        passed &= expect(
            first.valid() && observer.commit(first),
            "first owner/epoch transaction commits");
        const auto second = observer.begin(identity, pre_state);
        passed &= expect(
            second.valid() && !observer.commit(first) && observer.active() &&
                observer.failure() ==
                    galaxy::DspRawInstructionBoundaryFailure::
                        StaleTransactionToken &&
                observer.cancel(
                    second,
                    galaxy::DspRawInstructionBoundaryCancellation::
                        ExplicitAbort),
            "stale epoch cannot commit a newer active transaction");
    }

    {
        std::array<galaxy::DspRawInstructionBoundaryRecord, 1> first_storage{};
        std::array<galaxy::DspRawInstructionBoundaryRecord, 1> second_storage{};
        galaxy::DspRawInstructionBoundaryObserver first_observer{
            first_storage};
        galaxy::DspRawInstructionBoundaryObserver second_observer{
            second_storage};
        const auto token = first_observer.begin(identity, pre_state);
        passed &= expect(
            token.valid() && !second_observer.commit(token) &&
                second_observer.failure() ==
                    galaxy::DspRawInstructionBoundaryFailure::
                        StaleTransactionToken &&
                first_observer.cancel(
                    token,
                    galaxy::DspRawInstructionBoundaryCancellation::
                        ExplicitAbort),
            "transaction token owner prevents cross-observer commit");
    }
    return passed;
}

bool raw_instruction_provenance_invalid_shapes_and_failures_trap() {
    const auto identity = standalone_provenance_identity();
    galaxy::DspContext context{};
    galaxy::DspLogicalStackDepthTracker logical_depths{};
    if (!expect(
            galaxy::dsp_logical_stack_depth_begin_trusted_reset_owner_epoch(
                context, logical_depths),
            "raw provenance failures start from a known reset epoch")) {
        return false;
    }
    const auto pre_state =
        galaxy::dsp_capture_raw_instruction_pre_state(context);
    bool passed = true;

    {
        constexpr auto last_epoch =
            galaxy::dsp_instruction_provenance_detail::
                checked_transaction_epoch_increment(
                    std::numeric_limits<std::uint64_t>::max() - 1u);
        constexpr auto epoch_overflow =
            galaxy::dsp_instruction_provenance_detail::
                checked_transaction_epoch_increment(
                    std::numeric_limits<std::uint64_t>::max());
        constexpr auto last_sequence =
            galaxy::dsp_instruction_provenance_detail::
                checked_record_sequence_increment(
                    std::numeric_limits<std::uint64_t>::max() - 1u);
        constexpr auto sequence_overflow =
            galaxy::dsp_instruction_provenance_detail::
                checked_record_sequence_increment(
                    std::numeric_limits<std::uint64_t>::max());
        static_assert(
            last_epoch.available &&
            last_epoch.next ==
                std::numeric_limits<std::uint64_t>::max());
        static_assert(
            !epoch_overflow.available && epoch_overflow.next == 0u);
        static_assert(
            last_sequence.available &&
            last_sequence.next ==
                std::numeric_limits<std::uint64_t>::max());
        static_assert(
            !sequence_overflow.available && sequence_overflow.next == 0u);
        passed &= expect(
            last_epoch.available && !epoch_overflow.available &&
                last_sequence.available && !sequence_overflow.available,
            "transaction epoch and record sequence increments have checked max-edge arithmetic");
    }

    {
        std::array<galaxy::DspRawInstructionBoundaryRecord, 1> storage{};
        galaxy::DspRawInstructionBoundaryObserver observer{storage};
        auto invalid = parallel_provenance_identity();
        invalid.parallel_primary_word = 0x8500u;
        passed &= expect(
            !observer.begin(invalid, pre_state).valid() &&
                observer.failure() ==
                    galaxy::DspRawInstructionBoundaryFailure::
                        InvalidInstructionIdentity,
            "parallel provenance identity rejects an inconsistent primary split");
    }

    {
        std::array<galaxy::DspRawInstructionBoundaryRecord, 1> storage{};
        galaxy::DspRawInstructionBoundaryObserver observer{storage};
        auto invalid = identity;
        invalid.has_second_word = false;
        invalid.second_word = 0x4567u;
        passed &= expect(
            !observer.begin(invalid, pre_state).valid() &&
                observer.failure() ==
                    galaxy::DspRawInstructionBoundaryFailure::
                        InvalidInstructionIdentity,
            "provenance identity rejects second-word bytes without presence");
    }

    {
        std::array<galaxy::DspRawInstructionBoundaryRecord, 1> storage{};
        galaxy::DspRawInstructionBoundaryObserver observer{storage};
        auto invalid = parallel_provenance_identity();
        invalid.has_second_word = true;
        passed &= expect(
            !observer.begin(invalid, pre_state).valid() &&
                observer.failure() ==
                    galaxy::DspRawInstructionBoundaryFailure::
                        InvalidInstructionIdentity,
            "parallel provenance identity rejects an impossible second word");
    }

    {
        std::array<galaxy::DspRawInstructionBoundaryRecord, 1> storage{};
        galaxy::DspRawInstructionBoundaryObserver observer{storage};
        auto invalid = identity;
        invalid.fetch_address = 0x1000u;
        passed &= expect(
            !observer.begin(invalid, pre_state).valid() &&
                observer.failure() ==
                    galaxy::DspRawInstructionBoundaryFailure::
                        InvalidInstructionIdentity,
            "provenance identity rejects a non-IRAM/non-IROM fetch address");
    }

    {
        std::array<galaxy::DspRawInstructionBoundaryRecord, 1> storage{};
        galaxy::DspRawInstructionBoundaryObserver observer{storage};
        auto invalid = pre_state;
        invalid.stacks[2].cursor = 0x20u;
        passed &= expect(
            !observer.begin(identity, invalid).valid() &&
                observer.failure() ==
                    galaxy::DspRawInstructionBoundaryFailure::
                        InvalidStackCursor,
            "provenance prestate rejects a cursor wider than the raw five-bit hardware field");
    }

    {
        std::array<galaxy::DspRawInstructionBoundaryRecord, 1> storage{};
        galaxy::DspRawInstructionBoundaryObserver observer{storage};
        auto invalid = pre_state;
        invalid.external_vector.vector = 0x000eu;
        passed &= expect(
            !observer.begin(identity, invalid).valid() &&
                observer.failure() ==
                    galaxy::DspRawInstructionBoundaryFailure::
                        InvalidExternalVectorState,
            "provenance prestate rejects bytes attached to unavailable external-vector state");
    }

    {
        std::array<galaxy::DspRawInstructionBoundaryRecord, 1> storage{};
        galaxy::DspRawInstructionBoundaryObserver observer{storage};
        auto invalid = pre_state;
        invalid.accelerator =
            static_cast<galaxy::DspRawAcceleratorState>(0xffu);
        passed &= expect(
            !observer.begin(identity, invalid).valid() &&
                observer.failure() ==
                    galaxy::DspRawInstructionBoundaryFailure::
                        InvalidAcceleratorState,
            "provenance prestate rejects unknown accelerator availability state");
    }

    {
        std::array<galaxy::DspRawInstructionBoundaryRecord, 0> storage{};
        galaxy::DspRawInstructionBoundaryObserver observer{storage};
        context.raw_instruction_boundary_observer = &observer;
        bool trapped = false;
        try {
            galaxy::DspRawInstructionBoundaryScope scope{
                context.raw_instruction_boundary_observer,
                context,
                identity};
            galaxy::dsp_raw_instruction_boundary_require_ready(context, scope);
        } catch (const galaxy::DspHardTrap&) {
            trapped = true;
        }
        passed &= expect(
            trapped && observer.failure() ==
                    galaxy::DspRawInstructionBoundaryFailure::
                        RecordCapacityExceeded &&
                observer.committed_count() == 0u && !observer.active(),
            "installed observer begin failure hard-traps and never publishes");
    }

    {
        std::array<galaxy::DspRawInstructionBoundaryRecord, 1> storage{};
        galaxy::DspRawInstructionBoundaryObserver observer{storage};
        context.raw_instruction_boundary_observer = &observer;
        bool trapped = false;
        try {
            galaxy::DspRawInstructionBoundaryScope scope{
                context.raw_instruction_boundary_observer,
                context,
                identity};
            galaxy::dsp_raw_instruction_boundary_require_ready(context, scope);
            const auto nested = observer.begin(identity, pre_state);
            passed &= expect(
                !nested.valid() && observer.failure() ==
                        galaxy::DspRawInstructionBoundaryFailure::NestedBegin,
                "nested begin becomes sticky before commit failure test");
            galaxy::dsp_raw_instruction_boundary_commit(context, scope);
        } catch (const galaxy::DspHardTrap&) {
            trapped = true;
        }
        passed &= expect(
            trapped && observer.failure() ==
                    galaxy::DspRawInstructionBoundaryFailure::NestedBegin &&
                observer.committed_count() == 0u && !observer.active(),
            "installed observer commit failure hard-traps and unwind cancels without publication");
    }
    return passed;
}

bool logical_stack_depth_tracking_is_reset_bound_and_non_intrusive() {
    galaxy::DspContext ordinary{};
    galaxy::DspContext instrumented{};
    galaxy::DspLogicalStackDepthTracker tracker{};
    bool passed = expect(
        ordinary.logical_stack_depth_tracker == nullptr &&
            galaxy::dsp_logical_stack_depth_begin_trusted_reset_owner_epoch(
                instrumented, tracker),
        "logical stack tracking is absent by default and attaches only at a known reset");

    for (std::uint16_t value = 1u; value <= 40u; ++value) {
        galaxy::dsp_stack_push(ordinary, 0u, value);
        galaxy::dsp_stack_push(instrumented, 0u, value);
    }
    for (std::uint8_t index = 0u; index < 8u; ++index) {
        passed &= expect(
            galaxy::dsp_stack_pop(ordinary, 0u) ==
                galaxy::dsp_stack_pop(instrumented, 0u),
            "tracked and ordinary stack pops return identical hardware values");
    }
    passed &= expect(
        ordinary.st == instrumented.st &&
            ordinary.stack == instrumented.stack &&
            ordinary.stack_pointers == instrumented.stack_pointers &&
            tracker.depths[0] == 32u &&
            instrumented.stack_pointers[0] == 0u &&
            galaxy::dsp_logical_stack_depths_are_exact(instrumented),
        "logical depth 32 remains exact while the independent five-bit hardware cursor wraps to zero");

    for (std::uint8_t index = 0u; index < 32u; ++index) {
        (void)galaxy::dsp_stack_pop(ordinary, 0u);
        (void)galaxy::dsp_stack_pop(instrumented, 0u);
    }
    passed &= expect(
        tracker.depths[0] == 0u &&
            galaxy::dsp_logical_stack_depths_are_exact(instrumented),
        "balanced tracked pushes and pops return to exact empty depth");

    const auto ordinary_underflow = galaxy::dsp_stack_pop(ordinary, 0u);
    const auto tracked_underflow = galaxy::dsp_stack_pop(instrumented, 0u);
    passed &= expect(
        ordinary_underflow == tracked_underflow &&
            ordinary.st == instrumented.st &&
            ordinary.stack == instrumented.stack &&
            ordinary.stack_pointers == instrumented.stack_pointers &&
            tracker.availability[0] ==
                galaxy::DspLogicalStackDepthAvailability::Unavailable,
        "legal hardware pop-underflow completes identically and only makes logical evidence unavailable");
    galaxy::dsp_stack_push(ordinary, 0u, 0x7777u);
    galaxy::dsp_stack_push(instrumented, 0u, 0x7777u);
    passed &= expect(
        ordinary.st == instrumented.st &&
            ordinary.stack == instrumented.stack &&
            ordinary.stack_pointers == instrumented.stack_pointers &&
            tracker.availability[0] ==
                galaxy::DspLogicalStackDepthAvailability::Unavailable,
        "hardware execution continues unchanged after sticky evidence loss");

    galaxy::DspContext overflow_context{};
    galaxy::DspLogicalStackDepthTracker overflow_tracker{};
    passed &= expect(
        galaxy::dsp_logical_stack_depth_begin_trusted_reset_owner_epoch(
            overflow_context, overflow_tracker),
        "overflow test starts from an exact reset epoch");
    for (std::uint16_t count = 0u; count < 256u; ++count) {
        galaxy::dsp_stack_push(overflow_context, 2u, count);
    }
    passed &= expect(
        overflow_context.stack_pointers[2] == 0u &&
            overflow_context.st[2] == 255u &&
            overflow_tracker.depths[2] == 255u &&
            overflow_tracker.availability[2] ==
                galaxy::DspLogicalStackDepthAvailability::Unavailable,
        "the 256th push completes in hardware while u8 logical-depth overflow becomes unavailable");

    galaxy::DspContext interrupt_context{};
    galaxy::DspLogicalStackDepthTracker interrupt_tracker{};
    passed &= expect(
        galaxy::dsp_logical_stack_depth_begin_trusted_reset_owner_epoch(
            interrupt_context, interrupt_tracker),
        "external-interrupt test starts from an exact reset epoch");
    interrupt_context.sr = galaxy::kDspSrExtIntEnable;
    interrupt_context.pc = 0x0123u;
    passed &= expect(
        galaxy::dsp_enter_external_interrupt(interrupt_context, 0x000eu) &&
            interrupt_tracker.depths[0] == 1u &&
            interrupt_tracker.depths[1] == 1u &&
            interrupt_tracker.depths[2] == 0u &&
            interrupt_tracker.depths[3] == 0u,
        "external interrupt entry uses the same exact call/data stack bookkeeping");

    galaxy::DspContext qualification_context{};
    galaxy::DspLogicalStackDepthTracker qualification_tracker{};
    passed &= expect(
        galaxy::dsp_logical_stack_depth_begin_trusted_reset_owner_epoch(
            qualification_context, qualification_tracker),
        "observer qualification test starts from an exact reset epoch");
    std::array<galaxy::DspRawInstructionBoundaryRecord, 1> records{};
    galaxy::DspRawInstructionBoundaryObserver observer{records};
    {
        galaxy::DspRawInstructionBoundaryScope scope{
            &observer,
            qualification_context,
            standalone_provenance_identity()};
        passed &= expect(scope.ready(), "observer binds the exact tracker owner and epoch");
        (void)galaxy::dsp_stack_pop(qualification_context, 3u);
        passed &= expect(
            !scope.commit() && observer.failure() ==
                    galaxy::DspRawInstructionBoundaryFailure::
                        LogicalStackDepthUnavailable &&
                observer.committed_count() == 0u,
            "underflow during an instruction disqualifies instrumentation without publishing");
    }

    galaxy::DspContext epoch_context{};
    galaxy::DspLogicalStackDepthTracker epoch_tracker{};
    passed &= expect(
        galaxy::dsp_logical_stack_depth_begin_trusted_reset_owner_epoch(
            epoch_context, epoch_tracker),
        "epoch-change test starts from an exact reset epoch");
    std::array<galaxy::DspRawInstructionBoundaryRecord, 1> epoch_records{};
    galaxy::DspRawInstructionBoundaryObserver epoch_observer{epoch_records};
    {
        galaxy::DspRawInstructionBoundaryScope scope{
            &epoch_observer,
            epoch_context,
            standalone_provenance_identity()};
        ++epoch_tracker.epoch;
        passed &= expect(
            !scope.commit() && epoch_observer.failure() ==
                    galaxy::DspRawInstructionBoundaryFailure::
                        LogicalStackEpochChanged &&
                epoch_observer.committed_count() == 0u,
            "observer commit rechecks tracker owner and epoch before publication");
    }

    auto detached_context = std::make_unique<galaxy::DspContext>();
    galaxy::DspLogicalStackDepthTracker detached_tracker{};
    passed &= expect(
        galaxy::dsp_logical_stack_depth_begin_trusted_reset_owner_epoch(
            *detached_context, detached_tracker),
        "detach test starts from an exact reset epoch");
    std::array<galaxy::DspRawInstructionBoundaryRecord, 1> detached_records{};
    galaxy::DspRawInstructionBoundaryObserver detached_observer{
        detached_records};
    {
        galaxy::DspRawInstructionBoundaryScope scope{
            &detached_observer,
            *detached_context,
            standalone_provenance_identity()};
        detached_context->logical_stack_depth_tracker = nullptr;
        passed &= expect(
            !scope.commit() && detached_observer.failure() ==
                    galaxy::DspRawInstructionBoundaryFailure::
                        LogicalStackEpochChanged &&
                detached_observer.committed_count() == 0u,
            "observer commit rejects sidecar detach or pointer replacement with zero publication");
    }

    auto tracker_owner = std::make_unique<galaxy::DspContext>();
    galaxy::DspLogicalStackDepthTracker mismatched_owner_tracker{};
    passed &= expect(
        galaxy::dsp_logical_stack_depth_begin_trusted_reset_owner_epoch(
            *tracker_owner, mismatched_owner_tracker),
        "owner-mismatch test starts from an exact reset epoch");
    auto mismatched_owner =
        std::make_unique<galaxy::DspContext>(*tracker_owner);
    auto mismatch_baseline =
        std::make_unique<galaxy::DspContext>(*tracker_owner);
    mismatch_baseline->logical_stack_depth_tracker = nullptr;
    galaxy::dsp_stack_push(*mismatched_owner, 1u, 0x55aau);
    galaxy::dsp_stack_push(*mismatch_baseline, 1u, 0x55aau);
    passed &= expect(
        mismatched_owner->st == mismatch_baseline->st &&
            mismatched_owner->stack == mismatch_baseline->stack &&
            mismatched_owner->stack_pointers ==
                mismatch_baseline->stack_pointers &&
            mismatched_owner_tracker.availability[0] ==
                galaxy::DspLogicalStackDepthAvailability::Unavailable &&
            mismatched_owner_tracker.availability[3] ==
                galaxy::DspLogicalStackDepthAvailability::Unavailable,
        "owner mismatch preserves hardware mutation and only makes every logical-depth channel unavailable");

    galaxy::DspContext reset_context{};
    galaxy::DspLogicalStackDepthTracker reset_tracker{};
    passed &= expect(
        galaxy::dsp_logical_stack_depth_begin_trusted_reset_owner_epoch(
            reset_context, reset_tracker),
        "reset epoch baseline attaches");
    reset_context = galaxy::DspContext{};
    const auto prior_epoch = reset_tracker.epoch;
    passed &= expect(
        reset_context.logical_stack_depth_tracker == nullptr &&
            galaxy::dsp_logical_stack_depth_begin_trusted_reset_owner_epoch(
                reset_context, reset_tracker) &&
            reset_tracker.epoch == prior_epoch + 1u,
        "DspContext reset detaches the sidecar and reuse requires a new nonzero epoch");

    galaxy::DspContext other_owner{};
    passed &= expect(
        !galaxy::dsp_logical_stack_depth_begin_trusted_reset_owner_epoch(
            other_owner, reset_tracker) &&
            other_owner.logical_stack_depth_tracker == nullptr &&
            reset_tracker.owner == &reset_context &&
            galaxy::dsp_logical_stack_depths_are_exact(reset_context),
        "one tracker cannot be rebound across live context owners");

    galaxy::DspContext aliased_zero_cursor{};
    for (std::uint8_t count = 0u; count < 32u; ++count) {
        galaxy::dsp_stack_push(aliased_zero_cursor, 0u, count);
    }
    passed &= expect(
        aliased_zero_cursor.stack_pointers[0] == 0u &&
            aliased_zero_cursor.logical_stack_depth_tracker == nullptr,
        "32 untracked pushes alias a zero hardware cursor, documenting why only the trusted reset owner may attach tracking");

    galaxy::DspContext rejected_late_attach{};
    galaxy::DspLogicalStackDepthTracker rejected_tracker{};
    rejected_late_attach.stack_pointers[0] = 1u;
    passed &= expect(
        !galaxy::dsp_logical_stack_depth_begin_trusted_reset_owner_epoch(
            rejected_late_attach, rejected_tracker) &&
            rejected_late_attach.logical_stack_depth_tracker == nullptr,
        "a nonzero hardware cursor rejects attachment as defense in depth without being treated as depth proof");

    galaxy::DspContext exhausted_epoch_context{};
    galaxy::DspLogicalStackDepthTracker exhausted_epoch_tracker{};
    exhausted_epoch_tracker.epoch =
        std::numeric_limits<std::uint64_t>::max();
    passed &= expect(
        !galaxy::dsp_logical_stack_depth_begin_trusted_reset_owner_epoch(
            exhausted_epoch_context, exhausted_epoch_tracker) &&
            exhausted_epoch_context.logical_stack_depth_tracker == nullptr &&
            exhausted_epoch_tracker.owner == nullptr &&
            exhausted_epoch_tracker.availability[0] ==
                galaxy::DspLogicalStackDepthAvailability::Unavailable,
        "logical tracker epoch exhaustion fails closed without attaching or wrapping");

    reset_context.accelerator_reads_stopped = true;
    passed &= expect(
        galaxy::dsp_capture_raw_instruction_pre_state(reset_context).accelerator ==
            galaxy::DspRawAcceleratorState::Unavailable,
        "accelerator stopped-read state is never promoted into a fabricated pending exception latch");
    return passed;
}

template <typename Ledger>
concept HasUntrustedRetirementStateReset = requires(
    Ledger& ledger,
    galaxy::dsp_timing_runtime::DspTimingRetirementState state) {
    ledger.reset(state);
};

template <typename Accumulator>
concept HasUnauthenticatedCycleRestore = requires(
    Accumulator& accumulator,
    galaxy::dsp_timing_runtime::DspCycleGrantState state) {
    accumulator.restore(state);
};

template <typename Session>
concept HasReplayResetSurface = requires(Session& session) {
    session.reset();
};

template <typename Session>
concept HasReplayLedgerAccessor = requires(Session& session) {
    session.ledger();
};

galaxy::dsp_timing_runtime::DspTimingInstructionPlan timing_plan(
    std::uint16_t fetch_address,
    std::uint8_t expected_memory_slots = 0u) {
    namespace timing = galaxy::dsp_timing_runtime;

    galaxy::dsp_timing_runtime::DspTimingInstructionPlan plan{};
    plan.identity.fetch_address = fetch_address;
    plan.identity.opcode_word = 0x0000u;
    plan.binding.contract_version =
        timing::kDspTimingRuntimePlanContractVersion;
    plan.binding.resolver_plan_identity[0] = 0xa5u;
    plan.binding.resolver_plan_identity[18] =
        static_cast<std::uint8_t>(fetch_address >> 8u);
    plan.binding.resolver_plan_identity[19] =
        static_cast<std::uint8_t>(fetch_address);
    plan.binding.instruction_word_count = 1u;
    plan.binding.validated_bundle = timing::DspTimingBundleKind::Standalone;
    plan.binding.instruction_semantic =
        timing::DspTimingInstructionSemantic::Ordinary;
    plan.binding.hardware_loop_site =
        timing::DspTimingHardwareLoopSite::NotLoopEnd;
    plan.expected_memory_slots = expected_memory_slots;
    for (std::size_t index = 0u; index < plan.memory_operands.size(); ++index) {
        if ((expected_memory_slots & static_cast<std::uint8_t>(1u << index)) !=
            0u) {
            plan.memory_operands[index] =
                timing::DspTimingMemoryOperandAuthority{
                    timing::DspTimingMemorySpace::Data,
                    timing::DspTimingMemoryDirection::Read};
        }
    }
    plan.allowed_instruction_outcomes =
        timing::kDspTimingCompletedOutcomeBit;
    plan.allowed_hardware_loop_outcomes =
        timing::kDspTimingNoHardwareLoopOutcomeBit;
    return plan;
}

galaxy::dsp_timing_runtime::DspTimingAuthorizedPlanEntry authorized_entry(
    const galaxy::dsp_timing_runtime::DspTimingInstructionPlan& plan) {
    return {
        plan.binding.contract_version,
        plan.binding.resolver_plan_identity,
    };
}

void set_timing_semantic(
    galaxy::dsp_timing_runtime::DspTimingInstructionPlan& plan,
    galaxy::dsp_timing_runtime::DspTimingInstructionSemantic semantic) {
    namespace timing = galaxy::dsp_timing_runtime;

    plan.binding.instruction_semantic = semantic;
    switch (semantic) {
    case timing::DspTimingInstructionSemantic::Ordinary:
        plan.allowed_instruction_outcomes =
            timing::kDspTimingCompletedOutcomeBit;
        break;
    case timing::DspTimingInstructionSemantic::ConditionalAlways:
        plan.allowed_instruction_outcomes =
            timing::kDspTimingConditionSatisfiedOutcomeBit;
        break;
    case timing::DspTimingInstructionSemantic::ConditionalDynamic:
        plan.allowed_instruction_outcomes =
            timing::kDspTimingConditionSatisfiedOutcomeBit |
            timing::kDspTimingConditionNotSatisfiedOutcomeBit;
        break;
    case timing::DspTimingInstructionSemantic::LoopAlwaysArmed:
        plan.allowed_instruction_outcomes =
            timing::kDspTimingLoopArmedOutcomeBit;
        break;
    case timing::DspTimingInstructionSemantic::LoopAlwaysSkipped:
        plan.allowed_instruction_outcomes =
            timing::kDspTimingZeroCountLoopSkippedOutcomeBit;
        break;
    case timing::DspTimingInstructionSemantic::LoopDynamic:
        plan.allowed_instruction_outcomes =
            timing::kDspTimingLoopArmedOutcomeBit |
            timing::kDspTimingZeroCountLoopSkippedOutcomeBit;
        break;
    case timing::DspTimingInstructionSemantic::Halt:
        plan.allowed_instruction_outcomes =
            timing::kDspTimingHaltedOutcomeBit;
        break;
    }
}

void set_hardware_loop_site(
    galaxy::dsp_timing_runtime::DspTimingInstructionPlan& plan,
    galaxy::dsp_timing_runtime::DspTimingHardwareLoopSite site) {
    namespace timing = galaxy::dsp_timing_runtime;

    plan.binding.hardware_loop_site = site;
    plan.allowed_hardware_loop_outcomes =
        site == timing::DspTimingHardwareLoopSite::NotLoopEnd
        ? timing::kDspTimingNoHardwareLoopOutcomeBit
        : timing::kDspTimingAllHardwareLoopOutcomeBits;
}

galaxy::dsp_timing_runtime::DspTimingInstructionPlan parallel_timing_plan(
    std::uint16_t fetch_address,
    std::uint8_t expected_memory_slots) {
    namespace timing = galaxy::dsp_timing_runtime;

    auto plan = timing_plan(fetch_address, expected_memory_slots);
    const bool single_memory = expected_memory_slots ==
        timing::kDspTimingParallelPrimaryMemoryBit;
    const bool dual_memory = expected_memory_slots ==
        static_cast<std::uint8_t>(
            timing::kDspTimingParallelPrimaryMemoryBit |
            timing::kDspTimingParallelSecondaryMemoryBit);
    plan.identity.opcode_word = dual_memory
        ? 0x80aeu
        : single_memory ? 0x803bu : 0x8401u;
    plan.identity.bundle = timing::DspTimingBundleKind::Parallel;
    plan.identity.parallel_primary_word =
        dual_memory || single_memory ? 0x8000u : 0x8400u;
    plan.identity.parallel_extension = dual_memory
        ? 0xaeu
        : single_memory ? 0x3bu : 0x01u;
    plan.identity.parallel_extension_mask = 0xffu;
    plan.binding.validated_bundle = timing::DspTimingBundleKind::Parallel;
    if (single_memory) {
        plan.memory_operands[1] = timing::DspTimingMemoryOperandAuthority{
            timing::DspTimingMemorySpace::Data,
            timing::DspTimingMemoryDirection::Write};
    } else if (dual_memory) {
        plan.memory_operands[1] = timing::DspTimingMemoryOperandAuthority{
            timing::DspTimingMemorySpace::Data,
            timing::DspTimingMemoryDirection::Write};
        plan.memory_operands[2] = timing::DspTimingMemoryOperandAuthority{
            timing::DspTimingMemorySpace::Data,
            timing::DspTimingMemoryDirection::Read};
    }
    return plan;
}

galaxy::dsp_timing_runtime::DspTimingPreState timing_pre_state(
    const galaxy::dsp_timing_runtime::DspTimingInstructionPlan& plan) {
    galaxy::dsp_timing_runtime::DspTimingPreState state{};
    state.fetch_address = plan.identity.fetch_address;
    state.status_register = 0x63a0u;
    state.control_register = 0x00f1u;
    state.address_registers = {0x0001u, 0x0fffu, 0x1000u, 0xffffu};
    state.index_registers = {1, -1, 2, -2};
    state.wrap_registers = {0xffffu, 0x0fffu, 0x00ffu, 0x000fu};
    state.stack_depths = {1u, 2u, 3u, 4u};
    return state;
}

galaxy::DspGeneratedInstructionIdentity replay_raw_identity(
    const galaxy::dsp_timing_runtime::DspTimingInstructionPlan& plan) {
    galaxy::DspGeneratedInstructionIdentity identity{};
    identity.fetch_address = plan.identity.fetch_address;
    identity.opcode_word = plan.identity.opcode_word;
    identity.second_word = plan.identity.immediate_word;
    identity.has_second_word = plan.identity.has_immediate;
    identity.bundle = plan.identity.bundle ==
            galaxy::dsp_timing_runtime::DspTimingBundleKind::Standalone
        ? galaxy::DspGeneratedInstructionBundleKind::Standalone
        : galaxy::DspGeneratedInstructionBundleKind::Parallel;
    identity.parallel_primary_word = plan.identity.parallel_primary_word;
    identity.parallel_extension = plan.identity.parallel_extension;
    identity.parallel_extension_mask = plan.identity.parallel_extension_mask;
    return identity;
}

galaxy::DspRawInstructionBoundaryRecord replay_raw_record(
    const galaxy::dsp_timing_runtime::DspTimingInstructionPlan& plan,
    std::uint64_t sequence,
    galaxy::DspRawExternalVectorKind external_kind =
        galaxy::DspRawExternalVectorKind::NoPendingVector,
    std::uint16_t external_vector =
        galaxy::kDspNoPendingExternalInterrupt) {
    galaxy::DspRawInstructionBoundaryRecord record{};
    record.sequence = sequence;
    record.identity = replay_raw_identity(plan);
    record.pre_state.status_register = 0x63a0u;
    record.pre_state.control_register = 0x00f1u;
    record.pre_state.address_registers = {
        0x0001u, 0x0fffu, 0x1000u, 0xffffu};
    record.pre_state.index_registers = {1, -1, 2, -2};
    record.pre_state.wrap_registers = {
        0xffffu, 0x0fffu, 0x00ffu, 0x000fu};
    constexpr std::array<std::uint8_t, 4> depths{1u, 2u, 3u, 4u};
    for (std::size_t index = 0u; index < depths.size(); ++index) {
        record.pre_state.stacks[index] = {
            static_cast<std::uint16_t>(0x1100u + index), depths[index]};
        record.pre_state.logical_stack_depths[index] = {
            galaxy::DspRawLogicalStackDepthKind::Exact, depths[index]};
    }
    record.pre_state.logical_stack_epoch = 7u;
    record.pre_state.external_vector = {external_kind, external_vector};
    record.pre_state.accelerator =
        galaxy::DspRawAcceleratorState::Unavailable;
    return record;
}

galaxy::dsp_timing_replay::DspTimingReplaySupplement replay_supplement() {
    galaxy::dsp_timing_replay::DspTimingReplaySupplement supplement{};
    supplement.accelerator = galaxy::dsp_timing_replay::
        DspTimingReplayAcceleratorEvidence::NoPending;
    return supplement;
}

bool timing_replay_maps_raw_provenance_exactly() {
    namespace replay = galaxy::dsp_timing_replay;
    namespace timing = galaxy::dsp_timing_runtime;

    replay::DspTimingReplaySession session;

    auto first_plan = timing_plan(
        0x0300u, timing::kDspTimingStandaloneMemoryBit);
    first_plan.identity.opcode_word = 0x00c0u;
    auto first_raw = replay_raw_record(
        first_plan,
        1u,
        galaxy::DspRawExternalVectorKind::PendingVector,
        0x000eu);
    first_raw.memory_operand_count = 1u;
    first_raw.memory_operands[0] = {
        galaxy::DspGeneratedMemorySlot::Standalone,
        galaxy::DspGeneratedMemorySpace::Data,
        galaxy::DspGeneratedMemoryDirection::Read,
        0xf012u};
    auto first_supplement = replay_supplement();
    first_supplement.post_outcome.interrupt =
        timing::DspTimingInterruptOutcome::AcceptedAfterInstruction;
    first_supplement.accepted_interrupt =
        replay::DspTimingReplayAcceptedInterrupt::External;
    first_supplement.accepted_external_vector = 0x000eu;
    const auto first = session.replay(
        first_raw,
        first_plan,
        authorized_entry(first_plan),
        first_supplement);

    bool passed = expect(
        first.has_value() && !session.failed() && !session.ledger_failed() &&
            !session.ledger_active() && session.completed_raw_sequence() == 1u &&
            session.logical_stack_epoch() == 7u &&
            session.successful_commits() == 1u &&
            session.cancellation_attempts() == 0u,
        "offline timing replay commits one exact authorized raw record");
    passed &= expect(
        first.has_value() && first->raw_sequence == 1u &&
            first->raw_logical_stack_epoch == 7u &&
            first->raw_external_vector == first_raw.pre_state.external_vector &&
            first->retirement.pre_state.fetch_address == 0x0300u &&
            first->retirement.pre_state.status_register == 0x63a0u &&
            first->retirement.pre_state.control_register == 0x00f1u &&
            first->retirement.pre_state.address_registers ==
                first_raw.pre_state.address_registers &&
            first->retirement.pre_state.index_registers ==
                first_raw.pre_state.index_registers &&
            first->retirement.pre_state.wrap_registers ==
                first_raw.pre_state.wrap_registers &&
            first->retirement.pre_state.stack_depths ==
                std::array<std::uint8_t, 4>{1u, 2u, 3u, 4u} &&
            first->retirement.pre_state.external_interrupt_pending &&
            !first->retirement.pre_state.accelerator_interrupt_pending &&
            !first->retirement.pre_state.predecessor.has_value(),
        "offline timing replay exact-maps immutable SR/CR/AR/IX/WR/depth/external prestate and the initial predecessor");
    passed &= expect(
        first.has_value() &&
            first->retirement.plan.binding.contract_version ==
                first_plan.binding.contract_version &&
            first->retirement.plan.binding.resolver_plan_identity ==
                first_plan.binding.resolver_plan_identity,
        "offline timing replay binds the caller-owned plan to the caller-owned deterministic table row without claiming authenticated lookup");
    passed &= expect(
        first.has_value() && first->retirement.memory_access_count == 1u &&
            first->retirement.memory_accesses[0].slot ==
                timing::DspTimingMemorySlot::Standalone &&
            first->retirement.memory_accesses[0].space ==
                timing::DspTimingMemorySpace::Data &&
            first->retirement.memory_accesses[0].direction ==
                timing::DspTimingMemoryDirection::Read &&
            first->retirement.memory_accesses[0].address == 0xf012u &&
            first->retirement.memory_accesses[0].region ==
                timing::DspTimingMemoryRegion::InterfaceRegister,
        "offline timing replay preserves exact raw IFX address and ledger-derived region");

    auto second_plan = timing_plan(
        0x0301u, timing::kDspTimingStandaloneMemoryBit);
    second_plan.identity.opcode_word = 0x1700u;
    second_plan.memory_operands[0] = timing::DspTimingMemoryOperandAuthority{
        timing::DspTimingMemorySpace::Instruction,
        timing::DspTimingMemoryDirection::Read};
    auto second_raw = replay_raw_record(second_plan, 2u);
    second_raw.memory_operand_count = 1u;
    second_raw.memory_operands[0] = {
        galaxy::DspGeneratedMemorySlot::Standalone,
        galaxy::DspGeneratedMemorySpace::Instruction,
        galaxy::DspGeneratedMemoryDirection::Read,
        0x8123u};
    auto second_supplement = replay_supplement();
    second_supplement.expected_predecessor = first_plan.identity;
    second_supplement.accelerator =
        replay::DspTimingReplayAcceleratorEvidence::RawReadEndPending;
    second_supplement.post_outcome.interrupt =
        timing::DspTimingInterruptOutcome::AcceptedAfterInstruction;
    second_supplement.accepted_interrupt =
        replay::DspTimingReplayAcceptedInterrupt::Accelerator;
    const auto second = session.replay(
        second_raw,
        second_plan,
        authorized_entry(second_plan),
        second_supplement);
    passed &= expect(
        second.has_value() &&
            second->retirement.pre_state.predecessor == first_plan.identity &&
            !second->retirement.pre_state.external_interrupt_pending &&
            second->retirement.pre_state.accelerator_interrupt_pending &&
            second->retirement.memory_accesses[0].address == 0x8123u &&
            second->retirement.memory_accesses[0].region ==
                timing::DspTimingMemoryRegion::InstructionIrom &&
            second->accelerator_evidence ==
                replay::DspTimingReplayAcceleratorEvidence::
                    RawReadEndPending &&
            second->accepted_interrupt ==
                replay::DspTimingReplayAcceptedInterrupt::Accelerator,
        "offline timing replay preserves a full predecessor, raw IROM alias, and one explicit accelerator source");

    auto third_plan = parallel_timing_plan(
        0x0302u,
        static_cast<std::uint8_t>(
            timing::kDspTimingParallelPrimaryMemoryBit |
            timing::kDspTimingParallelSecondaryMemoryBit));
    auto third_raw = replay_raw_record(third_plan, 3u);
    third_raw.memory_operand_count = 2u;
    third_raw.memory_operands[0] = {
        galaxy::DspGeneratedMemorySlot::ParallelPrimary,
        galaxy::DspGeneratedMemorySpace::Data,
        galaxy::DspGeneratedMemoryDirection::Write,
        0x1000u};
    third_raw.memory_operands[1] = {
        galaxy::DspGeneratedMemorySlot::ParallelSecondary,
        galaxy::DspGeneratedMemorySpace::Data,
        galaxy::DspGeneratedMemoryDirection::Read,
        0x1000u};
    auto third_supplement = replay_supplement();
    third_supplement.expected_predecessor = second_plan.identity;
    const auto third = session.replay(
        third_raw,
        third_plan,
        authorized_entry(third_plan),
        third_supplement);
    passed &= expect(
        third.has_value() && third->retirement.memory_access_count == 2u &&
            third->retirement.memory_accesses[0].slot ==
                timing::DspTimingMemorySlot::ParallelPrimary &&
            third->retirement.memory_accesses[0].address == 0x1000u &&
            third->retirement.memory_accesses[0].region ==
                timing::DspTimingMemoryRegion::DataOpenBus &&
            third->retirement.memory_accesses[1].slot ==
                timing::DspTimingMemorySlot::ParallelSecondary &&
            third->retirement.memory_accesses[1].address == 0x1000u &&
            third->retirement.memory_accesses[1].region ==
                timing::DspTimingMemoryRegion::CoefficientRom &&
            session.successful_commits() == 3u,
        "offline timing replay retains both same-address parallel slots and distinguishes coefficient reads from open-bus writes");

    auto fourth_plan = timing_plan(0x0303u);
    fourth_plan.identity.opcode_word = 0x029fu;
    set_timing_semantic(
        fourth_plan, timing::DspTimingInstructionSemantic::ConditionalDynamic);
    set_hardware_loop_site(
        fourth_plan,
        timing::DspTimingHardwareLoopSite::MayResolveLoopEnd);
    const auto fourth_raw = replay_raw_record(fourth_plan, 4u);
    auto fourth_supplement = replay_supplement();
    fourth_supplement.expected_predecessor = third_plan.identity;
    fourth_supplement.post_outcome.instruction =
        timing::DspTimingInstructionOutcome::ConditionSatisfied;
    fourth_supplement.post_outcome.condition =
        timing::DspTimingConditionOutcome::Satisfied;
    fourth_supplement.post_outcome.hardware_loop =
        timing::DspTimingHardwareLoopOutcome::BackEdgeTaken;
    const auto fourth = session.replay(
        fourth_raw,
        fourth_plan,
        authorized_entry(fourth_plan),
        fourth_supplement);
    passed &= expect(
        fourth.has_value() &&
            fourth->retirement.post_outcome.instruction ==
                fourth_supplement.post_outcome.instruction &&
            fourth->retirement.post_outcome.condition ==
                fourth_supplement.post_outcome.condition &&
            fourth->retirement.post_outcome.hardware_loop ==
                fourth_supplement.post_outcome.hardware_loop &&
            fourth->retirement.post_outcome.interrupt ==
                fourth_supplement.post_outcome.interrupt &&
            fourth->retirement.pre_state.predecessor == third_plan.identity &&
            session.completed_raw_sequence() == 4u &&
            session.successful_commits() == 4u &&
            session.cancellation_attempts() == 0u,
        "offline timing replay exact-maps an explicit conditional and hardware-loop post-outcome without inferring control flow");
    return passed;
}

bool timing_replay_rejects_identity_sequence_and_lifecycle_mismatches() {
    namespace replay = galaxy::dsp_timing_replay;
    namespace timing = galaxy::dsp_timing_runtime;

    static_assert(
        std::is_default_constructible_v<replay::DspTimingReplaySession> &&
        !std::is_copy_constructible_v<replay::DspTimingReplaySession> &&
        !std::is_copy_assignable_v<replay::DspTimingReplaySession> &&
        !std::is_move_constructible_v<replay::DspTimingReplaySession> &&
        !std::is_move_assignable_v<replay::DspTimingReplaySession> &&
        !std::is_constructible_v<
            replay::DspTimingReplaySession,
            timing::DspTimingRetirementLedger&> &&
        !std::is_constructible_v<
            replay::DspTimingReplaySession,
            timing::DspTimingRetirementLedger&,
            std::uint64_t> &&
        !HasReplayResetSurface<replay::DspTimingReplaySession> &&
        !HasReplayLedgerAccessor<replay::DspTimingReplaySession>);
    static_assert(
        std::is_same_v<
            decltype(replay::DspTimingReplaySession{}.ledger_state()),
            timing::DspTimingRetirementState> &&
        std::is_same_v<
            decltype(replay::DspTimingReplaySession{}.ledger_failure()),
            timing::DspTimingRetirementFailure>);

    bool passed = true;
    {
        replay::DspTimingReplaySession session;
        passed &= expect(
            !session.failed() && session.completed_raw_sequence() == 0u &&
                !session.logical_stack_epoch().has_value() &&
                session.ledger_state().retired_instructions == 0u &&
                !session.ledger_state().last_retired_instruction.has_value(),
            "offline timing replay construction accepts only a fresh zero-state ledger and owns sequence plus epoch from zero");
    }
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(0x0340u);
        const auto raw = replay_raw_record(plan, 1u);
        auto entry = authorized_entry(plan);
        ++entry.contract_version;
        const auto record =
            session.replay(raw, plan, entry, replay_supplement());
        passed &= expect(
            !record.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::
                        UnauthorizedPlanTableIdentity &&
                session.ledger_state().retired_instructions == 0u &&
                session.successful_commits() == 0u &&
                session.cancellation_attempts() == 0u,
            "offline timing replay rejects the wrong timing-contract version before opening a ledger transaction");
    }
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(0x0341u);
        const auto raw = replay_raw_record(plan, 1u);
        auto entry = authorized_entry(plan);
        entry.resolver_plan_identity[7] ^= 0x5au;
        const auto record =
            session.replay(raw, plan, entry, replay_supplement());
        passed &= expect(
            !record.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::
                        UnauthorizedPlanTableIdentity &&
                !session.ledger_failed() && !session.ledger_active(),
            "offline timing replay rejects a wrong full-width resolver table identity without treating it as artifact authentication");
    }
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(0x0342u);
        auto raw = replay_raw_record(plan, 1u);
        raw.identity.opcode_word = 0x0001u;
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), replay_supplement());
        passed &= expect(
            !record.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::
                        InstructionIdentityMismatch &&
                session.ledger_state().retired_instructions == 0u,
            "offline timing replay rejects a structurally valid raw instruction identity that differs from the supplied plan");
    }
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(0x0343u);
        auto raw = replay_raw_record(plan, 1u);
        raw.identity.second_word = 0x1234u;
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), replay_supplement());
        passed &= expect(
            !record.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::
                        InvalidRawInstructionIdentity,
            "offline timing replay rejects a malformed generated raw identity rather than normalizing it");
    }
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(0x0344u);
        const auto raw = replay_raw_record(plan, 0u);
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), replay_supplement());
        passed &= expect(
            !record.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::InvalidRawSequence,
            "offline timing replay rejects raw sequence zero");
    }
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(0x0345u);
        const auto raw = replay_raw_record(plan, 2u);
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), replay_supplement());
        passed &= expect(
            !record.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::RawSequenceGap &&
                session.ledger_state().retired_instructions == 0u,
            "a fresh offline replay ledger must start at exact raw sequence one");
    }
    {
        replay::DspTimingReplaySession session;
        const auto first_plan = timing_plan(0x0346u);
        const auto first = session.replay(
            replay_raw_record(first_plan, 1u),
            first_plan,
            authorized_entry(first_plan),
            replay_supplement());
        auto hostile_state_copy = session.ledger_state();
        hostile_state_copy = {};
        hostile_state_copy.retired_instructions = 1u;
        hostile_state_copy.last_retired_instruction =
            timing_plan(0x0355u).identity;
        const auto second_plan = timing_plan(0x0354u);
        auto second_supplement = replay_supplement();
        second_supplement.expected_predecessor = first_plan.identity;
        const auto second = session.replay(
            replay_raw_record(second_plan, 2u),
            second_plan,
            authorized_entry(second_plan),
            second_supplement);
        passed &= expect(
            first.has_value() && second.has_value() && !session.failed() &&
                hostile_state_copy.retired_instructions == 1u &&
                hostile_state_copy.last_retired_instruction !=
                    first_plan.identity &&
                session.ledger_state().retired_instructions == 2u &&
                session.ledger_state().last_retired_instruction ==
                    second_plan.identity &&
                session.logical_stack_epoch() == 7u &&
                session.successful_commits() == 2u,
            "hostile reset and predecessor repopulation of a copied diagnostic cannot mutate the private ledger or splice the session-owned epoch");
    }
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(0x0347u);
        const auto raw = replay_raw_record(plan, 1u);
        const auto first = session.replay(
            raw, plan, authorized_entry(plan), replay_supplement());
        const auto duplicate = session.replay(
            raw, plan, authorized_entry(plan), replay_supplement());
        passed &= expect(
            first.has_value() && !duplicate.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::DuplicateRawSequence &&
                session.ledger_state().retired_instructions == 1u &&
                session.successful_commits() == 1u &&
                session.cancellation_attempts() == 0u,
            "offline timing replay accepts one raw sequence exactly once and rejects duplicate retirement without cancellation");
    }
    {
        replay::DspTimingReplaySession session;
        const auto first_plan = timing_plan(0x0348u);
        const auto first = session.replay(
            replay_raw_record(first_plan, 1u),
            first_plan,
            authorized_entry(first_plan),
            replay_supplement());
        const auto third_plan = timing_plan(0x034au);
        auto third_supplement = replay_supplement();
        third_supplement.expected_predecessor = first_plan.identity;
        const auto gap = session.replay(
            replay_raw_record(third_plan, 3u),
            third_plan,
            authorized_entry(third_plan),
            third_supplement);
        passed &= expect(
            first.has_value() && !gap.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::RawSequenceGap &&
                session.ledger_state().retired_instructions == 1u,
            "offline timing replay cannot skip a committed raw sequence during continuation");
    }
    {
        replay::DspTimingReplaySession session;
        const auto first_plan = timing_plan(0x034bu);
        const auto first = session.replay(
            replay_raw_record(first_plan, 1u),
            first_plan,
            authorized_entry(first_plan),
            replay_supplement());
        const auto second_plan = timing_plan(0x034cu);
        const auto mismatch = session.replay(
            replay_raw_record(second_plan, 2u),
            second_plan,
            authorized_entry(second_plan),
            replay_supplement());
        passed &= expect(
            first.has_value() && !mismatch.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::PredecessorMismatch &&
                session.ledger_state().last_retired_instruction == first_plan.identity,
            "offline timing replay uses the ledger predecessor and rejects an omitted caller assertion");
    }
    {
        replay::DspTimingReplaySession session;
        const auto first_plan = timing_plan(0x034du);
        const auto first = session.replay(
            replay_raw_record(first_plan, 1u),
            first_plan,
            authorized_entry(first_plan),
            replay_supplement());
        const auto second_plan = timing_plan(0x034eu);
        auto second_raw = replay_raw_record(second_plan, 2u);
        second_raw.pre_state.logical_stack_epoch = 8u;
        auto second_supplement = replay_supplement();
        second_supplement.expected_predecessor = first_plan.identity;
        const auto mismatch = session.replay(
            second_raw,
            second_plan,
            authorized_entry(second_plan),
            second_supplement);
        passed &= expect(
            first.has_value() && !mismatch.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::
                        LogicalStackEpochMismatch &&
                session.ledger_state().retired_instructions == 1u,
            "offline timing replay cannot cross a logical-stack reset epoch within one sequence");
    }
    {
        const auto overflow = replay::detail::next_raw_sequence(
            std::numeric_limits<std::uint64_t>::max());
        passed &= expect(
            !overflow.has_value(),
            "offline timing replay's session-owned next-sequence increment fails closed at uint64 overflow without exposing a resume baseline");
    }
    return passed;
}

bool timing_replay_rejects_memory_shape_and_authority_mismatches() {
    namespace replay = galaxy::dsp_timing_replay;
    namespace timing = galaxy::dsp_timing_runtime;

    bool passed = true;
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(0x0360u);
        auto raw = replay_raw_record(plan, 1u);
        raw.memory_operand_count = 3u;
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), replay_supplement());
        passed &= expect(
            !record.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::InvalidRawMemoryCount &&
                session.cancellation_attempts() == 0u,
            "offline timing replay rejects an unrepresentable three-operand raw memory record");
    }
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(0x0361u);
        auto raw = replay_raw_record(plan, 1u);
        raw.memory_operands[2].address = 1u;
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), replay_supplement());
        passed &= expect(
            !record.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::
                        InvalidRawMemoryPadding &&
                session.ledger_state().retired_instructions == 0u,
            "offline timing replay rejects nonzero raw memory padding rather than ignoring hidden operands");
    }
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(
            0x0362u, timing::kDspTimingStandaloneMemoryBit);
        auto raw = replay_raw_record(plan, 1u);
        raw.memory_operand_count = 1u;
        raw.memory_operands[0].slot =
            static_cast<galaxy::DspGeneratedMemorySlot>(0xffu);
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), replay_supplement());
        passed &= expect(
            !record.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::
                        InvalidRawMemoryDescriptor,
            "offline timing replay rejects an invalid generated memory descriptor enum");
    }
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(
            0x0363u, timing::kDspTimingStandaloneMemoryBit);
        auto raw = replay_raw_record(plan, 1u);
        raw.memory_operand_count = 1u;
        raw.memory_operands[0] = {
            galaxy::DspGeneratedMemorySlot::ParallelPrimary,
            galaxy::DspGeneratedMemorySpace::Data,
            galaxy::DspGeneratedMemoryDirection::Read,
            0x0001u};
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), replay_supplement());
        passed &= expect(
            !record.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::MemorySlotMismatch,
            "offline timing replay rejects a parallel raw slot for a standalone decoded plan");
    }
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(
            0x0364u, timing::kDspTimingStandaloneMemoryBit);
        auto raw = replay_raw_record(plan, 1u);
        raw.memory_operand_count = 1u;
        raw.memory_operands[0] = {
            galaxy::DspGeneratedMemorySlot::Standalone,
            galaxy::DspGeneratedMemorySpace::Instruction,
            galaxy::DspGeneratedMemoryDirection::Read,
            0x0001u};
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), replay_supplement());
        passed &= expect(
            !record.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::MemorySpaceMismatch,
            "offline timing replay rejects a raw memory space that disagrees with decoder authority");
    }
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(
            0x0365u, timing::kDspTimingStandaloneMemoryBit);
        auto raw = replay_raw_record(plan, 1u);
        raw.memory_operand_count = 1u;
        raw.memory_operands[0] = {
            galaxy::DspGeneratedMemorySlot::Standalone,
            galaxy::DspGeneratedMemorySpace::Data,
            galaxy::DspGeneratedMemoryDirection::Write,
            0x0001u};
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), replay_supplement());
        passed &= expect(
            !record.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::
                        MemoryDirectionMismatch,
            "offline timing replay rejects a raw memory direction that disagrees with decoder authority");
    }
    {
        replay::DspTimingReplaySession session;
        const auto plan = parallel_timing_plan(
            0x0366u,
            static_cast<std::uint8_t>(
                timing::kDspTimingParallelPrimaryMemoryBit |
                timing::kDspTimingParallelSecondaryMemoryBit));
        auto raw = replay_raw_record(plan, 1u);
        raw.memory_operand_count = 2u;
        raw.memory_operands[0] = {
            galaxy::DspGeneratedMemorySlot::ParallelSecondary,
            galaxy::DspGeneratedMemorySpace::Data,
            galaxy::DspGeneratedMemoryDirection::Read,
            0x1000u};
        raw.memory_operands[1] = {
            galaxy::DspGeneratedMemorySlot::ParallelPrimary,
            galaxy::DspGeneratedMemorySpace::Data,
            galaxy::DspGeneratedMemoryDirection::Write,
            0x1000u};
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), replay_supplement());
        passed &= expect(
            !record.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::
                        NoncanonicalRawMemoryOrder,
            "offline timing replay rejects reversed parallel raw slots instead of silently sorting them");
    }
    {
        replay::DspTimingReplaySession session;
        auto plan = timing_plan(
            0x0367u, timing::kDspTimingStandaloneMemoryBit);
        plan.memory_operands[0] = timing::DspTimingMemoryOperandAuthority{
            timing::DspTimingMemorySpace::Instruction,
            timing::DspTimingMemoryDirection::Read};
        auto raw = replay_raw_record(plan, 1u);
        raw.memory_operand_count = 1u;
        raw.memory_operands[0] = {
            galaxy::DspGeneratedMemorySlot::Standalone,
            galaxy::DspGeneratedMemorySpace::Instruction,
            galaxy::DspGeneratedMemoryDirection::Read,
            0x7000u};
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), replay_supplement());
        passed &= expect(
            record.has_value() &&
                record->retirement.memory_accesses[0].address == 0x7000u &&
                record->retirement.memory_accesses[0].region ==
                    timing::DspTimingMemoryRegion::InstructionOpenBus &&
                session.ledger_state().retired_instructions == 1u,
            "offline timing replay preserves an unmapped instruction address and classifies open bus without claiming source authority");
    }
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(
            0x0368u, timing::kDspTimingStandaloneMemoryBit);
        auto raw = replay_raw_record(plan, 1u);
        raw.memory_operand_count = 1u;
        raw.memory_operands[0] = {
            galaxy::DspGeneratedMemorySlot::Standalone,
            galaxy::DspGeneratedMemorySpace::Data,
            galaxy::DspGeneratedMemoryDirection::Read,
            0x2345u};
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), replay_supplement());
        passed &= expect(
            record.has_value() &&
                record->retirement.memory_accesses[0].address == 0x2345u &&
                record->retirement.memory_accesses[0].address !=
                    raw.pre_state.address_registers[0] &&
                record->retirement.memory_accesses[0].region ==
                    timing::DspTimingMemoryRegion::DataOpenBus,
            "a source-plausible but copied raw address is preserved and classified exactly; this offline adapter claims no address-source authenticity");
    }
    {
        replay::DspTimingReplaySession session;
        auto plan = timing_plan(
            0x0369u, timing::kDspTimingStandaloneMemoryBit);
        plan.memory_operands[0] = timing::DspTimingMemoryOperandAuthority{
            timing::DspTimingMemorySpace::Instruction,
            timing::DspTimingMemoryDirection::Read};
        auto raw = replay_raw_record(plan, 1u);
        raw.memory_operand_count = 1u;
        raw.memory_operands[0] = {
            galaxy::DspGeneratedMemorySlot::Standalone,
            galaxy::DspGeneratedMemorySpace::Instruction,
            galaxy::DspGeneratedMemoryDirection::Read,
            0x0012u};
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), replay_supplement());
        passed &= expect(
            record.has_value() &&
                record->retirement.memory_accesses[0].address == 0x0012u &&
                record->retirement.memory_accesses[0].region ==
                    timing::DspTimingMemoryRegion::InstructionIram,
            "offline timing replay preserves and classifies an exact IRAM address");
    }
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(
            0x036au, timing::kDspTimingStandaloneMemoryBit);
        auto raw = replay_raw_record(plan, 1u);
        raw.memory_operand_count = 1u;
        raw.memory_operands[0] = {
            galaxy::DspGeneratedMemorySlot::Standalone,
            galaxy::DspGeneratedMemorySpace::Data,
            galaxy::DspGeneratedMemoryDirection::Read,
            0x0012u};
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), replay_supplement());
        passed &= expect(
            record.has_value() &&
                record->retirement.memory_accesses[0].address == 0x0012u &&
                record->retirement.memory_accesses[0].region ==
                    timing::DspTimingMemoryRegion::DataDram,
            "offline timing replay preserves and classifies an exact DRAM address");
    }
    return passed;
}

bool timing_replay_rejects_unprovable_outcomes_and_async_events() {
    namespace replay = galaxy::dsp_timing_replay;
    namespace timing = galaxy::dsp_timing_runtime;

    bool passed = true;
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(0x0370u);
        auto raw = replay_raw_record(plan, 1u);
        raw.pre_state.external_vector = {
            galaxy::DspRawExternalVectorKind::PendingVector,
            galaxy::kDspNoPendingExternalInterrupt};
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), replay_supplement());
        passed &= expect(
            !record.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::InvalidRawPreState,
            "offline timing replay rejects malformed raw external-vector prestate");
    }
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(0x0371u);
        auto raw = replay_raw_record(plan, 1u);
        raw.pre_state.logical_stack_depths = {};
        raw.pre_state.logical_stack_epoch = 0u;
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), replay_supplement());
        passed &= expect(
            !record.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::
                        LogicalStackDepthUnavailable,
            "offline timing replay refuses unavailable logical stack depths instead of substituting wrapping cursors");
    }
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(0x0372u);
        auto raw = replay_raw_record(plan, 1u);
        raw.pre_state.external_vector = {
            galaxy::DspRawExternalVectorKind::Unavailable, 0u};
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), replay_supplement());
        passed &= expect(
            !record.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::
                        ExternalVectorUnavailable,
            "offline timing replay refuses unavailable external interrupt evidence");
    }
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(0x0373u);
        const auto raw = replay_raw_record(plan, 1u);
        replay::DspTimingReplaySupplement supplement{};
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), supplement);
        passed &= expect(
            !record.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::
                        AcceleratorEvidenceUnavailable,
            "offline timing replay requires explicit test-owned accelerator evidence because raw provenance reports unavailable");
    }
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(0x0374u);
        const auto raw = replay_raw_record(plan, 1u);
        auto supplement = replay_supplement();
        supplement.accelerator =
            replay::DspTimingReplayAcceleratorEvidence::MultiplePending;
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), supplement);
        passed &= expect(
            !record.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::
                        UnrepresentableAcceleratorEvidence,
            "offline timing replay rejects multiple accelerator causes that schema v2 cannot represent");
    }
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(0x0375u);
        const auto raw = replay_raw_record(
            plan,
            1u,
            galaxy::DspRawExternalVectorKind::PendingVector,
            0x000eu);
        auto supplement = replay_supplement();
        supplement.accelerator =
            replay::DspTimingReplayAcceleratorEvidence::RawReadEndPending;
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), supplement);
        passed &= expect(
            !record.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::
                        SimultaneousPendingSourcesUnrepresentable,
            "offline timing replay rejects simultaneous external and accelerator pending sources that schema v2 collapses");
    }
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(0x0376u);
        const auto raw = replay_raw_record(plan, 1u);
        auto supplement = replay_supplement();
        supplement.post_outcome.interrupt =
            timing::DspTimingInterruptOutcome::AcceptedAfterInstruction;
        supplement.accepted_interrupt =
            replay::DspTimingReplayAcceptedInterrupt::External;
        supplement.accepted_external_vector = 0x000eu;
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), supplement);
        passed &= expect(
            !record.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::
                        InterruptEvidenceMismatch &&
                !session.ledger_active() && !session.ledger_failed() &&
                session.ledger_state().retired_instructions == 0u,
            "an asynchronous external publication after immutable raw prestate is rejected; replay never infers pending from a late accepted vector");
    }
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(0x0377u);
        const auto raw = replay_raw_record(
            plan,
            1u,
            galaxy::DspRawExternalVectorKind::PendingVector,
            0x000eu);
        auto supplement = replay_supplement();
        supplement.post_outcome.interrupt =
            timing::DspTimingInterruptOutcome::AcceptedAfterInstruction;
        supplement.accepted_interrupt =
            replay::DspTimingReplayAcceptedInterrupt::External;
        supplement.accepted_external_vector = 0x000fu;
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), supplement);
        passed &= expect(
            !record.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::ExternalVectorMismatch,
            "offline timing replay requires an accepted external vector to equal immutable pending-at-entry evidence");
    }
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(0x0378u);
        const auto raw = replay_raw_record(
            plan,
            1u,
            galaxy::DspRawExternalVectorKind::PendingVector,
            0x000eu);
        auto supplement = replay_supplement();
        supplement.accepted_interrupt =
            replay::DspTimingReplayAcceptedInterrupt::External;
        supplement.accepted_external_vector = 0x000eu;
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), supplement);
        passed &= expect(
            !record.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::
                        InterruptEvidenceMismatch,
            "offline timing replay rejects accepted-source evidence when the supplied post-outcome says no interrupt");
    }
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(0x0379u);
        const auto raw = replay_raw_record(plan, 1u);
        auto supplement = replay_supplement();
        supplement.post_outcome.interrupt =
            timing::DspTimingInterruptOutcome::AcceptedAfterInstruction;
        supplement.accepted_interrupt =
            replay::DspTimingReplayAcceptedInterrupt::Accelerator;
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), supplement);
        passed &= expect(
            !record.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::
                        InterruptEvidenceMismatch,
            "offline timing replay rejects accelerator acceptance without pending accelerator evidence");
    }
    {
        constexpr std::array<
            replay::DspTimingReplayAcceleratorEvidence,
            2>
            pending_causes{
                replay::DspTimingReplayAcceleratorEvidence::
                    RawWriteEndPending,
                replay::DspTimingReplayAcceleratorEvidence::
                    SampleReadEndPending};
        bool all_causes_preserved = true;
        std::uint16_t fetch_address = 0x037au;
        for (const auto cause : pending_causes) {
            replay::DspTimingReplaySession session;
            const auto plan = timing_plan(fetch_address++);
            const auto raw = replay_raw_record(plan, 1u);
            auto supplement = replay_supplement();
            supplement.accelerator = cause;
            const auto record = session.replay(
                raw, plan, authorized_entry(plan), supplement);
            all_causes_preserved = all_causes_preserved &&
                record.has_value() &&
                record->accelerator_evidence == cause &&
                record->retirement.pre_state.accelerator_interrupt_pending &&
                record->retirement.post_outcome.interrupt ==
                    timing::DspTimingInterruptOutcome::None;
        }
        passed &= expect(
            all_causes_preserved,
            "offline timing replay preserves each representable nonaccepted accelerator pending cause without inventing acceptance");
    }
    {
        replay::DspTimingReplaySession session;
        const auto plan = timing_plan(0x037cu);
        const auto raw = replay_raw_record(plan, 1u);
        auto supplement = replay_supplement();
        supplement.post_outcome.instruction =
            timing::DspTimingInstructionOutcome::ConditionSatisfied;
        supplement.post_outcome.condition =
            timing::DspTimingConditionOutcome::Satisfied;
        const auto record = session.replay(
            raw, plan, authorized_entry(plan), supplement);
        passed &= expect(
            !record.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::LedgerCommitRejected &&
                session.ledger_failure() ==
                    timing::DspTimingRetirementFailure::InvalidPostOutcome &&
                !session.ledger_active() &&
                session.ledger_state().retired_instructions == 0u &&
                session.successful_commits() == 0u &&
                session.cancellation_attempts() == 1u,
            "a decoder-invalid post-outcome fails commit and the replay adapter cancels its authenticated live transaction exactly once");
    }
    {
        replay::DspTimingReplaySession session;
        const auto first_plan = timing_plan(0x037eu);
        auto supplement = replay_supplement();
        supplement.force_post_commit_record_mismatch = true;
        const auto mismatch = session.replay(
            replay_raw_record(first_plan, 1u),
            first_plan,
            authorized_entry(first_plan),
            supplement);
        const auto second_plan = timing_plan(0x037fu);
        auto second_supplement = replay_supplement();
        second_supplement.expected_predecessor = first_plan.identity;
        const auto after_terminal_mismatch = session.replay(
            replay_raw_record(second_plan, 2u),
            second_plan,
            authorized_entry(second_plan),
            second_supplement);
        passed &= expect(
            !mismatch.has_value() && !after_terminal_mismatch.has_value() &&
                session.failure() ==
                    replay::DspTimingReplayFailure::LedgerRecordMismatch &&
                session.ledger_state().retired_instructions == 1u &&
                session.ledger_state().last_retired_instruction ==
                    first_plan.identity &&
                !session.ledger_active() && !session.ledger_failed() &&
                session.completed_raw_sequence() == 0u &&
                session.successful_commits() == 1u &&
                session.cancellation_attempts() == 0u,
            "post-commit ledger-record mismatch is terminal: the ledger advanced exactly once, no committed transaction is cancelled, and replay cannot continue");
    }
    return passed;
}

bool timing_retirement_commit_is_explicit_and_prestate_is_immutable() {
    namespace timing = galaxy::dsp_timing_runtime;

    timing::DspTimingRetirementLedger ledger;
    const auto predecessor_plan = timing_plan(0x0122u);
    timing::DspTimingInstructionScope predecessor_scope(
        ledger,
        predecessor_plan,
        authorized_entry(predecessor_plan),
        timing_pre_state(predecessor_plan));
    bool passed = expect(
        predecessor_scope.commit({}).has_value(),
        "retirement predecessor enters state only through an authorized commit");
    auto plan = timing_plan(
        0x0123u, timing::kDspTimingStandaloneMemoryBit);
    plan.identity.opcode_word = 0x00c0u;
    set_timing_semantic(
        plan, timing::DspTimingInstructionSemantic::ConditionalDynamic);
    set_hardware_loop_site(
        plan, timing::DspTimingHardwareLoopSite::MayResolveLoopEnd);
    auto pre_state = timing_pre_state(plan);
    pre_state.predecessor = predecessor_plan.identity;
    pre_state.external_interrupt_pending = true;

    timing::DspTimingInstructionScope scope(
        ledger, plan, authorized_entry(plan), pre_state);
    passed &= expect(scope.began(), "exact DSP retirement transaction begins");
    pre_state.status_register = 0u;
    pre_state.control_register = 0u;
    pre_state.address_registers[0] = 0xffffu;
    pre_state.predecessor.reset();
    passed &= expect(
        scope.record_memory(
            timing::DspTimingMemorySlot::Standalone,
            timing::DspTimingMemorySpace::Data,
            timing::DspTimingMemoryDirection::Read,
            0xf012u),
        "retirement transaction records an exact standalone IFX read");

    timing::DspTimingPostOutcome outcome{};
    outcome.instruction =
        timing::DspTimingInstructionOutcome::ConditionSatisfied;
    outcome.condition = timing::DspTimingConditionOutcome::Satisfied;
    outcome.hardware_loop =
        timing::DspTimingHardwareLoopOutcome::BackEdgeTaken;
    outcome.interrupt =
        timing::DspTimingInterruptOutcome::AcceptedAfterInstruction;
    const auto record = scope.commit(outcome);
    const auto state = ledger.state();

    passed &= expect(
        record.has_value() && !ledger.failed() && !ledger.active(),
        "complete exact DSP timing key commits once");
    passed &= expect(
        state.retired_instructions == 2u &&
            state.last_retired_instruction.has_value() &&
            state.last_retired_instruction->fetch_address == 0x0123u &&
            *state.last_retired_instruction == plan.identity,
        "commit alone advances the true retirement identity");
    passed &= expect(
        record.has_value() && record->retirement_sequence == 2u &&
            record->pre_state.status_register == 0x63a0u &&
            record->pre_state.control_register == 0x00f1u &&
            record->pre_state.address_registers[0] == 0x0001u &&
            record->pre_state.predecessor.has_value() &&
            record->pre_state.predecessor->fetch_address == 0x0122u,
        "begin copies immutable pre-instruction state");
    passed &= expect(
        record.has_value() && record->memory_access_count == 1u &&
            record->memory_accesses[0].slot ==
                timing::DspTimingMemorySlot::Standalone &&
            record->memory_accesses[0].space ==
                timing::DspTimingMemorySpace::Data &&
            record->memory_accesses[0].direction ==
                timing::DspTimingMemoryDirection::Read &&
            record->memory_accesses[0].address == 0xf012u &&
            record->memory_accesses[0].region ==
                timing::DspTimingMemoryRegion::InterfaceRegister,
        "committed key retains exact memory slot, address, direction, and region");
    passed &= expect(
        record.has_value() &&
            record->post_outcome.instruction ==
                timing::DspTimingInstructionOutcome::ConditionSatisfied &&
            record->post_outcome.condition ==
                timing::DspTimingConditionOutcome::Satisfied &&
            record->post_outcome.hardware_loop ==
                timing::DspTimingHardwareLoopOutcome::BackEdgeTaken &&
            record->post_outcome.interrupt ==
                timing::DspTimingInterruptOutcome::AcceptedAfterInstruction,
        "commit retains explicit condition, loop, and interrupt outcomes");
    return passed;
}

bool timing_retirement_trap_and_abort_never_retire_current_instruction() {
    namespace timing = galaxy::dsp_timing_runtime;

    timing::DspTimingRetirementLedger ledger;
    const auto first_plan = timing_plan(0x0100u);
    {
        timing::DspTimingInstructionScope first(
            ledger,
            first_plan,
            authorized_entry(first_plan),
            timing_pre_state(first_plan));
        if (!first.commit({}).has_value()) {
            return expect(false, "retirement baseline commits");
        }
    }
    const auto committed_state = ledger.state();

    try {
        const auto trapping_plan = timing_plan(0x0101u);
        auto trapping_pre_state = timing_pre_state(trapping_plan);
        trapping_pre_state.predecessor =
            committed_state.last_retired_instruction;
        timing::DspTimingInstructionScope trapping(
            ledger,
            trapping_plan,
            authorized_entry(trapping_plan),
            trapping_pre_state);
        if (!trapping.began()) {
            return expect(false, "trapping retirement transaction begins");
        }
        throw galaxy::DspHardTrap{0x0101u, "synthetic timing trap", 0x0100u};
    } catch (const galaxy::DspHardTrap&) {
    }

    bool passed = expect(
        ledger.state().retired_instructions ==
                committed_state.retired_instructions &&
            ledger.state().last_retired_instruction ==
                committed_state.last_retired_instruction &&
            !ledger.active() && !ledger.failed(),
        "hard-trap unwind cancels the current instruction without retirement");

    try {
        const auto aborted_plan = timing_plan(0x0102u);
        auto aborted_pre_state = timing_pre_state(aborted_plan);
        aborted_pre_state.predecessor =
            committed_state.last_retired_instruction;
        timing::DspTimingInstructionScope aborted(
            ledger,
            aborted_plan,
            authorized_entry(aborted_plan),
            aborted_pre_state);
        if (!aborted.began()) {
            return expect(false, "aborted retirement transaction begins");
        }
        throw galaxy::DspExecutionAborted{};
    } catch (const galaxy::DspExecutionAborted&) {
    }
    passed &= expect(
        ledger.state().retired_instructions ==
                committed_state.retired_instructions &&
            ledger.state().last_retired_instruction ==
                committed_state.last_retired_instruction &&
            !ledger.active() && !ledger.failed(),
        "cooperative-abort unwind cannot retire the current instruction");

    {
        const auto forgotten_plan = timing_plan(0x0103u);
        auto forgotten_pre_state = timing_pre_state(forgotten_plan);
        forgotten_pre_state.predecessor =
            committed_state.last_retired_instruction;
        timing::DspTimingInstructionScope forgotten(
            ledger,
            forgotten_plan,
            authorized_entry(forgotten_plan),
            forgotten_pre_state);
        passed &= expect(forgotten.began(), "uncommitted transaction begins");
    }
    passed &= expect(
        ledger.state().retired_instructions ==
                committed_state.retired_instructions &&
            ledger.state().last_retired_instruction ==
                committed_state.last_retired_instruction &&
            !ledger.active() &&
            ledger.failure() ==
                timing::DspTimingRetirementFailure::UncommittedInstruction,
        "ordinary forgotten commit is sticky while preserving retirement");
    passed &= expect(
        ledger.reset() && ledger.state().retired_instructions == 0u &&
            !ledger.state().last_retired_instruction.has_value(),
        "fresh reset recovers by discarding unauthenticated prior history");

    {
        const auto cancelled_plan = timing_plan(0x0104u);
        auto cancelled_pre_state = timing_pre_state(cancelled_plan);
        timing::DspTimingInstructionScope cancelled(
            ledger,
            cancelled_plan,
            authorized_entry(cancelled_plan),
            cancelled_pre_state);
        passed &= expect(
            cancelled.began() && cancelled.abort(),
            "explicit cooperative cancellation authenticates its token");
    }
    passed &= expect(
        !ledger.active() && !ledger.failed() &&
            ledger.state().retired_instructions == 0u &&
            !ledger.state().last_retired_instruction.has_value(),
        "explicit abort cancels without retirement or sticky failure");
    return passed;
}

bool timing_retirement_memory_and_outcome_fail_closed() {
    namespace timing = galaxy::dsp_timing_runtime;

    timing::DspTimingRetirementLedger ledger;
    auto standalone = timing_plan(
        0x0200u, timing::kDspTimingStandaloneMemoryBit);
    bool passed = true;
    {
        timing::DspTimingInstructionScope missing(
            ledger,
            standalone,
            authorized_entry(standalone),
            timing_pre_state(standalone));
        passed &= expect(
            !missing.commit({}).has_value() &&
                ledger.failure() ==
                    timing::DspTimingRetirementFailure::
                        IncompleteMemoryAccesses &&
                ledger.state().retired_instructions == 0u,
            "missing declared memory slot makes timing retirement unavailable");
    }
    passed &= expect(
        !ledger.begin(
            standalone,
            authorized_entry(standalone),
            timing_pre_state(standalone)),
        "timing retirement failure remains sticky until reset");

    passed &= expect(ledger.reset(), "retirement ledger reset clears failure");
    {
        timing::DspTimingInstructionScope duplicate(
            ledger,
            standalone,
            authorized_entry(standalone),
            timing_pre_state(standalone));
        passed &= expect(
            duplicate.record_memory(
                timing::DspTimingMemorySlot::Standalone,
                timing::DspTimingMemorySpace::Data,
                timing::DspTimingMemoryDirection::Read,
                0x0001u),
            "first exact memory slot record succeeds");
        passed &= expect(
            !duplicate.record_memory(
                timing::DspTimingMemorySlot::Standalone,
                timing::DspTimingMemorySpace::Data,
                timing::DspTimingMemoryDirection::Write,
                0x0002u) &&
                ledger.failure() ==
                    timing::DspTimingRetirementFailure::DuplicateMemorySlot,
            "duplicate memory slot fails closed");
    }

    passed &= expect(ledger.reset(), "ledger resets after duplicate slot");
    {
        timing::DspTimingInstructionScope open_bus_address(
            ledger,
            standalone,
            authorized_entry(standalone),
            timing_pre_state(standalone));
        passed &= expect(
            open_bus_address.record_memory(
                timing::DspTimingMemorySlot::Standalone,
                timing::DspTimingMemorySpace::Data,
                timing::DspTimingMemoryDirection::Read,
                0x7fffu),
            "schema represents a legal ordinary-runtime open-bus data read");
        const auto record = open_bus_address.commit({});
        passed &= expect(
            record.has_value() && record->memory_accesses[0].address == 0x7fffu &&
                record->memory_accesses[0].region ==
                    timing::DspTimingMemoryRegion::DataOpenBus,
            "open-bus timing evidence retains the exact raw address and region");
    }

    passed &= expect(
        ledger.reset(), "ledger resets after exact open-bus evidence");
    {
        standalone.memory_operands[0] = timing::DspTimingMemoryOperandAuthority{
            timing::DspTimingMemorySpace::Instruction,
            timing::DspTimingMemoryDirection::Read};
        timing::DspTimingInstructionScope unmapped_imem_address(
            ledger,
            standalone,
            authorized_entry(standalone),
            timing_pre_state(standalone));
        passed &= expect(
            unmapped_imem_address.record_memory(
                timing::DspTimingMemorySlot::Standalone,
                timing::DspTimingMemorySpace::Instruction,
                timing::DspTimingMemoryDirection::Read,
                0x9001u),
            "schema represents an ordinary-runtime unmapped instruction read");
        const auto record = unmapped_imem_address.commit({});
        passed &= expect(
            record.has_value() && record->memory_accesses[0].address == 0x9001u &&
                record->memory_accesses[0].region ==
                    timing::DspTimingMemoryRegion::InstructionOpenBus,
            "instruction open-bus timing evidence retains the exact raw address");
    }

    passed &= expect(
        ledger.reset(), "ledger resets after exact instruction open-bus evidence");
    {
        timing::DspTimingInstructionScope invalid_direction(
            ledger,
            standalone,
            authorized_entry(standalone),
            timing_pre_state(standalone));
        passed &= expect(
            !invalid_direction.record_memory(
                timing::DspTimingMemorySlot::Standalone,
                timing::DspTimingMemorySpace::Data,
                static_cast<timing::DspTimingMemoryDirection>(0xffu),
                0x0001u) &&
                ledger.failure() ==
                    timing::DspTimingRetirementFailure::InvalidMemoryDirection,
            "unknown memory direction fails closed");
    }

    passed &= expect(ledger.reset(), "ledger resets after invalid direction");
    {
        timing::DspTimingInstructionScope invalid_space(
            ledger,
            standalone,
            authorized_entry(standalone),
            timing_pre_state(standalone));
        passed &= expect(
            !invalid_space.record_memory(
                timing::DspTimingMemorySlot::Standalone,
                static_cast<timing::DspTimingMemorySpace>(0xffu),
                timing::DspTimingMemoryDirection::Read,
                0x0001u) &&
                ledger.failure() ==
                    timing::DspTimingRetirementFailure::InvalidMemorySpace,
            "unknown memory space fails closed");
    }

    passed &= expect(ledger.reset(), "ledger resets after invalid memory space");
    auto exact_data_read = timing_plan(
        0x0200u, timing::kDspTimingStandaloneMemoryBit);
    {
        timing::DspTimingInstructionScope wrong_direction(
            ledger,
            exact_data_read,
            authorized_entry(exact_data_read),
            timing_pre_state(exact_data_read));
        passed &= expect(
            !wrong_direction.record_memory(
                timing::DspTimingMemorySlot::Standalone,
                timing::DspTimingMemorySpace::Data,
                timing::DspTimingMemoryDirection::Write,
                0x0001u) &&
                ledger.failure() ==
                    timing::DspTimingRetirementFailure::
                        MemoryDirectionMismatch,
            "a valid but decoder-unauthorized memory direction fails closed");
    }

    passed &= expect(
        ledger.reset(), "ledger resets after decoder direction mismatch");
    {
        timing::DspTimingInstructionScope wrong_space(
            ledger,
            exact_data_read,
            authorized_entry(exact_data_read),
            timing_pre_state(exact_data_read));
        passed &= expect(
            !wrong_space.record_memory(
                timing::DspTimingMemorySlot::Standalone,
                timing::DspTimingMemorySpace::Instruction,
                timing::DspTimingMemoryDirection::Read,
                0x9001u) &&
                ledger.failure() ==
                    timing::DspTimingRetirementFailure::MemorySpaceMismatch,
            "a valid but decoder-unauthorized memory space fails closed");
    }

    passed &= expect(
        ledger.reset(), "ledger resets after decoder space mismatch");
    {
        timing::DspTimingInstructionScope unavailable(
            ledger,
            standalone,
            authorized_entry(standalone),
            timing_pre_state(standalone));
        passed &= expect(
            !unavailable.mark_memory_capture_unavailable() &&
                ledger.failure() ==
                    timing::DspTimingRetirementFailure::
                        MemoryCaptureUnavailable,
            "explicitly unavailable memory capture cannot commit");
    }

    passed &= expect(ledger.reset(), "ledger resets after unavailable capture");
    {
        const auto no_memory_plan = timing_plan(0x0201u);
        timing::DspTimingInstructionScope unexpected_slot(
            ledger,
            no_memory_plan,
            authorized_entry(no_memory_plan),
            timing_pre_state(no_memory_plan));
        passed &= expect(
            !unexpected_slot.record_memory(
                timing::DspTimingMemorySlot::Standalone,
                timing::DspTimingMemorySpace::Data,
                timing::DspTimingMemoryDirection::Read,
                0x0001u) &&
                ledger.failure() ==
                    timing::DspTimingRetirementFailure::
                        UnexpectedMemorySlot,
            "undeclared memory slot fails before mutating captured accesses");
    }

    passed &= expect(
        ledger.reset(), "ledger resets after unexpected memory slot");
    auto parallel = parallel_timing_plan(
        0x0211u,
        static_cast<std::uint8_t>(
            timing::kDspTimingParallelPrimaryMemoryBit |
            timing::kDspTimingParallelSecondaryMemoryBit));
    {
        timing::DspTimingInstructionScope exact_parallel(
            ledger,
            parallel,
            authorized_entry(parallel),
            timing_pre_state(parallel));
        passed &= expect(
            exact_parallel.record_memory(
                timing::DspTimingMemorySlot::ParallelSecondary,
                timing::DspTimingMemorySpace::Data,
                timing::DspTimingMemoryDirection::Read,
                0xf0ffu) &&
                exact_parallel.record_memory(
                    timing::DspTimingMemorySlot::ParallelPrimary,
                    timing::DspTimingMemorySpace::Data,
                    timing::DspTimingMemoryDirection::Write,
                    0x0001u),
            "parallel primary and secondary slots record independently");
        const auto record = exact_parallel.commit({});
        passed &= expect(
            record.has_value() && record->memory_access_count == 2u &&
                record->memory_accesses[0].slot ==
                    timing::DspTimingMemorySlot::ParallelPrimary &&
                record->memory_accesses[0].address == 0x0001u &&
                record->memory_accesses[0].region ==
                    timing::DspTimingMemoryRegion::DataDram &&
                record->memory_accesses[1].slot ==
                    timing::DspTimingMemorySlot::ParallelSecondary &&
                record->memory_accesses[1].address == 0xf0ffu,
            "parallel memory record commits in canonical slot order");
    }

    passed &= expect(ledger.reset(), "ledger resets after exact parallel commit");
    auto no_memory = timing_plan(0x0202u);
    auto no_pending = timing_pre_state(no_memory);
    {
        timing::DspTimingInstructionScope impossible_interrupt(
            ledger, no_memory, authorized_entry(no_memory), no_pending);
        timing::DspTimingPostOutcome outcome{};
        outcome.interrupt =
            timing::DspTimingInterruptOutcome::AcceptedAfterInstruction;
        passed &= expect(
            !impossible_interrupt.commit(outcome).has_value() &&
                ledger.failure() ==
                    timing::DspTimingRetirementFailure::
                        InterruptAcceptedWithoutPendingSource,
            "accepted interrupt requires an immutable pending source");
    }

    passed &= expect(ledger.reset(), "ledger resets after interrupt mismatch");
    auto conditional_plan = no_memory;
    set_timing_semantic(
        conditional_plan,
        timing::DspTimingInstructionSemantic::ConditionalAlways);
    {
        timing::DspTimingInstructionScope mismatched_condition(
            ledger,
            conditional_plan,
            authorized_entry(conditional_plan),
            no_pending);
        timing::DspTimingPostOutcome outcome{};
        outcome.instruction =
            timing::DspTimingInstructionOutcome::ConditionSatisfied;
        passed &= expect(
            !mismatched_condition.commit(outcome).has_value() &&
                ledger.failure() ==
                    timing::DspTimingRetirementFailure::InvalidPostOutcome,
            "condition outcome must be explicit and internally consistent");
    }

    passed &= expect(ledger.reset(), "ledger resets after outcome mismatch");
    auto invalid_plan = timing_plan(
        0x0203u, timing::kDspTimingParallelPrimaryMemoryBit);
    passed &= expect(
        !ledger.begin(
            invalid_plan,
            authorized_entry(invalid_plan),
            timing_pre_state(invalid_plan)) &&
            ledger.failure() ==
                timing::DspTimingRetirementFailure::InvalidInstructionPlan,
        "standalone instruction cannot declare a parallel memory slot");

    const auto prior_plan = timing_plan(0x0204u);
    passed &= expect(
        ledger.reset(),
        "fresh ledger reset clears the invalid-plan failure");
    {
        timing::DspTimingInstructionScope prior(
            ledger,
            prior_plan,
            authorized_entry(prior_plan),
            timing_pre_state(prior_plan));
        passed &= expect(
            prior.commit({}).has_value(),
            "predecessor state is established only by an authorized commit");
    }
    const auto missing_predecessor_plan = timing_plan(0x0205u);
    passed &= expect(
        !ledger.begin(
            missing_predecessor_plan,
            authorized_entry(missing_predecessor_plan),
            timing_pre_state(missing_predecessor_plan)) &&
            ledger.failure() ==
                timing::DspTimingRetirementFailure::InvalidPreState,
        "noninitial timing transaction cannot omit its exact predecessor");
    passed &= expect(
        ledger.reset() && ledger.state().retired_instructions == 0u &&
            !ledger.state().last_retired_instruction.has_value(),
        "fresh reset discards prior history instead of accepting a structural checkpoint");
    static_assert(
        !HasUntrustedRetirementStateReset<
            timing::DspTimingRetirementLedger>);
    timing::DspTimingInstructionIdentity structural_lie{};
    structural_lie.fetch_address = 0x0206u;
    structural_lie.opcode_word = 0x8401u;
    structural_lie.bundle = timing::DspTimingBundleKind::Standalone;
    const timing::DspTimingRetirementState forged_bundle_checkpoint{
        1u, structural_lie};
    const timing::DspTimingRetirementState forged_count_checkpoint{
        std::numeric_limits<std::uint64_t>::max(), prior_plan.identity};
    passed &= expect(
        !HasUntrustedRetirementStateReset<
            timing::DspTimingRetirementLedger> &&
            forged_bundle_checkpoint.last_retired_instruction->opcode_word ==
                0x8401u &&
            forged_count_checkpoint.retired_instructions ==
                std::numeric_limits<std::uint64_t>::max(),
        "no API accepts forged count or structural 0x8401-as-standalone predecessor checkpoint");
    return passed;
}

bool timing_memory_regions_match_exact_runtime_access_semantics() {
    namespace timing = galaxy::dsp_timing_runtime;
    bool passed = true;

    for (std::uint16_t top = 0u; top <= 0x0fu; ++top) {
        const auto address = static_cast<std::uint16_t>((top << 12u) | 0x0321u);
        for (const auto direction : {
                 timing::DspTimingMemoryDirection::Read,
                 timing::DspTimingMemoryDirection::Write}) {
            timing::DspTimingRetirementLedger ledger;
            auto plan = timing_plan(
                0x0220u, timing::kDspTimingStandaloneMemoryBit);
            plan.memory_operands[0] = timing::DspTimingMemoryOperandAuthority{
                timing::DspTimingMemorySpace::Data,
                direction};
            timing::DspTimingInstructionScope scope(
                ledger, plan, authorized_entry(plan), timing_pre_state(plan));
            const bool recorded = scope.record_memory(
                timing::DspTimingMemorySlot::Standalone,
                timing::DspTimingMemorySpace::Data,
                direction,
                address);
            const auto record = scope.commit({});
            const auto expected =
                top == 0u
                ? timing::DspTimingMemoryRegion::DataDram
                : (top == 1u &&
                       direction == timing::DspTimingMemoryDirection::Read)
                ? timing::DspTimingMemoryRegion::CoefficientRom
                : top == 0x0fu
                ? timing::DspTimingMemoryRegion::InterfaceRegister
                : timing::DspTimingMemoryRegion::DataOpenBus;
            passed &= expect(
                recorded && record.has_value() &&
                    record->memory_accesses[0].address == address &&
                    record->memory_accesses[0].space ==
                        timing::DspTimingMemorySpace::Data &&
                    record->memory_accesses[0].direction == direction &&
                    record->memory_accesses[0].region == expected,
                "all 16 data top-nibbles retain exact direction, raw address, and runtime region");
        }
    }

    for (const auto address : {
             std::uint16_t{0x0000u},
             std::uint16_t{0x0fffu},
             std::uint16_t{0x1000u},
             std::uint16_t{0x7fffu},
             std::uint16_t{0x8000u},
             std::uint16_t{0x9001u},
             std::uint16_t{0xffffu}}) {
        timing::DspTimingRetirementLedger ledger;
        auto plan = timing_plan(
            0x0221u, timing::kDspTimingStandaloneMemoryBit);
        plan.memory_operands[0] = timing::DspTimingMemoryOperandAuthority{
            timing::DspTimingMemorySpace::Instruction,
            timing::DspTimingMemoryDirection::Read};
        timing::DspTimingInstructionScope scope(
            ledger, plan, authorized_entry(plan), timing_pre_state(plan));
        const bool recorded = scope.record_memory(
            timing::DspTimingMemorySlot::Standalone,
            timing::DspTimingMemorySpace::Instruction,
            timing::DspTimingMemoryDirection::Read,
            address);
        const auto record = scope.commit({});
        const auto expected = address <= 0x0fffu
            ? timing::DspTimingMemoryRegion::InstructionIram
            : (address >= 0x8000u && address <= 0x8fffu
                   ? timing::DspTimingMemoryRegion::InstructionIrom
                   : timing::DspTimingMemoryRegion::InstructionOpenBus);
        passed &= expect(
            recorded && record.has_value() &&
                record->memory_accesses[0].address == address &&
                record->memory_accesses[0].space ==
                    timing::DspTimingMemorySpace::Instruction &&
                record->memory_accesses[0].region == expected,
            "instruction accesses retain exact raw addresses and decoded IRAM/IROM/open-bus regions");
    }

    timing::DspTimingRetirementLedger write_ledger;
    const auto write_plan = timing_plan(
        0x0222u, timing::kDspTimingStandaloneMemoryBit);
    timing::DspTimingInstructionScope impossible_write(
        write_ledger,
        write_plan,
        authorized_entry(write_plan),
        timing_pre_state(write_plan));
    passed &= expect(
        !impossible_write.record_memory(
            timing::DspTimingMemorySlot::Standalone,
            timing::DspTimingMemorySpace::Instruction,
            timing::DspTimingMemoryDirection::Write,
            0x0000u) &&
            write_ledger.failure() ==
                timing::DspTimingRetirementFailure::InvalidMemoryDirection,
        "instruction-memory writes are rejected because no decoded DSP operand performs one");
    return passed;
}

bool timing_transaction_tokens_are_epoch_owned_and_cleanup_is_authenticated() {
    namespace timing = galaxy::dsp_timing_runtime;

    static_assert(
        !std::is_copy_constructible_v<timing::DspTimingRetirementLedger> &&
        !std::is_copy_assignable_v<timing::DspTimingRetirementLedger> &&
        !std::is_move_constructible_v<timing::DspTimingRetirementLedger> &&
        !std::is_move_assignable_v<timing::DspTimingRetirementLedger>);

    timing::DspTimingRetirementLedger ledger;
    const auto plan_a = timing_plan(0x0300u);
    auto scope_a = std::make_unique<timing::DspTimingInstructionScope>(
        ledger,
        plan_a,
        authorized_entry(plan_a),
        timing_pre_state(plan_a));
    if (!scope_a->token().has_value()) {
        return expect(false, "transaction A obtains an epoch token");
    }
    const auto token_a = *scope_a->token();
    bool passed = expect(
        ledger.cancel(token_a, timing::DspTimingCancellationReason::ExplicitAbort) &&
            !ledger.active() && !ledger.failed(),
        "matching token externally cancels transaction A without retirement");

    const auto plan_b = timing_plan(
        0x0301u, timing::kDspTimingStandaloneMemoryBit);
    auto scope_b = std::make_unique<timing::DspTimingInstructionScope>(
        ledger,
        plan_b,
        authorized_entry(plan_b),
        timing_pre_state(plan_b));
    if (!scope_b->token().has_value()) {
        return expect(false, "transaction B obtains a distinct epoch token");
    }
    const auto token_b = *scope_b->token();
    passed &= expect(
        token_b != token_a &&
            token_b.epoch_for_diagnostics() >
                token_a.epoch_for_diagnostics() &&
            scope_b->record_memory(
                timing::DspTimingMemorySlot::Standalone,
                timing::DspTimingMemorySpace::Data,
                timing::DspTimingMemoryDirection::Read,
                0x0001u),
        "transaction B owns a newer token and its exact memory capture");

    const auto state_before_stale_operation = ledger.state();
    passed &= expect(
        !ledger.record_memory(
            token_a,
            timing::DspTimingMemorySlot::Standalone,
            timing::DspTimingMemorySpace::Data,
            timing::DspTimingMemoryDirection::Write,
            0x0002u) &&
            !scope_a->commit({}).has_value() &&
            ledger.failure() ==
                timing::DspTimingRetirementFailure::StaleTransactionToken &&
            ledger.active_token() == token_b &&
            ledger.state().retired_instructions ==
                state_before_stale_operation.retired_instructions &&
            ledger.state().last_retired_instruction ==
                state_before_stale_operation.last_retired_instruction,
        "stale A record/commit poisons the ledger without mutating or clearing B");
    passed &= expect(
        !scope_b->commit({}).has_value() && ledger.active_token() == token_b &&
            ledger.state().retired_instructions == 0u,
        "matching B token cannot commit after any stale-token failure");
    passed &= expect(
        !ledger.reset() && ledger.active_token() == token_b &&
            ledger.failure() ==
                timing::DspTimingRetirementFailure::StaleTransactionToken,
        "reset cannot clear a live B transaction or overwrite first failure");
    passed &= expect(
        !scope_a->abort() && ledger.active_token() == token_b,
        "stale A cancellation cannot cancel B");
    scope_a.reset();
    passed &= expect(
        ledger.active_token() == token_b &&
            ledger.failure() ==
                timing::DspTimingRetirementFailure::StaleTransactionToken,
        "stale A destructor cannot clear the newer B transaction");
    passed &= expect(
        scope_b->abort() && !ledger.active() &&
            ledger.failure() ==
                timing::DspTimingRetirementFailure::StaleTransactionToken,
        "matching B token can perform cleanup while sticky-failed");
    scope_b.reset();
    passed &= expect(
        ledger.reset() && !ledger.failed() && !ledger.active(),
        "reset recovers only after authenticated B cleanup");

    const auto plan_c = timing_plan(0x0302u);
    {
        timing::DspTimingInstructionScope scope_c(
            ledger,
            plan_c,
            authorized_entry(plan_c),
            timing_pre_state(plan_c));
        passed &= expect(
            scope_c.commit({}).has_value(),
            "post-reset transaction C can retire normally");
    }
    passed &= expect(
        ledger.state().retired_instructions == 1u && !ledger.failed(),
        "stale-token recovery does not leak a retirement from A or B");

    timing::DspTimingRetirementLedger other_ledger;
    const auto other_plan = timing_plan(0x0310u);
    const auto other_token = other_ledger.begin(
        other_plan,
        authorized_entry(other_plan),
        timing_pre_state(other_plan));
    passed &= expect(
        other_token.has_value() &&
            !other_ledger.cancel(
                token_a, timing::DspTimingCancellationReason::ExplicitAbort) &&
            other_ledger.failure() ==
                timing::DspTimingRetirementFailure::StaleTransactionToken &&
            other_ledger.active_token() == other_token &&
            other_ledger.cancel(
                *other_token,
                timing::DspTimingCancellationReason::ExplicitAbort) &&
            other_ledger.reset(),
        "owner binding rejects a same-epoch token from another ledger");

    timing::DspTimingRetirementLedger reset_active_ledger;
    const auto reset_plan = timing_plan(0x0311u);
    const auto reset_token = reset_active_ledger.begin(
        reset_plan,
        authorized_entry(reset_plan),
        timing_pre_state(reset_plan));
    passed &= expect(
        reset_token.has_value() && !reset_active_ledger.reset() &&
            reset_active_ledger.failure() ==
                timing::DspTimingRetirementFailure::
                    ResetWhileInstructionActive &&
            reset_active_ledger.active_token() == reset_token &&
            reset_active_ledger.cancel(
                *reset_token,
                timing::DspTimingCancellationReason::ExplicitAbort) &&
            reset_active_ledger.reset(),
        "reset-active failure preserves the token for authenticated cleanup");

    timing::DspTimingRetirementLedger exhausted_epoch(
        std::numeric_limits<std::uint64_t>::max());
    const auto overflow_plan = timing_plan(0x0312u);
    passed &= expect(
        !exhausted_epoch.begin(
            overflow_plan,
            authorized_entry(overflow_plan),
            timing_pre_state(overflow_plan)) &&
            exhausted_epoch.failure() ==
                timing::DspTimingRetirementFailure::
                    TransactionEpochOverflow &&
            !exhausted_epoch.active() && exhausted_epoch.reset() &&
            !exhausted_epoch.begin(
                overflow_plan,
                authorized_entry(overflow_plan),
                timing_pre_state(overflow_plan)) &&
            exhausted_epoch.failure() ==
                timing::DspTimingRetirementFailure::
                    TransactionEpochOverflow,
        "transaction epoch overflow is permanent for that ledger instance");
    return passed;
}

bool timing_install_plan_and_loop_shapes_fail_closed() {
    namespace timing = galaxy::dsp_timing_runtime;

    bool passed = true;
    auto expect_begin_failure = [&passed](
                                    timing::DspTimingInstructionPlan plan,
                                    timing::DspTimingAuthorizedPlanEntry entry,
                                    timing::DspTimingPreState pre_state,
                                    timing::DspTimingRetirementFailure expected,
                                    const char* message) {
        timing::DspTimingRetirementLedger ledger;
        const auto token = ledger.begin(plan, entry, pre_state);
        passed &= expect(
            !token.has_value() && !ledger.active() &&
                ledger.failure() == expected,
            message);
    };

    auto invalid = timing_plan(0x0400u);
    invalid.binding.resolver_plan_identity = {};
    expect_begin_failure(
        invalid,
        authorized_entry(invalid),
        timing_pre_state(invalid),
        timing::DspTimingRetirementFailure::InvalidInstructionPlan,
        "all-zero resolver table identity is invalid");

    invalid = timing_plan(0x0401u);
    invalid.binding.instruction_word_count = 2u;
    expect_begin_failure(
        invalid,
        authorized_entry(invalid),
        timing_pre_state(invalid),
        timing::DspTimingRetirementFailure::InvalidInstructionPlan,
        "standalone instruction length cannot contradict immediate identity");

    auto exact_immediate = timing_plan(0x0402u);
    exact_immediate.identity.opcode_word = 0x029fu;
    exact_immediate.identity.has_immediate = true;
    exact_immediate.identity.immediate_word = 0x8123u;
    exact_immediate.binding.instruction_word_count = 2u;
    {
        timing::DspTimingRetirementLedger ledger;
        timing::DspTimingInstructionScope scope(
            ledger,
            exact_immediate,
            authorized_entry(exact_immediate),
            timing_pre_state(exact_immediate));
        passed &= expect(
            scope.abort() && !ledger.failed(),
            "exact two-word identity passes install-plan shape validation");
    }

    invalid = parallel_timing_plan(0x0403u, 0u);
    invalid.identity.parallel_primary_word ^= 1u;
    expect_begin_failure(
        invalid,
        authorized_entry(invalid),
        timing_pre_state(invalid),
        timing::DspTimingRetirementFailure::InvalidInstructionIdentity,
        "parallel identity rejects a false primary/extension split");

    invalid = parallel_timing_plan(0x0404u, 0u);
    invalid.binding.validated_bundle = timing::DspTimingBundleKind::Standalone;
    expect_begin_failure(
        invalid,
        authorized_entry(invalid),
        timing_pre_state(invalid),
        timing::DspTimingRetirementFailure::InvalidInstructionPlan,
        "install metadata cannot contradict the validated bundle");

    invalid = timing_plan(0x0405u);
    invalid.binding.instruction_semantic =
        static_cast<timing::DspTimingInstructionSemantic>(0xffu);
    invalid.allowed_instruction_outcomes = 0u;
    expect_begin_failure(
        invalid,
        authorized_entry(invalid),
        timing_pre_state(invalid),
        timing::DspTimingRetirementFailure::InvalidInstructionPlan,
        "unknown install-time instruction semantic fails closed");

    invalid = timing_plan(0x0406u);
    invalid.binding.hardware_loop_site =
        static_cast<timing::DspTimingHardwareLoopSite>(0xffu);
    invalid.allowed_hardware_loop_outcomes = 0u;
    expect_begin_failure(
        invalid,
        authorized_entry(invalid),
        timing_pre_state(invalid),
        timing::DspTimingRetirementFailure::InvalidInstructionPlan,
        "unknown hardware-loop site fails closed");

    invalid = timing_plan(0x0407u);
    invalid.binding.contract_version += 1u;
    expect_begin_failure(
        invalid,
        authorized_entry(invalid),
        timing_pre_state(invalid),
        timing::DspTimingRetirementFailure::InvalidInstructionPlan,
        "unknown runtime timing plan contract version fails closed");

    invalid = timing_plan(
        0x0408u, timing::kDspTimingStandaloneMemoryBit);
    invalid.memory_operands[0].reset();
    expect_begin_failure(
        invalid,
        authorized_entry(invalid),
        timing_pre_state(invalid),
        timing::DspTimingRetirementFailure::InvalidInstructionPlan,
        "memory authority presence must exactly match the authenticated slot mask");

    invalid = timing_plan(
        0x0409u, timing::kDspTimingStandaloneMemoryBit);
    invalid.memory_operands[0] = timing::DspTimingMemoryOperandAuthority{
        timing::DspTimingMemorySpace::Instruction,
        timing::DspTimingMemoryDirection::Write};
    expect_begin_failure(
        invalid,
        authorized_entry(invalid),
        timing_pre_state(invalid),
        timing::DspTimingRetirementFailure::InvalidInstructionPlan,
        "an authenticated plan cannot represent an instruction-memory write");

    invalid = parallel_timing_plan(
        0x040au, timing::kDspTimingParallelSecondaryMemoryBit);
    expect_begin_failure(
        invalid,
        authorized_entry(invalid),
        timing_pre_state(invalid),
        timing::DspTimingRetirementFailure::InvalidInstructionPlan,
        "a parallel secondary slot cannot be authenticated without its canonical primary slot");

    const auto authorized_plan = timing_plan(0x040bu);
    for (std::size_t index = 0u;
         index < timing::kDspTimingResolverIdentityBytes;
         ++index) {
        auto wrong_identity_entry = authorized_entry(authorized_plan);
        wrong_identity_entry.resolver_plan_identity[index] ^= 1u;
        expect_begin_failure(
            authorized_plan,
            wrong_identity_entry,
            timing_pre_state(authorized_plan),
            timing::DspTimingRetirementFailure::UnauthorizedInstructionPlan,
            "runtime compares every resolver identity byte with the table entry");
    }
    auto wrong_entry = authorized_entry(authorized_plan);
    wrong_entry.contract_version += 1u;
    expect_begin_failure(
        authorized_plan,
        wrong_entry,
        timing_pre_state(authorized_plan),
        timing::DspTimingRetirementFailure::UnauthorizedInstructionPlan,
        "runtime compares exact table-entry contract version");

    auto bad_pre_state = timing_pre_state(authorized_plan);
    ++bad_pre_state.fetch_address;
    expect_begin_failure(
        authorized_plan,
        authorized_entry(authorized_plan),
        bad_pre_state,
        timing::DspTimingRetirementFailure::InvalidPreState,
        "prestate fetch address must match the bound instruction identity");
    bad_pre_state = timing_pre_state(authorized_plan);
    timing::DspTimingInstructionIdentity malformed_predecessor{};
    malformed_predecessor.fetch_address = 0x8000u;
    malformed_predecessor.bundle =
        static_cast<timing::DspTimingBundleKind>(0xffu);
    bad_pre_state.predecessor = malformed_predecessor;
    expect_begin_failure(
        authorized_plan,
        authorized_entry(authorized_plan),
        bad_pre_state,
        timing::DspTimingRetirementFailure::InvalidPreState,
        "prestate rejects an invalid predecessor enum shape");

    auto halt = timing_plan(0x0410u);
    halt.identity.opcode_word = 0x0021u;
    set_timing_semantic(halt, timing::DspTimingInstructionSemantic::Halt);
    auto halt_at_loop_end = halt;
    set_hardware_loop_site(
        halt_at_loop_end,
        timing::DspTimingHardwareLoopSite::MayResolveLoopEnd);
    expect_begin_failure(
        halt_at_loop_end,
        authorized_entry(halt_at_loop_end),
        timing_pre_state(halt_at_loop_end),
        timing::DspTimingRetirementFailure::InvalidInstructionPlan,
        "halt plan cannot authorize any hardware-loop edge");

    auto run_outcome_failure = [&passed](
                                   timing::DspTimingInstructionPlan plan,
                                   timing::DspTimingPreState pre_state,
                                   timing::DspTimingPostOutcome outcome,
                                   const char* message) {
        timing::DspTimingRetirementLedger ledger;
        timing::DspTimingInstructionScope scope(
            ledger, plan, authorized_entry(plan), pre_state);
        passed &= expect(
            scope.began() && !scope.commit(outcome).has_value() &&
                ledger.failure() ==
                    timing::DspTimingRetirementFailure::InvalidPostOutcome &&
                ledger.state().retired_instructions == 0u,
            message);
    };

    timing::DspTimingPostOutcome back_edge{};
    back_edge.hardware_loop =
        timing::DspTimingHardwareLoopOutcome::BackEdgeTaken;
    run_outcome_failure(
        timing_plan(0x0411u),
        timing_pre_state(timing_plan(0x0411u)),
        back_edge,
        "non-loop-end plan rejects a hardware back edge");
    back_edge.hardware_loop = timing::DspTimingHardwareLoopOutcome::Exited;
    run_outcome_failure(
        timing_plan(0x0417u),
        timing_pre_state(timing_plan(0x0417u)),
        back_edge,
        "non-loop-end plan rejects a hardware-loop exit");
    auto loop_end = timing_plan(0x0412u);
    set_hardware_loop_site(
        loop_end, timing::DspTimingHardwareLoopSite::MayResolveLoopEnd);
    for (const auto edge : {
             timing::DspTimingHardwareLoopOutcome::BackEdgeTaken,
             timing::DspTimingHardwareLoopOutcome::Exited}) {
        timing::DspTimingPostOutcome halted_edge{};
        halted_edge.instruction =
            timing::DspTimingInstructionOutcome::Halted;
        halted_edge.hardware_loop = edge;
        run_outcome_failure(
            halt,
            timing_pre_state(halt),
            halted_edge,
            "halt outcome cannot be combined with either hardware-loop edge");

        for (const auto stack_index : {0u, 2u, 3u}) {
            auto zero_depth = timing_pre_state(loop_end);
            zero_depth.stack_depths[stack_index] = 0u;
            timing::DspTimingPostOutcome edge_outcome{};
            edge_outcome.hardware_loop = edge;
            run_outcome_failure(
                loop_end,
                zero_depth,
                edge_outcome,
                "hardware-loop edge requires nonzero target/address/counter depths");
        }

        auto high_depth = timing_pre_state(loop_end);
        high_depth.stack_depths[0] =
            std::numeric_limits<std::uint8_t>::max();
        high_depth.stack_depths[1] = 0u;
        high_depth.stack_depths[2] =
            std::numeric_limits<std::uint8_t>::max();
        high_depth.stack_depths[3] =
            std::numeric_limits<std::uint8_t>::max();
        timing::DspTimingPostOutcome edge_outcome{};
        edge_outcome.hardware_loop = edge;
        timing::DspTimingRetirementLedger ledger;
        timing::DspTimingInstructionScope scope(
            ledger, loop_end, authorized_entry(loop_end), high_depth);
        passed &= expect(
            scope.commit(edge_outcome).has_value() && !ledger.failed(),
            "foundation preserves logical u8 stack depths without guessing a 31/32 maximum");
    }

    timing::DspTimingPostOutcome invalid_outcome{};
    invalid_outcome.instruction =
        static_cast<timing::DspTimingInstructionOutcome>(0xffu);
    run_outcome_failure(
        timing_plan(0x0413u),
        timing_pre_state(timing_plan(0x0413u)),
        invalid_outcome,
        "unknown instruction outcome enum fails closed");
    invalid_outcome = {};
    invalid_outcome.hardware_loop =
        static_cast<timing::DspTimingHardwareLoopOutcome>(0xffu);
    run_outcome_failure(
        timing_plan(0x0414u),
        timing_pre_state(timing_plan(0x0414u)),
        invalid_outcome,
        "unknown hardware-loop outcome enum fails closed");
    invalid_outcome = {};
    invalid_outcome.interrupt =
        static_cast<timing::DspTimingInterruptOutcome>(0xffu);
    run_outcome_failure(
        timing_plan(0x0415u),
        timing_pre_state(timing_plan(0x0415u)),
        invalid_outcome,
        "unknown interrupt outcome enum fails closed");
    invalid_outcome = {};
    invalid_outcome.condition =
        static_cast<timing::DspTimingConditionOutcome>(0xffu);
    run_outcome_failure(
        timing_plan(0x0416u),
        timing_pre_state(timing_plan(0x0416u)),
        invalid_outcome,
        "unknown condition outcome enum fails closed");
    return passed;
}

bool dsp_cycle_grants_are_integer_deterministic_and_fail_closed() {
    namespace timing = galaxy::dsp_timing_runtime;

    constexpr std::uint64_t kSyntheticClockHz = 7u;
    constexpr std::uint64_t kAnchor = 123u;
    constexpr auto kTimelineTicksPerSecond =
        galaxy::timing::kTimelineTicksPerSecond;
    static_assert(
        !HasUnauthenticatedCycleRestore<timing::DspCycleGrantAccumulator> &&
        !std::is_copy_constructible_v<timing::DspCycleGrantAccumulator> &&
        !std::is_copy_assignable_v<timing::DspCycleGrantAccumulator> &&
        !std::is_move_constructible_v<timing::DspCycleGrantAccumulator> &&
        !std::is_move_assignable_v<timing::DspCycleGrantAccumulator>);
    timing::DspCycleGrantAccumulator incremental;
    bool passed = expect(
        !timing::kDspCycleAuthenticatedCheckpointRestoreAvailable &&
            !HasUnauthenticatedCycleRestore<
                timing::DspCycleGrantAccumulator> &&
            incremental.failed() &&
            incremental.failure() == timing::DspCycleGrantFailure::InvalidClock,
        "DSP cycle accumulator exposes neither implicit initialization nor unauthenticated restore");
    passed &= expect(
        incremental.reset_fresh(kSyntheticClockHz, kAnchor),
        "fresh reset initializes a zero-history clock and timeline anchor");
    for (std::uint32_t index = 0u; index < 7u; ++index) {
        passed &= expect(
            incremental.grant(1u).has_value(),
            "positive DSP cycle grant succeeds");
    }

    timing::DspCycleGrantAccumulator grouped(kSyntheticClockHz, kAnchor);
    const auto grouped_grant = grouped.grant(7u);
    const auto incremental_state = incremental.state();
    const auto grouped_state = grouped.state();
    passed &= expect(
        grouped_grant.has_value() &&
            incremental_state == grouped_state &&
            grouped_state.timeline_ticks ==
                kAnchor + kTimelineTicksPerSecond &&
            grouped_state.fractional_remainder == 0u,
        "grouped and incremental integer cycle grants are identical");

    passed &= expect(
        timing::detail::validate_cycle_checkpoint_arithmetic(
            grouped_state, kSyntheticClockHz, kAnchor) ==
            timing::DspCycleCheckpointArithmeticFailure::None,
        "internal checkpoint arithmetic reconstructs an owned grouped state exactly");

    const timing::DspCycleGrantState recomputed_forged_history{
        kSyntheticClockHz,
        14u,
        kAnchor,
        kAnchor + 2u * kTimelineTicksPerSecond,
        0u,
    };
    const auto owned_state_before_forgery_check = grouped.state();
    passed &= expect(
        timing::detail::validate_cycle_checkpoint_arithmetic(
            recomputed_forged_history, kSyntheticClockHz, kAnchor) ==
                timing::DspCycleCheckpointArithmeticFailure::None &&
            !timing::kDspCycleAuthenticatedCheckpointRestoreAvailable &&
            grouped.state() == owned_state_before_forgery_check,
        "self-consistent recomputed history is not authority and cannot be installed");

    auto wrong_clock = grouped_state;
    wrong_clock.dsp_clock_hz = kSyntheticClockHz + 1u;
    passed &= expect(
        timing::detail::validate_cycle_checkpoint_arithmetic(
            wrong_clock, kSyntheticClockHz, kAnchor) ==
            timing::DspCycleCheckpointArithmeticFailure::ClockMismatch,
        "checkpoint arithmetic rejects a clock different from the immutable expectation");
    auto wrong_anchor = grouped_state;
    ++wrong_anchor.timeline_anchor_ticks;
    ++wrong_anchor.timeline_ticks;
    passed &= expect(
        timing::detail::validate_cycle_checkpoint_arithmetic(
            wrong_anchor, kSyntheticClockHz, kAnchor) ==
            timing::DspCycleCheckpointArithmeticFailure::AnchorMismatch,
        "checkpoint arithmetic rejects a recomputed but unexpected anchor");
    passed &= expect(
        timing::detail::validate_cycle_checkpoint_arithmetic(
            grouped_state, 0u, kAnchor) ==
            timing::DspCycleCheckpointArithmeticFailure::
                InvalidExpectedClock,
        "checkpoint arithmetic requires a nonzero immutable expected clock");

    grouped.abort();
    passed &= expect(
        grouped.failure() == timing::DspCycleGrantFailure::Aborted &&
            !grouped.grant(1u).has_value(),
        "aborted DSP cycle timeline refuses every later grant");
    passed &= expect(
        grouped.reset_fresh(kSyntheticClockHz, kAnchor) &&
            grouped.state().total_dsp_cycles == 0u &&
            grouped.grant(1u).has_value(),
        "fresh reset discards prior history and clears a prior abort");

    passed &= expect(
        timing::detail::validate_cycle_checkpoint_arithmetic(
            {kSyntheticClockHz,
             0u,
             kAnchor,
             kAnchor,
             kSyntheticClockHz},
            kSyntheticClockHz,
            kAnchor) ==
            timing::DspCycleCheckpointArithmeticFailure::
                InvalidFractionalRemainder,
        "checkpoint arithmetic rejects a remainder outside the exact clock range");
    passed &= expect(
        timing::detail::validate_cycle_checkpoint_arithmetic(
            {kSyntheticClockHz, 7u, kAnchor, kAnchor, 0u},
            kSyntheticClockHz,
            kAnchor) ==
            timing::DspCycleCheckpointArithmeticFailure::InvalidTimeline,
        "checkpoint arithmetic rejects timeline ticks inconsistent with total cycles");
    passed &= expect(
        timing::detail::validate_cycle_checkpoint_arithmetic(
            {kSyntheticClockHz,
             1u,
             kAnchor,
             kAnchor + kTimelineTicksPerSecond / kSyntheticClockHz,
             1u},
            kSyntheticClockHz,
            kAnchor) ==
            timing::DspCycleCheckpointArithmeticFailure::
                InvalidFractionalRemainder,
        "checkpoint arithmetic rejects a remainder inconsistent with total cycles");
    passed &= expect(
        timing::detail::validate_cycle_checkpoint_arithmetic(
            {1u,
             std::numeric_limits<std::uint64_t>::max(),
             0u,
             0u,
             0u},
            1u,
            0u) ==
            timing::DspCycleCheckpointArithmeticFailure::ArithmeticOverflow,
        "checkpoint arithmetic rejects a total-cycle quotient wider than u64");
    passed &= expect(
        timing::detail::validate_cycle_checkpoint_arithmetic(
            {kTimelineTicksPerSecond,
             1u,
             std::numeric_limits<std::uint64_t>::max(),
             std::numeric_limits<std::uint64_t>::max(),
             0u},
            kTimelineTicksPerSecond,
            std::numeric_limits<std::uint64_t>::max()) ==
            timing::DspCycleCheckpointArithmeticFailure::ArithmeticOverflow,
        "checkpoint arithmetic rejects anchor-plus-quotient overflow");

    const timing::DspCycleGrantState max_total_exact{
        kTimelineTicksPerSecond,
        std::numeric_limits<std::uint64_t>::max(),
        0u,
        std::numeric_limits<std::uint64_t>::max(),
        0u,
    };
    passed &= expect(
        timing::detail::validate_cycle_checkpoint_arithmetic(
            max_total_exact, kTimelineTicksPerSecond, 0u) ==
                timing::DspCycleCheckpointArithmeticFailure::None,
        "portable 128-bit arithmetic reconstructs an exact maximum total");
    timing::DspCycleGrantFailure transition_failure{
        timing::DspCycleGrantFailure::None};
    passed &= expect(
        !timing::detail::advance_owned_cycle_state(
             max_total_exact, 1u, transition_failure)
             .has_value() &&
            transition_failure ==
                timing::DspCycleGrantFailure::TotalCycleOverflow,
        "owned-state transition fails closed at total-cycle overflow");

    constexpr auto kMaxU64 = std::numeric_limits<std::uint64_t>::max();
    const timing::DspCycleGrantState max_divisor_exact{
        kMaxU64,
        kMaxU64,
        11u,
        11u + kTimelineTicksPerSecond,
        0u,
    };
    passed &= expect(
        timing::detail::validate_cycle_checkpoint_arithmetic(
            max_divisor_exact, kMaxU64, 11u) ==
            timing::DspCycleCheckpointArithmeticFailure::None,
        "u128/u64 reconstruction handles maximum dividend factor and divisor exactly");

    const timing::DspCycleGrantState max_divisor_fractional{
        kMaxU64,
        kMaxU64 - 1u,
        0u,
        kTimelineTicksPerSecond - 1u,
        kMaxU64 - kTimelineTicksPerSecond,
    };
    passed &= expect(
        timing::detail::validate_cycle_checkpoint_arithmetic(
            max_divisor_fractional, kMaxU64, 0u) ==
            timing::DspCycleCheckpointArithmeticFailure::None,
        "u128/u64 reconstruction preserves quotient and nonzero remainder at u64 edges");

    passed &= expect(
        grouped.reset_fresh(
            1u, std::numeric_limits<std::uint64_t>::max()) &&
            !grouped.grant(1u).has_value() &&
            grouped.failure() == timing::DspCycleGrantFailure::TimelineOverflow &&
            grouped.state() ==
                timing::DspCycleGrantState{
                    1u,
                    0u,
                    std::numeric_limits<std::uint64_t>::max(),
                    std::numeric_limits<std::uint64_t>::max(),
                    0u},
        "timeline tick overflow is sticky and fail-closed");
    passed &= expect(
        grouped.reset_fresh(kSyntheticClockHz, kAnchor) &&
            !grouped.grant(0u).has_value() &&
            grouped.failure() == timing::DspCycleGrantFailure::ZeroCycles,
        "zero-cycle timing rule cannot enter the runtime timeline");
    passed &= expect(
        !grouped.reset_fresh(0u, kAnchor) &&
            grouped.failure() == timing::DspCycleGrantFailure::InvalidClock,
        "zero DSP clock fails closed");
    passed &= expect(
        grouped.reset_fresh(kSyntheticClockHz, kAnchor) &&
            !grouped.failed() && grouped.grant(7u).has_value(),
        "valid fresh reset recovers after every owned-state arithmetic failure");
    return passed;
}

bool snapshot_dram_equals(
    const galaxy::DspDiagnosticSnapshot& snapshot,
    std::uint16_t address,
    std::uint16_t expected) {
    std::uint16_t value = 0u;
    return snapshot.read_dram_word(address, value) && value == expected;
}

bool halted_snapshot_at(
    const std::optional<galaxy::DspDiagnosticSnapshot>& snapshot,
    std::uint16_t pc,
    std::uint64_t completed_runs) {
    return snapshot.has_value() && snapshot->halted && snapshot->pc == pc &&
           snapshot->worker_completed_runs >= completed_runs;
}

struct Probe {
    std::atomic<std::uint32_t> interrupt_count{};
    std::atomic<std::uint32_t> accelerator_exception_count{};
    galaxy::DspAcceleratorException last_accelerator_exception{};
};

void record_interrupt(void* user) {
    auto* const probe = static_cast<Probe*>(user);
    ++probe->interrupt_count;
}

void record_accelerator_exception(
    void* user,
    galaxy::DspAcceleratorException exception) {
    auto* const probe = static_cast<Probe*>(user);
    ++probe->accelerator_exception_count;
    probe->last_accelerator_exception = exception;
}

void compiled_dsp_probe(galaxy::DspContext& context) {
    context.dram[0x0007] = 0x55aau;
    galaxy::dsp_data_write(context, 0xfffc, 0xdcd1u);
    galaxy::dsp_data_write(context, 0xfffd, 0x0000u);
    galaxy::dsp_data_write(context, 0xfffb, 1u);
    context.halted = true;
}

void compiled_worker_probe(galaxy::DspContext& context) {
    context.dram[0x0008] = static_cast<std::uint16_t>(context.dram[0x0008] + 1u);
    context.halted = true;
}

void compiled_telemetry_probe(galaxy::DspContext& context) {
    // Generated AOT code emits exactly this assignment before each lowered
    // instruction. Three markers let the test prove dynamic counting and the
    // entry-return publication boundary without depending on a handwritten
    // estimate of an instruction block.
    context.last_retired_pc = 0x0100u;
    context.last_retired_pc = 0x0101u;
    context.last_retired_pc = 0x0102u;
    context.halted = true;
}

void feed_idle_sequence_markers(
    galaxy::DspContext& context,
    std::span<const std::uint16_t> sequence) {
    for (const auto pc : sequence) {
        context.last_retired_pc = pc;
    }
}

void configure_idle_fixed_point(
    galaxy::DspContext& context,
    galaxy::DspRmge01IdleBackEdgeKind kind) {
    context.sr = galaxy::kDspSrExtIntEnable;
    switch (kind) {
    case galaxy::DspRmge01IdleBackEdgeKind::ZeroQueue:
        context.dram[0x0351u] = 0x1234u;
        context.dram[0x0352u] = 0u;
        context.ar[0] = context.dram[0x0351u];
        context.wr[0] = 0xFFFFu;
        context.ax[0][1] = 0u;
        context.sr = galaxy::dsp_status_16(
            0, false, false, false, context.sr);
        context.pc = galaxy::kRmge01DspZeroQueueIdleTargetPc;
        return;
    case galaxy::DspRmge01IdleBackEdgeKind::CommandWait:
        context.dram[0x034Eu] = 1u;
        context.dram[0x0354u] = 2u;
        {
            const auto clear_ac0 =
                galaxy::dsp_accumulator_set_value(0, context.sr);
            const auto clear_ac1 =
                galaxy::dsp_accumulator_set_value(0, clear_ac0.status);
            context.ac[1] = galaxy::dsp_write_accumulator_mid(
                clear_ac1.value,
                context.dram[0x0354u],
                clear_ac1.status);
            context.ac[0] = galaxy::dsp_write_accumulator_mid(
                clear_ac0.value,
                context.dram[0x034Eu],
                clear_ac1.status);
            context.sr = galaxy::dsp_accumulator_subtract(
                             context.ac[0],
                             context.ac[1],
                             clear_ac1.status)
                             .status;
        }
        context.pc = galaxy::kRmge01DspCommandWaitIdleTargetPc;
        return;
    case galaxy::DspRmge01IdleBackEdgeKind::None:
        std::abort();
    }
}

void idle_sequence_invocation_reset_probe(galaxy::DspContext& context) {
    const auto* const tracker =
        context.last_retired_pc.idle_sequence_tracker;
    context.dram[0x003Fu] =
        tracker != nullptr &&
                tracker->kind == galaxy::DspRmge01IdleBackEdgeKind::None &&
                tracker->progress == 0u
            ? 1u
            : 0u;
}

struct IdleSequenceHarness {
    IdleSequenceHarness() {
        context.pending_idle_backedges = &pending;
        context.host_telemetry = &telemetry;
        context.last_retired_pc.idle_sequence_tracker = &tracker;
    }

    void feed(std::span<const std::uint16_t> sequence) {
        feed_idle_sequence_markers(context, sequence);
    }

    void invoke(galaxy::DspRmge01IdleBackEdgeKind kind) {
        switch (kind) {
        case galaxy::DspRmge01IdleBackEdgeKind::ZeroQueue:
            galaxy::dsp_native_handle_static_backedge(
                context,
                galaxy::kRmge01DspZeroQueueIdleSourcePc,
                galaxy::kRmge01DspZeroQueueIdleTargetPc);
            return;
        case galaxy::DspRmge01IdleBackEdgeKind::CommandWait:
            galaxy::dsp_native_handle_static_backedge(
                context,
                galaxy::kRmge01DspCommandWaitIdleSourcePc,
                galaxy::kRmge01DspCommandWaitIdleTargetPc);
            return;
        case galaxy::DspRmge01IdleBackEdgeKind::None:
            std::abort();
        }
    }

    galaxy::DspNativeTelemetrySnapshot publish() {
        galaxy::dsp_native_telemetry_publish_pending_worker_counts(context);
        return galaxy::dsp_native_telemetry_snapshot(telemetry);
    }

    std::unique_ptr<galaxy::DspContext> context_storage{
        std::make_unique<galaxy::DspContext>()};
    galaxy::DspContext& context{*context_storage};
    galaxy::DspRmge01IdleSequenceTracker tracker{};
    galaxy::DspPendingIdleBackEdgeCounters pending{};
    galaxy::DspNativeTelemetryState telemetry{};
};

// A faithful stand-in for the real ucode's idle/command loop: it never returns
// on its own. It polls the CPU mailbox high half for the mail-present bit
// exactly as the lowered RMGE01 ucode does (jdsp: `LRS @CMBH; ANDCF #0x8000`),
// consumes the command, echoes it back through the DSP mailbox, and raises
// DIRQ. The host breaks it out via request_abort(), which makes the CMBH read
// throw DspExecutionAborted. This exercises the whole free-running path:
// dedicated thread, cross-thread mailbox visibility, interrupt routing, and
// cooperative shutdown — the mechanics the real ucode depends on.
void free_running_echo_ucode(galaxy::DspContext& context) {
    for (;;) {
        context.pc = 0x07CCu;
        context.last_retired_pc = 0x07CCu;
        while ((galaxy::dsp_data_read(context, 0xFFFEu) & 0x8000u) == 0u) {
            // The host-assisted CMBH read parks this compiled instruction
            // until mail, PIINT, or cooperative abort changes the condition.
        }
        const auto high = static_cast<std::uint16_t>(
            galaxy::dsp_data_read(context, 0xFFFEu) & 0x7FFFu);
        const auto low = galaxy::dsp_data_read(context, 0xFFFFu);  // clears busy
        galaxy::dsp_data_write(context, 0xFFFCu, high);  // DMBH (clears busy)
        galaxy::dsp_data_write(context, 0xFFFDu, low);   // DMBL (sets busy)
        galaxy::dsp_data_write(context, 0xFFFBu, 1u);    // DIRQ -> host interrupt
    }
}

// A cancellation boundary can reject an in-flight host span after shutdown
// has been requested. The generated entry then reaches its ordinary hard-trap
// path rather than returning DspExecutionAborted directly. Keep this focused
// stand-in so the worker's shutdown classification remains covered separately
// from an actual, non-shutdown DSP fault.
std::atomic<bool> g_shutdown_trap_entry_ready{};

void shutdown_trap_after_abort_ucode(galaxy::DspContext& context) {
    context.pc = 0x05EFu;
    context.last_retired_pc = 0x05EFu;
    g_shutdown_trap_entry_ready.store(true, std::memory_order_release);
    for (;;) {
        try {
            galaxy::dsp_throw_if_host_abort_requested(context);
        } catch (const galaxy::DspExecutionAborted&) {
            galaxy::dsp_hard_trap_at_current(
                context,
                "test cancellation rejected an in-flight DSP host span");
        }
        std::this_thread::yield();
    }
}

void non_shutdown_hard_trap_ucode(galaxy::DspContext& context) {
    context.pc = 0x05F0u;
    context.last_retired_pc = 0x05F0u;
    galaxy::dsp_hard_trap_at_current(
        context, "test non-shutdown DSP hard trap");
}

void resumable_halt_echo_ucode(galaxy::DspContext& context) {
    if (context.pc == 0x0010u) {
        goto pc_resume;
    }
    if (context.pc != 0u) {
        context.halted = true;
        return;
    }
    galaxy::dsp_data_write(context, 0xFFFCu, 0xDCD1u);
    galaxy::dsp_data_write(context, 0xFFFDu, 0x0000u);
    galaxy::dsp_data_write(context, 0xFFFBu, 1u);
    context.pc = 1u;
    context.halted = true;
    return;

pc_resume:
    while ((galaxy::dsp_data_read(context, 0xFFFEu) & 0x8000u) == 0u) {
        // Wait for the host to publish the resume mail.
    }
    const auto high = static_cast<std::uint16_t>(
        galaxy::dsp_data_read(context, 0xFFFEu) & 0x7FFFu);
    const auto low = galaxy::dsp_data_read(context, 0xFFFFu);
    galaxy::dsp_data_write(context, 0xFFFCu, high);
    galaxy::dsp_data_write(context, 0xFFFDu, low);
    galaxy::dsp_data_write(context, 0xFFFBu, 1u);
    context.pc = 1u;
    context.halted = true;
}

void vector_resumable_halt_echo_ucode(galaxy::DspContext& context) {
    if (context.pc == 0x0010u) {
        goto pc_resume_vector;
    }
    if (context.pc != 0u) {
        context.halted = true;
        return;
    }
    galaxy::dsp_data_write(context, 0xFFFCu, 0xDCD1u);
    galaxy::dsp_data_write(context, 0xFFFDu, 0x0000u);
    galaxy::dsp_data_write(context, 0xFFFBu, 1u);
    context.pc = 0x0748u;
    context.halted = true;
    return;

pc_resume_vector:
    while ((galaxy::dsp_data_read(context, 0xFFFEu) & 0x8000u) == 0u) {
        // Wait for the host to publish the vector-resume mail.
    }
    const auto high = static_cast<std::uint16_t>(
        galaxy::dsp_data_read(context, 0xFFFEu) & 0x7FFFu);
    const auto low = galaxy::dsp_data_read(context, 0xFFFFu);
    galaxy::dsp_data_write(context, 0xFFFCu, high);
    galaxy::dsp_data_write(context, 0xFFFDu, low);
    galaxy::dsp_data_write(context, 0xFFFBu, 1u);
    context.pc = 0x0748u;
    context.halted = true;
}

void external_interrupt_count_ucode(galaxy::DspContext& context) {
    if (context.pc == 0x000Eu) {
        goto pc_external_interrupt;
    }

    context.sr = static_cast<std::uint16_t>(
        context.sr | galaxy::kDspSrExtIntEnable);
    for (;;) {
        context.pc = galaxy::kRmge01DspCmbhCommandPollPc;
        (void)galaxy::dsp_data_read(context, 0xFFFEu);
    }

pc_external_interrupt:
    context.dram[0x0010] = context.st[0];
    context.dram[0x0011] = context.st[1];
    context.pc = galaxy::kRmge01DspCmbhTaskPollPc;
    while ((galaxy::dsp_data_read(context, 0xFFFEu) & 0x8000u) == 0u) {
    }
    const auto count = galaxy::dsp_data_read(context, 0xFFFFu);
    galaxy::dsp_data_write(context, 0xFFFCu, 0x6E00u);
    galaxy::dsp_data_write(context, 0xFFFDu, count);
    galaxy::dsp_data_write(context, 0xFFFBu, 1u);
    context.sr = galaxy::dsp_stack_pop(context, 1);
    context.pc = galaxy::dsp_stack_pop(context, 0);
    context.halted = true;
}

void disabled_external_interrupt_halt_ucode(galaxy::DspContext& context) {
    if (context.pc == 0x000Eu) {
        context.dram[0x0020] = 0xBEEFu;
        context.halted = true;
        return;
    }

    context.sr = static_cast<std::uint16_t>(
        context.sr & ~galaxy::kDspSrExtIntEnable);
    context.pc = 0x0123u;
    context.halted = true;
}

void halted_external_interrupt_returns_to_halt_ucode(galaxy::DspContext& context) {
    if (context.pc == 0x000Eu) {
        goto pc_external_interrupt;
    }
    if (context.pc == 0u) {
        context.sr = static_cast<std::uint16_t>(
            context.sr | galaxy::kDspSrExtIntEnable);
        context.pc = 0x0123u;
        context.halted = true;
        return;
    }
    if (context.pc == 0x0123u) {
        context.dram[0x0032] = 0xBADu;
    }
    context.halted = true;
    return;

pc_external_interrupt:
    context.dram[0x0030] = context.st[0];
    context.dram[0x0031] = context.st[1];
    context.sr = galaxy::dsp_stack_pop(context, 1);
    context.pc = galaxy::dsp_stack_pop(context, 0);
    context.halted = true;
}

// Deterministic adversarial window for the worker's HALT publication. The
// lowered entry has already made the architectural HALT visible but is held
// before returning to NativeDspWorker, so its mutex-owned state is necessarily
// still Executing when the CPU posts the resume mail.
std::atomic<std::uint32_t> g_resume_before_wait_stage{};

void resume_before_wait_ucode(galaxy::DspContext& context) {
    if (context.pc == 0x0010u) {
        const auto high = static_cast<std::uint16_t>(
            galaxy::dsp_data_read(context, 0xFFFEu) & 0x7FFFu);
        const auto low = galaxy::dsp_data_read(context, 0xFFFFu);
        context.dram[0x0040u] =
            static_cast<std::uint16_t>(context.dram[0x0040u] + 1u);
        context.dram[0x0041u] = high;
        context.dram[0x0042u] = low;
        context.pc = 0x0001u;
        context.halted = true;
        return;
    }
    if (context.pc != 0u) {
        galaxy::dsp_hard_trap_at_current(
            context, "resume-before-wait test reached an invalid PC");
    }

    context.pc = 0x0001u;
    context.halted = true;
    g_resume_before_wait_stage.store(1u, std::memory_order_release);
    while (g_resume_before_wait_stage.load(std::memory_order_acquire) == 1u) {
        galaxy::dsp_throw_if_host_abort_requested(context);
        std::this_thread::yield();
    }
}

// Companion interleaving: the resume intent is retained while Executing, but
// the same active entry consumes that mailbox before it returns HALTed. The
// conditional latch must then be resolved without becoming a stale future
// resume.
std::atomic<std::uint32_t> g_consumed_before_halt_stage{};

void consumed_before_halt_ucode(galaxy::DspContext& context) {
    if (context.pc != 0u) {
        context.dram[0x0045u] =
            static_cast<std::uint16_t>(context.dram[0x0045u] + 1u);
        context.halted = true;
        return;
    }

    g_consumed_before_halt_stage.store(1u, std::memory_order_release);
    while (g_consumed_before_halt_stage.load(std::memory_order_acquire) == 1u) {
        galaxy::dsp_throw_if_host_abort_requested(context);
        std::this_thread::yield();
    }
    const auto high = static_cast<std::uint16_t>(
        galaxy::dsp_data_read(context, 0xFFFEu) & 0x7FFFu);
    const auto low = galaxy::dsp_data_read(context, 0xFFFFu);
    context.dram[0x0043u] = high;
    context.dram[0x0044u] = low;
    context.pc = 0x0001u;
    context.halted = true;
}

void park_on_empty_cpu_mailbox(galaxy::DspContext& context) {
    // Enter the already-proven CMBH event path immediately after the DMBH
    // scenario completes. This keeps the free-running entry alive until the
    // test explicitly aborts it, without burning a host core.
    context.pc = galaxy::kRmge01DspCmbhTaskPollPc;
    for (;;) {
        context.last_retired_pc = galaxy::kRmge01DspCmbhTaskPollPc;
        (void)galaxy::dsp_data_read(context, 0xFFFEu);
    }
}

std::atomic<std::uint32_t> g_cmbh_publish_before_wait_stage{};
std::atomic<std::uint32_t> g_cmbh_publish_before_wait_completed{};
std::atomic<std::uint16_t> g_classified_cmbh_poll_pc{
    galaxy::kRmge01DspCmbhTaskPollPc};
std::atomic<std::uint16_t> g_classified_cmbh_completed{};

void classified_cmbh_once_then_idle_ucode(galaxy::DspContext& context) {
    if (context.pc != 0u) {
        galaxy::dsp_hard_trap_at_current(
            context, "classified CMBH test reached an invalid entry PC");
    }

    const auto poll_pc = g_classified_cmbh_poll_pc.load(
        std::memory_order_acquire);
    if (!galaxy::dsp_is_rmge01_cmbh_poll_pc(poll_pc)) {
        galaxy::dsp_hard_trap_at_current(
            context, "classified CMBH test received a non-CMBH poll PC");
    }

    // PCs 0x078F and 0x0795 use `(CR << 8) | 0xFE` in the generated ucode.
    // Exercise that exact address formation rather than substituting a literal
    // IFX address in the regression test.
    context.cr = 0x00FFu;
    const auto cmbh_address = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(context.cr << 8u) | 0x00FEu);
    const auto cmbl_address = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(context.cr << 8u) | 0x00FFu);
    context.pc = poll_pc;
    context.last_retired_pc = poll_pc;
    const auto high = galaxy::dsp_data_read(context, cmbh_address);
    if ((high & 0x8000u) == 0u) {
        galaxy::dsp_hard_trap_at_current(
            context, "classified CMBH wait returned without a CPU mail");
    }
    const auto low = galaxy::dsp_data_read(context, cmbl_address);
    context.dram[0x0057u] = static_cast<std::uint16_t>(high & 0x7FFFu);
    context.dram[0x0058u] = low;
    g_classified_cmbh_completed.store(1u, std::memory_order_release);
    park_on_empty_cpu_mailbox(context);
}

void cmbh_publish_before_wait_ucode(galaxy::DspContext& context) {
    if (context.pc != 0u) {
        galaxy::dsp_hard_trap_at_current(
            context,
            "CMBH publish-before-wait test reached an invalid entry PC");
    }

    // Hold the compiled entry before its first CMBH read so the CPU can
    // publish both mailbox halves and advance the wake generation first.
    // atomic::wait also makes this test barrier event-driven and race-free.
    g_cmbh_publish_before_wait_stage.store(1u, std::memory_order_release);
    g_cmbh_publish_before_wait_stage.notify_one();
    g_cmbh_publish_before_wait_stage.wait(1u, std::memory_order_acquire);

    const auto poll_pc = g_classified_cmbh_poll_pc.load(
        std::memory_order_acquire);
    if (!galaxy::dsp_is_rmge01_cmbh_poll_pc(poll_pc)) {
        galaxy::dsp_hard_trap_at_current(
            context, "CMBH publish-before-wait received a non-CMBH poll PC");
    }
    context.cr = 0x00FFu;
    const auto cmbh_address = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(context.cr << 8u) | 0x00FEu);
    const auto cmbl_address = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(context.cr << 8u) | 0x00FFu);
    context.pc = poll_pc;
    context.last_retired_pc = poll_pc;
    const auto high = static_cast<std::uint16_t>(
        galaxy::dsp_data_read(context, cmbh_address) & 0x7FFFu);
    const auto low = galaxy::dsp_data_read(context, cmbl_address);
    context.dram[0x0055u] = high;
    context.dram[0x0056u] = low;
    g_cmbh_publish_before_wait_completed.store(
        1u, std::memory_order_release);
    park_on_empty_cpu_mailbox(context);
}

std::atomic<std::uint16_t> g_dmbh_one_shot_completed{};
std::atomic<std::uint16_t> g_dmbh_repeated_completed{};
std::atomic<std::uint16_t> g_dmbh_consume_before_wait_completed{};
std::atomic<std::uint16_t> g_dmbh_external_interrupt_completed{};
std::atomic<std::uint16_t> g_classified_dmbh_completed{};
std::atomic<std::uint32_t> g_classified_dmbh_consume_before_wait_stage{};

void classified_dmbh_once_then_idle_ucode(galaxy::DspContext& context) {
    if (context.pc != 0u) {
        galaxy::dsp_hard_trap_at_current(
            context, "classified DMBH test reached an invalid entry PC");
    }

    context.last_retired_pc = 0x07ABu;
    galaxy::dsp_data_write(context, 0xFFFCu, 0x5456u);
    context.last_retired_pc = 0x07ACu;
    galaxy::dsp_data_write(context, 0xFFFDu, 0xC789u);
    context.cr = 0x00FFu;
    const auto dmbh_address = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(context.cr << 8u) | 0x00FCu);
    context.pc = galaxy::kRmge01DspDmbhAc0PollPc;
    context.last_retired_pc = galaxy::kRmge01DspDmbhAc0PollPc;
    if ((galaxy::dsp_data_read(context, dmbh_address) & 0x8000u) != 0u) {
        galaxy::dsp_hard_trap_at_current(
            context, "classified DMBH wait returned before CPU consumption");
    }
    context.dram[0x0059u] = 1u;
    g_classified_dmbh_completed.store(1u, std::memory_order_release);
    park_on_empty_cpu_mailbox(context);
}

void classified_dmbh_consume_before_wait_ucode(
    galaxy::DspContext& context) {
    if (context.pc != 0u) {
        galaxy::dsp_hard_trap_at_current(
            context,
            "classified DMBH consume-before-wait reached an invalid entry PC");
    }

    context.last_retired_pc = 0x07ABu;
    galaxy::dsp_data_write(context, 0xFFFCu, 0x5567u);
    context.last_retired_pc = 0x07ACu;
    galaxy::dsp_data_write(context, 0xFFFDu, 0xD89Au);
    g_classified_dmbh_consume_before_wait_stage.store(
        1u, std::memory_order_release);
    g_classified_dmbh_consume_before_wait_stage.notify_one();
    g_classified_dmbh_consume_before_wait_stage.wait(
        1u, std::memory_order_acquire);

    context.cr = 0x00FFu;
    const auto dmbh_address = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(context.cr << 8u) | 0x00FCu);
    context.pc = galaxy::kRmge01DspDmbhAc0PollPc;
    context.last_retired_pc = galaxy::kRmge01DspDmbhAc0PollPc;
    if ((galaxy::dsp_data_read(context, dmbh_address) & 0x8000u) != 0u) {
        galaxy::dsp_hard_trap_at_current(
            context,
            "classified DMBH consume-before-wait retained a consumed mail");
    }
    context.dram[0x005Au] = 1u;
    g_classified_dmbh_completed.store(1u, std::memory_order_release);
    park_on_empty_cpu_mailbox(context);
}

void dmbh_one_shot_then_idle_ucode(galaxy::DspContext& context) {
    if (context.pc != 0u) {
        galaxy::dsp_hard_trap_at_current(
            context, "DMBH one-shot test reached an invalid entry PC");
    }

    context.last_retired_pc = 0x07B1u;
    galaxy::dsp_data_write(context, 0xFFFCu, 0x5123u);
    context.last_retired_pc = 0x07B2u;
    galaxy::dsp_data_write(context, 0xFFFDu, 0xA456u);
    context.pc = 0x07B3u;
    for (;;) {
        context.last_retired_pc = 0x07B3u;
        if ((galaxy::dsp_data_read(context, 0xFFFCu) & 0x8000u) == 0u) {
            break;
        }
    }
    context.dram[0x0050u] =
        static_cast<std::uint16_t>(context.dram[0x0050u] + 1u);
    g_dmbh_one_shot_completed.store(1u, std::memory_order_release);
    park_on_empty_cpu_mailbox(context);
}

void dmbh_three_round_ucode(galaxy::DspContext& context) {
    if (context.pc != 0u) {
        galaxy::dsp_hard_trap_at_current(
            context, "DMBH repeated-cycle test reached an invalid entry PC");
    }

    for (std::uint16_t round = 1u; round <= 3u; ++round) {
        context.last_retired_pc = 0x07B1u;
        galaxy::dsp_data_write(
            context,
            0xFFFCu,
            static_cast<std::uint16_t>(0x5100u + round));
        context.last_retired_pc = 0x07B2u;
        galaxy::dsp_data_write(
            context,
            0xFFFDu,
            static_cast<std::uint16_t>(0xA100u + round));
        context.pc = 0x07B3u;
        for (;;) {
            context.last_retired_pc = 0x07B3u;
            if ((galaxy::dsp_data_read(context, 0xFFFCu) & 0x8000u) ==
                0u) {
                break;
            }
        }
        context.dram[0x0051u] = round;
        g_dmbh_repeated_completed.store(round, std::memory_order_release);
    }
    park_on_empty_cpu_mailbox(context);
}

std::atomic<std::uint32_t> g_dmbh_consume_before_wait_stage{};

void dmbh_consume_before_wait_ucode(galaxy::DspContext& context) {
    if (context.pc != 0u) {
        galaxy::dsp_hard_trap_at_current(
            context, "DMBH consume-before-wait test reached an invalid entry PC");
    }

    context.last_retired_pc = 0x07B1u;
    galaxy::dsp_data_write(context, 0xFFFCu, 0x5234u);
    context.last_retired_pc = 0x07B2u;
    galaxy::dsp_data_write(context, 0xFFFDu, 0xB567u);
    g_dmbh_consume_before_wait_stage.store(1u, std::memory_order_release);
    while (g_dmbh_consume_before_wait_stage.load(std::memory_order_acquire) ==
           1u) {
        galaxy::dsp_throw_if_host_abort_requested(context);
        std::this_thread::yield();
    }

    context.pc = 0x07B3u;
    for (;;) {
        context.last_retired_pc = 0x07B3u;
        if ((galaxy::dsp_data_read(context, 0xFFFCu) & 0x8000u) == 0u) {
            break;
        }
    }
    context.dram[0x0052u] = 1u;
    g_dmbh_consume_before_wait_completed.store(
        1u, std::memory_order_release);
    park_on_empty_cpu_mailbox(context);
}

void dmbh_external_interrupt_ucode(galaxy::DspContext& context) {
    if (context.pc == 0x000Eu) {
        context.dram[0x0053u] =
            static_cast<std::uint16_t>(context.dram[0x0053u] + 1u);
        g_dmbh_external_interrupt_completed.store(
            1u, std::memory_order_release);
        context.halted = true;
        return;
    }
    if (context.pc != 0u) {
        galaxy::dsp_hard_trap_at_current(
            context, "DMBH external-interrupt test reached an invalid entry PC");
    }

    context.sr = static_cast<std::uint16_t>(
        context.sr | galaxy::kDspSrExtIntEnable);
    context.last_retired_pc = 0x07B1u;
    galaxy::dsp_data_write(context, 0xFFFCu, 0x5345u);
    context.last_retired_pc = 0x07B2u;
    galaxy::dsp_data_write(context, 0xFFFDu, 0xC678u);
    context.pc = 0x07B3u;
    for (;;) {
        context.last_retired_pc = 0x07B3u;
        if ((galaxy::dsp_data_read(context, 0xFFFCu) & 0x8000u) == 0u) {
            break;
        }
    }

    // Consuming the mail is the only ordinary way past the loop. Reaching this
    // line in the PIINT test would prove that the interrupt wake was lost.
    context.dram[0x0054u] = 0xBADu;
    context.halted = true;
}

std::atomic<std::uint32_t> g_diagnostic_snapshot_stage{};

void coherent_diagnostic_snapshot_ucode(galaxy::DspContext& context) {
    context.pc = 0x1111u;
    context.sr = 0x1112u;
    context.st[0] = 0x1113u;
    context.st[1] = 0x1114u;
    context.dram[0x0010u] = 0x1115u;
    context.dram[0x0280u] = 0x1116u;
    context.dram[0x0350u] = 0x1117u;
    context.dram[0x04FCu] = 0x1118u;
    galaxy::dsp_data_write(context, 0xFFFCu, 0xCAFEu);
    galaxy::dsp_data_write(context, 0xFFFDu, 0x1111u);
    g_diagnostic_snapshot_stage.store(1u, std::memory_order_release);
    g_diagnostic_snapshot_stage.notify_all();
    while (g_diagnostic_snapshot_stage.load(std::memory_order_acquire) < 2u) {
        galaxy::dsp_throw_if_host_abort_requested(context);
        std::this_thread::yield();
    }

    // Deliberately expose a long mixed live-context interval. CPU diagnostics
    // must continue to return the previously published all-0x111x sample.
    context.pc = 0x2221u;
    std::this_thread::yield();
    context.sr = 0x2222u;
    std::this_thread::yield();
    context.st[0] = 0x2223u;
    context.st[1] = 0x2224u;
    std::this_thread::yield();
    context.dram[0x0010u] = 0x2225u;
    context.dram[0x0280u] = 0x2226u;
    std::this_thread::yield();
    context.dram[0x0350u] = 0x2227u;
    context.dram[0x04FCu] = 0x2228u;
    g_diagnostic_snapshot_stage.store(3u, std::memory_order_release);
    g_diagnostic_snapshot_stage.notify_all();
    while (g_diagnostic_snapshot_stage.load(std::memory_order_acquire) < 4u) {
        galaxy::dsp_throw_if_host_abort_requested(context);
        std::this_thread::yield();
    }

    galaxy::dsp_data_write(context, 0xFFFBu, 1u);
    context.halted = true;
}

bool coherent_snapshot_tuple(
    const galaxy::DspDiagnosticSnapshot& snapshot,
    std::uint16_t prefix) {
    return snapshot.pc == static_cast<std::uint16_t>(prefix + 1u) &&
           snapshot.sr == static_cast<std::uint16_t>(prefix + 2u) &&
           snapshot.st[0] == static_cast<std::uint16_t>(prefix + 3u) &&
           snapshot.st[1] == static_cast<std::uint16_t>(prefix + 4u) &&
           snapshot_dram_equals(
               snapshot, 0x0010u, static_cast<std::uint16_t>(prefix + 5u)) &&
           snapshot_dram_equals(
               snapshot, 0x0280u, static_cast<std::uint16_t>(prefix + 6u)) &&
           snapshot_dram_equals(
               snapshot, 0x0350u, static_cast<std::uint16_t>(prefix + 7u)) &&
           snapshot_dram_equals(
               snapshot, 0x04FCu, static_cast<std::uint16_t>(prefix + 8u));
}

template <typename Predicate>
bool spin_until(Predicate pred, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    return true;
}

struct MramBoundaryProbe {
    std::array<
        std::uint8_t,
        galaxy::DspMramTransactionBoundary::kMaximumSpanBytes> mram{};
    std::atomic<std::uint32_t> wakes{};
    std::atomic<std::uint32_t> dirty_notifications{};
    std::atomic<bool> hold_wake{};
    std::atomic<bool> wake_entered{};
    std::atomic<bool> release_wake{};
    galaxy::timing::PendingEventLatch* event_latch{};
};

bool reject_mram_boundary_wake(void* /*user*/) noexcept {
    return false;
}

bool mram_boundary_wake(void* user) noexcept {
    auto& probe = *static_cast<MramBoundaryProbe*>(user);
    probe.wakes.fetch_add(1u, std::memory_order_release);
    probe.wake_entered.store(true, std::memory_order_release);
    while (probe.hold_wake.load(std::memory_order_acquire) &&
           !probe.release_wake.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    return probe.event_latch == nullptr ||
        probe.event_latch->publish(galaxy::timing::EventKind::Dsp);
}

bool mram_boundary_read(
    void* user,
    std::uint32_t address,
    std::uint8_t* destination,
    std::uint32_t size) noexcept {
    auto& probe = *static_cast<MramBoundaryProbe*>(user);
    if (destination == nullptr || address > probe.mram.size() ||
        size > probe.mram.size() - address) {
        return false;
    }
    std::memcpy(destination, probe.mram.data() + address, size);
    return true;
}

bool mram_boundary_write(
    void* user,
    std::uint32_t address,
    const std::uint8_t* source,
    std::uint32_t size) noexcept {
    auto& probe = *static_cast<MramBoundaryProbe*>(user);
    if (source == nullptr || address > probe.mram.size() ||
        size > probe.mram.size() - address) {
        return false;
    }
    std::memcpy(probe.mram.data() + address, source, size);
    probe.dirty_notifications.fetch_add(1u, std::memory_order_release);
    return true;
}

bool test_mram_transaction_boundary() {
    bool passed = true;
    MramBoundaryProbe probe{};
    for (std::size_t index = 0; index < probe.mram.size(); ++index) {
        probe.mram[index] = static_cast<std::uint8_t>(index ^ 0xA5u);
    }

    galaxy::DspMramTransactionBoundary boundary;
    passed &= expect(
        boundary.configure_wake(&mram_boundary_wake, &probe) &&
            boundary.reset(),
        "DSP MRAM boundary binds one CPU service thread and wake callback");

    std::array<std::uint8_t, 16> read_result{};
    read_result.fill(0xCCu);
    std::atomic<bool> read_succeeded{};
    std::thread read_worker([&] {
        read_succeeded.store(
            boundary.submit_read(
                7u,
                read_result.data(),
                static_cast<std::uint32_t>(read_result.size())),
            std::memory_order_release);
    });
    const bool read_published = spin_until(
        [&] { return boundary.pending(); }, std::chrono::seconds(1));
    const bool read_still_private =
        std::all_of(
            read_result.begin(),
            read_result.end(),
            [](std::uint8_t value) { return value == 0xCCu; });
    const auto read_service = boundary.service_one(
        &probe, &mram_boundary_read, &mram_boundary_write);
    read_worker.join();
    bool read_matches = true;
    for (std::size_t index = 0; index < read_result.size(); ++index) {
        read_matches = read_matches &&
            read_result[index] == probe.mram[7u + index];
    }
    passed &= expect(
        read_published && read_still_private &&
            read_service ==
                galaxy::DspMramTransactionBoundary::ServiceResult::Completed &&
            read_succeeded.load(std::memory_order_acquire) && read_matches,
        "DSP MRAM read stays invisible until one coherent CPU whole-span service");

    std::array<std::uint8_t, 16> write_source{};
    for (std::size_t index = 0; index < write_source.size(); ++index) {
        write_source[index] = static_cast<std::uint8_t>(0xF0u - index);
    }
    const auto before_write = probe.mram;
    std::atomic<bool> write_succeeded{};
    std::thread write_worker([&] {
        write_succeeded.store(
            boundary.submit_write(
                19u,
                write_source.data(),
                static_cast<std::uint32_t>(write_source.size())),
            std::memory_order_release);
    });
    const bool write_published = spin_until(
        [&] { return boundary.pending(); }, std::chrono::seconds(1));
    std::array<std::uint8_t, 1> rejected_destination{};
    const bool second_request_rejected = !boundary.submit_read(
        0u, rejected_destination.data(), 1u);
    const bool write_still_private = probe.mram == before_write;
    const auto write_service = boundary.service_one(
        &probe, &mram_boundary_read, &mram_boundary_write);
    write_worker.join();
    const bool write_matches = std::equal(
        write_source.begin(),
        write_source.end(),
        probe.mram.begin() + 19u);
    const auto coherent_stats = boundary.snapshot();
    passed &= expect(
        write_published && second_request_rejected && write_still_private &&
            write_service ==
                galaxy::DspMramTransactionBoundary::ServiceResult::Completed &&
            write_succeeded.load(std::memory_order_acquire) && write_matches &&
            probe.dirty_notifications.load(std::memory_order_acquire) == 1u &&
            coherent_stats.maximum_in_flight == 1u &&
            coherent_stats.rejected_requests == 1u &&
            coherent_stats.completed_services == 2u,
        "DSP MRAM write commits once, dirties once, and has no hidden request queue");

    MramBoundaryProbe race_probe{};
    race_probe.mram[3] = 0x6Du;
    race_probe.hold_wake.store(true, std::memory_order_release);
    galaxy::DspMramTransactionBoundary race_boundary;
    passed &= expect(
        race_boundary.configure_wake(&mram_boundary_wake, &race_probe) &&
            race_boundary.reset(),
        "DSP MRAM wake-race boundary configured");
    std::uint8_t race_result = 0u;
    std::atomic<bool> race_succeeded{};
    std::thread race_worker([&] {
        race_succeeded.store(
            race_boundary.submit_read(3u, &race_result, 1u),
            std::memory_order_release);
    });
    const bool wake_window_reached = spin_until(
        [&] {
            return race_probe.wake_entered.load(std::memory_order_acquire);
        },
        std::chrono::seconds(1));
    const auto race_service = race_boundary.service_one(
        &race_probe, &mram_boundary_read, &mram_boundary_write);
    const bool output_private_before_wake_return = race_result == 0u;
    race_probe.release_wake.store(true, std::memory_order_release);
    race_worker.join();
    passed &= expect(
        wake_window_reached && output_private_before_wake_return &&
            race_service ==
                galaxy::DspMramTransactionBoundary::ServiceResult::Completed &&
            race_succeeded.load(std::memory_order_acquire) &&
            race_result == 0x6Du,
        "DSP MRAM acknowledgement cannot be lost between request wake and worker wait");

    MramBoundaryProbe stale_event_probe{};
    galaxy::timing::PendingEventLatch stale_event_latch{};
    stale_event_probe.event_latch = &stale_event_latch;
    stale_event_probe.mram[0] = 0x31u;
    stale_event_probe.mram[1] = 0x72u;
    galaxy::DspMramTransactionBoundary stale_event_boundary;
    passed &= expect(
        stale_event_boundary.configure_wake(
            &mram_boundary_wake, &stale_event_probe) &&
            stale_event_boundary.reset(),
        "DSP MRAM stale-event boundary configured");
    std::uint8_t stale_first = 0u;
    std::atomic<bool> stale_first_succeeded{};
    std::thread stale_first_worker([&] {
        stale_first_succeeded.store(
            stale_event_boundary.submit_read(0u, &stale_first, 1u),
            std::memory_order_release);
    });
    const bool stale_first_published = spin_until(
        [&] {
            return stale_event_latch.pending(
                       galaxy::timing::EventKind::Dsp) == 1u &&
                stale_event_boundary.pending();
        },
        std::chrono::seconds(1));
    // This is the direct mailbox-loop path: it services the transaction but
    // intentionally does not consume the safepoint event publication.
    const auto stale_first_service = stale_event_boundary.service_one(
        &stale_event_probe, &mram_boundary_read, &mram_boundary_write);
    stale_first_worker.join();
    const bool first_wake_remained =
        stale_event_latch.pending(galaxy::timing::EventKind::Dsp) == 1u;

    std::uint8_t stale_second = 0u;
    std::atomic<bool> stale_second_succeeded{};
    std::thread stale_second_worker([&] {
        stale_second_succeeded.store(
            stale_event_boundary.submit_read(1u, &stale_second, 1u),
            std::memory_order_release);
    });
    const bool stale_second_published = spin_until(
        [&] {
            return stale_event_latch.pending(
                       galaxy::timing::EventKind::Dsp) == 2u &&
                stale_event_boundary.pending();
        },
        std::chrono::seconds(1));
    const std::uint64_t consumed_publications =
        stale_event_latch.take_all(galaxy::timing::EventKind::Dsp);
    const auto stale_second_service = stale_event_boundary.service_one(
        &stale_event_probe, &mram_boundary_read, &mram_boundary_write);
    stale_second_worker.join();
    passed &= expect(
        stale_first_published && first_wake_remained &&
            stale_second_published && consumed_publications == 2u &&
            stale_event_latch.pending(
                galaxy::timing::EventKind::Dsp) == 0u &&
            stale_first_service ==
                galaxy::DspMramTransactionBoundary::ServiceResult::Completed &&
            stale_second_service ==
                galaxy::DspMramTransactionBoundary::ServiceResult::Completed &&
            stale_first_succeeded.load(std::memory_order_acquire) &&
            stale_second_succeeded.load(std::memory_order_acquire) &&
            stale_first == 0x31u && stale_second == 0x72u,
        "DSP EventKind publication counts preserve a second request behind a stale direct-service wake");

    MramBoundaryProbe timeout_probe{};
    galaxy::DspMramTransactionBoundary timeout_boundary{
        std::chrono::milliseconds(15)};
    passed &= expect(
        timeout_boundary.configure_wake(
            &mram_boundary_wake, &timeout_probe) &&
            timeout_boundary.reset(std::chrono::milliseconds(15)),
        "DSP MRAM timeout boundary configured");
    std::uint8_t timeout_result = 0u;
    const bool timeout_succeeded =
        timeout_boundary.submit_read(0u, &timeout_result, 1u);
    const auto timeout_stats = timeout_boundary.snapshot();
    passed &= expect(
        !timeout_succeeded && timeout_stats.timeout_failures == 1u &&
            timeout_stats.failed_services == 1u && timeout_stats.failed &&
            timeout_stats.in_flight == 0u,
        "DSP MRAM request times out boundedly without an unserviced hidden request");

    MramBoundaryProbe cancel_probe{};
    galaxy::DspMramTransactionBoundary cancel_boundary;
    passed &= expect(
        cancel_boundary.configure_wake(&mram_boundary_wake, &cancel_probe) &&
            cancel_boundary.reset(),
        "DSP MRAM cancellation boundary configured");
    std::uint8_t cancel_result = 0u;
    std::atomic<bool> cancel_succeeded{true};
    std::thread cancel_worker([&] {
        cancel_succeeded.store(
            cancel_boundary.submit_read(0u, &cancel_result, 1u),
            std::memory_order_release);
    });
    const bool cancel_published = spin_until(
        [&] { return cancel_boundary.pending(); }, std::chrono::seconds(1));
    cancel_boundary.shutdown();
    cancel_worker.join();
    const auto cancel_stats = cancel_boundary.snapshot();
    passed &= expect(
        cancel_published &&
            !cancel_succeeded.load(std::memory_order_acquire) &&
            cancel_stats.shutdown_cancellations == 1u &&
            cancel_stats.shutdown && cancel_stats.in_flight == 0u,
        "DSP MRAM shutdown cancels and wakes one blocked worker request");

    MramBoundaryProbe completed_cancel_probe{};
    completed_cancel_probe.hold_wake.store(true, std::memory_order_release);
    galaxy::DspMramTransactionBoundary completed_cancel_boundary;
    passed &= expect(
        completed_cancel_boundary.configure_wake(
            &mram_boundary_wake, &completed_cancel_probe) &&
            completed_cancel_boundary.reset(),
        "DSP MRAM completed-request cancellation boundary configured");
    std::uint8_t completed_cancel_result = 0u;
    std::atomic<bool> completed_cancel_succeeded{true};
    std::thread completed_cancel_worker([&] {
        completed_cancel_succeeded.store(
            completed_cancel_boundary.submit_read(
                0u, &completed_cancel_result, 1u),
            std::memory_order_release);
    });
    const bool completed_cancel_wake = spin_until(
        [&] {
            return completed_cancel_probe.wake_entered.load(
                std::memory_order_acquire);
        },
        std::chrono::seconds(1));
    const auto completed_cancel_service =
        completed_cancel_boundary.service_one(
            &completed_cancel_probe,
            &mram_boundary_read,
            &mram_boundary_write);
    completed_cancel_boundary.shutdown();
    completed_cancel_probe.release_wake.store(
        true, std::memory_order_release);
    completed_cancel_worker.join();
    const auto completed_cancel_stats =
        completed_cancel_boundary.snapshot();
    passed &= expect(
        completed_cancel_wake &&
            completed_cancel_service ==
                galaxy::DspMramTransactionBoundary::ServiceResult::Completed &&
            !completed_cancel_succeeded.load(std::memory_order_acquire) &&
            completed_cancel_stats.shutdown_cancellations == 1u &&
            completed_cancel_stats.shutdown &&
            completed_cancel_stats.in_flight == 0u,
        "DSP MRAM shutdown cancels a serviced acknowledgement before the submitter relocks");

    MramBoundaryProbe wake_failure_probe{};
    galaxy::DspMramTransactionBoundary wake_failure_boundary;
    passed &= expect(
        wake_failure_boundary.configure_wake(
            &reject_mram_boundary_wake, &wake_failure_probe) &&
            wake_failure_boundary.reset(),
        "DSP MRAM wake-failure boundary configured");
    std::uint8_t wake_failure_result = 0u;
    const bool wake_failure_succeeded =
        wake_failure_boundary.submit_read(
            0u, &wake_failure_result, 1u);
    const auto wake_failure_stats = wake_failure_boundary.snapshot();
    passed &= expect(
        !wake_failure_succeeded &&
            wake_failure_stats.wake_failures == 1u &&
            wake_failure_stats.wake_publications == 0u &&
            wake_failure_stats.failed_services == 1u &&
            wake_failure_stats.failed &&
            wake_failure_stats.in_flight == 0u,
        "DSP MRAM safepoint wake publication failure is terminal, never silently polled");

    MramBoundaryProbe wrong_thread_probe{};
    galaxy::DspMramTransactionBoundary wrong_thread_boundary;
    passed &= expect(
        wrong_thread_boundary.configure_wake(
            &mram_boundary_wake, &wrong_thread_probe) &&
            wrong_thread_boundary.reset(),
        "DSP MRAM wrong-thread boundary configured");
    std::uint8_t wrong_thread_result = 0u;
    std::atomic<bool> wrong_thread_submit_succeeded{true};
    std::thread wrong_thread_submitter([&] {
        wrong_thread_submit_succeeded.store(
            wrong_thread_boundary.submit_read(
                0u, &wrong_thread_result, 1u),
            std::memory_order_release);
    });
    const bool wrong_thread_published = spin_until(
        [&] { return wrong_thread_boundary.pending(); },
        std::chrono::seconds(1));
    std::atomic<galaxy::DspMramTransactionBoundary::ServiceResult>
        wrong_thread_service{
            galaxy::DspMramTransactionBoundary::ServiceResult::None};
    std::thread wrong_service_thread([&] {
        wrong_thread_service.store(
            wrong_thread_boundary.service_one(
                &wrong_thread_probe,
                &mram_boundary_read,
                &mram_boundary_write),
            std::memory_order_release);
    });
    wrong_service_thread.join();
    wrong_thread_submitter.join();
    passed &= expect(
        wrong_thread_published &&
            wrong_thread_service.load(std::memory_order_acquire) ==
                galaxy::DspMramTransactionBoundary::ServiceResult::WrongThread &&
            !wrong_thread_submit_succeeded.load(std::memory_order_acquire) &&
            wrong_thread_boundary.snapshot().failed_services == 1u,
        "DSP MRAM service hard-rejects any thread other than the bound CPU owner");

    MramBoundaryProbe maximum_probe{};
    galaxy::DspMramTransactionBoundary maximum_boundary;
    passed &= expect(
        maximum_boundary.configure_wake(
            &mram_boundary_wake, &maximum_probe) &&
            maximum_boundary.reset(),
        "DSP MRAM maximum-span boundary configured");
    std::array<
        std::uint8_t,
        galaxy::DspMramTransactionBoundary::kMaximumSpanBytes>
        maximum_source{};
    for (std::size_t index = 0; index < maximum_source.size(); ++index) {
        maximum_source[index] =
            static_cast<std::uint8_t>((index * 131u) ^ 0x5Au);
    }
    std::atomic<bool> maximum_succeeded{};
    std::thread maximum_worker([&] {
        maximum_succeeded.store(
            maximum_boundary.submit_write(
                0u,
                maximum_source.data(),
                static_cast<std::uint32_t>(maximum_source.size())),
            std::memory_order_release);
    });
    const bool maximum_published = spin_until(
        [&] { return maximum_boundary.pending(); },
        std::chrono::seconds(1));
    const auto maximum_service_start = std::chrono::steady_clock::now();
    const auto maximum_service = maximum_boundary.service_one(
        &maximum_probe, &mram_boundary_read, &mram_boundary_write);
    const auto maximum_service_duration =
        std::chrono::steady_clock::now() - maximum_service_start;
    maximum_worker.join();
    passed &= expect(
        maximum_published &&
            maximum_service ==
                galaxy::DspMramTransactionBoundary::ServiceResult::Completed &&
            maximum_succeeded.load(std::memory_order_acquire) &&
            maximum_probe.mram == maximum_source &&
            maximum_probe.dirty_notifications.load(
                std::memory_order_acquire) == 1u &&
            maximum_service_duration < std::chrono::seconds(1),
        "DSP MRAM fixed CPU service completes the hardware-maximum 16 KiB span atomically");

    MramBoundaryProbe invalid_probe{};
    galaxy::DspMramTransactionBoundary invalid_boundary;
    passed &= expect(
        invalid_boundary.configure_wake(
            &mram_boundary_wake, &invalid_probe) &&
            invalid_boundary.reset(),
        "DSP MRAM invalid-span boundary configured");
    std::uint8_t byte = 0u;
    const bool invalid_rejected =
        !invalid_boundary.submit_read(0u, &byte, 0u) &&
        !invalid_boundary.submit_read(
            0u,
            &byte,
            galaxy::DspMramTransactionBoundary::kMaximumSpanBytes + 1u) &&
        !invalid_boundary.submit_read(0xFFFFFFFFu, &byte, 2u) &&
        !invalid_boundary.submit_write(0u, nullptr, 1u);
    std::array<std::uint8_t, 0x4001u> oversize{};
    galaxy::DspContext oversize_context{};
    oversize_context.pc = 0x0777u;
    bool oversize_hard_trapped = false;
    try {
        galaxy::dsp_external_read_span(
            oversize_context, 0u, std::span<std::uint8_t>{oversize});
    } catch (const galaxy::DspHardTrap& trap) {
        oversize_hard_trapped = trap.pc == 0x0777u;
    }
    const auto invalid_stats = invalid_boundary.snapshot();
    passed &= expect(
        invalid_rejected && invalid_stats.rejected_requests == 4u &&
            invalid_stats.wake_publications == 0u && oversize_hard_trapped,
        "DSP MRAM invalid, wrapping, and oversize requests hard-reject before publication");

    return passed;
}

bool test_frame_pe_slice_services_pending_mram_before_ai() {
    using galaxy::DspMramTransactionBoundary;
    using galaxy::timing::EventKind;

    MramBoundaryProbe probe{};
    galaxy::timing::PendingEventLatch event_latch{};
    probe.event_latch = &event_latch;
    DspMramTransactionBoundary boundary;
    const bool configured =
        boundary.configure_wake(&mram_boundary_wake, &probe) &&
        boundary.reset();

    constexpr std::uint32_t kWriteAddress = 37u;
    const std::array<std::uint8_t, 8> write_source{
        0x10u, 0x32u, 0x54u, 0x76u, 0x98u, 0xBAu, 0xDCu, 0xFEu};
    std::atomic<bool> submit_succeeded{};
    std::thread worker;
    if (configured) {
        worker = std::thread([&] {
            submit_succeeded.store(
                boundary.submit_write(
                    kWriteAddress,
                    write_source.data(),
                    static_cast<std::uint32_t>(write_source.size())),
                std::memory_order_release);
        });
    }

    const bool request_published = configured && spin_until(
        [&] {
            return boundary.pending() &&
                event_latch.pending(EventKind::Dsp) == 1u;
        },
        std::chrono::seconds(1));

    std::uint64_t consumed_wakes = 0u;
    auto service_result = DspMramTransactionBoundary::ServiceResult::None;
    std::uint32_t sequence = 0u;
    std::uint32_t dsp_sequence = 0u;
    std::uint32_t first_ai_sequence = 0u;
    std::uint32_t audio_failure_checks = 0u;
    std::uint32_t ai_service_calls = 0u;
    std::uint32_t input_service_calls = 0u;
    std::uint32_t guest_interrupt_entries = 0u;
    bool write_visible_before_ai = false;

    if (request_published) {
        galaxy::gx::service_frame_pe_realtime_device_slice(
            [&] {
                dsp_sequence = ++sequence;
                consumed_wakes += event_latch.take_all(EventKind::Dsp);
                service_result = boundary.service_one(
                    &probe,
                    &mram_boundary_read,
                    &mram_boundary_write);
            },
            [&] { ++audio_failure_checks; },
            [&](const void* guest_context) {
                if (ai_service_calls == 0u) {
                    first_ai_sequence = ++sequence;
                    write_visible_before_ai =
                        service_result ==
                            DspMramTransactionBoundary::ServiceResult::Completed &&
                        std::equal(
                            write_source.begin(),
                            write_source.end(),
                            probe.mram.begin() + kWriteAddress);
                }
                ++ai_service_calls;
                if (guest_context != nullptr) {
                    ++guest_interrupt_entries;
                }
            },
            [&] { ++input_service_calls; },
            [] { return false; });
    } else {
        boundary.shutdown();
    }

    if (worker.joinable()) {
        worker.join();
    }
    const auto stats = boundary.snapshot();

    return expect(
               configured && request_published,
               "PE service regression publishes one pending native DSP MRAM wake") &&
        expect(
            service_result ==
                    DspMramTransactionBoundary::ServiceResult::Completed &&
                submit_succeeded.load(std::memory_order_acquire) &&
                write_visible_before_ai && dsp_sequence == 1u &&
                first_ai_sequence == 2u,
            "one PE service slice commits the pending DSP span before AI") &&
        expect(
            probe.wakes.load(std::memory_order_acquire) == 1u &&
                stats.wake_publications == 1u && consumed_wakes == 1u &&
                event_latch.pending(EventKind::Dsp) == 0u &&
                stats.service_calls == 1u && stats.completed_services == 1u &&
                stats.in_flight == 0u && !stats.failed,
            "PE service slice conserves the DSP wake and transaction exactly once") &&
        expect(
            ai_service_calls == 1u && input_service_calls == 1u &&
                audio_failure_checks == 2u && guest_interrupt_entries == 0u,
            "PE service slice advances realtime devices without a guest interrupt handler");
}

struct AramMailProbe {
    galaxy::DspAramMirrorBoundary* boundary{};
    std::uint64_t expected_generation{};
    std::uint32_t word_address{};
    std::uint16_t expected_word{};
    std::uint32_t calls{};
    bool observed_after_apply{};
    bool accept{true};
};

bool consume_aram_mail(void* user, std::uint64_t generation) noexcept {
    auto& probe = *static_cast<AramMailProbe*>(user);
    std::uint16_t value = 0u;
    const bool read = probe.boundary != nullptr &&
        probe.boundary->worker_read_u16_be(probe.word_address, &value);
    probe.observed_after_apply =
        read && generation == probe.expected_generation &&
        value == probe.expected_word;
    ++probe.calls;
    return probe.accept && probe.observed_after_apply;
}

struct BlockingAramMailProbe {
    galaxy::DspAramMirrorBoundary* boundary{};
    std::uint64_t expected_generation{};
    std::atomic<bool> entered{};
    std::atomic<bool> release{};
    bool observed_after_apply{};
};

bool consume_blocking_aram_mail(
    void* user, std::uint64_t generation) noexcept {
    auto& probe = *static_cast<BlockingAramMailProbe*>(user);
    std::uint16_t value = 0u;
    probe.observed_after_apply =
        probe.boundary != nullptr &&
        probe.boundary->worker_read_u16_be(127u, &value) &&
        value == 0x1234u && generation == probe.expected_generation;
    probe.entered.store(true, std::memory_order_release);
    probe.entered.notify_all();
    while (!probe.release.load(std::memory_order_acquire)) {
        probe.release.wait(false, std::memory_order_acquire);
    }
    return probe.observed_after_apply;
}

struct AramOutboundProbe {
    std::uint64_t expected_generation{};
    std::uint32_t calls{};
    bool valid{true};
    bool reject{};
};

bool commit_aram_span(
    void* user,
    std::uint64_t generation,
    std::uint32_t address,
    const std::uint8_t* source,
    std::uint32_t size) noexcept {
    auto& probe = *static_cast<AramOutboundProbe*>(user);
    bool valid = generation == probe.expected_generation && source != nullptr;
    if (probe.calls == 0u) {
        valid = valid && address == 127u && size == 3u &&
            source[0] == 0xABu && source[1] == 0xCDu &&
            source[2] == 0xEFu;
    } else if (probe.calls == 1u) {
        valid = valid && address == 132u && size == 1u &&
            source[0] == 0x77u;
    } else {
        valid = false;
    }
    probe.valid = probe.valid && valid;
    ++probe.calls;
    return !probe.reject && valid;
}

struct RejectAramSpanProbe {
    std::uint32_t calls{};
};

struct SparseAramSpanProbe {
    std::uint32_t calls{};
    bool valid{true};
};

bool commit_sparse_aram_span(
    void* user,
    std::uint64_t generation,
    std::uint32_t address,
    const std::uint8_t* source,
    std::uint32_t size) noexcept {
    auto& probe = *static_cast<SparseAramSpanProbe*>(user);
    const bool valid = generation == 2u && probe.calls == 0u &&
        address == 128u && source != nullptr && size == 1u &&
        source[0] == 0x44u;
    probe.valid = probe.valid && valid;
    ++probe.calls;
    return valid;
}

bool reject_aram_span(
    void* user,
    std::uint64_t,
    std::uint32_t,
    const std::uint8_t*,
    std::uint32_t) noexcept {
    auto& probe = *static_cast<RejectAramSpanProbe*>(user);
    ++probe.calls;
    return false;
}

struct IfxOrderingProbe {
    std::uint32_t next_event{};
    std::uint32_t consume_event{};
    std::uint32_t dmbl_pre_event{};
    std::uint32_t dirq_pre_event{};
    std::uint32_t interrupt_event{};
    std::uint32_t word_reads{};
    std::uint32_t word_writes{};
    std::uint16_t stored_word{0xCAFEu};
    bool reject_publication{};
};

bool consume_ifx_mail(
    void* user,
    std::uint64_t generation,
    std::uint32_t* mailbox,
    std::uint16_t* value,
    bool* consumed_mail) {
    auto& probe = *static_cast<IfxOrderingProbe*>(user);
    probe.consume_event = ++probe.next_event;
    if (generation != 7u || mailbox == nullptr || value == nullptr ||
        consumed_mail == nullptr) {
        return false;
    }
    *value = galaxy::dsp_mailbox_read_low(*mailbox, consumed_mail);
    return *consumed_mail;
}

bool before_ifx_dmbl(void* user) {
    auto& probe = *static_cast<IfxOrderingProbe*>(user);
    probe.dmbl_pre_event = ++probe.next_event;
    return !probe.reject_publication;
}

bool before_ifx_dirq(void* user) {
    auto& probe = *static_cast<IfxOrderingProbe*>(user);
    probe.dirq_pre_event = ++probe.next_event;
    return !probe.reject_publication;
}

void record_ifx_interrupt(void* user) {
    auto& probe = *static_cast<IfxOrderingProbe*>(user);
    probe.interrupt_event = ++probe.next_event;
}

bool read_ifx_aram_word(
    void* user,
    std::uint32_t address,
    std::uint16_t* value) {
    auto& probe = *static_cast<IfxOrderingProbe*>(user);
    if (address != 6u || value == nullptr) {
        return false;
    }
    ++probe.word_reads;
    *value = probe.stored_word;
    return true;
}

bool write_ifx_aram_word(
    void* user,
    std::uint32_t address,
    std::uint16_t value) {
    auto& probe = *static_cast<IfxOrderingProbe*>(user);
    if (address != 6u) {
        return false;
    }
    ++probe.word_writes;
    probe.stored_word = value;
    return true;
}

bool validate_ifx_aram_word(
    void*, std::uint32_t address, std::uint32_t size) {
    return address == 6u && size == 2u;
}

void ignore_ifx_accelerator_exception(
    void*, galaxy::DspAcceleratorException) {}

bool test_ifx_ordering_hooks() {
    IfxOrderingProbe probe{};
    galaxy::DspContext context{};
    context.pc = 0x0770u;
    context.hardware_user = &probe;
    context.hardware.consume_cpu_mailbox_low = &consume_ifx_mail;
    context.hardware.before_dsp_mailbox_low = &before_ifx_dmbl;
    context.hardware.before_dsp_interrupt = &before_ifx_dirq;
    context.hardware.request_interrupt = &record_ifx_interrupt;
    context.hardware.validate_aram_span = &validate_ifx_aram_word;
    context.hardware.aram_read_u16 = &read_ifx_aram_word;
    context.hardware.aram_write_u16 = &write_ifx_aram_word;
    context.hardware.accelerator_exception =
        &ignore_ifx_accelerator_exception;
    std::atomic<std::uint64_t> generation{7u};
    context.host_cpu_mail_generation = &generation;

    galaxy::dsp_mailbox_write_high(
        galaxy::dsp_cpu_mailbox(context), 0x1122u);
    (void)galaxy::dsp_mailbox_write_low(
        galaxy::dsp_cpu_mailbox(context), 0x3344u);
    const std::uint16_t consumed =
        galaxy::dsp_ifx_read(context, galaxy::kDspIfxCmbl);

    galaxy::dsp_ifx_write(context, galaxy::kDspIfxDmbh, 0x5566u);
    galaxy::dsp_ifx_write(context, galaxy::kDspIfxDmbl, 0x7788u);
    const bool dmbl_after_hook = probe.dmbl_pre_event == 2u &&
        galaxy::dsp_mailbox_raw(galaxy::dsp_dsp_mailbox(context)) ==
            (0x55667788u | galaxy::kDspMailboxBusy);
    galaxy::dsp_ifx_write(context, galaxy::kDspIfxDirq, 1u);
    const bool dirq_after_hook = probe.dirq_pre_event == 3u &&
        probe.interrupt_event == 4u;

    galaxy::dsp_accelerator_set_current_address(context, 3u);
    context.ifx[galaxy::kDspIfxFormat] = 2u;
    const std::uint16_t word = galaxy::dsp_accelerator_read_raw(context);
    galaxy::dsp_accelerator_set_current_address(context, 0x80000003u);
    galaxy::dsp_accelerator_write_raw(context, 0xBEEFu);

    probe.reject_publication = true;
    galaxy::dsp_mailbox_write_high(
        galaxy::dsp_dsp_mailbox(context), 0x2468u);
    const std::uint32_t mailbox_before_reject =
        galaxy::dsp_mailbox_raw(galaxy::dsp_dsp_mailbox(context));
    bool dmbl_rejected = false;
    try {
        galaxy::dsp_ifx_write(context, galaxy::kDspIfxDmbl, 0x1357u);
    } catch (const galaxy::DspHardTrap&) {
        dmbl_rejected = true;
    }
    const std::uint32_t interrupts_before_reject = probe.interrupt_event;
    bool dirq_rejected = false;
    try {
        galaxy::dsp_ifx_write(context, galaxy::kDspIfxDirq, 1u);
    } catch (const galaxy::DspHardTrap&) {
        dirq_rejected = true;
    }

    return expect(
        consumed == 0x3344u && probe.consume_event == 1u &&
            dmbl_after_hook && dirq_after_hook && word == 0xCAFEu &&
            probe.word_reads == 1u && probe.word_writes == 1u &&
            probe.stored_word == 0xBEEFu && dmbl_rejected &&
            galaxy::dsp_mailbox_raw(galaxy::dsp_dsp_mailbox(context)) ==
                mailbox_before_reject && dirq_rejected &&
            probe.interrupt_event == interrupts_before_reject,
        "DSP IFX hooks prove exact CMBL consume, pre-DMBL/pre-DIRQ ordering, rejection invisibility, and atomic ARAM word routing");
}

struct HaltAckProbe {
    std::atomic<bool> entered{};
    std::atomic<bool> release{};
};

bool wait_for_halt_ack(void* user) {
    auto& probe = *static_cast<HaltAckProbe*>(user);
    probe.entered.store(true, std::memory_order_release);
    while (!probe.release.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    return true;
}

void clean_halt_entry(galaxy::DspContext& context) {
    context.pc = 0x0444u;
    context.halted = true;
}

bool test_clean_halt_ack_ordering() {
    HaltAckProbe probe{};
    galaxy::DspHardwareServices services{};
    services.before_clean_halt_return = &wait_for_halt_ack;
    auto dsp_storage =
        std::make_unique<galaxy::NativeDspCoprocessor>();
    auto& dsp = *dsp_storage;
    dsp.install_host_services(&probe, services);
    galaxy::NativeDspWorker worker(
        dsp, &clean_halt_entry, /*free_running=*/true);
    worker.start();
    const bool callback_entered = spin_until(
        [&] { return probe.entered.load(std::memory_order_acquire); },
        std::chrono::seconds(1));
    const bool invisible_before_ack =
        !worker.is_halted() && worker.completed_runs() == 0u;
    probe.release.store(true, std::memory_order_release);
    const bool completed = worker.wait_for_completed_runs(
        1u, std::chrono::seconds(1));
    const bool passed = expect(
        callback_entered && invisible_before_ack && completed &&
            worker.is_halted(),
        "native DSP clean HALT is not scheduler-visible until the pre-return host ACK completes");
    worker.stop();
    return passed;
}

bool test_diagnostic_snapshot_coherence() {
    g_diagnostic_snapshot_stage.store(0u, std::memory_order_release);
    Probe diagnostic_probe{};
    galaxy::NativeDspCoprocessor diagnostic_dsp;
    diagnostic_dsp.set_diagnostic_capture_enabled(true);
    diagnostic_dsp.set_host_callbacks(
        &diagnostic_probe,
        record_interrupt,
        record_accelerator_exception);
    galaxy::NativeDspWorker diagnostic_worker(
        diagnostic_dsp,
        coherent_diagnostic_snapshot_ucode,
        /*free_running=*/true);
    diagnostic_worker.start();

    const bool dmbl_stage_reached = spin_until(
        [] {
            return g_diagnostic_snapshot_stage.load(
                       std::memory_order_acquire) >= 1u;
        },
        std::chrono::seconds(2));
    const auto dmbl_snapshot = diagnostic_dsp.latest_diagnostic_snapshot();
    const bool dmbl_snapshot_exact =
        dmbl_snapshot.has_value() &&
        dmbl_snapshot->boundary ==
            galaxy::DspDiagnosticBoundary::DspMailboxLow &&
        dmbl_snapshot->publication_generation == 1u &&
        dmbl_snapshot->dsp_mail_generation == 1u &&
        dmbl_snapshot->dirq_generation == 0u &&
        dmbl_snapshot->return_generation == 0u &&
        dmbl_snapshot->dsp_mailbox ==
            (0xCAFE1111u | galaxy::kDspMailboxBusy) &&
        coherent_snapshot_tuple(*dmbl_snapshot, 0x1110u);

    g_diagnostic_snapshot_stage.store(2u, std::memory_order_release);
    g_diagnostic_snapshot_stage.notify_all();
    bool mixed_window_coherent = true;
    std::uint64_t mixed_window_reads = 0u;
    const bool mixed_stage_reached = spin_until(
        [&] {
            const auto snapshot =
                diagnostic_dsp.latest_diagnostic_snapshot();
            if (snapshot.has_value()) {
                ++mixed_window_reads;
                mixed_window_coherent =
                    mixed_window_coherent &&
                    snapshot->publication_generation == 1u &&
                    snapshot->boundary ==
                        galaxy::DspDiagnosticBoundary::DspMailboxLow &&
                    coherent_snapshot_tuple(*snapshot, 0x1110u);
            }
            return g_diagnostic_snapshot_stage.load(
                       std::memory_order_acquire) >= 3u;
        },
        std::chrono::seconds(2));
    const auto mixed_snapshot = diagnostic_dsp.latest_diagnostic_snapshot();
    mixed_window_coherent =
        mixed_window_coherent && mixed_snapshot.has_value() &&
        mixed_snapshot->publication_generation == 1u &&
        coherent_snapshot_tuple(*mixed_snapshot, 0x1110u);

    g_diagnostic_snapshot_stage.store(4u, std::memory_order_release);
    g_diagnostic_snapshot_stage.notify_all();
    const bool diagnostic_run_completed =
        diagnostic_worker.wait_for_completed_runs(
            1u, std::chrono::seconds(2));
    const auto return_snapshot = diagnostic_dsp.latest_diagnostic_snapshot();
    const auto halted_snapshot = diagnostic_worker.halted_snapshot();
    const bool return_snapshot_exact =
        return_snapshot.has_value() &&
        return_snapshot->boundary ==
            galaxy::DspDiagnosticBoundary::HaltReturn &&
        return_snapshot->publication_generation == 3u &&
        return_snapshot->dsp_mail_generation == 1u &&
        return_snapshot->dirq_generation == 1u &&
        return_snapshot->return_generation == 1u && return_snapshot->halted &&
        coherent_snapshot_tuple(*return_snapshot, 0x2220u);
    const bool quiescent_snapshot_exact =
        halted_snapshot_at(halted_snapshot, 0x2221u, 1u) &&
        coherent_snapshot_tuple(*halted_snapshot, 0x2220u);
    const bool passed = expect(
        dmbl_stage_reached && dmbl_snapshot_exact && mixed_stage_reached &&
            mixed_window_coherent && mixed_window_reads != 0u &&
            diagnostic_run_completed && diagnostic_worker.is_halted() &&
            return_snapshot_exact && quiescent_snapshot_exact &&
            diagnostic_probe.interrupt_count == 1u,
        "native DSP diagnostics publish bounded coherent DMBL/DIRQ/HALT snapshots without exposing live context");
    diagnostic_worker.stop();
    g_diagnostic_snapshot_stage.store(0u, std::memory_order_release);
    return passed;
}

bool test_aram_mirror_boundary() {
    using Boundary = galaxy::DspAramMirrorBoundary;
    static_assert(!Boundary::kWorkerHotPathUsesBoundaryMutex);

    bool passed = true;
    auto seed = std::make_unique<std::uint8_t[]>(Boundary::kMirrorSizeBytes);
    seed[127] = 0x12u;
    seed[128] = 0x34u;
    seed[Boundary::kMirrorSizeBytes - 2u] = 0x9Au;
    seed[Boundary::kMirrorSizeBytes - 1u] = 0xBCu;

    {
        auto boundary = std::make_unique<Boundary>();
        passed &= expect(
            boundary->reset() &&
                boundary->cpu_stage_seed(
                    1u,
                    std::span<const std::uint8_t>{
                        seed.get(), Boundary::kMirrorSizeBytes}),
            "DSP ARAM mirror accepts one full generation-bound seed");

        AramMailProbe seed_mail{
            boundary.get(), 1u, 127u, 0x1234u};
        std::atomic<bool> seed_worker_ok{};
        std::thread seed_worker([&] {
            const bool bound = boundary->worker_bind();
            const bool consumed = bound &&
                boundary->worker_apply_before_mail_consume(
                    1u, &consume_aram_mail, &seed_mail);
            std::uint16_t tail = 0u;
            const bool tail_read = consumed && boundary->worker_read_u16_be(
                Boundary::kMirrorSizeBytes - 2u, &tail) &&
                tail == 0x9ABCu;
            const bool detached = boundary->worker_detach();
            seed_worker_ok.store(
                bound && consumed && tail_read && detached,
                std::memory_order_release);
        });
        seed_worker.join();
        passed &= expect(
            seed_worker_ok.load(std::memory_order_acquire) &&
                seed_mail.calls == 1u && seed_mail.observed_after_apply,
            "DSP ARAM seed is wholly installed before the matching real mail consume callback");

        std::array<std::uint8_t, Boundary::kDirtyPageBytes> page_zero{};
        std::array<std::uint8_t, Boundary::kDirtyPageBytes> page_one{};
        page_zero.fill(0x41u);
        page_one.fill(0x82u);
        page_zero[Boundary::kDirtyPageBytes - 1u] = 0x56u;
        page_one[0] = 0x78u;
        std::array<Boundary::DirtyPageUpdate, 2> page_updates{};
        page_updates[0] = Boundary::DirtyPageUpdate{
            0u, std::span<const std::uint8_t>{page_zero}};
        page_updates[1] = Boundary::DirtyPageUpdate{
            1u, std::span<const std::uint8_t>{page_one}};
        passed &= expect(
            boundary->cpu_stage_dirty_pages(2u, page_updates),
            "DSP ARAM mirror stages sorted dirty pages as one mail generation");

        AramMailProbe dirty_mail{
            boundary.get(), 2u, 127u, 0x5678u};
        AramOutboundProbe outbound{};
        outbound.expected_generation = 1u;
        SparseAramSpanProbe sparse_outbound{};
        std::atomic<bool> dirty_worker_ok{};
        std::thread dirty_worker([&] {
            const bool bound = boundary->worker_bind();
            const bool consumed = bound &&
                boundary->worker_apply_before_mail_consume(
                    2u, &consume_aram_mail, &dirty_mail);
            const std::uint8_t adjacent = 0xEFu;
            const std::uint8_t isolated = 0x77u;
            const bool wrote_word = consumed &&
                boundary->worker_write_u16_be(127u, 0xABCDu);
            const bool wrote_adjacent = wrote_word &&
                boundary->worker_write_span(129u, &adjacent, 1u);
            const bool wrote_isolated = wrote_adjacent &&
                boundary->worker_write_span(132u, &isolated, 1u);
            const bool flushed = wrote_isolated &&
                boundary->worker_flush_outbound(
                    1u, &commit_aram_span, &outbound);
            const std::uint8_t sparse = 0x44u;
            const bool wrote_sparse = flushed &&
                boundary->worker_write_span(128u, &sparse, 1u);
            const bool flushed_sparse = wrote_sparse &&
                boundary->worker_flush_outbound(
                    2u, &commit_sparse_aram_span, &sparse_outbound);
            const bool detached = boundary->worker_detach();
            dirty_worker_ok.store(
                bound && consumed && wrote_word && wrote_adjacent &&
                    wrote_isolated && flushed && wrote_sparse &&
                    flushed_sparse && detached,
                std::memory_order_release);
        });
        dirty_worker.join();

        const auto coherent = boundary->snapshot();
        passed &= expect(
            dirty_worker_ok.load(std::memory_order_acquire) &&
                dirty_mail.observed_after_apply && outbound.valid &&
                outbound.calls == 2u && sparse_outbound.valid &&
                sparse_outbound.calls == 1u &&
                coherent.seed_packets_staged == 1u &&
                coherent.seed_packets_applied == 1u &&
                coherent.dirty_packets_staged == 1u &&
                coherent.dirty_packets_applied == 1u &&
                coherent.dirty_pages_staged == 2u &&
                coherent.dirty_pages_applied == 2u &&
                coherent.last_cpu_consumed_generation == 2u &&
                coherent.last_outbound_generation == 2u &&
                coherent.outbound_dirty_pages == 0u &&
                coherent.worker_read_spans == 3u &&
                coherent.worker_write_spans == 4u &&
                coherent.worker_write_bytes == 5u &&
                coherent.worker_word_writes == 1u &&
                coherent.outbound_flushes == 2u &&
                coherent.outbound_span_attempts == 3u &&
                coherent.outbound_span_callbacks == 3u &&
                coherent.outbound_bytes == 5u &&
                coherent.maximum_outbound_spans_per_flush == 2u &&
                coherent.maximum_outbound_bytes_per_flush == 4u &&
                coherent.worker_hot_telemetry_publications >= 2u &&
                !coherent.failed,
            "DSP ARAM page-edge PCM word is never torn and sparse successive flushes never leak stale dirty bits");
    }

    {
        auto boundary = std::make_unique<Boundary>();
        const bool staged = boundary->reset() &&
            boundary->cpu_stage_seed(
                10u,
                std::span<const std::uint8_t>{
                    seed.get(), Boundary::kMirrorSizeBytes});
        AramMailProbe mismatch_mail{
            boundary.get(), 11u, 127u, 0x1234u};
        std::atomic<bool> mismatch_rejected{};
        std::thread worker([&] {
            const bool bound = boundary->worker_bind();
            const bool consumed = boundary->worker_apply_before_mail_consume(
                11u, &consume_aram_mail, &mismatch_mail);
            const bool detached = boundary->worker_detach();
            mismatch_rejected.store(
                bound && !consumed && detached,
                std::memory_order_release);
        });
        worker.join();
        const auto mismatch = boundary->snapshot();
        passed &= expect(
            staged && mismatch_rejected.load(std::memory_order_acquire) &&
                mismatch_mail.calls == 0u && mismatch.failed &&
                mismatch.failure == Boundary::Failure::GenerationMismatch &&
                mismatch.generation_failures == 1u &&
                mismatch.seed_packets_applied == 0u,
            "DSP ARAM generation mismatch hard-fails before mirror apply or mail consume");
    }

    {
        auto boundary = std::make_unique<Boundary>();
        const bool first_staged = boundary->reset() &&
            boundary->cpu_stage_seed(
                1u,
                std::span<const std::uint8_t>{
                    seed.get(), Boundary::kMirrorSizeBytes});
        const bool second_rejected =
            !boundary->cpu_stage_dirty_pages(2u, {});
        const auto full = boundary->snapshot();
        passed &= expect(
            first_staged && second_rejected && full.failed &&
                full.failure == Boundary::Failure::QueueFull &&
                full.queue_full_failures == 1u && full.inbound_depth == 1u &&
                full.maximum_inbound_depth == 1u,
            "DSP ARAM one-hardware-mail staging slot hard-fails rather than overwrite a pending generation");
    }

    {
        auto boundary = std::make_unique<Boundary>();
        const bool seed_staged = boundary->reset() &&
            boundary->cpu_stage_seed(
                1u,
                std::span<const std::uint8_t>{
                    seed.get(), Boundary::kMirrorSizeBytes});
        BlockingAramMailProbe first_mail{boundary.get(), 1u};
        AramMailProbe second_mail{boundary.get(), 2u, 127u, 0x1234u};
        std::atomic<bool> stage_finished{};
        std::atomic<bool> stage_succeeded{};
        std::atomic<bool> staged_before_consume_commit{};
        std::atomic<bool> worker_succeeded{};

        std::thread worker([&] {
            const bool bound = boundary->worker_bind();
            const bool first_consumed = bound &&
                boundary->worker_apply_before_mail_consume(
                    1u, &consume_blocking_aram_mail, &first_mail);
            while (!stage_finished.load(std::memory_order_acquire)) {
                stage_finished.wait(false, std::memory_order_acquire);
            }
            const bool second_consumed = first_consumed &&
                stage_succeeded.load(std::memory_order_acquire) &&
                boundary->worker_apply_before_mail_consume(
                    2u, &consume_aram_mail, &second_mail);
            const bool detached = bound && boundary->worker_detach();
            worker_succeeded.store(
                bound && first_consumed && second_consumed && detached,
                std::memory_order_release);
        });
        while (!first_mail.entered.load(std::memory_order_acquire)) {
            first_mail.entered.wait(false, std::memory_order_acquire);
        }

        std::thread release_controller([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            staged_before_consume_commit.store(
                stage_finished.load(std::memory_order_acquire),
                std::memory_order_release);
            first_mail.release.store(true, std::memory_order_release);
            first_mail.release.notify_all();
        });
        const bool staged = boundary->cpu_stage_dirty_pages(2u, {});
        stage_succeeded.store(staged, std::memory_order_release);
        stage_finished.store(true, std::memory_order_release);
        stage_finished.notify_all();
        release_controller.join();
        worker.join();

        const auto ordered = boundary->snapshot();
        passed &= expect(
            seed_staged &&
                !staged_before_consume_commit.load(
                    std::memory_order_acquire) &&
                stage_succeeded.load(std::memory_order_acquire) &&
                worker_succeeded.load(std::memory_order_acquire) &&
                first_mail.observed_after_apply &&
                second_mail.observed_after_apply && !ordered.failed &&
                ordered.last_cpu_staged_generation == 2u &&
                ordered.last_cpu_consumed_generation == 2u,
            "DSP ARAM CMBL consume and inbound-slot retirement are one CPU-visible ordering boundary");
    }

    {
        auto boundary = std::make_unique<Boundary>();
        auto replacement =
            std::make_unique<std::uint8_t[]>(Boundary::kMirrorSizeBytes);
        replacement[10] = 0xBEu;
        replacement[11] = 0xEFu;
        const bool old_staged = boundary->reset() &&
            boundary->cpu_stage_seed(
                9u,
                std::span<const std::uint8_t>{
                    seed.get(), Boundary::kMirrorSizeBytes});
        boundary->cancel();
        const auto cancelled = boundary->snapshot();
        const bool replacement_staged = boundary->reset() &&
            boundary->cpu_stage_seed(
                1u,
                std::span<const std::uint8_t>{
                    replacement.get(), Boundary::kMirrorSizeBytes});
        AramMailProbe replacement_mail{
            boundary.get(), 1u, 10u, 0xBEEFu};
        std::atomic<bool> replacement_ok{};
        std::thread worker([&] {
            const bool bound = boundary->worker_bind();
            const bool consumed = bound &&
                boundary->worker_apply_before_mail_consume(
                    1u, &consume_aram_mail, &replacement_mail);
            const bool detached = boundary->worker_detach();
            replacement_ok.store(
                bound && consumed && detached,
                std::memory_order_release);
        });
        worker.join();
        const auto reset_stats = boundary->snapshot();
        passed &= expect(
            old_staged && cancelled.cancelled &&
                cancelled.cancelled_inbound_packets == 1u &&
                replacement_staged &&
                replacement_ok.load(std::memory_order_acquire) &&
                replacement_mail.observed_after_apply &&
                reset_stats.resets == 2u && reset_stats.cancellations == 1u &&
                reset_stats.last_cpu_staged_generation == 1u &&
                reset_stats.last_cpu_consumed_generation == 1u &&
                !reset_stats.failed,
            "DSP ARAM cancel/reset discards stale seed generations and requires a fresh seed");
    }

    {
        auto boundary = std::make_unique<Boundary>();
        const bool staged = boundary->reset() &&
            boundary->cpu_stage_seed(
                1u,
                std::span<const std::uint8_t>{
                    seed.get(), Boundary::kMirrorSizeBytes});
        AramMailProbe shutdown_mail{
            boundary.get(), 1u, 127u, 0x1234u};
        std::atomic<bool> worker_bound{};
        std::atomic<bool> release_worker{};
        std::atomic<bool> shutdown_observed{};
        std::thread worker([&] {
            const bool bound = boundary->worker_bind();
            worker_bound.store(bound, std::memory_order_release);
            while (!release_worker.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            const bool consumed = boundary->worker_apply_before_mail_consume(
                1u, &consume_aram_mail, &shutdown_mail);
            const bool detached = boundary->worker_detach();
            shutdown_observed.store(
                bound && !consumed && detached,
                std::memory_order_release);
        });
        const bool bound_before_shutdown = spin_until(
            [&] { return worker_bound.load(std::memory_order_acquire); },
            std::chrono::seconds(1));
        boundary->shutdown();
        release_worker.store(true, std::memory_order_release);
        worker.join();
        const auto shutdown = boundary->snapshot();
        passed &= expect(
            staged && bound_before_shutdown &&
                shutdown_observed.load(std::memory_order_acquire) &&
                shutdown_mail.calls == 0u && shutdown.shutdown &&
                shutdown.inbound_depth == 0u && !shutdown.worker_bound,
            "DSP ARAM shutdown rejects stale worker consumption and permits bounded detach");
    }

    {
        auto boundary = std::make_unique<Boundary>();
        const bool staged = boundary->reset() &&
            boundary->cpu_stage_seed(
                1u,
                std::span<const std::uint8_t>{
                    seed.get(), Boundary::kMirrorSizeBytes});
        AramMailProbe mail{boundary.get(), 1u, 127u, 0x1234u};
        RejectAramSpanProbe rejection{};
        std::atomic<bool> rejection_observed{};
        std::thread worker([&] {
            const bool bound = boundary->worker_bind();
            const bool consumed = bound &&
                boundary->worker_apply_before_mail_consume(
                    1u, &consume_aram_mail, &mail);
            const std::uint8_t value = 0x5Au;
            const bool wrote = consumed &&
                boundary->worker_write_span(64u, &value, 1u);
            const bool flushed = wrote &&
                boundary->worker_flush_outbound(
                    1u, &reject_aram_span, &rejection);
            const bool detached = boundary->worker_detach();
            rejection_observed.store(
                bound && consumed && wrote && !flushed && detached,
                std::memory_order_release);
        });
        worker.join();
        const auto rejected = boundary->snapshot();
        passed &= expect(
            staged && rejection_observed.load(std::memory_order_acquire) &&
                rejection.calls == 1u && rejected.failed &&
                rejected.failure == Boundary::Failure::OutboundSpanRejected &&
                rejected.outbound_span_rejections == 1u &&
                rejected.outbound_dirty_pages == 1u &&
                rejected.last_outbound_generation == 0u,
            "DSP ARAM outbound rejection is terminal and never publishes a false completion generation");
    }

    {
        auto boundary = std::make_unique<Boundary>();
        const bool staged = boundary->reset() &&
            boundary->cpu_stage_seed(
                1u,
                std::span<const std::uint8_t>{
                    seed.get(), Boundary::kMirrorSizeBytes});
        AramMailProbe mail{boundary.get(), 1u, 127u, 0x1234u};
        std::atomic<bool> dirty_ready{};
        std::atomic<bool> release_worker{};
        std::atomic<bool> worker_clean{};
        std::thread worker([&] {
            const bool bound = boundary->worker_bind();
            const bool consumed = bound &&
                boundary->worker_apply_before_mail_consume(
                    1u, &consume_aram_mail, &mail);
            const std::uint8_t value = 0xA5u;
            const bool wrote = consumed && boundary->worker_write_span(
                512u, &value, 1u);
            dirty_ready.store(wrote, std::memory_order_release);
            while (!release_worker.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            const bool detached = bound && boundary->worker_detach();
            worker_clean.store(
                bound && consumed && wrote && detached,
                std::memory_order_release);
        });
        const bool dirty_before_cancel = spin_until(
            [&] { return dirty_ready.load(std::memory_order_acquire); },
            std::chrono::seconds(1));
        boundary->cancel();
        release_worker.store(true, std::memory_order_release);
        worker.join();
        const auto cancelled = boundary->snapshot();
        const bool reset_after_cancel = boundary->reset();
        const auto reset = boundary->snapshot();
        passed &= expect(
            staged && dirty_before_cancel &&
                worker_clean.load(std::memory_order_acquire) &&
                cancelled.cancelled && cancelled.cancelled_outbound_pages == 1u &&
                cancelled.outbound_dirty_pages == 1u &&
                !cancelled.worker_bound && reset_after_cancel &&
                reset.outbound_dirty_pages == 0u && !reset.failed,
            "DSP ARAM cancel accounts for dirty outbound state, permits worker detach, and clears it only at the next quiescent reset");
    }

    return passed;
}

bool test_channel_selection_dma_probe() {
    bool passed = true;

    const auto begin = [] {
        galaxy::DspChannelSelectionDmaProbeRecorder recorder{};
        recorder.generated_probe_contract_version =
            galaxy::kDspGeneratedProbeContractVersion;
        recorder.generated_probe_capabilities =
            galaxy::kDspGeneratedProbeRequiredCapabilities;
        galaxy::dsp_channel_selection_dma_probe_begin_session(recorder);
        return recorder;
    };
    const auto publish = [](
                             galaxy::DspChannelSelectionDmaProbeRecorder& recorder,
                             const std::array<std::uint16_t, 4>& words) {
        for (std::uint16_t index = 0u; index < words.size(); ++index) {
            galaxy::dsp_channel_selection_dma_probe_record_selection_write(
                &recorder,
                static_cast<std::uint16_t>(0x04FCu + index),
                words[index]);
        }
    };
    const auto selected_branch = [](
                                     galaxy::DspChannelSelectionDmaProbeRecorder&
                                         recorder,
                                     std::uint16_t channel,
                                     std::uint32_t host_address,
                                     std::uint16_t observed_word) {
        galaxy::dsp_channel_selection_dma_probe_record_selected_channel_branch(
            &recorder, channel, observed_word, host_address);
    };
    const auto channel_dma = [](
                                 galaxy::DspChannelSelectionDmaProbeRecorder&
                                     recorder,
                                 std::uint32_t host_address = 0x807B04E0u,
                                 std::uint16_t pc = 0x05EFu) {
        galaxy::dsp_channel_selection_dma_probe_record_dma(
            &recorder,
            pc,
            false,
            false,
            host_address,
            0x0800u,
            0x0180u);
    };

    passed &= expect(
        galaxy::dsp_generated_probe_contract_compatible(
            galaxy::kDspGeneratedProbeContractVersion,
            galaxy::kDspGeneratedProbeRequiredCapabilities) &&
            !galaxy::dsp_generated_probe_contract_compatible(
                galaxy::kDspGeneratedProbeContractVersion + 1u,
                galaxy::kDspGeneratedProbeRequiredCapabilities) &&
            !galaxy::dsp_generated_probe_contract_compatible(
                galaxy::kDspGeneratedProbeContractVersion,
                galaxy::kDspGeneratedProbeCapabilitySelectionPublication),
        "generated DSP probe contract rejects stale versions and missing selected-channel hook capabilities");

    // Shape alone is never selected-channel evidence, before or after an
    // active publication.  The exact PC 0x02EF branch identity is mandatory.
    auto dma_before_active = begin();
    channel_dma(dma_before_active);
    passed &= expect(
        dma_before_active.publication_generation == 0u &&
            dma_before_active.exact_channel_record_dmas == 1u &&
            dma_before_active.validated_selected_channel_record_dmas == 0u &&
            dma_before_active.unmatched_channel_record_dmas == 1u &&
            dma_before_active
                    .dmas_without_validated_selected_channel_branch ==
                1u &&
            galaxy::dsp_channel_selection_dma_probe_state(
                dma_before_active) ==
                galaxy::DspChannelSelectionDmaProbeState::
                    NoCompletePublication,
        "selected-channel probe never credits an exact-shape DMA without a validated render branch");

    auto arbitrary_dma = begin();
    publish(arbitrary_dma, {0x8000u, 0u, 0u, 0u});
    channel_dma(arbitrary_dma);
    passed &= expect(
        arbitrary_dma.current_publication_active &&
            arbitrary_dma.validated_selected_channel_record_dmas == 0u &&
            arbitrary_dma.unmatched_channel_record_dmas == 1u &&
            arbitrary_dma.matched_active_publications == 0u &&
            galaxy::dsp_channel_selection_dma_probe_state(arbitrary_dma) ==
                galaxy::DspChannelSelectionDmaProbeState::
                    ActiveAwaitingValidatedSelectedChannelDma,
        "an arbitrary D[0800]/0x180 DMA cannot claim selected-channel causality from an active mask");

    // Any in-range write after a complete publication closes its causal window
    // immediately.  This includes a new zero prefix, an out-of-order suffix,
    // and a write that invalidates an already captured branch identity.
    auto active_partial_zero = begin();
    publish(active_partial_zero, {0x8000u, 0u, 0u, 0u});
    galaxy::dsp_channel_selection_dma_probe_record_selection_write(
        &active_partial_zero, 0x04FCu, 0u);
    channel_dma(active_partial_zero);
    passed &= expect(
        active_partial_zero.partial_word_count == 1u &&
            active_partial_zero.current_publication_blocked_by_selection_write &&
            active_partial_zero.validated_selected_channel_record_dmas == 0u &&
            active_partial_zero.dmas_after_intervening_selection == 1u &&
            galaxy::dsp_channel_selection_dma_probe_state(
                active_partial_zero) ==
                galaxy::DspChannelSelectionDmaProbeState::
                    IncompletePublicationAfterCurrent,
        "active publication followed by a partial zero publication can never credit a later DMA");

    auto active_out_of_order = begin();
    publish(active_out_of_order, {0x8000u, 0u, 0u, 0u});
    galaxy::dsp_channel_selection_dma_probe_record_selection_write(
        &active_out_of_order, 0x04FDu, 0u);
    channel_dma(active_out_of_order);
    passed &= expect(
        active_out_of_order.invalid_order_writes == 1u &&
            active_out_of_order.current_publication_blocked_by_selection_write &&
            active_out_of_order.validated_selected_channel_record_dmas == 0u &&
            active_out_of_order.dmas_after_intervening_selection == 1u,
        "an out-of-order partial selection write closes the preceding active publication window");

    auto branch_then_partial = begin();
    publish(branch_then_partial, {0x8000u, 0u, 0u, 0u});
    selected_branch(branch_then_partial, 0u, 0x807B04E0u, 0x8000u);
    galaxy::dsp_channel_selection_dma_probe_record_selection_write(
        &branch_then_partial, 0x04FCu, 0x8000u);
    channel_dma(branch_then_partial);
    passed &= expect(
        branch_then_partial.validated_selected_channel_branches == 1u &&
            branch_then_partial.expired_pending_selected_channel_branches ==
                1u &&
            branch_then_partial.validated_selected_channel_record_dmas == 0u &&
            branch_then_partial.dmas_after_intervening_selection == 1u,
        "an intervening partial selection write invalidates a previously validated render-branch identity before DMA");

    auto branch_then_out_of_range = begin();
    publish(branch_then_out_of_range, {0x8000u, 0u, 0u, 0u});
    selected_branch(
        branch_then_out_of_range, 0u, 0x807B04E0u, 0x8000u);
    galaxy::dsp_channel_selection_dma_probe_record_selection_write(
        &branch_then_out_of_range, 0x0500u, 0x8000u);
    channel_dma(branch_then_out_of_range);
    passed &= expect(
        branch_then_out_of_range.out_of_range_handler_writes == 1u &&
            branch_then_out_of_range.intervening_selection_writes == 1u &&
            branch_then_out_of_range
                    .current_publication_blocked_by_selection_write &&
            branch_then_out_of_range
                    .expired_pending_selected_channel_branches ==
                1u &&
            branch_then_out_of_range
                    .validated_selected_channel_record_dmas ==
                0u &&
            branch_then_out_of_range.dmas_after_intervening_selection == 1u,
        "an out-of-range exact-handler store fails closed and invalidates both current publication and pending branch identity");

    auto skipped_disabled_dma = begin();
    publish(skipped_disabled_dma, {0x8000u, 0u, 0u, 0u});
    selected_branch(skipped_disabled_dma, 0u, 0x807B04E0u, 0x8000u);
    galaxy::DspContext skipped_disabled_context{};
    skipped_disabled_context.channel_selection_dma_probe =
        &skipped_disabled_dma;
    skipped_disabled_context.pc = 0x05EFu;
    skipped_disabled_context.ifx[galaxy::kDspIfxAmdm] = 1u;
    galaxy::dsp_ifx_write(
        skipped_disabled_context, galaxy::kDspIfxDsbl, 0x0180u);
    channel_dma(skipped_disabled_dma);
    passed &= expect(
        skipped_disabled_dma.skipped_dma_attempts_with_pending_branch == 1u &&
            skipped_disabled_dma.expired_pending_selected_channel_branches ==
                1u &&
            !skipped_disabled_dma.pending_selected_channel_branch_valid &&
            skipped_disabled_dma.validated_selected_channel_record_dmas == 0u &&
            skipped_disabled_dma.unmatched_channel_record_dmas == 1u,
        "an AMDM-disabled one-shot DMA consumes pending selected-channel identity before a later identical transfer");

    auto skipped_zero_dma = begin();
    publish(skipped_zero_dma, {0x8000u, 0u, 0u, 0u});
    selected_branch(skipped_zero_dma, 0u, 0x807B04E0u, 0x8000u);
    galaxy::DspContext skipped_zero_context{};
    skipped_zero_context.channel_selection_dma_probe = &skipped_zero_dma;
    skipped_zero_context.pc = 0x05EFu;
    galaxy::dsp_ifx_write(
        skipped_zero_context, galaxy::kDspIfxDsbl, 0u);
    channel_dma(skipped_zero_dma);
    passed &= expect(
        skipped_zero_dma.skipped_dma_attempts_with_pending_branch == 1u &&
            skipped_zero_dma.expired_pending_selected_channel_branches == 1u &&
            !skipped_zero_dma.pending_selected_channel_branch_valid &&
            skipped_zero_dma.validated_selected_channel_record_dmas == 0u &&
            skipped_zero_dma.unmatched_channel_record_dmas == 1u,
        "a zero-length one-shot DMA consumes pending selected-channel identity before a later identical transfer");

    auto disordered = begin();
    galaxy::dsp_channel_selection_dma_probe_record_selection_write(
        &disordered, 0x04FCu, 0x8000u);
    galaxy::dsp_channel_selection_dma_probe_record_selection_write(
        &disordered, 0x04FEu, 0u);
    galaxy::dsp_channel_selection_dma_probe_record_selection_write(
        &disordered, 0x04FFu, 0u);
    const bool disordered_rejected = disordered.complete_publications == 0u &&
                                     disordered.invalid_order_writes == 2u;
    publish(disordered, {0x8000u, 0u, 0u, 0u});
    selected_branch(disordered, 0u, 0x807B07E0u, 0x8000u);
    channel_dma(disordered, 0x807B07E0u);
    passed &= expect(
        disordered_rejected && disordered.complete_publications == 1u &&
            disordered.current_publication_active &&
            disordered.current_publication_matched &&
            disordered.validated_selected_channel_record_dmas == 1u &&
            disordered.last_matched_publication_generation == 1u,
        "partial/out-of-order input recovers only through a new complete publication and matching selected-channel branch");

    // Channel 17 maps to word 1, bit 14.  Both that exact bit and the address
    // calculated by PC 0x02EF must match the PC 0x05EF DMA.
    auto active_dma = begin();
    publish(active_dma, {0u, 0x4000u, 0u, 0u});
    const std::uint64_t publication_sequence =
        active_dma.current_publication_sequence;
    selected_branch(active_dma, 17u, 0x807B04E0u, 0x4000u);
    channel_dma(active_dma);
    // A second arbitrary shape-identical DMA has no new render identity.
    channel_dma(active_dma, 0x807B0660u);
    const auto active_snapshot =
        galaxy::dsp_channel_selection_dma_probe_snapshot(active_dma);
    passed &= expect(
        active_snapshot.current_first_match_sequence > publication_sequence &&
            active_snapshot.exact_channel_record_dmas == 2u &&
            active_snapshot.validated_selected_channel_record_dmas == 1u &&
            active_snapshot.unmatched_channel_record_dmas == 1u &&
            active_snapshot.matched_active_publications == 1u &&
            active_snapshot.validated_channel_sequence == 1u &&
            active_snapshot.last_validated_channel == 17u &&
            active_snapshot.current_first_match_channel == 17u &&
            active_snapshot.current_first_match_dma_pc == 0x05EFu &&
            active_snapshot.current_first_match_dma_host_address ==
                0x807B04E0u &&
            galaxy::dsp_channel_selection_dma_probe_state(active_snapshot) ==
                galaxy::DspChannelSelectionDmaProbeState::
                    ActiveWithValidatedSelectedChannelDma,
        "only matching channel bit, render-branch address, DMA PC, and temporal order receive selected-channel credit");

    auto host_mismatch = begin();
    publish(host_mismatch, {0x8000u, 0u, 0u, 0u});
    selected_branch(host_mismatch, 0u, 0x807B04E0u, 0x8000u);
    channel_dma(host_mismatch, 0x807B0660u);
    selected_branch(host_mismatch, 0u, 0x807B04E0u, 0x8000u);
    channel_dma(host_mismatch, 0x807B04E0u, 0x060Bu);
    passed &= expect(
        host_mismatch.validated_selected_channel_record_dmas == 0u &&
            host_mismatch.selected_channel_dma_identity_mismatches == 2u &&
            host_mismatch.unmatched_channel_record_dmas == 2u,
        "selected-channel credit rejects both host-address and DMA-PC identity mismatches");

    // A later zero publication changes current state but cannot hide the
    // lifetime fact that an earlier active generation matched exactly.
    publish(active_dma, {0u, 0u, 0u, 0u});
    const auto replaced_snapshot =
        galaxy::dsp_channel_selection_dma_probe_snapshot(active_dma);
    passed &= expect(
        replaced_snapshot.current_publication_generation == 2u &&
            replaced_snapshot.current_first_match_sequence == 0u &&
            replaced_snapshot.last_matched_publication_generation == 1u &&
            replaced_snapshot.matched_active_publications == 1u &&
            galaxy::dsp_channel_selection_dma_probe_state(replaced_snapshot) ==
                galaxy::DspChannelSelectionDmaProbeState::
                    CurrentPublicationZero &&
            galaxy::dsp_channel_selection_dma_probe_lifetime_state(
                replaced_snapshot) ==
                galaxy::DspChannelSelectionDmaProbeLifetimeState::
                    ActivePublicationWithValidatedSelectedChannelDma,
        "current zero publication remains distinct from lifetime exact selected-channel match evidence");

    // Reset is an ordered boundary: it retains lifetime evidence, clears all
    // current/partial state, and prevents post-reset DMA or suffix words from
    // matching a pre-reset generation.
    auto reset_history = begin();
    publish(reset_history, {0x8000u, 0u, 0u, 0u});
    selected_branch(reset_history, 0u, 0x807B04E0u, 0x8000u);
    channel_dma(reset_history);
    galaxy::dsp_channel_selection_dma_probe_record_selection_write(
        &reset_history, 0x04FCu, 0u);
    galaxy::dsp_channel_selection_dma_probe_record_reset_boundary(
        reset_history);
    const auto sequence_after_reset = reset_history.worker_sequence;
    channel_dma(reset_history);
    galaxy::dsp_channel_selection_dma_probe_begin_session(reset_history);
    galaxy::dsp_channel_selection_dma_probe_record_selection_write(
        &reset_history, 0x04FEu, 0u);
    galaxy::dsp_channel_selection_dma_probe_record_selection_write(
        &reset_history, 0x04FFu, 0u);
    passed &= expect(
        reset_history.session_count == 2u && reset_history.reset_count == 1u &&
            reset_history.worker_sequence > sequence_after_reset &&
            reset_history.complete_publications == 1u &&
            reset_history.matched_active_publications == 1u &&
            reset_history.validated_selected_channel_record_dmas == 1u &&
            reset_history.unmatched_channel_record_dmas == 1u &&
            !reset_history.current_publication_valid &&
            galaxy::dsp_channel_selection_dma_probe_lifetime_state(
                reset_history) ==
                galaxy::DspChannelSelectionDmaProbeLifetimeState::
                    ActivePublicationWithValidatedSelectedChannelDma,
        "DSPCR reset preserves lifetime match history while invalidating current publication, pending identity, and partial suffixes");

    // Standalone fixtures own a reset-local recorder; an externally attached
    // recorder keeps lifetime totals but must close current/pending identity.
    galaxy::NativeDspCoprocessor lifecycle;
    lifecycle.set_channel_selection_dma_probe_enabled(true);
    const bool installed_before_worker =
        lifecycle.context().channel_selection_dma_probe != nullptr;
    lifecycle.reset();
    const auto reset_snapshot = lifecycle.channel_selection_dma_probe_snapshot();
    passed &= expect(
        installed_before_worker &&
            lifecycle.context().channel_selection_dma_probe != nullptr &&
            reset_snapshot.worker_sequence == 0u &&
            reset_snapshot.complete_publications == 0u &&
            reset_snapshot.session_count == 1u,
        "standalone owned probe remains reset-local and reattaches to a rebuilt DSP context");

    auto external = begin();
    publish(external, {0x8000u, 0u, 0u, 0u});
    selected_branch(external, 0u, 0x807B04E0u, 0x8000u);
    channel_dma(external);
    galaxy::NativeDspCoprocessor externally_owned_lifecycle;
    externally_owned_lifecycle.attach_channel_selection_dma_probe(&external);
    externally_owned_lifecycle.reset();
    channel_dma(external);
    const auto external_snapshot =
        externally_owned_lifecycle.channel_selection_dma_probe_snapshot();
    passed &= expect(
        externally_owned_lifecycle.context().channel_selection_dma_probe ==
                &external &&
            external_snapshot.session_count == 2u &&
            external_snapshot.reset_count == 1u &&
            external_snapshot.worker_sequence == 8u &&
            !external_snapshot.current_publication_valid &&
            !external_snapshot.pending_selected_channel_branch_valid &&
            external_snapshot.matched_active_publications == 1u &&
            external_snapshot.validated_selected_channel_record_dmas == 1u &&
            external_snapshot.unmatched_channel_record_dmas == 1u,
        "externally owned process-lifetime probe preserves totals but rejects cross-reset current and branch identity");

    return passed;
}

bool test_audio_causal_recorder() {
    bool passed = true;

    passed &= expect(
        galaxy::dsp_audio_causal_classify_dma(
            false, false, 0x0800u, 0x0180u) ==
                galaxy::DspAudioCausalEventKind::ChannelRecordIn &&
            galaxy::dsp_audio_causal_classify_dma(
                false, false, 0x0B00u, 0x00A0u) ==
                galaxy::DspAudioCausalEventKind::SampleWindowIn &&
            galaxy::dsp_audio_causal_classify_dma(
                false, false, 0x0B60u, 0x00C0u) ==
                galaxy::DspAudioCausalEventKind::SampleWindowIn &&
            galaxy::dsp_audio_causal_classify_dma(
                false, true, 0x0800u, 0x0180u) ==
                galaxy::DspAudioCausalEventKind::DmaIn,
        "audio causal DMA classifier is exact and does not credit IMEM shapes");

    constexpr std::array<std::uint32_t, 6> output_bus_addresses{
        0x1A00u, 0x1AC0u, 0x1B80u, 0x1C40u, 0x1D00u, 0x1DC0u};
    bool all_output_buses_classified = true;
    for (const auto address : output_bus_addresses) {
        all_output_buses_classified = all_output_buses_classified &&
            galaxy::dsp_audio_causal_classify_dma(
                true, false, address, 0x00A0u) ==
                galaxy::DspAudioCausalEventKind::OutputBlockOut;
    }
    passed &= expect(
        all_output_buses_classified &&
            galaxy::dsp_audio_causal_classify_dma(
                true, false, 0x1A00u, 0x0080u) ==
                galaxy::DspAudioCausalEventKind::DmaOut,
        "audio causal DMA classifier covers all six exact output DMA regions");

    galaxy::DspAudioCausalRecorder bounded{};
    for (std::size_t index = 0u;
         index < galaxy::kDspAudioCausalRecordCapacity + 2u;
         ++index) {
        galaxy::dsp_audio_causal_record_dma(
            &bounded,
            0x05EFu,
            0x0004u,
            false,
            false,
            static_cast<std::uint32_t>(0x80000000u + index * 4u),
            0x0400u,
            4u,
            index == 0u ? 0u : 1u);
    }
    const auto bounded_snapshot =
        galaxy::dsp_audio_causal_snapshot(bounded);
    passed &= expect(
        bounded_snapshot.record_count ==
                galaxy::kDspAudioCausalRecordCapacity &&
            bounded_snapshot.total_events ==
                galaxy::kDspAudioCausalRecordCapacity + 2u &&
            bounded_snapshot.overwritten_events == 2u &&
            bounded_snapshot.records.front().sequence == 3u &&
            bounded_snapshot.records[
                galaxy::kDspAudioCausalRecordCapacity - 1u]
                    .sequence ==
                galaxy::kDspAudioCausalRecordCapacity + 2u &&
            bounded_snapshot.dma_in_transfers ==
                galaxy::kDspAudioCausalRecordCapacity + 2u &&
            bounded_snapshot.dma_in_bytes ==
                (galaxy::kDspAudioCausalRecordCapacity + 2u) * 4u &&
            bounded_snapshot.dma_in_nonzero_bytes ==
                galaxy::kDspAudioCausalRecordCapacity + 1u,
        "audio causal recorder retains one ordered bounded suffix and lossless aggregates");

    // Capture disabled: the context carries a null pointer, recorder state is
    // untouched, and the architectural DRAM write is byte-for-byte identical.
    galaxy::NativeDspCoprocessor disabled;
    disabled.run_native_entry(+[](galaxy::DspContext& context) {
        galaxy::dsp_dram_write(context, 0x0D00u, 0x2468u);
        galaxy::dsp_audio_causal_record_dma(
            context.audio_causal_recorder,
            context.pc,
            0x0004u,
            false,
            false,
            0x807B04E0u,
            0x0800u,
            0x0180u,
            7u);
    });
    const auto disabled_snapshot = disabled.audio_causal_snapshot();

    galaxy::NativeDspCoprocessor enabled;
    enabled.set_audio_causal_capture_enabled(true);
    enabled.run_native_entry(+[](galaxy::DspContext& context) {
        context.pc = 0x0715u;
        galaxy::dsp_dram_write(context, 0x04FCu, 0x0005u);
        galaxy::dsp_dram_write(context, 0x04FFu, 0x8000u);
        context.pc = 0x0714u;
        galaxy::dsp_dram_write(context, 0x04FDu, 0xFFFFu);
        std::array<std::uint8_t, 0x0180u> channel_record{};
        channel_record[0x11u] = 0x02u;  // mixer bus 2
        channel_record[0x12u] = 0x01u;  // current gain 0x0100
        channel_record[0x14u] = 0x02u;  // target gain 0x0200
        channel_record[0x17u] = 0x01u;  // delta 0x0001
        galaxy::dsp_audio_causal_record_dma(
            context.audio_causal_recorder,
            0x05EFu,
            0x0004u,
            false,
            false,
            0x807B04E0u,
            0x0800u,
            0x0180u,
            4u,
            channel_record);
        galaxy::dsp_audio_causal_record_dma(
            context.audio_causal_recorder,
            0x05EFu,
            0x0004u,
            false,
            false,
            0x8087CD00u,
            0x0B00u,
            0x00A0u,
            9u);
        galaxy::dsp_dram_write(context, 0x038Eu, 0x4000u);
        galaxy::dsp_dram_write(context, 0x0D00u, 0x2468u);
        galaxy::dsp_audio_causal_record_dma(
            context.audio_causal_recorder,
            0x05EFu,
            0x0005u,
            false,
            true,
            0x807AEAA0u,
            0x1A00u,
            0x00A0u,
            2u);
        galaxy::dsp_audio_causal_record_accelerator_sample(
            context.audio_causal_recorder,
            0x0710u,
            0x0002u,
            0x00123456u,
            static_cast<std::int16_t>(0x1234),
            0x4321u);
    });
    const auto enabled_snapshot = enabled.audio_causal_snapshot();
    passed &= expect(
        disabled.context().audio_causal_recorder == nullptr &&
            disabled.context().dram[0x0D00u] == 0x2468u &&
            disabled_snapshot.total_events == 0u &&
            enabled.context().dram[0x0D00u] == 0x2468u &&
            enabled_snapshot.channel_record_transfers == 1u &&
            enabled_snapshot.channel_record_nonzero_bytes == 4u &&
            enabled_snapshot.channel_mixer_inspected_transfers == 1u &&
            enabled_snapshot.channel_mixer_nonzero_record_transfers == 1u &&
            enabled_snapshot.channel_mixer_nonzero_tuples == 1u &&
            enabled_snapshot.channel_mixer_nonzero_gain_tuples == 1u &&
            enabled_snapshot.channel_selection_writes == 2u &&
            enabled_snapshot.channel_selection_nonzero_writes == 2u &&
            enabled_snapshot.channel_selection_nonzero_bits == 3u &&
            enabled_snapshot.channel_selection_valid_mask == 0x09u &&
            enabled_snapshot.channel_selection_words[0] == 0x0005u &&
            enabled_snapshot.channel_selection_words[1] == 0u &&
            enabled_snapshot.channel_selection_words[2] == 0u &&
            enabled_snapshot.channel_selection_words[3] == 0x8000u &&
            enabled_snapshot.has_first_routed_channel_record &&
            enabled_snapshot.first_routed_channel_record
                    .mixer_first_index == 0u &&
            enabled_snapshot.first_routed_channel_record.mixer_bus == 2u &&
            enabled_snapshot.first_routed_channel_record.mixer_current ==
                0x0100u &&
            enabled_snapshot.first_routed_channel_record.mixer_target ==
                0x0200u &&
            enabled_snapshot.first_routed_channel_record.mixer_delta == 1u &&
            enabled_snapshot.sample_window_transfers == 1u &&
            enabled_snapshot.sample_window_nonzero_bytes == 9u &&
            enabled_snapshot.mixer_output_word_writes == 1u &&
            enabled_snapshot.mixer_output_nonzero_word_writes == 1u &&
            enabled_snapshot.primary_mix_bus_word_writes == 1u &&
            enabled_snapshot.primary_mix_bus_nonzero_word_writes == 1u &&
            enabled_snapshot.quiescent_mixer_level_valid &&
            enabled_snapshot.quiescent_mixer_level == 0x4000u &&
            enabled_snapshot.output_block_transfers == 1u &&
            enabled_snapshot.output_block_nonzero_bytes == 2u &&
            enabled_snapshot.accelerator_active_reads == 1u &&
            enabled_snapshot.accelerator_raw_nonzero_reads == 1u &&
            enabled_snapshot.accelerator_decoded_nonzero_reads == 1u &&
            galaxy::dsp_audio_causal_classify(enabled_snapshot) ==
                galaxy::DspAudioCausalClassification::OutputObserved,
        "audio causal capture is disabled-neutral and quiescent snapshots preserve correlated stages");

    galaxy::DspAudioCausalSnapshot classification{};
    auto classification_is = [&classification](
                                 galaxy::DspAudioCausalClassification expected) {
        return galaxy::dsp_audio_causal_classify(classification) == expected;
    };
    bool classification_ladder = classification_is(
        galaxy::DspAudioCausalClassification::NoDmaInput);
    classification.dma_in_transfers = 1u;
    classification_ladder = classification_ladder && classification_is(
        galaxy::DspAudioCausalClassification::DmaInputSilent);
    classification.dma_in_nonzero_bytes = 1u;
    classification_ladder = classification_ladder && classification_is(
        galaxy::DspAudioCausalClassification::NoChannelRecord);
    classification.channel_record_transfers = 1u;
    classification_ladder = classification_ladder && classification_is(
        galaxy::DspAudioCausalClassification::ChannelRecordSilent);
    classification.channel_record_nonzero_transfers = 1u;
    classification_ladder = classification_ladder && classification_is(
        galaxy::DspAudioCausalClassification::ChannelRoutingUnobserved);
    classification.channel_mixer_inspected_transfers = 1u;
    classification_ladder = classification_ladder && classification_is(
        galaxy::DspAudioCausalClassification::ChannelRoutingSilent);
    classification.channel_mixer_nonzero_gain_tuples = 1u;
    classification_ladder = classification_ladder && classification_is(
        galaxy::DspAudioCausalClassification::NoSampleSource);
    classification.sample_window_transfers = 1u;
    classification_ladder = classification_ladder && classification_is(
        galaxy::DspAudioCausalClassification::SampleSourceSilent);
    classification.sample_window_nonzero_transfers = 1u;
    classification_ladder = classification_ladder && classification_is(
        galaxy::DspAudioCausalClassification::MixerOutputUnwritten);
    classification.mixer_output_word_writes = 1u;
    classification_ladder = classification_ladder && classification_is(
        galaxy::DspAudioCausalClassification::MixerOutputSilent);
    classification.mixer_output_nonzero_word_writes = 1u;
    classification_ladder = classification_ladder && classification_is(
        galaxy::DspAudioCausalClassification::OutputDmaAbsent);
    classification.output_block_transfers = 1u;
    classification_ladder = classification_ladder && classification_is(
        galaxy::DspAudioCausalClassification::OutputDmaSilent);
    classification.output_block_nonzero_transfers = 1u;
    classification_ladder = classification_ladder && classification_is(
        galaxy::DspAudioCausalClassification::OutputObserved);
    passed &= expect(
        classification_ladder,
        "audio causal classification reports the earliest unproven DSP handoff");

    galaxy::DspAudioCausalSnapshot selection_state{};
    auto selection_state_is = [&selection_state](
                                  galaxy::DspAudioCausalChannelSelectionState
                                      expected) {
        return galaxy::dsp_audio_causal_channel_selection_state(
                   selection_state) == expected;
    };
    bool selection_state_ladder = selection_state_is(
        galaxy::DspAudioCausalChannelSelectionState::Unpublished);
    selection_state.channel_selection_writes = 4u;
    selection_state_ladder = selection_state_ladder && selection_state_is(
        galaxy::DspAudioCausalChannelSelectionState::PublishedZeroOnly);
    selection_state.channel_selection_nonzero_writes = 1u;
    selection_state_ladder = selection_state_ladder && selection_state_is(
        galaxy::DspAudioCausalChannelSelectionState::
            ActivePublishedWithoutChannelRecord);
    selection_state.channel_record_transfers = 1u;
    selection_state_ladder = selection_state_ladder && selection_state_is(
        galaxy::DspAudioCausalChannelSelectionState::
            ActivePublishedWithChannelRecord);
    passed &= expect(
        selection_state_ladder,
        "audio causal channel-selection state distinguishes idle publication from a missing active channel-record transfer");

    return passed;
}

bool test_native_audio_bus_controls() {
    using galaxy::audio::NativeAudioBus;
    using galaxy::audio::NativeAudioBusClassification;
    bool passed = true;

    const auto music = galaxy::audio::native_audio_bus_from_sound_id(
        0x01000000u);
    const auto voice = galaxy::audio::native_audio_bus_from_sound_id(
        0x00010000u);
    const auto ambience = galaxy::audio::native_audio_bus_from_sound_id(
        0x00060000u);
    const auto unknown = galaxy::audio::native_audio_bus_from_sound_id(
        0x7f7f0000u);
    passed &= expect(
        music.bus == NativeAudioBus::Music &&
            voice.bus == NativeAudioBus::Voice &&
            ambience.bus == NativeAudioBus::Ambience &&
            unknown.bus == NativeAudioBus::Sfx &&
            unknown.classification ==
                NativeAudioBusClassification::DeterministicSfxFallback,
        "native audio owner IDs separate music, voice, ambience, and deterministic SFX fallback");
    passed &= expect(
        galaxy::audio::native_audio_bus_from_channel_format(1u, 16u).bus ==
                NativeAudioBus::Music &&
            galaxy::audio::native_audio_bus_from_channel_format(16u, 9u).bus ==
                NativeAudioBus::Sfx &&
            galaxy::audio::native_audio_bus_from_channel_format(1u, 8u).bus ==
                NativeAudioBus::Sfx,
        "native audio unresolved-owner fallback uses Galaxy's stable channel formats");

    galaxy::audio::NativeAudioBusControls controls;
    std::array<NativeAudioBus,
               galaxy::audio::NativeAudioBusControls::kChannelCount>
        buses{};
    buses.fill(NativeAudioBus::Sfx);
    buses[3] = NativeAudioBus::Music;
    buses[4] = NativeAudioBus::Voice;
    buses[5] = NativeAudioBus::Ambience;
    controls.publish({0.5f, 0.5f, 0.75f, 1.0f, 0.25f}, buses);
    passed &= expect(
        controls.snapshot_channel(3).gain_q16 ==
                galaxy::audio::kNativeAudioUnityGainQ16 / 4u &&
            controls.snapshot_channel(4).gain_q16 == 24'576u &&
            controls.snapshot_channel(2).gain_q16 ==
                galaxy::audio::kNativeAudioUnityGainQ16 / 2u &&
            controls.snapshot_channel(5).gain_q16 == 8'192u,
        "native audio controls publish coherent master-times-bus gains");

    galaxy::audio::NativeDspAudioBusState state{};
    galaxy::audio::native_dsp_audio_bus_begin_channel(state, 3u, &controls);
    passed &= expect(
        galaxy::audio::native_dsp_audio_bus_apply_sample(state, 16'000) ==
            4'000,
        "a channel starts at its current bus gain without a startup fade");

    controls.publish({0.0f, 1.0f, 1.0f, 1.0f, 1.0f}, buses);
    galaxy::audio::native_dsp_audio_bus_begin_channel(state, 3u, &controls);
    std::int16_t ramped = 0;
    for (std::uint32_t sample = 0u;
         sample < galaxy::audio::kNativeAudioBusRampSamples;
         ++sample) {
        ramped = galaxy::audio::native_dsp_audio_bus_apply_sample(
            state, 16'000);
    }
    passed &= expect(
        ramped == 0 && state.ramps[3].remaining == 0u &&
            state.ramps[3].current_q16 == 0u,
        "live bus changes ramp to the exact target in one DSP subframe");
    passed &= expect(
        galaxy::audio::native_dsp_audio_bus_apply_sample(state, -32'768) == 0,
        "muted bus remains silent for negative full-scale samples");

    controls.publish({1.0f, 1.0f, 1.0f, 0.5f, 0.25f}, buses);
    galaxy::audio::native_dsp_audio_bus_begin_channel(state, 2u, &controls);
    passed &= expect(
        galaxy::audio::native_dsp_audio_bus_apply_sample(state, 16'000) ==
            8'000,
        "a causally selected unclassified channel takes the deterministic SFX gain");
    galaxy::audio::native_dsp_audio_bus_clear_selection(state);
    passed &= expect(
        galaxy::audio::native_dsp_audio_bus_apply_sample(state, -12'345) ==
            -12'345,
        "an unvalidated channel DMA clears stale routing and passes samples unchanged");

    return passed;
}

}  // namespace

int main() {
    bool passed = true;
    passed &= raw_instruction_provenance_captures_exact_prestate_and_identity();
    passed &= sbset_eie_post_retirement_interrupt_stacks_fallthrough_once();
    passed &= raw_instruction_memory_operands_are_decoder_planned_and_frozen();
    passed &= raw_instruction_provenance_lifecycle_is_owned_and_explicit();
    passed &= raw_instruction_provenance_invalid_shapes_and_failures_trap();
    passed &= logical_stack_depth_tracking_is_reset_bound_and_non_intrusive();
    passed &= timing_replay_maps_raw_provenance_exactly();
    passed &=
        timing_replay_rejects_identity_sequence_and_lifecycle_mismatches();
    passed &= timing_replay_rejects_memory_shape_and_authority_mismatches();
    passed &= timing_replay_rejects_unprovable_outcomes_and_async_events();
    passed &= timing_retirement_commit_is_explicit_and_prestate_is_immutable();
    passed &=
        timing_retirement_trap_and_abort_never_retire_current_instruction();
    passed &= timing_retirement_memory_and_outcome_fail_closed();
    passed &= timing_memory_regions_match_exact_runtime_access_semantics();
    passed &=
        timing_transaction_tokens_are_epoch_owned_and_cleanup_is_authenticated();
    passed &= timing_install_plan_and_loop_shapes_fail_closed();
    passed &= dsp_cycle_grants_are_integer_deterministic_and_fail_closed();
    passed &= test_mram_transaction_boundary();
    passed &= test_frame_pe_slice_services_pending_mram_before_ai();
    passed &= test_aram_mirror_boundary();
    passed &= test_ifx_ordering_hooks();
    passed &= test_clean_halt_ack_ordering();
    passed &= test_channel_selection_dma_probe();
    passed &= test_audio_causal_recorder();
    passed &= test_native_audio_bus_controls();

    std::array<std::byte, 64> guest_bytes{};
    galaxy::GuestMemoryV1 guest_memory{};
    guest_memory.fast_regions[0].host_base = guest_bytes.data();
    guest_memory.fast_regions[0].size = static_cast<std::uint32_t>(guest_bytes.size());
    guest_memory.fast_regions[8].host_base = guest_bytes.data();
    guest_memory.fast_regions[8].size = static_cast<std::uint32_t>(guest_bytes.size());

    Probe probe{};
    auto dsp_storage =
        std::make_unique<galaxy::NativeDspCoprocessor>();
    auto& dsp = *dsp_storage;
    dsp.connect_guest_memory(&guest_memory, nullptr, 0x80004000u);
    dsp.set_host_callbacks(
        &probe,
        record_interrupt,
        record_accelerator_exception);

    const std::array<std::uint16_t, 2> iram_words{0x1234u, 0x5678u};
    dsp.load_iram_words(0x0010, iram_words);
    passed &= expect(
        dsp.context().iram[0x0010] == 0x1234u &&
            dsp.context().iram[0x0011] == 0x5678u,
        "native DSP coprocessor loads IRAM words into owned context");

    const std::array<std::byte, 3> iram_bytes{
        std::byte{0xbe},
        std::byte{0xef},
        std::byte{0x7a},
    };
    dsp.load_iram_bytes(0x0024u, iram_bytes);
    passed &= expect(
        dsp.context().iram[0x0012] == 0xbeefu &&
            dsp.context().iram[0x0013] == 0x7a00u,
        "native DSP coprocessor loads big-endian IRAM byte payloads");

    const std::array<std::uint16_t, 2> irom_words{0x1357u, 0x2468u};
    dsp.load_irom_words(irom_words);
    passed &= expect(
        dsp.context().irom_loaded &&
            galaxy::dsp_iram_read(dsp.context(), 0x8000u) == 0x1357u &&
            galaxy::dsp_iram_read(dsp.context(), 0x8001u) == 0x2468u &&
            galaxy::dsp_iram_read(dsp.context(), 0x9001u) == 0u &&
            galaxy::dsp_iram_read(dsp.context(), 0x0012u) == 0xbeefu,
        "native DSP coprocessor decodes IROM only in its mapped instruction region");

    {
        auto instruction_bus_storage = std::make_unique<galaxy::DspContext>();
        auto& instruction_bus = *instruction_bus_storage;
        instruction_bus.iram[1] = 0x1357u;
        instruction_bus.irom[1] = 0x2468u;
        instruction_bus.iram[0xFFFu] = 0xABCDu;
        instruction_bus.irom[0xFFFu] = 0xFEDCu;
        instruction_bus.irom_loaded = true;
        for (std::uint16_t region = 0; region < 16u; ++region) {
            const auto address = static_cast<std::uint16_t>((region << 12u) | 1u);
            const auto expected = static_cast<std::uint16_t>(
                region == 0u ? 0x1357u : (region == 8u ? 0x2468u : 0u));
            passed &= expect(galaxy::dsp_iram_read(instruction_bus, address) == expected,
                             "instruction data bus decodes all sixteen top-nibble regions");
        }
        passed &= expect(galaxy::dsp_iram_read(instruction_bus, 0x0FFFu) == 0xABCDu &&
                             galaxy::dsp_iram_read(instruction_bus, 0x8FFFu) == 0xFEDCu,
                         "instruction bus retains both mapped region endpoints");
        instruction_bus.irom_loaded = false;
        passed &= expect(galaxy::dsp_iram_read(instruction_bus, 0x9001u) == 0u,
                         "unmapped instruction read does not require IROM initialization");
        bool trapped = false;
        try {
            static_cast<void>(galaxy::dsp_iram_read(instruction_bus, 0x8001u));
        } catch (const galaxy::DspHardTrap&) {
            trapped = true;
        }
        passed &= expect(trapped, "mapped IROM still fails before initialization");
    }

    const std::array<std::uint16_t, 1> dram_words{0xabcdu};
    dsp.load_dram_words(0x0004, dram_words);
    passed &= expect(
        dsp.context().dram[0x0004] == 0xabcdu,
        "native DSP coprocessor loads DRAM words into owned context");

    const std::array<std::byte, 3> dram_bytes{
        std::byte{0xca},
        std::byte{0xfe},
        std::byte{0x11},
    };
    dsp.load_dram_bytes(0x000au, dram_bytes);
    passed &= expect(
        dsp.context().dram[0x0005] == 0xcafeu &&
            dsp.context().dram[0x0006] == 0x1100u,
        "native DSP coprocessor loads big-endian DRAM byte payloads");

    const std::array<std::uint16_t, 1> coefficient_words{0x0f0fu};
    dsp.load_coefficient_words(coefficient_words);
    passed &= expect(
        dsp.context().coef_loaded &&
            galaxy::dsp_data_read(dsp.context(), 0x1000) == 0x0f0fu,
        "native DSP coprocessor loads coefficient words and enables reads");

    // The DSP data bus has no exception mechanism for undecoded address
    // ranges.  RMGE01's JAudio ucode reaches 0x7fff for the legitimate
    // non-positional-channel sentinel, so the native model must preserve the
    // silicon's open-bus behavior: read zero and drop writes.  Exercise more
    // than the observed address so this remains a decoded-region contract,
    // not a one-off exception for a captured trace value.
    dsp.context().dram[0x0007u] = 0x5aa5u;
    const std::array<std::uint16_t, 4> unmapped_data_addresses{
        0x2000u,
        0x7fffu,
        0xafffu,
        0xefffu,
    };
    bool unmapped_reads_zero = true;
    for (const auto address : unmapped_data_addresses) {
        unmapped_reads_zero =
            unmapped_reads_zero &&
            galaxy::dsp_data_read(dsp.context(), address) == 0u;
        galaxy::dsp_data_write(dsp.context(), address, 0xa55au);
    }
    passed &= expect(
        unmapped_reads_zero && dsp.context().dram[0x0007u] == 0x5aa5u &&
            galaxy::dsp_data_read(dsp.context(), 0x1000u) == 0x0f0fu,
        "native DSP unmapped data-bus ranges read zero and ignore writes "
        "without corrupting decoded DRAM or coefficient memory");

    // Native AOT evidence must come from generated instruction markers and
    // real IFX/DMA events, never the retired HLE mixer counters. Exercise each
    // source in isolation so every cumulative field has a precise contract.
    {
        std::array<std::byte, 32> telemetry_bytes{};
        telemetry_bytes[4] = std::byte{0x11};
        telemetry_bytes[5] = std::byte{0x00};
        telemetry_bytes[6] = std::byte{0x22};
        telemetry_bytes[7] = std::byte{0x33};
        galaxy::GuestMemoryV1 telemetry_memory{};
        telemetry_memory.fast_regions[0].host_base = telemetry_bytes.data();
        telemetry_memory.fast_regions[0].size =
            static_cast<std::uint32_t>(telemetry_bytes.size());
        telemetry_memory.fast_regions[8].host_base = telemetry_bytes.data();
        telemetry_memory.fast_regions[8].size =
            static_cast<std::uint32_t>(telemetry_bytes.size());

        Probe telemetry_probe{};
        galaxy::NativeDspCoprocessor telemetry_dsp;
        telemetry_dsp.connect_guest_memory(
            &telemetry_memory, nullptr, 0x80004000u);
        telemetry_dsp.set_host_callbacks(
            &telemetry_probe,
            record_interrupt,
            record_accelerator_exception);
        const auto before = telemetry_dsp.telemetry_snapshot();

        telemetry_dsp.run_native_entry(compiled_telemetry_probe);

        telemetry_dsp.cpu_write_to_dsp_mailbox_high(0x1234u);
        telemetry_dsp.cpu_write_to_dsp_mailbox_low(0x5678u);
        const auto cpu_mail_high =
            galaxy::dsp_data_read(telemetry_dsp.context(), 0xFFFEu);
        const auto cpu_mail_low =
            galaxy::dsp_data_read(telemetry_dsp.context(), 0xFFFFu);

        galaxy::dsp_data_write(telemetry_dsp.context(), 0xFFFCu, 0x2468u);
        galaxy::dsp_data_write(telemetry_dsp.context(), 0xFFFDu, 0xACE0u);
        const auto dsp_mail_high =
            telemetry_dsp.cpu_read_from_dsp_mailbox_high();
        const auto dsp_mail_low =
            telemetry_dsp.cpu_read_from_dsp_mailbox_low();

        galaxy::dsp_data_write(telemetry_dsp.context(), 0xFFFBu, 1u);

        // Four MRAM bytes -> DSP DRAM, three nonzero.
        galaxy::dsp_data_write(telemetry_dsp.context(), 0xFFCEu, 0x8000u);
        galaxy::dsp_data_write(telemetry_dsp.context(), 0xFFCFu, 0x0004u);
        galaxy::dsp_data_write(telemetry_dsp.context(), 0xFFCDu, 0x0002u);
        galaxy::dsp_data_write(telemetry_dsp.context(), 0xFFC9u, 0x0000u);
        galaxy::dsp_data_write(telemetry_dsp.context(), 0xFFCBu, 0x0004u);

        // Four DSP DRAM bytes -> MRAM, still three nonzero.
        galaxy::dsp_data_write(telemetry_dsp.context(), 0xFFCEu, 0x8000u);
        galaxy::dsp_data_write(telemetry_dsp.context(), 0xFFCFu, 0x0008u);
        galaxy::dsp_data_write(telemetry_dsp.context(), 0xFFCDu, 0x0002u);
        galaxy::dsp_data_write(telemetry_dsp.context(), 0xFFC9u, 0x0001u);
        galaxy::dsp_data_write(telemetry_dsp.context(), 0xFFCBu, 0x0004u);

        const auto after = telemetry_dsp.telemetry_snapshot();
        passed &= expect(
            before.retired_instructions == 0u &&
                before.cpu_mail_published == 0u &&
                before.cpu_mail_consumed == 0u &&
                before.dsp_mail_published == 0u &&
                before.dsp_mail_consumed == 0u &&
                before.dirq_writes == 0u &&
                before.dma_in_transfers == 0u &&
                before.dma_out_transfers == 0u,
            "native DSP telemetry starts at zero independently of HLE stats");
        passed &= expect(
            cpu_mail_high == 0x9234u && cpu_mail_low == 0x5678u &&
                dsp_mail_high == 0xA468u && dsp_mail_low == 0xACE0u &&
                telemetry_probe.interrupt_count == 1u,
            "native DSP telemetry probe traverses real mailbox and DIRQ paths");
        passed &= expect(
            after.retired_instructions == 3u &&
                after.cpu_mail_published == 1u &&
                after.cpu_mail_consumed == 1u &&
                after.dsp_mail_published == 1u &&
                after.dsp_mail_consumed == 1u &&
                after.dirq_writes == 1u &&
                after.dma_in_transfers == 1u &&
                after.dma_in_bytes == 4u &&
                after.dma_in_nonzero_bytes == 3u &&
                after.dma_out_transfers == 1u &&
                after.dma_out_bytes == 4u &&
                after.dma_out_nonzero_bytes == 3u,
            "native DSP telemetry counts generated instructions, mailbox "
            "handshakes, DIRQ, and successful DMA byte identities exactly");
        passed &= expect(
            after.cpu_mail_consumed <= after.cpu_mail_published &&
                after.dsp_mail_consumed <= after.dsp_mail_published &&
                telemetry_bytes[8] == std::byte{0x11} &&
                telemetry_bytes[9] == std::byte{0x00} &&
                telemetry_bytes[10] == std::byte{0x22} &&
                telemetry_bytes[11] == std::byte{0x33},
            "native DSP telemetry preserves mailbox conservation and reports "
            "the bytes actually transferred to MRAM");

        telemetry_dsp.reset();
        const auto after_reset = telemetry_dsp.telemetry_snapshot();
        passed &= expect(
            after_reset.retired_instructions == after.retired_instructions &&
                after_reset.cpu_mail_published == after.cpu_mail_published &&
                after_reset.cpu_mail_consumed == after.cpu_mail_consumed &&
                after_reset.dsp_mail_published == after.dsp_mail_published &&
                after_reset.dsp_mail_consumed == after.dsp_mail_consumed &&
                after_reset.dirq_writes == after.dirq_writes &&
                after_reset.dma_in_bytes == after.dma_in_bytes &&
                after_reset.dma_out_bytes == after.dma_out_bytes,
            "native DSP telemetry remains process-cumulative across hardware reset");
    }

    dsp.cpu_write_to_dsp_mailbox_high(0x1234u);
    dsp.cpu_write_to_dsp_mailbox_low(0x5678u);
    passed &= expect(
        galaxy::dsp_data_read(dsp.context(), 0xfffe) == 0x9234u &&
            galaxy::dsp_data_read(dsp.context(), 0xffff) == 0x5678u &&
            dsp.context().cpu_mailbox == 0x12345678u,
        "native DSP coprocessor exposes CPU-to-DSP mailbox writes to DSP IFX reads");

    dsp.cpu_write_mmio16(galaxy::kNativeDspMmioCpuMailboxHigh, 0x1357u);
    dsp.cpu_write_mmio16(galaxy::kNativeDspMmioCpuMailboxLow, 0x2468u);
    passed &= expect(
        dsp.cpu_read_mmio16(galaxy::kNativeDspMmioCpuMailboxHigh) == 0x9357u &&
            dsp.cpu_read_mmio16(galaxy::kNativeDspMmioCpuMailboxLow) == 0x2468u &&
            dsp.context().cpu_mailbox == (0x13572468u | galaxy::kDspMailboxBusy),
        "native DSP coprocessor CPU MMIO write/readback preserves to-DSP busy state");
    passed &= expect(
        galaxy::dsp_data_read(dsp.context(), 0xffff) == 0x2468u &&
            dsp.context().cpu_mailbox == 0x13572468u,
        "native DSP coprocessor DSP-side low read consumes the CPU-to-DSP mailbox");

    galaxy::dsp_data_write(dsp.context(), 0xfffc, 0x2468u);
    galaxy::dsp_data_write(dsp.context(), 0xfffd, 0xace0u);
    passed &= expect(
        dsp.cpu_read_from_dsp_mailbox_high() == 0xa468u &&
            dsp.cpu_read_from_dsp_mailbox_low() == 0xace0u &&
            dsp.context().dsp_mailbox == 0x2468ace0u,
        "native DSP coprocessor exposes DSP-to-CPU mailbox reads and consume semantics");

    galaxy::dsp_data_write(dsp.context(), 0xfffc, 0x1111u);
    galaxy::dsp_data_write(dsp.context(), 0xfffd, 0x2222u);
    passed &= expect(
        dsp.cpu_read_mmio16(galaxy::kNativeDspMmioDspMailboxHigh) == 0x9111u &&
            dsp.cpu_read_mmio16(galaxy::kNativeDspMmioDspMailboxLow) == 0x2222u &&
            dsp.context().dsp_mailbox == 0x11112222u,
        "native DSP coprocessor CPU MMIO reads consume the from-DSP low half");

    dsp.cpu_write_mmio16(galaxy::kNativeDspMmioControl, galaxy::kNativeDspControlHalt);
    passed &= expect(
        dsp.cpu_read_mmio16(galaxy::kNativeDspMmioControl) == galaxy::kNativeDspControlHalt &&
            dsp.context().halted,
        "native DSP coprocessor control MMIO halt bit updates context state");
    dsp.cpu_write_mmio16(galaxy::kNativeDspMmioControl, 0u);
    passed &= expect(
        !dsp.context().halted,
        "native DSP coprocessor clearing control halt bit resumes context state");
    dsp.cpu_write_mmio16(galaxy::kNativeDspMmioControl, 0x08adu);
    passed &= expect(
        dsp.cpu_read_mmio16(galaxy::kNativeDspMmioControl) == 0x08acu &&
            dsp.context().halted &&
            dsp.context().hardware.external_read_byte != nullptr,
        "native DSP coprocessor reset clears only the hardware-owned reset bit");
    dsp.cpu_write_mmio16(galaxy::kNativeDspMmioControl, 0u);

    guest_bytes[4] = std::byte{0xde};
    guest_bytes[5] = std::byte{0xad};
    galaxy::dsp_data_write(dsp.context(), 0xffce, 0x8000u);
    galaxy::dsp_data_write(dsp.context(), 0xffcf, 0x0004u);
    galaxy::dsp_data_write(dsp.context(), 0xffcd, 0x0002u);
    galaxy::dsp_data_write(dsp.context(), 0xffc9, 0x0000u);
    galaxy::dsp_data_write(dsp.context(), 0xffcb, 0x0002u);
    passed &= expect(
        dsp.context().dram[0x0002] == 0xdeadu,
        "native DSP coprocessor DMA reads aliased GuestMemoryV1 bytes into DRAM");

    dsp.context().dram[0x0003] = 0xbeefu;
    galaxy::dsp_data_write(dsp.context(), 0xffce, 0x8000u);
    galaxy::dsp_data_write(dsp.context(), 0xffcf, 0x0008u);
    galaxy::dsp_data_write(dsp.context(), 0xffcd, 0x0003u);
    galaxy::dsp_data_write(dsp.context(), 0xffc9, 0x0001u);
    galaxy::dsp_data_write(dsp.context(), 0xffcb, 0x0002u);
    passed &= expect(
        guest_bytes[8] == std::byte{0xbe} && guest_bytes[9] == std::byte{0xef},
        "native DSP coprocessor DMA writes DRAM bytes back to GuestMemoryV1");

    galaxy::dsp_data_write(dsp.context(), 0xfffb, 1u);
    passed &= expect(
        probe.interrupt_count == 1,
        "native DSP coprocessor forwards DIRQ through configured host callback");

    guest_bytes[16] = std::byte{0x33};
    guest_bytes[17] = std::byte{0x44};
    galaxy::dsp_data_write(dsp.context(), 0xffd1, 0x001au);
    galaxy::dsp_data_write(dsp.context(), 0xffde, 0x0001u);
    galaxy::dsp_data_write(dsp.context(), 0xffd4, 0x0000u);
    galaxy::dsp_data_write(dsp.context(), 0xffd5, 0x0010u);
    galaxy::dsp_data_write(dsp.context(), 0xffd6, 0x0000u);
    galaxy::dsp_data_write(dsp.context(), 0xffd7, 0x0008u);
    galaxy::dsp_data_write(dsp.context(), 0xffd8, 0x0000u);
    galaxy::dsp_data_write(dsp.context(), 0xffd9, 0x0008u);
    passed &= expect(
        galaxy::dsp_data_read(dsp.context(), 0xffdd) == 0x3344u,
        "native DSP coprocessor accelerator reads samples through GuestMemoryV1");
    passed &= expect(
        probe.accelerator_exception_count == 1 &&
            probe.last_accelerator_exception ==
                galaxy::DspAcceleratorException::SampleReadEnd,
        "native DSP coprocessor forwards accelerator exceptions");

    dsp.run_native_entry(compiled_dsp_probe);
    passed &= expect(
        dsp.context().dram[0x0007] == 0x55aau &&
            dsp.cpu_read_from_dsp_mailbox_high() == 0xdcd1u &&
            dsp.cpu_read_from_dsp_mailbox_low() == 0x0000u &&
            probe.interrupt_count == 2 &&
            dsp.context().halted,
        "native DSP coprocessor executes a compiled DSP entry through the IFX surface");

    galaxy::dsp_data_write(dsp.context(), 0xfffcu, 0xa468u);
    galaxy::dsp_data_write(dsp.context(), 0xfffdu, 0xace0u);
    const auto staged_high = dsp.cpu_read_from_dsp_mailbox_high();
    const auto staged_low = dsp.cpu_peek_from_dsp_mailbox_low();
    const bool still_busy_after_peek = dsp.dsp_mail_present();
    const auto consumed_low = dsp.cpu_read_from_dsp_mailbox_low();
    passed &= expect(
        staged_high == 0xa468u && staged_low == 0xace0u &&
            still_busy_after_peek && consumed_low == 0xace0u &&
            !dsp.dsp_mail_present(),
        "native DSP from-mail low peek stages without consuming the hardware mailbox");

    // The generated RMGE01 dispatcher owns the branch on this empty CMBH
    // result. It is not one of the four back-edge polling reads, so the host
    // event optimization must neither sleep nor consume wake credit here.
    {
        galaxy::DspHostWakeState nonblocking_wake{};
        galaxy::DspContext nonblocking_context{};
        std::atomic<bool> nonblocking_abort{};
        std::atomic<bool> nonblocking_returned{};
        std::atomic<bool> nonblocking_aborted{};
        std::uint16_t empty_high = 0xFFFFu;
        nonblocking_context.host_wake = &nonblocking_wake;
        nonblocking_context.host_abort = &nonblocking_abort;
        nonblocking_context.pc = 0x06F5u;

        std::thread nonblocking_read([&] {
            try {
                empty_high =
                    galaxy::dsp_data_read(nonblocking_context, 0xFFFEu);
                nonblocking_returned.store(true, std::memory_order_release);
            } catch (const galaxy::DspExecutionAborted&) {
                nonblocking_aborted.store(true, std::memory_order_release);
            }
        });
        const bool returned_without_parking = spin_until(
            [&] {
                return nonblocking_returned.load(std::memory_order_acquire);
            },
            std::chrono::seconds(2));
        if (!returned_without_parking) {
            // Bound a regression that incorrectly parks this read so the test
            // reports a failure instead of hanging the test process forever.
            nonblocking_abort.store(true, std::memory_order_release);
            nonblocking_wake.generation.fetch_add(
                1u, std::memory_order_acq_rel);
            nonblocking_wake.generation.notify_all();
        }
        nonblocking_read.join();

        const bool exact_sites_classified =
            galaxy::dsp_classify_rmge01_mailbox_poll_pc(
                galaxy::kRmge01DspCmbhCommandPollPc) ==
                galaxy::DspRmge01MailboxPollKind::CmbhEmptyBackEdge &&
            galaxy::dsp_classify_rmge01_mailbox_poll_pc(
                galaxy::kRmge01DspCmbhAc0PollPc) ==
                galaxy::DspRmge01MailboxPollKind::CmbhEmptyBackEdge &&
            galaxy::dsp_classify_rmge01_mailbox_poll_pc(
                galaxy::kRmge01DspCmbhAc1PollPc) ==
                galaxy::DspRmge01MailboxPollKind::CmbhEmptyBackEdge &&
            galaxy::dsp_classify_rmge01_mailbox_poll_pc(
                galaxy::kRmge01DspCmbhTaskPollPc) ==
                galaxy::DspRmge01MailboxPollKind::CmbhEmptyBackEdge &&
            galaxy::dsp_classify_rmge01_mailbox_poll_pc(
                galaxy::kRmge01DspDmbhAc0PollPc) ==
                galaxy::DspRmge01MailboxPollKind::DmbhBusyBackEdge &&
            galaxy::dsp_classify_rmge01_mailbox_poll_pc(
                galaxy::kRmge01DspDmbhTaskPollPc) ==
                galaxy::DspRmge01MailboxPollKind::DmbhBusyBackEdge;
        const bool neighboring_sites_fail_closed =
            galaxy::dsp_classify_rmge01_mailbox_poll_pc(0x06F5u) ==
                galaxy::DspRmge01MailboxPollKind::None &&
            galaxy::dsp_classify_rmge01_mailbox_poll_pc(0x06FFu) ==
                galaxy::DspRmge01MailboxPollKind::None &&
            galaxy::dsp_classify_rmge01_mailbox_poll_pc(0x078Eu) ==
                galaxy::DspRmge01MailboxPollKind::None &&
            galaxy::dsp_classify_rmge01_mailbox_poll_pc(0x0794u) ==
                galaxy::DspRmge01MailboxPollKind::None &&
            galaxy::dsp_classify_rmge01_mailbox_poll_pc(0x07ACu) ==
                galaxy::DspRmge01MailboxPollKind::None &&
            galaxy::dsp_classify_rmge01_mailbox_poll_pc(0x07B2u) ==
                galaxy::DspRmge01MailboxPollKind::None &&
            galaxy::dsp_classify_rmge01_mailbox_poll_pc(0xFFFFu) ==
                galaxy::DspRmge01MailboxPollKind::None;

        galaxy::DspPendingIdleBackEdgeCounters pending_idle_backedges{};
        galaxy::DspNativeTelemetryState idle_backedge_telemetry{};
        galaxy::DspContext idle_backedge_context{};
        idle_backedge_context.pending_idle_backedges =
            &pending_idle_backedges;
        idle_backedge_context.host_telemetry = &idle_backedge_telemetry;
        galaxy::dsp_native_handle_static_backedge(
            idle_backedge_context,
            galaxy::kRmge01DspZeroQueueIdleSourcePc,
            galaxy::kRmge01DspZeroQueueIdleTargetPc);
        galaxy::dsp_native_handle_static_backedge(
            idle_backedge_context,
            galaxy::kRmge01DspCommandWaitIdleSourcePc,
            galaxy::kRmge01DspCommandWaitIdleTargetPc);
        galaxy::dsp_native_handle_static_backedge(
            idle_backedge_context,
            galaxy::kRmge01DspZeroQueueShortReentrySourcePc,
            galaxy::kRmge01DspZeroQueueShortReentryTargetPc);
        // Source-only and target-only matches must fail closed.
        galaxy::dsp_native_handle_static_backedge(
            idle_backedge_context,
            galaxy::kRmge01DspZeroQueueIdleSourcePc,
            galaxy::kRmge01DspCommandWaitIdleTargetPc);
        galaxy::dsp_native_handle_static_backedge(
            idle_backedge_context,
            galaxy::kRmge01DspCommandWaitIdleSourcePc,
            galaxy::kRmge01DspZeroQueueIdleTargetPc);
        galaxy::dsp_native_telemetry_publish_pending_worker_counts(
            idle_backedge_context);
        const auto idle_backedge_snapshot =
            galaxy::dsp_native_telemetry_snapshot(idle_backedge_telemetry);
        const bool idle_backedges_classify_and_publish_exactly =
            idle_backedge_telemetry.worker_publication_sequence.load(
                std::memory_order_acquire) == 2u &&
            idle_backedge_snapshot.zero_queue_idle_backedges == 1u &&
            idle_backedge_snapshot.command_wait_idle_backedges == 1u &&
            idle_backedge_snapshot.zero_queue_short_reentries == 1u &&
            pending_idle_backedges.zero_queue == 0u &&
            pending_idle_backedges.command_wait == 0u &&
            pending_idle_backedges.zero_queue_short_reentries == 0u &&
            galaxy::dsp_classify_rmge01_idle_backedge(
                galaxy::kRmge01DspZeroQueueIdleSourcePc,
                galaxy::kRmge01DspZeroQueueIdleTargetPc) ==
                galaxy::DspRmge01IdleBackEdgeKind::ZeroQueue &&
            galaxy::dsp_classify_rmge01_idle_backedge(
                galaxy::kRmge01DspCommandWaitIdleSourcePc,
                galaxy::kRmge01DspCommandWaitIdleTargetPc) ==
                galaxy::DspRmge01IdleBackEdgeKind::CommandWait &&
            galaxy::dsp_classify_rmge01_idle_backedge(0x081Fu, 0x002Bu) ==
                galaxy::DspRmge01IdleBackEdgeKind::None &&
            galaxy::dsp_classify_rmge01_idle_backedge(0x029Au, 0x0292u) ==
                galaxy::DspRmge01IdleBackEdgeKind::None;

        passed &= expect(
            returned_without_parking &&
                !nonblocking_aborted.load(std::memory_order_acquire) &&
                empty_high == 0u &&
                nonblocking_wake.idle_wait_count.load(
                    std::memory_order_acquire) == 0u &&
                exact_sites_classified && neighboring_sites_fail_closed &&
                idle_backedges_classify_and_publish_exactly &&
                !galaxy::dsp_is_rmge01_dmbh_poll_pc(
                    galaxy::kRmge01DspCmbhAc0PollPc) &&
                !galaxy::dsp_is_rmge01_cmbh_poll_pc(
                    galaxy::kRmge01DspDmbhAc0PollPc),
            "native DSP idle evidence classifies exact source/target pairs, "
            "mailbox event waits classify exactly four CMBH and two DMBH "
            "back-edge PCs, and all classifiers fail closed around PC 0x06F5");

        // The register-indirect generated reads only reach IFX when CR is
        // 0x00FF. A classified PC with another CR must remain an ordinary DRAM
        // access, proving that PC classification cannot override data-space
        // routing.
        auto routed_context = std::make_unique<galaxy::DspContext>();
        routed_context->host_wake = &nonblocking_wake;
        routed_context->host_abort = &nonblocking_abort;
        routed_context->cr = 0x0004u;
        routed_context->pc = galaxy::kRmge01DspCmbhAc0PollPc;
        routed_context->dram[0x04FEu] = 0x1357u;
        const auto routed_cmbh = galaxy::dsp_data_read(
            *routed_context,
            static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(routed_context->cr << 8u) |
                0x00FEu));
        routed_context->pc = galaxy::kRmge01DspDmbhAc0PollPc;
        routed_context->dram[0x04FCu] = 0x2468u;
        const auto routed_dmbh = galaxy::dsp_data_read(
            *routed_context,
            static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(routed_context->cr << 8u) |
                0x00FCu));
        passed &= expect(
            routed_cmbh == 0x1357u && routed_dmbh == 0x2468u &&
                nonblocking_wake.idle_wait_count.load(
                    std::memory_order_acquire) == 0u,
            "native DSP mailbox poll classification applies only after an "
            "actual register-indirect IFX route");

        // Fail-closed behavior must also hold at the real DMBH IFX address.
        // PC 0x07AC is immediately adjacent to the proven 0x07AD poll but is a
        // return instruction, so a busy mailbox read there must return now.
        routed_context->cr = 0x00FFu;
        routed_context->pc = 0x07ACu;
        galaxy::dsp_data_write(*routed_context, 0xFFFCu, 0x3456u);
        galaxy::dsp_data_write(*routed_context, 0xFFFDu, 0x789Au);
        std::atomic<bool> dmbh_returned{};
        std::atomic<bool> dmbh_aborted{};
        std::uint16_t unclassified_dmbh = 0u;
        std::thread unclassified_dmbh_read([&] {
            try {
                unclassified_dmbh =
                    galaxy::dsp_data_read(*routed_context, 0xFFFCu);
                dmbh_returned.store(true, std::memory_order_release);
            } catch (const galaxy::DspExecutionAborted&) {
                dmbh_aborted.store(true, std::memory_order_release);
            }
        });
        const bool dmbh_returned_without_parking = spin_until(
            [&] { return dmbh_returned.load(std::memory_order_acquire); },
            std::chrono::seconds(2));
        if (!dmbh_returned_without_parking) {
            nonblocking_abort.store(true, std::memory_order_release);
            nonblocking_wake.generation.fetch_add(
                1u, std::memory_order_acq_rel);
            nonblocking_wake.generation.notify_all();
        }
        unclassified_dmbh_read.join();
        passed &= expect(
            dmbh_returned_without_parking &&
                !dmbh_aborted.load(std::memory_order_acquire) &&
                unclassified_dmbh == 0xB456u &&
                nonblocking_wake.idle_wait_count.load(
                    std::memory_order_acquire) == 0u,
            "native DSP unclassified DMBH IFX reads fail closed to immediate "
            "register semantics");
    }

    // Raw back-edge counts prove only that a branch was taken. Exact sequence
    // evidence additionally requires every audited RMGE01 SCC marker, in
    // order, with no dispatcher/re-entry discontinuity or intervening marker.
    passed &= [] {
        bool passed = true;
        struct ExactSequenceCase {
            galaxy::DspRmge01IdleBackEdgeKind kind;
            std::span<const std::uint16_t> sequence;
        };
        const std::array exact_cases{
            ExactSequenceCase{
                galaxy::DspRmge01IdleBackEdgeKind::ZeroQueue,
                galaxy::kRmge01DspZeroQueueIdleSequence},
            ExactSequenceCase{
                galaxy::DspRmge01IdleBackEdgeKind::CommandWait,
                galaxy::kRmge01DspCommandWaitIdleSequence},
        };

        bool exact_sequences_count_once = true;
        bool every_interior_suffix_is_rejected = true;
        for (const auto& test_case : exact_cases) {
            {
                IdleSequenceHarness harness;
                configure_idle_fixed_point(harness.context, test_case.kind);
                harness.feed(test_case.sequence);
                harness.invoke(test_case.kind);
                const auto snapshot = harness.publish();
                const bool zero_case =
                    test_case.kind ==
                    galaxy::DspRmge01IdleBackEdgeKind::ZeroQueue;
                exact_sequences_count_once &=
                    snapshot.zero_queue_complete_sequences ==
                        (zero_case ? 1u : 0u) &&
                    snapshot.command_wait_complete_sequences ==
                        (zero_case ? 0u : 1u) &&
                    harness.tracker.kind ==
                        galaxy::DspRmge01IdleBackEdgeKind::None &&
                    harness.tracker.progress == 0u;
            }
            for (std::size_t start = 1u;
                 start < test_case.sequence.size();
                 ++start) {
                IdleSequenceHarness harness;
                configure_idle_fixed_point(harness.context, test_case.kind);
                harness.feed(test_case.sequence.subspan(start));
                harness.invoke(test_case.kind);
                const auto snapshot = harness.publish();
                every_interior_suffix_is_rejected &=
                    snapshot.zero_queue_complete_sequences == 0u &&
                    snapshot.command_wait_complete_sequences == 0u;
            }
        }
        passed &= expect(
            exact_sequences_count_once && every_interior_suffix_is_rejected,
            "native DSP exact idle provenance counts both complete SCCs once and rejects every interior suffix start");

        {
            IdleSequenceHarness harness;
            configure_idle_fixed_point(
                harness.context,
                galaxy::DspRmge01IdleBackEdgeKind::ZeroQueue);
            harness.feed(
                std::span<const std::uint16_t>{
                    galaxy::kRmge01DspZeroQueueIdleSequence}
                    .first(3u));
            harness.context.last_retired_pc = 0x0555u;
            harness.feed(
                std::span<const std::uint16_t>{
                    galaxy::kRmge01DspZeroQueueIdleSequence}
                    .subspan(3u));
            harness.invoke(galaxy::DspRmge01IdleBackEdgeKind::ZeroQueue);
            const auto snapshot = harness.publish();
            passed &= expect(
                snapshot.zero_queue_idle_backedges == 1u &&
                    snapshot.zero_queue_complete_sequences == 0u,
                "native DSP idle provenance rejects a prefix followed by an unrelated marker and suffix");
        }

        {
            IdleSequenceHarness harness;
            configure_idle_fixed_point(
                harness.context,
                galaxy::DspRmge01IdleBackEdgeKind::ZeroQueue);
            harness.context.last_retired_pc = 0x078Eu;
            harness.feed(
                std::span<const std::uint16_t>{
                    galaxy::kRmge01DspZeroQueueIdleSequence}
                    .subspan(1u));
            harness.invoke(galaxy::DspRmge01IdleBackEdgeKind::ZeroQueue);
            const auto computed_jump_snapshot = harness.publish();

            configure_idle_fixed_point(
                harness.context,
                galaxy::DspRmge01IdleBackEdgeKind::ZeroQueue);
            harness.feed(
                std::span<const std::uint16_t>{
                    galaxy::kRmge01DspZeroQueueIdleSequence}
                    .first(3u));
            galaxy::dsp_native_reset_idle_sequence_progress(harness.context);
            harness.feed(
                std::span<const std::uint16_t>{
                    galaxy::kRmge01DspZeroQueueIdleSequence}
                    .subspan(3u));
            harness.invoke(galaxy::DspRmge01IdleBackEdgeKind::ZeroQueue);
            const auto dispatched_snapshot = harness.publish();
            passed &= expect(
                computed_jump_snapshot.zero_queue_complete_sequences == 0u &&
                    dispatched_snapshot.zero_queue_complete_sequences == 0u &&
                    dispatched_snapshot.zero_queue_idle_backedges == 2u,
                "native DSP idle provenance rejects computed interior entry and a dispatcher discontinuity");
        }

        {
            IdleSequenceHarness harness;
            configure_idle_fixed_point(
                harness.context,
                galaxy::DspRmge01IdleBackEdgeKind::ZeroQueue);
            harness.context.last_retired_pc =
                galaxy::kRmge01DspZeroQueueShortReentrySourcePc;
            harness.context.pc =
                galaxy::kRmge01DspZeroQueueShortReentryTargetPc;
            galaxy::dsp_native_handle_static_backedge(
                harness.context,
                galaxy::kRmge01DspZeroQueueShortReentrySourcePc,
                galaxy::kRmge01DspZeroQueueShortReentryTargetPc);
            harness.feed(
                std::span<const std::uint16_t>{
                    galaxy::kRmge01DspZeroQueueIdleSequence}
                    .subspan(3u));
            harness.context.pc =
                galaxy::kRmge01DspZeroQueueIdleTargetPc;
            harness.invoke(galaxy::DspRmge01IdleBackEdgeKind::ZeroQueue);
            const auto snapshot = harness.publish();
            passed &= expect(
                snapshot.zero_queue_short_reentries == 1u &&
                    snapshot.zero_queue_idle_backedges == 1u &&
                    snapshot.zero_queue_complete_sequences == 0u,
                "native DSP 0824-to-07FA short re-entry is raw evidence only and cannot fabricate a complete SCC");
        }

        {
            IdleSequenceHarness harness;
            configure_idle_fixed_point(
                harness.context,
                galaxy::DspRmge01IdleBackEdgeKind::CommandWait);
            harness.feed(galaxy::kRmge01DspCommandWaitIdleSequence);
            const bool full_without_edge_is_pending_only =
                harness.tracker.kind ==
                    galaxy::DspRmge01IdleBackEdgeKind::CommandWait &&
                galaxy::dsp_rmge01_idle_sequence_ready(
                    harness.tracker,
                    galaxy::DspRmge01IdleBackEdgeKind::CommandWait) &&
                harness.pending.command_wait_complete_sequences == 0u;
            harness.feed(galaxy::kRmge01DspCommandWaitIdleSequence);
            harness.invoke(galaxy::DspRmge01IdleBackEdgeKind::CommandWait);
            harness.invoke(galaxy::DspRmge01IdleBackEdgeKind::CommandWait);
            const auto snapshot = harness.publish();
            passed &= expect(
                full_without_edge_is_pending_only &&
                    snapshot.command_wait_idle_backedges == 2u &&
                    snapshot.command_wait_complete_sequences == 1u,
                "native DSP complete markers require a taken edge and duplicate hooks cannot re-credit one traversal");
        }

        {
            IdleSequenceHarness harness;
            configure_idle_fixed_point(
                harness.context,
                galaxy::DspRmge01IdleBackEdgeKind::ZeroQueue);
            harness.feed(galaxy::kRmge01DspZeroQueueIdleSequence);
            harness.context.dram[0x0352u] = 1u;
            harness.invoke(galaxy::DspRmge01IdleBackEdgeKind::ZeroQueue);
            const auto snapshot = harness.publish();
            passed &= expect(
                snapshot.zero_queue_idle_backedges == 1u &&
                    snapshot.zero_queue_complete_sequences == 0u &&
                    harness.tracker.kind ==
                        galaxy::DspRmge01IdleBackEdgeKind::None,
                "native DSP fixed-point mismatch retains raw edge evidence but consumes and rejects exact provenance");
        }

        {
            IdleSequenceHarness harness;
            configure_idle_fixed_point(
                harness.context,
                galaxy::DspRmge01IdleBackEdgeKind::CommandWait);
            harness.feed(galaxy::kRmge01DspCommandWaitIdleSequence);
            harness.pending.command_wait_complete_sequences =
                std::numeric_limits<std::uint64_t>::max() - 1u;
            harness.invoke(galaxy::DspRmge01IdleBackEdgeKind::CommandWait);
            passed &= expect(
                harness.pending.command_wait_complete_sequences ==
                        std::numeric_limits<std::uint64_t>::max() &&
                    harness.tracker.kind ==
                        galaxy::DspRmge01IdleBackEdgeKind::None,
                "native DSP exact provenance permits the final representable completion before its hard-fail overflow boundary");
        }

        {
            IdleSequenceHarness harness;
            configure_idle_fixed_point(
                harness.context,
                galaxy::DspRmge01IdleBackEdgeKind::ZeroQueue);
            harness.feed(
                std::span<const std::uint16_t>{
                    galaxy::kRmge01DspZeroQueueIdleSequence}
                    .first(3u));
            harness.pending.zero_queue = 2u;
            harness.pending.zero_queue_complete_sequences = 1u;
            const auto first_snapshot = harness.publish();
            const bool publication_preserved_progress =
                harness.tracker.kind ==
                    galaxy::DspRmge01IdleBackEdgeKind::ZeroQueue &&
                harness.tracker.progress == 3u &&
                harness.pending.zero_queue == 0u &&
                harness.pending.zero_queue_complete_sequences == 0u &&
                first_snapshot.zero_queue_idle_backedges == 2u &&
                first_snapshot.zero_queue_complete_sequences == 1u;
            harness.feed(
                std::span<const std::uint16_t>{
                    galaxy::kRmge01DspZeroQueueIdleSequence}
                    .subspan(3u));
            harness.invoke(galaxy::DspRmge01IdleBackEdgeKind::ZeroQueue);
            const auto second_snapshot = harness.publish();
            passed &= expect(
                publication_preserved_progress &&
                    second_snapshot.zero_queue_idle_backedges == 3u &&
                    second_snapshot.zero_queue_complete_sequences == 2u,
                "native DSP telemetry publication clears numeric pending counts without breaking in-flight provenance");
        }

        {
            IdleSequenceHarness harness;
            harness.feed(
                std::span<const std::uint16_t>{
                    galaxy::kRmge01DspZeroQueueIdleSequence}
                    .first(2u));
            harness.pending.zero_queue = 4u;
            harness.pending.zero_queue_complete_sequences = 3u;
            galaxy::dsp_native_reset_idle_sequence_progress(harness.context);
            passed &= expect(
                harness.tracker.kind ==
                        galaxy::DspRmge01IdleBackEdgeKind::None &&
                    harness.tracker.progress == 0u &&
                    harness.pending.zero_queue == 4u &&
                    harness.pending.zero_queue_complete_sequences == 3u,
                "native DSP progress-only reset leaves numeric pending telemetry intact");
        }

        {
            auto context_storage =
                std::make_unique<galaxy::DspContext>();
            auto& context = *context_storage;
            galaxy::DspPendingIdleBackEdgeCounters pending{};
            galaxy::DspNativeTelemetryState telemetry{};
            context.pending_idle_backedges = &pending;
            context.host_telemetry = &telemetry;
            configure_idle_fixed_point(
                context,
                galaxy::DspRmge01IdleBackEdgeKind::ZeroQueue);
            feed_idle_sequence_markers(
                context,
                galaxy::kRmge01DspZeroQueueIdleSequence);
            galaxy::dsp_native_handle_static_backedge(
                context,
                galaxy::kRmge01DspZeroQueueIdleSourcePc,
                galaxy::kRmge01DspZeroQueueIdleTargetPc);
            galaxy::dsp_native_telemetry_publish_pending_worker_counts(context);
            const auto snapshot =
                galaxy::dsp_native_telemetry_snapshot(telemetry);
            passed &= expect(
                context.last_retired_pc.idle_sequence_tracker == nullptr &&
                    snapshot.zero_queue_idle_backedges == 1u &&
                    snapshot.zero_queue_complete_sequences == 0u,
                "native DSP marker and edge helpers remain neutral when no provenance tracker is attached");
        }

        {
            auto reset_dsp =
                std::make_unique<galaxy::NativeDspCoprocessor>();
            auto& before = reset_dsp->context();
            auto* const attached_tracker =
                before.last_retired_pc.idle_sequence_tracker;
            feed_idle_sequence_markers(
                before,
                std::span<const std::uint16_t>{
                    galaxy::kRmge01DspZeroQueueIdleSequence}
                    .first(2u));
            before.pending_idle_backedges->zero_queue = 1u;
            before.pending_idle_backedges->zero_queue_complete_sequences = 1u;
            reset_dsp->reset();
            auto& after = reset_dsp->context();
            const bool full_reset_cleared_and_reattached =
                attached_tracker != nullptr &&
                after.last_retired_pc.idle_sequence_tracker ==
                    attached_tracker &&
                attached_tracker->kind ==
                    galaxy::DspRmge01IdleBackEdgeKind::None &&
                attached_tracker->progress == 0u &&
                after.last_retired_pc.pending_entries != nullptr &&
                after.pending_idle_backedges != nullptr &&
                after.pending_idle_backedges->zero_queue == 0u &&
                after.pending_idle_backedges
                        ->zero_queue_complete_sequences == 0u;

            feed_idle_sequence_markers(
                after,
                std::span<const std::uint16_t>{
                    galaxy::kRmge01DspZeroQueueIdleSequence}
                    .first(2u));
            reset_dsp->run_native_entry(idle_sequence_invocation_reset_probe);
            const bool single_shot_reset = after.dram[0x003Fu] == 1u;
            feed_idle_sequence_markers(
                after,
                std::span<const std::uint16_t>{
                    galaxy::kRmge01DspCommandWaitIdleSequence}
                    .first(2u));
            const bool interrupted =
                reset_dsp->run_free_running(
                    idle_sequence_invocation_reset_probe);
            passed &= expect(
                full_reset_cleared_and_reattached && single_shot_reset &&
                    !interrupted && after.dram[0x003Fu] == 1u,
                "native DSP full reset clears and reattaches provenance, and every host entry invocation starts with clean progress");
        }

        {
            IdleSequenceHarness harness;
            galaxy::DspHostWakeState wake{};
            std::atomic<bool> abort{};
            std::atomic<std::uint16_t> interrupt{0x000Eu};
            harness.context.host_wake = &wake;
            harness.context.host_abort = &abort;
            harness.context.host_external_interrupt = &interrupt;
            configure_idle_fixed_point(
                harness.context,
                galaxy::DspRmge01IdleBackEdgeKind::ZeroQueue);
            harness.feed(galaxy::kRmge01DspZeroQueueIdleSequence);
            bool interrupted_after_credit = false;
            try {
                harness.invoke(
                    galaxy::DspRmge01IdleBackEdgeKind::ZeroQueue);
            } catch (const galaxy::DspExternalInterruptRequested&) {
                interrupted_after_credit = true;
            }
            const bool first_credit_preceded_reentry =
                interrupted_after_credit &&
                harness.pending.zero_queue_complete_sequences == 1u &&
                harness.tracker.kind ==
                    galaxy::DspRmge01IdleBackEdgeKind::None;

            galaxy::dsp_native_reset_idle_sequence_progress(harness.context);
            harness.context.host_wake = nullptr;
            configure_idle_fixed_point(
                harness.context,
                galaxy::DspRmge01IdleBackEdgeKind::ZeroQueue);
            harness.feed(galaxy::kRmge01DspZeroQueueIdleSequence);
            harness.invoke(galaxy::DspRmge01IdleBackEdgeKind::ZeroQueue);
            const auto snapshot = harness.publish();
            passed &= expect(
                first_credit_preceded_reentry &&
                    snapshot.zero_queue_complete_sequences == 2u,
                "native DSP idle completion is credited before PIINT re-entry and a later full RTI traversal is the only second credit");
        }
        return passed;
    }();

    // The two non-mailbox RMGE01 idle SCCs may park only after their exact
    // taken edge reaches a statically certified fixed point. Exercise the
    // complete generation/recheck contract: spurious wake/re-park, PIINT,
    // pre-published PIINT, shutdown abort, and fail-open guard mismatch.
    {
        auto attach_idle_host = [](
                                    galaxy::DspContext& context,
                                    galaxy::DspHostWakeState& wake,
                                    galaxy::DspNativeTelemetryState& telemetry,
                                    galaxy::DspPendingIdleBackEdgeCounters&
                                        pending_backedges,
                                    galaxy::DspRmge01IdleSequenceTracker&
                                        sequence_tracker,
                                    std::uint64_t& pending_entries,
                                    std::atomic<bool>& abort,
                                    std::atomic<std::uint16_t>& interrupt) {
            context.host_wake = &wake;
            context.host_telemetry = &telemetry;
            context.pending_idle_backedges = &pending_backedges;
            context.last_retired_pc.pending_entries = &pending_entries;
            context.last_retired_pc.idle_sequence_tracker =
                &sequence_tracker;
            context.host_abort = &abort;
            context.host_external_interrupt = &interrupt;
        };
        auto configure_zero_queue_fixed_point = [](
                                                    galaxy::DspContext&
                                                        context) {
            context.sr = galaxy::kDspSrExtIntEnable;
            context.dram[0x0351u] = 0x1234u;
            context.dram[0x0352u] = 0u;
            context.ar[0] = context.dram[0x0351u];
            context.wr[0] = 0xFFFFu;
            context.ax[0][1] = 0u;
            context.sr = galaxy::dsp_status_16(
                0, false, false, false, context.sr);
            context.last_retired_pc =
                galaxy::kRmge01DspZeroQueueIdleSourcePc;
            context.pc = galaxy::kRmge01DspZeroQueueIdleTargetPc;
        };
        auto configure_command_wait_fixed_point = [](
                                                     galaxy::DspContext&
                                                         context) {
            context.sr = galaxy::kDspSrExtIntEnable;
            context.dram[0x034Eu] = 1u;
            context.dram[0x0354u] = 2u;
            const auto clear_ac0 =
                galaxy::dsp_accumulator_set_value(0, context.sr);
            context.ac[0] = clear_ac0.value;
            context.sr = clear_ac0.status;
            const auto clear_ac1 =
                galaxy::dsp_accumulator_set_value(0, context.sr);
            context.ac[1] = clear_ac1.value;
            context.sr = clear_ac1.status;
            context.ac[1] = galaxy::dsp_write_accumulator_mid(
                context.ac[1], context.dram[0x0354u], context.sr);
            context.ac[0] = galaxy::dsp_write_accumulator_mid(
                context.ac[0], context.dram[0x034Eu], context.sr);
            context.sr = galaxy::dsp_accumulator_subtract(
                             context.ac[0], context.ac[1], context.sr)
                             .status;
            context.last_retired_pc =
                galaxy::kRmge01DspCommandWaitIdleSourcePc;
            context.pc = galaxy::kRmge01DspCommandWaitIdleTargetPc;
        };
        auto notify_idle = [](galaxy::DspHostWakeState& wake) {
            wake.generation.fetch_add(1u, std::memory_order_acq_rel);
            wake.generation.notify_one();
        };

        auto zero_context = std::make_unique<galaxy::DspContext>();
        galaxy::DspHostWakeState zero_wake{};
        galaxy::DspNativeTelemetryState zero_telemetry{};
        galaxy::DspPendingIdleBackEdgeCounters zero_pending_backedges{};
        galaxy::DspRmge01IdleSequenceTracker zero_sequence_tracker{};
        std::uint64_t zero_pending_entries = 0u;
        std::atomic<bool> zero_abort{};
        std::atomic<std::uint16_t> zero_interrupt{
            galaxy::kDspNoPendingExternalInterrupt};
        attach_idle_host(
            *zero_context,
            zero_wake,
            zero_telemetry,
            zero_pending_backedges,
            zero_sequence_tracker,
            zero_pending_entries,
            zero_abort,
            zero_interrupt);
        configure_zero_queue_fixed_point(*zero_context);
        feed_idle_sequence_markers(
            *zero_context,
            galaxy::kRmge01DspZeroQueueIdleSequence);
        std::atomic<bool> zero_interrupted{};
        std::atomic<bool> zero_returned{};
        std::thread zero_waiter([&] {
            try {
                galaxy::dsp_native_handle_static_backedge(
                    *zero_context,
                    galaxy::kRmge01DspZeroQueueIdleSourcePc,
                    galaxy::kRmge01DspZeroQueueIdleTargetPc);
                zero_returned.store(true, std::memory_order_release);
            } catch (const galaxy::DspExternalInterruptRequested&) {
                zero_interrupted.store(true, std::memory_order_release);
            } catch (const galaxy::DspExecutionAborted&) {
            }
        });
        const bool zero_parked = spin_until(
            [&] {
                return zero_wake.idle_wait_count.load(
                           std::memory_order_acquire) >= 1u;
            },
            std::chrono::seconds(2));
        notify_idle(zero_wake);
        const bool zero_spurious_reparked = spin_until(
            [&] {
                return zero_wake.idle_wait_count.load(
                           std::memory_order_acquire) >= 2u;
            },
            std::chrono::seconds(2));
        zero_interrupt.store(0x000Eu, std::memory_order_release);
        notify_idle(zero_wake);
        const bool zero_piint_accepted = spin_until(
            [&] {
                return zero_interrupted.load(std::memory_order_acquire) ||
                       zero_returned.load(std::memory_order_acquire);
            },
            std::chrono::seconds(2));
        if (!zero_piint_accepted) {
            zero_abort.store(true, std::memory_order_release);
            notify_idle(zero_wake);
        }
        zero_waiter.join();
        const auto zero_snapshot =
            galaxy::dsp_native_telemetry_snapshot(zero_telemetry);
        passed &= expect(
            zero_parked && zero_spurious_reparked && zero_piint_accepted &&
                zero_interrupted.load(std::memory_order_acquire) &&
                !zero_returned.load(std::memory_order_acquire) &&
                zero_context->pc == 0x000Eu &&
                zero_context->st[0] ==
                    galaxy::kRmge01DspZeroQueueIdleTargetPc &&
                (zero_context->st[1] & galaxy::kDspSrExtIntEnable) != 0u &&
                zero_snapshot.zero_queue_idle_backedges == 1u &&
                zero_snapshot.zero_queue_complete_sequences == 1u &&
                zero_snapshot.zero_queue_idle_waits >= 2u &&
                zero_snapshot.command_wait_idle_backedges == 0u &&
                zero_snapshot.command_wait_complete_sequences == 0u,
            "native DSP zero-queue idle certificate parks without retiring, "
            "re-parks after a spurious wake, and accepts PIINT at target 0x002B");

        auto prewake_context = std::make_unique<galaxy::DspContext>();
        galaxy::DspHostWakeState prewake_wake{};
        galaxy::DspNativeTelemetryState prewake_telemetry{};
        galaxy::DspPendingIdleBackEdgeCounters prewake_pending_backedges{};
        galaxy::DspRmge01IdleSequenceTracker prewake_sequence_tracker{};
        std::uint64_t prewake_pending_entries = 0u;
        std::atomic<bool> prewake_abort{};
        std::atomic<std::uint16_t> prewake_interrupt{0x000Eu};
        attach_idle_host(
            *prewake_context,
            prewake_wake,
            prewake_telemetry,
            prewake_pending_backedges,
            prewake_sequence_tracker,
            prewake_pending_entries,
            prewake_abort,
            prewake_interrupt);
        configure_zero_queue_fixed_point(*prewake_context);
        feed_idle_sequence_markers(
            *prewake_context,
            galaxy::kRmge01DspZeroQueueIdleSequence);
        notify_idle(prewake_wake);
        bool prewake_interrupted = false;
        try {
            galaxy::dsp_native_handle_static_backedge(
                *prewake_context,
                galaxy::kRmge01DspZeroQueueIdleSourcePc,
                galaxy::kRmge01DspZeroQueueIdleTargetPc);
        } catch (const galaxy::DspExternalInterruptRequested&) {
            prewake_interrupted = true;
        }
        galaxy::dsp_native_telemetry_publish_pending_worker_counts(
            *prewake_context);
        const auto prewake_snapshot =
            galaxy::dsp_native_telemetry_snapshot(prewake_telemetry);
        passed &= expect(
            prewake_interrupted && prewake_context->pc == 0x000Eu &&
                prewake_wake.idle_wait_count.load(
                    std::memory_order_acquire) == 0u &&
                prewake_snapshot.zero_queue_idle_backedges == 1u &&
                prewake_snapshot.zero_queue_complete_sequences == 1u,
            "native DSP idle certificate consumes a pre-published PIINT "
            "without entering a lost-wake sleep");

        auto command_context = std::make_unique<galaxy::DspContext>();
        galaxy::DspHostWakeState command_wake{};
        galaxy::DspNativeTelemetryState command_telemetry{};
        galaxy::DspPendingIdleBackEdgeCounters command_pending_backedges{};
        galaxy::DspRmge01IdleSequenceTracker command_sequence_tracker{};
        std::uint64_t command_pending_entries = 0u;
        std::atomic<bool> command_abort{};
        std::atomic<std::uint16_t> command_interrupt{
            galaxy::kDspNoPendingExternalInterrupt};
        attach_idle_host(
            *command_context,
            command_wake,
            command_telemetry,
            command_pending_backedges,
            command_sequence_tracker,
            command_pending_entries,
            command_abort,
            command_interrupt);
        configure_command_wait_fixed_point(*command_context);
        feed_idle_sequence_markers(
            *command_context,
            galaxy::kRmge01DspCommandWaitIdleSequence);
        std::atomic<bool> command_aborted{};
        std::atomic<bool> command_returned{};
        std::thread command_waiter([&] {
            try {
                galaxy::dsp_native_handle_static_backedge(
                    *command_context,
                    galaxy::kRmge01DspCommandWaitIdleSourcePc,
                    galaxy::kRmge01DspCommandWaitIdleTargetPc);
                command_returned.store(true, std::memory_order_release);
            } catch (const galaxy::DspExecutionAborted&) {
                command_aborted.store(true, std::memory_order_release);
            } catch (const galaxy::DspExternalInterruptRequested&) {
            }
        });
        const bool command_parked = spin_until(
            [&] {
                return command_wake.idle_wait_count.load(
                           std::memory_order_acquire) >= 1u;
            },
            std::chrono::seconds(2));
        command_abort.store(true, std::memory_order_release);
        notify_idle(command_wake);
        const bool command_abort_observed = spin_until(
            [&] {
                return command_aborted.load(std::memory_order_acquire) ||
                       command_returned.load(std::memory_order_acquire);
            },
            std::chrono::seconds(2));
        command_waiter.join();
        const auto command_snapshot =
            galaxy::dsp_native_telemetry_snapshot(command_telemetry);
        passed &= expect(
            command_parked && command_abort_observed &&
                command_aborted.load(std::memory_order_acquire) &&
                !command_returned.load(std::memory_order_acquire) &&
                command_snapshot.command_wait_idle_backedges == 1u &&
                command_snapshot.command_wait_complete_sequences == 1u &&
                command_snapshot.command_wait_idle_waits >= 1u &&
                command_snapshot.zero_queue_idle_backedges == 0u &&
                command_snapshot.zero_queue_complete_sequences == 0u,
            "native DSP command-progress idle certificate parks at target "
            "0x0293 and cooperative shutdown cannot strand the worker");

        auto mismatch_context = std::make_unique<galaxy::DspContext>();
        galaxy::DspHostWakeState mismatch_wake{};
        galaxy::DspNativeTelemetryState mismatch_telemetry{};
        galaxy::DspPendingIdleBackEdgeCounters mismatch_pending_backedges{};
        galaxy::DspRmge01IdleSequenceTracker mismatch_sequence_tracker{};
        std::uint64_t mismatch_pending_entries = 0u;
        std::atomic<bool> mismatch_abort{};
        std::atomic<std::uint16_t> mismatch_interrupt{
            galaxy::kDspNoPendingExternalInterrupt};
        attach_idle_host(
            *mismatch_context,
            mismatch_wake,
            mismatch_telemetry,
            mismatch_pending_backedges,
            mismatch_sequence_tracker,
            mismatch_pending_entries,
            mismatch_abort,
            mismatch_interrupt);
        configure_zero_queue_fixed_point(*mismatch_context);
        feed_idle_sequence_markers(
            *mismatch_context,
            galaxy::kRmge01DspZeroQueueIdleSequence);
        mismatch_context->dram[0x0352u] = 1u;
        galaxy::dsp_native_handle_static_backedge(
            *mismatch_context,
            galaxy::kRmge01DspZeroQueueIdleSourcePc,
            galaxy::kRmge01DspZeroQueueIdleTargetPc);
        galaxy::dsp_native_telemetry_publish_pending_worker_counts(
            *mismatch_context);
        const auto mismatch_snapshot =
            galaxy::dsp_native_telemetry_snapshot(mismatch_telemetry);
        passed &= expect(
            mismatch_wake.idle_wait_count.load(std::memory_order_acquire) ==
                    0u &&
                mismatch_snapshot.zero_queue_idle_backedges == 1u &&
                mismatch_snapshot.zero_queue_complete_sequences == 0u &&
                mismatch_snapshot.zero_queue_idle_waits == 0u,
            "native DSP idle certificate mismatch fails open to the ordinary "
            "generated traversal instead of sleeping or fabricating state");
    }

    galaxy::NativeDspWorker worker(dsp, compiled_worker_probe);
    worker.start();
    worker.signal_work();
    worker.signal_work();
    passed &= expect(
        worker.wait_for_completed_runs(2, std::chrono::seconds(2)),
        "native DSP worker completes signaled compiled-entry runs");
    worker.stop();
    passed &= expect(
        worker.completed_runs() == 2 && dsp.context().dram[0x0008] == 2u,
        "native DSP worker executes precompiled DSP code without interpretation");

    // Free-running session: a never-returning ucode driven entirely through the
    // mailbox across two threads, then stopped cooperatively.
    {
        Probe free_probe{};
        galaxy::NativeDspCoprocessor free_dsp;
        free_dsp.set_host_callbacks(
            &free_probe, record_interrupt, record_accelerator_exception);
        // An empty compiled CMBH read must park immediately. There is no
        // host-speed warmup poll count and no timer-driven wake.
        galaxy::NativeDspWorker free_worker(
            free_dsp, free_running_echo_ucode, /*free_running=*/true);
        free_worker.start();

        const bool parked_on_idle_mailbox = spin_until(
            [&] { return free_dsp.host_idle_wait_count() >= 1u; },
            std::chrono::seconds(2));
        passed &= expect(
            parked_on_idle_mailbox,
            "free-running native DSP parks immediately on an empty compiled "
            "CPU-mailbox read");

        const auto first_cmbh_wait_count =
            free_dsp.host_idle_wait_count();
        const auto telemetry_at_cmbh_park =
            free_dsp.telemetry_snapshot();
        free_dsp.notify_mailbox_poll();
        const bool spurious_cmbh_wake_reparked = spin_until(
            [&] {
                return free_dsp.host_idle_wait_count() >=
                       first_cmbh_wait_count + 1u;
            },
            std::chrono::seconds(2));
        const auto telemetry_after_cmbh_repark =
            free_dsp.telemetry_snapshot();
        const auto wait_count_after_spurious =
            free_dsp.host_idle_wait_count();
        passed &= expect(
            spurious_cmbh_wake_reparked &&
                telemetry_at_cmbh_park.retired_instructions == 1u &&
                telemetry_after_cmbh_repark.retired_instructions ==
                    telemetry_at_cmbh_park.retired_instructions,
            "event-parked CMBH wait does not busy-retire the compiled polling "
            "instruction across a condition-preserving wake");

        passed &= expect(
            !free_worker.signal_resume_if_waiting(0x0010u),
            "free-running native DSP does not fabricate a task resume while "
            "ucode is running without a pending CPU mailbox publication");

        bool round_trips_ok = true;
        for (std::uint16_t round = 1; round <= 3u; ++round) {
            const auto sent_high = static_cast<std::uint16_t>(0x0100u + round);
            const auto sent_low = static_cast<std::uint16_t>(0xAB00u + round);
            free_dsp.cpu_write_to_dsp_mailbox_high(sent_high);
            free_dsp.cpu_write_to_dsp_mailbox_low(sent_low);
            const bool arrived = spin_until(
                [&] {
                    return (free_dsp.cpu_read_from_dsp_mailbox_high() &
                            0x8000u) != 0u &&
                           free_probe.interrupt_count.load(
                               std::memory_order_acquire) >= round;
                },
                std::chrono::seconds(2));
            const auto echo_high = static_cast<std::uint16_t>(
                free_dsp.cpu_read_from_dsp_mailbox_high() & 0x7FFFu);
            const auto echo_low = free_dsp.cpu_read_from_dsp_mailbox_low();
            const auto interrupt_count =
                free_probe.interrupt_count.load(std::memory_order_acquire);
            const bool round_ok = arrived && echo_high == sent_high &&
                                  echo_low == sent_low &&
                                  interrupt_count == round;
            if (!round_ok) {
                std::cerr << "free-running round " << round
                          << ": arrived=" << (arrived ? 1 : 0)
                          << " sent=0x" << std::hex << sent_high << ':'
                          << sent_low << " echo=0x" << echo_high << ':'
                          << echo_low << std::dec
                          << " irq=" << interrupt_count
                          << " cpu-consumed="
                          << (free_dsp.cpu_mail_consumed() ? 1 : 0) << '\n';
            }
            round_trips_ok = round_trips_ok && round_ok;
        }
        passed &= expect(
            round_trips_ok,
            "free-running native DSP consumes three serialized burst mails "
            "without deadlock and raises one DIRQ for each exact value");

        const bool parked_before_abort = spin_until(
            [&] {
                return free_dsp.host_idle_wait_count() >=
                       wait_count_after_spurious + 1u;
            },
            std::chrono::seconds(2));
        free_worker.stop();
        const auto free_telemetry = free_dsp.telemetry_snapshot();
        passed &= expect(
            parked_before_abort && free_worker.completed_runs() == 1u &&
                free_dsp.abort_requested(),
            "free-running native DSP abort wakes an event-parked mailbox "
            "poll and stops cooperatively");
        passed &= expect(
            free_telemetry.cpu_mail_published == 3u &&
                free_telemetry.cpu_mail_consumed == 3u &&
                free_telemetry.dsp_mail_published == 3u &&
                free_telemetry.dsp_mail_consumed == 3u &&
                free_telemetry.dirq_writes == 3u &&
                free_telemetry.host_idle_waits ==
                    free_dsp.host_idle_wait_count() &&
                free_telemetry.host_idle_waits >= 4u,
            "native DSP telemetry remains exact across free-running CPU/DSP "
            "threads, classified idle waits, and cooperative shutdown");
    }

    // A cancellation can make the active generated path throw DspHardTrap
    // while the host is already stopping it. That unwind is expected, must
    // publish AbortReturn, and must not downgrade a later real DSP fault into
    // a benign shutdown.
    {
        g_shutdown_trap_entry_ready.store(false, std::memory_order_release);
        // A coprocessor contains the complete DSP address space. Keep these
        // probes off this already large integration test's stack.
        auto shutdown_dsp = std::make_unique<galaxy::NativeDspCoprocessor>();
        shutdown_dsp->set_diagnostic_capture_enabled(true);
        galaxy::NativeDspWorker shutdown_worker(
            *shutdown_dsp,
            shutdown_trap_after_abort_ucode,
            /*free_running=*/true);
        shutdown_worker.start();
        const bool shutdown_entry_ready = spin_until(
            [] {
                return g_shutdown_trap_entry_ready.load(
                    std::memory_order_acquire);
            },
            std::chrono::seconds(2));
        shutdown_worker.stop();
        const auto shutdown_snapshot =
            shutdown_dsp->latest_diagnostic_snapshot();
        passed &= expect(
            shutdown_entry_ready && shutdown_worker.completed_runs() == 1u &&
                shutdown_dsp->abort_requested() &&
                !shutdown_dsp->hard_trap_active() &&
                shutdown_snapshot.has_value() &&
                shutdown_snapshot->boundary ==
                    galaxy::DspDiagnosticBoundary::AbortReturn,
            "native DSP shutdown classifies a cancellation-driven hard-trap "
            "unwind as AbortReturn without recording a fault");

        auto fault_dsp = std::make_unique<galaxy::NativeDspCoprocessor>();
        fault_dsp->set_diagnostic_capture_enabled(true);
        galaxy::NativeDspWorker fault_worker(
            *fault_dsp,
            non_shutdown_hard_trap_ucode,
            /*free_running=*/true);
        fault_worker.start();
        const bool fault_completed = fault_worker.wait_for_completed_runs(
            1u, std::chrono::seconds(2));
        const auto fault_snapshot = fault_dsp->latest_diagnostic_snapshot();
        fault_worker.stop();
        passed &= expect(
            fault_completed && fault_dsp->hard_trap_active() &&
                fault_dsp->hard_trap_pc() == 0x05F0u &&
                fault_snapshot.has_value() &&
                fault_snapshot->boundary ==
                    galaxy::DspDiagnosticBoundary::HardTrapReturn,
            "native DSP preserves hard-fail reporting for a non-shutdown "
            "hard-trap");
    }

    // The two register-indirect CMBH back-edges omitted from the original
    // classifier must park without busy-retiring and wake on one exact CPU mail.
    {
        bool classified_cmbh_wakes_exactly = true;
        constexpr std::array<std::uint16_t, 2> kRegisterIndirectCmbhPollPcs{
            galaxy::kRmge01DspCmbhAc0PollPc,
            galaxy::kRmge01DspCmbhAc1PollPc,
        };
        std::uint16_t round = 0u;
        for (const auto poll_pc : kRegisterIndirectCmbhPollPcs) {
            ++round;
            g_classified_cmbh_poll_pc.store(
                poll_pc, std::memory_order_release);
            g_classified_cmbh_completed.store(0u, std::memory_order_release);
            auto classified_dsp =
                std::make_unique<galaxy::NativeDspCoprocessor>();
            galaxy::NativeDspWorker classified_worker(
                *classified_dsp,
                classified_cmbh_once_then_idle_ucode,
                /*free_running=*/true);
            classified_worker.start();

            const bool parked = spin_until(
                [&] { return classified_dsp->host_idle_wait_count() == 1u; },
                std::chrono::seconds(2));
            const auto telemetry_at_park =
                classified_dsp->telemetry_snapshot();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            const bool no_busy_retirement =
                classified_dsp->telemetry_snapshot().retired_instructions ==
                telemetry_at_park.retired_instructions;
            const auto sent_high =
                static_cast<std::uint16_t>(0x6200u + round);
            const auto sent_low =
                static_cast<std::uint16_t>(0xB500u + round);
            classified_dsp->cpu_write_to_dsp_mailbox_high(sent_high);
            classified_dsp->cpu_write_to_dsp_mailbox_low(sent_low);
            const bool consumed_and_reparked = spin_until(
                [&] {
                    return g_classified_cmbh_completed.load(
                               std::memory_order_acquire) == 1u &&
                           classified_dsp->host_idle_wait_count() == 2u;
                },
                std::chrono::seconds(2));
            const auto telemetry = classified_dsp->telemetry_snapshot();
            classified_worker.stop();

            classified_cmbh_wakes_exactly =
                classified_cmbh_wakes_exactly && parked &&
                no_busy_retirement && consumed_and_reparked &&
                classified_dsp->context().dram[0x0057u] == sent_high &&
                classified_dsp->context().dram[0x0058u] == sent_low &&
                telemetry.cpu_mail_published == 1u &&
                telemetry.cpu_mail_consumed == 1u &&
                classified_worker.completed_runs() == 1u;
        }
        passed &= expect(
            classified_cmbh_wakes_exactly,
            "native DSP PCs 0x078F and 0x0795 park without busy retirement "
            "and wake on one exact register-indirect CMBH mail");
    }

    // Publish a complete CPU mail before the compiled CMBH instruction is
    // allowed to begin. The wake generation has already advanced when the
    // read takes its snapshot, so correctness depends on the immediate
    // mailbox recheck rather than on retaining a one-shot notification.
    {
        bool all_cmbh_lost_wake_windows_closed = true;
        constexpr std::array<std::uint16_t, 4> kCmbhPollPcs{
            galaxy::kRmge01DspCmbhCommandPollPc,
            galaxy::kRmge01DspCmbhAc0PollPc,
            galaxy::kRmge01DspCmbhAc1PollPc,
            galaxy::kRmge01DspCmbhTaskPollPc,
        };
        std::uint16_t round = 0u;
        for (const auto poll_pc : kCmbhPollPcs) {
            ++round;
            g_classified_cmbh_poll_pc.store(
                poll_pc, std::memory_order_release);
            g_cmbh_publish_before_wait_stage.store(
                0u, std::memory_order_release);
            g_cmbh_publish_before_wait_completed.store(
                0u, std::memory_order_release);
            galaxy::NativeDspCoprocessor before_wait_dsp;
            galaxy::NativeDspWorker before_wait_worker(
                before_wait_dsp,
                cmbh_publish_before_wait_ucode,
                /*free_running=*/true);
            before_wait_worker.start();

            const bool publish_window_reached = spin_until(
                [&] {
                    return g_cmbh_publish_before_wait_stage.load(
                               std::memory_order_acquire) == 1u;
                },
                std::chrono::seconds(2));
            const auto sent_high =
                static_cast<std::uint16_t>(0x6100u + round);
            const auto sent_low =
                static_cast<std::uint16_t>(0xB400u + round);
            before_wait_dsp.cpu_write_to_dsp_mailbox_high(sent_high);
            before_wait_dsp.cpu_write_to_dsp_mailbox_low(sent_low);
            g_cmbh_publish_before_wait_stage.store(
                2u, std::memory_order_release);
            g_cmbh_publish_before_wait_stage.notify_one();

            const bool published_mail_consumed = spin_until(
                [&] {
                    return g_cmbh_publish_before_wait_completed.load(
                               std::memory_order_acquire) == 1u &&
                           before_wait_dsp.host_idle_wait_count() == 1u;
                },
                std::chrono::seconds(2));
            const auto before_wait_telemetry =
                before_wait_dsp.telemetry_snapshot();
            before_wait_worker.stop();

            all_cmbh_lost_wake_windows_closed =
                all_cmbh_lost_wake_windows_closed &&
                publish_window_reached && published_mail_consumed &&
                before_wait_dsp.context().dram[0x0055u] == sent_high &&
                before_wait_dsp.context().dram[0x0056u] == sent_low &&
                before_wait_telemetry.cpu_mail_published == 1u &&
                before_wait_telemetry.cpu_mail_consumed == 1u &&
                before_wait_worker.completed_runs() == 1u;
        }
        g_cmbh_publish_before_wait_stage.store(0u, std::memory_order_release);
        g_classified_cmbh_poll_pc.store(
            galaxy::kRmge01DspCmbhTaskPollPc, std::memory_order_release);
        passed &= expect(
            all_cmbh_lost_wake_windows_closed,
            "native DSP CMBH generation/recheck closes publish-before-wait "
            "lost-wake windows at all four classified PCs");
    }

    // PC 0x07AD is the register-indirect DMBH busy back-edge omitted from the
    // original classifier. It must park until the CPU's consuming low read and
    // then continue exactly once.
    {
        g_classified_dmbh_completed.store(0u, std::memory_order_release);
        auto classified_dsp =
            std::make_unique<galaxy::NativeDspCoprocessor>();
        galaxy::NativeDspWorker classified_worker(
            *classified_dsp,
            classified_dmbh_once_then_idle_ucode,
            /*free_running=*/true);
        classified_worker.start();

        const bool parked = spin_until(
            [&] {
                return classified_dsp->dsp_mail_present() &&
                       classified_dsp->host_idle_wait_count() == 1u;
            },
            std::chrono::seconds(2));
        const auto telemetry_at_park = classified_dsp->telemetry_snapshot();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const bool no_busy_retirement =
            classified_dsp->telemetry_snapshot().retired_instructions ==
            telemetry_at_park.retired_instructions;
        const auto outgoing_high = static_cast<std::uint16_t>(
            classified_dsp->cpu_read_from_dsp_mailbox_high() & 0x7FFFu);
        const auto outgoing_low =
            classified_dsp->cpu_read_from_dsp_mailbox_low();
        const bool consumed_and_reparked = spin_until(
            [&] {
                return g_classified_dmbh_completed.load(
                           std::memory_order_acquire) == 1u &&
                       classified_dsp->host_idle_wait_count() == 2u;
            },
            std::chrono::seconds(2));
        const auto telemetry = classified_dsp->telemetry_snapshot();
        classified_worker.stop();

        passed &= expect(
            parked && no_busy_retirement && outgoing_high == 0x5456u &&
                outgoing_low == 0xC789u && consumed_and_reparked &&
                classified_dsp->context().dram[0x0059u] == 1u &&
                telemetry.dsp_mail_published == 1u &&
                telemetry.dsp_mail_consumed == 1u &&
                classified_worker.completed_runs() == 1u,
            "native DSP PC 0x07AD parks without busy retirement and wakes "
            "only when the CPU consumes its register-indirect DMBH mail");
    }

    // Consume the PC 0x07AD mail before its first DMBH read. The generation
    // may already have advanced, so the immediate condition recheck must pass
    // the cleared mailbox without sleeping or losing the wake.
    {
        g_classified_dmbh_completed.store(0u, std::memory_order_release);
        g_classified_dmbh_consume_before_wait_stage.store(
            0u, std::memory_order_release);
        auto before_wait_dsp =
            std::make_unique<galaxy::NativeDspCoprocessor>();
        galaxy::NativeDspWorker before_wait_worker(
            *before_wait_dsp,
            classified_dmbh_consume_before_wait_ucode,
            /*free_running=*/true);
        before_wait_worker.start();

        const bool consume_window_reached = spin_until(
            [&] {
                return g_classified_dmbh_consume_before_wait_stage.load(
                           std::memory_order_acquire) == 1u &&
                       before_wait_dsp->dsp_mail_present();
            },
            std::chrono::seconds(2));
        const auto outgoing_high = static_cast<std::uint16_t>(
            before_wait_dsp->cpu_read_from_dsp_mailbox_high() & 0x7FFFu);
        const auto outgoing_low =
            before_wait_dsp->cpu_read_from_dsp_mailbox_low();
        g_classified_dmbh_consume_before_wait_stage.store(
            2u, std::memory_order_release);
        g_classified_dmbh_consume_before_wait_stage.notify_one();
        const bool passed_consumed_mail = spin_until(
            [&] {
                return g_classified_dmbh_completed.load(
                           std::memory_order_acquire) == 1u &&
                       before_wait_dsp->host_idle_wait_count() == 1u;
            },
            std::chrono::seconds(2));
        const auto telemetry = before_wait_dsp->telemetry_snapshot();
        before_wait_worker.stop();
        g_classified_dmbh_consume_before_wait_stage.store(
            0u, std::memory_order_release);

        passed &= expect(
            consume_window_reached && outgoing_high == 0x5567u &&
                outgoing_low == 0xD89Au && passed_consumed_mail &&
                before_wait_dsp->context().dram[0x005Au] == 1u &&
                before_wait_dsp->host_idle_wait_count() == 1u &&
                telemetry.dsp_mail_published == 1u &&
                telemetry.dsp_mail_consumed == 1u &&
                before_wait_worker.completed_runs() == 1u,
            "native DSP PC 0x07AD generation/recheck closes its "
            "consume-before-wait lost-wake window");
    }

    // RMGE01's task dispatcher at DSP PC 0x07B3 waits for the CPU to consume
    // the previous DSP->CPU mail before it can publish another. Prove the
    // generated thread parks on that exact hardware condition, publishes its
    // pending AOT telemetry before sleeping, ignores peeks/spurious pokes, and
    // resumes only when the CPU low-half read really clears the busy bit.
    {
        g_dmbh_one_shot_completed.store(0u, std::memory_order_release);
        Probe backpressure_probe{};
        galaxy::NativeDspCoprocessor backpressure_dsp;
        backpressure_dsp.set_host_callbacks(
            &backpressure_probe,
            record_interrupt,
            record_accelerator_exception);
        galaxy::NativeDspWorker backpressure_worker(
            backpressure_dsp,
            dmbh_one_shot_then_idle_ucode,
            /*free_running=*/true);
        backpressure_worker.start();

        const bool dmbh_parked = spin_until(
            [&] {
                return backpressure_dsp.dsp_mail_present() &&
                       backpressure_dsp.host_idle_wait_count() >= 1u;
            },
            std::chrono::seconds(2));
        const auto first_wait_count =
            backpressure_dsp.host_idle_wait_count();
        const auto telemetry_at_park =
            backpressure_dsp.telemetry_snapshot();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const auto telemetry_after_idle =
            backpressure_dsp.telemetry_snapshot();
        const bool retired_stopped_while_parked =
            telemetry_at_park.retired_instructions ==
            telemetry_after_idle.retired_instructions;

        const auto peeked_low =
            backpressure_dsp.cpu_peek_from_dsp_mailbox_low();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const bool peek_did_not_wake =
            backpressure_dsp.host_idle_wait_count() == first_wait_count &&
            backpressure_dsp.dsp_mail_present();

        // An explicit poke changes the wake generation but not the mailbox.
        // The helper must recheck, remain blocked, and publish no fabricated
        // generated-instruction progress while it re-parks.
        backpressure_dsp.notify_mailbox_poll();
        const bool spurious_wake_reparked = spin_until(
            [&] {
                return backpressure_dsp.host_idle_wait_count() >=
                       first_wait_count + 1u;
            },
            std::chrono::seconds(2));
        const auto telemetry_after_spurious =
            backpressure_dsp.telemetry_snapshot();
        const bool spurious_wake_preserved_condition =
            backpressure_dsp.dsp_mail_present() &&
            g_dmbh_one_shot_completed.load(std::memory_order_acquire) == 0u &&
            telemetry_after_spurious.retired_instructions ==
                telemetry_at_park.retired_instructions;

        const auto outgoing_high = static_cast<std::uint16_t>(
            backpressure_dsp.cpu_read_from_dsp_mailbox_high() & 0x7FFFu);
        const auto outgoing_low =
            backpressure_dsp.cpu_read_from_dsp_mailbox_low();
        const bool consume_woke_dmbh = spin_until(
            [&] {
                return g_dmbh_one_shot_completed.load(
                           std::memory_order_acquire) == 1u &&
                       backpressure_dsp.host_idle_wait_count() >=
                           first_wait_count + 2u;
            },
            std::chrono::seconds(2));

        // The ucode is now parked on its empty CPU mailbox. A second low read
        // has no busy bit to consume and therefore must not notify/re-park it.
        const auto post_consume_wait_count =
            backpressure_dsp.host_idle_wait_count();
        const auto redundant_low =
            backpressure_dsp.cpu_read_from_dsp_mailbox_low();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const bool redundant_read_did_not_wake =
            backpressure_dsp.host_idle_wait_count() ==
            post_consume_wait_count;

        backpressure_worker.stop();
        const auto backpressure_telemetry =
            backpressure_dsp.telemetry_snapshot();
        passed &= expect(
            dmbh_parked && retired_stopped_while_parked &&
                peeked_low == 0xA456u && peek_did_not_wake &&
                spurious_wake_reparked &&
                spurious_wake_preserved_condition &&
                outgoing_high == 0x5123u && outgoing_low == 0xA456u &&
                consume_woke_dmbh && redundant_low == 0xA456u &&
                redundant_read_did_not_wake &&
                backpressure_worker.completed_runs() == 1u &&
                backpressure_telemetry.dsp_mail_published == 1u &&
                backpressure_telemetry.dsp_mail_consumed == 1u,
            "native DSP DMBH back-pressure wait parks without synthetic "
            "retirement, rejects spurious wakes, and resumes on one real consume");
    }

    // Publish and consume the mail before the generated DMBH read is allowed
    // to execute. This is a deterministic notify-before-wait interleaving: the
    // later snapshot/recheck must observe the free mailbox and never sleep on
    // the already-advanced generation.
    {
        g_dmbh_consume_before_wait_stage.store(0u, std::memory_order_release);
        g_dmbh_consume_before_wait_completed.store(
            0u, std::memory_order_release);
        Probe before_wait_probe{};
        galaxy::NativeDspCoprocessor before_wait_dsp;
        before_wait_dsp.set_host_callbacks(
            &before_wait_probe,
            record_interrupt,
            record_accelerator_exception);
        galaxy::NativeDspWorker before_wait_worker(
            before_wait_dsp,
            dmbh_consume_before_wait_ucode,
            /*free_running=*/true);
        before_wait_worker.start();

        const bool consume_window_reached = spin_until(
            [&] {
                return g_dmbh_consume_before_wait_stage.load(
                           std::memory_order_acquire) == 1u &&
                       before_wait_dsp.dsp_mail_present();
            },
            std::chrono::seconds(2));
        const auto before_wait_high = static_cast<std::uint16_t>(
            before_wait_dsp.cpu_read_from_dsp_mailbox_high() & 0x7FFFu);
        const auto before_wait_low =
            before_wait_dsp.cpu_read_from_dsp_mailbox_low();
        g_dmbh_consume_before_wait_stage.store(2u, std::memory_order_release);
        const bool passed_already_consumed_mail = spin_until(
            [&] {
                return g_dmbh_consume_before_wait_completed.load(
                           std::memory_order_acquire) == 1u &&
                       before_wait_dsp.host_idle_wait_count() >= 1u;
            },
            std::chrono::seconds(2));
        const auto before_wait_idle_count =
            before_wait_dsp.host_idle_wait_count();
        before_wait_worker.stop();
        g_dmbh_consume_before_wait_stage.store(0u, std::memory_order_release);

        passed &= expect(
            consume_window_reached && before_wait_high == 0x5234u &&
                before_wait_low == 0xB567u && passed_already_consumed_mail &&
                before_wait_idle_count == 1u &&
                before_wait_worker.completed_runs() == 1u,
            "native DSP DMBH generation/recheck closes the consume-before-wait "
            "lost-wake window");
    }

    // Three serialized mails prove that parking adds no hidden mailbox queue
    // or stale wake credit: each exact value becomes visible once, is consumed
    // once, and only then can the generated ucode publish the next round.
    {
        g_dmbh_repeated_completed.store(0u, std::memory_order_release);
        Probe repeated_probe{};
        galaxy::NativeDspCoprocessor repeated_dsp;
        repeated_dsp.set_host_callbacks(
            &repeated_probe,
            record_interrupt,
            record_accelerator_exception);
        galaxy::NativeDspWorker repeated_worker(
            repeated_dsp,
            dmbh_three_round_ucode,
            /*free_running=*/true);
        repeated_worker.start();

        bool rounds_exact = true;
        for (std::uint16_t round = 1u; round <= 3u; ++round) {
            const bool round_parked = spin_until(
                [&] {
                    return repeated_dsp.dsp_mail_present() &&
                           repeated_dsp.host_idle_wait_count() >= round;
                },
                std::chrono::seconds(2));
            const auto round_high = static_cast<std::uint16_t>(
                repeated_dsp.cpu_read_from_dsp_mailbox_high() & 0x7FFFu);
            const auto round_low = repeated_dsp.cpu_read_from_dsp_mailbox_low();
            const bool round_completed = spin_until(
                [&] {
                    return g_dmbh_repeated_completed.load(
                               std::memory_order_acquire) >= round;
                },
                std::chrono::seconds(2));
            rounds_exact = rounds_exact && round_parked && round_completed &&
                           round_high ==
                               static_cast<std::uint16_t>(0x5100u + round) &&
                           round_low ==
                               static_cast<std::uint16_t>(0xA100u + round);
        }
        const bool final_idle_parked = spin_until(
            [&] { return repeated_dsp.host_idle_wait_count() >= 4u; },
            std::chrono::seconds(2));
        const auto repeated_telemetry = repeated_dsp.telemetry_snapshot();
        const bool no_hidden_mail = !repeated_dsp.dsp_mail_present();
        repeated_worker.stop();

        passed &= expect(
            rounds_exact && final_idle_parked && no_hidden_mail &&
                repeated_dsp.context().dram[0x0051u] == 3u &&
                repeated_telemetry.dsp_mail_published == 3u &&
                repeated_telemetry.dsp_mail_consumed == 3u &&
                repeated_worker.completed_runs() == 1u,
            "native DSP DMBH parking preserves three exact serialized mailbox "
            "cycles without hidden queue or wake credit");
    }

    // Cooperative shutdown must wake a worker parked on DMBH even if the CPU
    // never consumes the outstanding mail.
    {
        g_dmbh_one_shot_completed.store(0u, std::memory_order_release);
        Probe abort_probe{};
        galaxy::NativeDspCoprocessor abort_dsp;
        abort_dsp.set_host_callbacks(
            &abort_probe, record_interrupt, record_accelerator_exception);
        galaxy::NativeDspWorker abort_worker(
            abort_dsp,
            dmbh_one_shot_then_idle_ucode,
            /*free_running=*/true);
        abort_worker.start();
        const bool abort_wait_reached = spin_until(
            [&] {
                return abort_dsp.dsp_mail_present() &&
                       abort_dsp.host_idle_wait_count() >= 1u;
            },
            std::chrono::seconds(2));
        const auto abort_started = std::chrono::steady_clock::now();
        abort_worker.stop();
        const auto abort_elapsed =
            std::chrono::steady_clock::now() - abort_started;
        const bool unconsumed_mail_preserved = abort_dsp.dsp_mail_present();
        passed &= expect(
            abort_wait_reached && abort_elapsed < std::chrono::seconds(2) &&
                unconsumed_mail_preserved && abort_dsp.abort_requested() &&
                abort_dsp.context().dram[0x0050u] == 0u &&
                abort_worker.completed_runs() == 1u,
            "native DSP abort wakes a DMBH-parked generated entry without "
            "fabricating mailbox consumption");
        (void)abort_dsp.cpu_read_from_dsp_mailbox_low();
    }

    // PIINT is another architectural wake source. It must preempt the DMBH
    // wait, enter vector 0x000E natively, and leave the unconsumed mailbox busy.
    {
        g_dmbh_external_interrupt_completed.store(
            0u, std::memory_order_release);
        Probe dmbh_interrupt_probe{};
        galaxy::NativeDspCoprocessor dmbh_interrupt_dsp;
        dmbh_interrupt_dsp.set_host_callbacks(
            &dmbh_interrupt_probe,
            record_interrupt,
            record_accelerator_exception);
        galaxy::NativeDspWorker dmbh_interrupt_worker(
            dmbh_interrupt_dsp,
            dmbh_external_interrupt_ucode,
            /*free_running=*/true);
        dmbh_interrupt_worker.start();
        const bool interrupt_wait_reached = spin_until(
            [&] {
                return dmbh_interrupt_dsp.dsp_mail_present() &&
                       dmbh_interrupt_dsp.host_idle_wait_count() >= 1u;
            },
            std::chrono::seconds(2));
        dmbh_interrupt_worker.signal_external_interrupt(0x000Eu);
        const bool interrupt_runs_completed =
            dmbh_interrupt_worker.wait_for_completed_runs(
                2u, std::chrono::seconds(2));
        const auto interrupt_snapshot =
            dmbh_interrupt_worker.halted_snapshot();
        const bool interrupt_vector_completed =
            interrupt_runs_completed &&
            g_dmbh_external_interrupt_completed.load(
                std::memory_order_acquire) == 1u &&
            halted_snapshot_at(interrupt_snapshot, 0x000Eu, 2u);
        const bool interrupt_left_mail_unconsumed =
            dmbh_interrupt_dsp.dsp_mail_present();
        dmbh_interrupt_worker.stop();
        passed &= expect(
            interrupt_wait_reached && interrupt_vector_completed &&
                interrupt_left_mail_unconsumed &&
                dmbh_interrupt_dsp.context().dram[0x0053u] == 1u &&
                dmbh_interrupt_dsp.context().dram[0x0054u] == 0u &&
                dmbh_interrupt_worker.completed_runs() == 2u,
            "native DSP PIINT wakes DMBH back-pressure and enters the compiled "
            "external-interrupt vector without consuming the mail");
        (void)dmbh_interrupt_dsp.cpu_read_from_dsp_mailbox_low();
    }

    // Free-running session with lowered HALT boundaries: the compiled entry
    // posts a boot mail, records a HALT PC, and returns. A CPU-to-DSP mail by
    // itself must not clear HALT; the scheduler resumes halted code explicitly.
    {
        Probe halt_probe{};
        galaxy::NativeDspCoprocessor halt_dsp;
        halt_dsp.set_host_callbacks(
            &halt_probe, record_interrupt, record_accelerator_exception);
        galaxy::NativeDspWorker halt_worker(
            halt_dsp, resumable_halt_echo_ucode, /*free_running=*/true);
        halt_worker.start();

        const bool boot_mail_arrived = spin_until(
            [&] {
                const auto snapshot = halt_worker.halted_snapshot();
                return halted_snapshot_at(snapshot, 1u, 1u) &&
                       halt_dsp.dsp_mail_present() &&
                       halt_probe.interrupt_count == 1u;
            },
            std::chrono::seconds(2));
        const auto boot_high = halt_dsp.cpu_read_from_dsp_mailbox_high();
        const auto boot_low = halt_dsp.cpu_read_from_dsp_mailbox_low();
        const auto boot_snapshot = halt_worker.halted_snapshot();
        const bool halt_boot_ok =
            boot_mail_arrived && boot_high == 0xDCD1u && boot_low == 0x0000u &&
            halted_snapshot_at(boot_snapshot, 1u, 1u) &&
            halt_probe.interrupt_count == 1u;
        if (!halt_boot_ok) {
            std::cerr << "halt boot state: arrived=" << boot_mail_arrived
                      << " high=0x" << std::hex << boot_high
                      << " low=0x" << boot_low
                      << " snapshot=" << (boot_snapshot.has_value() ? 1 : 0)
                      << " pc=0x"
                      << (boot_snapshot.has_value() ? boot_snapshot->pc : 0u)
                      << std::dec
                      << " halted="
                      << (boot_snapshot.has_value() && boot_snapshot->halted
                              ? 1
                              : 0)
                      << " runs=" << halt_worker.completed_runs()
                      << " irq=" << halt_probe.interrupt_count << '\n';
        }
        passed &= expect(
            halt_boot_ok,
            "free-running native DSP worker reaches a lowered HALT and "
            "retains the HALT PC");

        halt_dsp.cpu_write_to_dsp_mailbox_high(0x2222u);
        halt_dsp.cpu_write_to_dsp_mailbox_low(0x3333u);
        halt_worker.signal_work();
        const bool mail_only_resumed =
            halt_worker.wait_for_completed_runs(2u, std::chrono::milliseconds(25));
        const auto mail_only_snapshot = halt_worker.halted_snapshot();
        passed &= expect(
            !mail_only_resumed && halt_worker.completed_runs() == 1u &&
                halted_snapshot_at(mail_only_snapshot, 1u, 1u) &&
                !halt_dsp.cpu_mail_consumed(),
            "free-running native DSP does not leave a lowered HALT on "
            "CPU-to-DSP mail alone");

        const bool resume_signaled =
            halt_worker.signal_resume_if_waiting(0x0010u);
        const bool resumed_mail_arrived = spin_until(
            [&] {
                return halt_dsp.dsp_mail_present() &&
                       halt_worker.completed_runs() >= 2u;
            },
            std::chrono::seconds(2));
        const auto resumed_high = static_cast<std::uint16_t>(
            halt_dsp.cpu_read_from_dsp_mailbox_high() & 0x7FFFu);
        const auto resumed_low = halt_dsp.cpu_read_from_dsp_mailbox_low();
        const auto resumed_snapshot = halt_worker.halted_snapshot();
        passed &= expect(
            resume_signaled && resumed_mail_arrived && resumed_high == 0x2222u &&
                resumed_low == 0x3333u &&
                halted_snapshot_at(resumed_snapshot, 1u, 2u) &&
                halt_probe.interrupt_count == 2u,
            "free-running native DSP worker resumes lowered HALT code only "
            "after an explicit resume and then halts again");

        halt_worker.stop();
        passed &= expect(
            halt_worker.completed_runs() == 2u,
            "stopping a HALTed native DSP worker executes no extra entry");
    }

    // Resume-before-wait adversarial proof. This forces the exact old loss
    // window rather than relying on scheduler luck: ctx.halted is set, the
    // generated entry is paused before return, and the CPU publishes one mail
    // plus duplicate resume attempts while WorkerState is still Executing.
    {
        g_resume_before_wait_stage.store(0u, std::memory_order_release);
        Probe race_probe{};
        galaxy::NativeDspCoprocessor race_dsp;
        race_dsp.set_host_callbacks(
            &race_probe, record_interrupt, record_accelerator_exception);
        galaxy::NativeDspWorker race_worker(
            race_dsp, resume_before_wait_ucode, /*free_running=*/true);
        race_worker.start();

        const bool halt_return_gap_reached = spin_until(
            [] {
                return g_resume_before_wait_stage.load(
                           std::memory_order_acquire) == 1u;
            },
            std::chrono::seconds(2));
        race_dsp.cpu_write_to_dsp_mailbox_high(0x1234u);
        race_dsp.cpu_write_to_dsp_mailbox_low(0x5678u);
        const auto mail_generation = race_dsp.cpu_mail_generation();
        const bool first_resume_retained =
            race_worker.signal_resume_if_waiting(0x0010u);
        const bool duplicate_resume_coalesced =
            race_worker.signal_resume_if_waiting(0x0010u);
        race_worker.signal_work();
        g_resume_before_wait_stage.store(2u, std::memory_order_release);

        const bool resumed_once =
            race_worker.wait_for_completed_runs(2u, std::chrono::seconds(2));
        const bool no_duplicate_run = !race_worker.wait_for_completed_runs(
            3u, std::chrono::milliseconds(25));
        race_worker.stop();
        passed &= expect(
            halt_return_gap_reached && mail_generation == 1u &&
                first_resume_retained && duplicate_resume_coalesced &&
                resumed_once && no_duplicate_run &&
                race_worker.completed_runs() == 2u &&
                race_dsp.context().dram[0x0040u] == 1u &&
                race_dsp.context().dram[0x0041u] == 0x1234u &&
                race_dsp.context().dram[0x0042u] == 0x5678u &&
                race_dsp.cpu_mail_consumed(),
            "native DSP losslessly latches resume-before-wait and coalesces "
            "duplicates by CPU-mail generation");
    }

    // A speculative resume is not permission to execute a future task. If the
    // currently-running ucode consumes that exact mail before HALT, the worker
    // must retire the latch and remain halted.
    {
        g_consumed_before_halt_stage.store(0u, std::memory_order_release);
        Probe consumed_probe{};
        galaxy::NativeDspCoprocessor consumed_dsp;
        consumed_dsp.set_host_callbacks(
            &consumed_probe, record_interrupt, record_accelerator_exception);
        galaxy::NativeDspWorker consumed_worker(
            consumed_dsp, consumed_before_halt_ucode, /*free_running=*/true);
        consumed_worker.start();

        const bool active_window_reached = spin_until(
            [] {
                return g_consumed_before_halt_stage.load(
                           std::memory_order_acquire) == 1u;
            },
            std::chrono::seconds(2));
        consumed_dsp.cpu_write_to_dsp_mailbox_high(0x2345u);
        consumed_dsp.cpu_write_to_dsp_mailbox_low(0x6789u);
        const bool speculative_resume_retained =
            consumed_worker.signal_resume_if_waiting(0x0010u);
        g_consumed_before_halt_stage.store(2u, std::memory_order_release);

        const bool first_halt_published = consumed_worker.wait_for_completed_runs(
            1u, std::chrono::seconds(2));
        const bool stale_resume_discarded = !consumed_worker.wait_for_completed_runs(
            2u, std::chrono::milliseconds(25));
        consumed_worker.stop();
        passed &= expect(
            active_window_reached && speculative_resume_retained &&
                first_halt_published && stale_resume_discarded &&
                consumed_worker.completed_runs() == 1u &&
                consumed_dsp.context().dram[0x0043u] == 0x2345u &&
                consumed_dsp.context().dram[0x0044u] == 0x6789u &&
                consumed_dsp.context().dram[0x0045u] == 0u &&
                consumed_dsp.cpu_mail_consumed(),
            "native DSP discards a pre-wait resume when active ucode consumed "
            "that mailbox publication before HALT");
    }

    // Shutdown in the same post-HALT/pre-return window must win without a
    // resume, an extra entry, or a join deadlock. The barrier is intentionally
    // never released; only the worker's cooperative abort can unwind it.
    {
        g_resume_before_wait_stage.store(0u, std::memory_order_release);
        Probe shutdown_probe{};
        galaxy::NativeDspCoprocessor shutdown_dsp;
        shutdown_dsp.set_host_callbacks(
            &shutdown_probe, record_interrupt, record_accelerator_exception);
        galaxy::NativeDspWorker shutdown_worker(
            shutdown_dsp, resume_before_wait_ucode, /*free_running=*/true);
        shutdown_worker.start();
        const bool shutdown_gap_reached = spin_until(
            [] {
                return g_resume_before_wait_stage.load(
                           std::memory_order_acquire) == 1u;
            },
            std::chrono::seconds(2));
        const auto stop_started = std::chrono::steady_clock::now();
        shutdown_worker.stop();
        const auto stop_elapsed =
            std::chrono::steady_clock::now() - stop_started;
        g_resume_before_wait_stage.store(0u, std::memory_order_release);
        passed &= expect(
            shutdown_gap_reached &&
                stop_elapsed < std::chrono::seconds(2) &&
                shutdown_worker.completed_runs() == 1u &&
                shutdown_dsp.context().dram[0x0040u] == 0u,
            "native DSP shutdown wins in the post-HALT publication gap and "
            "joins without dispatching a resume");
    }

    // Free-running session with a task resume vector: RMGE01's DSP task
    // descriptor uses init vector 0 and resume vector 0x10. The lowered HALT
    // records the HALT instruction PC, but DSPAssertInt resumes the task
    // through the descriptor vector, so the worker must be able to steer the
    // next compiled entry to that vector without interpreting DSP instructions.
    {
        Probe vector_probe{};
        galaxy::NativeDspCoprocessor vector_dsp;
        vector_dsp.set_host_callbacks(
            &vector_probe, record_interrupt, record_accelerator_exception);
        galaxy::NativeDspWorker vector_worker(
            vector_dsp, vector_resumable_halt_echo_ucode, /*free_running=*/true);
        vector_worker.start();

        const bool boot_mail_arrived = spin_until(
            [&] {
                const auto snapshot = vector_worker.halted_snapshot();
                return halted_snapshot_at(snapshot, 0x0748u, 1u) &&
                       vector_dsp.dsp_mail_present() &&
                       vector_probe.interrupt_count == 1u;
            },
            std::chrono::seconds(2));
        const auto boot_high = vector_dsp.cpu_read_from_dsp_mailbox_high();
        const auto boot_low = vector_dsp.cpu_read_from_dsp_mailbox_low();
        const auto boot_snapshot = vector_worker.halted_snapshot();
        const bool vector_boot_ok =
            boot_mail_arrived && boot_high == 0xDCD1u && boot_low == 0x0000u &&
            halted_snapshot_at(boot_snapshot, 0x0748u, 1u) &&
            vector_probe.interrupt_count == 1u;
        if (!vector_boot_ok) {
            std::cerr << "vector boot state: arrived=" << boot_mail_arrived
                      << " high=0x" << std::hex << boot_high
                      << " low=0x" << boot_low
                      << " snapshot=" << (boot_snapshot.has_value() ? 1 : 0)
                      << " pc=0x"
                      << (boot_snapshot.has_value() ? boot_snapshot->pc : 0u)
                      << std::dec
                      << " halted="
                      << (boot_snapshot.has_value() && boot_snapshot->halted
                              ? 1
                              : 0)
                      << " runs=" << vector_worker.completed_runs()
                      << " irq=" << vector_probe.interrupt_count << '\n';
        }
        passed &= expect(
            vector_boot_ok,
            "free-running native DSP worker reaches HALT before vector resume");

        vector_dsp.cpu_write_to_dsp_mailbox_high(0x4444u);
        vector_dsp.cpu_write_to_dsp_mailbox_low(0x5555u);
        vector_worker.signal_resume(0x0010u);
        const bool vector_mail_arrived = spin_until(
            [&] {
                return vector_dsp.dsp_mail_present() &&
                       vector_worker.completed_runs() >= 2u;
            },
            std::chrono::seconds(2));
        const auto vector_high = static_cast<std::uint16_t>(
            vector_dsp.cpu_read_from_dsp_mailbox_high() & 0x7FFFu);
        const auto vector_low = vector_dsp.cpu_read_from_dsp_mailbox_low();
        const auto vector_snapshot = vector_worker.halted_snapshot();
        passed &= expect(
            vector_mail_arrived && vector_high == 0x4444u &&
                vector_low == 0x5555u &&
                halted_snapshot_at(vector_snapshot, 0x0748u, 2u) &&
                vector_probe.interrupt_count == 2u,
            "free-running native DSP worker resumes halted code through the "
            "task resume vector");

        vector_worker.stop();
    }

    // Free-running external interrupt: RMGE01's audio task posts a CPU->DSP
    // mailbox word, asserts DSPCR PIINT, and expects vector 0x000E to consume
    // that mail while preserving RTI return state. This is a native interrupt
    // path, not a high-level command reply.
    {
        Probe external_probe{};
        galaxy::NativeDspCoprocessor external_dsp;
        external_dsp.set_host_callbacks(
            &external_probe, record_interrupt, record_accelerator_exception);
        galaxy::NativeDspWorker external_worker(
            external_dsp,
            external_interrupt_count_ucode,
            /*free_running=*/true);
        external_worker.start();

        const bool parked_before_piint = spin_until(
            [&] { return external_dsp.host_idle_wait_count() >= 1u; },
            std::chrono::seconds(2));
        external_worker.signal_external_interrupt(0x000Eu);
        const bool parked_in_interrupt_handler = spin_until(
            [&] { return external_dsp.host_idle_wait_count() >= 2u; },
            std::chrono::seconds(2));
        external_dsp.cpu_write_to_dsp_mailbox_high(0x0000u);
        external_dsp.cpu_write_to_dsp_mailbox_low(0x0002u);
        const bool interrupt_mail_arrived = spin_until(
            [&] {
                return external_dsp.dsp_mail_present() &&
                       external_worker.completed_runs() >= 2u;
            },
            std::chrono::seconds(2));
        const auto interrupt_high = static_cast<std::uint16_t>(
            external_dsp.cpu_read_from_dsp_mailbox_high() & 0x7FFFu);
        const auto interrupt_low = external_dsp.cpu_read_from_dsp_mailbox_low();
        const auto external_snapshot = external_worker.halted_snapshot();
        passed &= expect(
            parked_before_piint && parked_in_interrupt_handler &&
                interrupt_mail_arrived && interrupt_high == 0x6E00u &&
                interrupt_low == 0x0002u &&
                external_snapshot.has_value() &&
                snapshot_dram_equals(
                    *external_snapshot,
                    0x0010u,
                    galaxy::kRmge01DspCmbhCommandPollPc) &&
                ([&] {
                    std::uint16_t stacked_sr = 0u;
                    return external_snapshot->read_dram_word(
                               0x0011u, stacked_sr) &&
                           (stacked_sr & galaxy::kDspSrExtIntEnable) != 0u;
                }()) &&
                halted_snapshot_at(
                    external_snapshot,
                    galaxy::kRmge01DspCmbhCommandPollPc,
                    2u) &&
                external_probe.interrupt_count == 1u,
            "free-running native DSP accepts PIINT at a mailbox poll, "
            "runs vector 0x000E, preserves RTI state, and consumes the "
            "CPU-to-DSP mail");

        external_worker.stop();
    }

    {
        Probe disabled_probe{};
        galaxy::NativeDspCoprocessor disabled_dsp;
        disabled_dsp.set_host_callbacks(
            &disabled_probe, record_interrupt, record_accelerator_exception);
        galaxy::NativeDspWorker disabled_worker(
            disabled_dsp,
            disabled_external_interrupt_halt_ucode,
            /*free_running=*/true);
        disabled_worker.start();

        const bool disabled_halt_arrived = spin_until(
            [&] {
                return halted_snapshot_at(
                    disabled_worker.halted_snapshot(), 0x0123u, 1u);
            },
            std::chrono::seconds(2));
        disabled_worker.signal_external_interrupt(0x000Eu);
        const bool disabled_piint_resumed =
            disabled_worker.wait_for_completed_runs(
                2u, std::chrono::milliseconds(25));
        const auto disabled_snapshot = disabled_worker.halted_snapshot();
        passed &= expect(
            disabled_halt_arrived && !disabled_piint_resumed &&
                disabled_worker.completed_runs() == 1u &&
                halted_snapshot_at(disabled_snapshot, 0x0123u, 1u) &&
                snapshot_dram_equals(*disabled_snapshot, 0x0020u, 0u),
            "free-running native DSP defers PIINT while SR external "
            "interrupts are disabled instead of running vector 0x000E "
            "without stacked state");

        disabled_worker.stop();
    }

    // A PIINT that is accepted while the lowered ucode is sitting on a HALT
    // instruction must stack the visible HALT PC exactly. Plain mailbox writes
    // are still covered above: they do not clear HALT by themselves.
    {
        Probe halted_external_probe{};
        galaxy::NativeDspCoprocessor halted_external_dsp;
        halted_external_dsp.set_host_callbacks(
            &halted_external_probe,
            record_interrupt,
            record_accelerator_exception);
        galaxy::NativeDspWorker halted_external_worker(
            halted_external_dsp,
            halted_external_interrupt_returns_to_halt_ucode,
            /*free_running=*/true);
        halted_external_worker.start();

        const bool halted_state_arrived = spin_until(
            [&] {
                return halted_snapshot_at(
                    halted_external_worker.halted_snapshot(), 0x0123u, 1u);
            },
            std::chrono::seconds(2));
        halted_external_worker.signal_external_interrupt(0x000Eu);
        const bool interrupt_return_arrived = spin_until(
            [&] {
                return halted_snapshot_at(
                    halted_external_worker.halted_snapshot(), 0x0123u, 2u);
            },
            std::chrono::seconds(2));
        const auto halted_external_snapshot =
            halted_external_worker.halted_snapshot();
        std::uint16_t halted_external_stacked_sr = 0u;
        const bool halted_external_sr_ok =
            halted_external_snapshot.has_value() &&
            halted_external_snapshot->read_dram_word(
                0x0031u, halted_external_stacked_sr) &&
            (halted_external_stacked_sr & galaxy::kDspSrExtIntEnable) != 0u;
        passed &= expect(
            halted_state_arrived && interrupt_return_arrived &&
                !halted_external_dsp.dsp_mail_present() &&
                halted_external_snapshot.has_value() &&
                snapshot_dram_equals(
                    *halted_external_snapshot, 0x0030u, 0x0123u) &&
                halted_external_sr_ok &&
                snapshot_dram_equals(
                    *halted_external_snapshot, 0x0032u, 0u) &&
                halted_snapshot_at(
                    halted_external_snapshot, 0x0123u, 2u) &&
                halted_external_probe.interrupt_count == 0u,
            "free-running native DSP returns from PIINT to the visible HALT "
            "PC without executing the following IRAM word");

        halted_external_worker.stop();
    }

    passed &= test_diagnostic_snapshot_coherence();

    dsp.reset();
    passed &= expect(
        dsp.context().dram[0x0002] == 0u &&
            dsp.context().hardware.external_read_byte != nullptr,
        "native DSP coprocessor reset clears state and keeps bridge callbacks attached");

    return passed ? 0 : 1;
}
