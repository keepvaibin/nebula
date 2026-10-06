#pragma once

#include "galaxy/dsp_instruction_provenance.h"
#include "galaxy/dsp_timing_runtime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace galaxy::dsp_timing_replay {

// Offline, test-owned replay only. This header is not installed in DspContext,
// generated code, or the native DSP worker. It consumes an already committed
// raw boundary record and supplies only evidence that the raw schema cannot
// currently carry. Trap, unwind, abort, and reset boundaries do not produce a
// committed raw input and therefore cannot be synthesized here; reset starts a
// new ledger and replay session.

enum class DspTimingReplayAcceleratorEvidence : std::uint8_t {
    Unavailable,
    NoPending,
    RawReadEndPending,
    RawWriteEndPending,
    SampleReadEndPending,
    MultiplePending,
};

enum class DspTimingReplayAcceptedInterrupt : std::uint8_t {
    None,
    External,
    Accelerator,
};

struct DspTimingReplaySupplement {
    // Assertion only: the ledger remains the source of predecessor truth.
    std::optional<dsp_timing_runtime::DspTimingInstructionIdentity>
        expected_predecessor{};
    dsp_timing_runtime::DspTimingPostOutcome post_outcome{};
    // Raw provenance truthfully reports accelerator state as unavailable, so a
    // test must explicitly provide one representable pre-instruction state.
    DspTimingReplayAcceleratorEvidence accelerator{
        DspTimingReplayAcceleratorEvidence::Unavailable};
    DspTimingReplayAcceptedInterrupt accepted_interrupt{
        DspTimingReplayAcceptedInterrupt::None};
    // Exact vector assertion when accepted_interrupt is External. It must be
    // the no-pending sentinel for None and Accelerator.
    std::uint16_t accepted_external_vector{
        kDspNoPendingExternalInterrupt};
    // Test-only fault injection for the otherwise unreachable invariant that a
    // ledger commit advanced but its returned copy disagreed with the exact
    // mapped record. The session must become terminal and cannot cancel an
    // already committed transaction.
    bool force_post_commit_record_mismatch{};
};

enum class DspTimingReplayFailure : std::uint8_t {
    None,
    LedgerUnavailable,
    LedgerStateMismatch,
    InvalidRawSequence,
    DuplicateRawSequence,
    RawSequenceGap,
    RawSequenceOverflow,
    InvalidRawInstructionIdentity,
    InstructionIdentityMismatch,
    UnauthorizedPlanTableIdentity,
    InvalidRawPreState,
    LogicalStackDepthUnavailable,
    LogicalStackEpochMismatch,
    ExternalVectorUnavailable,
    AcceleratorEvidenceUnavailable,
    UnrepresentableAcceleratorEvidence,
    SimultaneousPendingSourcesUnrepresentable,
    InterruptEvidenceMismatch,
    ExternalVectorMismatch,
    PredecessorMismatch,
    InvalidRawMemoryCount,
    InvalidRawMemoryPadding,
    InvalidRawMemoryDescriptor,
    NoncanonicalRawMemoryOrder,
    MemorySlotMismatch,
    MemorySpaceMismatch,
    MemoryDirectionMismatch,
    LedgerBeginRejected,
    LedgerMemoryRejected,
    LedgerCommitRejected,
    LedgerCancellationRejected,
    LedgerRecordMismatch,
};

struct DspTimingReplayRecord {
    std::uint64_t raw_sequence{};
    std::uint64_t raw_logical_stack_epoch{};
    DspRawExternalVectorState raw_external_vector{};
    DspTimingReplayAcceleratorEvidence accelerator_evidence{
        DspTimingReplayAcceleratorEvidence::Unavailable};
    DspTimingReplayAcceptedInterrupt accepted_interrupt{
        DspTimingReplayAcceptedInterrupt::None};
    dsp_timing_runtime::DspTimingRetirementRecord retirement{};
};

