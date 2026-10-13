//! Guest register residency (F5).
//!
//! A lowered body names `context->gpr[N]`, `context->lr`, `context->ctr`,
//! `context->fpr_bits[N]` and `context->ps1_bits[N]` for every operand, so the
//! host compiler treats each as a memory access into `PpcContext`. Because
//! `context` is passed to other translated functions, every opaque call
//! (including the cold paths of loads, stores and checkpoints) forces the
//! compiler to reload those fields afterwards, and the guest register file
//! never lives in host registers.
//!
//! This pass renames the directly indexed GPRs plus LR and CTR to
//! function-local scalars, and the FPR/PS1 file to two function-local arrays
//! (`fpr[32]`, `ps1[32]`, constant-indexed, never escaping), and makes the
//! local copies authoritative between synchronization points. A
//! synchronization point is every statement that hands `context` to code
//! which may read or write the register file:
//!
//! * a translated call, host call, native substitution, trace hook,
//!   `call_return_checkpoint` or any other context-receiving helper that is not
//!   known to leave the register file alone: the written integer locals and
//!   every used FPR/PS1 entry are stored before it and every local is reloaded
//!   after it;
//! * `branch_checkpoint_taken`: split into the inline counter test and a cold
//!   block that stores, calls the complete policy and reloads;
//! * `ppc_commit_scalar_result`: its target register is synchronized around it;
//! * `return;`: store;
//! * an interior entry (dispatch `case`): jump to a stub that reloads first.
//!
//! The native float fast paths (`fast::scalar_*`, `fast::paired_*`,
//! `fast::psq_*`, `fast::*_fpr_single`, and the `_hw` conversion forms of the
//! loads and stores) are rewritten to their `_r` overloads,
//! which operate on the local arrays and synchronize through `context` only on
//! their exact fallback. Helpers that only touch CR, XER, FPSCR or HID2
//! (`cr_bit`, `compare_*`, `write_fpscr_fields`, ...) are left in place. Any
//! helper not on those lists is a full synchronization point, so an unknown
//! callee is always handled conservatively.
//!
//! The pass works on the emitted text. It returns `None` (keep the original
//! body) for shapes it does not handle: a runtime-indexed register access, a
//! synchronizing call used as an expression, two synchronizing calls on one
//! line, or a float helper with a non-literal register argument.
//!
//! Under the EABI contract (translator `--abi-call-sync`, `--abi-exit-sync`;
//! see `crate::ppc_dataflow::ModuleAbi`) the translator marks a call to a
//! translated function, or an indirect call, with `GALAXY_ABI_CALL(0x<pc>u);`
//! and a `blr` with `GALAXY_GUEST_RETURN;` (both no-ops in a body this pass
//! leaves alone). A marked call stores only the written locals the callee may
//! read (arguments, stack and small-data bases, non-volatile registers, LR,
//! plus the callee's recorded extras) and reloads only the results and the
//! registers the callee may clobber; a marked return stores only what a caller
//! may observe. A marker whose next synchronizing call is not a translated or
//! indirect guest call is dropped, so that call keeps the full sequence.

use crate::ppc_dataflow::{CallSync, Regs};
use std::collections::{BTreeMap, BTreeSet};
use std::fmt::Write as _;

const LR: usize = 32;
const CTR: usize = 33;
const FPR_BASE: usize = 34;
const PS1_BASE: usize = 66;
const SLOTS: usize = 98;

fn fpr_slot(index: usize) -> usize {
    FPR_BASE + index
}

fn ps1_slot(index: usize) -> usize {
    PS1_BASE + index
}

fn is_float_slot(slot: usize) -> bool {
    slot >= FPR_BASE
}

fn local_name(slot: usize) -> String {
    match slot {
        LR => "glr".to_owned(),
        CTR => "gctr".to_owned(),
        n if n < 32 => format!("gr{n}"),
        n if n < PS1_BASE => format!("fpr[{}]", n - FPR_BASE),
        n => format!("ps1[{}]", n - PS1_BASE),
    }
}

fn field_name(slot: usize) -> String {
    match slot {
        LR => "context->lr".to_owned(),
        CTR => "context->ctr".to_owned(),
        n if n < 32 => format!("context->gpr[{n}]"),
        n if n < PS1_BASE => format!("context->fpr_bits[{}]", n - FPR_BASE),
        n => format!("context->ps1_bits[{}]", n - PS1_BASE),
    }
}

fn is_ident(byte: u8) -> bool {
    byte.is_ascii_alphanumeric() || byte == b'_'
}

/// Helpers that receive `context` but neither read nor write any resident
/// register (GPR, LR, CTR, FPR, PS1).
fn callee_leaves_register_file_alone(callee: &str) -> bool {
    let name = callee.strip_prefix("galaxy::").unwrap_or(callee);
    matches!(
        name,
        "cr_bit"
            | "compare_signed"
            | "compare_unsigned"
            | "compare_f64"
            | "update_cr0"
            | "set_xer_ca"
            | "set_cr_bit"
            | "xer_ca"
            | "write_cr_fields"
            | "write_fpscr_fields"
            | "set_fpscr_bit"
            | "require_paired_single_mode"
            // mfspr/mftb for SPRs other than LR/CTR/XER (those are lowered to
            // direct field reads): time base, DEC, HID, GQR and friends.
            | "read_spr"
            // Register-resident float fast paths: they take the local arrays
            // and synchronize through `context` only on their exact fallback.
            | "fast::load_fpr_single_r"
            | "fast::store_fpr_single_r"
            | "fast::psq_load_r"
            | "fast::psq_store_r"
            | "fast::load_fpr_single_hw_r"
            | "fast::store_fpr_single_hw_r"
            | "fast::psq_load_hw_r"
            | "fast::psq_store_hw_r"
            | "fast::scalar_binary_r"
            | "fast::scalar_ternary_r"
            | "fast::scalar_frsp_r"
            | "fast::paired_binary_r"
            | "fast::paired_ternary_r"
            | "fast::paired_sum_r"
            | "fast::fctiwz_r"
            // --fast-float-forms: CR field only; HID2 only.
            | "fast::compare_f64_cr"
            | "fast::duplicates_single"
    )
}

/// Float helpers that read and write only FPR/PS1, FPSCR, GQR, HID2 and CR
/// through `context`, never a GPR, LR or CTR (their address operands are
/// passed by value). Without float residency the context holds the float
/// registers, so they are not synchronization points for the resident integer
/// locals.
fn fpr_only_helper(callee: &str) -> bool {
    let name = callee.strip_prefix("galaxy::").unwrap_or(callee);
    matches!(
        name,
        "fast::load_fpr_single"
            | "fast::store_fpr_single"
            | "fast::psq_load"
            | "fast::psq_store"
            | "fast::load_fpr_single_hw"
            | "fast::store_fpr_single_hw"
            | "fast::psq_load_hw"
            | "fast::psq_store_hw"
            | "fast::scalar_binary"
            | "fast::scalar_ternary"
            | "fast::scalar_frsp"
            | "fast::paired_binary"
            | "fast::paired_ternary"
            | "fast::paired_sum"
            | "fast::fctiwz"
            | "ppc_commit_scalar_result"
    )
}

/// Position (0-based, in the argument list) of the register the `_r` float
/// helper writes, if any.
fn float_helper_target_position(callee: &str) -> Option<usize> {
    let name = callee.strip_prefix("galaxy::").unwrap_or(callee);
    match name {
        "fast::scalar_binary_r"
        | "fast::paired_binary_r"
        | "fast::scalar_ternary_r"
        | "fast::paired_ternary_r"
        | "fast::paired_sum_r"
        | "fast::scalar_frsp_r"
        | "fast::fctiwz_r" => Some(3),
        "fast::load_fpr_single_r"
        | "fast::psq_load_r"
        | "fast::load_fpr_single_hw_r"
        | "fast::psq_load_hw_r" => Some(4),
        _ => None,
    }
}

/// Positions (0-based, in the argument list) of the register-index arguments
/// of the `_r` float helpers, by callee name.
fn float_helper_index_positions(callee: &str) -> Option<&'static [usize]> {
    let name = callee.strip_prefix("galaxy::").unwrap_or(callee);
    Some(match name {
        "fast::fctiwz_r" => &[3, 4],
        "fast::scalar_binary_r" | "fast::paired_binary_r" => &[3, 4, 5],
        "fast::scalar_ternary_r" | "fast::paired_ternary_r" | "fast::paired_sum_r" => &[3, 4, 5, 6],
        "fast::scalar_frsp_r" => &[3, 4],
        "fast::load_fpr_single_r"
        | "fast::store_fpr_single_r"
        | "fast::psq_load_r"
        | "fast::psq_store_r"
        | "fast::load_fpr_single_hw_r"
        | "fast::store_fpr_single_hw_r"
        | "fast::psq_load_hw_r"
        | "fast::psq_store_hw_r" => &[4],
        _ => return None,
    })
}

