use anyhow::{bail, Context, Result};
use clap::{Parser, Subcommand, ValueEnum};
use nebula_recomp_core::{
    analyze_input, audit_dsp_program, extract_rmge01_dsp_ucode_candidate,
    lower_dsp_program_from_entry_vectors_with_static_memory_and_raw_provenance,
    lower_dsp_program_to_cpp, lower_dsp_program_to_cpp_at_entry,
    lower_ppc_code_range_with_entries_and_immediate_overrides, parse_rso, parse_sel,
    require_production_dsp_timing_execution, scan_rmge01_dsp_ucode_candidates,
    translate_module_with_options, translate_one, translation_coverage, DspStaticMemoryImages,
    DspTimingProfile, DspUcodeDescriptorFormat, DspUcodeScan, GameAnalysis,
    ModuleTranslationOptions, RsoImage, RsoLinkLayout, DEFAULT_MODULE_SHARD_SOURCE_KIB,
    DSP_COEF_WORDS, DSP_IROM_WORDS,
};
use std::{
    fs,
    io::{self, Write},
    path::{Path, PathBuf},
};

fn write_generated_text_if_changed(path: &Path, contents: &str) -> io::Result<bool> {
    match fs::read(path) {
        Ok(existing) if existing == contents.as_bytes() => Ok(false),
        Ok(_) => {
            fs::write(path, contents)?;
            Ok(true)
        }
        Err(error) if error.kind() == io::ErrorKind::NotFound => {
            fs::write(path, contents)?;
            Ok(true)
        }
        Err(error) => Err(error),
    }
}

fn write_generated_text_create_new(path: &Path, contents: &str) -> io::Result<()> {
    let mut file = fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(path)?;
    file.write_all(contents.as_bytes())?;
    file.flush()?;
    file.sync_all()
}

#[derive(Debug, Parser)]
#[command(
    name = "nebula-recomp",
    version,
    about = "Install-time native recompiler tooling for RMGE01"
)]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Clone, Copy, Debug, ValueEnum)]
enum DspUcodeEntry {
    Init,
    Resume,
    Base,
}

#[derive(Debug, Subcommand)]
enum Command {
    /// Inspect a supported dump and print a deterministic analysis manifest.
    Inspect {
        input: PathBuf,
        #[arg(long)]
        json: bool,
    },
    /// Verify that the input is the exact supported RMGE01 executable.
    Verify { input: PathBuf },
    /// Validate the dump and write installation metadata without copying content.
    Prepare {
        input: PathBuf,
        #[arg(long)]
        output: PathBuf,
    },
    /// Translate one supported RMGE01 function into a native C++ DLL source.
    TranslateOne {
        input: PathBuf,
        #[arg(long, value_parser = parse_address)]
        address: u32,
        #[arg(long)]
        output: PathBuf,
    },
    /// Generate a sharded partial or full native RMGE01 module source tree.
    TranslateModule {
        input: PathBuf,
        #[arg(long)]
        output: PathBuf,
        /// Maximum translated functions to emit; zero emits every supported function.
        #[arg(long, default_value_t = 256)]
        count: usize,
        #[arg(long, default_value_t = 64)]
        shard_size: usize,
        /// Maximum rendered C++ source size of one function shard, in KiB.
        #[arg(long, default_value_t = DEFAULT_MODULE_SHARD_SOURCE_KIB)]
        shard_source_kib: usize,
        /// Experimental typed guest-register residency; off in production.
        #[arg(long)]
        experimental_guest_resident_leaf: bool,
        /// Exact RMGE01 0x80165478 private typed-region pilot; off by default.
        #[arg(long)]
        experimental_typed_region_80165478: bool,
        /// Experimental WiiCompiled-derived flat RAM reads; checked fallback remains active.
        #[arg(long)]
        experimental_flat_ram_reads: bool,
        /// Fused paired arithmetic/commit helper; exact guards and slow fallback.
        #[arg(long)]
        experimental_fused_paired_binary: bool,
        /// In-header widened-single scalar add/subtract/multiply; off by default.
        #[arg(long)]
        experimental_inline_scalar_single_binary: bool,
        /// Retained compatibility spelling; exact PSMTX local lanes are enabled by default.
        #[arg(long, default_value_t = true)]
        exact_psmtx_local_lanes: bool,
        /// Disable exact PSMTX local lanes for differential regression controls.
        #[arg(long, conflicts_with = "trace_psmtx_guard")]
        no_exact_psmtx_local_lanes: bool,
        /// Enable the exact, guarded PSVECCrossProduct local-lane experiment.
        #[arg(long)]
        exact_psvec_cross_local_lanes: bool,
        /// Count CrossProduct admission during the main-frame trace window.
        #[arg(long, requires = "exact_psvec_cross_local_lanes")]
        trace_psvec_cross_guard: bool,
        /// Enable the exact, guarded PSVECNormalize local-lane experiment.
        #[arg(long)]
        exact_psvec_normalize_local_lanes: bool,
        /// Count PSVECNormalize admission during the main-frame trace window.
        #[arg(long, requires = "exact_psvec_normalize_local_lanes")]
        trace_psvec_normalize_guard: bool,
        /// Count admission during the runtime main-frame trace window; flush on next call.
        #[arg(long)]
        trace_psmtx_guard: bool,
        /// Add eight buffered end-frame continuation markers (development only; requires --count 0).
        #[arg(long)]
        trace_end_frame_spans: bool,
        /// Trace eight GameScene update calls and their exact returns (requires --count 0).
        #[arg(long)]
        trace_scene_update_spans: bool,
        /// Profile the ten static child calls in particle direction drawing (requires --count 0).
        #[arg(long)]
        profile_particle_direction_edges: bool,
        /// Profile all 77 static child calls in NW4R material setup (requires --count 0).
        #[arg(long)]
        profile_nw4r_material_setup_edges: bool,
        /// Profile 17 static child calls in the two hot NW4R Pane DrawSelf implementations.
        #[arg(long)]
        profile_nw4r_pane_draw_self_edges: bool,
        /// Profile six static calls in the two dominant Pane DrawSelf children.
        #[arg(long)]
        profile_nw4r_pane_hot_child_edges: bool,
        /// Profile 66 static calls below CalcStringRect and the hot text formatter.
        #[arg(long)]
        profile_nw4r_text_format_edges: bool,
        /// Profile 16 static calls in the dominant NW4R text-layout children.
        #[arg(long)]
        profile_nw4r_text_layout_edges: bool,
        /// Profile ten static calls and the matrix body in hot JPA draw 0x803A3B6C.
        #[arg(long)]
        profile_jpa_hot_draw_edges: bool,
        /// Profile direct calls below the top unresolved UI/input self-time entries.
        #[arg(long)]
        profile_top_self_closure_edges: bool,
    },
    /// Report the current hard-fail translator coverage over all functions.
    Coverage {
        input: PathBuf,
        #[arg(long)]
        json: bool,
    },
    /// Validate a Wii RSO container without executing or copying its contents.
    RsoAudit {
        input: PathBuf,
        #[arg(long)]
        json: bool,
    },
    /// Resolve every RSO import through a validated SEL table and exact RMGE01 DOL.
    RsoLinkAudit {
        rso: PathBuf,
        sel: PathBuf,
        game: PathBuf,
        #[arg(long)]
        json: bool,
    },
    /// Hard-fail audit of one fully linked RSO code section for static native lowering.
    RsoLowerAudit {
        rso: PathBuf,
        sel: PathBuf,
        game: PathBuf,
        #[arg(long, default_value_t = 1)]
        section: u32,
    },
    /// Generate a native Home Button RSO sidecar from an extracted RMGE01 DATA directory.
    TranslateHomeButtonRso {
        game: PathBuf,
        #[arg(long)]
        output: PathBuf,
    },
    /// Validate a Wii SEL symbol-link table without copying its contents.
    SelAudit {
        input: PathBuf,
        #[arg(long)]
        json: bool,
    },
    /// Audit a user-supplied raw DSP ucode blob as big-endian 16-bit words.
    DspAudit {
        input: PathBuf,
        #[arg(long, value_parser = parse_dsp_address, default_value = "0x0000")]
        start_address: u16,
        #[arg(long)]
        json: bool,
    },
    /// Identify an ISO/RVZ/WIA/WBFS image or extracted folder and report
    /// whether it is the supported RMGE01 revision.
    Identify {
        input: PathBuf,
        #[arg(long)]
        json: bool,
    },
    /// Extract the game partition of a supported disc image into OUTPUT/DATA.
    Extract {
        input: PathBuf,
        /// New directory to create.
        #[arg(long)]
        output: PathBuf,
    },
    /// Generate every game-derived source of a Nebula installation: game
    /// module, Home Menu module, DSP program and boot image.
    Generate {
        /// Extracted RMGE01 game folder (DATA root or its parent).
        game: PathBuf,
        /// New staging directory to create.
        #[arg(long)]
        output: PathBuf,
    },
    /// Lower RMGE01's AX DSP ucode with the bundled free DSP ROMs.
    GenerateDsp {
        /// Extracted RMGE01 game folder (DATA root or its parent).
        game: PathBuf,
        #[arg(long)]
        output: PathBuf,
    },
    /// Build the installed content package (game.pak and loose partition files).
    #[cfg(target_os = "windows")]
    PackageContent {
        /// Extracted RMGE01 game folder (DATA root or its parent).
        game: PathBuf,
        /// New content directory to create.
        #[arg(long)]
        output: PathBuf,
    },
    /// Statically lower a user-supplied raw DSP ucode blob to native C++.
    DspLower {
        input: PathBuf,
        #[arg(long, value_parser = parse_dsp_address, default_value = "0x0000")]
        start_address: u16,
        #[arg(long, default_value = "galaxy_dsp_entry")]
        function_name: String,
        #[arg(long)]
        output: PathBuf,
        /// Follow control flow from the reset/interrupt vectors instead of
        /// decoding the image linearly. Required for real ucode that interleaves
        /// data with code.
        #[arg(long)]
        cfg: bool,
        /// Optional 0x1000-word DSP instruction ROM image to compile as native
        /// code at addresses 0x8000..0x8fff.
        #[arg(long)]
        irom: Option<PathBuf>,
        /// Optional 0x800-word DSP coefficient ROM image to initialize native
        /// coefficient memory.
        #[arg(long)]
        coef: Option<PathBuf>,
        /// Require a reviewed retail-Wii timing profile and production timing
        /// execution. This remains fail-closed until exact generated retirement
        /// accounting and deterministic DSP-clock scheduling are implemented.
        #[arg(long, requires = "timing_profile")]
        production: bool,
        /// Project-owned DSP timing profile. Accepted only with --production.
        #[arg(long, requires = "production")]
        timing_profile: Option<PathBuf>,
    },
    /// Scan a verified RMGE01 DOL for DSP ucode upload descriptors and hashes.
    DspScanUcode {
        input: PathBuf,
        #[arg(long)]
        json: bool,
    },
    /// Validate a project-owned DSP timing profile without accepting unknown or
    /// unreviewed timing data.
    DspTimingValidate {
        input: PathBuf,
        /// Require retail provenance and a source-control-approved profile
        /// identity. This currently hard-fails until retail capture review is
        /// complete.
        #[arg(long)]
        production: bool,
    },
    /// Statically lower a scanned RMGE01 DSP ucode upload candidate to native C++.
    DspLowerUcode {
        input: PathBuf,
        #[arg(long, default_value_t = 0)]
        candidate: usize,
        #[arg(long, value_enum, default_value = "init")]
        entry: DspUcodeEntry,
        #[arg(long, default_value = "galaxy_dsp_entry")]
        function_name: String,
        #[arg(long)]
        output: PathBuf,
        /// Follow control flow from the ucode vectors instead of lowering the
        /// scanned payload linearly.
        #[arg(long)]
        cfg: bool,
        /// Optional 0x1000-word DSP instruction ROM image to compile as native
        /// code at addresses 0x8000..0x8fff.
        #[arg(long)]
        irom: Option<PathBuf>,
        /// Optional 0x800-word DSP coefficient ROM image to initialize native
        /// coefficient memory.
        #[arg(long)]
        coef: Option<PathBuf>,
    },
    /// Build a sparse game.pak from an extracted RMGE01 disc image (Windows only).
    ///
    /// game.pak is a single contiguous sparse NTFS file where every game asset
    /// resides at its exact partition-relative disc byte offset.  NebulaRuntime
    /// memory-maps it so DVDLowRead is a single O(1) pointer-add at runtime.
    #[cfg(target_os = "windows")]
    BuildPak {
        /// Path to the extracted game directory (the folder containing DATA/).
        input: PathBuf,
        /// Destination path for the output game.pak file.
        #[arg(long)]
        output: PathBuf,
    },
    /// Build the deterministic RMGE01 initial-memory image for the native runtime.
    ///
    /// The image contains only validated initialized DOL section bytes, their
    /// guest destinations, and the zero-before-copy BSS range. It is installer
    /// data, not a runtime PPC decoder, interpreter, JIT, or executable loader.
    #[cfg(target_os = "windows")]
    BuildBootImage {
        /// Exact supported RMGE01 extracted game directory.
        input: PathBuf,
        /// Destination, whose file name must be RMGE01_boot_image.bin.
        #[arg(long)]
        output: PathBuf,
    },
}

