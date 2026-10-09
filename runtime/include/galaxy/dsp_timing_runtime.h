#pragma once

#include "galaxy/runtime_timeline.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>

namespace galaxy::dsp_timing_runtime {

// These types define the runtime side of an exact timing-key retirement
// transaction. They are deliberately not installed in DspContext or called by
// generated code yet: doing so is only correct once every lowering path can
// supply every field represented here and commit immediately before its
// control transfer. In particular, none of these types is a production-timing
// capability bit or a worker-pacing mechanism.

enum class DspTimingBundleKind : std::uint8_t {
    Standalone,
    Parallel,
};

struct DspTimingInstructionIdentity {
    std::uint16_t fetch_address{};
    std::uint16_t opcode_word{};
    std::uint16_t immediate_word{};
    bool has_immediate{};
    DspTimingBundleKind bundle{DspTimingBundleKind::Standalone};
    std::uint16_t parallel_primary_word{};
    std::uint8_t parallel_extension{};
    std::uint8_t parallel_extension_mask{};

    bool operator==(const DspTimingInstructionIdentity&) const = default;
};

// Encoded architectural operand identity, independent of helper-call order or
// dynamic coalescing. A single parallel load/store is Primary. In dual
// operations Primary is the decoded address-register operand (AR0 for the
// accumulator-mid load/store family), while Secondary is AR3; LS is therefore
// primary-read/secondary-write and SL primary-write/secondary-read.
enum class DspTimingMemorySlot : std::uint8_t {
    Standalone = 0,
    ParallelPrimary = 1,
    ParallelSecondary = 2,
};

enum class DspTimingMemorySpace : std::uint8_t {
    Instruction,
    Data,
};

enum class DspTimingMemoryDirection : std::uint8_t {
    Read,
    Write,
};

struct DspTimingMemoryOperandAuthority {
    DspTimingMemorySpace space{DspTimingMemorySpace::Data};
    DspTimingMemoryDirection direction{DspTimingMemoryDirection::Read};

