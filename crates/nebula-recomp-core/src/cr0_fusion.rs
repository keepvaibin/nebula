// CR0 record/compare to branch fusion.
//
// `cr_bit(context, k)` reads CR0 bit k. Each recording integer instruction (`andi.`,
// `add.`) and each `cmpwi`/`cmplwi` emits a CR field build and a read-modify-write of
// `context->cr`. When the next meaningful line is a CR0 branch, this pass removes that
// round trip. The C++ compiler does not remove it itself, because opaque helper calls
// keep `context->cr` in memory.
//
// The record line overwrites `context->gpr[D]`, and the branch must test `context->gpr[D]`,
// not the pre-record expression. Re-deriving the operand is wrong for a non-idempotent mask,
// so the pass reads the value the translator stored.
//
// A fusion requires, on the emitted text:
//   1. a record pair (`context->gpr[D] = EXPR;` then `galaxy::update_cr0(context,
//      context->gpr[D]);`) or a `galaxy::compare_(un)signed(context, 0u, A, B);` line;
//   2. the next meaningful line is a CR0 branch on `cr_bit(context, k)` with `k/4 == 0`
//      and `k <= 2` (bit 3 is SO = XER[SO], which is not rebuilt here);
//   3. no label, interior entry or other CR writer in between, enforced by the adjacency
//      in rule 2;
//   4. no CTR term in the branch condition, since fusing would need the CTR decrement.
//
// The rewrite keeps the guest GPR store, which the guest needs, and removes only the CR
// construction. The compare form removes the whole line, because `compare_*` writes no GPR.

/// Which CR0 bit a branch tests.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Cr0Bit {
    /// CR0[LT] -- bit 0.
    Lt,
    /// CR0[GT] -- bit 1.
    Gt,
    /// CR0[EQ] -- bit 2.
    Eq,
}

impl Cr0Bit {
    fn from_index(index: u32) -> Option<Self> {
        match index {
            0 => Some(Cr0Bit::Lt),
            1 => Some(Cr0Bit::Gt),
            2 => Some(Cr0Bit::Eq),
            _ => None,
        }
    }
}

/// Counters so callers report what happened instead of asserting a saving.
#[derive(Debug, Default, Clone, PartialEq, Eq)]
pub struct Cr0FusionStats {
    pub record_sites: usize,
    pub compare_sites: usize,
    pub declined_non_cr0_field: usize,
    pub declined_so_bit: usize,
    pub declined_ctr_condition: usize,
    pub declined_label_or_entry: usize,
    pub declined_impure_operand: usize,
    /// A few representative declined lines, for the census tool.
    /// Bounded so a large sweep cannot grow it without limit.
    pub decline_samples: Vec<String>,
}

impl Cr0FusionStats {
    pub fn fused(&self) -> usize {
        self.record_sites + self.compare_sites
    }
}

/// `galaxy::update_cr0(context, context->gpr[N]);` -> N
fn parse_update_cr0(line: &str) -> Option<u32> {
    let t = line.trim();
    let rest = t.strip_prefix("galaxy::update_cr0(context, context->gpr[")?;
    let close = rest.find(']')?;
    rest[..close].parse().ok()
}

/// `context->gpr[N] = <expr>;` -> N
fn parse_gpr_assign(line: &str) -> Option<u32> {
    let t = line.trim();
    let rest = t.strip_prefix("context->gpr[")?;
    let close = rest.find(']')?;
    let index: u32 = rest[..close].parse().ok()?;
    let after = rest[close + 1..].trim_start();
    after.strip_prefix('=')?;
    Some(index)
}

/// `galaxy::compare_signed(context, 0u, A, B);` -> (signed, A, B)
///
/// Angle brackets are not counted when splitting: a type such as `std::int32_t`
/// contains a `<` with no matching `>`.
fn parse_compare(line: &str) -> Option<(bool, String, String)> {
    let t = line.trim();
    let (signed, rest) = if let Some(r) = t.strip_prefix("galaxy::compare_signed(context,") {
        (true, r)
    } else {
        (false, t.strip_prefix("galaxy::compare_unsigned(context,")?)
    };
    let rest = rest.trim().strip_suffix(';')?;
    let (field, rest) = rest.trim().split_once(',')?;
    if field.trim().trim_end_matches('u') != "0" {
        return None;
    }
    let (left, right) = split_top_level_comma(rest.trim())?;
    Some((signed, left.trim().to_owned(), right.trim().to_owned()))
}

