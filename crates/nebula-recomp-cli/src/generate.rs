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
/// 3: straight-line integer GPR residency. The release module leaves it off,
///    so v3 modules remain valid and the number is not reverted.
// 4: PSMTX RAM/lane reuse corrected; ABI-compatible, regenerate for the fix.
// 5: clang-cl contraction policy is forwarded instead of ignored.
// 9: paired estimates use the corrected table/status boundary; regenerate modules.
// 10: call-return policy and paired lane-fact corrections; regenerate modules.
// 11: checked multi-register RAM helpers; regenerate affected modules.
// 12: native float, flat RAM reads/writes, direct indirect dispatch and the
//     function-record export (native ABI 24); regenerate every module.
// 13: eager FPU (no MSR[FP] guards or exception-7 retry entries; native ABI
//     25); regenerate every module.
// 18: hardware float conversions, flat-view-only memory paths, EABI call and
//     return synchronization, the module checkpoint gate, per-site indirect
//     call caches and the profile-ordered function layout; regenerate every
//     module.
// 19 (1.0.1): fused multiply-adds without the C runtime, no LodDistance hook,
//     dialogue text steps for the text speed setting (SiteHookKind 28) and the
//     language setting's answer to SCGetLanguage (SiteHookKind 30); regenerate
//     every module.
pub const GENERATION_VERSION: u32 = 19;

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
        // Native execution model: flat RAM in both directions, native IEEE
        // arithmetic with exact fallbacks, and direct dispatch for virtual
        // calls. See galaxy/native_fast_paths.h for the admission rules.
        flat_ram_reads: true,
        flat_ram_writes: true,
        native_float: true,
        // Fast float (GALAXY_FAST_FLOAT=1): no exact fallback, no FPSCR status,
        // flush-to-zero on the simulation thread. This trades exact float results for speed,
        // at Dolphin/WiiCompiled-class accuracy.
        fast_float: true,
        direct_indirect_calls: true,
        // The exact PSMTX local-lane path emits out-of-line exact helpers for every lane
        // operation, which is too slow for the SDK matrix library. The generic native
        // lowering handles PSMTX instead.
        exact_psmtx_local_lanes: false,
        // Register residency (crate::residency): GPRs, LR and CTR live in host
        // locals between synchronization points; every context-receiving call
        // that is not proven to leave the integer file alone stores first and
        // reloads afterwards, interior entries reload through stubs, and every
        // return stores. Unknown helpers are full synchronization points.
        guest_resident_integer: true,
        // Lean memory paths: hardware binary32 conversions for lfs/stfs/psq,
        // no fallback for a missing flat view, stack stores through r11 without
        // the gather-pipe test, and HID2 paired-single mode taken as set (the
        // game sets it once at boot).
        hw_float_conversions: true,
        flat_view_only: true,
        stack_r11_stores: true,
        assume_paired_singles: true,
        // Calls and returns synchronize only the registers the PowerPC EABI lets
        // the other side observe. Functions the analysis cannot vouch for keep
        // full synchronization (abi_contract.json lists them).
        abi_call_sync: true,
        abi_exit_sync: true,
        // Checkpoint tests read a module-owned gate snapshot, indirect calls
        // cache their last direct entry per call site, and reciprocal estimates,
        // selects and compares skip FPSCR updates.
        checkpoint_globals: true,
        bctrl_site_cache: true,
        fast_float_forms: true,
        // The functions the game runs most, placed first and together.
        function_order: Some(nebula_recomp_core::RMGE01_HOT_FUNCTIONS),
        ..ModuleTranslationOptions::default()
    }
}

/// One build graph for everything Setup compiles. The two largest single
/// files (Home Menu, DSP) come first so they start first.
const BUILD_CMAKE: &str = r#"cmake_minimum_required(VERSION 3.24)
project(NebulaGenerated LANGUAGES CXX)
# Below MSVC 19.0 CMake defines no -std: flag for clang-cl, and the build
# fails deep in the C++ headers instead of here.
if(CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC" AND CMAKE_CXX_SIMULATE_VERSION VERSION_LESS 19.30)
    message(FATAL_ERROR "clang-cl is emulating MSVC ${CMAKE_CXX_SIMULATE_VERSION}; C++20 needs 19.30 or newer (-fms-compatibility-version).")
endif()
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
        # The generated function is the DSP thread's hot loop, built at /O2.
        # clang-cl uses the lowest -O level it is given, so a lower one here
        # would also cancel the Release default.
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

    // Report the options that were actually used, not a second copy of them. Literals
    // here would drift from `release_module_options()` whenever its residency flag changes,
    // so the manifest could claim a setting the module was not built with.
    let module_options = release_module_options();

    // Persist the translator's own coverage numbers.
    //
    // `interior_entry_points` is otherwise unavailable. The CLI computes it for
    // `nebula-recomp coverage` (main.rs) and prints it to the console, so nothing records it.
    // It separates "a function the guest can call" from "a lookup key that aliases another
    // function's body". Scanning guest text for branch targets gives unreliable answers, so the
    // value comes from the translator's `discover_external_interior_targets`. Writing it costs one
    // extra pass over inputs that step 1 has already validated.
    //
    // This does not bump GENERATION_VERSION. That constant invalidates compiled modules and caches,
    // and this change only alters the manifest's contents. The generated source is untouched.
    // generation.json is not part of the module key hash either.
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
            "flatRamReads": module_options.flat_ram_reads,
            "flatRamWrites": module_options.flat_ram_writes,
            "nativeFloat": module_options.native_float,
            "fastFloat": module_options.fast_float,
            "directIndirectCalls": module_options.direct_indirect_calls,
            "fusedPairedTernary": module_options.fused_paired_ternary,
            "exactPsmtxLocalLanes": module_options.exact_psmtx_local_lanes,
            "guestResidentInteger": module_options.guest_resident_integer,
            "hwFloatConversions": module_options.hw_float_conversions,
            "flatViewOnly": module_options.flat_view_only,
            "stackR11Stores": module_options.stack_r11_stores,
            "assumePairedSingles": module_options.assume_paired_singles,
            "abiCallSync": module_options.abi_call_sync,
            "abiExitSync": module_options.abi_exit_sync,
            "checkpointGlobals": module_options.checkpoint_globals,
            "bctrlSiteCache": module_options.bctrl_site_cache,
            "fastFloatForms": module_options.fast_float_forms,
            "functionOrder": module_options.function_order.map_or(0, <[u32]>::len),
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
#[cfg(target_os = "windows")]
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