    bool operator==(const DspTimingMemoryOperandAuthority&) const = default;
};

inline constexpr std::uint8_t kDspTimingStandaloneMemoryBit = 1u << 0u;
inline constexpr std::uint8_t kDspTimingParallelPrimaryMemoryBit = 1u << 1u;
inline constexpr std::uint8_t kDspTimingParallelSecondaryMemoryBit = 1u << 2u;
inline constexpr std::uint8_t kDspTimingAllMemoryBits =
    kDspTimingStandaloneMemoryBit | kDspTimingParallelPrimaryMemoryBit |
    kDspTimingParallelSecondaryMemoryBit;

[[nodiscard]] constexpr std::uint8_t dsp_timing_memory_slot_bit(
    DspTimingMemorySlot slot) noexcept {
    switch (slot) {
    case DspTimingMemorySlot::Standalone:
        return kDspTimingStandaloneMemoryBit;
    case DspTimingMemorySlot::ParallelPrimary:
        return kDspTimingParallelPrimaryMemoryBit;
    case DspTimingMemorySlot::ParallelSecondary:
        return kDspTimingParallelSecondaryMemoryBit;
    }
    return 0u;
}

inline constexpr std::uint32_t kDspTimingRuntimePlanContractVersion = 2u;
inline constexpr std::size_t kDspTimingResolverIdentityBytes = 20u;

// This is install-time validated metadata, not a runtime decoder result. The
// Rust resolver derives it from the decoded instruction and binds it into the
// authenticated plan identity. Runtime validation below checks internal
// consistency only; exact opcode authorization still requires lookup of that
// resolver identity in the authenticated timing contract.
enum class DspTimingInstructionSemantic : std::uint8_t {
    Ordinary,
    ConditionalAlways,
    ConditionalDynamic,
    LoopAlwaysArmed,
    LoopAlwaysSkipped,
    LoopDynamic,
    Halt,
};

enum class DspTimingHardwareLoopSite : std::uint8_t {
    NotLoopEnd,
    MayResolveLoopEnd,
};

struct DspTimingInstallPlanBinding {
    std::uint32_t contract_version{};
    std::array<std::uint8_t, kDspTimingResolverIdentityBytes>
        resolver_plan_identity{};
    std::uint8_t instruction_word_count{};
    DspTimingBundleKind validated_bundle{DspTimingBundleKind::Standalone};
    DspTimingInstructionSemantic instruction_semantic{
        DspTimingInstructionSemantic::Ordinary};
    DspTimingHardwareLoopSite hardware_loop_site{
        DspTimingHardwareLoopSite::NotLoopEnd};
};

// Exact entry obtained from the independently authenticated generated timing
// table. The 20-byte value is a deterministic SHA-1 table identity, never an
// artifact-integrity primitive or authentication mechanism. The future staged
// producer/signer and generated-source hash remain the security authority;
// this entry only makes the runtime comparison full-width and non-optional.
struct DspTimingAuthorizedPlanEntry {
    std::uint32_t contract_version{};
    std::array<std::uint8_t, kDspTimingResolverIdentityBytes>
        resolver_plan_identity{};
};

struct DspTimingInstructionPlan {
    DspTimingInstructionIdentity identity{};
    DspTimingInstallPlanBinding binding{};
    // The install-time Rust resolver derives every encoded architectural
    // memory-operand slot from the decoded instruction and binds this mask into
    // its authenticated identity. A commit succeeds only after every derived
    // slot has supplied one exact address and direction. Slots are operands,
    // not host-helper call counts; dynamically coalesced dual loads still have
    // primary and secondary encoded slots.
    std::uint8_t expected_memory_slots{};
    // Canonical slot-indexed decoder authority. Presence must exactly equal
    // expected_memory_slots; every record must match its authorized space and
    // direction before an address can be classified.
    std::array<std::optional<DspTimingMemoryOperandAuthority>, 3>
        memory_operands{};
    // Rust lowering declares the complete set of outcomes valid for the
    // decoded instruction. Runtime code does not decode opcodes and therefore
    // cannot safely infer this set.
    std::uint8_t allowed_instruction_outcomes{};
    std::uint8_t allowed_hardware_loop_outcomes{};
};

struct DspTimingPreState {
    std::uint16_t fetch_address{};
    std::uint16_t status_register{};
    std::uint16_t control_register{};
    std::array<std::uint16_t, 4> address_registers{};
    std::array<std::int16_t, 4> index_registers{};
    std::array<std::uint16_t, 4> wrap_registers{};
    // These are logical depths required by timing-contract schema v2, not the
    // wrapping hardware-stack pointers currently stored in DspContext. Zero is
    // therefore exact empty/nonempty evidence, but the reviewed upper bound of
    // a logical depth is unresolved; this foundation deliberately does not
    // guess that it is 31 or 32. Stack 0 carries both call return addresses
    // and the hardware-loop body target; a hardware-loop edge requires exact
    // nonzero logical depths for stacks 0, 2, and 3. Until exact logical-depth tracking exists,
    // generated code cannot construct this pre-state and production timing
    // remains unavailable. Indices are call, data, loop-address, loop-counter.
    std::array<std::uint8_t, 4> stack_depths{};
    bool external_interrupt_pending{};
    bool accelerator_interrupt_pending{};
    std::optional<DspTimingInstructionIdentity> predecessor{};
};

enum class DspTimingMemoryRegion : std::uint8_t {
    InstructionIram,
    InstructionIrom,
    DataDram,
    CoefficientRom,
    InterfaceRegister,
    DataOpenBus,
    InstructionOpenBus,
};

struct DspTimingMemoryAccess {
    DspTimingMemorySlot slot{DspTimingMemorySlot::Standalone};
    DspTimingMemorySpace space{DspTimingMemorySpace::Data};
    DspTimingMemoryDirection direction{DspTimingMemoryDirection::Read};
    std::uint16_t address{};
    DspTimingMemoryRegion region{DspTimingMemoryRegion::DataDram};
};

enum class DspTimingInstructionOutcome : std::uint8_t {
    Completed,
    ConditionSatisfied,
    ConditionNotSatisfied,
    LoopArmed,
    ZeroCountLoopSkipped,
    Halted,
};

inline constexpr std::uint8_t kDspTimingCompletedOutcomeBit = 1u << 0u;
inline constexpr std::uint8_t kDspTimingConditionSatisfiedOutcomeBit = 1u << 1u;
inline constexpr std::uint8_t kDspTimingConditionNotSatisfiedOutcomeBit =
    1u << 2u;
inline constexpr std::uint8_t kDspTimingLoopArmedOutcomeBit = 1u << 3u;
inline constexpr std::uint8_t kDspTimingZeroCountLoopSkippedOutcomeBit =
    1u << 4u;
inline constexpr std::uint8_t kDspTimingHaltedOutcomeBit = 1u << 5u;
inline constexpr std::uint8_t kDspTimingAllInstructionOutcomeBits =
    kDspTimingCompletedOutcomeBit | kDspTimingConditionSatisfiedOutcomeBit |
    kDspTimingConditionNotSatisfiedOutcomeBit |
    kDspTimingLoopArmedOutcomeBit |
    kDspTimingZeroCountLoopSkippedOutcomeBit | kDspTimingHaltedOutcomeBit;

[[nodiscard]] constexpr std::uint8_t dsp_timing_instruction_outcome_bit(
    DspTimingInstructionOutcome outcome) noexcept {
    switch (outcome) {
    case DspTimingInstructionOutcome::Completed:
        return kDspTimingCompletedOutcomeBit;
    case DspTimingInstructionOutcome::ConditionSatisfied:
        return kDspTimingConditionSatisfiedOutcomeBit;
    case DspTimingInstructionOutcome::ConditionNotSatisfied:
        return kDspTimingConditionNotSatisfiedOutcomeBit;
    case DspTimingInstructionOutcome::LoopArmed:
        return kDspTimingLoopArmedOutcomeBit;
    case DspTimingInstructionOutcome::ZeroCountLoopSkipped:
        return kDspTimingZeroCountLoopSkippedOutcomeBit;
    case DspTimingInstructionOutcome::Halted:
        return kDspTimingHaltedOutcomeBit;
    }
    return 0u;
}

enum class DspTimingConditionOutcome : std::uint8_t {
    NotApplicable,
    Satisfied,
    NotSatisfied,
};

enum class DspTimingHardwareLoopOutcome : std::uint8_t {
    None,
    BackEdgeTaken,
    Exited,
};

inline constexpr std::uint8_t kDspTimingNoHardwareLoopOutcomeBit = 1u << 0u;
inline constexpr std::uint8_t kDspTimingHardwareLoopBackEdgeOutcomeBit =
    1u << 1u;
inline constexpr std::uint8_t kDspTimingHardwareLoopExitedOutcomeBit = 1u << 2u;
inline constexpr std::uint8_t kDspTimingAllHardwareLoopOutcomeBits =
    kDspTimingNoHardwareLoopOutcomeBit |
    kDspTimingHardwareLoopBackEdgeOutcomeBit |
    kDspTimingHardwareLoopExitedOutcomeBit;

[[nodiscard]] constexpr std::uint8_t dsp_timing_hardware_loop_outcome_bit(
    DspTimingHardwareLoopOutcome outcome) noexcept {
    switch (outcome) {
    case DspTimingHardwareLoopOutcome::None:
        return kDspTimingNoHardwareLoopOutcomeBit;
    case DspTimingHardwareLoopOutcome::BackEdgeTaken:
        return kDspTimingHardwareLoopBackEdgeOutcomeBit;
    case DspTimingHardwareLoopOutcome::Exited:
        return kDspTimingHardwareLoopExitedOutcomeBit;
    }
    return 0u;
}

enum class DspTimingInterruptOutcome : std::uint8_t {
    None,
    AcceptedAfterInstruction,
};

struct DspTimingPostOutcome {
    DspTimingInstructionOutcome instruction{
        DspTimingInstructionOutcome::Completed};
    DspTimingConditionOutcome condition{
        DspTimingConditionOutcome::NotApplicable};
    DspTimingHardwareLoopOutcome hardware_loop{
        DspTimingHardwareLoopOutcome::None};
    DspTimingInterruptOutcome interrupt{DspTimingInterruptOutcome::None};
};

struct DspTimingRetirementRecord {
    std::uint64_t retirement_sequence{};
    DspTimingInstructionPlan plan{};
    DspTimingPreState pre_state{};
    std::array<DspTimingMemoryAccess, 3> memory_accesses{};
    std::uint8_t memory_access_count{};
    DspTimingPostOutcome post_outcome{};
};

class DspTimingTransactionToken {
public:
    DspTimingTransactionToken() noexcept = default;

