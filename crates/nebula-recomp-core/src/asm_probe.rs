//! Synthetic PowerPC probes for code-generation measurement.
//!
//! Each probe is a small, self-contained guest function written for one
//! instruction class of the hot RMGE01 mix (integer ALU, loads, stack stores,
//! single and paired float, compare-and-branch, carry chains, direct and
//! virtual calls, loops, register save helpers). `translate_probe_module`
//! lowers the corpus through the same pipeline as the game module (lowering,
//! residency, transfer returns, the module-wide option passes and the sharded
//! module emitter), so the C++ it returns is what a module built with the same
//! options would contain for those instructions. `tools/perf/asm_probe.py`
//! compiles it and counts x86 instructions per guest instruction per class.
//!
//! The probes live at 0x80700000 and above, outside RMGE01's text, so no
//! address-keyed substitution or site hook of the game applies to them. No
//! game data is involved.

use crate::function_map::FunctionRange;
use crate::ppc_dataflow::{AbiFunctionInput, ModuleAbi};
use crate::translate::{
    lower_probe_function, module_option_passes, probe_function_needs_fpu,
    probe_is_checkpoint_free_leaf, render_probe_module, GeneratedSource, ModuleTranslationOptions,
    TranslationError,
};
use std::collections::{BTreeMap, BTreeSet};

/// Base address of the first probe; each probe gets its own 4 KiB slot.
pub const PROBE_BASE: u32 = 0x8070_0000;
const PROBE_SLOT: u32 = 0x1000;

/// One probe function of the corpus.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ProbeSpec {
    pub name: &'static str,
    /// Instruction class the probe measures (several probes may share one).
    pub class: &'static str,
    pub address: u32,
    pub words: Vec<u32>,
    /// Guest instructions one call executes from the probe driver's register
    /// state (see `DRIVER_STATE`), counting only this function's own code.
    pub executed: u32,
    /// Other probes one call reaches (direct `bl` targets and, for the
    /// virtual call, the vtable target), with call counts, so the measurement
    /// can subtract their cost from this probe's inclusive cost.
    pub callees: Vec<(&'static str, u32)>,
}

/// The register state the probe driver (`tools/perf/asm_probe_driver.cpp`)
/// sets before every call; `ProbeSpec::executed` assumes it.
pub const DRIVER_STATE: &str = "r1=0x80400000 r3=0x80100000 r4=16 r5..r12=1..8 \
f1=1.5 f2=2.5 f3..f8=3.5 cr=0 xer=0 hid2=PSE|LSQE gqr0=0; guest RAM at 0x80100000 holds \
0x80100100 (a vtable pointer), 1.5f at +0x20..+0xFC, and the vtable word at +8 is callee_leaf";

/// The lowered probe module and its index.
#[derive(Clone, Debug)]
pub struct ProbeModule {
    pub sources: Vec<GeneratedSource>,
    pub probes: Vec<ProbeSpec>,
}

impl ProbeModule {
    /// `name,class,address,guest_instructions,executed,callees` rows, one per
    /// probe (`callees` is a space-separated `name:count` list).
    pub fn manifest_csv(&self) -> String {
        let mut out = String::from("name,class,address,guest_instructions,executed,callees\n");
        for probe in &self.probes {
            let callees = probe
                .callees
                .iter()
                .map(|(name, count)| format!("{name}:{count}"))
                .collect::<Vec<_>>()
                .join(" ");
            out.push_str(&format!(
                "{},{},0x{:08X},{},{},{}\n",
                probe.name,
                probe.class,
                probe.address,
                probe.words.len(),
                probe.executed,
                callees
            ));
        }
        out
    }
}

// ---------------------------------------------------------------------------
// A minimal PowerPC encoder (only the forms the corpus uses)
// ---------------------------------------------------------------------------

