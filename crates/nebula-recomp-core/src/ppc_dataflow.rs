//! Register dataflow over translated PowerPC functions.
//!
//! A register-effect decoder for every instruction form the translator lowers
//! (explicit operands plus the implicit ones: LR, CTR, the XER carry and
//! summary-overflow bits, CR0/CR1 of record forms, CR fields of compares and CR
//! logic, the register runs of lmw/stmw), and a backward liveness fixed point
//! over one function with a pluggable model of what a call reads and
//! clobbers. The facts the code generator needs derive from it:
//!
//! * the registers a function reads before writing them (entry live-in);
//! * the volatile registers it reads after a call before rewriting them, which
//!   the PowerPC EABI says hold garbage (only non-ABI code does that);
//! * the non-volatile registers it writes without having read them first
//!   (it cannot have saved them, so it clobbers its caller's values);
//! * condition-register fields and the carry bit written but never read.
//!
//! Every decision is conservative: an instruction the decoder does not know,
//! an indirect jump inside the function, a trap or a system call makes every
//! register live there.

use powerpc::{Extensions, Ins, Opcode};
use std::collections::BTreeMap;

/// A set of guest registers. CR is field-granular (CR0..CR7), XER is split into
/// the carry bit (CA) and the summary/overflow bits (SO, OV).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub struct Regs {
    pub gpr: u32,
    pub fpr: u32,
    pub cr: u8,
    pub lr: bool,
    pub ctr: bool,
    pub ca: bool,
    pub so: bool,
    pub fpscr: bool,
}

impl Regs {
    pub const ALL: Self = Self {
        gpr: u32::MAX,
        fpr: u32::MAX,
        cr: u8::MAX,
        lr: true,
        ctr: true,
        ca: true,
        so: true,
        fpscr: true,
    };

    #[must_use]
    pub fn gpr(index: u32) -> Self {
        Self {
            gpr: 1 << index,
            ..Self::default()
        }
    }

    #[must_use]
    pub fn fpr(index: u32) -> Self {
        Self {
            fpr: 1 << index,
            ..Self::default()
        }
    }

    #[must_use]
    pub fn cr_field(field: u32) -> Self {
        Self {
            cr: 1 << field,
            ..Self::default()
        }
    }

    #[must_use]
    pub fn union(self, other: Self) -> Self {
        Self {
            gpr: self.gpr | other.gpr,
            fpr: self.fpr | other.fpr,
            cr: self.cr | other.cr,
            lr: self.lr || other.lr,
            ctr: self.ctr || other.ctr,
            ca: self.ca || other.ca,
            so: self.so || other.so,
            fpscr: self.fpscr || other.fpscr,
        }
    }

    #[must_use]
    pub fn minus(self, other: Self) -> Self {
        Self {
            gpr: self.gpr & !other.gpr,
            fpr: self.fpr & !other.fpr,
            cr: self.cr & !other.cr,
            lr: self.lr && !other.lr,
            ctr: self.ctr && !other.ctr,
            ca: self.ca && !other.ca,
            so: self.so && !other.so,
            fpscr: self.fpscr && !other.fpscr,
        }
    }

    #[must_use]
    pub fn intersect(self, other: Self) -> Self {
        Self {
            gpr: self.gpr & other.gpr,
            fpr: self.fpr & other.fpr,
            cr: self.cr & other.cr,
            lr: self.lr && other.lr,
            ctr: self.ctr && other.ctr,
            ca: self.ca && other.ca,
            so: self.so && other.so,
            fpscr: self.fpscr && other.fpscr,
        }
    }

    #[must_use]
    pub fn is_empty(self) -> bool {
        self == Self::default()
    }
}

/// The PowerPC EABI as CodeWarrior compiles RMGE01.
pub mod abi {
    use super::Regs;

    /// r3..r10 and f1..f8 carry arguments.
    pub const ARGUMENT_GPRS: u32 = 0x0000_07F8;
    pub const ARGUMENT_FPRS: u32 = 0x0000_01FE;
    /// r3/r4 and f1/f2 carry results.
    pub const RESULT_GPRS: u32 = 0x0000_0018;
    pub const RESULT_FPRS: u32 = 0x0000_0006;
    /// r14..r31 and f14..f31 survive calls; so do r1 (stack), r2 and r13
    /// (small-data bases, never written by game code).
    pub const NONVOLATILE_GPRS: u32 = 0xFFFF_C000 | (1 << 1) | (1 << 2) | (1 << 13);
    pub const NONVOLATILE_FPRS: u32 = 0xFFFF_C000;
    /// CR2..CR4 survive calls.
    pub const NONVOLATILE_CR: u8 = 0b0001_1100;

    /// What a call may read: the arguments, the stack and small-data bases,
    /// and CR1 (CR1[EQ] tells a variadic callee whether FP arguments exist).
    #[must_use]
    pub fn call_uses() -> Regs {
        Regs {
            gpr: ARGUMENT_GPRS | (1 << 1) | (1 << 2) | (1 << 13),
            fpr: ARGUMENT_FPRS,
            cr: 1 << 1,
            ..Regs::default()
        }
    }

