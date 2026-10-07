//! Agent 20, developer-only: run `cr0_fusion` over real generated shards.
//!
//! The unit tests in `cr0_fusion.rs` cover the shapes I could reproduce by hand.
//! This runs the pass over the actual emitted corpus and reports what it fuses,
//! what it declines and why — the numbers a reviewer needs to decide whether the
//! pass is safe to wire into the lowering loop.
//!
//! It does NOT compile the rewritten text; that is the next step and needs a
//! shard in front of clang. What it does prove is that the pass terminates, does
//! not corrupt line structure, and reports honest decline counts on real input.
//!
//! Usage: cr0_fusion_census <generated-game-dir> [shard-limit]

use nebula_recomp_core::{fuse_cr0_branches, Cr0FusionStats};
use std::fs;
use std::path::Path;

fn main() {
    let mut args = std::env::args().skip(1);
    let root = args
        .next()
        .unwrap_or_else(|| r"D:\NebulaWork\gen3\game".to_owned());
    let limit: usize = args.next().and_then(|v| v.parse().ok()).unwrap_or(usize::MAX);
    let root = Path::new(&root);

    let mut shards: Vec<_> = fs::read_dir(root)
        .expect("read generated game dir")
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .filter(|p| {
            p.file_name()
                .and_then(|n| n.to_str())
                .is_some_and(|n| n.starts_with("functions_") && n.ends_with(".cpp"))
        })
        .collect();
    shards.sort();
    shards.truncate(limit);

    let mut total = Cr0FusionStats::default();
    let mut changed_shards = 0usize;
    let mut line_count_mismatch = 0usize;
    let mut trailing_mismatch = 0usize;
    let mut cr_bit_remaining = 0usize;
    let mut update_cr0_remaining = 0usize;

    for path in &shards {
        let Ok(text) = fs::read_to_string(path) else {
            continue;
        };
        let (out, stats) = fuse_cr0_branches(&text);
        let Cr0FusionStats {
            record_sites,
            compare_sites,
            declined_non_cr0_field,
            declined_so_bit,
            declined_ctr_condition,
            declined_label_or_entry,
            declined_impure_operand,
            decline_samples,
        } = stats;
        total.record_sites += record_sites;
        total.compare_sites += compare_sites;
        total.declined_non_cr0_field += declined_non_cr0_field;
        total.declined_so_bit += declined_so_bit;
        total.declined_ctr_condition += declined_ctr_condition;
        total.declined_label_or_entry += declined_label_or_entry;
        total.declined_impure_operand += declined_impure_operand;
        for sample in decline_samples {
            if total.decline_samples.len() < 12 {
                total.decline_samples.push(sample);
            }
        }

        if record_sites + compare_sites > 0 {
            changed_shards += 1;
        }
        // Structural invariants that must hold for the output to stay valid C++
        // at the same source positions.
        if out.lines().count() != text.lines().count() {
            line_count_mismatch += 1;
        }
        if out.ends_with('\n') != text.ends_with('\n') {
            trailing_mismatch += 1;
        }
        cr_bit_remaining += out.matches("galaxy::cr_bit(").count();
        update_cr0_remaining += out.matches("galaxy::update_cr0(").count();
    }

    let (_, before) = shards
        .first()
        .and_then(|p| fs::read_to_string(p).ok())
        .map(|t| (0usize, t.matches("galaxy::cr_bit(").count()))
        .unwrap_or((0, 0));
    let _ = before;

    println!("shards scanned                : {}", shards.len());
    println!("shards with >=1 fusion        : {changed_shards}");
    println!();
    println!("FUSED");
    println!("  record form  (update_cr0)   : {}", total.record_sites);
    println!("  compare form (compare_*)    : {}", total.compare_sites);
    println!("  total                       : {}", total.fused());
    println!();
    println!("DECLINED, by reason");
    println!("  CR field other than CR0     : {}", total.declined_non_cr0_field);
    println!("  SO bit (index 3)            : {}", total.declined_so_bit);
    println!("  CTR term in the branch      : {}", total.declined_ctr_condition);
    println!("  label/entry between the two : {}", total.declined_label_or_entry);
    println!("  impure compare operand      : {}", total.declined_impure_operand);
    println!();
    println!("WHY THE WINDOW CLOSED (representative lines)");
    for sample in &total.decline_samples {
        println!("  {sample}");
    }
    println!();
    println!("STRUCTURAL CHECKS on the rewritten text");
    println!("  shards whose line count changed   : {line_count_mismatch}  (must be 0)");
    println!("  shards whose trailing NL changed  : {trailing_mismatch}  (must be 0)");
    println!("  galaxy::cr_bit( occurrences left  : {cr_bit_remaining}");
    println!("  galaxy::update_cr0( occurrences   : {update_cr0_remaining}");
    println!();
    if line_count_mismatch == 0 && trailing_mismatch == 0 {
        println!("VERDICT: pass terminates and preserves line structure on real emission.");
    } else {
        println!("VERDICT: STRUCTURAL FAILURE - the rewritten text is not line-compatible.");
    }
}