namespace detail {

[[nodiscard]] constexpr std::optional<
    dsp_timing_runtime::DspTimingBundleKind>
timing_bundle(DspGeneratedInstructionBundleKind bundle) noexcept {
    using TimingBundle = dsp_timing_runtime::DspTimingBundleKind;
    switch (bundle) {
    case DspGeneratedInstructionBundleKind::Standalone:
        return TimingBundle::Standalone;
    case DspGeneratedInstructionBundleKind::Parallel:
        return TimingBundle::Parallel;
    }
    return std::nullopt;
}

[[nodiscard]] constexpr std::optional<
    dsp_timing_runtime::DspTimingInstructionIdentity>
timing_identity(const DspGeneratedInstructionIdentity& raw) noexcept {
    const auto bundle = timing_bundle(raw.bundle);
    if (!bundle.has_value()) {
        return std::nullopt;
    }
    return dsp_timing_runtime::DspTimingInstructionIdentity{
        raw.fetch_address,
        raw.opcode_word,
        raw.second_word,
        raw.has_second_word,
        *bundle,
        raw.parallel_primary_word,
        raw.parallel_extension,
        raw.parallel_extension_mask,
    };
}

[[nodiscard]] constexpr std::optional<dsp_timing_runtime::DspTimingMemorySlot>
timing_memory_slot(DspGeneratedMemorySlot slot) noexcept {
    using TimingSlot = dsp_timing_runtime::DspTimingMemorySlot;
    switch (slot) {
    case DspGeneratedMemorySlot::Standalone:
        return TimingSlot::Standalone;
    case DspGeneratedMemorySlot::ParallelPrimary:
        return TimingSlot::ParallelPrimary;
    case DspGeneratedMemorySlot::ParallelSecondary:
        return TimingSlot::ParallelSecondary;
    }
    return std::nullopt;
}

[[nodiscard]] constexpr std::optional<dsp_timing_runtime::DspTimingMemorySpace>
timing_memory_space(DspGeneratedMemorySpace space) noexcept {
    using TimingSpace = dsp_timing_runtime::DspTimingMemorySpace;
    switch (space) {
    case DspGeneratedMemorySpace::Instruction:
        return TimingSpace::Instruction;
    case DspGeneratedMemorySpace::Data:
        return TimingSpace::Data;
    }
    return std::nullopt;
}

[[nodiscard]] constexpr std::optional<
    dsp_timing_runtime::DspTimingMemoryDirection>
timing_memory_direction(DspGeneratedMemoryDirection direction) noexcept {
    using TimingDirection = dsp_timing_runtime::DspTimingMemoryDirection;
    switch (direction) {
    case DspGeneratedMemoryDirection::Read:
        return TimingDirection::Read;
    case DspGeneratedMemoryDirection::Write:
        return TimingDirection::Write;
    }
    return std::nullopt;
}

struct MappedMemoryOperand {
    dsp_timing_runtime::DspTimingMemorySlot slot{
        dsp_timing_runtime::DspTimingMemorySlot::Standalone};
    dsp_timing_runtime::DspTimingMemorySpace space{
        dsp_timing_runtime::DspTimingMemorySpace::Data};
    dsp_timing_runtime::DspTimingMemoryDirection direction{
        dsp_timing_runtime::DspTimingMemoryDirection::Read};
    std::uint16_t address{};
    dsp_timing_runtime::DspTimingMemoryRegion region{
        dsp_timing_runtime::DspTimingMemoryRegion::DataDram};
};

[[nodiscard]] constexpr bool same_pre_state(
    const dsp_timing_runtime::DspTimingPreState& left,
    const dsp_timing_runtime::DspTimingPreState& right) noexcept {
    return left.fetch_address == right.fetch_address &&
        left.status_register == right.status_register &&
        left.control_register == right.control_register &&
        left.address_registers == right.address_registers &&
        left.index_registers == right.index_registers &&
        left.wrap_registers == right.wrap_registers &&
        left.stack_depths == right.stack_depths &&
        left.external_interrupt_pending == right.external_interrupt_pending &&
        left.accelerator_interrupt_pending ==
            right.accelerator_interrupt_pending &&
        left.predecessor == right.predecessor;
}

[[nodiscard]] constexpr bool same_plan(
    const dsp_timing_runtime::DspTimingInstructionPlan& left,
    const dsp_timing_runtime::DspTimingInstructionPlan& right) noexcept {
    return left.identity == right.identity &&
        left.binding.contract_version == right.binding.contract_version &&
        left.binding.resolver_plan_identity ==
            right.binding.resolver_plan_identity &&
        left.binding.instruction_word_count ==
            right.binding.instruction_word_count &&
        left.binding.validated_bundle == right.binding.validated_bundle &&
        left.binding.instruction_semantic ==
            right.binding.instruction_semantic &&
        left.binding.hardware_loop_site ==
            right.binding.hardware_loop_site &&
        left.expected_memory_slots == right.expected_memory_slots &&
        left.memory_operands == right.memory_operands &&
        left.allowed_instruction_outcomes ==
            right.allowed_instruction_outcomes &&
        left.allowed_hardware_loop_outcomes ==
            right.allowed_hardware_loop_outcomes;
}

[[nodiscard]] constexpr bool same_post_outcome(
    const dsp_timing_runtime::DspTimingPostOutcome& left,
    const dsp_timing_runtime::DspTimingPostOutcome& right) noexcept {
    return left.instruction == right.instruction &&
        left.condition == right.condition &&
        left.hardware_loop == right.hardware_loop &&
        left.interrupt == right.interrupt;
}

[[nodiscard]] constexpr std::optional<std::uint64_t> next_raw_sequence(
    std::uint64_t completed_raw_sequence) noexcept {
    if (completed_raw_sequence ==
        std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }
    return completed_raw_sequence + 1u;
}

}  // namespace detail

