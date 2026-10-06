use std::collections::{BTreeMap, BTreeSet};

use serde::{Deserialize, Serialize};
use sha1::{Digest, Sha1};
use thiserror::Error;

use crate::dsp::{
    decode_dsp_instruction, decoded_memory_operands, instruction_memory_region, DspCondition,
    DspDecodeError, DspDecodedMemoryDirection, DspDecodedMemorySlot, DspDecodedMemorySpace,
    DspInstruction, DspInstructionMemoryRegion,
};

/// Version of the install-time DSP timing contract.
///
/// This is intentionally independent of `kNativeAbiVersion`: timing metadata can
/// be exported by generated DSP code without changing `PpcContext` or
/// `NativeServicesV1`. Increment this whenever key meaning, cycle accounting, or
/// generated timing-boundary semantics change.
pub const DSP_TIMING_CONTRACT_VERSION: u32 = 2;
pub const DSP_TIMING_PROFILE_SCHEMA_VERSION: u32 = 2;
pub const DSP_TIMING_CAPTURE_SCHEMA_VERSION: u32 = 1;

// Production acceptance is an explicit source-controlled review decision. This
// list intentionally remains empty until retail-Wii captures, their clock
// measurement, and every covered key have been independently reviewed. Merely
// labeling a JSON document "retail" must never qualify it for lowering.
const APPROVED_RETAIL_DSP_TIMING_PROFILE_IDENTITIES: &[&str] = &[];

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DspTimingProfileKind {
    SyntheticTest,
    RetailWiiCapture,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DspTimingBundleKey {
    Standalone,
    Parallel {
        primary_word: u16,
        parallel_extension: u8,
        extension_mask: u8,
    },
}

/// Exact instruction identity used by a timing rule.
///
/// Immediate operands deliberately remain part of the identity. A future
/// profile format may add evidence-backed masks, but schema v2 never assumes an
/// operand is timing-irrelevant. Parallel issue is represented both by the raw
/// opcode and by its decoded primary/extension split so bundles cannot silently
/// collapse into their primary operation.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DspTimingInstructionKey {
    pub opcode_word: u16,
    pub immediate_word: Option<u16>,
    pub bundle: DspTimingBundleKey,
}

impl DspTimingInstructionKey {
    pub fn from_words(address: u16, words: &[u16]) -> Result<Self, DspDecodeError> {
        let decoded = decode_dsp_instruction(address, words)?;
        let immediate_word = if decoded.length_words == 2 {
            Some(words[1])
        } else {
            None
        };
        let bundle = bundle_key(decoded.instruction, decoded.raw_word);
        Ok(Self {
            opcode_word: decoded.raw_word,
            immediate_word,
            bundle,
        })
    }

    fn validate_at(self, address: u16) -> Result<DspInstruction, DspTimingKeyError> {
        let words = match self.immediate_word {
            Some(immediate) => vec![self.opcode_word, immediate],
            None => vec![self.opcode_word],
        };
        let decoded = decode_dsp_instruction(address, &words)
            .map_err(|source| DspTimingKeyError::InvalidInstruction { source })?;
        let decoded_has_immediate = decoded.length_words == 2;
        if decoded_has_immediate != self.immediate_word.is_some() {
            return Err(DspTimingKeyError::InstructionLengthMismatch {
                opcode_word: self.opcode_word,
                decoded_words: decoded.length_words,
                supplied_words: if self.immediate_word.is_some() { 2 } else { 1 },
            });
        }
        let expected_bundle = bundle_key(decoded.instruction, decoded.raw_word);
        if self.bundle != expected_bundle {
            return Err(DspTimingKeyError::BundleMismatch {
                opcode_word: self.opcode_word,
                expected: expected_bundle,
                actual: self.bundle,
            });
        }
        Ok(decoded.instruction)
    }
}