/// Resident registers a lean host body (`galaxy::lean_*`, see
/// `translate::lean_call_contract`) reads from or writes to `context`. Bit N is
/// GPR or FPR N; an FPR entry covers ps0 and ps1.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub(crate) struct LeanCallContract {
    pub gpr_reads: u32,
    pub gpr_writes: u32,
    pub fpr_reads: u32,
    pub fpr_writes: u32,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Sync {
    /// No resident register effect.
    None,
    /// Store the written locals first, reload every local afterwards.
    Full,
    /// A lean host body: store the written locals it may read or write,
    /// reload the used locals it writes.
    Lean(LeanCallContract),
    /// `branch_checkpoint_taken`: inline test, cold synchronized block.
    Checkpoint,
    /// `call_return_checkpoint`: inline test, cold synchronized block.
    CallReturn,
    /// `ppc_commit_scalar_result(context, T, ...)`: synchronize FPR/PS1 `T`.
    FprTarget,
}

fn classify(callee: &str, floats: bool) -> Sync {
    let name = callee.strip_prefix("galaxy::").unwrap_or(callee);
    if callee_leaves_register_file_alone(callee) || (!floats && fpr_only_helper(callee)) {
        Sync::None
    } else if name == "branch_checkpoint_taken" || name == "branch_checkpoint_taken_budgeted" {
        Sync::Checkpoint
    } else if name == "call_return_checkpoint" {
        Sync::CallReturn
    } else if let Some(contract) = crate::translate::lean_call_contract(name) {
        Sync::Lean(contract)
    } else if name == "ppc_commit_scalar_result" && floats {
        Sync::FprTarget
    } else {
        Sync::Full
    }
}

struct ContextCall {
    /// Start of the callee name (including any `galaxy::` and template args).
    callee_start: usize,
    /// Byte after the `(` that opens the argument list.
    args_start: usize,
    /// Byte index of the closing `)`.
    args_end: usize,
    /// Callee name without template arguments, for classification.
    callee: String,
    /// Callee as written, including any template arguments, for re-emission.
    callee_text: String,
    /// `true` when `);` directly follows the call (a statement).
    statement: bool,
}

/// Finds the call that receives the bare `context` token at `at`.
fn enclosing_call(line: &str, at: usize) -> Option<ContextCall> {
    let bytes = line.as_bytes();
    // Walk back to the unmatched `(`.
    let mut depth = 0i32;
    let mut open = None;
    let mut index = at;
    while index > 0 {
        index -= 1;
        match bytes[index] {
            b')' => depth += 1,
            b'(' => {
                if depth == 0 {
                    open = Some(index);
                    break;
                }
                depth -= 1;
            }
            _ => {}
        }
    }
    let open = open?;
    // Callee: optional template argument list, then identifier/`::` run.
    let mut name_end = open;
    let mut cursor = open;
    if cursor > 0 && bytes[cursor - 1] == b'>' {
        let mut angle = 0i32;
        while cursor > 0 {
            cursor -= 1;
            match bytes[cursor] {
                b'>' => angle += 1,
                b'<' => {
                    angle -= 1;
                    if angle == 0 {
                        break;
                    }
                }
                _ => {}
            }
        }
        name_end = cursor;
    }
    let mut start = name_end;
    while start > 0 && (is_ident(bytes[start - 1]) || bytes[start - 1] == b':') {
        start -= 1;
    }
    if start == name_end {
        return None;
    }
    let callee = line[start..name_end].to_owned();
    let callee_text = line[start..open].to_owned();
    // Matching close paren.
    let mut depth = 0i32;
    let mut close = None;
    for (offset, byte) in bytes[open..].iter().enumerate() {
        match byte {
            b'(' => depth += 1,
            b')' => {
                depth -= 1;
                if depth == 0 {
                    close = Some(open + offset);
                    break;
                }
            }
            _ => {}
        }
    }
    let close = close?;
    let statement = bytes.get(close + 1) == Some(&b';');
    Some(ContextCall {
        callee_start: start,
        args_start: open + 1,
        args_end: close,
        callee,
        callee_text,
        statement,
    })
}

/// Bare `context` tokens on a line: not part of a longer identifier and not a
/// field access (`context->...`).
fn bare_context_tokens(line: &str) -> Vec<usize> {
    const TOKEN: &str = "context";
    let bytes = line.as_bytes();
    let mut found = Vec::new();
    let mut search = 0usize;
    while let Some(offset) = line[search..].find(TOKEN) {
        let at = search + offset;
        let after = at + TOKEN.len();
        let before_ok = at == 0 || !is_ident(bytes[at - 1]);
        let after_ok = after >= bytes.len() || !is_ident(bytes[after]);
        let field = line[after..].starts_with("->");
        if before_ok && after_ok && !field {
            found.push(at);
        }
        search = after;
    }
    found
}

/// Splits an argument list at top-level commas.
fn split_arguments(args: &str) -> Vec<&str> {
    let mut parts = Vec::new();
    let mut depth = 0i32;
    let mut start = 0usize;
    for (index, byte) in args.bytes().enumerate() {
        match byte {
            b'(' | b'<' | b'[' => depth += 1,
            b')' | b'>' | b']' => depth -= 1,
            b',' if depth == 0 => {
                parts.push(args[start..index].trim());
                start = index + 1;
            }
            _ => {}
        }
    }
    parts.push(args[start..].trim());
    parts
}

/// `12u` -> 12 for a register-index argument.
fn literal_register_index(argument: &str) -> Option<usize> {
    let digits = argument.strip_suffix('u')?;
    let value: usize = digits.parse().ok()?;
    (value < 32).then_some(value)
}

struct Renamed {
    text: String,
    used: [bool; SLOTS],
}

/// Renames every directly indexed register access. `None` for a
/// runtime-indexed access.
fn rename_register_accesses(body: &str, floats: bool) -> Option<Renamed> {
    const OPEN: &str = "context->";
    let mut text = String::with_capacity(body.len());
    let mut used = [false; SLOTS];
    let mut cursor = 0usize;
    while let Some(offset) = body[cursor..].find(OPEN) {
        let at = cursor + offset;
        let after = at + OPEN.len();
        text.push_str(&body[cursor..at]);
        let rest = &body[after..];
        let mut handled = false;
        for (prefix, base) in [
            ("gpr[", 0usize),
            ("fpr_bits[", FPR_BASE),
            ("ps1_bits[", PS1_BASE),
        ] {
            if base != 0 && !floats {
                continue;
            }
            if let Some(index_text) = rest.strip_prefix(prefix) {
                let digits = index_text
                    .bytes()
                    .take_while(|byte| byte.is_ascii_digit())
                    .count();
                if digits == 0 || index_text.as_bytes().get(digits) != Some(&b']') {
                    return None;
                }
                let index: usize = index_text[..digits].parse().ok()?;
                if index >= 32 {
                    return None;
                }
                let slot = base + index;
                used[slot] = true;
                text.push_str(&local_name(slot));
                cursor = after + prefix.len() + digits + 1;
                handled = true;
                break;
            }
        }
        if handled {
            continue;
        }
        for (field, slot) in [("lr", LR), ("ctr", CTR)] {
            if rest.starts_with(field)
                && rest
                    .as_bytes()
                    .get(field.len())
                    .map_or(true, |byte| !is_ident(*byte))
            {
                used[slot] = true;
                text.push_str(&local_name(slot));
                cursor = after + field.len();
                handled = true;
                break;
            }
        }
        if handled {
            continue;
        }
        // Another field (pc, cr, xer, msr, gqr, hid2, fpscr, spr, ...): copy
        // the prefix and go on.
        text.push_str(OPEN);
        cursor = after;
    }
    text.push_str(&body[cursor..]);
    Some(Renamed { text, used })
}