    /// What a call leaves undefined: every volatile register, LR and CTR,
    /// the XER bits and FPSCR.
    #[must_use]
    pub fn call_clobbers() -> Regs {
        Regs {
            gpr: !NONVOLATILE_GPRS,
            fpr: !NONVOLATILE_FPRS,
            cr: !NONVOLATILE_CR,
            lr: true,
            ctr: true,
            ca: true,
            so: true,
            fpscr: true,
        }
    }

    /// What the caller observes after a return: the results and every
    /// non-volatile register.
    #[must_use]
    pub fn return_live() -> Regs {
        Regs {
            gpr: RESULT_GPRS | NONVOLATILE_GPRS,
            fpr: RESULT_FPRS | NONVOLATILE_FPRS,
            cr: NONVOLATILE_CR,
            ..Regs::default()
        }
    }
}

/// Control flow of one instruction.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Flow {
    /// Falls through to the next instruction.
    Next,
    /// Unconditional branch (inside or outside the function).
    Branch(u32),
    /// Conditional branch: the target or the next instruction.
    CondBranch(u32),
    /// `bl target`: a call that returns to the next instruction.
    Call(u32),
    /// `bctrl` / `bclrl`: an indirect call that returns to the next instruction.
    IndirectCall,
    /// `blr`.
    Return,
    /// Conditional `blr`: returns or falls through.
    CondReturn,
    /// `bctr` / conditional `bcctr`: a computed jump (switch table or tail call).
    IndirectJump,
    /// `rfi`: never falls through.
    End,
}

/// Register effects of one instruction. `writes` are definite on the
/// instruction's own path (a conditional write is not a write).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Effect {
    pub reads: Regs,
    pub writes: Regs,
    pub flow: Flow,
    /// The instruction observes or replaces the complete architectural state
    /// (system call, trap, interrupt return, MSR write). Everything is live
    /// before it.
    pub boundary: bool,
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
fn rc(word: u32) -> u32 {
    (word >> 6) & 31
}
fn record(word: u32) -> bool {
    word & 1 != 0
}
fn oe(word: u32) -> bool {
    word & 0x400 != 0
}

fn branch_target(pc: u32, word: u32) -> u32 {
    let displacement = (((word & 0x03FF_FFFC) << 6) as i32) >> 6;
    if word & 2 != 0 {
        displacement as u32
    } else {
        pc.wrapping_add(displacement as u32)
    }
}

fn conditional_target(pc: u32, word: u32) -> u32 {
    let displacement = (word & 0xFFFC) as u16 as i16 as i32;
    if word & 2 != 0 {
        displacement as u32
    } else {
        pc.wrapping_add(displacement as u32)
    }
}

/// The CR field and CTR reads of a BO/BI condition, and whether CTR is
/// decremented (written).
fn condition_effects(word: u32) -> (Regs, Regs) {
    let bo = rt(word);
    let bi = ra(word);
    let mut reads = Regs::default();
    let mut writes = Regs::default();
    if bo & 0x10 == 0 {
        reads.cr |= 1 << (bi / 4);
    }
    if bo & 0x04 == 0 {
        reads.ctr = true;
        writes.ctr = true;
    }
    (reads, writes)
}