fn main() -> Result<()> {
    let cli = Cli::parse();
    match cli.command {
        Command::Inspect { input, json } => {
            let analysis = analyze(&input)?;
            if json {
                println!("{}", serde_json::to_string_pretty(&analysis)?);
            } else {
                print_human(&analysis);
            }
        }
        Command::Verify { input } => {
            let analysis = analyze(&input)?;
            require_clean_function_audit(&analysis)?;
            println!(
                "Verified {}: SHA-1 {}, {}/{} raw text words recognized",
                analysis.game_id,
                analysis.main_dol_sha1,
                analysis.raw_text_audit.recognized_word_count,
                analysis.raw_text_audit.raw_word_count
            );
            if analysis.raw_text_audit.unrecognized_word_count != 0 {
                println!(
                    "{} raw text words require code/data classification before translation",
                    analysis.raw_text_audit.unrecognized_word_count
                );
            }
            println!(
                "Function map: {}/{} words decoded across {} functions",
                analysis.function_audit.recognized_word_count,
                analysis.function_audit.function_word_count,
                analysis.function_audit.function_count
            );
        }
        Command::Prepare { input, output } => {
            let analysis = analyze(&input)?;
            require_clean_function_audit(&analysis)?;
            fs::create_dir_all(&output)
                .with_context(|| format!("failed to create {}", output.display()))?;
            let manifest_path = output.join("install-manifest.json");
            let json = serde_json::to_vec_pretty(&analysis)?;
            fs::write(&manifest_path, json)
                .with_context(|| format!("failed to write {}", manifest_path.display()))?;
            println!(
                "Prepared deterministic manifest: {}",
                manifest_path.display()
            );
            println!("No game content was copied.");
        }
        Command::TranslateOne {
            input,
            address,
            output,
        } => {
            let unit = translate_one(&input, address)
                .with_context(|| format!("failed to translate function 0x{address:08X}"))?;
            if let Some(parent) = output.parent() {
                fs::create_dir_all(parent)
                    .with_context(|| format!("failed to create {}", parent.display()))?;
            }
            fs::write(&output, unit.cpp)
                .with_context(|| format!("failed to write {}", output.display()))?;
            println!(
                "Translated 0x{:08X}: {} bytes, {} instructions -> {}",
                unit.address,
                unit.size,
                unit.instruction_count,
                output.display()
            );
        }
        Command::TranslateModule {
            input,
            output,
            count,
            shard_size,
            shard_source_kib,
            experimental_guest_resident_leaf,
            experimental_typed_region_80165478,
            experimental_flat_ram_reads,
            experimental_fused_paired_binary,
            experimental_inline_scalar_single_binary,
            exact_psmtx_local_lanes,
            no_exact_psmtx_local_lanes,
            exact_psvec_cross_local_lanes,
            trace_psvec_cross_guard,
            exact_psvec_normalize_local_lanes,
            trace_psvec_normalize_guard,
            trace_psmtx_guard,
            trace_end_frame_spans,
            trace_scene_update_spans,
            profile_particle_direction_edges,
            profile_nw4r_material_setup_edges,
            profile_nw4r_pane_draw_self_edges,
            profile_nw4r_pane_hot_child_edges,
            profile_nw4r_text_format_edges,
            profile_nw4r_text_layout_edges,
            profile_jpa_hot_draw_edges,
            profile_top_self_closure_edges,
        } => {
            let options = ModuleTranslationOptions {
                guest_resident_leaf: experimental_guest_resident_leaf,
                typed_region_80165478: experimental_typed_region_80165478,
                flat_ram_reads: experimental_flat_ram_reads,
                fused_paired_binary: experimental_fused_paired_binary,
                inline_scalar_single_binary: experimental_inline_scalar_single_binary,
                exact_psmtx_local_lanes: exact_psmtx_local_lanes && !no_exact_psmtx_local_lanes,
                exact_psvec_cross_local_lanes,
                exact_psvec_normalize_local_lanes,
                trace_psvec_cross_guard,
                trace_psvec_normalize_guard,
                trace_psmtx_guard,
                trace_end_frame_spans,
                trace_scene_update_spans,
                profile_particle_direction_edges,
                profile_nw4r_material_setup_edges,
                profile_nw4r_pane_draw_self_edges,
                profile_nw4r_pane_hot_child_edges,
                profile_nw4r_text_format_edges,
                profile_nw4r_text_layout_edges,
                profile_jpa_hot_draw_edges,
                profile_top_self_closure_edges,
            };
            generate_module_sources(
                &input,
                &output,
                count,
                shard_size,
                shard_source_kib,
                options,
            )?;
            println!("Output: {}", output.display());
        }
        Command::Coverage { input, json } => {
            let coverage = translation_coverage(&input)
                .with_context(|| format!("failed to scan {}", input.display()))?;
            if json {
                println!("{}", serde_json::to_string_pretty(&coverage)?);
            } else {
                println!(
                    "Translator coverage: {}/{} functions, {} instructions, {} interior entry points",
                    coverage.translatable_functions,
                    coverage.total_functions,
                    coverage.translatable_instructions,
                    coverage.interior_entry_points
                );
                println!("Blocked functions: {}", coverage.blocked_functions);
                println!("First blockers:");
                for (blocker, count) in &coverage.first_blocker_counts {
                    println!("  {blocker}: {count}");
                    if let Some(examples) = coverage.first_blocker_examples.get(blocker) {
                        for example in examples {
                            println!("    {example}");
                        }
                    }
                }
            }
        }
        Command::RsoAudit { input, json } => {
            let bytes = fs::read(&input)
                .with_context(|| format!("failed to read RSO {}", input.display()))?;
            let image = parse_rso(&bytes)
                .with_context(|| format!("failed to validate RSO {}", input.display()))?;
            if json {
                println!("{}", serde_json::to_string_pretty(&image)?);
            } else {
                println!("RSO: {}", input.display());
                println!(
                    "Module: {} (version {}), {} sections, BSS 0x{:X} bytes",
                    image.module_name.as_deref().unwrap_or("<unnamed>"),
                    image.version,
                    image.sections.len(),
                    image.bss_size
                );
                println!(
                    "Relocations: {} internal, {} external; symbols: {} exports, {} imports",
                    image.internal_relocations.len(),
                    image.external_relocations.len(),
                    image.exports.len(),
                    image.imports.len()
                );
                println!(
                    "Entrypoints: prolog section {} + 0x{:X}, epilog section {} + 0x{:X}, unresolved section {} + 0x{:X}",
                    image.prolog_section,
                    image.prolog_offset,
                    image.epilog_section,
                    image.epilog_offset,
                    image.unresolved_section,
                    image.unresolved_offset
                );
                println!("No RSO content was copied or executed.");
            }
        }
        Command::RsoLinkAudit {
            rso,
            sel,
            game,
            json,
        } => {
            let rso_bytes =
                fs::read(&rso).with_context(|| format!("failed to read RSO {}", rso.display()))?;
            let rso_image = parse_rso(&rso_bytes)
                .with_context(|| format!("failed to validate RSO {}", rso.display()))?;
            let sel_bytes =
                fs::read(&sel).with_context(|| format!("failed to read SEL {}", sel.display()))?;
            let sel_image = parse_sel(&sel_bytes)
                .with_context(|| format!("failed to validate SEL {}", sel.display()))?;
            let analysis = analyze_input(&game)
                .with_context(|| format!("failed to validate RMGE01 input {}", game.display()))?;
            let resolved_symbols = sel_image
                .resolve_dol_symbols(&analysis.sections)
                .context("failed to resolve SEL symbols through the RMGE01 DOL")?;
            let bindings = rso_image
                .bind_imports(&resolved_symbols)
                .context("failed to bind every RSO import through the SEL table")?;
            let link_layout = canonical_rso_link_layout(&rso_image)?;
            let _linked_image = rso_image
                .apply_relocations(&rso_bytes, &link_layout, &bindings)
                .context("failed to apply every RSO relocation without a runtime linker")?;
            let relocation_count =
                rso_image.internal_relocations.len() + rso_image.external_relocations.len();
            if json {
                println!(
                    "{}",
                    serde_json::to_string_pretty(&serde_json::json!({
                        "game_id": analysis.game_id,
                        "main_dol_sha1": analysis.main_dol_sha1,
                        "rso_module": rso_image.module_name,
                        "rso_import_count": rso_image.imports.len(),
                        "rso_external_relocation_count": rso_image.external_relocations.len(),
                        "rso_relocation_count": relocation_count,
                        "sel_executable": sel_image.executable_name,
                        "sel_symbol_count": sel_image.symbols.len(),
                        "bindings": bindings,
                    }))?
                );
            } else {
                println!("RSO link audit: {}", rso.display());
                println!(
                    "Validated {} ({}) against {}",
                    analysis.game_id,
                    analysis.main_dol_sha1,
                    game.display()
                );
                println!(
                    "Resolved {}/{} RSO imports through {} SEL symbols ({} external relocations).",
                    bindings.len(),
                    rso_image.imports.len(),
                    sel_image.symbols.len(),
                    rso_image.external_relocations.len(),
                );
                println!(
                    "Applied all {relocation_count} relocations to an installer-only canonical link image."
                );
                println!("No RSO or SEL content was copied or executed.");
            }
        }
        Command::RsoLowerAudit {
            rso,
            sel,
            game,
            section,
        } => {
            let rso_bytes =
                fs::read(&rso).with_context(|| format!("failed to read RSO {}", rso.display()))?;
            let rso_image = parse_rso(&rso_bytes)
                .with_context(|| format!("failed to validate RSO {}", rso.display()))?;
            let sel_bytes =
                fs::read(&sel).with_context(|| format!("failed to read SEL {}", sel.display()))?;
            let sel_image = parse_sel(&sel_bytes)
                .with_context(|| format!("failed to validate SEL {}", sel.display()))?;
            let analysis = analyze_input(&game)
                .with_context(|| format!("failed to validate RMGE01 input {}", game.display()))?;
            let resolved_symbols = sel_image
                .resolve_dol_symbols(&analysis.sections)
                .context("failed to resolve SEL symbols through the RMGE01 DOL")?;
            let bindings = rso_image
                .bind_imports(&resolved_symbols)
                .context("failed to bind every RSO import through the SEL table")?;
            let link_layout = canonical_rso_link_layout(&rso_image)?;
            let linked = rso_image
                .apply_relocations(&rso_bytes, &link_layout, &bindings)
                .context("failed to apply every RSO relocation without a runtime linker")?;
            let rso_section = rso_image
                .section(section)
                .with_context(|| format!("RSO section {section} does not exist"))?;
            let file_offset = rso_section
                .file_offset
                .with_context(|| format!("RSO section {section} is BSS and has no code bytes"))?;
            let end = usize::try_from(file_offset)
                .context("RSO section file offset does not fit host memory")?
                .checked_add(
                    usize::try_from(rso_section.size)
                        .context("RSO section size does not fit host memory")?,
                )
                .context("RSO section byte range overflows host memory")?;
            let start = usize::try_from(file_offset)
                .context("RSO section file offset does not fit host memory")?;
            let address = link_layout.section_addresses
                [usize::try_from(section).context("RSO section index does not fit host memory")?];
            let lowering_metadata = rso_image
                .code_section_lowering_metadata(&rso_bytes, section, &link_layout, &bindings)
                .context("failed to derive static RSO lowering metadata")?;
            let body = lower_ppc_code_range_with_entries_and_immediate_overrides(
                address,
                &linked[start..end],
                &lowering_metadata.entries,
                &lowering_metadata.immediate_overrides,
            )
            .with_context(|| {
                format!(
                    "RSO section {section} failed static PPC lowering at canonical address 0x{address:08X}"
                )
            })?;
            println!("RSO static lowering audit: {}", rso.display());
            println!(
                "Validated {} ({}) and lowered section {section} (0x{:X} bytes) at canonical 0x{address:08X}.",
                analysis.game_id, analysis.main_dol_sha1, rso_section.size
            );
            println!(
                "Generated {} native C++ body bytes in memory; no RSO code was written or executed.",
                body.len()
            );
            println!(
                "Recorded {} address-taken entries and {} symbolic immediates for runtime-native relocation.",
                lowering_metadata.entries.len(),
                lowering_metadata.immediate_overrides.len()
            );
        }
        Command::TranslateHomeButtonRso { game, output } => {
            generate_home_button_sidecar(&game, &output)?;
        }
        Command::SelAudit { input, json } => {
            let bytes = fs::read(&input)
                .with_context(|| format!("failed to read SEL {}", input.display()))?;
            let image = parse_sel(&bytes)
                .with_context(|| format!("failed to validate SEL {}", input.display()))?;
            if json {
                println!("{}", serde_json::to_string_pretty(&image)?);
            } else {
                println!("SEL: {}", input.display());
                println!(
                    "Executable: {}; version {}; {} linker symbols",
                    image.executable_name.as_deref().unwrap_or("<unnamed>"),
                    image.version,
                    image.symbols.len()
                );
                println!("No SEL content was copied or executed.");
            }
        }
        Command::DspAudit {
            input,
            start_address,
            json,
        } => {
            let words = read_dsp_words_be(&input)?;
            let audit = audit_dsp_program(start_address, &words)
                .with_context(|| format!("failed to audit DSP ucode {}", input.display()))?;
            if json {
                println!("{}", serde_json::to_string_pretty(&audit)?);
            } else {
                print_dsp_audit(&input, &audit);
            }
        }
        Command::DspLower {
            input,
            start_address,
            function_name,
            output,
            cfg,
            irom,
            coef,
            production,
            timing_profile,
        } => {
            let _timing_binding = if production {
                let timing_profile_path = timing_profile
                    .as_deref()
                    .context("--production requires --timing-profile for DSP lowering")?;
                let bytes = fs::read(timing_profile_path).with_context(|| {
                    format!(
                        "failed to read DSP timing profile {}",
                        timing_profile_path.display()
                    )
                })?;
                let profile = DspTimingProfile::from_json(&bytes).with_context(|| {
                    format!(
                        "failed to parse DSP timing profile {}",
                        timing_profile_path.display()
                    )
                })?;
                let validated = profile.validate_for_production().with_context(|| {
                    format!(
                        "DSP timing profile {} is not production-qualified",
                        timing_profile_path.display()
                    )
                })?;
                Some(
                    require_production_dsp_timing_execution(Some(&validated)).with_context(
                        || format!("cannot lower {} for production", input.display()),
                    )?,
                )
            } else {
                None
            };
            let raw_iram_words = read_dsp_words_be(&input)?;
            let mut words = raw_iram_words.clone();
            let irom_words = read_optional_exact_dsp_words(
                irom.as_deref(),
                DSP_IROM_WORDS,
                "DSP instruction ROM",
            )?;
            let coefficient_words = read_optional_exact_dsp_words(
                coef.as_deref(),
                DSP_COEF_WORDS,
                "DSP coefficient ROM",
            )?;
            if let Some(irom_words) = irom_words.as_deref() {
                splice_irom_words(start_address, &mut words, irom_words)?;
            }
            let use_cfg = cfg || irom_words.is_some() || coefficient_words.is_some();
            let generated = if use_cfg {
                // Seed the reset vector plus the eight standard DSP exception
                // vector slots (two words each) so interrupt handlers are lowered
                // alongside the main entry path.
                let entry_points = dsp_cfg_entry_points(start_address, start_address);
                lower_dsp_program_from_entry_vectors_with_static_memory_and_raw_provenance(
                    start_address,
                    &words,
                    &entry_points,
                    &function_name,
                    DspStaticMemoryImages {
                        irom_words: irom_words.as_deref(),
                        coefficient_words: coefficient_words.as_deref(),
                    },
                    &raw_iram_words,
                )
            } else {
                lower_dsp_program_to_cpp(start_address, &words, &function_name)
            }
            .with_context(|| format!("failed to lower DSP ucode {}", input.display()))?;
            if let Some(parent) = output
                .parent()
                .filter(|parent| !parent.as_os_str().is_empty())
            {
                fs::create_dir_all(parent)
                    .with_context(|| format!("failed to create {}", parent.display()))?;
            }
            write_generated_text_create_new(&output, &generated.cpp)
                .with_context(|| format!("failed to write {}", output.display()))?;
            println!(
                "Lowered DSP ucode: {} words as {} instructions -> {}",
                generated.word_count,
                generated.instruction_count,
                output.display()
            );
            println!(
                "DSP timing qualification: unavailable; no reviewed retail-Wii timing profile is approved, so this source is not production cycle-timed."
            );
        }
        Command::DspScanUcode { input, json } => {
            let scan = scan_rmge01_dsp_ucode_candidates(&input).with_context(|| {
                format!("failed to scan DSP ucode uploads in {}", input.display())
            })?;
            if json {
                println!("{}", serde_json::to_string_pretty(&scan)?);
            } else {
                print_dsp_ucode_scan(&input, &scan);
            }
        }
        Command::DspTimingValidate { input, production } => {
            let bytes = fs::read(&input).with_context(|| {
                format!("failed to read DSP timing profile {}", input.display())
            })?;
            let profile = DspTimingProfile::from_json(&bytes).with_context(|| {
                format!("failed to parse DSP timing profile {}", input.display())
            })?;
            let validated = if production {
                profile.validate_for_production().with_context(|| {
                    format!(
                        "DSP timing profile {} is not production-qualified",
                        input.display()
                    )
                })?
            } else {
                profile
                    .validate()
                    .with_context(|| format!("DSP timing profile {} is invalid", input.display()))?
            };
            println!(
                "Validated DSP timing profile {}: identity {}, {} capture(s), {} exact rule(s), production-qualified={}",
                input.display(),
                validated.profile_identity(),
                validated.profile().captures.len(),
                validated.profile().rules.len(),
                validated.production_qualified()
            );
        }
        Command::DspLowerUcode {
            input,
            candidate,
            entry,
            function_name,
            output,
            cfg,
            irom,
            coef,
        } => {
            let Some(payload) = extract_rmge01_dsp_ucode_candidate(&input, candidate)
                .with_context(|| {
                    format!(
                        "failed to extract DSP ucode candidate {candidate} from {}",
                        input.display()
                    )
                })?
            else {
                bail!(
                    "DSP ucode candidate index {candidate} was not found in {}",
                    input.display()
                );
            };
            let base_address = u16::try_from(payload.candidate.iram_destination / 2)
                .context("DSP IRAM destination does not fit in a word address")?;
            let entry_address = match entry {
                DspUcodeEntry::Init => payload.candidate.dsp_vector,
                DspUcodeEntry::Resume => payload.candidate.dsp_resume_vector,
                DspUcodeEntry::Base => base_address,
            };
            let raw_iram_words = payload.iram_words_be();
            let mut words = raw_iram_words.clone();
            let irom_words = read_optional_exact_dsp_words(
                irom.as_deref(),
                DSP_IROM_WORDS,
                "DSP instruction ROM",
            )?;
            let coefficient_words = read_optional_exact_dsp_words(
                coef.as_deref(),
                DSP_COEF_WORDS,
                "DSP coefficient ROM",
            )?;
            if let Some(irom_words) = irom_words.as_deref() {
                splice_irom_words(base_address, &mut words, irom_words)?;
            }
            let use_cfg = cfg || irom_words.is_some() || coefficient_words.is_some();
            let generated = if use_cfg {
                let entry_points = dsp_cfg_entry_points(base_address, entry_address);
                lower_dsp_program_from_entry_vectors_with_static_memory_and_raw_provenance(
                    base_address,
                    &words,
                    &entry_points,
                    &function_name,
                    DspStaticMemoryImages {
                        irom_words: irom_words.as_deref(),
                        coefficient_words: coefficient_words.as_deref(),
                    },
                    &raw_iram_words,
                )
            } else {
                lower_dsp_program_to_cpp_at_entry(
                    base_address,
                    &words,
                    entry_address,
                    &function_name,
                )
            }
            .with_context(|| {
                format!(
                    "failed to lower DSP ucode candidate {candidate} at entry 0x{entry_address:04X}"
                )
            })?;
            if let Some(parent) = output
                .parent()
                .filter(|parent| !parent.as_os_str().is_empty())
            {
                fs::create_dir_all(parent)
                    .with_context(|| format!("failed to create {}", parent.display()))?;
            }
            write_generated_text_create_new(&output, &generated.cpp)
                .with_context(|| format!("failed to write {}", output.display()))?;
            println!(
                "Lowered DSP ucode candidate {candidate}: descriptor 0x{:08X}, IRAM sha1 {}, base 0x{:04X}, {} entry 0x{:04X}, {} words as {} instructions -> {}",
                payload.candidate.descriptor_address,
                payload.candidate.iram_sha1,
                base_address,
                dsp_ucode_entry_label(entry),
                entry_address,
                generated.word_count,
                generated.instruction_count,
                output.display()
            );
            if let Some(dram_bytes) = &payload.dram_bytes {
                println!(
                    "DRAM payload: {} bytes, sha1 {}",
                    dram_bytes.len(),
                    payload
                        .candidate
                        .dram_sha1
                        .as_deref()
                        .unwrap_or("<missing>")
                );
            } else {
                println!("DRAM payload: none");
            }
            println!("No DSP ucode bytes were written; only generated C++ was written.");
            println!(
                "DSP timing qualification: unavailable; no reviewed retail-Wii timing profile is approved, so this source is not production cycle-timed."
            );
        }
        Command::Identify { input, json } => {
            let identity = disc::identify(&input)?;
            if json {
                println!("{}", serde_json::to_string_pretty(&identity)?);
            } else {
                println!(
                    "{} {} revision {} ({})",
                    identity.game_id, identity.title, identity.revision, identity.format
                );
                println!("{}", identity.message);
            }
            if !identity.supported {
                std::process::exit(2);
            }
        }
        Command::Extract { input, output } => disc::extract(&input, &output)?,
        Command::Generate { game, output } => generate::generate_all(&game, &output)?,
        Command::GenerateDsp { game, output } => {
            let data = generate::resolve_game_data_root(&game)?;
            generate::generate_dsp(&data, &output)?;
        }
        #[cfg(target_os = "windows")]
        Command::PackageContent { game, output } => generate::package_content(&game, &output)?,
        #[cfg(target_os = "windows")]
        Command::BuildPak { input, output } => {
            build_pak::run(&input, &output)?;
        }
        #[cfg(target_os = "windows")]
        Command::BuildBootImage { input, output } => {
            build_boot_image::run(&input, &output)?;
        }
    }
    Ok(())
}

