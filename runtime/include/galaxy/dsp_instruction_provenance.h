#pragma once

#include "galaxy/dsp_context.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <span>

namespace galaxy {

// Inert, test-owned evidence for the exact generated instruction block that
// entered. This is deterministic install-time metadata, not runtime decoding,
// artifact authentication, a timing-profile lookup, or a production capability.
enum class DspGeneratedInstructionBundleKind : std::uint8_t {
    Standalone,
    Parallel,
};

struct DspGeneratedInstructionIdentity {
    std::uint16_t fetch_address{};
    std::uint16_t opcode_word{};
    // Exact optional second fetched word. No immediate/address semantic is
    // inferred from its presence or value.
    std::uint16_t second_word{};
    bool has_second_word{};
    DspGeneratedInstructionBundleKind bundle{
        DspGeneratedInstructionBundleKind::Standalone};
    std::uint16_t parallel_primary_word{};
    std::uint8_t parallel_extension{};
    std::uint8_t parallel_extension_mask{};

    bool operator==(const DspGeneratedInstructionIdentity&) const = default;
};

enum class DspGeneratedMemorySlot : std::uint8_t {
    Standalone = 0,
    ParallelPrimary = 1,
    ParallelSecondary = 2,
};

enum class DspGeneratedMemorySpace : std::uint8_t {
    Instruction,
    Data,
};

enum class DspGeneratedMemoryDirection : std::uint8_t {
    Read,
    Write,
};

enum class DspGeneratedMemoryAddressSourceKind : std::uint8_t {
    Static,
    AddressRegister,
    DirectPage,
};

struct DspGeneratedMemoryOperandPlan {
    DspGeneratedMemorySlot slot{DspGeneratedMemorySlot::Standalone};
    DspGeneratedMemorySpace space{DspGeneratedMemorySpace::Data};
    DspGeneratedMemoryDirection direction{DspGeneratedMemoryDirection::Read};
    DspGeneratedMemoryAddressSourceKind address_source{
        DspGeneratedMemoryAddressSourceKind::Static};
    std::uint16_t address_source_value{};

    bool operator==(const DspGeneratedMemoryOperandPlan&) const = default;
};

// Fixed-width generated metadata. Only the first operand_count entries are
// present; the remainder must be exact value-initialized descriptors. This is
// install-time decoder output, never runtime opcode decoding.
struct DspGeneratedMemoryOperandPlanSet {
    std::array<DspGeneratedMemoryOperandPlan, 3> operands{};
    std::uint8_t operand_count{};

    bool operator==(const DspGeneratedMemoryOperandPlanSet&) const = default;
};

struct DspRawResolvedMemoryOperand {
    DspGeneratedMemorySlot slot{DspGeneratedMemorySlot::Standalone};
    DspGeneratedMemorySpace space{DspGeneratedMemorySpace::Data};
    DspGeneratedMemoryDirection direction{DspGeneratedMemoryDirection::Read};
    std::uint16_t address{};

    bool operator==(const DspRawResolvedMemoryOperand&) const = default;
};

// A wrapping hardware cursor is not a logical stack depth. The observer keeps
// the exact top register and exact five-bit cursor separately and never infers
// how many pushes are logically live.
struct DspRawStackCursorState {
    std::uint16_t top{};
    std::uint8_t cursor{};

    bool operator==(const DspRawStackCursorState&) const = default;
};

enum class DspRawLogicalStackDepthKind : std::uint8_t {
    Unavailable,
    Exact,
};

struct DspRawLogicalStackDepthState {
    DspRawLogicalStackDepthKind kind{
        DspRawLogicalStackDepthKind::Unavailable};
    std::uint8_t depth{};

    bool operator==(const DspRawLogicalStackDepthState&) const = default;
};

enum class DspRawExternalVectorKind : std::uint8_t {
    Unavailable,
    NoPendingVector,
    PendingVector,
};

struct DspRawExternalVectorState {
    DspRawExternalVectorKind kind{DspRawExternalVectorKind::Unavailable};
    std::uint16_t vector{};