    [[nodiscard]] std::uint64_t epoch_for_diagnostics() const noexcept {
        return epoch_;
    }
    [[nodiscard]] bool valid() const noexcept {
        return owner_ != nullptr && epoch_ != 0u;
    }

    bool operator==(const DspTimingTransactionToken&) const = default;

private:
    explicit DspTimingTransactionToken(
        const void* owner,
        std::uint64_t epoch) noexcept
        : owner_(owner), epoch_(epoch) {}

    const void* owner_{};
    std::uint64_t epoch_{};

    friend class DspTimingRetirementLedger;
};

enum class DspTimingCancellationReason : std::uint8_t {
    ExceptionUnwind,
    ExplicitAbort,
    UncommittedInstruction,
};

enum class DspTimingRetirementFailure : std::uint8_t {
    None,
    InvalidInstructionIdentity,
    InvalidInstructionPlan,
    UnauthorizedInstructionPlan,
    InvalidPreState,
    NestedBegin,
    ResetWhileInstructionActive,
    TransactionEpochOverflow,
    StaleTransactionToken,
    NoActiveInstruction,
    MemoryCaptureUnavailable,
    UnexpectedMemorySlot,
    DuplicateMemorySlot,
    InvalidMemorySpace,
    InvalidMemoryDirection,
    MemorySpaceMismatch,
    MemoryDirectionMismatch,
    InvalidMemoryAddress,
    IncompleteMemoryAccesses,
    InvalidPostOutcome,
    InvalidCancellationReason,
    UncommittedInstruction,
    InterruptAcceptedWithoutPendingSource,
    RetirementCountOverflow,
};

struct DspTimingRetirementState {
    std::uint64_t retired_instructions{};
    // The full identity, rather than only a PC, is retained so the next begin
    // transaction can prove its exact predecessor without decoding at runtime.
    std::optional<DspTimingInstructionIdentity> last_retired_instruction{};
};

namespace detail {

[[nodiscard]] constexpr bool valid_instruction_address(
    std::uint16_t address) noexcept {
    const auto region = static_cast<std::uint16_t>(address & 0xf000u);
    return region == 0x0000u || region == 0x8000u;
}

[[nodiscard]] constexpr bool valid_instruction_identity(
    const DspTimingInstructionIdentity& identity) noexcept {
    if (!valid_instruction_address(identity.fetch_address) ||
        (!identity.has_immediate && identity.immediate_word != 0u)) {
        return false;
    }
    switch (identity.bundle) {
    case DspTimingBundleKind::Standalone:
        return identity.parallel_primary_word == 0u &&
            identity.parallel_extension == 0u &&
            identity.parallel_extension_mask == 0u;
    case DspTimingBundleKind::Parallel: {
        const auto expected_extension_mask =
            static_cast<std::uint8_t>(
                identity.opcode_word >> 12u == 0x3u ? 0x7fu : 0xffu);
        if (identity.has_immediate ||
            identity.parallel_extension_mask != expected_extension_mask) {
            return false;
        }
        const auto mask =
            static_cast<std::uint16_t>(identity.parallel_extension_mask);
        return identity.parallel_primary_word ==
                static_cast<std::uint16_t>(identity.opcode_word & ~mask) &&
            identity.parallel_extension ==
                static_cast<std::uint8_t>(identity.opcode_word & mask);
    }
    }
    return false;
}

[[nodiscard]] constexpr bool resolver_identity_is_bound(
    const DspTimingInstallPlanBinding& binding) noexcept {
    for (const auto byte : binding.resolver_plan_identity) {
        if (byte != 0u) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] constexpr std::uint8_t instruction_outcomes_for_semantic(
    DspTimingInstructionSemantic semantic) noexcept {
    switch (semantic) {
    case DspTimingInstructionSemantic::Ordinary:
        return kDspTimingCompletedOutcomeBit;
    case DspTimingInstructionSemantic::ConditionalAlways:
        return kDspTimingConditionSatisfiedOutcomeBit;
    case DspTimingInstructionSemantic::ConditionalDynamic:
        return kDspTimingConditionSatisfiedOutcomeBit |
            kDspTimingConditionNotSatisfiedOutcomeBit;
    case DspTimingInstructionSemantic::LoopAlwaysArmed:
        return kDspTimingLoopArmedOutcomeBit;
    case DspTimingInstructionSemantic::LoopAlwaysSkipped:
        return kDspTimingZeroCountLoopSkippedOutcomeBit;
    case DspTimingInstructionSemantic::LoopDynamic:
        return kDspTimingLoopArmedOutcomeBit |
            kDspTimingZeroCountLoopSkippedOutcomeBit;
    case DspTimingInstructionSemantic::Halt:
        return kDspTimingHaltedOutcomeBit;
    }
    return 0u;
}

[[nodiscard]] constexpr std::uint8_t hardware_loop_outcomes_for_site(
    DspTimingHardwareLoopSite site) noexcept {
    switch (site) {
    case DspTimingHardwareLoopSite::NotLoopEnd:
        return kDspTimingNoHardwareLoopOutcomeBit;
    case DspTimingHardwareLoopSite::MayResolveLoopEnd:
        return kDspTimingAllHardwareLoopOutcomeBits;
    }
    return 0u;
}

[[nodiscard]] constexpr bool valid_instruction_plan(
    const DspTimingInstructionPlan& plan) noexcept {
    if (!valid_instruction_identity(plan.identity) ||
        plan.binding.contract_version !=
            kDspTimingRuntimePlanContractVersion ||
        !resolver_identity_is_bound(plan.binding) ||
        (plan.binding.instruction_word_count != 1u &&
         plan.binding.instruction_word_count != 2u) ||
        plan.identity.has_immediate !=
            (plan.binding.instruction_word_count == 2u) ||
        plan.binding.validated_bundle != plan.identity.bundle ||
        (plan.expected_memory_slots & ~kDspTimingAllMemoryBits) != 0u ||
        instruction_outcomes_for_semantic(
            plan.binding.instruction_semantic) == 0u ||
        plan.allowed_instruction_outcomes !=
            instruction_outcomes_for_semantic(
                plan.binding.instruction_semantic) ||
        hardware_loop_outcomes_for_site(
            plan.binding.hardware_loop_site) == 0u ||
        plan.allowed_hardware_loop_outcomes !=
            hardware_loop_outcomes_for_site(
                plan.binding.hardware_loop_site) ||
        (plan.binding.instruction_semantic ==
             DspTimingInstructionSemantic::Halt &&
         plan.binding.hardware_loop_site !=
             DspTimingHardwareLoopSite::NotLoopEnd)) {
        return false;
    }
    std::uint8_t authority_mask{};
    for (std::size_t index = 0u; index < plan.memory_operands.size(); ++index) {
        if (!plan.memory_operands[index].has_value()) {
            continue;
        }
        const auto authority = *plan.memory_operands[index];
        switch (authority.space) {
        case DspTimingMemorySpace::Instruction:
            if (authority.direction != DspTimingMemoryDirection::Read) {
                return false;
            }
            break;
        case DspTimingMemorySpace::Data:
            break;
        default:
            return false;
        }
        switch (authority.direction) {
        case DspTimingMemoryDirection::Read:
        case DspTimingMemoryDirection::Write:
            break;
        default:
            return false;
        }
        authority_mask = static_cast<std::uint8_t>(
            authority_mask | static_cast<std::uint8_t>(1u << index));
    }
    if (authority_mask != plan.expected_memory_slots) {
        return false;
    }
    switch (plan.identity.bundle) {
    case DspTimingBundleKind::Standalone:
        return (plan.expected_memory_slots &
                (kDspTimingParallelPrimaryMemoryBit |
                 kDspTimingParallelSecondaryMemoryBit)) == 0u;
    case DspTimingBundleKind::Parallel:
        return plan.binding.instruction_word_count == 1u &&
            (plan.expected_memory_slots &
             kDspTimingStandaloneMemoryBit) == 0u &&
            ((plan.expected_memory_slots &
              kDspTimingParallelSecondaryMemoryBit) == 0u ||
             (plan.expected_memory_slots &
              kDspTimingParallelPrimaryMemoryBit) != 0u);
    }
    return false;
}

[[nodiscard]] constexpr bool plan_matches_authorized_entry(
    const DspTimingInstructionPlan& plan,
    const DspTimingAuthorizedPlanEntry& authorized_entry) noexcept {
    return authorized_entry.contract_version ==
            plan.binding.contract_version &&
        authorized_entry.resolver_plan_identity ==
            plan.binding.resolver_plan_identity;
}

[[nodiscard]] constexpr bool valid_pre_state(
    const DspTimingInstructionPlan& plan,
    const DspTimingPreState& pre_state) noexcept {
    return pre_state.fetch_address == plan.identity.fetch_address &&
        (!pre_state.predecessor.has_value() ||
         valid_instruction_identity(*pre_state.predecessor));
}

[[nodiscard]] constexpr std::optional<DspTimingMemoryRegion>
memory_region_for(
    DspTimingMemorySpace space,
    DspTimingMemoryDirection direction,
    std::uint16_t address) noexcept {
    const auto region = static_cast<std::uint16_t>(address & 0xf000u);
    if (space == DspTimingMemorySpace::Instruction) {
        if (direction != DspTimingMemoryDirection::Read) {
            return std::nullopt;
        }
        if (address <= 0x0fffu) {
            return DspTimingMemoryRegion::InstructionIram;
        }
        if (region == 0x8000u) {
            return DspTimingMemoryRegion::InstructionIrom;
        }
        return DspTimingMemoryRegion::InstructionOpenBus;
    }
    if (space != DspTimingMemorySpace::Data) {
        return std::nullopt;
    }
    if (region == 0x0000u) {
        return DspTimingMemoryRegion::DataDram;
    }
    if (region == 0x1000u &&
        direction == DspTimingMemoryDirection::Read) {
        return DspTimingMemoryRegion::CoefficientRom;
    }
    if (region == 0xf000u) {
        return DspTimingMemoryRegion::InterfaceRegister;
    }
    // Ordinary reads from other data regions return zero (open bus), while
    // ordinary writes are dropped. Direction remains separate key material.
    return DspTimingMemoryRegion::DataOpenBus;
}

[[nodiscard]] constexpr bool valid_post_outcome(
    const DspTimingInstructionPlan& plan,
    const DspTimingPreState& pre_state,
    const DspTimingPostOutcome& outcome,
    DspTimingRetirementFailure& failure) noexcept {
    switch (outcome.instruction) {
    case DspTimingInstructionOutcome::ConditionSatisfied:
        if (outcome.condition != DspTimingConditionOutcome::Satisfied) {
            failure = DspTimingRetirementFailure::InvalidPostOutcome;
            return false;
        }
        break;
    case DspTimingInstructionOutcome::ConditionNotSatisfied:
        if (outcome.condition != DspTimingConditionOutcome::NotSatisfied) {
            failure = DspTimingRetirementFailure::InvalidPostOutcome;
            return false;
        }
        break;
    case DspTimingInstructionOutcome::Completed:
    case DspTimingInstructionOutcome::LoopArmed:
    case DspTimingInstructionOutcome::ZeroCountLoopSkipped:
    case DspTimingInstructionOutcome::Halted:
        if (outcome.condition != DspTimingConditionOutcome::NotApplicable) {
            failure = DspTimingRetirementFailure::InvalidPostOutcome;
            return false;
        }
        break;
    default:
        failure = DspTimingRetirementFailure::InvalidPostOutcome;
        return false;
    }

    switch (outcome.hardware_loop) {
    case DspTimingHardwareLoopOutcome::None:
        break;
    case DspTimingHardwareLoopOutcome::BackEdgeTaken:
    case DspTimingHardwareLoopOutcome::Exited:
        if (plan.binding.hardware_loop_site !=
                DspTimingHardwareLoopSite::MayResolveLoopEnd ||
            plan.binding.instruction_semantic ==
                DspTimingInstructionSemantic::Halt ||
            pre_state.stack_depths[0] == 0u ||
            pre_state.stack_depths[2] == 0u ||
            pre_state.stack_depths[3] == 0u) {
            failure = DspTimingRetirementFailure::InvalidPostOutcome;
            return false;
        }
        break;
    default:
        failure = DspTimingRetirementFailure::InvalidPostOutcome;
        return false;
    }

    switch (outcome.interrupt) {
    case DspTimingInterruptOutcome::None:
        return true;
    case DspTimingInterruptOutcome::AcceptedAfterInstruction:
        if (pre_state.external_interrupt_pending ||
            pre_state.accelerator_interrupt_pending) {
            return true;
        }
        failure =
            DspTimingRetirementFailure::InterruptAcceptedWithoutPendingSource;
        return false;
    default:
        failure = DspTimingRetirementFailure::InvalidPostOutcome;
        return false;
    }
}

}  // namespace detail

class DspTimingRetirementLedger {
public:
    explicit DspTimingRetirementLedger(
        std::uint64_t completed_transaction_epoch = 0u) noexcept
        : last_transaction_epoch_(completed_transaction_epoch) {}

