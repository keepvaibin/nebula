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
    read_rmge01_ax_ucode, DspStaticMemoryImages, ModuleTranslationOptions, DSP_COEF_WORDS,
    DSP_IROM_WORDS, RMGE01_DOL_SHA1, RMGE01_GAME_ID,
};
use serde_json::json;
use sha2::{Digest, Sha256};
use std::{fs, path::Path};

/// Bump whenever generated source for the same game changes, so installers
/// know that previously compiled modules can no longer be reused.
pub const GENERATION_VERSION: u32 = 1;

/// Shard size and per-shard source budget of the qualified module build.
pub const MODULE_SHARD_SIZE: usize = 64;
pub const MODULE_SHARD_SOURCE_KIB: usize = 512;

/// Entry symbol the native runtime expects for the lowered AX ucode.
pub const DSP_ENTRY_FUNCTION: &str = "galaxy_rmge01_dsp_entry";

const DSP_IROM: &[u8] = include_bytes!("../../../third_party/dolphin-free-dsp-rom/dsp_rom.bin");
const DSP_COEF: &[u8] = include_bytes!("../../../third_party/dolphin-free-dsp-rom/dsp_coef.bin");

/// Translation options of the qualified release module: inline widened-single
/// scalar arithmetic and exact PSMTX local lanes; every trace/profile off.
pub fn release_module_options() -> ModuleTranslationOptions {
    ModuleTranslationOptions {
        inline_scalar_single_binary: true,
        exact_psmtx_local_lanes: true,
        ..ModuleTranslationOptions::default()
    }
}

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
    println!("STEP 5/5 Building the boot image");
    #[cfg(target_os = "windows")]
    crate::build_boot_image::run(game, &output.join("RMGE01_boot_image.bin"))?;

    let manifest = json!({
        "schema": "nebula.generation.v1",
        "generator": format!("nebula-recomp {}", env!("CARGO_PKG_VERSION")),
        "generationVersion": GENERATION_VERSION,
        "gameId": analysis.game_id,
        "mainDolSha1": analysis.main_dol_sha1,
        "moduleOptions": {
            "count": 0,
            "shardSize": MODULE_SHARD_SIZE,
            "shardSourceKiB": MODULE_SHARD_SOURCE_KIB,
            "inlineScalarSingleBinary": true,
            "exactPsmtxLocalLanes": true,
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