/// Rewrites the native float fast paths to their register-resident `_r`
/// overloads and records the FPR/PS1 entries they touch. `None` when a
/// register argument is not a literal.
fn rewrite_float_helpers(
    text: &str,
    used: &mut [bool; SLOTS],
    written: &mut [bool; SLOTS],
) -> Option<String> {
    const ARITHMETIC: [&str; 7] = [
        "scalar_binary",
        "scalar_ternary",
        "scalar_frsp",
        "paired_binary",
        "paired_ternary",
        "paired_sum",
        "fctiwz",
    ];
    const MEMORY: [&str; 8] = [
        "load_fpr_single",
        "store_fpr_single",
        "psq_load",
        "psq_store",
        "load_fpr_single_hw",
        "store_fpr_single_hw",
        "psq_load_hw",
        "psq_store_hw",
    ];
    let mut out = text.to_owned();
    for name in ARITHMETIC {
        // Templated form: galaxy::fast::NAME<...>(context, ...)
        let templated = format!("galaxy::fast::{name}<");
        out = out.replace(&templated, &format!("galaxy::fast::{name}_r<"));
        // Plain form: galaxy::fast::NAME(context, ...)
        let plain = format!("galaxy::fast::{name}(context, ");
        out = out.replace(
            &plain,
            &format!("galaxy::fast::{name}_r(context, fpr, ps1, "),
        );
    }
    // Templated arithmetic: insert the arrays after `context, `.
    let mut rewritten = String::with_capacity(out.len() + 256);
    let mut cursor = 0usize;
    while let Some(offset) = out[cursor..].find("_r<") {
        let at = cursor + offset;
        // find the `(context, ` that follows the template argument list
        let open_rel = out[at..].find("(context, ")?;
        let open = at + open_rel;
        rewritten.push_str(&out[cursor..open]);
        rewritten.push_str("(context, fpr, ps1, ");
        cursor = open + "(context, ".len();
    }
    rewritten.push_str(&out[cursor..]);
    let mut out = rewritten;
    for name in MEMORY {
        // galaxy::fast::NAME(<base>, context, ...)
        let needle = format!("galaxy::fast::{name}(");
        let mut rewritten = String::with_capacity(out.len() + 64);
        let mut cursor = 0usize;
        while let Some(offset) = out[cursor..].find(&needle) {
            let at = cursor + offset;
            let args_start = at + needle.len();
            let comma_rel = out[args_start..].find(", context, ")?;
            let comma = args_start + comma_rel;
            rewritten.push_str(&out[cursor..at]);
            let _ = write!(rewritten, "galaxy::fast::{name}_r(");
            rewritten.push_str(&out[args_start..comma]);
            rewritten.push_str(", context, fpr, ps1, ");
            cursor = comma + ", context, ".len();
        }
        rewritten.push_str(&out[cursor..]);
        out = rewritten;
    }
    // Record the registers each rewritten helper touches.
    for line in out.lines() {
        for token in bare_context_tokens(line) {
            let Some(call) = enclosing_call(line, token) else {
                continue;
            };
            let Some(positions) = float_helper_index_positions(&call.callee) else {
                continue;
            };
            let arguments = split_arguments(&line[call.args_start..call.args_end]);
            for position in positions {
                let argument = arguments.get(*position)?;
                let index = literal_register_index(argument)?;
                used[fpr_slot(index)] = true;
                used[ps1_slot(index)] = true;
            }
            if let Some(position) = float_helper_target_position(&call.callee) {
                let index = literal_register_index(arguments.get(position)?)?;
                written[fpr_slot(index)] = true;
                written[ps1_slot(index)] = true;
            }
        }
    }
    Some(out)
}

/// Locals that the body assigns textually (plain `=` and compound
/// assignments, `++`, `--`). Float entries written through the `_r` helpers
/// are recorded by `rewrite_float_helpers` and merged by the caller.
fn written_locals_in_text(text: &str, used: &[bool; SLOTS]) -> [bool; SLOTS] {
    let bytes = text.as_bytes();
    let mut written = [false; SLOTS];
    for (slot, flag) in used.iter().enumerate() {
        if !*flag {
            continue;
        }
        let name = local_name(slot);
        let mut search = 0usize;
        while let Some(offset) = text[search..].find(&name) {
            let at = search + offset;
            let after = at + name.len();
            search = after;
            let before_ok = at == 0 || !is_ident(bytes[at - 1]);
            let after_ok = after >= bytes.len() || !is_ident(bytes[after]);
            if !before_ok || !after_ok {
                continue;
            }
            if at >= 2 && (&text[at - 2..at] == "--" || &text[at - 2..at] == "++") {
                written[slot] = true;
                break;
            }
            let mut probe = after;
            while probe < bytes.len() && bytes[probe] == b' ' {
                probe += 1;
            }
            let tail = &text[probe..];
            let assigns = (tail.starts_with('=') && !tail.starts_with("=="))
                || tail.starts_with("+=")
                || tail.starts_with("-=")
                || tail.starts_with("|=")
                || tail.starts_with("&=")
                || tail.starts_with("^=")
                || tail.starts_with("<<=")
                || tail.starts_with(">>=")
                || tail.starts_with("++")
                || tail.starts_with("--");
            if assigns {
                written[slot] = true;
                break;
            }
        }
    }
    written
}

/// Stores: every local the body may have written (textually, through an `_r`
/// float helper target, or through an exact scalar commit). A local that was
/// only read still equals the context's value and needs no store.
fn store_sequence(written: &[bool; SLOTS]) -> String {
    let mut out = String::new();
    for (slot, flag) in written.iter().enumerate() {
        if *flag {
            let _ = write!(out, "{} = {}; ", field_name(slot), local_name(slot));
        }
    }
    out
}

fn reload_sequence(used: &[bool; SLOTS]) -> String {
    let mut out = String::new();
    for (slot, flag) in used.iter().enumerate() {
        if *flag {
            let _ = write!(out, "{} = {}; ", local_name(slot), field_name(slot));
        }
    }
    out
}

/// The entries of `flags` named by a GPR mask and an FPR mask (FPRs with
/// their PS1 halves).
fn contract_slots(flags: &[bool; SLOTS], gpr_mask: u32, fpr_mask: u32) -> [bool; SLOTS] {
    let mut slots = [false; SLOTS];
    for index in 0..32 {
        if gpr_mask & (1u32 << index) != 0 {
            slots[index] = flags[index];
        }
        if fpr_mask & (1u32 << index) != 0 {
            slots[fpr_slot(index)] = flags[fpr_slot(index)];
            slots[ps1_slot(index)] = flags[ps1_slot(index)];
        }
    }
    slots
}

/// Stores before a lean body: the written locals it may read or write (a
/// register it writes on some paths only must hold the current value).
fn lean_store_sequence(written: &[bool; SLOTS], contract: LeanCallContract) -> String {
    store_sequence(&contract_slots(
        written,
        contract.gpr_reads | contract.gpr_writes,
        contract.fpr_reads | contract.fpr_writes,
    ))
}

/// Reloads after a lean body: the used locals it writes.
fn lean_reload_sequence(used: &[bool; SLOTS], contract: LeanCallContract) -> String {
    reload_sequence(&contract_slots(
        used,
        contract.gpr_writes,
        contract.fpr_writes,
    ))
}

/// The local declarations, loaded from the context. Under the EABI contract a
/// written volatile local may never be read again (no return stores it), so
/// those declarations are `[[maybe_unused]]` (MSVC C4189, clang
/// -Wunused-but-set-variable).
fn declarations(used: &[bool; SLOTS], maybe_unused: bool) -> String {
    let mut out = String::new();
    let attribute = if maybe_unused {
        "[[maybe_unused]] "
    } else {
        ""
    };
    for (slot, flag) in used.iter().enumerate() {
        if *flag && !is_float_slot(slot) {
            let _ = writeln!(
                out,
                "    {attribute}std::uint32_t {} = {};",
                local_name(slot),
                field_name(slot)
            );
        }
    }
    if used[FPR_BASE..].iter().any(|flag| *flag) {
        out.push_str("    std::uint64_t fpr[32];\n    std::uint64_t ps1[32];\n");
        for (slot, flag) in used.iter().enumerate() {
            if *flag && is_float_slot(slot) {
                let _ = writeln!(out, "    {} = {};", local_name(slot), field_name(slot));
            }
        }
    }
    out
}

/// EABI synchronization of one function (see the module documentation).
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct AbiResidency {
    /// Call sites marked `GALAXY_ABI_CALL(0x<pc>u);` that use the contract,
    /// by call pc. A marked site missing here keeps full synchronization.
    pub call_sites: BTreeMap<u32, CallSync>,
    /// Registers stored at `GALAXY_GUEST_RETURN;`, or `None` for every
    /// written local.
    pub exit: Option<Regs>,
}

/// The local slots of a register set (an FPR covers both lanes).
fn register_slots(registers: Regs) -> [bool; SLOTS] {
    let mut slots = [false; SLOTS];
    for index in 0..32 {
        slots[index] = registers.gpr & (1u32 << index) != 0;
        slots[fpr_slot(index)] = registers.fpr & (1u32 << index) != 0;
        slots[ps1_slot(index)] = registers.fpr & (1u32 << index) != 0;
    }
    slots[LR] = registers.lr;
    slots[CTR] = registers.ctr;
    slots
}

