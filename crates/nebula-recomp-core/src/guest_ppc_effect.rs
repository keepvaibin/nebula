//! Fail-closed architectural effects for audited RMGE01 instruction forms.
//!
//! The possible/definite write and unknown-call fence policy is adapted from
//! WiiCompiled GPLv3 `GuestAbiContractAnalyzer.cs::{Reads,Writes}` and
//! `GuestHelperEffectCatalog.cs::Analyze`, commit
//! 83463764b8acda394e058b0c689a10b8561fc380
//! (https://github.com/patchzyy/Wiicompiled). PPC bit decoding and Gekko PS1,
//! FPSCR, and checked-memory effects are verified against Galaxy's existing
//! `translate.rs` and native API, not copied from WiiCompiled's different IR.
//! This catalog is inert: it does not select an emitter or waive a checkpoint.

use crate::guest_abi_effect::{
    analyze_guest_abi, GuestAbiContract, GuestBoundaryFlags, GuestEffectAnalysisError,
    GuestEffectBlock, GuestHelperEffect, GuestInstructionEffect, GuestRegisterSet,
};
use crate::guest_cfg_liveness::{
    build_guest_cfg, guest_direct_call_components, GuestCfg, GuestCfgError, GuestCfgFlow,
    GuestCfgSite,
};
use powerpc::{Extensions, Ins, Opcode};
use std::collections::{BTreeMap, BTreeSet};

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum GuestPpcEffectError {
    InvalidLayout,
    Unsupported {
        address: u32,
        word: u32,
        opcode: String,
    },
    ExternalBranch {
        address: u32,
        target: u32,
    },
}

fn gpr(index: u32) -> GuestRegisterSet {
    GuestRegisterSet {
        gpr: 1u32 << index,
        ..Default::default()
    }
}

fn fpr(index: u32) -> GuestRegisterSet {
    GuestRegisterSet {
        fpr: 1u32 << index,
        ..Default::default()
    }
}

fn ps1(index: u32) -> GuestRegisterSet {
    GuestRegisterSet {
        ps1: 1u32 << index,
        ..Default::default()
    }
}

fn rt(word: u32) -> u32 {
    (word >> 21) & 31
}
fn ra(word: u32) -> u32 {
    (word >> 16) & 31
}
fn rb(word: u32) -> u32 {
    (word >> 11) & 31
}
fn fc(word: u32) -> u32 {
    (word >> 6) & 31
}

/// One decoded integer instruction's operand mapping. `None` for the first
/// source means the RA=0 literal-zero encoding of addi/addis, not guest r0.
/// Keeping this mapping shared by the effect recorder and C++ emitter avoids
/// silently treating source and destination fields differently during future
/// register residency work. It deliberately has no register-cache policy.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) struct GuestGprAccess {
    pub destination: u32,
    pub first_source: Option<u32>,
    pub second_source: Option<u32>,
}

impl GuestGprAccess {
    pub fn reads(self) -> GuestRegisterSet {
        let mut registers = GuestRegisterSet::default();
        if let Some(index) = self.first_source {
            registers = registers.union(gpr(index));
        }
        if let Some(index) = self.second_source {
            registers = registers.union(gpr(index));
        }
        registers
    }

    pub fn writes(self) -> GuestRegisterSet {
        gpr(self.destination)
    }
}

/// A coherent, pure integer subset present 37 times in the verified JPA draw
/// stream. Every other opcode returns `None`; callers must keep its existing
/// lowering or reject it, rather than infer field roles from opcode bits.
pub(crate) fn guest_integer_gpr_access(opcode: Opcode, word: u32) -> Option<GuestGprAccess> {
    match opcode {
        Opcode::Addi | Opcode::Addis => Some(GuestGprAccess {
            destination: rt(word),
            first_source: (ra(word) != 0).then(|| ra(word)),
            second_source: None,
        }),
        Opcode::Or => Some(GuestGprAccess {
            destination: ra(word),
            first_source: Some(rt(word)),
            second_source: Some(rb(word)),
        }),
        Opcode::Rlwinm => Some(GuestGprAccess {
            destination: ra(word),
            first_source: Some(rt(word)),
            second_source: None,
        }),
        _ => None,
    }
}
fn spr(word: u32) -> u32 {
    ((word >> 16) & 31) | ((word >> 6) & 0x3e0)
}

fn dbase(word: u32) -> GuestRegisterSet {
    if ra(word) == 0 {
        GuestRegisterSet::default()
    } else {
        gpr(ra(word))
    }
}

fn direct_target(pc: u32, word: u32) -> u32 {
    let displacement = (((word & 0x03ff_fffc) as i32) << 6) >> 6;
    if word & 2 != 0 {
        displacement as u32
    } else {
        pc.wrapping_add(displacement as u32)
    }
}

fn conditional_target(pc: u32, word: u32) -> u32 {
    let displacement = (word as u16 as i16 as i32) & !3;
    if word & 2 != 0 {
        displacement as u32
    } else {
        pc.wrapping_add(displacement as u32)
    }
}

fn written(effect: &mut GuestInstructionEffect, set: GuestRegisterSet) {
    effect.possible_writes = effect.possible_writes.union(set);
    effect.definite_writes = effect.definite_writes.union(set);
}

fn cr_field(word: u32) -> GuestRegisterSet {
    GuestRegisterSet {
        cr: 1u8 << ((word >> 23) & 7),
        ..Default::default()
    }
}

fn cr_bit_field(bit: u32) -> GuestRegisterSet {
    GuestRegisterSet {
        cr: 1u8 << (bit / 4),
        ..Default::default()
    }
}

fn cr0_record(effect: &mut GuestInstructionEffect, word: u32) {
    if word & 1 != 0 {
        effect.reads.xer = true; // XER.SO is copied into CR0.
        written(
            effect,
            GuestRegisterSet {
                cr: 1,
                ..Default::default()
            },
        );
    }
}