    bool operator==(const DspRawExternalVectorState&) const = default;
};

// DspContext has no reviewed accelerator pending/exception latch. Existing IFX
// registers and accelerator_reads_stopped are partial operational state, not
// equivalent evidence, so the raw observer can truthfully report only
// Unavailable.
enum class DspRawAcceleratorState : std::uint8_t {
    Unavailable,
};

struct DspRawInstructionPreState {
    std::uint16_t status_register{};
    std::uint16_t control_register{};
    std::array<std::uint16_t, kDspAddressRegisterCount> address_registers{};
    std::array<std::int16_t, kDspIndexRegisterCount> index_registers{};
    std::array<std::uint16_t, kDspWrapRegisterCount> wrap_registers{};
    std::array<DspRawStackCursorState, kDspStackRegisterCount> stacks{};
    std::array<
        DspRawLogicalStackDepthState,
        kDspStackRegisterCount>
        logical_stack_depths{};
    std::uint64_t logical_stack_epoch{};
    DspRawExternalVectorState external_vector{};
    DspRawAcceleratorState accelerator{DspRawAcceleratorState::Unavailable};

    bool operator==(const DspRawInstructionPreState&) const = default;
};

struct DspRawInstructionBoundaryRecord {
    std::uint64_t sequence{};
    DspGeneratedInstructionIdentity identity{};
    DspRawInstructionPreState pre_state{};
    std::array<DspRawResolvedMemoryOperand, 3> memory_operands{};
    std::uint8_t memory_operand_count{};

    bool operator==(const DspRawInstructionBoundaryRecord&) const = default;
};

class DspRawInstructionBoundaryToken {
public:
    DspRawInstructionBoundaryToken() noexcept = default;

    [[nodiscard]] bool valid() const noexcept {
        return owner_ != nullptr && epoch_ != 0u;
    }
    [[nodiscard]] std::uint64_t epoch_for_diagnostics() const noexcept {
        return epoch_;
    }

    bool operator==(const DspRawInstructionBoundaryToken&) const = default;

private:
    DspRawInstructionBoundaryToken(
        const void* owner,
        std::uint64_t epoch) noexcept
        : owner_(owner), epoch_(epoch) {}

    const void* owner_{};
    std::uint64_t epoch_{};