fn bundle_key(instruction: DspInstruction, raw_word: u16) -> DspTimingBundleKey {
    if matches!(instruction, DspInstruction::Parallel { .. }) {
        let extension_mask = if raw_word >> 12 == 0x3 { 0x7f } else { 0xff };
        DspTimingBundleKey::Parallel {
            primary_word: raw_word & !u16::from(extension_mask),
            parallel_extension: (raw_word & u16::from(extension_mask)) as u8,
            extension_mask,
        }
    } else {
        DspTimingBundleKey::Standalone
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DspTimingInstructionOutcome {
    Completed,
    ConditionSatisfied,
    ConditionNotSatisfied,
    LoopArmed,
    ZeroCountLoopSkipped,
    Halted,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DspTimingHardwareLoopOutcome {
    None,
    BackEdgeTaken,
    Exited,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DspTimingInterruptOutcome {
    None,
    AcceptedAfterInstruction,
}

/// Post-semantics identity for one successfully retired instruction.
///
/// A timing key is never formed at instruction entry. The runtime must first
/// snapshot the complete immutable pre-state, execute the instruction, resolve
/// its condition and hardware-loop edge, and then commit exactly once. A hard
/// trap or cooperative abort before that commit produces no timing key and does
/// not retire the current instruction.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DspTimingOutcomeKey {
    pub instruction: DspTimingInstructionOutcome,
    pub hardware_loop: DspTimingHardwareLoopOutcome,
    pub interrupt: DspTimingInterruptOutcome,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
/// Encoded architectural operand identity, independent of helper-call order or
/// dynamic access coalescing. Single parallel loads/stores use `Primary`. For
/// dual operations, `Primary` is the decoded address-register operand (AR0 for
/// accumulator-mid load/store) and `Secondary` is AR3. Thus LS is primary-read
/// plus secondary-write, while SL is primary-write plus secondary-read.
pub enum DspTimingMemorySlot {
    Standalone,
    ParallelPrimary,
    ParallelSecondary,
}

pub const DSP_TIMING_RUNTIME_PLAN_CONTRACT_VERSION: u32 = 2;
pub const DSP_TIMING_RUNTIME_PLAN_IDENTITY_BYTES: usize = 20;

pub const DSP_TIMING_STANDALONE_MEMORY_BIT: u8 = 1 << 0;
pub const DSP_TIMING_PARALLEL_PRIMARY_MEMORY_BIT: u8 = 1 << 1;
pub const DSP_TIMING_PARALLEL_SECONDARY_MEMORY_BIT: u8 = 1 << 2;
pub const DSP_TIMING_ALL_MEMORY_BITS: u8 = DSP_TIMING_STANDALONE_MEMORY_BIT
    | DSP_TIMING_PARALLEL_PRIMARY_MEMORY_BIT
    | DSP_TIMING_PARALLEL_SECONDARY_MEMORY_BIT;

pub const DSP_TIMING_COMPLETED_OUTCOME_BIT: u8 = 1 << 0;
pub const DSP_TIMING_CONDITION_SATISFIED_OUTCOME_BIT: u8 = 1 << 1;
pub const DSP_TIMING_CONDITION_NOT_SATISFIED_OUTCOME_BIT: u8 = 1 << 2;
pub const DSP_TIMING_LOOP_ARMED_OUTCOME_BIT: u8 = 1 << 3;
pub const DSP_TIMING_ZERO_COUNT_LOOP_SKIPPED_OUTCOME_BIT: u8 = 1 << 4;
pub const DSP_TIMING_HALTED_OUTCOME_BIT: u8 = 1 << 5;

pub const DSP_TIMING_NO_HARDWARE_LOOP_OUTCOME_BIT: u8 = 1 << 0;
pub const DSP_TIMING_HARDWARE_LOOP_BACK_EDGE_OUTCOME_BIT: u8 = 1 << 1;
pub const DSP_TIMING_HARDWARE_LOOP_EXITED_OUTCOME_BIT: u8 = 1 << 2;
pub const DSP_TIMING_ALL_HARDWARE_LOOP_OUTCOME_BITS: u8 = DSP_TIMING_NO_HARDWARE_LOOP_OUTCOME_BIT
    | DSP_TIMING_HARDWARE_LOOP_BACK_EDGE_OUTCOME_BIT
    | DSP_TIMING_HARDWARE_LOOP_EXITED_OUTCOME_BIT;

/// Install-time semantic class exported with a generated timing plan.
///
/// This is derived from the decoded instruction by the Rust resolver. The
/// native runtime deliberately has no DSP opcode decoder and may only accept
/// this metadata after the plan identity is found in an authenticated timing
/// contract produced by that resolver.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum DspTimingInstructionSemantic {
    Ordinary,
    ConditionalAlways,
    ConditionalDynamic,
    LoopAlwaysArmed,
    LoopAlwaysSkipped,
    LoopDynamic,
    Halt,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum DspTimingHardwareLoopSite {
    NotLoopEnd,
    MayResolveLoopEnd,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct DspTimingRuntimeMemoryOperand {
    pub space: DspTimingMemorySpace,
    pub direction: DspTimingMemoryDirection,
}

/// Source-complete install-time metadata for one runtime retirement plan.
///
/// `resolver_plan_identity` is a full 20-byte deterministic SHA-1 table
/// identity for the canonical fields below. SHA-1 is used only as a stable
/// table key: it is not artifact integrity, a signature, or authentication,
/// and caller construction is not authorization. An independently
/// authenticated generated timing contract must authorize the exact version
/// and identity; the future staged producer/signer and generated-source hash
/// remain the security authority. Runtime validation can reject internal standalone/parallel,
/// instruction-length, semantic, and outcome-mask lies without decoding an
/// opcode; exact opcode and memory-operation authority remains the Rust
/// resolver plus that authenticated lookup.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DspTimingRuntimePlan {
    pub contract_version: u32,
    pub resolver_plan_identity: [u8; DSP_TIMING_RUNTIME_PLAN_IDENTITY_BYTES],
    pub fetch_address: u16,
    pub instruction: DspTimingInstructionKey,
    pub instruction_word_count: u8,
    pub validated_bundle: DspTimingBundleKey,
    pub instruction_semantic: DspTimingInstructionSemantic,
    pub hardware_loop_site: DspTimingHardwareLoopSite,
    pub expected_memory_slots: u8,
    /// Decoder-authoritative memory space and direction in canonical slot
    /// order: Standalone, ParallelPrimary, ParallelSecondary. Address sources
    /// remain generated raw-boundary metadata because their values resolve
    /// from immutable pre-state at execution time.
    pub memory_operands: [Option<DspTimingRuntimeMemoryOperand>; 3],
    pub allowed_instruction_outcomes: u8,
    pub allowed_hardware_loop_outcomes: u8,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct DspTimingRuntimePlanTableEntry {
    pub contract_version: u32,
    pub resolver_plan_identity: [u8; DSP_TIMING_RUNTIME_PLAN_IDENTITY_BYTES],
}

impl DspTimingRuntimePlan {
    /// Builds the internally constrained install plan for decoded `words`.
    ///
    /// The exact encoded architectural memory-operand slots are derived from
    /// the decoded instruction. They describe operands, not the number or order
    /// of host helper calls: a later lowering may coalesce two equal-address
    /// loads, but both encoded parallel operands remain distinct timing slots.
    pub fn from_words(
        fetch_address: u16,
        words: &[u16],
        hardware_loop_site: DspTimingHardwareLoopSite,
    ) -> Result<Self, DspTimingRuntimePlanError> {
        timing_instruction_region(fetch_address)
            .map_err(|_| DspTimingRuntimePlanError::InvalidInstructionAddress { fetch_address })?;
        let decoded = decode_dsp_instruction(fetch_address, words)
            .map_err(|source| DspTimingRuntimePlanError::InvalidInstruction { source })?;
        if words.len() != usize::from(decoded.length_words) {
            return Err(DspTimingRuntimePlanError::InstructionWordCountMismatch {
                decoded_words: decoded.length_words,
                supplied_words: words.len(),
            });
        }
        let instruction = DspTimingInstructionKey {
            opcode_word: decoded.raw_word,
            immediate_word: if decoded.length_words == 2 {
                Some(words[1])
            } else {
                None
            },
            bundle: bundle_key(decoded.instruction, decoded.raw_word),
        };
        let instruction_semantic = runtime_instruction_semantic(decoded.instruction);
        let memory_operands = runtime_memory_operand_authority(decoded.instruction)?;
        let expected_memory_slots = runtime_memory_operand_mask(&memory_operands);
        let allowed_instruction_outcomes = runtime_instruction_outcomes(instruction_semantic);
        let allowed_hardware_loop_outcomes = runtime_hardware_loop_outcomes(hardware_loop_site);
        let mut plan = Self {
            contract_version: DSP_TIMING_RUNTIME_PLAN_CONTRACT_VERSION,
            resolver_plan_identity: [0; DSP_TIMING_RUNTIME_PLAN_IDENTITY_BYTES],
            fetch_address,
            instruction,
            instruction_word_count: decoded.length_words,
            validated_bundle: instruction.bundle,
            instruction_semantic,
            hardware_loop_site,
            expected_memory_slots,
            memory_operands,
            allowed_instruction_outcomes,
            allowed_hardware_loop_outcomes,
        };
        plan.validate_fields()?;
        plan.resolver_plan_identity = plan.compute_resolver_plan_identity();
        Ok(plan)
    }

    pub fn validate(&self) -> Result<(), DspTimingRuntimePlanError> {
        self.validate_fields()?;
        let computed = self.compute_resolver_plan_identity();
        if self.resolver_plan_identity != computed {
            return Err(DspTimingRuntimePlanError::ResolverPlanIdentityMismatch {
                expected: computed,
                actual: self.resolver_plan_identity,
            });
        }
        Ok(())
    }

    pub fn authorized_table_entry(&self) -> DspTimingRuntimePlanTableEntry {
        DspTimingRuntimePlanTableEntry {
            contract_version: self.contract_version,
            resolver_plan_identity: self.resolver_plan_identity,
        }
    }

    pub fn validate_authorized_table_entry(
        &self,
        entry: DspTimingRuntimePlanTableEntry,
    ) -> Result<(), DspTimingRuntimePlanError> {
        self.validate()?;
        let expected = self.authorized_table_entry();
        if entry != expected {
            return Err(DspTimingRuntimePlanError::AuthorizedTableEntryMismatch {
                expected,
                actual: entry,
            });
        }
        Ok(())
    }

    fn validate_fields(&self) -> Result<(), DspTimingRuntimePlanError> {
        if self.contract_version != DSP_TIMING_RUNTIME_PLAN_CONTRACT_VERSION {
            return Err(DspTimingRuntimePlanError::UnsupportedContractVersion {
                actual: self.contract_version,
            });
        }
        timing_instruction_region(self.fetch_address).map_err(|_| {
            DspTimingRuntimePlanError::InvalidInstructionAddress {
                fetch_address: self.fetch_address,
            }
        })?;
        let decoded = self
            .instruction
            .validate_at(self.fetch_address)
            .map_err(|source| DspTimingRuntimePlanError::InvalidInstructionKey { source })?;
        let expected_word_count = if self.instruction.immediate_word.is_some() {
            2
        } else {
            1
        };
        if self.instruction_word_count != expected_word_count {
            return Err(DspTimingRuntimePlanError::InstructionWordCountMismatch {
                decoded_words: expected_word_count,
                supplied_words: usize::from(self.instruction_word_count),
            });
        }
        if self.validated_bundle != self.instruction.bundle {
            return Err(DspTimingRuntimePlanError::ValidatedBundleMismatch {
                expected: self.instruction.bundle,
                actual: self.validated_bundle,
            });
        }
        validate_runtime_memory_slots(self.instruction.bundle, self.expected_memory_slots)?;
        let expected_memory_operands = runtime_memory_operand_authority(decoded)?;
        let expected_memory_slots = runtime_memory_operand_mask(&expected_memory_operands);
        if self.expected_memory_slots != expected_memory_slots {
            return Err(DspTimingRuntimePlanError::MemorySlotMaskMismatch {
                expected: expected_memory_slots,
                actual: self.expected_memory_slots,
            });
        }
        if self.memory_operands != expected_memory_operands {
            return Err(DspTimingRuntimePlanError::MemoryOperandAuthorityMismatch {
                expected: expected_memory_operands,
                actual: self.memory_operands,
            });
        }
        let expected_semantic = runtime_instruction_semantic(decoded);
        if self.instruction_semantic != expected_semantic {
            return Err(DspTimingRuntimePlanError::InstructionSemanticMismatch {
                expected: expected_semantic,
                actual: self.instruction_semantic,
            });
        }
        let expected_instruction_outcomes = runtime_instruction_outcomes(self.instruction_semantic);
        if self.allowed_instruction_outcomes != expected_instruction_outcomes {
            return Err(DspTimingRuntimePlanError::InstructionOutcomeMaskMismatch {
                expected: expected_instruction_outcomes,
                actual: self.allowed_instruction_outcomes,
            });
        }
        if self.instruction_semantic == DspTimingInstructionSemantic::Halt
            && self.hardware_loop_site != DspTimingHardwareLoopSite::NotLoopEnd
        {
            return Err(DspTimingRuntimePlanError::HaltAtHardwareLoopEnd);
        }
        let expected_hardware_loop_outcomes =
            runtime_hardware_loop_outcomes(self.hardware_loop_site);
        if self.allowed_hardware_loop_outcomes != expected_hardware_loop_outcomes {
            return Err(DspTimingRuntimePlanError::HardwareLoopOutcomeMaskMismatch {
                expected: expected_hardware_loop_outcomes,
                actual: self.allowed_hardware_loop_outcomes,
            });
        }
        Ok(())
    }

    fn compute_resolver_plan_identity(&self) -> [u8; DSP_TIMING_RUNTIME_PLAN_IDENTITY_BYTES] {
        let mut hasher = Sha1::new();
        hasher.update(b"galaxy-dsp-runtime-timing-plan-v2\0");
        hasher.update(self.contract_version.to_be_bytes());
        hasher.update(self.fetch_address.to_be_bytes());
        hasher.update(self.instruction.opcode_word.to_be_bytes());
        match self.instruction.immediate_word {
            Some(word) => {
                hasher.update([1]);
                hasher.update(word.to_be_bytes());
            }
            None => {
                hasher.update([0]);
                hasher.update(0u16.to_be_bytes());
            }
        }
        hash_runtime_bundle(&mut hasher, self.instruction.bundle);
        hasher.update([self.instruction_word_count]);
        hash_runtime_bundle(&mut hasher, self.validated_bundle);
        hasher.update([self.instruction_semantic as u8]);
        hasher.update([self.hardware_loop_site as u8]);
        hasher.update([self.expected_memory_slots]);
        for operand in self.memory_operands {
            match operand {
                Some(operand) => hasher.update([1, operand.space as u8, operand.direction as u8]),
                None => hasher.update([0, 0, 0]),
            }
        }
        hasher.update([self.allowed_instruction_outcomes]);
        hasher.update([self.allowed_hardware_loop_outcomes]);
        hasher.finalize().into()
    }
}

fn hash_runtime_bundle(hasher: &mut Sha1, bundle: DspTimingBundleKey) {
    match bundle {
        DspTimingBundleKey::Standalone => hasher.update([0, 0, 0, 0, 0, 0]),
        DspTimingBundleKey::Parallel {
            primary_word,
            parallel_extension,
            extension_mask,
        } => {
            hasher.update([1]);
            hasher.update(primary_word.to_be_bytes());
            hasher.update([parallel_extension, extension_mask, 0]);
        }
    }
}

fn runtime_instruction_semantic(instruction: DspInstruction) -> DspTimingInstructionSemantic {
    match instruction {
        DspInstruction::Halt => DspTimingInstructionSemantic::Halt,
        DspInstruction::If { condition }
        | DspInstruction::JumpImmediate { condition, .. }
        | DspInstruction::JumpRegister { condition, .. }
        | DspInstruction::Return { condition, .. }
        | DspInstruction::CallImmediate { condition, .. }
        | DspInstruction::CallRegister { condition, .. } => {
            if condition == DspCondition::Always {
                DspTimingInstructionSemantic::ConditionalAlways
            } else {
                DspTimingInstructionSemantic::ConditionalDynamic
            }
        }
        DspInstruction::LoopImmediate { count }
        | DspInstruction::BlockLoopImmediate { count, .. } => {
            if count == 0 {
                DspTimingInstructionSemantic::LoopAlwaysSkipped
            } else {
                DspTimingInstructionSemantic::LoopAlwaysArmed
            }
        }
        DspInstruction::Loop { .. } | DspInstruction::BlockLoop { .. } => {
            DspTimingInstructionSemantic::LoopDynamic
        }
        _ => DspTimingInstructionSemantic::Ordinary,
    }
}

fn runtime_instruction_outcomes(semantic: DspTimingInstructionSemantic) -> u8 {
    match semantic {
        DspTimingInstructionSemantic::Ordinary => DSP_TIMING_COMPLETED_OUTCOME_BIT,
        DspTimingInstructionSemantic::ConditionalAlways => {
            DSP_TIMING_CONDITION_SATISFIED_OUTCOME_BIT
        }
        DspTimingInstructionSemantic::ConditionalDynamic => {
            DSP_TIMING_CONDITION_SATISFIED_OUTCOME_BIT
                | DSP_TIMING_CONDITION_NOT_SATISFIED_OUTCOME_BIT
        }
        DspTimingInstructionSemantic::LoopAlwaysArmed => DSP_TIMING_LOOP_ARMED_OUTCOME_BIT,
        DspTimingInstructionSemantic::LoopAlwaysSkipped => {
            DSP_TIMING_ZERO_COUNT_LOOP_SKIPPED_OUTCOME_BIT
        }
        DspTimingInstructionSemantic::LoopDynamic => {
            DSP_TIMING_LOOP_ARMED_OUTCOME_BIT | DSP_TIMING_ZERO_COUNT_LOOP_SKIPPED_OUTCOME_BIT
        }
        DspTimingInstructionSemantic::Halt => DSP_TIMING_HALTED_OUTCOME_BIT,
    }
}

fn runtime_hardware_loop_outcomes(site: DspTimingHardwareLoopSite) -> u8 {
    match site {
        DspTimingHardwareLoopSite::NotLoopEnd => DSP_TIMING_NO_HARDWARE_LOOP_OUTCOME_BIT,
        DspTimingHardwareLoopSite::MayResolveLoopEnd => DSP_TIMING_ALL_HARDWARE_LOOP_OUTCOME_BITS,
    }
}

fn runtime_memory_operand_authority(
    instruction: DspInstruction,
) -> Result<[Option<DspTimingRuntimeMemoryOperand>; 3], DspTimingRuntimePlanError> {
    let mut result = [None; 3];
    for decoded in decoded_memory_operands(instruction) {
        let slot_index = match decoded.slot {
            DspDecodedMemorySlot::Standalone => 0,
            DspDecodedMemorySlot::ParallelPrimary => 1,
            DspDecodedMemorySlot::ParallelSecondary => 2,
        };
        let space = match decoded.space {
            DspDecodedMemorySpace::Instruction => DspTimingMemorySpace::Instruction,
            DspDecodedMemorySpace::Data => DspTimingMemorySpace::Data,
        };
        let direction = match decoded.direction {
            DspDecodedMemoryDirection::Read => DspTimingMemoryDirection::Read,
            DspDecodedMemoryDirection::Write => DspTimingMemoryDirection::Write,
        };
        if space == DspTimingMemorySpace::Instruction
            && direction == DspTimingMemoryDirection::Write
        {
            return Err(DspTimingRuntimePlanError::UnrepresentableInstructionMemoryWrite);
        }
        debug_assert!(result[slot_index].is_none());
        result[slot_index] = Some(DspTimingRuntimeMemoryOperand { space, direction });
    }
    Ok(result)
}

fn runtime_memory_operand_mask(operands: &[Option<DspTimingRuntimeMemoryOperand>; 3]) -> u8 {
    operands
        .iter()
        .enumerate()
        .fold(0u8, |mask, (index, operand)| {
            if operand.is_some() {
                mask | (1u8 << index)
            } else {
                mask
            }
        })
}

fn validate_runtime_memory_slots(
    bundle: DspTimingBundleKey,
    expected_memory_slots: u8,
) -> Result<(), DspTimingRuntimePlanError> {
    if expected_memory_slots & !DSP_TIMING_ALL_MEMORY_BITS != 0 {
        return Err(DspTimingRuntimePlanError::InvalidMemorySlotMask {
            mask: expected_memory_slots,
        });
    }
    let incompatible = match bundle {
        DspTimingBundleKey::Standalone => {
            expected_memory_slots
                & (DSP_TIMING_PARALLEL_PRIMARY_MEMORY_BIT
                    | DSP_TIMING_PARALLEL_SECONDARY_MEMORY_BIT)
                != 0
        }
        DspTimingBundleKey::Parallel { .. } => {
            expected_memory_slots & DSP_TIMING_STANDALONE_MEMORY_BIT != 0
                || (expected_memory_slots & DSP_TIMING_PARALLEL_SECONDARY_MEMORY_BIT != 0
                    && expected_memory_slots & DSP_TIMING_PARALLEL_PRIMARY_MEMORY_BIT == 0)
        }
    };
    if incompatible {
        return Err(
            DspTimingRuntimePlanError::MemorySlotMaskIncompatibleWithBundle {
                mask: expected_memory_slots,
                bundle,
            },
        );
    }
    Ok(())
}

#[derive(Debug, Error, PartialEq, Eq)]
pub enum DspTimingRuntimePlanError {
    #[error("invalid DSP instruction in runtime timing plan: {source}")]
    InvalidInstruction { source: DspDecodeError },
    #[error("invalid DSP instruction key in runtime timing plan: {source}")]
    InvalidInstructionKey { source: DspTimingKeyError },
    #[error("invalid DSP instruction address 0x{fetch_address:04X} in runtime timing plan")]
    InvalidInstructionAddress { fetch_address: u16 },
    #[error("unsupported DSP runtime timing plan contract version {actual}")]
    UnsupportedContractVersion { actual: u32 },
    #[error(
        "DSP runtime timing plan decoded as {decoded_words} words but supplied {supplied_words}"
    )]
    InstructionWordCountMismatch {
        decoded_words: u8,
        supplied_words: usize,
    },
    #[error("validated DSP timing bundle mismatch: expected {expected:?}, got {actual:?}")]
    ValidatedBundleMismatch {
        expected: DspTimingBundleKey,
        actual: DspTimingBundleKey,
    },
    #[error("DSP runtime timing memory slot mask 0x{mask:02X} is invalid")]
    InvalidMemorySlotMask { mask: u8 },
    #[error("DSP runtime timing memory slot mask 0x{mask:02X} is incompatible with {bundle:?}")]
    MemorySlotMaskIncompatibleWithBundle {
        mask: u8,
        bundle: DspTimingBundleKey,
    },
    #[error("DSP runtime timing memory slot mask mismatch: expected 0x{expected:02X}, got 0x{actual:02X}")]
    MemorySlotMaskMismatch { expected: u8, actual: u8 },
    #[error("DSP runtime timing memory operand authority mismatch")]
    MemoryOperandAuthorityMismatch {
        expected: [Option<DspTimingRuntimeMemoryOperand>; 3],
        actual: [Option<DspTimingRuntimeMemoryOperand>; 3],
    },
    #[error("decoded DSP instruction-memory writes are unrepresentable")]
    UnrepresentableInstructionMemoryWrite,
    #[error("DSP runtime timing semantic mismatch: expected {expected:?}, got {actual:?}")]
    InstructionSemanticMismatch {
        expected: DspTimingInstructionSemantic,
        actual: DspTimingInstructionSemantic,
    },
    #[error("DSP runtime timing instruction outcome mask mismatch: expected 0x{expected:02X}, got 0x{actual:02X}")]
    InstructionOutcomeMaskMismatch { expected: u8, actual: u8 },
    #[error("a DSP halt cannot be installed as a hardware-loop end")]
    HaltAtHardwareLoopEnd,
    #[error("DSP runtime timing hardware-loop outcome mask mismatch: expected 0x{expected:02X}, got 0x{actual:02X}")]
    HardwareLoopOutcomeMaskMismatch { expected: u8, actual: u8 },
    #[error("DSP runtime timing resolver plan identity mismatch")]
    ResolverPlanIdentityMismatch {
        expected: [u8; DSP_TIMING_RUNTIME_PLAN_IDENTITY_BYTES],
        actual: [u8; DSP_TIMING_RUNTIME_PLAN_IDENTITY_BYTES],
    },
    #[error("DSP runtime timing plan does not match the exact authorized table entry")]
    AuthorizedTableEntryMismatch {
        expected: DspTimingRuntimePlanTableEntry,
        actual: DspTimingRuntimePlanTableEntry,
    },
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
#[repr(u8)]
pub enum DspTimingMemorySpace {
    Instruction,
    Data,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
#[repr(u8)]
pub enum DspTimingMemoryDirection {
    Read,
    Write,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DspTimingMemoryRegion {
    InstructionIram,
    InstructionIrom,
    DataDram,
    CoefficientRom,
    InterfaceRegister,
    DataOpenBus,
}

/// One architecturally visible memory operation in a statically declared issue
/// slot. Standalone instructions may use only `Standalone`; parallel bundles
/// may use only `ParallelPrimary` and `ParallelSecondary`. The effective
/// address is dynamic key material; space and direction must match the
/// decoder-authoritative descriptor for that exact slot and may never be
/// substituted from a different operand.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DspTimingMemoryAccessKey {
    pub slot: DspTimingMemorySlot,
    pub direction: DspTimingMemoryDirection,
    pub address: u16,
    pub region: DspTimingMemoryRegion,
}

impl DspTimingMemoryAccessKey {
    pub fn instruction(
        slot: DspTimingMemorySlot,
        direction: DspTimingMemoryDirection,
        address: u16,
    ) -> Result<Self, DspTimingKeyError> {
        let region = timing_instruction_access_region(direction, address)?;
        Ok(Self {
            slot,
            direction,
            address,
            region,
        })
    }

    pub fn data(
        slot: DspTimingMemorySlot,
        direction: DspTimingMemoryDirection,
        address: u16,
    ) -> Result<Self, DspTimingKeyError> {
        let region = timing_data_region(direction, address);
        Ok(Self {
            slot,
            direction,
            address,
            region,
        })
    }

    fn validate(self) -> Result<(), DspTimingKeyError> {
        let expected = match self.region {
            DspTimingMemoryRegion::InstructionIram | DspTimingMemoryRegion::InstructionIrom => {
                timing_instruction_access_region(self.direction, self.address)?
            }
            DspTimingMemoryRegion::DataDram
            | DspTimingMemoryRegion::CoefficientRom
            | DspTimingMemoryRegion::InterfaceRegister
            | DspTimingMemoryRegion::DataOpenBus => {
                timing_data_region(self.direction, self.address)
            }
        };
        if self.region != expected {
            return Err(DspTimingKeyError::MemoryRegionMismatch {
                address: self.address,
                expected,
                actual: self.region,
            });
        }
        Ok(())
    }

    fn space(self) -> DspTimingMemorySpace {
        match self.region {
            DspTimingMemoryRegion::InstructionIram | DspTimingMemoryRegion::InstructionIrom => {
                DspTimingMemorySpace::Instruction
            }
            DspTimingMemoryRegion::DataDram
            | DspTimingMemoryRegion::CoefficientRom
            | DspTimingMemoryRegion::InterfaceRegister
            | DspTimingMemoryRegion::DataOpenBus => DspTimingMemorySpace::Data,
        }
    }
}

fn timing_instruction_region(address: u16) -> Result<DspTimingMemoryRegion, DspTimingKeyError> {
    match instruction_memory_region(address) {
        Ok(DspInstructionMemoryRegion::Iram { .. }) => Ok(DspTimingMemoryRegion::InstructionIram),
        Ok(DspInstructionMemoryRegion::Irom { .. }) => Ok(DspTimingMemoryRegion::InstructionIrom),
        Err(_) => Err(DspTimingKeyError::InvalidInstructionAddress { address }),
    }
}

fn timing_instruction_access_region(
    direction: DspTimingMemoryDirection,
    address: u16,
) -> Result<DspTimingMemoryRegion, DspTimingKeyError> {
    if direction != DspTimingMemoryDirection::Read {
        return Err(DspTimingKeyError::InvalidInstructionMemoryDirection { direction });
    }
    if address <= 0x0fff {
        Ok(DspTimingMemoryRegion::InstructionIram)
    } else {
        // dsp_iram_read masks every non-IRAM address into the 0x1000-word IROM.
        Ok(DspTimingMemoryRegion::InstructionIrom)
    }
}

fn timing_data_region(direction: DspTimingMemoryDirection, address: u16) -> DspTimingMemoryRegion {
    match (direction, address & 0xf000) {
        (_, 0x0000) => DspTimingMemoryRegion::DataDram,
        (DspTimingMemoryDirection::Read, 0x1000) => DspTimingMemoryRegion::CoefficientRom,
        (_, 0xf000) => DspTimingMemoryRegion::InterfaceRegister,
        _ => DspTimingMemoryRegion::DataOpenBus,
    }
}

/// Exact pre-instruction context represented by schema v2.
///
/// No wildcard fields exist. Profiles may only combine contexts after a future
/// contract version defines and validates evidence-backed equivalence rules.
/// Register, stack, pending-interrupt, and predecessor fields are sampled before
/// any semantics of the current instruction. Exact effective memory accesses
/// are appended to that immutable snapshot while semantics execute and the
/// combined key exists only at commit. The four stack values are logical depths,
/// not an unreviewed reinterpretation of wrapping stack-pointer storage. Schema
/// Stack 0 carries both call return addresses and hardware-loop body targets;
/// a hardware-loop edge therefore requires its logical depth along with stacks
/// 2 and 3. Schema v2 stores depths as `u8`; the native inert sidecar starts at
/// a known reset epoch and becomes unavailable on underflow or values above
/// 255 rather than inferring a 31/32 limit from wrapping hardware cursors. No
/// product worker installs that sidecar yet.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DspTimingPredecessorKey {
    pub fetch_address: u16,
    pub instruction: DspTimingInstructionKey,
}

impl DspTimingPredecessorKey {
    pub fn from_words(fetch_address: u16, words: &[u16]) -> Result<Self, DspTimingKeyError> {
        timing_instruction_region(fetch_address)?;
        let instruction = DspTimingInstructionKey::from_words(fetch_address, words)
            .map_err(|source| DspTimingKeyError::InvalidInstruction { source })?;
        Ok(Self {
            fetch_address,
            instruction,
        })
    }

    fn validate(self) -> Result<(), DspTimingKeyError> {
        timing_instruction_region(self.fetch_address)?;
        self.instruction.validate_at(self.fetch_address).map(|_| ())
    }
}

#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DspTimingContextKey {
    pub fetch_address: u16,
    pub status_register: u16,
    pub control_register: u16,
    pub address_registers: [u16; 4],
    pub index_registers: [i16; 4],
    pub wrap_registers: [u16; 4],
    pub call_stack_depth: u8,
    pub data_stack_depth: u8,
    pub loop_address_stack_depth: u8,
    pub loop_counter_stack_depth: u8,
    pub external_interrupt_pending: bool,
    pub accelerator_interrupt_pending: bool,
    pub predecessor: Option<DspTimingPredecessorKey>,
    pub memory_accesses: Vec<DspTimingMemoryAccessKey>,
}

impl DspTimingContextKey {
    pub fn validate(&self) -> Result<(), DspTimingKeyError> {
        timing_instruction_region(self.fetch_address)?;
        if let Some(predecessor) = self.predecessor {
            predecessor.validate()?;
        }
        let mut prior = None;
        for access in &self.memory_accesses {
            access.validate()?;
            if let Some(previous) = prior {
                if previous >= access.slot {
                    return Err(if previous == access.slot {
                        DspTimingKeyError::DuplicateMemorySlot { slot: access.slot }
                    } else {
                        DspTimingKeyError::MemoryAccessesNotCanonical
                    });
                }
            }
            prior = Some(access.slot);
        }
        Ok(())
    }
}

#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DspTimingKey {
    pub instruction: DspTimingInstructionKey,
    pub outcome: DspTimingOutcomeKey,
    pub context: DspTimingContextKey,
}

impl DspTimingKey {
    pub fn from_words(
        words: &[u16],
        outcome: DspTimingOutcomeKey,
        context: DspTimingContextKey,
    ) -> Result<Self, DspTimingKeyError> {
        let instruction = DspTimingInstructionKey::from_words(context.fetch_address, words)
            .map_err(|source| DspTimingKeyError::InvalidInstruction { source })?;
        let key = Self {
            instruction,
            outcome,
            context,
        };
        key.validate()?;
        Ok(key)
    }

    pub fn validate(&self) -> Result<(), DspTimingKeyError> {
        let instruction = self.instruction.validate_at(self.context.fetch_address)?;
        self.context.validate()?;
        let expected_memory_operands = runtime_memory_operand_authority(instruction)
            .map_err(|_| DspTimingKeyError::UnrepresentableInstructionMemoryWrite)?;
        let mut actual_memory_slots = 0u8;
        for access in &self.context.memory_accesses {
            let slot_index = match access.slot {
                DspTimingMemorySlot::Standalone => 0,
                DspTimingMemorySlot::ParallelPrimary => 1,
                DspTimingMemorySlot::ParallelSecondary => 2,
            };
            actual_memory_slots |= match access.slot {
                DspTimingMemorySlot::Standalone => DSP_TIMING_STANDALONE_MEMORY_BIT,
                DspTimingMemorySlot::ParallelPrimary => DSP_TIMING_PARALLEL_PRIMARY_MEMORY_BIT,
                DspTimingMemorySlot::ParallelSecondary => DSP_TIMING_PARALLEL_SECONDARY_MEMORY_BIT,
            };
            let compatible = match self.instruction.bundle {
                DspTimingBundleKey::Standalone => access.slot == DspTimingMemorySlot::Standalone,
                DspTimingBundleKey::Parallel { .. } => matches!(
                    access.slot,
                    DspTimingMemorySlot::ParallelPrimary | DspTimingMemorySlot::ParallelSecondary
                ),
            };
            if !compatible {
                return Err(DspTimingKeyError::MemorySlotIncompatibleWithBundle {
                    slot: access.slot,
                    bundle: self.instruction.bundle,
                });
            }
            if let Some(expected) = expected_memory_operands[slot_index] {
                let actual_space = access.space();
                if actual_space != expected.space {
                    return Err(DspTimingKeyError::MemorySpaceMismatch {
                        slot: access.slot,
                        expected: expected.space,
                        actual: actual_space,
                    });
                }
                if access.direction != expected.direction {
                    return Err(DspTimingKeyError::MemoryDirectionMismatch {
                        slot: access.slot,
                        expected: expected.direction,
                        actual: access.direction,
                    });
                }
            }
        }
        let expected_memory_slots = runtime_memory_operand_mask(&expected_memory_operands);
        if actual_memory_slots != expected_memory_slots {
            return Err(DspTimingKeyError::MemorySlotInventoryMismatch {
                expected: expected_memory_slots,
                actual: actual_memory_slots,
            });
        }
        validate_outcome(instruction, self.outcome.instruction)?;
        validate_hardware_loop_outcome(instruction, self.outcome.hardware_loop, &self.context)?;
        if self.outcome.interrupt == DspTimingInterruptOutcome::AcceptedAfterInstruction
            && !self.context.external_interrupt_pending
            && !self.context.accelerator_interrupt_pending
        {
            return Err(DspTimingKeyError::InterruptAcceptedWithoutPendingSource);
        }
        Ok(())
    }
}

fn validate_hardware_loop_outcome(
    instruction: DspInstruction,
    outcome: DspTimingHardwareLoopOutcome,
    context: &DspTimingContextKey,
) -> Result<(), DspTimingKeyError> {
    if outcome == DspTimingHardwareLoopOutcome::None {
        return Ok(());
    }
    if instruction == DspInstruction::Halt {
        return Err(DspTimingKeyError::HardwareLoopEdgeOnHalt { outcome });
    }
    for (stack_name, depth) in [
        ("loop target stack", context.call_stack_depth),
        ("loop address stack", context.loop_address_stack_depth),
        ("loop counter stack", context.loop_counter_stack_depth),
    ] {
        if depth == 0 {
            return Err(DspTimingKeyError::HardwareLoopEdgeWithoutRequiredStack {
                outcome,
                stack_name,
            });
        }
    }
    Ok(())
}

fn validate_outcome(
    instruction: DspInstruction,
    outcome: DspTimingInstructionOutcome,
) -> Result<(), DspTimingKeyError> {
    let valid = match instruction {
        DspInstruction::Halt => outcome == DspTimingInstructionOutcome::Halted,
        DspInstruction::If { condition }
        | DspInstruction::JumpImmediate { condition, .. }
        | DspInstruction::JumpRegister { condition, .. }
        | DspInstruction::Return { condition, .. }
        | DspInstruction::CallImmediate { condition, .. }
        | DspInstruction::CallRegister { condition, .. } => {
            outcome == DspTimingInstructionOutcome::ConditionSatisfied
                || (condition != DspCondition::Always
                    && outcome == DspTimingInstructionOutcome::ConditionNotSatisfied)
        }
        DspInstruction::LoopImmediate { count } => {
            if count == 0 {
                outcome == DspTimingInstructionOutcome::ZeroCountLoopSkipped
            } else {
                outcome == DspTimingInstructionOutcome::LoopArmed
            }
        }
        DspInstruction::BlockLoopImmediate { count, .. } => {
            if count == 0 {
                outcome == DspTimingInstructionOutcome::ZeroCountLoopSkipped
            } else {
                outcome == DspTimingInstructionOutcome::LoopArmed
            }
        }
        DspInstruction::Loop { .. } | DspInstruction::BlockLoop { .. } => matches!(
            outcome,
            DspTimingInstructionOutcome::LoopArmed
                | DspTimingInstructionOutcome::ZeroCountLoopSkipped
        ),
        _ => outcome == DspTimingInstructionOutcome::Completed,
    };
    if valid {
        Ok(())
    } else {
        Err(DspTimingKeyError::InvalidOutcome { outcome })
    }
}

#[derive(Debug, Error, PartialEq, Eq)]
pub enum DspTimingKeyError {
    #[error("invalid DSP instruction in timing key: {source}")]
    InvalidInstruction { source: DspDecodeError },
    #[error(
        "DSP timing instruction 0x{opcode_word:04X} decoded as {decoded_words} words but key supplied {supplied_words}"
    )]
    InstructionLengthMismatch {
        opcode_word: u16,
        decoded_words: u8,
        supplied_words: u8,
    },
    #[error(
        "DSP timing bundle split mismatch for opcode 0x{opcode_word:04X}: expected {expected:?}, got {actual:?}"
    )]
    BundleMismatch {
        opcode_word: u16,
        expected: DspTimingBundleKey,
        actual: DspTimingBundleKey,
    },
    #[error("invalid DSP instruction address 0x{address:04X} in timing context")]
    InvalidInstructionAddress { address: u16 },
    #[error("DSP timing instruction-memory access has invalid direction {direction:?}")]
    InvalidInstructionMemoryDirection { direction: DspTimingMemoryDirection },
    #[error(
        "DSP timing memory region mismatch at 0x{address:04X}: expected {expected:?}, got {actual:?}"
    )]
    MemoryRegionMismatch {
        address: u16,
        expected: DspTimingMemoryRegion,
        actual: DspTimingMemoryRegion,
    },
    #[error("duplicate DSP timing memory slot {slot:?}")]
    DuplicateMemorySlot { slot: DspTimingMemorySlot },
    #[error("DSP timing memory accesses are not in canonical slot order")]
    MemoryAccessesNotCanonical,
    #[error("DSP timing memory slot {slot:?} is incompatible with instruction bundle {bundle:?}")]
    MemorySlotIncompatibleWithBundle {
        slot: DspTimingMemorySlot,
        bundle: DspTimingBundleKey,
    },
    #[error(
        "DSP timing memory space mismatch for {slot:?}: expected {expected:?}, got {actual:?}"
    )]
    MemorySpaceMismatch {
        slot: DspTimingMemorySlot,
        expected: DspTimingMemorySpace,
        actual: DspTimingMemorySpace,
    },
    #[error(
        "DSP timing memory direction mismatch for {slot:?}: expected {expected:?}, got {actual:?}"
    )]
    MemoryDirectionMismatch {
        slot: DspTimingMemorySlot,
        expected: DspTimingMemoryDirection,
        actual: DspTimingMemoryDirection,
    },
    #[error("decoded DSP instruction-memory writes are unrepresentable in timing keys")]
    UnrepresentableInstructionMemoryWrite,
    #[error(
        "DSP timing memory-slot inventory mismatch: expected 0x{expected:02X}, got 0x{actual:02X}"
    )]
    MemorySlotInventoryMismatch { expected: u8, actual: u8 },
    #[error("invalid DSP timing outcome {outcome:?} for decoded instruction")]
    InvalidOutcome {
        outcome: DspTimingInstructionOutcome,
    },
    #[error("DSP timing hardware-loop outcome {outcome:?} cannot occur on HALT")]
    HardwareLoopEdgeOnHalt {
        outcome: DspTimingHardwareLoopOutcome,
    },
    #[error("DSP timing hardware-loop outcome {outcome:?} requires nonzero {stack_name}")]
    HardwareLoopEdgeWithoutRequiredStack {
        outcome: DspTimingHardwareLoopOutcome,
        stack_name: &'static str,
    },
    #[error("DSP timing key accepts an interrupt without a pending interrupt source")]
    InterruptAcceptedWithoutPendingSource,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DspTimingTarget {
    pub platform: String,
    pub hardware_model: String,
    pub hardware_revision: String,
    pub dsp_revision: String,
    pub dsp_clock_hz: u64,
    pub clock_evidence_capture_id: String,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DspTimingCaptureProvenance {
    pub capture_schema_version: u32,
    pub capture_id: String,
    pub kind: DspTimingProfileKind,
    pub source_sha256: String,
    pub captured_at_utc: String,
    pub capture_tool: String,
    pub capture_tool_version: String,
    pub capture_tool_source_revision: String,
    pub measurement_protocol: String,
    pub measurement_protocol_version: u32,
    pub measurement_method: String,
    pub hardware_model: String,
    pub hardware_revision: String,
    pub dsp_revision: String,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DspTimingEvidence {
    pub capture_id: String,
    pub sample_count: u32,
    pub minimum_cycles: u32,
    pub maximum_cycles: u32,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DspTimingRule {
    pub key: DspTimingKey,
    pub cycles: u32,
    pub evidence: Vec<DspTimingEvidence>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DspTimingProfile {
    pub schema_version: u32,
    pub timing_contract_version: u32,
    pub profile_identity: String,
    pub kind: DspTimingProfileKind,
    pub target: DspTimingTarget,
    pub captures: Vec<DspTimingCaptureProvenance>,
    pub rules: Vec<DspTimingRule>,
}

#[derive(Serialize)]
struct DspTimingProfileIdentityPayload<'a> {
    schema_version: u32,
    timing_contract_version: u32,
    kind: DspTimingProfileKind,
    target: &'a DspTimingTarget,
    captures: &'a [DspTimingCaptureProvenance],
    rules: &'a [DspTimingRule],
}

impl DspTimingProfile {
    pub fn from_json(bytes: &[u8]) -> Result<Self, DspTimingProfileDocumentError> {
        serde_json::from_slice(bytes).map_err(DspTimingProfileDocumentError::Json)
    }

    pub fn to_canonical_json(&self) -> Result<Vec<u8>, DspTimingProfileDocumentError> {
        validate_profile(self, false)
            .map_err(|source| DspTimingProfileDocumentError::Profile(Box::new(source)))?;
        serde_json::to_vec(self).map_err(DspTimingProfileDocumentError::Json)
    }

    pub fn compute_identity(&self) -> String {
        let payload = DspTimingProfileIdentityPayload {
            schema_version: self.schema_version,
            timing_contract_version: self.timing_contract_version,
            kind: self.kind,
            target: &self.target,
            captures: &self.captures,
            rules: &self.rules,
        };
        // Struct field order and canonical vector order are part of schema v2.
        // Validation rejects unsorted captures/rules/evidence before a profile is
        // eligible for use.
        let bytes = serde_json::to_vec(&payload)
            .expect("DSP timing profile identity payload serialization cannot fail");
        format!("sha1:{:x}", Sha1::digest(bytes))
    }

    pub fn refresh_identity(&mut self) {
        self.profile_identity = self.compute_identity();
    }

    pub fn validate(self) -> Result<ValidatedDspTimingProfile, DspTimingProfileError> {
        validate_profile(&self, false)?;
        let rules = self
            .rules
            .iter()
            .map(|rule| (rule.key.clone(), rule.cycles))
            .collect();
        Ok(ValidatedDspTimingProfile {
            profile: self,
            rules,
            production_qualified: false,
        })
    }

    pub fn validate_for_production(
        self,
    ) -> Result<ValidatedDspTimingProfile, DspTimingProfileError> {
        validate_profile(&self, true)?;
        let rules = self
            .rules
            .iter()
            .map(|rule| (rule.key.clone(), rule.cycles))
            .collect();
        Ok(ValidatedDspTimingProfile {
            profile: self,
            rules,
            production_qualified: true,
        })
    }
}

#[derive(Debug, Error)]
pub enum DspTimingProfileDocumentError {
    #[error("invalid DSP timing profile JSON: {0}")]
    Json(serde_json::Error),
    #[error(transparent)]
    Profile(#[from] Box<DspTimingProfileError>),
}

fn validate_profile(
    profile: &DspTimingProfile,
    production: bool,
) -> Result<(), DspTimingProfileError> {
    if profile.schema_version != DSP_TIMING_PROFILE_SCHEMA_VERSION {
        return Err(DspTimingProfileError::UnsupportedSchemaVersion {
            actual: profile.schema_version,
            expected: DSP_TIMING_PROFILE_SCHEMA_VERSION,
        });
    }
    if profile.timing_contract_version != DSP_TIMING_CONTRACT_VERSION {
        return Err(DspTimingProfileError::UnsupportedContractVersion {
            actual: profile.timing_contract_version,
            expected: DSP_TIMING_CONTRACT_VERSION,
        });
    }
    if production && profile.kind != DspTimingProfileKind::RetailWiiCapture {
        return Err(DspTimingProfileError::SyntheticProfileNotProduction);
    }
    validate_nonempty("target.platform", &profile.target.platform)?;
    if production && profile.target.platform != "retail_wii" {
        return Err(DspTimingProfileError::InvalidProductionPlatform {
            actual: profile.target.platform.clone(),
        });
    }
    validate_nonempty("target.hardware_model", &profile.target.hardware_model)?;
    validate_nonempty(
        "target.hardware_revision",
        &profile.target.hardware_revision,
    )?;
    validate_nonempty("target.dsp_revision", &profile.target.dsp_revision)?;
    if profile.target.dsp_clock_hz == 0 {
        return Err(DspTimingProfileError::ZeroDspClock);
    }
    validate_identifier(
        "target.clock_evidence_capture_id",
        &profile.target.clock_evidence_capture_id,
    )?;
    if profile.captures.is_empty() {
        return Err(DspTimingProfileError::MissingCaptures);
    }
    if profile.rules.is_empty() {
        return Err(DspTimingProfileError::MissingRules);
    }

    let mut capture_ids = BTreeSet::new();
    let mut previous_capture_id: Option<&str> = None;
    for capture in &profile.captures {
        validate_capture(capture, production)?;
        if let Some(previous) = previous_capture_id {
            if previous >= capture.capture_id.as_str() {
                return Err(if previous == capture.capture_id {
                    DspTimingProfileError::DuplicateCaptureId {
                        capture_id: capture.capture_id.clone(),
                    }
                } else {
                    DspTimingProfileError::CapturesNotCanonical
                });
            }
        }
        previous_capture_id = Some(&capture.capture_id);
        capture_ids.insert(capture.capture_id.as_str());
        if capture.hardware_model != profile.target.hardware_model
            || capture.hardware_revision != profile.target.hardware_revision
            || capture.dsp_revision != profile.target.dsp_revision
        {
            return Err(DspTimingProfileError::CaptureTargetMismatch {
                capture_id: capture.capture_id.clone(),
            });
        }
    }
    if !capture_ids.contains(profile.target.clock_evidence_capture_id.as_str()) {
        return Err(DspTimingProfileError::UnknownClockEvidenceCapture {
            capture_id: profile.target.clock_evidence_capture_id.clone(),
        });
    }

    let mut previous_key: Option<&DspTimingKey> = None;
    for rule in &profile.rules {
        rule.key
            .validate()
            .map_err(|source| DspTimingProfileError::InvalidTimingKey { source })?;
        if let Some(previous) = previous_key {
            if previous >= &rule.key {
                return Err(if previous == &rule.key {
                    DspTimingProfileError::DuplicateTimingKey {
                        key: Box::new(rule.key.clone()),
                    }
                } else {
                    DspTimingProfileError::RulesNotCanonical
                });
            }
        }
        previous_key = Some(&rule.key);
        if rule.cycles == 0 {
            return Err(DspTimingProfileError::ZeroCycles {
                key: Box::new(rule.key.clone()),
            });
        }
        if rule.evidence.is_empty() {
            return Err(DspTimingProfileError::MissingRuleEvidence {
                key: Box::new(rule.key.clone()),
            });
        }
        let mut previous_evidence: Option<&str> = None;
        for evidence in &rule.evidence {
            validate_identifier("rule.evidence.capture_id", &evidence.capture_id)?;
            if let Some(previous) = previous_evidence {
                if previous >= evidence.capture_id.as_str() {
                    return Err(if previous == evidence.capture_id {
                        DspTimingProfileError::DuplicateRuleEvidence {
                            key: Box::new(rule.key.clone()),
                            capture_id: evidence.capture_id.clone(),
                        }
                    } else {
                        DspTimingProfileError::RuleEvidenceNotCanonical {
                            key: Box::new(rule.key.clone()),
                        }
                    });
                }
            }
            previous_evidence = Some(&evidence.capture_id);
            if !capture_ids.contains(evidence.capture_id.as_str()) {
                return Err(DspTimingProfileError::UnknownEvidenceCapture {
                    key: Box::new(rule.key.clone()),
                    capture_id: evidence.capture_id.clone(),
                });
            }
            if evidence.sample_count == 0 {
                return Err(DspTimingProfileError::ZeroEvidenceSamples {
                    key: Box::new(rule.key.clone()),
                    capture_id: evidence.capture_id.clone(),
                });
            }
            if evidence.minimum_cycles != evidence.maximum_cycles {
                return Err(DspTimingProfileError::NonExactEvidence {
                    key: Box::new(rule.key.clone()),
                    capture_id: evidence.capture_id.clone(),
                    minimum_cycles: evidence.minimum_cycles,
                    maximum_cycles: evidence.maximum_cycles,
                });
            }
            if evidence.minimum_cycles != rule.cycles {
                return Err(DspTimingProfileError::EvidenceCycleMismatch {
                    key: Box::new(rule.key.clone()),
                    capture_id: evidence.capture_id.clone(),
                    rule_cycles: rule.cycles,
                    evidence_cycles: evidence.minimum_cycles,
                });
            }
        }
    }

    let computed = profile.compute_identity();
    if profile.profile_identity != computed {
        return Err(DspTimingProfileError::IdentityMismatch {
            declared: profile.profile_identity.clone(),
            computed,
        });
    }
    if production && !APPROVED_RETAIL_DSP_TIMING_PROFILE_IDENTITIES.contains(&computed.as_str()) {
        return Err(DspTimingProfileError::UnapprovedRetailProfile { identity: computed });
    }
    Ok(())
}

fn validate_capture(
    capture: &DspTimingCaptureProvenance,
    production: bool,
) -> Result<(), DspTimingProfileError> {
    if capture.capture_schema_version != DSP_TIMING_CAPTURE_SCHEMA_VERSION {
        return Err(DspTimingProfileError::UnsupportedCaptureSchemaVersion {
            capture_id: capture.capture_id.clone(),
            actual: capture.capture_schema_version,
            expected: DSP_TIMING_CAPTURE_SCHEMA_VERSION,
        });
    }
    validate_identifier("capture.capture_id", &capture.capture_id)?;
    validate_sha256(&capture.capture_id, &capture.source_sha256)?;
    validate_utc_timestamp(&capture.capture_id, &capture.captured_at_utc)?;
    validate_nonempty("capture.capture_tool", &capture.capture_tool)?;
    validate_nonempty(
        "capture.capture_tool_version",
        &capture.capture_tool_version,
    )?;
    validate_source_revision(&capture.capture_id, &capture.capture_tool_source_revision)?;
    validate_identifier(
        "capture.measurement_protocol",
        &capture.measurement_protocol,
    )?;
    if capture.measurement_protocol_version == 0 {
        return Err(DspTimingProfileError::ZeroMeasurementProtocolVersion {
            capture_id: capture.capture_id.clone(),
        });
    }
    validate_nonempty("capture.measurement_method", &capture.measurement_method)?;
    validate_nonempty("capture.hardware_model", &capture.hardware_model)?;
    validate_nonempty("capture.hardware_revision", &capture.hardware_revision)?;
    validate_nonempty("capture.dsp_revision", &capture.dsp_revision)?;
    if production && capture.kind != DspTimingProfileKind::RetailWiiCapture {
        return Err(DspTimingProfileError::SyntheticCaptureNotProduction {
            capture_id: capture.capture_id.clone(),
        });
    }
    Ok(())
}

fn validate_nonempty(field: &'static str, value: &str) -> Result<(), DspTimingProfileError> {
    if value.trim().is_empty()
        || value != value.trim()
        || value.len() > 4096
        || value.chars().any(char::is_control)
    {
        Err(DspTimingProfileError::InvalidTextField { field })
    } else {
        Ok(())
    }
}

fn validate_identifier(field: &'static str, value: &str) -> Result<(), DspTimingProfileError> {
    let valid = !value.is_empty()
        && value.len() <= 128
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_' | b'.'));
    if valid {
        Ok(())
    } else {
        Err(DspTimingProfileError::InvalidIdentifier {
            field,
            value: value.to_string(),
        })
    }
}

fn validate_sha256(capture_id: &str, value: &str) -> Result<(), DspTimingProfileError> {
    if value.len() == 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
    {
        Ok(())
    } else {
        Err(DspTimingProfileError::InvalidCaptureSha256 {
            capture_id: capture_id.to_string(),
        })
    }
}

fn validate_source_revision(capture_id: &str, value: &str) -> Result<(), DspTimingProfileError> {
    if (7..=64).contains(&value.len())
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
    {
        Ok(())
    } else {
        Err(DspTimingProfileError::InvalidCaptureToolSourceRevision {
            capture_id: capture_id.to_string(),
        })
    }
}

fn validate_utc_timestamp(capture_id: &str, value: &str) -> Result<(), DspTimingProfileError> {
    let bytes = value.as_bytes();
    let digits = |start: usize, end: usize| {
        bytes
            .get(start..end)
            .is_some_and(|part| part.iter().all(u8::is_ascii_digit))
    };
    let parse = |start: usize, end: usize| -> Option<u32> {
        std::str::from_utf8(bytes.get(start..end)?)
            .ok()?
            .parse()
            .ok()
    };
    let valid_shape = bytes.len() == 20
        && digits(0, 4)
        && bytes[4] == b'-'
        && digits(5, 7)
        && bytes[7] == b'-'
        && digits(8, 10)
        && bytes[10] == b'T'
        && digits(11, 13)
        && bytes[13] == b':'
        && digits(14, 16)
        && bytes[16] == b':'
        && digits(17, 19)
        && bytes[19] == b'Z';
    let valid_ranges = valid_shape
        && parse(0, 4).is_some_and(|year| year != 0)
        && parse(5, 7).is_some_and(|month| (1..=12).contains(&month))
        && parse(0, 4)
            .zip(parse(5, 7))
            .zip(parse(8, 10))
            .is_some_and(|((year, month), day)| {
                let leap_year = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
                let days_in_month = match month {
                    2 if leap_year => 29,
                    2 => 28,
                    4 | 6 | 9 | 11 => 30,
                    _ => 31,
                };
                (1..=days_in_month).contains(&day)
            })
        && parse(11, 13).is_some_and(|hour| hour <= 23)
        && parse(14, 16).is_some_and(|minute| minute <= 59)
        && parse(17, 19).is_some_and(|second| second <= 59);
    if valid_ranges {
        Ok(())
    } else {
        Err(DspTimingProfileError::InvalidCaptureTimestamp {
            capture_id: capture_id.to_string(),
            value: value.to_string(),
        })
    }
}

#[derive(Debug, Error, PartialEq, Eq)]
pub enum DspTimingProfileError {
    #[error("unsupported DSP timing profile schema {actual}; expected {expected}")]
    UnsupportedSchemaVersion { actual: u32, expected: u32 },
    #[error("unsupported DSP timing contract {actual}; expected {expected}")]
    UnsupportedContractVersion { actual: u32, expected: u32 },
    #[error("DSP timing capture {capture_id:?} uses schema {actual}; expected {expected}")]
    UnsupportedCaptureSchemaVersion {
        capture_id: String,
        actual: u32,
        expected: u32,
    },
    #[error("synthetic DSP timing profiles cannot qualify production lowering")]
    SyntheticProfileNotProduction,
    #[error("synthetic DSP timing capture {capture_id:?} cannot qualify production lowering")]
    SyntheticCaptureNotProduction { capture_id: String },
    #[error("production DSP timing target platform must be retail_wii, got {actual:?}")]
    InvalidProductionPlatform { actual: String },
    #[error("retail DSP timing profile {identity:?} has not been source-control approved")]
    UnapprovedRetailProfile { identity: String },
    #[error("invalid or non-canonical text in DSP timing field {field}")]
    InvalidTextField { field: &'static str },
    #[error("invalid DSP timing identifier {value:?} in field {field}")]
    InvalidIdentifier { field: &'static str, value: String },
    #[error("DSP timing target clock must be measured and nonzero")]
    ZeroDspClock,
    #[error("DSP timing profile has no source captures")]
    MissingCaptures,
    #[error("DSP timing profile has no timing rules")]
    MissingRules,
    #[error("DSP timing captures are not in canonical capture-id order")]
    CapturesNotCanonical,
    #[error("duplicate DSP timing capture id {capture_id:?}")]
    DuplicateCaptureId { capture_id: String },
    #[error("DSP timing capture {capture_id:?} has an invalid lowercase SHA-256")]
    InvalidCaptureSha256 { capture_id: String },
    #[error("DSP timing capture {capture_id:?} has an invalid tool source revision")]
    InvalidCaptureToolSourceRevision { capture_id: String },
    #[error("DSP timing capture {capture_id:?} has invalid UTC timestamp {value:?}")]
    InvalidCaptureTimestamp { capture_id: String, value: String },
    #[error("DSP timing capture {capture_id:?} has measurement protocol version zero")]
    ZeroMeasurementProtocolVersion { capture_id: String },
    #[error("DSP timing capture {capture_id:?} does not match the profile target")]
    CaptureTargetMismatch { capture_id: String },
    #[error("DSP clock evidence references unknown capture {capture_id:?}")]
    UnknownClockEvidenceCapture { capture_id: String },
    #[error("invalid DSP timing key: {source}")]
    InvalidTimingKey { source: DspTimingKeyError },
    #[error("DSP timing rules are not in canonical key order")]
    RulesNotCanonical,
    #[error("duplicate DSP timing key {key:?}")]
    DuplicateTimingKey { key: Box<DspTimingKey> },
    #[error("DSP timing key {key:?} has zero cycles")]
    ZeroCycles { key: Box<DspTimingKey> },
    #[error("DSP timing key {key:?} has no capture evidence")]
    MissingRuleEvidence { key: Box<DspTimingKey> },
    #[error("DSP timing evidence for key {key:?} is not in canonical capture-id order")]
    RuleEvidenceNotCanonical { key: Box<DspTimingKey> },
    #[error("duplicate evidence capture {capture_id:?} for DSP timing key {key:?}")]
    DuplicateRuleEvidence {
        key: Box<DspTimingKey>,
        capture_id: String,
    },
    #[error("unknown evidence capture {capture_id:?} for DSP timing key {key:?}")]
    UnknownEvidenceCapture {
        key: Box<DspTimingKey>,
        capture_id: String,
    },
    #[error("zero evidence samples in capture {capture_id:?} for DSP timing key {key:?}")]
    ZeroEvidenceSamples {
        key: Box<DspTimingKey>,
        capture_id: String,
    },
    #[error(
        "non-exact evidence in capture {capture_id:?} for DSP timing key {key:?}: {minimum_cycles}..={maximum_cycles} cycles"
    )]
    NonExactEvidence {
        key: Box<DspTimingKey>,
        capture_id: String,
        minimum_cycles: u32,
        maximum_cycles: u32,
    },
    #[error(
        "evidence in capture {capture_id:?} reports {evidence_cycles} cycles for DSP timing key {key:?}, but rule declares {rule_cycles}"
    )]
    EvidenceCycleMismatch {
        key: Box<DspTimingKey>,
        capture_id: String,
        rule_cycles: u32,
        evidence_cycles: u32,
    },
    #[error("DSP timing profile identity mismatch: declared {declared:?}, computed {computed:?}")]
    IdentityMismatch { declared: String, computed: String },
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ValidatedDspTimingProfile {
    profile: DspTimingProfile,
    rules: BTreeMap<DspTimingKey, u32>,
    production_qualified: bool,
}

impl ValidatedDspTimingProfile {
    pub fn profile(&self) -> &DspTimingProfile {
        &self.profile
    }

    pub fn profile_identity(&self) -> &str {
        &self.profile.profile_identity
    }

    pub fn production_qualified(&self) -> bool {
        self.production_qualified
    }

    pub fn cycles_for(&self, key: &DspTimingKey) -> Result<u32, DspTimingResolutionError> {
        self.rules
            .get(key)
            .copied()
            .ok_or_else(|| DspTimingResolutionError::UnknownTiming {
                key: key.clone(),
                profile_identity: self.profile.profile_identity.clone(),
            })
    }
}

pub fn require_dsp_timing<'a>(
    profile: Option<&'a ValidatedDspTimingProfile>,
    key: &DspTimingKey,
) -> Result<(&'a ValidatedDspTimingProfile, u32), DspTimingResolutionError> {
    let profile = profile.ok_or(DspTimingResolutionError::MissingTimingProfile)?;
    if !profile.production_qualified() {
        return Err(DspTimingResolutionError::UnqualifiedTimingProfile {
            profile_identity: profile.profile_identity().to_string(),
        });
    }
    let cycles = profile.cycles_for(key)?;
    Ok((profile, cycles))
}

/// Gate for production DSP lowering until generated code and the native worker
/// use the complete timing-contract retirement and scheduling boundary.
///
/// Profile approval is necessary but not sufficient. Schema v2 keys contain
/// dynamic pre-instruction state and post-instruction outcomes that the
/// current generated entry does not capture coherently. Runtime foundation
/// primitives are intentionally insufficient: the emitter does not yet supply
/// exact predecessor, interrupt/outcome state, or a timing-ledger transaction.
/// Generated lowering now emits decoder-authoritative memory address plans to
/// a nullable test-only raw observer and schema v2 represents CR, open-bus data,
/// and masked-IROM accesses, but no product worker installs that observer or
/// connects its records to an authenticated timing table. Accelerator exception
/// pending state still has no reviewed latch,
/// and no product reset/worker installs the logical-depth sidecar. The native foundation exposes only fresh
/// retirement/timeline reset: neither prior retirement identity nor cycle
/// checkpoint history has an authenticated restore provider, and no grant is
/// connected to a reviewed deterministic event scheduler. Returning a binding before all of
/// those pieces exist would let metadata be mistaken for timing-correct
/// execution.
pub fn require_production_dsp_timing_execution(
    profile: Option<&ValidatedDspTimingProfile>,
) -> Result<DspTimingContractBinding, DspTimingExecutionError> {
    let profile = profile.ok_or(DspTimingExecutionError::MissingTimingProfile)?;
    if !profile.production_qualified() {
        return Err(DspTimingExecutionError::UnqualifiedTimingProfile {
            profile_identity: profile.profile_identity().to_string(),
        });
    }

    Err(DspTimingExecutionError::RuntimeAccountingUnavailable {
        contract_version: DSP_TIMING_CONTRACT_VERSION,
        profile_identity: profile.profile_identity().to_string(),
        runtime_plan_foundation_ready: production_runtime_plan_foundation_ready(),
    })
}

fn production_runtime_plan_foundation_ready() -> bool {
    // Keep every inert install-plan entry point compile-checked while making
    // the product capability bit unconditionally false. This function must
    // not become true merely because these constructors and the nullable raw
    // provenance descriptors compile: authenticated table publication, product
    // observer/ledger installation, accelerator state, exact timing-outcome
    // retirement hooks, and deterministic scheduling remain absent.
    let _ = DspTimingRuntimePlan::from_words;
    let _ = DspTimingRuntimePlan::validate;
    let _ = DspTimingRuntimePlan::authorized_table_entry;
    let _ = DspTimingRuntimePlan::validate_authorized_table_entry;
    let _ = DspTimingHardwareLoopSite::MayResolveLoopEnd;
    false
}

#[derive(Clone, Debug, Error, PartialEq, Eq)]
pub enum DspTimingResolutionError {
    #[error("DSP timing profile is required; untimed production lowering is forbidden")]
    MissingTimingProfile,
    #[error("DSP timing profile {profile_identity:?} is not production-qualified")]
    UnqualifiedTimingProfile { profile_identity: String },
    #[error("DSP timing is unknown for key {key:?} in profile {profile_identity:?}")]
    UnknownTiming {
        key: DspTimingKey,
        profile_identity: String,
    },
}

#[derive(Clone, Debug, Error, PartialEq, Eq)]
pub enum DspTimingExecutionError {
    #[error("DSP timing profile is required; untimed production lowering is forbidden")]
    MissingTimingProfile,
    #[error("DSP timing profile {profile_identity:?} is not production-qualified")]
    UnqualifiedTimingProfile { profile_identity: String },
    #[error(
        "production DSP timing execution is unavailable for contract {contract_version} profile {profile_identity:?} (runtime_plan_foundation_ready={runtime_plan_foundation_ready}): nullable raw per-slot provenance is not connected to generated exact-key timing capture/commit, a product timing ledger, truthful accelerator pending state, authenticated retirement/cycle checkpoint restore, or deterministic DSP-clock scheduling"
    )]
    RuntimeAccountingUnavailable {
        contract_version: u32,
        profile_identity: String,
        runtime_plan_foundation_ready: bool,
    },
}