    DspTimingRetirementLedger(const DspTimingRetirementLedger&) = delete;
    DspTimingRetirementLedger& operator=(
        const DspTimingRetirementLedger&) = delete;
    DspTimingRetirementLedger(DspTimingRetirementLedger&&) = delete;
    DspTimingRetirementLedger& operator=(
        DspTimingRetirementLedger&&) = delete;

    // This is a fresh-ledger reset only. A nonzero retirement count and prior
    // identity are provenance-bearing checkpoint data, not structural state;
    // no restore API is exposed until a provider authenticates the count and
    // the full previously decoded plan/table identity.
    [[nodiscard]] bool reset() noexcept {
        if (active_.has_value()) {
            sticky_fail(
                DspTimingRetirementFailure::ResetWhileInstructionActive);
            return false;
        }
        state_ = {};
        failure_ = DspTimingRetirementFailure::None;
        memory_by_slot_ = {};
        recorded_memory_slots_ = 0u;
        return true;
    }

    [[nodiscard]] std::optional<DspTimingTransactionToken> begin(
        const DspTimingInstructionPlan& plan,
        const DspTimingAuthorizedPlanEntry& authorized_entry,
        const DspTimingPreState& pre_state) noexcept {
        if (failure_ != DspTimingRetirementFailure::None) {
            return std::nullopt;
        }
        if (active_.has_value()) {
            sticky_fail(DspTimingRetirementFailure::NestedBegin);
            return std::nullopt;
        }
        if (!detail::valid_instruction_identity(plan.identity)) {
            sticky_fail(
                DspTimingRetirementFailure::InvalidInstructionIdentity);
            return std::nullopt;
        }
        if (!detail::valid_instruction_plan(plan)) {
            sticky_fail(DspTimingRetirementFailure::InvalidInstructionPlan);
            return std::nullopt;
        }
        if (!detail::plan_matches_authorized_entry(
                plan, authorized_entry)) {
            sticky_fail(
                DspTimingRetirementFailure::UnauthorizedInstructionPlan);
            return std::nullopt;
        }
        if (!detail::valid_pre_state(plan, pre_state) ||
            pre_state.predecessor != state_.last_retired_instruction) {
            sticky_fail(DspTimingRetirementFailure::InvalidPreState);
            return std::nullopt;
        }
        if (last_transaction_epoch_ ==
            std::numeric_limits<std::uint64_t>::max()) {
            sticky_fail(
                DspTimingRetirementFailure::TransactionEpochOverflow);
            return std::nullopt;
        }
        const DspTimingTransactionToken token{
            this, ++last_transaction_epoch_};
        active_.emplace(ActiveInstruction{token, plan, pre_state});
        memory_by_slot_ = {};
        recorded_memory_slots_ = 0u;
        return token;
    }