    friend class DspRawInstructionBoundaryObserver;
};

enum class DspRawInstructionBoundaryCancellation : std::uint8_t {
    ExceptionUnwind,
    ExplicitAbort,
    UncommittedInstruction,
};

enum class DspRawInstructionBoundaryFailure : std::uint8_t {
    None,
    InvalidInstructionIdentity,
    InvalidMemoryOperandPlan,
    InvalidStackCursor,
    LogicalStackDepthUnavailable,
    LogicalStackEpochChanged,
    InvalidExternalVectorState,
    InvalidAcceleratorState,
    NestedBegin,
    TransactionEpochOverflow,
    StaleTransactionToken,
    RecordCapacityExceeded,
    SequenceOverflow,
    InvalidCancellationReason,
    UncommittedInstruction,
};

namespace dsp_instruction_provenance_detail {

struct CheckedCounterIncrement {
    bool available{};
    std::uint64_t next{};
};

[[nodiscard]] constexpr CheckedCounterIncrement checked_counter_increment(
    std::uint64_t current) noexcept {
    if (current == std::numeric_limits<std::uint64_t>::max()) {
        return {};
    }
    return {true, current + 1u};
}

[[nodiscard]] constexpr CheckedCounterIncrement
checked_transaction_epoch_increment(std::uint64_t current) noexcept {
    return checked_counter_increment(current);
}

[[nodiscard]] constexpr CheckedCounterIncrement
checked_record_sequence_increment(std::uint64_t current) noexcept {
    return checked_counter_increment(current);
}

[[nodiscard]] constexpr bool valid_instruction_address(
    std::uint16_t address) noexcept {
    const auto region = static_cast<std::uint16_t>(address & 0xf000u);
    return region == 0x0000u || region == 0x8000u;
}

[[nodiscard]] constexpr bool valid_generated_identity(
    const DspGeneratedInstructionIdentity& identity) noexcept {
    if (!valid_instruction_address(identity.fetch_address) ||
        (!identity.has_second_word && identity.second_word != 0u)) {
        return false;
    }
    switch (identity.bundle) {
    case DspGeneratedInstructionBundleKind::Standalone:
        return identity.parallel_primary_word == 0u &&
            identity.parallel_extension == 0u &&
            identity.parallel_extension_mask == 0u;
    case DspGeneratedInstructionBundleKind::Parallel: {
        if (identity.has_second_word || identity.opcode_word >> 12u < 0x3u) {
            return false;
        }
        const auto expected_mask = static_cast<std::uint8_t>(
            identity.opcode_word >> 12u == 0x3u ? 0x7fu : 0xffu);
        const auto mask = static_cast<std::uint16_t>(expected_mask);
        return identity.parallel_extension != 0u &&
            identity.parallel_extension_mask == expected_mask &&
            identity.parallel_primary_word ==
                static_cast<std::uint16_t>(identity.opcode_word & ~mask) &&
            identity.parallel_extension ==
                static_cast<std::uint8_t>(identity.opcode_word & mask);
    }
    }
    return false;
}

[[nodiscard]] constexpr bool valid_generated_memory_operand_plan(
    const DspGeneratedInstructionIdentity& identity,
    const DspGeneratedMemoryOperandPlanSet& plan) noexcept {
    if (plan.operand_count > 2u) {
        return false;
    }
    for (std::size_t index = plan.operand_count;
         index < plan.operands.size();
         ++index) {
        if (plan.operands[index] != DspGeneratedMemoryOperandPlan{}) {
            return false;
        }
    }
    if (identity.bundle == DspGeneratedInstructionBundleKind::Parallel &&
        plan.operand_count == 1u &&
        plan.operands[0].slot != DspGeneratedMemorySlot::ParallelPrimary) {
        return false;
    }

    std::uint8_t prior_slot{};
    bool have_prior_slot = false;
    for (std::size_t index = 0u; index < plan.operand_count; ++index) {
        const auto& operand = plan.operands[index];
        const auto slot = static_cast<std::uint8_t>(operand.slot);
        switch (operand.slot) {
        case DspGeneratedMemorySlot::Standalone:
            if (identity.bundle !=
                DspGeneratedInstructionBundleKind::Standalone) {
                return false;
            }
            break;
        case DspGeneratedMemorySlot::ParallelPrimary:
        case DspGeneratedMemorySlot::ParallelSecondary:
            if (identity.bundle != DspGeneratedInstructionBundleKind::Parallel) {
                return false;
            }
            break;
        default:
            return false;
        }
        if (have_prior_slot && prior_slot >= slot) {
            return false;
        }
        have_prior_slot = true;
        prior_slot = slot;

        switch (operand.space) {
        case DspGeneratedMemorySpace::Instruction:
            if (operand.direction != DspGeneratedMemoryDirection::Read) {
                return false;
            }
            break;
        case DspGeneratedMemorySpace::Data:
            break;
        default:
            return false;
        }
        switch (operand.direction) {
        case DspGeneratedMemoryDirection::Read:
        case DspGeneratedMemoryDirection::Write:
            break;
        default:
            return false;
        }
        switch (operand.address_source) {
        case DspGeneratedMemoryAddressSourceKind::Static:
            if (operand.space != DspGeneratedMemorySpace::Data) {
                return false;
            }
            break;
        case DspGeneratedMemoryAddressSourceKind::AddressRegister:
            if (operand.address_source_value >= kDspAddressRegisterCount) {
                return false;
            }
            break;
        case DspGeneratedMemoryAddressSourceKind::DirectPage:
            if (operand.space != DspGeneratedMemorySpace::Data ||
                operand.address_source_value > 0x00ffu) {
                return false;
            }
            break;
        default:
            return false;
        }
    }
    return true;
}

struct ResolvedMemoryOperandPlan {
    bool available{};
    std::array<DspRawResolvedMemoryOperand, 3> operands{};
    std::uint8_t operand_count{};
};

[[nodiscard]] constexpr ResolvedMemoryOperandPlan
resolve_generated_memory_operand_plan(
    const DspGeneratedMemoryOperandPlanSet& plan,
    const DspRawInstructionPreState& pre_state) noexcept {
    ResolvedMemoryOperandPlan resolved{};
    resolved.operand_count = plan.operand_count;
    for (std::size_t index = 0u; index < plan.operand_count; ++index) {
        const auto& operand = plan.operands[index];
        std::uint16_t address{};
        switch (operand.address_source) {
        case DspGeneratedMemoryAddressSourceKind::Static:
            address = operand.address_source_value;
            break;
        case DspGeneratedMemoryAddressSourceKind::AddressRegister:
            if (operand.address_source_value >=
                pre_state.address_registers.size()) {
                return {};
            }
            address = pre_state.address_registers[operand.address_source_value];
            break;
        case DspGeneratedMemoryAddressSourceKind::DirectPage:
            if (operand.address_source_value > 0x00ffu) {
                return {};
            }
            address = static_cast<std::uint16_t>(
                (static_cast<std::uint32_t>(pre_state.control_register) << 8u) |
                operand.address_source_value);
            break;
        default:
            return {};
        }
        resolved.operands[index] = {
            operand.slot,
            operand.space,
            operand.direction,
            address,
        };
    }
    resolved.available = true;
    return resolved;
}

[[nodiscard]] constexpr bool valid_pre_state(
    const DspRawInstructionPreState& state) noexcept {
    for (const auto& stack : state.stacks) {
        if (stack.cursor > kDspStackMask) {
            return false;
        }
    }
    bool any_exact_depth = false;
    bool any_unavailable_depth = false;
    for (const auto& stack : state.logical_stack_depths) {
        switch (stack.kind) {
        case DspRawLogicalStackDepthKind::Unavailable:
            if (stack.depth != 0u) {
                return false;
            }
            any_unavailable_depth = true;
            break;
        case DspRawLogicalStackDepthKind::Exact:
            any_exact_depth = true;
            break;
        default:
            return false;
        }
    }
    if ((state.logical_stack_epoch == 0u && any_exact_depth) ||
        (state.logical_stack_epoch != 0u && !any_exact_depth) ||
        (state.logical_stack_epoch == 0u && !any_unavailable_depth)) {
        return false;
    }
    switch (state.external_vector.kind) {
    case DspRawExternalVectorKind::Unavailable:
        if (state.external_vector.vector != 0u) {
            return false;
        }
        break;
    case DspRawExternalVectorKind::NoPendingVector:
        if (state.external_vector.vector != kDspNoPendingExternalInterrupt) {
            return false;
        }
        break;
    case DspRawExternalVectorKind::PendingVector:
        if (state.external_vector.vector == kDspNoPendingExternalInterrupt) {
            return false;
        }
        break;
    default:
        return false;
    }
    return state.accelerator == DspRawAcceleratorState::Unavailable;
}

}  // namespace dsp_instruction_provenance_detail

// Single-execution-thread, test-owned recorder backed by caller-owned bounded
// storage. Product DspContext instances leave their observer pointer null.
class DspRawInstructionBoundaryObserver {
public:
    explicit DspRawInstructionBoundaryObserver(
        std::span<DspRawInstructionBoundaryRecord> storage) noexcept
        : storage_(storage) {}