/// Metadata the generated DSP module and runtime loader must compare before a
/// timing-qualified entry point can execute. This does not change the native
/// ABI structs; it is intended for additional generated exports.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DspTimingContractBinding {
    contract_version: u32,
    profile_identity: String,
}

impl DspTimingContractBinding {
    pub fn contract_version(&self) -> u32 {
        self.contract_version
    }

    pub fn profile_identity(&self) -> &str {
        &self.profile_identity
    }

    pub fn from_profile(
        profile: &ValidatedDspTimingProfile,
    ) -> Result<Self, DspTimingBindingError> {
        if !profile.production_qualified() {
            return Err(DspTimingBindingError::ProfileNotProduction {
                profile_identity: profile.profile_identity().to_string(),
            });
        }
        Ok(Self {
            contract_version: DSP_TIMING_CONTRACT_VERSION,
            profile_identity: profile.profile_identity().to_string(),
        })
    }

    pub fn verify(
        &self,
        runtime_contract_version: u32,
        runtime_profile_identity: &str,
    ) -> Result<(), DspTimingBindingError> {
        if runtime_contract_version != self.contract_version {
            return Err(DspTimingBindingError::ContractVersionMismatch {
                generated: self.contract_version,
                runtime: runtime_contract_version,
            });
        }
        if runtime_profile_identity != self.profile_identity {
            return Err(DspTimingBindingError::ProfileIdentityMismatch {
                generated: self.profile_identity.clone(),
                runtime: runtime_profile_identity.to_string(),
            });
        }
        Ok(())
    }