fn masked(flags: &[bool; SLOTS], registers: Regs) -> [bool; SLOTS] {
    let mask = register_slots(registers);
    let mut out = [false; SLOTS];
    for slot in 0..SLOTS {
        out[slot] = flags[slot] && mask[slot];
    }
    out
}

/// `GALAXY_ABI_CALL(0x8000400Cu);` -> 0x8000400C.
fn abi_call_marker(trimmed: &str) -> Option<u32> {
    let hex = trimmed
        .strip_prefix("GALAXY_ABI_CALL(0x")?
        .strip_suffix("u);")?;
    u32::from_str_radix(hex, 16).ok()
}

/// A guest call the EABI contract may cover: a translated function or the
/// indirect-call fast path.
fn abi_callee(callee: &str) -> bool {
    callee.starts_with("rmge01::fn_")
        || callee == "galaxy::fast::call_guest_indirect"
        || callee == "galaxy::fast::call_guest_indirect_site"
}

/// Replaces every `GALAXY_GUEST_RETURN;` with a stored-back return.
fn store_before_guest_returns(line: &str, stores: &str) -> String {
    const GUEST_RETURN: &str = "GALAXY_GUEST_RETURN;";
    if !line.contains(GUEST_RETURN) {
        return line.to_owned();
    }
    line.replace(GUEST_RETURN, &format!("{{ {stores}return; }}"))
}

/// Replaces every statement-position `return;` with a stored-back return.
fn store_before_returns(line: &str, stores: &str) -> String {
    const RETURN: &str = "return;";
    if stores.is_empty() || !line.contains(RETURN) {
        return line.to_owned();
    }
    let bytes = line.as_bytes();
    let mut out = String::with_capacity(line.len() + 64);
    let mut cursor = 0usize;
    while let Some(offset) = line[cursor..].find(RETURN) {
        let at = cursor + offset;
        let before_ok = at == 0 || !is_ident(bytes[at - 1]);
        out.push_str(&line[cursor..at]);
        if before_ok {
            let _ = write!(out, "{{ {stores}return; }}");
        } else {
            out.push_str(RETURN);
        }
        cursor = at + RETURN.len();
    }
    out.push_str(&line[cursor..]);
    out
}

/// Applies the residency transform to one lowered body. See the module
/// documentation for the contract.
#[cfg(test)]
pub fn apply_register_residency(body: &str, floats: bool) -> Option<String> {
    apply_register_residency_with_abi(body, floats, None)
}