/// Generate the sharded native game module sources for one option set.
pub(crate) fn generate_module_sources(
    input: &Path,
    output: &Path,
    count: usize,
    shard_size: usize,
    shard_source_kib: usize,
    options: ModuleTranslationOptions,
) -> Result<()> {
    let module = translate_module_with_options(input, count, shard_size, shard_source_kib, options)
        .with_context(|| format!("failed to generate module from {}", input.display()))?;
    fs::create_dir_all(output).with_context(|| format!("failed to create {}", output.display()))?;
    for source in module.source_files {
        let path = output.join(source.name);
        write_generated_text_if_changed(&path, &source.contents)
            .with_context(|| format!("failed to write {}", path.display()))?;
    }
    println!(
        "Generated native module sources: {}/{} functions, {} callable entries, {} instructions, {} excluded",
        module.translated_functions,
        module.total_functions,
        module.callable_entries,
        module.translated_instructions,
        module.blocked_functions
    );
    println!("Output: {}", output.display());
    Ok(())
}

/// Statically lower the Home Button RSO into a native sidecar source tree.
pub(crate) fn generate_home_button_sidecar(game: &Path, output: &Path) -> Result<()> {
    let analysis = analyze_input(game)
        .with_context(|| format!("failed to validate RMGE01 input {}", game.display()))?;
    let rso_path = game
        .join("files")
        .join("ModuleData")
        .join("HomeButtonMenuWrapperRSO.rso");
    let sel_path = game.join("files").join("ModuleData").join("product.sel");
    let rso_bytes = fs::read(&rso_path)
        .with_context(|| format!("failed to read Home Button RSO {}", rso_path.display()))?;
    let rso_image = parse_rso(&rso_bytes)
        .with_context(|| format!("failed to validate RSO {}", rso_path.display()))?;
    let sel_bytes = fs::read(&sel_path)
        .with_context(|| format!("failed to read SEL {}", sel_path.display()))?;
    let sel_image = parse_sel(&sel_bytes)
        .with_context(|| format!("failed to validate SEL {}", sel_path.display()))?;
    let resolved_symbols = sel_image
        .resolve_dol_symbols(&analysis.sections)
        .context("failed to resolve SEL symbols through the RMGE01 DOL")?;
    let bindings = rso_image
        .bind_imports(&resolved_symbols)
        .context("failed to bind every RSO import through the SEL table")?;
    let link_layout = canonical_rso_link_layout(&rso_image)?;
    let sidecar = rso_image
        .generate_native_sidecar(&rso_bytes, 1, &link_layout, &bindings)
        .context("failed to statically lower the Home Button RSO")?;
    fs::create_dir_all(output)
        .with_context(|| format!("failed to create sidecar output {}", output.display()))?;
    write_generated_text_if_changed(&output.join("home_button_native.cpp"), &sidecar.cpp)
        .with_context(|| format!("failed to write native sidecar under {}", output.display()))?;
    write_generated_text_if_changed(&output.join("CMakeLists.txt"), &sidecar.cmake)
        .with_context(|| format!("failed to write sidecar CMake under {}", output.display()))?;
    println!("Home Button native sidecar: {}", output.display());
    println!(
        "Validated {} ({}) and statically lowered section {} (0x{:X} bytes).",
        analysis.game_id, analysis.main_dol_sha1, sidecar.code_section, sidecar.code_size
    );
    println!(
        "Generated native source only: {} entries, {} symbolic immediates, RSO SHA-1={}",
        sidecar.entry_count, sidecar.symbolic_immediate_count, sidecar.rso_sha1
    );
    Ok(())
}