    [[nodiscard]] bool record_memory(
        DspTimingTransactionToken token,
        DspTimingMemorySlot slot,
        DspTimingMemorySpace space,
        DspTimingMemoryDirection direction,
        std::uint16_t address) noexcept {
        if (!authenticate(token)) {
            return false;
        }
        if (failure_ != DspTimingRetirementFailure::None) {
            return false;
        }
        const auto slot_bit = dsp_timing_memory_slot_bit(slot);
        if (slot_bit == 0u ||
            (active_->plan.expected_memory_slots & slot_bit) == 0u) {
            sticky_fail(DspTimingRetirementFailure::UnexpectedMemorySlot);
            return false;
        }
        if ((recorded_memory_slots_ & slot_bit) != 0u) {
            sticky_fail(DspTimingRetirementFailure::DuplicateMemorySlot);
            return false;
        }
        switch (space) {
        case DspTimingMemorySpace::Instruction:
        case DspTimingMemorySpace::Data:
            break;
        default:
            sticky_fail(DspTimingRetirementFailure::InvalidMemorySpace);
            return false;
        }
        switch (direction) {
        case DspTimingMemoryDirection::Read:
        case DspTimingMemoryDirection::Write:
            break;
        default:
            sticky_fail(DspTimingRetirementFailure::InvalidMemoryDirection);
            return false;
        }
        if (space == DspTimingMemorySpace::Instruction &&
            direction != DspTimingMemoryDirection::Read) {
            sticky_fail(DspTimingRetirementFailure::InvalidMemoryDirection);
            return false;
        }
        const auto index = static_cast<std::size_t>(slot);
        if (index >= memory_by_slot_.size()) {
            sticky_fail(DspTimingRetirementFailure::UnexpectedMemorySlot);
            return false;
        }
        const auto expected = active_->plan.memory_operands[index];
        if (!expected.has_value()) {
            sticky_fail(DspTimingRetirementFailure::UnexpectedMemorySlot);
            return false;
        }
        if (space != expected->space) {
            sticky_fail(DspTimingRetirementFailure::MemorySpaceMismatch);
            return false;
        }
        if (direction != expected->direction) {
            sticky_fail(DspTimingRetirementFailure::MemoryDirectionMismatch);
            return false;
        }
        const auto region = detail::memory_region_for(space, direction, address);
        if (!region.has_value()) {
            sticky_fail(DspTimingRetirementFailure::InvalidMemoryAddress);
            return false;
        }
        memory_by_slot_[index] =
            DspTimingMemoryAccess{slot, space, direction, address, *region};
        recorded_memory_slots_ =
            static_cast<std::uint8_t>(recorded_memory_slots_ | slot_bit);
        return true;
    }

    [[nodiscard]] bool mark_memory_capture_unavailable(
        DspTimingTransactionToken token) noexcept {
        if (!authenticate(token)) {
            return false;
        }
        if (failure_ != DspTimingRetirementFailure::None) {
            return false;
        }
        sticky_fail(DspTimingRetirementFailure::MemoryCaptureUnavailable);
        return false;
    }