/// The register effects of `word` at `pc`, or `None` for a form the decoder
/// does not model (the caller must then treat every register as live).
#[must_use]
pub fn effect(pc: u32, word: u32) -> Option<Effect> {
    use Opcode as O;
    let ins = Ins::new(word, Extensions::gekko_broadway());
    let mut reads = Regs::default();
    let mut writes = Regs::default();
    let mut flow = Flow::Next;
    let mut boundary = false;
    let g = Regs::gpr;
    let f = Regs::fpr;
    let base = |index: u32| {
        if index == 0 {
            Regs::default()
        } else {
            g(index)
        }
    };
    match ins.op {
        // Integer arithmetic and logic: D-form rD <- rA op imm.
        O::Addi | O::Addis => {
            reads = base(ra(word));
            writes = g(rt(word));
        }
        O::Mulli => {
            reads = g(ra(word));
            writes = g(rt(word));
        }
        O::Addic | O::Addic_ | O::Subfic => {
            reads = g(ra(word));
            writes = g(rt(word));
            writes.ca = true;
            if ins.op == O::Addic_ {
                reads.so = true;
                writes = writes.union(Regs::cr_field(0));
            }
        }
        O::Ori | O::Oris | O::Xori | O::Xoris => {
            reads = g(rt(word));
            writes = g(ra(word));
        }
        O::Andi_ | O::Andis_ => {
            reads = g(rt(word));
            reads.so = true;
            writes = g(ra(word)).union(Regs::cr_field(0));
        }
        // XO-form arithmetic rD <- rA op rB (or rA alone).
        O::Add | O::Subf | O::Mullw | O::Mulhw | O::Mulhwu | O::Divw | O::Divwu => {
            reads = g(ra(word)).union(g(rb(word)));
            writes = g(rt(word));
        }
        O::Addc | O::Subfc => {
            reads = g(ra(word)).union(g(rb(word)));
            writes = g(rt(word));
            writes.ca = true;
        }
        O::Adde | O::Subfe => {
            reads = g(ra(word)).union(g(rb(word)));
            reads.ca = true;
            writes = g(rt(word));
            writes.ca = true;
        }
        O::Addze | O::Subfze | O::Addme | O::Subfme => {
            reads = g(ra(word));
            reads.ca = true;
            writes = g(rt(word));
            writes.ca = true;
        }
        O::Neg => {
            reads = g(ra(word));
            writes = g(rt(word));
        }
        // X-form logic rA <- rS op rB.
        O::And
        | O::Or
        | O::Xor
        | O::Nand
        | O::Nor
        | O::Eqv
        | O::Andc
        | O::Orc
        | O::Slw
        | O::Srw => {
            reads = g(rt(word)).union(g(rb(word)));
            writes = g(ra(word));
        }
        O::Sraw => {
            reads = g(rt(word)).union(g(rb(word)));
            writes = g(ra(word));
            writes.ca = true;
        }
        O::Srawi => {
            reads = g(rt(word));
            writes = g(ra(word));
            writes.ca = true;
        }
        O::Cntlzw | O::Extsb | O::Extsh => {
            reads = g(rt(word));
            writes = g(ra(word));
        }
        O::Rlwinm => {
            reads = g(rt(word));
            writes = g(ra(word));
        }
        O::Rlwimi => {
            reads = g(rt(word)).union(g(ra(word)));
            writes = g(ra(word));
        }
        O::Rlwnm => {
            reads = g(rt(word)).union(g(rb(word)));
            writes = g(ra(word));
        }
        // Compares: CR field crfD, XER[SO] copied in.
        O::Cmpi | O::Cmpli => {
            reads = g(ra(word));
            reads.so = true;
            writes = Regs::cr_field(rt(word) >> 2);
        }
        O::Cmp | O::Cmpl => {
            reads = g(ra(word)).union(g(rb(word)));
            reads.so = true;
            writes = Regs::cr_field(rt(word) >> 2);
        }
        // Integer loads.
        O::Lbz | O::Lhz | O::Lha | O::Lwz => {
            reads = base(ra(word));
            writes = g(rt(word));
        }
        O::Lbzu | O::Lhzu | O::Lhau | O::Lwzu => {
            reads = g(ra(word));
            writes = g(rt(word)).union(g(ra(word)));
        }
        O::Lbzx | O::Lhzx | O::Lhax | O::Lwzx | O::Lhbrx | O::Lwbrx => {
            reads = base(ra(word)).union(g(rb(word)));
            writes = g(rt(word));
        }
        O::Lbzux | O::Lhzux | O::Lhaux | O::Lwzux => {
            reads = g(ra(word)).union(g(rb(word)));
            writes = g(rt(word)).union(g(ra(word)));
        }
        O::Lmw => {
            reads = base(ra(word));
            writes.gpr = u32::MAX << rt(word);
        }
        // Integer stores.
        O::Stb | O::Sth | O::Stw => {
            reads = base(ra(word)).union(g(rt(word)));
        }
        O::Stbu | O::Sthu | O::Stwu => {
            reads = g(ra(word)).union(g(rt(word)));
            writes = g(ra(word));
        }
        O::Stbx | O::Sthx | O::Stwx | O::Sthbrx | O::Stwbrx => {
            reads = base(ra(word)).union(g(rb(word))).union(g(rt(word)));
        }
        O::Stbux | O::Sthux | O::Stwux => {
            reads = g(ra(word)).union(g(rb(word))).union(g(rt(word)));
            writes = g(ra(word));
        }
        O::Stmw => {
            reads = base(ra(word));
            reads.gpr |= u32::MAX << rt(word);
        }
        // Float loads and stores. An FPR covers both paired-single lanes.
        O::Lfs | O::Lfd | O::PsqL => {
            reads = base(ra(word));
            writes = f(rt(word));
        }
        O::Lfsu | O::Lfdu | O::PsqLu => {
            reads = g(ra(word));
            writes = f(rt(word)).union(g(ra(word)));
        }
        O::Lfsx | O::Lfdx | O::PsqLx => {
            reads = base(ra(word)).union(g(rb(word)));
            writes = f(rt(word));
        }
        O::Lfsux | O::Lfdux | O::PsqLux => {
            reads = g(ra(word)).union(g(rb(word)));
            writes = f(rt(word)).union(g(ra(word)));
        }
        O::Stfs | O::Stfd | O::PsqSt => {
            reads = base(ra(word)).union(f(rt(word)));
        }
        O::Stfsu | O::Stfdu | O::PsqStu => {
            reads = g(ra(word)).union(f(rt(word)));
            writes = g(ra(word));
        }
        O::Stfsx | O::Stfdx | O::PsqStx | O::Stfiwx => {
            reads = base(ra(word)).union(g(rb(word))).union(f(rt(word)));
        }
        O::Stfsux | O::Stfdux | O::PsqStux => {
            reads = g(ra(word)).union(g(rb(word))).union(f(rt(word)));
            writes = g(ra(word));
        }
        // Float arithmetic (A-form: frD, frA, frB, frC as the form uses them).
        O::Fadd
        | O::Fadds
        | O::Fsub
        | O::Fsubs
        | O::Fdiv
        | O::Fdivs
        | O::PsAdd
        | O::PsSub
        | O::PsDiv => {
            reads = f(ra(word)).union(f(rb(word)));
            writes = f(rt(word));
        }
        O::Fmul | O::Fmuls | O::PsMul | O::PsMuls0 | O::PsMuls1 => {
            reads = f(ra(word)).union(f(rc(word)));
            writes = f(rt(word));
        }
        O::Fmadd
        | O::Fmadds
        | O::Fmsub
        | O::Fmsubs
        | O::Fnmadd
        | O::Fnmadds
        | O::Fnmsub
        | O::Fnmsubs
        | O::PsMadd
        | O::PsMsub
        | O::PsNmadd
        | O::PsNmsub
        | O::PsMadds0
        | O::PsMadds1
        | O::PsSum0
        | O::PsSum1
        | O::Fsel
        | O::PsSel => {
            reads = f(ra(word)).union(f(rb(word))).union(f(rc(word)));
            writes = f(rt(word));
        }
        O::PsMerge00 | O::PsMerge01 | O::PsMerge10 | O::PsMerge11 => {
            reads = f(ra(word)).union(f(rb(word)));
            writes = f(rt(word));
        }
        O::Fmr
        | O::Fneg
        | O::Fabs
        | O::Fnabs
        | O::Frsp
        | O::Fctiw
        | O::Fctiwz
        | O::Fres
        | O::Frsqrte
        | O::PsMr
        | O::PsNeg
        | O::PsAbs
        | O::PsNabs
        | O::PsRes
        | O::PsRsqrte => {
            reads = f(rb(word));
            writes = f(rt(word));
            if matches!(
                ins.op,
                O::Fmr | O::Fneg | O::Fabs | O::Fnabs | O::PsMr | O::PsNeg | O::PsAbs | O::PsNabs
            ) {
                // The scalar moves keep ps1; the paired ones replace both
                // lanes. Either way the register is read for the kept lane.
                reads = reads.union(f(rt(word)));
            }
        }
        O::Fcmpu | O::Fcmpo | O::PsCmpu0 | O::PsCmpo0 | O::PsCmpu1 | O::PsCmpo1 => {
            reads = f(ra(word)).union(f(rb(word)));
            reads.fpscr = true;
            writes = Regs::cr_field(rt(word) >> 2);
        }
        O::Mffs => {
            reads.fpscr = true;
            writes = f(rt(word));
        }
        O::Mtfsf => {
            reads = f(rb(word));
            reads.fpscr = true;
            writes.fpscr = true;
        }
        O::Mtfsfi | O::Mtfsb0 | O::Mtfsb1 => {
            reads.fpscr = true;
            writes.fpscr = true;
        }
        // Condition register.
        O::Crand | O::Crandc | O::Creqv | O::Crnand | O::Crnor | O::Cror | O::Crorc | O::Crxor => {
            reads.cr = (1 << (ra(word) / 4)) | (1 << (rb(word) / 4)) | (1 << (rt(word) / 4));
            writes.cr = 1 << (rt(word) / 4);
        }
        O::Mcrf => {
            reads = Regs::cr_field(ra(word) >> 2);
            writes = Regs::cr_field(rt(word) >> 2);
        }
        O::Mfcr => {
            reads.cr = u8::MAX;
            writes = g(rt(word));
        }
        O::Mtcrf => {
            let mask = ((word >> 12) & 0xFF) as u8;
            reads = g(rt(word));
            // A whole-field write for every selected field; partially kept
            // state cannot arise because CRM selects whole fields.
            writes.cr = mask.reverse_bits();
        }
        // Special registers.
        O::Mfspr => {
            let spr = ((word >> 16) & 31) | ((word >> 6) & 0x3E0);
            writes = g(rt(word));
            match spr {
                1 => {
                    reads.ca = true;
                    reads.so = true;
                }
                8 => reads.lr = true,
                9 => reads.ctr = true,
                _ => {}
            }
        }
        O::Mtspr => {
            let spr = ((word >> 16) & 31) | ((word >> 6) & 0x3E0);
            reads = g(rt(word));
            match spr {
                1 => {
                    writes.ca = true;
                    writes.so = true;
                }
                8 => writes.lr = true,
                9 => writes.ctr = true,
                _ => boundary = true,
            }
        }
        O::Mftb | O::Mfmsr | O::Mfsr => {
            writes = g(rt(word));
        }
        O::Mtmsr | O::Mtsr => {
            reads = g(rt(word));
            boundary = true;
        }
        // Cache and synchronization.
        O::Dcbf | O::Dcbst | O::Dcbi | O::Dcbt | O::Dcbz | O::DcbzL | O::Icbi | O::Dcbtst => {
            reads = base(ra(word)).union(g(rb(word)));
        }
        O::Sync | O::Isync | O::Eieio => {}
        O::Sc | O::Tw | O::Twi => {
            boundary = true;
        }
        O::Rfi => {
            boundary = true;
            flow = Flow::End;
        }
        // Branches.
        O::B => {
            let target = branch_target(pc, word);
            if word & 1 != 0 {
                writes.lr = true;
                flow = Flow::Call(target);
            } else {
                flow = Flow::Branch(target);
            }
        }
        O::Bc => {
            if word & 1 != 0 {
                // bcl: CodeWarrior's PC-materialising `bcl 20,31,$+4` is the
                // only use; it is not modelled.
                return None;
            }
            let (condition_reads, condition_writes) = condition_effects(word);
            reads = condition_reads;
            writes = condition_writes;
            let bo = rt(word);
            flow = if bo & 0x14 == 0x14 {
                Flow::Branch(conditional_target(pc, word))
            } else {
                Flow::CondBranch(conditional_target(pc, word))
            };
        }
        O::Bclr => {
            let (condition_reads, condition_writes) = condition_effects(word);
            reads = condition_reads;
            reads.lr = true;
            writes = condition_writes;
            let unconditional = rt(word) & 0x14 == 0x14;
            if word & 1 != 0 {
                if !unconditional {
                    return None;
                }
                writes.lr = true;
                flow = Flow::IndirectCall;
            } else {
                flow = if unconditional {
                    Flow::Return
                } else {
                    Flow::CondReturn
                };
            }
        }
        O::Bcctr => {
            let (condition_reads, condition_writes) = condition_effects(word);
            reads = condition_reads;
            reads.ctr = true;
            writes = condition_writes;
            let unconditional = rt(word) & 0x14 == 0x14;
            if word & 1 != 0 {
                if !unconditional {
                    return None;
                }
                writes.lr = true;
                flow = Flow::IndirectCall;
            } else {
                flow = Flow::IndirectJump;
            }
        }
        _ => return None,
    }
    // Record forms: integer CR0 (with XER[SO]); float CR1 (from FPSCR).
    let primary = word >> 26;
    let xo_form_record = matches!(primary, 31 | 21 | 23 | 20) && record(word);
    if xo_form_record
        && !matches!(
            ins.op,
            O::Mfcr | O::Mtcrf | O::Mfspr | O::Mtspr | O::Cmp | O::Cmpl
        )
    {
        reads.so = true;
        writes = writes.union(Regs::cr_field(0));
    }
    if matches!(primary, 59 | 63 | 4) && record(word) && !matches!(ins.op, O::Fcmpu | O::Fcmpo) {
        reads.fpscr = true;
        writes = writes.union(Regs::cr_field(1));
    }
    if primary == 31
        && oe(word)
        && matches!(
            ins.op,
            O::Add
                | O::Addc
                | O::Adde
                | O::Addze
                | O::Addme
                | O::Subf
                | O::Subfc
                | O::Subfe
                | O::Subfze
                | O::Subfme
                | O::Neg
                | O::Mullw
                | O::Divw
                | O::Divwu
        )
    {
        reads.so = true;
        writes.so = true;
    }
    Some(Effect {
        reads,
        writes,
        flow,
        boundary,
    })
}