fn split_top_level_comma(text: &str) -> Option<(&str, &str)> {
    let mut depth = 0i32;
    for (i, c) in text.char_indices() {
        match c {
            '(' | '[' => depth += 1,
            ')' | ']' => depth -= 1,
            ',' if depth == 0 => return Some((&text[..i], &text[i + 1..])),
            _ => {}
        }
    }
    None
}

/// May this operand be re-read inside the branch condition?
///
/// It is already read by the emitted line, so re-reading `context->gpr[]` is
/// fine. A call is not: the pass cannot prove a callee is pure or unordered.
fn operand_is_rereadable(expr: &str) -> bool {
    if expr.contains("galaxy::") || expr.contains("rmge01::") {
        return false;
    }
    if expr.contains('=') || expr.contains("++") || expr.contains("--") {
        return false;
    }
    true
}

/// Parse `... galaxy::cr_bit(context, Ku) ...` -> (K, negated)
fn parse_cr_bit_index(line: &str) -> Option<(u32, bool)> {
    const NEEDLE: &str = "galaxy::cr_bit(context,";
    let start = line.find(NEEDLE)?;
    let rest = line[start + NEEDLE.len()..].trim_start();
    let digits: String = rest.chars().take_while(|c| c.is_ascii_digit()).collect();
    let index: u32 = digits.parse().ok()?;
    let negated = line[..start].trim_end().ends_with('!');
    Some((index, negated))
}

/// Replace the CR term of an emitted branch line with `replacement`.
///
/// The emitted shape is `if ((true) && (COND)) goto ...;`, sometimes wrapped in a
/// `{ ... }` block on one line. Anything that does not match is returned
/// unchanged, and the caller must treat that as a decline.
fn rewrite_branch_condition(line: &str, replacement: &str) -> Option<String> {
    let marker = "(true) && (";
    let pos = line.find(marker)?;
    let tail_start = pos + marker.len();
    let tail = &line[tail_start..];
    // Find the matching close of the outer `(` that began at `pos`.
    let mut depth = 1i32;
    let mut end = None;
    for (i, c) in tail.char_indices() {
        match c {
            '(' => depth += 1,
            ')' => {
                depth -= 1;
                if depth == 0 {
                    end = Some(i);
                    break;
                }
            }
            _ => {}
        }
    }
    let end = end?;
    Some(format!(
        "{}({}){}",
        &line[..pos],
        replacement,
        &tail[end + 1..]
    ))
}