fn canonical_rso_link_layout(image: &RsoImage) -> Result<RsoLinkLayout> {
    // This address space exists only while the installer validates and lowers
    // one RSO. It must be near the RMGE01 DOL so every direct REL24 target is
    // representable, but it must not overlap the DOL: the static lowerer uses
    // the resolved branch address to distinguish an RSO-local branch from an
    // imported native DOL call. 0x8100_0000 is within REL24 reach of RMGE01's
    // text while remaining disjoint from its executable sections.
    const BASE: u32 = 0x8100_0000;
    const ALIGNMENT: u32 = 0x20;

    let max_file_offset = image
        .sections
        .iter()
        .filter_map(|section| section.file_range().map(|range| range.end))
        .max()
        .unwrap_or(0);
    let max_file_offset = u32::try_from(max_file_offset)
        .context("RSO file-backed sections do not fit a 32-bit guest layout")?;
    let mut bss_cursor = BASE
        .checked_add(max_file_offset)
        .and_then(|value| value.checked_add(ALIGNMENT - 1))
        .map(|value| value & !(ALIGNMENT - 1))
        .context("canonical RSO BSS base overflows a 32-bit guest address")?;

    let mut section_addresses = Vec::with_capacity(image.sections.len());
    for section in &image.sections {
        let address = if let Some(file_offset) = section.file_offset {
            BASE.checked_add(file_offset)
                .context("canonical RSO file section base overflows a 32-bit guest address")?
        } else if section.size == 0 {
            0
        } else {
            let address = bss_cursor;
            bss_cursor = bss_cursor
                .checked_add(section.size)
                .and_then(|value| value.checked_add(ALIGNMENT - 1))
                .map(|value| value & !(ALIGNMENT - 1))
                .context("canonical RSO BSS layout overflows a 32-bit guest address")?;
            address
        };
        section_addresses.push(address);
    }
    Ok(RsoLinkLayout { section_addresses })
}