    DspRawInstructionBoundaryObserver(
        const DspRawInstructionBoundaryObserver&) = delete;
    DspRawInstructionBoundaryObserver& operator=(
        const DspRawInstructionBoundaryObserver&) = delete;
    DspRawInstructionBoundaryObserver(
        DspRawInstructionBoundaryObserver&&) = delete;
    DspRawInstructionBoundaryObserver& operator=(
        DspRawInstructionBoundaryObserver&&) = delete;

    // Structural recorder entry used by isolated ledger unit tests. Generated
    // execution always enters through begin_tracked(), which additionally
    // binds and later rechecks the DspContext plus logical-depth epoch. Neither
    // path is a production timing capability.
    [[nodiscard]] DspRawInstructionBoundaryToken begin(
        const DspGeneratedInstructionIdentity& identity,
        const DspRawInstructionPreState& pre_state) noexcept {
        if (failure_ != DspRawInstructionBoundaryFailure::None) {
            return {};
        }
        if (active_) {
            sticky_fail(DspRawInstructionBoundaryFailure::NestedBegin);
            return {};
        }
        if (!dsp_instruction_provenance_detail::valid_generated_identity(
                identity)) {
            sticky_fail(
                DspRawInstructionBoundaryFailure::InvalidInstructionIdentity);
            return {};
        }
        if (!dsp_instruction_provenance_detail::valid_pre_state(pre_state)) {
            bool invalid_cursor = false;
            for (const auto& stack : pre_state.stacks) {
                invalid_cursor = invalid_cursor || stack.cursor > kDspStackMask;
            }
            if (invalid_cursor) {
                sticky_fail(
                    DspRawInstructionBoundaryFailure::InvalidStackCursor);
            } else if (pre_state.accelerator !=
                       DspRawAcceleratorState::Unavailable) {
                sticky_fail(
                    DspRawInstructionBoundaryFailure::InvalidAcceleratorState);
            } else {
                sticky_fail(
                    DspRawInstructionBoundaryFailure::InvalidExternalVectorState);
            }
            return {};
        }
        const auto next_epoch =
            dsp_instruction_provenance_detail::
                checked_transaction_epoch_increment(last_transaction_epoch_);
        if (!next_epoch.available) {
            sticky_fail(
                DspRawInstructionBoundaryFailure::TransactionEpochOverflow);
            return {};
        }
        const auto next_sequence =
            dsp_instruction_provenance_detail::
                checked_record_sequence_increment(committed_count_);
        if (!next_sequence.available) {
            sticky_fail(DspRawInstructionBoundaryFailure::SequenceOverflow);
            return {};
        }
        if (committed_count_ >= storage_.size()) {
            sticky_fail(
                DspRawInstructionBoundaryFailure::RecordCapacityExceeded);
            return {};
        }

        last_transaction_epoch_ = next_epoch.next;
        const DspRawInstructionBoundaryToken token{
            this, last_transaction_epoch_};
        active_ = true;
        active_token_ = token;
        active_sequence_ = next_sequence.next;
        active_identity_ = identity;
        active_pre_state_ = pre_state;
        return token;
    }