fn unsupported(pc: u32, word: u32, opcode: Opcode) -> GuestPpcEffectError {
    GuestPpcEffectError::Unsupported {
        address: pc,
        word,
        opcode: format!("{opcode:?}"),
    }
}

/// Decode one instruction, rejecting every form outside the audited hot
/// function vocabulary. Even a known mnemonic is rejected when its modifiers
/// require effects this catalog has not proven.
pub fn decode_guest_ppc_effect(
    pc: u32,
    word: u32,
    function_start: u32,
    function_end: u32,
) -> Result<GuestCfgSite, GuestPpcEffectError> {
    if pc & 3 != 0
        || function_start & 3 != 0
        || function_end & 3 != 0
        || pc < function_start
        || pc >= function_end
    {
        return Err(GuestPpcEffectError::InvalidLayout);
    }
    let opcode = Ins::new(word, Extensions::gekko_broadway()).op;
    let mut effect = GuestInstructionEffect::default();
    let mut flow = GuestCfgFlow::Continue;
    let mut may_exception = false;
    match opcode {
        Opcode::Addi | Opcode::Addis => {
            let access = guest_integer_gpr_access(opcode, word).expect("classified integer opcode");
            effect.reads = access.reads();
            written(&mut effect, access.writes());
        }
        Opcode::Or => {
            let access = guest_integer_gpr_access(opcode, word).expect("classified integer opcode");
            effect.reads = access.reads();
            written(&mut effect, access.writes());
            cr0_record(&mut effect, word);
        }
        Opcode::Rlwinm => {
            let access = guest_integer_gpr_access(opcode, word).expect("classified integer opcode");
            effect.reads = access.reads();
            written(&mut effect, access.writes());
            cr0_record(&mut effect, word);
        }
        Opcode::Lbz | Opcode::Lwz => {
            effect.reads = dbase(word);
            written(&mut effect, gpr(rt(word)));
            may_exception = true;
        }
        Opcode::Lwzx => {
            effect.reads = dbase(word).union(gpr(rb(word)));
            written(&mut effect, gpr(rt(word)));
            may_exception = true;
        }
        Opcode::Stb | Opcode::Stw | Opcode::Stwu => {
            if opcode == Opcode::Stwu && ra(word) == 0 {
                return Err(unsupported(pc, word, opcode));
            }
            effect.reads = dbase(word).union(gpr(rt(word)));
            if opcode == Opcode::Stwu {
                written(&mut effect, gpr(ra(word)));
            }
            may_exception = true;
        }
        Opcode::Lfs => {
            effect.reads = dbase(word);
            effect.reads.hid |= 0b010; // HID2.PSE determines the PS1 write.
            written(&mut effect, fpr(rt(word)));
            // Galaxy load_fpr_single also writes PS1 when paired singles are enabled.
            effect.possible_writes = effect.possible_writes.union(ps1(rt(word)));
            may_exception = true;
        }
        Opcode::Stfs => {
            effect.reads = dbase(word).union(fpr(rt(word)));
            effect.reads.fpscr = true; // Scalar narrowing uses FPSCR.
            may_exception = true;
        }
        Opcode::Lfd => {
            effect.reads = dbase(word);
            written(&mut effect, fpr(rt(word)));
            may_exception = true;
        }
        Opcode::Stfd => {
            effect.reads = dbase(word).union(fpr(rt(word)));
            may_exception = true;
        }
        Opcode::PsqL | Opcode::PsqSt => {
            // D-form W and I are independent of the signed 12-bit displacement.
            // Galaxy's checked helper tests HID2.PSE/LSQE and reads GQR[I]
            // before accessing guest memory. The second load can fault after
            // FPR0 is written, so neither destination is a definite write.
            let index = (word >> 12) & 7;
            let one_element = word & 0x8000 != 0;
            effect.reads = dbase(word);
            effect.reads.hid |= 0b010;
            effect.reads.gqr |= 1u8 << index;
            if opcode == Opcode::PsqL {
                effect.possible_writes = effect
                    .possible_writes
                    .union(fpr(rt(word)))
                    .union(ps1(rt(word)));
            } else {
                effect.reads = effect.reads.union(fpr(rt(word)));
                if !one_element {
                    effect.reads = effect.reads.union(ps1(rt(word)));
                }
            }
            may_exception = true;
        }
        Opcode::Fmuls => {
            effect.reads = fpr(ra(word)).union(fpr(fc(word)));
            effect.reads.fpscr = true;
            effect.reads.hid |= 0b010; // Commit duplicates PS1 iff HID2.PSE.
            written(&mut effect, fpr(rt(word)));
            effect.possible_writes = effect.possible_writes.union(ps1(rt(word)));
            // Native FP helpers update selected FPSCR fields, not the whole
            // register represented by this one coarse effect bit.
            effect.possible_writes.fpscr = true;
            if word & 1 != 0 {
                // ppc_commit_scalar_result records FPSCR status in CR1, not
                // integer CR0 and not via XER.SO.
                effect.possible_writes.cr |= 1 << 1;
            }
            may_exception = true; // FP-unavailable/program exceptions and helper callback.
        }
        Opcode::Cmpi => {
            if word & 0x0020_0000 != 0 {
                return Err(unsupported(pc, word, opcode));
            }
            effect.reads = gpr(ra(word));
            effect.reads.xer = true; // SO flows into selected CR field.
            written(
                &mut effect,
                GuestRegisterSet {
                    cr: 1u8 << ((word >> 23) & 7),
                    ..Default::default()
                },
            );
        }
        Opcode::Cmp | Opcode::Cmpl => {
            if word & 0x0020_0000 != 0 {
                return Err(unsupported(pc, word, opcode));
            }
            effect.reads = gpr(ra(word)).union(gpr(rb(word)));
            effect.reads.xer = true; // Compare copies XER.SO into CR[crfD].
            written(&mut effect, cr_field(word));
        }
        Opcode::Add => {
            if word & 0x400 != 0 {
                return Err(unsupported(pc, word, opcode));
            }
            effect.reads = gpr(ra(word)).union(gpr(rb(word)));
            written(&mut effect, gpr(rt(word)));
            cr0_record(&mut effect, word);
        }
        Opcode::Cror => {
            // BT, BA, BB are CR bit indices; the effect representation is
            // field-granular, so it conservatively keeps each entire field.
            effect.reads = cr_bit_field(rt(word)).union(cr_bit_field(ra(word)));
            effect.reads = effect.reads.union(cr_bit_field(rb(word)));
            // Only BT changes. The field-granular bit is a *possible* write;
            // claiming a whole-field definite write would lose BA/BB values.
            effect.possible_writes = effect.possible_writes.union(cr_bit_field(rt(word)));
        }
        Opcode::Fcmpu | Opcode::Fcmpo => {
            effect.reads = fpr(ra(word)).union(fpr(rb(word)));
            effect.reads.fpscr = true;
            written(&mut effect, cr_field(word));
            // FPSCR.FPCC changes, while other FPSCR bits retain their values.
            effect.possible_writes.fpscr = true;
            // The native compare helper handles signaling NaNs and FPSCR
            // exception flags; an FP exception boundary must keep full state.
            may_exception = true;
        }
        Opcode::Mfspr => {
            if spr(word) != 8 {
                return Err(unsupported(pc, word, opcode));
            }
            effect.reads.lr = true;
            written(&mut effect, gpr(rt(word)));
        }
        Opcode::Mtspr => {
            let mut state = GuestRegisterSet::default();
            match spr(word) {
                8 => state.lr = true,
                9 => state.ctr = true,
                _ => return Err(unsupported(pc, word, opcode)),
            }
            effect.reads = gpr(rt(word));
            written(&mut effect, state);
        }
        Opcode::B => {
            let target = direct_target(pc, word);
            if word & 1 != 0 {
                effect = GuestInstructionEffect::unknown_boundary();
                effect.direct_target = Some(target);
                effect.possible_writes.lr = true;
                flow = GuestCfgFlow::Call;
                may_exception = true;
            } else if target >= function_start && target < function_end {
                flow = GuestCfgFlow::Branch(target);
                if target <= pc {
                    // Generated backward branches call the guest checkpoint.
                    effect = GuestInstructionEffect::unknown_boundary();
                    may_exception = true;
                }
            } else {
                // A non-link branch outside this function is an external tail
                // transfer. It can invoke native/guest code, so its whole
                // context is observable and no local successor is inferred.
                effect = GuestInstructionEffect::unknown_boundary();
                effect.direct_target = Some(target);
                flow = GuestCfgFlow::ExternalExit;
                may_exception = true;
            }
        }
        Opcode::Bc => {
            if word & 3 != 0 {
                return Err(unsupported(pc, word, opcode));
            }
            let target = conditional_target(pc, word);
            if target < function_start || target >= function_end {
                return Err(GuestPpcEffectError::ExternalBranch {
                    address: pc,
                    target,
                });
            }
            let bo = (word >> 21) & 31;
            if bo & 0x10 == 0 {
                effect.reads.cr |= 1u8 << (((word >> 16) & 31) / 4);
            }
            if bo & 4 == 0 {
                effect.reads.ctr = true;
                written(
                    &mut effect,
                    GuestRegisterSet {
                        ctr: true,
                        ..Default::default()
                    },
                );
            }
            flow = GuestCfgFlow::Conditional(vec![target]);
            if target <= pc {
                effect = GuestInstructionEffect::unknown_boundary();
                flow = GuestCfgFlow::Conditional(vec![target]);
                may_exception = true;
            }
        }
        Opcode::Bcctr => {
            if word != 0x4e80_0421 {
                return Err(unsupported(pc, word, opcode));
            }
            effect = GuestInstructionEffect::unknown_boundary();
            effect.reads.ctr = true;
            flow = GuestCfgFlow::Call;
            may_exception = true;
        }
        Opcode::Bclr => {
            if word == 0x4e80_0020 {
                effect.reads.lr = true;
                flow = GuestCfgFlow::Return;
            } else if function_start == 0x8016_c7e4
                && function_end == 0x8016_c7f8
                && pc == 0x8016_c7ec
                && word == 0x4d82_0020
            {
                // Exact RMGE01 conditional blr: BO=12 tests CR[BI=2],
                // with no CTR decrement and no link. Its taken edge returns
                // via LR; its false edge executes the external tail branch
                // at 0x8016C7F0. Other conditional bclr forms stay rejected.
                effect.reads.lr = true;
                effect.reads.cr = 1;
                flow = GuestCfgFlow::ConditionalReturn;
            } else {
                return Err(unsupported(pc, word, opcode));
            }
        }
        _ => return Err(unsupported(pc, word, opcode)),
    }
    if may_exception {
        // The checked helper or checkpoint can transfer control before an
        // ordinary destination write commits. Keep the write in the possible
        // set, but never use it to kill liveness on an exception path. Until
        // memory alias publication, callbacks, and fault retry are proved for
        // a state-free emitter, every such site also requires full context.
        effect.definite_writes = GuestRegisterSet::default();
        effect.boundary |= crate::guest_abi_effect::GuestBoundaryFlags::REQUIRES_COMPLETE_CONTEXT;
    }
    Ok(GuestCfgSite {
        address: pc,
        effect,
        flow,
        may_exception,
    })
}