fn parse_address(value: &str) -> Result<u32, String> {
    let trimmed = value.strip_prefix("0x").unwrap_or(value);
    u32::from_str_radix(trimmed, 16)
        .map_err(|error| format!("invalid hexadecimal address: {error}"))
}

fn parse_dsp_address(value: &str) -> Result<u16, String> {
    let parsed = parse_address(value)?;
    u16::try_from(parsed).map_err(|_| "DSP word address must fit in 16 bits".to_string())
}

fn analyze(path: &Path) -> Result<GameAnalysis> {
    analyze_input(path).with_context(|| format!("failed to analyze {}", path.display()))
}

fn read_dsp_words_be(path: &Path) -> Result<Vec<u16>> {
    let bytes = fs::read(path).with_context(|| format!("failed to read {}", path.display()))?;
    if bytes.len() % 2 != 0 {
        bail!(
            "DSP ucode blob {} has odd byte length {}; expected big-endian 16-bit words",
            path.display(),
            bytes.len()
        );
    }
    Ok(bytes
        .chunks_exact(2)
        .map(|chunk| u16::from_be_bytes([chunk[0], chunk[1]]))
        .collect())
}

fn read_optional_exact_dsp_words(
    path: Option<&Path>,
    expected_words: usize,
    label: &str,
) -> Result<Option<Vec<u16>>> {
    let Some(path) = path else {
        return Ok(None);
    };
    let words = read_dsp_words_be(path)?;
    if words.len() != expected_words {
        bail!(
            "{label} {} has {} words; expected {expected_words}",
            path.display(),
            words.len()
        );
    }
    Ok(Some(words))
}