    [[nodiscard]] DspRawInstructionBoundaryToken begin_tracked(
        const DspGeneratedInstructionIdentity& identity,
        const DspRawInstructionPreState& pre_state,
        const DspContext& context) noexcept {
        return begin_tracked(identity, pre_state, context, {});
    }

    [[nodiscard]] DspRawInstructionBoundaryToken begin_tracked(
        const DspGeneratedInstructionIdentity& identity,
        const DspRawInstructionPreState& pre_state,
        const DspContext& context,
        const DspGeneratedMemoryOperandPlanSet& memory_plan) noexcept {
        if (!dsp_instruction_provenance_detail::
                valid_generated_memory_operand_plan(identity, memory_plan)) {
            sticky_fail(
                DspRawInstructionBoundaryFailure::InvalidMemoryOperandPlan);
            return {};
        }
        const auto resolved =
            dsp_instruction_provenance_detail::
                resolve_generated_memory_operand_plan(memory_plan, pre_state);
        if (!resolved.available) {
            sticky_fail(
                DspRawInstructionBoundaryFailure::InvalidMemoryOperandPlan);
            return {};
        }
        if (!dsp_logical_stack_depths_are_exact(context)) {
            sticky_fail(
                DspRawInstructionBoundaryFailure::
                    LogicalStackDepthUnavailable);
            return {};
        }
        const auto* tracker = context.logical_stack_depth_tracker;
        if (tracker == nullptr || pre_state.logical_stack_epoch != tracker->epoch) {
            sticky_fail(
                DspRawInstructionBoundaryFailure::LogicalStackEpochChanged);
            return {};
        }
        for (std::size_t index = 0u; index < kDspStackRegisterCount; ++index) {
            if (pre_state.logical_stack_depths[index] !=
                DspRawLogicalStackDepthState{
                    DspRawLogicalStackDepthKind::Exact,
                    tracker->depths[index]}) {
                sticky_fail(
                    DspRawInstructionBoundaryFailure::
                        LogicalStackDepthUnavailable);
                return {};
            }
        }
        const auto token = begin(identity, pre_state);
        if (token.valid()) {
            active_memory_operands_ = resolved.operands;
            active_memory_operand_count_ = resolved.operand_count;
            active_context_ = &context;
            active_logical_stack_tracker_ = tracker;
            active_logical_stack_epoch_ = tracker->epoch;
        }
        return token;
    }

    [[nodiscard]] bool commit(
        DspRawInstructionBoundaryToken token) noexcept {
        if (!authenticate(token)) {
            return false;
        }
        if (failure_ != DspRawInstructionBoundaryFailure::None) {
            return false;
        }
        const auto index = static_cast<std::size_t>(committed_count_);
        storage_[index] = {
            active_sequence_,
            active_identity_,
            active_pre_state_,
            active_memory_operands_,
            active_memory_operand_count_,
        };
        committed_count_ = active_sequence_;
        clear_active();
        return true;
    }