    pub fn emit_cpp_identity_exports(
        &self,
        symbol_prefix: &str,
    ) -> Result<String, DspTimingBindingError> {
        if !is_cpp_identifier(symbol_prefix) {
            return Err(DspTimingBindingError::InvalidSymbolPrefix {
                symbol_prefix: symbol_prefix.to_string(),
            });
        }
        let mut cpp = String::new();
        cpp.push_str("extern \"C\" std::uint32_t ");
        cpp.push_str(symbol_prefix);
        cpp.push_str("_dsp_timing_contract_version() {\n    return ");
        cpp.push_str(&self.contract_version.to_string());
        cpp.push_str("u;\n}\n\nextern \"C\" const char* ");
        cpp.push_str(symbol_prefix);
        cpp.push_str("_dsp_timing_profile_identity() {\n    return \"");
        cpp.push_str(&self.profile_identity);
        cpp.push_str("\";\n}\n\nextern \"C\" bool ");
        cpp.push_str(symbol_prefix);
        cpp.push_str("_dsp_timing_contract_matches(std::uint32_t runtime_version, const char* runtime_identity) {\n");
        cpp.push_str("    if (runtime_version != ");
        cpp.push_str(&self.contract_version.to_string());
        cpp.push_str("u || runtime_identity == nullptr) {\n        return false;\n    }\n");
        cpp.push_str("    const char* expected = \"");
        cpp.push_str(&self.profile_identity);
        cpp.push_str("\";\n    while (*expected != '\\0' && *runtime_identity == *expected) {\n");
        cpp.push_str("        ++expected;\n        ++runtime_identity;\n    }\n");
        cpp.push_str("    return *expected == *runtime_identity;\n}\n");
        Ok(cpp)
    }
}