/// What a call site reads and leaves undefined.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct CallModel {
    pub uses: Regs,
    pub clobbers: Regs,
}

impl CallModel {
    /// The EABI contract of an ordinary callee.
    #[must_use]
    pub fn abi() -> Self {
        Self {
            uses: abi::call_uses(),
            clobbers: abi::call_clobbers(),
        }
    }
}

/// Liveness of one function.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct FunctionLiveness {
    pub start: u32,
    /// Registers live before each instruction, by instruction index.
    pub live_before: Vec<Regs>,
    /// Registers live after each instruction, by instruction index.
    pub live_after: Vec<Regs>,
    /// The decoded effects (`None`: undecodable, everything live).
    pub effects: Vec<Option<Effect>>,
}

impl FunctionLiveness {
    #[must_use]
    pub fn entry_live(&self) -> Regs {
        self.live_before.first().copied().unwrap_or(Regs::ALL)
    }

    fn index(&self, pc: u32) -> Option<usize> {
        let offset = pc.checked_sub(self.start)?;
        let index = (offset / 4) as usize;
        (offset % 4 == 0 && index < self.live_before.len()).then_some(index)
    }

    #[must_use]
    pub fn live_before_pc(&self, pc: u32) -> Option<Regs> {
        self.index(pc).map(|index| self.live_before[index])
    }

