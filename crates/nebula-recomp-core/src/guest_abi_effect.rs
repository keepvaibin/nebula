//! Conservative guest-state effects for future translated register residency.
//!
//! Adapted from WiiCompiled's GPLv3 `GuestAbiContractAnalyzer.cs` and
//! `GuestHelperEffectCatalog.cs` at commit
//! 83463764b8acda394e058b0c689a10b8561fc380
//! (https://github.com/patchzyy/Wiicompiled). The CFG meet/transfer algorithm,
//! helper effect families, and possible-versus-definite write distinction come
//! from those files. This Rust representation takes decoded effects rather than
//! WiiCompiled's Mario Kart IR and does not assume its helper names are safe in
//! Galaxy. Callers must explicitly verify a helper class; unknown helpers
//! fence and may access the complete architectural state.

use std::ops::{BitOr, BitOrAssign};

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct GuestRegisterSet {
    pub gpr: u32,
    pub fpr: u32,
    /// Gekko paired-single lane 1; scalar loads may also replace this lane.
    pub ps1: u32,
    /// Eight four-bit CR fields, not 32 independent bits.
    pub cr: u8,
    pub xer: bool,
    pub ctr: bool,
    pub lr: bool,
    pub fpscr: bool,
    pub gqr: u8,
    pub hid: u8,
}

impl GuestRegisterSet {
    pub const FULL: Self = Self {
        gpr: u32::MAX,
        fpr: u32::MAX,
        ps1: u32::MAX,
        cr: u8::MAX,
        xer: true,
        ctr: true,
        lr: true,
        fpscr: true,
        gqr: u8::MAX,
        hid: 0b111,
    };

    #[must_use]
    pub fn union(self, other: Self) -> Self {
        Self {
            gpr: self.gpr | other.gpr,
            fpr: self.fpr | other.fpr,
            ps1: self.ps1 | other.ps1,
            cr: self.cr | other.cr,
            xer: self.xer || other.xer,
            ctr: self.ctr || other.ctr,
            lr: self.lr || other.lr,
            fpscr: self.fpscr || other.fpscr,
            gqr: self.gqr | other.gqr,
            hid: self.hid | other.hid,
        }
    }

    #[must_use]
    pub fn intersect(self, other: Self) -> Self {
        Self {
            gpr: self.gpr & other.gpr,
            fpr: self.fpr & other.fpr,
            ps1: self.ps1 & other.ps1,
            cr: self.cr & other.cr,
            xer: self.xer && other.xer,
            ctr: self.ctr && other.ctr,
            lr: self.lr && other.lr,
            fpscr: self.fpscr && other.fpscr,
            gqr: self.gqr & other.gqr,
            hid: self.hid & other.hid,
        }
    }

    #[must_use]
    pub fn except(self, other: Self) -> Self {
        Self {
            gpr: self.gpr & !other.gpr,
            fpr: self.fpr & !other.fpr,
            ps1: self.ps1 & !other.ps1,
            cr: self.cr & !other.cr,
            xer: self.xer && !other.xer,
            ctr: self.ctr && !other.ctr,
            lr: self.lr && !other.lr,
            fpscr: self.fpscr && !other.fpscr,
            gqr: self.gqr & !other.gqr,
            hid: self.hid & !other.hid,
        }
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct GuestBoundaryFlags(u8);

impl GuestBoundaryFlags {
    pub const NONE: Self = Self(0);
    pub const REQUIRES_COMPLETE_CONTEXT: Self = Self(1 << 0);
    pub const CAN_SUSPEND: Self = Self(1 << 1);
    pub const CAN_SWITCH_THREADS: Self = Self(1 << 2);
    pub const INVOKES_GUEST_CODE: Self = Self(1 << 3);

    #[must_use]
    pub const fn contains(self, other: Self) -> bool {
        self.0 & other.0 == other.0
    }
}

impl BitOr for GuestBoundaryFlags {
    type Output = Self;

    fn bitor(self, rhs: Self) -> Self::Output {
        Self(self.0 | rhs.0)
    }
}

impl BitOrAssign for GuestBoundaryFlags {
    fn bitor_assign(&mut self, rhs: Self) {
        self.0 |= rhs.0;
    }
}

/// One decoded instruction's *architectural* effects. A conditional write
/// belongs in `possible_writes` only. Guest calls may take the read-before-write,
/// possible-write, and definite-write sets of a separately analyzed callee;
/// unknown calls must use `unknown_boundary`.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct GuestInstructionEffect {
    pub reads: GuestRegisterSet,
    pub possible_writes: GuestRegisterSet,
    pub definite_writes: GuestRegisterSet,
    pub boundary: GuestBoundaryFlags,
    pub direct_target: Option<u32>,
}

impl GuestInstructionEffect {
    #[must_use]
    pub const fn unknown_boundary() -> Self {
        Self {
            reads: GuestRegisterSet::FULL,
            possible_writes: GuestRegisterSet::FULL,
            definite_writes: GuestRegisterSet {
                gpr: 0,
                fpr: 0,
                ps1: 0,
                cr: 0,
                xer: false,
                ctr: false,
                lr: false,
                fpscr: false,
                gqr: 0,
                hid: 0,
            },
            boundary: GuestBoundaryFlags::REQUIRES_COMPLETE_CONTEXT,
            direct_target: None,
        }
    }