class DspTimingReplaySession {
public:
    // Replay has no checkpoint-restore authority. The session exclusively owns
    // a fresh ledger; no caller can reset or repopulate it between records and
    // splice predecessor history while the session retains its stack epoch.
    DspTimingReplaySession() noexcept = default;

    DspTimingReplaySession(const DspTimingReplaySession&) = delete;
    DspTimingReplaySession& operator=(const DspTimingReplaySession&) = delete;
    DspTimingReplaySession(DspTimingReplaySession&&) = delete;
    DspTimingReplaySession& operator=(DspTimingReplaySession&&) = delete;

    [[nodiscard]] std::optional<DspTimingReplayRecord> replay(
        const DspRawInstructionBoundaryRecord& raw,
        const dsp_timing_runtime::DspTimingInstructionPlan& plan,
        const dsp_timing_runtime::DspTimingAuthorizedPlanEntry& authorized_entry,
        const DspTimingReplaySupplement& supplement) noexcept {
        namespace timing = dsp_timing_runtime;

        if (failed()) {
            return std::nullopt;
        }
        if (ledger_.active() || ledger_.failed()) {
            return fail(DspTimingReplayFailure::LedgerUnavailable);
        }
        const auto expected_sequence =
            detail::next_raw_sequence(completed_raw_sequence_);
        if (!expected_sequence.has_value()) {
            return fail(DspTimingReplayFailure::RawSequenceOverflow);
        }
        if (raw.sequence == 0u) {
            return fail(DspTimingReplayFailure::InvalidRawSequence);
        }
        if (raw.sequence <= completed_raw_sequence_) {
            return fail(DspTimingReplayFailure::DuplicateRawSequence);
        }
        if (raw.sequence != *expected_sequence) {
            return fail(DspTimingReplayFailure::RawSequenceGap);
        }
        const auto ledger_state = ledger_.state();
        if (ledger_state.retired_instructions != completed_raw_sequence_) {
            return fail(DspTimingReplayFailure::LedgerStateMismatch);
        }

        if (!dsp_instruction_provenance_detail::valid_generated_identity(
                raw.identity)) {
            return fail(
                DspTimingReplayFailure::InvalidRawInstructionIdentity);
        }
        const auto mapped_identity = detail::timing_identity(raw.identity);
        if (!mapped_identity.has_value()) {
            return fail(
                DspTimingReplayFailure::InvalidRawInstructionIdentity);
        }
        if (*mapped_identity != plan.identity) {
            return fail(DspTimingReplayFailure::InstructionIdentityMismatch);
        }
        // This exact row comparison binds the caller-supplied plan to its
        // deterministic table identity; it is not artifact authentication.
        // Offline tests own both values. A future authenticated generated-table
        // lookup remains required before any execution use can be considered.
        if (authorized_entry.contract_version !=
                plan.binding.contract_version ||
            authorized_entry.resolver_plan_identity !=
                plan.binding.resolver_plan_identity) {
            return fail(
                DspTimingReplayFailure::UnauthorizedPlanTableIdentity);
        }

        if (!dsp_instruction_provenance_detail::valid_pre_state(
                raw.pre_state)) {
            return fail(DspTimingReplayFailure::InvalidRawPreState);
        }
        if (raw.pre_state.logical_stack_epoch == 0u) {
            return fail(
                DspTimingReplayFailure::LogicalStackDepthUnavailable);
        }
        timing::DspTimingPreState mapped_pre_state{};
        mapped_pre_state.fetch_address = raw.identity.fetch_address;
        mapped_pre_state.status_register = raw.pre_state.status_register;
        mapped_pre_state.control_register = raw.pre_state.control_register;
        mapped_pre_state.address_registers = raw.pre_state.address_registers;
        mapped_pre_state.index_registers = raw.pre_state.index_registers;
        mapped_pre_state.wrap_registers = raw.pre_state.wrap_registers;
        // Raw wrapping stack tops/cursors have no schema-v2 timing-key field.
        // They were structurally validated above; only the separately tracked
        // exact logical depths can be mapped into the retirement key.
        for (std::size_t index = 0u;
             index < raw.pre_state.logical_stack_depths.size();
             ++index) {
            const auto depth = raw.pre_state.logical_stack_depths[index];
            if (depth.kind != DspRawLogicalStackDepthKind::Exact) {
                return fail(
                    DspTimingReplayFailure::LogicalStackDepthUnavailable);
            }
            mapped_pre_state.stack_depths[index] = depth.depth;
        }
        if (logical_stack_epoch_.has_value() &&
            *logical_stack_epoch_ != raw.pre_state.logical_stack_epoch) {
            return fail(DspTimingReplayFailure::LogicalStackEpochMismatch);
        }

        switch (raw.pre_state.external_vector.kind) {
        case DspRawExternalVectorKind::Unavailable:
            return fail(DspTimingReplayFailure::ExternalVectorUnavailable);
        case DspRawExternalVectorKind::NoPendingVector:
            mapped_pre_state.external_interrupt_pending = false;
            break;
        case DspRawExternalVectorKind::PendingVector:
            mapped_pre_state.external_interrupt_pending = true;
            break;
        default:
            return fail(DspTimingReplayFailure::InvalidRawPreState);
        }

        switch (supplement.accelerator) {
        case DspTimingReplayAcceleratorEvidence::Unavailable:
            return fail(
                DspTimingReplayFailure::AcceleratorEvidenceUnavailable);
        case DspTimingReplayAcceleratorEvidence::NoPending:
            mapped_pre_state.accelerator_interrupt_pending = false;
            break;
        case DspTimingReplayAcceleratorEvidence::RawReadEndPending:
        case DspTimingReplayAcceleratorEvidence::RawWriteEndPending:
        case DspTimingReplayAcceleratorEvidence::SampleReadEndPending:
            mapped_pre_state.accelerator_interrupt_pending = true;
            break;
        case DspTimingReplayAcceleratorEvidence::MultiplePending:
            return fail(
                DspTimingReplayFailure::UnrepresentableAcceleratorEvidence);
        default:
            return fail(
                DspTimingReplayFailure::UnrepresentableAcceleratorEvidence);
        }
        if (mapped_pre_state.external_interrupt_pending &&
            mapped_pre_state.accelerator_interrupt_pending) {
            return fail(
                DspTimingReplayFailure::
                    SimultaneousPendingSourcesUnrepresentable);
        }

        switch (supplement.post_outcome.interrupt) {
        case timing::DspTimingInterruptOutcome::None:
            if (supplement.accepted_interrupt !=
                    DspTimingReplayAcceptedInterrupt::None ||
                supplement.accepted_external_vector !=
                    kDspNoPendingExternalInterrupt) {
                return fail(
                    DspTimingReplayFailure::InterruptEvidenceMismatch);
            }
            break;
        case timing::DspTimingInterruptOutcome::AcceptedAfterInstruction:
            switch (supplement.accepted_interrupt) {
            case DspTimingReplayAcceptedInterrupt::External:
                if (!mapped_pre_state.external_interrupt_pending) {
                    return fail(
                        DspTimingReplayFailure::InterruptEvidenceMismatch);
                }
                if (supplement.accepted_external_vector !=
                    raw.pre_state.external_vector.vector) {
                    return fail(
                        DspTimingReplayFailure::ExternalVectorMismatch);
                }
                break;
            case DspTimingReplayAcceptedInterrupt::Accelerator:
                if (!mapped_pre_state.accelerator_interrupt_pending ||
                    supplement.accepted_external_vector !=
                        kDspNoPendingExternalInterrupt) {
                    return fail(
                        DspTimingReplayFailure::InterruptEvidenceMismatch);
                }
                break;
            case DspTimingReplayAcceptedInterrupt::None:
            default:
                return fail(
                    DspTimingReplayFailure::InterruptEvidenceMismatch);
            }
            break;
        default:
            return fail(DspTimingReplayFailure::InterruptEvidenceMismatch);
        }

        mapped_pre_state.predecessor = ledger_state.last_retired_instruction;
        if (supplement.expected_predecessor !=
            mapped_pre_state.predecessor) {
            return fail(DspTimingReplayFailure::PredecessorMismatch);
        }

        if (raw.memory_operand_count > 2u) {
            return fail(DspTimingReplayFailure::InvalidRawMemoryCount);
        }
        for (std::size_t index = raw.memory_operand_count;
             index < raw.memory_operands.size();
             ++index) {
            if (raw.memory_operands[index] != DspRawResolvedMemoryOperand{}) {
                return fail(DspTimingReplayFailure::InvalidRawMemoryPadding);
            }
        }

        std::array<detail::MappedMemoryOperand, 3> mapped_memory{};
        std::uint8_t mapped_memory_mask{};
        std::uint8_t prior_slot{};
        bool have_prior_slot = false;
        for (std::size_t index = 0u; index < raw.memory_operand_count; ++index) {
            const auto& operand = raw.memory_operands[index];
            const auto slot = detail::timing_memory_slot(operand.slot);
            const auto space = detail::timing_memory_space(operand.space);
            const auto direction =
                detail::timing_memory_direction(operand.direction);
            if (!slot.has_value() || !space.has_value() ||
                !direction.has_value()) {
                return fail(
                    DspTimingReplayFailure::InvalidRawMemoryDescriptor);
            }
            const auto slot_index = static_cast<std::uint8_t>(*slot);
            if (have_prior_slot && prior_slot >= slot_index) {
                return fail(
                    DspTimingReplayFailure::NoncanonicalRawMemoryOrder);
            }
            have_prior_slot = true;
            prior_slot = slot_index;
            if ((plan.identity.bundle == timing::DspTimingBundleKind::Standalone &&
                 *slot != timing::DspTimingMemorySlot::Standalone) ||
                (plan.identity.bundle == timing::DspTimingBundleKind::Parallel &&
                 *slot == timing::DspTimingMemorySlot::Standalone)) {
                return fail(DspTimingReplayFailure::MemorySlotMismatch);
            }
            const auto slot_bit = timing::dsp_timing_memory_slot_bit(*slot);
            mapped_memory_mask = static_cast<std::uint8_t>(
                mapped_memory_mask | slot_bit);
            const auto plan_index = static_cast<std::size_t>(*slot);
            if (slot_bit == 0u || plan_index >= plan.memory_operands.size() ||
                !plan.memory_operands[plan_index].has_value()) {
                return fail(DspTimingReplayFailure::MemorySlotMismatch);
            }
            const auto authority = *plan.memory_operands[plan_index];
            if (*space != authority.space) {
                return fail(DspTimingReplayFailure::MemorySpaceMismatch);
            }
            if (*direction != authority.direction) {
                return fail(DspTimingReplayFailure::MemoryDirectionMismatch);
            }
            // The timing plan intentionally omits the generated address-source
            // descriptor. An observer-committed raw address is therefore
            // preserved and classified exactly, never re-derived or promoted
            // into a claim that an arbitrary copied record is authentic.
            const auto region = timing::detail::memory_region_for(
                *space, *direction, operand.address);
            if (!region.has_value()) {
                return fail(
                    DspTimingReplayFailure::InvalidRawMemoryDescriptor);
            }
            mapped_memory[index] = {
                *slot,
                *space,
                *direction,
                operand.address,
                *region,
            };
        }
        if (mapped_memory_mask != plan.expected_memory_slots) {
            return fail(DspTimingReplayFailure::MemorySlotMismatch);
        }

        const auto token = ledger_.begin(
            plan, authorized_entry, mapped_pre_state);
        if (!token.has_value()) {
            return fail(DspTimingReplayFailure::LedgerBeginRejected);
        }
        for (std::size_t index = 0u; index < raw.memory_operand_count; ++index) {
            const auto& operand = mapped_memory[index];
            if (!ledger_.record_memory(
                    *token,
                    operand.slot,
                    operand.space,
                    operand.direction,
                    operand.address)) {
                return fail_after_cancel(
                    *token, DspTimingReplayFailure::LedgerMemoryRejected);
            }
        }
        auto retirement = ledger_.commit(*token, supplement.post_outcome);
        if (!retirement.has_value()) {
            return fail_after_cancel(
                *token, DspTimingReplayFailure::LedgerCommitRejected);
        }
        ++successful_commits_;

        bool record_matches =
            retirement->retirement_sequence == raw.sequence &&
            detail::same_plan(retirement->plan, plan) &&
            detail::same_pre_state(retirement->pre_state, mapped_pre_state) &&
            retirement->memory_access_count == raw.memory_operand_count &&
            detail::same_post_outcome(
                retirement->post_outcome, supplement.post_outcome);
        if (supplement.force_post_commit_record_mismatch) {
            record_matches = false;
        }
        for (std::size_t index = 0u;
             record_matches && index < raw.memory_operand_count;
             ++index) {
            const auto& expected = mapped_memory[index];
            const auto& actual = retirement->memory_accesses[index];
            record_matches = actual.slot == expected.slot &&
                actual.space == expected.space &&
                actual.direction == expected.direction &&
                actual.address == expected.address &&
                actual.region == expected.region;
        }
        if (!record_matches) {
            return fail(DspTimingReplayFailure::LedgerRecordMismatch);
        }

        completed_raw_sequence_ = raw.sequence;
        if (!logical_stack_epoch_.has_value()) {
            logical_stack_epoch_ = raw.pre_state.logical_stack_epoch;
        }
        return DspTimingReplayRecord{
            raw.sequence,
            raw.pre_state.logical_stack_epoch,
            raw.pre_state.external_vector,
            supplement.accelerator,
            supplement.accepted_interrupt,
            *retirement,
        };
    }