    #[must_use]
    pub fn live_after_pc(&self, pc: u32) -> Option<Regs> {
        self.index(pc).map(|index| self.live_after[index])
    }
}

/// Backward liveness over the function at `start`. `exit_live` is live at
/// every return and at every branch out of the function; `call_model` gives a
/// direct call's reads and clobbers by target (indirect calls use the EABI
/// model); `entries` are interior entries (everything is live before them,
/// since control can arrive with any state).
#[must_use]
pub fn analyze_liveness(
    start: u32,
    words: &[u32],
    exit_live: Regs,
    call_model: &dyn Fn(u32) -> CallModel,
    entries: &[u32],
) -> FunctionLiveness {
    let count = words.len();
    let effects: Vec<Option<Effect>> = words
        .iter()
        .enumerate()
        .map(|(index, word)| effect(start + index as u32 * 4, *word))
        .collect();
    let index_of = |pc: u32| -> Option<usize> {
        let offset = pc.checked_sub(start)?;
        let index = (offset / 4) as usize;
        (offset % 4 == 0 && index < count).then_some(index)
    };
    let mut entry_indices: Vec<usize> = entries.iter().filter_map(|pc| index_of(*pc)).collect();
    entry_indices.sort_unstable();
    let mut live_before = vec![Regs::default(); count];
    let mut live_after = vec![Regs::default(); count];
    loop {
        let mut changed = false;
        for index in (0..count).rev() {
            let next = |index: usize| -> Regs {
                if index + 1 < count {
                    live_before[index + 1]
                } else {
                    // Falling off the end of the function: unknown.
                    Regs::ALL
                }
            };
            let target_live = |target: u32| -> Regs {
                match index_of(target) {
                    Some(target) => live_before[target],
                    None => exit_live,
                }
            };
            let (after, before) = match effects[index] {
                None => (Regs::ALL, Regs::ALL),
                Some(effect) => {
                    let after = match effect.flow {
                        Flow::Next => next(index),
                        Flow::Branch(target) => target_live(target),
                        Flow::CondBranch(target) => next(index).union(target_live(target)),
                        Flow::Call(_) | Flow::IndirectCall => next(index),
                        Flow::Return => exit_live,
                        Flow::CondReturn => exit_live.union(next(index)),
                        Flow::IndirectJump | Flow::End => Regs::ALL,
                    };
                    let before = match effect.flow {
                        Flow::Call(target) => {
                            let model = call_model(target);
                            after
                                .minus(model.clobbers)
                                .minus(effect.writes)
                                .union(model.uses)
                                .union(effect.reads)
                        }
                        Flow::IndirectCall => {
                            let model = CallModel::abi();
                            after
                                .minus(model.clobbers)
                                .minus(effect.writes)
                                .union(model.uses)
                                .union(effect.reads)
                        }
                        _ if effect.boundary => Regs::ALL,
                        _ => after.minus(effect.writes).union(effect.reads),
                    };
                    (after, before)
                }
            };
            let before = if entry_indices.binary_search(&index).is_ok() {
                before.union(Regs::ALL)
            } else {
                before
            };
            if after != live_after[index] || before != live_before[index] {
                live_after[index] = after;
                live_before[index] = before;
                changed = true;
            }
        }
        if !changed {
            break;
        }
    }
    FunctionLiveness {
        start,
        live_before,
        live_after,
        effects,
    }
}

