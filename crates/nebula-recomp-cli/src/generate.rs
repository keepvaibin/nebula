//! One-shot generation of every game-derived source a Nebula install needs.
//!
//! The installer runs `nebula-recomp generate` once per installation (and
//! again only when generation inputs change). Everything written here is
//! derived from the user's own game and stays on the user's machine.

use crate::{
    dsp_cfg_entry_points, generate_home_button_sidecar, generate_module_sources,
    require_clean_function_audit, splice_irom_words, write_generated_text_create_new,
};
use anyhow::{bail, Context, Result};
use nebula_recomp_core::{
    analyze_input, lower_dsp_program_from_entry_vectors_with_static_memory_and_raw_provenance,
    read_rmge01_ax_ucode, translation_coverage, DspStaticMemoryImages, ModuleTranslationOptions,
    DSP_COEF_WORDS, DSP_IROM_WORDS, RMGE01_DOL_SHA1, RMGE01_GAME_ID,
};
use serde_json::json;
use sha2::{Digest, Sha256};
use std::{fs, path::Path};

/// Bump whenever generated source for the same game changes, so installers
/// know that previously compiled modules can no longer be reused.
///
/// 3: straight-line integer GPR residency was enabled for the release module.
///    It has since been measured net-negative and turned **off** again
///    (`AgentWork/agent-23/06-residency-measured-negative.md`), so the release
///    module is byte-identical to a pre-3 build for every body that was
///    eligible. The version is deliberately **not** reverted: existing v3
///    modules are valid, and moving the number would invalidate compiled
///    artefacts and caches for no functional reason.
// 4: proved PSMTX RAM/lane reuse; existing modules remain ABI-compatible but
// installers must regenerate to receive the corrected lowering.
// 5: clang-cl contraction policy is forwarded instead of silently ignored.
// 9: paired estimates use the corrected table/status boundary; regenerate modules.
// 10: call-return policy and paired lane-fact corrections; regenerate modules.
// 11: checked multi-register RAM helpers; regenerate affected modules.
pub const GENERATION_VERSION: u32 = 11;

/// Shard size and per-shard source budget of the qualified module build.
pub const MODULE_SHARD_SIZE: usize = 64;
pub const MODULE_SHARD_SOURCE_KIB: usize = 512;

/// Entry symbol the native runtime expects for the lowered AX ucode.
pub const DSP_ENTRY_FUNCTION: &str = "galaxy_rmge01_dsp_entry";

const DSP_IROM: &[u8] = include_bytes!("../../../third_party/dolphin-free-dsp-rom/dsp_rom.bin");
const DSP_COEF: &[u8] = include_bytes!("../../../third_party/dolphin-free-dsp-rom/dsp_coef.bin");

/// Translation options of the qualified release module: inline widened-single
/// scalar arithmetic and exact PSMTX local lanes; integer residency and every
/// trace/profile remain off.
pub fn release_module_options() -> ModuleTranslationOptions {
    ModuleTranslationOptions {
        inline_scalar_single_binary: true,
        exact_psmtx_local_lanes: true,
        // General text-based GPR residency remains disabled. The prior emitted-
        // source census added 2.73 context accesses per removed access; this is
        // not a linked-code or gameplay benchmark. The CPU audit also found
        // conditional-return and ordinary FP-label writeback counterexamples.
        // Keep this pass off. Broader residency needs typed effect/entry/exit
        // proofs, not a global switch or a claim that the current pass is correct.
        // See AgentWork/agent-23/06-residency-measured-negative.md and the
        // Agent 1 CPU audit findings N21/N22/N89.
        guest_resident_integer: false,
        ..ModuleTranslationOptions::default()
    }
}

/// One build graph for everything Setup compiles. The two largest single
/// files (Home Menu, DSP) come first so they start first.
const BUILD_CMAKE: &str = r#"cmake_minimum_required(VERSION 3.24)
project(NebulaGenerated LANGUAGES CXX)
add_subdirectory(home)
add_subdirectory(dsp)
add_subdirectory(game)
"#;

/// RMGE01_dsp.dll: the lowered AX ucode, loaded by the prebuilt runtime.
const DSP_CMAKE: &str = r#"cmake_minimum_required(VERSION 3.24)
project(RMGE01NativeDsp LANGUAGES CXX)
if(NOT DEFINED GALAXY_RUNTIME_INCLUDE OR NOT DEFINED GALAXY_DSP_ALU_LIBRARY)
    message(FATAL_ERROR "Set the Nebula runtime include and DSP ALU library")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(RMGE01_dsp SHARED rmge01_dsp.cpp)