/// `apply_register_residency` with the EABI synchronization of marked calls
/// and returns (`None`: every marker keeps full synchronization).
pub fn apply_register_residency_with_abi(
    body: &str,
    floats: bool,
    abi: Option<&AbiResidency>,
) -> Option<String> {
    let Renamed { text, mut used } = rename_register_accesses(body, floats)?;
    let mut helper_written = [false; SLOTS];
    let text = if floats {
        rewrite_float_helpers(&text, &mut used, &mut helper_written)?
    } else {
        text
    };
    if !used.iter().any(|flag| *flag) {
        return None;
    }
    // `ppc_commit_scalar_result` targets are synchronized through the arrays.
    for line in text.lines() {
        for token in bare_context_tokens(line) {
            let Some(call) = enclosing_call(line, token) else {
                continue;
            };
            if classify(&call.callee, floats) == Sync::FprTarget {
                let arguments = split_arguments(&line[call.args_start..call.args_end]);
                let index = literal_register_index(arguments.get(1)?)?;
                used[fpr_slot(index)] = true;
                used[ps1_slot(index)] = true;
                helper_written[fpr_slot(index)] = true;
                helper_written[ps1_slot(index)] = true;
            }
        }
    }
    let mut written = written_locals_in_text(&text, &used);
    for (slot, flag) in helper_written.iter().enumerate() {
        written[slot] |= *flag;
    }
    let stores = store_sequence(&written);
    let reloads = reload_sequence(&used);
    let exit_stores = match abi.and_then(|abi| abi.exit) {
        Some(exit) => store_sequence(&masked(&written, exit)),
        None => stores.clone(),
    };
    // The contract of the call announced by the last marker, until the next
    // synchronizing call or label consumes or drops it.
    let mut pending_abi: Option<CallSync> = None;
    let mut pending_depth = 0i64;

    let mut output = String::with_capacity(text.len() + text.len() / 8);
    let mut entries: BTreeSet<String> = BTreeSet::new();
    let mut in_entry_switch = false;

    // Declarations first, at function scope, before any dispatch `goto`.
    let signature_end = if body.starts_with("void ") || body.starts_with("[[nodiscard") {
        text.find('\n').map_or(0, |newline| newline + 1)
    } else {
        0
    };
    output.push_str(&text[..signature_end]);
    output.push_str(&declarations(&used, abi.is_some()));
    let shared_declarations_at = output.len();
    let rest = &text[signature_end..];

    // A call-return checkpoint that ends a top-level instruction block, or a
    // continuation stub's checkpoint followed by its `goto label_X;`, shares one
    // cold block per function: the site stores its index and jumps there, and
    // the shared block synchronizes, runs the policy and jumps to the site's
    // resume point (a label after the block, or label_X). Every site's stores
    // and reloads are the same sequence, so only one copy is emitted.
    let raw_lines: Vec<&str> = rest.split_inclusive('\n').collect();
    let mut depth_before = Vec::with_capacity(raw_lines.len() + 1);
    let mut depth = 0i64;
    for raw_line in &raw_lines {
        depth_before.push(depth);
        for byte in raw_line.bytes() {
            match byte {
                b'{' => depth += 1,
                b'}' => depth -= 1,
                _ => {}
            }
        }
    }
    depth_before.push(depth);
    let closes_top_level_block = |index: usize| {
        raw_lines
            .get(index)
            .is_some_and(|raw_line| raw_line.trim() == "}" && depth_before[index] == 1)
    };
    // `goto label_X;` at function scope, as a continuation stub ends.
    let stub_resume_label = |index: usize| -> Option<String> {
        let raw_line = raw_lines.get(index)?;
        let name = raw_line
            .trim()
            .strip_prefix("goto label_")?
            .strip_suffix(';')?;
        (depth_before[index] == 0
            && name.len() == 8
            && name.bytes().all(|byte| byte.is_ascii_hexdigit()))
        .then(|| format!("label_{name}"))
    };
    // (call and return pc pair, resume target) per shared site.
    let mut shared_sites: Vec<(String, String)> = Vec::new();
    let mut resume_label_after: Option<(usize, usize)> = None;

    for (line_index, raw_line) in raw_lines.iter().copied().enumerate() {
        let (line, newline) = match raw_line.strip_suffix('\n') {
            Some(line) => (line, "\n"),
            None => (raw_line, ""),
        };
        let trimmed = line.trim_start();

        // Interior-entry dispatch: `case 0x...u: goto label_X;` becomes a jump
        // to a reloading stub. `goto call_return_X;` keeps its own stub, whose
        // call_return_checkpoint synchronizes.
        if trimmed.starts_with("switch (context->pc) {") {
            in_entry_switch = true;
            output.push_str(line);
            output.push_str(newline);
            continue;
        }
        if in_entry_switch {
            if trimmed.starts_with("default:") {
                in_entry_switch = false;
            } else if trimmed.starts_with("case 0x") {
                if let Some(label_at) = line.find("goto label_") {
                    let name_start = label_at + "goto label_".len();
                    let name_end = name_start
                        + line[name_start..]
                            .bytes()
                            .take_while(|byte| byte.is_ascii_hexdigit())
                            .count();
                    let name = &line[name_start..name_end];
                    entries.insert(name.to_owned());
                    output.push_str(&line[..label_at]);
                    let _ = write!(output, "goto resident_entry_{name}");
                    output.push_str(&line[name_end..]);
                    output.push_str(newline);
                    continue;
                }
            }
            output.push_str(line);
            output.push_str(newline);
            continue;
        }

        // EABI call markers: remembered for the next synchronizing call in
        // the same block, dropped at a label or when the block closes.
        if let Some(pc) = abi_call_marker(trimmed) {
            pending_abi = abi.and_then(|abi| abi.call_sites.get(&pc).copied());
            pending_depth = depth_before[line_index];
            continue;
        }
        if depth_before[line_index] < pending_depth
            || (!line.starts_with(' ') && line.trim_end().ends_with(':'))
        {
            pending_abi = None;
        }

        // Context-receiving calls.
        let tokens = bare_context_tokens(line);
        let mut calls: Vec<ContextCall> = Vec::new();
        for token in tokens {
            let call = enclosing_call(line, token)?;
            if calls
                .iter()
                .any(|known| known.callee_start == call.callee_start)
            {
                continue;
            }
            calls.push(call);
        }
        let mut syncing: Vec<ContextCall> = Vec::new();
        for call in calls {
            match classify(&call.callee, floats) {
                Sync::None => {}
                _ => {
                    // The call must be a complete statement: `);` follows it and
                    // nothing but a statement boundary precedes it on the line.
                    let before = line[..call.callee_start].trim_end();
                    let at_statement_start = before.is_empty()
                        || before.ends_with('{')
                        || before.ends_with('}')
                        || before.ends_with(';')
                        || before.ends_with(':');
                    if !call.statement || !at_statement_start {
                        return None;
                    }
                    syncing.push(call);
                }
            }
        }
        if syncing.len() > 1 {
            return None;
        }
        let line = if let Some(call) = syncing.pop() {
            let abi_sync = pending_abi.take().filter(|_| abi_callee(&call.callee));
            let before = &line[..call.callee_start];
            let args = &line[call.args_start..call.args_end];
            let after = &line[call.args_end + 2..]; // past `);`
            let mut rewritten = String::with_capacity(line.len() + 256);
            rewritten.push_str(before);
            match classify(&call.callee, floats) {
                Sync::Checkpoint => {
                    let due = if call.callee.ends_with("_budgeted") {
                        "galaxy::branch_checkpoint_due_budgeted"
                    } else {
                        "galaxy::branch_checkpoint_due"
                    };
                    let _ = write!(
                        rewritten,
                        "if (const std::uint64_t counted__ = {due}(services); counted__ != 0u) [[unlikely]] {{ GALAXY_RETURN_IF_TRANSFER_PENDING(services); {stores}galaxy::branch_checkpoint_taken_counted({args}, counted__); {reloads}GALAXY_RETURN_IF_TRANSFER_PENDING(services); }}"
                    );
                }
                // A completed call already reloaded the callee's results; the
                // checkpoint's own synchronization is needed only when its
                // policy runs, which is the cold branch.
                Sync::CallReturn => {
                    let arguments = split_arguments(args);
                    let site = shared_sites.len();
                    let resume = if !before.trim().is_empty()
                        || !after.trim().is_empty()
                        || arguments.len() != 5
                        || arguments[0] != "services"
                        || arguments[3] != "context"
                        || arguments[4] != "memory"
                    {
                        None
                    } else if closes_top_level_block(line_index + 1) {
                        resume_label_after = Some((line_index + 1, site));
                        Some(format!("shared_resume_{site}"))
                    } else if depth_before[line_index] == 0 {
                        stub_resume_label(line_index + 1)
                    } else {
                        None
                    };
                    if let Some(resume) = resume {
                        shared_sites
                            .push((format!("{{{}, {}}}", arguments[1], arguments[2]), resume));
                        let _ = write!(
                            rewritten,
                            "if (const std::uint64_t counted__ = galaxy::call_return_checkpoint_due(services); counted__ != 0u) [[unlikely]] {{ shared_counted__ = counted__; shared_site__ = {site}u; goto shared_call_return__; }}"
                        );
                    } else {
                        let _ = write!(
                            rewritten,
                            "if (const std::uint64_t counted__ = galaxy::call_return_checkpoint_due(services); counted__ != 0u) [[unlikely]] {{ GALAXY_RETURN_IF_TRANSFER_PENDING(services); {stores}galaxy::call_return_checkpoint_counted({args}, counted__); {reloads}GALAXY_RETURN_IF_TRANSFER_PENDING(services); }}"
                        );
                    }
                }
                Sync::Full => match abi_sync {
                    Some(sync) => {
                        let _ = write!(
                            rewritten,
                            "{{ {}{}({args}); {}}}",
                            store_sequence(&masked(&written, sync.store)),
                            call.callee_text,
                            reload_sequence(&masked(&used, sync.reload))
                        );
                    }
                    None => {
                        let _ = write!(
                            rewritten,
                            "{{ {stores}{}({args}); {reloads}}}",
                            call.callee_text
                        );
                    }
                },
                Sync::Lean(contract) => {
                    let _ = write!(
                        rewritten,
                        "{{ {}{}({args}); {}}}",
                        lean_store_sequence(&written, contract),
                        call.callee_text,
                        lean_reload_sequence(&used, contract)
                    );
                }
                Sync::FprTarget => {
                    let arguments = split_arguments(args);
                    let index = literal_register_index(arguments.get(1)?)?;
                    let f = local_name(fpr_slot(index));
                    let p = local_name(ps1_slot(index));
                    let ff = field_name(fpr_slot(index));
                    let pf = field_name(ps1_slot(index));
                    let _ = write!(
                        rewritten,
                        "{{ {ff} = {f}; {pf} = {p}; {}({args}); {f} = {ff}; {p} = {pf}; }}",
                        call.callee_text
                    );
                }
                Sync::None => unreachable!("filtered above"),
            }
            rewritten.push_str(after);
            rewritten
        } else {
            line.to_owned()
        };

        let line = store_before_returns(&line, &stores);
        let line = store_before_guest_returns(&line, &exit_stores);
        output.push_str(&line);
        output.push_str(newline);
        if let Some((after_line, site)) = resume_label_after {
            if after_line == line_index {
                if !output.ends_with('\n') {
                    output.push('\n');
                }
                let _ = writeln!(output, "shared_resume_{site}:;");
                resume_label_after = None;
            }
        }
    }

    if !output.ends_with('\n') {
        output.push('\n');
    }
    for name in &entries {
        let _ = writeln!(
            output,
            "resident_entry_{name}:\n    {reloads}\n    goto label_{name};"
        );
    }
    if !shared_sites.is_empty() {
        let _ = writeln!(
            output,
            "shared_call_return__:\n    {{\n    static constexpr std::uint32_t shared_call_return_sites__[][2] = {{{}}};\n    GALAXY_RETURN_IF_TRANSFER_PENDING(services);\n    {stores}galaxy::call_return_checkpoint_counted(services, shared_call_return_sites__[shared_site__][0], shared_call_return_sites__[shared_site__][1], context, memory, shared_counted__);\n    {reloads}GALAXY_RETURN_IF_TRANSFER_PENDING(services);\n    }}\n    switch (shared_site__) {{",
            shared_sites
                .iter()
                .map(|(pcs, _)| pcs.as_str())
                .collect::<Vec<_>>()
                .join(", ")
        );
        for (site, (_, resume)) in shared_sites.iter().enumerate() {
            let _ = writeln!(output, "    case {site}u: goto {resume};");
        }
        let _ = writeln!(output, "    default: {{ {stores}return; }}\n    }}");
        output.insert_str(
            shared_declarations_at,
            "    std::uint64_t shared_counted__ = 0u;\n    std::uint32_t shared_site__ = 0u;\n",
        );
    }
    // A body with hundreds of synchronization points grows by one store/reload
    // sequence per call. Keep the very largest functions on the plain lowering
    // so they stay inside the module's per-shard source budget.
    if output.len() > MAX_RESIDENT_BODY_BYTES {
        return None;
    }
    Some(output)
}