/// ABI facts about one function, from its liveness under the EABI call model.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct AbiFacts {
    /// Registers read before written (entry live-in).
    pub entry_live: Regs,
    /// Every register any instruction of the function writes.
    pub writes: Regs,
    /// Non-volatile registers written without being entry-live: not saved,
    /// so the caller's value is lost.
    pub nonvolatile_clobbers: Regs,
    /// Volatile non-argument registers read at entry (r0, r11, r12, CTR, ...).
    pub scratch_entry_reads: Regs,
    /// Call sites after which a volatile register other than the results is
    /// read before being rewritten, with those registers.
    pub volatile_reads_after_calls: BTreeMap<u32, Regs>,
    /// The function has an instruction the decoder does not model.
    pub undecodable: bool,
}

impl AbiFacts {
    /// True when the function follows the EABI as far as the analysis sees.
    #[must_use]
    pub fn abi_conformant(&self) -> bool {
        !self.undecodable
            && self.nonvolatile_clobbers.is_empty()
            && self.scratch_entry_reads.is_empty()
            && self.volatile_reads_after_calls.is_empty()
    }
}

/// The EABI facts of the function at `start`.
#[must_use]
pub fn abi_facts(start: u32, words: &[u32], entries: &[u32]) -> AbiFacts {
    let liveness = analyze_liveness(
        start,
        words,
        abi::return_live(),
        &|_| CallModel::abi(),
        entries,
    );
    let mut facts = AbiFacts {
        entry_live: liveness.entry_live(),
        ..AbiFacts::default()
    };
    for (index, effect) in liveness.effects.iter().enumerate() {
        let Some(effect) = effect else {
            facts.undecodable = true;
            continue;
        };
        facts.writes = facts.writes.union(effect.writes);
        if matches!(effect.flow, Flow::Call(_) | Flow::IndirectCall) {
            let pc = start + index as u32 * 4;
            let clobbered = abi::call_clobbers();
            let read_after = liveness.live_after[index]
                .intersect(clobbered)
                .minus(Regs {
                    gpr: abi::RESULT_GPRS,
                    fpr: abi::RESULT_FPRS,
                    ..Regs::default()
                })
                // XER, FPSCR and LR are not modelled as call results; their
                // reads after a call (mfxer, mffs, mflr) are rare and
                // covered by the context, which keeps them.
                .minus(Regs {
                    lr: true,
                    ca: true,
                    so: true,
                    fpscr: true,
                    ..Regs::default()
                });
            if !read_after.is_empty() {
                facts.volatile_reads_after_calls.insert(pc, read_after);
            }
        }
    }
    if facts.undecodable {
        return facts;
    }
    let nonvolatile = Regs {
        gpr: abi::NONVOLATILE_GPRS,
        fpr: abi::NONVOLATILE_FPRS,
        cr: abi::NONVOLATILE_CR,
        ..Regs::default()
    };
    facts.nonvolatile_clobbers = facts.writes.intersect(nonvolatile).minus(facts.entry_live);
    facts.scratch_entry_reads = facts
        .entry_live
        .minus(abi::call_uses())
        .minus(nonvolatile)
        .minus(Regs {
            lr: true,
            fpscr: true,
            so: true,
            ..Regs::default()
        });
    facts
}