fn is_cpp_identifier(value: &str) -> bool {
    let mut characters = value.chars();
    matches!(characters.next(), Some(first) if first == '_' || first.is_ascii_alphabetic())
        && characters.all(|character| character == '_' || character.is_ascii_alphanumeric())
}

#[derive(Clone, Debug, Error, PartialEq, Eq)]
pub enum DspTimingBindingError {
    #[error("DSP timing profile {profile_identity:?} is not production-qualified")]
    ProfileNotProduction { profile_identity: String },
    #[error("invalid generated DSP timing symbol prefix {symbol_prefix:?}")]
    InvalidSymbolPrefix { symbol_prefix: String },
    #[error("generated DSP timing contract {generated} does not match runtime contract {runtime}")]
    ContractVersionMismatch { generated: u32, runtime: u32 },
    #[error(
        "generated DSP timing profile {generated:?} does not match runtime profile {runtime:?}"
    )]
    ProfileIdentityMismatch { generated: String, runtime: String },
}

#[cfg(test)]
mod tests {
    use super::*;

    fn context(fetch_address: u16) -> DspTimingContextKey {
        DspTimingContextKey {
            fetch_address,
            status_register: 0x63a0,
            control_register: 0x00f1,
            address_registers: [0x0001, 0x0fff, 0x1000, 0xffff],
            index_registers: [1, -1, 2, -2],
            wrap_registers: [0xffff, 0x0fff, 0x00ff, 0x000f],
            call_stack_depth: 1,
            data_stack_depth: 2,
            loop_address_stack_depth: 3,
            loop_counter_stack_depth: 4,
            external_interrupt_pending: false,
            accelerator_interrupt_pending: true,
            predecessor: None,
            memory_accesses: Vec::new(),
        }
    }

    fn completed() -> DspTimingOutcomeKey {
        DspTimingOutcomeKey {
            instruction: DspTimingInstructionOutcome::Completed,
            hardware_loop: DspTimingHardwareLoopOutcome::None,
            interrupt: DspTimingInterruptOutcome::None,
        }
    }

    fn key(opcode: u16) -> DspTimingKey {
        DspTimingKey::from_words(&[opcode], completed(), context(0)).unwrap()
    }

    fn capture(capture_id: &str, kind: DspTimingProfileKind) -> DspTimingCaptureProvenance {
        DspTimingCaptureProvenance {
            capture_schema_version: DSP_TIMING_CAPTURE_SCHEMA_VERSION,
            capture_id: capture_id.to_string(),
            kind,
            source_sha256: "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
                .to_string(),
            captured_at_utc: "2000-01-01T00:00:00Z".to_string(),
            capture_tool: "synthetic-test-harness".to_string(),
            capture_tool_version: "1".to_string(),
            capture_tool_source_revision: "0123456".to_string(),
            measurement_protocol: "synthetic_fixture".to_string(),
            measurement_protocol_version: 1,
            measurement_method: "deterministic synthetic fixture".to_string(),
            hardware_model: "synthetic-model".to_string(),
            hardware_revision: "synthetic-revision".to_string(),
            dsp_revision: "synthetic-dsp".to_string(),
        }
    }

    fn profile_with_rules(mut rules: Vec<DspTimingRule>) -> DspTimingProfile {
        rules.sort_by(|left, right| left.key.cmp(&right.key));
        let mut profile = DspTimingProfile {
            schema_version: DSP_TIMING_PROFILE_SCHEMA_VERSION,
            timing_contract_version: DSP_TIMING_CONTRACT_VERSION,
            profile_identity: String::new(),
            kind: DspTimingProfileKind::SyntheticTest,
            target: DspTimingTarget {
                platform: "synthetic".to_string(),
                hardware_model: "synthetic-model".to_string(),
                hardware_revision: "synthetic-revision".to_string(),
                dsp_revision: "synthetic-dsp".to_string(),
                dsp_clock_hz: 1,
                clock_evidence_capture_id: "capture-a".to_string(),
            },
            captures: vec![capture("capture-a", DspTimingProfileKind::SyntheticTest)],
            rules,
        };
        profile.refresh_identity();
        profile
    }

