//! Agent 20, developer-only: run `cr0_fusion` on ONE real generated function and
//! print the before/after so the pass can be inspected against actual emission
//! rather than only against hand-written shapes.
//!
//! Usage: cr0_fusion_one <shard.cpp> <fnName>

use nebula_recomp_core::{fuse_cr0_branches, Cr0FusionStats};
use std::fs;

fn main() {
    let mut args = std::env::args().skip(1);
    let path = args.next().expect("shard path");
    let want = args.next().expect("fn name");
    let text = fs::read_to_string(&path).expect("read shard");

    // Extract the named function body: from `void <name>(` to the next top-level
    // `void fn_` or end of file.
    let start = text
        .find(&format!("void {want}("))
        .unwrap_or_else(|| panic!("{want} not found in {path}"));
    let after = &text[start + 5..];
    let end = after
        .find("\nvoid fn_")
        .map(|o| start + 5 + o)
        .unwrap_or(text.len());
    let body = &text[start..end];

    let (out, stats) = fuse_cr0_branches(body);
    print_stats(&stats);
    println!();
    println!("=== cr_bit / update_cr0 BEFORE: {} / {} ===",
        body.matches("galaxy::cr_bit(").count(),
        body.matches("galaxy::update_cr0(").count());
    println!("=== cr_bit / update_cr0 AFTER : {} / {} ===",
        out.matches("galaxy::cr_bit(").count(),
        out.matches("galaxy::update_cr0(").count());
    println!("=== compare_* BEFORE/AFTER   : {} / {} ===",
        body.matches("galaxy::compare_").count(),
        out.matches("galaxy::compare_").count());
    println!("=== line count BEFORE/AFTER  : {} / {} ===",
        body.lines().count(), out.lines().count());
    println!();
    // Show the first fusion site in context.
    if let Some(pos) = out.find("// cr0-fused:") {
        let lo = out[..pos].rfind('\n').map(|i| i + 1).unwrap_or(0);
        let hi = out[pos..].find('\n').map(|i| pos + i).unwrap_or(out.len());
        println!("=== first compare fusion ===");
        println!("{}", &out[lo..hi]);
    }
    if let Some(pos) = out.find("cr0_fused_") {
        let lo = out[..pos].rfind('\n').map(|i| i + 1).unwrap_or(0);
        let hi = out[pos..].find('\n').map(|i| pos + i).unwrap_or(out.len());
        println!("=== first record fusion ===");
        println!("{}", &out[lo..hi]);
    }
}

fn print_stats(stats: &Cr0FusionStats) {
    println!("record_sites          : {}", stats.record_sites);
    println!("compare_sites         : {}", stats.compare_sites);
    println!("declined_non_cr0_field: {}", stats.declined_non_cr0_field);
    println!("declined_so_bit       : {}", stats.declined_so_bit);
    println!("declined_ctr_condition: {}", stats.declined_ctr_condition);
    println!("declined_impure_operand: {}", stats.declined_impure_operand);
    println!("decline samples ({}):", stats.decline_samples.len());
    for s in &stats.decline_samples {
        println!("   {s}");
    }
}