    [[nodiscard]] std::optional<DspTimingRetirementRecord> commit(
        DspTimingTransactionToken token,
        const DspTimingPostOutcome& post_outcome) noexcept {
        if (!authenticate(token)) {
            return std::nullopt;
        }
        if (failure_ != DspTimingRetirementFailure::None) {
            return std::nullopt;
        }
        if (recorded_memory_slots_ != active_->plan.expected_memory_slots) {
            sticky_fail(
                DspTimingRetirementFailure::IncompleteMemoryAccesses);
            return std::nullopt;
        }
        const auto outcome_bit =
            dsp_timing_instruction_outcome_bit(post_outcome.instruction);
        if (outcome_bit == 0u ||
            (active_->plan.allowed_instruction_outcomes & outcome_bit) == 0u) {
            sticky_fail(DspTimingRetirementFailure::InvalidPostOutcome);
            return std::nullopt;
        }
        const auto hardware_loop_outcome_bit =
            dsp_timing_hardware_loop_outcome_bit(
                post_outcome.hardware_loop);
        if (hardware_loop_outcome_bit == 0u ||
            (active_->plan.allowed_hardware_loop_outcomes &
             hardware_loop_outcome_bit) == 0u) {
            sticky_fail(DspTimingRetirementFailure::InvalidPostOutcome);
            return std::nullopt;
        }
        DspTimingRetirementFailure outcome_failure{
            DspTimingRetirementFailure::None};
        if (!detail::valid_post_outcome(
                active_->plan,
                active_->pre_state,
                post_outcome,
                outcome_failure)) {
            sticky_fail(outcome_failure);
            return std::nullopt;
        }
        if (state_.retired_instructions ==
            std::numeric_limits<std::uint64_t>::max()) {
            sticky_fail(
                DspTimingRetirementFailure::RetirementCountOverflow);
            return std::nullopt;
        }

        DspTimingRetirementRecord record{};
        record.retirement_sequence = state_.retired_instructions + 1u;
        record.plan = active_->plan;
        record.pre_state = active_->pre_state;
        for (std::size_t index = 0; index < memory_by_slot_.size(); ++index) {
            if (!memory_by_slot_[index].has_value()) {
                continue;
            }
            record.memory_accesses[record.memory_access_count] =
                *memory_by_slot_[index];
            ++record.memory_access_count;
        }
        record.post_outcome = post_outcome;

        state_.retired_instructions = record.retirement_sequence;
        state_.last_retired_instruction = active_->plan.identity;
        clear_active();
        return record;
    }

    // Only the matching token can end a live transaction. Exception unwind and
    // explicit abort preserve the prior retirement identity; ordinary scope
    // destruction is a forgotten commit and poisons the ledger.
    [[nodiscard]] bool cancel(
        DspTimingTransactionToken token,
        DspTimingCancellationReason reason) noexcept {
        if (!authenticate(token)) {
            return false;
        }
        switch (reason) {
        case DspTimingCancellationReason::ExceptionUnwind:
        case DspTimingCancellationReason::ExplicitAbort:
        case DspTimingCancellationReason::UncommittedInstruction:
            break;
        default:
            sticky_fail(
                DspTimingRetirementFailure::InvalidCancellationReason);
            return false;
        }
        clear_active();
        if (reason ==
            DspTimingCancellationReason::UncommittedInstruction) {
            sticky_fail(
                DspTimingRetirementFailure::UncommittedInstruction);
        }
        return true;
    }

    [[nodiscard]] DspTimingRetirementFailure failure() const noexcept {
        return failure_;
    }
    [[nodiscard]] bool failed() const noexcept {
        return failure_ != DspTimingRetirementFailure::None;
    }
    [[nodiscard]] bool active() const noexcept {
        return active_.has_value();
    }
    [[nodiscard]] std::optional<DspTimingTransactionToken> active_token()
        const noexcept {
        return active_.has_value()
            ? std::optional<DspTimingTransactionToken>{active_->token}
            : std::nullopt;
    }
    [[nodiscard]] DspTimingRetirementState state() const noexcept {
        return state_;
    }

private:
    struct ActiveInstruction {
        DspTimingTransactionToken token{};
        DspTimingInstructionPlan plan{};
        DspTimingPreState pre_state{};
    };

    [[nodiscard]] bool authenticate(
        DspTimingTransactionToken token) noexcept {
        if (token.owner_ != this || !active_.has_value() ||
            token != active_->token) {
            sticky_fail(
                DspTimingRetirementFailure::StaleTransactionToken);
            return false;
        }
        return true;
    }

    void clear_active() noexcept {
        active_.reset();
        memory_by_slot_ = {};
        recorded_memory_slots_ = 0u;
    }

    void sticky_fail(DspTimingRetirementFailure failure) noexcept {
        if (failure_ == DspTimingRetirementFailure::None) {
            failure_ = failure;
        }
    }

    DspTimingRetirementState state_{};
    DspTimingRetirementFailure failure_{DspTimingRetirementFailure::None};
    std::optional<ActiveInstruction> active_{};
    std::array<std::optional<DspTimingMemoryAccess>, 3> memory_by_slot_{};
    std::uint8_t recorded_memory_slots_{};
    std::uint64_t last_transaction_epoch_{};
};

class DspTimingInstructionScope {
public:
    DspTimingInstructionScope(
        DspTimingRetirementLedger& ledger,
        const DspTimingInstructionPlan& plan,
        const DspTimingAuthorizedPlanEntry& authorized_entry,
        const DspTimingPreState& pre_state) noexcept
        : ledger_(&ledger),
          token_(ledger.begin(plan, authorized_entry, pre_state)),
          uncaught_exceptions_at_begin_(std::uncaught_exceptions()) {}

    DspTimingInstructionScope(const DspTimingInstructionScope&) = delete;
    DspTimingInstructionScope& operator=(const DspTimingInstructionScope&) =
        delete;
    DspTimingInstructionScope(DspTimingInstructionScope&&) = delete;
    DspTimingInstructionScope& operator=(DspTimingInstructionScope&&) = delete;

    ~DspTimingInstructionScope() {
        if (token_.has_value()) {
            const auto reason =
                std::uncaught_exceptions() > uncaught_exceptions_at_begin_
                ? DspTimingCancellationReason::ExceptionUnwind
                : DspTimingCancellationReason::UncommittedInstruction;
            (void)ledger_->cancel(*token_, reason);
        }
    }

    [[nodiscard]] bool began() const noexcept {
        return token_.has_value();
    }
    [[nodiscard]] std::optional<DspTimingTransactionToken> token() const
        noexcept {
        return token_;
    }

    [[nodiscard]] bool record_memory(
        DspTimingMemorySlot slot,
        DspTimingMemorySpace space,
        DspTimingMemoryDirection direction,
        std::uint16_t address) noexcept {
        if (!token_.has_value()) {
            return false;
        }
        return ledger_->record_memory(
            *token_, slot, space, direction, address);
    }