fn splice_irom_words(start_address: u16, words: &mut Vec<u16>, irom_words: &[u16]) -> Result<()> {
    const DSP_IROM_BASE: usize = 0x8000;
    let start = usize::from(start_address);
    if start > DSP_IROM_BASE {
        bail!("cannot splice DSP IROM at 0x8000 into image that starts at 0x{start_address:04X}");
    }
    let irom_offset = DSP_IROM_BASE - start;
    if words.len() > irom_offset {
        bail!(
            "DSP ucode image starting at 0x{start_address:04X} already overlaps IROM base 0x8000"
        );
    }
    words.resize(irom_offset, 0xffff);
    words.extend_from_slice(irom_words);
    Ok(())
}

fn dsp_cfg_entry_points(start_address: u16, entry_address: u16) -> Vec<u16> {
    let mut entry_points = vec![entry_address];
    for slot in 0..8 {
        let vector = start_address.wrapping_add(slot * 2);
        if !entry_points.contains(&vector) {
            entry_points.push(vector);
        }
    }
    entry_points
}

fn require_clean_function_audit(analysis: &GameAnalysis) -> Result<()> {
    if !analysis.function_audit.invalid_function_ranges.is_empty() {
        bail!(
            "{} function ranges are outside DOL text sections",
            analysis.function_audit.invalid_function_ranges.len()
        );
    }
    if analysis.function_audit.unrecognized_word_count != 0 {
        bail!(
            "{} words inside configured functions are not recognized Broadway instructions",
            analysis.function_audit.unrecognized_word_count
        );
    }
    Ok(())
}

fn print_human(analysis: &GameAnalysis) {
    println!("Game: {}", analysis.game_id);
    println!("main.dol SHA-1: {}", analysis.main_dol_sha1);
    println!("Entry point: 0x{:08X}", analysis.entry_point);
    println!(
        "DOL sections: {} (BSS 0x{:08X}, {} bytes)",
        analysis.sections.len(),
        analysis.bss_address,
        analysis.bss_size
    );
    println!(
        "Raw text decode: {}/{} recognized, {} unclassified",
        analysis.raw_text_audit.recognized_word_count,
        analysis.raw_text_audit.raw_word_count,
        analysis.raw_text_audit.unrecognized_word_count
    );
    println!(
        "Function decode: {}/{} recognized across {} functions; {} invalid ranges",
        analysis.function_audit.recognized_word_count,
        analysis.function_audit.function_word_count,
        analysis.function_audit.function_count,
        analysis.function_audit.invalid_function_ranges.len()
    );
    println!(
        "Input files: {}, {} bytes; Home Button RSO: {}",
        analysis.assets.file_count,
        analysis.assets.total_bytes,
        if analysis.assets.home_button_rso_present {
            "present"
        } else {
            "not found"
        }
    );
}

// ── Build-pak (Windows only) ──────────────────────────────────────────────────

fn print_dsp_audit(path: &Path, audit: &nebula_recomp_core::DspProgramAudit) {
    println!("DSP ucode: {}", path.display());
    println!("Start address: 0x{:04X}", audit.start_address);
    println!(
        "Decoded: {} words as {} instructions ({} multi-word)",
        audit.decoded_word_count, audit.instruction_count, audit.multi_word_instruction_count
    );
    println!(
        "Parallel instructions: {}",
        audit.parallel_instruction_count
    );
    println!(
        "Control flow: {} ({} conditional, {} calls, {} jumps, {} returns, {} loops, {} halts)",
        audit.control_flow_instruction_count,
        audit.conditional_control_flow_instruction_count,
        audit.call_instruction_count,
        audit.jump_instruction_count,
        audit.return_instruction_count,
        audit.loop_instruction_count,
        audit.halt_instruction_count
    );
    println!(
        "Categories: {} arithmetic, {} memory, {} mode/status, {} address-update",
        audit.arithmetic_instruction_count,
        audit.memory_instruction_count,
        audit.mode_instruction_count,
        audit.address_update_instruction_count
    );
}

fn print_dsp_ucode_scan(path: &Path, scan: &DspUcodeScan) {
    println!("DSP ucode scan input: {}", path.display());
    println!("Candidates: {}", scan.candidate_count);
    for (index, candidate) in scan.candidates.iter().enumerate() {
        println!(
            "  [{}] descriptor 0x{:08X} ({}, {})",
            index,
            candidate.descriptor_address,
            candidate.descriptor_section,
            dsp_descriptor_format_label(candidate.descriptor_format)
        );
        println!(
            "      IRAM: guest 0x{:08X} -> DSP byte 0x{:04X}, {} bytes ({} words), sha1 {}",
            candidate.iram_mmem_addr,
            candidate.iram_destination,
            candidate.iram_byte_len,
            candidate.iram_word_count,
            candidate.iram_sha1
        );
        if let Some(audit) = &candidate.iram_audit {
            println!(
                "      IRAM audit: {} instructions, {} decoded words, {} parallel, {} control-flow, {} halt",
                audit.instruction_count,
                audit.decoded_word_count,
                audit.parallel_instruction_count,
                audit.control_flow_instruction_count,
                audit.halt_instruction_count
            );
        } else if let Some(error) = &candidate.iram_audit_error {
            println!("      IRAM audit: failed: {error}");
        }
        if candidate.dram_byte_len == 0 {
            println!("      DRAM: none");
        } else {
            println!(
                "      DRAM: guest 0x{:08X} -> DSP byte 0x{:04X}, {} bytes, sha1 {}",
                candidate.dram_mmem_addr,
                candidate.dram_destination,
                candidate.dram_byte_len,
                candidate.dram_sha1.as_deref().unwrap_or("<missing>")
            );
        }
        println!(
            "      vectors: entry 0x{:04X}, resume 0x{:04X}",
            candidate.dsp_vector, candidate.dsp_resume_vector
        );
    }
    println!("No DSP ucode bytes were written.");
}