target_include_directories(RMGE01_dsp PRIVATE "${GALAXY_RUNTIME_INCLUDE}")
target_link_libraries(RMGE01_dsp PRIVATE "${GALAXY_DSP_ALU_LIBRARY}")
set_target_properties(RMGE01_dsp PROPERTIES PREFIX "")
if(MSVC)
    target_compile_options(RMGE01_dsp PRIVATE /EHsc /GS- /GR-)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        # This is the DSP's hot loop: the generated function is one giant
        # straight-line state machine that retires ~11.2 M instructions/s, i.e.
        # the whole ~1.5 k-instruction ucode program re-executes thousands of
        # times per second, and it is 0.19 s of every second of process CPU on
        # the reference machine.
        #
        # /O1 (= -Os) was chosen for build time, but clang-cl keeps the MINIMUM
        # of all -O flags, so /O1 here also cancels the -O3 that
        # CMAKE_CXX_FLAGS_RELEASE supplies for -DCMAKE_BUILD_TYPE=Release.
        # Measured with the pinned toolchain on the generated 232 k-line TU:
        #   /O1 -> object 4 214 901 B, compile 36 s
        #   /O2 -> object 8 054 664 B, compile 5 min 12 s
        # A 1.91x object for one translation unit is the signature of an
        # optimiser that is now actually working: register allocation and
        # cross-block redundancy elimination inside the giant function, plus
        # inlining of the small helpers defined in dsp_context.h, none of which
        # fold into the dispatch loop at -Os.
        #
        # CORRECTION: this comment previously claimed that /O2 also inlines the
        # ~1.05 k galaxy::dsp_accumulator_* / dsp_product_* / dsp_condition_holds
        # call sites in the generated code. That is WRONG and the claim has been
        # removed. Those functions are *defined* in runtime/src/dsp_alu.cpp
        # (dsp_alu.cpp:78, :207, :226, :252, :261, :267, :277) and have ZERO
        # inline definitions in runtime/include/galaxy/dsp_alu.h - checked. They
        # are compiled into galaxy_dsp_alu, a separate STATIC library
        # (CMakeLists.txt:256-263, with no -flto), so no /O level on this module
        # can inline across the translation-unit boundary. The generated module
        # currently contains 1 143 such call sites and they remain real calls.
        # Folding them would need LTO across both targets, which was assessed at
        # roughly 1-2 % of total process CPU against a blast radius of every
        # consumer of a prebuilt library, and was therefore not taken.
        #
        # The cost is paid once, at install time, on a machine that is already
        # compiling 701 game-module translation units. Revert to /O1 only if
        # install time becomes the binding constraint; the runtime effect of
        # this flag is otherwise unmeasured. It is the one change in this tree
        # with a plausible double-digit-percent effect on the DSP thread, which
        # is ~19 % of process CPU, and no recording has yet measured it.
        target_compile_options(RMGE01_dsp PRIVATE -w -fwrapv -fno-strict-aliasing /O2)
    endif()
    foreach(symbol IN ITEMS
            galaxy_rmge01_dsp_entry
            galaxy_rmge01_dsp_entry_expected_iram_start_address
            galaxy_rmge01_dsp_entry_expected_iram_byte_len
            galaxy_rmge01_dsp_entry_expected_iram_sha1
            galaxy_rmge01_dsp_entry_generated_probe_contract_version
            galaxy_rmge01_dsp_entry_generated_probe_capabilities)
        target_link_options(RMGE01_dsp PRIVATE "/EXPORT:${symbol}")
    endforeach()
endif()
"#;

/// Lower RMGE01's AX ucode with the bundled Dolphin free DSP ROMs.
pub fn generate_dsp(game: &Path, output: &Path) -> Result<()> {
    let iram = read_rmge01_ax_ucode(game)
        .with_context(|| format!("failed to read the AX DSP ucode from {}", game.display()))?;
    let raw_iram_words: Vec<u16> = iram
        .chunks_exact(2)
        .map(|pair| u16::from_be_bytes([pair[0], pair[1]]))
        .collect();
    let irom_words = rom_words(DSP_IROM, DSP_IROM_WORDS, "DSP instruction ROM")?;
    let coefficient_words = rom_words(DSP_COEF, DSP_COEF_WORDS, "DSP coefficient ROM")?;
    let mut words = raw_iram_words.clone();
    splice_irom_words(0, &mut words, &irom_words)?;
    let entry_points = dsp_cfg_entry_points(0, 0);
    let generated = lower_dsp_program_from_entry_vectors_with_static_memory_and_raw_provenance(
        0,
        &words,
        &entry_points,
        DSP_ENTRY_FUNCTION,
        DspStaticMemoryImages {
            irom_words: Some(&irom_words),
            coefficient_words: Some(&coefficient_words),
        },
        &raw_iram_words,
    )
    .context("failed to lower the RMGE01 AX DSP ucode")?;
    if let Some(parent) = output.parent().filter(|p| !p.as_os_str().is_empty()) {
        fs::create_dir_all(parent)
            .with_context(|| format!("failed to create {}", parent.display()))?;
    }
    write_generated_text_create_new(output, &generated.cpp)
        .with_context(|| format!("failed to write {}", output.display()))?;
    println!(
        "Lowered AX DSP ucode: {} words as {} instructions -> {}",
        generated.word_count,
        generated.instruction_count,
        output.display()
    );
    Ok(())
}