/// A native substitution stays an unknown full-context call unless an audited
/// per-target contract is supplied. This must only be used for a call site that
/// already has a verified native replacement; it does not infer one by address.
pub fn apply_verified_native_call_effect(
    site: &mut GuestCfgSite,
    helper: GuestHelperEffect,
) -> bool {
    if !matches!(site.flow, GuestCfgFlow::Call) {
        return false;
    }
    let target = site.effect.direct_target;
    site.effect = GuestInstructionEffect::from_helper(helper, GuestRegisterSet::default());
    site.effect.direct_target = target;
    site.effect.possible_writes.lr = true;
    site.effect.definite_writes.lr = true;
    site.may_exception = true;
    true
}

pub fn decode_guest_ppc_function(
    function_start: u32,
    bytes: &[u8],
) -> Result<Vec<GuestCfgSite>, GuestPpcEffectError> {
    if bytes.is_empty() || bytes.len() & 3 != 0 {
        return Err(GuestPpcEffectError::InvalidLayout);
    }
    let byte_len = u32::try_from(bytes.len()).map_err(|_| GuestPpcEffectError::InvalidLayout)?;
    let function_end = function_start
        .checked_add(byte_len)
        .ok_or(GuestPpcEffectError::InvalidLayout)?;
    bytes
        .chunks_exact(4)
        .enumerate()
        .map(|(index, chunk)| {
            let pc = function_start + (index as u32) * 4;
            let word = u32::from_be_bytes(chunk.try_into().expect("exact chunk"));
            decode_guest_ppc_effect(pc, word, function_start, function_end)
        })
        .collect()
}

