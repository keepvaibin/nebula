//! Developer-only measurement of the generated-code register-residency prize.
//!
//! Two independent residency mechanisms exist. As of `GENERATION_VERSION` 3 the
//! shipped release options turn exactly one of them on:
//!
//! * `guest_resident_leaf` — two hand-written lowering tiers selected by
//!   `analyze_resident_integer_leaf`, covering a narrow opcode whitelist.
//!   **Off in the release options.**
//! * `guest_resident_integer` — `apply_integer_gpr_residency`, a post-pass over
//!   any already-lowered body that keeps directly-indexed GPRs in locals.
//!   **On in the release options** (`generate.rs::release_module_options`), which
//!   is why the `SHIPPED configuration` row below is expected to report a large
//!   `resident-fns` count rather than zero. Its `resident-fns` is therefore the
//!   coverage of the shipped mechanism, and the `shipped without residency` row is
//!   the pre-v3 baseline that the supplied recordings were generated from.
//!
//! This tool translates the whole module once per configuration and reports how
//! many functions each mechanism actually reaches, plus generated-source deltas.
//! That is the number that decides how much the shipped residency is worth; it
//! needs no build of the game and no game launch.
//!
//! Usage:
//!   cargo run --release -p nebula-recomp-core --bin residency_census -- <game-data-dir>
//!
//! It reads the user's extracted game directory and writes nothing.

use nebula_recomp_core::{translate_module_with_options, ModuleTranslationOptions, TranslationModule};
use std::collections::BTreeMap;
use std::env;
use std::path::Path;

/// Shard parameters used by the installed release module.
const SHARD_SIZE: usize = 64;
const SHARD_SOURCE_KIB: usize = 512;

/// Emitted exactly once at the top of every body produced by
/// `lower_resident_integer_direct_calls`. The string occurs exactly once in the
/// whole crate, inside that emitter, so this count cannot pick up a test fixture
/// or a second emitter.
///
/// Scope limit: the other leaf tier, `lower_resident_integer_leaf`, emits no
/// marker, so this is formally a lower bound on `guest_resident_leaf` coverage.
/// It runs first and is much narrower than the direct-call tier, so almost every
/// `guest_resident_leaf` body carries the marker.
const LEAF_CONTRACT_MARKER: &str = "galaxy-resident-direct-call-contract-v1";

/// The post-pass declares one local per register it names, as
/// `std::uint32_t resident_rN = context->gpr[N];`, and the interior-tier lowerer
/// emits the identical declaration. This pattern is therefore the union of the
/// two mechanisms; use it for the combined figure only.
const RESIDENT_LOCAL_DECLARATION: &str = "std::uint32_t resident_r";