    [[nodiscard]] bool commit_tracked(
        DspRawInstructionBoundaryToken token,
        const DspContext& context) noexcept {
        if (!authenticate(token)) {
            return false;
        }
        if (active_context_ != &context ||
            context.logical_stack_depth_tracker !=
                active_logical_stack_tracker_ ||
            active_logical_stack_tracker_ == nullptr ||
            active_logical_stack_tracker_->owner != &context ||
            active_logical_stack_tracker_->epoch !=
                active_logical_stack_epoch_) {
            sticky_fail(
                DspRawInstructionBoundaryFailure::LogicalStackEpochChanged);
            return false;
        }
        if (!dsp_logical_stack_depths_are_exact(context)) {
            sticky_fail(
                DspRawInstructionBoundaryFailure::
                    LogicalStackDepthUnavailable);
            return false;
        }
        return commit(token);
    }

    [[nodiscard]] bool cancel(
        DspRawInstructionBoundaryToken token,
        DspRawInstructionBoundaryCancellation reason) noexcept {
        if (!authenticate(token)) {
            return false;
        }
        switch (reason) {
        case DspRawInstructionBoundaryCancellation::ExceptionUnwind:
        case DspRawInstructionBoundaryCancellation::ExplicitAbort:
        case DspRawInstructionBoundaryCancellation::UncommittedInstruction:
            break;
        default:
            sticky_fail(
                DspRawInstructionBoundaryFailure::InvalidCancellationReason);
            return false;
        }
        clear_active();
        if (reason ==
            DspRawInstructionBoundaryCancellation::UncommittedInstruction) {
            sticky_fail(
                DspRawInstructionBoundaryFailure::UncommittedInstruction);
        }
        return true;
    }

    [[nodiscard]] bool failed() const noexcept {
        return failure_ != DspRawInstructionBoundaryFailure::None;
    }
    [[nodiscard]] DspRawInstructionBoundaryFailure failure() const noexcept {
        return failure_;
    }
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] std::uint64_t committed_count() const noexcept {
        return committed_count_;
    }
    [[nodiscard]] std::span<const DspRawInstructionBoundaryRecord> records()
        const noexcept {
        return storage_.first(static_cast<std::size_t>(committed_count_));
    }

private:
    [[nodiscard]] bool authenticate(
        DspRawInstructionBoundaryToken token) noexcept {
        if (!active_ || token.owner_ != this || token != active_token_) {
            sticky_fail(
                DspRawInstructionBoundaryFailure::StaleTransactionToken);
            return false;
        }
        return true;
    }

    void clear_active() noexcept {
        active_ = false;
        active_token_ = {};
        active_sequence_ = 0u;
        active_identity_ = {};
        active_pre_state_ = {};
        active_memory_operands_ = {};
        active_memory_operand_count_ = 0u;
        active_context_ = nullptr;
        active_logical_stack_tracker_ = nullptr;
        active_logical_stack_epoch_ = 0u;
    }

    void sticky_fail(DspRawInstructionBoundaryFailure failure) noexcept {
        if (failure_ == DspRawInstructionBoundaryFailure::None) {
            failure_ = failure;
        }
    }

    std::span<DspRawInstructionBoundaryRecord> storage_{};
    DspRawInstructionBoundaryFailure failure_{
        DspRawInstructionBoundaryFailure::None};
    bool active_{};
    DspRawInstructionBoundaryToken active_token_{};
    std::uint64_t active_sequence_{};
    DspGeneratedInstructionIdentity active_identity_{};
    DspRawInstructionPreState active_pre_state_{};
    std::array<DspRawResolvedMemoryOperand, 3> active_memory_operands_{};
    std::uint8_t active_memory_operand_count_{};
    const DspContext* active_context_{};
    const DspLogicalStackDepthTracker* active_logical_stack_tracker_{};
    std::uint64_t active_logical_stack_epoch_{};
    std::uint64_t committed_count_{};
    std::uint64_t last_transaction_epoch_{};
};