/// Instructions whose CR-field or carry write is dead: written, then
/// overwritten or abandoned on every path before any read. Keyed by pc, with
/// the dead part of the write.
#[must_use]
pub fn dead_flag_writes(liveness: &FunctionLiveness) -> BTreeMap<u32, Regs> {
    let mut dead = BTreeMap::new();
    for (index, effect) in liveness.effects.iter().enumerate() {
        let Some(effect) = effect else { continue };
        if effect.boundary {
            continue;
        }
        let flags = Regs {
            cr: effect.writes.cr,
            ca: effect.writes.ca,
            ..Regs::default()
        };
        if flags.is_empty() {
            continue;
        }
        let unread = flags.minus(liveness.live_after[index]);
        if !unread.is_empty() {
            dead.insert(liveness.start + index as u32 * 4, unread);
        }
    }
    dead
}

#[cfg(test)]
mod tests {
    use super::*;

    const BLR: u32 = 0x4E80_0020;

    fn addi(rd: u32, ra: u32, imm: i32) -> u32 {
        (14 << 26) | (rd << 21) | (ra << 16) | (imm as u32 & 0xFFFF)
    }
    fn stw(rs: u32, d: i32, ra: u32) -> u32 {
        (36 << 26) | (rs << 21) | (ra << 16) | (d as u32 & 0xFFFF)
    }
    fn lwz(rt: u32, d: i32, ra: u32) -> u32 {
        (32 << 26) | (rt << 21) | (ra << 16) | (d as u32 & 0xFFFF)
    }
    fn bl(pc: u32, target: u32) -> u32 {
        (18 << 26) | (target.wrapping_sub(pc) & 0x03FF_FFFC) | 1
    }
    fn x(rt: u32, ra: u32, rb: u32, xo: u32, rc: bool) -> u32 {
        (31 << 26) | (rt << 21) | (ra << 16) | (rb << 11) | (xo << 1) | u32::from(rc)
    }

    #[test]
    fn decodes_implicit_operands() {
        // add. r3,r4,r5 writes CR0 and reads SO.
        let add_dot = effect(0, x(3, 4, 5, 266, true)).unwrap();
        assert_eq!(add_dot.writes.gpr, 1 << 3);
        assert_eq!(add_dot.writes.cr, 1);
        assert!(add_dot.reads.so);
        // adde reads and writes CA.
        let adde = effect(0, x(3, 4, 5, 138, false)).unwrap();
        assert!(adde.reads.ca && adde.writes.ca);
        // srawi writes CA.
        assert!(effect(0, x(4, 3, 2, 824, false)).unwrap().writes.ca);
        // lmw r27 writes r27..r31; stmw r27 reads them.
        assert_eq!(
            effect(0, (46 << 26) | (27 << 21) | (1 << 16) | 8)
                .unwrap()
                .writes
                .gpr,
            0xF800_0000
        );
        assert_eq!(
            effect(0, (47 << 26) | (27 << 21) | (1 << 16) | 8)
                .unwrap()
                .reads
                .gpr,
            0xF800_0000 | 2
        );
        // bdnz reads and writes CTR; blr reads LR; bl writes LR.
        let bdnz = effect(0x100, (16 << 26) | (16 << 21) | 0xFFF8).unwrap();
        assert!(bdnz.reads.ctr && bdnz.writes.ctr);
        assert_eq!(bdnz.flow, Flow::CondBranch(0xF8));
        assert!(effect(0, BLR).unwrap().reads.lr);
        assert_eq!(
            effect(0x100, bl(0x100, 0x200)).unwrap().flow,
            Flow::Call(0x200)
        );
        // cmpwi cr7 writes field 7.
        assert_eq!(
            effect(0, (11 << 26) | (7 << 23) | (3 << 16))
                .unwrap()
                .writes
                .cr,
            1 << 7
        );
        // fadds. writes CR1.
        assert_eq!(
            effect(
                0,
                (59 << 26) | (1 << 21) | (2 << 16) | (3 << 11) | (21 << 1) | 1
            )
            .unwrap()
            .writes
            .cr,
            1 << 1
        );
        // mflr / mtctr.
        assert!(effect(0, 0x7C08_02A6).unwrap().reads.lr);
        assert!(effect(0, 0x7D89_03A6).unwrap().writes.ctr);
    }