fn dsp_descriptor_format_label(format: DspUcodeDescriptorFormat) -> &'static str {
    match format {
        DspUcodeDescriptorFormat::PackedVectors => "packed vectors",
        DspUcodeDescriptorFormat::ExpandedVectors => "expanded vectors",
    }
}

fn dsp_ucode_entry_label(entry: DspUcodeEntry) -> &'static str {
    match entry {
        DspUcodeEntry::Init => "init",
        DspUcodeEntry::Resume => "resume",
        DspUcodeEntry::Base => "base",
    }
}

#[cfg(test)]
mod tests {
    use super::{write_generated_text_create_new, write_generated_text_if_changed, Cli, Command};
    use clap::Parser;
    use std::fs;

    #[test]
    fn local_lane_module_lowering_is_default_with_explicit_regression_control() {
        let cli = Cli::try_parse_from([
            "nebula-recomp",
            "translate-module",
            "dump",
            "--output",
            "generated/module",
        ])
        .unwrap();
        assert!(matches!(
            cli.command,
            Command::TranslateModule {
                experimental_flat_ram_reads: false,
                experimental_fused_paired_binary: false,
                experimental_inline_scalar_single_binary: false,
                exact_psmtx_local_lanes: true,
                no_exact_psmtx_local_lanes: false,
                exact_psvec_cross_local_lanes: false,
                trace_psvec_cross_guard: false,
                exact_psvec_normalize_local_lanes: false,
                trace_psvec_normalize_guard: false,
                trace_psmtx_guard: false,
                trace_end_frame_spans: false,
                trace_scene_update_spans: false,
                profile_particle_direction_edges: false,
                ..
            }
        ));
        let flat_cli = Cli::try_parse_from([
            "nebula-recomp",
            "translate-module",
            "dump",
            "--output",
            "generated/module",
            "--experimental-flat-ram-reads",
        ])
        .unwrap();
        assert!(matches!(
            flat_cli.command,
            Command::TranslateModule {
                experimental_flat_ram_reads: true,
                ..
            }
        ));
        let inline_cli = Cli::try_parse_from([
            "nebula-recomp",
            "translate-module",
            "dump",
            "--output",
            "generated/module",
            "--experimental-inline-scalar-single-binary",
        ])
        .unwrap();
        assert!(matches!(
            inline_cli.command,
            Command::TranslateModule {
                experimental_inline_scalar_single_binary: true,
                experimental_fused_paired_binary: false,
                ..
            }
        ));
        let fused_cli = Cli::try_parse_from([
            "nebula-recomp",
            "translate-module",
            "dump",
            "--output",
            "generated/module",
            "--experimental-fused-paired-binary",
        ])
        .unwrap();
        assert!(matches!(
            fused_cli.command,
            Command::TranslateModule {
                experimental_fused_paired_binary: true,
                experimental_flat_ram_reads: false,
                ..
            }
        ));
        let cli = Cli::try_parse_from([
            "nebula-recomp",
            "translate-module",
            "dump",
            "--output",
            "generated/module",
            "--trace-psmtx-guard",
        ])
        .unwrap();
        assert!(matches!(
            cli.command,
            Command::TranslateModule {
                exact_psmtx_local_lanes: true,
                no_exact_psmtx_local_lanes: false,
                trace_psmtx_guard: true,
                ..
            }
        ));
        let cli = Cli::try_parse_from([
            "nebula-recomp",
            "translate-module",
            "dump",
            "--output",
            "generated/module",
            "--exact-psmtx-local-lanes",
            "--trace-psmtx-guard",
        ])
        .unwrap();
        assert!(matches!(
            cli.command,
            Command::TranslateModule {
                exact_psmtx_local_lanes: true,
                no_exact_psmtx_local_lanes: false,
                trace_psmtx_guard: true,
                ..
            }
        ));
        let cli = Cli::try_parse_from([
            "nebula-recomp",
            "translate-module",
            "dump",
            "--output",
            "generated/module",
            "--no-exact-psmtx-local-lanes",
        ])
        .unwrap();
        assert!(matches!(
            cli.command,
            Command::TranslateModule {
                no_exact_psmtx_local_lanes: true,
                trace_psmtx_guard: false,
                ..
            }
        ));
        assert!(Cli::try_parse_from([
            "nebula-recomp",
            "translate-module",
            "dump",
            "--output",
            "generated/module",
            "--no-exact-psmtx-local-lanes",
            "--trace-psmtx-guard",
        ])
        .is_err());

        let cli = Cli::try_parse_from([
            "nebula-recomp",
            "translate-module",
            "dump",
            "--output",
            "generated/module",
            "--exact-psvec-cross-local-lanes",
        ])
        .unwrap();
        assert!(matches!(
            cli.command,
            Command::TranslateModule {
                exact_psmtx_local_lanes: true,
                exact_psvec_cross_local_lanes: true,
                trace_psvec_cross_guard: false,
                ..
            }
        ));

        let cli = Cli::try_parse_from([
            "nebula-recomp",
            "translate-module",
            "dump",
            "--output",
            "generated/module",
            "--exact-psvec-cross-local-lanes",
            "--trace-psvec-cross-guard",
        ])
        .unwrap();
        assert!(matches!(
            cli.command,
            Command::TranslateModule {
                exact_psvec_cross_local_lanes: true,
                trace_psvec_cross_guard: true,
                ..
            }
        ));
        assert!(Cli::try_parse_from([
            "nebula-recomp",
            "translate-module",
            "dump",
            "--output",
            "generated/module",
            "--trace-psvec-cross-guard",
        ])
        .is_err());

        let cli = Cli::try_parse_from([
            "nebula-recomp",
            "translate-module",
            "dump",
            "--output",
            "generated/module",
            "--exact-psvec-normalize-local-lanes",
            "--trace-psvec-normalize-guard",
        ])
        .unwrap();
        assert!(matches!(
            cli.command,
            Command::TranslateModule {
                exact_psvec_normalize_local_lanes: true,
                trace_psvec_normalize_guard: true,
                ..
            }
        ));
        assert!(Cli::try_parse_from([
            "nebula-recomp",
            "translate-module",
            "dump",
            "--output",
            "generated/module",
            "--trace-psvec-normalize-guard",
        ])
        .is_err());
    }

    #[test]
    fn end_frame_span_trace_is_an_explicit_full_module_option() {
        let cli = Cli::try_parse_from([
            "nebula-recomp",
            "translate-module",
            "dump",
            "--output",
            "generated/module",
            "--count",
            "0",
            "--trace-end-frame-spans",
        ])
        .unwrap();
        assert!(matches!(
            cli.command,
            Command::TranslateModule {
                count: 0,
                trace_end_frame_spans: true,
                exact_psmtx_local_lanes: true,
                ..
            }
        ));
    }

    #[test]
    fn scene_update_trace_is_an_independent_full_module_option() {
        let cli = Cli::try_parse_from([
            "nebula-recomp",
            "translate-module",
            "dump",
            "--output",
            "generated/module",
            "--count",
            "0",
            "--trace-scene-update-spans",
        ])
        .unwrap();
        assert!(matches!(
            cli.command,
            Command::TranslateModule {
                count: 0,
                trace_scene_update_spans: true,
                trace_end_frame_spans: false,
                exact_psmtx_local_lanes: true,
                ..
            }
        ));
    }