    fn rule(key: DspTimingKey, cycles: u32) -> DspTimingRule {
        DspTimingRule {
            key,
            cycles,
            evidence: vec![DspTimingEvidence {
                capture_id: "capture-a".to_string(),
                sample_count: 4,
                minimum_cycles: cycles,
                maximum_cycles: cycles,
            }],
        }
    }

    #[test]
    fn instruction_key_preserves_immediate_and_parallel_bundle_bits() {
        let immediate = DspTimingInstructionKey::from_words(0, &[0x029f, 0x1234]).unwrap();
        assert_eq!(immediate.opcode_word, 0x029f);
        assert_eq!(immediate.immediate_word, Some(0x1234));
        assert_eq!(immediate.bundle, DspTimingBundleKey::Standalone);

        let parallel = DspTimingInstructionKey::from_words(0, &[0x8401]).unwrap();
        assert_eq!(parallel.immediate_word, None);
        assert_eq!(
            parallel.bundle,
            DspTimingBundleKey::Parallel {
                primary_word: 0x8400,
                parallel_extension: 0x01,
                extension_mask: 0xff,
            }
        );

        let narrow_parallel = DspTimingInstructionKey::from_words(0, &[0x3001]).unwrap();
        assert_eq!(
            narrow_parallel.bundle,
            DspTimingBundleKey::Parallel {
                primary_word: 0x3000,
                parallel_extension: 0x01,
                extension_mask: 0x7f,
            }
        );
    }

    #[test]
    fn runtime_plan_derives_exact_semantics_and_hardware_loop_masks() {
        let ordinary =
            DspTimingRuntimePlan::from_words(0, &[0x0000], DspTimingHardwareLoopSite::NotLoopEnd)
                .unwrap();
        assert_eq!(
            ordinary.instruction_semantic,
            DspTimingInstructionSemantic::Ordinary
        );
        assert_eq!(
            ordinary.allowed_instruction_outcomes,
            DSP_TIMING_COMPLETED_OUTCOME_BIT
        );
        assert_eq!(
            ordinary.allowed_hardware_loop_outcomes,
            DSP_TIMING_NO_HARDWARE_LOOP_OUTCOME_BIT
        );
        assert!(ordinary.validate().is_ok());

        let conditional = DspTimingRuntimePlan::from_words(
            0,
            &[0x0275],
            DspTimingHardwareLoopSite::MayResolveLoopEnd,
        )
        .unwrap();
        assert_eq!(
            conditional.instruction_semantic,
            DspTimingInstructionSemantic::ConditionalDynamic
        );
        assert_eq!(
            conditional.allowed_instruction_outcomes,
            DSP_TIMING_CONDITION_SATISFIED_OUTCOME_BIT
                | DSP_TIMING_CONDITION_NOT_SATISFIED_OUTCOME_BIT
        );
        assert_eq!(
            conditional.allowed_hardware_loop_outcomes,
            DSP_TIMING_ALL_HARDWARE_LOOP_OUTCOME_BITS
        );

        let unconditional =
            DspTimingRuntimePlan::from_words(0, &[0x027f], DspTimingHardwareLoopSite::NotLoopEnd)
                .unwrap();
        assert_eq!(
            unconditional.instruction_semantic,
            DspTimingInstructionSemantic::ConditionalAlways
        );
        assert_eq!(
            unconditional.allowed_instruction_outcomes,
            DSP_TIMING_CONDITION_SATISFIED_OUTCOME_BIT
        );

        for (words, semantic, outcomes) in [
            (
                &[0x1000][..],
                DspTimingInstructionSemantic::LoopAlwaysSkipped,
                DSP_TIMING_ZERO_COUNT_LOOP_SKIPPED_OUTCOME_BIT,
            ),
            (
                &[0x1001][..],
                DspTimingInstructionSemantic::LoopAlwaysArmed,
                DSP_TIMING_LOOP_ARMED_OUTCOME_BIT,
            ),
            (
                &[0x0043][..],
                DspTimingInstructionSemantic::LoopDynamic,
                DSP_TIMING_LOOP_ARMED_OUTCOME_BIT | DSP_TIMING_ZERO_COUNT_LOOP_SKIPPED_OUTCOME_BIT,
            ),
        ] {
            let plan =
                DspTimingRuntimePlan::from_words(0, words, DspTimingHardwareLoopSite::NotLoopEnd)
                    .unwrap();
            assert_eq!(plan.instruction_semantic, semantic);
            assert_eq!(plan.allowed_instruction_outcomes, outcomes);
            assert!(plan.validate().is_ok());
        }

        let halt =
            DspTimingRuntimePlan::from_words(0, &[0x0021], DspTimingHardwareLoopSite::NotLoopEnd)
                .unwrap();
        assert_eq!(
            halt.instruction_semantic,
            DspTimingInstructionSemantic::Halt
        );
        assert_eq!(
            halt.allowed_instruction_outcomes,
            DSP_TIMING_HALTED_OUTCOME_BIT
        );
        assert!(matches!(
            DspTimingRuntimePlan::from_words(
                0,
                &[0x0021],
                DspTimingHardwareLoopSite::MayResolveLoopEnd,
            ),
            Err(DspTimingRuntimePlanError::HaltAtHardwareLoopEnd)
        ));
    }

    #[test]
    fn runtime_plan_identity_is_deterministic_and_covers_contract_fields() {
        let plan = DspTimingRuntimePlan::from_words(
            0x0123,
            &[0x00da, 0x8123],
            DspTimingHardwareLoopSite::MayResolveLoopEnd,
        )
        .unwrap();
        let same = DspTimingRuntimePlan::from_words(
            0x0123,
            &[0x00da, 0x8123],
            DspTimingHardwareLoopSite::MayResolveLoopEnd,
        )
        .unwrap();
        assert_eq!(plan, same);
        assert_ne!(
            plan.resolver_plan_identity,
            [0; DSP_TIMING_RUNTIME_PLAN_IDENTITY_BYTES]
        );
        assert!(plan.validate().is_ok());
        assert!(plan
            .validate_authorized_table_entry(plan.authorized_table_entry())
            .is_ok());

        let mut wrong_entry = plan.authorized_table_entry();
        wrong_entry.resolver_plan_identity[19] ^= 1;
        assert!(matches!(
            plan.validate_authorized_table_entry(wrong_entry),
            Err(DspTimingRuntimePlanError::AuthorizedTableEntryMismatch { .. })
        ));
        let mut wrong_entry = plan.authorized_table_entry();
        wrong_entry.contract_version += 1;
        assert!(matches!(
            plan.validate_authorized_table_entry(wrong_entry),
            Err(DspTimingRuntimePlanError::AuthorizedTableEntryMismatch { .. })
        ));

        let changed_address = DspTimingRuntimePlan::from_words(
            0x0124,
            &[0x00da, 0x8123],
            DspTimingHardwareLoopSite::MayResolveLoopEnd,
        )
        .unwrap();
        let mut changed_memory = plan.clone();
        changed_memory.expected_memory_slots = 0;
        changed_memory.resolver_plan_identity = changed_memory.compute_resolver_plan_identity();
        let mut changed_direction = plan.clone();
        changed_direction.memory_operands[0]
            .as_mut()
            .unwrap()
            .direction = DspTimingMemoryDirection::Write;
        changed_direction.resolver_plan_identity =
            changed_direction.compute_resolver_plan_identity();
        let changed_site = DspTimingRuntimePlan::from_words(
            0x0123,
            &[0x00da, 0x8123],
            DspTimingHardwareLoopSite::NotLoopEnd,
        )
        .unwrap();
        assert_ne!(
            plan.resolver_plan_identity,
            changed_address.resolver_plan_identity
        );
        assert_ne!(
            plan.resolver_plan_identity,
            changed_memory.resolver_plan_identity
        );
        assert!(matches!(
            changed_memory.validate(),
            Err(DspTimingRuntimePlanError::MemorySlotMaskMismatch {
                expected: DSP_TIMING_STANDALONE_MEMORY_BIT,
                actual: 0,
            })
        ));
        assert_ne!(
            plan.resolver_plan_identity,
            changed_direction.resolver_plan_identity
        );
        assert!(matches!(
            changed_direction.validate(),
            Err(DspTimingRuntimePlanError::MemoryOperandAuthorityMismatch { .. })
        ));
        assert_ne!(
            plan.resolver_plan_identity,
            changed_site.resolver_plan_identity
        );
    }

    #[test]
    fn runtime_plan_derives_exact_encoded_memory_operand_slots() {
        let no_access =
            DspTimingRuntimePlan::from_words(0, &[0x0000], DspTimingHardwareLoopSite::NotLoopEnd)
                .unwrap();
        let standalone = DspTimingRuntimePlan::from_words(
            0,
            &[0x00da, 0x0123],
            DspTimingHardwareLoopSite::NotLoopEnd,
        )
        .unwrap();
        let parallel_none =
            DspTimingRuntimePlan::from_words(0, &[0x8401], DspTimingHardwareLoopSite::NotLoopEnd)
                .unwrap();
        let parallel_single =
            DspTimingRuntimePlan::from_words(0, &[0x803b], DspTimingHardwareLoopSite::NotLoopEnd)
                .unwrap();
        let parallel_dual =
            DspTimingRuntimePlan::from_words(0, &[0x80ae], DspTimingHardwareLoopSite::NotLoopEnd)
                .unwrap();
        assert_eq!(no_access.expected_memory_slots, 0);
        assert_eq!(
            standalone.expected_memory_slots,
            DSP_TIMING_STANDALONE_MEMORY_BIT
        );
        assert_eq!(parallel_none.expected_memory_slots, 0);
        assert_eq!(
            parallel_single.expected_memory_slots,
            DSP_TIMING_PARALLEL_PRIMARY_MEMORY_BIT
        );
        assert_eq!(
            parallel_dual.expected_memory_slots,
            DSP_TIMING_PARALLEL_PRIMARY_MEMORY_BIT | DSP_TIMING_PARALLEL_SECONDARY_MEMORY_BIT
        );
        for plan in [
            no_access,
            standalone,
            parallel_none,
            parallel_single,
            parallel_dual,
        ] {
            assert!(plan.validate().is_ok());
        }
    }

    #[test]
    fn runtime_plan_memory_slot_derivation_covers_every_decoded_operand_family() {
        for (words, expected) in [
            (&[0x0093, 0xabcd][..], 0),
            (&[0x00da, 0x0123][..], DSP_TIMING_STANDALONE_MEMORY_BIT),
            (&[0x00ff, 0x0321][..], DSP_TIMING_STANDALONE_MEMORY_BIT),
            (&[0x1ef9][..], 0),
            (&[0x16fe, 0xbeef][..], DSP_TIMING_STANDALONE_MEMORY_BIT),
            (&[0x0212][..], DSP_TIMING_STANDALONE_MEMORY_BIT),
            (&[0x1853][..], DSP_TIMING_STANDALONE_MEMORY_BIT),
            (&[0x1a53][..], DSP_TIMING_STANDALONE_MEMORY_BIT),
            (&[0x2304][..], DSP_TIMING_STANDALONE_MEMORY_BIT),
            (&[0x29aa][..], DSP_TIMING_STANDALONE_MEMORY_BIT),
            (&[0x2f55][..], DSP_TIMING_STANDALONE_MEMORY_BIT),
            (&[0x8401][..], 0),
            (&[0x8007][..], 0),
            (&[0x801f][..], 0),
            (&[0x803b][..], DSP_TIMING_PARALLEL_PRIMARY_MEMORY_BIT),
            (&[0x807b][..], DSP_TIMING_PARALLEL_PRIMARY_MEMORY_BIT),
            (
                &[0x80ae][..],
                DSP_TIMING_PARALLEL_PRIMARY_MEMORY_BIT | DSP_TIMING_PARALLEL_SECONDARY_MEMORY_BIT,
            ),
            (
                &[0x80d7][..],
                DSP_TIMING_PARALLEL_PRIMARY_MEMORY_BIT | DSP_TIMING_PARALLEL_SECONDARY_MEMORY_BIT,
            ),
            (
                &[0x80ec][..],
                DSP_TIMING_PARALLEL_PRIMARY_MEMORY_BIT | DSP_TIMING_PARALLEL_SECONDARY_MEMORY_BIT,
            ),
        ] {
            let plan =
                DspTimingRuntimePlan::from_words(0, words, DspTimingHardwareLoopSite::NotLoopEnd)
                    .unwrap();
            assert_eq!(plan.expected_memory_slots, expected, "words={words:04X?}");
            assert!(plan.validate().is_ok());
        }
    }

    #[test]
    fn runtime_plan_preserves_ls_sl_and_dual_load_operand_authority() {
        let load_then_store =
            DspTimingRuntimePlan::from_words(0, &[0x80ac], DspTimingHardwareLoopSite::NotLoopEnd)
                .unwrap();
        let store_then_load =
            DspTimingRuntimePlan::from_words(0, &[0x80ae], DspTimingHardwareLoopSite::NotLoopEnd)
                .unwrap();
        let dual_load =
            DspTimingRuntimePlan::from_words(0, &[0x80d7], DspTimingHardwareLoopSite::NotLoopEnd)
                .unwrap();
        let data_read = Some(DspTimingRuntimeMemoryOperand {
            space: DspTimingMemorySpace::Data,
            direction: DspTimingMemoryDirection::Read,
        });
        let data_write = Some(DspTimingRuntimeMemoryOperand {
            space: DspTimingMemorySpace::Data,
            direction: DspTimingMemoryDirection::Write,
        });
        assert_eq!(
            load_then_store.memory_operands,
            [None, data_read, data_write]
        );
        assert_eq!(
            store_then_load.memory_operands,
            [None, data_write, data_read]
        );
        assert_eq!(dual_load.memory_operands, [None, data_read, data_read]);
        assert_ne!(
            load_then_store.resolver_plan_identity,
            store_then_load.resolver_plan_identity
        );
    }

    #[test]
    fn runtime_plan_validation_rejects_length_bundle_mask_and_identity_lies() {
        assert!(matches!(
            DspTimingRuntimePlan::from_words(
                0,
                &[0x0000, 0x0000],
                DspTimingHardwareLoopSite::NotLoopEnd,
            ),
            Err(DspTimingRuntimePlanError::InstructionWordCountMismatch { .. })
        ));
        let parallel =
            DspTimingRuntimePlan::from_words(0, &[0x80ae], DspTimingHardwareLoopSite::NotLoopEnd)
                .unwrap();
        assert_eq!(parallel.instruction_word_count, 1);
        assert_eq!(parallel.validated_bundle, parallel.instruction.bundle);
        assert!(parallel.validate().is_ok());

        let mut secondary_only = parallel.clone();
        secondary_only.expected_memory_slots = DSP_TIMING_PARALLEL_SECONDARY_MEMORY_BIT;
        secondary_only.memory_operands[1] = None;
        secondary_only.resolver_plan_identity = secondary_only.compute_resolver_plan_identity();
        assert!(matches!(
            secondary_only.validate(),
            Err(DspTimingRuntimePlanError::MemorySlotMaskIncompatibleWithBundle { .. })
        ));

        let base =
            DspTimingRuntimePlan::from_words(0, &[0x0275], DspTimingHardwareLoopSite::NotLoopEnd)
                .unwrap();

        let mut invalid = base.clone();
        invalid.contract_version += 1;
        assert!(matches!(
            invalid.validate(),
            Err(DspTimingRuntimePlanError::UnsupportedContractVersion { .. })
        ));

        let mut invalid = base.clone();
        invalid.instruction_word_count = 2;
        assert!(matches!(
            invalid.validate(),
            Err(DspTimingRuntimePlanError::InstructionWordCountMismatch { .. })
        ));

        let mut invalid = base.clone();
        invalid.validated_bundle = DspTimingBundleKey::Parallel {
            primary_word: 0,
            parallel_extension: 0,
            extension_mask: 0xff,
        };
        assert!(matches!(
            invalid.validate(),
            Err(DspTimingRuntimePlanError::ValidatedBundleMismatch { .. })
        ));

        let mut invalid = base.clone();
        invalid.instruction_semantic = DspTimingInstructionSemantic::Ordinary;
        assert!(matches!(
            invalid.validate(),
            Err(DspTimingRuntimePlanError::InstructionSemanticMismatch { .. })
        ));

        let mut invalid = base.clone();
        invalid.allowed_instruction_outcomes = DSP_TIMING_COMPLETED_OUTCOME_BIT;
        assert!(matches!(
            invalid.validate(),
            Err(DspTimingRuntimePlanError::InstructionOutcomeMaskMismatch { .. })
        ));

        let mut invalid = base.clone();
        invalid.allowed_hardware_loop_outcomes = DSP_TIMING_ALL_HARDWARE_LOOP_OUTCOME_BITS;
        assert!(matches!(
            invalid.validate(),
            Err(DspTimingRuntimePlanError::HardwareLoopOutcomeMaskMismatch { .. })
        ));

        let mut invalid = base;
        invalid.resolver_plan_identity = [0; DSP_TIMING_RUNTIME_PLAN_IDENTITY_BYTES];
        assert!(matches!(
            invalid.validate(),
            Err(DspTimingRuntimePlanError::ResolverPlanIdentityMismatch { .. })
        ));
    }