/// Fuse every CR0 record/compare with its adjacent branch in one lowered body.
///
/// The scan is branch-first: looking back from the branch keeps every line to rewrite
/// at or before the cursor.
///
/// Returns the rewritten body and counters. Any site the pass is not certain about is
/// left unchanged.
pub fn fuse_cr0_branches(body: &str) -> (String, Cr0FusionStats) {
    let mut stats = Cr0FusionStats::default();
    let trailing_newline = body.ends_with('\n');
    let mut lines: Vec<String> = body.split('\n').map(|s| s.to_owned()).collect();
    if trailing_newline {
        lines.pop();
    }

    for i in 0..lines.len() {
        // Only consider lines that actually consume CR0.
        let trimmed = lines[i].trim();
        if !trimmed.contains("cr_bit(context,") {
            continue;
        }
        let Some((index, negated)) = parse_cr_bit_index(trimmed) else {
            continue;
        };
        if index / 4 != 0 {
            stats.declined_non_cr0_field += 1;
            continue;
        }
        let Some(bit) = Cr0Bit::from_index(index) else {
            // index == 3 is CR0[SO] = XER[SO], which a fused test cannot rebuild.
            stats.declined_so_bit += 1;
            continue;
        };
        if trimmed.contains("context->ctr") {
            stats.declined_ctr_condition += 1;
            continue;
        }

        // Walk back to the nearest meaningful line: that must be the record.
        let Some(prev) = previous_meaningful(&lines, i) else {
            continue;
        };

        if let Some((signed, left, right)) = parse_compare(&lines[prev]) {
            if !operand_is_rereadable(&left) || !operand_is_rereadable(&right) {
                stats.declined_impure_operand += 1;
                continue;
            }
            let predicate = compare_predicate(bit, &left, &right, signed, negated);
            let Some(rewritten) = rewrite_branch_condition(&lines[i], &predicate) else {
                continue;
            };
            // The compare writes no GPR, so its line can go. Blank it rather than
            // delete it so every other source position stays stable.
            let indent = leading_whitespace(&lines[prev]).to_owned();
            lines[prev] = format!("{indent}// cr0-fused: compare folded into the branch");
            lines[i] = rewritten;
            stats.compare_sites += 1;
            continue;
        }

        if let Some(dst) = parse_update_cr0(&lines[prev]) {
            // The record is the pair `gpr[D] = EXPR;` / `update_cr0(.., gpr[D]);`,
            // so require exactly that adjacency.
            let Some(assign_idx) = prev.checked_sub(1) else {
                continue;
            };
            let Some(assigned) = parse_gpr_assign(&lines[assign_idx]) else {
                continue;
            };
            if assigned != dst {
                continue;
            }
            // Name the value the translator recorded. The branch tests `context->gpr[D]`, not the
            // pre-record operand, which is wrong for any non-idempotent mask.
            let name = format!("cr0_fused_{dst}");
            let predicate = record_predicate(bit, &name, negated);
            let Some(rewritten) = rewrite_branch_condition(&lines[i], &predicate) else {
                continue;
            };
            let indent = leading_whitespace(&lines[prev]).to_owned();
            lines[prev] = format!("{indent}const std::uint32_t {name} = context->gpr[{dst}];");
            lines[i] = rewritten;
            stats.record_sites += 1;
            continue;
        }

        // A `{` / `}` or a label is expected when the CR0 read is not fed by a fusible record:
        // an interior branch target, a value carried across a call, or a record whose GPR is
        // named differently.
        if stats.decline_samples.len() < 8 {
            stats.decline_samples.push(format!(
                "no adjacent record before: {}",
                lines[prev].trim().chars().take(72).collect::<String>()
            ));
        }
        continue;
    }

    let mut out = lines.join("\n");
    if trailing_newline {
        out.push('\n');
    }
    (out, stats)
}

/// The nearest preceding line that is not blank and not a brace.
fn previous_meaningful(lines: &[String], from: usize) -> Option<usize> {
    let mut j = from;
    while j > 0 {
        j -= 1;
        let t = lines[j].trim();
        if t.is_empty() || t == "{" || t == "}" {
            continue;
        }
        return Some(j);
    }
    None
}

fn leading_whitespace(line: &str) -> &str {
    let end = line.len() - line.trim_start().len();
    &line[..end]
}

fn record_predicate(bit: Cr0Bit, value: &str, negated: bool) -> String {
    let base = match bit {
        Cr0Bit::Eq => format!("{value} == 0u"),
        Cr0Bit::Lt => format!("static_cast<std::int32_t>({value}) < 0"),
        Cr0Bit::Gt => format!("static_cast<std::int32_t>({value}) > 0"),
    };
    if negated {
        format!("!({base})")
    } else {
        base
    }
}