/// Decode one embedded big-endian ROM image of an exact word count.
fn rom_words(bytes: &[u8], expected_words: usize, label: &str) -> Result<Vec<u16>> {
    if bytes.len() != expected_words * 2 {
        bail!(
            "{label} has {} bytes; expected {}",
            bytes.len(),
            expected_words * 2
        );
    }
    Ok(bytes
        .chunks_exact(2)
        .map(|pair| u16::from_be_bytes([pair[0], pair[1]]))
        .collect())
}

/// Generate the game module, Home module, DSP and boot image sources.
pub fn generate_all(game: &Path, output: &Path) -> Result<()> {
    if output.exists() {
        bail!(
            "generation output {} already exists; choose a new staging directory",
            output.display()
        );
    }
    println!("STEP 1/5 Validating the game");
    let data = resolve_game_data_root(game)?;
    let game = data.as_path();
    let analysis = analyze_input(game)
        .with_context(|| format!("failed to validate RMGE01 input {}", game.display()))?;
    require_clean_function_audit(&analysis)?;
    if analysis.game_id != RMGE01_GAME_ID || analysis.main_dol_sha1 != RMGE01_DOL_SHA1 {
        bail!("input is not the supported RMGE01 revision");
    }
    fs::create_dir_all(output).with_context(|| format!("failed to create {}", output.display()))?;

    println!("STEP 2/5 Recompiling the game code");
    generate_module_sources(
        game,
        &output.join("game"),
        0,
        MODULE_SHARD_SIZE,
        MODULE_SHARD_SOURCE_KIB,
        release_module_options(),
    )?;
    println!("STEP 3/5 Recompiling the Home Menu module");
    generate_home_button_sidecar(game, &output.join("home"))?;
    println!("STEP 4/5 Lowering the audio DSP program");
    generate_dsp(game, &output.join("dsp").join("rmge01_dsp.cpp"))?;
    write_generated_text_create_new(&output.join("dsp").join("CMakeLists.txt"), DSP_CMAKE)?;
    write_generated_text_create_new(&output.join("CMakeLists.txt"), BUILD_CMAKE)?;
    println!("STEP 5/5 Building the boot image");
    #[cfg(target_os = "windows")]
    crate::build_boot_image::run(game, &output.join("RMGE01_boot_image.bin"))?;

    // Report the options that were actually used, not a second copy of them.
    // The literals here previously disagreed with `release_module_options()` the
    // moment that function's residency flag was turned off, so the manifest
    // claimed `guestResidentInteger: true` for a module generated with it false.
    // Nothing reads this file today, which is exactly why the drift was invisible;
    // deriving it means the next flip cannot repeat it.
    let module_options = release_module_options();

    // Persist the translator's own coverage numbers.
    //
    // `interior_entry_points` in particular is otherwise unobtainable: the CLI
    // computes it for `nebula-recomp coverage` (main.rs) and prints it to a
    // console nobody captures, but it is the only number that separates "a
    // function the guest can call" from "a lookup key that aliases another
    // function's body". Two independent audits tried to recover it by scanning
    // guest text for branch targets and produced contradictory answers
    // (AgentWork/agent-17/16-kfunctions-is-not-a-function-list.md), because it
    // needs the translator's own `discover_external_interior_targets`, which
    // already exists here. Writing it costs one earlier pass over inputs that
    // step 1 has already validated.
    //
    // This does not bump GENERATION_VERSION. That constant invalidates compiled
    // modules and caches, and this changes only the manifest's contents - the
    // generated source is untouched - so moving it would invalidate every
    // existing artefact for nothing. generation.json is not part of the module
    // key hash either.
    let coverage = translation_coverage(game).with_context(|| {
        format!(
            "failed to compute translator coverage for {}",
            game.display()
        )
    })?;

    let manifest = json!({
        "schema": "nebula.generation.v1",
        "generator": format!("nebula-recomp {}", env!("CARGO_PKG_VERSION")),
        "generationVersion": GENERATION_VERSION,
        "gameId": analysis.game_id,
        "mainDolSha1": analysis.main_dol_sha1,
        "translationCoverage": {
            "totalFunctions": coverage.total_functions,
            "translatableFunctions": coverage.translatable_functions,
            "translatableInstructions": coverage.translatable_instructions,
            "interiorEntryPoints": coverage.interior_entry_points,
            "blockedFunctions": coverage.blocked_functions,
        },
        "moduleOptions": {
            "count": 0,
            "shardSize": MODULE_SHARD_SIZE,
            "shardSourceKiB": MODULE_SHARD_SOURCE_KIB,
            "inlineScalarSingleBinary": module_options.inline_scalar_single_binary,
            "fusedPairedTernary": module_options.fused_paired_ternary,
            "exactPsmtxLocalLanes": module_options.exact_psmtx_local_lanes,
            "guestResidentInteger": module_options.guest_resident_integer,
        },
        "dspRoms": {
            "irom": sha256_hex(DSP_IROM),
            "coef": sha256_hex(DSP_COEF),
        },
        "trees": {
            "game": tree_sha256(&output.join("game"))?,
            "home": tree_sha256(&output.join("home"))?,
            "dsp": tree_sha256(&output.join("dsp"))?,
        },
    });
    let manifest_path = output.join("generation.json");
    fs::write(&manifest_path, serde_json::to_vec_pretty(&manifest)?)
        .with_context(|| format!("failed to write {}", manifest_path.display()))?;
    println!("Generation complete: {}", output.display());
    Ok(())
}