    #[test]
    fn key_distinguishes_outcome_context_predecessor_and_memory_slots() {
        let base = key(0x0000);
        let mut changed_outcome = base.clone();
        changed_outcome.outcome.hardware_loop = DspTimingHardwareLoopOutcome::BackEdgeTaken;
        assert_ne!(base, changed_outcome);

        let mut changed_status = base.clone();
        changed_status.context.status_register ^= 1;
        assert_ne!(base, changed_status);

        let mut changed_control = base.clone();
        changed_control.context.control_register ^= 1;
        assert_ne!(base, changed_control);

        let mut changed_predecessor = base.clone();
        changed_predecessor.context.predecessor =
            Some(DspTimingPredecessorKey::from_words(0x8000, &[0x0021]).unwrap());
        assert_ne!(base, changed_predecessor);

        let mut changed_memory = base.clone();
        changed_memory.context.memory_accesses.push(
            DspTimingMemoryAccessKey::data(
                DspTimingMemorySlot::Standalone,
                DspTimingMemoryDirection::Read,
                0xf012,
            )
            .unwrap(),
        );
        assert_ne!(base, changed_memory);
    }

    #[test]
    fn key_validation_rejects_bundle_length_region_order_and_outcome_mismatches() {
        let mut bad_bundle = key(0x0000);
        bad_bundle.instruction.bundle = DspTimingBundleKey::Parallel {
            primary_word: 0,
            parallel_extension: 1,
            extension_mask: 0xff,
        };
        assert!(matches!(
            bad_bundle.validate(),
            Err(DspTimingKeyError::BundleMismatch { .. })
        ));

        let mut bad_length = key(0x0000);
        bad_length.instruction.immediate_word = Some(0);
        assert!(matches!(
            bad_length.validate(),
            Err(DspTimingKeyError::InstructionLengthMismatch { .. })
        ));

        let mut bad_region = key(0x0000);
        bad_region
            .context
            .memory_accesses
            .push(DspTimingMemoryAccessKey {
                slot: DspTimingMemorySlot::Standalone,
                direction: DspTimingMemoryDirection::Read,
                address: 0xf000,
                region: DspTimingMemoryRegion::DataDram,
            });
        assert!(matches!(
            bad_region.validate(),
            Err(DspTimingKeyError::MemoryRegionMismatch { .. })
        ));

        let mut bad_order = key(0x0000);
        bad_order.context.memory_accesses = vec![
            DspTimingMemoryAccessKey::data(
                DspTimingMemorySlot::ParallelSecondary,
                DspTimingMemoryDirection::Read,
                0,
            )
            .unwrap(),
            DspTimingMemoryAccessKey::data(
                DspTimingMemorySlot::Standalone,
                DspTimingMemoryDirection::Write,
                1,
            )
            .unwrap(),
        ];
        assert_eq!(
            bad_order.validate(),
            Err(DspTimingKeyError::MemoryAccessesNotCanonical)
        );

        let mut bad_outcome = key(0x0000);
        bad_outcome.outcome.instruction = DspTimingInstructionOutcome::Halted;
        assert!(matches!(
            bad_outcome.validate(),
            Err(DspTimingKeyError::InvalidOutcome { .. })
        ));

        let mut duplicate_slot = key(0x0000);
        duplicate_slot.context.memory_accesses = vec![
            DspTimingMemoryAccessKey::data(
                DspTimingMemorySlot::Standalone,
                DspTimingMemoryDirection::Read,
                0,
            )
            .unwrap(),
            DspTimingMemoryAccessKey::data(
                DspTimingMemorySlot::Standalone,
                DspTimingMemoryDirection::Write,
                1,
            )
            .unwrap(),
        ];
        assert!(matches!(
            duplicate_slot.validate(),
            Err(DspTimingKeyError::DuplicateMemorySlot { .. })
        ));

        let unconditional = DspTimingKey::from_words(
            &[0x029f, 0x0000],
            DspTimingOutcomeKey {
                instruction: DspTimingInstructionOutcome::ConditionNotSatisfied,
                hardware_loop: DspTimingHardwareLoopOutcome::None,
                interrupt: DspTimingInterruptOutcome::None,
            },
            context(0),
        );
        assert!(matches!(
            unconditional,
            Err(DspTimingKeyError::InvalidOutcome { .. })
        ));

        let nonzero_loop = DspTimingKey::from_words(
            &[0x1001],
            DspTimingOutcomeKey {
                instruction: DspTimingInstructionOutcome::ZeroCountLoopSkipped,
                hardware_loop: DspTimingHardwareLoopOutcome::None,
                interrupt: DspTimingInterruptOutcome::None,
            },
            context(0),
        );
        assert!(matches!(
            nonzero_loop,
            Err(DspTimingKeyError::InvalidOutcome { .. })
        ));

        let mut impossible_interrupt = key(0x0000);
        impossible_interrupt.context.external_interrupt_pending = false;
        impossible_interrupt.context.accelerator_interrupt_pending = false;
        impossible_interrupt.outcome.interrupt =
            DspTimingInterruptOutcome::AcceptedAfterInstruction;
        assert_eq!(
            impossible_interrupt.validate(),
            Err(DspTimingKeyError::InterruptAcceptedWithoutPendingSource)
        );
    }

    #[test]
    fn hardware_loop_edges_require_nonhalt_and_all_three_logical_stacks() {
        for edge in [
            DspTimingHardwareLoopOutcome::BackEdgeTaken,
            DspTimingHardwareLoopOutcome::Exited,
        ] {
            let halt = DspTimingKey::from_words(
                &[0x0021],
                DspTimingOutcomeKey {
                    instruction: DspTimingInstructionOutcome::Halted,
                    hardware_loop: edge,
                    interrupt: DspTimingInterruptOutcome::None,
                },
                context(0),
            );
            assert_eq!(
                halt,
                Err(DspTimingKeyError::HardwareLoopEdgeOnHalt { outcome: edge })
            );

            for (stack_index, expected_name) in [
                (0, "loop target stack"),
                (2, "loop address stack"),
                (3, "loop counter stack"),
            ] {
                let mut zero_depth = context(0);
                match stack_index {
                    0 => zero_depth.call_stack_depth = 0,
                    2 => zero_depth.loop_address_stack_depth = 0,
                    3 => zero_depth.loop_counter_stack_depth = 0,
                    _ => unreachable!(),
                }
                let invalid = DspTimingKey::from_words(
                    &[0x0000],
                    DspTimingOutcomeKey {
                        instruction: DspTimingInstructionOutcome::Completed,
                        hardware_loop: edge,
                        interrupt: DspTimingInterruptOutcome::None,
                    },
                    zero_depth,
                );
                assert_eq!(
                    invalid,
                    Err(DspTimingKeyError::HardwareLoopEdgeWithoutRequiredStack {
                        outcome: edge,
                        stack_name: expected_name,
                    })
                );
            }

            let mut exact_required_depths = context(0);
            exact_required_depths.data_stack_depth = 0;
            assert!(DspTimingKey::from_words(
                &[0x0000],
                DspTimingOutcomeKey {
                    instruction: DspTimingInstructionOutcome::Completed,
                    hardware_loop: edge,
                    interrupt: DspTimingInterruptOutcome::None,
                },
                exact_required_depths,
            )
            .is_ok());
        }
    }

    #[test]
    fn profile_validation_wraps_invalid_hardware_loop_stack_evidence() {
        let mut invalid = key(0x0000);
        invalid.outcome.hardware_loop = DspTimingHardwareLoopOutcome::BackEdgeTaken;
        invalid.context.call_stack_depth = 0;
        let profile = profile_with_rules(vec![rule(invalid, 3)]);
        assert!(matches!(
            profile.validate(),
            Err(DspTimingProfileError::InvalidTimingKey {
                source: DspTimingKeyError::HardwareLoopEdgeWithoutRequiredStack {
                    outcome: DspTimingHardwareLoopOutcome::BackEdgeTaken,
                    stack_name: "loop target stack",
                },
            })
        ));
    }

    #[test]
    fn memory_access_constructors_cover_masked_irom_and_directional_open_bus() {
        for (address, expected) in [
            (0x0000, DspTimingMemoryRegion::InstructionIram),
            (0x0fff, DspTimingMemoryRegion::InstructionIram),
            (0x1000, DspTimingMemoryRegion::InstructionIrom),
            (0x7fff, DspTimingMemoryRegion::InstructionIrom),
            (0x8000, DspTimingMemoryRegion::InstructionIrom),
            (0x9001, DspTimingMemoryRegion::InstructionIrom),
            (0xffff, DspTimingMemoryRegion::InstructionIrom),
        ] {
            let access = DspTimingMemoryAccessKey::instruction(
                DspTimingMemorySlot::Standalone,
                DspTimingMemoryDirection::Read,
                address,
            )
            .unwrap();
            assert_eq!(access.address, address);
            assert_eq!(access.region, expected);
            assert!(access.validate().is_ok());
        }
        assert_eq!(
            DspTimingMemoryAccessKey::instruction(
                DspTimingMemorySlot::Standalone,
                DspTimingMemoryDirection::Write,
                0x0000,
            ),
            Err(DspTimingKeyError::InvalidInstructionMemoryDirection {
                direction: DspTimingMemoryDirection::Write,
            })
        );

        for top_nibble in 0u16..=0x0fu16 {
            let address = (top_nibble << 12) | 0x0321;
            for direction in [
                DspTimingMemoryDirection::Read,
                DspTimingMemoryDirection::Write,
            ] {
                let expected = match (direction, top_nibble) {
                    (_, 0x0) => DspTimingMemoryRegion::DataDram,
                    (DspTimingMemoryDirection::Read, 0x1) => DspTimingMemoryRegion::CoefficientRom,
                    (_, 0xf) => DspTimingMemoryRegion::InterfaceRegister,
                    _ => DspTimingMemoryRegion::DataOpenBus,
                };
                let access = DspTimingMemoryAccessKey::data(
                    DspTimingMemorySlot::Standalone,
                    direction,
                    address,
                )
                .unwrap();
                assert_eq!(access.address, address);
                assert_eq!(access.region, expected);
                assert!(access.validate().is_ok());
            }
        }

        let mismatched = DspTimingMemoryAccessKey {
            slot: DspTimingMemorySlot::Standalone,
            direction: DspTimingMemoryDirection::Read,
            address: 0x1000,
            region: DspTimingMemoryRegion::DataOpenBus,
        };
        assert_eq!(
            mismatched.validate(),
            Err(DspTimingKeyError::MemoryRegionMismatch {
                address: 0x1000,
                expected: DspTimingMemoryRegion::CoefficientRom,
                actual: DspTimingMemoryRegion::DataOpenBus,
            })
        );

        let mut invalid_fetch = key(0x0000);
        invalid_fetch.context.fetch_address = 0x4000;
        assert_eq!(
            invalid_fetch.validate(),
            Err(DspTimingKeyError::InvalidInstructionAddress { address: 0x4000 })
        );
    }

    #[test]
    fn memory_slots_must_match_standalone_or_parallel_issue_shape() {
        let mut standalone = key(0x0000);
        standalone.context.memory_accesses.push(
            DspTimingMemoryAccessKey::data(
                DspTimingMemorySlot::ParallelPrimary,
                DspTimingMemoryDirection::Read,
                0x0001,
            )
            .unwrap(),
        );
        assert_eq!(
            standalone.validate(),
            Err(DspTimingKeyError::MemorySlotIncompatibleWithBundle {
                slot: DspTimingMemorySlot::ParallelPrimary,
                bundle: DspTimingBundleKey::Standalone,
            })
        );

        let parallel_bundle = DspTimingBundleKey::Parallel {
            primary_word: 0x8000,
            parallel_extension: 0xae,
            extension_mask: 0xff,
        };
        let mut invalid_parallel_context = context(0);
        invalid_parallel_context.memory_accesses.push(
            DspTimingMemoryAccessKey::data(
                DspTimingMemorySlot::Standalone,
                DspTimingMemoryDirection::Write,
                0xf012,
            )
            .unwrap(),
        );
        assert_eq!(
            DspTimingKey::from_words(&[0x80ae], completed(), invalid_parallel_context),
            Err(DspTimingKeyError::MemorySlotIncompatibleWithBundle {
                slot: DspTimingMemorySlot::Standalone,
                bundle: parallel_bundle,
            })
        );

        let mut exact_parallel_context = context(0);
        exact_parallel_context.memory_accesses = vec![
            DspTimingMemoryAccessKey::data(
                DspTimingMemorySlot::ParallelPrimary,
                DspTimingMemoryDirection::Write,
                0x0001,
            )
            .unwrap(),
            DspTimingMemoryAccessKey::data(
                DspTimingMemorySlot::ParallelSecondary,
                DspTimingMemoryDirection::Read,
                0xf012,
            )
            .unwrap(),
        ];
        assert!(DspTimingKey::from_words(&[0x80ae], completed(), exact_parallel_context).is_ok());

        let mut wrong_direction = context(0);
        wrong_direction.memory_accesses = vec![
            DspTimingMemoryAccessKey::data(
                DspTimingMemorySlot::ParallelPrimary,
                DspTimingMemoryDirection::Read,
                0x0001,
            )
            .unwrap(),
            DspTimingMemoryAccessKey::data(
                DspTimingMemorySlot::ParallelSecondary,
                DspTimingMemoryDirection::Read,
                0xf012,
            )
            .unwrap(),
        ];
        assert_eq!(
            DspTimingKey::from_words(&[0x80ae], completed(), wrong_direction),
            Err(DspTimingKeyError::MemoryDirectionMismatch {
                slot: DspTimingMemorySlot::ParallelPrimary,
                expected: DspTimingMemoryDirection::Write,
                actual: DspTimingMemoryDirection::Read,
            })
        );

        let mut wrong_space = context(0);
        wrong_space.memory_accesses = vec![
            DspTimingMemoryAccessKey::data(
                DspTimingMemorySlot::ParallelPrimary,
                DspTimingMemoryDirection::Write,
                0x0001,
            )
            .unwrap(),
            DspTimingMemoryAccessKey::instruction(
                DspTimingMemorySlot::ParallelSecondary,
                DspTimingMemoryDirection::Read,
                0x9001,
            )
            .unwrap(),
        ];
        assert_eq!(
            DspTimingKey::from_words(&[0x80ae], completed(), wrong_space),
            Err(DspTimingKeyError::MemorySpaceMismatch {
                slot: DspTimingMemorySlot::ParallelSecondary,
                expected: DspTimingMemorySpace::Data,
                actual: DspTimingMemorySpace::Instruction,
            })
        );

        let mut wrong_slot = context(0);
        wrong_slot.memory_accesses.push(
            DspTimingMemoryAccessKey::data(
                DspTimingMemorySlot::ParallelSecondary,
                DspTimingMemoryDirection::Write,
                0x0001,
            )
            .unwrap(),
        );
        assert_eq!(
            DspTimingKey::from_words(&[0x803b], completed(), wrong_slot),
            Err(DspTimingKeyError::MemorySlotInventoryMismatch {
                expected: DSP_TIMING_PARALLEL_PRIMARY_MEMORY_BIT,
                actual: DSP_TIMING_PARALLEL_SECONDARY_MEMORY_BIT,
            })
        );

        let mut impossible_nop_context = context(0);
        impossible_nop_context.memory_accesses.push(
            DspTimingMemoryAccessKey::data(
                DspTimingMemorySlot::Standalone,
                DspTimingMemoryDirection::Read,
                0x0001,
            )
            .unwrap(),
        );
        assert_eq!(
            DspTimingKey::from_words(&[0x0000], completed(), impossible_nop_context),
            Err(DspTimingKeyError::MemorySlotInventoryMismatch {
                expected: 0,
                actual: DSP_TIMING_STANDALONE_MEMORY_BIT,
            })
        );
    }

