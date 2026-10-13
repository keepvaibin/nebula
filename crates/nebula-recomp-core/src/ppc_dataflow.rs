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
//! a computed jump whose targets are not known, a trap, a system call or an
//! interrupt return makes every register live there. A switch's `bctr` makes
//! live what its own case labels read; a `bctr` through a pointer loaded
//! from memory what a tail call (or an in-function address the image holds)
//! reads.

use powerpc::{Extensions, Ins, Opcode};
use std::collections::{BTreeMap, BTreeSet};
use std::fmt::Write as _;

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
    /// The instruction hands the complete architectural state to a handler
    /// or replaces it (system call, trap, interrupt return). Everything is
    /// live before it.
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
        // `ori rX,rX,0` (the `nop` encoding when X = 0) and its siblings leave
        // the register unchanged: no read, no write.
        O::Ori | O::Oris | O::Xori | O::Xoris if word & 0xFFFF == 0 && rt(word) == ra(word) => {}
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
            // The scalar moves keep frD's ps1 lane. It is not counted as a
            // read: scalar code never reads ps1, and EABI code never relies
            // on a volatile register's ps1 surviving a call.
            reads = f(rb(word));
            writes = f(rt(word));
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
        // Writes of machine state (MSR, segment and special registers) read
        // their operand only. An interrupt that MSR[EE] lets in is delivered
        // at the translated checkpoint, which synchronizes the whole register
        // file itself, so it adds nothing to the liveness here.
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
                _ => {}
            }
        }
        O::Mftb | O::Mfmsr | O::Mfsr => {
            writes = g(rt(word));
        }
        O::Mtmsr | O::Mtsr => {
            reads = g(rt(word));
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
                // `bl $+4` materialises the PC in LR and falls through.
                flow = if target == pc.wrapping_add(4) {
                    Flow::Next
                } else {
                    Flow::Call(target)
                };
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
/// every return; `call_model` gives a call's reads and clobbers by target
/// (`None`: an indirect call). A branch out of the function is a tail call:
/// live there are the target's reads, LR and `exit_live`. Interior entries
/// need no special case: liveness at a point is what the code after it reads,
/// however control arrives.
#[must_use]
pub fn analyze_liveness(
    start: u32,
    words: &[u32],
    exit_live: Regs,
    call_model: &dyn Fn(Option<u32>) -> CallModel,
) -> FunctionLiveness {
    analyze_effects(
        start,
        decode(start, words, &|_| None),
        exit_live,
        call_model,
    )
}

/// A call to a helper the translator lowers in place (`bl _savegpr_N`,
/// `bl _restgpr_N`): its register reads and writes, by target.
pub type InlineCalls<'a> = dyn Fn(u32) -> Option<(Regs, Regs)> + 'a;

/// The effects of a function's instructions, with in-place helper calls
/// replaced by their register effects.
#[must_use]
pub fn decode(start: u32, words: &[u32], inline_calls: &InlineCalls<'_>) -> Vec<Option<Effect>> {
    words
        .iter()
        .enumerate()
        .map(|(index, word)| {
            let decoded = effect(start + index as u32 * 4, *word)?;
            match decoded.flow {
                Flow::Call(target) => match inline_calls(target) {
                    Some((reads, writes)) => Some(Effect {
                        reads: decoded.reads.union(reads),
                        writes: decoded.writes.union(writes),
                        flow: Flow::Next,
                        boundary: false,
                    }),
                    None => Some(decoded),
                },
                _ => Some(decoded),
            }
        })
        .collect()
}

/// `analyze_liveness` over decoded effects.
#[must_use]
pub fn analyze_effects(
    start: u32,
    effects: Vec<Option<Effect>>,
    exit_live: Regs,
    call_model: &dyn Fn(Option<u32>) -> CallModel,
) -> FunctionLiveness {
    analyze_effects_with_jump_targets(start, effects, exit_live, call_model, &NO_JUMP_TARGETS)
}

/// The in-function targets of a function's computed jumps (`bctr` through a
/// switch table).
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct JumpTargets {
    /// The case labels of one `bctr`'s own table, by the `bctr`'s pc.
    pub by_site: BTreeMap<u32, Vec<u32>>,
    /// Every address inside the function past its start that the image's
    /// data holds or its code builds: the possible in-function targets of a
    /// `bctr` whose table is not known.
    pub any: Vec<u32>,
    /// `bctr`s through a pointer loaded from memory (a call in tail
    /// position, or a jump to one of `any`).
    pub loaded: BTreeSet<u32>,
}

/// A function without computed jumps (or with no known targets).
pub static NO_JUMP_TARGETS: JumpTargets = JumpTargets {
    by_site: BTreeMap::new(),
    any: Vec::new(),
    loaded: BTreeSet::new(),
};

/// `analyze_effects` knowing the in-function targets of the function's
/// computed jumps. A switch makes live what its case labels read; a jump
/// through a loaded pointer what a tail call or one of `any` reads; any other
/// computed jump everything.
#[must_use]
pub fn analyze_effects_with_jump_targets(
    start: u32,
    effects: Vec<Option<Effect>>,
    exit_live: Regs,
    call_model: &dyn Fn(Option<u32>) -> CallModel,
    jump_targets: &JumpTargets,
) -> FunctionLiveness {
    let count = effects.len();
    let index_of = |pc: u32| -> Option<usize> {
        let offset = pc.checked_sub(start)?;
        let index = (offset / 4) as usize;
        (offset % 4 == 0 && index < count).then_some(index)
    };
    let any_indices: Vec<usize> = jump_targets
        .any
        .iter()
        .filter_map(|target| index_of(*target))
        .collect();
    let site_indices: BTreeMap<usize, Vec<usize>> = jump_targets
        .by_site
        .iter()
        .filter_map(|(site, targets)| {
            let indices: Vec<usize> = targets
                .iter()
                .filter_map(|target| index_of(*target))
                .collect();
            (!indices.is_empty()).then_some((index_of(*site)?, indices))
        })
        .collect();
    // A computed jump that is not a known switch may also be a tail call
    // through a pointer (a virtual call in tail position).
    let tail_live = exit_live.union(call_model(None).uses).union(Regs {
        lr: true,
        ..Regs::default()
    });
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
                    None => exit_live.union(call_model(Some(target)).uses).union(Regs {
                        lr: true,
                        ..Regs::default()
                    }),
                }
            };
            let jump_live = |index: usize| -> Regs {
                match site_indices.get(&index) {
                    Some(targets) => targets.iter().fold(Regs::default(), |live, target| {
                        live.union(live_before[*target])
                    }),
                    None if jump_targets.loaded.contains(&(start + index as u32 * 4)) => {
                        any_indices
                            .iter()
                            .fold(tail_live, |live, target| live.union(live_before[*target]))
                    }
                    None => Regs::ALL,
                }
            };
            let (after, before) = match effects[index] {
                None => (Regs::ALL, Regs::ALL),
                // `bl` into the function's own body past its start: a local
                // subroutine whose `blr` returns inside the function. Not
                // modelled. A `bl` to the start is ordinary recursion.
                Some(Effect {
                    flow: Flow::Call(target),
                    ..
                }) if index_of(target).is_some_and(|target| target != 0) => (Regs::ALL, Regs::ALL),
                Some(effect) => {
                    let after = match effect.flow {
                        Flow::Next => next(index),
                        Flow::Branch(target) => target_live(target),
                        Flow::CondBranch(target) => next(index).union(target_live(target)),
                        // A call the function ends with does not return
                        // (CodeWarrior's calls to noreturn functions): nothing
                        // after it reads anything.
                        Flow::Call(_) | Flow::IndirectCall if index + 1 == count => Regs::default(),
                        Flow::Call(_) | Flow::IndirectCall => next(index),
                        Flow::Return => exit_live,
                        Flow::CondReturn => exit_live.union(next(index)),
                        Flow::IndirectJump => jump_live(index),
                        Flow::End => Regs::ALL,
                    };
                    let before = match effect.flow {
                        Flow::Call(target) => {
                            let model = call_model(Some(target));
                            after
                                .minus(model.clobbers)
                                .minus(effect.writes)
                                .union(model.uses)
                                .union(effect.reads)
                        }
                        Flow::IndirectCall => {
                            let model = call_model(None);
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

/// The EABI facts of the function at `start`, every call under the EABI
/// model.
#[must_use]
pub fn abi_facts(start: u32, words: &[u32]) -> AbiFacts {
    abi_facts_with(start, decode(start, words, &|_| None), &|_| {
        CallModel::abi()
    })
}

/// The EABI facts of the function at `start` under a call model.
#[must_use]
pub fn abi_facts_with(
    start: u32,
    effects: Vec<Option<Effect>>,
    call_model: &dyn Fn(Option<u32>) -> CallModel,
) -> AbiFacts {
    abi_facts_with_jump_targets(start, effects, call_model, &NO_JUMP_TARGETS)
}

/// `abi_facts_with` knowing the in-function targets of computed jumps.
#[must_use]
pub fn abi_facts_with_jump_targets(
    start: u32,
    effects: Vec<Option<Effect>>,
    call_model: &dyn Fn(Option<u32>) -> CallModel,
    jump_targets: &JumpTargets,
) -> AbiFacts {
    let liveness = analyze_effects_with_jump_targets(
        start,
        effects,
        abi::return_live(),
        call_model,
        jump_targets,
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
                .minus(abi_results())
                // LR after a call is the return address, which the caller
                // itself sets; CR, XER and FPSCR live in the context.
                .minus(Regs {
                    cr: u8::MAX,
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
    facts.nonvolatile_clobbers = facts
        .writes
        .intersect(nonvolatile())
        .minus(facts.entry_live);
    facts.scratch_entry_reads = facts
        .entry_live
        .minus(abi::call_uses())
        .minus(nonvolatile())
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

/// One translated function as the module ABI analysis sees it.
#[derive(Clone, Debug)]
pub struct AbiFunctionInput<'a> {
    pub start: u32,
    pub words: &'a [u32],
    /// The function's body is not its guest code (host substitution, lean
    /// body, audited wrapper): direct callers keep full synchronization.
    pub native: bool,
    /// What the function's computed jumps may reach, from the image's data
    /// and code (`NO_JUMP_TARGETS`: anywhere).
    pub jump_targets: &'a JumpTargets,
}

/// Why a function keeps full context synchronization at its calls, at its
/// returns and when called directly.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum AbiOptOut {
    /// A host substitution or lean body.
    Native,
    /// An instruction form the effect decoder does not model.
    Undecodable,
    /// `bl` into its own body (a local subroutine returning inside it).
    InternalLink,
    /// Reads a volatile register after a call that its callee's call tree
    /// may write: an extra result (OS assembly) or an uninitialized local on
    /// a default path.
    ReadsAfterCall,
    /// The direct callee a `ReadsAfterCall` function reads from: its returns
    /// store everything it wrote.
    ExtraResult,
}

impl AbiOptOut {
    #[must_use]
    pub fn name(self) -> &'static str {
        match self {
            Self::Native => "native",
            Self::Undecodable => "undecodable",
            Self::InternalLink => "internal-link",
            Self::ReadsAfterCall => "reads-after-call",
            Self::ExtraResult => "extra-result",
        }
    }

    /// The function's guest code was analyzed: its entry registers and
    /// clobbers are known even though it keeps full synchronization.
    #[must_use]
    pub fn modelled(self) -> bool {
        matches!(self, Self::ReadsAfterCall | Self::ExtraResult)
    }
}

/// The ABI facts of one function the code generator uses.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct FunctionAbi {
    pub opt_out: Option<AbiOptOut>,
    /// Volatile non-argument registers read at entry; callers store them.
    pub entry_extra: Regs,
    /// Non-volatile registers that may differ when a call returns: the
    /// function's own unsaved writes and those of everything it calls.
    pub clobbers: Regs,
    /// Call sites after which a volatile register other than the results
    /// is read before being rewritten, with those registers.
    pub reads_after_calls: BTreeMap<u32, Regs>,
    /// Direct calls: (pc, target).
    pub direct_calls: Vec<(u32, u32)>,
    /// Branches to another function's start (tail calls): (pc, target).
    pub tail_calls: Vec<(u32, u32)>,
    /// The function makes an indirect call or computed jump.
    pub indirect_calls: bool,
    /// Indirect calls (`bctrl`, `blrl`) by pc.
    pub indirect_call_sites: Vec<u32>,
    /// Non-volatile registers the function saves and restores: it reads
    /// them before it writes them. A callee's clobber of one stays hidden.
    pub saved: Regs,
    /// The function can return to its caller: it has a `blr`, a tail call
    /// or a computed jump, or it was not decoded. A function that ends in
    /// `rfi`, an endless loop or a call that does not return never hands its
    /// register state back, so its callers do not inherit its clobbers.
    pub returns: bool,
    /// Every register an instruction of the function may write (every
    /// register when unknown: host substitutions, unmodelled code).
    pub writes: Regs,
}

/// What a caller synchronizes around one call under the EABI contract.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct CallSync {
    /// Registers stored to the context before the call (when written).
    pub store: Regs,
    /// Registers reloaded from the context after it (when used).
    pub reload: Regs,
}

/// Module-wide EABI facts (translator --abi-call-sync / --abi-exit-sync).
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ModuleAbi {
    pub functions: BTreeMap<u32, FunctionAbi>,
    /// Volatile registers some caller reads after a call. They stay fully
    /// synchronized: stored before and reloaded after every call, and
    /// stored at every return.
    pub always_synced: Regs,
    /// The contract of an unknown (indirect) callee, or `None` when an
    /// address-taken function is opted out.
    pub indirect: Option<CallSync>,
}

/// Registers a callee may read under the EABI: the arguments, the stack and
/// small-data bases, every non-volatile register (it saves them) and LR.
#[must_use]
pub fn abi_call_store() -> Regs {
    Regs {
        gpr: abi::ARGUMENT_GPRS | abi::NONVOLATILE_GPRS,
        fpr: abi::ARGUMENT_FPRS | abi::NONVOLATILE_FPRS,
        lr: true,
        ..Regs::default()
    }
}

/// Registers that carry a result back.
#[must_use]
pub fn abi_results() -> Regs {
    Regs {
        gpr: abi::RESULT_GPRS,
        fpr: abi::RESULT_FPRS,
        ..Regs::default()
    }
}

/// Registers a caller may observe after a return: the results, every
/// non-volatile register and LR.
#[must_use]
pub fn abi_exit_store() -> Regs {
    Regs {
        gpr: abi::RESULT_GPRS | abi::NONVOLATILE_GPRS,
        fpr: abi::RESULT_FPRS | abi::NONVOLATILE_FPRS,
        lr: true,
        ..Regs::default()
    }
}

/// The registers translated code may hold in host locals: GPRs, FPRs (both
/// lanes), LR and CTR. CR, XER and FPSCR always live in the context.
fn resident() -> Regs {
    Regs {
        gpr: u32::MAX,
        fpr: u32::MAX,
        lr: true,
        ctr: true,
        ..Regs::default()
    }
}

fn nonvolatile() -> Regs {
    Regs {
        gpr: abi::NONVOLATILE_GPRS,
        fpr: abi::NONVOLATILE_FPRS,
        cr: abi::NONVOLATILE_CR,
        ..Regs::default()
    }
}

impl ModuleAbi {
    /// Analyzes every function. `address_taken` lists the functions whose
    /// address appears in data or is materialized in code (possible indirect
    /// call targets); empty means any function may be one.
    #[must_use]
    pub fn analyze(
        functions: &[AbiFunctionInput<'_>],
        address_taken: &BTreeSet<u32>,
        inline_calls: &InlineCalls<'_>,
    ) -> Self {
        let mut module = Self::default();
        let facts_model = |_| CallModel::abi();
        let starts: BTreeSet<u32> = functions.iter().map(|function| function.start).collect();
        let decoded: BTreeMap<u32, Vec<Option<Effect>>> = functions
            .iter()
            .map(|function| {
                (
                    function.start,
                    decode(function.start, function.words, inline_calls),
                )
            })
            .collect();
        let jump_targets: BTreeMap<u32, &JumpTargets> = functions
            .iter()
            .map(|function| (function.start, function.jump_targets))
            .collect();
        for function in functions {
            let effects = &decoded[&function.start];
            let facts = abi_facts_with_jump_targets(
                function.start,
                effects.clone(),
                &facts_model,
                function.jump_targets,
            );
            let end = function.start.wrapping_add(function.words.len() as u32 * 4);
            let mut direct_calls = Vec::new();
            let mut tail_calls = Vec::new();
            let mut indirect_calls = false;
            let mut indirect_call_sites = Vec::new();
            let mut internal_link = false;
            let mut returns = facts.undecodable;
            for (index, effect) in effects.iter().enumerate() {
                let pc = function.start + index as u32 * 4;
                match effect.map(|effect| effect.flow) {
                    // A `bl` to the start is recursion, an ordinary call.
                    Some(Flow::Call(target)) if target > function.start && target < end => {
                        internal_link = true;
                    }
                    Some(Flow::Call(target)) => direct_calls.push((pc, target)),
                    Some(Flow::Branch(target) | Flow::CondBranch(target))
                        if (target < function.start || target >= end)
                            && starts.contains(&target) =>
                    {
                        // Returns when the target does (resolved below).
                        tail_calls.push((pc, target));
                    }
                    // A branch out of the function to no known start.
                    Some(Flow::Branch(target) | Flow::CondBranch(target))
                        if target < function.start || target >= end =>
                    {
                        returns = true;
                    }
                    Some(Flow::IndirectCall) => {
                        indirect_calls = true;
                        indirect_call_sites.push(pc);
                    }
                    Some(Flow::IndirectJump) => {
                        indirect_calls = true;
                        returns = true;
                    }
                    Some(Flow::Return | Flow::CondReturn) => returns = true,
                    _ => {}
                }
            }
            let opt_out = if function.native {
                Some(AbiOptOut::Native)
            } else if facts.undecodable {
                Some(AbiOptOut::Undecodable)
            } else if internal_link {
                Some(AbiOptOut::InternalLink)
            } else {
                None
            };
            // A host substitution implements a C function: it reads its
            // arguments and writes its results only, so a tail call to it
            // adds nothing. Unmodelled guest code may do anything.
            let (entry_extra, clobbers) = match opt_out {
                None => (
                    facts.scratch_entry_reads.intersect(resident()),
                    facts.nonvolatile_clobbers.intersect(resident()),
                ),
                Some(AbiOptOut::Native) => (Regs::default(), Regs::default()),
                Some(_) => (Regs::ALL, Regs::ALL),
            };
            module.functions.insert(
                function.start,
                FunctionAbi {
                    opt_out,
                    entry_extra,
                    clobbers,
                    reads_after_calls: BTreeMap::new(),
                    direct_calls,
                    tail_calls,
                    indirect_calls,
                    indirect_call_sites,
                    saved: facts
                        .writes
                        .intersect(facts.entry_live)
                        .intersect(nonvolatile()),
                    // Writing r1 without reading it first switches to another
                    // stack: control never comes back to the caller's frame
                    // (OSLoadContext, TRKLoadContext, __init_registers).
                    returns: returns && facts.nonvolatile_clobbers.gpr & (1 << 1) == 0,
                    writes: if opt_out.is_some() || facts.undecodable {
                        Regs::ALL
                    } else {
                        facts.writes
                    },
                },
            );
        }
        // A tail call returns when its target does.
        loop {
            let returning: BTreeSet<u32> = module
                .functions
                .iter()
                .filter(|(_, function)| function.returns)
                .map(|(start, _)| *start)
                .collect();
            let mut changed = false;
            for function in module.functions.values_mut() {
                // The clobbers are still the function's own here.
                let switches_stack = function.clobbers.gpr & (1 << 1) != 0;
                if function.returns || switches_stack {
                    continue;
                }
                if function
                    .tail_calls
                    .iter()
                    .any(|(_, target)| returning.contains(target))
                {
                    function.returns = true;
                    changed = true;
                }
            }
            if !changed {
                break;
            }
        }
        // Volatile registers read after a call before being rewritten, where
        // a later call's argument registers do not count as reads. Compiled
        // code sets every argument after the previous call; and an argument
        // register the caller set before an earlier call reaches the later
        // callee unchanged either way, because a register the earlier callee
        // did not touch holds the same value in the caller's local copy. What
        // remains is code that reads a volatile register itself after a call:
        // it relies on a callee's undeclared extra result or on a callee
        // preserving a scratch register, and those registers stay
        // synchronized at every call.
        let site_model = CallModel {
            uses: Regs {
                gpr: (1 << 1) | (1 << 2) | (1 << 13),
                ..Regs::default()
            },
            clobbers: abi::call_clobbers(),
        };
        for (start, function) in module.functions.iter_mut() {
            if function.opt_out.is_some() {
                continue;
            }
            function.reads_after_calls = abi_facts_with_jump_targets(
                *start,
                decoded[start].clone(),
                &|_| site_model,
                jump_targets[start],
            )
            .volatile_reads_after_calls;
        }
        // A register read after a call holds what the callee's call tree left
        // in it. When nothing in that tree may write the register, the
        // caller's own copy is still exact and needs no synchronization (the
        // caller relies on the callee preserving it, as code around runtime
        // helpers such as `__va_arg` does); only registers the tree may write
        // keep the fact.
        let indirect_target =
            |start: &u32| address_taken.is_empty() || address_taken.contains(start);
        let mut tree_writes: BTreeMap<u32, Regs> = module
            .functions
            .iter()
            .map(|(start, function)| (*start, function.writes))
            .collect();
        let indirect_writes = loop {
            let indirect = tree_writes
                .iter()
                .filter(|(start, _)| indirect_target(start))
                .fold(Regs::default(), |all, (_, writes)| all.union(*writes));
            let mut changed = false;
            for (start, function) in &module.functions {
                let mut writes = tree_writes[start];
                for (_, target) in function.direct_calls.iter().chain(&function.tail_calls) {
                    writes = writes.union(tree_writes.get(target).copied().unwrap_or(Regs::ALL));
                }
                if function.indirect_calls {
                    writes = writes.union(indirect);
                }
                if writes != tree_writes[start] {
                    tree_writes.insert(*start, writes);
                    changed = true;
                }
            }
            if !changed {
                break indirect;
            }
        };
        for function in module.functions.values_mut() {
            let targets: BTreeMap<u32, u32> = function.direct_calls.iter().copied().collect();
            function.reads_after_calls = function
                .reads_after_calls
                .iter()
                .filter_map(|(pc, registers)| {
                    let written = match targets.get(pc) {
                        Some(target) => tree_writes.get(target).copied().unwrap_or(Regs::ALL),
                        None => indirect_writes,
                    };
                    let kept = registers.intersect(written);
                    (!kept.is_empty()).then_some((*pc, kept))
                })
                .collect();
        }
        // A function that still reads a register its callee's tree may write
        // keeps full synchronization at its own calls and returns, and so does
        // each direct callee it reads from (whose returns then store every
        // register it wrote). Syncing those registers at every call of the
        // module instead would bring back the scratch-register traffic (r0 in
        // every prologue) the contract removes.
        let mut extra_results = BTreeSet::new();
        for function in module.functions.values_mut() {
            if function.opt_out.is_some() || function.reads_after_calls.is_empty() {
                continue;
            }
            function.opt_out = Some(AbiOptOut::ReadsAfterCall);
            let targets: BTreeMap<u32, u32> = function.direct_calls.iter().copied().collect();
            extra_results.extend(
                function
                    .reads_after_calls
                    .keys()
                    .filter_map(|pc| targets.get(pc).copied()),
            );
        }
        for target in extra_results {
            if let Some(function) = module.functions.get_mut(&target) {
                if function.opt_out.is_none() {
                    function.opt_out = Some(AbiOptOut::ExtraResult);
                }
            }
        }
        // Volatile registers read after calls by a function that keeps the
        // contract: none once the rule above applied; kept as the general
        // mechanism for facts a later analysis stage leaves in place.
        for function in module.functions.values() {
            if function.opt_out.is_some() {
                continue;
            }
            for registers in function.reads_after_calls.values() {
                module.always_synced = module.always_synced.union(*registers);
            }
        }
        // Transitive facts. A call returns with the non-volatile registers its
        // callees clobbered and it did not save itself (an indirect call or a
        // computed jump may reach any function); a tail call also needs its
        // target's entry registers.
        let unmodelled =
            |function: &FunctionAbi| function.opt_out.is_some_and(|reason| !reason.modelled());
        loop {
            // A callee that never returns hands back no register state.
            let returned = |function: &FunctionAbi| {
                if function.returns {
                    function.clobbers
                } else {
                    Regs::default()
                }
            };
            let any_clobbers = module
                .functions
                .iter()
                .filter(|(start, _)| indirect_target(start))
                .fold(Regs::default(), |all, (_, function)| {
                    all.union(returned(function))
                });
            let snapshot: BTreeMap<u32, (Regs, Regs)> = module
                .functions
                .iter()
                .map(|(start, function)| (*start, (function.entry_extra, returned(function))))
                .collect();
            let mut changed = false;
            for function in module.functions.values_mut() {
                if unmodelled(function) {
                    continue;
                }
                let mut inherited = Regs::default();
                let mut entry_extra = function.entry_extra;
                // Targets outside the module are trusted.
                for (_, target) in &function.direct_calls {
                    if let Some((_, callee)) = snapshot.get(target) {
                        inherited = inherited.union(*callee);
                    }
                }
                for (_, target) in &function.tail_calls {
                    if let Some((callee_entry, callee)) = snapshot.get(target) {
                        inherited = inherited.union(*callee);
                        entry_extra = entry_extra.union(*callee_entry);
                    }
                }
                if function.indirect_calls {
                    inherited = inherited.union(any_clobbers);
                }
                let clobbers = function.clobbers.union(
                    inherited
                        .intersect(nonvolatile())
                        .intersect(resident())
                        .minus(function.saved),
                );
                if clobbers != function.clobbers || entry_extra != function.entry_extra {
                    function.clobbers = clobbers;
                    function.entry_extra = entry_extra;
                    changed = true;
                }
            }
            if !changed {
                break;
            }
        }
        let indirect_safe = !address_taken.iter().any(|address| {
            module.functions.get(address).is_some_and(|function| {
                matches!(
                    function.opt_out,
                    Some(AbiOptOut::Undecodable | AbiOptOut::InternalLink)
                )
            })
        });
        if indirect_safe {
            let mut entry_extra = Regs::default();
            let mut clobbers = Regs::default();
            for (start, function) in &module.functions {
                // As for `indirect_entry` (ModuleAbi::analyze): code that
                // never returns is not an indirect callee.
                if unmodelled(function) || !indirect_target(start) || !function.returns {
                    continue;
                }
                entry_extra = entry_extra.union(function.entry_extra);
                clobbers = clobbers.union(function.clobbers);
            }
            module.indirect = Some(CallSync {
                store: abi_call_store()
                    .union(entry_extra)
                    .union(module.always_synced),
                reload: abi_results().union(clobbers).union(module.always_synced),
            });
        }
        module
    }

    /// The synchronization of the call at `call_pc` in `caller` to `target`
    /// (`None`: an indirect call), or `None` when the call must keep full
    /// synchronization (an opted-out caller or callee, or an unknown target).
    #[must_use]
    pub fn call_sync(&self, caller: u32, call_pc: u32, target: Option<u32>) -> Option<CallSync> {
        let caller = self.functions.get(&caller)?;
        if caller.opt_out.is_some() {
            return None;
        }
        let base = match target {
            Some(target) => {
                let callee = self.functions.get(&target)?;
                if callee.opt_out.is_some() {
                    return None;
                }
                CallSync {
                    store: abi_call_store().union(callee.entry_extra),
                    reload: abi_results().union(callee.clobbers),
                }
            }
            None => self.indirect?,
        };
        let site = caller
            .reads_after_calls
            .get(&call_pc)
            .copied()
            .unwrap_or_default()
            .union(self.always_synced);
        Some(CallSync {
            store: base.store.union(site),
            reload: base.reload.union(site),
        })
    }

    /// The registers a return of `function` stores, or `None` for full
    /// stores (an opted-out function).
    #[must_use]
    pub fn exit_stores(&self, function: u32) -> Option<Regs> {
        let function = self.functions.get(&function)?;
        if function.opt_out.is_some() {
            return None;
        }
        Some(abi_exit_store().union(self.always_synced))
    }

    /// Every call site of `function` that may use the EABI contract, by
    /// call pc.
    #[must_use]
    pub fn call_sites(&self, function: u32) -> BTreeMap<u32, CallSync> {
        let Some(facts) = self.functions.get(&function) else {
            return BTreeMap::new();
        };
        let direct = facts
            .direct_calls
            .iter()
            .filter_map(|(pc, target)| Some((*pc, self.call_sync(function, *pc, Some(*target))?)));
        let indirect = facts
            .indirect_call_sites
            .iter()
            .filter_map(|pc| Some((*pc, self.call_sync(function, *pc, None)?)));
        direct.chain(indirect).collect()
    }

    /// The opt-out list and the non-ABI facts as JSON, for the module
    /// manifest (`abi_contract.json`).
    #[must_use]
    pub fn manifest_json(&self) -> String {
        let mut out = String::from("{\n  \"schema\": \"nebula.abi-contract.v1\",\n");
        let opted_out: Vec<_> = self
            .functions
            .iter()
            .filter_map(|(start, function)| Some((*start, function.opt_out?)))
            .collect();
        let non_abi: Vec<_> = self
            .functions
            .iter()
            .filter(|(_, function)| {
                function.opt_out.is_none()
                    && (!function.entry_extra.is_empty()
                        || !function.clobbers.is_empty()
                        || !function.reads_after_calls.is_empty())
            })
            .collect();
        let sites: usize = self
            .functions
            .keys()
            .map(|start| self.call_sites(*start).len())
            .sum();
        let _ = write!(
            out,
            "  \"functions\": {},\n  \"optedOut\": {},\n  \"nonAbiFacts\": {},\n  \"abiCallSites\": {sites},\n  \"alwaysSynced\": \"{}\",\n",
            self.functions.len(),
            opted_out.len(),
            non_abi.len(),
            self.always_synced.describe()
        );
        match self.indirect {
            Some(sync) => {
                let _ = writeln!(
                    out,
                    "  \"indirectCall\": {{\"storeExtra\": \"{}\", \"reloadExtra\": \"{}\"}},",
                    sync.store.minus(abi_call_store()).describe(),
                    sync.reload.minus(abi_results()).describe()
                );
            }
            None => out.push_str("  \"indirectCall\": null,\n"),
        }
        out.push_str("  \"optOut\": [");
        for (index, (start, reason)) in opted_out.iter().enumerate() {
            let _ = write!(
                out,
                "{}\n    {{\"address\": \"0x{start:08X}\", \"reason\": \"{}\"}}",
                if index == 0 { "" } else { "," },
                reason.name()
            );
        }
        out.push_str(if opted_out.is_empty() {
            "],\n"
        } else {
            "\n  ],\n"
        });
        out.push_str("  \"nonAbi\": [");
        for (index, (start, function)) in non_abi.iter().enumerate() {
            let reads_after: Vec<String> = function
                .reads_after_calls
                .iter()
                .map(|(pc, registers)| format!("0x{pc:08X}: {}", registers.describe()))
                .collect();
            let _ = write!(
                out,
                "{}\n    {{\"address\": \"0x{start:08X}\", \"entryExtra\": \"{}\", \"clobbers\": \"{}\", \"readsAfterCalls\": \"{}\"}}",
                if index == 0 { "" } else { "," },
                function.entry_extra.describe(),
                function.clobbers.describe(),
                reads_after.join("; ")
            );
        }
        out.push_str(if non_abi.is_empty() {
            "]\n}\n"
        } else {
            "\n  ]\n}\n"
        });
        out
    }
}

impl Regs {
    /// `r0 r12 f1 cr0 lr ctr ca so fpscr`, or `-` for the empty set.
    #[must_use]
    pub fn describe(self) -> String {
        let mut parts = Vec::new();
        for index in 0..32 {
            if self.gpr & (1 << index) != 0 {
                parts.push(format!("r{index}"));
            }
        }
        for index in 0..32 {
            if self.fpr & (1 << index) != 0 {
                parts.push(format!("f{index}"));
            }
        }
        for field in 0..8 {
            if self.cr & (1 << field) != 0 {
                parts.push(format!("cr{field}"));
            }
        }
        for (flag, name) in [
            (self.lr, "lr"),
            (self.ctr, "ctr"),
            (self.ca, "ca"),
            (self.so, "so"),
            (self.fpscr, "fpscr"),
        ] {
            if flag {
                parts.push(name.to_owned());
            }
        }
        if parts.is_empty() {
            "-".to_owned()
        } else {
            parts.join(" ")
        }
    }
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
        let facts = abi_facts(start, &words);
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
        let facts = abi_facts(start, &after_call);
        assert_eq!(
            facts.volatile_reads_after_calls.get(&start).map(|r| r.gpr),
            Some(1 << 5)
        );
        // Reads r12 at entry (scratch).
        let scratch = [x(3, 12, 12, 266, false), BLR];
        assert_eq!(abi_facts(start, &scratch).scratch_entry_reads.gpr, 1 << 12);
        // Writes r31 without saving it.
        let clobber = [addi(31, 0, 1), BLR];
        assert_eq!(abi_facts(start, &clobber).nonvolatile_clobbers.gpr, 1 << 31);
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
        let liveness = analyze_liveness(start, &words, abi::return_live(), &|_| CallModel::abi());
        let dead = dead_flag_writes(&liveness);
        assert_eq!(dead.get(&start).map(|r| r.cr), Some(1));
        assert_eq!(dead.get(&(start + 4)).map(|r| r.ca), Some(true));
        assert!(
            !dead.contains_key(&(start + 8)),
            "the compare feeds the branch"
        );
    }

    #[test]
    fn unknown_forms_and_local_subroutines_are_conservative() {
        let start = 0x8000_4000;
        let unknown = [0x0000_0000, BLR];
        let facts = abi_facts(start, &unknown);
        assert!(facts.undecodable && !facts.abi_conformant());
        // bl $+4 materialises the PC and falls through; a bl to any other
        // address in the body is a local subroutine: everything is live.
        assert_eq!(effect(0x100, bl(0x100, 0x104)).unwrap().flow, Flow::Next);
        let local = [bl(start, start + 8), BLR, addi(3, 0, 1), BLR];
        let liveness = analyze_liveness(start, &local, abi::return_live(), &|_| CallModel::abi());
        assert_eq!(liveness.live_before[0], Regs::ALL);
    }

    #[test]
    fn tail_branch_keeps_the_target_arguments_live() {
        let start = 0x8000_4000;
        // addi r3,r3,1; b 0x80005000
        let branch = (18 << 26) | ((0x8000_5000u32 - (start + 4)) & 0x03FF_FFFC);
        let words = [addi(3, 3, 1), branch];
        let liveness = analyze_liveness(start, &words, abi::return_live(), &|_| CallModel::abi());
        assert!(
            liveness.live_after[0].gpr & (1 << 4) != 0,
            "r4 is an argument"
        );
        assert!(
            liveness.live_after[0].lr,
            "the tail callee returns through LR"
        );
    }

    const CALLER: u32 = 0x8000_4000;
    const LEAF: u32 = 0x8000_5000;
    const OTHER: u32 = 0x8000_6000;

    /// stwu r1,-16(r1); mflr r0; stw r0,20(r1); stw r31,12(r1); mr r31,r3;
    /// bl LEAF; add r3,r31,r3; bl OTHER; lwz r0,20(r1); lwz r31,12(r1);
    /// mtlr r0; addi r1,r1,16; blr
    fn caller_words() -> Vec<u32> {
        vec![
            (37 << 26) | (1 << 21) | (1 << 16) | 0xFFF0,
            0x7C08_02A6,
            stw(0, 20, 1),
            stw(31, 12, 1),
            x(3, 31, 3, 444, false),
            bl(CALLER + 20, LEAF),
            x(3, 31, 3, 266, false),
            bl(CALLER + 28, OTHER),
            lwz(0, 20, 1),
            lwz(31, 12, 1),
            0x7C08_03A6,
            addi(1, 1, 16),
            BLR,
        ]
    }

    fn module(leaf: &[u32], other: &[u32], native_other: bool) -> ModuleAbi {
        let caller = caller_words();
        ModuleAbi::analyze(
            &[
                AbiFunctionInput {
                    start: CALLER,
                    words: &caller,
                    native: false,
                    jump_targets: &NO_JUMP_TARGETS,
                },
                AbiFunctionInput {
                    start: LEAF,
                    words: leaf,
                    native: false,
                    jump_targets: &NO_JUMP_TARGETS,
                },
                AbiFunctionInput {
                    start: OTHER,
                    words: other,
                    native: native_other,
                    jump_targets: &NO_JUMP_TARGETS,
                },
            ],
            &BTreeSet::new(),
            &|_| None,
        )
    }

    #[test]
    fn conformant_calls_store_the_abi_set_and_reload_the_results() {
        let leaf = [addi(3, 3, 1), BLR];
        let abi = module(&leaf, &leaf, false);
        assert!(abi.always_synced.is_empty(), "{abi:?}");
        let sync = abi.call_sync(CALLER, CALLER + 20, Some(LEAF)).unwrap();
        assert_eq!(sync.store, abi_call_store());
        assert_eq!(sync.reload, abi_results());
        assert_eq!(abi.exit_stores(CALLER), Some(abi_exit_store()));
        assert_eq!(abi.call_sites(CALLER).len(), 2);
        // `bl LEAF; ...; bl OTHER`: OTHER reads only r3, so no argument
        // register counts as read after the first call.
        assert!(abi.functions[&CALLER].reads_after_calls.is_empty());
    }

    #[test]
    fn non_abi_callees_add_their_registers_to_the_call() {
        // Reads r12 at entry; writes r30 without saving it.
        let leaf = [x(3, 12, 3, 266, false), addi(30, 0, 7), BLR];
        let other = [addi(3, 3, 1), BLR];
        let abi = module(&leaf, &other, false);
        let sync = abi.call_sync(CALLER, CALLER + 20, Some(LEAF)).unwrap();
        assert_eq!(sync.store.gpr & (1 << 12), 1 << 12);
        assert_eq!(sync.reload.gpr & (1 << 30), 1 << 30);
        // The caller saves r31 but not r30, so it passes r30 on.
        assert_eq!(abi.functions[&CALLER].clobbers.gpr & (1 << 30), 1 << 30);
        assert_eq!(abi.functions[&CALLER].clobbers.gpr & (1 << 31), 0);
    }

    /// stwu r1,-16(r1); mflr r0; stw r0,20(r1); bl LEAF; add r3,r5,r3;
    /// lwz r0,20(r1); mtlr r0; addi r1,r1,16; blr: reads r5 after the call.
    fn reads_r5_after_leaf() -> [u32; 9] {
        [
            (37 << 26) | (1 << 21) | (1 << 16) | 0xFFF0,
            0x7C08_02A6,
            stw(0, 20, 1),
            bl(OTHER + 12, LEAF),
            x(3, 5, 3, 266, false),
            lwz(0, 20, 1),
            0x7C08_03A6,
            addi(1, 1, 16),
            BLR,
        ]
    }

    #[test]
    fn a_volatile_read_after_a_call_needs_nothing_when_the_callee_keeps_it() {
        // LEAF leaves r5 alone: OTHER's own copy of r5 is still exact.
        let leaf = [addi(3, 3, 1), BLR];
        let abi = module(&leaf, &reads_r5_after_leaf(), false);
        assert!(abi.always_synced.is_empty(), "{abi:?}");
        assert_eq!(abi.functions[&OTHER].opt_out, None);
        assert!(abi.functions[&OTHER].reads_after_calls.is_empty());
        assert_eq!(abi.exit_stores(LEAF), Some(abi_exit_store()));
    }

    #[test]
    fn a_volatile_read_after_a_call_keeps_both_sides_fully_synchronized() {
        // LEAF leaves an extra result in r5: OTHER and LEAF keep full
        // synchronization, every other call keeps the contract.
        let leaf = [addi(5, 0, 7), addi(3, 3, 1), BLR];
        let abi = module(&leaf, &reads_r5_after_leaf(), false);
        assert!(abi.always_synced.is_empty(), "{abi:?}");
        assert_eq!(
            abi.functions[&OTHER].opt_out,
            Some(AbiOptOut::ReadsAfterCall)
        );
        assert_eq!(abi.functions[&LEAF].opt_out, Some(AbiOptOut::ExtraResult));
        assert_eq!(abi.call_sync(OTHER, OTHER + 12, Some(LEAF)), None);
        assert_eq!(abi.call_sync(CALLER, CALLER + 20, Some(LEAF)), None);
        assert_eq!(abi.call_sync(CALLER, CALLER + 28, Some(OTHER)), None);
        assert_eq!(abi.exit_stores(LEAF), None);
        assert_eq!(abi.exit_stores(CALLER), Some(abi_exit_store()));
        // Both stay indirect callees with known facts.
        assert!(abi.indirect.is_some());
        let json = abi.manifest_json();
        assert!(
            json.contains("\"address\": \"0x80006000\", \"reason\": \"reads-after-call\""),
            "{json}"
        );
        assert!(
            json.contains("\"address\": \"0x80005000\", \"reason\": \"extra-result\""),
            "{json}"
        );
    }

    #[test]
    fn a_later_calls_arguments_are_not_reads_after_the_earlier_call() {
        // CALLER: bl LEAF; add r3,r31,r3; bl OTHER. OTHER is a host
        // substitution, so it may read any argument register, yet r4..r10
        // left over from LEAF are not facts about CALLER.
        let leaf = [addi(3, 3, 1), BLR];
        let other = [x(3, 3, 9, 266, false), BLR];
        let abi = module(&leaf, &other, true);
        assert!(abi.functions[&CALLER].reads_after_calls.is_empty());
        assert!(abi.always_synced.is_empty());
    }

    #[test]
    fn recursion_is_an_ordinary_call() {
        // stwu r1,-16(r1); mflr r0; stw r0,20(r1); bl LEAF (its own start);
        // lwz r0,20(r1); mtlr r0; addi r1,r1,16; blr
        let leaf = [
            (37 << 26) | (1 << 21) | (1 << 16) | 0xFFF0,
            0x7C08_02A6,
            stw(0, 20, 1),
            bl(LEAF + 12, LEAF),
            lwz(0, 20, 1),
            0x7C08_03A6,
            addi(1, 1, 16),
            BLR,
        ];
        let other = [addi(3, 3, 1), BLR];
        let abi = module(&leaf, &other, false);
        assert_eq!(abi.functions[&LEAF].opt_out, None);
        assert_eq!(abi.functions[&LEAF].direct_calls, vec![(LEAF + 12, LEAF)]);
        assert!(abi.call_sync(LEAF, LEAF + 12, Some(LEAF)).is_some());
    }

    #[test]
    fn a_call_the_function_ends_with_does_not_return() {
        let start = 0x8000_4000;
        // add r3,r3,r3; bl 0x80005000 (noreturn): nothing after it is read.
        let words = [x(3, 3, 3, 266, false), bl(start + 4, 0x8000_5000)];
        let facts = abi_facts(start, &words);
        assert!(facts.volatile_reads_after_calls.is_empty(), "{facts:?}");
        assert!(facts.scratch_entry_reads.is_empty(), "{facts:?}");
    }

    #[test]
    fn a_callee_that_never_returns_hands_back_no_clobbers() {
        // lwz r31,4(r3); rfi: clobbers r31, never returns to its caller.
        let rfi_leaf = [lwz(31, 4, 3), 0x4C00_0064];
        // lwz r1,0(r3); lwz r31,4(r3); blr: switches stacks.
        let stack_switch = [lwz(1, 0, 3), lwz(31, 4, 3), BLR];
        let other = [addi(3, 3, 1), BLR];
        for leaf in [&rfi_leaf[..], &stack_switch[..]] {
            let abi = module(leaf, &other, false);
            let callee = &abi.functions[&LEAF];
            assert!(!callee.returns, "{callee:?}");
            assert_eq!(callee.clobbers.gpr & (1 << 31), 1 << 31);
            // The call still reloads what the callee wrote; the caller's own
            // summary does not inherit it.
            let sync = abi.call_sync(CALLER, CALLER + 20, Some(LEAF)).unwrap();
            assert_eq!(sync.reload.gpr & (1 << 31), 1 << 31);
            assert!(abi.functions[&CALLER].clobbers.is_empty(), "{abi:?}");
        }
    }

    #[test]
    fn scalar_moves_nop_and_machine_state_writes_read_only_their_operands() {
        // fmr f3,f1 keeps f3's ps1 lane but does not count as reading f3.
        let fmr = effect(0, (63 << 26) | (3 << 21) | (1 << 11) | (72 << 1)).unwrap();
        assert_eq!(fmr.reads.fpr, 1 << 1);
        assert_eq!(fmr.writes.fpr, 1 << 3);
        // nop (ori r0,r0,0) reads and writes nothing.
        let nop = effect(0, 0x6000_0000).unwrap();
        assert!(nop.reads.is_empty() && nop.writes.is_empty());
        // ori r3,r3,1 still does.
        assert_eq!(effect(0, 0x6063_0001).unwrap().reads.gpr, 1 << 3);
        // mtmsr r3 and mtspr GQR0 read their operand; sc stays a boundary.
        let mtmsr = effect(0, (31 << 26) | (3 << 21) | (146 << 1)).unwrap();
        assert!(!mtmsr.boundary && mtmsr.reads.gpr == 1 << 3);
        let mtgqr = effect(0, 0x7C70_E3A6).unwrap();
        assert!(!mtgqr.boundary && mtgqr.reads.gpr == 1 << 3);
        assert!(effect(0, 0x4400_0002).unwrap().boundary);
    }

    #[test]
    fn a_switch_bctr_reads_only_its_own_case_labels() {
        let start = 0x8000_4000;
        // bl X; mtctr r3; bctr (switch B: case S+16); blr; S+16: li r3,0;
        // blr; S+24: add r3,r6,r6 (a case of another switch); blr
        let words = [
            bl(start, 0x8000_9000),
            0x7C69_03A6,
            0x4E80_0420,
            BLR,
            addi(3, 0, 0),
            BLR,
            x(3, 6, 6, 266, false),
            BLR,
        ];
        let effects = || decode(start, &words, &|_| None);
        let model = |_| CallModel::abi();
        let mut targets = JumpTargets {
            by_site: BTreeMap::from([(start + 8, vec![start + 16])]),
            any: vec![start + 16, start + 24],
            loaded: BTreeSet::new(),
        };
        let facts = abi_facts_with_jump_targets(start, effects(), &model, &targets);
        assert!(facts.volatile_reads_after_calls.is_empty(), "{facts:?}");
        // A jump through a loaded pointer: a tail call or any interior
        // target, so r6 is read after the call.
        targets.by_site.clear();
        targets.loaded.insert(start + 8);
        let facts = abi_facts_with_jump_targets(start, effects(), &model, &targets);
        assert_eq!(
            facts
                .volatile_reads_after_calls
                .get(&start)
                .map(|regs| regs.gpr & (1 << 6)),
            Some(1 << 6)
        );
        // Any other computed jump: everything is live.
        targets.loaded.clear();
        let liveness = analyze_effects_with_jump_targets(
            start,
            effects(),
            abi::return_live(),
            &model,
            &targets,
        );
        assert_eq!(liveness.live_after[2], Regs::ALL);
    }

    #[test]
    fn opted_out_functions_keep_full_synchronization() {
        let leaf = [bl(LEAF, LEAF + 8), BLR, addi(3, 0, 1), BLR];
        let other = [addi(3, 3, 1), BLR];
        let abi = module(&leaf, &other, true);
        assert_eq!(abi.functions[&LEAF].opt_out, Some(AbiOptOut::InternalLink));
        assert_eq!(abi.functions[&OTHER].opt_out, Some(AbiOptOut::Native));
        assert_eq!(abi.call_sync(CALLER, CALLER + 20, Some(LEAF)), None);
        assert_eq!(abi.call_sync(CALLER, CALLER + 28, Some(OTHER)), None);
        assert_eq!(abi.exit_stores(LEAF), None);
        // An address-taken function the analysis cannot vouch for makes every
        // indirect call fully synchronized.
        let caller = caller_words();
        let taken = ModuleAbi::analyze(
            &[
                AbiFunctionInput {
                    start: CALLER,
                    words: &caller,
                    native: false,
                    jump_targets: &NO_JUMP_TARGETS,
                },
                AbiFunctionInput {
                    start: LEAF,
                    words: &leaf,
                    native: false,
                    jump_targets: &NO_JUMP_TARGETS,
                },
            ],
            &BTreeSet::from([LEAF]),
            &|_| None,
        );
        assert_eq!(taken.indirect, None);
        let json = taken.manifest_json();
        assert!(
            json.contains("\"address\": \"0x80005000\", \"reason\": \"internal-link\""),
            "{json}"
        );
        assert!(json.contains("\"indirectCall\": null"), "{json}");
    }

    #[test]
    fn register_save_helpers_are_modelled_in_place() {
        const SAVE_25: u32 = 0x8051_7518;
        const REST_25: u32 = 0x8051_7564;
        let inline = |target: u32| match target {
            SAVE_25 => Some((
                Regs::gpr(11).union(Regs {
                    gpr: u32::MAX << 25,
                    ..Regs::default()
                }),
                Regs::default(),
            )),
            REST_25 => Some((
                Regs::gpr(11),
                Regs {
                    gpr: u32::MAX << 25,
                    ..Regs::default()
                },
            )),
            _ => None,
        };
        // stwu r1,-64(r1); addi r11,r1,64; bl _savegpr_25; mr r25,r3;
        // bl LEAF; mr r3,r25; addi r11,r1,64; bl _restgpr_25; addi r1,r1,64; blr
        let words = [
            (37 << 26) | (1 << 21) | (1 << 16) | 0xFFC0,
            addi(11, 1, 64),
            bl(CALLER + 8, SAVE_25),
            x(3, 25, 3, 444, false),
            bl(CALLER + 16, LEAF),
            x(25, 3, 25, 444, false),
            addi(11, 1, 64),
            bl(CALLER + 28, REST_25),
            addi(1, 1, 64),
            BLR,
        ];
        let leaf = [addi(3, 3, 1), BLR];
        let abi = ModuleAbi::analyze(
            &[
                AbiFunctionInput {
                    start: CALLER,
                    words: &words,
                    native: false,
                    jump_targets: &NO_JUMP_TARGETS,
                },
                AbiFunctionInput {
                    start: LEAF,
                    words: &leaf,
                    native: false,
                    jump_targets: &NO_JUMP_TARGETS,
                },
            ],
            &BTreeSet::new(),
            &inline,
        );
        let caller = &abi.functions[&CALLER];
        // The helpers are not calls; r25..r31 are saved and restored.
        assert_eq!(caller.direct_calls, vec![(CALLER + 16, LEAF)]);
        assert!(caller.clobbers.is_empty(), "{caller:?}");
        assert_eq!(caller.saved.gpr & (1 << 25), 1 << 25);
        assert!(caller.reads_after_calls.is_empty(), "{caller:?}");
        assert!(abi.always_synced.is_empty());
    }
}