fn d_form(op: u32, rt: u32, ra: u32, d: i32) -> u32 {
    (op << 26) | (rt << 21) | (ra << 16) | (d as u32 & 0xFFFF)
}
fn x_form(rt: u32, ra: u32, rb: u32, xo: u32, rc: bool) -> u32 {
    (31 << 26) | (rt << 21) | (ra << 16) | (rb << 11) | (xo << 1) | u32::from(rc)
}
fn addi(rd: u32, ra: u32, imm: i32) -> u32 {
    d_form(14, rd, ra, imm)
}
fn li(rd: u32, imm: i32) -> u32 {
    addi(rd, 0, imm)
}
fn addis(rd: u32, ra: u32, imm: i32) -> u32 {
    d_form(15, rd, ra, imm)
}
fn add(rd: u32, ra: u32, rb: u32) -> u32 {
    x_form(rd, ra, rb, 266, false)
}
fn subf(rd: u32, ra: u32, rb: u32) -> u32 {
    x_form(rd, ra, rb, 40, false)
}
fn mullw(rd: u32, ra: u32, rb: u32) -> u32 {
    x_form(rd, ra, rb, 235, false)
}
fn addc(rd: u32, ra: u32, rb: u32) -> u32 {
    x_form(rd, ra, rb, 10, false)
}
fn adde(rd: u32, ra: u32, rb: u32) -> u32 {
    x_form(rd, ra, rb, 138, false)
}
fn subfc(rd: u32, ra: u32, rb: u32) -> u32 {
    x_form(rd, ra, rb, 8, false)
}
fn subfe(rd: u32, ra: u32, rb: u32) -> u32 {
    x_form(rd, ra, rb, 136, false)
}
fn addze(rd: u32, ra: u32) -> u32 {
    x_form(rd, ra, 0, 202, false)
}
fn neg(rd: u32, ra: u32) -> u32 {
    x_form(rd, ra, 0, 104, false)
}
fn and(ra: u32, rs: u32, rb: u32, rc: bool) -> u32 {
    x_form(rs, ra, rb, 28, rc)
}
fn or(ra: u32, rs: u32, rb: u32) -> u32 {
    x_form(rs, ra, rb, 444, false)
}
fn mr(ra: u32, rs: u32) -> u32 {
    or(ra, rs, rs)
}
fn srawi(ra: u32, rs: u32, sh: u32) -> u32 {
    x_form(rs, ra, sh, 824, false)
}
fn rlwinm(ra: u32, rs: u32, sh: u32, mb: u32, me: u32, rc: bool) -> u32 {
    (21 << 26) | (rs << 21) | (ra << 16) | (sh << 11) | (mb << 6) | (me << 1) | u32::from(rc)
}
fn cmpwi(crf: u32, ra: u32, imm: i32) -> u32 {
    (11 << 26) | (crf << 23) | (ra << 16) | (imm as u32 & 0xFFFF)
}
fn cmplw(crf: u32, ra: u32, rb: u32) -> u32 {
    (31 << 26) | (crf << 23) | (ra << 16) | (rb << 11) | (32 << 1)
}
fn lwz(rt: u32, d: i32, ra: u32) -> u32 {
    d_form(32, rt, ra, d)
}
fn lbz(rt: u32, d: i32, ra: u32) -> u32 {
    d_form(34, rt, ra, d)
}
fn lhz(rt: u32, d: i32, ra: u32) -> u32 {
    d_form(40, rt, ra, d)
}
fn lha(rt: u32, d: i32, ra: u32) -> u32 {
    d_form(42, rt, ra, d)
}
fn stw(rs: u32, d: i32, ra: u32) -> u32 {
    d_form(36, rs, ra, d)
}
fn stwu(rs: u32, d: i32, ra: u32) -> u32 {
    d_form(37, rs, ra, d)
}
fn stb(rs: u32, d: i32, ra: u32) -> u32 {
    d_form(38, rs, ra, d)
}
fn sth(rs: u32, d: i32, ra: u32) -> u32 {
    d_form(44, rs, ra, d)
}
fn lwzx(rt: u32, ra: u32, rb: u32) -> u32 {
    x_form(rt, ra, rb, 23, false)
}
fn stwx(rs: u32, ra: u32, rb: u32) -> u32 {
    x_form(rs, ra, rb, 151, false)
}
fn lmw(rt: u32, d: i32, ra: u32) -> u32 {
    d_form(46, rt, ra, d)
}
fn stmw(rs: u32, d: i32, ra: u32) -> u32 {
    d_form(47, rs, ra, d)
}
fn dcbz(ra: u32, rb: u32) -> u32 {
    x_form(0, ra, rb, 1014, false)
}
fn lfs(ft: u32, d: i32, ra: u32) -> u32 {
    d_form(48, ft, ra, d)
}
fn lfd(ft: u32, d: i32, ra: u32) -> u32 {
    d_form(50, ft, ra, d)
}
fn stfs(fs: u32, d: i32, ra: u32) -> u32 {
    d_form(52, fs, ra, d)
}
fn stfd(fs: u32, d: i32, ra: u32) -> u32 {
    d_form(54, fs, ra, d)
}
fn psq_l(ft: u32, d: i32, ra: u32) -> u32 {
    (56 << 26) | (ft << 21) | (ra << 16) | (d as u32 & 0xFFF)
}
fn psq_st(fs: u32, d: i32, ra: u32) -> u32 {
    (60 << 26) | (fs << 21) | (ra << 16) | (d as u32 & 0xFFF)
}
fn a_form(op: u32, frt: u32, fra: u32, frb: u32, frc: u32, xo: u32) -> u32 {
    (op << 26) | (frt << 21) | (fra << 16) | (frb << 11) | (frc << 6) | (xo << 1)
}
fn fadds(frt: u32, fra: u32, frb: u32) -> u32 {
    a_form(59, frt, fra, frb, 0, 21)
}
fn fmuls(frt: u32, fra: u32, frc: u32) -> u32 {
    a_form(59, frt, fra, 0, frc, 25)
}
fn fmadds(frt: u32, fra: u32, frc: u32, frb: u32) -> u32 {
    a_form(59, frt, fra, frb, frc, 29)
}
fn fadd(frt: u32, fra: u32, frb: u32) -> u32 {
    a_form(63, frt, fra, frb, 0, 21)
}
fn fmul(frt: u32, fra: u32, frc: u32) -> u32 {
    a_form(63, frt, fra, 0, frc, 25)
}
fn fmadd(frt: u32, fra: u32, frc: u32, frb: u32) -> u32 {
    a_form(63, frt, fra, frb, frc, 29)
}
fn fmr(frt: u32, frb: u32) -> u32 {
    (63 << 26) | (frt << 21) | (frb << 11) | (72 << 1)
}
fn fctiwz(frt: u32, frb: u32) -> u32 {
    (63 << 26) | (frt << 21) | (frb << 11) | (15 << 1)
}
fn fcmpu(crf: u32, fra: u32, frb: u32) -> u32 {
    (63 << 26) | (crf << 23) | (fra << 16) | (frb << 11)
}
fn fcmpo(crf: u32, fra: u32, frb: u32) -> u32 {
    (63 << 26) | (crf << 23) | (fra << 16) | (frb << 11) | (32 << 1)
}
fn fsel(frt: u32, fra: u32, frc: u32, frb: u32) -> u32 {
    a_form(63, frt, fra, frb, frc, 23)
}
fn frsqrte(frt: u32, frb: u32) -> u32 {
    a_form(63, frt, 0, frb, 0, 26)
}
fn fres(frt: u32, frb: u32) -> u32 {
    a_form(59, frt, 0, frb, 0, 24)
}
/// `psq_l` loading one element (W=1): ps1 becomes 1.0.
fn psq_l1(ft: u32, d: i32, ra: u32) -> u32 {
    psq_l(ft, d, ra) | 0x8000
}
fn ps_mul(frt: u32, fra: u32, frc: u32) -> u32 {
    a_form(4, frt, fra, 0, frc, 25)
}
fn ps_madd(frt: u32, fra: u32, frc: u32, frb: u32) -> u32 {
    a_form(4, frt, fra, frb, frc, 29)
}
fn ps_sum0(frt: u32, fra: u32, frc: u32, frb: u32) -> u32 {
    a_form(4, frt, fra, frb, frc, 10)
}
fn crxor(bt: u32, ba: u32, bb: u32) -> u32 {
    (19 << 26) | (bt << 21) | (ba << 16) | (bb << 11) | (193 << 1)
}
fn mfcr(rd: u32) -> u32 {
    (31 << 26) | (rd << 21) | (19 << 1)
}
fn mflr(rd: u32) -> u32 {
    0x7C08_02A6 | (rd << 21)
}
fn mtlr(rs: u32) -> u32 {
    0x7C08_03A6 | (rs << 21)
}
fn mtctr(rs: u32) -> u32 {
    0x7C09_03A6 | (rs << 21)
}
const BLR: u32 = 0x4E80_0020;
const BCTRL: u32 = 0x4E80_0421;
/// Conditional branch by `instructions` (relative, may be negative).
fn bc(bo: u32, bi: u32, instructions: i32) -> u32 {
    (16 << 26) | (bo << 21) | (bi << 16) | ((instructions * 4) as u32 & 0xFFFC)
}
fn beq(instructions: i32) -> u32 {
    bc(12, 2, instructions)
}
fn bne(instructions: i32) -> u32 {
    bc(4, 2, instructions)
}
fn blt(instructions: i32) -> u32 {
    bc(12, 0, instructions)
}
fn bge(instructions: i32) -> u32 {
    bc(4, 0, instructions)
}
fn bgt_cr1(instructions: i32) -> u32 {
    bc(12, 5, instructions)
}
fn bdnz(instructions: i32) -> u32 {
    bc(16, 0, instructions)
}
fn b(instructions: i32) -> u32 {
    (18 << 26) | ((instructions * 4) as u32 & 0x03FF_FFFC)
}
/// `bl target` from `pc`.
fn bl(pc: u32, target: u32) -> u32 {
    (18 << 26) | (target.wrapping_sub(pc) & 0x03FF_FFFC) | 1
}