    #[test]
    fn standard_prologue_function_is_abi_conformant() {
        // stwu r1,-16(r1); mflr r0; stw r0,20(r1); stw r31,12(r1); mr r31,r3;
        // bl f; add r3,r31,r3; lwz r0,20(r1); lwz r31,12(r1); mtlr r0;
        // addi r1,r1,16; blr
        let start = 0x8000_4000;
        let words = [
            (37 << 26) | (1 << 21) | (1 << 16) | 0xFFF0,
            0x7C08_02A6,
            stw(0, 20, 1),
            stw(31, 12, 1),
            x(3, 31, 3, 444, false),
            bl(start + 20, 0x8000_5000),
            x(3, 31, 3, 266, false),
            lwz(0, 20, 1),
            lwz(31, 12, 1),
            0x7C08_03A6,
            addi(1, 1, 16),
            BLR,
        ];
        let facts = abi_facts(start, &words, &[]);
        assert!(facts.abi_conformant(), "{facts:?}");
        // r1, r3 (argument), r31 (saved) and LR are read at entry.
        assert!(facts.entry_live.gpr & (1 << 1) != 0);
        assert!(facts.entry_live.gpr & (1 << 3) != 0);
        assert!(facts.entry_live.gpr & (1 << 31) != 0);
        assert!(facts.entry_live.lr);
    }

    #[test]
    fn non_abi_patterns_are_reported() {
        let start = 0x8000_4000;
        // Reads r5 after a call: lwz r5 is not done; add r3,r5,r5 after bl.
        let after_call = [bl(start, 0x8000_5000), x(3, 5, 5, 266, false), BLR];
        let facts = abi_facts(start, &after_call, &[]);
        assert_eq!(
            facts.volatile_reads_after_calls.get(&start).map(|r| r.gpr),
            Some(1 << 5)
        );
        // Reads r12 at entry (scratch).
        let scratch = [x(3, 12, 12, 266, false), BLR];
        assert_eq!(
            abi_facts(start, &scratch, &[]).scratch_entry_reads.gpr,
            1 << 12
        );
        // Writes r31 without saving it.
        let clobber = [addi(31, 0, 1), BLR];
        assert_eq!(
            abi_facts(start, &clobber, &[]).nonvolatile_clobbers.gpr,
            1 << 31
        );
    }

    #[test]
    fn dead_record_and_carry_writes_are_found() {
        let start = 0x8000_4000;
        // add. r3,r4,r5 (CR0 never read); addc r6,r3,r4 (CA never read);
        // cmpwi r3,0; beq +8 (CR0 read); blr; blr
        let words = [
            x(3, 4, 5, 266, true),
            x(6, 3, 4, 10, false),
            (11 << 26) | (3 << 16),
            (16 << 26) | (12 << 21) | (2 << 16) | 8,
            BLR,
            BLR,
        ];
        let liveness = analyze_liveness(
            start,
            &words,
            abi::return_live(),
            &|_| CallModel::abi(),
            &[],
        );
        let dead = dead_flag_writes(&liveness);
        assert_eq!(dead.get(&start).map(|r| r.cr), Some(1));
        assert_eq!(dead.get(&(start + 4)).map(|r| r.ca), Some(true));
        assert!(
            !dead.contains_key(&(start + 8)),
            "the compare feeds the branch"
        );
    }

    #[test]
    fn interior_entries_and_unknown_forms_are_conservative() {
        let start = 0x8000_4000;
        let words = [addi(3, 0, 1), x(3, 3, 3, 266, true), BLR];
        let liveness = analyze_liveness(
            start,
            &words,
            abi::return_live(),
            &|_| CallModel::abi(),
            &[start + 4],
        );
        assert_eq!(liveness.live_before[1], Regs::ALL);
        let unknown = [0x0000_0000, BLR];
        let facts = abi_facts(start, &unknown, &[]);
        assert!(facts.undecodable && !facts.abi_conformant());
    }
}