fn compare_predicate(bit: Cr0Bit, left: &str, right: &str, signed: bool, negated: bool) -> String {
    let base = if signed {
        let l = format!("static_cast<std::int32_t>({left})");
        let r = format!("static_cast<std::int32_t>({right})");
        match bit {
            Cr0Bit::Eq => format!("{l} == {r}"),
            Cr0Bit::Lt => format!("{l} < {r}"),
            Cr0Bit::Gt => format!("{l} > {r}"),
        }
    } else {
        match bit {
            Cr0Bit::Eq => format!("{left} == {right}"),
            Cr0Bit::Lt => format!("{left} < {right}"),
            Cr0Bit::Gt => format!("{left} > {right}"),
        }
    };
    if negated {
        format!("!({base})")
    } else {
        base
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The emitted record form.
    fn emitted_record() -> String {
        [
            "    {",
            "    context->gpr[5] = context->gpr[3] & 0x00008000u;",
            "    galaxy::update_cr0(context, context->gpr[5]);",
            "    }",
            "    {",
            "    if ((true) && (galaxy::cr_bit(context, 2u))) goto label_80004180;",
            "    }",
            "",
        ]
        .join("\n")
    }

    /// The emitted compare form.
    fn emitted_compare() -> String {
        [
            "    {",
            "    galaxy::compare_unsigned(context, 0u, context->gpr[3], 0x00000001u);",
            "    }",
            "    {",
            "    if ((true) && (!galaxy::cr_bit(context, 2u))) goto label_80004184;",
            "    }",
            "",
        ]
        .join("\n")
    }

    #[test]
    fn record_form_fuses_and_drops_update_cr0() {
        let (out, stats) = fuse_cr0_branches(&emitted_record());
        assert_eq!(
            stats.record_sites, 1,
            "expected one record fusion: {stats:?}"
        );
        assert_eq!(stats.compare_sites, 0);
        assert!(
            !out.contains("update_cr0"),
            "the CR write must be gone:\n{out}"
        );
        // The guest GPR store is architecturally required and must survive.
        assert!(
            out.contains("context->gpr[5] = context->gpr[3] & 0x00008000u;"),
            "the record store must survive:\n{out}"
        );
        // The branch tests the recorded value, named in a temporary. The check is on the
        // semantics: the tested operand is whatever `context->gpr[D]` held after the record store.
        assert!(
            out.contains("const std::uint32_t cr0_fused_5 = context->gpr[5];"),
            "the recorded value must be named:\n{out}"
        );
        assert!(
            out.contains("cr0_fused_5 == 0u"),
            "branch must test the recorded value:\n{out}"
        );
        assert!(
            !out.contains("galaxy::cr_bit"),
            "no CR read may remain at a fused site:\n{out}"
        );
    }

    /// For `andi. r5,r3,0x8000` the guest GPR holds the masked value, so the branch tests
    /// `gpr[5]`, not `gpr[3] & 0x8000`. Re-deriving the operand is wrong whenever the mask
    /// is not idempotent, and a probe input whose mask is idempotent would not show it.
    #[test]
    fn must_not_retest_the_pre_record_operand() {
        let (out, _) = fuse_cr0_branches(&emitted_record());
        assert!(
            !out.contains("context->gpr[3] & 0x00008000u) == 0u"),
            "fused condition re-derives the pre-record operand instead of \
             testing the recorded value:\n{out}"
        );
    }

    #[test]
    fn compare_form_folds_both_operands() {
        let (out, stats) = fuse_cr0_branches(&emitted_compare());
        assert_eq!(
            stats.compare_sites, 1,
            "expected one compare fusion: {stats:?}"
        );
        assert!(
            !out.contains("compare_unsigned("),
            "the compare line must be folded away:\n{out}"
        );
        // Negated EQ: `cmplwi r3,1` then `bc 4` (branch if NOT equal).
        assert!(
            out.contains("!(context->gpr[3] == 0x00000001u)"),
            "expected the negated equality test:\n{out}"
        );
        assert!(
            !out.contains("galaxy::cr_bit"),
            "no CR read may remain:\n{out}"
        );
    }

    #[test]
    fn declines_a_cr_field_other_than_cr0() {
        // cr_bit(context, 6u) is CR1 bit 2, not CR0.
        let body = emitted_record().replace("cr_bit(context, 2u)", "cr_bit(context, 6u)");
        let (out, stats) = fuse_cr0_branches(&body);
        assert_eq!(stats.fused(), 0);
        assert!(stats.declined_non_cr0_field >= 1);
        assert_eq!(out, body, "a declined body must be returned unchanged");
    }

    #[test]
    fn declines_the_so_bit() {
        // cr_bit index 3 is CR0[SO] = XER[SO], which a fused test cannot rebuild.
        let body = emitted_record().replace("cr_bit(context, 2u)", "cr_bit(context, 3u)");
        let (out, stats) = fuse_cr0_branches(&body);
        assert_eq!(stats.fused(), 0);
        assert!(stats.declined_so_bit >= 1);
        assert_eq!(out, body);
    }

    #[test]
    fn declines_a_label_between_record_and_branch() {
        // A label between the record and the CR0 read means the read may not come from that
        // record: the branch can be an interior entry reached with CR0 set elsewhere. Without
        // an adjacent record the pass declines.
        let body = emitted_record().replace(
            "    if ((true) && (galaxy::cr_bit(context, 2u)))",
            "label_80004180:\n    if ((true) && (galaxy::cr_bit(context, 2u)))",
        );
        let (out, stats) = fuse_cr0_branches(&body);
        assert_eq!(stats.fused(), 0, "must not fuse across a label: {stats:?}");
        assert_eq!(out, body, "a declined body must be returned unchanged");
    }

    #[test]
    fn declines_a_ctr_condition_on_the_branch() {
        let body = emitted_record().replace(
            "if ((true) && (galaxy::cr_bit(context, 2u)))",
            "if ((--context->ctr != 0u) && (galaxy::cr_bit(context, 2u)))",
        );
        let (out, stats) = fuse_cr0_branches(&body);
        assert_eq!(stats.fused(), 0);
        assert!(stats.declined_ctr_condition >= 1);
        assert_eq!(out, body);
    }

    #[test]
    fn declines_a_compare_with_impure_operands() {
        let body = replaced_compare(
            "galaxy::compare_unsigned(context, 0u, galaxy::guest_load_u32(memory, context->gpr[4], services, 0x80001000u), 0x1u);",
        );
        let (out, stats) = fuse_cr0_branches(&body);
        assert_eq!(stats.fused(), 0);
        assert!(stats.declined_impure_operand >= 1);
        assert_eq!(out, body);
    }

    #[test]
    fn declines_a_nonzero_compare_field() {
        // Field 1 is CR1; the pass only handles CR0.
        let body =
            replaced_compare("galaxy::compare_unsigned(context, 1u, context->gpr[3], 0x1u);");
        let (out, stats) = fuse_cr0_branches(&body);
        assert_eq!(stats.fused(), 0);
        assert_eq!(out, body);
    }

    #[test]
    fn declines_a_mismatched_assignment_before_update_cr0() {
        // `update_cr0` names r5 but the previous line assigns r6: not a record pair.
        let body = emitted_record().replace(
            "context->gpr[5] = context->gpr[3] & 0x00008000u;",
            "context->gpr[6] = context->gpr[3] & 0x00008000u;",
        );
        let (out, stats) = fuse_cr0_branches(&body);
        assert_eq!(stats.fused(), 0);
        assert_eq!(out, body);
    }

    #[test]
    fn leaves_an_unrelated_body_untouched() {
        let body = "    {\n    context->gpr[3] = 0x1u;\n    }\n    {\n    return;\n    }\n";
        let (out, stats) = fuse_cr0_branches(body);
        assert_eq!(stats.fused(), 0);
        assert_eq!(out, body);
    }

    #[test]
    fn lt_and_gt_bits_use_signed_tests() {
        for (index, needle) in [("0u", "< 0"), ("1u", "> 0")] {
            let body = emitted_record()
                .replace("cr_bit(context, 2u)", &format!("cr_bit(context, {index})"));
            let (out, stats) = fuse_cr0_branches(&body);
            assert_eq!(stats.record_sites, 1, "index {index}: {stats:?}");
            assert!(
                out.contains(needle),
                "index {index} should produce a signed test containing {needle}:\n{out}"
            );
        }
    }

    #[test]
    fn preserves_line_count_and_trailing_newline() {
        let body = emitted_record();
        let before = body.lines().count();
        let (out, _) = fuse_cr0_branches(&body);
        assert_eq!(out.lines().count(), before, "line count must be stable");
        assert!(out.ends_with('\n'), "trailing newline must be preserved");
        assert_eq!(out.matches('\n').count(), body.matches('\n').count());
    }

    fn replaced_compare(compare_line: &str) -> String {
        emitted_compare().replace(
            "galaxy::compare_unsigned(context, 0u, context->gpr[3], 0x00000001u);",
            compare_line,
        )
    }
}