    [[nodiscard]] bool mark_memory_capture_unavailable() noexcept {
        if (!token_.has_value()) {
            return false;
        }
        return ledger_->mark_memory_capture_unavailable(*token_);
    }

    [[nodiscard]] std::optional<DspTimingRetirementRecord> commit(
        const DspTimingPostOutcome& post_outcome) noexcept {
        if (!token_.has_value()) {
            return std::nullopt;
        }
        auto record = ledger_->commit(*token_, post_outcome);
        if (record.has_value()) {
            token_.reset();
        }
        return record;
    }

    [[nodiscard]] bool abort() noexcept {
        if (!token_.has_value()) {
            return false;
        }
        const bool cancelled = ledger_->cancel(
            *token_, DspTimingCancellationReason::ExplicitAbort);
        if (cancelled) {
            token_.reset();
        }
        return cancelled;
    }

private:
    DspTimingRetirementLedger* ledger_{};
    std::optional<DspTimingTransactionToken> token_{};
    int uncaught_exceptions_at_begin_{};
};

// Pure checked arithmetic for converting an already-resolved positive DSP
// cycle grant into the RuntimeTimeline tick domain. This neither reads the host
// clock nor publishes a deadline, and is intentionally not connected to the
// native DSP worker. The caller must authenticate the clock and timing rule.
enum class DspCycleGrantFailure : std::uint8_t {
    None,
    InvalidClock,
    ZeroCycles,
    TotalCycleOverflow,
    TimelineOverflow,
    Aborted,
};

struct DspCycleGrantState {
    std::uint64_t dsp_clock_hz{};
    std::uint64_t total_dsp_cycles{};
    // RuntimeTimeline tick corresponding to total_dsp_cycles == 0. Arithmetic
    // consistency can be checked against an expected immutable anchor, but
    // consistency is not checkpoint provenance or restore authority.
    std::uint64_t timeline_anchor_ticks{};
    std::uint64_t timeline_ticks{};
    // Numerator remainder in timeline-ticks-per-second units. It is always
    // strictly less than dsp_clock_hz.
    std::uint64_t fractional_remainder{};

    bool operator==(const DspCycleGrantState&) const = default;
};

inline constexpr bool kDspCycleAuthenticatedCheckpointRestoreAvailable =
    false;

enum class DspCycleCheckpointArithmeticFailure : std::uint8_t {
    None,
    InvalidExpectedClock,
    ClockMismatch,
    AnchorMismatch,
    InvalidFractionalRemainder,
    InvalidTimeline,
    ArithmeticOverflow,
};

struct DspCycleGrant {
    std::uint32_t dsp_cycles{};
    std::uint64_t first_timeline_tick{};
    std::uint64_t next_timeline_tick{};
    DspCycleGrantState state_after{};
};

namespace detail {

struct DspTimingUnsigned128 {
    std::uint64_t high{};
    std::uint64_t low{};
};

[[nodiscard]] constexpr DspTimingUnsigned128 multiply_u64_by_u32(
    std::uint64_t value,
    std::uint32_t factor) noexcept {
    constexpr std::uint64_t kLowMask = 0xffff'ffffull;
    const auto value_low = value & kLowMask;
    const auto value_high = value >> 32u;
    const auto low_product = value_low * factor;
    const auto high_product = value_high * factor;
    const auto shifted_high_product = high_product << 32u;
    const auto product_low = low_product + shifted_high_product;
    const auto carry = product_low < low_product ? 1u : 0u;
    return {
        (high_product >> 32u) + carry,
        product_low,
    };
}

struct DspTimingScaleResult {
    std::uint64_t quotient{};
    std::uint64_t remainder{};
    bool quotient_overflow{};
};

// Portable 128/64 restoring division. MSVC has no standard `__int128`; this
// fixed 128-step path supports internal checkpoint arithmetic checks and the
// rare overflowing-QPC-product fallback on clang-cl. It is never checkpoint
// authentication or part of the per-instruction grant path.
[[nodiscard]] constexpr DspTimingScaleResult divide_u128_by_u64(
    DspTimingUnsigned128 dividend,
    std::uint64_t divisor) noexcept {
    if (divisor == 0u) {
        return {0u, 0u, true};
    }
    DspTimingScaleResult result{};
    for (int bit_index = 127; bit_index >= 0; --bit_index) {
        const auto source_shift = static_cast<unsigned>(
            bit_index >= 64 ? bit_index - 64 : bit_index);
        const bool source_bit = bit_index >= 64
            ? ((dividend.high >> source_shift) & 1u) != 0u
            : ((dividend.low >> source_shift) & 1u) != 0u;
        const bool carried_high_bit =
            (result.remainder & (std::uint64_t{1} << 63u)) != 0u;
        const auto shifted_remainder = static_cast<std::uint64_t>(
            (result.remainder << 1u) | (source_bit ? 1u : 0u));
        if (!carried_high_bit && shifted_remainder < divisor) {
            result.remainder = shifted_remainder;
            continue;
        }
        result.remainder =
            static_cast<std::uint64_t>(shifted_remainder - divisor);
        if (bit_index >= 64) {
            result.quotient_overflow = true;
        } else {
            result.quotient |=
                std::uint64_t{1} << static_cast<unsigned>(bit_index);
        }
    }
    return result;
}

[[nodiscard]] constexpr DspTimingScaleResult scale_total_dsp_cycles(
    std::uint64_t total_dsp_cycles,
    std::uint64_t dsp_clock_hz) noexcept {
    static_assert(
        galaxy::timing::kTimelineTicksPerSecond <=
        std::numeric_limits<std::uint32_t>::max());
    return divide_u128_by_u64(
        multiply_u64_by_u32(
            total_dsp_cycles,
            static_cast<std::uint32_t>(
                galaxy::timing::kTimelineTicksPerSecond)),
        dsp_clock_hz);
}

// Internal/test-only arithmetic validation. Expected clock and anchor are
// immutable inputs from a hypothetical trusted provider. Passing this check
// proves only quotient/remainder consistency; even a forged history can be
// recomputed consistently. No accumulator restore API consumes this result.
[[nodiscard]] constexpr DspCycleCheckpointArithmeticFailure
validate_cycle_checkpoint_arithmetic(
    const DspCycleGrantState& state,
    std::uint64_t expected_dsp_clock_hz,
    std::uint64_t expected_timeline_anchor_ticks) noexcept {
    if (expected_dsp_clock_hz == 0u) {
        return DspCycleCheckpointArithmeticFailure::InvalidExpectedClock;
    }
    if (state.dsp_clock_hz != expected_dsp_clock_hz) {
        return DspCycleCheckpointArithmeticFailure::ClockMismatch;
    }
    if (state.timeline_anchor_ticks != expected_timeline_anchor_ticks) {
        return DspCycleCheckpointArithmeticFailure::AnchorMismatch;
    }
    if (state.fractional_remainder >= expected_dsp_clock_hz) {
        return DspCycleCheckpointArithmeticFailure::
            InvalidFractionalRemainder;
    }
    const auto scaled = scale_total_dsp_cycles(
        state.total_dsp_cycles, expected_dsp_clock_hz);
    if (scaled.quotient_overflow ||
        expected_timeline_anchor_ticks >
            std::numeric_limits<std::uint64_t>::max() - scaled.quotient) {
        return DspCycleCheckpointArithmeticFailure::ArithmeticOverflow;
    }
    if (state.timeline_ticks !=
        expected_timeline_anchor_ticks + scaled.quotient) {
        return DspCycleCheckpointArithmeticFailure::InvalidTimeline;
    }
    if (state.fractional_remainder != scaled.remainder) {
        return DspCycleCheckpointArithmeticFailure::
            InvalidFractionalRemainder;
    }
    return DspCycleCheckpointArithmeticFailure::None;
}

// Pure transition used by the accumulator after it already owns a fresh,
// valid state. Tests call it directly at u64 boundaries that cannot be reached
// by iterating. It neither installs checkpoint state nor grants authority.
[[nodiscard]] constexpr std::optional<DspCycleGrant>
advance_owned_cycle_state(
    const DspCycleGrantState& state,
    std::uint32_t dsp_cycles,
    DspCycleGrantFailure& failure) noexcept {
    if (state.dsp_clock_hz == 0u) {
        failure = DspCycleGrantFailure::InvalidClock;
        return std::nullopt;
    }
    if (dsp_cycles == 0u) {
        failure = DspCycleGrantFailure::ZeroCycles;
        return std::nullopt;
    }
    if (state.total_dsp_cycles >
        std::numeric_limits<std::uint64_t>::max() - dsp_cycles) {
        failure = DspCycleGrantFailure::TotalCycleOverflow;
        return std::nullopt;
    }
    constexpr auto kTicksPerSecond =
        galaxy::timing::kTimelineTicksPerSecond;
    const auto scaled_cycles =
        static_cast<std::uint64_t>(dsp_cycles) * kTicksPerSecond;
    auto elapsed_ticks = scaled_cycles / state.dsp_clock_hz;
    const auto scaled_remainder = scaled_cycles % state.dsp_clock_hz;
    auto next_remainder = state.fractional_remainder;
    if (scaled_remainder != 0u) {
        const auto distance_to_clock =
            state.dsp_clock_hz - scaled_remainder;
        if (next_remainder >= distance_to_clock) {
            next_remainder -= distance_to_clock;
            ++elapsed_ticks;
        } else {
            next_remainder += scaled_remainder;
        }
    }
    if (state.timeline_ticks >
        std::numeric_limits<std::uint64_t>::max() - elapsed_ticks) {
        failure = DspCycleGrantFailure::TimelineOverflow;
        return std::nullopt;
    }

    DspCycleGrant result{};
    result.dsp_cycles = dsp_cycles;
    result.first_timeline_tick = state.timeline_ticks;
    result.state_after = state;
    result.state_after.total_dsp_cycles += dsp_cycles;
    result.state_after.timeline_ticks += elapsed_ticks;
    result.state_after.fractional_remainder = next_remainder;
    result.next_timeline_tick = result.state_after.timeline_ticks;
    failure = DspCycleGrantFailure::None;
    return result;
}

}  // namespace detail

class DspCycleGrantAccumulator {
public:
    DspCycleGrantAccumulator() noexcept = default;