    [[nodiscard]] bool failed() const noexcept {
        return failure_ != DspTimingReplayFailure::None;
    }
    [[nodiscard]] DspTimingReplayFailure failure() const noexcept {
        return failure_;
    }
    [[nodiscard]] std::uint64_t completed_raw_sequence() const noexcept {
        return completed_raw_sequence_;
    }
    [[nodiscard]] std::optional<std::uint64_t> logical_stack_epoch() const
        noexcept {
        return logical_stack_epoch_;
    }
    [[nodiscard]] std::uint64_t successful_commits() const noexcept {
        return successful_commits_;
    }
    [[nodiscard]] std::uint64_t cancellation_attempts() const noexcept {
        return cancellation_attempts_;
    }
    // Diagnostics are copies only. No ledger reference or mutation surface is
    // exposed to tests or future callers.
    [[nodiscard]] dsp_timing_runtime::DspTimingRetirementState ledger_state()
        const noexcept {
        return ledger_.state();
    }
    [[nodiscard]] dsp_timing_runtime::DspTimingRetirementFailure
    ledger_failure() const noexcept {
        return ledger_.failure();
    }
    [[nodiscard]] bool ledger_active() const noexcept {
        return ledger_.active();
    }
    [[nodiscard]] bool ledger_failed() const noexcept {
        return ledger_.failed();
    }

private:
    [[nodiscard]] std::optional<DspTimingReplayRecord> fail(
        DspTimingReplayFailure failure) noexcept {
        if (failure_ == DspTimingReplayFailure::None) {
            failure_ = failure;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<DspTimingReplayRecord> fail_after_cancel(
        dsp_timing_runtime::DspTimingTransactionToken token,
        DspTimingReplayFailure failure) noexcept {
        ++cancellation_attempts_;
        if (!ledger_.cancel(
                token,
                dsp_timing_runtime::DspTimingCancellationReason::
                    ExplicitAbort)) {
            return fail(DspTimingReplayFailure::LedgerCancellationRejected);
        }
        return fail(failure);
    }

    dsp_timing_runtime::DspTimingRetirementLedger ledger_{};
    DspTimingReplayFailure failure_{DspTimingReplayFailure::None};
    std::uint64_t completed_raw_sequence_{};
    std::optional<std::uint64_t> logical_stack_epoch_{};
    std::uint64_t successful_commits_{};
    std::uint64_t cancellation_attempts_{};
};

}  // namespace galaxy::dsp_timing_replay