    #[test]
    fn particle_direction_edge_profile_is_an_independent_full_module_option() {
        let cli = Cli::try_parse_from([
            "nebula-recomp",
            "translate-module",
            "dump",
            "--output",
            "generated/module",
            "--count",
            "0",
            "--profile-particle-direction-edges",
        ])
        .unwrap();
        assert!(matches!(
            cli.command,
            Command::TranslateModule {
                count: 0,
                profile_particle_direction_edges: true,
                trace_scene_update_spans: false,
                exact_psmtx_local_lanes: true,
                ..
            }
        ));
    }

    #[test]
    fn nw4r_material_setup_edge_profile_is_an_independent_full_module_option() {
        let cli = Cli::try_parse_from([
            "nebula-recomp",
            "translate-module",
            "dump",
            "--output",
            "generated/module",
            "--count",
            "0",
            "--profile-nw4r-material-setup-edges",
        ])
        .unwrap();
        assert!(matches!(
            cli.command,
            Command::TranslateModule {
                count: 0,
                profile_nw4r_material_setup_edges: true,
                profile_particle_direction_edges: false,
                exact_psmtx_local_lanes: true,
                ..
            }
        ));
    }

    #[test]
    fn nw4r_pane_draw_self_edge_profile_is_an_independent_full_module_option() {
        let cli = Cli::try_parse_from([
            "nebula-recomp",
            "translate-module",
            "dump",
            "--output",
            "generated/module",
            "--count",
            "0",
            "--profile-nw4r-pane-draw-self-edges",
        ])
        .unwrap();
        assert!(matches!(
            cli.command,
            Command::TranslateModule {
                count: 0,
                profile_nw4r_pane_draw_self_edges: true,
                profile_nw4r_material_setup_edges: false,
                exact_psmtx_local_lanes: true,
                ..
            }
        ));
    }

    #[test]
    fn nw4r_pane_hot_child_edge_profile_is_an_independent_full_module_option() {
        let cli = Cli::try_parse_from([
            "nebula-recomp",
            "translate-module",
            "dump",
            "--output",
            "generated/module",
            "--count",
            "0",
            "--profile-nw4r-pane-hot-child-edges",
        ])
        .unwrap();
        assert!(matches!(
            cli.command,
            Command::TranslateModule {
                count: 0,
                profile_nw4r_pane_hot_child_edges: true,
                profile_nw4r_pane_draw_self_edges: false,
                exact_psmtx_local_lanes: true,
                ..
            }
        ));
    }

    #[test]
    fn nw4r_text_format_edge_profile_is_an_independent_full_module_option() {
        let cli = Cli::try_parse_from([
            "nebula-recomp",
            "translate-module",
            "dump",
            "--output",
            "generated/module",
            "--count",
            "0",
            "--profile-nw4r-text-format-edges",
        ])
        .unwrap();
        assert!(matches!(
            cli.command,
            Command::TranslateModule {
                count: 0,
                profile_nw4r_text_format_edges: true,
                profile_nw4r_pane_hot_child_edges: false,
                exact_psmtx_local_lanes: true,
                ..
            }
        ));
    }

    #[test]
    fn nw4r_text_layout_edge_profile_is_an_independent_full_module_option() {
        let cli = Cli::try_parse_from([
            "nebula-recomp",
            "translate-module",
            "dump",
            "--output",
            "generated/module",
            "--count",
            "0",
            "--profile-nw4r-text-layout-edges",
        ])
        .unwrap();
        assert!(matches!(
            cli.command,
            Command::TranslateModule {
                count: 0,
                profile_nw4r_text_layout_edges: true,
                profile_nw4r_text_format_edges: false,
                exact_psmtx_local_lanes: true,
                ..
            }
        ));
    }

    #[test]
    fn jpa_hot_draw_edge_profile_is_an_independent_full_module_option() {
        let cli = Cli::try_parse_from([
            "nebula-recomp",
            "translate-module",
            "dump",
            "--output",
            "generated/module",
            "--count",
            "0",
            "--profile-jpa-hot-draw-edges",
        ])
        .unwrap();
        assert!(matches!(
            cli.command,
            Command::TranslateModule {
                count: 0,
                profile_jpa_hot_draw_edges: true,
                profile_nw4r_text_layout_edges: false,
                exact_psmtx_local_lanes: true,
                ..
            }
        ));
    }

    #[test]
    fn top_self_closure_edge_profile_is_an_independent_full_module_option() {
        let cli = Cli::try_parse_from([
            "nebula-recomp",
            "translate-module",
            "dump",
            "--output",
            "generated/module",
            "--count",
            "0",
            "--profile-top-self-closure-edges",
        ])
        .unwrap();
        assert!(matches!(
            cli.command,
            Command::TranslateModule {
                count: 0,
                profile_top_self_closure_edges: true,
                profile_jpa_hot_draw_edges: false,
                exact_psmtx_local_lanes: true,
                ..
            }
        ));
    }

    #[test]
    fn dsp_lower_production_requires_timing_profile() {
        let error = Cli::try_parse_from([
            "nebula-recomp",
            "dsp-lower",
            "iram.bin",
            "--output",
            "out.cpp",
            "--production",
        ])
        .unwrap_err();
        assert!(error.to_string().contains("--timing-profile"));
    }

    #[test]
    fn dsp_lower_rejects_timing_profile_without_production() {
        let error = Cli::try_parse_from([
            "nebula-recomp",
            "dsp-lower",
            "iram.bin",
            "--output",
            "out.cpp",
            "--timing-profile",
            "timing.json",
        ])
        .unwrap_err();
        assert!(error.to_string().contains("--production"));
    }

    #[test]
    fn dsp_lower_non_production_arguments_remain_optional() {
        let cli = Cli::try_parse_from([
            "nebula-recomp",
            "dsp-lower",
            "iram.bin",
            "--output",
            "out.cpp",
        ])
        .unwrap();
        assert!(matches!(
            cli.command,
            Command::DspLower {
                production: false,
                timing_profile: None,
                ..
            }
        ));
    }

    #[test]
    fn generated_text_write_preserves_unchanged_file_metadata() {
        let temp = tempfile::tempdir().expect("temporary directory is available");
        let output = temp.path().join("functions_0000.cpp");

        assert!(
            write_generated_text_if_changed(&output, "stable contents\n")
                .expect("first generated write succeeds")
        );
        let initial_modified = fs::metadata(&output)
            .expect("generated file metadata is available")
            .modified()
            .expect("generated file modification time is available");

        assert!(
            !write_generated_text_if_changed(&output, "stable contents\n")
                .expect("unchanged generated write succeeds")
        );
        let unchanged_modified = fs::metadata(&output)
            .expect("unchanged generated file metadata is available")
            .modified()
            .expect("unchanged generated file modification time is available");

        assert_eq!(initial_modified, unchanged_modified);
        assert_eq!(
            fs::read_to_string(&output).expect("generated file remains readable"),
            "stable contents\n"
        );
    }

    #[test]
    fn generated_text_write_replaces_changed_bytes() {
        let temp = tempfile::tempdir().expect("temporary directory is available");
        let output = temp.path().join("functions_0000.cpp");
        fs::write(&output, "old contents\n").expect("seed generated file");

        assert!(write_generated_text_if_changed(&output, "new contents\n")
            .expect("changed generated write succeeds"));
        assert_eq!(
            fs::read_to_string(&output).expect("changed generated file is readable"),
            "new contents\n"
        );
    }

    #[test]
    fn dsp_lower_create_new_output_rejects_existing_file_without_clobbering() {
        let temp = tempfile::tempdir().expect("temporary directory is available");
        let output = temp.path().join("rmge01_dsp.cpp");
        fs::write(&output, "owned existing bytes\n").expect("seed existing output");

        let error = write_generated_text_create_new(&output, "replacement bytes\n")
            .expect_err("create-new DSP output must reject an existing path");
        assert_eq!(error.kind(), std::io::ErrorKind::AlreadyExists);
        assert_eq!(
            fs::read_to_string(&output).expect("existing output remains readable"),
            "owned existing bytes\n"
        );
    }
}

#[cfg(target_os = "windows")]
#[path = "build_boot_image.rs"]
mod build_boot_image;

#[cfg(target_os = "windows")]
#[path = "build_pak.rs"]
mod build_pak;

mod disc;
mod generate;