fn sha256_hex(bytes: &[u8]) -> String {
    format!("{:x}", Sha256::digest(bytes))
}

/// SHA-256 over sorted relative paths and file contents of one tree.
fn tree_sha256(root: &Path) -> Result<String> {
    let mut files = Vec::new();
    collect_files(root, root, &mut files)?;
    files.sort();
    let mut hasher = Sha256::new();
    for relative in files {
        let bytes = fs::read(root.join(&relative))
            .with_context(|| format!("failed to read {}", root.join(&relative).display()))?;
        hasher.update(relative.replace('\\', "/").as_bytes());
        hasher.update([0]);
        hasher.update(Sha256::digest(&bytes));
    }
    Ok(format!("{:x}", hasher.finalize()))
}

fn collect_files(root: &Path, dir: &Path, files: &mut Vec<String>) -> Result<()> {
    for entry in fs::read_dir(dir).with_context(|| format!("failed to list {}", dir.display()))? {
        let entry = entry?;
        let path = entry.path();
        if entry.file_type()?.is_dir() {
            collect_files(root, &path, files)?;
        } else {
            let relative = path
                .strip_prefix(root)
                .context("generated file escaped its tree")?
                .to_string_lossy()
                .into_owned();
            files.push(relative);
        }
    }
    Ok(())
}

/// Resolve the DATA partition root of an extracted game folder. Accepts the
/// folder itself or a parent that contains `DATA/`.
pub fn resolve_game_data_root(input: &Path) -> Result<std::path::PathBuf> {
    let nested = input.join("DATA");
    if nested.join("sys").join("main.dol").is_file() {
        return Ok(nested);
    }
    if input.join("sys").join("main.dol").is_file() {
        return Ok(input.to_owned());
    }
    bail!(
        "{} is not an extracted Wii game folder: expected DATA/sys/main.dol or sys/main.dol",
        input.display()
    )
}

/// Small partition files the runtime reads directly, next to game.pak.
const CONTENT_FILES: &[&str] = &[
    "sys/boot.bin",
    "sys/bi2.bin",
    "sys/fst.bin",
    "ticket.bin",
    "tmd.bin",
    "files/LayoutData/FileSelect.arc",
];

/// Build the installed content package: a sparse game.pak plus the loose
/// partition files the runtime opens directly. The source is only read.
#[cfg(target_os = "windows")]
pub fn package_content(game: &Path, output: &Path) -> Result<()> {
    if output.exists() {
        bail!(
            "content output {} already exists; choose a new staging directory",
            output.display()
        );
    }
    let data = resolve_game_data_root(game)?;
    for relative in CONTENT_FILES {
        let source = data.join(relative);
        if !source.is_file() {
            bail!(
                "{} is missing {relative}; extract the complete game partition (Dolphin \
                 \"Extract Entire Disc\" layout) or choose the ISO/RVZ instead",
                data.display()
            );
        }
    }
    fs::create_dir_all(output).with_context(|| format!("failed to create {}", output.display()))?;
    for relative in CONTENT_FILES {
        let target = output.join(relative);
        if let Some(parent) = target.parent() {
            fs::create_dir_all(parent)
                .with_context(|| format!("failed to create {}", parent.display()))?;
        }
        fs::copy(data.join(relative), &target)
            .with_context(|| format!("failed to copy {relative}"))?;
    }
    crate::build_pak::run(&data, &output.join("game.pak"))
}
