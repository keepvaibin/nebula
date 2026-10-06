use std::collections::{BTreeMap, BTreeSet};

use serde::Serialize;
use sha1::{Digest, Sha1};
use sha2::Sha256;
use thiserror::Error;

pub const DSP_IRAM_WORDS: usize = 0x1000;
pub const DSP_IROM_WORDS: usize = 0x1000;
pub const DSP_DRAM_WORDS: usize = 0x1000;
pub const DSP_COEF_WORDS: usize = 0x800;

pub const DSP_GENERATED_LOWERING_CONTRACT_VERSION: u32 = 1;

const DSP_GENERATED_PROBE_CONTRACT_VERSION: u32 = 1;
const DSP_GENERATED_PROBE_CAPABILITY_SELECTION_PUBLICATION: u32 = 1 << 0;
const DSP_GENERATED_PROBE_CAPABILITY_SELECTED_CHANNEL_BRANCH: u32 = 1 << 1;
const DSP_RAW_INPUT_AGGREGATE_DOMAIN: &[u8] = b"nebula.dsp-raw-input-aggregate.v1\n";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspInstructionMemoryRegion {
    Iram { word_index: u16 },
    Irom { word_index: u16 },
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspDataMemoryRegion {
    Dram { word_index: u16 },
    CoefRom { word_index: u16 },
    Ifx { register: u16 },
}

#[derive(Debug, Error, PartialEq, Eq)]
pub enum DspMemoryError {
    #[error("invalid DSP instruction address 0x{address:04X}")]
    InvalidInstructionAddress { address: u16 },
    #[error("invalid DSP data address 0x{address:04X}")]
    InvalidDataAddress { address: u16 },
}

pub fn instruction_memory_region(
    address: u16,
) -> Result<DspInstructionMemoryRegion, DspMemoryError> {
    match address & 0xf000 {
        0x0000 => Ok(DspInstructionMemoryRegion::Iram {
            word_index: address & 0x0fff,
        }),
        0x8000 => Ok(DspInstructionMemoryRegion::Irom {
            word_index: address & 0x0fff,
        }),
        _ => Err(DspMemoryError::InvalidInstructionAddress { address }),
    }
}

pub fn data_memory_region(address: u16) -> Result<DspDataMemoryRegion, DspMemoryError> {
    match address & 0xf000 {
        0x0000 => Ok(DspDataMemoryRegion::Dram {
            word_index: address & 0x0fff,
        }),
        0x1000 => Ok(DspDataMemoryRegion::CoefRom {
            word_index: address & 0x07ff,
        }),
        0xf000 => Ok(DspDataMemoryRegion::Ifx {
            register: address & 0x00ff,
        }),
        _ => Err(DspMemoryError::InvalidDataAddress { address }),
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspStatusFlag {
    Carry,
    Overflow,
    ArithmeticZero,
    Sign,
    OverSigned32,
    TopTwoBitsEqual,
    LogicZero,
    StickyOverflow,
    InterruptEnable,
    ExternalInterruptEnable,
    MultiplyModifier,
    Mode40Bit,
    UnsignedMultiply,
}

impl DspStatusFlag {
    pub const fn mask(self) -> u16 {
        match self {
            DspStatusFlag::Carry => 0x0001,
            DspStatusFlag::Overflow => 0x0002,
            DspStatusFlag::ArithmeticZero => 0x0004,
            DspStatusFlag::Sign => 0x0008,
            DspStatusFlag::OverSigned32 => 0x0010,
            DspStatusFlag::TopTwoBitsEqual => 0x0020,
            DspStatusFlag::LogicZero => 0x0040,
            DspStatusFlag::StickyOverflow => 0x0080,
            DspStatusFlag::InterruptEnable => 0x0200,
            DspStatusFlag::ExternalInterruptEnable => 0x0800,
            DspStatusFlag::MultiplyModifier => 0x2000,
            DspStatusFlag::Mode40Bit => 0x4000,
            DspStatusFlag::UnsignedMultiply => 0x8000,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspAddressRegister {
    Ar0,
    Ar1,
    Ar2,
    Ar3,
}

impl DspAddressRegister {
    const fn from_bits(bits: u16) -> Self {
        match bits & 0x0003 {
            0 => DspAddressRegister::Ar0,
            1 => DspAddressRegister::Ar1,
            2 => DspAddressRegister::Ar2,
            _ => DspAddressRegister::Ar3,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspIndexRegister {
    Ix0,
    Ix1,
    Ix2,
    Ix3,
}

impl DspIndexRegister {
    const fn from_bits(bits: u16) -> Self {
        match bits & 0x0003 {
            0 => DspIndexRegister::Ix0,
            1 => DspIndexRegister::Ix1,
            2 => DspIndexRegister::Ix2,
            _ => DspIndexRegister::Ix3,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspWrapRegister {
    Wr0,
    Wr1,
    Wr2,
    Wr3,
}

impl DspWrapRegister {
    const fn from_bits(bits: u16) -> Self {
        match bits & 0x0003 {
            0 => DspWrapRegister::Wr0,
            1 => DspWrapRegister::Wr1,
            2 => DspWrapRegister::Wr2,
            _ => DspWrapRegister::Wr3,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspStackRegister {
    Call,
    Data,
    LoopAddress,
    LoopCounter,
}

impl DspStackRegister {
    const fn from_bits(bits: u16) -> Self {
        match bits & 0x0003 {
            0 => DspStackRegister::Call,
            1 => DspStackRegister::Data,
            2 => DspStackRegister::LoopAddress,
            _ => DspStackRegister::LoopCounter,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspAxRegister {
    Ax0,
    Ax1,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspAccumulator {
    Ac0,
    Ac1,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspRegisterHalf {
    Low,
    High,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspAccumulatorPart {
    Low,
    Mid,
    High,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspRegister {
    Address(DspAddressRegister),
    Index(DspIndexRegister),
    Wrap(DspWrapRegister),
    Stack(DspStackRegister),
    Control,
    Status,
    ProductLow,
    ProductMid,
    ProductHigh,
    ProductMid2,
    Ax {
        register: DspAxRegister,
        half: DspRegisterHalf,
    },
    Accumulator {
        register: DspAccumulator,
        part: DspAccumulatorPart,
    },
}

const fn register_from_bits(bits: u16) -> DspRegister {
    match bits & 0x001f {
        0x00..=0x03 => DspRegister::Address(DspAddressRegister::from_bits(bits)),
        0x04..=0x07 => DspRegister::Index(DspIndexRegister::from_bits(bits)),
        0x08..=0x0b => DspRegister::Wrap(DspWrapRegister::from_bits(bits)),
        0x0c..=0x0f => DspRegister::Stack(DspStackRegister::from_bits(bits)),
        0x10 => DspRegister::Accumulator {
            register: DspAccumulator::Ac0,
            part: DspAccumulatorPart::High,
        },
        0x11 => DspRegister::Accumulator {
            register: DspAccumulator::Ac1,
            part: DspAccumulatorPart::High,
        },
        0x12 => DspRegister::Control,
        0x13 => DspRegister::Status,
        0x14 => DspRegister::ProductLow,
        0x15 => DspRegister::ProductMid,
        0x16 => DspRegister::ProductHigh,
        0x17 => DspRegister::ProductMid2,
        0x18 => DspRegister::Ax {
            register: DspAxRegister::Ax0,
            half: DspRegisterHalf::Low,
        },
        0x19 => DspRegister::Ax {
            register: DspAxRegister::Ax1,
            half: DspRegisterHalf::Low,
        },
        0x1a => DspRegister::Ax {
            register: DspAxRegister::Ax0,
            half: DspRegisterHalf::High,
        },
        0x1b => DspRegister::Ax {
            register: DspAxRegister::Ax1,
            half: DspRegisterHalf::High,
        },
        0x1c => DspRegister::Accumulator {
            register: DspAccumulator::Ac0,
            part: DspAccumulatorPart::Low,
        },
        0x1d => DspRegister::Accumulator {
            register: DspAccumulator::Ac1,
            part: DspAccumulatorPart::Low,
        },
        0x1e => DspRegister::Accumulator {
            register: DspAccumulator::Ac0,
            part: DspAccumulatorPart::Mid,
        },
        _ => DspRegister::Accumulator {
            register: DspAccumulator::Ac1,
            part: DspAccumulatorPart::Mid,
        },
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspCondition {
    GreaterOrEqual,
    Less,
    Greater,
    LessOrEqual,
    NotZero,
    Zero,
    NotCarry,
    Carry,
    NotOverSigned32,
    OverSigned32,
    ExtendedA,
    ExtendedB,
    LogicNotZero,
    LogicZero,
    Overflow,
    Always,
}

impl DspCondition {
    const fn from_low_nibble(bits: u16) -> Self {
        match bits & 0x000f {
            0x0 => DspCondition::GreaterOrEqual,
            0x1 => DspCondition::Less,
            0x2 => DspCondition::Greater,
            0x3 => DspCondition::LessOrEqual,
            0x4 => DspCondition::NotZero,
            0x5 => DspCondition::Zero,
            0x6 => DspCondition::NotCarry,
            0x7 => DspCondition::Carry,
            0x8 => DspCondition::NotOverSigned32,
            0x9 => DspCondition::OverSigned32,
            0xa => DspCondition::ExtendedA,
            0xb => DspCondition::ExtendedB,
            0xc => DspCondition::LogicNotZero,
            0xd => DspCondition::LogicZero,
            0xe => DspCondition::Overflow,
            _ => DspCondition::Always,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct DspAxPart {
    pub register: DspAxRegister,
    pub half: DspRegisterHalf,
}

impl DspAxPart {
    pub const fn new(register: DspAxRegister, half: DspRegisterHalf) -> Self {
        Self { register, half }
    }

    const fn from_ax0_bit(bit: u16) -> Self {
        Self::new(DspAxRegister::Ax0, register_half_from_bit(bit))
    }

    const fn from_ax1_bit(bit: u16) -> Self {
        Self::new(DspAxRegister::Ax1, register_half_from_bit(bit))
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspAccumulatorShiftKind {
    LogicalLeft,
    LogicalRight,
    ArithmeticLeft,
    ArithmeticRight,
}

const fn accumulator_from_bit(bit: u16) -> DspAccumulator {
    if (bit & 1) == 0 {
        DspAccumulator::Ac0
    } else {
        DspAccumulator::Ac1
    }
}

const fn other_accumulator(accumulator: DspAccumulator) -> DspAccumulator {
    match accumulator {
        DspAccumulator::Ac0 => DspAccumulator::Ac1,
        DspAccumulator::Ac1 => DspAccumulator::Ac0,
    }
}

const fn ax_register_from_bit(bit: u16) -> DspAxRegister {
    if (bit & 1) == 0 {
        DspAxRegister::Ax0
    } else {
        DspAxRegister::Ax1
    }
}

const fn ax_part_from_linear_bits(bits: u16) -> DspAxPart {
    let register = ax_register_from_bit(bits);
    let half = if (bits & 0x2) == 0 {
        DspRegisterHalf::Low
    } else {
        DspRegisterHalf::High
    };
    DspAxPart { register, half }
}

const fn register_half_from_bit(bit: u16) -> DspRegisterHalf {
    if (bit & 1) == 0 {
        DspRegisterHalf::Low
    } else {
        DspRegisterHalf::High
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspAddressOperation {
    Decrement {
        address: DspAddressRegister,
    },
    Increment {
        address: DspAddressRegister,
    },
    SubtractIndex {
        address: DspAddressRegister,
        index: DspIndexRegister,
    },
    AddIndex {
        address: DspAddressRegister,
        index: DspIndexRegister,
    },
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspAddressUpdate {
    None,
    Decrement,
    Increment,
    AddIndex,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspStatusBitOperation {
    Clear { bit: u8 },
    Set { bit: u8 },
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspRegisterTransferOperation {
    LoadImmediate {
        target: DspRegister,
        value: u16,
    },
    LoadFromMemory {
        target: DspRegister,
        address: u16,
    },
    StoreToMemory {
        address: u16,
        source: DspRegister,
    },
    Move {
        target: DspRegister,
        source: DspRegister,
    },
    StoreImmediate {
        address: u16,
        value: u16,
    },
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspImmediateOperation {
    AddShort { target: DspAccumulator, value: u8 },
    CompareShort { target: DspAccumulator, value: u8 },
    LoadRegisterShort { target: DspRegister, value: u8 },
    Add { target: DspAccumulator, value: u16 },
    Xor { target: DspAccumulator, value: u16 },
    And { target: DspAccumulator, value: u16 },
    Or { target: DspAccumulator, value: u16 },
    Compare { target: DspAccumulator, value: u16 },
    AndField { target: DspAccumulator, value: u16 },
    AndCompareField { target: DspAccumulator, value: u16 },
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspModeOperation {
    ExtendableNop,
    ProductMultiplyByTwo { enabled: bool },
    UnsignedMultiply { enabled: bool },
    Mode40Bit { enabled: bool },
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspMemoryTransferOperation {
    LoadInstructionMemory {
        target: DspAccumulator,
        address: DspAddressRegister,
        update: DspAddressUpdate,
    },
    LoadDataMemory {
        target: DspRegister,
        address: DspAddressRegister,
        update: DspAddressUpdate,
    },
    StoreDataMemory {
        address: DspAddressRegister,
        source: DspRegister,
        update: DspAddressUpdate,
    },
    DirectPageLoad {
        target: DspRegister,
        address: u8,
    },
    DirectPageStoreAccumulatorHigh {
        address: u8,
        source: DspAccumulator,
    },
    DirectPageStoreRegister {
        address: u8,
        source: DspRegister,
    },
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspLogicOperand {
    AxHigh(DspAxRegister),
    AccumulatorMid(DspAccumulator),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspLogicOperation {
    Xor {
        target: DspAccumulator,
        operand: DspLogicOperand,
    },
    And {
        target: DspAccumulator,
        operand: DspLogicOperand,
    },
    Or {
        target: DspAccumulator,
        operand: DspLogicOperand,
    },
    Not {
        target: DspAccumulator,
    },
    ShiftByRegister {
        target: DspAccumulator,
        kind: DspAccumulatorShiftKind,
        operand: DspLogicOperand,
    },
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspParallelPrimary {
    Mode(DspModeOperation),
    Logic(DspLogicOperation),
    Accumulator(DspAccumulatorOperation),
    Product(DspProductOperation),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspParallelLoadStoreOrder {
    LoadThenStore,
    StoreThenLoad,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspParallelOperation {
    Nop {
        raw_extension: u8,
    },
    Address(DspAddressOperation),
    Move {
        target: DspRegister,
        source: DspRegister,
    },
    StoreSingle {
        address: DspAddressRegister,
        source: DspRegister,
        update: DspAddressUpdate,
    },
    LoadSingle {
        target: DspRegister,
        address: DspAddressRegister,
        update: DspAddressUpdate,
    },
    AccumulatorMidLoadStore {
        order: DspParallelLoadStoreOrder,
        target: DspRegister,
        accumulator: DspAccumulator,
        ar0_update: DspAddressUpdate,
        ar3_update: DspAddressUpdate,
    },
    LoadAxPair {
        ax: DspAxRegister,
        address: DspAddressRegister,
        ar_update: DspAddressUpdate,
        ar3_update: DspAddressUpdate,
    },
    LoadAxParts {
        ax0_half: DspRegisterHalf,
        ax1_half: DspRegisterHalf,
        address: DspAddressRegister,
        ar_update: DspAddressUpdate,
        ar3_update: DspAddressUpdate,
    },
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspAccumulatorOperand {
    Accumulator(DspAccumulator),
    Ax(DspAxRegister),
    AxPartShifted(DspAxPart),
    AxLowUnsigned(DspAxRegister),
    Product,
    One,
    MidUnit,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspAccumulatorMoveSource {
    Accumulator(DspAccumulator),
    Ax(DspAxRegister),
    AxPartShifted(DspAxPart),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspAccumulatorOperation {
    Clear {
        target: DspAccumulator,
    },
    ClearLow {
        target: DspAccumulator,
    },
    Test {
        target: DspAccumulator,
    },
    TestAxHigh {
        ax: DspAxRegister,
    },
    CompareAccumulators,
    CompareWithAxHigh {
        accumulator: DspAccumulator,
        ax: DspAxRegister,
    },
    Add {
        target: DspAccumulator,
        operand: DspAccumulatorOperand,
    },
    Subtract {
        target: DspAccumulator,
        operand: DspAccumulatorOperand,
    },
    Negate {
        target: DspAccumulator,
    },
    Absolute {
        target: DspAccumulator,
    },
    Move {
        target: DspAccumulator,
        source: DspAccumulatorMoveSource,
    },
    ShiftImmediate {
        target: DspAccumulator,
        kind: DspAccumulatorShiftKind,
        amount: u8,
    },
    Shift16 {
        target: DspAccumulator,
        kind: DspAccumulatorShiftKind,
    },
    ShiftByAccumulatorMid {
        kind: DspAccumulatorShiftKind,
    },
    AddProductAndAxHighClearLow {
        target: DspAccumulator,
        ax: DspAxRegister,
    },
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspProductMoveMode {
    Raw,
    Negated,
    HighMidClearLow,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspMultiplyAccumulatorAction {
    AddPreviousProduct,
    MovePreviousProduct,
    MoveRoundedPreviousProduct,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspProductOperation {
    Clear,
    Test,
    MoveToAccumulator {
        target: DspAccumulator,
        mode: DspProductMoveMode,
    },
    MultiplyAxHighSquared,
    MultiplyAxPair {
        ax: DspAxRegister,
    },
    MultiplyAxPairWithAccumulator {
        ax: DspAxRegister,
        target: DspAccumulator,
        action: DspMultiplyAccumulatorAction,
    },
    MultiplyCross {
        left: DspAxPart,
        right: DspAxPart,
    },
    MultiplyCrossWithAccumulator {
        left: DspAxPart,
        right: DspAxPart,
        target: DspAccumulator,
        action: DspMultiplyAccumulatorAction,
    },
    MultiplyAccumulatorMidByAxHigh {
        accumulator: DspAccumulator,
        ax: DspAxRegister,
    },
    MultiplyAccumulatorMidByAxHighWithAccumulator {
        accumulator: DspAccumulator,
        ax: DspAxRegister,
        target: DspAccumulator,
        action: DspMultiplyAccumulatorAction,
    },
    AddCrossToProduct {
        left: DspAxPart,
        right: DspAxPart,
    },
    SubtractCrossFromProduct {
        left: DspAxPart,
        right: DspAxPart,
    },
    AddAccumulatorMidByAxHighToProduct {
        accumulator: DspAccumulator,
        ax: DspAxRegister,
    },
    SubtractAccumulatorMidByAxHighFromProduct {
        accumulator: DspAccumulator,
        ax: DspAxRegister,
    },
    AddAxPairToProduct {
        ax: DspAxRegister,
    },
    SubtractAxPairFromProduct {
        ax: DspAxRegister,
    },
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspInstruction {
    Nop,
    Halt,
    Address(DspAddressOperation),
    StatusBit(DspStatusBitOperation),
    RegisterTransfer(DspRegisterTransferOperation),
    Immediate(DspImmediateOperation),
    MemoryTransfer(DspMemoryTransferOperation),
    Mode(DspModeOperation),
    Logic(DspLogicOperation),
    Accumulator(DspAccumulatorOperation),
    Product(DspProductOperation),
    Parallel {
        primary: DspParallelPrimary,
        parallel: DspParallelOperation,
        raw_word: u16,
    },
    If {
        condition: DspCondition,
    },
    JumpImmediate {
        condition: DspCondition,
        target: u16,
    },
    JumpRegister {
        condition: DspCondition,
        target: DspRegister,
    },
    Return {
        interrupt: bool,
        condition: DspCondition,
    },
    CallImmediate {
        condition: DspCondition,
        target: u16,
    },
    CallRegister {
        condition: DspCondition,
        target: DspRegister,
    },
    Loop {
        count: DspRegister,
    },
    LoopImmediate {
        count: u8,
    },
    BlockLoop {
        count: DspRegister,
        end_address: u16,
    },
    BlockLoopImmediate {
        count: u8,
        end_address: u16,
    },
}

/// Canonical encoded memory-operand slot for one decoded DSP instruction.
///
/// Slots describe architectural operands, not the order or number of C++
/// helper calls used by lowering. In particular, a dual parallel load retains
/// two slots even when both addresses are equal and lowering coalesces the host
/// read.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspDecodedMemorySlot {
    Standalone,
    ParallelPrimary,
    ParallelSecondary,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspDecodedMemorySpace {
    Instruction,
    Data,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspDecodedMemoryDirection {
    Read,
    Write,
}

/// Install-time authority for resolving an encoded memory operand at the raw
/// instruction boundary. `DirectPage` is resolved from the immutable
/// pre-instruction CR high byte plus the encoded low byte.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DspDecodedMemoryAddressSource {
    Static(u16),
    AddressRegister(DspAddressRegister),
    DirectPage(u8),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct DspDecodedMemoryOperand {
    pub slot: DspDecodedMemorySlot,
    pub space: DspDecodedMemorySpace,
    pub direction: DspDecodedMemoryDirection,
    pub address_source: DspDecodedMemoryAddressSource,
}

/// Returns every encoded architectural memory operand in canonical slot order.
///
/// This is the single exhaustive decoder authority shared by generated raw
/// instruction provenance and install-time timing-plan construction. Fetch is
/// implicit instruction identity and is deliberately not represented here.
pub fn decoded_memory_operands(instruction: DspInstruction) -> Vec<DspDecodedMemoryOperand> {
    use DspDecodedMemoryAddressSource::{AddressRegister, DirectPage, Static};
    use DspDecodedMemoryDirection::{Read, Write};
    use DspDecodedMemorySlot::{ParallelPrimary, ParallelSecondary, Standalone};
    use DspDecodedMemorySpace::{Data, Instruction};

    let operand = |slot, space, direction, address_source| DspDecodedMemoryOperand {
        slot,
        space,
        direction,
        address_source,
    };

    match instruction {
        DspInstruction::RegisterTransfer(operation) => match operation {
            DspRegisterTransferOperation::LoadFromMemory { address, .. } => {
                vec![operand(Standalone, Data, Read, Static(address))]
            }
            DspRegisterTransferOperation::StoreToMemory { address, .. }
            | DspRegisterTransferOperation::StoreImmediate { address, .. } => {
                vec![operand(Standalone, Data, Write, Static(address))]
            }
            DspRegisterTransferOperation::LoadImmediate { .. }
            | DspRegisterTransferOperation::Move { .. } => Vec::new(),
        },
        DspInstruction::MemoryTransfer(operation) => match operation {
            DspMemoryTransferOperation::LoadInstructionMemory { address, .. } => vec![operand(
                Standalone,
                Instruction,
                Read,
                AddressRegister(address),
            )],
            DspMemoryTransferOperation::LoadDataMemory { address, .. } => {
                vec![operand(Standalone, Data, Read, AddressRegister(address))]
            }
            DspMemoryTransferOperation::StoreDataMemory { address, .. } => {
                vec![operand(Standalone, Data, Write, AddressRegister(address))]
            }
            DspMemoryTransferOperation::DirectPageLoad { address, .. } => {
                vec![operand(Standalone, Data, Read, DirectPage(address))]
            }
            DspMemoryTransferOperation::DirectPageStoreAccumulatorHigh { address, .. }
            | DspMemoryTransferOperation::DirectPageStoreRegister { address, .. } => {
                vec![operand(Standalone, Data, Write, DirectPage(address))]
            }
        },
        DspInstruction::Parallel { parallel, .. } => match parallel {
            DspParallelOperation::StoreSingle { address, .. } => vec![operand(
                ParallelPrimary,
                Data,
                Write,
                AddressRegister(address),
            )],
            DspParallelOperation::LoadSingle { address, .. } => vec![operand(
                ParallelPrimary,
                Data,
                Read,
                AddressRegister(address),
            )],
            DspParallelOperation::AccumulatorMidLoadStore { order, .. } => {
                let (primary_direction, secondary_direction) = match order {
                    DspParallelLoadStoreOrder::LoadThenStore => (Read, Write),
                    DspParallelLoadStoreOrder::StoreThenLoad => (Write, Read),
                };
                vec![
                    operand(
                        ParallelPrimary,
                        Data,
                        primary_direction,
                        AddressRegister(DspAddressRegister::Ar0),
                    ),
                    operand(
                        ParallelSecondary,
                        Data,
                        secondary_direction,
                        AddressRegister(DspAddressRegister::Ar3),
                    ),
                ]
            }
            DspParallelOperation::LoadAxPair { address, .. }
            | DspParallelOperation::LoadAxParts { address, .. } => vec![
                operand(ParallelPrimary, Data, Read, AddressRegister(address)),
                operand(
                    ParallelSecondary,
                    Data,
                    Read,
                    AddressRegister(DspAddressRegister::Ar3),
                ),
            ],
            DspParallelOperation::Nop { .. }
            | DspParallelOperation::Address(_)
            | DspParallelOperation::Move { .. } => Vec::new(),
        },
        DspInstruction::Nop
        | DspInstruction::Halt
        | DspInstruction::Address(_)
        | DspInstruction::StatusBit(_)
        | DspInstruction::Immediate(_)
        | DspInstruction::Mode(_)
        | DspInstruction::Logic(_)
        | DspInstruction::Accumulator(_)
        | DspInstruction::Product(_)
        | DspInstruction::If { .. }
        | DspInstruction::JumpImmediate { .. }
        | DspInstruction::JumpRegister { .. }
        | DspInstruction::Return { .. }
        | DspInstruction::CallImmediate { .. }
        | DspInstruction::CallRegister { .. }
        | DspInstruction::Loop { .. }
        | DspInstruction::LoopImmediate { .. }
        | DspInstruction::BlockLoop { .. }
        | DspInstruction::BlockLoopImmediate { .. } => Vec::new(),
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct DecodedDspInstruction {
    pub address: u16,
    pub raw_word: u16,
    pub second_word: Option<u16>,
    pub length_words: u8,
    pub instruction: DspInstruction,
}

#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize)]
pub struct DspProgramAudit {
    pub start_address: u16,
    pub word_count: usize,
    pub decoded_word_count: usize,
    pub instruction_count: usize,
    pub multi_word_instruction_count: usize,
    pub parallel_instruction_count: usize,
    pub control_flow_instruction_count: usize,
    pub conditional_control_flow_instruction_count: usize,
    pub call_instruction_count: usize,
    pub jump_instruction_count: usize,
    pub return_instruction_count: usize,
    pub loop_instruction_count: usize,
    pub halt_instruction_count: usize,
    pub arithmetic_instruction_count: usize,
    pub memory_instruction_count: usize,
    pub mode_instruction_count: usize,
    pub address_update_instruction_count: usize,
}

impl DspProgramAudit {
    fn record(&mut self, decoded: &DecodedDspInstruction) {
        self.decoded_word_count += usize::from(decoded.length_words);
        self.instruction_count += 1;
        if decoded.length_words > 1 {
            self.multi_word_instruction_count += 1;
        }
        self.record_instruction(&decoded.instruction);
    }

    fn record_instruction(&mut self, instruction: &DspInstruction) {
        match instruction {
            DspInstruction::Nop => {}
            DspInstruction::Halt => {
                self.control_flow_instruction_count += 1;
                self.halt_instruction_count += 1;
            }
            DspInstruction::Address(_) => {
                self.address_update_instruction_count += 1;
            }
            DspInstruction::StatusBit(_) => {
                self.mode_instruction_count += 1;
            }
            DspInstruction::RegisterTransfer(operation) => {
                self.record_register_transfer(operation);
            }
            DspInstruction::Immediate(operation) => {
                self.record_immediate(operation);
            }
            DspInstruction::MemoryTransfer(_) => {
                self.memory_instruction_count += 1;
            }
            DspInstruction::Mode(_) => {
                self.mode_instruction_count += 1;
            }
            DspInstruction::Logic(_)
            | DspInstruction::Accumulator(_)
            | DspInstruction::Product(_) => {
                self.arithmetic_instruction_count += 1;
            }
            DspInstruction::Parallel {
                primary, parallel, ..
            } => {
                self.parallel_instruction_count += 1;
                self.record_parallel_primary(primary);
                self.record_parallel_operation(parallel);
            }
            DspInstruction::If { condition } => {
                self.control_flow_instruction_count += 1;
                self.record_condition(*condition);
            }
            DspInstruction::JumpImmediate { condition, .. }
            | DspInstruction::JumpRegister { condition, .. } => {
                self.control_flow_instruction_count += 1;
                self.jump_instruction_count += 1;
                self.record_condition(*condition);
            }
            DspInstruction::Return { condition, .. } => {
                self.control_flow_instruction_count += 1;
                self.return_instruction_count += 1;
                self.record_condition(*condition);
            }
            DspInstruction::CallImmediate { condition, .. }
            | DspInstruction::CallRegister { condition, .. } => {
                self.control_flow_instruction_count += 1;
                self.call_instruction_count += 1;
                self.record_condition(*condition);
            }
            DspInstruction::Loop { .. }
            | DspInstruction::LoopImmediate { .. }
            | DspInstruction::BlockLoop { .. }
            | DspInstruction::BlockLoopImmediate { .. } => {
                self.control_flow_instruction_count += 1;
                self.loop_instruction_count += 1;
            }
        }
    }

    fn record_register_transfer(&mut self, operation: &DspRegisterTransferOperation) {
        match operation {
            DspRegisterTransferOperation::LoadFromMemory { .. }
            | DspRegisterTransferOperation::StoreToMemory { .. }
            | DspRegisterTransferOperation::StoreImmediate { .. } => {
                self.memory_instruction_count += 1;
            }
            DspRegisterTransferOperation::LoadImmediate { .. }
            | DspRegisterTransferOperation::Move { .. } => {}
        }
    }

    fn record_immediate(&mut self, operation: &DspImmediateOperation) {
        match operation {
            DspImmediateOperation::LoadRegisterShort { .. } => {}
            DspImmediateOperation::AddShort { .. }
            | DspImmediateOperation::CompareShort { .. }
            | DspImmediateOperation::Add { .. }
            | DspImmediateOperation::Xor { .. }
            | DspImmediateOperation::And { .. }
            | DspImmediateOperation::Or { .. }
            | DspImmediateOperation::Compare { .. }
            | DspImmediateOperation::AndField { .. }
            | DspImmediateOperation::AndCompareField { .. } => {
                self.arithmetic_instruction_count += 1;
            }
        }
    }

    fn record_parallel_primary(&mut self, primary: &DspParallelPrimary) {
        match primary {
            DspParallelPrimary::Mode(_) => self.mode_instruction_count += 1,
            DspParallelPrimary::Logic(_)
            | DspParallelPrimary::Accumulator(_)
            | DspParallelPrimary::Product(_) => self.arithmetic_instruction_count += 1,
        }
    }

    fn record_parallel_operation(&mut self, operation: &DspParallelOperation) {
        match operation {
            DspParallelOperation::Nop { .. } => {}
            DspParallelOperation::Address(_) => {
                self.address_update_instruction_count += 1;
            }
            DspParallelOperation::Move { .. } => {}
            DspParallelOperation::StoreSingle { .. }
            | DspParallelOperation::LoadSingle { .. }
            | DspParallelOperation::AccumulatorMidLoadStore { .. }
            | DspParallelOperation::LoadAxPair { .. }
            | DspParallelOperation::LoadAxParts { .. } => {
                self.memory_instruction_count += 1;
            }
        }
    }

    fn record_condition(&mut self, condition: DspCondition) {
        if condition != DspCondition::Always {
            self.conditional_control_flow_instruction_count += 1;
        }
    }
}

#[derive(Debug, Error, PartialEq, Eq)]
pub enum DspDecodeError {
    #[error("missing DSP opcode word at word address 0x{address:04X}")]
    MissingOpcode { address: u16 },
    #[error("invalid DSP instruction address 0x{address:04X}")]
    InvalidInstructionAddress { address: u16 },
    #[error("unsupported DSP opcode 0x{word:04X} at word address 0x{address:04X}")]
    UnsupportedOpcode { address: u16, word: u16 },
    #[error(
        "DSP instruction 0x{word:04X} at word address 0x{address:04X} needs {expected_words} words, but only {available_words} remain"
    )]
    TruncatedInstruction {
        address: u16,
        word: u16,
        expected_words: usize,
        available_words: usize,
    },
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct GeneratedDspSource {
    pub cpp: String,
    pub instruction_count: usize,
    pub word_count: usize,
    pub lowering_contract_version: u32,
    pub raw_input_aggregate_sha256: String,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DspProgramIdentity {
    pub start_address: u16,
    pub byte_len: usize,
    pub sha1: String,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct DspStaticMemoryImages<'a> {
    pub irom_words: Option<&'a [u16]>,
    pub coefficient_words: Option<&'a [u16]>,
}

#[derive(Debug, Error, PartialEq, Eq)]
pub enum DspLoweringError {
    #[error(transparent)]
    Decode(#[from] DspDecodeError),
    #[error("invalid generated DSP C++ function name {name:?}")]
    InvalidFunctionName { name: String },
    #[error("DSP entry word address 0x{address:04X} is not present in the lowered program")]
    MissingEntryAddress { address: u16 },
    #[error("unsupported DSP lowering for opcode 0x{word:04X} at word address 0x{address:04X}: {reason}")]
    UnsupportedInstruction {
        address: u16,
        word: u16,
        reason: &'static str,
    },
}

pub fn dsp_program_identity_from_words_be(start_address: u16, words: &[u16]) -> DspProgramIdentity {
    let bytes = dsp_words_to_be_bytes(words);

    DspProgramIdentity {
        start_address,
        byte_len: bytes.len(),
        sha1: format!("{:x}", Sha1::digest(&bytes)),
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct DspRawInputComponentIdentity {
    role: &'static str,
    byte_len: usize,
    sha256: [u8; 32],
}

fn dsp_words_to_be_bytes(words: &[u16]) -> Vec<u8> {
    let mut bytes = Vec::with_capacity(words.len() * 2);
    for word in words {
        bytes.extend_from_slice(&word.to_be_bytes());
    }
    bytes
}

fn dsp_raw_input_component_identity(
    role: &'static str,
    words: Option<&[u16]>,
) -> DspRawInputComponentIdentity {
    let words = words.unwrap_or(&[]);
    let mut hasher = Sha256::new();
    for word in words {
        hasher.update(word.to_be_bytes());
    }
    DspRawInputComponentIdentity {
        role,
        byte_len: words.len() * 2,
        sha256: hasher.finalize().into(),
    }
}

fn append_lowercase_hex(output: &mut Vec<u8>, bytes: &[u8]) {
    const HEX: &[u8; 16] = b"0123456789abcdef";
    for byte in bytes {
        output.push(HEX[usize::from(byte >> 4)]);
        output.push(HEX[usize::from(byte & 0x0f)]);
    }
}

fn dsp_raw_input_aggregate_canonical_bytes(
    components: &[DspRawInputComponentIdentity; 3],
) -> Vec<u8> {
    let mut canonical = DSP_RAW_INPUT_AGGREGATE_DOMAIN.to_vec();
    for component in components {
        canonical.extend_from_slice(component.role.as_bytes());
        canonical.push(b'\t');
        canonical.extend_from_slice(component.byte_len.to_string().as_bytes());
        canonical.push(b'\t');
        append_lowercase_hex(&mut canonical, &component.sha256);
        canonical.push(b'\n');
    }
    canonical
}

fn dsp_raw_input_aggregate_sha256(
    raw_iram_words: &[u16],
    static_memory: DspStaticMemoryImages<'_>,
) -> String {
    // An absent optional image is represented truthfully and deterministically
    // as a zero-byte component whose digest is SHA-256(empty). The three roles
    // and their order never change, so absence cannot alias the reviewed full
    // RMGE01 IRAM/IROM/COEF aggregate.
    let components = [
        dsp_raw_input_component_identity("iram", Some(raw_iram_words)),
        dsp_raw_input_component_identity("irom", static_memory.irom_words),
        dsp_raw_input_component_identity("coef", static_memory.coefficient_words),
    ];
    format!(
        "{:x}",
        Sha256::digest(dsp_raw_input_aggregate_canonical_bytes(&components))
    )
}

#[derive(Clone, Debug, Default)]
struct DspProgramLayout {
    labels: BTreeSet<u16>,
    instruction_lengths: BTreeMap<u16, u8>,
    // Word addresses compared against `pc - 1` by the DSP loop hardware. For a
    // BLOOP/BLOOPI this can be the immediate word of a multi-word instruction,
    // not necessarily the instruction's opcode address.
    loop_end_word_addresses: BTreeSet<u16>,
    /// When true the program was recovered by following control flow over a real
    /// ucode image that interleaves data with code, so a branch/loop target that
    /// is not a decoded instruction is lowered as a runtime hard-fail trap rather
    /// than aborting the whole installation. The strict linear path keeps the
    /// original hard-fail behavior.
    tolerant: bool,
}

impl DspProgramLayout {
    fn from_decoded(
        decoded: &[DecodedDspInstruction],
        tolerant: bool,
    ) -> Result<Self, DspLoweringError> {
        let labels = decoded
            .iter()
            .map(|instruction| instruction.address)
            .collect::<BTreeSet<_>>();
        let instruction_lengths = decoded
            .iter()
            .map(|instruction| (instruction.address, instruction.length_words))
            .collect::<BTreeMap<_, _>>();
        let mut loop_end_word_addresses = BTreeSet::new();

        for (index, instruction) in decoded.iter().enumerate() {
            match instruction.instruction {
                DspInstruction::Loop { .. } | DspInstruction::LoopImmediate { .. } => {
                    let Some(body_address) = decoded.get(index + 1).map(|next| next.address) else {
                        if tolerant {
                            continue;
                        }
                        return unsupported_lowering(
                            instruction,
                            "loop body outside lowered DSP program",
                        );
                    };
                    loop_end_word_addresses.insert(body_address);
                }
                DspInstruction::BlockLoop { end_address, .. }
                | DspInstruction::BlockLoopImmediate { end_address, .. } => {
                    if instruction_start_containing_word(&instruction_lengths, end_address)
                        .is_some()
                    {
                        loop_end_word_addresses.insert(end_address);
                    } else if !tolerant {
                        return unsupported_lowering(
                            instruction,
                            "block loop end outside lowered DSP program",
                        );
                    }
                    // In tolerant mode an unresolved end means this is speculative
                    // gap-recovered code (or data) that only decoded as a block
                    // loop; the emitter lowers it as a runtime hard-fail trap.
                }
                _ => {}
            }
        }

        Ok(Self {
            labels,
            instruction_lengths,
            loop_end_word_addresses,
            tolerant,
        })
    }

    fn instruction_after(&self, address: u16) -> Option<u16> {
        self.instruction_lengths
            .get(&address)
            .map(|length| address.wrapping_add(u16::from(*length)))
    }

    fn instruction_after_word(&self, address: u16) -> Option<u16> {
        let start = instruction_start_containing_word(&self.instruction_lengths, address)?;
        self.instruction_after(start)
    }

    fn contains_instruction_word(&self, address: u16) -> bool {
        instruction_start_containing_word(&self.instruction_lengths, address).is_some()
    }

    fn loop_end_word_for_instruction(&self, instruction: &DecodedDspInstruction) -> Option<u16> {
        let end_word = instruction
            .address
            .wrapping_add(u16::from(instruction.length_words) - 1);
        self.loop_end_word_addresses
            .contains(&end_word)
            .then_some(end_word)
    }
}

fn instruction_start_containing_word(
    instruction_lengths: &BTreeMap<u16, u8>,
    address: u16,
) -> Option<u16> {
    let (start, length) = instruction_lengths.range(..=address).next_back()?;
    let end = start.wrapping_add(u16::from(*length) - 1);
    (*start <= address && address <= end).then_some(*start)
}

pub fn decode_dsp_stream(
    start_address: u16,
    words: &[u16],
) -> Result<Vec<DecodedDspInstruction>, DspDecodeError> {
    let mut decoded = Vec::new();
    let mut offset = 0_usize;
    while offset < words.len() {
        let address = start_address.wrapping_add(offset as u16);
        let instruction = decode_dsp_instruction(address, &words[offset..])?;
        offset += usize::from(instruction.length_words);
        decoded.push(instruction);
    }
    Ok(decoded)
}

pub fn audit_dsp_program(
    start_address: u16,
    words: &[u16],
) -> Result<DspProgramAudit, DspDecodeError> {
    let mut audit = DspProgramAudit {
        start_address,
        word_count: words.len(),
        ..DspProgramAudit::default()
    };

    let mut offset = 0_usize;
    while offset < words.len() {
        let address = start_address.wrapping_add(offset as u16);
        require_valid_instruction_address(address)?;
        let decoded = decode_dsp_instruction(address, &words[offset..])?;
        for word_offset in 0..usize::from(decoded.length_words) {
            require_valid_instruction_address(address.wrapping_add(word_offset as u16))?;
        }
        audit.record(&decoded);
        offset += usize::from(decoded.length_words);
    }

    Ok(audit)
}

pub fn lower_dsp_program_to_cpp(
    start_address: u16,
    words: &[u16],
    function_name: &str,
) -> Result<GeneratedDspSource, DspLoweringError> {
    lower_dsp_program_to_cpp_at_entry(start_address, words, start_address, function_name)
}

pub fn lower_dsp_program_to_cpp_at_entry(
    start_address: u16,
    words: &[u16],
    entry_address: u16,
    function_name: &str,
) -> Result<GeneratedDspSource, DspLoweringError> {
    if !is_cpp_identifier(function_name) {
        return Err(DspLoweringError::InvalidFunctionName {
            name: function_name.to_string(),
        });
    }

    let audit = audit_dsp_program(start_address, words)?;
    let decoded = decode_dsp_stream(start_address, words)?;
    let program_identity = dsp_program_identity_from_words_be(start_address, words);
    lower_decoded_instructions(
        &decoded,
        entry_address,
        function_name,
        audit.word_count,
        audit.instruction_count,
        false,
        DspStaticMemoryImages::default(),
        &program_identity,
        words,
    )
}

/// Lower a real DSP ucode image by following control flow from its entry and
/// interrupt vectors instead of decoding the whole image linearly.  Real ucode
/// interleaves data/coefficient pools with code, so a linear sweep mis-aligns
/// instruction boundaries (e.g. a block-loop end lands mid-instruction).  This
/// reachability walk decodes only the words the DSP actually executes, starting
/// from `entry_points`, which keeps every branch/loop boundary aligned.  Data
/// the control flow never reaches is correctly left undecoded.
pub fn lower_dsp_program_from_entry_vectors(
    start_address: u16,
    words: &[u16],
    entry_points: &[u16],
    function_name: &str,
) -> Result<GeneratedDspSource, DspLoweringError> {
    lower_dsp_program_from_entry_vectors_with_static_memory(
        start_address,
        words,
        entry_points,
        function_name,
        DspStaticMemoryImages::default(),
    )
}

/// Lower a DSP program with optional hardware ROM images that are copied into
/// `DspContext` by the generated native entry before any ucode label executes.
pub fn lower_dsp_program_from_entry_vectors_with_static_memory(
    start_address: u16,
    words: &[u16],
    entry_points: &[u16],
    function_name: &str,
    static_memory: DspStaticMemoryImages<'_>,
) -> Result<GeneratedDspSource, DspLoweringError> {
    lower_dsp_program_from_entry_vectors_with_static_memory_and_raw_provenance(
        start_address,
        words,
        entry_points,
        function_name,
        static_memory,
        words,
    )
}

/// Lower a DSP program with optional ROM images while deriving every generated
/// identity and provenance value from the unspliced raw IRAM words. The
/// executable image may contain a sparse IROM splice used only for CFG recovery;
/// it is never treated as the raw IRAM identity.
pub fn lower_dsp_program_from_entry_vectors_with_static_memory_and_raw_provenance(
    start_address: u16,
    executable_words: &[u16],
    entry_points: &[u16],
    function_name: &str,
    static_memory: DspStaticMemoryImages<'_>,
    raw_iram_words: &[u16],
) -> Result<GeneratedDspSource, DspLoweringError> {
    if !is_cpp_identifier(function_name) {
        return Err(DspLoweringError::InvalidFunctionName {
            name: function_name.to_string(),
        });
    }

    let program_identity = dsp_program_identity_from_words_be(start_address, raw_iram_words);
    let mut decoded = decode_dsp_reachable(start_address, executable_words, entry_points)?;
    recover_dsp_gaps(start_address, executable_words, &mut decoded);
    let entry_address = entry_points.first().copied().unwrap_or(start_address);
    lower_decoded_instructions(
        &decoded,
        entry_address,
        function_name,
        executable_words.len(),
        decoded.len(),
        true,
        static_memory,
        &program_identity,
        raw_iram_words,
    )
}

fn has_static_memory(static_memory: DspStaticMemoryImages<'_>) -> bool {
    static_memory.irom_words.is_some() || static_memory.coefficient_words.is_some()
}

fn emit_static_memory_definitions(cpp: &mut String, static_memory: DspStaticMemoryImages<'_>) {
    if !has_static_memory(static_memory) {
        return;
    }

    cpp.push_str("namespace {\n\n");
    if let Some(words) = static_memory.irom_words {
        emit_static_word_array(cpp, "kGalaxyDspIromWords", words);
    }
    if let Some(words) = static_memory.coefficient_words {
        emit_static_word_array(cpp, "kGalaxyDspCoefficientWords", words);
    }

    cpp.push_str("void galaxy_dsp_load_static_memory(galaxy::DspContext& ctx) {\n");
    if let Some(words) = static_memory.irom_words {
        cpp.push_str("    if (!ctx.irom_loaded) {\n");
        cpp.push_str(&format!(
            "        for (std::size_t i = 0; i < {}u; ++i) {{\n",
            words.len()
        ));
        cpp.push_str("            ctx.irom[i] = kGalaxyDspIromWords[i];\n");
        cpp.push_str("        }\n");
        cpp.push_str("        ctx.irom_loaded = true;\n");
        cpp.push_str("    }\n");
    }
    if let Some(words) = static_memory.coefficient_words {
        cpp.push_str("    if (!ctx.coef_loaded) {\n");
        cpp.push_str(&format!(
            "        for (std::size_t i = 0; i < {}u; ++i) {{\n",
            words.len()
        ));
        cpp.push_str("            ctx.coef[i] = kGalaxyDspCoefficientWords[i];\n");
        cpp.push_str("        }\n");
        cpp.push_str("        ctx.coef_loaded = true;\n");
        cpp.push_str("    }\n");
    }
    cpp.push_str("}\n\n");
    cpp.push_str("}  // namespace\n\n");
}

fn emit_static_word_array(cpp: &mut String, name: &str, words: &[u16]) {
    cpp.push_str(&format!("constexpr std::uint16_t {name}[] = {{\n"));
    for chunk in words.chunks(8) {
        cpp.push_str("    ");
        for (index, word) in chunk.iter().enumerate() {
            if index != 0 {
                cpp.push_str(", ");
            }
            cpp.push_str(&cpp_u16_literal(*word));
        }
        cpp.push_str(",\n");
    }
    cpp.push_str("};\n\n");
}

fn emit_program_identity_definitions(
    cpp: &mut String,
    function_name: &str,
    program_identity: &DspProgramIdentity,
    raw_input_aggregate_sha256: &str,
    probe_capabilities: u32,
) {
    let sha1_literal = program_identity.sha1.escape_default().to_string();
    cpp.push_str("extern \"C\" std::uint16_t ");
    cpp.push_str(function_name);
    cpp.push_str("_expected_iram_start_address() {\n");
    cpp.push_str(&format!(
        "    return {};\n",
        cpp_u16_literal(program_identity.start_address)
    ));
    cpp.push_str("}\n\n");

    cpp.push_str("extern \"C\" std::uint32_t ");
    cpp.push_str(function_name);
    cpp.push_str("_expected_iram_byte_len() {\n");
    cpp.push_str(&format!("    return {}u;\n", program_identity.byte_len));
    cpp.push_str("}\n\n");

    cpp.push_str("extern \"C\" const char* ");
    cpp.push_str(function_name);
    cpp.push_str("_expected_iram_sha1() {\n");
    cpp.push_str(&format!("    return \"{sha1_literal}\";\n"));
    cpp.push_str("}\n\n");

    cpp.push_str("extern \"C\" std::uint32_t ");
    cpp.push_str(function_name);
    cpp.push_str("_generated_lowering_contract_version(){return ");
    cpp.push_str(&DSP_GENERATED_LOWERING_CONTRACT_VERSION.to_string());
    cpp.push_str("u;}\n\n");

    cpp.push_str("extern \"C\" const char* ");
    cpp.push_str(function_name);
    cpp.push_str("_generated_raw_input_aggregate_sha256(){return \"");
    cpp.push_str(raw_input_aggregate_sha256);
    cpp.push_str("\";}\n\n");

    // These are literal generator identities, deliberately not values read
    // from a runtime header.  Old generated C++ has no symbols and fails to
    // link; a source/runtime mismatch that still exports them fails the host's
    // explicit version/capability check before the DSP worker starts.
    cpp.push_str("extern \"C\" std::uint32_t ");
    cpp.push_str(function_name);
    cpp.push_str("_generated_probe_contract_version() {\n");
    cpp.push_str(&format!(
        "    return {DSP_GENERATED_PROBE_CONTRACT_VERSION}u;\n"
    ));
    cpp.push_str("}\n\n");

    cpp.push_str("extern \"C\" std::uint32_t ");
    cpp.push_str(function_name);
    cpp.push_str("_generated_probe_capabilities() {\n");
    cpp.push_str(&format!("    return {probe_capabilities}u;\n"));
    cpp.push_str("}\n\n");
}

fn is_rmge01_selection_publication_hook(instruction: &DecodedDspInstruction) -> bool {
    instruction.address == 0x0715 && instruction.raw_word == 0x1A1E
}

fn is_rmge01_selected_channel_branch_hook(instruction: &DecodedDspInstruction) -> bool {
    instruction.address == 0x02EF
        && instruction.raw_word == 0x02BF
        && matches!(
            instruction.instruction,
            DspInstruction::CallImmediate {
                condition: DspCondition::Always,
                target: 0x00CC
            }
        )
}

fn generated_probe_capabilities(decoded: &[DecodedDspInstruction]) -> u32 {
    let mut capabilities = 0u32;
    for instruction in decoded {
        if is_rmge01_selection_publication_hook(instruction) {
            capabilities |= DSP_GENERATED_PROBE_CAPABILITY_SELECTION_PUBLICATION;
        }
        if is_rmge01_selected_channel_branch_hook(instruction) {
            capabilities |= DSP_GENERATED_PROBE_CAPABILITY_SELECTED_CHANNEL_BRANCH;
        }
    }
    capabilities
}

#[allow(clippy::too_many_arguments)]
fn lower_decoded_instructions(
    decoded: &[DecodedDspInstruction],
    entry_address: u16,
    function_name: &str,
    word_count: usize,
    instruction_count: usize,
    tolerant: bool,
    static_memory: DspStaticMemoryImages<'_>,
    program_identity: &DspProgramIdentity,
    raw_iram_words: &[u16],
) -> Result<GeneratedDspSource, DspLoweringError> {
    let layout = DspProgramLayout::from_decoded(decoded, tolerant)?;
    let raw_input_aggregate_sha256 = dsp_raw_input_aggregate_sha256(raw_iram_words, static_memory);

    let mut cpp = String::new();
    cpp.push_str("// Generated by nebula-recomp DSP static lowering.\n");
    cpp.push_str("// The installer must hard-fail before emitting unsupported opcodes.\n");
    cpp.push_str("#include \"galaxy/dsp_context.h\"\n");
    cpp.push_str("#include \"galaxy/dsp_instruction_provenance.h\"\n\n");
    emit_static_memory_definitions(&mut cpp, static_memory);
    emit_program_identity_definitions(
        &mut cpp,
        function_name,
        program_identity,
        &raw_input_aggregate_sha256,
        generated_probe_capabilities(decoded),
    );
    cpp.push_str("#if defined(_MSC_VER)\n");
    cpp.push_str("#pragma warning(push)\n");
    cpp.push_str("#pragma warning(disable : 4102)\n");
    cpp.push_str("#pragma warning(disable : 4127)\n");
    cpp.push_str("#pragma warning(disable : 4702)\n");
    cpp.push_str("#endif\n\n");
    cpp.push_str("extern \"C\" void ");
    cpp.push_str(function_name);
    cpp.push_str("(galaxy::DspContext& ctx) {\n");
    cpp.push_str("    (void)ctx;\n");
    if has_static_memory(static_memory) {
        cpp.push_str("    galaxy_dsp_load_static_memory(ctx);\n");
    }

    if decoded.is_empty() {
        cpp.push_str("    return;\n");
    } else if layout.labels.contains(&entry_address) {
        cpp.push_str("    if (ctx.pc != 0u) {\n");
        cpp.push_str("        goto pc_dispatch;\n");
        cpp.push_str("    }\n");
        emit_dsp_goto(&mut cpp, entry_address);
    } else {
        return Err(DspLoweringError::MissingEntryAddress {
            address: entry_address,
        });
    }

    for instruction in decoded {
        cpp.push('\n');
        cpp.push_str(&dsp_label(instruction.address));
        cpp.push_str(":\n{\n");
        cpp.push_str(&format!(
            "    // {:04X}: {:04X}\n",
            instruction.address, instruction.raw_word
        ));
        cpp.push_str(&format!(
            "    ctx.pc = {};\n",
            cpp_u16_literal(instruction.address)
        ));
        cpp.push_str(&format!(
            "    ctx.last_retired_pc = {};\n",
            cpp_u16_literal(instruction.address)
        ));
        emit_raw_instruction_boundary_scope(&mut cpp, instruction);
        let next_address = layout.instruction_after(instruction.address);
        let skip_address = next_address.and_then(|next| layout.instruction_after(next));
        emit_lowered_dsp_instruction(&mut cpp, instruction, next_address, skip_address, &layout)?;
        cpp.push_str("}\n");
    }

    if !decoded.is_empty() {
        emit_dsp_dispatch_block(&mut cpp, &layout.labels);
    }
    emit_dsp_trap_labels(&mut cpp, &layout.labels);

    cpp.push_str("}\n");
    cpp.push_str("\n#if defined(_MSC_VER)\n");
    cpp.push_str("#pragma warning(pop)\n");
    cpp.push_str("#endif\n");
    Ok(GeneratedDspSource {
        cpp,
        instruction_count,
        word_count,
        lowering_contract_version: DSP_GENERATED_LOWERING_CONTRACT_VERSION,
        raw_input_aggregate_sha256,
    })
}

fn emit_raw_instruction_boundary_scope(cpp: &mut String, decoded: &DecodedDspInstruction) {
    let (bundle_kind, parallel_primary_word, parallel_extension, parallel_extension_mask) =
        if matches!(decoded.instruction, DspInstruction::Parallel { .. }) {
            let extension_mask = if decoded.raw_word >> 12 == 0x3 {
                0x007f_u16
            } else {
                0x00ff_u16
            };
            (
                "Parallel",
                decoded.raw_word & !extension_mask,
                decoded.raw_word & extension_mask,
                extension_mask,
            )
        } else {
            ("Standalone", 0_u16, 0_u16, 0_u16)
        };
    let second_word = decoded.second_word.unwrap_or(0);
    cpp.push_str("    galaxy::DspRawInstructionBoundaryScope raw_instruction_boundary{\n");
    cpp.push_str("        ctx.raw_instruction_boundary_observer,\n");
    cpp.push_str("        ctx,\n");
    cpp.push_str("        galaxy::DspGeneratedInstructionIdentity{\n");
    cpp.push_str(&format!(
        "            {},\n",
        cpp_u16_literal(decoded.address)
    ));
    cpp.push_str(&format!(
        "            {},\n",
        cpp_u16_literal(decoded.raw_word)
    ));
    cpp.push_str(&format!("            {},\n", cpp_u16_literal(second_word)));
    cpp.push_str(if decoded.second_word.is_some() {
        "            true,\n"
    } else {
        "            false,\n"
    });
    cpp.push_str(&format!(
        "            galaxy::DspGeneratedInstructionBundleKind::{bundle_kind},\n"
    ));
    cpp.push_str(&format!(
        "            {},\n",
        cpp_u16_literal(parallel_primary_word)
    ));
    cpp.push_str(&format!(
        "            static_cast<std::uint8_t>({}),\n",
        cpp_u16_literal(parallel_extension)
    ));
    cpp.push_str(&format!(
        "            static_cast<std::uint8_t>({})}},\n",
        cpp_u16_literal(parallel_extension_mask)
    ));
    let memory_operands = decoded_memory_operands(decoded.instruction);
    debug_assert!(memory_operands.len() <= 3);
    cpp.push_str("        galaxy::DspGeneratedMemoryOperandPlanSet{\n");
    cpp.push_str("            {\n");
    for operand in &memory_operands {
        let slot = match operand.slot {
            DspDecodedMemorySlot::Standalone => "Standalone",
            DspDecodedMemorySlot::ParallelPrimary => "ParallelPrimary",
            DspDecodedMemorySlot::ParallelSecondary => "ParallelSecondary",
        };
        let space = match operand.space {
            DspDecodedMemorySpace::Instruction => "Instruction",
            DspDecodedMemorySpace::Data => "Data",
        };
        let direction = match operand.direction {
            DspDecodedMemoryDirection::Read => "Read",
            DspDecodedMemoryDirection::Write => "Write",
        };
        let (source_kind, source_value) = match operand.address_source {
            DspDecodedMemoryAddressSource::Static(address) => ("Static", address),
            DspDecodedMemoryAddressSource::AddressRegister(address) => {
                ("AddressRegister", address_register_index(address) as u16)
            }
            DspDecodedMemoryAddressSource::DirectPage(address) => {
                ("DirectPage", u16::from(address))
            }
        };
        cpp.push_str("                galaxy::DspGeneratedMemoryOperandPlan{\n");
        cpp.push_str(&format!(
            "                    galaxy::DspGeneratedMemorySlot::{slot},\n"
        ));
        cpp.push_str(&format!(
            "                    galaxy::DspGeneratedMemorySpace::{space},\n"
        ));
        cpp.push_str(&format!(
            "                    galaxy::DspGeneratedMemoryDirection::{direction},\n"
        ));
        cpp.push_str(&format!(
            "                    galaxy::DspGeneratedMemoryAddressSourceKind::{source_kind},\n"
        ));
        cpp.push_str(&format!(
            "                    {}}},\n",
            cpp_u16_literal(source_value)
        ));
    }
    for _ in memory_operands.len()..3 {
        cpp.push_str("                galaxy::DspGeneratedMemoryOperandPlan{},\n");
    }
    cpp.push_str("            },\n");
    cpp.push_str(&format!(
        "            static_cast<std::uint8_t>({})}}}};\n",
        memory_operands.len()
    ));
    cpp.push_str(
        "    galaxy::dsp_raw_instruction_boundary_require_ready(ctx, raw_instruction_boundary);\n",
    );
}

/// Decode every instruction reachable from `entry_points` by following static
/// control flow (fallthrough, immediate jumps/calls, loop bodies).  Computed
/// register jumps/calls cannot be followed statically; their targets are reached
/// through the compiled-label dispatch at runtime and must be decoded via some
/// other static edge.  Returns the reachable instructions sorted by address.
fn decode_dsp_reachable(
    start_address: u16,
    words: &[u16],
    entry_points: &[u16],
) -> Result<Vec<DecodedDspInstruction>, DspDecodeError> {
    let word_index = |address: u16| -> Option<usize> {
        let offset = usize::from(address.wrapping_sub(start_address));
        (offset < words.len()).then_some(offset)
    };

    let mut decoded: BTreeMap<u16, DecodedDspInstruction> = BTreeMap::new();
    let mut worklist: Vec<u16> = entry_points
        .iter()
        .copied()
        .filter(|address| word_index(*address).is_some())
        .collect();

    while let Some(address) = worklist.pop() {
        if decoded.contains_key(&address) {
            continue;
        }
        let Some(offset) = word_index(address) else {
            continue;
        };
        require_valid_instruction_address(address)?;
        let instruction = decode_dsp_instruction(address, &words[offset..])?;
        for word_offset in 0..u16::from(instruction.length_words) {
            require_valid_instruction_address(address.wrapping_add(word_offset))?;
        }
        let successors = dsp_static_successors(start_address, words, &instruction);
        decoded.insert(address, instruction);
        for successor in successors {
            if !decoded.contains_key(&successor) && word_index(successor).is_some() {
                worklist.push(successor);
            }
        }
    }

    Ok(decoded.into_values().collect())
}

/// Recover code that is only reachable through computed (register) jumps — the
/// jump-table handlers a static CFG walk cannot follow.  Every gap between two
/// strictly-reachable instructions begins at an aligned instruction boundary
/// (the fallthrough of the instruction before the gap), so each gap start is a
/// sound place to resume decoding.  This walk is tolerant: words that do not
/// decode, or that would overlap already-covered code, are treated as data and
/// skipped rather than hard-failing, since they are never proven to execute.
/// (A runtime computed jump into an undecoded address still hard-fails through
/// the compiled-label dispatch.)
fn recover_dsp_gaps(start_address: u16, words: &[u16], decoded: &mut Vec<DecodedDspInstruction>) {
    let mut map: BTreeMap<u16, DecodedDspInstruction> =
        decoded.iter().map(|inst| (inst.address, *inst)).collect();

    let mut covered = vec![false; words.len()];
    let mark = |covered: &mut [bool], inst: &DecodedDspInstruction| {
        let base = usize::from(inst.address.wrapping_sub(start_address));
        for offset in 0..usize::from(inst.length_words) {
            if let Some(slot) = covered.get_mut(base + offset) {
                *slot = true;
            }
        }
    };
    for inst in map.values() {
        mark(&mut covered, inst);
    }

    let word_index = |address: u16| -> Option<usize> {
        let offset = usize::from(address.wrapping_sub(start_address));
        (offset < words.len()).then_some(offset)
    };

    let mut scan = 0usize;
    while scan < words.len() {
        if covered[scan] {
            scan += 1;
            continue;
        }

        let mut progressed = false;
        let mut worklist = vec![start_address.wrapping_add(scan as u16)];
        while let Some(address) = worklist.pop() {
            if map.contains_key(&address) {
                continue;
            }
            let Some(offset) = word_index(address) else {
                continue;
            };
            if require_valid_instruction_address(address).is_err() {
                continue;
            }
            let Ok(instruction) = decode_dsp_instruction(address, &words[offset..]) else {
                continue;
            };
            let length = usize::from(instruction.length_words);
            // Reject decodes that run off the image or overlap established code;
            // an overlap means this position is not a real instruction boundary.
            if (0..length).any(|word| {
                covered.get(offset + word).copied().unwrap_or(true)
                    || require_valid_instruction_address(address.wrapping_add(word as u16)).is_err()
            }) {
                continue;
            }

            mark(&mut covered, &instruction);
            let successors = dsp_static_successors(start_address, words, &instruction);
            map.insert(address, instruction);
            progressed = true;
            for successor in successors {
                if !map.contains_key(&successor) {
                    worklist.push(successor);
                }
            }
        }

        if !progressed {
            // No instruction starts here; treat the word as data and move on.
            covered[scan] = true;
        }
    }

    *decoded = map.into_values().collect();
}

/// Static control-flow successors of a decoded instruction: the addresses the
/// DSP can reach without a runtime-computed branch target.
fn dsp_static_successors(
    start_address: u16,
    words: &[u16],
    decoded: &DecodedDspInstruction,
) -> Vec<u16> {
    let fallthrough = decoded
        .address
        .wrapping_add(u16::from(decoded.length_words));
    let is_conditional = |condition: DspCondition| !matches!(condition, DspCondition::Always);
    let with_skip_after = |body_address: u16| {
        let mut successors = vec![body_address];
        if let Some(skip_address) =
            dsp_instruction_after_in_image(start_address, words, body_address)
        {
            successors.push(skip_address);
        }
        successors
    };
    match decoded.instruction {
        // The DSP stops at HALT until it is reset; no static successor.
        DspInstruction::Halt => Vec::new(),
        DspInstruction::If { .. } => with_skip_after(fallthrough),
        DspInstruction::JumpImmediate { condition, target } => {
            if is_conditional(condition) {
                vec![target, fallthrough]
            } else {
                vec![target]
            }
        }
        // Computed jumps and returns leave through a runtime target; only the
        // conditional not-taken path falls through statically.
        DspInstruction::JumpRegister { condition, .. }
        | DspInstruction::Return { condition, .. } => {
            if is_conditional(condition) {
                vec![fallthrough]
            } else {
                Vec::new()
            }
        }
        // A call always resumes at the return address (fallthrough) once it
        // returns, and an immediate call also reaches its target.
        DspInstruction::CallImmediate { target, .. } => vec![target, fallthrough],
        DspInstruction::CallRegister { .. } => vec![fallthrough],
        DspInstruction::Loop { .. } | DspInstruction::LoopImmediate { .. } => {
            with_skip_after(fallthrough)
        }
        DspInstruction::BlockLoop { end_address, .. }
        | DspInstruction::BlockLoopImmediate { end_address, .. } => {
            let mut successors = vec![fallthrough];
            if let Some(skip_address) = dsp_instruction_after_block_end_in_image(
                start_address,
                words,
                fallthrough,
                end_address,
            ) {
                successors.push(skip_address);
            }
            successors
        }
        // Everything else continues by fallthrough.
        _ => vec![fallthrough],
    }
}

fn dsp_instruction_after_in_image(start_address: u16, words: &[u16], address: u16) -> Option<u16> {
    let offset = usize::from(address.wrapping_sub(start_address));
    if offset >= words.len() || require_valid_instruction_address(address).is_err() {
        return None;
    }
    let instruction = decode_dsp_instruction(address, &words[offset..]).ok()?;
    for word_offset in 0..u16::from(instruction.length_words) {
        if require_valid_instruction_address(address.wrapping_add(word_offset)).is_err() {
            return None;
        }
    }
    Some(address.wrapping_add(u16::from(instruction.length_words)))
}

fn dsp_instruction_after_block_end_in_image(
    start_address: u16,
    words: &[u16],
    body_address: u16,
    end_word_address: u16,
) -> Option<u16> {
    let mut address = body_address;
    loop {
        let offset = usize::from(address.wrapping_sub(start_address));
        if offset >= words.len() || require_valid_instruction_address(address).is_err() {
            return None;
        }
        let instruction = decode_dsp_instruction(address, &words[offset..]).ok()?;
        for word_offset in 0..u16::from(instruction.length_words) {
            if require_valid_instruction_address(address.wrapping_add(word_offset)).is_err() {
                return None;
            }
        }
        let instruction_end = address.wrapping_add(u16::from(instruction.length_words) - 1);
        if address <= end_word_address && end_word_address <= instruction_end {
            return Some(instruction_end.wrapping_add(1));
        }
        if instruction_end >= end_word_address {
            return None;
        }
        address = instruction_end.wrapping_add(1);
    }
}

fn emit_lowered_dsp_instruction(
    cpp: &mut String,
    decoded: &DecodedDspInstruction,
    next_address: Option<u16>,
    skip_address: Option<u16>,
    layout: &DspProgramLayout,
) -> Result<(), DspLoweringError> {
    match decoded.instruction {
        DspInstruction::Nop => emit_fallthrough(cpp, decoded, next_address, layout),
        DspInstruction::StatusBit(operation) => {
            emit_status_bit_operation(cpp, operation);
            emit_fallthrough_with_post_commit_interrupt_acceptance(
                cpp,
                decoded,
                next_address,
                layout,
                status_bit_enables_external_interrupt(operation),
            )
        }
        DspInstruction::Address(operation) => {
            emit_address_operation(cpp, operation);
            emit_fallthrough(cpp, decoded, next_address, layout)
        }
        DspInstruction::Mode(operation) => {
            emit_mode_operation(cpp, operation);
            emit_fallthrough(cpp, decoded, next_address, layout)
        }
        DspInstruction::MemoryTransfer(operation) => {
            emit_memory_transfer_operation(cpp, operation, decoded)?;
            emit_fallthrough(cpp, decoded, next_address, layout)
        }
        DspInstruction::Logic(operation) => {
            emit_logic_operation(cpp, operation, decoded)?;
            emit_fallthrough(cpp, decoded, next_address, layout)
        }
        DspInstruction::Accumulator(operation) => {
            emit_accumulator_operation(cpp, operation, decoded)?;
            emit_fallthrough(cpp, decoded, next_address, layout)
        }
        DspInstruction::Product(operation) => {
            emit_product_operation(cpp, operation, decoded)?;
            emit_fallthrough(cpp, decoded, next_address, layout)
        }
        DspInstruction::Parallel {
            primary,
            parallel: DspParallelOperation::Nop { .. },
            ..
        } => {
            emit_parallel_primary(cpp, primary, decoded)?;
            emit_fallthrough(cpp, decoded, next_address, layout)
        }
        DspInstruction::Parallel {
            primary,
            parallel: DspParallelOperation::Address(operation),
            ..
        } => {
            emit_parallel_primary(cpp, primary, decoded)?;
            emit_address_operation(cpp, operation);
            emit_fallthrough(cpp, decoded, next_address, layout)
        }
        DspInstruction::Parallel {
            primary: DspParallelPrimary::Mode(DspModeOperation::ExtendableNop),
            parallel: DspParallelOperation::Move { target, source },
            ..
        } => {
            emit_parallel_register_move(cpp, target, source, decoded)?;
            emit_fallthrough(cpp, decoded, next_address, layout)
        }
        DspInstruction::Parallel {
            primary: DspParallelPrimary::Mode(DspModeOperation::ExtendableNop),
            parallel:
                DspParallelOperation::StoreSingle {
                    address,
                    source,
                    update,
                },
            ..
        } => {
            emit_parallel_store_single(cpp, address, source, update, decoded)?;
            emit_fallthrough(cpp, decoded, next_address, layout)
        }
        DspInstruction::Parallel {
            primary: DspParallelPrimary::Mode(DspModeOperation::ExtendableNop),
            parallel:
                DspParallelOperation::LoadSingle {
                    target,
                    address,
                    update,
                },
            ..
        } => {
            emit_parallel_load_single(cpp, target, address, update, decoded)?;
            emit_fallthrough(cpp, decoded, next_address, layout)
        }
        DspInstruction::Parallel {
            primary: DspParallelPrimary::Mode(DspModeOperation::ExtendableNop),
            parallel:
                DspParallelOperation::AccumulatorMidLoadStore {
                    order,
                    target,
                    accumulator,
                    ar0_update,
                    ar3_update,
                },
            ..
        } => {
            emit_parallel_accumulator_mid_load_store(
                cpp,
                order,
                target,
                accumulator,
                ar0_update,
                ar3_update,
                decoded,
            )?;
            emit_fallthrough(cpp, decoded, next_address, layout)
        }
        DspInstruction::Parallel {
            primary: DspParallelPrimary::Mode(DspModeOperation::ExtendableNop),
            parallel:
                DspParallelOperation::LoadAxPair {
                    ax,
                    address,
                    ar_update,
                    ar3_update,
                },
            ..
        } => {
            emit_parallel_load_ax_pair(cpp, ax, address, ar_update, ar3_update, decoded)?;
            emit_fallthrough(cpp, decoded, next_address, layout)
        }
        DspInstruction::Parallel {
            primary: DspParallelPrimary::Mode(DspModeOperation::ExtendableNop),
            parallel:
                DspParallelOperation::LoadAxParts {
                    ax0_half,
                    ax1_half,
                    address,
                    ar_update,
                    ar3_update,
                },
            ..
        } => {
            emit_parallel_load_ax_parts(
                cpp, ax0_half, ax1_half, address, ar_update, ar3_update, decoded,
            )?;
            emit_fallthrough(cpp, decoded, next_address, layout)
        }
        DspInstruction::Parallel {
            primary,
            parallel: DspParallelOperation::Move { target, source },
            ..
        } => {
            emit_parallel_register_move_with_primary(cpp, primary, target, source, decoded)?;
            emit_fallthrough(cpp, decoded, next_address, layout)
        }
        DspInstruction::Parallel {
            primary,
            parallel:
                DspParallelOperation::StoreSingle {
                    address,
                    source,
                    update,
                },
            ..
        } => {
            emit_parallel_store_single_with_primary(
                cpp, primary, address, source, update, decoded,
            )?;
            emit_fallthrough(cpp, decoded, next_address, layout)
        }
        DspInstruction::Parallel {
            primary,
            parallel:
                DspParallelOperation::LoadSingle {
                    target,
                    address,
                    update,
                },
            ..
        } => {
            emit_parallel_load_single_with_primary(cpp, primary, target, address, update, decoded)?;
            emit_fallthrough(cpp, decoded, next_address, layout)
        }
        DspInstruction::Parallel {
            primary,
            parallel:
                DspParallelOperation::AccumulatorMidLoadStore {
                    order,
                    target,
                    accumulator,
                    ar0_update,
                    ar3_update,
                },
            ..
        } => {
            emit_parallel_accumulator_mid_load_store_with_primary(
                cpp,
                primary,
                order,
                target,
                accumulator,
                (ar0_update, ar3_update),
                decoded,
            )?;
            emit_fallthrough(cpp, decoded, next_address, layout)
        }
        DspInstruction::Parallel {
            primary,
            parallel:
                DspParallelOperation::LoadAxPair {
                    ax,
                    address,
                    ar_update,
                    ar3_update,
                },
            ..
        } => {
            emit_parallel_load_ax_pair_with_primary(
                cpp, primary, ax, address, ar_update, ar3_update, decoded,
            )?;
            emit_fallthrough(cpp, decoded, next_address, layout)
        }
        DspInstruction::Parallel {
            primary,
            parallel:
                DspParallelOperation::LoadAxParts {
                    ax0_half,
                    ax1_half,
                    address,
                    ar_update,
                    ar3_update,
                },
            ..
        } => {
            emit_parallel_load_ax_parts_with_primary(
                cpp,
                primary,
                (ax0_half, ax1_half),
                address,
                ar_update,
                ar3_update,
                decoded,
            )?;
            emit_fallthrough(cpp, decoded, next_address, layout)
        }
        DspInstruction::RegisterTransfer(operation) => {
            emit_register_transfer_operation(cpp, operation, decoded)?;
            emit_fallthrough(cpp, decoded, next_address, layout)
        }
        DspInstruction::Immediate(operation) => {
            emit_immediate_operation(cpp, operation, decoded)?;
            emit_fallthrough(cpp, decoded, next_address, layout)
        }
        DspInstruction::If {
            condition: DspCondition::Always,
        } => emit_fallthrough(cpp, decoded, next_address, layout),
        DspInstruction::If { condition } => {
            let Some(execute_address) = next_address else {
                return bail_or_trap(
                    cpp,
                    layout,
                    decoded,
                    "conditional IF has no following instruction",
                );
            };
            let Some(skip_address) = skip_address else {
                return bail_or_trap(
                    cpp,
                    layout,
                    decoded,
                    "conditional IF skip target outside lowered DSP program",
                );
            };
            require_decoded_static_target(
                layout,
                decoded,
                execute_address,
                "conditional IF has no following instruction",
            )?;
            require_decoded_static_target(
                layout,
                decoded,
                skip_address,
                "conditional IF skip target outside lowered DSP program",
            )?;
            if layout.loop_end_word_for_instruction(decoded).is_some() {
                let target_name = format!("if_target_{:04X}", decoded.address);
                cpp.push_str(&format!(
                    "    std::uint16_t {target_name} = {};\n",
                    cpp_u16_literal(skip_address)
                ));
                cpp.push_str(&format!(
                    "    if (galaxy::dsp_condition_holds(galaxy::DspCondition::{}, ctx.sr)) {{\n",
                    condition_cpp_name(condition)
                ));
                cpp.push_str(&format!(
                    "        {target_name} = {};\n",
                    cpp_u16_literal(execute_address)
                ));
                cpp.push_str("    }\n");
                emit_dynamic_transfer(cpp, decoded, &target_name, layout, 4);
                return Ok(());
            }
            cpp.push_str(&format!(
                "    if (galaxy::dsp_condition_holds(galaxy::DspCondition::{}, ctx.sr)) {{\n",
                condition_cpp_name(condition)
            ));
            emit_raw_instruction_commit(cpp, 8);
            emit_static_backedge_interrupt_checkpoint(cpp, decoded, execute_address, 8);
            emit_dsp_goto_indented(cpp, execute_address, 8);
            cpp.push_str("    }\n");
            emit_raw_instruction_commit(cpp, 4);
            emit_static_backedge_interrupt_checkpoint(cpp, decoded, skip_address, 4);
            emit_dsp_goto(cpp, skip_address);
            Ok(())
        }
        DspInstruction::Halt => {
            cpp.push_str(&format!(
                "    ctx.pc = {};\n",
                cpp_u16_literal(decoded.address)
            ));
            cpp.push_str("    ctx.halted = true;\n");
            emit_raw_instruction_commit(cpp, 4);
            cpp.push_str("    return;\n");
            Ok(())
        }
        DspInstruction::JumpImmediate {
            condition: DspCondition::Always,
            target,
        } => {
            if !layout.labels.contains(&target) && !layout.tolerant {
                return unsupported_lowering(decoded, "jump target outside lowered DSP program");
            }
            emit_static_transfer(cpp, decoded, target, layout)
        }
        DspInstruction::JumpImmediate { condition, target } => {
            if !layout.labels.contains(&target) && !layout.tolerant {
                return unsupported_lowering(decoded, "jump target outside lowered DSP program");
            }
            let Some(fallthrough) = next_address else {
                return bail_or_trap(
                    cpp,
                    layout,
                    decoded,
                    "conditional jump fallthrough leaves lowered DSP program",
                );
            };
            require_decoded_static_target(
                layout,
                decoded,
                fallthrough,
                "conditional jump fallthrough leaves lowered DSP program",
            )?;
            if layout.loop_end_word_for_instruction(decoded).is_some() {
                let target_name = format!("jump_target_{:04X}", decoded.address);
                cpp.push_str(&format!(
                    "    std::uint16_t {target_name} = {};\n",
                    cpp_u16_literal(fallthrough)
                ));
                cpp.push_str(&format!(
                    "    if (galaxy::dsp_condition_holds(galaxy::DspCondition::{}, ctx.sr)) {{\n",
                    condition_cpp_name(condition)
                ));
                cpp.push_str(&format!(
                    "        {target_name} = {};\n",
                    cpp_u16_literal(target)
                ));
                cpp.push_str("    }\n");
                emit_dynamic_transfer(cpp, decoded, &target_name, layout, 4);
                return Ok(());
            }
            cpp.push_str(&format!(
                "    if (galaxy::dsp_condition_holds(galaxy::DspCondition::{}, ctx.sr)) {{\n",
                condition_cpp_name(condition)
            ));
            emit_raw_instruction_commit(cpp, 8);
            emit_static_backedge_interrupt_checkpoint(cpp, decoded, target, 8);
            emit_dsp_goto_indented(cpp, target, 8);
            cpp.push_str("    }\n");
            emit_raw_instruction_commit(cpp, 4);
            emit_static_backedge_interrupt_checkpoint(cpp, decoded, fallthrough, 4);
            emit_dsp_goto(cpp, fallthrough);
            Ok(())
        }
        DspInstruction::JumpRegister { condition, target } => {
            let target_expression = register_read_expression(target);
            let target_name = format!("jump_target_{:04X}", decoded.address);
            if condition == DspCondition::Always {
                cpp.push_str(&format!(
                    "    const auto {target_name} = static_cast<std::uint16_t>({target_expression});\n"
                ));
                emit_dynamic_transfer(cpp, decoded, &target_name, layout, 4);
                return Ok(());
            }

            let Some(fallthrough) = next_address else {
                return bail_or_trap(
                    cpp,
                    layout,
                    decoded,
                    "conditional register jump fallthrough leaves lowered DSP program",
                );
            };
            require_decoded_static_target(
                layout,
                decoded,
                fallthrough,
                "conditional register jump fallthrough leaves lowered DSP program",
            )?;
            if layout.loop_end_word_for_instruction(decoded).is_some() {
                let next_target_name = format!("jump_next_{:04X}", decoded.address);
                cpp.push_str(&format!(
                    "    std::uint16_t {next_target_name} = {};\n",
                    cpp_u16_literal(fallthrough)
                ));
                cpp.push_str(&format!(
                    "    if (galaxy::dsp_condition_holds(galaxy::DspCondition::{}, ctx.sr)) {{\n",
                    condition_cpp_name(condition)
                ));
                cpp.push_str(&format!(
                    "        const auto {target_name} = static_cast<std::uint16_t>({target_expression});\n"
                ));
                cpp.push_str(&format!("        {next_target_name} = {target_name};\n"));
                cpp.push_str("    }\n");
                emit_dynamic_transfer(cpp, decoded, &next_target_name, layout, 4);
                return Ok(());
            }
            cpp.push_str(&format!(
                "    if (galaxy::dsp_condition_holds(galaxy::DspCondition::{}, ctx.sr)) {{\n",
                condition_cpp_name(condition)
            ));
            cpp.push_str(&format!(
                "        const auto {target_name} = static_cast<std::uint16_t>({target_expression});\n"
            ));
            emit_raw_instruction_commit(cpp, 8);
            emit_dynamic_backedge_interrupt_checkpoint(cpp, decoded, &target_name, 8);
            emit_dynamic_dsp_goto(cpp, &target_name, 8);
            cpp.push_str("    }\n");
            emit_raw_instruction_commit(cpp, 4);
            emit_static_backedge_interrupt_checkpoint(cpp, decoded, fallthrough, 4);
            emit_dsp_goto(cpp, fallthrough);
            Ok(())
        }
        DspInstruction::CallImmediate { condition, target } => {
            if !layout.labels.contains(&target) && !layout.tolerant {
                return unsupported_lowering(decoded, "call target outside lowered DSP program");
            }
            let Some(return_address) = next_address else {
                return bail_or_trap(
                    cpp,
                    layout,
                    decoded,
                    "call return address leaves lowered DSP program",
                );
            };
            require_decoded_static_target(
                layout,
                decoded,
                return_address,
                "call return address leaves lowered DSP program",
            )?;

            if condition == DspCondition::Always {
                if is_rmge01_selected_channel_branch_hook(decoded) {
                    cpp.push_str("    const auto selected_channel_02EF = ctx.dram[0x0354u];\n");
                    cpp.push_str(
                        "    const std::uint16_t selection_word_02EF = selected_channel_02EF < 64u ? ctx.dram[static_cast<std::size_t>(0x04FCu + (selected_channel_02EF >> 4u))] : std::uint16_t{0};\n",
                    );
                    cpp.push_str(
                        "    const auto expected_channel_record_host_02EF = (static_cast<std::uint32_t>(ctx.dram[0x034Cu]) << 16u) | ctx.dram[0x034Du];\n",
                    );
                    cpp.push_str(
                        "    galaxy::dsp_channel_selection_dma_probe_record_selected_channel_branch(ctx.channel_selection_dma_probe, selected_channel_02EF, selection_word_02EF, expected_channel_record_host_02EF);\n",
                    );
                }
                emit_dsp_call_stack_push(cpp, return_address, 4);
                return emit_static_transfer(cpp, decoded, target, layout);
            }

            if layout.loop_end_word_for_instruction(decoded).is_some() {
                let target_name = format!("call_target_{:04X}", decoded.address);
                cpp.push_str(&format!(
                    "    std::uint16_t {target_name} = {};\n",
                    cpp_u16_literal(return_address)
                ));
                cpp.push_str(&format!(
                    "    if (galaxy::dsp_condition_holds(galaxy::DspCondition::{}, ctx.sr)) {{\n",
                    condition_cpp_name(condition)
                ));
                emit_dsp_call_stack_push(cpp, return_address, 8);
                cpp.push_str(&format!(
                    "        {target_name} = {};\n",
                    cpp_u16_literal(target)
                ));
                cpp.push_str("    }\n");
                emit_dynamic_transfer(cpp, decoded, &target_name, layout, 4);
                return Ok(());
            }
            cpp.push_str(&format!(
                "    if (galaxy::dsp_condition_holds(galaxy::DspCondition::{}, ctx.sr)) {{\n",
                condition_cpp_name(condition)
            ));
            emit_dsp_call_stack_push(cpp, return_address, 8);
            emit_raw_instruction_commit(cpp, 8);
            emit_static_backedge_interrupt_checkpoint(cpp, decoded, target, 8);
            emit_dsp_goto_indented(cpp, target, 8);
            cpp.push_str("    }\n");
            emit_raw_instruction_commit(cpp, 4);
            emit_static_backedge_interrupt_checkpoint(cpp, decoded, return_address, 4);
            emit_dsp_goto(cpp, return_address);
            Ok(())
        }
        DspInstruction::CallRegister { condition, target } => {
            let Some(return_address) = next_address else {
                return bail_or_trap(
                    cpp,
                    layout,
                    decoded,
                    "call return address leaves lowered DSP program",
                );
            };
            require_decoded_static_target(
                layout,
                decoded,
                return_address,
                "call return address leaves lowered DSP program",
            )?;
            let target_expression = register_read_expression(target);
            let target_name = format!("call_target_{:04X}", decoded.address);

            if condition == DspCondition::Always {
                cpp.push_str(&format!(
                    "    const auto {target_name} = static_cast<std::uint16_t>({target_expression});\n"
                ));
                emit_dsp_call_stack_push(cpp, return_address, 4);
                emit_dynamic_transfer(cpp, decoded, &target_name, layout, 4);
                return Ok(());
            }

            if layout.loop_end_word_for_instruction(decoded).is_some() {
                let next_target_name = format!("call_next_{:04X}", decoded.address);
                cpp.push_str(&format!(
                    "    std::uint16_t {next_target_name} = {};\n",
                    cpp_u16_literal(return_address)
                ));
                cpp.push_str(&format!(
                    "    if (galaxy::dsp_condition_holds(galaxy::DspCondition::{}, ctx.sr)) {{\n",
                    condition_cpp_name(condition)
                ));
                cpp.push_str(&format!(
                    "        const auto {target_name} = static_cast<std::uint16_t>({target_expression});\n"
                ));
                emit_dsp_call_stack_push(cpp, return_address, 8);
                cpp.push_str(&format!("        {next_target_name} = {target_name};\n"));
                cpp.push_str("    }\n");
                emit_dynamic_transfer(cpp, decoded, &next_target_name, layout, 4);
                return Ok(());
            }
            cpp.push_str(&format!(
                "    if (galaxy::dsp_condition_holds(galaxy::DspCondition::{}, ctx.sr)) {{\n",
                condition_cpp_name(condition)
            ));
            cpp.push_str(&format!(
                "        const auto {target_name} = static_cast<std::uint16_t>({target_expression});\n"
            ));
            emit_dsp_call_stack_push(cpp, return_address, 8);
            emit_raw_instruction_commit(cpp, 8);
            emit_dynamic_backedge_interrupt_checkpoint(cpp, decoded, &target_name, 8);
            emit_dynamic_dsp_goto(cpp, &target_name, 8);
            cpp.push_str("    }\n");
            emit_raw_instruction_commit(cpp, 4);
            emit_static_backedge_interrupt_checkpoint(cpp, decoded, return_address, 4);
            emit_dsp_goto(cpp, return_address);
            Ok(())
        }
        DspInstruction::Return {
            interrupt: false,
            condition,
        } => {
            if condition == DspCondition::Always {
                cpp.push_str(&format!(
                    "    const auto return_target_{:04X} = galaxy::dsp_stack_pop(ctx, 0);\n",
                    decoded.address
                ));
                emit_dynamic_transfer(
                    cpp,
                    decoded,
                    &format!("return_target_{:04X}", decoded.address),
                    layout,
                    4,
                );
                return Ok(());
            }

            let Some(fallthrough) = next_address else {
                return bail_or_trap(
                    cpp,
                    layout,
                    decoded,
                    "conditional return fallthrough leaves lowered DSP program",
                );
            };
            require_decoded_static_target(
                layout,
                decoded,
                fallthrough,
                "conditional return fallthrough leaves lowered DSP program",
            )?;
            if layout.loop_end_word_for_instruction(decoded).is_some() {
                let target_name = format!("return_next_{:04X}", decoded.address);
                cpp.push_str(&format!(
                    "    std::uint16_t {target_name} = {};\n",
                    cpp_u16_literal(fallthrough)
                ));
                cpp.push_str(&format!(
                    "    if (galaxy::dsp_condition_holds(galaxy::DspCondition::{}, ctx.sr)) {{\n",
                    condition_cpp_name(condition)
                ));
                cpp.push_str(&format!(
                    "        {target_name} = galaxy::dsp_stack_pop(ctx, 0);\n",
                ));
                cpp.push_str("    }\n");
                emit_dynamic_transfer(cpp, decoded, &target_name, layout, 4);
                return Ok(());
            }
            cpp.push_str(&format!(
                "    if (galaxy::dsp_condition_holds(galaxy::DspCondition::{}, ctx.sr)) {{\n",
                condition_cpp_name(condition)
            ));
            cpp.push_str(&format!(
                "        const auto return_target_{:04X} = galaxy::dsp_stack_pop(ctx, 0);\n",
                decoded.address
            ));
            let target_name = format!("return_target_{:04X}", decoded.address);
            emit_raw_instruction_commit(cpp, 8);
            emit_dynamic_backedge_interrupt_checkpoint(cpp, decoded, &target_name, 8);
            emit_dynamic_dsp_goto(cpp, &target_name, 8);
            cpp.push_str("    }\n");
            emit_raw_instruction_commit(cpp, 4);
            emit_static_backedge_interrupt_checkpoint(cpp, decoded, fallthrough, 4);
            emit_dsp_goto(cpp, fallthrough);
            Ok(())
        }
        DspInstruction::Return {
            interrupt: true,
            condition,
        } => {
            if condition == DspCondition::Always {
                cpp.push_str("    ctx.sr = galaxy::dsp_stack_pop(ctx, 1);\n");
                cpp.push_str(&format!(
                    "    const auto return_target_{:04X} = galaxy::dsp_stack_pop(ctx, 0);\n",
                    decoded.address
                ));
                emit_dynamic_transfer(
                    cpp,
                    decoded,
                    &format!("return_target_{:04X}", decoded.address),
                    layout,
                    4,
                );
                return Ok(());
            }

            let Some(fallthrough) = next_address else {
                return bail_or_trap(
                    cpp,
                    layout,
                    decoded,
                    "conditional return fallthrough leaves lowered DSP program",
                );
            };
            require_decoded_static_target(
                layout,
                decoded,
                fallthrough,
                "conditional return fallthrough leaves lowered DSP program",
            )?;
            if layout.loop_end_word_for_instruction(decoded).is_some() {
                let target_name = format!("return_next_{:04X}", decoded.address);
                cpp.push_str(&format!(
                    "    std::uint16_t {target_name} = {};\n",
                    cpp_u16_literal(fallthrough)
                ));
                cpp.push_str(&format!(
                    "    if (galaxy::dsp_condition_holds(galaxy::DspCondition::{}, ctx.sr)) {{\n",
                    condition_cpp_name(condition)
                ));
                cpp.push_str("        ctx.sr = galaxy::dsp_stack_pop(ctx, 1);\n");
                cpp.push_str(&format!(
                    "        {target_name} = galaxy::dsp_stack_pop(ctx, 0);\n",
                ));
                cpp.push_str("    }\n");
                emit_dynamic_transfer(cpp, decoded, &target_name, layout, 4);
                return Ok(());
            }
            cpp.push_str(&format!(
                "    if (galaxy::dsp_condition_holds(galaxy::DspCondition::{}, ctx.sr)) {{\n",
                condition_cpp_name(condition)
            ));
            cpp.push_str("        ctx.sr = galaxy::dsp_stack_pop(ctx, 1);\n");
            cpp.push_str(&format!(
                "        const auto return_target_{:04X} = galaxy::dsp_stack_pop(ctx, 0);\n",
                decoded.address
            ));
            let target_name = format!("return_target_{:04X}", decoded.address);
            emit_raw_instruction_commit(cpp, 8);
            emit_dynamic_backedge_interrupt_checkpoint(cpp, decoded, &target_name, 8);
            emit_dynamic_dsp_goto(cpp, &target_name, 8);
            cpp.push_str("    }\n");
            emit_raw_instruction_commit(cpp, 4);
            emit_static_backedge_interrupt_checkpoint(cpp, decoded, fallthrough, 4);
            emit_dsp_goto(cpp, fallthrough);
            Ok(())
        }
        DspInstruction::Loop { count } => {
            emit_loop_instruction(cpp, decoded, count, next_address, skip_address, layout)
        }
        DspInstruction::LoopImmediate { count } => {
            emit_loop_immediate_instruction(cpp, decoded, count, next_address, skip_address, layout)
        }
        DspInstruction::BlockLoop { count, end_address } => {
            emit_block_loop_instruction(cpp, decoded, count, end_address, next_address, layout)
        }
        DspInstruction::BlockLoopImmediate { count, end_address } => {
            emit_block_loop_immediate_instruction(
                cpp,
                decoded,
                count,
                end_address,
                next_address,
                layout,
            )
        }
    }
}

fn emit_loop_instruction(
    cpp: &mut String,
    decoded: &DecodedDspInstruction,
    count: DspRegister,
    next_address: Option<u16>,
    skip_address: Option<u16>,
    layout: &DspProgramLayout,
) -> Result<(), DspLoweringError> {
    let Some(body_address) = next_address else {
        return bail_or_trap(
            cpp,
            layout,
            decoded,
            "loop body outside lowered DSP program",
        );
    };
    require_decoded_static_target(
        layout,
        decoded,
        body_address,
        "loop body outside lowered DSP program",
    )?;
    let Some(skip_address) = skip_address else {
        return bail_or_trap(
            cpp,
            layout,
            decoded,
            "zero-count loop skip target outside lowered DSP program",
        );
    };
    require_decoded_static_target(
        layout,
        decoded,
        skip_address,
        "zero-count loop skip target outside lowered DSP program",
    )?;
    let count_expression = register_read_expression(count);
    let count_name = format!("loop_count_{:04X}", decoded.address);

    cpp.push_str(&format!(
        "    const auto {count_name} = static_cast<std::uint16_t>({count_expression});\n"
    ));
    emit_counted_loop_transfer(
        cpp,
        decoded,
        &count_name,
        body_address,
        body_address,
        skip_address,
        layout,
    )
}

fn emit_loop_immediate_instruction(
    cpp: &mut String,
    decoded: &DecodedDspInstruction,
    count: u8,
    next_address: Option<u16>,
    skip_address: Option<u16>,
    layout: &DspProgramLayout,
) -> Result<(), DspLoweringError> {
    let Some(body_address) = next_address else {
        return bail_or_trap(
            cpp,
            layout,
            decoded,
            "loop body outside lowered DSP program",
        );
    };
    require_decoded_static_target(
        layout,
        decoded,
        body_address,
        "loop body outside lowered DSP program",
    )?;
    let Some(skip_address) = skip_address else {
        return bail_or_trap(
            cpp,
            layout,
            decoded,
            "zero-count loop skip target outside lowered DSP program",
        );
    };
    require_decoded_static_target(
        layout,
        decoded,
        skip_address,
        "zero-count loop skip target outside lowered DSP program",
    )?;

    emit_counted_loop_transfer(
        cpp,
        decoded,
        &cpp_u16_literal(u16::from(count)),
        body_address,
        body_address,
        skip_address,
        layout,
    )
}

/// A block loop whose end address was never decoded as an instruction is
/// speculative gap-recovered code or data; emit a runtime hard-fail rather than
/// lowering a loop over an unknown boundary.
fn emit_unresolved_block_loop(
    cpp: &mut String,
    decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    let pc_literal = cpp_u16_literal(decoded.address);
    emit_dsp_hard_trap(
        cpp,
        &pc_literal,
        "block loop end outside lowered DSP program",
        4,
    );
    Ok(())
}

fn emit_block_loop_instruction(
    cpp: &mut String,
    decoded: &DecodedDspInstruction,
    count: DspRegister,
    end_address: u16,
    next_address: Option<u16>,
    layout: &DspProgramLayout,
) -> Result<(), DspLoweringError> {
    if !layout.contains_instruction_word(end_address) {
        return emit_unresolved_block_loop(cpp, decoded);
    }
    let Some(body_address) = next_address else {
        return bail_or_trap(
            cpp,
            layout,
            decoded,
            "block loop body outside lowered DSP program",
        );
    };
    require_decoded_static_target(
        layout,
        decoded,
        body_address,
        "block loop body outside lowered DSP program",
    )?;
    let Some(skip_address) = layout.instruction_after_word(end_address) else {
        return bail_or_trap(
            cpp,
            layout,
            decoded,
            "zero-count block loop skip target outside lowered DSP program",
        );
    };
    require_decoded_static_target(
        layout,
        decoded,
        skip_address,
        "zero-count block loop skip target outside lowered DSP program",
    )?;
    let count_expression = register_read_expression(count);
    let count_name = format!("block_loop_count_{:04X}", decoded.address);

    cpp.push_str(&format!(
        "    const auto {count_name} = static_cast<std::uint16_t>({count_expression});\n"
    ));
    emit_counted_loop_transfer(
        cpp,
        decoded,
        &count_name,
        body_address,
        end_address,
        skip_address,
        layout,
    )
}

fn emit_block_loop_immediate_instruction(
    cpp: &mut String,
    decoded: &DecodedDspInstruction,
    count: u8,
    end_address: u16,
    next_address: Option<u16>,
    layout: &DspProgramLayout,
) -> Result<(), DspLoweringError> {
    if !layout.contains_instruction_word(end_address) {
        return emit_unresolved_block_loop(cpp, decoded);
    }
    let Some(body_address) = next_address else {
        return bail_or_trap(
            cpp,
            layout,
            decoded,
            "block loop body outside lowered DSP program",
        );
    };
    require_decoded_static_target(
        layout,
        decoded,
        body_address,
        "block loop body outside lowered DSP program",
    )?;
    let Some(skip_address) = layout.instruction_after_word(end_address) else {
        return bail_or_trap(
            cpp,
            layout,
            decoded,
            "zero-count block loop skip target outside lowered DSP program",
        );
    };
    require_decoded_static_target(
        layout,
        decoded,
        skip_address,
        "zero-count block loop skip target outside lowered DSP program",
    )?;

    emit_counted_loop_transfer(
        cpp,
        decoded,
        &cpp_u16_literal(u16::from(count)),
        body_address,
        end_address,
        skip_address,
        layout,
    )
}

fn emit_counted_loop_transfer(
    cpp: &mut String,
    decoded: &DecodedDspInstruction,
    count_expression: &str,
    body_address: u16,
    end_address: u16,
    zero_count_target: u16,
    layout: &DspProgramLayout,
) -> Result<(), DspLoweringError> {
    if layout.loop_end_word_for_instruction(decoded).is_some() {
        let target_name = format!("loop_next_{:04X}", decoded.address);
        cpp.push_str(&format!(
            "    std::uint16_t {target_name} = {};\n",
            cpp_u16_literal(zero_count_target)
        ));
        cpp.push_str(&format!("    if ({count_expression} != 0u) {{\n"));
        emit_loop_stack_setup(cpp, count_expression, body_address, end_address, 8);
        cpp.push_str(&format!(
            "        {target_name} = {};\n",
            cpp_u16_literal(body_address)
        ));
        cpp.push_str("    }\n");
        emit_dynamic_transfer(cpp, decoded, &target_name, layout, 4);
        return Ok(());
    }

    cpp.push_str(&format!("    if ({count_expression} != 0u) {{\n"));
    emit_loop_stack_setup(cpp, count_expression, body_address, end_address, 8);
    emit_raw_instruction_commit(cpp, 8);
    emit_dsp_goto_indented(cpp, body_address, 8);
    cpp.push_str("    }\n");
    emit_raw_instruction_commit(cpp, 4);
    emit_dsp_goto(cpp, zero_count_target);
    Ok(())
}

fn emit_loop_stack_setup(
    cpp: &mut String,
    count_expression: &str,
    body_address: u16,
    end_address: u16,
    indent_spaces: usize,
) {
    emit_indent(cpp, indent_spaces);
    cpp.push_str(&format!(
        "galaxy::dsp_stack_push(ctx, 0, {});\n",
        cpp_u16_literal(body_address)
    ));
    emit_indent(cpp, indent_spaces);
    cpp.push_str(&format!(
        "galaxy::dsp_stack_push(ctx, 2, {});\n",
        cpp_u16_literal(end_address)
    ));
    emit_indent(cpp, indent_spaces);
    cpp.push_str(&format!(
        "galaxy::dsp_stack_push(ctx, 3, static_cast<std::uint16_t>({count_expression}));\n"
    ));
}

fn emit_parallel_primary(
    cpp: &mut String,
    primary: DspParallelPrimary,
    decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    match primary {
        DspParallelPrimary::Mode(operation) => {
            emit_mode_operation(cpp, operation);
            Ok(())
        }
        DspParallelPrimary::Logic(operation) => emit_logic_operation(cpp, operation, decoded),
        DspParallelPrimary::Accumulator(operation) => {
            emit_accumulator_operation(cpp, operation, decoded)
        }
        DspParallelPrimary::Product(operation) => emit_product_operation(cpp, operation, decoded),
    }
}

fn emit_parallel_register_move(
    cpp: &mut String,
    target: DspRegister,
    source: DspRegister,
    decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    let source_expression = register_read_expression(source);
    emit_register_write(cpp, target, &source_expression, decoded)
}

fn emit_parallel_register_move_with_primary(
    cpp: &mut String,
    primary: DspParallelPrimary,
    target: DspRegister,
    source: DspRegister,
    decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    let source_expression = register_read_expression(source);
    let value_name = parallel_value_name(decoded);
    cpp.push_str(&format!(
        "    const auto {value_name} = {source_expression};\n"
    ));
    emit_parallel_primary(cpp, primary, decoded)?;
    emit_register_write(cpp, target, &value_name, decoded)
}

fn emit_parallel_store_single(
    cpp: &mut String,
    address: DspAddressRegister,
    source: DspRegister,
    update: DspAddressUpdate,
    _decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    let source_expression = register_read_expression(source);
    let address_index = address_register_index(address);
    cpp.push_str(&format!(
        "    galaxy::dsp_data_write(ctx, ctx.ar[{address_index}], static_cast<std::uint16_t>({source_expression}));\n"
    ));
    emit_address_update(cpp, address, update);
    Ok(())
}

fn emit_parallel_store_single_with_primary(
    cpp: &mut String,
    primary: DspParallelPrimary,
    address: DspAddressRegister,
    source: DspRegister,
    update: DspAddressUpdate,
    decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    let source_expression = register_read_expression(source);
    let value_name = parallel_value_name(decoded);
    cpp.push_str(&format!(
        "    const auto {value_name} = static_cast<std::uint16_t>({source_expression});\n"
    ));
    let address_index = address_register_index(address);
    cpp.push_str(&format!(
        "    galaxy::dsp_data_write(ctx, ctx.ar[{address_index}], {value_name});\n"
    ));
    emit_parallel_primary(cpp, primary, decoded)?;
    emit_address_update(cpp, address, update);
    Ok(())
}

fn emit_parallel_load_single(
    cpp: &mut String,
    target: DspRegister,
    address: DspAddressRegister,
    update: DspAddressUpdate,
    decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    let address_index = address_register_index(address);
    emit_register_write(
        cpp,
        target,
        &format!("galaxy::dsp_data_read(ctx, ctx.ar[{address_index}])"),
        decoded,
    )?;
    emit_address_update(cpp, address, update);
    Ok(())
}

fn emit_parallel_load_single_with_primary(
    cpp: &mut String,
    primary: DspParallelPrimary,
    target: DspRegister,
    address: DspAddressRegister,
    update: DspAddressUpdate,
    decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    let value_name = parallel_value_name(decoded);
    let address_index = address_register_index(address);
    // The extension chooses its writeback parts before the primary changes SR.
    // Keep the post-primary accumulator value, but use the old SR40 mode when
    // deciding whether the mid load also overwrites high/low.
    let mid_target = match target {
        DspRegister::Accumulator {
            register,
            part: DspAccumulatorPart::Mid,
        } => Some(accumulator_index(register)),
        _ => None,
    };
    let status_name = parallel_named_value(decoded, "load_status");
    if mid_target.is_some() {
        cpp.push_str(&format!("    const auto {status_name} = ctx.sr;\n"));
    }
    cpp.push_str(&format!(
        "    const auto {value_name} = galaxy::dsp_data_read(ctx, ctx.ar[{address_index}]);\n"
    ));
    emit_parallel_primary(cpp, primary, decoded)?;
    if let Some(index) = mid_target {
        cpp.push_str(&format!(
            "    ctx.ac[{index}] = galaxy::dsp_write_accumulator_mid(ctx.ac[{index}], static_cast<std::uint16_t>({value_name}), {status_name});\n"
        ));
    } else {
        emit_register_write(cpp, target, &value_name, decoded)?;
    }
    emit_address_update(cpp, address, update);
    Ok(())
}

fn parallel_value_name(decoded: &DecodedDspInstruction) -> String {
    parallel_named_value(decoded, "value")
}

fn parallel_named_value(decoded: &DecodedDspInstruction, suffix: &str) -> String {
    format!("parallel_{:04X}_{suffix}", decoded.address)
}

fn emit_parallel_accumulator_mid_load_store(
    cpp: &mut String,
    order: DspParallelLoadStoreOrder,
    target: DspRegister,
    accumulator: DspAccumulator,
    ar0_update: DspAddressUpdate,
    ar3_update: DspAddressUpdate,
    decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    let accumulator_mid = DspRegister::Accumulator {
        register: accumulator,
        part: DspAccumulatorPart::Mid,
    };
    let (load_address, store_address) = match order {
        DspParallelLoadStoreOrder::LoadThenStore => {
            (DspAddressRegister::Ar0, DspAddressRegister::Ar3)
        }
        DspParallelLoadStoreOrder::StoreThenLoad => {
            (DspAddressRegister::Ar3, DspAddressRegister::Ar0)
        }
    };

    // The hardware-visible behavior stores before loading when the two address
    // registers alias, even for the LS mnemonic family.
    emit_dynamic_dram_store(cpp, store_address, accumulator_mid, decoded)?;
    emit_dynamic_dram_load(cpp, target, load_address, decoded)?;
    emit_address_update(cpp, DspAddressRegister::Ar3, ar3_update);
    emit_address_update(cpp, DspAddressRegister::Ar0, ar0_update);
    Ok(())
}

fn emit_parallel_accumulator_mid_load_store_with_primary(
    cpp: &mut String,
    primary: DspParallelPrimary,
    order: DspParallelLoadStoreOrder,
    target: DspRegister,
    accumulator: DspAccumulator,
    updates: (DspAddressUpdate, DspAddressUpdate),
    decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    let (ar0_update, ar3_update) = updates;
    let accumulator_mid = DspRegister::Accumulator {
        register: accumulator,
        part: DspAccumulatorPart::Mid,
    };
    let (load_address, store_address) = match order {
        DspParallelLoadStoreOrder::LoadThenStore => {
            (DspAddressRegister::Ar0, DspAddressRegister::Ar3)
        }
        DspParallelLoadStoreOrder::StoreThenLoad => {
            (DspAddressRegister::Ar3, DspAddressRegister::Ar0)
        }
    };
    let store_expression = register_read_expression(accumulator_mid);
    let store_name = parallel_named_value(decoded, "store_value");
    let load_name = parallel_named_value(decoded, "load_value");
    let store_index = address_register_index(store_address);
    let load_index = address_register_index(load_address);
    cpp.push_str(&format!(
        "    const auto {store_name} = static_cast<std::uint16_t>({store_expression});\n"
    ));
    cpp.push_str(&format!(
        "    galaxy::dsp_data_write(ctx, ctx.ar[{store_index}], {store_name});\n"
    ));
    cpp.push_str(&format!(
        "    const auto {load_name} = galaxy::dsp_data_read(ctx, ctx.ar[{load_index}]);\n"
    ));
    emit_parallel_primary(cpp, primary, decoded)?;
    emit_register_write(cpp, target, &load_name, decoded)?;
    emit_address_update(cpp, DspAddressRegister::Ar3, ar3_update);
    emit_address_update(cpp, DspAddressRegister::Ar0, ar0_update);
    Ok(())
}

fn emit_parallel_load_ax_pair(
    cpp: &mut String,
    ax: DspAxRegister,
    address: DspAddressRegister,
    ar_update: DspAddressUpdate,
    ar3_update: DspAddressUpdate,
    decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    let (primary_value, secondary_value) = emit_parallel_dual_load_values(cpp, address, decoded);
    emit_register_write(
        cpp,
        DspRegister::Ax {
            register: ax,
            half: DspRegisterHalf::High,
        },
        &primary_value,
        decoded,
    )?;
    emit_register_write(
        cpp,
        DspRegister::Ax {
            register: ax,
            half: DspRegisterHalf::Low,
        },
        &secondary_value,
        decoded,
    )?;
    emit_address_update(cpp, address, ar_update);
    emit_address_update(cpp, DspAddressRegister::Ar3, ar3_update);
    Ok(())
}

fn emit_parallel_load_ax_pair_with_primary(
    cpp: &mut String,
    primary: DspParallelPrimary,
    ax: DspAxRegister,
    address: DspAddressRegister,
    ar_update: DspAddressUpdate,
    ar3_update: DspAddressUpdate,
    decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    let (primary_value, secondary_value) = emit_parallel_dual_load_values(cpp, address, decoded);
    emit_parallel_primary(cpp, primary, decoded)?;
    emit_register_write(
        cpp,
        DspRegister::Ax {
            register: ax,
            half: DspRegisterHalf::High,
        },
        &primary_value,
        decoded,
    )?;
    emit_register_write(
        cpp,
        DspRegister::Ax {
            register: ax,
            half: DspRegisterHalf::Low,
        },
        &secondary_value,
        decoded,
    )?;
    emit_address_update(cpp, address, ar_update);
    emit_address_update(cpp, DspAddressRegister::Ar3, ar3_update);
    Ok(())
}

fn emit_parallel_load_ax_parts(
    cpp: &mut String,
    ax0_half: DspRegisterHalf,
    ax1_half: DspRegisterHalf,
    address: DspAddressRegister,
    ar_update: DspAddressUpdate,
    ar3_update: DspAddressUpdate,
    decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    let (primary_value, secondary_value) = emit_parallel_dual_load_values(cpp, address, decoded);
    emit_register_write(
        cpp,
        DspRegister::Ax {
            register: DspAxRegister::Ax0,
            half: ax0_half,
        },
        &primary_value,
        decoded,
    )?;
    emit_register_write(
        cpp,
        DspRegister::Ax {
            register: DspAxRegister::Ax1,
            half: ax1_half,
        },
        &secondary_value,
        decoded,
    )?;
    emit_address_update(cpp, address, ar_update);
    emit_address_update(cpp, DspAddressRegister::Ar3, ar3_update);
    Ok(())
}

fn emit_parallel_load_ax_parts_with_primary(
    cpp: &mut String,
    primary: DspParallelPrimary,
    halves: (DspRegisterHalf, DspRegisterHalf),
    address: DspAddressRegister,
    ar_update: DspAddressUpdate,
    ar3_update: DspAddressUpdate,
    decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    let (ax0_half, ax1_half) = halves;
    let (primary_value, secondary_value) = emit_parallel_dual_load_values(cpp, address, decoded);
    emit_parallel_primary(cpp, primary, decoded)?;
    emit_register_write(
        cpp,
        DspRegister::Ax {
            register: DspAxRegister::Ax0,
            half: ax0_half,
        },
        &primary_value,
        decoded,
    )?;
    emit_register_write(
        cpp,
        DspRegister::Ax {
            register: DspAxRegister::Ax1,
            half: ax1_half,
        },
        &secondary_value,
        decoded,
    )?;
    emit_address_update(cpp, address, ar_update);
    emit_address_update(cpp, DspAddressRegister::Ar3, ar3_update);
    Ok(())
}

fn emit_parallel_dual_load_values(
    cpp: &mut String,
    address: DspAddressRegister,
    decoded: &DecodedDspInstruction,
) -> (String, String) {
    let address_index = address_register_index(address);
    let primary_value = format!("parallel_load_{:04X}_primary", decoded.address);
    let secondary_value = format!("parallel_load_{:04X}_secondary", decoded.address);
    cpp.push_str(&format!(
        "    const auto {primary_value} = galaxy::dsp_data_read(ctx, ctx.ar[{address_index}]);\n"
    ));
    cpp.push_str(&format!(
        "    const auto {secondary_value} = ((ctx.ar[{address_index}] & 0xFC00u) == (ctx.ar[3] & 0xFC00u)) ? {primary_value} : galaxy::dsp_data_read(ctx, ctx.ar[3]);\n"
    ));
    (primary_value, secondary_value)
}

fn emit_accumulator_operation(
    cpp: &mut String,
    operation: DspAccumulatorOperation,
    decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    match operation {
        DspAccumulatorOperation::Add { target, operand } => {
            let operand_expression = accumulator_operand_expression(operand);
            emit_binary_accumulator_helper_call(
                cpp,
                decoded,
                "dsp_accumulator_add",
                target,
                &operand_expression,
            );
            Ok(())
        }
        DspAccumulatorOperation::Subtract { target, operand } => {
            let operand_expression = accumulator_operand_expression(operand);
            emit_binary_accumulator_helper_call(
                cpp,
                decoded,
                "dsp_accumulator_subtract",
                target,
                &operand_expression,
            );
            Ok(())
        }
        DspAccumulatorOperation::Negate { target } => {
            let target_index = accumulator_index(target);
            let result_name = format!("accumulator_{:04X}", decoded.address);
            cpp.push_str(&format!(
                "    const auto {result_name} = galaxy::dsp_accumulator_negate(ctx.ac[{target_index}], ctx.sr);\n"
            ));
            emit_accumulator_result_commit(cpp, target_index, &result_name);
            Ok(())
        }
        DspAccumulatorOperation::Clear { target } => {
            emit_set_accumulator_operation(cpp, decoded, target, "0");
            Ok(())
        }
        DspAccumulatorOperation::ClearLow { target } => {
            emit_unary_accumulator_helper_call(cpp, decoded, "dsp_accumulator_clear_low", target);
            Ok(())
        }
        DspAccumulatorOperation::Test { target } => {
            let target_index = accumulator_index(target);
            let result_name = format!("accumulator_{:04X}", decoded.address);
            cpp.push_str(&format!(
                "    const auto {result_name} = galaxy::dsp_accumulator_set_value(ctx.ac[{target_index}], ctx.sr);\n"
            ));
            emit_accumulator_status_commit(cpp, &result_name);
            Ok(())
        }
        DspAccumulatorOperation::TestAxHigh { ax } => {
            let ax_index = ax_register_index(ax);
            cpp.push_str(&format!(
                "    ctx.sr = galaxy::dsp_status_16(static_cast<std::int16_t>(ctx.ax[{ax_index}][1]), false, false, false, ctx.sr);\n"
            ));
            Ok(())
        }
        DspAccumulatorOperation::CompareAccumulators => {
            let result_name = format!("accumulator_{:04X}", decoded.address);
            cpp.push_str(&format!(
                "    const auto {result_name} = galaxy::dsp_accumulator_subtract(ctx.ac[0], ctx.ac[1], ctx.sr);\n"
            ));
            emit_accumulator_status_commit(cpp, &result_name);
            Ok(())
        }
        DspAccumulatorOperation::CompareWithAxHigh { accumulator, ax } => {
            let accumulator_index = accumulator_index(accumulator);
            let ax_index = ax_register_index(ax);
            let result_name = format!("accumulator_{:04X}", decoded.address);
            cpp.push_str(&format!(
                "    const auto {result_name} = galaxy::dsp_accumulator_subtract(ctx.ac[{accumulator_index}], galaxy::dsp_accumulator_from_signed_mid(ctx.ax[{ax_index}][1]), ctx.sr);\n"
            ));
            emit_accumulator_status_commit(cpp, &result_name);
            Ok(())
        }
        DspAccumulatorOperation::Absolute { target } => {
            emit_unary_accumulator_helper_call(cpp, decoded, "dsp_accumulator_absolute", target);
            Ok(())
        }
        DspAccumulatorOperation::Move { target, source } => {
            let source_expression = accumulator_move_source_expression(source);
            emit_set_accumulator_operation(cpp, decoded, target, &source_expression);
            Ok(())
        }
        DspAccumulatorOperation::ShiftImmediate {
            target,
            kind,
            amount,
        } => {
            emit_accumulator_shift_operation(
                cpp,
                decoded,
                target,
                kind,
                accumulator_immediate_shift_amount(kind, amount),
            );
            Ok(())
        }
        DspAccumulatorOperation::Shift16 { target, kind } => {
            emit_accumulator_shift_operation(cpp, decoded, target, kind, 16);
            Ok(())
        }
        DspAccumulatorOperation::ShiftByAccumulatorMid { kind } => {
            let arithmetic_right = match kind {
                DspAccumulatorShiftKind::LogicalRight => "false",
                DspAccumulatorShiftKind::ArithmeticRight => "true",
                DspAccumulatorShiftKind::LogicalLeft | DspAccumulatorShiftKind::ArithmeticLeft => {
                    unreachable!("decoder only emits right-shift accumulator-mid variable forms");
                }
            };
            let result_name = format!("accumulator_{:04X}", decoded.address);
            cpp.push_str(&format!(
                "    const auto {result_name} = galaxy::dsp_accumulator_shift_by_signed_count(ctx.ac[0], {arithmetic_right}, galaxy::dsp_read_accumulator_mid_raw(ctx.ac[1]), ctx.sr);\n"
            ));
            emit_accumulator_result_commit(cpp, 0, &result_name);
            Ok(())
        }
        DspAccumulatorOperation::AddProductAndAxHighClearLow { target, ax } => {
            let target_index = accumulator_index(target);
            let ax_index = ax_register_index(ax);
            let result_name = format!("accumulator_{:04X}", decoded.address);
            cpp.push_str(&format!(
                "    const auto {result_name} = galaxy::dsp_accumulator_add_rounded_product_and_ax_clear_low(ctx.prod, ctx.ax[{ax_index}][0], ctx.ax[{ax_index}][1], ctx.sr);\n"
            ));
            emit_accumulator_result_commit(cpp, target_index, &result_name);
            Ok(())
        }
    }
}

fn emit_accumulator_shift_operation(
    cpp: &mut String,
    decoded: &DecodedDspInstruction,
    target: DspAccumulator,
    kind: DspAccumulatorShiftKind,
    amount: u8,
) {
    let target_index = accumulator_index(target);
    let result_name = format!("accumulator_{:04X}", decoded.address);
    let kind_name = accumulator_shift_kind_cpp_name(kind);
    cpp.push_str(&format!(
        "    const auto {result_name} = galaxy::dsp_accumulator_shift(ctx.ac[{target_index}], galaxy::DspAccumulatorShiftKind::{kind_name}, {amount}u, ctx.sr);\n"
    ));
    emit_accumulator_result_commit(cpp, target_index, &result_name);
}

fn emit_binary_accumulator_helper_call(
    cpp: &mut String,
    decoded: &DecodedDspInstruction,
    helper_name: &str,
    target: DspAccumulator,
    operand_expression: &str,
) {
    let target_index = accumulator_index(target);
    let result_name = format!("accumulator_{:04X}", decoded.address);
    cpp.push_str(&format!(
        "    const auto {result_name} = galaxy::{helper_name}(ctx.ac[{target_index}], {operand_expression}, ctx.sr);\n"
    ));
    emit_accumulator_result_commit(cpp, target_index, &result_name);
}

fn emit_unary_accumulator_helper_call(
    cpp: &mut String,
    decoded: &DecodedDspInstruction,
    helper_name: &str,
    target: DspAccumulator,
) {
    let target_index = accumulator_index(target);
    let result_name = format!("accumulator_{:04X}", decoded.address);
    cpp.push_str(&format!(
        "    const auto {result_name} = galaxy::{helper_name}(ctx.ac[{target_index}], ctx.sr);\n"
    ));
    emit_accumulator_result_commit(cpp, target_index, &result_name);
}

fn emit_set_accumulator_operation(
    cpp: &mut String,
    decoded: &DecodedDspInstruction,
    target: DspAccumulator,
    value_expression: &str,
) {
    let target_index = accumulator_index(target);
    let result_name = format!("accumulator_{:04X}", decoded.address);
    cpp.push_str(&format!(
        "    const auto {result_name} = galaxy::dsp_accumulator_set_value({value_expression}, ctx.sr);\n"
    ));
    emit_accumulator_result_commit(cpp, target_index, &result_name);
}

fn emit_accumulator_result_commit(cpp: &mut String, target_index: usize, result_name: &str) {
    cpp.push_str(&format!(
        "    ctx.ac[{target_index}] = {result_name}.value;\n"
    ));
    cpp.push_str(&format!("    ctx.sr = {result_name}.status;\n"));
}

fn emit_accumulator_status_commit(cpp: &mut String, result_name: &str) {
    cpp.push_str(&format!("    ctx.sr = {result_name}.status;\n"));
}

fn accumulator_operand_expression(operand: DspAccumulatorOperand) -> String {
    match operand {
        DspAccumulatorOperand::Accumulator(accumulator) => {
            format!("ctx.ac[{}]", accumulator_index(accumulator))
        }
        DspAccumulatorOperand::One => "1".to_string(),
        DspAccumulatorOperand::MidUnit => "0x00010000ll".to_string(),
        DspAccumulatorOperand::Product => "galaxy::dsp_product_value(ctx.prod)".to_string(),
        DspAccumulatorOperand::Ax(ax) => accumulator_ax_expression(ax),
        DspAccumulatorOperand::AxPartShifted(part) => accumulator_ax_part_shifted_expression(part),
        DspAccumulatorOperand::AxLowUnsigned(ax) => {
            format!(
                "static_cast<std::int64_t>(ctx.ax[{}][0])",
                ax_register_index(ax)
            )
        }
    }
}

fn accumulator_move_source_expression(source: DspAccumulatorMoveSource) -> String {
    match source {
        DspAccumulatorMoveSource::Accumulator(accumulator) => {
            format!("ctx.ac[{}]", accumulator_index(accumulator))
        }
        DspAccumulatorMoveSource::Ax(ax) => accumulator_ax_expression(ax),
        DspAccumulatorMoveSource::AxPartShifted(part) => {
            accumulator_ax_part_shifted_expression(part)
        }
    }
}

fn accumulator_ax_expression(ax: DspAxRegister) -> String {
    let ax_index = ax_register_index(ax);
    format!("galaxy::dsp_accumulator_from_ax_pair(ctx.ax[{ax_index}][0], ctx.ax[{ax_index}][1])")
}

fn accumulator_ax_part_shifted_expression(part: DspAxPart) -> String {
    format!(
        "galaxy::dsp_accumulator_from_signed_mid(ctx.ax[{}][{}])",
        ax_register_index(part.register),
        register_half_index(part.half)
    )
}

const fn accumulator_immediate_shift_amount(kind: DspAccumulatorShiftKind, amount: u8) -> u8 {
    match kind {
        DspAccumulatorShiftKind::LogicalRight | DspAccumulatorShiftKind::ArithmeticRight => {
            if amount == 0 {
                0
            } else {
                64 - amount
            }
        }
        DspAccumulatorShiftKind::LogicalLeft | DspAccumulatorShiftKind::ArithmeticLeft => amount,
    }
}

const fn accumulator_shift_kind_cpp_name(kind: DspAccumulatorShiftKind) -> &'static str {
    match kind {
        DspAccumulatorShiftKind::LogicalLeft => "LogicalLeft",
        DspAccumulatorShiftKind::LogicalRight => "LogicalRight",
        DspAccumulatorShiftKind::ArithmeticLeft => "ArithmeticLeft",
        DspAccumulatorShiftKind::ArithmeticRight => "ArithmeticRight",
    }
}

fn emit_logic_operation(
    cpp: &mut String,
    operation: DspLogicOperation,
    decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    if let DspLogicOperation::ShiftByRegister {
        target,
        kind,
        operand,
    } = operation
    {
        let arithmetic_right = match kind {
            DspAccumulatorShiftKind::LogicalRight => "false",
            DspAccumulatorShiftKind::ArithmeticRight => "true",
            DspAccumulatorShiftKind::LogicalLeft | DspAccumulatorShiftKind::ArithmeticLeft => {
                unreachable!("decoder only emits right-shift register-counted logic forms");
            }
        };
        let target_index = accumulator_index(target);
        let count_expression = logic_shift_count_expression(operand);
        let result_name = format!("logic_{:04X}", decoded.address);
        cpp.push_str(&format!(
            "    const auto {result_name} = galaxy::dsp_accumulator_shift_by_register_count(ctx.ac[{target_index}], {arithmetic_right}, {count_expression}, ctx.sr);\n"
        ));
        emit_accumulator_result_commit(cpp, target_index, &result_name);
        return Ok(());
    }

    let target = match operation {
        DspLogicOperation::Xor { target, .. }
        | DspLogicOperation::And { target, .. }
        | DspLogicOperation::Or { target, .. }
        | DspLogicOperation::Not { target } => target,
        DspLogicOperation::ShiftByRegister { .. } => unreachable!(),
    };
    let target_index = accumulator_index(target);
    let result_name = format!("logic_{:04X}", decoded.address);

    match operation {
        DspLogicOperation::Xor { operand, .. } => emit_logic_helper_call(
            cpp,
            &result_name,
            "dsp_accumulator_mid_xor",
            target_index,
            Some(logic_operand_expression(operand)),
        ),
        DspLogicOperation::And { operand, .. } => emit_logic_helper_call(
            cpp,
            &result_name,
            "dsp_accumulator_mid_and",
            target_index,
            Some(logic_operand_expression(operand)),
        ),
        DspLogicOperation::Or { operand, .. } => emit_logic_helper_call(
            cpp,
            &result_name,
            "dsp_accumulator_mid_or",
            target_index,
            Some(logic_operand_expression(operand)),
        ),
        DspLogicOperation::Not { .. } => emit_logic_helper_call(
            cpp,
            &result_name,
            "dsp_accumulator_mid_not",
            target_index,
            None,
        ),
        DspLogicOperation::ShiftByRegister { .. } => unreachable!(),
    }
    emit_logic_result_commit(cpp, target_index, &result_name);
    Ok(())
}

fn emit_product_operation(
    cpp: &mut String,
    operation: DspProductOperation,
    decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    match operation {
        DspProductOperation::Clear => {
            cpp.push_str("    ctx.prod = galaxy::dsp_cleared_product();\n");
            Ok(())
        }
        DspProductOperation::Test => {
            let result_name = format!("product_{:04X}", decoded.address);
            cpp.push_str(&format!(
                "    const auto {result_name} = galaxy::dsp_accumulator_set_value(galaxy::dsp_product_value(ctx.prod), ctx.sr);\n"
            ));
            emit_accumulator_status_commit(cpp, &result_name);
            Ok(())
        }
        DspProductOperation::MoveToAccumulator { target, mode } => {
            let value_expression = product_move_value_expression(mode);
            emit_set_accumulator_operation(cpp, decoded, target, &value_expression);
            Ok(())
        }
        DspProductOperation::MultiplyAxHighSquared => {
            emit_product_multiply(
                cpp,
                "ctx.ax[0][1]",
                "ctx.ax[0][1]",
                "galaxy::DspMultiplyOperandMode::Signed",
            );
            Ok(())
        }
        DspProductOperation::MultiplyAxPair { ax } => {
            emit_product_multiply(
                cpp,
                &ax_pair_low_expression(ax),
                &ax_pair_high_expression(ax),
                "galaxy::DspMultiplyOperandMode::Signed",
            );
            Ok(())
        }
        DspProductOperation::MultiplyAxPairWithAccumulator { ax, target, action } => {
            emit_product_multiply_with_accumulator(
                cpp,
                decoded,
                target,
                action,
                &product_multiply_expression(
                    &ax_pair_low_expression(ax),
                    &ax_pair_high_expression(ax),
                    "galaxy::DspMultiplyOperandMode::Signed",
                ),
            );
            Ok(())
        }
        DspProductOperation::MultiplyCross { left, right } => {
            let operands = multiply_operands_for_ax_parts(left, right);
            emit_product_multiply(cpp, &operands.left, &operands.right, operands.mode);
            Ok(())
        }
        DspProductOperation::MultiplyCrossWithAccumulator {
            left,
            right,
            target,
            action,
        } => {
            let operands = multiply_operands_for_ax_parts(left, right);
            emit_product_multiply_with_accumulator(
                cpp,
                decoded,
                target,
                action,
                &product_multiply_expression(&operands.left, &operands.right, operands.mode),
            );
            Ok(())
        }
        DspProductOperation::MultiplyAccumulatorMidByAxHigh { accumulator, ax } => {
            emit_product_multiply(
                cpp,
                &accumulator_mid_raw_expression(accumulator),
                &ax_pair_high_expression(ax),
                "galaxy::DspMultiplyOperandMode::Signed",
            );
            Ok(())
        }
        DspProductOperation::MultiplyAccumulatorMidByAxHighWithAccumulator {
            accumulator,
            ax,
            target,
            action,
        } => {
            emit_product_multiply_with_accumulator(
                cpp,
                decoded,
                target,
                action,
                &product_multiply_expression(
                    &accumulator_mid_raw_expression(accumulator),
                    &ax_pair_high_expression(ax),
                    "galaxy::DspMultiplyOperandMode::Signed",
                ),
            );
            Ok(())
        }
        DspProductOperation::AddCrossToProduct { left, right } => {
            emit_product_cross_accumulate(cpp, "dsp_product_multiply_add", left, right);
            Ok(())
        }
        DspProductOperation::SubtractCrossFromProduct { left, right } => {
            emit_product_cross_accumulate(cpp, "dsp_product_multiply_subtract", left, right);
            Ok(())
        }
        DspProductOperation::AddAccumulatorMidByAxHighToProduct { accumulator, ax } => {
            emit_product_accumulate(
                cpp,
                "dsp_product_multiply_add",
                &accumulator_mid_raw_expression(accumulator),
                &ax_pair_high_expression(ax),
                "galaxy::DspMultiplyOperandMode::Signed",
            );
            Ok(())
        }
        DspProductOperation::SubtractAccumulatorMidByAxHighFromProduct { accumulator, ax } => {
            emit_product_accumulate(
                cpp,
                "dsp_product_multiply_subtract",
                &accumulator_mid_raw_expression(accumulator),
                &ax_pair_high_expression(ax),
                "galaxy::DspMultiplyOperandMode::Signed",
            );
            Ok(())
        }
        DspProductOperation::AddAxPairToProduct { ax } => {
            emit_product_accumulate(
                cpp,
                "dsp_product_multiply_add",
                &ax_pair_low_expression(ax),
                &ax_pair_high_expression(ax),
                "galaxy::DspMultiplyOperandMode::Signed",
            );
            Ok(())
        }
        DspProductOperation::SubtractAxPairFromProduct { ax } => {
            emit_product_accumulate(
                cpp,
                "dsp_product_multiply_subtract",
                &ax_pair_low_expression(ax),
                &ax_pair_high_expression(ax),
                "galaxy::DspMultiplyOperandMode::Signed",
            );
            Ok(())
        }
    }
}

fn product_move_value_expression(mode: DspProductMoveMode) -> String {
    match mode {
        DspProductMoveMode::Raw => "galaxy::dsp_product_value(ctx.prod)".to_string(),
        DspProductMoveMode::Negated => "-galaxy::dsp_product_value(ctx.prod)".to_string(),
        DspProductMoveMode::HighMidClearLow => "galaxy::dsp_round_product(ctx.prod)".to_string(),
    }
}

fn emit_product_cross_accumulate(
    cpp: &mut String,
    helper_name: &str,
    left: DspAxPart,
    right: DspAxPart,
) {
    emit_product_accumulate(
        cpp,
        helper_name,
        &ax_part_expression(left),
        &ax_part_expression(right),
        "galaxy::DspMultiplyOperandMode::Signed",
    );
}

fn emit_product_multiply(
    cpp: &mut String,
    left_expression: &str,
    right_expression: &str,
    mode: &str,
) {
    cpp.push_str(&format!(
        "    ctx.prod = {};\n",
        product_multiply_expression(left_expression, right_expression, mode)
    ));
}

fn emit_product_accumulate(
    cpp: &mut String,
    helper_name: &str,
    left_expression: &str,
    right_expression: &str,
    mode: &str,
) {
    cpp.push_str(&format!(
        "    ctx.prod = galaxy::{helper_name}(ctx.prod, {left_expression}, {right_expression}, {mode}, ctx.sr);\n"
    ));
}

fn emit_product_multiply_with_accumulator(
    cpp: &mut String,
    decoded: &DecodedDspInstruction,
    target: DspAccumulator,
    action: DspMultiplyAccumulatorAction,
    product_expression: &str,
) {
    let target_index = accumulator_index(target);
    let result_name = format!("accumulator_{:04X}", decoded.address);
    let accumulator_expression = product_accumulator_action_expression(action, target_index);
    cpp.push_str(&format!(
        "    const auto {result_name} = galaxy::dsp_accumulator_set_value({accumulator_expression}, ctx.sr);\n"
    ));
    cpp.push_str(&format!("    ctx.prod = {product_expression};\n"));
    emit_accumulator_result_commit(cpp, target_index, &result_name);
}

fn product_accumulator_action_expression(
    action: DspMultiplyAccumulatorAction,
    target_index: usize,
) -> String {
    match action {
        DspMultiplyAccumulatorAction::AddPreviousProduct => {
            format!("ctx.ac[{target_index}] + galaxy::dsp_product_value(ctx.prod)")
        }
        DspMultiplyAccumulatorAction::MovePreviousProduct => {
            "galaxy::dsp_product_value(ctx.prod)".to_string()
        }
        DspMultiplyAccumulatorAction::MoveRoundedPreviousProduct => {
            "galaxy::dsp_round_product(ctx.prod)".to_string()
        }
    }
}

fn product_multiply_expression(
    left_expression: &str,
    right_expression: &str,
    mode: &str,
) -> String {
    format!("galaxy::dsp_product_multiply({left_expression}, {right_expression}, {mode}, ctx.sr)")
}

fn ax_pair_low_expression(ax: DspAxRegister) -> String {
    format!("ctx.ax[{}][0]", ax_register_index(ax))
}

fn ax_pair_high_expression(ax: DspAxRegister) -> String {
    format!("ctx.ax[{}][1]", ax_register_index(ax))
}

fn accumulator_mid_raw_expression(accumulator: DspAccumulator) -> String {
    format!(
        "galaxy::dsp_read_accumulator_mid_raw(ctx.ac[{}])",
        accumulator_index(accumulator)
    )
}

struct DspProductMultiplyOperands {
    left: String,
    right: String,
    mode: &'static str,
}

fn multiply_operands_for_ax_parts(left: DspAxPart, right: DspAxPart) -> DspProductMultiplyOperands {
    match (left.half, right.half) {
        (DspRegisterHalf::Low, DspRegisterHalf::Low) => DspProductMultiplyOperands {
            left: ax_part_expression(left),
            right: ax_part_expression(right),
            mode: "galaxy::DspMultiplyOperandMode::UnsignedWhenSrUnsigned",
        },
        (DspRegisterHalf::Low, DspRegisterHalf::High) => DspProductMultiplyOperands {
            left: ax_part_expression(left),
            right: ax_part_expression(right),
            mode: "galaxy::DspMultiplyOperandMode::MixedUnsignedLeftWhenSrUnsigned",
        },
        (DspRegisterHalf::High, DspRegisterHalf::Low) => DspProductMultiplyOperands {
            left: ax_part_expression(right),
            right: ax_part_expression(left),
            mode: "galaxy::DspMultiplyOperandMode::MixedUnsignedLeftWhenSrUnsigned",
        },
        (DspRegisterHalf::High, DspRegisterHalf::High) => DspProductMultiplyOperands {
            left: ax_part_expression(left),
            right: ax_part_expression(right),
            mode: "galaxy::DspMultiplyOperandMode::Signed",
        },
    }
}

fn ax_part_expression(part: DspAxPart) -> String {
    format!(
        "ctx.ax[{}][{}]",
        ax_register_index(part.register),
        register_half_index(part.half)
    )
}

fn emit_logic_helper_call(
    cpp: &mut String,
    result_name: &str,
    helper_name: &str,
    target_index: usize,
    operand_expression: Option<String>,
) {
    cpp.push_str(&format!(
        "    const auto {result_name} = galaxy::{helper_name}(ctx.ac[{target_index}]"
    ));
    if let Some(operand_expression) = operand_expression {
        cpp.push_str(", ");
        cpp.push_str(&operand_expression);
    }
    cpp.push_str(", ctx.sr);\n");
}

fn logic_operand_expression(operand: DspLogicOperand) -> String {
    match operand {
        DspLogicOperand::AxHigh(register) => {
            format!("ctx.ax[{}][1]", ax_register_index(register))
        }
        DspLogicOperand::AccumulatorMid(accumulator) => {
            format!(
                "galaxy::dsp_read_accumulator_mid_raw(ctx.ac[{}])",
                accumulator_index(accumulator)
            )
        }
    }
}

fn logic_shift_count_expression(operand: DspLogicOperand) -> String {
    match operand {
        DspLogicOperand::AxHigh(register) => {
            format!("ctx.ax[{}][1]", ax_register_index(register))
        }
        DspLogicOperand::AccumulatorMid(accumulator) => {
            format!(
                "galaxy::dsp_read_accumulator_mid_raw(ctx.ac[{}])",
                accumulator_index(accumulator)
            )
        }
    }
}

fn emit_logic_result_commit(cpp: &mut String, target_index: usize, result_name: &str) {
    cpp.push_str(&format!(
        "    ctx.ac[{target_index}] = {result_name}.accumulator;\n"
    ));
    cpp.push_str(&format!("    ctx.sr = {result_name}.status;\n"));
}

fn emit_immediate_operation(
    cpp: &mut String,
    operation: DspImmediateOperation,
    decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    match operation {
        DspImmediateOperation::LoadRegisterShort { target, value } => {
            let signed_value = i16::from(value as i8) as u16;
            emit_register_write(cpp, target, &cpp_u16_literal(signed_value), decoded)
        }
        DspImmediateOperation::Xor { target, value } => {
            emit_immediate_logic_operation(cpp, decoded, "dsp_accumulator_mid_xor", target, value);
            Ok(())
        }
        DspImmediateOperation::And { target, value } => {
            emit_immediate_logic_operation(cpp, decoded, "dsp_accumulator_mid_and", target, value);
            Ok(())
        }
        DspImmediateOperation::Or { target, value } => {
            emit_immediate_logic_operation(cpp, decoded, "dsp_accumulator_mid_or", target, value);
            Ok(())
        }
        DspImmediateOperation::AddShort { target, value } => {
            emit_immediate_accumulator_operation(
                cpp,
                decoded,
                "dsp_accumulator_add",
                target,
                &signed_short_mid_literal(value),
                true,
            );
            Ok(())
        }
        DspImmediateOperation::CompareShort { target, value } => {
            emit_immediate_accumulator_operation(
                cpp,
                decoded,
                "dsp_accumulator_subtract",
                target,
                &signed_short_mid_literal(value),
                false,
            );
            Ok(())
        }
        DspImmediateOperation::Add { target, value } => {
            emit_immediate_accumulator_operation(
                cpp,
                decoded,
                "dsp_accumulator_add",
                target,
                &signed_mid_expression(value),
                true,
            );
            Ok(())
        }
        DspImmediateOperation::Compare { target, value } => {
            emit_immediate_accumulator_operation(
                cpp,
                decoded,
                "dsp_accumulator_subtract",
                target,
                &signed_mid_expression(value),
                false,
            );
            Ok(())
        }
        DspImmediateOperation::AndField { target, value } => {
            emit_immediate_field_test(cpp, target, value, "0u");
            Ok(())
        }
        DspImmediateOperation::AndCompareField { target, value } => {
            let value_literal = cpp_u16_literal(value);
            emit_immediate_field_test(cpp, target, value, &value_literal);
            Ok(())
        }
    }
}

fn emit_immediate_accumulator_operation(
    cpp: &mut String,
    decoded: &DecodedDspInstruction,
    helper_name: &str,
    target: DspAccumulator,
    value_expression: &str,
    commit_accumulator: bool,
) {
    let target_index = accumulator_index(target);
    let result_name = format!("accumulator_{:04X}", decoded.address);
    cpp.push_str(&format!(
        "    const auto {result_name} = galaxy::{helper_name}(ctx.ac[{target_index}], {value_expression}, ctx.sr);\n"
    ));
    if commit_accumulator {
        emit_accumulator_result_commit(cpp, target_index, &result_name);
    } else {
        emit_accumulator_status_commit(cpp, &result_name);
    }
}

fn emit_immediate_logic_operation(
    cpp: &mut String,
    decoded: &DecodedDspInstruction,
    helper_name: &str,
    target: DspAccumulator,
    value: u16,
) {
    let target_index = accumulator_index(target);
    let result_name = format!("logic_{:04X}", decoded.address);
    emit_logic_helper_call(
        cpp,
        &result_name,
        helper_name,
        target_index,
        Some(cpp_u16_literal(value)),
    );
    emit_logic_result_commit(cpp, target_index, &result_name);
}

fn emit_immediate_field_test(
    cpp: &mut String,
    target: DspAccumulator,
    value: u16,
    expected_expression: &str,
) {
    let target_index = accumulator_index(target);
    let value_literal = cpp_u16_literal(value);
    cpp.push_str(&format!(
        "    ctx.sr = galaxy::dsp_logic_zero_status((galaxy::dsp_read_accumulator_mid_raw(ctx.ac[{target_index}]) & {value_literal}) == {expected_expression}, ctx.sr);\n"
    ));
}

fn signed_mid_expression(value: u16) -> String {
    format!(
        "galaxy::dsp_accumulator_from_signed_mid({})",
        cpp_u16_literal(value)
    )
}

fn signed_short_mid_literal(value: u8) -> String {
    let signed = i8::from_ne_bytes([value]);
    cpp_i64_literal(i64::from(signed) << 16)
}

fn emit_memory_transfer_operation(
    cpp: &mut String,
    operation: DspMemoryTransferOperation,
    decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    match operation {
        DspMemoryTransferOperation::DirectPageLoad { target, address } => {
            emit_direct_page_load(cpp, target, address, decoded)
        }
        DspMemoryTransferOperation::DirectPageStoreRegister { address, source } => {
            emit_direct_page_store(cpp, address, &register_read_expression(source));
            Ok(())
        }
        DspMemoryTransferOperation::LoadDataMemory {
            target,
            address,
            update,
        } => {
            emit_dynamic_dram_load(cpp, target, address, decoded)?;
            emit_address_update(cpp, address, update);
            Ok(())
        }
        DspMemoryTransferOperation::StoreDataMemory {
            address,
            source,
            update,
        } => {
            emit_dynamic_dram_store(cpp, address, source, decoded)?;
            emit_address_update(cpp, address, update);
            Ok(())
        }
        DspMemoryTransferOperation::LoadInstructionMemory {
            target,
            address,
            update,
        } => {
            emit_instruction_memory_load(cpp, target, address);
            emit_address_update(cpp, address, update);
            Ok(())
        }
        DspMemoryTransferOperation::DirectPageStoreAccumulatorHigh { address, source } => {
            emit_direct_page_store(cpp, address, &accumulator_high_read_expression(source));
            Ok(())
        }
    }
}

fn emit_dynamic_dram_load(
    cpp: &mut String,
    target: DspRegister,
    address: DspAddressRegister,
    decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    emit_register_write(
        cpp,
        target,
        &format!(
            "galaxy::dsp_data_read(ctx, ctx.ar[{}])",
            address_register_index(address)
        ),
        decoded,
    )
}

fn emit_instruction_memory_load(
    cpp: &mut String,
    target: DspAccumulator,
    address: DspAddressRegister,
) {
    let target_index = accumulator_index(target);
    let address_index = address_register_index(address);
    cpp.push_str(&format!(
        "    ctx.ac[{target_index}] = galaxy::dsp_write_accumulator_mid(ctx.ac[{target_index}], galaxy::dsp_iram_read(ctx, ctx.ar[{address_index}]), ctx.sr);\n"
    ));
}

fn emit_dynamic_dram_store(
    cpp: &mut String,
    address: DspAddressRegister,
    source: DspRegister,
    decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    let source_expression = register_read_expression(source);
    let address_index = address_register_index(address);
    // Exact RMGE01 external-interrupt selection publication.  Emit the probe
    // at this statically known instruction instead of branching in the common
    // dsp_dram_write path used by millions of mixer stores.  The runtime only
    // installs the recorder after validating the generated ucode SHA-1.
    if is_rmge01_selection_publication_hook(decoded) {
        cpp.push_str(&format!(
            "    const auto selection_0715_address = ctx.ar[{address_index}];\n"
        ));
        cpp.push_str(&format!(
            "    const auto selection_0715_value = static_cast<std::uint16_t>({source_expression});\n"
        ));
        cpp.push_str(
            "    galaxy::dsp_data_write(ctx, selection_0715_address, selection_0715_value);\n",
        );
        cpp.push_str(
            "    galaxy::dsp_channel_selection_dma_probe_record_selection_write(ctx.channel_selection_dma_probe, selection_0715_address, selection_0715_value);\n",
        );
        return Ok(());
    }
    cpp.push_str(&format!(
        "    galaxy::dsp_data_write(ctx, ctx.ar[{}], static_cast<std::uint16_t>({source_expression}));\n",
        address_index
    ));
    Ok(())
}

fn emit_register_transfer_operation(
    cpp: &mut String,
    operation: DspRegisterTransferOperation,
    decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    match operation {
        DspRegisterTransferOperation::LoadImmediate { target, value } => {
            emit_register_write(cpp, target, &cpp_u16_literal(value), decoded)
        }
        DspRegisterTransferOperation::Move { target, source } => {
            let source_expression = register_read_expression(source);
            emit_register_write(cpp, target, &source_expression, decoded)
        }
        DspRegisterTransferOperation::LoadFromMemory { target, address } => {
            let address = require_static_readable_data_address(decoded, address)?;
            emit_static_data_load(cpp, target, address, decoded)
        }
        DspRegisterTransferOperation::StoreToMemory { address, source } => {
            let address = require_static_writable_data_address(decoded, address)?;
            emit_static_data_store(cpp, address, source)
        }
        DspRegisterTransferOperation::StoreImmediate { address, value } => {
            let address = require_static_writable_data_address(decoded, address)?;
            emit_static_data_write(cpp, address, &cpp_u16_literal(value));
            Ok(())
        }
    }
}

fn emit_static_data_load(
    cpp: &mut String,
    target: DspRegister,
    address: u16,
    decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    emit_register_write(
        cpp,
        target,
        &format!("galaxy::dsp_data_read(ctx, {})", cpp_u16_literal(address)),
        decoded,
    )
}

fn emit_static_data_store(
    cpp: &mut String,
    address: u16,
    source: DspRegister,
) -> Result<(), DspLoweringError> {
    let source_expression = register_read_expression(source);
    emit_static_data_write(cpp, address, &source_expression);
    Ok(())
}

fn emit_static_data_write(cpp: &mut String, address: u16, value_expression: &str) {
    cpp.push_str(&format!(
        "    galaxy::dsp_data_write(ctx, {}, static_cast<std::uint16_t>({value_expression}));\n",
        cpp_u16_literal(address)
    ));
}

fn require_static_readable_data_address(
    decoded: &DecodedDspInstruction,
    address: u16,
) -> Result<u16, DspLoweringError> {
    match data_memory_region(address) {
        Ok(DspDataMemoryRegion::Dram { word_index }) => Ok(word_index),
        Ok(DspDataMemoryRegion::CoefRom { word_index }) => Ok(0x1000u16 | word_index),
        Ok(DspDataMemoryRegion::Ifx { register }) => Ok(0xff00u16 | register),
        Err(_) => unsupported_lowering(decoded, "static DSP data read address is unmapped"),
    }
}

fn require_static_writable_data_address(
    decoded: &DecodedDspInstruction,
    address: u16,
) -> Result<u16, DspLoweringError> {
    match data_memory_region(address) {
        Ok(DspDataMemoryRegion::Dram { word_index }) => Ok(word_index),
        Ok(DspDataMemoryRegion::Ifx { register }) => Ok(0xff00u16 | register),
        Ok(DspDataMemoryRegion::CoefRom { .. }) => {
            unsupported_lowering(decoded, "static DSP data write address is read-only")
        }
        Err(_) => unsupported_lowering(decoded, "static DSP data write address is unmapped"),
    }
}

fn emit_direct_page_load(
    cpp: &mut String,
    target: DspRegister,
    address: u8,
    decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    emit_register_write(
        cpp,
        target,
        &format!(
            "galaxy::dsp_data_read(ctx, {})",
            direct_page_address_expression(address)
        ),
        decoded,
    )
}

fn emit_direct_page_store(cpp: &mut String, address: u8, value_expression: &str) {
    cpp.push_str(&format!(
        "    galaxy::dsp_data_write(ctx, {}, static_cast<std::uint16_t>({value_expression}));\n",
        direct_page_address_expression(address)
    ));
}

fn direct_page_address_expression(address: u8) -> String {
    format!("static_cast<std::uint16_t>((ctx.cr << 8u) | 0x{address:02X}u)")
}

fn emit_register_write(
    cpp: &mut String,
    target: DspRegister,
    value_expression: &str,
    decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    if let DspRegister::Accumulator { register, part } = target {
        return emit_accumulator_part_write(cpp, register, part, value_expression, decoded);
    }
    if let DspRegister::Stack(register) = target {
        cpp.push_str(&format!(
            "    galaxy::dsp_stack_push(ctx, {}, static_cast<std::uint16_t>({value_expression}));\n",
            stack_register_index(register)
        ));
        return Ok(());
    }

    let destination = register_write_destination(target);
    cpp.push_str("    ");
    cpp.push_str(&destination);
    cpp.push_str(" = ");
    cpp.push_str(&format!(
        "static_cast<{}>({value_expression});\n",
        register_write_cast_type(target)
    ));
    Ok(())
}

fn emit_accumulator_part_write(
    cpp: &mut String,
    register: DspAccumulator,
    part: DspAccumulatorPart,
    value_expression: &str,
    _decoded: &DecodedDspInstruction,
) -> Result<(), DspLoweringError> {
    let index = accumulator_index(register);
    match part {
        DspAccumulatorPart::Low => {
            cpp.push_str(&format!(
                "    ctx.ac[{index}] = galaxy::dsp_write_accumulator_low(ctx.ac[{index}], static_cast<std::uint16_t>({value_expression}));\n"
            ));
            Ok(())
        }
        DspAccumulatorPart::Mid => {
            cpp.push_str(&format!(
                "    ctx.ac[{index}] = galaxy::dsp_write_accumulator_mid(ctx.ac[{index}], static_cast<std::uint16_t>({value_expression}), ctx.sr);\n"
            ));
            Ok(())
        }
        DspAccumulatorPart::High => {
            cpp.push_str(&format!(
                "    ctx.ac[{index}] = galaxy::dsp_write_accumulator_high(ctx.ac[{index}], static_cast<std::uint16_t>({value_expression}));\n"
            ));
            Ok(())
        }
    }
}

fn register_read_expression(register: DspRegister) -> String {
    match register {
        DspRegister::Address(register) => format!("ctx.ar[{}]", address_register_index(register)),
        DspRegister::Index(register) => {
            format!(
                "static_cast<std::uint16_t>(ctx.ix[{}])",
                index_register_index(register)
            )
        }
        DspRegister::Wrap(register) => format!("ctx.wr[{}]", wrap_register_index(register)),
        DspRegister::Status => "ctx.sr".to_string(),
        DspRegister::ProductLow => "ctx.prod.low".to_string(),
        DspRegister::ProductMid => "ctx.prod.mid".to_string(),
        DspRegister::ProductHigh => "ctx.prod.high".to_string(),
        DspRegister::ProductMid2 => "ctx.prod.mid2".to_string(),
        DspRegister::Stack(register) => {
            format!(
                "galaxy::dsp_stack_pop(ctx, {})",
                stack_register_index(register)
            )
        }
        DspRegister::Control => "ctx.cr".to_string(),
        DspRegister::Ax { register, half } => {
            format!(
                "ctx.ax[{}][{}]",
                ax_register_index(register),
                register_half_index(half)
            )
        }
        DspRegister::Accumulator {
            register,
            part: DspAccumulatorPart::Low,
        } => format!(
            "galaxy::dsp_read_accumulator_low(ctx.ac[{}])",
            accumulator_index(register)
        ),
        DspRegister::Accumulator {
            register,
            part: DspAccumulatorPart::Mid,
        } => format!(
            "galaxy::dsp_read_accumulator_mid(ctx.ac[{}], ctx.sr)",
            accumulator_index(register)
        ),
        DspRegister::Accumulator {
            register,
            part: DspAccumulatorPart::High,
        } => accumulator_high_read_expression(register),
    }
}

fn accumulator_high_read_expression(register: DspAccumulator) -> String {
    format!(
        "galaxy::dsp_read_accumulator_high(ctx.ac[{}])",
        accumulator_index(register)
    )
}

fn register_write_destination(register: DspRegister) -> String {
    match register {
        DspRegister::Address(register) => format!("ctx.ar[{}]", address_register_index(register)),
        DspRegister::Index(register) => format!("ctx.ix[{}]", index_register_index(register)),
        DspRegister::Wrap(register) => format!("ctx.wr[{}]", wrap_register_index(register)),
        DspRegister::Status => "ctx.sr".to_string(),
        DspRegister::ProductLow => "ctx.prod.low".to_string(),
        DspRegister::ProductMid => "ctx.prod.mid".to_string(),
        DspRegister::ProductHigh => "ctx.prod.high".to_string(),
        DspRegister::ProductMid2 => "ctx.prod.mid2".to_string(),
        DspRegister::Control => "ctx.cr".to_string(),
        DspRegister::Ax { register, half } => {
            format!(
                "ctx.ax[{}][{}]",
                ax_register_index(register),
                register_half_index(half)
            )
        }
        DspRegister::Stack(_) | DspRegister::Accumulator { .. } => {
            unreachable!("stack and accumulator writes are handled before field destinations")
        }
    }
}

const fn register_write_cast_type(register: DspRegister) -> &'static str {
    match register {
        DspRegister::Index(_) => "std::int16_t",
        _ => "std::uint16_t",
    }
}

fn cpp_u16_literal(value: u16) -> String {
    format!("0x{value:04X}u")
}

fn cpp_i64_literal(value: i64) -> String {
    format!("{value}ll")
}

const fn condition_cpp_name(condition: DspCondition) -> &'static str {
    match condition {
        DspCondition::GreaterOrEqual => "GreaterOrEqual",
        DspCondition::Less => "Less",
        DspCondition::Greater => "Greater",
        DspCondition::LessOrEqual => "LessOrEqual",
        DspCondition::NotZero => "NotZero",
        DspCondition::Zero => "Zero",
        DspCondition::NotCarry => "NotCarry",
        DspCondition::Carry => "Carry",
        DspCondition::NotOverSigned32 => "NotOverSigned32",
        DspCondition::OverSigned32 => "OverSigned32",
        DspCondition::ExtendedA => "ExtendedA",
        DspCondition::ExtendedB => "ExtendedB",
        DspCondition::LogicNotZero => "LogicNotZero",
        DspCondition::LogicZero => "LogicZero",
        DspCondition::Overflow => "Overflow",
        DspCondition::Always => "Always",
    }
}

fn emit_fallthrough(
    cpp: &mut String,
    decoded: &DecodedDspInstruction,
    next_address: Option<u16>,
    layout: &DspProgramLayout,
) -> Result<(), DspLoweringError> {
    emit_fallthrough_with_post_commit_interrupt_acceptance(
        cpp,
        decoded,
        next_address,
        layout,
        false,
    )
}

fn emit_fallthrough_with_post_commit_interrupt_acceptance(
    cpp: &mut String,
    decoded: &DecodedDspInstruction,
    next_address: Option<u16>,
    layout: &DspProgramLayout,
    accept_pending_external_interrupt: bool,
) -> Result<(), DspLoweringError> {
    let Some(target) = next_address else {
        return bail_or_trap(
            cpp,
            layout,
            decoded,
            "fallthrough leaves lowered DSP program",
        );
    };
    require_decoded_static_target(
        layout,
        decoded,
        target,
        "fallthrough leaves lowered DSP program",
    )?;
    emit_static_transfer_with_post_commit_interrupt_acceptance(
        cpp,
        decoded,
        target,
        layout,
        accept_pending_external_interrupt,
    )
}

fn emit_static_transfer(
    cpp: &mut String,
    decoded: &DecodedDspInstruction,
    target: u16,
    layout: &DspProgramLayout,
) -> Result<(), DspLoweringError> {
    emit_static_transfer_with_post_commit_interrupt_acceptance(cpp, decoded, target, layout, false)
}

fn emit_static_transfer_with_post_commit_interrupt_acceptance(
    cpp: &mut String,
    decoded: &DecodedDspInstruction,
    target: u16,
    layout: &DspProgramLayout,
    accept_pending_external_interrupt: bool,
) -> Result<(), DspLoweringError> {
    if let Some(loop_end_word) = layout.loop_end_word_for_instruction(decoded) {
        emit_loop_checkpoint(cpp, loop_end_word, 4);
    }
    emit_raw_instruction_commit(cpp, 4);
    if accept_pending_external_interrupt {
        cpp.push_str(&format!("    ctx.pc = {};\n", cpp_u16_literal(target)));
        cpp.push_str("    galaxy::dsp_accept_pending_external_interrupt(ctx);\n");
    }
    emit_static_backedge_interrupt_checkpoint(cpp, decoded, target, 4);
    emit_dsp_goto(cpp, target);
    Ok(())
}

fn emit_dynamic_transfer(
    cpp: &mut String,
    decoded: &DecodedDspInstruction,
    target_expression: &str,
    layout: &DspProgramLayout,
    indent_spaces: usize,
) {
    if let Some(loop_end_word) = layout.loop_end_word_for_instruction(decoded) {
        emit_indent(cpp, indent_spaces);
        cpp.push_str(&format!(
            "ctx.pc = static_cast<std::uint16_t>({target_expression});\n"
        ));
        emit_loop_checkpoint(cpp, loop_end_word, indent_spaces);
        emit_raw_instruction_commit(cpp, indent_spaces);
        emit_dynamic_backedge_interrupt_checkpoint(cpp, decoded, "ctx.pc", indent_spaces);
        emit_dynamic_dsp_goto(cpp, "ctx.pc", indent_spaces);
    } else {
        emit_raw_instruction_commit(cpp, indent_spaces);
        emit_dynamic_backedge_interrupt_checkpoint(cpp, decoded, target_expression, indent_spaces);
        emit_dynamic_dsp_goto(cpp, target_expression, indent_spaces);
    }
}

fn emit_static_backedge_interrupt_checkpoint(
    cpp: &mut String,
    decoded: &DecodedDspInstruction,
    target: u16,
    indent_spaces: usize,
) {
    if target <= decoded.address {
        emit_indent(cpp, indent_spaces);
        cpp.push_str(&format!("ctx.pc = {};\n", cpp_u16_literal(target)));
        emit_indent(cpp, indent_spaces);
        cpp.push_str(&format!(
            "galaxy::dsp_native_handle_static_backedge(ctx, {}, {});\n",
            cpp_u16_literal(decoded.address),
            cpp_u16_literal(target)
        ));
        emit_indent(cpp, indent_spaces);
        cpp.push_str("galaxy::dsp_accept_pending_external_interrupt(ctx);\n");
    }
}

fn emit_dynamic_backedge_interrupt_checkpoint(
    cpp: &mut String,
    decoded: &DecodedDspInstruction,
    target_expression: &str,
    indent_spaces: usize,
) {
    emit_indent(cpp, indent_spaces);
    cpp.push_str(&format!(
        "if (static_cast<std::uint16_t>({target_expression}) <= {}) {{\n",
        cpp_u16_literal(decoded.address)
    ));
    emit_indent(cpp, indent_spaces + 4);
    cpp.push_str(&format!(
        "ctx.pc = static_cast<std::uint16_t>({target_expression});\n"
    ));
    emit_indent(cpp, indent_spaces + 4);
    cpp.push_str("galaxy::dsp_accept_pending_external_interrupt(ctx);\n");
    emit_indent(cpp, indent_spaces);
    cpp.push_str("}\n");
}

fn emit_raw_instruction_commit(cpp: &mut String, indent_spaces: usize) {
    emit_indent(cpp, indent_spaces);
    cpp.push_str("galaxy::dsp_raw_instruction_boundary_commit(ctx, raw_instruction_boundary);\n");
}

fn emit_loop_checkpoint(cpp: &mut String, address: u16, indent_spaces: usize) {
    emit_indent(cpp, indent_spaces);
    cpp.push_str(&format!(
        "if (ctx.st[2] != 0u && ctx.st[2] == {} && ctx.st[3] != 0u) {{\n",
        cpp_u16_literal(address)
    ));
    emit_indent(cpp, indent_spaces + 4);
    cpp.push_str("ctx.st[3] = static_cast<std::uint16_t>(ctx.st[3] - 1u);\n");
    emit_indent(cpp, indent_spaces + 4);
    cpp.push_str("if (ctx.st[3] != 0u) {\n");
    emit_indent(cpp, indent_spaces + 8);
    cpp.push_str(&format!(
        "const auto loop_target_{address:04X} = ctx.st[0];\n"
    ));
    emit_indent(cpp, indent_spaces + 8);
    cpp.push_str(&format!(
        "ctx.pc = static_cast<std::uint16_t>(loop_target_{address:04X});\n"
    ));
    emit_raw_instruction_commit(cpp, indent_spaces + 8);
    emit_indent(cpp, indent_spaces + 8);
    cpp.push_str("galaxy::dsp_accept_pending_external_interrupt(ctx);\n");
    emit_dynamic_dsp_goto(cpp, "ctx.pc", indent_spaces + 8);
    emit_indent(cpp, indent_spaces + 4);
    cpp.push_str("}\n");
    emit_indent(cpp, indent_spaces + 4);
    cpp.push_str("(void)galaxy::dsp_stack_pop(ctx, 0);\n");
    emit_indent(cpp, indent_spaces + 4);
    cpp.push_str("(void)galaxy::dsp_stack_pop(ctx, 2);\n");
    emit_indent(cpp, indent_spaces + 4);
    cpp.push_str("(void)galaxy::dsp_stack_pop(ctx, 3);\n");
    emit_indent(cpp, indent_spaces);
    cpp.push_str("}\n");
}

fn emit_status_bit_operation(cpp: &mut String, operation: DspStatusBitOperation) {
    match operation {
        DspStatusBitOperation::Clear { bit } => {
            emit_status_mask_update(cpp, 1_u16 << bit, false);
        }
        DspStatusBitOperation::Set { bit } => {
            emit_status_mask_update(cpp, 1_u16 << bit, true);
        }
    }
}

fn status_bit_enables_external_interrupt(operation: DspStatusBitOperation) -> bool {
    matches!(
        operation,
        DspStatusBitOperation::Set { bit }
            if (1_u16 << bit) & DspStatusFlag::ExternalInterruptEnable.mask() != 0
    )
}

fn emit_mode_operation(cpp: &mut String, operation: DspModeOperation) {
    match operation {
        DspModeOperation::ExtendableNop => {}
        DspModeOperation::ProductMultiplyByTwo { enabled } => {
            emit_status_mask_update(cpp, DspStatusFlag::MultiplyModifier.mask(), !enabled);
        }
        DspModeOperation::UnsignedMultiply { enabled } => {
            emit_status_mask_update(cpp, DspStatusFlag::UnsignedMultiply.mask(), enabled);
        }
        DspModeOperation::Mode40Bit { enabled } => {
            emit_status_mask_update(cpp, DspStatusFlag::Mode40Bit.mask(), enabled);
        }
    }
}

fn emit_status_mask_update(cpp: &mut String, mask: u16, set: bool) {
    if set {
        cpp.push_str(&format!(
            "    ctx.sr = static_cast<std::uint16_t>(ctx.sr | 0x{mask:04X}u);\n"
        ));
    } else {
        cpp.push_str(&format!(
            "    ctx.sr = static_cast<std::uint16_t>(ctx.sr & ~0x{mask:04X}u);\n"
        ));
    }
}

fn emit_address_operation(cpp: &mut String, operation: DspAddressOperation) {
    match operation {
        DspAddressOperation::Decrement { address } => {
            let address_index = address_register_index(address);
            cpp.push_str(&format!(
                "    ctx.ar[{address_index}] = galaxy::dsp_decrement_address(ctx.ar[{address_index}], ctx.wr[{address_index}]);\n"
            ));
        }
        DspAddressOperation::Increment { address } => {
            let address_index = address_register_index(address);
            cpp.push_str(&format!(
                "    ctx.ar[{address_index}] = galaxy::dsp_increment_address(ctx.ar[{address_index}], ctx.wr[{address_index}]);\n"
            ));
        }
        DspAddressOperation::SubtractIndex { address, index } => {
            let address_index = address_register_index(address);
            let index_index = index_register_index(index);
            cpp.push_str(&format!(
                "    ctx.ar[{address_index}] = galaxy::dsp_decrease_address(ctx.ar[{address_index}], ctx.ix[{index_index}], ctx.wr[{address_index}]);\n"
            ));
        }
        DspAddressOperation::AddIndex { address, index } => {
            let address_index = address_register_index(address);
            let index_index = index_register_index(index);
            cpp.push_str(&format!(
                "    ctx.ar[{address_index}] = galaxy::dsp_increase_address(ctx.ar[{address_index}], ctx.ix[{index_index}], ctx.wr[{address_index}]);\n"
            ));
        }
    }
}

fn emit_address_update(cpp: &mut String, address: DspAddressRegister, update: DspAddressUpdate) {
    let address_index = address_register_index(address);
    match update {
        DspAddressUpdate::None => {}
        DspAddressUpdate::Decrement => {
            cpp.push_str(&format!(
                "    ctx.ar[{address_index}] = galaxy::dsp_decrement_address(ctx.ar[{address_index}], ctx.wr[{address_index}]);\n"
            ));
        }
        DspAddressUpdate::Increment => {
            cpp.push_str(&format!(
                "    ctx.ar[{address_index}] = galaxy::dsp_increment_address(ctx.ar[{address_index}], ctx.wr[{address_index}]);\n"
            ));
        }
        DspAddressUpdate::AddIndex => {
            cpp.push_str(&format!(
                "    ctx.ar[{address_index}] = galaxy::dsp_increase_address(ctx.ar[{address_index}], ctx.ix[{address_index}], ctx.wr[{address_index}]);\n"
            ));
        }
    }
}

const fn address_register_index(register: DspAddressRegister) -> usize {
    match register {
        DspAddressRegister::Ar0 => 0,
        DspAddressRegister::Ar1 => 1,
        DspAddressRegister::Ar2 => 2,
        DspAddressRegister::Ar3 => 3,
    }
}

const fn index_register_index(register: DspIndexRegister) -> usize {
    match register {
        DspIndexRegister::Ix0 => 0,
        DspIndexRegister::Ix1 => 1,
        DspIndexRegister::Ix2 => 2,
        DspIndexRegister::Ix3 => 3,
    }
}

const fn wrap_register_index(register: DspWrapRegister) -> usize {
    match register {
        DspWrapRegister::Wr0 => 0,
        DspWrapRegister::Wr1 => 1,
        DspWrapRegister::Wr2 => 2,
        DspWrapRegister::Wr3 => 3,
    }
}

const fn stack_register_index(register: DspStackRegister) -> usize {
    match register {
        DspStackRegister::Call => 0,
        DspStackRegister::Data => 1,
        DspStackRegister::LoopAddress => 2,
        DspStackRegister::LoopCounter => 3,
    }
}

const fn ax_register_index(register: DspAxRegister) -> usize {
    match register {
        DspAxRegister::Ax0 => 0,
        DspAxRegister::Ax1 => 1,
    }
}

const fn accumulator_index(register: DspAccumulator) -> usize {
    match register {
        DspAccumulator::Ac0 => 0,
        DspAccumulator::Ac1 => 1,
    }
}

const fn register_half_index(half: DspRegisterHalf) -> usize {
    match half {
        DspRegisterHalf::Low => 0,
        DspRegisterHalf::High => 1,
    }
}

fn unsupported_lowering<T>(
    decoded: &DecodedDspInstruction,
    reason: &'static str,
) -> Result<T, DspLoweringError> {
    Err(DspLoweringError::UnsupportedInstruction {
        address: decoded.address,
        word: decoded.raw_word,
        reason,
    })
}

/// A control-flow edge that leaves the lowered program. On the strict linear
/// path this is a hard installation error. On the tolerant CFG path it means the
/// edge was reconstructed over a region we treated as data, so it is lowered as
/// a runtime hard-fail trap instead of failing the whole module.
fn bail_or_trap(
    cpp: &mut String,
    layout: &DspProgramLayout,
    decoded: &DecodedDspInstruction,
    reason: &'static str,
) -> Result<(), DspLoweringError> {
    if layout.tolerant {
        let pc_literal = cpp_u16_literal(decoded.address);
        emit_dsp_hard_trap(cpp, &pc_literal, reason, 4);
        Ok(())
    } else {
        unsupported_lowering(decoded, reason)
    }
}

fn require_decoded_static_target(
    layout: &DspProgramLayout,
    decoded: &DecodedDspInstruction,
    target: u16,
    reason: &'static str,
) -> Result<(), DspLoweringError> {
    if layout.labels.contains(&target) || layout.tolerant {
        Ok(())
    } else {
        unsupported_lowering(decoded, reason)
    }
}

fn emit_dsp_goto(cpp: &mut String, address: u16) {
    emit_dsp_goto_indented(cpp, address, 4);
}

/// Emit a hard-fail trap label for every static `goto` target that was not
/// decoded into a real instruction label. This happens when speculative
/// gap-recovered code branches into a region we treated as data. The branch can
/// only be reached if our code/data split was wrong, so it aborts rather than
/// silently falling through — and it lets the generated unit compile instead of
/// leaving a dangling label reference.
fn emit_dsp_trap_labels(cpp: &mut String, defined: &BTreeSet<u16>) {
    const NEEDLE: &str = "goto pc_";
    let mut referenced: BTreeSet<u16> = BTreeSet::new();
    let mut search = 0usize;
    while let Some(found) = cpp[search..].find(NEEDLE) {
        let start = search + found + NEEDLE.len();
        if let Some(hex) = cpp.get(start..start + 4) {
            if let Ok(address) = u16::from_str_radix(hex, 16) {
                referenced.insert(address);
            }
        }
        search = start;
    }

    for address in referenced.difference(defined) {
        cpp.push('\n');
        cpp.push_str(&dsp_label(*address));
        cpp.push_str(":\n");
        let pc_literal = cpp_u16_literal(*address);
        emit_dsp_hard_trap(
            cpp,
            &pc_literal,
            "static DSP target outside lowered program",
            4,
        );
    }
}

fn emit_dsp_call_stack_push(cpp: &mut String, return_address: u16, indent_spaces: usize) {
    emit_indent(cpp, indent_spaces);
    cpp.push_str(&format!(
        "galaxy::dsp_stack_push(ctx, 0, {});\n",
        cpp_u16_literal(return_address)
    ));
}

/// Computed (register) branch: store the runtime target in `ctx.pc` and jump to
/// the single shared `pc_dispatch` block, which maps the target to a label. This
/// replaces emitting a full label switch at every computed branch — for a real
/// ucode with hundreds of register jumps that is the difference between a
/// multi-million-line module and a few thousand lines, with no behavior change.
fn emit_dynamic_dsp_goto(cpp: &mut String, target_expression: &str, indent_spaces: usize) {
    if target_expression != "ctx.pc" {
        emit_indent(cpp, indent_spaces);
        cpp.push_str(&format!(
            "ctx.pc = static_cast<std::uint16_t>({target_expression});\n"
        ));
    }
    emit_indent(cpp, indent_spaces);
    cpp.push_str("goto pc_dispatch;\n");
}

/// Emit the one shared computed-branch dispatch block: `ctx.pc` -> label, with a
/// hard-fail default for a target that is not a decoded instruction.
fn emit_dsp_dispatch_block(cpp: &mut String, labels: &BTreeSet<u16>) {
    cpp.push_str("\npc_dispatch:\n");
    // Any computed control transfer invalidates a partially observed RMGE01
    // idle-sequence prefix. Keep this in the generator so regenerating the
    // canonical native DSP cannot silently weaken exact provenance tracking.
    cpp.push_str("    galaxy::dsp_native_reset_idle_sequence_progress(ctx);\n");
    cpp.push_str("    switch (ctx.pc) {\n");
    for label in labels {
        cpp.push_str(&format!("    case {}:\n", cpp_u16_literal(*label)));
        emit_dsp_goto_indented(cpp, *label, 8);
    }
    cpp.push_str("    default:\n");
    emit_dsp_hard_trap(
        cpp,
        "ctx.pc",
        "computed DSP target outside lowered program",
        8,
    );
    cpp.push_str("    }\n");
}

fn emit_dsp_goto_indented(cpp: &mut String, address: u16, indent_spaces: usize) {
    emit_indent(cpp, indent_spaces);
    cpp.push_str("goto ");
    cpp.push_str(&dsp_label(address));
    cpp.push_str(";\n");
}

fn emit_indent(cpp: &mut String, indent_spaces: usize) {
    for _ in 0..indent_spaces {
        cpp.push(' ');
    }
}

fn emit_dsp_hard_trap(cpp: &mut String, pc_expression: &str, reason: &str, indent_spaces: usize) {
    let reason_literal = reason.escape_default().to_string();
    emit_indent(cpp, indent_spaces);
    cpp.push_str(&format!(
        "galaxy::dsp_hard_trap(ctx, {pc_expression}, \"{reason_literal}\");\n"
    ));
}

fn dsp_label(address: u16) -> String {
    format!("pc_{address:04X}")
}

fn is_cpp_identifier(name: &str) -> bool {
    let mut chars = name.chars();
    match chars.next() {
        Some(first) if first == '_' || first.is_ascii_alphabetic() => {}
        _ => return false,
    }

    chars.all(|character| character == '_' || character.is_ascii_alphanumeric())
}

fn require_valid_instruction_address(address: u16) -> Result<(), DspDecodeError> {
    instruction_memory_region(address)
        .map(|_| ())
        .map_err(|_| DspDecodeError::InvalidInstructionAddress { address })
}

pub fn decode_dsp_instruction(
    address: u16,
    words: &[u16],
) -> Result<DecodedDspInstruction, DspDecodeError> {
    let word = *words
        .first()
        .ok_or(DspDecodeError::MissingOpcode { address })?;
    let (instruction, length_words) = decode_word(address, words)?;
    Ok(DecodedDspInstruction {
        address,
        raw_word: word,
        second_word: (length_words == 2).then(|| words[1]),
        length_words,
        instruction,
    })
}

fn decode_word(address: u16, words: &[u16]) -> Result<(DspInstruction, u8), DspDecodeError> {
    let word = *words
        .first()
        .ok_or(DspDecodeError::MissingOpcode { address })?;

    let single = match word {
        word if word & 0xfffc == 0x0000 => Some(DspInstruction::Nop),
        word if word & 0xfffc == 0x0004 => {
            Some(DspInstruction::Address(DspAddressOperation::Decrement {
                address: DspAddressRegister::from_bits(word),
            }))
        }
        word if word & 0xfffc == 0x0008 => {
            Some(DspInstruction::Address(DspAddressOperation::Increment {
                address: DspAddressRegister::from_bits(word),
            }))
        }
        word if word & 0xfffc == 0x000c => {
            let register = DspAddressRegister::from_bits(word);
            Some(DspInstruction::Address(
                DspAddressOperation::SubtractIndex {
                    address: register,
                    index: DspIndexRegister::from_bits(word),
                },
            ))
        }
        word if word & 0xfff0 == 0x0010 => {
            Some(DspInstruction::Address(DspAddressOperation::AddIndex {
                address: DspAddressRegister::from_bits(word),
                index: DspIndexRegister::from_bits(word >> 2),
            }))
        }
        0x0021 => Some(DspInstruction::Halt),
        word if word & 0xfff0 == 0x0270 => Some(DspInstruction::If {
            condition: DspCondition::from_low_nibble(word),
        }),
        word if word & 0xfff0 == 0x02d0 => Some(DspInstruction::Return {
            interrupt: false,
            condition: DspCondition::from_low_nibble(word),
        }),
        word if word & 0xfff0 == 0x02f0 => Some(DspInstruction::Return {
            interrupt: true,
            condition: DspCondition::from_low_nibble(word),
        }),
        _ => None,
    };

    if let Some(instruction) = single {
        return Ok((instruction, 1));
    }

    if let Some(instruction) = decode_parallel_instruction(address, word)? {
        return Ok((instruction, 1));
    }

    if let Some((instruction, length_words)) =
        decode_control_or_transfer_instruction(address, word, words)?
    {
        return Ok((instruction, length_words));
    }

    if let Some((instruction, length_words)) =
        decode_immediate_or_memory_instruction(address, word, words)?
    {
        return Ok((instruction, length_words));
    }

    if let Some(instruction) = decode_mode_instruction(address, word)? {
        return Ok((instruction, 1));
    }

    if let Some(instruction) = decode_logic_instruction(address, word)? {
        return Ok((instruction, 1));
    }

    if let Some(instruction) = decode_product_instruction(address, word)? {
        return Ok((instruction, 1));
    }

    if let Some(instruction) = decode_accumulator_instruction(address, word)? {
        return Ok((instruction, 1));
    }

    Err(DspDecodeError::UnsupportedOpcode { address, word })
}

fn decode_parallel_instruction(
    address: u16,
    word: u16,
) -> Result<Option<DspInstruction>, DspDecodeError> {
    let high_nibble = word >> 12;
    if high_nibble < 0x3 {
        return Ok(None);
    }

    let extension_mask = if high_nibble == 0x3 { 0x007f } else { 0x00ff };
    let extension = word & extension_mask;
    if extension == 0 {
        return Ok(None);
    }

    let base_word = word & !extension_mask;
    let Some(primary) = decode_parallel_primary(address, base_word)? else {
        return Ok(None);
    };
    let Some(parallel) = decode_parallel_operation(extension) else {
        return Err(DspDecodeError::UnsupportedOpcode { address, word });
    };

    Ok(Some(DspInstruction::Parallel {
        primary,
        parallel,
        raw_word: word,
    }))
}

fn decode_parallel_primary(
    address: u16,
    base_word: u16,
) -> Result<Option<DspParallelPrimary>, DspDecodeError> {
    if let Some(DspInstruction::Mode(operation)) = decode_mode_instruction(address, base_word)? {
        return Ok(Some(DspParallelPrimary::Mode(operation)));
    }
    if let Some(DspInstruction::Logic(operation)) = decode_logic_instruction(address, base_word)? {
        return Ok(Some(DspParallelPrimary::Logic(operation)));
    }
    if let Some(DspInstruction::Product(operation)) =
        decode_product_instruction(address, base_word)?
    {
        return Ok(Some(DspParallelPrimary::Product(operation)));
    }
    if let Some(DspInstruction::Accumulator(operation)) =
        decode_accumulator_instruction(address, base_word)?
    {
        return Ok(Some(DspParallelPrimary::Accumulator(operation)));
    }
    Ok(None)
}

fn decode_parallel_operation(extension: u16) -> Option<DspParallelOperation> {
    match extension {
        ext if ext & 0x00fc == 0x0000 => Some(DspParallelOperation::Nop {
            raw_extension: ext as u8,
        }),
        ext if ext & 0x00fc == 0x0004 => Some(DspParallelOperation::Address(
            DspAddressOperation::Decrement {
                address: DspAddressRegister::from_bits(ext),
            },
        )),
        ext if ext & 0x00fc == 0x0008 => Some(DspParallelOperation::Address(
            DspAddressOperation::Increment {
                address: DspAddressRegister::from_bits(ext),
            },
        )),
        ext if ext & 0x00fc == 0x000c => {
            let address = DspAddressRegister::from_bits(ext);
            Some(DspParallelOperation::Address(
                DspAddressOperation::AddIndex {
                    address,
                    index: DspIndexRegister::from_bits(ext),
                },
            ))
        }
        ext if ext & 0x00f0 == 0x0010 => Some(DspParallelOperation::Move {
            target: register_from_bits(0x18 + ((ext >> 2) & 0x0003)),
            source: register_from_bits(0x1c + (ext & 0x0003)),
        }),
        ext if ext & 0x00e4 == 0x0020 => Some(DspParallelOperation::StoreSingle {
            address: DspAddressRegister::from_bits(ext),
            source: register_from_bits(0x1c + ((ext >> 3) & 0x0003)),
            update: DspAddressUpdate::Increment,
        }),
        ext if ext & 0x00e4 == 0x0024 => Some(DspParallelOperation::StoreSingle {
            address: DspAddressRegister::from_bits(ext),
            source: register_from_bits(0x1c + ((ext >> 3) & 0x0003)),
            update: DspAddressUpdate::AddIndex,
        }),
        ext if ext & 0x00c4 == 0x0040 => Some(DspParallelOperation::LoadSingle {
            target: register_from_bits(0x18 + ((ext >> 3) & 0x0007)),
            address: DspAddressRegister::from_bits(ext),
            update: DspAddressUpdate::Increment,
        }),
        ext if ext & 0x00c4 == 0x0044 => Some(DspParallelOperation::LoadSingle {
            target: register_from_bits(0x18 + ((ext >> 3) & 0x0007)),
            address: DspAddressRegister::from_bits(ext),
            update: DspAddressUpdate::AddIndex,
        }),
        ext if ext & 0x00ce == 0x0080 => decode_parallel_accumulator_mid_load_store(ext),
        ext if ext & 0x00ce == 0x0082 => decode_parallel_accumulator_mid_load_store(ext),
        ext if ext & 0x00ce == 0x0084 => decode_parallel_accumulator_mid_load_store(ext),
        ext if ext & 0x00ce == 0x0086 => decode_parallel_accumulator_mid_load_store(ext),
        ext if ext & 0x00ce == 0x0088 => decode_parallel_accumulator_mid_load_store(ext),
        ext if ext & 0x00ce == 0x008a => decode_parallel_accumulator_mid_load_store(ext),
        ext if ext & 0x00ce == 0x008c => decode_parallel_accumulator_mid_load_store(ext),
        ext if ext & 0x00ce == 0x008e => decode_parallel_accumulator_mid_load_store(ext),
        ext if ext & 0x00cf == 0x00c3 => decode_parallel_load_ax_pair(ext),
        ext if ext & 0x00cf == 0x00c7 => decode_parallel_load_ax_pair(ext),
        ext if ext & 0x00cf == 0x00cb => decode_parallel_load_ax_pair(ext),
        ext if ext & 0x00cf == 0x00cf => decode_parallel_load_ax_pair(ext),
        ext if ext & 0x00cc == 0x00c0 => decode_parallel_load_ax_parts(ext),
        ext if ext & 0x00cc == 0x00c4 => decode_parallel_load_ax_parts(ext),
        ext if ext & 0x00cc == 0x00c8 => decode_parallel_load_ax_parts(ext),
        ext if ext & 0x00cc == 0x00cc => decode_parallel_load_ax_parts(ext),
        _ => None,
    }
}

fn decode_parallel_accumulator_mid_load_store(extension: u16) -> Option<DspParallelOperation> {
    let order = if extension & 0x0002 == 0 {
        DspParallelLoadStoreOrder::LoadThenStore
    } else {
        DspParallelLoadStoreOrder::StoreThenLoad
    };
    let ar0_update = if extension & 0x0004 == 0 {
        DspAddressUpdate::Increment
    } else {
        DspAddressUpdate::AddIndex
    };
    let ar3_update = if extension & 0x0008 == 0 {
        DspAddressUpdate::Increment
    } else {
        DspAddressUpdate::AddIndex
    };

    Some(DspParallelOperation::AccumulatorMidLoadStore {
        order,
        target: register_from_bits(0x18 + ((extension >> 4) & 0x0003)),
        accumulator: accumulator_from_bit(extension),
        ar0_update,
        ar3_update,
    })
}

fn decode_parallel_load_ax_pair(extension: u16) -> Option<DspParallelOperation> {
    Some(DspParallelOperation::LoadAxPair {
        ax: ax_register_from_bit(extension >> 4),
        address: DspAddressRegister::from_bits((extension >> 5) & 0x0001),
        ar_update: parallel_update_from_bit(extension >> 2),
        ar3_update: parallel_update_from_bit(extension >> 3),
    })
}

fn decode_parallel_load_ax_parts(extension: u16) -> Option<DspParallelOperation> {
    Some(DspParallelOperation::LoadAxParts {
        ax0_half: register_half_from_bit(extension >> 5),
        ax1_half: register_half_from_bit(extension >> 4),
        address: DspAddressRegister::from_bits(extension),
        ar_update: parallel_update_from_bit(extension >> 2),
        ar3_update: parallel_update_from_bit(extension >> 3),
    })
}

const fn parallel_update_from_bit(bit: u16) -> DspAddressUpdate {
    if bit & 1 == 0 {
        DspAddressUpdate::Increment
    } else {
        DspAddressUpdate::AddIndex
    }
}

fn decode_control_or_transfer_instruction(
    address: u16,
    word: u16,
    words: &[u16],
) -> Result<Option<(DspInstruction, u8)>, DspDecodeError> {
    let decoded = match word {
        word if word & 0xfff0 == 0x0290 => {
            require_words(address, word, words, 2)?;
            Some((
                DspInstruction::JumpImmediate {
                    condition: DspCondition::from_low_nibble(word),
                    target: words[1],
                },
                2,
            ))
        }
        word if word & 0xfff0 == 0x02b0 => {
            require_words(address, word, words, 2)?;
            Some((
                DspInstruction::CallImmediate {
                    condition: DspCondition::from_low_nibble(word),
                    target: words[1],
                },
                2,
            ))
        }
        word if word & 0xff10 == 0x1700 => Some((
            DspInstruction::JumpRegister {
                condition: DspCondition::from_low_nibble(word),
                target: register_from_bits((word >> 5) & 0x0007),
            },
            1,
        )),
        word if word & 0xff10 == 0x1710 => Some((
            DspInstruction::CallRegister {
                condition: DspCondition::from_low_nibble(word),
                target: register_from_bits((word >> 5) & 0x0007),
            },
            1,
        )),
        word if word & 0xffe0 == 0x0040 => Some((
            DspInstruction::Loop {
                count: register_from_bits(word),
            },
            1,
        )),
        word if word & 0xffe0 == 0x0060 => {
            require_words(address, word, words, 2)?;
            Some((
                DspInstruction::BlockLoop {
                    count: register_from_bits(word),
                    end_address: words[1],
                },
                2,
            ))
        }
        word if word & 0xff00 == 0x1000 => Some((
            DspInstruction::LoopImmediate {
                count: (word & 0x00ff) as u8,
            },
            1,
        )),
        word if word & 0xff00 == 0x1100 => {
            require_words(address, word, words, 2)?;
            Some((
                DspInstruction::BlockLoopImmediate {
                    count: (word & 0x00ff) as u8,
                    end_address: words[1],
                },
                2,
            ))
        }
        word if word & 0xff00 == 0x1200 => Some((
            DspInstruction::StatusBit(DspStatusBitOperation::Clear {
                bit: ((word & 0x0007) as u8) + 6,
            }),
            1,
        )),
        word if word & 0xff00 == 0x1300 => Some((
            DspInstruction::StatusBit(DspStatusBitOperation::Set {
                bit: ((word & 0x0007) as u8) + 6,
            }),
            1,
        )),
        word if word & 0xffe0 == 0x0080 => {
            require_words(address, word, words, 2)?;
            Some((
                DspInstruction::RegisterTransfer(DspRegisterTransferOperation::LoadImmediate {
                    target: register_from_bits(word),
                    value: words[1],
                }),
                2,
            ))
        }
        word if word & 0xffe0 == 0x00c0 => {
            require_words(address, word, words, 2)?;
            Some((
                DspInstruction::RegisterTransfer(DspRegisterTransferOperation::LoadFromMemory {
                    target: register_from_bits(word),
                    address: words[1],
                }),
                2,
            ))
        }
        word if word & 0xffe0 == 0x00e0 => {
            require_words(address, word, words, 2)?;
            Some((
                DspInstruction::RegisterTransfer(DspRegisterTransferOperation::StoreToMemory {
                    address: words[1],
                    source: register_from_bits(word),
                }),
                2,
            ))
        }
        word if word & 0xfc00 == 0x1c00 => Some((
            DspInstruction::RegisterTransfer(DspRegisterTransferOperation::Move {
                target: register_from_bits(word >> 5),
                source: register_from_bits(word),
            }),
            1,
        )),
        word if word & 0xff00 == 0x1600 => {
            require_words(address, word, words, 2)?;
            Some((
                DspInstruction::RegisterTransfer(DspRegisterTransferOperation::StoreImmediate {
                    address: sign_extend_8_bit_data_address(word),
                    value: words[1],
                }),
                2,
            ))
        }
        _ => None,
    };

    Ok(decoded)
}

fn sign_extend_8_bit_data_address(word: u16) -> u16 {
    (i16::from(i8::from_ne_bytes([(word & 0x00ff) as u8]))) as u16
}

fn decode_immediate_or_memory_instruction(
    address: u16,
    word: u16,
    words: &[u16],
) -> Result<Option<(DspInstruction, u8)>, DspDecodeError> {
    let decoded = match word {
        word if word & 0xfe00 == 0x0400 => Some((
            DspInstruction::Immediate(DspImmediateOperation::AddShort {
                target: accumulator_from_bit(word >> 8),
                value: (word & 0x00ff) as u8,
            }),
            1,
        )),
        word if word & 0xfe00 == 0x0600 => Some((
            DspInstruction::Immediate(DspImmediateOperation::CompareShort {
                target: accumulator_from_bit(word >> 8),
                value: (word & 0x00ff) as u8,
            }),
            1,
        )),
        word if word & 0xf800 == 0x0800 => Some((
            DspInstruction::Immediate(DspImmediateOperation::LoadRegisterShort {
                target: register_from_bits(0x18 + ((word >> 8) & 0x0007)),
                value: (word & 0x00ff) as u8,
            }),
            1,
        )),
        word if word & 0xfeff == 0x0200 => {
            require_words(address, word, words, 2)?;
            Some((
                DspInstruction::Immediate(DspImmediateOperation::Add {
                    target: accumulator_from_bit(word >> 8),
                    value: words[1],
                }),
                2,
            ))
        }
        word if word & 0xfeff == 0x0220 => {
            require_words(address, word, words, 2)?;
            Some((
                DspInstruction::Immediate(DspImmediateOperation::Xor {
                    target: accumulator_from_bit(word >> 8),
                    value: words[1],
                }),
                2,
            ))
        }
        word if word & 0xfeff == 0x0240 => {
            require_words(address, word, words, 2)?;
            Some((
                DspInstruction::Immediate(DspImmediateOperation::And {
                    target: accumulator_from_bit(word >> 8),
                    value: words[1],
                }),
                2,
            ))
        }
        word if word & 0xfeff == 0x0260 => {
            require_words(address, word, words, 2)?;
            Some((
                DspInstruction::Immediate(DspImmediateOperation::Or {
                    target: accumulator_from_bit(word >> 8),
                    value: words[1],
                }),
                2,
            ))
        }
        word if word & 0xfeff == 0x0280 => {
            require_words(address, word, words, 2)?;
            Some((
                DspInstruction::Immediate(DspImmediateOperation::Compare {
                    target: accumulator_from_bit(word >> 8),
                    value: words[1],
                }),
                2,
            ))
        }
        word if word & 0xfeff == 0x02a0 => {
            require_words(address, word, words, 2)?;
            Some((
                DspInstruction::Immediate(DspImmediateOperation::AndField {
                    target: accumulator_from_bit(word >> 8),
                    value: words[1],
                }),
                2,
            ))
        }
        word if word & 0xfeff == 0x02c0 => {
            require_words(address, word, words, 2)?;
            Some((
                DspInstruction::Immediate(DspImmediateOperation::AndCompareField {
                    target: accumulator_from_bit(word >> 8),
                    value: words[1],
                }),
                2,
            ))
        }
        word if word & 0xfef0 == 0x0210 => Some((
            DspInstruction::MemoryTransfer(DspMemoryTransferOperation::LoadInstructionMemory {
                target: accumulator_from_bit(word >> 8),
                address: DspAddressRegister::from_bits(word),
                update: address_update_from_bits(word >> 2),
            }),
            1,
        )),
        word if word & 0xfe00 == 0x1800 => Some((
            DspInstruction::MemoryTransfer(DspMemoryTransferOperation::LoadDataMemory {
                target: register_from_bits(word),
                address: DspAddressRegister::from_bits(word >> 5),
                update: address_update_from_bits(word >> 7),
            }),
            1,
        )),
        word if word & 0xfe00 == 0x1a00 => Some((
            DspInstruction::MemoryTransfer(DspMemoryTransferOperation::StoreDataMemory {
                address: DspAddressRegister::from_bits(word >> 5),
                source: register_from_bits(word),
                update: address_update_from_bits(word >> 7),
            }),
            1,
        )),
        word if word & 0xf800 == 0x2000 => Some((
            DspInstruction::MemoryTransfer(DspMemoryTransferOperation::DirectPageLoad {
                target: register_from_bits(0x18 + ((word >> 8) & 0x0007)),
                address: (word & 0x00ff) as u8,
            }),
            1,
        )),
        word if word & 0xfe00 == 0x2800 => Some((
            DspInstruction::MemoryTransfer(
                DspMemoryTransferOperation::DirectPageStoreAccumulatorHigh {
                    address: (word & 0x00ff) as u8,
                    source: accumulator_from_bit(word >> 8),
                },
            ),
            1,
        )),
        word if word & 0xfc00 == 0x2c00 => Some((
            DspInstruction::MemoryTransfer(DspMemoryTransferOperation::DirectPageStoreRegister {
                address: (word & 0x00ff) as u8,
                source: register_from_bits(0x1c + ((word >> 8) & 0x0003)),
            }),
            1,
        )),
        _ => None,
    };

    Ok(decoded)
}

const fn address_update_from_bits(bits: u16) -> DspAddressUpdate {
    match bits & 0x0003 {
        0 => DspAddressUpdate::None,
        1 => DspAddressUpdate::Decrement,
        2 => DspAddressUpdate::Increment,
        _ => DspAddressUpdate::AddIndex,
    }
}

fn decode_mode_instruction(
    address: u16,
    word: u16,
) -> Result<Option<DspInstruction>, DspDecodeError> {
    let operation = match word {
        word if word & 0xf700 == 0x8000 => Some(DspModeOperation::ExtendableNop),
        word if word & 0xff00 == 0x8a00 => {
            Some(DspModeOperation::ProductMultiplyByTwo { enabled: true })
        }
        word if word & 0xff00 == 0x8b00 => {
            Some(DspModeOperation::ProductMultiplyByTwo { enabled: false })
        }
        word if word & 0xff00 == 0x8c00 => {
            Some(DspModeOperation::UnsignedMultiply { enabled: false })
        }
        word if word & 0xff00 == 0x8d00 => {
            Some(DspModeOperation::UnsignedMultiply { enabled: true })
        }
        word if word & 0xff00 == 0x8e00 => Some(DspModeOperation::Mode40Bit { enabled: false }),
        word if word & 0xff00 == 0x8f00 => Some(DspModeOperation::Mode40Bit { enabled: true }),
        _ => None,
    };

    if let Some(operation) = operation {
        if word & 0x00ff != 0 {
            return Err(DspDecodeError::UnsupportedOpcode { address, word });
        }
        return Ok(Some(DspInstruction::Mode(operation)));
    }

    Ok(None)
}

fn decode_logic_instruction(
    address: u16,
    word: u16,
) -> Result<Option<DspInstruction>, DspDecodeError> {
    let operation = match word {
        word if word & 0xfc80 == 0x3000 => Some(DspLogicOperation::Xor {
            target: accumulator_from_bit(word >> 8),
            operand: DspLogicOperand::AxHigh(ax_register_from_bit(word >> 9)),
        }),
        word if word & 0xfc80 == 0x3400 => Some(DspLogicOperation::And {
            target: accumulator_from_bit(word >> 8),
            operand: DspLogicOperand::AxHigh(ax_register_from_bit(word >> 9)),
        }),
        word if word & 0xfc80 == 0x3800 => Some(DspLogicOperation::Or {
            target: accumulator_from_bit(word >> 8),
            operand: DspLogicOperand::AxHigh(ax_register_from_bit(word >> 9)),
        }),
        word if word & 0xfe80 == 0x3080 => {
            let target = accumulator_from_bit(word >> 8);
            Some(DspLogicOperation::Xor {
                target,
                operand: DspLogicOperand::AccumulatorMid(other_accumulator(target)),
            })
        }
        word if word & 0xfe80 == 0x3280 => Some(DspLogicOperation::Not {
            target: accumulator_from_bit(word >> 8),
        }),
        word if word & 0xfe80 == 0x3c00 => {
            let target = accumulator_from_bit(word >> 8);
            Some(DspLogicOperation::And {
                target,
                operand: DspLogicOperand::AccumulatorMid(other_accumulator(target)),
            })
        }
        word if word & 0xfe80 == 0x3e00 => {
            let target = accumulator_from_bit(word >> 8);
            Some(DspLogicOperation::Or {
                target,
                operand: DspLogicOperand::AccumulatorMid(other_accumulator(target)),
            })
        }
        word if word & 0xfc80 == 0x3480 => Some(DspLogicOperation::ShiftByRegister {
            target: accumulator_from_bit(word >> 8),
            kind: DspAccumulatorShiftKind::LogicalRight,
            operand: DspLogicOperand::AxHigh(ax_register_from_bit(word >> 9)),
        }),
        word if word & 0xfc80 == 0x3880 => Some(DspLogicOperation::ShiftByRegister {
            target: accumulator_from_bit(word >> 8),
            kind: DspAccumulatorShiftKind::ArithmeticRight,
            operand: DspLogicOperand::AxHigh(ax_register_from_bit(word >> 9)),
        }),
        word if word & 0xfe80 == 0x3c80 => {
            let target = accumulator_from_bit(word >> 8);
            Some(DspLogicOperation::ShiftByRegister {
                target,
                kind: DspAccumulatorShiftKind::LogicalRight,
                operand: DspLogicOperand::AccumulatorMid(other_accumulator(target)),
            })
        }
        word if word & 0xfe80 == 0x3e80 => {
            let target = accumulator_from_bit(word >> 8);
            Some(DspLogicOperation::ShiftByRegister {
                target,
                kind: DspAccumulatorShiftKind::ArithmeticRight,
                operand: DspLogicOperand::AccumulatorMid(other_accumulator(target)),
            })
        }
        _ => None,
    };

    if let Some(operation) = operation {
        if word & 0x007f != 0 {
            return Err(DspDecodeError::UnsupportedOpcode { address, word });
        }
        return Ok(Some(DspInstruction::Logic(operation)));
    }

    Ok(None)
}

fn decode_accumulator_instruction(
    address: u16,
    word: u16,
) -> Result<Option<DspInstruction>, DspDecodeError> {
    if word & 0xfc00 == 0x1400 {
        let kind = match (word >> 6) & 0x3 {
            0 => DspAccumulatorShiftKind::LogicalLeft,
            1 => DspAccumulatorShiftKind::LogicalRight,
            2 => DspAccumulatorShiftKind::ArithmeticLeft,
            _ => DspAccumulatorShiftKind::ArithmeticRight,
        };
        return Ok(Some(DspInstruction::Accumulator(
            DspAccumulatorOperation::ShiftImmediate {
                target: accumulator_from_bit(word >> 8),
                kind,
                amount: (word & 0x003f) as u8,
            },
        )));
    }

    match word {
        0x02ca => {
            return Ok(Some(DspInstruction::Accumulator(
                DspAccumulatorOperation::ShiftByAccumulatorMid {
                    kind: DspAccumulatorShiftKind::LogicalRight,
                },
            )));
        }
        0x02cb => {
            return Ok(Some(DspInstruction::Accumulator(
                DspAccumulatorOperation::ShiftByAccumulatorMid {
                    kind: DspAccumulatorShiftKind::ArithmeticRight,
                },
            )));
        }
        _ => {}
    }

    let operation = match word {
        word if word & 0xf700 == 0x8100 => Some(DspAccumulatorOperation::Clear {
            target: accumulator_from_bit(word >> 11),
        }),
        word if word & 0xfe00 == 0xfc00 => Some(DspAccumulatorOperation::ClearLow {
            target: accumulator_from_bit(word >> 8),
        }),
        word if word & 0xf700 == 0xb100 => Some(DspAccumulatorOperation::Test {
            target: accumulator_from_bit(word >> 11),
        }),
        word if word & 0xfe00 == 0x8600 => Some(DspAccumulatorOperation::TestAxHigh {
            ax: ax_register_from_bit(word >> 8),
        }),
        word if word & 0xff00 == 0x8200 => Some(DspAccumulatorOperation::CompareAccumulators),
        word if word & 0xe700 == 0xc100 => Some(DspAccumulatorOperation::CompareWithAxHigh {
            accumulator: accumulator_from_bit(word >> 11),
            ax: ax_register_from_bit(word >> 12),
        }),
        word if word & 0xf800 == 0x4000 => Some(DspAccumulatorOperation::Add {
            target: accumulator_from_bit(word >> 8),
            operand: DspAccumulatorOperand::AxPartShifted(ax_part_from_linear_bits(word >> 9)),
        }),
        word if word & 0xfc00 == 0x4800 => Some(DspAccumulatorOperation::Add {
            target: accumulator_from_bit(word >> 8),
            operand: DspAccumulatorOperand::Ax(ax_register_from_bit(word >> 9)),
        }),
        word if word & 0xfe00 == 0x4c00 => {
            let target = accumulator_from_bit(word >> 8);
            Some(DspAccumulatorOperation::Add {
                target,
                operand: DspAccumulatorOperand::Accumulator(other_accumulator(target)),
            })
        }
        word if word & 0xfe00 == 0x4e00 => Some(DspAccumulatorOperation::Add {
            target: accumulator_from_bit(word >> 8),
            operand: DspAccumulatorOperand::Product,
        }),
        word if word & 0xfc00 == 0x7000 => Some(DspAccumulatorOperation::Add {
            target: accumulator_from_bit(word >> 8),
            operand: DspAccumulatorOperand::AxLowUnsigned(ax_register_from_bit(word >> 9)),
        }),
        word if word & 0xfe00 == 0x7400 => Some(DspAccumulatorOperation::Add {
            target: accumulator_from_bit(word >> 8),
            operand: DspAccumulatorOperand::MidUnit,
        }),
        word if word & 0xfe00 == 0x7600 => Some(DspAccumulatorOperation::Add {
            target: accumulator_from_bit(word >> 8),
            operand: DspAccumulatorOperand::One,
        }),
        word if word & 0xf800 == 0x5000 => Some(DspAccumulatorOperation::Subtract {
            target: accumulator_from_bit(word >> 8),
            operand: DspAccumulatorOperand::AxPartShifted(ax_part_from_linear_bits(word >> 9)),
        }),
        word if word & 0xfc00 == 0x5800 => Some(DspAccumulatorOperation::Subtract {
            target: accumulator_from_bit(word >> 8),
            operand: DspAccumulatorOperand::Ax(ax_register_from_bit(word >> 9)),
        }),
        word if word & 0xfe00 == 0x5c00 => {
            let target = accumulator_from_bit(word >> 8);
            Some(DspAccumulatorOperation::Subtract {
                target,
                operand: DspAccumulatorOperand::Accumulator(other_accumulator(target)),
            })
        }
        word if word & 0xfe00 == 0x5e00 => Some(DspAccumulatorOperation::Subtract {
            target: accumulator_from_bit(word >> 8),
            operand: DspAccumulatorOperand::Product,
        }),
        word if word & 0xfe00 == 0x7800 => Some(DspAccumulatorOperation::Subtract {
            target: accumulator_from_bit(word >> 8),
            operand: DspAccumulatorOperand::MidUnit,
        }),
        word if word & 0xfe00 == 0x7a00 => Some(DspAccumulatorOperation::Subtract {
            target: accumulator_from_bit(word >> 8),
            operand: DspAccumulatorOperand::One,
        }),
        word if word & 0xfe00 == 0x7c00 => Some(DspAccumulatorOperation::Negate {
            target: accumulator_from_bit(word >> 8),
        }),
        word if word & 0xf700 == 0xa100 => Some(DspAccumulatorOperation::Absolute {
            target: accumulator_from_bit(word >> 11),
        }),
        word if word & 0xf800 == 0x6000 => Some(DspAccumulatorOperation::Move {
            target: accumulator_from_bit(word >> 8),
            source: DspAccumulatorMoveSource::AxPartShifted(ax_part_from_linear_bits(word >> 9)),
        }),
        word if word & 0xfc00 == 0x6800 => Some(DspAccumulatorOperation::Move {
            target: accumulator_from_bit(word >> 8),
            source: DspAccumulatorMoveSource::Ax(ax_register_from_bit(word >> 9)),
        }),
        word if word & 0xfe00 == 0x6c00 => {
            let target = accumulator_from_bit(word >> 8);
            Some(DspAccumulatorOperation::Move {
                target,
                source: DspAccumulatorMoveSource::Accumulator(other_accumulator(target)),
            })
        }
        word if word & 0xfe00 == 0xf000 => Some(DspAccumulatorOperation::Shift16 {
            target: accumulator_from_bit(word >> 8),
            kind: DspAccumulatorShiftKind::LogicalLeft,
        }),
        word if word & 0xfe00 == 0xf400 => Some(DspAccumulatorOperation::Shift16 {
            target: accumulator_from_bit(word >> 8),
            kind: DspAccumulatorShiftKind::LogicalRight,
        }),
        word if word & 0xf700 == 0x9100 => Some(DspAccumulatorOperation::Shift16 {
            target: accumulator_from_bit(word >> 11),
            kind: DspAccumulatorShiftKind::ArithmeticRight,
        }),
        word if word & 0xfc00 == 0xf800 => {
            Some(DspAccumulatorOperation::AddProductAndAxHighClearLow {
                target: accumulator_from_bit(word >> 9),
                ax: ax_register_from_bit(word >> 8),
            })
        }
        _ => None,
    };

    if let Some(operation) = operation {
        if word & 0x00ff != 0 {
            return Err(DspDecodeError::UnsupportedOpcode { address, word });
        }
        return Ok(Some(DspInstruction::Accumulator(operation)));
    }

    Ok(None)
}

fn decode_product_instruction(
    address: u16,
    word: u16,
) -> Result<Option<DspInstruction>, DspDecodeError> {
    let operation = match word {
        word if word & 0xff00 == 0x8400 => Some(DspProductOperation::Clear),
        word if word & 0xff00 == 0x8500 => Some(DspProductOperation::Test),
        word if word & 0xfe00 == 0x6e00 => Some(DspProductOperation::MoveToAccumulator {
            target: accumulator_from_bit(word >> 8),
            mode: DspProductMoveMode::Raw,
        }),
        word if word & 0xfe00 == 0x7e00 => Some(DspProductOperation::MoveToAccumulator {
            target: accumulator_from_bit(word >> 8),
            mode: DspProductMoveMode::Negated,
        }),
        word if word & 0xfe00 == 0xfe00 => Some(DspProductOperation::MoveToAccumulator {
            target: accumulator_from_bit(word >> 8),
            mode: DspProductMoveMode::HighMidClearLow,
        }),
        word if word & 0xff00 == 0x8300 => Some(DspProductOperation::MultiplyAxHighSquared),
        word if word & 0xf700 == 0x9000 => Some(DspProductOperation::MultiplyAxPair {
            ax: ax_register_from_bit(word >> 11),
        }),
        word if word & 0xf600 == 0x9400 => {
            Some(DspProductOperation::MultiplyAxPairWithAccumulator {
                ax: ax_register_from_bit(word >> 11),
                target: accumulator_from_bit(word >> 8),
                action: DspMultiplyAccumulatorAction::AddPreviousProduct,
            })
        }
        word if word & 0xf600 == 0x9600 => {
            Some(DspProductOperation::MultiplyAxPairWithAccumulator {
                ax: ax_register_from_bit(word >> 11),
                target: accumulator_from_bit(word >> 8),
                action: DspMultiplyAccumulatorAction::MovePreviousProduct,
            })
        }
        word if word & 0xf600 == 0x9200 => {
            Some(DspProductOperation::MultiplyAxPairWithAccumulator {
                ax: ax_register_from_bit(word >> 11),
                target: accumulator_from_bit(word >> 8),
                action: DspMultiplyAccumulatorAction::MoveRoundedPreviousProduct,
            })
        }
        word if word & 0xe700 == 0xa000 => Some(DspProductOperation::MultiplyCross {
            left: DspAxPart::from_ax0_bit(word >> 12),
            right: DspAxPart::from_ax1_bit(word >> 11),
        }),
        word if word & 0xe600 == 0xa400 => {
            Some(DspProductOperation::MultiplyCrossWithAccumulator {
                left: DspAxPart::from_ax0_bit(word >> 12),
                right: DspAxPart::from_ax1_bit(word >> 11),
                target: accumulator_from_bit(word >> 8),
                action: DspMultiplyAccumulatorAction::AddPreviousProduct,
            })
        }
        word if word & 0xe600 == 0xa600 => {
            Some(DspProductOperation::MultiplyCrossWithAccumulator {
                left: DspAxPart::from_ax0_bit(word >> 12),
                right: DspAxPart::from_ax1_bit(word >> 11),
                target: accumulator_from_bit(word >> 8),
                action: DspMultiplyAccumulatorAction::MovePreviousProduct,
            })
        }
        word if word & 0xe600 == 0xa200 => {
            Some(DspProductOperation::MultiplyCrossWithAccumulator {
                left: DspAxPart::from_ax0_bit(word >> 12),
                right: DspAxPart::from_ax1_bit(word >> 11),
                target: accumulator_from_bit(word >> 8),
                action: DspMultiplyAccumulatorAction::MoveRoundedPreviousProduct,
            })
        }
        word if word & 0xe700 == 0xc000 => {
            Some(DspProductOperation::MultiplyAccumulatorMidByAxHigh {
                accumulator: accumulator_from_bit(word >> 12),
                ax: ax_register_from_bit(word >> 11),
            })
        }
        word if word & 0xe600 == 0xc400 => Some(
            DspProductOperation::MultiplyAccumulatorMidByAxHighWithAccumulator {
                accumulator: accumulator_from_bit(word >> 12),
                ax: ax_register_from_bit(word >> 11),
                target: accumulator_from_bit(word >> 8),
                action: DspMultiplyAccumulatorAction::AddPreviousProduct,
            },
        ),
        word if word & 0xe600 == 0xc600 => Some(
            DspProductOperation::MultiplyAccumulatorMidByAxHighWithAccumulator {
                accumulator: accumulator_from_bit(word >> 12),
                ax: ax_register_from_bit(word >> 11),
                target: accumulator_from_bit(word >> 8),
                action: DspMultiplyAccumulatorAction::MovePreviousProduct,
            },
        ),
        word if word & 0xe600 == 0xc200 => Some(
            DspProductOperation::MultiplyAccumulatorMidByAxHighWithAccumulator {
                accumulator: accumulator_from_bit(word >> 12),
                ax: ax_register_from_bit(word >> 11),
                target: accumulator_from_bit(word >> 8),
                action: DspMultiplyAccumulatorAction::MoveRoundedPreviousProduct,
            },
        ),
        word if word & 0xfc00 == 0xe000 => Some(DspProductOperation::AddCrossToProduct {
            left: DspAxPart::from_ax0_bit(word >> 9),
            right: DspAxPart::from_ax1_bit(word >> 8),
        }),
        word if word & 0xfc00 == 0xe400 => Some(DspProductOperation::SubtractCrossFromProduct {
            left: DspAxPart::from_ax0_bit(word >> 9),
            right: DspAxPart::from_ax1_bit(word >> 8),
        }),
        word if word & 0xfc00 == 0xe800 => {
            Some(DspProductOperation::AddAccumulatorMidByAxHighToProduct {
                accumulator: accumulator_from_bit(word >> 9),
                ax: ax_register_from_bit(word >> 8),
            })
        }
        word if word & 0xfc00 == 0xec00 => Some(
            DspProductOperation::SubtractAccumulatorMidByAxHighFromProduct {
                accumulator: accumulator_from_bit(word >> 9),
                ax: ax_register_from_bit(word >> 8),
            },
        ),
        word if word & 0xfe00 == 0xf200 => Some(DspProductOperation::AddAxPairToProduct {
            ax: ax_register_from_bit(word >> 8),
        }),
        word if word & 0xfe00 == 0xf600 => Some(DspProductOperation::SubtractAxPairFromProduct {
            ax: ax_register_from_bit(word >> 8),
        }),
        _ => None,
    };

    if let Some(operation) = operation {
        if word & 0x00ff != 0 {
            return Err(DspDecodeError::UnsupportedOpcode { address, word });
        }
        return Ok(Some(DspInstruction::Product(operation)));
    }

    Ok(None)
}

fn require_words(
    address: u16,
    word: u16,
    words: &[u16],
    expected_words: usize,
) -> Result<(), DspDecodeError> {
    if words.len() < expected_words {
        return Err(DspDecodeError::TruncatedInstruction {
            address,
            word,
            expected_words,
            available_words: words.len(),
        });
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn assert_decodes_words(words: &[u16]) {
        decode_dsp_instruction(0x0200, words)
            .unwrap_or_else(|error| panic!("expected {words:?} to decode: {error}"));
    }

    fn assert_decodes_word(word: u16) {
        assert_decodes_words(&[word]);
    }

    fn assert_ordered_substrings(haystack: &str, needles: &[&str]) {
        let mut search_start = 0;
        for needle in needles {
            let remaining = &haystack[search_start..];
            let Some(relative_position) = remaining.find(needle) else {
                panic!("expected generated C++ to contain substring after byte {search_start}: {needle}");
            };
            search_start += relative_position + needle.len();
        }
    }

    fn generated_instruction_block(cpp: &str, address: u16) -> &str {
        let marker = format!("pc_{address:04X}:\n{{\n");
        let start = cpp
            .find(&marker)
            .unwrap_or_else(|| panic!("missing generated instruction block {address:04X}"));
        let remaining = &cpp[start..];
        let end = remaining
            .find("\n}\n")
            .unwrap_or_else(|| panic!("unterminated generated instruction block {address:04X}"));
        &remaining[..end + 3]
    }

    fn sha256_bytes_from_hex(value: &str) -> [u8; 32] {
        assert_eq!(value.len(), 64);
        let mut bytes = [0u8; 32];
        for (index, output) in bytes.iter_mut().enumerate() {
            let start = index * 2;
            *output = u8::from_str_radix(&value[start..start + 2], 16)
                .expect("test SHA-256 is lowercase hexadecimal");
        }
        bytes
    }

    #[test]
    fn raw_input_aggregate_has_exact_domain_roles_and_big_endian_bytes() {
        let iram = [0x0000, 0x0021];
        let irom = [0x1234];
        let coefficient = [0xabcd];
        let components = [
            dsp_raw_input_component_identity("iram", Some(&iram)),
            dsp_raw_input_component_identity("irom", Some(&irom)),
            dsp_raw_input_component_identity("coef", Some(&coefficient)),
        ];
        let canonical = dsp_raw_input_aggregate_canonical_bytes(&components);
        let expected = concat!(
            "nebula.dsp-raw-input-aggregate.v1\n",
            "iram\t4\t83440636eff7b2ec0f78ef7b8e480a033e8aeb67e6ecb657ce9bcdfdb21aa744\n",
            "irom\t2\t3a103a4e5729ad68c02a678ae39accfbc0ae208096437401b7ceab63cca0622f\n",
            "coef\t2\t123d4c7ef2d1600a1b3a0f6addc60a10f05a3495c9409f2ecbf4cc095d000a6b\n",
        );
        assert_eq!(canonical, expected.as_bytes());
        assert_eq!(
            dsp_raw_input_aggregate_sha256(
                &iram,
                DspStaticMemoryImages {
                    irom_words: Some(&irom),
                    coefficient_words: Some(&coefficient),
                },
            ),
            "fd9cda7c7078454d50ab764c279c174d0dfa6ad0b9d7e52a42bceb0ca8eb9c84"
        );
    }

    #[test]
    fn raw_input_aggregate_encodes_missing_optional_images_as_zero_bytes() {
        let iram = [0x0021];
        let components = [
            dsp_raw_input_component_identity("iram", Some(&iram)),
            dsp_raw_input_component_identity("irom", None),
            dsp_raw_input_component_identity("coef", None),
        ];
        let canonical = String::from_utf8(dsp_raw_input_aggregate_canonical_bytes(&components))
            .expect("aggregate domain is UTF-8");

        assert_eq!(
            canonical,
            concat!(
                "nebula.dsp-raw-input-aggregate.v1\n",
                "iram\t2\t1321e1ca91757e8c23c934ff4047d69657e9caeed9ce5a29e5fcf94e7e648ca6\n",
                "irom\t0\te3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n",
                "coef\t0\te3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n",
            )
        );
        assert_ne!(
            dsp_raw_input_aggregate_sha256(&iram, DspStaticMemoryImages::default()),
            "eab7f704638798e986c4a3950567bfcf069a77551df3973624f0f49f036e7f8e"
        );
    }

    #[test]
    fn reviewed_rmge01_component_identities_produce_pinned_aggregate() {
        let components = [
            DspRawInputComponentIdentity {
                role: "iram",
                byte_len: 7936,
                sha256: sha256_bytes_from_hex(
                    "75c5859f566de81b9c1f539d63c53e629ce5465daaa208ba53714ac451106634",
                ),
            },
            DspRawInputComponentIdentity {
                role: "irom",
                byte_len: 8192,
                sha256: sha256_bytes_from_hex(
                    "4ea1fea6c649bcf9f627007bc9403d5437896c681d3e089b083263a7646cd3ae",
                ),
            },
            DspRawInputComponentIdentity {
                role: "coef",
                byte_len: 4096,
                sha256: sha256_bytes_from_hex(
                    "d7741279c2e8ec5c5fb318f8fbdd6de6bf583520d288e836a5383233a4238179",
                ),
            },
        ];
        let aggregate = format!(
            "{:x}",
            Sha256::digest(dsp_raw_input_aggregate_canonical_bytes(&components))
        );
        assert_eq!(
            aggregate,
            "eab7f704638798e986c4a3950567bfcf069a77551df3973624f0f49f036e7f8e"
        );
    }

    #[test]
    fn raw_input_aggregate_changes_for_each_input_and_optional_absence() {
        let iram = [0x1234, 0x5678];
        let irom = [0x9abc, 0xdef0];
        let coefficient = [0x1357, 0x2468];
        let full = dsp_raw_input_aggregate_sha256(
            &iram,
            DspStaticMemoryImages {
                irom_words: Some(&irom),
                coefficient_words: Some(&coefficient),
            },
        );

        let mut mutated_iram = iram;
        mutated_iram[0] ^= 0x0100;
        let mut mutated_irom = irom;
        mutated_irom[0] ^= 0x0100;
        let mut mutated_coefficient = coefficient;
        mutated_coefficient[0] ^= 0x0100;
        for changed in [
            dsp_raw_input_aggregate_sha256(
                &mutated_iram,
                DspStaticMemoryImages {
                    irom_words: Some(&irom),
                    coefficient_words: Some(&coefficient),
                },
            ),
            dsp_raw_input_aggregate_sha256(
                &iram,
                DspStaticMemoryImages {
                    irom_words: Some(&mutated_irom),
                    coefficient_words: Some(&coefficient),
                },
            ),
            dsp_raw_input_aggregate_sha256(
                &iram,
                DspStaticMemoryImages {
                    irom_words: Some(&irom),
                    coefficient_words: Some(&mutated_coefficient),
                },
            ),
            dsp_raw_input_aggregate_sha256(
                &iram,
                DspStaticMemoryImages {
                    irom_words: None,
                    coefficient_words: Some(&coefficient),
                },
            ),
            dsp_raw_input_aggregate_sha256(
                &iram,
                DspStaticMemoryImages {
                    irom_words: Some(&irom),
                    coefficient_words: None,
                },
            ),
            dsp_raw_input_aggregate_sha256(&iram, DspStaticMemoryImages::default()),
        ] {
            assert_ne!(changed, full);
            assert_ne!(
                changed,
                "eab7f704638798e986c4a3950567bfcf069a77551df3973624f0f49f036e7f8e"
            );
        }
    }

    #[test]
    fn maps_instruction_memory_regions() {
        assert_eq!(
            instruction_memory_region(0x0000),
            Ok(DspInstructionMemoryRegion::Iram { word_index: 0 })
        );
        assert_eq!(
            instruction_memory_region(0x0fff),
            Ok(DspInstructionMemoryRegion::Iram { word_index: 0x0fff })
        );
        assert_eq!(
            instruction_memory_region(0x8123),
            Ok(DspInstructionMemoryRegion::Irom { word_index: 0x0123 })
        );
        assert_eq!(
            instruction_memory_region(0x2000),
            Err(DspMemoryError::InvalidInstructionAddress { address: 0x2000 })
        );
    }

    #[test]
    fn maps_data_memory_regions() {
        assert_eq!(
            data_memory_region(0x0007),
            Ok(DspDataMemoryRegion::Dram { word_index: 0x0007 })
        );
        assert_eq!(
            data_memory_region(0x1801),
            Ok(DspDataMemoryRegion::CoefRom { word_index: 0x0001 })
        );
        assert_eq!(
            data_memory_region(0xf0fd),
            Ok(DspDataMemoryRegion::Ifx { register: 0x00fd })
        );
        assert_eq!(
            data_memory_region(0x4000),
            Err(DspMemoryError::InvalidDataAddress { address: 0x4000 })
        );
    }

    #[test]
    fn exposes_status_flag_masks() {
        assert_eq!(DspStatusFlag::Carry.mask(), 0x0001);
        assert_eq!(DspStatusFlag::StickyOverflow.mask(), 0x0080);
        assert_eq!(DspStatusFlag::MultiplyModifier.mask(), 0x2000);
        assert_eq!(DspStatusFlag::Mode40Bit.mask(), 0x4000);
        assert_eq!(DspStatusFlag::UnsignedMultiply.mask(), 0x8000);
    }

    #[test]
    fn decodes_representative_single_word_opcodes() {
        assert_eq!(
            decode_dsp_instruction(0x0000, &[0x0000])
                .unwrap()
                .instruction,
            DspInstruction::Nop
        );
        assert_eq!(
            decode_dsp_instruction(0x0001, &[0x0005])
                .unwrap()
                .instruction,
            DspInstruction::Address(DspAddressOperation::Decrement {
                address: DspAddressRegister::Ar1
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0002, &[0x000a])
                .unwrap()
                .instruction,
            DspInstruction::Address(DspAddressOperation::Increment {
                address: DspAddressRegister::Ar2
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0003, &[0x000f])
                .unwrap()
                .instruction,
            DspInstruction::Address(DspAddressOperation::SubtractIndex {
                address: DspAddressRegister::Ar3,
                index: DspIndexRegister::Ix3,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0004, &[0x001b])
                .unwrap()
                .instruction,
            DspInstruction::Address(DspAddressOperation::AddIndex {
                address: DspAddressRegister::Ar3,
                index: DspIndexRegister::Ix2,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0005, &[0x0021])
                .unwrap()
                .instruction,
            DspInstruction::Halt
        );
    }

    #[test]
    fn decodes_representative_primary_opcode_matrix() {
        for condition in 0..=0x000f {
            assert_decodes_word(0x0270 | condition);
            assert_decodes_words(&[0x0290 | condition, 0x8123]);
            assert_decodes_words(&[0x02b0 | condition, 0x8123]);
            assert_decodes_word(0x02d0 | condition);
            assert_decodes_word(0x02f0 | condition);
            assert_decodes_word(0x1700 | (0x0003 << 5) | condition);
            assert_decodes_word(0x1710 | (0x0003 << 5) | condition);
        }

        for bit in 0..=0x0007 {
            assert_decodes_word(0x1200 | bit);
            assert_decodes_word(0x1300 | bit);
        }

        for words in [
            &[0x0000][..],
            &[0x0004],
            &[0x0008],
            &[0x000c],
            &[0x0010],
            &[0x0021],
            &[0x0040],
            &[0x0060, 0x0123],
            &[0x0080, 0xabcd],
            &[0x00c0, 0x0010],
            &[0x00e0, 0x0010],
            &[0x0200, 0x0001],
            &[0x0210],
            &[0x0214],
            &[0x0218],
            &[0x021c],
            &[0x0220, 0x00ff],
            &[0x0240, 0x00ff],
            &[0x0260, 0x00ff],
            &[0x0280, 0x00ff],
            &[0x02a0, 0x00ff],
            &[0x02c0, 0x00ff],
            &[0x02ca],
            &[0x02cb],
            &[0x0400],
            &[0x0600],
            &[0x0800],
            &[0x1001],
            &[0x1101, 0x0456],
            &[0x1401],
            &[0x1441],
            &[0x1481],
            &[0x14c1],
            &[0x1600, 0xbeef],
            &[0x1800],
            &[0x1880],
            &[0x1900],
            &[0x1980],
            &[0x1a00],
            &[0x1a80],
            &[0x1b00],
            &[0x1b80],
            &[0x1c00],
            &[0x2000],
            &[0x2800],
            &[0x2c00],
            &[0x3000],
            &[0x3080],
            &[0x3280],
            &[0x3400],
            &[0x3480],
            &[0x3800],
            &[0x3880],
            &[0x3c00],
            &[0x3c80],
            &[0x3e00],
            &[0x3e80],
            &[0x4000],
            &[0x4800],
            &[0x4c00],
            &[0x4e00],
            &[0x5000],
            &[0x5800],
            &[0x5c00],
            &[0x5e00],
            &[0x6000],
            &[0x6800],
            &[0x6c00],
            &[0x6e00],
            &[0x7000],
            &[0x7400],
            &[0x7600],
            &[0x7800],
            &[0x7a00],
            &[0x7c00],
            &[0x7e00],
            &[0x8000],
            &[0x8100],
            &[0x8200],
            &[0x8300],
            &[0x8400],
            &[0x8500],
            &[0x8600],
            &[0x8a00],
            &[0x8b00],
            &[0x8c00],
            &[0x8d00],
            &[0x8e00],
            &[0x8f00],
            &[0x9000],
            &[0x9100],
            &[0x9200],
            &[0x9400],
            &[0x9600],
            &[0xa000],
            &[0xa100],
            &[0xa200],
            &[0xa400],
            &[0xa600],
            &[0xb100],
            &[0xc000],
            &[0xc100],
            &[0xc200],
            &[0xc400],
            &[0xc600],
            &[0xe000],
            &[0xe400],
            &[0xe800],
            &[0xec00],
            &[0xf000],
            &[0xf200],
            &[0xf400],
            &[0xf600],
            &[0xf800],
            &[0xfc00],
            &[0xfe00],
        ] {
            assert_decodes_words(words);
        }
    }

    #[test]
    fn decodes_representative_parallel_extension_matrix() {
        for extension in [
            0x0001, 0x0004, 0x0008, 0x000c, 0x0010, 0x0020, 0x0024, 0x0040, 0x0044, 0x0080, 0x0082,
            0x0084, 0x0086, 0x0088, 0x008a, 0x008c, 0x008e, 0x00c0, 0x00c3, 0x00c4, 0x00c7, 0x00c8,
            0x00cb, 0x00cc, 0x00cf,
        ] {
            assert_decodes_word(0x8000 | extension);
        }

        assert_decodes_word(0x3001);
        assert_decodes_word(0xbcf0);
        assert_decodes_word(0xf2e7);
    }

    #[test]
    fn decodes_control_flow_conditions() {
        assert_eq!(
            decode_dsp_instruction(0x0100, &[0x0275])
                .unwrap()
                .instruction,
            DspInstruction::If {
                condition: DspCondition::Zero
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0101, &[0x02df])
                .unwrap()
                .instruction,
            DspInstruction::Return {
                interrupt: false,
                condition: DspCondition::Always,
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0102, &[0x02f7])
                .unwrap()
                .instruction,
            DspInstruction::Return {
                interrupt: true,
                condition: DspCondition::Carry,
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0103, &[0x02b4, 0x8123])
                .unwrap()
                .instruction,
            DspInstruction::CallImmediate {
                condition: DspCondition::NotZero,
                target: 0x8123,
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0105, &[0x0278])
                .unwrap()
                .instruction,
            DspInstruction::If {
                condition: DspCondition::NotOverSigned32,
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0106, &[0x0279])
                .unwrap()
                .instruction,
            DspInstruction::If {
                condition: DspCondition::OverSigned32,
            }
        );
    }

    #[test]
    fn decodes_jump_call_and_register_indirect_control_flow() {
        assert_eq!(
            decode_dsp_instruction(0x0110, &[0x0295, 0x8123])
                .unwrap()
                .instruction,
            DspInstruction::JumpImmediate {
                condition: DspCondition::Zero,
                target: 0x8123,
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0112, &[0x029f, 0x8000])
                .unwrap()
                .instruction,
            DspInstruction::JumpImmediate {
                condition: DspCondition::Always,
                target: 0x8000,
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0114, &[0x1744])
                .unwrap()
                .instruction,
            DspInstruction::JumpRegister {
                condition: DspCondition::NotZero,
                target: DspRegister::Address(DspAddressRegister::Ar2),
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0115, &[0x17b7])
                .unwrap()
                .instruction,
            DspInstruction::CallRegister {
                condition: DspCondition::Carry,
                target: DspRegister::Index(DspIndexRegister::Ix1),
            }
        );
    }

    #[test]
    fn decodes_loop_and_status_bit_opcodes() {
        assert_eq!(
            decode_dsp_instruction(0x0120, &[0x0043])
                .unwrap()
                .instruction,
            DspInstruction::Loop {
                count: DspRegister::Address(DspAddressRegister::Ar3),
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0121, &[0x0065, 0x0234])
                .unwrap()
                .instruction,
            DspInstruction::BlockLoop {
                count: DspRegister::Index(DspIndexRegister::Ix1),
                end_address: 0x0234,
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0123, &[0x1012])
                .unwrap()
                .instruction,
            DspInstruction::LoopImmediate { count: 0x12 }
        );
        assert_eq!(
            decode_dsp_instruction(0x0124, &[0x11ff, 0x0456])
                .unwrap()
                .instruction,
            DspInstruction::BlockLoopImmediate {
                count: 0xff,
                end_address: 0x0456,
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0126, &[0x1203])
                .unwrap()
                .instruction,
            DspInstruction::StatusBit(DspStatusBitOperation::Clear { bit: 9 })
        );
        assert_eq!(
            decode_dsp_instruction(0x0127, &[0x1305])
                .unwrap()
                .instruction,
            DspInstruction::StatusBit(DspStatusBitOperation::Set { bit: 11 })
        );
    }

    #[test]
    fn decodes_mode_control_opcodes_without_parallel_extensions() {
        assert_eq!(
            decode_dsp_instruction(0x0128, &[0x8000])
                .unwrap()
                .instruction,
            DspInstruction::Mode(DspModeOperation::ExtendableNop)
        );
        assert_eq!(
            decode_dsp_instruction(0x0129, &[0x8a00])
                .unwrap()
                .instruction,
            DspInstruction::Mode(DspModeOperation::ProductMultiplyByTwo { enabled: true })
        );
        assert_eq!(
            decode_dsp_instruction(0x012a, &[0x8b00])
                .unwrap()
                .instruction,
            DspInstruction::Mode(DspModeOperation::ProductMultiplyByTwo { enabled: false })
        );
        assert_eq!(
            decode_dsp_instruction(0x012b, &[0x8c00])
                .unwrap()
                .instruction,
            DspInstruction::Mode(DspModeOperation::UnsignedMultiply { enabled: false })
        );
        assert_eq!(
            decode_dsp_instruction(0x012c, &[0x8d00])
                .unwrap()
                .instruction,
            DspInstruction::Mode(DspModeOperation::UnsignedMultiply { enabled: true })
        );
        assert_eq!(
            decode_dsp_instruction(0x012d, &[0x8e00])
                .unwrap()
                .instruction,
            DspInstruction::Mode(DspModeOperation::Mode40Bit { enabled: false })
        );
        assert_eq!(
            decode_dsp_instruction(0x012e, &[0x8f00])
                .unwrap()
                .instruction,
            DspInstruction::Mode(DspModeOperation::Mode40Bit { enabled: true })
        );
    }

    #[test]
    fn decodes_register_transfer_opcodes() {
        assert_eq!(
            decode_dsp_instruction(0x0130, &[0x0093, 0xabcd])
                .unwrap()
                .instruction,
            DspInstruction::RegisterTransfer(DspRegisterTransferOperation::LoadImmediate {
                target: DspRegister::Status,
                value: 0xabcd,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0132, &[0x00da, 0x0123])
                .unwrap()
                .instruction,
            DspInstruction::RegisterTransfer(DspRegisterTransferOperation::LoadFromMemory {
                target: DspRegister::Ax {
                    register: DspAxRegister::Ax0,
                    half: DspRegisterHalf::High,
                },
                address: 0x0123,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0134, &[0x00ff, 0x0321])
                .unwrap()
                .instruction,
            DspInstruction::RegisterTransfer(DspRegisterTransferOperation::StoreToMemory {
                address: 0x0321,
                source: DspRegister::Accumulator {
                    register: DspAccumulator::Ac1,
                    part: DspAccumulatorPart::Mid,
                },
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0136, &[0x1ef9])
                .unwrap()
                .instruction,
            DspInstruction::RegisterTransfer(DspRegisterTransferOperation::Move {
                target: DspRegister::ProductMid2,
                source: DspRegister::Ax {
                    register: DspAxRegister::Ax1,
                    half: DspRegisterHalf::Low,
                },
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0137, &[0x16fe, 0xbeef])
                .unwrap()
                .instruction,
            DspInstruction::RegisterTransfer(DspRegisterTransferOperation::StoreImmediate {
                address: 0xfffe,
                value: 0xbeef,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0139, &[0x1601, 0xbeef])
                .unwrap()
                .instruction,
            DspInstruction::RegisterTransfer(DspRegisterTransferOperation::StoreImmediate {
                address: 0x0001,
                value: 0xbeef,
            })
        );
    }

    #[test]
    fn decodes_immediate_alu_opcodes() {
        assert_eq!(
            decode_dsp_instruction(0x0140, &[0x0501])
                .unwrap()
                .instruction,
            DspInstruction::Immediate(DspImmediateOperation::AddShort {
                target: DspAccumulator::Ac1,
                value: 0x01,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0141, &[0x07fe])
                .unwrap()
                .instruction,
            DspInstruction::Immediate(DspImmediateOperation::CompareShort {
                target: DspAccumulator::Ac1,
                value: 0xfe,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0142, &[0x0a7f])
                .unwrap()
                .instruction,
            DspInstruction::Immediate(DspImmediateOperation::LoadRegisterShort {
                target: DspRegister::Ax {
                    register: DspAxRegister::Ax0,
                    half: DspRegisterHalf::High,
                },
                value: 0x7f,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0143, &[0x0300, 0x1111])
                .unwrap()
                .instruction,
            DspInstruction::Immediate(DspImmediateOperation::Add {
                target: DspAccumulator::Ac1,
                value: 0x1111,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0145, &[0x0320, 0x2222])
                .unwrap()
                .instruction,
            DspInstruction::Immediate(DspImmediateOperation::Xor {
                target: DspAccumulator::Ac1,
                value: 0x2222,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0147, &[0x0340, 0x3333])
                .unwrap()
                .instruction,
            DspInstruction::Immediate(DspImmediateOperation::And {
                target: DspAccumulator::Ac1,
                value: 0x3333,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0149, &[0x0360, 0x4444])
                .unwrap()
                .instruction,
            DspInstruction::Immediate(DspImmediateOperation::Or {
                target: DspAccumulator::Ac1,
                value: 0x4444,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x014b, &[0x0380, 0x5555])
                .unwrap()
                .instruction,
            DspInstruction::Immediate(DspImmediateOperation::Compare {
                target: DspAccumulator::Ac1,
                value: 0x5555,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x014d, &[0x03a0, 0x6666])
                .unwrap()
                .instruction,
            DspInstruction::Immediate(DspImmediateOperation::AndField {
                target: DspAccumulator::Ac1,
                value: 0x6666,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x014f, &[0x03c0, 0x7777])
                .unwrap()
                .instruction,
            DspInstruction::Immediate(DspImmediateOperation::AndCompareField {
                target: DspAccumulator::Ac1,
                value: 0x7777,
            })
        );
    }

    #[test]
    fn decodes_instruction_memory_load_opcodes() {
        assert_eq!(
            decode_dsp_instruction(0x0160, &[0x0212])
                .unwrap()
                .instruction,
            DspInstruction::MemoryTransfer(DspMemoryTransferOperation::LoadInstructionMemory {
                target: DspAccumulator::Ac0,
                address: DspAddressRegister::Ar2,
                update: DspAddressUpdate::None,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0161, &[0x0317])
                .unwrap()
                .instruction,
            DspInstruction::MemoryTransfer(DspMemoryTransferOperation::LoadInstructionMemory {
                target: DspAccumulator::Ac1,
                address: DspAddressRegister::Ar3,
                update: DspAddressUpdate::Decrement,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0162, &[0x0219])
                .unwrap()
                .instruction,
            DspInstruction::MemoryTransfer(DspMemoryTransferOperation::LoadInstructionMemory {
                target: DspAccumulator::Ac0,
                address: DspAddressRegister::Ar1,
                update: DspAddressUpdate::Increment,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0163, &[0x031c])
                .unwrap()
                .instruction,
            DspInstruction::MemoryTransfer(DspMemoryTransferOperation::LoadInstructionMemory {
                target: DspAccumulator::Ac1,
                address: DspAddressRegister::Ar0,
                update: DspAddressUpdate::AddIndex,
            })
        );
    }

    #[test]
    fn decodes_indirect_data_memory_load_store_opcodes() {
        assert_eq!(
            decode_dsp_instruction(0x0170, &[0x1853])
                .unwrap()
                .instruction,
            DspInstruction::MemoryTransfer(DspMemoryTransferOperation::LoadDataMemory {
                target: DspRegister::Status,
                address: DspAddressRegister::Ar2,
                update: DspAddressUpdate::None,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0171, &[0x18fb])
                .unwrap()
                .instruction,
            DspInstruction::MemoryTransfer(DspMemoryTransferOperation::LoadDataMemory {
                target: DspRegister::Ax {
                    register: DspAxRegister::Ax1,
                    half: DspRegisterHalf::High,
                },
                address: DspAddressRegister::Ar3,
                update: DspAddressUpdate::Decrement,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0172, &[0x1936])
                .unwrap()
                .instruction,
            DspInstruction::MemoryTransfer(DspMemoryTransferOperation::LoadDataMemory {
                target: DspRegister::ProductHigh,
                address: DspAddressRegister::Ar1,
                update: DspAddressUpdate::Increment,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0173, &[0x199e])
                .unwrap()
                .instruction,
            DspInstruction::MemoryTransfer(DspMemoryTransferOperation::LoadDataMemory {
                target: DspRegister::Accumulator {
                    register: DspAccumulator::Ac0,
                    part: DspAccumulatorPart::Mid,
                },
                address: DspAddressRegister::Ar0,
                update: DspAddressUpdate::AddIndex,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0174, &[0x1a53])
                .unwrap()
                .instruction,
            DspInstruction::MemoryTransfer(DspMemoryTransferOperation::StoreDataMemory {
                address: DspAddressRegister::Ar2,
                source: DspRegister::Status,
                update: DspAddressUpdate::None,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0175, &[0x1af8])
                .unwrap()
                .instruction,
            DspInstruction::MemoryTransfer(DspMemoryTransferOperation::StoreDataMemory {
                address: DspAddressRegister::Ar3,
                source: DspRegister::Ax {
                    register: DspAxRegister::Ax0,
                    half: DspRegisterHalf::Low,
                },
                update: DspAddressUpdate::Decrement,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0176, &[0x1b34])
                .unwrap()
                .instruction,
            DspInstruction::MemoryTransfer(DspMemoryTransferOperation::StoreDataMemory {
                address: DspAddressRegister::Ar1,
                source: DspRegister::ProductLow,
                update: DspAddressUpdate::Increment,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0177, &[0x1b9f])
                .unwrap()
                .instruction,
            DspInstruction::MemoryTransfer(DspMemoryTransferOperation::StoreDataMemory {
                address: DspAddressRegister::Ar0,
                source: DspRegister::Accumulator {
                    register: DspAccumulator::Ac1,
                    part: DspAccumulatorPart::Mid,
                },
                update: DspAddressUpdate::AddIndex,
            })
        );
    }

    #[test]
    fn decodes_direct_page_memory_load_store_opcodes() {
        assert_eq!(
            decode_dsp_instruction(0x0180, &[0x2304])
                .unwrap()
                .instruction,
            DspInstruction::MemoryTransfer(DspMemoryTransferOperation::DirectPageLoad {
                target: DspRegister::Ax {
                    register: DspAxRegister::Ax1,
                    half: DspRegisterHalf::High,
                },
                address: 0x04,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0181, &[0x29aa])
                .unwrap()
                .instruction,
            DspInstruction::MemoryTransfer(
                DspMemoryTransferOperation::DirectPageStoreAccumulatorHigh {
                    address: 0xaa,
                    source: DspAccumulator::Ac1,
                },
            )
        );
        assert_eq!(
            decode_dsp_instruction(0x0182, &[0x2f55])
                .unwrap()
                .instruction,
            DspInstruction::MemoryTransfer(DspMemoryTransferOperation::DirectPageStoreRegister {
                address: 0x55,
                source: DspRegister::Accumulator {
                    register: DspAccumulator::Ac1,
                    part: DspAccumulatorPart::Mid,
                },
            })
        );
    }

    #[test]
    fn decoded_memory_operands_are_exhaustive_and_decoder_authoritative() {
        use DspDecodedMemoryAddressSource::{AddressRegister, DirectPage, Static};
        use DspDecodedMemoryDirection::{Read, Write};
        use DspDecodedMemorySlot::{ParallelPrimary, ParallelSecondary, Standalone};
        use DspDecodedMemorySpace::{Data, Instruction};

        let operand = |slot, space, direction, address_source| DspDecodedMemoryOperand {
            slot,
            space,
            direction,
            address_source,
        };
        let cases: &[(&[u16], &[DspDecodedMemoryOperand])] = &[
            (&[0x0000], &[]),
            (
                &[0x00da, 0x0123],
                &[operand(Standalone, Data, Read, Static(0x0123))],
            ),
            (
                &[0x00ff, 0x0321],
                &[operand(Standalone, Data, Write, Static(0x0321))],
            ),
            (
                &[0x16fe, 0xbeef],
                &[operand(Standalone, Data, Write, Static(0xfffe))],
            ),
            (
                &[0x0212],
                &[operand(
                    Standalone,
                    Instruction,
                    Read,
                    AddressRegister(DspAddressRegister::Ar2),
                )],
            ),
            (
                &[0x1853],
                &[operand(
                    Standalone,
                    Data,
                    Read,
                    AddressRegister(DspAddressRegister::Ar2),
                )],
            ),
            (
                &[0x1a53],
                &[operand(
                    Standalone,
                    Data,
                    Write,
                    AddressRegister(DspAddressRegister::Ar2),
                )],
            ),
            (
                &[0x2304],
                &[operand(Standalone, Data, Read, DirectPage(0x04))],
            ),
            (
                &[0x29aa],
                &[operand(Standalone, Data, Write, DirectPage(0xaa))],
            ),
            (
                &[0x2f55],
                &[operand(Standalone, Data, Write, DirectPage(0x55))],
            ),
            (
                &[0x803b],
                &[operand(
                    ParallelPrimary,
                    Data,
                    Write,
                    AddressRegister(DspAddressRegister::Ar3),
                )],
            ),
            (
                &[0x807b],
                &[operand(
                    ParallelPrimary,
                    Data,
                    Read,
                    AddressRegister(DspAddressRegister::Ar3),
                )],
            ),
            (
                &[0x80ac],
                &[
                    operand(
                        ParallelPrimary,
                        Data,
                        Read,
                        AddressRegister(DspAddressRegister::Ar0),
                    ),
                    operand(
                        ParallelSecondary,
                        Data,
                        Write,
                        AddressRegister(DspAddressRegister::Ar3),
                    ),
                ],
            ),
            (
                &[0x80ae],
                &[
                    operand(
                        ParallelPrimary,
                        Data,
                        Write,
                        AddressRegister(DspAddressRegister::Ar0),
                    ),
                    operand(
                        ParallelSecondary,
                        Data,
                        Read,
                        AddressRegister(DspAddressRegister::Ar3),
                    ),
                ],
            ),
            (
                &[0x80d7],
                &[
                    operand(
                        ParallelPrimary,
                        Data,
                        Read,
                        AddressRegister(DspAddressRegister::Ar0),
                    ),
                    operand(
                        ParallelSecondary,
                        Data,
                        Read,
                        AddressRegister(DspAddressRegister::Ar3),
                    ),
                ],
            ),
            (
                &[0x80ec],
                &[
                    operand(
                        ParallelPrimary,
                        Data,
                        Read,
                        AddressRegister(DspAddressRegister::Ar0),
                    ),
                    operand(
                        ParallelSecondary,
                        Data,
                        Read,
                        AddressRegister(DspAddressRegister::Ar3),
                    ),
                ],
            ),
        ];

        for (words, expected) in cases {
            let decoded = decode_dsp_instruction(0, words).unwrap();
            assert_eq!(
                decoded_memory_operands(decoded.instruction),
                *expected,
                "words={words:04X?}"
            );
        }
    }

    #[test]
    fn decodes_logic_and_register_shift_opcodes_without_parallel_extensions() {
        assert_eq!(
            decode_dsp_instruction(0x0190, &[0x3300])
                .unwrap()
                .instruction,
            DspInstruction::Logic(DspLogicOperation::Xor {
                target: DspAccumulator::Ac1,
                operand: DspLogicOperand::AxHigh(DspAxRegister::Ax1),
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0191, &[0x3700])
                .unwrap()
                .instruction,
            DspInstruction::Logic(DspLogicOperation::And {
                target: DspAccumulator::Ac1,
                operand: DspLogicOperand::AxHigh(DspAxRegister::Ax1),
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0192, &[0x3b00])
                .unwrap()
                .instruction,
            DspInstruction::Logic(DspLogicOperation::Or {
                target: DspAccumulator::Ac1,
                operand: DspLogicOperand::AxHigh(DspAxRegister::Ax1),
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0193, &[0x3d00])
                .unwrap()
                .instruction,
            DspInstruction::Logic(DspLogicOperation::And {
                target: DspAccumulator::Ac1,
                operand: DspLogicOperand::AccumulatorMid(DspAccumulator::Ac0),
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0194, &[0x3f00])
                .unwrap()
                .instruction,
            DspInstruction::Logic(DspLogicOperation::Or {
                target: DspAccumulator::Ac1,
                operand: DspLogicOperand::AccumulatorMid(DspAccumulator::Ac0),
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0195, &[0x3180])
                .unwrap()
                .instruction,
            DspInstruction::Logic(DspLogicOperation::Xor {
                target: DspAccumulator::Ac1,
                operand: DspLogicOperand::AccumulatorMid(DspAccumulator::Ac0),
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0196, &[0x3380])
                .unwrap()
                .instruction,
            DspInstruction::Logic(DspLogicOperation::Not {
                target: DspAccumulator::Ac1,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0197, &[0x3780])
                .unwrap()
                .instruction,
            DspInstruction::Logic(DspLogicOperation::ShiftByRegister {
                target: DspAccumulator::Ac1,
                kind: DspAccumulatorShiftKind::LogicalRight,
                operand: DspLogicOperand::AxHigh(DspAxRegister::Ax1),
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0198, &[0x3b80])
                .unwrap()
                .instruction,
            DspInstruction::Logic(DspLogicOperation::ShiftByRegister {
                target: DspAccumulator::Ac1,
                kind: DspAccumulatorShiftKind::ArithmeticRight,
                operand: DspLogicOperand::AxHigh(DspAxRegister::Ax1),
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0199, &[0x3d80])
                .unwrap()
                .instruction,
            DspInstruction::Logic(DspLogicOperation::ShiftByRegister {
                target: DspAccumulator::Ac1,
                kind: DspAccumulatorShiftKind::LogicalRight,
                operand: DspLogicOperand::AccumulatorMid(DspAccumulator::Ac0),
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x019a, &[0x3f80])
                .unwrap()
                .instruction,
            DspInstruction::Logic(DspLogicOperation::ShiftByRegister {
                target: DspAccumulator::Ac1,
                kind: DspAccumulatorShiftKind::ArithmeticRight,
                operand: DspLogicOperand::AccumulatorMid(DspAccumulator::Ac0),
            })
        );
    }

    #[test]
    fn decodes_delroth_extended_examples_as_bundles() {
        assert_eq!(
            decode_dsp_instruction(0x0200, &[0xbcf0])
                .unwrap()
                .instruction,
            DspInstruction::Parallel {
                primary: DspParallelPrimary::Product(
                    DspProductOperation::MultiplyCrossWithAccumulator {
                        left: DspAxPart::new(DspAxRegister::Ax0, DspRegisterHalf::High),
                        right: DspAxPart::new(DspAxRegister::Ax1, DspRegisterHalf::High),
                        target: DspAccumulator::Ac0,
                        action: DspMultiplyAccumulatorAction::AddPreviousProduct,
                    },
                ),
                parallel: DspParallelOperation::LoadAxParts {
                    ax0_half: DspRegisterHalf::High,
                    ax1_half: DspRegisterHalf::High,
                    address: DspAddressRegister::Ar0,
                    ar_update: DspAddressUpdate::Increment,
                    ar3_update: DspAddressUpdate::Increment,
                },
                raw_word: 0xbcf0,
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0201, &[0xf2e7])
                .unwrap()
                .instruction,
            DspInstruction::Parallel {
                primary: DspParallelPrimary::Product(DspProductOperation::AddAxPairToProduct {
                    ax: DspAxRegister::Ax0,
                }),
                parallel: DspParallelOperation::LoadAxPair {
                    ax: DspAxRegister::Ax0,
                    address: DspAddressRegister::Ar1,
                    ar_update: DspAddressUpdate::AddIndex,
                    ar3_update: DspAddressUpdate::Increment,
                },
                raw_word: 0xf2e7,
            }
        );
    }

    #[test]
    fn decodes_parallel_extension_slot_families() {
        assert_eq!(
            decode_dsp_instruction(0x0210, &[0x8007])
                .unwrap()
                .instruction,
            DspInstruction::Parallel {
                primary: DspParallelPrimary::Mode(DspModeOperation::ExtendableNop),
                parallel: DspParallelOperation::Address(DspAddressOperation::Decrement {
                    address: DspAddressRegister::Ar3,
                }),
                raw_word: 0x8007,
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0211, &[0x800a])
                .unwrap()
                .instruction,
            DspInstruction::Parallel {
                primary: DspParallelPrimary::Mode(DspModeOperation::ExtendableNop),
                parallel: DspParallelOperation::Address(DspAddressOperation::Increment {
                    address: DspAddressRegister::Ar2,
                }),
                raw_word: 0x800a,
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0212, &[0x800d])
                .unwrap()
                .instruction,
            DspInstruction::Parallel {
                primary: DspParallelPrimary::Mode(DspModeOperation::ExtendableNop),
                parallel: DspParallelOperation::Address(DspAddressOperation::AddIndex {
                    address: DspAddressRegister::Ar1,
                    index: DspIndexRegister::Ix1,
                }),
                raw_word: 0x800d,
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0213, &[0x801f])
                .unwrap()
                .instruction,
            DspInstruction::Parallel {
                primary: DspParallelPrimary::Mode(DspModeOperation::ExtendableNop),
                parallel: DspParallelOperation::Move {
                    target: DspRegister::Ax {
                        register: DspAxRegister::Ax1,
                        half: DspRegisterHalf::High,
                    },
                    source: DspRegister::Accumulator {
                        register: DspAccumulator::Ac1,
                        part: DspAccumulatorPart::Mid,
                    },
                },
                raw_word: 0x801f,
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0214, &[0x803b])
                .unwrap()
                .instruction,
            DspInstruction::Parallel {
                primary: DspParallelPrimary::Mode(DspModeOperation::ExtendableNop),
                parallel: DspParallelOperation::StoreSingle {
                    address: DspAddressRegister::Ar3,
                    source: DspRegister::Accumulator {
                        register: DspAccumulator::Ac1,
                        part: DspAccumulatorPart::Mid,
                    },
                    update: DspAddressUpdate::Increment,
                },
                raw_word: 0x803b,
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0215, &[0x8027])
                .unwrap()
                .instruction,
            DspInstruction::Parallel {
                primary: DspParallelPrimary::Mode(DspModeOperation::ExtendableNop),
                parallel: DspParallelOperation::StoreSingle {
                    address: DspAddressRegister::Ar3,
                    source: DspRegister::Accumulator {
                        register: DspAccumulator::Ac0,
                        part: DspAccumulatorPart::Low,
                    },
                    update: DspAddressUpdate::AddIndex,
                },
                raw_word: 0x8027,
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0216, &[0x807b])
                .unwrap()
                .instruction,
            DspInstruction::Parallel {
                primary: DspParallelPrimary::Mode(DspModeOperation::ExtendableNop),
                parallel: DspParallelOperation::LoadSingle {
                    target: DspRegister::Accumulator {
                        register: DspAccumulator::Ac1,
                        part: DspAccumulatorPart::Mid,
                    },
                    address: DspAddressRegister::Ar3,
                    update: DspAddressUpdate::Increment,
                },
                raw_word: 0x807b,
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0217, &[0x8067])
                .unwrap()
                .instruction,
            DspInstruction::Parallel {
                primary: DspParallelPrimary::Mode(DspModeOperation::ExtendableNop),
                parallel: DspParallelOperation::LoadSingle {
                    target: DspRegister::Accumulator {
                        register: DspAccumulator::Ac0,
                        part: DspAccumulatorPart::Low,
                    },
                    address: DspAddressRegister::Ar3,
                    update: DspAddressUpdate::AddIndex,
                },
                raw_word: 0x8067,
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0218, &[0x80ae])
                .unwrap()
                .instruction,
            DspInstruction::Parallel {
                primary: DspParallelPrimary::Mode(DspModeOperation::ExtendableNop),
                parallel: DspParallelOperation::AccumulatorMidLoadStore {
                    order: DspParallelLoadStoreOrder::StoreThenLoad,
                    target: DspRegister::Ax {
                        register: DspAxRegister::Ax0,
                        half: DspRegisterHalf::High,
                    },
                    accumulator: DspAccumulator::Ac0,
                    ar0_update: DspAddressUpdate::AddIndex,
                    ar3_update: DspAddressUpdate::AddIndex,
                },
                raw_word: 0x80ae,
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0219, &[0x80d7])
                .unwrap()
                .instruction,
            DspInstruction::Parallel {
                primary: DspParallelPrimary::Mode(DspModeOperation::ExtendableNop),
                parallel: DspParallelOperation::LoadAxPair {
                    ax: DspAxRegister::Ax1,
                    address: DspAddressRegister::Ar0,
                    ar_update: DspAddressUpdate::AddIndex,
                    ar3_update: DspAddressUpdate::Increment,
                },
                raw_word: 0x80d7,
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x021a, &[0x80ec])
                .unwrap()
                .instruction,
            DspInstruction::Parallel {
                primary: DspParallelPrimary::Mode(DspModeOperation::ExtendableNop),
                parallel: DspParallelOperation::LoadAxParts {
                    ax0_half: DspRegisterHalf::High,
                    ax1_half: DspRegisterHalf::Low,
                    address: DspAddressRegister::Ar0,
                    ar_update: DspAddressUpdate::AddIndex,
                    ar3_update: DspAddressUpdate::AddIndex,
                },
                raw_word: 0x80ec,
            }
        );
    }

    #[test]
    fn decodes_product_control_opcodes_without_parallel_extensions() {
        assert_eq!(
            decode_dsp_instruction(0x0300, &[0x8400])
                .unwrap()
                .instruction,
            DspInstruction::Product(DspProductOperation::Clear)
        );
        assert_eq!(
            decode_dsp_instruction(0x0301, &[0x8500])
                .unwrap()
                .instruction,
            DspInstruction::Product(DspProductOperation::Test)
        );
        assert_eq!(
            decode_dsp_instruction(0x0302, &[0x6f00])
                .unwrap()
                .instruction,
            DspInstruction::Product(DspProductOperation::MoveToAccumulator {
                target: DspAccumulator::Ac1,
                mode: DspProductMoveMode::Raw,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0303, &[0x7e00])
                .unwrap()
                .instruction,
            DspInstruction::Product(DspProductOperation::MoveToAccumulator {
                target: DspAccumulator::Ac0,
                mode: DspProductMoveMode::Negated,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0304, &[0xff00])
                .unwrap()
                .instruction,
            DspInstruction::Product(DspProductOperation::MoveToAccumulator {
                target: DspAccumulator::Ac1,
                mode: DspProductMoveMode::HighMidClearLow,
            })
        );
    }

    #[test]
    fn decodes_product_multiply_opcodes_without_parallel_extensions() {
        assert_eq!(
            decode_dsp_instruction(0x0310, &[0x8300])
                .unwrap()
                .instruction,
            DspInstruction::Product(DspProductOperation::MultiplyAxHighSquared)
        );
        assert_eq!(
            decode_dsp_instruction(0x0311, &[0x9800])
                .unwrap()
                .instruction,
            DspInstruction::Product(DspProductOperation::MultiplyAxPair {
                ax: DspAxRegister::Ax1,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0312, &[0x9500])
                .unwrap()
                .instruction,
            DspInstruction::Product(DspProductOperation::MultiplyAxPairWithAccumulator {
                ax: DspAxRegister::Ax0,
                target: DspAccumulator::Ac1,
                action: DspMultiplyAccumulatorAction::AddPreviousProduct,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0313, &[0x9f00])
                .unwrap()
                .instruction,
            DspInstruction::Product(DspProductOperation::MultiplyAxPairWithAccumulator {
                ax: DspAxRegister::Ax1,
                target: DspAccumulator::Ac1,
                action: DspMultiplyAccumulatorAction::MovePreviousProduct,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0314, &[0x9300])
                .unwrap()
                .instruction,
            DspInstruction::Product(DspProductOperation::MultiplyAxPairWithAccumulator {
                ax: DspAxRegister::Ax0,
                target: DspAccumulator::Ac1,
                action: DspMultiplyAccumulatorAction::MoveRoundedPreviousProduct,
            })
        );
    }

    #[test]
    fn decodes_cross_and_accumulator_mid_multiply_opcodes() {
        assert_eq!(
            decode_dsp_instruction(0x0320, &[0xb800])
                .unwrap()
                .instruction,
            DspInstruction::Product(DspProductOperation::MultiplyCross {
                left: DspAxPart::new(DspAxRegister::Ax0, DspRegisterHalf::High),
                right: DspAxPart::new(DspAxRegister::Ax1, DspRegisterHalf::High),
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0321, &[0xad00])
                .unwrap()
                .instruction,
            DspInstruction::Product(DspProductOperation::MultiplyCrossWithAccumulator {
                left: DspAxPart::new(DspAxRegister::Ax0, DspRegisterHalf::Low),
                right: DspAxPart::new(DspAxRegister::Ax1, DspRegisterHalf::High),
                target: DspAccumulator::Ac1,
                action: DspMultiplyAccumulatorAction::AddPreviousProduct,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0322, &[0xd800])
                .unwrap()
                .instruction,
            DspInstruction::Product(DspProductOperation::MultiplyAccumulatorMidByAxHigh {
                accumulator: DspAccumulator::Ac1,
                ax: DspAxRegister::Ax1,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0323, &[0xcd00])
                .unwrap()
                .instruction,
            DspInstruction::Product(
                DspProductOperation::MultiplyAccumulatorMidByAxHighWithAccumulator {
                    accumulator: DspAccumulator::Ac0,
                    ax: DspAxRegister::Ax1,
                    target: DspAccumulator::Ac1,
                    action: DspMultiplyAccumulatorAction::AddPreviousProduct,
                }
            )
        );
    }

    #[test]
    fn decodes_product_accumulate_opcodes_without_parallel_extensions() {
        assert_eq!(
            decode_dsp_instruction(0x0330, &[0xe300])
                .unwrap()
                .instruction,
            DspInstruction::Product(DspProductOperation::AddCrossToProduct {
                left: DspAxPart::new(DspAxRegister::Ax0, DspRegisterHalf::High),
                right: DspAxPart::new(DspAxRegister::Ax1, DspRegisterHalf::High),
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0331, &[0xe700])
                .unwrap()
                .instruction,
            DspInstruction::Product(DspProductOperation::SubtractCrossFromProduct {
                left: DspAxPart::new(DspAxRegister::Ax0, DspRegisterHalf::High),
                right: DspAxPart::new(DspAxRegister::Ax1, DspRegisterHalf::High),
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0332, &[0xeb00])
                .unwrap()
                .instruction,
            DspInstruction::Product(DspProductOperation::AddAccumulatorMidByAxHighToProduct {
                accumulator: DspAccumulator::Ac1,
                ax: DspAxRegister::Ax1,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0333, &[0xef00])
                .unwrap()
                .instruction,
            DspInstruction::Product(
                DspProductOperation::SubtractAccumulatorMidByAxHighFromProduct {
                    accumulator: DspAccumulator::Ac1,
                    ax: DspAxRegister::Ax1,
                }
            )
        );
        assert_eq!(
            decode_dsp_instruction(0x0334, &[0xf300])
                .unwrap()
                .instruction,
            DspInstruction::Product(DspProductOperation::AddAxPairToProduct {
                ax: DspAxRegister::Ax1,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0335, &[0xf700])
                .unwrap()
                .instruction,
            DspInstruction::Product(DspProductOperation::SubtractAxPairFromProduct {
                ax: DspAxRegister::Ax1,
            })
        );
    }

    #[test]
    fn decodes_accumulator_status_and_compare_opcodes_without_parallel_extensions() {
        assert_eq!(
            decode_dsp_instruction(0x0340, &[0x8100])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::Clear {
                target: DspAccumulator::Ac0,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0341, &[0x8900])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::Clear {
                target: DspAccumulator::Ac1,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0342, &[0xfd00])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::ClearLow {
                target: DspAccumulator::Ac1,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0343, &[0xb900])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::Test {
                target: DspAccumulator::Ac1,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0344, &[0x8700])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::TestAxHigh {
                ax: DspAxRegister::Ax1,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0345, &[0x8200])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::CompareAccumulators)
        );
        assert_eq!(
            decode_dsp_instruction(0x0346, &[0xd900])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::CompareWithAxHigh {
                accumulator: DspAccumulator::Ac1,
                ax: DspAxRegister::Ax1,
            })
        );
    }

    #[test]
    fn decodes_accumulator_add_subtract_opcodes_without_parallel_extensions() {
        assert_eq!(
            decode_dsp_instruction(0x0350, &[0x4700])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::Add {
                target: DspAccumulator::Ac1,
                operand: DspAccumulatorOperand::AxPartShifted(DspAxPart::new(
                    DspAxRegister::Ax1,
                    DspRegisterHalf::High,
                )),
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0351, &[0x4b00])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::Add {
                target: DspAccumulator::Ac1,
                operand: DspAccumulatorOperand::Ax(DspAxRegister::Ax1),
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0352, &[0x4d00])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::Add {
                target: DspAccumulator::Ac1,
                operand: DspAccumulatorOperand::Accumulator(DspAccumulator::Ac0),
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0353, &[0x4f00])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::Add {
                target: DspAccumulator::Ac1,
                operand: DspAccumulatorOperand::Product,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0354, &[0x7300])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::Add {
                target: DspAccumulator::Ac1,
                operand: DspAccumulatorOperand::AxLowUnsigned(DspAxRegister::Ax1),
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0355, &[0x7500])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::Add {
                target: DspAccumulator::Ac1,
                operand: DspAccumulatorOperand::MidUnit,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0356, &[0x7700])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::Add {
                target: DspAccumulator::Ac1,
                operand: DspAccumulatorOperand::One,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0357, &[0x5700])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::Subtract {
                target: DspAccumulator::Ac1,
                operand: DspAccumulatorOperand::AxPartShifted(DspAxPart::new(
                    DspAxRegister::Ax1,
                    DspRegisterHalf::High,
                )),
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0358, &[0x5b00])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::Subtract {
                target: DspAccumulator::Ac1,
                operand: DspAccumulatorOperand::Ax(DspAxRegister::Ax1),
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0359, &[0x5d00])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::Subtract {
                target: DspAccumulator::Ac1,
                operand: DspAccumulatorOperand::Accumulator(DspAccumulator::Ac0),
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x035a, &[0x5f00])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::Subtract {
                target: DspAccumulator::Ac1,
                operand: DspAccumulatorOperand::Product,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x035b, &[0x7900])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::Subtract {
                target: DspAccumulator::Ac1,
                operand: DspAccumulatorOperand::MidUnit,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x035c, &[0x7b00])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::Subtract {
                target: DspAccumulator::Ac1,
                operand: DspAccumulatorOperand::One,
            })
        );
    }

    #[test]
    fn decodes_accumulator_unary_move_and_shift_opcodes_without_parallel_extensions() {
        assert_eq!(
            decode_dsp_instruction(0x0360, &[0x7d00])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::Negate {
                target: DspAccumulator::Ac1,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0361, &[0xa900])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::Absolute {
                target: DspAccumulator::Ac1,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0362, &[0x6700])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::Move {
                target: DspAccumulator::Ac1,
                source: DspAccumulatorMoveSource::AxPartShifted(DspAxPart::new(
                    DspAxRegister::Ax1,
                    DspRegisterHalf::High,
                )),
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0363, &[0x6b00])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::Move {
                target: DspAccumulator::Ac1,
                source: DspAccumulatorMoveSource::Ax(DspAxRegister::Ax1),
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0364, &[0x6d00])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::Move {
                target: DspAccumulator::Ac1,
                source: DspAccumulatorMoveSource::Accumulator(DspAccumulator::Ac0),
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0365, &[0x151f])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::ShiftImmediate {
                target: DspAccumulator::Ac1,
                kind: DspAccumulatorShiftKind::LogicalLeft,
                amount: 31,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0366, &[0x1541])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::ShiftImmediate {
                target: DspAccumulator::Ac1,
                kind: DspAccumulatorShiftKind::LogicalRight,
                amount: 1,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0367, &[0x1582])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::ShiftImmediate {
                target: DspAccumulator::Ac1,
                kind: DspAccumulatorShiftKind::ArithmeticLeft,
                amount: 2,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0368, &[0x15c3])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::ShiftImmediate {
                target: DspAccumulator::Ac1,
                kind: DspAccumulatorShiftKind::ArithmeticRight,
                amount: 3,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0369, &[0xf100])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::Shift16 {
                target: DspAccumulator::Ac1,
                kind: DspAccumulatorShiftKind::LogicalLeft,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x036a, &[0xf500])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::Shift16 {
                target: DspAccumulator::Ac1,
                kind: DspAccumulatorShiftKind::LogicalRight,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x036b, &[0x9900])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::Shift16 {
                target: DspAccumulator::Ac1,
                kind: DspAccumulatorShiftKind::ArithmeticRight,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x036c, &[0x02ca])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::ShiftByAccumulatorMid {
                kind: DspAccumulatorShiftKind::LogicalRight,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x036d, &[0x02cb])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::ShiftByAccumulatorMid {
                kind: DspAccumulatorShiftKind::ArithmeticRight,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x036e, &[0xfb00])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::AddProductAndAxHighClearLow {
                target: DspAccumulator::Ac1,
                ax: DspAxRegister::Ax1,
            })
        );
    }

    #[test]
    fn hard_fails_unknown_opcode() {
        assert_eq!(
            decode_dsp_instruction(0x0333, &[0x03ff]),
            Err(DspDecodeError::UnsupportedOpcode {
                address: 0x0333,
                word: 0x03ff,
            })
        );
    }

    #[test]
    fn decodes_parallel_nop_extension_for_product_and_accumulator_primaries() {
        assert_eq!(
            decode_dsp_instruction(0x0440, &[0x8401])
                .unwrap()
                .instruction,
            DspInstruction::Parallel {
                primary: DspParallelPrimary::Product(DspProductOperation::Clear),
                parallel: DspParallelOperation::Nop { raw_extension: 1 },
                raw_word: 0x8401,
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0441, &[0xf201])
                .unwrap()
                .instruction,
            DspInstruction::Parallel {
                primary: DspParallelPrimary::Product(DspProductOperation::AddAxPairToProduct {
                    ax: DspAxRegister::Ax0,
                }),
                parallel: DspParallelOperation::Nop { raw_extension: 1 },
                raw_word: 0xf201,
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0442, &[0x8101])
                .unwrap()
                .instruction,
            DspInstruction::Parallel {
                primary: DspParallelPrimary::Accumulator(DspAccumulatorOperation::Clear {
                    target: DspAccumulator::Ac0,
                }),
                parallel: DspParallelOperation::Nop { raw_extension: 1 },
                raw_word: 0x8101,
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0443, &[0x4c01])
                .unwrap()
                .instruction,
            DspInstruction::Parallel {
                primary: DspParallelPrimary::Accumulator(DspAccumulatorOperation::Add {
                    target: DspAccumulator::Ac0,
                    operand: DspAccumulatorOperand::Accumulator(DspAccumulator::Ac1),
                }),
                parallel: DspParallelOperation::Nop { raw_extension: 1 },
                raw_word: 0x4c01,
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0444, &[0x1541])
                .unwrap()
                .instruction,
            DspInstruction::Accumulator(DspAccumulatorOperation::ShiftImmediate {
                target: DspAccumulator::Ac1,
                kind: DspAccumulatorShiftKind::LogicalRight,
                amount: 1,
            })
        );
    }

    #[test]
    fn decodes_parallel_nop_extension_for_mode_and_logic_primaries() {
        assert_eq!(
            decode_dsp_instruction(0x0445, &[0x8a01])
                .unwrap()
                .instruction,
            DspInstruction::Parallel {
                primary: DspParallelPrimary::Mode(DspModeOperation::ProductMultiplyByTwo {
                    enabled: true,
                }),
                parallel: DspParallelOperation::Nop { raw_extension: 1 },
                raw_word: 0x8a01,
            }
        );
        assert_eq!(
            decode_dsp_instruction(0x0446, &[0x3301])
                .unwrap()
                .instruction,
            DspInstruction::Parallel {
                primary: DspParallelPrimary::Logic(DspLogicOperation::Xor {
                    target: DspAccumulator::Ac1,
                    operand: DspLogicOperand::AxHigh(DspAxRegister::Ax1),
                }),
                parallel: DspParallelOperation::Nop { raw_extension: 1 },
                raw_word: 0x3301,
            }
        );
    }

    #[test]
    fn hard_fails_missing_opcode() {
        assert_eq!(
            decode_dsp_instruction(0x0400, &[]),
            Err(DspDecodeError::MissingOpcode { address: 0x0400 })
        );
    }

    #[test]
    fn hard_fails_truncated_call_immediate() {
        assert_eq!(
            decode_dsp_instruction(0x0444, &[0x02bf]),
            Err(DspDecodeError::TruncatedInstruction {
                address: 0x0444,
                word: 0x02bf,
                expected_words: 2,
                available_words: 1,
            })
        );
    }

    #[test]
    fn hard_fails_truncated_two_word_control_and_transfer_opcodes() {
        assert_eq!(
            decode_dsp_instruction(0x0445, &[0x029f]),
            Err(DspDecodeError::TruncatedInstruction {
                address: 0x0445,
                word: 0x029f,
                expected_words: 2,
                available_words: 1,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0446, &[0x0060]),
            Err(DspDecodeError::TruncatedInstruction {
                address: 0x0446,
                word: 0x0060,
                expected_words: 2,
                available_words: 1,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0447, &[0x0080]),
            Err(DspDecodeError::TruncatedInstruction {
                address: 0x0447,
                word: 0x0080,
                expected_words: 2,
                available_words: 1,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0448, &[0x1600]),
            Err(DspDecodeError::TruncatedInstruction {
                address: 0x0448,
                word: 0x1600,
                expected_words: 2,
                available_words: 1,
            })
        );
        assert_eq!(
            decode_dsp_instruction(0x0449, &[0x0300]),
            Err(DspDecodeError::TruncatedInstruction {
                address: 0x0449,
                word: 0x0300,
                expected_words: 2,
                available_words: 1,
            })
        );
    }

    #[test]
    fn decodes_stream_with_mixed_lengths() {
        let decoded =
            decode_dsp_stream(0x0100, &[0x0000, 0x02bf, 0x8123, 0x029f, 0x8000, 0x0021]).unwrap();
        assert_eq!(
            decoded
                .iter()
                .map(|instruction| (instruction.address, instruction.length_words))
                .collect::<Vec<_>>(),
            vec![(0x0100, 1), (0x0101, 2), (0x0103, 2), (0x0105, 1)]
        );
        assert_eq!(
            decoded[1].instruction,
            DspInstruction::CallImmediate {
                condition: DspCondition::Always,
                target: 0x8123,
            }
        );
        assert_eq!(
            decoded[2].instruction,
            DspInstruction::JumpImmediate {
                condition: DspCondition::Always,
                target: 0x8000,
            }
        );
        assert_eq!(decoded[0].second_word, None);
        assert_eq!(decoded[1].second_word, Some(0x8123));
        assert_eq!(decoded[2].second_word, Some(0x8000));
        assert_eq!(decoded[3].second_word, None);
        let zero_second_word = decode_dsp_instruction(0x0200, &[0x02bf, 0x0000]).unwrap();
        assert_eq!(zero_second_word.length_words, 2);
        assert_eq!(zero_second_word.second_word, Some(0x0000));
    }

    #[test]
    fn audits_supported_dsp_program_with_category_counts() {
        let audit = audit_dsp_program(
            0x0000,
            &[
                0x0000, 0x02b4, 0x8123, 0xbcf0, 0x11ff, 0x0456, 0x8e00, 0x00da, 0x0123, 0x0021,
            ],
        )
        .unwrap();

        assert_eq!(
            audit,
            DspProgramAudit {
                start_address: 0x0000,
                word_count: 10,
                decoded_word_count: 10,
                instruction_count: 7,
                multi_word_instruction_count: 3,
                parallel_instruction_count: 1,
                control_flow_instruction_count: 3,
                conditional_control_flow_instruction_count: 1,
                call_instruction_count: 1,
                jump_instruction_count: 0,
                return_instruction_count: 0,
                loop_instruction_count: 1,
                halt_instruction_count: 1,
                arithmetic_instruction_count: 1,
                memory_instruction_count: 2,
                mode_instruction_count: 1,
                address_update_instruction_count: 0,
            }
        );
    }

    #[test]
    fn audit_hard_fails_with_unknown_opcode_location() {
        assert_eq!(
            audit_dsp_program(0x0100, &[0x0000, 0x03ff]),
            Err(DspDecodeError::UnsupportedOpcode {
                address: 0x0101,
                word: 0x03ff,
            })
        );
    }

    #[test]
    fn audit_hard_fails_invalid_instruction_start_address() {
        assert_eq!(
            audit_dsp_program(0x1000, &[0x0000]),
            Err(DspDecodeError::InvalidInstructionAddress { address: 0x1000 })
        );
    }

    #[test]
    fn audit_hard_fails_two_word_instruction_crossing_memory_region() {
        assert_eq!(
            audit_dsp_program(0x0fff, &[0x02bf, 0x8123]),
            Err(DspDecodeError::InvalidInstructionAddress { address: 0x1000 })
        );
    }

    #[test]
    fn lowers_nop_and_halt_to_static_cpp_labels() {
        let generated =
            lower_dsp_program_to_cpp(0x0000, &[0x0000, 0x0021], "galaxy_dsp_test").unwrap();
        let identity = dsp_program_identity_from_words_be(0x0000, &[0x0000, 0x0021]);

        assert_eq!(generated.instruction_count, 2);
        assert_eq!(generated.word_count, 2);
        assert_eq!(identity.byte_len, 4);
        assert!(generated
            .cpp
            .contains("extern \"C\" std::uint16_t galaxy_dsp_test_expected_iram_start_address()"));
        assert!(generated.cpp.contains("    return 0x0000u;\n"));
        assert!(generated
            .cpp
            .contains("extern \"C\" std::uint32_t galaxy_dsp_test_expected_iram_byte_len()"));
        assert!(generated.cpp.contains("    return 4u;\n"));
        assert!(generated
            .cpp
            .contains("extern \"C\" const char* galaxy_dsp_test_expected_iram_sha1()"));
        assert!(generated
            .cpp
            .contains(&format!("    return \"{}\";\n", identity.sha1)));
        assert!(generated
            .cpp
            .contains("extern \"C\" void galaxy_dsp_test(galaxy::DspContext& ctx)"));
        assert!(generated.cpp.contains("    (void)ctx;\n"));
        assert!(generated.cpp.contains("    if (ctx.pc != 0u) {\n"));
        assert!(generated.cpp.contains("        goto pc_dispatch;\n"));
        assert!(generated.cpp.contains("    goto pc_0000;\n"));
        assert!(generated.cpp.contains("pc_0000:\n"));
        assert!(generated.cpp.contains("    // 0000: 0000\n"));
        assert!(generated.cpp.contains("    goto pc_0001;\n"));
        assert!(generated.cpp.contains("pc_0001:\n"));
        assert!(generated.cpp.contains("    // 0001: 0021\n"));
        assert!(generated.cpp.contains("    ctx.pc = 0x0001u;\n"));
        assert!(generated.cpp.contains("    ctx.halted = true;\n"));
        assert!(generated.cpp.contains("    return;\n"));
    }

    #[test]
    fn repeated_lowering_is_byte_identical_with_exact_unique_provenance_exports() {
        let iram = [0x0000, 0x0021];
        let first = lower_dsp_program_to_cpp(0x0000, &iram, "dsp_regeneration_test").unwrap();
        let second = lower_dsp_program_to_cpp(0x0000, &iram, "dsp_regeneration_test").unwrap();

        assert_eq!(first, second);
        assert_eq!(
            first.lowering_contract_version,
            DSP_GENERATED_LOWERING_CONTRACT_VERSION
        );
        assert_eq!(first.lowering_contract_version, 1);
        assert_eq!(first.raw_input_aggregate_sha256.len(), 64);
        assert!(first
            .raw_input_aggregate_sha256
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte)));

        let version_export = concat!(
            "extern \"C\" std::uint32_t ",
            "dsp_regeneration_test_generated_lowering_contract_version(){return 1u;}"
        );
        let aggregate_export = format!(
            "extern \"C\" const char* dsp_regeneration_test_generated_raw_input_aggregate_sha256(){{return \"{}\";}}",
            first.raw_input_aggregate_sha256
        );
        assert_eq!(first.cpp.matches(version_export).count(), 1);
        assert_eq!(first.cpp.matches(&aggregate_export).count(), 1);
        assert_eq!(
            first
                .cpp
                .matches("dsp_regeneration_test_generated_lowering_contract_version")
                .count(),
            1
        );
        assert_eq!(
            first
                .cpp
                .matches("dsp_regeneration_test_generated_raw_input_aggregate_sha256")
                .count(),
            1
        );
    }

    #[test]
    fn emits_full_generated_instruction_identity_and_nullable_scope() {
        let standalone = lower_dsp_program_to_cpp(
            0x0000,
            &[0x02bf, 0x0003, 0x0021, 0x0021],
            "dsp_provenance_standalone_test",
        )
        .unwrap();
        let standalone_block = generated_instruction_block(&standalone.cpp, 0x0000);
        assert!(standalone
            .cpp
            .contains("#include \"galaxy/dsp_instruction_provenance.h\"\n"));
        assert!(standalone_block.contains(
            "galaxy::DspGeneratedInstructionIdentity{\n            0x0000u,\n            0x02BFu,\n            0x0003u,\n            true,\n            galaxy::DspGeneratedInstructionBundleKind::Standalone,\n            0x0000u,\n            static_cast<std::uint8_t>(0x0000u),\n            static_cast<std::uint8_t>(0x0000u)},"
        ));
        assert_ordered_substrings(
            standalone_block,
            &[
                "ctx.raw_instruction_boundary_observer",
                "galaxy::dsp_raw_instruction_boundary_require_ready(ctx, raw_instruction_boundary);",
                "galaxy::dsp_stack_push(ctx, 0, 0x0002u);",
                "galaxy::dsp_raw_instruction_boundary_commit(ctx, raw_instruction_boundary);",
                "goto pc_0003;",
            ],
        );

        let parallel =
            lower_dsp_program_to_cpp(0x0000, &[0x8401, 0x0021], "dsp_provenance_parallel_test")
                .unwrap();
        let parallel_block = generated_instruction_block(&parallel.cpp, 0x0000);
        assert!(parallel_block.contains(
            "galaxy::DspGeneratedInstructionIdentity{\n            0x0000u,\n            0x8401u,\n            0x0000u,\n            false,\n            galaxy::DspGeneratedInstructionBundleKind::Parallel,\n            0x8400u,\n            static_cast<std::uint8_t>(0x0001u),\n            static_cast<std::uint8_t>(0x00FFu)},"
        ));
        assert_eq!(
            parallel_block
                .matches("galaxy::dsp_raw_instruction_boundary_commit")
                .count(),
            1
        );

        let narrow_parallel = lower_dsp_program_to_cpp(
            0x0000,
            &[0x3301, 0x0021],
            "dsp_provenance_narrow_parallel_test",
        )
        .unwrap();
        let narrow_parallel_block = generated_instruction_block(&narrow_parallel.cpp, 0x0000);
        assert!(narrow_parallel_block.contains(
            "galaxy::DspGeneratedInstructionIdentity{\n            0x0000u,\n            0x3301u,\n            0x0000u,\n            false,\n            galaxy::DspGeneratedInstructionBundleKind::Parallel,\n            0x3300u,\n            static_cast<std::uint8_t>(0x0001u),\n            static_cast<std::uint8_t>(0x007Fu)},"
        ));

        let zero_second_word = lower_dsp_program_to_cpp(
            0x0000,
            &[0x02bf, 0x0000, 0x0021],
            "dsp_provenance_zero_second_word_test",
        )
        .unwrap();
        let zero_second_word_block = generated_instruction_block(&zero_second_word.cpp, 0x0000);
        assert!(zero_second_word_block.contains(
            "galaxy::DspGeneratedInstructionIdentity{\n            0x0000u,\n            0x02BFu,\n            0x0000u,\n            true,\n            galaxy::DspGeneratedInstructionBundleKind::Standalone,"
        ));
    }

    #[test]
    fn emits_decoder_authoritative_memory_plans_before_semantics() {
        let instruction = lower_dsp_program_to_cpp(
            0x0000,
            &[0x0212, 0x0021],
            "dsp_instruction_memory_plan_test",
        )
        .unwrap();
        let instruction_block = generated_instruction_block(&instruction.cpp, 0x0000);
        assert_ordered_substrings(
            instruction_block,
            &[
                "galaxy::DspGeneratedMemorySlot::Standalone",
                "galaxy::DspGeneratedMemorySpace::Instruction",
                "galaxy::DspGeneratedMemoryDirection::Read",
                "galaxy::DspGeneratedMemoryAddressSourceKind::AddressRegister",
                "0x0002u}",
                "static_cast<std::uint8_t>(1)}",
                "galaxy::dsp_raw_instruction_boundary_require_ready(ctx, raw_instruction_boundary);",
                "galaxy::dsp_iram_read(ctx, ctx.ar[2])",
            ],
        );

        let direct =
            lower_dsp_program_to_cpp(0x0000, &[0x2f55, 0x0021], "dsp_direct_memory_plan_test")
                .unwrap();
        let direct_block = generated_instruction_block(&direct.cpp, 0x0000);
        assert_ordered_substrings(
            direct_block,
            &[
                "galaxy::DspGeneratedMemorySpace::Data",
                "galaxy::DspGeneratedMemoryDirection::Write",
                "galaxy::DspGeneratedMemoryAddressSourceKind::DirectPage",
                "0x0055u}",
                "galaxy::dsp_raw_instruction_boundary_require_ready(ctx, raw_instruction_boundary);",
                "galaxy::dsp_data_write(ctx, static_cast<std::uint16_t>((ctx.cr << 8u) | 0x55u)",
            ],
        );

        let parallel = lower_dsp_program_to_cpp(
            0x0000,
            &[0x80ac, 0x80ae, 0x80d7, 0x0021],
            "dsp_parallel_memory_plan_test",
        )
        .unwrap();
        let load_then_store = generated_instruction_block(&parallel.cpp, 0x0000);
        assert_ordered_substrings(
            load_then_store,
            &[
                "galaxy::DspGeneratedMemorySlot::ParallelPrimary",
                "galaxy::DspGeneratedMemoryDirection::Read",
                "galaxy::DspGeneratedMemoryAddressSourceKind::AddressRegister",
                "0x0000u}",
                "galaxy::DspGeneratedMemorySlot::ParallelSecondary",
                "galaxy::DspGeneratedMemoryDirection::Write",
                "galaxy::DspGeneratedMemoryAddressSourceKind::AddressRegister",
                "0x0003u}",
                "static_cast<std::uint8_t>(2)}",
            ],
        );
        let store_then_load = generated_instruction_block(&parallel.cpp, 0x0001);
        assert_ordered_substrings(
            store_then_load,
            &[
                "galaxy::DspGeneratedMemorySlot::ParallelPrimary",
                "galaxy::DspGeneratedMemoryDirection::Write",
                "0x0000u}",
                "galaxy::DspGeneratedMemorySlot::ParallelSecondary",
                "galaxy::DspGeneratedMemoryDirection::Read",
                "0x0003u}",
            ],
        );
        let dual_load = generated_instruction_block(&parallel.cpp, 0x0002);
        assert_eq!(
            dual_load
                .matches("galaxy::DspGeneratedMemoryDirection::Read")
                .count(),
            2
        );
        assert_eq!(
            dual_load
                .matches("galaxy::DspGeneratedMemorySlot::Parallel")
                .count(),
            2
        );
    }

    #[test]
    fn emits_explicit_commit_for_each_successful_control_exit_but_not_inline_trap() {
        let conditional = lower_dsp_program_to_cpp(
            0x0000,
            &[0x0295, 0x0003, 0x0021, 0x0021],
            "dsp_provenance_conditional_test",
        )
        .unwrap();
        let conditional_block = generated_instruction_block(&conditional.cpp, 0x0000);
        assert_eq!(
            conditional_block
                .matches("galaxy::dsp_raw_instruction_boundary_commit")
                .count(),
            2
        );
        assert_ordered_substrings(
            conditional_block,
            &[
                "if (galaxy::dsp_condition_holds",
                "galaxy::dsp_raw_instruction_boundary_commit(ctx, raw_instruction_boundary);",
                "goto pc_0003;",
                "galaxy::dsp_raw_instruction_boundary_commit(ctx, raw_instruction_boundary);",
                "goto pc_0002;",
            ],
        );

        let counted_loop = lower_dsp_program_to_cpp(
            0x0000,
            &[0x1001, 0x0000, 0x0021],
            "dsp_provenance_counted_loop_test",
        )
        .unwrap();
        let counted_loop_block = generated_instruction_block(&counted_loop.cpp, 0x0000);
        assert_eq!(
            counted_loop_block
                .matches("galaxy::dsp_raw_instruction_boundary_commit")
                .count(),
            2
        );

        let loop_end = lower_dsp_program_to_cpp(
            0x0000,
            &[0x1002, 0x0000, 0x0021],
            "dsp_provenance_loop_end_test",
        )
        .unwrap();
        let loop_end_block = generated_instruction_block(&loop_end.cpp, 0x0001);
        assert_eq!(
            loop_end_block
                .matches("galaxy::dsp_raw_instruction_boundary_commit")
                .count(),
            2
        );
        assert_ordered_substrings(
            loop_end_block,
            &[
                "ctx.pc = static_cast<std::uint16_t>(loop_target_0001);",
                "galaxy::dsp_raw_instruction_boundary_commit(ctx, raw_instruction_boundary);",
                "galaxy::dsp_accept_pending_external_interrupt(ctx);",
                "goto pc_dispatch;",
            ],
        );

        let trapped = lower_dsp_program_from_entry_vectors(
            0x0000,
            &[0x1101, 0x0005, 0x0021],
            &[0x0000],
            "dsp_provenance_inline_trap_test",
        )
        .unwrap();
        let trapped_block = generated_instruction_block(&trapped.cpp, 0x0000);
        assert!(trapped_block.contains(
            "galaxy::dsp_hard_trap(ctx, 0x0000u, \"block loop end outside lowered DSP program\");"
        ));
        assert!(!trapped_block.contains("galaxy::dsp_raw_instruction_boundary_commit"));
    }

    #[test]
    fn lowers_halt_to_halt_pc_not_fallthrough_pc() {
        let generated =
            lower_dsp_program_to_cpp(0x0000, &[0x0021, 0x0021], "galaxy_dsp_halt_pc_test").unwrap();

        assert!(generated.cpp.contains(
            "    ctx.pc = 0x0000u;\n    ctx.halted = true;\n    galaxy::dsp_raw_instruction_boundary_commit(ctx, raw_instruction_boundary);\n    return;\n"
        ));
    }

    #[test]
    fn lowers_program_from_explicit_entry_vector() {
        let generated = lower_dsp_program_to_cpp_at_entry(
            0x0000,
            &[0x0000, 0x0021],
            0x0001,
            "galaxy_dsp_entry_vector_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 2);
        assert!(generated.cpp.contains("    goto pc_0001;\n"));
        assert!(generated.cpp.contains("pc_0000:\n"));
        assert!(generated.cpp.contains("pc_0001:\n"));
    }

    #[test]
    fn lowering_hard_fails_when_entry_vector_was_not_decoded() {
        assert_eq!(
            lower_dsp_program_to_cpp_at_entry(
                0x0000,
                &[0x0000, 0x0021],
                0x0003,
                "missing_dsp_entry_vector_test",
            ),
            Err(DspLoweringError::MissingEntryAddress { address: 0x0003 })
        );
    }

    #[test]
    fn lowers_unconditional_immediate_jump_to_known_label() {
        let generated =
            lower_dsp_program_to_cpp(0x0000, &[0x029f, 0x0003, 0x0021, 0x0021], "dsp_jump_test")
                .unwrap();

        assert_eq!(generated.instruction_count, 3);
        assert_eq!(generated.word_count, 4);
        assert!(generated.cpp.contains("pc_0000:\n"));
        assert!(generated.cpp.contains("    // 0000: 029F\n"));
        assert!(generated.cpp.contains("    goto pc_0003;\n"));
        assert!(generated.cpp.contains("pc_0003:\n"));
    }

    #[test]
    fn lowers_backward_jump_with_external_interrupt_checkpoint() {
        let generated =
            lower_dsp_program_to_cpp(0x0000, &[0x029f, 0x0000], "dsp_backedge_test").unwrap();

        assert_eq!(generated.instruction_count, 1);
        assert!(generated.cpp.contains(
            "ctx.pc = 0x0000u;\n    galaxy::dsp_native_handle_static_backedge(ctx, 0x0000u, 0x0000u);\n    galaxy::dsp_accept_pending_external_interrupt(ctx);"
        ));
        assert!(generated.cpp.contains("    goto pc_0000;\n"));
    }

    #[test]
    fn lowers_conditional_immediate_jump_with_checked_fallthrough() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x0295, 0x0003, 0x0021, 0x0021],
            "dsp_cond_jump_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 3);
        assert!(generated
            .cpp
            .contains("if (galaxy::dsp_condition_holds(galaxy::DspCondition::Zero, ctx.sr))"));
        assert!(generated.cpp.contains("    goto pc_0003;\n"));
        assert!(generated.cpp.contains("    goto pc_0002;\n"));
    }

    #[test]
    fn lowers_conditional_backward_jump_with_external_interrupt_checkpoint() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x0021, 0x0295, 0x0000, 0x0021],
            "dsp_cond_backedge_test",
        )
        .unwrap();

        assert!(generated.cpp.contains(
            "        ctx.pc = 0x0000u;\n        galaxy::dsp_native_handle_static_backedge(ctx, 0x0001u, 0x0000u);\n        galaxy::dsp_accept_pending_external_interrupt(ctx);\n        goto pc_0000;\n"
        ));
        assert!(generated.cpp.contains("    goto pc_0003;\n"));
    }

    #[test]
    fn lowers_unconditional_immediate_call_to_known_label() {
        let generated =
            lower_dsp_program_to_cpp(0x0000, &[0x02bf, 0x0003, 0x0021, 0x0021], "dsp_call_test")
                .unwrap();

        assert_eq!(generated.instruction_count, 3);
        assert!(generated
            .cpp
            .contains("    galaxy::dsp_stack_push(ctx, 0, 0x0002u);\n    galaxy::dsp_raw_instruction_boundary_commit(ctx, raw_instruction_boundary);\n    goto pc_0003;\n"));
    }

    #[test]
    fn lowers_conditional_immediate_call_with_checked_fallthrough() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x02b5, 0x0003, 0x0021, 0x0021],
            "dsp_cond_call_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 3);
        assert!(generated
            .cpp
            .contains("if (galaxy::dsp_condition_holds(galaxy::DspCondition::Zero, ctx.sr))"));
        assert!(generated
            .cpp
            .contains("        galaxy::dsp_stack_push(ctx, 0, 0x0002u);\n        galaxy::dsp_raw_instruction_boundary_commit(ctx, raw_instruction_boundary);\n        goto pc_0003;\n"));
        assert!(generated.cpp.contains("    goto pc_0002;\n"));
    }

    #[test]
    fn lowers_register_jump_through_compiled_label_dispatch() {
        let generated =
            lower_dsp_program_to_cpp(0x0000, &[0x170f, 0x0021], "dsp_register_jump_test").unwrap();

        assert_eq!(generated.instruction_count, 2);
        assert!(generated
            .cpp
            .contains("const auto jump_target_0000 = static_cast<std::uint16_t>(ctx.ar[0]);"));
        assert!(generated
            .cpp
            .contains("ctx.pc = static_cast<std::uint16_t>(jump_target_0000);"));
        assert!(generated.cpp.contains("goto pc_dispatch;"));
        assert!(generated.cpp.contains(
            "pc_dispatch:\n    galaxy::dsp_native_reset_idle_sequence_progress(ctx);\n    switch (ctx.pc)"
        ));
        assert!(generated.cpp.contains("case 0x0000u:\n"));
        assert!(generated.cpp.contains("case 0x0001u:\n"));
        assert!(generated.cpp.contains(
            "galaxy::dsp_hard_trap(ctx, ctx.pc, \"computed DSP target outside lowered program\");"
        ));
        assert!(generated.cpp.contains(
            "if (static_cast<std::uint16_t>(jump_target_0000) <= 0x0000u) {\n        ctx.pc = static_cast<std::uint16_t>(jump_target_0000);\n        galaxy::dsp_accept_pending_external_interrupt(ctx);\n    }\n"
        ));
    }

    #[test]
    fn cfg_lowering_recovers_code_only_reachable_past_an_unconditional_jump() {
        // 0x0000: JMP 0x0004 (jumps over 0x0002/0x0003), 0x0004: HALT.
        // 0x0002: HALT and 0x0003: NOP are never reached from the entry, so a
        // pure control-flow walk misses them; gap recovery must still decode them
        // (they stand in for jump-table handlers reached only by computed jumps).
        let words = [0x029f, 0x0004, 0x0021, 0x0000, 0x0021];
        let generated =
            lower_dsp_program_from_entry_vectors(0x0000, &words, &[0x0000], "dsp_cfg_gap_test")
                .unwrap();

        assert_eq!(generated.instruction_count, 4);
        for label in ["pc_0000:", "pc_0002:", "pc_0003:", "pc_0004:"] {
            assert!(
                generated.cpp.contains(label),
                "expected recovered label {label}"
            );
        }
        assert!(generated.cpp.contains("goto pc_0004;"));
    }

    #[test]
    fn cfg_lowering_traps_a_static_branch_into_undecoded_data() {
        // Reset jumps to 0x0100, which is outside the lowered image, so the
        // target is never decoded. Tolerant lowering must still succeed and emit
        // a hard-fail trap label for the dangling target instead of failing.
        let words = [0x029f, 0x0100, 0x0021];
        let generated =
            lower_dsp_program_from_entry_vectors(0x0000, &words, &[0x0000], "dsp_cfg_trap_test")
                .unwrap();

        assert!(generated.cpp.contains("goto pc_0100;"));
        assert!(generated.cpp.contains("pc_0100:"));
        assert!(generated.cpp.contains(
            "galaxy::dsp_hard_trap(ctx, 0x0100u, \"static DSP target outside lowered program\");"
        ));
    }

    #[test]
    fn cfg_lowering_follows_supplied_irom_branch_targets() {
        // Sparse combined image: IRAM starts at 0x0000 and the DSP instruction
        // ROM is spliced at 0x8000. The invalid padding must remain data, while
        // the explicit branch to IROM lowers as a real native label.
        let mut words = vec![0x03ff; 0x8001];
        words[0] = 0x029f;
        words[1] = 0x8000;
        words[0x8000] = 0x0021;

        let generated =
            lower_dsp_program_from_entry_vectors(0x0000, &words, &[0x0000], "dsp_cfg_irom_test")
                .unwrap();

        assert!(generated.cpp.contains("goto pc_8000;"));
        assert!(generated.cpp.contains("pc_8000:"));
        assert!(generated.cpp.contains("    // 8000: 0021\n"));
        assert!(!generated
            .cpp
            .contains("pc_8000:\n    galaxy::dsp_hard_trap"));
    }

    #[test]
    fn generated_entry_initializes_static_irom_and_coefficient_memory() {
        let original_iram_identity = dsp_program_identity_from_words_be(0x0000, &[0x029f, 0x8000]);
        let generated = lower_dsp_program_from_entry_vectors_with_static_memory(
            0x0000,
            &[0x0021],
            &[0x0000],
            "dsp_static_memory_test",
            DspStaticMemoryImages {
                irom_words: Some(&[0x1234, 0xabcd]),
                coefficient_words: Some(&[0x0080, 0x7fff]),
            },
        )
        .unwrap();

        assert!(generated
            .cpp
            .contains("constexpr std::uint16_t kGalaxyDspIromWords[]"));
        assert!(generated
            .cpp
            .contains("constexpr std::uint16_t kGalaxyDspCoefficientWords[]"));
        assert!(generated
            .cpp
            .contains("ctx.irom[i] = kGalaxyDspIromWords[i];"));
        assert!(generated
            .cpp
            .contains("ctx.coef[i] = kGalaxyDspCoefficientWords[i];"));
        assert!(generated
            .cpp
            .contains("galaxy_dsp_load_static_memory(ctx);"));
        assert!(!generated.cpp.contains(&original_iram_identity.sha1));

        let mut sparse_words = vec![0x03ff; 0x8001];
        sparse_words[0] = 0x029f;
        sparse_words[1] = 0x8000;
        sparse_words[0x8000] = 0x0021;
        let generated = lower_dsp_program_from_entry_vectors_with_static_memory_and_raw_provenance(
            0x0000,
            &sparse_words,
            &[0x0000],
            "dsp_static_memory_identity_test",
            DspStaticMemoryImages {
                irom_words: Some(&[0x1234, 0xabcd]),
                coefficient_words: Some(&[0x0080, 0x7fff]),
            },
            &[0x029f, 0x8000],
        )
        .unwrap();
        assert!(generated.cpp.contains(&format!(
            "    return \"{}\";\n",
            original_iram_identity.sha1
        )));
    }

    #[test]
    fn lowers_register_call_through_compiled_label_dispatch() {
        let generated =
            lower_dsp_program_to_cpp(0x0000, &[0x171f, 0x0021], "dsp_register_call_test").unwrap();

        assert_eq!(generated.instruction_count, 2);
        assert!(generated
            .cpp
            .contains("const auto call_target_0000 = static_cast<std::uint16_t>(ctx.ar[0]);"));
        assert!(generated
            .cpp
            .contains("galaxy::dsp_stack_push(ctx, 0, 0x0001u);"));
        assert!(generated
            .cpp
            .contains("ctx.pc = static_cast<std::uint16_t>(call_target_0000);"));
        assert!(generated.cpp.contains("goto pc_dispatch;"));
        assert!(generated.cpp.contains("switch (ctx.pc)"));
    }

    #[test]
    fn lowers_return_and_interrupt_return_through_compiled_label_dispatch() {
        let generated =
            lower_dsp_program_to_cpp(0x0000, &[0x02df, 0x02ff, 0x0021], "dsp_return_test").unwrap();

        assert_eq!(generated.instruction_count, 3);
        assert!(generated
            .cpp
            .contains("const auto return_target_0000 = galaxy::dsp_stack_pop(ctx, 0);"));
        assert!(generated
            .cpp
            .contains("ctx.pc = static_cast<std::uint16_t>(return_target_0000);"));
        assert!(generated
            .cpp
            .contains("ctx.sr = galaxy::dsp_stack_pop(ctx, 1);"));
        assert!(generated
            .cpp
            .contains("const auto return_target_0001 = galaxy::dsp_stack_pop(ctx, 0);"));
        assert!(generated
            .cpp
            .contains("ctx.pc = static_cast<std::uint16_t>(return_target_0001);"));
        assert!(generated.cpp.contains("switch (ctx.pc)"));
    }

    #[test]
    fn lowers_conditional_return_with_checked_fallthrough() {
        let generated =
            lower_dsp_program_to_cpp(0x0000, &[0x02d5, 0x0021], "dsp_cond_return_test").unwrap();

        assert_eq!(generated.instruction_count, 2);
        assert!(generated
            .cpp
            .contains("if (galaxy::dsp_condition_holds(galaxy::DspCondition::Zero, ctx.sr))"));
        assert!(generated
            .cpp
            .contains("const auto return_target_0000 = galaxy::dsp_stack_pop(ctx, 0);"));
        assert!(generated.cpp.contains("    goto pc_0001;\n"));
    }

    #[test]
    fn lowers_if_as_conditional_execution_of_next_instruction() {
        let generated =
            lower_dsp_program_to_cpp(0x0000, &[0x0275, 0x0000, 0x0021], "dsp_if_test").unwrap();

        assert_eq!(generated.instruction_count, 3);
        assert!(generated
            .cpp
            .contains("if (galaxy::dsp_condition_holds(galaxy::DspCondition::Zero, ctx.sr))"));
        assert!(generated.cpp.contains("        goto pc_0001;\n"));
        assert!(generated.cpp.contains("    goto pc_0002;\n"));
    }

    #[test]
    fn lowers_always_if_as_fallthrough_to_next_instruction() {
        let generated =
            lower_dsp_program_to_cpp(0x0000, &[0x027f, 0x0021], "dsp_if_always_test").unwrap();

        assert_eq!(generated.instruction_count, 2);
        assert!(generated.cpp.contains("pc_0000:\n"));
        assert!(generated.cpp.contains("    goto pc_0001;\n"));
        assert!(!generated.cpp.contains("dsp_condition_holds"));
    }

    #[test]
    fn lowers_register_loop_with_hardware_loop_stack_and_checkpoint() {
        let generated =
            lower_dsp_program_to_cpp(0x0000, &[0x0040, 0x0000, 0x0021], "dsp_loop_test").unwrap();

        assert_eq!(generated.instruction_count, 3);
        assert!(generated
            .cpp
            .contains("const auto loop_count_0000 = static_cast<std::uint16_t>(ctx.ar[0]);"));
        assert!(generated
            .cpp
            .contains("galaxy::dsp_stack_push(ctx, 0, 0x0001u);"));
        assert!(generated
            .cpp
            .contains("galaxy::dsp_stack_push(ctx, 2, 0x0001u);"));
        assert!(generated.cpp.contains(
            "galaxy::dsp_stack_push(ctx, 3, static_cast<std::uint16_t>(loop_count_0000));"
        ));
        assert!(generated
            .cpp
            .contains("if (ctx.st[2] != 0u && ctx.st[2] == 0x0001u && ctx.st[3] != 0u)"));
        assert!(generated
            .cpp
            .contains("const auto loop_target_0001 = ctx.st[0];"));
        assert!(generated
            .cpp
            .contains(
                "const auto loop_target_0001 = ctx.st[0];\n            ctx.pc = static_cast<std::uint16_t>(loop_target_0001);\n            galaxy::dsp_raw_instruction_boundary_commit(ctx, raw_instruction_boundary);\n            galaxy::dsp_accept_pending_external_interrupt(ctx);\n            goto pc_dispatch;"
            ));
        assert!(generated.cpp.contains("goto pc_dispatch;"));
        assert!(generated
            .cpp
            .contains("(void)galaxy::dsp_stack_pop(ctx, 0);"));
        assert!(generated
            .cpp
            .contains("(void)galaxy::dsp_stack_pop(ctx, 2);"));
        assert!(generated
            .cpp
            .contains("(void)galaxy::dsp_stack_pop(ctx, 3);"));
    }

    #[test]
    fn lowers_immediate_loop_zero_count_to_skip_following_instruction() {
        let generated =
            lower_dsp_program_to_cpp(0x0000, &[0x1000, 0x0000, 0x0021], "dsp_loopi_zero_test")
                .unwrap();

        assert_eq!(generated.instruction_count, 3);
        assert!(generated.cpp.contains("if (0x0000u != 0u)"));
        assert!(generated.cpp.contains("    goto pc_0002;\n"));
    }

    #[test]
    fn lowers_block_loop_with_inclusive_end_address() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x1102, 0x0003, 0x0000, 0x0000, 0x0021],
            "dsp_bloopi_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 4);
        assert!(generated
            .cpp
            .contains("galaxy::dsp_stack_push(ctx, 0, 0x0002u);"));
        assert!(generated
            .cpp
            .contains("galaxy::dsp_stack_push(ctx, 2, 0x0003u);"));
        assert!(generated
            .cpp
            .contains("galaxy::dsp_stack_push(ctx, 3, static_cast<std::uint16_t>(0x0002u));"));
        assert!(generated.cpp.contains("        goto pc_0002;\n"));
        assert!(generated
            .cpp
            .contains("if (ctx.st[2] != 0u && ctx.st[2] == 0x0003u && ctx.st[3] != 0u)"));
    }

    #[test]
    fn lowers_backward_register_block_loop_end_when_label_is_decoded() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x0000, 0x0000, 0x007e, 0x0001, 0x0021],
            "dsp_backward_bloop_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 4);
        assert!(generated.cpp.contains(
            "const auto block_loop_count_0002 = static_cast<std::uint16_t>(galaxy::dsp_read_accumulator_mid(ctx.ac[0], ctx.sr));"
        ));
        assert!(generated
            .cpp
            .contains("galaxy::dsp_stack_push(ctx, 0, 0x0004u);"));
        assert!(generated
            .cpp
            .contains("galaxy::dsp_stack_push(ctx, 2, 0x0001u);"));
        assert!(generated
            .cpp
            .contains("if (ctx.st[2] != 0u && ctx.st[2] == 0x0001u && ctx.st[3] != 0u)"));
        assert!(generated.cpp.contains("    goto pc_0004;\n"));
        assert!(generated.cpp.contains("    goto pc_0002;\n"));
    }

    #[test]
    fn lowers_block_loop_end_on_final_word_of_multiword_instruction() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x007e, 0x0004, 0x0000, 0x00fe, 0x0123, 0x0021],
            "dsp_multiword_end_bloop_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 4);
        assert!(generated
            .cpp
            .contains("galaxy::dsp_stack_push(ctx, 0, 0x0002u);"));
        assert!(generated
            .cpp
            .contains("galaxy::dsp_stack_push(ctx, 2, 0x0004u);"));
        assert!(generated
            .cpp
            .contains("if (ctx.st[2] != 0u && ctx.st[2] == 0x0004u && ctx.st[3] != 0u)"));
        assert!(generated.cpp.contains("    goto pc_0005;\n"));
    }

    #[test]
    fn lowers_zero_count_block_loop_to_instruction_after_inclusive_end() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x1100, 0x0003, 0x0000, 0x0000, 0x0021],
            "dsp_bloopi_zero_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 4);
        assert!(generated.cpp.contains("if (0x0000u != 0u)"));
        assert!(generated.cpp.contains("    goto pc_0004;\n"));
    }

    #[test]
    fn lowers_address_updates_to_context_mutations() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x0005, 0x000a, 0x000f, 0x001b, 0x0021],
            "dsp_address_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 5);
        assert!(generated
            .cpp
            .contains("ctx.ar[1] = galaxy::dsp_decrement_address(ctx.ar[1], ctx.wr[1]);"));
        assert!(generated
            .cpp
            .contains("ctx.ar[2] = galaxy::dsp_increment_address(ctx.ar[2], ctx.wr[2]);"));
        assert!(generated.cpp.contains(
            "ctx.ar[3] = galaxy::dsp_decrease_address(ctx.ar[3], ctx.ix[3], ctx.wr[3]);"
        ));
        assert!(generated.cpp.contains(
            "ctx.ar[3] = galaxy::dsp_increase_address(ctx.ar[3], ctx.ix[2], ctx.wr[3]);"
        ));
    }

    #[test]
    fn lowers_status_bit_updates_to_context_status_register() {
        let generated =
            lower_dsp_program_to_cpp(0x0000, &[0x1200, 0x1307, 0x0021], "dsp_status_test").unwrap();

        assert_eq!(generated.instruction_count, 3);
        assert!(generated
            .cpp
            .contains("ctx.sr = static_cast<std::uint16_t>(ctx.sr & ~0x0040u);"));
        assert!(generated
            .cpp
            .contains("ctx.sr = static_cast<std::uint16_t>(ctx.sr | 0x2000u);"));
    }

    #[test]
    fn lowering_retains_sbset_eie_commit_fallthrough_pc_accept_order() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x1305, 0x0021],
            "dsp_external_interrupt_enable_test",
        )
        .unwrap();

        assert!(generated.cpp.contains(
            "ctx.sr = static_cast<std::uint16_t>(ctx.sr | 0x0800u);\n    galaxy::dsp_raw_instruction_boundary_commit(ctx, raw_instruction_boundary);\n    ctx.pc = 0x0001u;\n    galaxy::dsp_accept_pending_external_interrupt(ctx);\n    goto pc_0001;"
        ));
    }

    #[test]
    fn lowers_mode_updates_to_context_status_register() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[
                0x8a00, 0x8b00, 0x8d00, 0x8c00, 0x8f00, 0x8e00, 0x8000, 0x0021,
            ],
            "dsp_mode_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 8);
        assert!(generated
            .cpp
            .contains("ctx.sr = static_cast<std::uint16_t>(ctx.sr & ~0x2000u);"));
        assert!(generated
            .cpp
            .contains("ctx.sr = static_cast<std::uint16_t>(ctx.sr | 0x2000u);"));
        assert!(generated
            .cpp
            .contains("ctx.sr = static_cast<std::uint16_t>(ctx.sr | 0x8000u);"));
        assert!(generated
            .cpp
            .contains("ctx.sr = static_cast<std::uint16_t>(ctx.sr & ~0x8000u);"));
        assert!(generated
            .cpp
            .contains("ctx.sr = static_cast<std::uint16_t>(ctx.sr | 0x4000u);"));
        assert!(generated
            .cpp
            .contains("ctx.sr = static_cast<std::uint16_t>(ctx.sr & ~0x4000u);"));
        assert!(generated.cpp.contains("    // 0006: 8000\n"));
        assert!(generated.cpp.contains("    goto pc_0007;\n"));
    }

    #[test]
    fn lowers_logic_operations_through_runtime_alu_helpers() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x3000, 0x3400, 0x3e00, 0x3280, 0x0021],
            "dsp_logic_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 5);
        assert!(generated.cpp.contains(
            "const auto logic_0000 = galaxy::dsp_accumulator_mid_xor(ctx.ac[0], ctx.ax[0][1], ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto logic_0001 = galaxy::dsp_accumulator_mid_and(ctx.ac[0], ctx.ax[0][1], ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto logic_0002 = galaxy::dsp_accumulator_mid_or(ctx.ac[0], galaxy::dsp_read_accumulator_mid_raw(ctx.ac[1]), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto logic_0003 = galaxy::dsp_accumulator_mid_not(ctx.ac[0], ctx.sr);"
        ));
        assert!(generated
            .cpp
            .contains("ctx.ac[0] = logic_0003.accumulator;"));
        assert!(generated.cpp.contains("ctx.sr = logic_0003.status;"));
    }

    #[test]
    fn lowers_register_counted_logic_shifts_through_runtime_helpers() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x3480, 0x3980, 0x3d80, 0x3f80, 0x0021],
            "dsp_logic_shift_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 5);
        assert!(generated.cpp.contains(
            "const auto logic_0000 = galaxy::dsp_accumulator_shift_by_register_count(ctx.ac[0], false, ctx.ax[0][1], ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto logic_0001 = galaxy::dsp_accumulator_shift_by_register_count(ctx.ac[1], true, ctx.ax[0][1], ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto logic_0002 = galaxy::dsp_accumulator_shift_by_register_count(ctx.ac[1], false, galaxy::dsp_read_accumulator_mid_raw(ctx.ac[0]), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto logic_0003 = galaxy::dsp_accumulator_shift_by_register_count(ctx.ac[1], true, galaxy::dsp_read_accumulator_mid_raw(ctx.ac[0]), ctx.sr);"
        ));
        assert!(generated.cpp.contains("ctx.ac[1] = logic_0003.value;"));
        assert!(generated.cpp.contains("ctx.sr = logic_0003.status;"));
        assert!(generated.cpp.contains("    goto pc_0004;\n"));
    }

    #[test]
    fn lowers_product_clear_to_runtime_alu_helper() {
        let generated =
            lower_dsp_program_to_cpp(0x0000, &[0x8400, 0x0021], "dsp_product_clear_test").unwrap();

        assert_eq!(generated.instruction_count, 2);
        assert!(generated
            .cpp
            .contains("ctx.prod = galaxy::dsp_cleared_product();"));
        assert!(generated.cpp.contains("    goto pc_0001;\n"));
    }

    #[test]
    fn lowers_product_test_and_moves_through_runtime_helpers() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x8500, 0x6f00, 0x7f00, 0xff00, 0x0021],
            "dsp_product_move_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 5);
        assert!(generated.cpp.contains(
            "const auto product_0000 = galaxy::dsp_accumulator_set_value(galaxy::dsp_product_value(ctx.prod), ctx.sr);"
        ));
        assert!(generated.cpp.contains("ctx.sr = product_0000.status;"));
        assert!(generated.cpp.contains(
            "const auto accumulator_0001 = galaxy::dsp_accumulator_set_value(galaxy::dsp_product_value(ctx.prod), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0002 = galaxy::dsp_accumulator_set_value(-galaxy::dsp_product_value(ctx.prod), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0003 = galaxy::dsp_accumulator_set_value(galaxy::dsp_round_product(ctx.prod), ctx.sr);"
        ));
        assert!(generated
            .cpp
            .contains("ctx.ac[1] = accumulator_0003.value;"));
        assert!(generated.cpp.contains("    goto pc_0004;\n"));
    }

    #[test]
    fn lowers_ax_pair_product_multiply_and_accumulator_actions() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x8300, 0x9800, 0x9500, 0x9f00, 0x9300, 0x0021],
            "dsp_product_ax_pair_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 6);
        assert!(generated.cpp.contains(
            "ctx.prod = galaxy::dsp_product_multiply(ctx.ax[0][1], ctx.ax[0][1], galaxy::DspMultiplyOperandMode::Signed, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "ctx.prod = galaxy::dsp_product_multiply(ctx.ax[1][0], ctx.ax[1][1], galaxy::DspMultiplyOperandMode::Signed, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0002 = galaxy::dsp_accumulator_set_value(ctx.ac[1] + galaxy::dsp_product_value(ctx.prod), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "ctx.prod = galaxy::dsp_product_multiply(ctx.ax[0][0], ctx.ax[0][1], galaxy::DspMultiplyOperandMode::Signed, ctx.sr);"
        ));
        assert!(generated
            .cpp
            .contains("ctx.ac[1] = accumulator_0002.value;"));
        assert!(generated.cpp.contains(
            "const auto accumulator_0003 = galaxy::dsp_accumulator_set_value(galaxy::dsp_product_value(ctx.prod), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "ctx.prod = galaxy::dsp_product_multiply(ctx.ax[1][0], ctx.ax[1][1], galaxy::DspMultiplyOperandMode::Signed, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0004 = galaxy::dsp_accumulator_set_value(galaxy::dsp_round_product(ctx.prod), ctx.sr);"
        ));
        assert!(generated.cpp.contains("    goto pc_0005;\n"));
    }

    #[test]
    fn lowers_cross_multiply_operand_modes_to_runtime_product_helper() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0xa000, 0xa800, 0xb000, 0xb800, 0x0021],
            "dsp_product_mul_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 5);
        assert!(generated.cpp.contains(
            "ctx.prod = galaxy::dsp_product_multiply(ctx.ax[0][0], ctx.ax[1][0], galaxy::DspMultiplyOperandMode::UnsignedWhenSrUnsigned, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "ctx.prod = galaxy::dsp_product_multiply(ctx.ax[0][0], ctx.ax[1][1], galaxy::DspMultiplyOperandMode::MixedUnsignedLeftWhenSrUnsigned, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "ctx.prod = galaxy::dsp_product_multiply(ctx.ax[1][0], ctx.ax[0][1], galaxy::DspMultiplyOperandMode::MixedUnsignedLeftWhenSrUnsigned, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "ctx.prod = galaxy::dsp_product_multiply(ctx.ax[0][1], ctx.ax[1][1], galaxy::DspMultiplyOperandMode::Signed, ctx.sr);"
        ));
        assert!(generated.cpp.contains("    goto pc_0004;\n"));
    }

    #[test]
    fn lowers_signed_high_cross_product_accumulate_to_runtime_helpers() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0xe300, 0xe700, 0x0021],
            "dsp_product_accumulate_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 3);
        assert!(generated.cpp.contains(
            "ctx.prod = galaxy::dsp_product_multiply_add(ctx.prod, ctx.ax[0][1], ctx.ax[1][1], galaxy::DspMultiplyOperandMode::Signed, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "ctx.prod = galaxy::dsp_product_multiply_subtract(ctx.prod, ctx.ax[0][1], ctx.ax[1][1], galaxy::DspMultiplyOperandMode::Signed, ctx.sr);"
        ));
        assert!(generated.cpp.contains("    goto pc_0002;\n"));
    }

    #[test]
    fn lowers_signed_high_cross_multiply_with_accumulator_actions() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0xbd00, 0xbf00, 0xbb00, 0x0021],
            "dsp_product_cross_accumulator_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 4);
        assert!(generated.cpp.contains(
            "const auto accumulator_0000 = galaxy::dsp_accumulator_set_value(ctx.ac[1] + galaxy::dsp_product_value(ctx.prod), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "ctx.prod = galaxy::dsp_product_multiply(ctx.ax[0][1], ctx.ax[1][1], galaxy::DspMultiplyOperandMode::Signed, ctx.sr);"
        ));
        assert!(generated
            .cpp
            .contains("ctx.ac[1] = accumulator_0000.value;"));
        assert!(generated.cpp.contains(
            "const auto accumulator_0001 = galaxy::dsp_accumulator_set_value(galaxy::dsp_product_value(ctx.prod), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0002 = galaxy::dsp_accumulator_set_value(galaxy::dsp_round_product(ctx.prod), ctx.sr);"
        ));
        assert!(generated.cpp.contains("    goto pc_0003;\n"));
    }

    #[test]
    fn lowers_cross_multiply_with_accumulator_operand_modes() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0xb500, 0xae00, 0xa200, 0x0021],
            "dsp_product_cross_accumulator_modes_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 4);
        assert!(generated.cpp.contains(
            "const auto accumulator_0000 = galaxy::dsp_accumulator_set_value(ctx.ac[1] + galaxy::dsp_product_value(ctx.prod), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "ctx.prod = galaxy::dsp_product_multiply(ctx.ax[1][0], ctx.ax[0][1], galaxy::DspMultiplyOperandMode::MixedUnsignedLeftWhenSrUnsigned, ctx.sr);"
        ));
        assert!(generated
            .cpp
            .contains("ctx.ac[1] = accumulator_0000.value;"));
        assert!(generated.cpp.contains(
            "const auto accumulator_0001 = galaxy::dsp_accumulator_set_value(galaxy::dsp_product_value(ctx.prod), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "ctx.prod = galaxy::dsp_product_multiply(ctx.ax[0][0], ctx.ax[1][1], galaxy::DspMultiplyOperandMode::MixedUnsignedLeftWhenSrUnsigned, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0002 = galaxy::dsp_accumulator_set_value(galaxy::dsp_round_product(ctx.prod), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "ctx.prod = galaxy::dsp_product_multiply(ctx.ax[0][0], ctx.ax[1][0], galaxy::DspMultiplyOperandMode::UnsignedWhenSrUnsigned, ctx.sr);"
        ));
        assert!(generated.cpp.contains("    goto pc_0003;\n"));
    }

    #[test]
    fn lowers_accumulator_mid_and_signed_product_accumulate_operations() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[
                0xd800, 0xcd00, 0xcf00, 0xcb00, 0xeb00, 0xef00, 0xf300, 0xf700, 0xe000, 0xe400,
                0x0021,
            ],
            "dsp_product_accumulator_mid_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 11);
        assert!(generated.cpp.contains(
            "ctx.prod = galaxy::dsp_product_multiply(galaxy::dsp_read_accumulator_mid_raw(ctx.ac[1]), ctx.ax[1][1], galaxy::DspMultiplyOperandMode::Signed, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0001 = galaxy::dsp_accumulator_set_value(ctx.ac[1] + galaxy::dsp_product_value(ctx.prod), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "ctx.prod = galaxy::dsp_product_multiply(galaxy::dsp_read_accumulator_mid_raw(ctx.ac[0]), ctx.ax[1][1], galaxy::DspMultiplyOperandMode::Signed, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0002 = galaxy::dsp_accumulator_set_value(galaxy::dsp_product_value(ctx.prod), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0003 = galaxy::dsp_accumulator_set_value(galaxy::dsp_round_product(ctx.prod), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "ctx.prod = galaxy::dsp_product_multiply_add(ctx.prod, galaxy::dsp_read_accumulator_mid_raw(ctx.ac[1]), ctx.ax[1][1], galaxy::DspMultiplyOperandMode::Signed, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "ctx.prod = galaxy::dsp_product_multiply_subtract(ctx.prod, galaxy::dsp_read_accumulator_mid_raw(ctx.ac[1]), ctx.ax[1][1], galaxy::DspMultiplyOperandMode::Signed, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "ctx.prod = galaxy::dsp_product_multiply_add(ctx.prod, ctx.ax[1][0], ctx.ax[1][1], galaxy::DspMultiplyOperandMode::Signed, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "ctx.prod = galaxy::dsp_product_multiply_subtract(ctx.prod, ctx.ax[1][0], ctx.ax[1][1], galaxy::DspMultiplyOperandMode::Signed, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "ctx.prod = galaxy::dsp_product_multiply_add(ctx.prod, ctx.ax[0][0], ctx.ax[1][0], galaxy::DspMultiplyOperandMode::Signed, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "ctx.prod = galaxy::dsp_product_multiply_subtract(ctx.prod, ctx.ax[0][0], ctx.ax[1][0], galaxy::DspMultiplyOperandMode::Signed, ctx.sr);"
        ));
        assert!(generated.cpp.contains("    goto pc_000A;\n"));
    }

    #[test]
    fn lowers_parallel_nop_primary_operations_through_existing_emitters() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x4c01, 0x3301, 0x8401, 0x8a01, 0x0021],
            "dsp_parallel_nop_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 5);
        assert!(generated.cpp.contains(
            "const auto accumulator_0000 = galaxy::dsp_accumulator_add(ctx.ac[0], ctx.ac[1], ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto logic_0001 = galaxy::dsp_accumulator_mid_xor(ctx.ac[1], ctx.ax[1][1], ctx.sr);"
        ));
        assert!(generated
            .cpp
            .contains("ctx.prod = galaxy::dsp_cleared_product();"));
        assert!(generated
            .cpp
            .contains("ctx.sr = static_cast<std::uint16_t>(ctx.sr & ~0x2000u);"));
        assert!(generated.cpp.contains("    goto pc_0004;\n"));
    }

    #[test]
    fn lowers_parallel_address_extensions_after_primary_operations() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x8007, 0x3309, 0x0021],
            "dsp_parallel_address_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 3);
        assert!(generated
            .cpp
            .contains("ctx.ar[3] = galaxy::dsp_decrement_address(ctx.ar[3], ctx.wr[3]);"));
        assert!(generated.cpp.contains(
            "const auto logic_0001 = galaxy::dsp_accumulator_mid_xor(ctx.ac[1], ctx.ax[1][1], ctx.sr);"
        ));
        assert!(generated
            .cpp
            .contains("ctx.ac[1] = logic_0001.accumulator;"));
        assert!(generated
            .cpp
            .contains("ctx.ar[1] = galaxy::dsp_increment_address(ctx.ar[1], ctx.wr[1]);"));
        assert!(generated.cpp.contains("    goto pc_0002;\n"));
    }

    #[test]
    fn lowers_backlogged_parallel_move_store_and_load_with_non_nop_primaries() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x331f, 0x333b, 0x3340, 0x0021],
            "dsp_parallel_backlog_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 4);
        assert_ordered_substrings(
            &generated.cpp,
            &[
                "const auto parallel_0000_value = galaxy::dsp_read_accumulator_mid(ctx.ac[1], ctx.sr);",
                "const auto logic_0000 = galaxy::dsp_accumulator_mid_xor(ctx.ac[1], ctx.ax[1][1], ctx.sr);",
                "ctx.ax[1][1] = static_cast<std::uint16_t>(parallel_0000_value);",
            ],
        );
        assert_ordered_substrings(
            &generated.cpp,
            &[
                "const auto parallel_0001_value = static_cast<std::uint16_t>(galaxy::dsp_read_accumulator_mid(ctx.ac[1], ctx.sr));",
                "galaxy::dsp_data_write(ctx, ctx.ar[3], parallel_0001_value);",
                "const auto logic_0001 = galaxy::dsp_accumulator_mid_xor(ctx.ac[1], ctx.ax[1][1], ctx.sr);",
                "ctx.ar[3] = galaxy::dsp_increment_address(ctx.ar[3], ctx.wr[3]);",
            ],
        );
        assert_ordered_substrings(
            &generated.cpp,
            &[
                "const auto parallel_0002_value = galaxy::dsp_data_read(ctx, ctx.ar[0]);",
                "const auto logic_0002 = galaxy::dsp_accumulator_mid_xor(ctx.ac[1], ctx.ax[1][1], ctx.sr);",
                "ctx.ax[0][0] = static_cast<std::uint16_t>(parallel_0002_value);",
                "ctx.ar[0] = galaxy::dsp_increment_address(ctx.ar[0], ctx.wr[0]);",
            ],
        );
        assert!(generated.cpp.contains("    goto pc_0003;\n"));
    }

    #[test]
    fn parallel_mid_load_chooses_extension_mode_before_primary() {
        // SET40/CLR40 plus L/LN into both mid registers. The mode decision is
        // made before the primary; writeback still uses its resulting AC value.
        for (primary, mode_write) in [
            (0x8F00u16, "ctx.sr | 0x4000u"),
            (0x8E00, "ctx.sr & ~0x4000u"),
        ] {
            for (extension, accumulator, update) in [
                (
                    0x70u16,
                    0,
                    "galaxy::dsp_increment_address(ctx.ar[0], ctx.wr[0])",
                ),
                (
                    0x74,
                    0,
                    "galaxy::dsp_increase_address(ctx.ar[0], ctx.ix[0], ctx.wr[0])",
                ),
                (
                    0x78,
                    1,
                    "galaxy::dsp_increment_address(ctx.ar[0], ctx.wr[0])",
                ),
                (
                    0x7C,
                    1,
                    "galaxy::dsp_increase_address(ctx.ar[0], ctx.ix[0], ctx.wr[0])",
                ),
            ] {
                let generated = lower_dsp_program_to_cpp(
                    0,
                    &[primary | extension, 0x0021],
                    "parallel_mid_mode",
                )
                .unwrap();
                let writeback = format!(
                    "ctx.ac[{accumulator}] = galaxy::dsp_write_accumulator_mid(ctx.ac[{accumulator}], static_cast<std::uint16_t>(parallel_0000_value), parallel_0000_load_status);"
                );
                assert_ordered_substrings(
                    &generated.cpp,
                    &[
                        "const auto parallel_0000_load_status = ctx.sr;",
                        "const auto parallel_0000_value = galaxy::dsp_data_read(ctx, ctx.ar[0]);",
                        mode_write,
                        &writeback,
                        update,
                    ],
                );
                assert!(!generated
                    .cpp
                    .contains("static_cast<std::uint16_t>(parallel_0000_value), ctx.sr)"));
            }
        }
    }

    #[test]
    fn lowers_backlogged_compound_parallel_operations_with_non_nop_primaries() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x4c80, 0x4cd7, 0x4cc0, 0x0021],
            "dsp_parallel_compound_backlog_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 4);
        assert_ordered_substrings(
            &generated.cpp,
            &[
                "const auto parallel_0000_store_value = static_cast<std::uint16_t>(galaxy::dsp_read_accumulator_mid(ctx.ac[0], ctx.sr));",
                "galaxy::dsp_data_write(ctx, ctx.ar[3], parallel_0000_store_value);",
                "const auto parallel_0000_load_value = galaxy::dsp_data_read(ctx, ctx.ar[0]);",
                "const auto accumulator_0000 = galaxy::dsp_accumulator_add(ctx.ac[0], ctx.ac[1], ctx.sr);",
                "ctx.ax[0][0] = static_cast<std::uint16_t>(parallel_0000_load_value);",
                "ctx.ar[3] = galaxy::dsp_increment_address(ctx.ar[3], ctx.wr[3]);",
                "ctx.ar[0] = galaxy::dsp_increment_address(ctx.ar[0], ctx.wr[0]);",
            ],
        );
        assert_ordered_substrings(
            &generated.cpp,
            &[
                "const auto parallel_load_0001_primary = galaxy::dsp_data_read(ctx, ctx.ar[0]);",
                "const auto parallel_load_0001_secondary = ((ctx.ar[0] & 0xFC00u) == (ctx.ar[3] & 0xFC00u)) ? parallel_load_0001_primary : galaxy::dsp_data_read(ctx, ctx.ar[3]);",
                "const auto accumulator_0001 = galaxy::dsp_accumulator_add(ctx.ac[0], ctx.ac[1], ctx.sr);",
                "ctx.ax[1][1] = static_cast<std::uint16_t>(parallel_load_0001_primary);",
                "ctx.ax[1][0] = static_cast<std::uint16_t>(parallel_load_0001_secondary);",
                "ctx.ar[0] = galaxy::dsp_increase_address(ctx.ar[0], ctx.ix[0], ctx.wr[0]);",
            ],
        );
        assert_ordered_substrings(
            &generated.cpp,
            &[
                "const auto parallel_load_0002_primary = galaxy::dsp_data_read(ctx, ctx.ar[0]);",
                "const auto accumulator_0002 = galaxy::dsp_accumulator_add(ctx.ac[0], ctx.ac[1], ctx.sr);",
                "ctx.ax[0][0] = static_cast<std::uint16_t>(parallel_load_0002_primary);",
                "ctx.ax[1][0] = static_cast<std::uint16_t>(parallel_load_0002_secondary);",
            ],
        );
        assert!(generated.cpp.contains("    goto pc_0003;\n"));
    }

    #[test]
    fn lowers_parallel_nop_register_moves_through_register_helpers() {
        let generated =
            lower_dsp_program_to_cpp(0x0000, &[0x801f, 0x0021], "dsp_parallel_move_test").unwrap();

        assert_eq!(generated.instruction_count, 2);
        assert!(generated.cpp.contains(
            "ctx.ax[1][1] = static_cast<std::uint16_t>(galaxy::dsp_read_accumulator_mid(ctx.ac[1], ctx.sr));"
        ));
        assert!(generated.cpp.contains("    goto pc_0001;\n"));
    }

    #[test]
    fn lowers_parallel_nop_single_load_store_with_post_updates() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x803b, 0x807b, 0x8067, 0x0021],
            "dsp_parallel_single_memory_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 4);
        assert!(generated.cpp.contains(
            "galaxy::dsp_data_write(ctx, ctx.ar[3], static_cast<std::uint16_t>(galaxy::dsp_read_accumulator_mid(ctx.ac[1], ctx.sr)));"
        ));
        assert!(generated
            .cpp
            .contains("ctx.ar[3] = galaxy::dsp_increment_address(ctx.ar[3], ctx.wr[3]);"));
        assert!(generated.cpp.contains(
            "ctx.ac[1] = galaxy::dsp_write_accumulator_mid(ctx.ac[1], static_cast<std::uint16_t>(galaxy::dsp_data_read(ctx, ctx.ar[3])), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "ctx.ac[0] = galaxy::dsp_write_accumulator_low(ctx.ac[0], static_cast<std::uint16_t>(galaxy::dsp_data_read(ctx, ctx.ar[3])));"
        ));
        assert!(generated.cpp.contains(
            "ctx.ar[3] = galaxy::dsp_increase_address(ctx.ar[3], ctx.ix[3], ctx.wr[3]);"
        ));
        assert!(generated.cpp.contains("    goto pc_0003;\n"));
    }

    #[test]
    fn lowers_parallel_nop_accumulator_mid_load_store_with_post_updates() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x8080, 0x80ae, 0x0021],
            "dsp_parallel_accumulator_memory_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 3);
        assert!(generated.cpp.contains(
            "galaxy::dsp_data_write(ctx, ctx.ar[3], static_cast<std::uint16_t>(galaxy::dsp_read_accumulator_mid(ctx.ac[0], ctx.sr)));"
        ));
        assert!(generated.cpp.contains(
            "ctx.ax[0][0] = static_cast<std::uint16_t>(galaxy::dsp_data_read(ctx, ctx.ar[0]));"
        ));
        assert!(generated
            .cpp
            .contains("ctx.ar[3] = galaxy::dsp_increment_address(ctx.ar[3], ctx.wr[3]);"));
        assert!(generated
            .cpp
            .contains("ctx.ar[0] = galaxy::dsp_increment_address(ctx.ar[0], ctx.wr[0]);"));
        assert!(generated.cpp.contains(
            "galaxy::dsp_data_write(ctx, ctx.ar[0], static_cast<std::uint16_t>(galaxy::dsp_read_accumulator_mid(ctx.ac[0], ctx.sr)));"
        ));
        assert!(generated.cpp.contains(
            "ctx.ax[0][1] = static_cast<std::uint16_t>(galaxy::dsp_data_read(ctx, ctx.ar[3]));"
        ));
        assert!(generated.cpp.contains(
            "ctx.ar[3] = galaxy::dsp_increase_address(ctx.ar[3], ctx.ix[3], ctx.wr[3]);"
        ));
        assert!(generated.cpp.contains(
            "ctx.ar[0] = galaxy::dsp_increase_address(ctx.ar[0], ctx.ix[0], ctx.wr[0]);"
        ));
        assert!(generated.cpp.contains("    goto pc_0002;\n"));
    }

    #[test]
    fn emits_channel_selection_probe_only_at_exact_rmge01_handler_store() {
        let handler = lower_dsp_program_to_cpp(
            0x0715,
            &[0x1A1E, 0x0021],
            "dsp_selection_handler_probe_test",
        )
        .unwrap();
        assert!(handler
            .cpp
            .contains("const auto selection_0715_address = ctx.ar[0];"));
        assert!(handler.cpp.contains(
            "galaxy::dsp_data_write(ctx, selection_0715_address, selection_0715_value);"
        ));
        assert!(handler.cpp.contains(
            "galaxy::dsp_channel_selection_dma_probe_record_selection_write(ctx.channel_selection_dma_probe, selection_0715_address, selection_0715_value);"
        ));
        assert!(handler.cpp.contains(
            "dsp_selection_handler_probe_test_generated_probe_contract_version() {\n    return 1u;"
        ));
        assert!(handler.cpp.contains(
            "dsp_selection_handler_probe_test_generated_probe_capabilities() {\n    return 1u;"
        ));

        let same_opcode_elsewhere =
            lower_dsp_program_to_cpp(0x0000, &[0x1A1E, 0x0021], "dsp_non_selection_store_test")
                .unwrap();
        assert!(!same_opcode_elsewhere
            .cpp
            .contains("dsp_channel_selection_dma_probe_record_selection_write"));
        assert!(same_opcode_elsewhere.cpp.contains(
            "dsp_non_selection_store_test_generated_probe_capabilities() {\n    return 0u;"
        ));
    }

    #[test]
    fn emits_selected_channel_branch_probe_and_literal_capability_only_for_exact_rmge01_call() {
        let handler = decode_dsp_instruction(0x0715, &[0x1A1E]).unwrap();
        let exact_branch = decode_dsp_instruction(0x02EF, &[0x02BF, 0x00CC]).unwrap();
        assert!(is_rmge01_selection_publication_hook(&handler));
        assert!(is_rmge01_selected_channel_branch_hook(&exact_branch));
        assert_eq!(
            generated_probe_capabilities(&[handler, exact_branch]),
            DSP_GENERATED_PROBE_CAPABILITY_SELECTION_PUBLICATION
                | DSP_GENERATED_PROBE_CAPABILITY_SELECTED_CHANNEL_BRANCH
        );

        let same_call_elsewhere = decode_dsp_instruction(0x02EE, &[0x02BF, 0x00CC]).unwrap();
        let wrong_target = decode_dsp_instruction(0x02EF, &[0x02BF, 0x00CD]).unwrap();
        let conditional_call = decode_dsp_instruction(0x02EF, &[0x02B5, 0x00CC]).unwrap();
        assert!(!is_rmge01_selected_channel_branch_hook(
            &same_call_elsewhere
        ));
        assert!(!is_rmge01_selected_channel_branch_hook(&wrong_target));
        assert!(!is_rmge01_selected_channel_branch_hook(&conditional_call));

        // Include the exact RMGE01 address and its target in one strict linear
        // test program so this checks emitted C++, not only the hook predicate.
        let branch_index = usize::from(0x02EFu16 - 0x00CCu16);
        let mut words = vec![0x0021u16; branch_index + 3];
        words[branch_index] = 0x02BFu16;
        words[branch_index + 1] = 0x00CCu16;
        let generated =
            lower_dsp_program_to_cpp(0x00CC, &words, "dsp_selected_channel_branch_probe_test")
                .unwrap();
        assert_eq!(
            generated
                .cpp
                .matches("dsp_channel_selection_dma_probe_record_selected_channel_branch")
                .count(),
            1
        );
        assert!(generated.cpp.contains(
            "dsp_selected_channel_branch_probe_test_generated_probe_contract_version() {\n    return 1u;"
        ));
        assert!(generated.cpp.contains(
            "dsp_selected_channel_branch_probe_test_generated_probe_capabilities() {\n    return 2u;"
        ));
        assert!(generated.cpp.contains(
            "dsp_channel_selection_dma_probe_record_selected_channel_branch(ctx.channel_selection_dma_probe, selected_channel_02EF, selection_word_02EF, expected_channel_record_host_02EF);"
        ));
    }

    #[test]
    fn lowers_parallel_nop_dual_ax_loads_with_page_alias_guard() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x80d7, 0x80c0, 0x0021],
            "dsp_parallel_dual_load_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 3);
        assert!(generated.cpp.contains(
            "const auto parallel_load_0000_primary = galaxy::dsp_data_read(ctx, ctx.ar[0]);"
        ));
        assert!(generated.cpp.contains(
            "const auto parallel_load_0000_secondary = ((ctx.ar[0] & 0xFC00u) == (ctx.ar[3] & 0xFC00u)) ? parallel_load_0000_primary : galaxy::dsp_data_read(ctx, ctx.ar[3]);"
        ));
        assert!(generated
            .cpp
            .contains("ctx.ax[1][1] = static_cast<std::uint16_t>(parallel_load_0000_primary);"));
        assert!(generated
            .cpp
            .contains("ctx.ax[1][0] = static_cast<std::uint16_t>(parallel_load_0000_secondary);"));
        assert!(generated.cpp.contains(
            "ctx.ar[0] = galaxy::dsp_increase_address(ctx.ar[0], ctx.ix[0], ctx.wr[0]);"
        ));
        assert!(generated.cpp.contains(
            "const auto parallel_load_0001_primary = galaxy::dsp_data_read(ctx, ctx.ar[0]);"
        ));
        assert!(generated
            .cpp
            .contains("ctx.ax[0][0] = static_cast<std::uint16_t>(parallel_load_0001_primary);"));
        assert!(generated
            .cpp
            .contains("ctx.ax[1][0] = static_cast<std::uint16_t>(parallel_load_0001_secondary);"));
        assert!(generated.cpp.contains("    goto pc_0002;\n"));
    }

    #[test]
    fn lowers_accumulator_add_subtract_and_negate_through_runtime_alu_helpers() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x4d00, 0x7b00, 0x7500, 0x7d00, 0x0021],
            "dsp_accumulator_alu_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 5);
        assert!(generated.cpp.contains(
            "const auto accumulator_0000 = galaxy::dsp_accumulator_add(ctx.ac[1], ctx.ac[0], ctx.sr);"
        ));
        assert!(generated
            .cpp
            .contains("ctx.ac[1] = accumulator_0000.value;"));
        assert!(generated.cpp.contains("ctx.sr = accumulator_0000.status;"));
        assert!(generated.cpp.contains(
            "const auto accumulator_0001 = galaxy::dsp_accumulator_subtract(ctx.ac[1], 1, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0002 = galaxy::dsp_accumulator_add(ctx.ac[1], 0x00010000ll, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0003 = galaxy::dsp_accumulator_negate(ctx.ac[1], ctx.sr);"
        ));
        assert!(generated
            .cpp
            .contains("ctx.ac[1] = accumulator_0003.value;"));
        assert!(generated.cpp.contains("ctx.sr = accumulator_0003.status;"));
    }

    #[test]
    fn lowers_accumulator_ax_operands_through_runtime_alu_helpers() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x4700, 0x4b00, 0x7300, 0x5700, 0x5b00, 0x0021],
            "dsp_accumulator_ax_operands_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 6);
        assert!(generated.cpp.contains(
            "const auto accumulator_0000 = galaxy::dsp_accumulator_add(ctx.ac[1], galaxy::dsp_accumulator_from_signed_mid(ctx.ax[1][1]), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0001 = galaxy::dsp_accumulator_add(ctx.ac[1], galaxy::dsp_accumulator_from_ax_pair(ctx.ax[1][0], ctx.ax[1][1]), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0002 = galaxy::dsp_accumulator_add(ctx.ac[1], static_cast<std::int64_t>(ctx.ax[1][0]), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0003 = galaxy::dsp_accumulator_subtract(ctx.ac[1], galaxy::dsp_accumulator_from_signed_mid(ctx.ax[1][1]), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0004 = galaxy::dsp_accumulator_subtract(ctx.ac[1], galaxy::dsp_accumulator_from_ax_pair(ctx.ax[1][0], ctx.ax[1][1]), ctx.sr);"
        ));
        assert!(generated
            .cpp
            .contains("ctx.ac[1] = accumulator_0004.value;"));
        assert!(generated.cpp.contains("ctx.sr = accumulator_0004.status;"));
    }

    #[test]
    fn lowers_accumulator_control_compare_and_move_operations() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[
                0x8100, 0xfd00, 0xb900, 0x8200, 0xd900, 0xa900, 0x6700, 0x6b00, 0x6d00, 0x0021,
            ],
            "dsp_accumulator_control_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 10);
        assert!(generated.cpp.contains(
            "const auto accumulator_0000 = galaxy::dsp_accumulator_set_value(0, ctx.sr);"
        ));
        assert!(generated
            .cpp
            .contains("ctx.ac[0] = accumulator_0000.value;"));
        assert!(generated.cpp.contains(
            "const auto accumulator_0001 = galaxy::dsp_accumulator_clear_low(ctx.ac[1], ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0002 = galaxy::dsp_accumulator_set_value(ctx.ac[1], ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0003 = galaxy::dsp_accumulator_subtract(ctx.ac[0], ctx.ac[1], ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0004 = galaxy::dsp_accumulator_subtract(ctx.ac[1], galaxy::dsp_accumulator_from_signed_mid(ctx.ax[1][1]), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0005 = galaxy::dsp_accumulator_absolute(ctx.ac[1], ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0006 = galaxy::dsp_accumulator_set_value(galaxy::dsp_accumulator_from_signed_mid(ctx.ax[1][1]), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0007 = galaxy::dsp_accumulator_set_value(galaxy::dsp_accumulator_from_ax_pair(ctx.ax[1][0], ctx.ax[1][1]), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0008 = galaxy::dsp_accumulator_set_value(ctx.ac[0], ctx.sr);"
        ));
        assert!(generated.cpp.contains("    goto pc_0009;\n"));
    }

    #[test]
    fn lowers_accumulator_ax_high_test_through_16_bit_status_helper() {
        let generated =
            lower_dsp_program_to_cpp(0x0000, &[0x8700, 0x0021], "dsp_test_ax_high_test").unwrap();

        assert_eq!(generated.instruction_count, 2);
        assert!(generated.cpp.contains(
            "ctx.sr = galaxy::dsp_status_16(static_cast<std::int16_t>(ctx.ax[1][1]), false, false, false, ctx.sr);"
        ));
        assert!(generated.cpp.contains("    goto pc_0001;\n"));
    }

    #[test]
    fn lowers_accumulator_shift_operations_through_runtime_alu_helpers() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[
                0x151f, 0x1541, 0x1582, 0x15c3, 0xf100, 0xf500, 0x9900, 0x02ca, 0x02cb, 0x0021,
            ],
            "dsp_accumulator_shift_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 10);
        assert!(generated.cpp.contains(
            "const auto accumulator_0000 = galaxy::dsp_accumulator_shift(ctx.ac[1], galaxy::DspAccumulatorShiftKind::LogicalLeft, 31u, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0001 = galaxy::dsp_accumulator_shift(ctx.ac[1], galaxy::DspAccumulatorShiftKind::LogicalRight, 63u, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0002 = galaxy::dsp_accumulator_shift(ctx.ac[1], galaxy::DspAccumulatorShiftKind::ArithmeticLeft, 2u, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0003 = galaxy::dsp_accumulator_shift(ctx.ac[1], galaxy::DspAccumulatorShiftKind::ArithmeticRight, 61u, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0004 = galaxy::dsp_accumulator_shift(ctx.ac[1], galaxy::DspAccumulatorShiftKind::LogicalLeft, 16u, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0005 = galaxy::dsp_accumulator_shift(ctx.ac[1], galaxy::DspAccumulatorShiftKind::LogicalRight, 16u, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0006 = galaxy::dsp_accumulator_shift(ctx.ac[1], galaxy::DspAccumulatorShiftKind::ArithmeticRight, 16u, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0007 = galaxy::dsp_accumulator_shift_by_signed_count(ctx.ac[0], false, galaxy::dsp_read_accumulator_mid_raw(ctx.ac[1]), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0008 = galaxy::dsp_accumulator_shift_by_signed_count(ctx.ac[0], true, galaxy::dsp_read_accumulator_mid_raw(ctx.ac[1]), ctx.sr);"
        ));
        assert!(generated
            .cpp
            .contains("ctx.ac[0] = accumulator_0008.value;"));
        assert!(generated.cpp.contains("    goto pc_0009;\n"));
    }

    #[test]
    fn lowers_product_accumulator_operands_through_runtime_helpers() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x4f00, 0x5f00, 0x0021],
            "dsp_accumulator_product_operand_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 3);
        assert!(generated.cpp.contains(
            "const auto accumulator_0000 = galaxy::dsp_accumulator_add(ctx.ac[1], galaxy::dsp_product_value(ctx.prod), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0001 = galaxy::dsp_accumulator_subtract(ctx.ac[1], galaxy::dsp_product_value(ctx.prod), ctx.sr);"
        ));
        assert!(generated
            .cpp
            .contains("ctx.ac[1] = accumulator_0001.value;"));
        assert!(generated.cpp.contains("ctx.sr = accumulator_0001.status;"));
    }

    #[test]
    fn lowers_add_rounded_product_and_ax_clear_low_through_runtime_helper() {
        let generated =
            lower_dsp_program_to_cpp(0x0000, &[0xfb00, 0x0021], "dsp_addpaxz_test").unwrap();

        assert_eq!(generated.instruction_count, 2);
        assert!(generated.cpp.contains(
            "const auto accumulator_0000 = galaxy::dsp_accumulator_add_rounded_product_and_ax_clear_low(ctx.prod, ctx.ax[1][0], ctx.ax[1][1], ctx.sr);"
        ));
        assert!(generated
            .cpp
            .contains("ctx.ac[1] = accumulator_0000.value;"));
        assert!(generated.cpp.contains("ctx.sr = accumulator_0000.status;"));
        assert!(generated.cpp.contains("    goto pc_0001;\n"));
    }

    #[test]
    fn lowers_immediate_logic_operations_through_runtime_alu_helpers() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x0220, 0x00ff, 0x0240, 0x0f0f, 0x0260, 0xf000, 0x0021],
            "dsp_immediate_logic_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 4);
        assert!(generated.cpp.contains(
            "const auto logic_0000 = galaxy::dsp_accumulator_mid_xor(ctx.ac[0], 0x00FFu, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto logic_0002 = galaxy::dsp_accumulator_mid_and(ctx.ac[0], 0x0F0Fu, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto logic_0004 = galaxy::dsp_accumulator_mid_or(ctx.ac[0], 0xF000u, ctx.sr);"
        ));
        assert!(generated
            .cpp
            .contains("ctx.ac[0] = logic_0004.accumulator;"));
        assert!(generated.cpp.contains("ctx.sr = logic_0004.status;"));
    }

    #[test]
    fn lowers_immediate_arithmetic_and_field_tests_through_runtime_helpers() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[
                0x047f, 0x0580, 0x07ff, 0x0200, 0x8000, 0x0380, 0x0001, 0x02a0, 0x00f0, 0x03c0,
                0x00f0, 0x0021,
            ],
            "dsp_immediate_arithmetic_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 8);
        assert!(generated.cpp.contains(
            "const auto accumulator_0000 = galaxy::dsp_accumulator_add(ctx.ac[0], 8323072ll, ctx.sr);"
        ));
        assert!(generated
            .cpp
            .contains("ctx.ac[0] = accumulator_0000.value;"));
        assert!(generated.cpp.contains(
            "const auto accumulator_0001 = galaxy::dsp_accumulator_add(ctx.ac[1], -8388608ll, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0002 = galaxy::dsp_accumulator_subtract(ctx.ac[1], -65536ll, ctx.sr);"
        ));
        assert!(generated.cpp.contains("ctx.sr = accumulator_0002.status;"));
        assert!(generated.cpp.contains(
            "const auto accumulator_0003 = galaxy::dsp_accumulator_add(ctx.ac[0], galaxy::dsp_accumulator_from_signed_mid(0x8000u), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "const auto accumulator_0005 = galaxy::dsp_accumulator_subtract(ctx.ac[1], galaxy::dsp_accumulator_from_signed_mid(0x0001u), ctx.sr);"
        ));
        assert!(generated.cpp.contains("ctx.sr = accumulator_0005.status;"));
        assert!(generated.cpp.contains(
            "ctx.sr = galaxy::dsp_logic_zero_status((galaxy::dsp_read_accumulator_mid_raw(ctx.ac[0]) & 0x00F0u) == 0u, ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "ctx.sr = galaxy::dsp_logic_zero_status((galaxy::dsp_read_accumulator_mid_raw(ctx.ac[1]) & 0x00F0u) == 0x00F0u, ctx.sr);"
        ));
        assert!(generated.cpp.contains("    goto pc_000B;\n"));
    }

    #[test]
    fn lowers_static_and_direct_page_data_transfers_through_context_helpers() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[
                0x00d8, 0x0123, 0x00f8, 0x0124, 0x1601, 0xbeef, 0x2004, 0x00f0, 0x0125, 0x2906,
                0x0021,
            ],
            "dsp_static_dram_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 7);
        assert!(generated.cpp.contains(
            "ctx.ax[0][0] = static_cast<std::uint16_t>(galaxy::dsp_data_read(ctx, 0x0123u));"
        ));
        assert!(generated.cpp.contains(
            "galaxy::dsp_data_write(ctx, 0x0124u, static_cast<std::uint16_t>(ctx.ax[0][0]));"
        ));
        assert!(generated.cpp.contains(
            "galaxy::dsp_data_write(ctx, 0x0001u, static_cast<std::uint16_t>(0xBEEFu));"
        ));
        assert!(generated.cpp.contains(
            "ctx.ax[0][0] = static_cast<std::uint16_t>(galaxy::dsp_data_read(ctx, static_cast<std::uint16_t>((ctx.cr << 8u) | 0x04u)));"
        ));
        assert!(generated.cpp.contains(
            "galaxy::dsp_data_write(ctx, 0x0125u, static_cast<std::uint16_t>(galaxy::dsp_read_accumulator_high(ctx.ac[0])));"
        ));
        assert!(generated.cpp.contains(
            "galaxy::dsp_data_write(ctx, static_cast<std::uint16_t>((ctx.cr << 8u) | 0x06u), static_cast<std::uint16_t>(galaxy::dsp_read_accumulator_high(ctx.ac[1])));"
        ));
    }

    #[test]
    fn lowers_static_coefficient_rom_reads_through_data_memory_helper() {
        let generated =
            lower_dsp_program_to_cpp(0x0000, &[0x00d8, 0x1803, 0x0021], "dsp_static_coef_test")
                .unwrap();

        assert_eq!(generated.instruction_count, 2);
        assert!(generated.cpp.contains(
            "ctx.ax[0][0] = static_cast<std::uint16_t>(galaxy::dsp_data_read(ctx, 0x1003u));"
        ));
    }

    #[test]
    fn lowers_static_ifx_accesses_through_data_memory_helper() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x00d8, 0xfffc, 0x00f8, 0xfffd, 0x16fc, 0xdcd1, 0x0021],
            "dsp_static_ifx_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 4);
        assert!(generated.cpp.contains(
            "ctx.ax[0][0] = static_cast<std::uint16_t>(galaxy::dsp_data_read(ctx, 0xFFFCu));"
        ));
        assert!(generated.cpp.contains(
            "galaxy::dsp_data_write(ctx, 0xFFFDu, static_cast<std::uint16_t>(ctx.ax[0][0]));"
        ));
        assert!(generated.cpp.contains(
            "galaxy::dsp_data_write(ctx, 0xFFFCu, static_cast<std::uint16_t>(0xDCD1u));"
        ));
    }

    #[test]
    fn lowers_indirect_dram_transfers_without_address_update() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x1818, 0x1a18, 0x1a10, 0x0021],
            "dsp_indirect_dram_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 4);
        assert!(generated.cpp.contains(
            "ctx.ax[0][0] = static_cast<std::uint16_t>(galaxy::dsp_data_read(ctx, ctx.ar[0]));"
        ));
        assert!(generated.cpp.contains(
            "galaxy::dsp_data_write(ctx, ctx.ar[0], static_cast<std::uint16_t>(ctx.ax[0][0]));"
        ));
        assert!(generated.cpp.contains(
            "galaxy::dsp_data_write(ctx, ctx.ar[0], static_cast<std::uint16_t>(galaxy::dsp_read_accumulator_high(ctx.ac[0])));"
        ));
    }

    #[test]
    fn lowers_indirect_dram_transfers_with_address_updates() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x18fb, 0x1936, 0x1af8, 0x1b9f, 0x0021],
            "dsp_indirect_dram_update_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 5);
        assert!(generated.cpp.contains(
            "ctx.ax[1][1] = static_cast<std::uint16_t>(galaxy::dsp_data_read(ctx, ctx.ar[3]));"
        ));
        assert!(generated
            .cpp
            .contains("ctx.ar[3] = galaxy::dsp_decrement_address(ctx.ar[3], ctx.wr[3]);"));
        assert!(generated.cpp.contains(
            "ctx.prod.high = static_cast<std::uint16_t>(galaxy::dsp_data_read(ctx, ctx.ar[1]));"
        ));
        assert!(generated
            .cpp
            .contains("ctx.ar[1] = galaxy::dsp_increment_address(ctx.ar[1], ctx.wr[1]);"));
        assert!(generated.cpp.contains(
            "galaxy::dsp_data_write(ctx, ctx.ar[3], static_cast<std::uint16_t>(ctx.ax[0][0]));"
        ));
        assert!(generated.cpp.contains(
            "galaxy::dsp_data_write(ctx, ctx.ar[0], static_cast<std::uint16_t>(galaxy::dsp_read_accumulator_mid(ctx.ac[1], ctx.sr)));"
        ));
        assert!(generated.cpp.contains(
            "ctx.ar[0] = galaxy::dsp_increase_address(ctx.ar[0], ctx.ix[0], ctx.wr[0]);"
        ));
    }

    #[test]
    fn lowers_accumulator_low_and_mid_register_views_through_alu_helpers() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[
                0x009c, 0x5678, 0x009e, 0x1234, 0x2401, 0x2602, 0x2c20, 0x2e21, 0x0021,
            ],
            "dsp_accumulator_part_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 7);
        assert!(generated.cpp.contains(
            "ctx.ac[0] = galaxy::dsp_write_accumulator_low(ctx.ac[0], static_cast<std::uint16_t>(0x5678u));"
        ));
        assert!(generated.cpp.contains(
            "ctx.ac[0] = galaxy::dsp_write_accumulator_mid(ctx.ac[0], static_cast<std::uint16_t>(0x1234u), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "ctx.ac[0] = galaxy::dsp_write_accumulator_low(ctx.ac[0], static_cast<std::uint16_t>(galaxy::dsp_data_read(ctx, static_cast<std::uint16_t>((ctx.cr << 8u) | 0x01u))));"
        ));
        assert!(generated.cpp.contains(
            "ctx.ac[0] = galaxy::dsp_write_accumulator_mid(ctx.ac[0], static_cast<std::uint16_t>(galaxy::dsp_data_read(ctx, static_cast<std::uint16_t>((ctx.cr << 8u) | 0x02u))), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "galaxy::dsp_data_write(ctx, static_cast<std::uint16_t>((ctx.cr << 8u) | 0x20u), static_cast<std::uint16_t>(galaxy::dsp_read_accumulator_low(ctx.ac[0])));"
        ));
        assert!(generated.cpp.contains(
            "galaxy::dsp_data_write(ctx, static_cast<std::uint16_t>((ctx.cr << 8u) | 0x21u), static_cast<std::uint16_t>(galaxy::dsp_read_accumulator_mid(ctx.ac[0], ctx.sr)));"
        ));
    }

    #[test]
    fn lowers_register_immediate_loads_to_supported_context_registers() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[
                0x0084, 0xffff, 0x0090, 0xff80, 0x0092, 0x1234, 0x0093, 0xa5a5, 0x009b, 0x1234,
                0x0021,
            ],
            "dsp_register_load_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 6);
        assert!(generated
            .cpp
            .contains("ctx.ix[0] = static_cast<std::int16_t>(0xFFFFu);"));
        assert!(generated.cpp.contains(
            "ctx.ac[0] = galaxy::dsp_write_accumulator_high(ctx.ac[0], static_cast<std::uint16_t>(0xFF80u));"
        ));
        assert!(generated
            .cpp
            .contains("ctx.cr = static_cast<std::uint16_t>(0x1234u);"));
        assert!(generated
            .cpp
            .contains("ctx.sr = static_cast<std::uint16_t>(0xA5A5u);"));
        assert!(generated
            .cpp
            .contains("ctx.ax[1][1] = static_cast<std::uint16_t>(0x1234u);"));
    }

    #[test]
    fn lowers_short_register_loads_to_ax_halves() {
        let generated =
            lower_dsp_program_to_cpp(0x0000, &[0x0801, 0x0bff, 0x0021], "dsp_short_load_test")
                .unwrap();

        assert_eq!(generated.instruction_count, 3);
        assert!(generated
            .cpp
            .contains("ctx.ax[0][0] = static_cast<std::uint16_t>(0x0001u);"));
        assert!(generated
            .cpp
            .contains("ctx.ax[1][1] = static_cast<std::uint16_t>(0xFFFFu);"));
    }

    #[test]
    fn short_register_loads_sign_extend_all_destination_parts() {
        // LRIS encodes a signed byte even though the decoded identity keeps u8.
        // Literal 16-bit goldens cover positive, negative-limit, and minus-one.
        for (byte, expected) in [(0x7Fu16, "007F"), (0x80, "FF80"), (0xFF, "FFFF")] {
            for (base, destination) in [
                (0x0800u16, "ctx.ax[0][0] = static_cast<std::uint16_t>"),
                (0x0900, "ctx.ax[1][0] = static_cast<std::uint16_t>"),
                (0x0A00, "ctx.ax[0][1] = static_cast<std::uint16_t>"),
                (0x0B00, "ctx.ax[1][1] = static_cast<std::uint16_t>"),
            ] {
                let generated =
                    lower_dsp_program_to_cpp(0, &[base | byte, 0x0021], "signed_lris_ax").unwrap();
                assert!(generated
                    .cpp
                    .contains(&format!("{destination}(0x{expected}u);")));
            }
            for (base, accumulator, part) in [
                (0x0C00u16, 0, "low"),
                (0x0D00, 1, "low"),
                (0x0E00, 0, "mid"),
                (0x0F00, 1, "mid"),
            ] {
                let generated =
                    lower_dsp_program_to_cpp(0, &[base | byte, 0x0021], "signed_lris_ac").unwrap();
                let status = if part == "mid" { ", ctx.sr" } else { "" };
                assert!(generated.cpp.contains(&format!(
                    "ctx.ac[{accumulator}] = galaxy::dsp_write_accumulator_{part}(ctx.ac[{accumulator}], static_cast<std::uint16_t>(0x{expected}u){status});"
                )));
            }
        }
    }

    #[test]
    fn lowers_supported_register_moves_to_context_assignments() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[0x1d5b, 0x1eca, 0x1e18, 0x1f10, 0x0021],
            "dsp_register_move_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 5);
        assert!(generated
            .cpp
            .contains("ctx.wr[2] = static_cast<std::uint16_t>(ctx.ax[1][1]);"));
        assert!(generated
            .cpp
            .contains("ctx.prod.high = static_cast<std::uint16_t>(ctx.wr[2]);"));
        assert!(generated.cpp.contains(
            "ctx.ac[0] = galaxy::dsp_write_accumulator_high(ctx.ac[0], static_cast<std::uint16_t>(ctx.ax[0][0]));"
        ));
        assert!(generated.cpp.contains(
            "ctx.ax[0][0] = static_cast<std::uint16_t>(galaxy::dsp_read_accumulator_high(ctx.ac[0]));"
        ));
    }

    #[test]
    fn lowers_stack_and_control_register_transfers_through_context_helpers() {
        let generated = lower_dsp_program_to_cpp(
            0x0000,
            &[
                0x008c, 0x1111, 0x00ec, 0x0123, 0x1f0c, 0x1e58, 0x1f12, 0x0021,
            ],
            "dsp_stack_control_register_test",
        )
        .unwrap();

        assert_eq!(generated.instruction_count, 6);
        assert!(generated
            .cpp
            .contains("galaxy::dsp_stack_push(ctx, 0, static_cast<std::uint16_t>(0x1111u));"));
        assert!(generated.cpp.contains(
            "galaxy::dsp_data_write(ctx, 0x0123u, static_cast<std::uint16_t>(galaxy::dsp_stack_pop(ctx, 0)));"
        ));
        assert!(generated
            .cpp
            .contains("ctx.ax[0][0] = static_cast<std::uint16_t>(galaxy::dsp_stack_pop(ctx, 0));"));
        assert!(generated
            .cpp
            .contains("ctx.cr = static_cast<std::uint16_t>(ctx.ax[0][0]);"));
        assert!(generated
            .cpp
            .contains("ctx.ax[0][0] = static_cast<std::uint16_t>(ctx.cr);"));
    }

    #[test]
    fn lowering_hard_fails_invalid_cpp_function_name() {
        assert_eq!(
            lower_dsp_program_to_cpp(0x0000, &[0x0021], "1_bad"),
            Err(DspLoweringError::InvalidFunctionName {
                name: "1_bad".to_string(),
            })
        );
    }

    #[test]
    fn lowering_hard_fails_when_loop_body_is_not_lowered() {
        assert_eq!(
            lower_dsp_program_to_cpp(0x0000, &[0x0040], "bad_loop_body_test"),
            Err(DspLoweringError::UnsupportedInstruction {
                address: 0x0000,
                word: 0x0040,
                reason: "loop body outside lowered DSP program",
            })
        );
    }

    #[test]
    fn lowering_hard_fails_when_block_loop_end_is_not_lowered() {
        assert_eq!(
            lower_dsp_program_to_cpp(0x0000, &[0x1101, 0x0005, 0x0021], "bad_block_loop_end_test"),
            Err(DspLoweringError::UnsupportedInstruction {
                address: 0x0000,
                word: 0x1101,
                reason: "block loop end outside lowered DSP program",
            })
        );
    }

    #[test]
    fn lowering_hard_fails_when_zero_count_block_loop_skip_is_not_lowered() {
        assert_eq!(
            lower_dsp_program_to_cpp(
                0x0000,
                &[0x1100, 0x0002, 0x0000],
                "bad_block_loop_skip_test"
            ),
            Err(DspLoweringError::UnsupportedInstruction {
                address: 0x0000,
                word: 0x1100,
                reason: "zero-count block loop skip target outside lowered DSP program",
            })
        );
    }

    #[test]
    fn lowering_hard_fails_for_static_read_only_transfer_address() {
        assert_eq!(
            lower_dsp_program_to_cpp(0x0000, &[0x00f8, 0x1000], "bad_static_memory_test"),
            Err(DspLoweringError::UnsupportedInstruction {
                address: 0x0000,
                word: 0x00f8,
                reason: "static DSP data write address is read-only",
            })
        );
    }

    #[test]
    fn lowers_instruction_memory_loads_from_user_supplied_iram() {
        let generated =
            lower_dsp_program_to_cpp(0x0000, &[0x031c, 0x0021], "dsp_iram_load_test").unwrap();

        assert_eq!(generated.instruction_count, 2);
        assert!(generated.cpp.contains(
            "ctx.ac[1] = galaxy::dsp_write_accumulator_mid(ctx.ac[1], galaxy::dsp_iram_read(ctx, ctx.ar[0]), ctx.sr);"
        ));
        assert!(generated.cpp.contains(
            "ctx.ar[0] = galaxy::dsp_increase_address(ctx.ar[0], ctx.ix[0], ctx.wr[0]);"
        ));
    }

    #[test]
    fn lowering_hard_fails_when_fallthrough_leaves_program() {
        assert_eq!(
            lower_dsp_program_to_cpp(0x0000, &[0x0000], "fallthrough_dsp_test"),
            Err(DspLoweringError::UnsupportedInstruction {
                address: 0x0000,
                word: 0x0000,
                reason: "fallthrough leaves lowered DSP program",
            })
        );
    }

    #[test]
    fn lowering_hard_fails_when_jump_target_is_not_lowered() {
        assert_eq!(
            lower_dsp_program_to_cpp(0x0000, &[0x029f, 0x8000], "jump_outside_dsp_test"),
            Err(DspLoweringError::UnsupportedInstruction {
                address: 0x0000,
                word: 0x029f,
                reason: "jump target outside lowered DSP program",
            })
        );
    }

    #[test]
    fn lowering_hard_fails_when_conditional_jump_fallthrough_is_not_lowered() {
        assert_eq!(
            lower_dsp_program_to_cpp(0x0000, &[0x0295, 0x0000], "jump_no_fallthrough_dsp_test"),
            Err(DspLoweringError::UnsupportedInstruction {
                address: 0x0000,
                word: 0x0295,
                reason: "conditional jump fallthrough leaves lowered DSP program",
            })
        );
    }

    #[test]
    fn lowering_hard_fails_when_if_skip_target_is_not_lowered() {
        assert_eq!(
            lower_dsp_program_to_cpp(0x0000, &[0x0275, 0x0021], "if_no_skip_target_dsp_test"),
            Err(DspLoweringError::UnsupportedInstruction {
                address: 0x0000,
                word: 0x0275,
                reason: "conditional IF skip target outside lowered DSP program",
            })
        );
    }
}