    DspCycleGrantAccumulator(const DspCycleGrantAccumulator&) = delete;
    DspCycleGrantAccumulator& operator=(
        const DspCycleGrantAccumulator&) = delete;
    DspCycleGrantAccumulator(DspCycleGrantAccumulator&&) = delete;
    DspCycleGrantAccumulator& operator=(
        DspCycleGrantAccumulator&&) = delete;

    DspCycleGrantAccumulator(
        std::uint64_t dsp_clock_hz,
        std::uint64_t timeline_anchor_ticks) noexcept {
        (void)reset_fresh(dsp_clock_hz, timeline_anchor_ticks);
    }

    // Starts a new zero-history timeline. This is deliberately not checkpoint
    // restore: total cycles and remainder are always cleared, and changing the
    // clock or anchor discards the prior history.
    [[nodiscard]] bool reset_fresh(
        std::uint64_t dsp_clock_hz,
        std::uint64_t timeline_anchor_ticks) noexcept {
        if (dsp_clock_hz == 0u) {
            failure_ = DspCycleGrantFailure::InvalidClock;
            return false;
        }
        state_ = {
            dsp_clock_hz,
            0u,
            timeline_anchor_ticks,
            timeline_anchor_ticks,
            0u,
        };
        failure_ = DspCycleGrantFailure::None;
        return true;
    }

    [[nodiscard]] std::optional<DspCycleGrant> grant(
        std::uint32_t dsp_cycles) noexcept {
        if (failure_ != DspCycleGrantFailure::None) {
            return std::nullopt;
        }
        auto result = detail::advance_owned_cycle_state(
            state_, dsp_cycles, failure_);
        if (result.has_value()) {
            state_ = result->state_after;
        }
        return result;
    }

    void abort() noexcept {
        if (failure_ == DspCycleGrantFailure::None) {
            failure_ = DspCycleGrantFailure::Aborted;
        }
    }

    [[nodiscard]] DspCycleGrantFailure failure() const noexcept {
        return failure_;
    }
    [[nodiscard]] bool failed() const noexcept {
        return failure_ != DspCycleGrantFailure::None;
    }
    [[nodiscard]] DspCycleGrantState state() const noexcept { return state_; }

private:
    DspCycleGrantState state_{};
    DspCycleGrantFailure failure_{DspCycleGrantFailure::InvalidClock};
};

}  // namespace galaxy::dsp_timing_runtime