/// Label for the configuration `release_module_options()` actually builds.
const SHIPPED_LABEL: &str = "SHIPPED configuration";

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let game = env::args()
        .nth(1)
        .unwrap_or_else(|| ".".to_owned());
    let game = Path::new(&game);

    // The option set `nebula-recomp-cli/src/generate.rs::release_module_options()`
    // builds. Keep this in step with that function: if it drifts, every comparison
    // below is against a configuration nobody ships. As of this writing it sets
    // `guest_resident_integer: true` and leaves `guest_resident_leaf` off.
    let shipped = ModuleTranslationOptions {
        inline_scalar_single_binary: true,
        exact_psmtx_local_lanes: true,
        guest_resident_integer: true,
        ..ModuleTranslationOptions::default()
    };

    let configurations: [(&str, ModuleTranslationOptions); 4] = [
        (SHIPPED_LABEL, shipped),
        (
            "shipped without residency",
            ModuleTranslationOptions {
                guest_resident_integer: false,
                ..shipped
            },
        ),
        (
            "shipped + leaf tier",
            ModuleTranslationOptions {
                guest_resident_leaf: true,
                ..shipped
            },
        ),
        (
            "both post-pass and leaf tier off",
            ModuleTranslationOptions {
                guest_resident_integer: false,
                guest_resident_leaf: false,
                ..shipped
            },
        ),
    ];

    let mut results: Vec<(&str, TranslationModule)> = Vec::new();
    for (label, options) in configurations {
        println!("translating with: {label} ...");
        let module =
            translate_module_with_options(game, 0, SHARD_SIZE, SHARD_SOURCE_KIB, options)?;
        results.push((label, module));
    }

    println!();
    println!("== configuration comparison ==");
    println!(
        "{:<28} {:>12} {:>12} {:>10} {:>12}",
        "options", "resident-fns", "contract-fns", "source KiB", "vs shipped"
    );
    let shipped_bytes = total_bytes(&results[0].1) as i64;
    for (label, module) in &results {
        let bytes = total_bytes(module) as i64;
        println!(
            "{:<28} {:>12} {:>12} {:>10} {:>11.2}%",
            label,
            count_functions_with(&module, RESIDENT_LOCAL_DECLARATION),
            count_marker(&module, LEAF_CONTRACT_MARKER),
            bytes / 1024,
            percent_delta(bytes - shipped_bytes, shipped_bytes)
        );
    }

    println!();
    println!(
        "  resident-fns is an exact count of bodies declaring at least one\n  \
         `std::uint32_t resident_rN = context->gpr[N];` local, which is the union of the\n  \
         post-pass and the interior leaf tier. contract-fns counts only the narrower leaf\n  \
         direct-call contract marker, so it is 0 unless `guest_resident_leaf` is on.\n  \
         `vs shipped` is relative to the first row, which is the real release options set."
    );

    // Per-translation-unit churn, against the shipped configuration.
    let best = results
        .iter()
        .max_by_key(|(_, module)| {
            count_functions_with(module, RESIDENT_LOCAL_DECLARATION)
        })
        .map(|(label, _)| *label)
        .unwrap_or(SHIPPED_LABEL);
    println!();
    println!("== translation-unit size churn: {SHIPPED_LABEL} vs \"{best}\" ==");
    let baseline_files: BTreeMap<&str, usize> = results[0]
        .1
        .source_files
        .iter()
        .map(|file| (file.name.as_str(), file.contents.len()))
        .collect();
    let best_module = &results
        .iter()
        .find(|(label, _)| *label == best)
        .expect("best configuration is in the results")
        .1;
    let mut shrank = 0usize;
    let mut grew = 0usize;
    let mut same = 0usize;
    for file in &best_module.source_files {
        match baseline_files.get(file.name.as_str()) {
            Some(before) if file.contents.len() < *before => shrank += 1,
            Some(before) if file.contents.len() > *before => grew += 1,
            Some(_) => same += 1,
            None => {}
        }
    }
    println!(
        "  {} units: {shrank} shrank, {grew} grew, {same} identical",
        shrank + grew + same
    );
    println!();
    println!(
        "  NOTE: source size is a proxy, not host instruction count. A unit can grow\n  \
         while the functions in it get faster, because moving a `context->gpr[N]` load\n  \
         to the prologue adds a line while removing one per use. The decisive numbers\n  \
         are coverage (above, exact) and host instructions per guest instruction\n  \
         (needs a build)."
    );
    Ok(())
}

fn total_bytes(module: &TranslationModule) -> usize {
    module
        .source_files
        .iter()
        .map(|file| file.contents.len())
        .sum()
}

/// Counts generated function bodies that use the resident path, by splitting each
/// translation unit on the generated function signature.
///
/// Every generated definition ends its signature with `... services) {` at the
/// start of a line, and the sharded source contains nothing else with that shape.
/// Splitting on it gives exact body boundaries, so this is an exact body count
/// rather than a guess about declaration placement. The per-function variant with
/// a renamed symbol keeps the same signature, which is fine: it is still one body.
fn count_functions_with(module: &TranslationModule, needle: &str) -> u64 {
    const SIGNATURE_TAIL: &str = "services) {";
    let mut count = 0u64;
    for file in &module.source_files {
        // Every segment after a signature tail is one function body, up to the
        // next signature tail. The text before the first tail is the shard
        // preamble and is skipped.
        let mut cursor = 0usize;
        while let Some(found) = file.contents[cursor..].find(SIGNATURE_TAIL) {
            let body_start = cursor + found + SIGNATURE_TAIL.len();
            let body_end = file.contents[body_start..]
                .find(SIGNATURE_TAIL)
                .map_or(file.contents.len(), |offset| body_start + offset);
            if file.contents[body_start..body_end].contains(needle) {
                count += 1;
            }
            cursor = body_start;
        }
    }
    count
}

/// Counts non-overlapping occurrences of a marker string.
fn count_marker(module: &TranslationModule, marker: &str) -> u64 {
    let mut count = 0u64;
    for file in &module.source_files {
        let mut rest = file.contents.as_str();
        while let Some(found) = rest.find(marker) {
            count += 1;
            rest = &rest[found + marker.len()..];
        }
    }
    count
}

fn percent_delta(delta: i64, base: i64) -> f64 {
    if base == 0 {
        0.0
    } else {
        delta as f64 * 100.0 / base as f64
    }
}