    #[test]
    fn profile_identity_is_deterministic_and_covers_all_payload_fields() {
        let profile = profile_with_rules(vec![rule(key(0x0000), 3)]);
        assert_eq!(profile.compute_identity(), profile.profile_identity);
        assert_eq!(profile.compute_identity(), profile.compute_identity());

        let mut changed_cycles = profile.clone();
        changed_cycles.rules[0].cycles += 1;
        changed_cycles.rules[0].evidence[0].minimum_cycles += 1;
        changed_cycles.rules[0].evidence[0].maximum_cycles += 1;
        assert_ne!(
            profile.compute_identity(),
            changed_cycles.compute_identity()
        );
        changed_cycles.refresh_identity();
        assert!(changed_cycles.validate().is_ok());

        let mut changed = profile.clone();
        changed.rules[0].evidence[0].sample_count += 1;
        assert_ne!(profile.compute_identity(), changed.compute_identity());
        changed.refresh_identity();
        assert!(changed.validate().is_ok());
    }

    #[test]
    fn synthetic_profile_validates_but_cannot_qualify_production() {
        let profile = profile_with_rules(vec![rule(key(0x0000), 3)]);
        let validated = profile.clone().validate().unwrap();
        assert!(!validated.production_qualified());
        assert_eq!(
            profile.validate_for_production(),
            Err(DspTimingProfileError::SyntheticProfileNotProduction)
        );
    }

    #[test]
    fn retail_profile_requires_retail_capture_provenance() {
        let mut profile = profile_with_rules(vec![rule(key(0x0000), 3)]);
        profile.kind = DspTimingProfileKind::RetailWiiCapture;
        profile.target.platform = "retail_wii".to_string();
        profile.refresh_identity();
        assert_eq!(
            profile.validate_for_production(),
            Err(DspTimingProfileError::SyntheticCaptureNotProduction {
                capture_id: "capture-a".to_string(),
            })
        );
    }

    #[test]
    fn structurally_valid_retail_fixture_remains_blocked_without_reviewed_identity() {
        let mut profile = profile_with_rules(vec![rule(key(0x0000), 3)]);
        profile.kind = DspTimingProfileKind::RetailWiiCapture;
        profile.target.platform = "retail_wii".to_string();
        profile.captures[0].kind = DspTimingProfileKind::RetailWiiCapture;
        profile.refresh_identity();
        assert!(matches!(
            profile.validate_for_production(),
            Err(DspTimingProfileError::UnapprovedRetailProfile { .. })
        ));
    }

    #[test]
    fn profile_validation_rejects_contract_target_and_capture_metadata_failures() {
        let base = profile_with_rules(vec![rule(key(0x0000), 3)]);

        let mut profile = base.clone();
        profile.schema_version += 1;
        profile.refresh_identity();
        assert!(matches!(
            profile.validate(),
            Err(DspTimingProfileError::UnsupportedSchemaVersion { .. })
        ));

        let mut profile = base.clone();
        profile.timing_contract_version += 1;
        profile.refresh_identity();
        assert!(matches!(
            profile.validate(),
            Err(DspTimingProfileError::UnsupportedContractVersion { .. })
        ));

        let mut profile = base.clone();
        profile.target.dsp_clock_hz = 0;
        profile.refresh_identity();
        assert_eq!(profile.validate(), Err(DspTimingProfileError::ZeroDspClock));

        let mut profile = base.clone();
        profile.captures.clear();
        profile.refresh_identity();
        assert_eq!(
            profile.validate(),
            Err(DspTimingProfileError::MissingCaptures)
        );

        let mut profile = base.clone();
        profile.rules.clear();
        profile.refresh_identity();
        assert_eq!(profile.validate(), Err(DspTimingProfileError::MissingRules));

        let mut profile = base.clone();
        profile.captures[0].capture_schema_version += 1;
        profile.refresh_identity();
        assert!(matches!(
            profile.validate(),
            Err(DspTimingProfileError::UnsupportedCaptureSchemaVersion { .. })
        ));

        let mut profile = base.clone();
        profile.captures[0].source_sha256 = "ABC".to_string();
        profile.refresh_identity();
        assert!(matches!(
            profile.validate(),
            Err(DspTimingProfileError::InvalidCaptureSha256 { .. })
        ));

        let mut profile = base.clone();
        profile.captures[0].captured_at_utc = "2001-02-29T00:00:00Z".to_string();
        profile.refresh_identity();
        assert!(matches!(
            profile.validate(),
            Err(DspTimingProfileError::InvalidCaptureTimestamp { .. })
        ));

        let mut profile = base.clone();
        profile.captures[0].capture_tool_source_revision = "not-a-revision".to_string();
        profile.refresh_identity();
        assert!(matches!(
            profile.validate(),
            Err(DspTimingProfileError::InvalidCaptureToolSourceRevision { .. })
        ));

        let mut profile = base.clone();
        profile.captures[0].measurement_protocol_version = 0;
        profile.refresh_identity();
        assert!(matches!(
            profile.validate(),
            Err(DspTimingProfileError::ZeroMeasurementProtocolVersion { .. })
        ));

        let mut profile = base.clone();
        profile.captures[0].hardware_revision = "different".to_string();
        profile.refresh_identity();
        assert!(matches!(
            profile.validate(),
            Err(DspTimingProfileError::CaptureTargetMismatch { .. })
        ));

        let mut profile = base;
        profile.target.clock_evidence_capture_id = "missing".to_string();
        profile.refresh_identity();
        assert!(matches!(
            profile.validate(),
            Err(DspTimingProfileError::UnknownClockEvidenceCapture { .. })
        ));
    }

    #[test]
    fn profile_validation_rejects_noncanonical_duplicates_and_unknown_evidence() {
        let first = rule(key(0x0000), 3);
        let second = rule(key(0x0004), 4);
        let mut unsorted = profile_with_rules(vec![first.clone(), second.clone()]);
        unsorted.rules.reverse();
        unsorted.refresh_identity();
        assert_eq!(
            unsorted.validate(),
            Err(DspTimingProfileError::RulesNotCanonical)
        );

        let mut duplicate = profile_with_rules(vec![first.clone()]);
        duplicate.rules.push(first);
        duplicate.refresh_identity();
        assert!(matches!(
            duplicate.validate(),
            Err(DspTimingProfileError::DuplicateTimingKey { .. })
        ));

        let mut unknown = profile_with_rules(vec![second]);
        unknown.rules[0].evidence[0].capture_id = "missing".to_string();
        unknown.refresh_identity();
        assert!(matches!(
            unknown.validate(),
            Err(DspTimingProfileError::UnknownEvidenceCapture { .. })
        ));
    }

    #[test]
    fn profile_validation_rejects_inexact_zero_and_tampered_data() {
        let mut inexact = profile_with_rules(vec![rule(key(0x0000), 3)]);
        inexact.rules[0].evidence[0].maximum_cycles = 4;
        inexact.refresh_identity();
        assert!(matches!(
            inexact.validate(),
            Err(DspTimingProfileError::NonExactEvidence { .. })
        ));

        let mut zero = profile_with_rules(vec![rule(key(0x0000), 3)]);
        zero.rules[0].cycles = 0;
        zero.rules[0].evidence[0].minimum_cycles = 0;
        zero.rules[0].evidence[0].maximum_cycles = 0;
        zero.refresh_identity();
        assert!(matches!(
            zero.validate(),
            Err(DspTimingProfileError::ZeroCycles { .. })
        ));

        let mut tampered = profile_with_rules(vec![rule(key(0x0000), 3)]);
        tampered.target.dsp_clock_hz = 2;
        assert!(matches!(
            tampered.validate(),
            Err(DspTimingProfileError::IdentityMismatch { .. })
        ));
    }

    #[test]
    fn profile_validation_rejects_every_noncanonical_evidence_shape() {
        let base = profile_with_rules(vec![rule(key(0x0000), 3)]);

        let mut profile = base.clone();
        profile.rules[0].evidence.clear();
        profile.refresh_identity();
        assert!(matches!(
            profile.validate(),
            Err(DspTimingProfileError::MissingRuleEvidence { .. })
        ));

        let mut profile = base.clone();
        profile.rules[0].evidence[0].sample_count = 0;
        profile.refresh_identity();
        assert!(matches!(
            profile.validate(),
            Err(DspTimingProfileError::ZeroEvidenceSamples { .. })
        ));

        let mut profile = base.clone();
        profile.rules[0].evidence[0].minimum_cycles = 4;
        profile.rules[0].evidence[0].maximum_cycles = 4;
        profile.refresh_identity();
        assert!(matches!(
            profile.validate(),
            Err(DspTimingProfileError::EvidenceCycleMismatch { .. })
        ));

        let mut second_capture = capture("capture-b", DspTimingProfileKind::SyntheticTest);
        second_capture.source_sha256 =
            "1123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef".to_string();
        let second_evidence = DspTimingEvidence {
            capture_id: "capture-b".to_string(),
            sample_count: 1,
            minimum_cycles: 3,
            maximum_cycles: 3,
        };

        let mut profile = base.clone();
        profile.captures.push(second_capture.clone());
        profile.rules[0].evidence.push(second_evidence.clone());
        profile.rules[0].evidence.reverse();
        profile.refresh_identity();
        assert!(matches!(
            profile.validate(),
            Err(DspTimingProfileError::RuleEvidenceNotCanonical { .. })
        ));

        let mut profile = base.clone();
        profile.captures.push(second_capture);
        let duplicate_evidence = profile.rules[0].evidence[0].clone();
        profile.rules[0].evidence.push(duplicate_evidence);
        profile.refresh_identity();
        assert!(matches!(
            profile.validate(),
            Err(DspTimingProfileError::DuplicateRuleEvidence { .. })
        ));
    }

    #[test]
    fn missing_and_unknown_timing_are_distinct_hard_failures() {
        let known = key(0x0000);
        assert_eq!(
            require_dsp_timing(None, &known),
            Err(DspTimingResolutionError::MissingTimingProfile)
        );

        let validated = profile_with_rules(vec![rule(known.clone(), 3)])
            .validate()
            .unwrap();
        assert_eq!(validated.cycles_for(&known).unwrap(), 3);
        assert!(matches!(
            require_dsp_timing(Some(&validated), &known),
            Err(DspTimingResolutionError::UnqualifiedTimingProfile { .. })
        ));

        let unknown = key(0x0004);
        assert!(matches!(
            validated.cycles_for(&unknown),
            Err(DspTimingResolutionError::UnknownTiming { .. })
        ));

        let mut production = validated;
        production.production_qualified = true;
        assert!(matches!(
            require_dsp_timing(Some(&production), &unknown),
            Err(DspTimingResolutionError::UnknownTiming { .. })
        ));
    }

    #[test]
    fn production_execution_gate_rejects_missing_unqualified_and_unconnected_timing() {
        assert_eq!(
            require_production_dsp_timing_execution(None),
            Err(DspTimingExecutionError::MissingTimingProfile)
        );

        let mut validated = profile_with_rules(vec![rule(key(0x0000), 3)])
            .validate()
            .unwrap();
        assert!(matches!(
            require_production_dsp_timing_execution(Some(&validated)),
            Err(DspTimingExecutionError::UnqualifiedTimingProfile { .. })
        ));

        // No fixture bypasses retail approval in product code. This private
        // test mutation exercises only the independent execution-readiness
        // gate that must remain closed even after a future profile approval.
        validated.production_qualified = true;
        assert_eq!(
            require_production_dsp_timing_execution(Some(&validated)),
            Err(DspTimingExecutionError::RuntimeAccountingUnavailable {
                contract_version: DSP_TIMING_CONTRACT_VERSION,
                profile_identity: validated.profile_identity().to_string(),
                runtime_plan_foundation_ready: false,
            })
        );
    }

    #[test]
    fn timing_contract_binding_rejects_version_and_identity_mismatch() {
        let mut validated = profile_with_rules(vec![rule(key(0x0000), 3)])
            .validate()
            .unwrap();
        assert!(matches!(
            DspTimingContractBinding::from_profile(&validated),
            Err(DspTimingBindingError::ProfileNotProduction { .. })
        ));
        validated.production_qualified = true;
        let binding = DspTimingContractBinding::from_profile(&validated).unwrap();
        assert!(binding
            .verify(DSP_TIMING_CONTRACT_VERSION, validated.profile_identity())
            .is_ok());
        assert!(matches!(
            binding.verify(
                DSP_TIMING_CONTRACT_VERSION + 1,
                validated.profile_identity()
            ),
            Err(DspTimingBindingError::ContractVersionMismatch { .. })
        ));
        assert!(matches!(
            binding.verify(DSP_TIMING_CONTRACT_VERSION, "sha1:wrong"),
            Err(DspTimingBindingError::ProfileIdentityMismatch { .. })
        ));

        let cpp = binding
            .emit_cpp_identity_exports("galaxy_dsp_test")
            .unwrap();
        assert_eq!(
            cpp,
            binding
                .emit_cpp_identity_exports("galaxy_dsp_test")
                .unwrap()
        );
        assert!(cpp.contains("galaxy_dsp_test_dsp_timing_contract_version"));
        assert!(cpp.contains(validated.profile_identity()));
        assert!(cpp.contains("galaxy_dsp_test_dsp_timing_contract_matches"));
        assert!(matches!(
            binding.emit_cpp_identity_exports("bad-prefix"),
            Err(DspTimingBindingError::InvalidSymbolPrefix { .. })
        ));
    }

    #[test]
    fn json_round_trip_preserves_canonical_identity() {
        let profile = profile_with_rules(vec![rule(key(0x0000), 3)]);
        let json = profile.to_canonical_json().unwrap();
        let decoded = DspTimingProfile::from_json(&json).unwrap();
        assert_eq!(profile, decoded);
        assert_eq!(profile.profile_identity, decoded.compute_identity());
        assert!(decoded.validate().is_ok());

        let mut value: serde_json::Value = serde_json::from_slice(&json).unwrap();
        value
            .as_object_mut()
            .unwrap()
            .insert("unknown_field".to_string(), serde_json::Value::Bool(true));
        let unknown_json = serde_json::to_vec(&value).unwrap();
        assert!(matches!(
            DspTimingProfile::from_json(&unknown_json),
            Err(DspTimingProfileDocumentError::Json(_))
        ));
    }
}
