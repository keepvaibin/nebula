//! Conservative guest-state CFG and liveness for future structural residency.
//!
//! Adapted from WiiCompiled GPLv3 `BasicBlockBuilder.cs::Build` and
//! `GuestStateLivenessAnalyzer.cs::Analyze/TransferBlock` at pinned commit
//! 83463764b8acda394e058b0c689a10b8561fc380
//! (https://github.com/patchzyy/Wiicompiled). Galaxy uses exact PPC addresses
//! and explicit entry/exception metadata instead of WiiCompiled's IR labels.
//! This module is inert until the native emitter supplies verified effects.

use crate::guest_abi_effect::{
    GuestAbiContract, GuestBoundaryFlags, GuestInstructionEffect, GuestRegisterSet,
};
use std::collections::{BTreeMap, BTreeSet};

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum GuestCfgFlow {
    Continue,
    /// A returning call ends the current block; the continuation is next.
    Call,
    Branch(u32),
    /// All explicit targets plus the next instruction are possible.
    Conditional(Vec<u32>),
    Return,
    /// A conditional LR return is a public exit when taken, and falls through
    /// to the next instruction when not taken. Neither edge may be erased.
    ConditionalReturn,
    /// Indirect or tail transfer with no statically proven local successor.
    ExternalExit,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct GuestCfgSite {
    pub address: u32,
    pub effect: GuestInstructionEffect,
    pub flow: GuestCfgFlow,
    /// A fault can expose the architectural state before this instruction's
    /// ordinary writes commit. Use true for any memory or FP instruction whose
    /// exact exception behavior has not been separately proven.
    pub may_exception: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct GuestCfgBlock {
    pub start: u32,
    pub sites: Vec<GuestCfgSite>,
    pub successors: Vec<usize>,
    pub public_entry: bool,
    pub public_exit: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct GuestCfg {
    pub blocks: Vec<GuestCfgBlock>,
    pub block_by_address: BTreeMap<u32, usize>,
}

/// Iterative Kosaraju over exact, already-validated canonical guest-call
/// edges. Adapted from WiiCompiled GPLv3
/// `GuestAbiInterproceduralAnalyzer.cs::BuildComponents` at commit
/// 83463764b8acda394e058b0c689a10b8561fc380. The caller must omit
/// unresolved, native-substituted and interior-entry targets; those remain
/// full-context fences in the effect summary, not graph edges.
#[must_use]
pub fn guest_direct_call_components(edges: &BTreeMap<u32, BTreeSet<u32>>) -> Vec<Vec<u32>> {
    let adjacency = edges
        .iter()
        .map(|(&address, targets)| {
            (
                address,
                targets
                    .iter()
                    .filter(|target| edges.contains_key(*target))
                    .copied()
                    .collect::<Vec<_>>(),
            )
        })
        .collect::<BTreeMap<_, _>>();
    let mut reverse = edges
        .keys()
        .map(|&address| (address, Vec::<u32>::new()))
        .collect::<BTreeMap<_, _>>();
    for (&caller, targets) in &adjacency {
        for &target in targets {
            reverse.get_mut(&target).expect("known callee").push(caller);
        }
    }
    let mut visited = BTreeSet::new();
    let mut finish_order = Vec::with_capacity(edges.len());
    for &root in edges.keys() {
        if !visited.insert(root) {
            continue;
        }
        let mut stack = vec![(root, 0usize)];
        while let Some((address, next_target)) = stack.pop() {
            let targets = &adjacency[&address];
            if let Some(&target) = targets.get(next_target) {
                stack.push((address, next_target + 1));
                if visited.insert(target) {
                    stack.push((target, 0));
                }
            } else {
                finish_order.push(address);
            }
        }
    }
    visited.clear();
    let mut components = Vec::new();
    for &root in finish_order.iter().rev() {
        if !visited.insert(root) {
            continue;
        }
        let mut component = Vec::new();
        let mut stack = vec![root];
        while let Some(address) = stack.pop() {
            component.push(address);
            for &caller in &reverse[&address] {
                if visited.insert(caller) {
                    stack.push(caller);
                }
            }
        }
        component.sort_unstable();
        components.push(component);
    }
    components.sort_by_key(|component| component[0]);
    components
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum GuestCfgError {
    Empty,
    NonContiguousOrUnaligned { address: u32 },
    InvalidPublicEntry { address: u32 },
    InvalidTarget { source: u32, target: u32 },
    UnclassifiedCall { address: u32 },
    FallthroughPastEnd { address: u32 },
}

/// WiiCompiled's leader construction adapted to Galaxy's legal interior
/// entries. An unresolved in-function target is an error, never an implicit
/// external edge. Call and possible-exception continuations start blocks so
/// later emission has exact flush/reload and retry sites.
pub fn build_guest_cfg(
    sites: &[GuestCfgSite],
    public_entries: &BTreeSet<u32>,
) -> Result<GuestCfg, GuestCfgError> {
    let Some(first) = sites.first() else {
        return Err(GuestCfgError::Empty);
    };
    for (index, site) in sites.iter().enumerate() {
        if site.address & 3 != 0
            || (index > 0 && sites[index - 1].address.checked_add(4) != Some(site.address))
        {
            return Err(GuestCfgError::NonContiguousOrUnaligned {
                address: site.address,
            });
        }
    }
    let address_to_site = sites
        .iter()
        .enumerate()
        .map(|(index, site)| (site.address, index))
        .collect::<BTreeMap<_, _>>();
    for &entry in public_entries {
        if !address_to_site.contains_key(&entry) {
            return Err(GuestCfgError::InvalidPublicEntry { address: entry });
        }
    }
    let mut leaders = public_entries.clone();
    leaders.insert(first.address);
    for (index, site) in sites.iter().enumerate() {
        if matches!(&site.flow, GuestCfgFlow::Call)
            && site.effect.direct_target.is_none()
            && !site
                .effect
                .boundary
                .contains(GuestBoundaryFlags::REQUIRES_COMPLETE_CONTEXT)
        {
            return Err(GuestCfgError::UnclassifiedCall {
                address: site.address,
            });
        }
        let targets: &[u32] = match &site.flow {
            GuestCfgFlow::Branch(target) => std::slice::from_ref(target),
            GuestCfgFlow::Conditional(targets) => targets,
            _ => &[],
        };
        for &target in targets {
            if !address_to_site.contains_key(&target) {
                return Err(GuestCfgError::InvalidTarget {
                    source: site.address,
                    target,
                });
            }
            leaders.insert(target);
        }
        if (site.may_exception || !matches!(&site.flow, GuestCfgFlow::Continue))
            && index + 1 < sites.len()
        {
            leaders.insert(sites[index + 1].address);
        }
    }
    let leaders = leaders.into_iter().collect::<Vec<_>>();
    let mut blocks = Vec::with_capacity(leaders.len());
    let mut block_by_address = BTreeMap::new();
    for (index, &start) in leaders.iter().enumerate() {
        let first_site = address_to_site[&start];
        let end_site = leaders
            .get(index + 1)
            .map_or(sites.len(), |next| address_to_site[next]);
        let block_sites = sites[first_site..end_site].to_vec();
        for site in &block_sites {
            block_by_address.insert(site.address, index);
        }
        blocks.push(GuestCfgBlock {
            start,
            sites: block_sites,
            successors: Vec::new(),
            public_entry: public_entries.contains(&start) || index == 0,
            public_exit: false,
        });
    }
    for index in 0..blocks.len() {
        let last = blocks[index].sites.last().expect("leader contains a site");
        let last_address = last.address;
        let flow = last.flow.clone();
        let mut targets = Vec::new();
        let needs_next = matches!(
            &flow,
            GuestCfgFlow::Continue
                | GuestCfgFlow::Call
                | GuestCfgFlow::Conditional(_)
                | GuestCfgFlow::ConditionalReturn
        );
        if needs_next {
            let Some(next) = blocks.get(index + 1) else {
                return Err(GuestCfgError::FallthroughPastEnd {
                    address: last_address,
                });
            };
            targets.push(next.start);
        }
        match &flow {
            GuestCfgFlow::Branch(target) => targets.push(*target),
            GuestCfgFlow::Conditional(branches) => targets.extend(branches.iter().copied()),
            GuestCfgFlow::Return | GuestCfgFlow::ConditionalReturn | GuestCfgFlow::ExternalExit => {
                blocks[index].public_exit = true
            }
            GuestCfgFlow::Continue | GuestCfgFlow::Call => {}
        }
        let mut successors = targets
            .into_iter()
            .map(|address| block_by_address[&address])
            .collect::<Vec<_>>();
        successors.sort_unstable();
        successors.dedup();
        blocks[index].successors = successors;
    }
    Ok(GuestCfg {
        blocks,
        block_by_address,
    })
}

/// A public translated-function exit must materialize every possibly written
/// architectural value. This is WiiCompiled's `MaterializedContextExit`; a
/// full-context effect in the contract naturally returns the full mask.
#[must_use]
pub fn materialized_context_exit(contract: &GuestAbiContract) -> GuestRegisterSet {
    contract.possible_writes
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct GuestStateLiveness {
    pub block_live_in: Vec<GuestRegisterSet>,
    pub block_live_out: Vec<GuestRegisterSet>,
    pub site_live_before: BTreeMap<u32, GuestRegisterSet>,
    pub public_entry_live_in: BTreeMap<u32, GuestRegisterSet>,
}

/// Backward `liveIn = reads | (liveOut - definiteWrites)` fixed point from
/// WiiCompiled, with an exact full-context fence at a possible exception or
/// unknown/suspending guest boundary. Possible writes never kill liveness.
/// A public exit includes all observable, possibly written state supplied by
/// the contract; callers may pass `GuestRegisterSet::FULL` for an unproven one.
#[must_use]
pub fn analyze_guest_state_liveness(
    cfg: &GuestCfg,
    public_exit_live: GuestRegisterSet,
) -> GuestStateLiveness {
    let count = cfg.blocks.len();
    let mut live_in = vec![GuestRegisterSet::default(); count];
    let mut live_out = live_in.clone();
    loop {
        let mut changed = false;
        for index in (0..count).rev() {
            let block = &cfg.blocks[index];
            let mut outgoing = if block.public_exit {
                public_exit_live
            } else {
                GuestRegisterSet::default()
            };
            for &successor in &block.successors {
                outgoing = outgoing.union(live_in[successor]);
            }
            let mut incoming = outgoing;
            for site in block.sites.iter().rev() {
                incoming = transfer_site(site, incoming);
            }
            if outgoing != live_out[index] || incoming != live_in[index] {
                live_out[index] = outgoing;
                live_in[index] = incoming;
                changed = true;
            }
        }
        if !changed {
            break;
        }
    }
    let mut site_live_before = BTreeMap::new();
    let mut public_entry_live_in = BTreeMap::new();
    for (index, block) in cfg.blocks.iter().enumerate() {
        let mut live = live_out[index];
        for site in block.sites.iter().rev() {
            live = transfer_site(site, live);
            site_live_before.insert(site.address, live);
        }
        if block.public_entry {
            public_entry_live_in.insert(block.start, live_in[index]);
        }
    }
    GuestStateLiveness {
        block_live_in: live_in,
        block_live_out: live_out,
        site_live_before,
        public_entry_live_in,
    }
}

fn transfer_site(site: &GuestCfgSite, live_after: GuestRegisterSet) -> GuestRegisterSet {
    let mut live_before = live_after
        .except(site.effect.definite_writes)
        .union(site.effect.reads);
    if matches!(
        &site.flow,
        GuestCfgFlow::Return | GuestCfgFlow::ConditionalReturn
    ) {
        // A PPC blr consumes LR even when the guest decoder's supplied
        // arithmetic effect omitted that control-flow operand.
        live_before.lr = true;
    }
    let boundary = site.effect.boundary;
    if matches!(&site.flow, GuestCfgFlow::ExternalExit)
        || site.may_exception
        || boundary.contains(GuestBoundaryFlags::REQUIRES_COMPLETE_CONTEXT)
        || boundary.contains(GuestBoundaryFlags::CAN_SUSPEND)
        || boundary.contains(GuestBoundaryFlags::CAN_SWITCH_THREADS)
        || boundary.contains(GuestBoundaryFlags::INVOKES_GUEST_CODE)
    {
        // A fault/callback can observe pre-instruction state even when the
        // ordinary successful path definitely writes that same register.
        live_before = live_before.union(GuestRegisterSet::FULL);
    }
    live_before
}

#[cfg(test)]
mod tests {
    use super::*;

    fn gpr(index: u32) -> GuestRegisterSet {
        GuestRegisterSet {
            gpr: 1u32 << index,
            ..Default::default()
        }
    }

    fn site(address: u32, flow: GuestCfgFlow) -> GuestCfgSite {
        GuestCfgSite {
            address,
            effect: GuestInstructionEffect::default(),
            flow,
            may_exception: false,
        }
    }

    #[test]
    fn branch_leaders_and_public_interior_entry_remain_distinct() {
        let mut sites = vec![
            site(0x8000_4000, GuestCfgFlow::Conditional(vec![0x8000_400C])),
            site(0x8000_4004, GuestCfgFlow::Continue),
            site(0x8000_4008, GuestCfgFlow::Branch(0x8000_4010)),
            site(0x8000_400C, GuestCfgFlow::Continue),
            site(0x8000_4010, GuestCfgFlow::Return),
        ];
        sites[1].effect.reads = gpr(5);
        sites[3].effect.reads = gpr(6);
        let cfg = build_guest_cfg(&sites, &BTreeSet::from([0x8000_400C])).unwrap();
        assert_eq!(
            cfg.blocks
                .iter()
                .map(|block| block.start)
                .collect::<Vec<_>>(),
            [0x8000_4000, 0x8000_4004, 0x8000_400C, 0x8000_4010]
        );
        assert!(cfg.blocks[cfg.block_by_address[&0x8000_400C]].public_entry);
        let live = analyze_guest_state_liveness(&cfg, GuestRegisterSet::default());
        assert_eq!(live.public_entry_live_in[&0x8000_400C].gpr, gpr(6).gpr);
        assert_eq!(
            live.public_entry_live_in[&0x8000_4000].gpr,
            gpr(5).gpr | gpr(6).gpr
        );
    }

    #[test]
    fn conditional_lr_return_keeps_public_exit_and_fallthrough() {
        let mut sites = vec![
            site(0x8016_c7ec, GuestCfgFlow::ConditionalReturn),
            site(0x8016_c7f0, GuestCfgFlow::ExternalExit),
        ];
        sites[0].effect.reads.cr = 1;
        sites[0].effect.reads.lr = true;
        sites[1].effect = GuestInstructionEffect::unknown_boundary();
        let cfg = build_guest_cfg(&sites, &BTreeSet::new()).unwrap();
        let first = &cfg.blocks[cfg.block_by_address[&0x8016_c7ec]];
        assert!(first.public_exit);
        assert_eq!(first.successors, vec![cfg.block_by_address[&0x8016_c7f0]]);
        let live = analyze_guest_state_liveness(&cfg, GuestRegisterSet::FULL);
        assert!(live.site_live_before[&0x8016_c7ec].lr);
        assert_eq!(live.site_live_before[&0x8016_c7ec].cr, u8::MAX);
    }

    #[test]
    fn exception_exit_preserves_prewrite_state_and_public_writeback() {
        let mut sites = vec![
            site(0x8000_5000, GuestCfgFlow::Continue),
            site(0x8000_5004, GuestCfgFlow::Return),
        ];
        sites[0].effect.definite_writes = gpr(3);
        sites[0].effect.possible_writes = gpr(3);
        sites[0].may_exception = true;
        let cfg = build_guest_cfg(&sites, &BTreeSet::new()).unwrap();
        let live = analyze_guest_state_liveness(&cfg, gpr(3));
        assert_eq!(live.site_live_before[&0x8000_5000], GuestRegisterSet::FULL);
        assert_eq!(
            live.block_live_out[cfg.block_by_address[&0x8000_5004]].gpr,
            gpr(3).gpr
        );
    }

    #[test]
    fn malformed_target_and_fallthrough_fail_closed() {
        assert_eq!(
            build_guest_cfg(&[], &BTreeSet::new()),
            Err(GuestCfgError::Empty)
        );
        assert_eq!(
            build_guest_cfg(
                &[site(0x8000_6000, GuestCfgFlow::Branch(0x8000_7000))],
                &BTreeSet::new()
            ),
            Err(GuestCfgError::InvalidTarget {
                source: 0x8000_6000,
                target: 0x8000_7000
            }),
        );
        assert_eq!(
            build_guest_cfg(
                &[site(0x8000_6000, GuestCfgFlow::Continue)],
                &BTreeSet::new()
            ),
            Err(GuestCfgError::FallthroughPastEnd {
                address: 0x8000_6000
            }),
        );
        assert_eq!(
            build_guest_cfg(
                &[site(0x8000_6000, GuestCfgFlow::Return)],
                &BTreeSet::from([0x8000_6004])
            ),
            Err(GuestCfgError::InvalidPublicEntry {
                address: 0x8000_6004
            }),
        );
    }

    #[test]
    fn unknown_guest_callback_forces_complete_live_state() {
        let mut sites = vec![
            site(0x8000_7000, GuestCfgFlow::Call),
            site(0x8000_7004, GuestCfgFlow::Return),
        ];
        sites[0].effect = GuestInstructionEffect::unknown_boundary();
        let cfg = build_guest_cfg(&sites, &BTreeSet::new()).unwrap();
        let live = analyze_guest_state_liveness(&cfg, GuestRegisterSet::default());
        assert_eq!(live.site_live_before[&0x8000_7000], GuestRegisterSet::FULL);
    }

    #[test]
    fn indirect_exit_fences_and_unclassified_call_is_rejected() {
        let exit = build_guest_cfg(
            &[site(0x8000_8000, GuestCfgFlow::ExternalExit)],
            &BTreeSet::new(),
        )
        .unwrap();
        let live = analyze_guest_state_liveness(&exit, GuestRegisterSet::default());
        assert_eq!(live.site_live_before[&0x8000_8000], GuestRegisterSet::FULL);
        assert_eq!(
            build_guest_cfg(
                &[
                    site(0x8000_8000, GuestCfgFlow::Call),
                    site(0x8000_8004, GuestCfgFlow::Return)
                ],
                &BTreeSet::new(),
            ),
            Err(GuestCfgError::UnclassifiedCall {
                address: 0x8000_8000
            }),
        );
    }

    #[test]
    fn direct_call_components_keep_recursive_cycle_and_known_leaf_separate() {
        let edges = BTreeMap::from([
            (0x8000_4000, BTreeSet::from([0x8000_4010])),
            (0x8000_4010, BTreeSet::from([0x8000_4000, 0x8000_4020])),
            (0x8000_4020, BTreeSet::new()),
        ]);
        assert_eq!(
            guest_direct_call_components(&edges),
            vec![vec![0x8000_4000, 0x8000_4010], vec![0x8000_4020]]
        );
    }
}