[[nodiscard]] inline DspRawInstructionPreState
dsp_capture_raw_instruction_pre_state(const DspContext& context) noexcept {
    DspRawInstructionPreState state{};
    state.status_register = context.sr;
    state.control_register = context.cr;
    state.address_registers = context.ar;
    state.index_registers = context.ix;
    state.wrap_registers = context.wr;
    for (std::size_t index = 0u; index < state.stacks.size(); ++index) {
        state.stacks[index] = {
            context.st[index],
            context.stack_pointers[index],
        };
    }
    const auto* tracker = context.logical_stack_depth_tracker;
    if (tracker != nullptr && tracker->owner == &context &&
        tracker->epoch != 0u) {
        state.logical_stack_epoch = tracker->epoch;
        for (std::size_t index = 0u;
             index < state.logical_stack_depths.size();
             ++index) {
            if (tracker->availability[index] ==
                    DspLogicalStackDepthAvailability::Exact &&
                static_cast<std::uint8_t>(
                    tracker->depths[index] & kDspStackMask) ==
                    context.stack_pointers[index]) {
                state.logical_stack_depths[index] = {
                    DspRawLogicalStackDepthKind::Exact,
                    tracker->depths[index],
                };
            }
        }
    }
    if (context.host_external_interrupt == nullptr) {
        state.external_vector = {
            DspRawExternalVectorKind::Unavailable,
            0u,
        };
    } else {
        const auto vector = context.host_external_interrupt->load(
            std::memory_order_acquire);
        state.external_vector = {
            vector == kDspNoPendingExternalInterrupt
                ? DspRawExternalVectorKind::NoPendingVector
                : DspRawExternalVectorKind::PendingVector,
            vector,
        };
    }
    state.accelerator = DspRawAcceleratorState::Unavailable;
    return state;
}

class DspRawInstructionBoundaryScope {
public:
    DspRawInstructionBoundaryScope(
        DspRawInstructionBoundaryObserver* observer,
        const DspContext& context,
        const DspGeneratedInstructionIdentity& identity) noexcept
        : DspRawInstructionBoundaryScope(observer, context, identity, {}) {}

    DspRawInstructionBoundaryScope(
        DspRawInstructionBoundaryObserver* observer,
        const DspContext& context,
        const DspGeneratedInstructionIdentity& identity,
        const DspGeneratedMemoryOperandPlanSet& memory_plan) noexcept
        : observer_(observer) {
        // Product mode is the null branch: do not query exception state and do
        // not touch any raw DSP/host state unless a test observer is installed.
        if (observer_ != nullptr) {
            context_ = &context;
            uncaught_exceptions_at_begin_ = std::uncaught_exceptions();
            token_ = observer_->begin_tracked(
                identity,
                dsp_capture_raw_instruction_pre_state(context),
                context,
                memory_plan);
        }
    }

    DspRawInstructionBoundaryScope(
        const DspRawInstructionBoundaryScope&) = delete;
    DspRawInstructionBoundaryScope& operator=(
        const DspRawInstructionBoundaryScope&) = delete;
    DspRawInstructionBoundaryScope(
        DspRawInstructionBoundaryScope&&) = delete;
    DspRawInstructionBoundaryScope& operator=(
        DspRawInstructionBoundaryScope&&) = delete;

    ~DspRawInstructionBoundaryScope() {
        if (observer_ != nullptr && token_.valid()) {
            const auto reason =
                std::uncaught_exceptions() > uncaught_exceptions_at_begin_
                ? DspRawInstructionBoundaryCancellation::ExceptionUnwind
                : DspRawInstructionBoundaryCancellation::UncommittedInstruction;
            (void)observer_->cancel(token_, reason);
        }
    }

    [[nodiscard]] bool ready() const noexcept {
        return observer_ == nullptr || token_.valid();
    }

    [[nodiscard]] bool commit() noexcept {
        if (observer_ == nullptr) {
            return true;
        }
        if (!token_.valid()) {
            return false;
        }
        const bool committed = observer_->commit_tracked(token_, *context_);
        if (committed) {
            token_ = {};
        }
        return committed;
    }

    [[nodiscard]] bool abort() noexcept {
        if (observer_ == nullptr) {
            return true;
        }
        if (!token_.valid()) {
            return false;
        }
        const bool cancelled = observer_->cancel(
            token_, DspRawInstructionBoundaryCancellation::ExplicitAbort);
        if (cancelled) {
            token_ = {};
        }
        return cancelled;
    }

private:
    DspRawInstructionBoundaryObserver* observer_{};
    const DspContext* context_{};
    DspRawInstructionBoundaryToken token_{};
    int uncaught_exceptions_at_begin_{};
};

inline void dsp_raw_instruction_boundary_require_ready(
    DspContext& context,
    const DspRawInstructionBoundaryScope& scope) {
    if (!scope.ready()) {
        dsp_hard_trap_at_current(
            context, "DSP raw instruction observer begin failed");
    }
}

inline void dsp_raw_instruction_boundary_commit(
    DspContext& context,
    DspRawInstructionBoundaryScope& scope) {
    if (!scope.commit()) {
        dsp_hard_trap_at_current(
            context, "DSP raw instruction observer commit failed");
    }
}

}  // namespace galaxy