    /// Native helpers have possible hidden writes, but never prove a write on
    /// every path. Their explicit destination may be supplied separately.
    #[must_use]
    pub fn from_helper(effect: GuestHelperEffect, explicit_destination: GuestRegisterSet) -> Self {
        Self {
            reads: effect.reads,
            possible_writes: effect.writes.union(explicit_destination),
            definite_writes: explicit_destination,
            boundary: effect.boundary,
            direct_target: None,
        }
    }

    #[must_use]
    pub fn from_callee(
        target: u32,
        callee: &GuestAbiContract,
        explicit_destination: GuestRegisterSet,
    ) -> Self {
        Self {
            reads: callee.read_before_write,
            possible_writes: callee.possible_writes.union(explicit_destination),
            definite_writes: callee.definite_writes.union(explicit_destination),
            boundary: callee.boundary,
            direct_target: Some(target),
        }
    }
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct GuestEffectBlock {
    /// Indices of successor blocks. An empty list is a function exit.
    pub successors: Vec<usize>,
    pub instructions: Vec<GuestInstructionEffect>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum GuestEffectAnalysisError {
    InvalidEntry,
    InvalidSuccessor { block: usize, successor: usize },
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct GuestAbiContract {
    pub read_before_write: GuestRegisterSet,
    pub possible_writes: GuestRegisterSet,
    pub definite_writes: GuestRegisterSet,
    pub boundary: GuestBoundaryFlags,
    pub direct_targets: Vec<u32>,
}

/// Port of WiiCompiled `CxxLinearCodeGenerator.RegisterResidency.cs`'s
/// `ResidencyBoundarySync.FromCalleeContract`: flush state a callee may read
/// before replacing or may write, then reload what it may write. Galaxy keeps
/// FPSCR, GQR, and HID in this policy as well because future local residency
/// must not assume WiiCompiled's context-owned subset. A full fence overrides
/// every selective mask, including state represented outside these masks.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct GuestResidencyBoundarySync {
    pub flush: GuestRegisterSet,
    pub reload: GuestRegisterSet,
    pub complete_context: bool,
}

impl GuestResidencyBoundarySync {
    #[must_use]
    pub const fn full() -> Self {
        Self {
            flush: GuestRegisterSet::FULL,
            reload: GuestRegisterSet::FULL,
            complete_context: true,
        }
    }

    #[must_use]
    pub fn from_optional_callee_contract(contract: Option<&GuestAbiContract>) -> Self {
        contract.map_or_else(Self::full, Self::from_callee_contract)
    }

    #[must_use]
    pub fn from_callee_contract(contract: &GuestAbiContract) -> Self {
        if contract
            .boundary
            .contains(GuestBoundaryFlags::REQUIRES_COMPLETE_CONTEXT)
        {
            return Self::full();
        }
        let mut flush = contract.read_before_write.union(contract.possible_writes);
        // A callback or interrupt can inspect the architectural stack pointer
        // and XER even when the ordinary callee contract does not mention them.
        flush.gpr |= 1u32 << 1;
        flush.xer = true;
        Self {
            flush,
            reload: contract.possible_writes,
            complete_context: false,
        }
    }
}

/// WiiCompiled's forward definite-write meet (intersection at joins), followed
/// by a read-before-write pass. The entry has no prior definitions even when a
/// back-edge reaches it. Only producer-supplied, legal CFG successors may be
/// used; a missing target is an error instead of an optimistic analysis.
pub fn analyze_guest_abi(
    blocks: &[GuestEffectBlock],
    entry: usize,
) -> Result<GuestAbiContract, GuestEffectAnalysisError> {
    if entry >= blocks.len() {
        return Err(GuestEffectAnalysisError::InvalidEntry);
    }

    let mut predecessors = vec![Vec::<usize>::new(); blocks.len()];
    for (block_index, block) in blocks.iter().enumerate() {
        for &successor in &block.successors {
            if successor >= blocks.len() {
                return Err(GuestEffectAnalysisError::InvalidSuccessor {
                    block: block_index,
                    successor,
                });
            }
            predecessors[successor].push(block_index);
        }
    }

    let block_writes: Vec<_> = blocks
        .iter()
        .map(|block| {
            block
                .instructions
                .iter()
                .fold(GuestRegisterSet::default(), |writes, instruction| {
                    writes.union(instruction.definite_writes)
                })
        })
        .collect();
    let mut definite_in = vec![GuestRegisterSet::default(); blocks.len()];
    let mut definite_out = definite_in.clone();
    loop {
        let mut changed = false;
        for index in 0..blocks.len() {
            let incoming = if index == entry || predecessors[index].is_empty() {
                GuestRegisterSet::default()
            } else {
                let mut incoming = definite_out[predecessors[index][0]];
                for &predecessor in &predecessors[index][1..] {
                    incoming = incoming.intersect(definite_out[predecessor]);
                }
                incoming
            };
            let outgoing = incoming.union(block_writes[index]);
            if incoming != definite_in[index] || outgoing != definite_out[index] {
                definite_in[index] = incoming;
                definite_out[index] = outgoing;
                changed = true;
            }
        }
        if !changed {
            break;
        }
    }

    let mut contract = GuestAbiContract::default();
    for (index, block) in blocks.iter().enumerate() {
        let mut written = definite_in[index];
        for instruction in &block.instructions {
            contract.read_before_write = contract
                .read_before_write
                .union(instruction.reads.except(written));
            contract.possible_writes = contract
                .possible_writes
                .union(instruction.possible_writes)
                .union(instruction.definite_writes);
            written = written.union(instruction.definite_writes);
            contract.boundary |= instruction.boundary;
            if let Some(target) = instruction.direct_target {
                contract.direct_targets.push(target);
            }
        }
    }
    contract.direct_targets.sort_unstable();
    contract.direct_targets.dedup();

    let mut exit_writes = None::<GuestRegisterSet>;
    for (index, block) in blocks.iter().enumerate() {
        if block.successors.is_empty() {
            exit_writes = Some(match exit_writes {
                None => definite_out[index],
                Some(prior) => prior.intersect(definite_out[index]),
            });
        }
    }
    contract.definite_writes = exit_writes.unwrap_or_default();
    Ok(contract)
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct GuestHelperEffect {
    pub reads: GuestRegisterSet,
    pub writes: GuestRegisterSet,
    pub boundary: GuestBoundaryFlags,
}

impl GuestHelperEffect {
    #[must_use]
    pub const fn complete(extra: GuestBoundaryFlags) -> Self {
        Self {
            reads: GuestRegisterSet::FULL,
            writes: GuestRegisterSet::FULL,
            boundary: GuestBoundaryFlags(extra.0 | GuestBoundaryFlags::REQUIRES_COMPLETE_CONTEXT.0),
        }
    }
}

/// Caller-verified semantic family. WiiCompiled's helper-name allowlist is
/// deliberately not installed as a Galaxy allowlist: identically named native
/// functions need not have identical side effects in the two runtimes.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum VerifiedHelperClass {
    Pure,
    CarryRead,
    CarryUpdate,
    StringLoadImmediate,
    StringStoreImmediate,
    StringLoadDynamic,
    StringStoreDynamic,
    MoveCrFromXer,
    MoveCrField,
    MoveCrFromFpscr,
    SetCrBit,
    CrLogical,
    CompareCrField,
    ReadSpr,
    WriteSpr,
    ConditionalStore,
    SystemCall,
    Unknown,
}

fn cr_field(argument: Option<i64>) -> u8 {
    argument.map_or(u8::MAX, |field| 1u8 << ((field as u8) & 7))
}

fn cr_bit_field(argument: Option<i64>) -> u8 {
    argument.map_or(u8::MAX, |bit| 1u8 << (((bit as u8) & 31) / 4))
}

fn string_registers(arguments: &[Option<i64>]) -> u32 {
    let (Some(Some(start)), Some(Some(count))) = (arguments.first(), arguments.get(2)) else {
        return u32::MAX;
    };
    let registers = if *count == 0 {
        8usize
    } else if *count > 0 && *count <= 128 {
        (*count as usize).div_ceil(4)
    } else {
        return u32::MAX;
    };
    let start = (*start as u32) & 31;
    (0..registers).fold(0u32, |mask, index| {
        mask | (1u32 << ((start + index as u32) & 31))
    })
}

/// Port of WiiCompiled's argument-dependent hidden-effect rules. The caller
/// supplies a *verified* class, not merely a similarly spelled helper name.
/// Unknown helpers conservatively read/may-write all represented state.
#[must_use]
pub fn classify_verified_helper(
    class: VerifiedHelperClass,
    arguments: &[Option<i64>],
) -> GuestHelperEffect {
    use VerifiedHelperClass as Class;
    let mut effect = GuestHelperEffect::default();
    match class {
        Class::Pure => {}
        Class::CarryRead => effect.reads.xer = true,
        Class::CarryUpdate => {
            effect.reads.xer = true;
            effect.writes.xer = true;
        }
        Class::StringLoadImmediate => effect.writes.gpr = string_registers(arguments),
        Class::StringStoreImmediate => effect.reads.gpr = string_registers(arguments),
        Class::StringLoadDynamic => {
            effect.writes.gpr = u32::MAX;
            effect.reads.xer = true;
        }
        Class::StringStoreDynamic => {
            effect.reads.gpr = u32::MAX;
            effect.reads.xer = true;
        }
        Class::MoveCrFromXer => {
            effect.reads.cr = u8::MAX;
            effect.reads.xer = true;
            effect.writes.cr = cr_field(arguments.first().copied().flatten());
            effect.writes.xer = true;
        }
        Class::MoveCrField | Class::CompareCrField => {
            effect.reads.cr = u8::MAX;
            effect.writes.cr = cr_field(arguments.first().copied().flatten());
        }
        Class::MoveCrFromFpscr => {
            effect.reads.cr = u8::MAX;
            effect.reads.fpscr = true;
            effect.writes.cr = cr_field(arguments.first().copied().flatten());
        }
        Class::SetCrBit => {
            effect.reads.cr = u8::MAX;
            effect.writes.cr = cr_bit_field(arguments.first().copied().flatten());
        }
        Class::CrLogical => {
            effect.reads.cr = u8::MAX;
            effect.writes.cr = cr_bit_field(arguments.get(1).copied().flatten());
        }
        Class::ReadSpr | Class::WriteSpr => {
            let Some(Some(spr)) = arguments.first() else {
                return GuestHelperEffect::complete(GuestBoundaryFlags::NONE);
            };
            let touched = if class == Class::ReadSpr {
                &mut effect.reads
            } else {
                &mut effect.writes
            };
            match *spr {
                1 => touched.xer = true,
                8 => touched.lr = true,
                9 => touched.ctr = true,
                _ => {
                    // Unrecognized SPRs can include GQR, HID, and control
                    // state; unlike WiiCompiled's context-owned assumption,
                    // Galaxy must fence until the exact SPR is proven.
                    return GuestHelperEffect::complete(GuestBoundaryFlags::NONE);
                }
            }
        }
        Class::ConditionalStore => {
            effect.reads.cr = u8::MAX;
            effect.reads.xer = true;
            effect.writes.cr = 1;
        }
        Class::SystemCall => {
            return GuestHelperEffect::complete(
                GuestBoundaryFlags::CAN_SUSPEND
                    | GuestBoundaryFlags::CAN_SWITCH_THREADS
                    | GuestBoundaryFlags::INVOKES_GUEST_CODE,
            );
        }
        Class::Unknown => return GuestHelperEffect::complete(GuestBoundaryFlags::NONE),
    }
    effect
}

#[cfg(test)]
mod tests {
    use super::*;

    fn gpr(index: u32) -> GuestRegisterSet {
        GuestRegisterSet {
            gpr: 1u32 << index,
            ..GuestRegisterSet::default()
        }
    }

    #[test]
    fn branch_join_requires_write_on_every_predecessor() {
        let blocks = [
            GuestEffectBlock {
                successors: vec![1, 2],
                ..Default::default()
            },
            GuestEffectBlock {
                successors: vec![3],
                instructions: vec![GuestInstructionEffect {
                    possible_writes: gpr(3),
                    definite_writes: gpr(3),
                    ..Default::default()
                }],
            },
            GuestEffectBlock {
                successors: vec![3],
                ..Default::default()
            },
            GuestEffectBlock {
                instructions: vec![GuestInstructionEffect {
                    reads: gpr(3),
                    ..Default::default()
                }],
                ..Default::default()
            },
        ];
        let result = analyze_guest_abi(&blocks, 0).unwrap();
        assert_eq!(result.read_before_write.gpr, gpr(3).gpr);
        assert_eq!(result.possible_writes.gpr, gpr(3).gpr);
        assert_eq!(result.definite_writes.gpr, 0);
    }

    #[test]
    fn common_write_suppresses_later_read_and_survives_exit_meet() {
        let write = GuestInstructionEffect {
            possible_writes: gpr(4),
            definite_writes: gpr(4),
            ..Default::default()
        };
        let blocks = [
            GuestEffectBlock {
                successors: vec![1, 2],
                ..Default::default()
            },
            GuestEffectBlock {
                instructions: vec![write],
                ..Default::default()
            },
            GuestEffectBlock {
                instructions: vec![write],
                ..Default::default()
            },
        ];
        let result = analyze_guest_abi(&blocks, 0).unwrap();
        assert_eq!(result.read_before_write.gpr, 0);
        assert_eq!(result.definite_writes.gpr, gpr(4).gpr);
    }

    #[test]
    fn unknown_helper_fences_and_does_not_prove_writes() {
        let effect = classify_verified_helper(VerifiedHelperClass::Unknown, &[]);
        let instruction = GuestInstructionEffect::from_helper(effect, GuestRegisterSet::default());
        assert_eq!(instruction.reads, GuestRegisterSet::FULL);
        assert_eq!(instruction.possible_writes, GuestRegisterSet::FULL);
        assert_eq!(instruction.definite_writes, GuestRegisterSet::default());
        assert!(instruction
            .boundary
            .contains(GuestBoundaryFlags::REQUIRES_COMPLETE_CONTEXT));
    }

    #[test]
    fn argument_dependent_cr_and_spr_effects_are_field_precise() {
        let cr = classify_verified_helper(VerifiedHelperClass::CrLogical, &[None, Some(9)]);
        assert_eq!(cr.reads.cr, u8::MAX);
        assert_eq!(cr.writes.cr, 1 << 2);
        let read = classify_verified_helper(VerifiedHelperClass::ReadSpr, &[Some(8)]);
        assert!(read.reads.lr);
        assert!(!read.writes.lr);
        let write = classify_verified_helper(VerifiedHelperClass::WriteSpr, &[Some(8)]);
        assert!(write.writes.lr);
        assert!(!write.reads.lr);
        let unknown = classify_verified_helper(VerifiedHelperClass::ReadSpr, &[Some(912)]);
        assert!(unknown
            .boundary
            .contains(GuestBoundaryFlags::REQUIRES_COMPLETE_CONTEXT));
    }

    #[test]
    fn malformed_cfg_fails_closed() {
        assert_eq!(
            analyze_guest_abi(&[], 0),
            Err(GuestEffectAnalysisError::InvalidEntry)
        );
        assert_eq!(
            analyze_guest_abi(
                &[GuestEffectBlock {
                    successors: vec![1],
                    ..Default::default()
                }],
                0
            ),
            Err(GuestEffectAnalysisError::InvalidSuccessor {
                block: 0,
                successor: 1
            }),
        );
    }

    #[test]
    fn residency_sync_flushes_reads_and_writes_plus_stack_and_xer() {
        let contract = GuestAbiContract {
            read_before_write: GuestRegisterSet {
                gpr: 1 << 3,
                fpr: 1 << 2,
                cr: 1 << 4,
                fpscr: true,
                ..Default::default()
            },
            possible_writes: GuestRegisterSet {
                gpr: 1 << 5,
                gqr: 1 << 6,
                hid: 1 << 1,
                ..Default::default()
            },
            ..Default::default()
        };
        let sync = GuestResidencyBoundarySync::from_callee_contract(&contract);
        assert_eq!(sync.flush.gpr, (1 << 1) | (1 << 3) | (1 << 5));
        assert_eq!(sync.flush.fpr, 1 << 2);
        assert_eq!(sync.flush.cr, 1 << 4);
        assert!(sync.flush.xer && sync.flush.fpscr);
        assert_eq!(sync.flush.gqr, 1 << 6);
        assert_eq!(sync.flush.hid, 1 << 1);
        assert_eq!(sync.reload, contract.possible_writes);
        assert!(!sync.complete_context);
    }

    #[test]
    fn residency_unknown_boundary_forces_full_sync() {
        let contract = GuestAbiContract {
            boundary: GuestBoundaryFlags::REQUIRES_COMPLETE_CONTEXT,
            ..Default::default()
        };
        assert_eq!(
            GuestResidencyBoundarySync::from_callee_contract(&contract),
            GuestResidencyBoundarySync::full(),
        );
        assert_eq!(
            GuestResidencyBoundarySync::from_optional_callee_contract(None),
            GuestResidencyBoundarySync::full(),
        );
    }
}