/// Exact source bytes and legal entry set supplied by the module owner. A
/// public interior entry keeps the ordinary full-context ABI; this analysis
/// never turns the function into a private state-free variant.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct GuestPpcRegionFunction {
    pub address: u32,
    pub bytes: Vec<u8>,
    pub public_entries: BTreeSet<u32>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum GuestPpcRegionError {
    DuplicateFunction(u32),
    InvalidExtent {
        address: u32,
    },
    OverlappingFunctions {
        first: u32,
        second: u32,
    },
    EffectDidNotConverge {
        rounds: usize,
    },
    Decode {
        address: u32,
        error: GuestPpcEffectError,
    },
    Cfg {
        address: u32,
        error: GuestCfgError,
    },
    Abi {
        address: u32,
        error: GuestEffectAnalysisError,
    },
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct GuestPpcRegionSummary {
    pub contracts: BTreeMap<u32, GuestAbiContract>,
    pub strongly_connected_components: Vec<Vec<u32>>,
    /// Every linked call remains a full-context checkpoint/fault fence, even
    /// when its exact canonical callee has a known register contract.
    pub fenced_call_sites: BTreeSet<u32>,
}

fn full_region_contract(sites: &[GuestCfgSite]) -> GuestAbiContract {
    let mut direct_targets = sites
        .iter()
        .filter_map(|site| site.effect.direct_target)
        .collect::<Vec<_>>();
    direct_targets.sort_unstable();
    direct_targets.dedup();
    GuestAbiContract {
        read_before_write: GuestRegisterSet::FULL,
        possible_writes: GuestRegisterSet::FULL,
        definite_writes: GuestRegisterSet::default(),
        boundary: GuestBoundaryFlags::REQUIRES_COMPLETE_CONTEXT,
        direct_targets,
    }
}

/// Frozen-round SCC effect propagation adapted from WiiCompiled GPLv3
/// `GuestAbiInterproceduralAnalyzer.cs::Analyze/BuildComponents`, pinned
/// 83463764b8acda394e058b0c689a10b8561fc380. Only supplied canonical
/// function starts form known graph edges. Missing targets, indirect calls,
/// native substitutions, public interiors and all checked/faulting sites
/// remain full-context fences. The returned contracts are analysis data only:
/// this function does not select a private ABI or emit any translated code.
pub fn analyze_guest_ppc_region(
    functions: &[GuestPpcRegionFunction],
    native_function_owners: &BTreeSet<u32>,
    intercepted_call_sites: &BTreeSet<u32>,
) -> Result<GuestPpcRegionSummary, GuestPpcRegionError> {
    let mut decoded = BTreeMap::<u32, (Vec<GuestCfgSite>, GuestCfg, bool)>::new();
    for function in functions {
        if decoded.contains_key(&function.address) {
            return Err(GuestPpcRegionError::DuplicateFunction(function.address));
        }
        let sites =
            decode_guest_ppc_function(function.address, &function.bytes).map_err(|error| {
                GuestPpcRegionError::Decode {
                    address: function.address,
                    error,
                }
            })?;
        // The decoder enumerates bytes starting at function.address, and
        // build_guest_cfg always inserts sites[0] as the canonical public
        // leader even when public_entries lists only additional aliases.
        let cfg = build_guest_cfg(&sites, &function.public_entries).map_err(|error| {
            GuestPpcRegionError::Cfg {
                address: function.address,
                error,
            }
        })?;
        let has_public_interior = function
            .public_entries
            .iter()
            .any(|&entry| entry != function.address);
        decoded.insert(function.address, (sites, cfg, has_public_interior));
    }
    let starts = decoded.keys().copied().collect::<Vec<_>>();
    for pair in starts.windows(2) {
        let first = pair[0];
        let second = pair[1];
        let first_sites = &decoded[&first].0;
        let first_end = first_sites
            .last()
            .expect("nonempty decoded function")
            .address
            .checked_add(4)
            .ok_or(GuestPpcRegionError::InvalidExtent { address: first })?;
        if second < first_end {
            return Err(GuestPpcRegionError::OverlappingFunctions { first, second });
        }
    }

    let mut edges = decoded
        .keys()
        .map(|&address| (address, BTreeSet::new()))
        .collect::<BTreeMap<_, _>>();
    let mut fenced_call_sites = BTreeSet::new();
    for (&address, (sites, _, _)) in &decoded {
        for site in sites {
            if !matches!(site.flow, GuestCfgFlow::Call) {
                continue;
            }
            fenced_call_sites.insert(site.address);
            let Some(target) = site.effect.direct_target else {
                continue;
            };
            let Some((_, _, callee_has_public_interior)) = decoded.get(&target) else {
                continue;
            };
            if !native_function_owners.contains(&address)
                && !native_function_owners.contains(&target)
                && !intercepted_call_sites.contains(&site.address)
                && !callee_has_public_interior
            {
                edges
                    .get_mut(&address)
                    .expect("known caller")
                    .insert(target);
            }
        }
    }
    let strongly_connected_components = guest_direct_call_components(&edges);
    // Begin at the safest upper bound. A pure callee can narrow its own
    // contract; callers see only the preceding *frozen* round, so recursion
    // never invents a selectively safe base case.
    let mut contracts = decoded
        .iter()
        .map(|(&address, (sites, _, _))| (address, full_region_contract(sites)))
        .collect::<BTreeMap<_, _>>();
    // A malformed or nonmonotone future effect catalog must not leave module
    // generation spinning forever. Hitting this generous bound rejects the
    // entire supplied region instead of exporting an unfinished contract.
    let max_rounds = decoded.len().saturating_mul(512).max(1);
    let mut converged = false;
    for _round in 0..max_rounds {
        let previous = contracts.clone();
        let mut changed = false;
        for (&address, (sites, cfg, has_public_interior)) in &decoded {
            let contract = if *has_public_interior || native_function_owners.contains(&address) {
                full_region_contract(sites)
            } else {
                let blocks = cfg
                    .blocks
                    .iter()
                    .map(|block| GuestEffectBlock {
                        successors: block.successors.clone(),
                        instructions: block
                            .sites
                            .iter()
                            .map(|site| {
                                let mut effect = site.effect;
                                if matches!(site.flow, GuestCfgFlow::Call)
                                    && !intercepted_call_sites.contains(&site.address)
                                {
                                    if let Some(target) = site
                                        .effect
                                        .direct_target
                                        .filter(|target| edges[&address].contains(target))
                                    {
                                        let callee = &previous[&target];
                                        effect.reads = callee.read_before_write;
                                        effect.possible_writes = callee.possible_writes;
                                        effect.possible_writes.lr = true;
                                        // The translated return checkpoint may
                                        // observe or mutate all guest state; a
                                        // call may also fault before returning.
                                        effect.definite_writes = GuestRegisterSet::default();
                                        effect.boundary |= callee.boundary
                                            | GuestBoundaryFlags::REQUIRES_COMPLETE_CONTEXT;
                                    }
                                }
                                if effect.boundary != GuestBoundaryFlags::NONE {
                                    // A Galaxy checkpoint, checked helper or
                                    // native callback can observe and mutate
                                    // full context. Callee-only masks may
                                    // describe its translated body but cannot
                                    // bound this host-visible boundary.
                                    effect.reads = GuestRegisterSet::FULL;
                                    effect.possible_writes = GuestRegisterSet::FULL;
                                    effect.definite_writes = GuestRegisterSet::default();
                                }
                                effect
                            })
                            .collect(),
                    })
                    .collect::<Vec<_>>();
                analyze_guest_abi(&blocks, 0)
                    .map_err(|error| GuestPpcRegionError::Abi { address, error })?
            };
            changed |= previous[&address] != contract;
            contracts.insert(address, contract);
        }
        if !changed {
            converged = true;
            break;
        }
    }
    if !converged {
        return Err(GuestPpcRegionError::EffectDidNotConverge { rounds: max_rounds });
    }
    Ok(GuestPpcRegionSummary {
        contracts,
        strongly_connected_components,
        fenced_call_sites,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::game::load_verified_game;
    use crate::guest_cfg_liveness::build_guest_cfg;
    use std::collections::BTreeSet;
    use std::path::PathBuf;

    fn region_function(address: u32, words: &[u32], entries: &[u32]) -> GuestPpcRegionFunction {
        GuestPpcRegionFunction {
            address,
            bytes: words.iter().flat_map(|word| word.to_be_bytes()).collect(),
            public_entries: entries.iter().copied().collect(),
        }
    }

    #[test]
    fn direct_call_region_keeps_checkpoint_fence_even_with_known_callee() {
        let caller = region_function(0x8000_4000, &[0x4800_0011, 0x4e80_0020], &[]);
        let callee = region_function(0x8000_4010, &[0x3863_0001, 0x4e80_0020], &[]);
        let summary =
            analyze_guest_ppc_region(&[caller, callee], &BTreeSet::new(), &BTreeSet::new())
                .expect("exact canonical direct call");
        assert_eq!(
            summary.strongly_connected_components,
            vec![vec![0x8000_4000], vec![0x8000_4010]]
        );
        assert_eq!(summary.fenced_call_sites, BTreeSet::from([0x8000_4000]));
        let caller_contract = &summary.contracts[&0x8000_4000];
        assert!(caller_contract
            .boundary
            .contains(GuestBoundaryFlags::REQUIRES_COMPLETE_CONTEXT));
        assert_eq!(caller_contract.possible_writes, GuestRegisterSet::FULL);
        assert_eq!(summary.contracts[&0x8000_4010].possible_writes.gpr, 1 << 3);
    }

    #[test]
    fn recursive_region_uses_frozen_full_context_contracts() {
        let first = region_function(0x8000_4000, &[0x4800_0011, 0x4e80_0020], &[]);
        let second = region_function(0x8000_4010, &[0x4bff_fff1, 0x4e80_0020], &[]);
        let summary =
            analyze_guest_ppc_region(&[first, second], &BTreeSet::new(), &BTreeSet::new())
                .expect("exact recursive closure");
        assert_eq!(
            summary.strongly_connected_components,
            vec![vec![0x8000_4000, 0x8000_4010]]
        );
        assert_eq!(
            summary.fenced_call_sites,
            BTreeSet::from([0x8000_4000, 0x8000_4010])
        );
        for contract in summary.contracts.values() {
            assert!(contract
                .boundary
                .contains(GuestBoundaryFlags::REQUIRES_COMPLETE_CONTEXT));
        }
    }

    #[test]
    fn missing_callee_and_native_intercept_remain_full_fences() {
        let caller = region_function(0x8000_4000, &[0x4800_0011, 0x4e80_0020], &[]);
        let unknown = analyze_guest_ppc_region(
            std::slice::from_ref(&caller),
            &BTreeSet::new(),
            &BTreeSet::new(),
        )
        .expect("unresolved calls are legal full fences");
        assert_eq!(
            unknown.contracts[&0x8000_4000].read_before_write,
            GuestRegisterSet::FULL
        );
        assert_eq!(
            unknown.contracts[&0x8000_4000].possible_writes,
            GuestRegisterSet::FULL
        );

        let callee = region_function(0x8000_4010, &[0x3863_0001, 0x4e80_0020], &[]);
        let intercepted = analyze_guest_ppc_region(
            &[caller, callee],
            &BTreeSet::new(),
            &BTreeSet::from([0x8000_4000]),
        )
        .expect("intercepted call is a full fence");
        assert_eq!(
            intercepted.contracts[&0x8000_4000].possible_writes,
            GuestRegisterSet::FULL
        );
        assert_eq!(intercepted.strongly_connected_components.len(), 2);
    }

    #[test]
    fn checked_memory_callee_cannot_export_selective_abi_effects() {
        let caller = region_function(0x8000_4000, &[0x4800_0011, 0x4e80_0020], &[]);
        let callee = region_function(0x8000_4010, &[0x8064_0000, 0x4e80_0020], &[]);
        let summary =
            analyze_guest_ppc_region(&[caller, callee], &BTreeSet::new(), &BTreeSet::new())
                .expect("checked load is a full-context fence");
        for contract in summary.contracts.values() {
            assert_eq!(contract.possible_writes, GuestRegisterSet::FULL);
            assert!(contract
                .boundary
                .contains(GuestBoundaryFlags::REQUIRES_COMPLETE_CONTEXT));
        }
    }

    #[test]
    fn public_interior_alias_rejects_selective_callee_and_invalid_alias_fails() {
        let caller = region_function(0x8000_4000, &[0x4800_0011, 0x4e80_0020], &[]);
        let aliased = region_function(0x8000_4010, &[0x3863_0001, 0x4e80_0020], &[0x8000_4014]);
        let summary = analyze_guest_ppc_region(
            &[caller.clone(), aliased],
            &BTreeSet::new(),
            &BTreeSet::new(),
        )
        .expect("legal interior entry retains public ABI");
        assert_eq!(
            summary.contracts[&0x8000_4010].possible_writes,
            GuestRegisterSet::FULL
        );
        assert_eq!(
            summary.contracts[&0x8000_4000].possible_writes,
            GuestRegisterSet::FULL
        );
        let invalid = region_function(0x8000_4010, &[0x3863_0001, 0x4e80_0020], &[0x8000_4018]);
        assert_eq!(
            analyze_guest_ppc_region(&[caller, invalid], &BTreeSet::new(), &BTreeSet::new()),
            Err(GuestPpcRegionError::Cfg {
                address: 0x8000_4010,
                error: GuestCfgError::InvalidPublicEntry {
                    address: 0x8000_4018,
                },
            })
        );
    }

    #[test]
    fn overlapping_function_owners_fail_closed() {
        let first = region_function(0x8000_4000, &[0x3863_0001, 0x4e80_0020], &[]);
        let second = region_function(0x8000_4004, &[0x4e80_0020], &[]);
        assert_eq!(
            analyze_guest_ppc_region(&[first, second], &BTreeSet::new(), &BTreeSet::new()),
            Err(GuestPpcRegionError::OverlappingFunctions {
                first: 0x8000_4000,
                second: 0x8000_4004,
            })
        );
    }

    #[test]
    fn verified_rmge01_jpa_draw_stream_is_fully_classified() {
        let path = std::env::var_os("GALAXY_TEST_MAIN_DOL")
            .map(PathBuf::from)
            .unwrap_or_else(|| {
                PathBuf::from(env!("CARGO_MANIFEST_DIR"))
                    .join("../../../MarioGalaxy1/DATA/sys/main.dol")
            });
        if !path.is_file() {
            return;
        } // CI without user-owned game assets.
        let game = load_verified_game(&path).expect("verified RMGE01 DOL");
        let start = 0x803a_3b6c;
        let len = 0x1ac;
        let section = game
            .dol
            .sections
            .iter()
            .find(|s| s.address <= start && start + len <= s.address + s.size)
            .unwrap();
        let file_start = (section.file_offset + start - section.address) as usize;
        let function_bytes = &game.dol_bytes[file_start..file_start + len as usize];
        let sites = decode_guest_ppc_function(start, function_bytes).unwrap();
        assert_eq!(sites.len(), 107);
        let mut shared_integer_count = 0;
        for (chunk, site) in function_bytes.chunks_exact(4).zip(&sites) {
            let word = u32::from_be_bytes(chunk.try_into().unwrap());
            let opcode = Ins::new(word, Extensions::gekko_broadway()).op;
            let Some(access) = guest_integer_gpr_access(opcode, word) else {
                continue;
            };
            shared_integer_count += 1;
            let old_destination = if matches!(opcode, Opcode::Addi | Opcode::Addis) {
                rt(word)
            } else {
                ra(word)
            };
            let old_first = if matches!(opcode, Opcode::Addi | Opcode::Addis) && ra(word) == 0 {
                None
            } else if matches!(opcode, Opcode::Addi | Opcode::Addis) {
                Some(ra(word))
            } else {
                Some(rt(word))
            };
            assert_eq!(
                access.destination, old_destination,
                "0x{:08X}",
                site.address
            );
            assert_eq!(access.first_source, old_first, "0x{:08X}", site.address);
            let old_second = (opcode == Opcode::Or).then(|| rb(word));
            assert_eq!(access.second_source, old_second, "0x{:08X}", site.address);
            assert_eq!(
                site.effect.reads.gpr,
                access.reads().gpr,
                "0x{:08X}",
                site.address
            );
            assert_eq!(
                site.effect.possible_writes.gpr,
                access.writes().gpr,
                "0x{:08X}",
                site.address
            );
        }
        assert_eq!(shared_integer_count, 37);
        assert_eq!(
            sites
                .iter()
                .filter(|s| matches!(s.flow, GuestCfgFlow::Call))
                .count(),
            15
        );
        assert_eq!(sites.iter().filter(|s| s.may_exception).count(), 59);
        build_guest_cfg(&sites, &BTreeSet::new())
            .expect("all real branch targets and calls classified");
        let at = |address| sites.iter().find(|site| site.address == address).unwrap();
        assert_eq!(at(0x803a_3b6c).effect.reads.gpr, 1 << 1);
        assert!(at(0x803a_3b6c).may_exception);
        assert_eq!(at(0x803a_3bc4).effect.possible_writes.cr, 1);
        assert!(at(0x803a_3bc4).effect.reads.xer);
        assert_eq!(at(0x803a_3bb8).effect.possible_writes.ps1, 1 << 1);
        assert!(at(0x803a_3c44).effect.reads.fpscr);
        assert!(at(0x803a_3c44).may_exception);
        assert!(
            sites
                .iter()
                .find(|s| s.address == 0x803a_3c30)
                .unwrap()
                .effect
                .reads
                .fpscr
        );
        assert!(
            sites
                .iter()
                .find(|s| s.address == 0x803a_3c30)
                .unwrap()
                .effect
                .possible_writes
                .ps1
                != 0
        );
        assert!(
            sites
                .iter()
                .find(|s| s.address == 0x803a_3b90)
                .unwrap()
                .effect
                .reads
                .xer
        );
        assert!(sites
            .iter()
            .find(|s| s.address == 0x803a_3bb4)
            .unwrap()
            .effect
            .boundary
            .contains(crate::guest_abi_effect::GuestBoundaryFlags::REQUIRES_COMPLETE_CONTEXT));
        assert_eq!(at(0x803a_3bb4).effect.reads.gqr, u8::MAX);
        assert_eq!(at(0x803a_3bb4).effect.reads.ps1, u32::MAX);
    }

    #[test]
    fn unknown_word_is_rejected() {
        assert!(matches!(
            decode_guest_ppc_effect(0x803a_3b6c, 0, 0x803a_3b6c, 0x803a_3b70),
            Err(GuestPpcEffectError::Unsupported { .. })
        ));
    }

    #[test]
    fn scalar_fp_record_uses_cr1_and_hid2_not_integer_cr0_or_xer() {
        // Rc variant of the verified 0x803A3C30 fmuls word 0xEC230072.
        let site =
            decode_guest_ppc_effect(0x803a_3c30, 0xec23_0073, 0x803a_3c30, 0x803a_3c34).unwrap();
        assert_eq!(site.effect.possible_writes.cr, 1 << 1);
        assert_eq!(site.effect.reads.hid & 0b010, 0b010);
        assert!(!site.effect.reads.xer);
        assert!(site.effect.reads.fpscr);
        assert!(site.may_exception);
    }

    #[test]
    fn conditional_lr_return_is_exact_owner_gated() {
        assert!(matches!(
            decode_guest_ppc_effect(0x8016_c7ec, 0x4d82_0020, 0x8016_c7e0, 0x8016_c7f8,),
            Err(GuestPpcEffectError::Unsupported { .. })
        ));
        assert!(matches!(
            decode_guest_ppc_effect(0x8016_c7ec, 0x4d82_0021, 0x8016_c7e4, 0x8016_c7f8,),
            Err(GuestPpcEffectError::Unsupported { .. })
        ));
    }

    #[test]
    fn verified_rmge01_direct_call_neighborhood_has_fail_closed_effects() {
        let path = std::env::var_os("GALAXY_TEST_MAIN_DOL")
            .map(PathBuf::from)
            .unwrap_or_else(|| {
                PathBuf::from(env!("CARGO_MANIFEST_DIR"))
                    .join("../../../MarioGalaxy1/DATA/sys/main.dol")
            });
        if !path.is_file() {
            return;
        }
        let game = load_verified_game(&path).expect("verified RMGE01 DOL");
        // Root plus its 13 canonical direct targets. These bounds are exact
        // function records in metadata/rmge01/functions.csv for this DOL.
        let functions = [
            (0x8016_5478u32, 0x11cu32),
            (0x8015_b8c4, 0x04),
            (0x8015_c708, 0x114),
            (0x8016_266c, 0x34),
            (0x8016_41c4, 0x5c),
            (0x8016_6000, 0x84),
            (0x8016_8f8c, 0x9c),
            (0x8016_c7e4, 0x14),
            (0x8016_fae8, 0x58),
            (0x803c_2604, 0x38),
            (0x803d_c5e8, 0x0c),
            (0x803d_d8d4, 0x08),
            (0x803d_e004, 0x5c),
            (0x803f_8f40, 0x98),
        ];
        let mut admitted = 0;
        for (start, len) in functions {
            let section = game
                .dol
                .sections
                .iter()
                .find(|s| s.address <= start && start + len <= s.address + s.size)
                .unwrap();
            let file_start = (section.file_offset + start - section.address) as usize;
            let bytes = &game.dol_bytes[file_start..file_start + len as usize];
            let sites = decode_guest_ppc_function(start, bytes).expect("audited vocabulary");
            assert_eq!(sites.len(), (len / 4) as usize);
            // All 64 retained public interior aliases for these 14 owners,
            // read from generated/native-module-proof/module.cpp. A later
            // state-free emitter must use the current discovery pass too;
            // this frozen proof corpus is a source test, not a runtime gate.
            let aliases: &[u32] = match start {
                0x8016_5478 => &[
                    0x8016_54ac,
                    0x8016_54bc,
                    0x8016_54c4,
                    0x8016_54d4,
                    0x8016_54e4,
                    0x8016_5500,
                    0x8016_5520,
                    0x8016_5534,
                    0x8016_5544,
                    0x8016_5554,
                    0x8016_5568,
                    0x8016_5570,
                    0x8016_5578,
                    0x8016_5580,
                ],
                0x8015_c708 => &[
                    0x8015_c714,
                    0x8015_c718,
                    0x8015_c724,
                    0x8015_c730,
                    0x8015_c744,
                    0x8015_c750,
                    0x8015_c758,
                    0x8015_c75c,
                    0x8015_c774,
                    0x8015_c788,
                    0x8015_c794,
                    0x8015_c7b8,
                    0x8015_c7bc,
                    0x8015_c7c4,
                    0x8015_c7c8,
                    0x8015_c7dc,
                    0x8015_c7fc,
                    0x8015_c804,
                    0x8015_c80c,
                ],
                0x8016_266c => &[0x8016_2684, 0x8016_268c],
                0x8016_41c4 => &[0x8016_41d8, 0x8016_41e8, 0x8016_41f4, 0x8016_4210],
                0x8016_6000 => &[
                    0x8016_602c,
                    0x8016_6048,
                    0x8016_6050,
                    0x8016_6064,
                    0x8016_6070,
                ],
                0x8016_8f8c => &[
                    0x8016_8fb8,
                    0x8016_8fc0,
                    0x8016_8fd0,
                    0x8016_8fe0,
                    0x8016_8ff0,
                    0x8016_9000,
                    0x8016_9010,
                ],
                0x8016_fae8 => &[0x8016_fb00, 0x8016_fb18, 0x8016_fb2c],
                0x803c_2604 => &[0x803c_2620],
                0x803d_e004 => &[0x803d_e02c, 0x803d_e038, 0x803d_e04c],
                0x803f_8f40 => &[
                    0x803f_8f68,
                    0x803f_8f74,
                    0x803f_8f88,
                    0x803f_8fa0,
                    0x803f_8fac,
                    0x803f_8fc0,
                ],
                _ => &[],
            };
            let public_entries = aliases.iter().copied().collect::<BTreeSet<_>>();
            let cfg = build_guest_cfg(&sites, &public_entries)
                .expect("local branch targets, public continuations, and exits");
            assert_eq!(
                cfg.blocks.iter().filter(|block| block.public_entry).count(),
                aliases.len() + 1,
                "0x{start:08X}",
            );
            admitted += 1;
            if start == 0x8016_5478 {
                let byte_load = sites.iter().find(|s| s.address == 0x8016_5498).unwrap();
                assert_eq!(byte_load.effect.reads.gpr, 1 << 3);
                assert_eq!(byte_load.effect.possible_writes.gpr, 1);
                assert!(byte_load.may_exception);
                assert!(
                    sites
                        .iter()
                        .filter(|s| s.flow == GuestCfgFlow::Call)
                        .count()
                        >= 13
                );
                assert!(sites
                    .iter()
                    .filter(|s| s.flow == GuestCfgFlow::Call)
                    .all(|s| {
                        s.effect.boundary.contains(
                            crate::guest_abi_effect::GuestBoundaryFlags::REQUIRES_COMPLETE_CONTEXT,
                        )
                    }));
            }
            if start == 0x8015_c708 {
                let psq_store = sites.iter().find(|s| s.address == 0x8015_c718).unwrap();
                assert_eq!(psq_store.effect.reads.gqr, 1);
                assert_eq!(psq_store.effect.reads.ps1, 1 << 31);
                assert!(psq_store.may_exception);
                let cror = sites.iter().find(|s| s.address == 0x8015_c7cc).unwrap();
                assert_eq!(cror.effect.definite_writes.cr, 0);
                assert_ne!(cror.effect.possible_writes.cr, 0);
                let fcmp = sites.iter().find(|s| s.address == 0x8015_c7bc).unwrap();
                assert!(fcmp.effect.reads.fpscr);
                assert!(fcmp.effect.possible_writes.fpscr);
                assert!(!fcmp.effect.definite_writes.fpscr);
            }
            if start == 0x8016_c7e4 {
                let conditional_return = sites.iter().find(|s| s.address == 0x8016_c7ec).unwrap();
                assert_eq!(conditional_return.flow, GuestCfgFlow::ConditionalReturn);
                assert!(conditional_return.effect.reads.lr);
                assert_eq!(conditional_return.effect.reads.cr, 1);
                let block = &cfg.blocks[cfg.block_by_address[&0x8016_c7ec]];
                assert!(block.public_exit);
                assert_eq!(block.successors, vec![cfg.block_by_address[&0x8016_c7f0]]);
                let external_tail = sites.iter().find(|s| s.address == 0x8016_c7f0).unwrap();
                assert_eq!(external_tail.flow, GuestCfgFlow::ExternalExit);
                assert!(external_tail.effect.boundary.contains(
                    crate::guest_abi_effect::GuestBoundaryFlags::REQUIRES_COMPLETE_CONTEXT,
                ));
            }
        }
        assert_eq!(admitted, 14);
    }
}