/// Upper bound on a resident body's source size; the module shard budget is
/// 512 KiB and the shard carries several functions.
const MAX_RESIDENT_BODY_BYTES: usize = 384 * 1024;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn renames_gprs_lr_and_ctr_and_declares_them_first() {
        let body = "    {\n    context->gpr[3] = context->gpr[4] + 0x00000008u;\n    }\n    {\n    context->lr = context->gpr[0];\n    }\n    return;\n";
        let out = apply_register_residency(body, false).expect("eligible");
        assert!(out.starts_with(
            "    std::uint32_t gr0 = context->gpr[0];\n    std::uint32_t gr3 = context->gpr[3];\n    std::uint32_t gr4 = context->gpr[4];\n    std::uint32_t glr = context->lr;\n"
        ));
        assert!(out.contains("    gr3 = gr4 + 0x00000008u;\n"));
        assert!(out.contains("    glr = gr0;\n"));
        // Only written locals are stored at the exit.
        assert!(out.contains("{ context->gpr[3] = gr3; context->lr = glr; return; }"));
        assert!(!out.contains("context->gpr[4] = gr4"));
    }

    #[test]
    fn rejects_runtime_indexed_gpr_access() {
        let body = "    for (std::uint32_t reg = 14u; reg < 32u; ++reg) {\n        context->gpr[reg] = 0u;\n    }\n    return;\n";
        assert!(apply_register_residency(body, false).is_none());
    }

    #[test]
    fn direct_call_stores_before_and_reloads_after() {
        let body = "    {\n    context->gpr[3] = context->gpr[31];\n    }\n    {\n    context->lr = 0x80001010u;\n    context->pc = 0x80002000u;\n    rmge01::fn_80002000(context, memory, services);\n    galaxy::call_return_checkpoint(services, 0x8000100Cu, 0x80001010u, context, memory);\n    }\n    return;\n";
        let out = apply_register_residency(body, false).expect("eligible");
        assert!(out.contains(
            "    { context->gpr[3] = gr3; context->lr = glr; rmge01::fn_80002000(context, memory, services); gr3 = context->gpr[3]; gr31 = context->gpr[31]; glr = context->lr; }\n"
        ));
        // The checkpoint ends a top-level block, so its cold half is the
        // function's shared block: one copy of the stores and reloads.
        assert!(out.contains(
            "    if (const std::uint64_t counted__ = galaxy::call_return_checkpoint_due(services); counted__ != 0u) [[unlikely]] { shared_counted__ = counted__; shared_site__ = 0u; goto shared_call_return__; }\n    }\nshared_resume_0:;\n"
        ));
        assert!(out.contains(
            "    std::uint64_t shared_counted__ = 0u;\n    std::uint32_t shared_site__ = 0u;\n"
        ));
        assert!(out.contains(
            "shared_call_return__:\n    {\n    static constexpr std::uint32_t shared_call_return_sites__[][2] = {{0x8000100Cu, 0x80001010u}};\n    GALAXY_RETURN_IF_TRANSFER_PENDING(services);\n    context->gpr[3] = gr3; context->lr = glr; galaxy::call_return_checkpoint_counted(services, shared_call_return_sites__[shared_site__][0], shared_call_return_sites__[shared_site__][1], context, memory, shared_counted__);\n    gr3 = context->gpr[3]; gr31 = context->gpr[31]; glr = context->lr; GALAXY_RETURN_IF_TRANSFER_PENDING(services);\n    }\n    switch (shared_site__) {\n    case 0u: goto shared_resume_0;\n    default: { context->gpr[3] = gr3; context->lr = glr; return; }\n    }\n"
        ));
    }

    #[test]
    fn condition_register_helpers_do_not_synchronize() {
        let body = "    {\n    galaxy::compare_signed(context, 0u, static_cast<std::int32_t>(context->gpr[5]), static_cast<std::int32_t>(context->gpr[3]));\n    if (!galaxy::cr_bit(context, 2u)) goto label_80004010;\n    }\nlabel_80004010:\n    return;\n";
        let out = apply_register_residency(body, false).expect("eligible");
        assert!(out.contains("galaxy::compare_signed(context, 0u, static_cast<std::int32_t>(gr5), static_cast<std::int32_t>(gr3));"));
        assert!(!out.contains("context->gpr[5] = gr5"));
        assert!(!out.contains("gr5 = context->gpr[5]; "));
    }

    #[test]
    fn float_helpers_take_the_resident_arrays_and_declare_their_registers() {
        let body = "    {\n    galaxy::fast::scalar_binary<galaxy::PpcFloatBinaryOperation::Add, true>(context, 1u, 2u, 3u, false, services, 0x80004008u);\n    }\n    {\n    galaxy::fast::load_fpr_single(flat_guest_read_base, context, 5u, memory, context->gpr[4] + 0x00000008u, services, 0x8000400Cu);\n    }\n    {\n    galaxy::fast::psq_store(flat_guest_base, context, 1u, memory, context->gpr[3], 0u, false, true, services, 0x80004010u);\n    }\n    {\n    context->fpr_bits[7] = context->fpr_bits[1] ^ 0x8000000000000000ull;\n    }\n    return;\n";
        let out = apply_register_residency(body, true).expect("eligible");
        assert!(out.contains("    std::uint64_t fpr[32];\n    std::uint64_t ps1[32];\n    fpr[1] = context->fpr_bits[1];\n    fpr[2] = context->fpr_bits[2];\n    fpr[3] = context->fpr_bits[3];\n    fpr[5] = context->fpr_bits[5];\n    fpr[7] = context->fpr_bits[7];\n    ps1[1] = context->ps1_bits[1];\n"));
        assert!(out.contains("galaxy::fast::scalar_binary_r<galaxy::PpcFloatBinaryOperation::Add, true>(context, fpr, ps1, 1u, 2u, 3u, false, services, 0x80004008u);"));
        assert!(out.contains("galaxy::fast::load_fpr_single_r(flat_guest_read_base, context, fpr, ps1, 5u, memory, gr4 + 0x00000008u, services, 0x8000400Cu);"));
        assert!(out.contains("galaxy::fast::psq_store_r(flat_guest_base, context, fpr, ps1, 1u, memory, gr3, 0u, false, true, services, 0x80004010u);"));
        assert!(out.contains("    fpr[7] = fpr[1] ^ 0x8000000000000000ull;\n"));
        // Written float entries (helper targets and textual writes) are stored at
        // the exit; read-only entries and the read-only integer locals are not.
        assert!(out.contains("{ context->fpr_bits[1] = fpr[1]; context->fpr_bits[5] = fpr[5]; context->fpr_bits[7] = fpr[7]; context->ps1_bits[1] = ps1[1]; context->ps1_bits[5] = ps1[5]; return; }"));
    }

    const HW_FLOAT_MEMORY_BODY: &str = "    {\n    context->gpr[4] = context->gpr[4] + 0x00000010u;\n    }\n    {\n    galaxy::fast::load_fpr_single_hw(flat_guest_read_base, context, 5u, memory, context->gpr[4] + 0x00000008u, services, 0x80004004u);\n    }\n    {\n    galaxy::fast::store_fpr_single_hw(flat_guest_base, context, 5u, memory, context->gpr[3], services, 0x80004008u);\n    }\n    {\n    galaxy::fast::psq_load_hw(flat_guest_read_base, context, 6u, memory, context->gpr[3], 0u, false, true, services, 0x8000400Cu);\n    }\n    {\n    galaxy::fast::psq_store_hw(flat_guest_base, context, 6u, memory, context->gpr[4], 0u, false, true, services, 0x80004010u);\n    }\n    return;\n";

    #[test]
    fn hw_float_memory_helpers_are_not_integer_synchronization_points() {
        let out = apply_register_residency(HW_FLOAT_MEMORY_BODY, false).expect("eligible");
        for call in [
            "galaxy::fast::load_fpr_single_hw(flat_guest_read_base, context, 5u, memory, gr4 + 0x00000008u, services, 0x80004004u);",
            "galaxy::fast::store_fpr_single_hw(flat_guest_base, context, 5u, memory, gr3, services, 0x80004008u);",
            "galaxy::fast::psq_load_hw(flat_guest_read_base, context, 6u, memory, gr3, 0u, false, true, services, 0x8000400Cu);",
            "galaxy::fast::psq_store_hw(flat_guest_base, context, 6u, memory, gr4, 0u, false, true, services, 0x80004010u);",
        ] {
            assert!(out.contains(&format!("    {call}\n")), "{call}\n{out}");
        }
        // gr4 is written before the helpers and stored only at the exit.
        assert_eq!(out.matches("context->gpr[4] = gr4;").count(), 1, "{out}");
        assert!(!out.contains("gr4 = context->gpr[4]; "), "{out}");
    }

    #[test]
    fn hw_float_memory_helpers_take_the_resident_arrays() {
        let out = apply_register_residency(HW_FLOAT_MEMORY_BODY, true).expect("eligible");
        for call in [
            "galaxy::fast::load_fpr_single_hw_r(flat_guest_read_base, context, fpr, ps1, 5u, memory, gr4 + 0x00000008u, services, 0x80004004u);",
            "galaxy::fast::store_fpr_single_hw_r(flat_guest_base, context, fpr, ps1, 5u, memory, gr3, services, 0x80004008u);",
            "galaxy::fast::psq_load_hw_r(flat_guest_read_base, context, fpr, ps1, 6u, memory, gr3, 0u, false, true, services, 0x8000400Cu);",
            "galaxy::fast::psq_store_hw_r(flat_guest_base, context, fpr, ps1, 6u, memory, gr4, 0u, false, true, services, 0x80004010u);",
        ] {
            assert!(out.contains(call), "{call}\n{out}");
        }
        // The loads' targets are written float entries, stored at the exit.
        assert!(out.contains("context->fpr_bits[5] = fpr[5]; context->fpr_bits[6] = fpr[6]; context->ps1_bits[5] = ps1[5]; context->ps1_bits[6] = ps1[6]; return; }"), "{out}");
    }

    #[test]
    fn exact_scalar_commit_synchronizes_its_target() {
        let body = "    {\n    const galaxy::PpcFloatResult result = galaxy::ppc_f64_to_i32_round_zero(context->fpr_bits[0], context->fpscr);\n    galaxy::ppc_commit_scalar_result(context, 2u, result, false, false, services, 0x80004000u);\n    }\n    return;\n";
        let out = apply_register_residency(body, true).expect("eligible");
        assert!(out.contains("galaxy::ppc_f64_to_i32_round_zero(fpr[0], context->fpscr);"));
        assert!(out.contains("    { context->fpr_bits[2] = fpr[2]; context->ps1_bits[2] = ps1[2]; galaxy::ppc_commit_scalar_result(context, 2u, result, false, false, services, 0x80004000u); fpr[2] = context->fpr_bits[2]; ps1[2] = context->ps1_bits[2]; }\n"));
    }

    #[test]
    fn float_helper_with_non_literal_register_is_rejected() {
        let body = "    {\n    galaxy::fast::scalar_frsp(context, target, 4u, false, services, 0x80004000u);\n    }\n    return;\n";
        assert!(apply_register_residency(body, false).is_none());
    }

    #[test]
    fn backward_branch_checkpoint_is_split_into_inline_test_and_cold_sync() {
        let body = "label_80004000:\n    {\n    context->gpr[3] = context->gpr[3] + 0x00000001u;\n    }\n    {\n    if (--context->ctr != 0u) { galaxy::branch_checkpoint_taken(services, 0x80004008u, 0x80004000u, context, memory); goto label_80004000; }\n    }\n    return;\n";
        let out = apply_register_residency(body, false).expect("eligible");
        assert!(out.contains(
            "if (--gctr != 0u) { if (const std::uint64_t counted__ = galaxy::branch_checkpoint_due(services); counted__ != 0u) [[unlikely]] { GALAXY_RETURN_IF_TRANSFER_PENDING(services); context->gpr[3] = gr3; context->ctr = gctr; galaxy::branch_checkpoint_taken_counted(services, 0x80004008u, 0x80004000u, context, memory, counted__); gr3 = context->gpr[3]; gctr = context->ctr; GALAXY_RETURN_IF_TRANSFER_PENDING(services); } goto label_80004000; }"
        ));
    }

    #[test]
    fn interior_entries_reload_through_stubs() {
        let body = "    if (context->pc != 0x80004000u) [[unlikely]] {\n    switch (context->pc) {\n        case 0x80004008u: goto label_80004008;\n        case 0x80004010u: goto call_return_80004010;\n        default: galaxy::guest_execution_fault(services, context->pc, \"invalid interior function entry\");\n    }\n    }\n    {\n    context->gpr[3] = 0x00000001u;\n    }\nlabel_80004008:\n    {\n    context->gpr[4] = context->gpr[3];\n    }\n    return;\ncall_return_80004010:\n    galaxy::call_return_checkpoint(services, 0x8000400Cu, 0x80004010u, context, memory);\n    goto label_80004008;\n";
        let out = apply_register_residency(body, false).expect("eligible");
        assert!(out.contains("        case 0x80004008u: goto resident_entry_80004008;\n"));
        assert!(out.contains("        case 0x80004010u: goto call_return_80004010;\n"));
        assert!(out.contains("resident_entry_80004008:\n    gr3 = context->gpr[3]; gr4 = context->gpr[4]; \n    goto label_80004008;\n"));
        // The continuation stub shares the function's cold block and resumes
        // at its own goto target.
        assert!(out.contains("call_return_80004010:\n    if (const std::uint64_t counted__ = galaxy::call_return_checkpoint_due(services); counted__ != 0u) [[unlikely]] { shared_counted__ = counted__; shared_site__ = 0u; goto shared_call_return__; }\n    goto label_80004008;\n"));
        assert!(out.contains("    static constexpr std::uint32_t shared_call_return_sites__[][2] = {{0x8000400Cu, 0x80004010u}};\n"));
        assert!(out.contains("    context->gpr[3] = gr3; context->gpr[4] = gr4; galaxy::call_return_checkpoint_counted(services, shared_call_return_sites__[shared_site__][0], shared_call_return_sites__[shared_site__][1], context, memory, shared_counted__);\n    gr3 = context->gpr[3]; gr4 = context->gpr[4]; GALAXY_RETURN_IF_TRANSFER_PENDING(services);\n"));
        assert!(out.contains("    case 0u: goto label_80004008;\n"));
    }

    #[test]
    fn unknown_context_helper_is_a_full_synchronization_point() {
        let body = "    {\n    context->gpr[11] = context->gpr[1] + 0x00000010u;\n    galaxy::native_savegpr_805174FC(context, memory, services, 0x80517538u);\n    }\n    return;\n";
        let out = apply_register_residency(body, false).expect("eligible");
        assert!(out.contains("{ context->gpr[11] = gr11; galaxy::native_savegpr_805174FC(context, memory, services, 0x80517538u); gr1 = context->gpr[1]; gr11 = context->gpr[11]; }"));
    }

    #[test]
    fn float_helpers_need_no_integer_synchronization_without_float_residency() {
        let body = "    {\n    context->gpr[3] = context->gpr[3] + 0x00000004u;\n    }\n    {\n    galaxy::fast::psq_store(flat_guest_base, context, 1u, memory, context->gpr[3], 0u, false, true, services, 0x80004004u);\n    }\n    {\n    galaxy::fast::scalar_binary<galaxy::PpcFloatBinaryOperation::Add, true>(context, 1u, 2u, 3u, false, services, 0x80004008u);\n    }\n    {\n    context->gpr[4] = context->gpr[3];\n    }\n    return;\n";
        let out = apply_register_residency(body, false).expect("eligible");
        assert!(out.contains("    galaxy::fast::psq_store(flat_guest_base, context, 1u, memory, gr3, 0u, false, true, services, 0x80004004u);\n"));
        assert!(out.contains("    galaxy::fast::scalar_binary<galaxy::PpcFloatBinaryOperation::Add, true>(context, 1u, 2u, 3u, false, services, 0x80004008u);\n"));
        assert!(!out.contains("gr3 = context->gpr[3]; "));
    }

    #[test]
    fn lean_body_synchronizes_only_its_contract_registers() {
        // lean_tvec3_add_assign reads r3 and r4. r3 is written in the body, so
        // it is stored; r4 is only read, so the context already holds it.
        // Nothing is reloaded: the body writes no register.
        let body = "    {\n    context->gpr[3] = context->gpr[31] + 0x00000010u;\n    }\n    {\n    context->lr = 0x80001010u;\n    galaxy::lean_tvec3_add_assign(context, memory, services, 0x80018BA0u);\n    }\n    {\n    context->gpr[31] = context->gpr[3];\n    context->fpr_bits[2] = context->fpr_bits[1];\n    }\n    return;\n";
        let out = apply_register_residency(body, true).expect("eligible");
        assert!(out.contains(
            "    { context->gpr[3] = gr3; galaxy::lean_tvec3_add_assign(context, memory, services, 0x80018BA0u); }\n"
        ));
    }

    #[test]
    fn lean_body_result_registers_are_reloaded() {
        // lean_tvec3_dot reads r3 and r4 and returns f1 (ps0 and ps1).
        let body = "    {\n    context->gpr[4] = context->gpr[30];\n    context->fpr_bits[1] = context->fpr_bits[31];\n    }\n    {\n    galaxy::lean_tvec3_dot(context, memory, services, 0x8001CF44u);\n    }\n    {\n    context->fpr_bits[30] = context->fpr_bits[1];\n    context->ps1_bits[30] = context->ps1_bits[1];\n    }\n    return;\n";
        let out = apply_register_residency(body, true).expect("eligible");
        assert!(out.contains(
            "    { context->gpr[4] = gr4; context->fpr_bits[1] = fpr[1]; galaxy::lean_tvec3_dot(context, memory, services, 0x8001CF44u); fpr[1] = context->fpr_bits[1]; ps1[1] = context->ps1_bits[1]; }\n"
        ));
    }

    #[test]
    fn templated_full_sync_callee_keeps_its_template_arguments() {
        let body = "    {\n    galaxy::guest_store_multiple_gprs<13u>(context, memory, context->gpr[3] + 0x00000034u, services, 0x804A379Cu);\n    }\n    return;\n";
        let out = apply_register_residency(body, false).expect("eligible");
        assert!(out.contains("{ galaxy::guest_store_multiple_gprs<13u>(context, memory, gr3 + 0x00000034u, services, 0x804A379Cu); gr3 = context->gpr[3]; }"));
    }

    #[test]
    fn synchronizing_call_on_the_right_of_an_assignment_is_rejected() {
        let body = "    {\n    context->gpr[10] = galaxy::native_something(context, memory, services, 0x80004000u);\n    }\n    return;\n";
        assert!(apply_register_residency(body, false).is_none());
    }

    #[test]
    fn spr_reads_other_than_lr_and_ctr_do_not_synchronize() {
        let body = "    {\n    context->gpr[10] = galaxy::read_spr(context, 268u, services, 0x80004000u);\n    }\n    return;\n";
        let out = apply_register_residency(body, false).expect("eligible");
        assert!(
            out.contains("    gr10 = galaxy::read_spr(context, 268u, services, 0x80004000u);\n")
        );
    }

    #[test]
    fn context_call_used_as_an_expression_is_rejected() {
        let body = "    {\n    context->gpr[3] = 0u;\n    if (galaxy::some_predicate(context, memory)) goto label_80004008;\n    }\nlabel_80004008:\n    return;\n";
        assert!(apply_register_residency(body, false).is_none());
    }

    #[test]
    fn body_without_register_accesses_is_left_alone() {
        assert!(apply_register_residency("    return;\n", false).is_none());
    }

    #[test]
    fn budgeted_backward_branch_checkpoint_splits_inside_the_budget_test() {
        let body = "    [[maybe_unused]] std::uint32_t checkpoint_budget__ = galaxy::kBranchCheckpointBudget;\n    label_80004000:\n    context->gpr[3] = context->gpr[3] + 1u;\n    if (context->gpr[3] < 10u) { if (--checkpoint_budget__ == 0u) [[unlikely]] { checkpoint_budget__ = galaxy::kBranchCheckpointBudget; galaxy::branch_checkpoint_taken_budgeted(services, 0x80004004u, 0x80004000u, context, memory); } goto label_80004000; }\n    return;\n";
        let out = apply_register_residency(body, false).expect("eligible");
        assert!(out.contains("if (const std::uint64_t counted__ = galaxy::branch_checkpoint_due_budgeted(services); counted__ != 0u) [[unlikely]] { GALAXY_RETURN_IF_TRANSFER_PENDING(services); context->gpr[3] = gr3; galaxy::branch_checkpoint_taken_counted(services, 0x80004004u, 0x80004000u, context, memory, counted__); gr3 = context->gpr[3]; GALAXY_RETURN_IF_TRANSFER_PENDING(services); }"));
        assert!(out.contains("if (--checkpoint_budget__ == 0u) [[unlikely]] { checkpoint_budget__ = galaxy::kBranchCheckpointBudget; if (const std::uint64_t counted__"));
    }

    const ABI_BODY: &str = "    {\n    context->gpr[0] = context->lr;\n    context->gpr[3] = context->gpr[31];\n    context->gpr[12] = 0x00000001u;\n    }\n    {\n    context->lr = 0x80001010u;\n    GALAXY_ABI_CALL(0x8000100Cu);\n    context->pc = 0x80002000u;\n    rmge01::fn_80002000(context, memory, services);\n    }\n    {\n    context->lr = 0x80001018u;\n    GALAXY_ABI_CALL(0x80001014u);\n    galaxy::call_guest_resolved(services, 0x80003000u, &rmge01::fn_80003000, context, memory, 0x80001014u);\n    }\n    if (context->gpr[3] == 0u) GALAXY_GUEST_RETURN;\n    return;\n";

    fn abi_plan() -> AbiResidency {
        let sync = CallSync {
            store: crate::ppc_dataflow::abi_call_store(),
            reload: crate::ppc_dataflow::abi_results(),
        };
        AbiResidency {
            call_sites: BTreeMap::from([(0x8000_100C, sync), (0x8000_1014, sync)]),
            exit: Some(crate::ppc_dataflow::abi_exit_store()),
        }
    }

    #[test]
    fn abi_marked_call_synchronizes_the_contract_registers_only() {
        let plan = abi_plan();
        let out =
            apply_register_residency_with_abi(ABI_BODY, false, Some(&plan)).expect("eligible");
        // r0 and r12 are volatile non-arguments: not stored; r31 is only read:
        // already in the context; only the result r3 is reloaded.
        assert!(out.contains(
            "    { context->gpr[3] = gr3; context->lr = glr; rmge01::fn_80002000(context, memory, services); gr3 = context->gpr[3]; }\n"
        ), "{out}");
        assert!(!out.contains("GALAXY_ABI_CALL"), "{out}");
        // The marker before a runtime-routed call is dropped: full sync.
        assert!(out.contains(
            "    { context->gpr[0] = gr0; context->gpr[3] = gr3; context->gpr[12] = gr12; context->lr = glr; galaxy::call_guest_resolved(services, 0x80003000u, &rmge01::fn_80003000, context, memory, 0x80001014u); gr0 = context->gpr[0]; gr3 = context->gpr[3]; gr12 = context->gpr[12]; gr31 = context->gpr[31]; glr = context->lr; }\n"
        ), "{out}");
        // A guest return stores what a caller may observe; a plain return
        // stores every written local.
        assert!(
            out.contains(
                "    if (gr3 == 0u) { context->gpr[3] = gr3; context->lr = glr; return; }\n"
            ),
            "{out}"
        );
        assert!(out.contains("    { context->gpr[0] = gr0; context->gpr[3] = gr3; context->gpr[12] = gr12; context->lr = glr; return; }\n"), "{out}");
    }

    #[test]
    fn abi_markers_without_a_plan_keep_full_synchronization() {
        for plan in [None, Some(AbiResidency::default())] {
            let out = apply_register_residency_with_abi(ABI_BODY, false, plan.as_ref())
                .expect("eligible");
            assert!(out.contains(
                "    { context->gpr[0] = gr0; context->gpr[3] = gr3; context->gpr[12] = gr12; context->lr = glr; rmge01::fn_80002000(context, memory, services); gr0 = context->gpr[0]; gr3 = context->gpr[3]; gr12 = context->gpr[12]; gr31 = context->gpr[31]; glr = context->lr; }\n"
            ), "{out}");
            assert!(out.contains("    if (gr3 == 0u) { context->gpr[0] = gr0; context->gpr[3] = gr3; context->gpr[12] = gr12; context->lr = glr; return; }\n"), "{out}");
        }
    }

    #[test]
    fn abi_marker_does_not_leak_past_its_block() {
        let body = "    {\n    context->gpr[3] = context->gpr[4];\n    GALAXY_ABI_CALL(0x8000100Cu);\n    }\n    {\n    rmge01::fn_80002000(context, memory, services);\n    }\n    return;\n";
        let plan = abi_plan();
        let out = apply_register_residency_with_abi(body, false, Some(&plan)).expect("eligible");
        assert!(out.contains("    { context->gpr[3] = gr3; rmge01::fn_80002000(context, memory, services); gr3 = context->gpr[3]; gr4 = context->gpr[4]; }\n"), "{out}");
    }

    #[test]
    fn abi_call_covers_both_lanes_of_float_arguments_and_results() {
        let body = "    {\n    context->fpr_bits[1] = context->fpr_bits[31];\n    context->fpr_bits[9] = context->fpr_bits[31];\n    context->lr = 0x80001010u;\n    GALAXY_ABI_CALL(0x8000100Cu);\n    context->pc = 0x80002000u;\n    rmge01::fn_80002000(context, memory, services);\n    }\n    {\n    context->fpr_bits[30] = context->fpr_bits[1];\n    }\n    return;\n";
        let plan = abi_plan();
        let out = apply_register_residency_with_abi(body, true, Some(&plan)).expect("eligible");
        // f9 is volatile: neither stored nor reloaded; f1 goes out and comes
        // back (the body never names its ps1 lane, so that stays in context).
        assert!(out.contains("    { context->lr = glr; context->fpr_bits[1] = fpr[1]; context->fpr_bits[30] = fpr[30]; rmge01::fn_80002000(context, memory, services); fpr[1] = context->fpr_bits[1]; }\n"), "{out}");
    }

    #[test]
    fn abi_marker_covers_the_site_cached_indirect_call() {
        let body = "    {\n    context->gpr[0] = context->lr;\n    context->lr = 0x80001010u;\n    GALAXY_ABI_CALL(0x8000100Cu);\n    galaxy::fast::call_guest_indirect_site(services, branch_target, &cached_target_8000100C, &cached_function_8000100C, &site_target_8000100C, &site_function_8000100C, context, memory, 0x8000100Cu);\n    }\n    return;\n";
        let plan = abi_plan();
        let out = apply_register_residency_with_abi(body, false, Some(&plan)).expect("eligible");
        assert!(
            out.contains("{ context->lr = glr; galaxy::fast::call_guest_indirect_site("),
            "{out}"
        );
        assert!(out.contains("0x8000100Cu); }\n"), "{out}");
    }
}