/// CodeWarrior's `_savegpr_N` / `_restgpr_N` entries (translated inline).
fn savegpr(first: u32) -> u32 {
    0x8051_74FC + 4 * (first - 14)
}
fn restgpr(first: u32) -> u32 {
    0x8051_7548 + 4 * (first - 14)
}

/// A probe body as a function of its own address, so calls can encode
/// relative displacements.
type Body = fn(u32, &BTreeMap<&'static str, u32>) -> Vec<u32>;

const CORPUS: &[(&str, &str, Body)] = &[
    // Callees first: their addresses are fixed before the callers encode `bl`.
    ("callee_leaf", "callee", |_, _| vec![lwz(3, 0, 3), BLR]),
    ("callee_frame", "callee", |pc, at| {
        vec![
            stwu(1, -16, 1),
            mflr(0),
            stw(0, 20, 1),
            bl(pc + 12, at["callee_leaf"]),
            lwz(0, 20, 1),
            mtlr(0),
            addi(1, 1, 16),
            BLR,
        ]
    }),
    ("int_alu", "integer ALU", |_, _| {
        vec![
            addi(5, 3, 4),
            add(6, 5, 4),
            rlwinm(7, 6, 2, 0, 29, false),
            subf(8, 7, 3),
            mullw(9, 8, 4),
            or(10, 9, 5),
            addis(11, 10, 1),
            and(12, 11, 6, false),
            srawi(3, 12, 2),
            neg(4, 3),
            BLR,
        ]
    }),
    ("int_load", "integer load", |_, _| {
        vec![
            lwz(5, 0, 3),
            lwz(6, 4, 3),
            lhz(7, 8, 3),
            lbz(8, 10, 3),
            lwz(9, 0, 4),
            lha(10, 12, 4),
            lwzx(11, 3, 4),
            add(3, 5, 6),
            BLR,
        ]
    }),
    ("int_store", "integer store", |_, _| {
        vec![
            stw(5, 0, 3),
            stw(6, 4, 3),
            sth(7, 8, 3),
            stb(8, 10, 3),
            stw(9, 0, 4),
            stwx(10, 3, 4),
            BLR,
        ]
    }),
    ("stack_frame", "stack load/store", |_, _| {
        vec![
            stwu(1, -32, 1),
            stw(31, 28, 1),
            stw(30, 24, 1),
            mr(31, 3),
            mr(30, 4),
            stw(5, 8, 1),
            lwz(3, 8, 1),
            add(3, 3, 31),
            lwz(30, 24, 1),
            lwz(31, 28, 1),
            addi(1, 1, 32),
            BLR,
        ]
    }),
    ("float_single", "float single", |_, _| {
        vec![
            lfs(1, 0, 3),
            lfs(2, 4, 3),
            fmuls(3, 1, 2),
            lfs(4, 8, 3),
            fadds(3, 3, 4),
            fmadds(5, 1, 2, 3),
            stfs(3, 0, 4),
            stfs(5, 4, 4),
            BLR,
        ]
    }),
    ("float_double", "float double", |_, _| {
        vec![
            lfd(1, 0, 3),
            lfd(2, 8, 3),
            fmul(3, 1, 2),
            fadd(4, 3, 1),
            fmadd(5, 1, 2, 4),
            stfd(5, 0, 4),
            BLR,
        ]
    }),
    ("paired", "paired single", |_, _| {
        vec![
            psq_l(1, 0, 3),
            psq_l(2, 8, 3),
            ps_mul(3, 1, 2),
            ps_madd(4, 1, 2, 3),
            ps_sum0(5, 4, 4, 4),
            psq_st(4, 0, 4),
            stfs(5, 8, 4),
            BLR,
        ]
    }),
    ("float_compare", "float compare/convert", |_, _| {
        vec![
            fcmpu(0, 1, 2),
            blt(2),
            fmr(1, 2),
            fctiwz(3, 1),
            stfd(3, 0, 4),
            lwz(3, 4, 4),
            BLR,
        ]
    }),
    ("float_loop", "float loop", |_, _| {
        // Scale four floats in guest RAM by a loop-invariant factor.
        vec![
            lfs(1, 0x40, 3),
            li(5, 4),
            mtctr(5),
            addi(6, 3, 0x20),
            lfs(0, 0, 6),
            fmuls(0, 0, 1),
            stfs(0, 0, 6),
            addi(6, 6, 4),
            bdnz(-4),
            BLR,
        ]
    }),
    ("paired_mtx", "paired matrix", |_, _| {
        // PSMTXMultVec's first row: (m00 m01 m02 m03) . (x y z 1).
        vec![
            psq_l(0, 0x80, 3),
            psq_l(2, 0x40, 3),
            psq_l1(1, 0x88, 3),
            ps_mul(4, 2, 0),
            psq_l(3, 0x48, 3),
            ps_madd(5, 3, 1, 4),
            ps_sum0(6, 5, 5, 5),
            stfs(6, 0, 4),
            BLR,
        ]
    }),
    ("float_select", "float select/estimate", |_, _| {
        vec![
            fsel(3, 1, 2, 4),
            frsqrte(5, 2),
            fres(6, 2),
            fcmpo(1, 5, 6),
            bgt_cr1(2),
            fmr(5, 6),
            stfs(5, 0, 4),
            stfs(3, 4, 4),
            BLR,
        ]
    }),
    ("compare_branch", "compare and branch", |_, _| {
        vec![
            cmpwi(0, 3, 0),
            beq(3),
            addi(3, 3, 1),
            b(2),
            addi(3, 3, -1),
            cmplw(0, 3, 4),
            bge(2),
            mr(3, 4),
            BLR,
        ]
    }),
    ("record_forms", "record forms", |_, _| {
        vec![
            rlwinm(5, 3, 0, 31, 31, true),
            beq(2),
            addi(3, 3, 1),
            and(6, 3, 4, true),
            bne(2),
            li(3, 0),
            BLR,
        ]
    }),
    ("carry", "carry chain", |_, _| {
        vec![
            addc(5, 3, 4),
            adde(6, 7, 8),
            subfc(9, 3, 4),
            subfe(10, 7, 8),
            addze(11, 6),
            mr(3, 5),
            mr(4, 6),
            BLR,
        ]
    }),
    ("cr_logic", "CR logic", |_, _| {
        vec![crxor(6, 6, 6), mfcr(3), BLR]
    }),
    ("leaf_accessor", "entry/exit", |_, _| {
        vec![lwz(3, 4, 3), BLR]
    }),
    ("call_direct", "direct call", |pc, at| {
        vec![
            stwu(1, -16, 1),
            mflr(0),
            stw(0, 20, 1),
            stw(31, 12, 1),
            mr(31, 3),
            bl(pc + 20, at["callee_frame"]),
            add(31, 31, 3),
            mr(3, 31),
            bl(pc + 32, at["callee_frame"]),
            lwz(0, 20, 1),
            lwz(31, 12, 1),
            mtlr(0),
            addi(1, 1, 16),
            BLR,
        ]
    }),
    ("call_leaf", "direct call", |pc, at| {
        vec![
            stwu(1, -16, 1),
            mflr(0),
            stw(0, 20, 1),
            bl(pc + 12, at["callee_leaf"]),
            lwz(0, 20, 1),
            mtlr(0),
            addi(1, 1, 16),
            BLR,
        ]
    }),
    ("call_virtual", "virtual call", |_, _| {
        vec![
            stwu(1, -16, 1),
            mflr(0),
            stw(0, 20, 1),
            stw(31, 12, 1),
            mr(31, 3),
            lwz(12, 0, 3),
            lwz(12, 8, 12),
            mtctr(12),
            BCTRL,
            mr(3, 31),
            lwz(0, 20, 1),
            lwz(31, 12, 1),
            mtlr(0),
            addi(1, 1, 16),
            BLR,
        ]
    }),
    ("loop_sum", "counted loop", |_, _| {
        vec![
            li(5, 0),
            mtctr(4),
            lwz(6, 0, 3),
            add(5, 5, 6),
            addi(3, 3, 4),
            bdnz(-3),
            mr(3, 5),
            BLR,
        ]
    }),
    ("savegpr_frame", "register save helpers", |pc, _| {
        vec![
            stwu(1, -64, 1),
            mflr(0),
            stw(0, 68, 1),
            addi(11, 1, 64),
            bl(pc + 16, savegpr(25)),
            mr(25, 3),
            add(26, 25, 4),
            mr(3, 26),
            addi(11, 1, 64),
            bl(pc + 36, restgpr(25)),
            lwz(0, 68, 1),
            mtlr(0),
            addi(1, 1, 64),
            BLR,
        ]
    }),
    ("stmw_frame", "load/store multiple", |_, _| {
        vec![
            stwu(1, -48, 1),
            stmw(26, 24, 1),
            mr(26, 3),
            add(27, 26, 4),
            mr(3, 27),
            lmw(26, 24, 1),
            addi(1, 1, 48),
            BLR,
        ]
    }),
    ("stmw_low", "load/store multiple", |_, _| {
        vec![
            stwu(1, -64, 1),
            stmw(22, 16, 1),
            mr(22, 3),
            add(23, 22, 4),
            mr(3, 23),
            lmw(22, 16, 1),
            addi(1, 1, 64),
            BLR,
        ]
    }),
    ("dcbz_line", "cache line zero", |_, _| {
        vec![
            addi(5, 3, 0x40),
            dcbz(0, 5),
            addi(5, 5, 0x20),
            dcbz(0, 5),
            BLR,
        ]
    }),
];

/// Guest instructions one call executes from `DRIVER_STATE` (own code only).
/// Straight-line probes execute every word; the others are worked out here:
/// compare_branch: r3 != 0 skips nothing at beq, b skips one, r3 > 16 takes
/// bge (7); record_forms: r3 & 1 == 0 takes beq, r3 & 16 == 0 falls through
/// bne (6); float_compare: f1 < f2 takes blt (6); loop_sum: 16 iterations of
/// lwz/add/addi/bdnz plus four (68); float_loop: four iterations of five plus
/// five (25); float_select: frsqrte(2.5) > fres(2.5) takes bgt (8).
fn executed_instructions(name: &str, words: usize) -> u32 {
    match name {
        "compare_branch" => 7,
        "record_forms" => 6,
        "float_compare" => 6,
        "loop_sum" => 68,
        "float_loop" => 25,
        "float_select" => 8,
        _ => words as u32,
    }
}

/// The probe corpus with addresses assigned.
pub fn probe_corpus() -> Vec<ProbeSpec> {
    let mut addresses = BTreeMap::new();
    for (index, (name, _, _)) in CORPUS.iter().enumerate() {
        addresses.insert(*name, PROBE_BASE + index as u32 * PROBE_SLOT);
    }
    CORPUS
        .iter()
        .map(|(name, class, body)| {
            let address = addresses[name];
            let words = body(address, &addresses);
            let mut callees: BTreeMap<&'static str, u32> = BTreeMap::new();
            for (index, word) in words.iter().enumerate() {
                if word >> 26 == 18 && word & 3 == 1 {
                    let pc = address + index as u32 * 4;
                    let offset = ((word & 0x03FF_FFFC) << 6) as i32 >> 6;
                    let target = pc.wrapping_add(offset as u32);
                    if let Some((callee, _)) = addresses.iter().find(|(_, start)| **start == target)
                    {
                        *callees.entry(callee).or_default() += 1;
                    }
                }
            }
            if *name == "call_virtual" {
                // The driver's vtable entry is callee_leaf.
                *callees.entry("callee_leaf").or_default() += 1;
            }
            ProbeSpec {
                name,
                class,
                address,
                executed: executed_instructions(name, words.len()),
                words,
                callees: callees.into_iter().collect(),
            }
        })
        .collect()
}

/// Lowers the corpus through the module pipeline with `options` and returns
/// the generated sources (`functions.h`, one function shard, `module.cpp`,
/// `CMakeLists.txt`) plus the probe index.
pub fn translate_probe_module(
    options: &ModuleTranslationOptions,
) -> Result<ProbeModule, TranslationError> {
    crate::translate::validate_module_options(options)?;
    let probes = probe_corpus();
    let bytes: BTreeMap<u32, Vec<u8>> = probes
        .iter()
        .map(|probe| {
            (
                probe.address,
                probe
                    .words
                    .iter()
                    .flat_map(|word| word.to_be_bytes())
                    .collect(),
            )
        })
        .collect();
    let callable_entries: BTreeMap<u32, u32> = probes
        .iter()
        .map(|probe| (probe.address, probe.address))
        .collect();
    let exact_fpu_functions: BTreeSet<u32> = probes
        .iter()
        .filter(|probe| probe_function_needs_fpu(probe.address, &bytes[&probe.address]))
        .map(|probe| probe.address)
        .collect();
    let checkpoint_free_leaves: BTreeSet<u32> = probes
        .iter()
        .filter(|probe| probe_is_checkpoint_free_leaf(probe.address, &bytes[&probe.address]))
        .map(|probe| probe.address)
        .collect();
    // The corpus is a module of its own for the EABI analysis: no host
    // substitutions and no data to take addresses from.
    let module_abi = (options.abi_call_sync || options.abi_exit_sync).then(|| {
        let inputs: Vec<AbiFunctionInput<'_>> = probes
            .iter()
            .map(|probe| AbiFunctionInput {
                start: probe.address,
                words: &probe.words,
                native: false,
                jump_targets: &crate::ppc_dataflow::NO_JUMP_TARGETS,
            })
            .collect();
        ModuleAbi::analyze(
            &inputs,
            &BTreeSet::new(),
            &crate::translate::abi_inline_call,
        )
    });
    let mut functions = Vec::with_capacity(probes.len());
    for probe in &probes {
        let body = lower_probe_function(
            probe.address,
            &bytes[&probe.address],
            &callable_entries,
            &exact_fpu_functions,
            &checkpoint_free_leaves,
            options,
            module_abi.as_ref(),
        )?;
        functions.push((
            FunctionRange {
                address: probe.address,
                size: probe.words.len() as u32 * 4,
            },
            body,
        ));
    }
    let mut sources = render_probe_module(&functions, &exact_fpu_functions)?;
    module_option_passes(&mut sources, options);
    if let Some(module_abi) = &module_abi {
        sources.push(GeneratedSource {
            name: crate::translate::ABI_CONTRACT_FILE.to_owned(),
            contents: module_abi.manifest_json(),
        });
    }
    Ok(ProbeModule { sources, probes })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn corpus_addresses_are_unique_and_outside_the_game_text() {
        let corpus = probe_corpus();
        let addresses: BTreeSet<u32> = corpus.iter().map(|probe| probe.address).collect();
        assert_eq!(addresses.len(), corpus.len());
        assert!(corpus.iter().all(|probe| probe.address >= PROBE_BASE));
        assert!(corpus
            .iter()
            .all(|probe| *probe.words.last().unwrap() == BLR));
    }

    #[test]
    fn every_probe_lowers_in_the_default_and_release_shapes() {
        let release = ModuleTranslationOptions {
            inline_scalar_single_binary: true,
            flat_ram_reads: true,
            flat_ram_writes: true,
            native_float: true,
            fast_float: true,
            direct_indirect_calls: true,
            exact_psmtx_local_lanes: false,
            guest_resident_integer: true,
            ..ModuleTranslationOptions::default()
        };
        for options in [ModuleTranslationOptions::default(), release] {
            let module = translate_probe_module(&options).expect("corpus lowers");
            let shards: Vec<_> = module
                .sources
                .iter()
                .filter(|source| source.name.starts_with("functions_"))
                .collect();
            assert_eq!(shards.len(), module.probes.len(), "one probe per shard");
            for probe in &module.probes {
                assert!(
                    shards.iter().any(|shard| shard
                        .contents
                        .contains(&format!("void fn_{:08X}(", probe.address))),
                    "{}",
                    probe.name
                );
            }
            assert!(module.manifest_csv().lines().count() == module.probes.len() + 1);
        }
    }

    #[test]
    fn probe_calls_reach_the_translated_callees() {
        let module = translate_probe_module(&ModuleTranslationOptions::default()).unwrap();
        let at = |name: &str| {
            module
                .probes
                .iter()
                .find(|probe| probe.name == name)
                .unwrap()
                .address
        };
        let shard = module
            .sources
            .iter()
            .filter(|source| source.name.starts_with("functions_"))
            .map(|source| source.contents.as_str())
            .collect::<String>();
        assert!(shard.contains(&format!(
            "rmge01::fn_{:08X}(context, memory, services);",
            at("callee_frame")
        )));
        assert!(shard.contains(&format!(
            "rmge01::fn_{:08X}(context, memory, services);",
            at("callee_leaf")
        )));
    }

    fn release_with(extra: impl Fn(&mut ModuleTranslationOptions)) -> ModuleTranslationOptions {
        let mut options = ModuleTranslationOptions {
            inline_scalar_single_binary: true,
            flat_ram_reads: true,
            flat_ram_writes: true,
            native_float: true,
            fast_float: true,
            direct_indirect_calls: true,
            exact_psmtx_local_lanes: false,
            guest_resident_integer: true,
            ..ModuleTranslationOptions::default()
        };
        extra(&mut options);
        options
    }

    fn probe_body(module: &ProbeModule, address: u32) -> String {
        let signature = format!("void fn_{address:08X}(");
        let shard = module
            .sources
            .iter()
            .find(|source| {
                source.name.starts_with("functions_") && source.contents.contains(&signature)
            })
            .expect("probe shard");
        let start = shard.contents.find(&signature).unwrap();
        let end = start + shard.contents[start..].find("\n}\n").unwrap();
        shard.contents[start..end].to_owned()
    }

    #[test]
    fn abi_sync_narrows_the_direct_and_virtual_call_sequences() {
        let address = |name: &str| {
            probe_corpus()
                .iter()
                .find(|probe| probe.name == name)
                .map(|probe| probe.address)
                .unwrap()
        };
        let call_direct = address("call_direct");
        let call_virtual = address("call_virtual");
        let full = translate_probe_module(&release_with(|_| {})).unwrap();
        let body = probe_body(&full, call_direct);
        assert!(body.contains("{ context->gpr[0] = gr0; context->gpr[1] = gr1; context->gpr[3] = gr3; context->gpr[31] = gr31; context->lr = glr; rmge01::fn_80701000(context, memory, services); gr0 = context->gpr[0]; gr1 = context->gpr[1]; gr3 = context->gpr[3]; gr31 = context->gpr[31]; glr = context->lr; }"), "{body}");
        assert!(body.contains("{ context->gpr[0] = gr0; context->gpr[1] = gr1; context->gpr[3] = gr3; context->gpr[31] = gr31; context->lr = glr; return; }"), "{body}");
        let abi = translate_probe_module(&release_with(|options| {
            options.abi_call_sync = true;
            options.abi_exit_sync = true;
        }))
        .unwrap();
        let body = probe_body(&abi, call_direct);
        assert!(body.contains("{ context->gpr[1] = gr1; context->gpr[3] = gr3; context->gpr[31] = gr31; context->lr = glr; rmge01::fn_80701000(context, memory, services); gr3 = context->gpr[3]; }"), "{body}");
        assert!(body.contains("{ context->gpr[1] = gr1; context->gpr[3] = gr3; context->gpr[31] = gr31; context->lr = glr; return; }"), "{body}");
        assert!(!body.contains("GALAXY_ABI_CALL"), "{body}");
        // The virtual call: r12 and CTR are volatile non-arguments.
        let body = probe_body(&abi, call_virtual);
        assert!(body.contains("{ context->gpr[1] = gr1; context->gpr[3] = gr3; context->gpr[31] = gr31; context->lr = glr; galaxy::fast::call_guest_indirect("), "{body}");
        assert!(
            body.contains(&format!(
                "0x{:08X}u); gr3 = context->gpr[3]; }}",
                call_virtual + 0x20
            )),
            "{body}"
        );
        // The other probes are unchanged by the flags except for their calls
        // and returns: a leaf without calls keeps its body.
        let leaf = PROBE_BASE;
        assert_eq!(
            probe_body(&abi, leaf)
                .replace("GALAXY_GUEST_RETURN;", "return;")
                .replace("[[maybe_unused]] std::uint32_t gr", "std::uint32_t gr"),
            probe_body(&full, leaf)
        );
    }
}
