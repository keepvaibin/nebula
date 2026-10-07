use crate::{
    game::{load_verified_game, GameError},
    guest_abi_effect::{
        analyze_guest_abi, GuestAbiContract, GuestBoundaryFlags, GuestEffectBlock,
        GuestInstructionEffect, GuestRegisterSet, GuestResidencyBoundarySync,
    },
    guest_ppc_effect::{guest_integer_gpr_access, GuestGprAccess},
    rmge01_function_map, DolSection, DolSectionKind, FunctionRange, RMGE01_DOL_SHA1,
};
use powerpc::{Argument, Extensions, Ins, Opcode};
use serde::Serialize;
use sha2::{Digest, Sha256};
use std::{
    collections::{BTreeMap, BTreeSet},
    fmt::Write,
    path::Path,
};
use thiserror::Error;

#[derive(Debug, Error)]
pub enum TranslationError {
    #[error(transparent)]
    Game(#[from] GameError),
    #[error("no configured function begins at 0x{0:08X}")]
    FunctionNotFound(u32),
    #[error("function 0x{address:08X} is not fully contained in a DOL text section")]
    FunctionOutsideText { address: u32 },
    #[error("unsupported instruction {opcode} at 0x{address:08X}")]
    UnsupportedInstruction { address: u32, opcode: String },
    #[error("function 0x{address:08X} does not end in a supported return")]
    MissingReturn { address: u32 },
    #[error("failed to format generated source")]
    Formatting,
    #[error("module shard size must be greater than zero")]
    InvalidShardSize,
    #[error("module shard source budget must be greater than zero and fit in bytes")]
    InvalidShardSourceBudget,
    #[error(
        "generated function 0x{address:08X} requires {generated_bytes} source bytes, exceeding the module shard limit of {limit_bytes} bytes"
    )]
    FunctionExceedsShardSourceBudget {
        address: u32,
        generated_bytes: usize,
        limit_bytes: usize,
    },
    #[error(
        "generated shard {name} requires {generated_bytes} source bytes, exceeding the module shard limit of {limit_bytes} bytes"
    )]
    ShardExceedsSourceBudget {
        name: String,
        generated_bytes: usize,
        limit_bytes: usize,
    },
    #[error("no functions can be translated with the current hard-fail policy")]
    NoTranslatableFunctions,
    #[error("relocated immediate override at 0x{address:08X} ({kind:?}) does not match PPC opcode {opcode}")]
    InvalidImmediateOverride {
        address: u32,
        kind: PpcImmediateKind,
        opcode: String,
    },
    #[error("call-return continuation 0x{address:08X} is not declared as a native entry")]
    CallReturnEntryNotDeclared { address: u32 },
    #[error("exact local paired-single proof failed at 0x{address:08X}: {reason}")]
    LocalPairedProof { address: u32, reason: String },
    #[error("exact typed-region proof failed at 0x{address:08X}: {reason}")]
    TypedRegionProof { address: u32, reason: String },
    #[error("end-frame span trace requires a full module and exact continuation labels: {0}")]
    EndFrameSpanTrace(String),
    #[error("scene-update span trace requires a full module and exact call/return anchors: {0}")]
    SceneUpdateSpanTrace(String),
    #[error(
        "particle-direction edge profile requires a full module and exact direct-call anchors: {0}"
    )]
    ParticleDirectionEdgeProfile(String),
    #[error(
        "NW4R material-setup edge profile requires a full module and exact direct-call anchors: {0}"
    )]
    Nw4rMaterialSetupEdgeProfile(String),
    #[error(
        "NW4R Pane DrawSelf edge profile requires a full module and exact direct-call anchors: {0}"
    )]
    Nw4rPaneDrawSelfEdgeProfile(String),
    #[error(
        "NW4R Pane hot-child edge profile requires a full module and exact direct-call anchors: {0}"
    )]
    Nw4rPaneHotChildEdgeProfile(String),
    #[error(
        "NW4R text-format edge profile requires a full module and exact direct-call anchors: {0}"
    )]
    Nw4rTextFormatEdgeProfile(String),
    #[error(
        "NW4R text-layout edge profile requires a full module and exact direct-call anchors: {0}"
    )]
    Nw4rTextLayoutEdgeProfile(String),
    #[error("JPA hot-draw edge profile requires a full module and exact direct-call anchors: {0}")]
    JpaHotDrawEdgeProfile(String),
    #[error(
        "top self-time closure edge profile requires a full module and exact direct-call anchors: {0}"
    )]
    TopSelfClosureEdgeProfile(String),
}

/// Relocation form of one PPC instruction's 16-bit immediate field.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PpcImmediateKind {
    /// ELF/RVL `ADDR16_LO`: sign-extended low half of the resolved address.
    Low16,
    /// ELF/RVL `ADDR16_HA`: high-adjusted half of the resolved address.
    HighAdjusted16,
}

/// Install-time replacement for a relocated PPC immediate, emitted directly
/// as a C++ expression.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PpcImmediateOverride {
    pub kind: PpcImmediateKind,
    pub expression: String,
}

#[derive(Clone, Debug)]
pub struct TranslationUnit {
    pub address: u32,
    pub size: u32,
    pub instruction_count: u32,
    pub cpp: String,
}

#[derive(Clone, Debug)]
pub struct GeneratedSource {
    pub name: String,
    pub contents: String,
}

#[derive(Clone, Debug)]
pub struct TranslationModule {
    pub total_functions: u64,
    pub translated_functions: u64,
    pub callable_entries: u64,
    pub translated_instructions: u64,
    pub blocked_functions: u64,
    pub source_files: Vec<GeneratedSource>,
}

#[derive(Clone, Debug, Serialize)]
pub struct TranslationCoverage {
    pub total_functions: u64,
    pub translatable_functions: u64,
    pub interior_entry_points: u64,
    pub translatable_instructions: u64,
    pub blocked_functions: u64,
    pub first_blocker_counts: BTreeMap<String, u64>,
    pub first_blocker_examples: BTreeMap<String, Vec<String>>,
}

// RMGE01's largest translated function, with its lazy-FPU restart table, is
// slightly over 336 KiB and must fit in one shard.
pub const DEFAULT_MODULE_SHARD_SOURCE_KIB: usize = 384;
const KIBIBYTE: usize = 1024;

/// Install-time lowering controls. PSMTX local-lane reuse is on by default;
/// trace instrumentation is off.
#[derive(Clone, Copy, Debug)]
pub struct ModuleTranslationOptions {
    /// Local GPR residency for straight-line integer leaves and guarded
    /// direct-call callers whose effects the typed analysis proves.
    pub guest_resident_leaf: bool,
    /// Private typed-GPR region for RMGE01 0x80165478. Public and interior
    /// entries keep the full-context translation.
    pub typed_region_80165478: bool,
    /// Flat RAM read helpers derived from WiiCompiled. The Galaxy ABI checks
    /// the whole RAM span and keeps the device/fault fallback.
    pub flat_ram_reads: bool,
    /// Experimental fused paired-single arithmetic/commit helper. Public
    /// entries, operand validation, FPSCR and precise FP faults are unaffected.
    pub fused_paired_binary: bool,
    /// Experimental inline widened-single scalar add/subtract/multiply.
    /// Unhandled inputs, modes and results use the original lowering.
    pub inline_scalar_single_binary: bool,
    /// Straight-line integer GPR residency across a whole function body.
    ///
    /// Every other field here is a local lowering decision; this one is a
    /// function-wide property. The translated body names `context->gpr[N]` for
    /// *every* guest register operand, so the host compiler cannot keep the
    /// guest register file in registers: each operand is an independent 4-byte
    /// access into `PpcContext`, and an opaque call in between (every
    /// call-return checkpoint, every guest load or store) forces every later
    /// read to be re-issued. The generated module therefore performs roughly
    /// one `PpcContext` access per guest instruction.
    ///
    /// With this option the body instead keeps every directly-indexed GPR in a
    /// function-local variable, so the host compiler's own register allocator
    /// owns the guest register file across the straight-line body. Only the
    /// function exit writes the locals back. See
    /// [`apply_integer_gpr_residency`] for the eligibility rule, and why
    /// interior entries and runtime-indexed GPR access are excluded.
    pub guest_resident_integer: bool,
    pub exact_psmtx_local_lanes: bool,
    pub exact_psvec_cross_local_lanes: bool,
    pub exact_psvec_normalize_local_lanes: bool,
    pub trace_psmtx_guard: bool,
    pub trace_psvec_cross_guard: bool,
    pub trace_psvec_normalize_guard: bool,
    pub trace_end_frame_spans: bool,
    pub trace_scene_update_spans: bool,
    pub profile_particle_direction_edges: bool,
    pub profile_nw4r_material_setup_edges: bool,
    pub profile_nw4r_pane_draw_self_edges: bool,
    pub profile_nw4r_pane_hot_child_edges: bool,
    pub profile_nw4r_text_format_edges: bool,
    pub profile_nw4r_text_layout_edges: bool,
    pub profile_jpa_hot_draw_edges: bool,
    pub profile_top_self_closure_edges: bool,
}

impl Default for ModuleTranslationOptions {
    fn default() -> Self {
        Self {
            guest_resident_leaf: false,
            typed_region_80165478: false,
            flat_ram_reads: false,
            fused_paired_binary: false,
            inline_scalar_single_binary: false,
            guest_resident_integer: false,
            exact_psmtx_local_lanes: true,
            exact_psvec_cross_local_lanes: false,
            exact_psvec_normalize_local_lanes: false,
            trace_psmtx_guard: false,
            trace_psvec_cross_guard: false,
            trace_psvec_normalize_guard: false,
            trace_end_frame_spans: false,
            trace_scene_update_spans: false,
            profile_particle_direction_edges: false,
            profile_nw4r_material_setup_edges: false,
            profile_nw4r_pane_draw_self_edges: false,
            profile_nw4r_pane_hot_child_edges: false,
            profile_nw4r_text_format_edges: false,
            profile_nw4r_text_layout_edges: false,
            profile_jpa_hot_draw_edges: false,
            profile_top_self_closure_edges: false,
        }
    }
}

struct PsmtxLocalExperiment {
    body: String,
    trace_guard: bool,
}

struct PsvecCrossLocalExperiment {
    body: String,
    trace_guard: bool,
}

struct PsvecNormalizeLocalExperiment {
    body: String,
    trace_guard: bool,
}

#[derive(Default)]
struct LocalPairedExperiments<'a> {
    psmtx: Option<&'a PsmtxLocalExperiment>,
    psvec_cross: Option<&'a PsvecCrossLocalExperiment>,
    psvec_normalize: Option<&'a PsvecNormalizeLocalExperiment>,
    typed_region_80165478: Option<&'a str>,
}

/// Lowers a validated PPC code range into a native C++ function body.
/// Callers supply function boundaries and apply relocations first.
pub fn lower_ppc_code_range(address: u32, bytes: &[u8]) -> Result<String, TranslationError> {
    lower_words(address, bytes)
}

/// Lowers a validated PPC range whose selected 16-bit fields are symbolic
/// relocation expressions. `entries` are address-taken starts in the range
/// and get the normal resume dispatch. A relocation that does not match a
/// supported PPC immediate is an error.
pub fn lower_ppc_code_range_with_entries_and_immediate_overrides(
    address: u32,
    bytes: &[u8],
    entries: &BTreeSet<u32>,
    immediate_overrides: &BTreeMap<u32, PpcImmediateOverride>,
) -> Result<String, TranslationError> {
    lower_ppc_code_range_with_entries_call_returns_and_immediate_overrides(
        address,
        bytes,
        entries,
        &BTreeSet::new(),
        immediate_overrides,
    )
}

/// Also takes explicit return continuations for linked indirect calls, needed
/// when control resumes after an OS context unwind instead of returning
/// through the C++ caller frame. Each continuation must also be an entry.
pub fn lower_ppc_code_range_with_entries_call_returns_and_immediate_overrides(
    address: u32,
    bytes: &[u8],
    entries: &BTreeSet<u32>,
    call_return_entries: &BTreeSet<u32>,
    immediate_overrides: &BTreeMap<u32, PpcImmediateOverride>,
) -> Result<String, TranslationError> {
    if let Some(address) = call_return_entries
        .iter()
        .find(|entry| !entries.contains(entry))
    {
        return Err(TranslationError::CallReturnEntryNotDeclared { address: *address });
    }
    lower_words_with_options(
        address,
        bytes,
        entries,
        call_return_entries,
        None,
        None,
        Some(immediate_overrides),
        false,
    )
}

pub fn translate_one(input: &Path, address: u32) -> Result<TranslationUnit, TranslationError> {
    let loaded = load_verified_game(input)?;
    let function = rmge01_function_map()
        .into_iter()
        .find(|function| function.address == address)
        .ok_or(TranslationError::FunctionNotFound(address))?;
    let section = containing_section(&loaded.dol.sections, function)
        .ok_or(TranslationError::FunctionOutsideText { address })?;
    let section_relative = (function.address - section.address) as usize;
    let file_start = section.file_offset as usize + section_relative;
    let file_end = file_start + function.size as usize;
    let bytes = &loaded.dol_bytes[file_start..file_end];
    let body = lower_words(address, bytes)?;
    let cpp = emit_module(
        function,
        &body,
        function_requires_fpu(bytes) || native_substitution_uses_fpu(function.address),
    )?;

    Ok(TranslationUnit {
        address,
        size: function.size,
        instruction_count: function.size / 4,
        cpp,
    })
}

pub fn translate_module(
    input: &Path,
    count: usize,
    shard_size: usize,
) -> Result<TranslationModule, TranslationError> {
    translate_module_with_source_budget(input, count, shard_size, DEFAULT_MODULE_SHARD_SOURCE_KIB)
}

pub fn translate_module_with_source_budget(
    input: &Path,
    count: usize,
    shard_size: usize,
    shard_source_kib: usize,
) -> Result<TranslationModule, TranslationError> {
    translate_module_with_options(
        input,
        count,
        shard_size,
        shard_source_kib,
        ModuleTranslationOptions::default(),
    )
}

fn selected_psmtx_local_target(
    ranges: &[FunctionRange],
    trace_guard: bool,
) -> Result<Option<FunctionRange>, TranslationError> {
    if let Some(function) = ranges
        .iter()
        .find(|function| function.address == 0x804B_5F3C)
    {
        return Ok(Some(*function));
    }
    if trace_guard {
        return Err(local_paired_proof_error(
            0x804B_5F3C,
            "guard trace target not selected; use --count 0",
        ));
    }
    // Partial module emission is supported: if the selected prefix does not
    // contain this function there is nothing to transform.
    Ok(None)
}

pub fn translate_module_with_options(
    input: &Path,
    count: usize,
    shard_size: usize,
    shard_source_kib: usize,
    options: ModuleTranslationOptions,
) -> Result<TranslationModule, TranslationError> {
    if options.trace_scene_update_spans && count != 0 {
        return Err(TranslationError::SceneUpdateSpanTrace(
            "use --count 0".to_owned(),
        ));
    }
    if options.trace_end_frame_spans && count != 0 {
        return Err(TranslationError::EndFrameSpanTrace(
            "use --count 0".to_owned(),
        ));
    }
    if options.profile_particle_direction_edges && count != 0 {
        return Err(TranslationError::ParticleDirectionEdgeProfile(
            "use --count 0".to_owned(),
        ));
    }
    if options.profile_nw4r_material_setup_edges && count != 0 {
        return Err(TranslationError::Nw4rMaterialSetupEdgeProfile(
            "use --count 0".to_owned(),
        ));
    }
    if options.profile_nw4r_pane_draw_self_edges && count != 0 {
        return Err(TranslationError::Nw4rPaneDrawSelfEdgeProfile(
            "use --count 0".to_owned(),
        ));
    }
    if options.profile_nw4r_pane_hot_child_edges && count != 0 {
        return Err(TranslationError::Nw4rPaneHotChildEdgeProfile(
            "use --count 0".to_owned(),
        ));
    }
    if options.profile_nw4r_text_format_edges && count != 0 {
        return Err(TranslationError::Nw4rTextFormatEdgeProfile(
            "use --count 0".to_owned(),
        ));
    }
    if options.profile_nw4r_text_layout_edges && count != 0 {
        return Err(TranslationError::Nw4rTextLayoutEdgeProfile(
            "use --count 0".to_owned(),
        ));
    }
    if options.profile_jpa_hot_draw_edges && count != 0 {
        return Err(TranslationError::JpaHotDrawEdgeProfile(
            "use --count 0".to_owned(),
        ));
    }
    if options.profile_top_self_closure_edges && count != 0 {
        return Err(TranslationError::TopSelfClosureEdgeProfile(
            "use --count 0".to_owned(),
        ));
    }
    if options.trace_psmtx_guard && !options.exact_psmtx_local_lanes {
        return Err(local_paired_proof_error(
            0x804B_5F3C,
            "guard tracing requires the explicit local-lane experiment",
        ));
    }
    if options.trace_psvec_cross_guard && !options.exact_psvec_cross_local_lanes {
        return Err(local_paired_proof_error(
            0x804B_6CB8,
            "guard tracing requires the explicit local-lane experiment",
        ));
    }
    if options.trace_psvec_normalize_guard && !options.exact_psvec_normalize_local_lanes {
        return Err(local_paired_proof_error(
            0x804B_6BCC,
            "guard tracing requires the explicit local-lane experiment",
        ));
    }
    if shard_size == 0 {
        return Err(TranslationError::InvalidShardSize);
    }
    let shard_source_bytes = shard_source_kib
        .checked_mul(KIBIBYTE)
        .filter(|budget| *budget != 0)
        .ok_or(TranslationError::InvalidShardSourceBudget)?;
    let loaded = load_verified_game(input)?;
    let functions = rmge01_function_map();
    let mut translated = Vec::new();
    let mut all_translatable = 0_u64;
    let mut blocked = 0_u64;

    for function in &functions {
        let result = containing_section(&loaded.dol.sections, *function)
            .ok_or(TranslationError::FunctionOutsideText {
                address: function.address,
            })
            .and_then(|section| {
                let section_relative = (function.address - section.address) as usize;
                let file_start = section.file_offset as usize + section_relative;
                let file_end = file_start + function.size as usize;
                lower_words(function.address, &loaded.dol_bytes[file_start..file_end])
            });
        match result {
            Ok(body) => {
                all_translatable += 1;
                if count == 0 || translated.len() < count {
                    translated.push((*function, body));
                }
            }
            Err(
                TranslationError::UnsupportedInstruction { .. }
                | TranslationError::MissingReturn { .. }
                | TranslationError::FunctionOutsideText { .. },
            ) => blocked += 1,
            Err(other) => return Err(other),
        }
    }

    let translated_instructions = translated
        .iter()
        .map(|(function, _)| u64::from(function.size / 4))
        .sum();
    let ranges = translated
        .iter()
        .map(|(function, _)| *function)
        .collect::<Vec<_>>();
    let mut exact_fpu_functions = BTreeSet::new();
    for function in &ranges {
        let bytes = function_bytes(&loaded.dol.sections, &loaded.dol_bytes, *function).ok_or(
            TranslationError::FunctionOutsideText {
                address: function.address,
            },
        )?;
        if function_requires_fpu(bytes) || native_substitution_uses_fpu(function.address) {
            exact_fpu_functions.insert(function.address);
        }
    }
    let call_return_entries =
        call_return_continuations(&loaded.dol.sections, &loaded.dol_bytes, &ranges);
    let aliases = discover_alias_entries_with_call_returns(
        &loaded.dol.sections,
        &loaded.dol_bytes,
        &ranges,
        &call_return_entries,
    );
    let mut entries_by_function: BTreeMap<u32, BTreeSet<u32>> = BTreeMap::new();
    for (alias, owner) in &aliases {
        entries_by_function
            .entry(*owner)
            .or_default()
            .insert(*alias);
    }
    let mut call_return_entries_by_function: BTreeMap<u32, BTreeSet<u32>> = BTreeMap::new();
    for entry in &call_return_entries {
        let owner = mapped_function_containing(&ranges, *entry)
            .expect("call-return continuation must remain mapped");
        call_return_entries_by_function
            .entry(owner.address)
            .or_default()
            .insert(*entry);
    }
    let callable_entries = translated
        .iter()
        .map(|(function, _)| (function.address, function.address))
        .chain(aliases.iter().copied())
        .collect::<BTreeMap<_, _>>();
    let empty_entries = BTreeSet::new();
    // As in WiiCompiled, caller residency narrows a call boundary only for a
    // callee with a fully known contract: the proven integer leaves decoded
    // below. Substitutions, FP bodies and leaves with interior entries stay
    // full fences.
    let mut resident_leaf_contracts = BTreeMap::new();
    if options.guest_resident_leaf {
        for (function, _) in &translated {
            if exact_fpu_functions.contains(&function.address)
                || native_function_body(function.address).is_some()
            {
                continue;
            }
            let bytes = function_bytes(&loaded.dol.sections, &loaded.dol_bytes, *function).ok_or(
                TranslationError::FunctionOutsideText {
                    address: function.address,
                },
            )?;
            if let Some((_, contract)) = analyze_resident_integer_leaf(
                function.address,
                bytes,
                entries_by_function
                    .get(&function.address)
                    .unwrap_or(&empty_entries),
                call_return_entries_by_function
                    .get(&function.address)
                    .unwrap_or(&empty_entries),
            ) {
                resident_leaf_contracts.insert(function.address, contract);
            }
        }
    }
    for (function, body) in &mut translated {
        let entries = entries_by_function
            .get(&function.address)
            .unwrap_or(&empty_entries);
        let call_return_entries = call_return_entries_by_function
            .get(&function.address)
            .unwrap_or(&empty_entries);
        let bytes = function_bytes(&loaded.dol.sections, &loaded.dol_bytes, *function).ok_or(
            TranslationError::FunctionOutsideText {
                address: function.address,
            },
        )?;
        *body = if options.guest_resident_leaf
            && !exact_fpu_functions.contains(&function.address)
            && native_function_body(function.address).is_none()
        {
            match lower_resident_integer_leaf(function.address, bytes, entries, call_return_entries)
            {
                Some(resident) => resident,
                None => match lower_resident_integer_direct_calls(
                    function.address,
                    bytes,
                    entries,
                    call_return_entries,
                    &callable_entries,
                    &exact_fpu_functions,
                    &resident_leaf_contracts,
                )? {
                    Some(resident) => resident,
                    None => apply_residency_to_body(
                        function.address,
                        lower_words_for_module_with_call_return_entries_and_exact_fpu_flat_reads(
                            function.address,
                            bytes,
                            entries,
                            call_return_entries,
                            &callable_entries,
                            &exact_fpu_functions,
                            &options,
                        )?,
                        entries,
                        &options,
                    ),
                },
            }
        } else {
            apply_residency_to_body(
                function.address,
                lower_words_for_module_with_call_return_entries_and_exact_fpu_flat_reads(
                    function.address,
                    bytes,
                    entries,
                    call_return_entries,
                    &callable_entries,
                    &exact_fpu_functions,
                    &options,
                )?,
                entries,
                &options,
            )
        };
    }
    let typed_region_80165478 = if options.typed_region_80165478 {
        const ADDRESS: u32 = 0x8016_5478;
        let function = ranges
            .iter()
            .find(|function| function.address == ADDRESS)
            .ok_or(TranslationError::FunctionNotFound(ADDRESS))?;
        if native_function_body(ADDRESS).is_some() || exact_fpu_functions.contains(&ADDRESS) {
            return Err(TranslationError::TypedRegionProof {
                address: ADDRESS,
                reason: "owner gained a native substitution or exact-FPU path".to_owned(),
            });
        }
        let bytes = function_bytes(&loaded.dol.sections, &loaded.dol_bytes, *function)
            .ok_or(TranslationError::FunctionOutsideText { address: ADDRESS })?;
        let entries = entries_by_function.get(&ADDRESS).unwrap_or(&empty_entries);
        let returns = call_return_entries_by_function
            .get(&ADDRESS)
            .unwrap_or(&empty_entries);
        let mask = typed_region_80165478_mask(bytes, entries, returns)?;
        Some(lower_words_with_config(
            ADDRESS,
            bytes,
            entries,
            returns,
            LoweringConfig {
                callable_entries: Some(&callable_entries),
                exact_fpu_functions: Some(&exact_fpu_functions),
                flat_ram_reads: options.flat_ram_reads,
                typed_region_gpr_mask: mask,
                ..LoweringConfig::default()
            },
        )?)
    } else {
        None
    };
    let psmtx_experiment = if options.exact_psmtx_local_lanes {
        match selected_psmtx_local_target(&ranges, options.trace_psmtx_guard)? {
            Some(function) => {
                let bytes = function_bytes(&loaded.dol.sections, &loaded.dol_bytes, function)
                    .ok_or(TranslationError::FunctionOutsideText {
                        address: function.address,
                    })?;
                validate_psmtx_local_memory_contract(bytes)?;
                Some(PsmtxLocalExperiment {
                    body: lower_words_with_config(
                        function.address,
                        bytes,
                        &BTreeSet::new(),
                        &BTreeSet::new(),
                        LoweringConfig {
                            local_lane_profile: LocalLaneProfile::PsmtxConcat,
                            ..LoweringConfig::default()
                        },
                    )?,
                    trace_guard: options.trace_psmtx_guard,
                })
            }
            None => None,
        }
    } else {
        None
    };
    let psvec_cross_experiment = if options.exact_psvec_cross_local_lanes {
        let function = ranges
            .iter()
            .find(|function| function.address == 0x804B_6CB8)
            .ok_or_else(|| {
                local_paired_proof_error(0x804B_6CB8, "target not selected; use --count 0")
            })?;
        let bytes = function_bytes(&loaded.dol.sections, &loaded.dol_bytes, *function).ok_or(
            TranslationError::FunctionOutsideText {
                address: function.address,
            },
        )?;
        validate_psvec_cross_local_memory_contract(bytes)?;
        Some(PsvecCrossLocalExperiment {
            body: lower_words_with_config(
                function.address,
                bytes,
                &BTreeSet::new(),
                &BTreeSet::new(),
                LoweringConfig {
                    local_lane_profile: LocalLaneProfile::PsvecCross,
                    ..LoweringConfig::default()
                },
            )?,
            trace_guard: options.trace_psvec_cross_guard,
        })
    } else {
        None
    };
    let psvec_normalize_experiment = if options.exact_psvec_normalize_local_lanes {
        let function = ranges
            .iter()
            .find(|function| function.address == 0x804B_6BCC)
            .ok_or_else(|| {
                local_paired_proof_error(0x804B_6BCC, "target not selected; use --count 0")
            })?;
        let bytes = function_bytes(&loaded.dol.sections, &loaded.dol_bytes, *function).ok_or(
            TranslationError::FunctionOutsideText {
                address: function.address,
            },
        )?;
        validate_psvec_normalize_local_memory_contract(bytes)?;
        Some(PsvecNormalizeLocalExperiment {
            body: lower_words_with_config(
                function.address,
                bytes,
                &BTreeSet::new(),
                &BTreeSet::new(),
                LoweringConfig {
                    local_lane_profile: LocalLaneProfile::PsvecNormalize,
                    ..LoweringConfig::default()
                },
            )?,
            trace_guard: options.trace_psvec_normalize_guard,
        })
    } else {
        None
    };
    let callable_entries = translated.len() as u64 + aliases.len() as u64;
    let mut source_files = emit_sharded_module_with_experiment(
        &translated,
        &aliases,
        shard_size,
        shard_source_bytes,
        loaded.dol.entry_point,
        &exact_fpu_functions,
        LocalPairedExperiments {
            psmtx: psmtx_experiment.as_ref(),
            psvec_cross: psvec_cross_experiment.as_ref(),
            psvec_normalize: psvec_normalize_experiment.as_ref(),
            typed_region_80165478: typed_region_80165478.as_deref(),
        },
    )?;
    if options.trace_end_frame_spans {
        insert_end_frame_span_markers(&mut source_files, shard_source_bytes)?;
    }
    if options.trace_scene_update_spans {
        insert_scene_update_span_markers(&mut source_files, shard_source_bytes)?;
    }
    if options.profile_particle_direction_edges {
        insert_particle_direction_edge_profile(&mut source_files, shard_source_bytes)?;
    }
    if options.profile_nw4r_material_setup_edges {
        insert_nw4r_material_setup_edge_profile(&mut source_files, shard_source_bytes)?;
    }
    if options.profile_nw4r_pane_draw_self_edges {
        insert_nw4r_pane_draw_self_edge_profile(&mut source_files, shard_source_bytes)?;
    }
    if options.profile_nw4r_pane_hot_child_edges {
        insert_nw4r_pane_hot_child_edge_profile(&mut source_files, shard_source_bytes)?;
    }
    if options.profile_nw4r_text_format_edges {
        insert_nw4r_text_format_edge_profile(&mut source_files, shard_source_bytes)?;
    }
    if options.profile_nw4r_text_layout_edges {
        insert_nw4r_text_layout_edge_profile(&mut source_files, shard_source_bytes)?;
    }
    if options.profile_jpa_hot_draw_edges {
        insert_jpa_hot_draw_edge_profile(&mut source_files, shard_source_bytes)?;
    }
    if options.profile_top_self_closure_edges {
        insert_top_self_closure_edge_profile(&mut source_files, shard_source_bytes)?;
    }
    validate_function_shard_source_budget(&source_files, shard_source_bytes)?;
    Ok(TranslationModule {
        total_functions: functions.len() as u64,
        translated_functions: translated.len() as u64,
        callable_entries,
        translated_instructions,
        blocked_functions: blocked + (all_translatable - translated.len() as u64),
        source_files,
    })
}

pub fn translation_coverage(input: &Path) -> Result<TranslationCoverage, TranslationError> {
    let loaded = load_verified_game(input)?;
    let functions = rmge01_function_map();
    let mut coverage = TranslationCoverage {
        total_functions: functions.len() as u64,
        translatable_functions: 0,
        interior_entry_points: 0,
        translatable_instructions: 0,
        blocked_functions: 0,
        first_blocker_counts: BTreeMap::new(),
        first_blocker_examples: BTreeMap::new(),
    };

    for function in &functions {
        let result = containing_section(&loaded.dol.sections, *function)
            .ok_or(TranslationError::FunctionOutsideText {
                address: function.address,
            })
            .and_then(|section| {
                let section_relative = (function.address - section.address) as usize;
                let file_start = section.file_offset as usize + section_relative;
                let file_end = file_start + function.size as usize;
                lower_words(function.address, &loaded.dol_bytes[file_start..file_end])
            });

        match result {
            Ok(_) => {
                coverage.translatable_functions += 1;
                coverage.translatable_instructions += (function.size / 4) as u64;
            }
            Err(error) => {
                coverage.blocked_functions += 1;
                let (blocker, example) = match error {
                    TranslationError::UnsupportedInstruction { address, opcode } => (
                        opcode,
                        format!(
                            "function 0x{:08X}, instruction 0x{address:08X}",
                            function.address
                        ),
                    ),
                    TranslationError::MissingReturn { address } => (
                        "missing_return".to_owned(),
                        format!("function 0x{address:08X}"),
                    ),
                    TranslationError::FunctionOutsideText { .. } => (
                        "function_outside_text".to_owned(),
                        format!("function 0x{:08X}", function.address),
                    ),
                    other => return Err(other),
                };
                *coverage
                    .first_blocker_counts
                    .entry(blocker.clone())
                    .or_default() += 1;
                let examples = coverage.first_blocker_examples.entry(blocker).or_default();
                if examples.len() < 8 {
                    examples.push(example);
                }
            }
        }
    }
    coverage.interior_entry_points =
        discover_external_interior_targets(&loaded.dol.sections, &loaded.dol_bytes, &functions)
            .len() as u64;

    Ok(coverage)
}

fn containing_section(sections: &[DolSection], function: FunctionRange) -> Option<&DolSection> {
    let function_end = function.address.checked_add(function.size)?;
    sections.iter().find(|section| {
        let section_end = section.address.checked_add(section.size);
        section_end.is_some_and(|end| {
            function.address >= section.address
                && function_end <= end
                && section.name.starts_with("text")
        })
    })
}

fn function_bytes<'a>(
    sections: &[DolSection],
    dol_bytes: &'a [u8],
    function: FunctionRange,
) -> Option<&'a [u8]> {
    let section = containing_section(sections, function)?;
    let section_relative = (function.address - section.address) as usize;
    let file_start = section.file_offset as usize + section_relative;
    let file_end = file_start.checked_add(function.size as usize)?;
    dol_bytes.get(file_start..file_end)
}

fn mapped_function_containing(functions: &[FunctionRange], address: u32) -> Option<FunctionRange> {
    let mut low = 0;
    let mut high = functions.len();
    while low < high {
        let middle = low + (high - low) / 2;
        if functions[middle].address <= address {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    let function = *functions.get(low.checked_sub(1)?)?;
    let end = function.address.checked_add(function.size)?;
    (address < end).then_some(function)
}

// Broadway raises exception 7 before any FP or paired-single instruction
// while MSR[FP] is clear. The handler's same-PC RFI is normally caught inside
// the AOT guard, but a pending asynchronous exception taken at that RFI
// abandons the native frame, so interior guarded PCs also get static
// continuation aliases.
fn opcode_requires_fpu(opcode: Opcode) -> bool {
    matches!(
        opcode,
        Opcode::PsqL
            | Opcode::PsqLu
            | Opcode::PsqLx
            | Opcode::PsqLux
            | Opcode::PsqSt
            | Opcode::PsqStu
            | Opcode::PsqStx
            | Opcode::PsqStux
            | Opcode::Lfs
            | Opcode::Lfsu
            | Opcode::Lfd
            | Opcode::Lfdu
            | Opcode::Stfs
            | Opcode::Stfsu
            | Opcode::Stfd
            | Opcode::Stfdu
            | Opcode::Lfsx
            | Opcode::Lfsux
            | Opcode::Lfdx
            | Opcode::Lfdux
            | Opcode::Stfsx
            | Opcode::Stfsux
            | Opcode::Stfdx
            | Opcode::Stfdux
            | Opcode::Fmr
            | Opcode::Fneg
            | Opcode::Fabs
            | Opcode::Frsp
            | Opcode::Fctiwz
            | Opcode::Fadd
            | Opcode::Fadds
            | Opcode::Fsub
            | Opcode::Fsubs
            | Opcode::Fmul
            | Opcode::Fmuls
            | Opcode::Fdiv
            | Opcode::Fdivs
            | Opcode::Fmadd
            | Opcode::Fmadds
            | Opcode::Fmsub
            | Opcode::Fmsubs
            | Opcode::Fnmadd
            | Opcode::Fnmadds
            | Opcode::Fnmsub
            | Opcode::Fnmsubs
            | Opcode::Fsel
            | Opcode::Fres
            | Opcode::Frsqrte
            | Opcode::PsMr
            | Opcode::PsNeg
            | Opcode::PsAbs
            | Opcode::PsNabs
            | Opcode::PsMerge00
            | Opcode::PsMerge01
            | Opcode::PsMerge10
            | Opcode::PsMerge11
            | Opcode::PsAdd
            | Opcode::PsSub
            | Opcode::PsMul
            | Opcode::PsMuls0
            | Opcode::PsMuls1
            | Opcode::PsDiv
            | Opcode::PsMadd
            | Opcode::PsMsub
            | Opcode::PsNmadd
            | Opcode::PsNmsub
            | Opcode::PsMadds0
            | Opcode::PsMadds1
            | Opcode::PsSum0
            | Opcode::PsSum1
            | Opcode::PsCmpo0
            | Opcode::PsCmpo1
            | Opcode::PsCmpu0
            | Opcode::PsCmpu1
            | Opcode::PsRes
            | Opcode::PsRsqrte
            | Opcode::PsSel
            | Opcode::Mffs
            | Opcode::Mtfsb0
            | Opcode::Mtfsb1
            | Opcode::Mtfsf
            | Opcode::Mtfsfi
            | Opcode::Fcmpu
            | Opcode::Fcmpo
    )
}

// One MSR[FP] check covers a straight-line region, since ordinary PPC
// instructions do not change MSR[FP]. The opcodes below write machine state
// or may enter a service/continuation that returns with a different MSR, so
// the check is repeated after them and the next FP instruction stays the
// exact exception-7 restart point.
fn opcode_invalidates_fpu_availability_proof(opcode: Opcode) -> bool {
    matches!(
        opcode,
        Opcode::B
            | Opcode::Bc
            | Opcode::Bcctr
            | Opcode::Bclr
            | Opcode::Sc
            | Opcode::Twi
            | Opcode::Rfi
            | Opcode::Mtmsr
            // A DEC write can immediately enter an asynchronous boundary.
            // Other SPR writes are rare enough to treat the same way.
            | Opcode::Mtspr
    )
}

fn function_requires_fpu(bytes: &[u8]) -> bool {
    bytes.chunks_exact(4).any(|chunk| {
        let word = u32::from_be_bytes(chunk.try_into().expect("four-byte instruction"));
        opcode_requires_fpu(Ins::new(word, Extensions::gekko_broadway()).op)
    })
}

// Some entry/direct-call substitutions touch FPR/PS/FPSCR directly or fold
// in a tail-called FP routine even when the wrapper has no FP opcode. They
// cannot reproduce the suppressed-instruction boundary, so those call
// chains are kept.
fn native_substitution_uses_fpu(address: u32) -> bool {
    native_pure_helper(address).is_some_and(|helper| helper.uses_fpu)
        || audited_inline_wrapper_uses_fpu(address)
}

fn discover_external_interior_targets(
    sections: &[DolSection],
    dol_bytes: &[u8],
    functions: &[FunctionRange],
) -> BTreeSet<u32> {
    let call_return_entries = call_return_continuations(sections, dol_bytes, functions);
    discover_external_interior_targets_with_call_returns(
        sections,
        dol_bytes,
        functions,
        &call_return_entries,
    )
}

fn discover_external_interior_targets_with_call_returns(
    sections: &[DolSection],
    dol_bytes: &[u8],
    functions: &[FunctionRange],
    call_return_entries: &BTreeSet<u32>,
) -> BTreeSet<u32> {
    let mut targets = BTreeSet::new();
    for source in functions {
        let Some(bytes) = function_bytes(sections, dol_bytes, *source) else {
            continue;
        };
        targets.extend(backward_branch_resume_entries(source.address, bytes));
        targets.extend(interior_fpu_retry_entries(source.address, bytes));
        targets.extend(architectural_interrupt_resume_entries(
            source.address,
            bytes,
        ));
        for target in direct_branch_targets(source.address, bytes) {
            let Some(destination) = mapped_function_containing(functions, target) else {
                continue;
            };
            if destination.address != source.address && target != destination.address {
                targets.insert(target);
            }
        }
    }
    targets.extend(rfi_unwind_continuations(sections, dol_bytes, functions));
    targets.extend(saved_context_continuations(sections, dol_bytes, functions));
    targets.extend(call_return_entries.iter().copied());
    for section in sections
        .iter()
        .filter(|section| section.kind == DolSectionKind::Data)
    {
        let file_start = section.file_offset as usize;
        let file_end = file_start + section.size as usize;
        if let Some(bytes) = dol_bytes.get(file_start..file_end) {
            targets.extend(interior_code_pointers(bytes, functions));
        }
    }
    targets
}

// Exception 7 suppresses the faulting FP instruction. If its RFI re-enables
// EE, a pending external/decrementer exception can be taken first: it
// records the suppressed PC in SRR0 and unwinds the AOT guard, so the later
// RFI needs a static entry at that instruction. Function starts are already
// entries and are not added again.
fn interior_fpu_retry_entries(address: u32, bytes: &[u8]) -> BTreeSet<u32> {
    bytes
        .chunks_exact(4)
        .enumerate()
        .skip(1)
        .filter_map(|(index, chunk)| {
            let pc = address.checked_add(index as u32 * 4)?;
            let word = u32::from_be_bytes(chunk.try_into().expect("four-byte instruction"));
            let instruction = Ins::new(word, Extensions::gekko_broadway());
            opcode_requires_fpu(instruction.op).then_some(pc)
        })
        .collect()
}

// mtmsr and mtspr DEC are interrupt-recognition points. Either can enter a
// translated exception and abandon the native C++ frame, so the next
// instruction must be a continuation alias for the eventual RFI. Other SPR
// writes do not create entries.
fn architectural_interrupt_resume_entries(address: u32, bytes: &[u8]) -> BTreeSet<u32> {
    let function_end = address.wrapping_add(bytes.len() as u32);
    bytes
        .chunks_exact(4)
        .enumerate()
        .filter_map(|(index, chunk)| {
            let pc = address.checked_add(index as u32 * 4)?;
            let word = u32::from_be_bytes(chunk.try_into().expect("four-byte instruction"));
            let instruction = Ins::new(word, Extensions::gekko_broadway());
            let is_recognition_boundary = match instruction.op {
                Opcode::Mtmsr => true,
                Opcode::Mtspr => spr_number(word) == 22,
                _ => false,
            };
            if !is_recognition_boundary {
                return None;
            }
            let resume_pc = pc.checked_add(4)?;
            (resume_pc < function_end).then_some(resume_pc)
        })
        .collect()
}

fn backward_branch_resume_entries(address: u32, bytes: &[u8]) -> BTreeSet<u32> {
    let function_end = address.wrapping_add(bytes.len() as u32);
    bytes
        .chunks_exact(4)
        .enumerate()
        .filter_map(|(index, chunk)| {
            let pc = address + index as u32 * 4;
            let word = u32::from_be_bytes(chunk.try_into().expect("four-byte instruction"));
            let instruction = Ins::new(word, Extensions::gekko_broadway());
            let target = match instruction.op {
                Opcode::B => branch_target(pc, word),
                Opcode::Bc => conditional_branch_target(pc, word),
                _ => return None,
            };
            (target > address && target < function_end && target <= pc).then_some(target)
        })
        .collect()
}

fn saved_context_continuations(
    sections: &[DolSection],
    dol_bytes: &[u8],
    functions: &[FunctionRange],
) -> BTreeSet<u32> {
    // OSSaveContext records LR as the future SRR0, so its linked callers are
    // legitimate dynamic entry points when OSLoadContext switches threads.
    const RMGE01_CONTEXT_SAVE_ROUTINES: [u32; 1] = [0x804A_379C];
    let save_routines = BTreeSet::from(RMGE01_CONTEXT_SAVE_ROUTINES);
    let mut continuations = BTreeSet::new();
    for source in functions {
        let Some(bytes) = function_bytes(sections, dol_bytes, *source) else {
            continue;
        };
        let source_end = source.address.wrapping_add(source.size);
        for (target, return_address) in direct_link_calls(source.address, bytes) {
            let Some(destination) = mapped_function_containing(functions, target) else {
                continue;
            };
            if save_routines.contains(&destination.address)
                && return_address > source.address
                && return_address < source_end
            {
                continuations.insert(return_address);
            }
        }
    }
    continuations
}

fn function_contains_rfi(bytes: &[u8]) -> bool {
    bytes.chunks_exact(4).any(|chunk| {
        let word = u32::from_be_bytes(chunk.try_into().expect("four-byte instruction"));
        Ins::new(word, Extensions::gekko_broadway()).op == Opcode::Rfi
    })
}

fn direct_link_calls(address: u32, bytes: &[u8]) -> Vec<(u32, u32)> {
    bytes
        .chunks_exact(4)
        .enumerate()
        .filter_map(|(index, chunk)| {
            let pc = address + index as u32 * 4;
            let word = u32::from_be_bytes(chunk.try_into().expect("four-byte instruction"));
            let instruction = Ins::new(word, Extensions::gekko_broadway());
            if !matches!(instruction.op, Opcode::B | Opcode::Bc) || word & 1 == 0 {
                return None;
            }
            let target = if instruction.op == Opcode::B {
                branch_target(pc, word)
            } else {
                conditional_branch_target(pc, word)
            };
            let return_address = pc.wrapping_add(4);
            Some((target, return_address))
        })
        .collect()
}

// A decrementer/external interrupt taken at a branch checkpoint can freeze a
// thread at any call depth. The interrupt resumes at the branch target (see
// backward_branch_resume_entries), but the cooperative-resume LR trampoline
// then unwinds through every outer frame's return address, so every direct
// and indirect call return site must be a callable continuation.
fn call_return_continuations(
    sections: &[DolSection],
    dol_bytes: &[u8],
    functions: &[FunctionRange],
) -> BTreeSet<u32> {
    let mut continuations = BTreeSet::new();
    for source in functions {
        let Some(bytes) = function_bytes(sections, dol_bytes, *source) else {
            continue;
        };
        let source_end = source.address.wrapping_add(source.size);
        let in_body = |site: &u32| *site > source.address && *site < source_end;
        continuations.extend(
            direct_link_calls(source.address, bytes)
                .into_iter()
                .map(|(_, return_address)| return_address)
                .filter(in_body),
        );
        continuations.extend(
            indirect_link_call_return_sites(source.address, bytes)
                .into_iter()
                .filter(in_body),
        );
    }
    continuations
}

fn indirect_link_call_return_sites(address: u32, bytes: &[u8]) -> Vec<u32> {
    bytes
        .chunks_exact(4)
        .enumerate()
        .filter_map(|(index, chunk)| {
            let pc = address + index as u32 * 4;
            let word = u32::from_be_bytes(chunk.try_into().expect("four-byte instruction"));
            let instruction = Ins::new(word, Extensions::gekko_broadway());
            (matches!(instruction.op, Opcode::Bcctr | Opcode::Bclr) && word & 1 == 1)
                .then_some(pc.wrapping_add(4))
        })
        .collect()
}

fn rfi_unwind_continuations(
    sections: &[DolSection],
    dol_bytes: &[u8],
    functions: &[FunctionRange],
) -> BTreeSet<u32> {
    let mut tainted = functions
        .iter()
        .filter_map(|function| {
            let bytes = function_bytes(sections, dol_bytes, *function)?;
            function_contains_rfi(bytes).then_some(function.address)
        })
        .collect::<BTreeSet<_>>();
    let mut continuations = BTreeSet::new();
    // Indirect linked calls (bctrl/blrl: virtual dispatch, callbacks) can
    // reach any address-taken function, including the OSLockMutex/
    // OSSleepThread ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ OSLoadContext chain, which the static taint cannot
    // follow. Functions containing one are tainted and their indirect return
    // sites become continuations.
    for source in functions {
        let Some(bytes) = function_bytes(sections, dol_bytes, *source) else {
            continue;
        };
        let source_end = source.address.wrapping_add(source.size);
        let indirect_returns = indirect_link_call_return_sites(source.address, bytes);
        if indirect_returns.is_empty() {
            continue;
        }
        tainted.insert(source.address);
        continuations.extend(
            indirect_returns
                .into_iter()
                .filter(|site| *site > source.address && *site < source_end),
        );
    }
    loop {
        let mut changed = false;
        for source in functions {
            let Some(bytes) = function_bytes(sections, dol_bytes, *source) else {
                continue;
            };
            let source_end = source.address.wrapping_add(source.size);
            for (target, return_address) in direct_link_calls(source.address, bytes) {
                let Some(destination) = mapped_function_containing(functions, target) else {
                    continue;
                };
                if !tainted.contains(&destination.address) {
                    continue;
                }
                if return_address > source.address && return_address < source_end {
                    continuations.insert(return_address);
                }
                changed |= tainted.insert(source.address);
            }
            // Taint also propagates through direct tail calls (`b`/`bc` into
            // another function): the callee returns through the caller's LR,
            // so a thread suspended below it unwinds to the caller's call
            // sites. Example: loadFileUsingRipper (fn_80398364:
            // `b FileRipper::loadToMainRAM`).
            for target in direct_branch_targets(source.address, bytes) {
                if target >= source.address && target < source_end {
                    continue; // internal branch, not a tail call
                }
                let Some(destination) = mapped_function_containing(functions, target) else {
                    continue;
                };
                if tainted.contains(&destination.address) {
                    changed |= tainted.insert(source.address);
                }
            }
        }
        if !changed {
            return continuations;
        }
    }
}

fn interior_code_pointers(bytes: &[u8], functions: &[FunctionRange]) -> BTreeSet<u32> {
    bytes
        .chunks_exact(4)
        .filter_map(|chunk| {
            let address = u32::from_be_bytes(chunk.try_into().expect("four-byte initialized word"));
            let function = mapped_function_containing(functions, address)?;
            (address % 4 == 0 && address != function.address).then_some(address)
        })
        .collect()
}

// Maps each interior entry point to its owning function. The owner's body
// dispatches on pc to all of its interior entries, so an alias only adds a
// table row.
#[cfg(test)]
fn discover_alias_entries(
    sections: &[DolSection],
    dol_bytes: &[u8],
    functions: &[FunctionRange],
) -> Vec<(u32, u32)> {
    let call_return_entries = call_return_continuations(sections, dol_bytes, functions);
    discover_alias_entries_with_call_returns(sections, dol_bytes, functions, &call_return_entries)
}

fn discover_alias_entries_with_call_returns(
    sections: &[DolSection],
    dol_bytes: &[u8],
    functions: &[FunctionRange],
    call_return_entries: &BTreeSet<u32>,
) -> Vec<(u32, u32)> {
    discover_external_interior_targets_with_call_returns(
        sections,
        dol_bytes,
        functions,
        call_return_entries,
    )
    .into_iter()
    .map(|target| {
        let function = mapped_function_containing(functions, target)
            .expect("discovered interior target must remain mapped");
        (target, function.address)
    })
    .collect()
}

fn lower_words(address: u32, bytes: &[u8]) -> Result<String, TranslationError> {
    lower_words_with_entries(address, bytes, &BTreeSet::new())
}

/// Structural emitter for straight-line integer leaves, using WiiCompiled's
/// typed read-before-write/possible-write analysis. Unsupported instructions
/// or any interior entry fall back to the full-context lowerer. Locals load
/// from the read-before-write set, and every possibly written GPR is
/// committed before `blr`.
fn analyze_resident_integer_leaf(
    address: u32,
    bytes: &[u8],
    entries: &BTreeSet<u32>,
    call_return_entries: &BTreeSet<u32>,
) -> Option<(Vec<(u32, Opcode)>, GuestAbiContract)> {
    if !entries.is_empty()
        || !call_return_entries.is_empty()
        || bytes.is_empty()
        || bytes.len() % 4 != 0
        || bytes.len() > 512
        || should_emit_audio_trace_hook(address)
        || should_emit_main_frame_trace_hook(address)
        || should_emit_jutvideo_mq_trace_hook(address)
        || should_emit_file_select_trace_hook(address)
        || should_emit_movie_trace_hook(address)
    {
        return None;
    }
    let mut effects = Vec::with_capacity(bytes.len() / 4);
    let mut words = Vec::with_capacity(bytes.len() / 4);
    for (index, chunk) in bytes.chunks_exact(4).enumerate() {
        let pc = address.checked_add((index as u32).checked_mul(4)?)?;
        if function_async_marker(address, pc).is_some()
            || main_frame_stage_marker(address, pc).is_some()
        {
            return None;
        }
        let word = u32::from_be_bytes(chunk.try_into().ok()?);
        let opcode = Ins::new(word, Extensions::gekko_broadway()).op;
        let mut effect = GuestInstructionEffect::default();
        let reg = |index: u32| GuestRegisterSet {
            gpr: 1u32 << index,
            ..GuestRegisterSet::default()
        };
        match opcode {
            Opcode::Addi | Opcode::Addis => {
                let access = guest_integer_gpr_access(opcode, word)?;
                effect.reads = access.reads();
                effect.possible_writes = access.writes();
                effect.definite_writes = effect.possible_writes;
            }
            Opcode::Ori | Opcode::Oris | Opcode::Xori | Opcode::Xoris => {
                effect.reads = reg(gpr_rt(word));
                effect.possible_writes = reg(gpr_ra(word));
                effect.definite_writes = effect.possible_writes;
            }
            Opcode::Bclr if word == 0x4E80_0020 && index + 1 == bytes.len() / 4 => {
                effect.reads.lr = true;
            }
            _ => return None,
        }
        words.push((word, opcode));
        effects.push(effect);
    }
    let contract = analyze_guest_abi(
        &[GuestEffectBlock {
            successors: Vec::new(),
            instructions: effects,
        }],
        0,
    )
    .ok()?;
    let permitted_writes = GuestRegisterSet {
        gpr: contract.possible_writes.gpr,
        ..GuestRegisterSet::default()
    };
    let permitted_reads = GuestRegisterSet {
        gpr: contract.read_before_write.gpr,
        lr: true,
        ..GuestRegisterSet::default()
    };
    if contract.boundary != GuestBoundaryFlags::NONE
        || contract.possible_writes != permitted_writes
        || contract.read_before_write != permitted_reads
    {
        return None;
    }
    Some((words, contract))
}

fn lower_resident_integer_leaf(
    address: u32,
    bytes: &[u8],
    entries: &BTreeSet<u32>,
    call_return_entries: &BTreeSet<u32>,
) -> Option<String> {
    let (words, contract) =
        analyze_resident_integer_leaf(address, bytes, entries, call_return_entries)?;
    // analyze_resident_integer_leaf already rejects any word it cannot classify and
    // pushes exactly one entry per instruction, in order, so the opcode match below
    // must accept every word it just accepted. Assert that instead of relying on
    // it: falling out of the match at `_ => return None` abandons a half-written
    // body, and the caller reads None as "not eligible" and re-lowers the function
    // with the ordinary emitter. The shipped code would still be correct, but this
    // tier would silently lose coverage -- and coverage is the point of the
    // optimisation. An edit that adds an opcode to one match and not the other has
    // to fail loudly in tests rather than quietly shrink the fast path.
    debug_assert_eq!(
        words.len(),
        bytes.len() / 4,
        "resident leaf analyzer/emitter opcode sets diverged at 0x{address:08X}"
    );
    let live = contract.read_before_write.gpr | contract.possible_writes.gpr;
    let mut output = String::new();
    for index in 0..32 {
        if live & (1u32 << index) == 0 {
            continue;
        }
        if contract.read_before_write.gpr & (1u32 << index) != 0 {
            writeln!(
                output,
                "    std::uint32_t resident_r{index} = context->gpr[{index}];"
            )
            .ok()?;
        } else {
            writeln!(output, "    std::uint32_t resident_r{index} = 0u;").ok()?;
        }
    }
    for (word, opcode) in words {
        match opcode {
            Opcode::Addi | Opcode::Addis => {
                let access = guest_integer_gpr_access(opcode, word)?;
                let target = access.destination;
                let immediate = if opcode == Opcode::Addis {
                    (signed_immediate(word) as u32) << 16
                } else {
                    signed_immediate(word) as u32
                };
                if access.first_source.is_none() {
                    writeln!(output, "    resident_r{target} = 0x{immediate:08X}u;").ok()?;
                } else {
                    writeln!(
                        output,
                        "    resident_r{target} = resident_r{} + 0x{immediate:08X}u;",
                        access.first_source?
                    )
                    .ok()?;
                }
            }
            Opcode::Ori | Opcode::Oris | Opcode::Xori | Opcode::Xoris => {
                let source = gpr_rt(word);
                let target = gpr_ra(word);
                let immediate = if matches!(opcode, Opcode::Oris | Opcode::Xoris) {
                    (word & 0xFFFF) << 16
                } else {
                    word & 0xFFFF
                };
                let operator = if matches!(opcode, Opcode::Ori | Opcode::Oris) {
                    "|"
                } else {
                    "^"
                };
                writeln!(
                    output,
                    "    resident_r{target} = resident_r{source} {operator} 0x{immediate:08X}u;"
                )
                .ok()?;
            }
            Opcode::Bclr => {
                for index in 0..32 {
                    if contract.possible_writes.gpr & (1u32 << index) != 0 {
                        writeln!(output, "    context->gpr[{index}] = resident_r{index};").ok()?;
                    }
                }
                output.push_str("    return;\n");
            }
            _ => return None,
        }
    }
    Some(output)
}

/// WiiCompiled's direct-call residency policy, applied where the whole
/// integer callee contract is known from the DOL. Arithmetic and LR moves use
/// locals; checked stack memory uses the guest helpers behind a full fence;
/// calls, checkpoints and return continuations keep their native boundaries.
/// Unknown, substituted or observed callees get a full fence. Other memory,
/// FP, indirect calls and unclassified opcodes use the ordinary emitter.
fn lower_resident_integer_direct_calls(
    address: u32,
    bytes: &[u8],
    entries: &BTreeSet<u32>,
    call_return_entries: &BTreeSet<u32>,
    callable_entries: &BTreeMap<u32, u32>,
    exact_fpu_functions: &BTreeSet<u32>,
    leaf_contracts: &BTreeMap<u32, GuestAbiContract>,
) -> Result<Option<String>, TranslationError> {
    if bytes.is_empty()
        || bytes.len() % 4 != 0
        || bytes.len() > 2048
        || native_function_body(address).is_some()
        || should_emit_audio_trace_hook(address)
        || should_emit_main_frame_trace_hook(address)
        || should_emit_jutvideo_mq_trace_hook(address)
        || should_emit_file_select_trace_hook(address)
        || should_emit_movie_trace_hook(address)
        || !call_return_entries.is_subset(entries)
    {
        return Ok(None);
    }
    let Some(end) = address.checked_add(bytes.len() as u32) else {
        return Ok(None);
    };
    if entries
        .iter()
        .any(|entry| *entry <= address || *entry >= end || *entry % 4 != 0)
    {
        return Ok(None);
    }

    let mut instructions = Vec::with_capacity(bytes.len() / 4);
    let mut local_gpr_mask = 0u32;
    let mut expected_return_entries = BTreeSet::new();
    for (index, chunk) in bytes.chunks_exact(4).enumerate() {
        let pc = address + index as u32 * 4;
        if function_async_marker(address, pc).is_some()
            || main_frame_stage_marker(address, pc).is_some()
        {
            return Ok(None);
        }
        let word = u32::from_be_bytes(chunk.try_into().expect("four-byte instruction"));
        let opcode = Ins::new(word, Extensions::gekko_broadway()).op;
        let target = match opcode {
            Opcode::Addi | Opcode::Addis => {
                let Some(access) = guest_integer_gpr_access(opcode, word) else {
                    return Ok(None);
                };
                local_gpr_mask |= access.reads().gpr | access.writes().gpr;
                None
            }
            Opcode::Ori | Opcode::Oris | Opcode::Xori | Opcode::Xoris => {
                local_gpr_mask |= (1u32 << gpr_rt(word)) | (1u32 << gpr_ra(word));
                None
            }
            Opcode::Mfspr | Opcode::Mtspr if spr_number(word) == 8 => {
                local_gpr_mask |= 1u32 << gpr_rt(word);
                None
            }
            Opcode::Lwz | Opcode::Stw | Opcode::Stwu if gpr_ra(word) == 1 => {
                local_gpr_mask |= (1u32 << 1) | (1u32 << gpr_rt(word));
                None
            }
            Opcode::B if word & 1 != 0 && index + 1 < bytes.len() / 4 => {
                let target = branch_target(pc, word);
                if (address..end).contains(&target) {
                    return Ok(None);
                }
                expected_return_entries.insert(pc + 4);
                Some(target)
            }
            Opcode::Bclr if word == 0x4E80_0020 && index + 1 == bytes.len() / 4 => None,
            _ => return Ok(None),
        };
        instructions.push((pc, word, opcode, target));
    }
    if expected_return_entries.is_empty() || &expected_return_entries != call_return_entries {
        return Ok(None);
    }

    let mut output = String::new();
    output.push_str("    // galaxy-resident-direct-call-contract-v1\n");
    for register in 0..32 {
        if local_gpr_mask & (1u32 << register) != 0 {
            writeln!(
                output,
                "    std::uint32_t resident_r{register} = context->gpr[{register}];"
            )
            .map_err(|_| TranslationError::Formatting)?;
        }
    }
    if !entries.is_empty() {
        writeln!(
            output,
            "    if (context->pc != 0x{address:08X}u) [[unlikely]] {{\n    switch (context->pc) {{"
        )
        .map_err(|_| TranslationError::Formatting)?;
        for entry in entries {
            let destination = if call_return_entries.contains(entry) {
                "call_return"
            } else {
                "label"
            };
            writeln!(
                output,
                "        case 0x{entry:08X}u: goto {destination}_{entry:08X};"
            )
            .map_err(|_| TranslationError::Formatting)?;
        }
        output.push_str("        default: galaxy::guest_execution_fault(services, context->pc, \"invalid interior function entry\");\n    }\n    }\n");
    }

    for (pc, word, opcode, target) in instructions {
        if entries.contains(&pc) {
            writeln!(output, "label_{pc:08X}:").map_err(|_| TranslationError::Formatting)?;
        }
        // Each PPC instruction is scoped so an interior-entry goto never
        // crosses a local's initialization, as in the ordinary lowerer.
        output.push_str("    {\n");
        match opcode {
            Opcode::Addi | Opcode::Addis => {
                let access = guest_integer_gpr_access(opcode, word)
                    .expect("classified integer operand remains available");
                let destination = access.destination;
                let immediate = if opcode == Opcode::Addis {
                    (signed_immediate(word) as u32) << 16
                } else {
                    signed_immediate(word) as u32
                };
                if let Some(source) = access.first_source {
                    writeln!(
                        output,
                        "    resident_r{destination} = resident_r{source} + 0x{immediate:08X}u;"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                } else {
                    writeln!(output, "    resident_r{destination} = 0x{immediate:08X}u;")
                        .map_err(|_| TranslationError::Formatting)?;
                }
            }
            Opcode::Ori | Opcode::Oris | Opcode::Xori | Opcode::Xoris => {
                let source = gpr_rt(word);
                let destination = gpr_ra(word);
                let immediate = if matches!(opcode, Opcode::Oris | Opcode::Xoris) {
                    (word & 0xFFFF) << 16
                } else {
                    word & 0xFFFF
                };
                let operator = if matches!(opcode, Opcode::Ori | Opcode::Oris) {
                    "|"
                } else {
                    "^"
                };
                writeln!(output, "    resident_r{destination} = resident_r{source} {operator} 0x{immediate:08X}u;")
                    .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Mfspr => {
                writeln!(output, "    resident_r{} = context->lr;", gpr_rt(word))
                    .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Mtspr => {
                writeln!(output, "    context->lr = resident_r{};", gpr_rt(word))
                    .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Lwz | Opcode::Stw | Opcode::Stwu => {
                // The checked helper may fault or call native code: flush all
                // resident registers, keep the store-before-RA-update order,
                // and reload locals after it returns. The EA temporary is
                // scoped so interior-entry gotos do not cross its
                // initialization.
                output.push_str("    {\n");
                emit_resident_gpr_sync(&mut output, local_gpr_mask, false)?;
                let update = opcode == Opcode::Stwu;
                let ea = d_form_effective_address(&mut output, pc, word, 1, update, None)?;
                let register = gpr_rt(word);
                match opcode {
                    Opcode::Lwz => writeln!(
                        output,
                        "    context->gpr[{register}] = galaxy::guest_load_u32(memory, {ea}, services, 0x{pc:08X}u);"
                    ),
                    Opcode::Stw | Opcode::Stwu => writeln!(
                        output,
                        "    galaxy::guest_store_u32(memory, {ea}, context->gpr[{register}], services, 0x{pc:08X}u);"
                    ),
                    _ => unreachable!(),
                }
                .map_err(|_| TranslationError::Formatting)?;
                if update {
                    writeln!(output, "    context->gpr[1] = {ea};")
                        .map_err(|_| TranslationError::Formatting)?;
                }
                emit_resident_gpr_sync(&mut output, local_gpr_mask, true)?;
                output.push_str("    }\n");
            }
            Opcode::B => {
                let target = target.expect("classified direct linked call has target");
                writeln!(output, "    context->lr = 0x{:08X}u;", pc + 4)
                    .map_err(|_| TranslationError::Formatting)?;
                let verified_owner = callable_entries.get(&target).copied().filter(|owner| {
                    *owner == target
                        && leaf_contracts.contains_key(&target)
                        && !exact_fpu_functions.contains(&target)
                        && !requires_host_guest_call_at(target, pc)
                        && native_pure_helper(target).is_none()
                        && !should_observe_route_marker_generated_direct_call(target)
                        && !should_emit_resource_helper_return_trace(pc)
                        && !(target == 0x8000_4338
                            && matches!(
                                pc,
                                0x804D_945C | 0x804D_947C | 0x804D_949C | 0x804D_94B0 | 0x804D_94C4
                            ))
                        && !(target == 0x8045_14EC && pc == 0x8038_D30C)
                        && pc != 0x803A_BDF0
                        && !(target == 0x8039_8D3C && pc == 0x8039_8E7C)
                });
                let sync = verified_owner.map_or_else(GuestResidencyBoundarySync::full, |_| {
                    GuestResidencyBoundarySync::from_optional_callee_contract(
                        leaf_contracts.get(&target),
                    )
                });
                emit_resident_gpr_sync(&mut output, sync.flush.gpr & local_gpr_mask, false)?;
                if let Some(owner) = verified_owner {
                    emit_resolved_generated_direct_call(&mut output, target, owner, pc)?;
                } else {
                    emit_direct_guest_call_with_exact_fpu(
                        &mut output,
                        target,
                        pc,
                        Some(callable_entries),
                        Some(exact_fpu_functions),
                    )?;
                }
                emit_resident_gpr_sync(&mut output, sync.reload.gpr & local_gpr_mask, true)?;
                // The checkpoint can inspect or change the whole context, so
                // linked calls do a full flush/reload even for a pure callee.
                emit_resident_gpr_sync(&mut output, local_gpr_mask, false)?;
                emit_call_return_checkpoint(&mut output, pc)?;
                emit_resident_gpr_sync(&mut output, local_gpr_mask, true)?;
            }
            Opcode::Bclr => {
                emit_resident_gpr_sync(&mut output, local_gpr_mask, false)?;
                output.push_str("    return;\n");
            }
            _ => unreachable!("unclassified opcode was rejected above"),
        }
        output.push_str("    }\n");
    }
    for entry in call_return_entries {
        writeln!(output, "call_return_{entry:08X}:").map_err(|_| TranslationError::Formatting)?;
        emit_call_return_checkpoint(&mut output, entry - 4)?;
        emit_resident_gpr_sync(&mut output, local_gpr_mask, true)?;
        writeln!(output, "    goto label_{entry:08X};")
            .map_err(|_| TranslationError::Formatting)?;
    }
    Ok(Some(output))
}

fn emit_resident_gpr_sync(
    output: &mut String,
    mask: u32,
    reload: bool,
) -> Result<(), TranslationError> {
    for register in 0..32 {
        if mask & (1u32 << register) == 0 {
            continue;
        }
        if reload {
            writeln!(
                output,
                "    resident_r{register} = context->gpr[{register}];"
            )
        } else {
            writeln!(
                output,
                "    context->gpr[{register}] = resident_r{register};"
            )
        }
        .map_err(|_| TranslationError::Formatting)?;
    }
    Ok(())
}

#[cfg(test)]
fn lower_words_for_module(
    address: u32,
    bytes: &[u8],
    entries: &BTreeSet<u32>,
    callable_entries: &BTreeMap<u32, u32>,
) -> Result<String, TranslationError> {
    lower_words_for_module_with_call_return_entries(
        address,
        bytes,
        entries,
        &BTreeSet::new(),
        callable_entries,
    )
}

#[cfg(test)]
fn lower_words_for_module_with_call_return_entries(
    address: u32,
    bytes: &[u8],
    entries: &BTreeSet<u32>,
    call_return_entries: &BTreeSet<u32>,
    callable_entries: &BTreeMap<u32, u32>,
) -> Result<String, TranslationError> {
    lower_words_for_module_with_call_return_entries_and_exact_fpu(
        address,
        bytes,
        entries,
        call_return_entries,
        callable_entries,
        &BTreeSet::new(),
    )
}

#[cfg(test)]
fn lower_words_for_module_with_call_return_entries_and_exact_fpu(
    address: u32,
    bytes: &[u8],
    entries: &BTreeSet<u32>,
    call_return_entries: &BTreeSet<u32>,
    callable_entries: &BTreeMap<u32, u32>,
    exact_fpu_functions: &BTreeSet<u32>,
) -> Result<String, TranslationError> {
    lower_words_for_module_with_call_return_entries_and_exact_fpu_flat_reads(
        address,
        bytes,
        entries,
        call_return_entries,
        callable_entries,
        exact_fpu_functions,
        &ModuleTranslationOptions::default(),
    )
}

fn lower_words_for_module_with_call_return_entries_and_exact_fpu_flat_reads(
    address: u32,
    bytes: &[u8],
    entries: &BTreeSet<u32>,
    call_return_entries: &BTreeSet<u32>,
    callable_entries: &BTreeMap<u32, u32>,
    exact_fpu_functions: &BTreeSet<u32>,
    options: &ModuleTranslationOptions,
) -> Result<String, TranslationError> {
    lower_words_with_config(
        address,
        bytes,
        entries,
        call_return_entries,
        LoweringConfig {
            callable_entries: Some(callable_entries),
            exact_fpu_functions: Some(exact_fpu_functions),
            flat_ram_reads: options.flat_ram_reads,
            fused_paired_binary: options.fused_paired_binary,
            inline_scalar_single_binary: options.inline_scalar_single_binary,
            ..LoweringConfig::default()
        },
    )
}

// Interior entry points (resume continuations, checkpoint resume PCs,
// address-taken labels) share the owning function's body. Every native
// invocation sets context->pc to the entry address first, so a pc-dispatch
// prologue jumps to the label. The function start skips the switch; an
// unknown entry address hard-fails.
fn lower_words_with_entries(
    address: u32,
    bytes: &[u8],
    entries: &BTreeSet<u32>,
) -> Result<String, TranslationError> {
    lower_words_with_options(
        address,
        bytes,
        entries,
        &BTreeSet::new(),
        None,
        None,
        None,
        false,
    )
}

// Compatibility wrapper that keeps each lowering control explicit.
#[allow(clippy::too_many_arguments)]
fn lower_words_with_options(
    address: u32,
    bytes: &[u8],
    entries: &BTreeSet<u32>,
    external_call_return_entries: &BTreeSet<u32>,
    callable_entries: Option<&BTreeMap<u32, u32>>,
    exact_fpu_functions: Option<&BTreeSet<u32>>,
    immediate_overrides: Option<&BTreeMap<u32, PpcImmediateOverride>>,
    flat_ram_reads: bool,
) -> Result<String, TranslationError> {
    lower_words_with_config(
        address,
        bytes,
        entries,
        external_call_return_entries,
        LoweringConfig {
            callable_entries,
            exact_fpu_functions,
            immediate_overrides,
            local_lane_profile: LocalLaneProfile::None,
            flat_ram_reads,
            fused_paired_binary: false,
            inline_scalar_single_binary: false,
            typed_region_gpr_mask: 0,
        },
    )
}

#[derive(Clone, Copy, Default, Eq, PartialEq)]
enum LocalLaneProfile {
    #[default]
    None,
    PsmtxConcat,
    PsvecCross,
    PsvecNormalize,
}

#[derive(Default)]
struct LoweringConfig<'a> {
    callable_entries: Option<&'a BTreeMap<u32, u32>>,
    exact_fpu_functions: Option<&'a BTreeSet<u32>>,
    immediate_overrides: Option<&'a BTreeMap<u32, PpcImmediateOverride>>,
    local_lane_profile: LocalLaneProfile,
    flat_ram_reads: bool,
    fused_paired_binary: bool,
    inline_scalar_single_binary: bool,
    typed_region_gpr_mask: u32,
}

// The typed-region pilot has a single DOL owner; its public body still
// handles interior entries and faults. Only non-recording integer
// instructions may use GPR locals; all others read and publish the full
// context.
fn typed_region_pure_gpr_access(opcode: Opcode, word: u32) -> Option<GuestGprAccess> {
    if !matches!(
        opcode,
        Opcode::Addi | Opcode::Addis | Opcode::Or | Opcode::Rlwinm
    ) || (matches!(opcode, Opcode::Or | Opcode::Rlwinm) && record_bit(word))
    {
        return None;
    }
    guest_integer_gpr_access(opcode, word)
}

fn typed_region_80165478_mask(
    bytes: &[u8],
    entries: &BTreeSet<u32>,
    call_return_entries: &BTreeSet<u32>,
) -> Result<u32, TranslationError> {
    const ADDRESS: u32 = 0x8016_5478;
    const SHA256: &str = "aab976d3188cdaf77ebad83f496014583bc85a697314db133abd2c6d28a86764";
    const PUBLIC_ENTRIES: [u32; 14] = [
        0x8016_54AC,
        0x8016_54BC,
        0x8016_54C4,
        0x8016_54D4,
        0x8016_54E4,
        0x8016_5500,
        0x8016_5520,
        0x8016_5534,
        0x8016_5544,
        0x8016_5554,
        0x8016_5568,
        0x8016_5570,
        0x8016_5578,
        0x8016_5580,
    ];
    let expected = PUBLIC_ENTRIES.into_iter().collect::<BTreeSet<_>>();
    if bytes.len() != 0x11C
        || format!("{:x}", Sha256::digest(bytes)) != SHA256
        || entries != &expected
        || call_return_entries != &expected
    {
        return Err(TranslationError::TypedRegionProof {
            address: ADDRESS,
            reason: "DOL bytes, range, or fourteen public continuations changed".to_owned(),
        });
    }
    let mut mask = 0u32;
    for chunk in bytes.chunks_exact(4) {
        let word = u32::from_be_bytes(chunk.try_into().expect("four-byte PPC word"));
        let opcode = Ins::new(word, Extensions::gekko_broadway()).op;
        if let Some(access) = typed_region_pure_gpr_access(opcode, word) {
            mask |= access.reads().gpr | access.writes().gpr;
        }
    }
    if mask == 0 {
        return Err(TranslationError::TypedRegionProof {
            address: ADDRESS,
            reason: "no typed integer operands survived the decoder".to_owned(),
        });
    }
    Ok(mask)
}

fn cache_typed_region_instruction(
    output: &mut String,
    start: usize,
    pc: u32,
    word: u32,
    access: GuestGprAccess,
) -> Result<(), TranslationError> {
    // Decoded only to identify the `or rD,rS,rS` encoding: that arm renders the
    // shared source once, so the operand-count proof below must expect one fewer
    // occurrence of it. No other opcode admitted by
    // `typed_region_pure_gpr_access` can name one register in both source fields.
    let decoded = Ins::new(word, Extensions::gekko_broadway());
    let mut rendered = output.split_off(start);
    for register in 0..32 {
        let operand = format!("context->gpr[{register}]");
        let collapsed_self_or = decoded.op == Opcode::Or
            && access.first_source.is_some()
            && access.first_source == access.second_source
            && access.first_source == Some(register);
        let expected = usize::from(access.destination == register)
            + usize::from(access.first_source == Some(register))
            + usize::from(access.second_source == Some(register))
            - usize::from(collapsed_self_or);
        if rendered.matches(&operand).count() != expected {
            return Err(TranslationError::TypedRegionProof {
                address: pc,
                reason: format!("rendered r{register} operand count changed"),
            });
        }
        rendered = rendered.replace(&operand, &format!("resident_r{register}"));
    }
    if rendered.contains("context->") {
        return Err(TranslationError::TypedRegionProof {
            address: pc,
            reason: "pure integer instruction gained a hidden context effect".to_owned(),
        });
    }
    output.push_str(&rendered);
    Ok(())
}

#[derive(Clone, Copy)]
struct WgpipeStoreInstruction {
    pc: u32,
    word: u32,
    width: u32,
}

struct WgpipeStoreRun {
    address: u32,
    stores: Vec<WgpipeStoreInstruction>,
}

fn wgpipe_store_width(opcode: Opcode) -> Option<u32> {
    match opcode {
        Opcode::Stb | Opcode::Stbx => Some(1),
        Opcode::Sth | Opcode::Sthx => Some(2),
        Opcode::Stw | Opcode::Stwx => Some(4),
        _ => None,
    }
}

fn function_async_marker(address: u32, pc: u32) -> Option<&'static str> {
    match (address, pc) {
        (0x8039_8E00, 0x8039_8E6C) => Some("kFunctionAsyncTraceWorkerDequeued"),
        (0x8039_8E00, 0x8039_8E80) => Some("kFunctionAsyncTraceWorkerComplete"),
        (0x8039_8E00, 0x8039_8E90) => Some("kFunctionAsyncTraceDoneMessagePosted"),
        (0x8039_8E00, 0x8039_8E94) => Some("kFunctionAsyncTraceEndFlagPublished"),
        (0x8039_9144, 0x8039_9268) => Some("kFunctionAsyncTraceReaped"),
        (0x8039_9390, 0x8039_9424) => Some("kFunctionAsyncTraceExecInfoPublished"),
        _ => None,
    }
}

fn discover_wgpipe_store_runs(
    address: u32,
    bytes: &[u8],
    interior_targets: &BTreeSet<u32>,
    immediate_overrides: Option<&BTreeMap<u32, PpcImmediateOverride>>,
) -> BTreeMap<u32, WgpipeStoreRun> {
    const MAX_STORES_PER_BATCH: usize = 16;
    let decoded = bytes
        .chunks_exact(4)
        .enumerate()
        .map(|(index, chunk)| {
            let pc = address + index as u32 * 4;
            let word = u32::from_be_bytes(chunk.try_into().expect("four-byte instruction"));
            let opcode = Ins::new(word, Extensions::gekko_broadway()).op;
            (pc, word, opcode)
        })
        .collect::<Vec<_>>();
    // Local constant tracking: values are dropped at every join and after
    // every branch/call, and unknown GPR definitions invalidate their
    // destination via the decoder's `defs`. A dynamic object/stack store can
    // therefore never become a runtime WGPIPE probe.
    let mut constants: [Option<u32>; 32] = [None; 32];
    let mut known_store_addresses = BTreeMap::new();
    for (pc, word, opcode) in &decoded {
        if interior_targets.contains(pc) {
            constants.fill(None);
        }
        if wgpipe_store_width(*opcode).is_some()
            && !immediate_overrides.is_some_and(|overrides| overrides.contains_key(pc))
        {
            let address = match opcode {
                Opcode::Stb | Opcode::Sth | Opcode::Stw => {
                    let base = gpr_ra(*word);
                    let base = if base == 0 {
                        Some(0u32)
                    } else {
                        constants[base as usize]
                    };
                    base.map(|value| value.wrapping_add(signed_immediate(*word) as u32))
                }
                Opcode::Stbx | Opcode::Sthx | Opcode::Stwx => {
                    let base = gpr_ra(*word);
                    let base = if base == 0 {
                        Some(0u32)
                    } else {
                        constants[base as usize]
                    };
                    base.zip(constants[gpr_rb(*word) as usize])
                        .map(|(left, right)| left.wrapping_add(right))
                }
                _ => None,
            };
            if address.is_some_and(is_wgpipe_address) {
                known_store_addresses.insert(*pc, address.expect("known WGPIPE address"));
            }
        }

        let prior = constants;
        for definition in Ins::new(*word, Extensions::gekko_broadway()).defs() {
            if let Argument::GPR(register) = definition {
                constants[register.0 as usize] = None;
            }
        }
        if !immediate_overrides.is_some_and(|overrides| overrides.contains_key(pc)) {
            match opcode {
                Opcode::Addi | Opcode::Addis => {
                    let target = gpr_rt(*word) as usize;
                    let base = gpr_ra(*word);
                    let base = if base == 0 {
                        Some(0u32)
                    } else {
                        prior[base as usize]
                    };
                    let scale = if *opcode == Opcode::Addis { 65_536 } else { 1 };
                    constants[target] = base.map(|value| {
                        value.wrapping_add((signed_immediate(*word).wrapping_mul(scale)) as u32)
                    });
                }
                Opcode::Ori | Opcode::Oris | Opcode::Xori | Opcode::Xoris => {
                    let source = prior[gpr_rt(*word) as usize];
                    let immediate = u32::from((*word & 0xFFFF) as u16)
                        << if matches!(opcode, Opcode::Oris | Opcode::Xoris) {
                            16
                        } else {
                            0
                        };
                    constants[gpr_ra(*word) as usize] = source.map(|value| {
                        if matches!(opcode, Opcode::Ori | Opcode::Oris) {
                            value | immediate
                        } else {
                            value ^ immediate
                        }
                    });
                }
                Opcode::Or if gpr_rt(*word) == gpr_rb(*word) => {
                    constants[gpr_ra(*word) as usize] = prior[gpr_rt(*word) as usize];
                }
                _ => {}
            }
        }
        if Ins::new(*word, Extensions::gekko_broadway()).is_branch() {
            constants.fill(None);
        }
    }

    let mut runs = BTreeMap::new();
    let mut index = 0usize;
    while index < decoded.len() {
        let start = index;
        let mut run: Vec<WgpipeStoreInstruction> = Vec::new();
        while index < decoded.len() && run.len() < MAX_STORES_PER_BATCH {
            let (pc, word, opcode) = decoded[index];
            let Some(width) = wgpipe_store_width(opcode) else {
                break;
            };
            let Some(&known_address) = known_store_addresses.get(&pc) else {
                break;
            };
            if (!run.is_empty() && interior_targets.contains(&pc))
                || immediate_overrides.is_some_and(|overrides| overrides.contains_key(&pc))
                || function_async_marker(address, pc).is_some()
                || main_frame_stage_marker(address, pc).is_some()
            {
                break;
            }
            let store = WgpipeStoreInstruction { pc, word, width };
            if run
                .first()
                .is_some_and(|first| known_store_addresses.get(&first.pc) != Some(&known_address))
            {
                break;
            }
            run.push(store);
            index += 1;
        }
        if run.len() >= 2 {
            runs.insert(
                run[0].pc,
                WgpipeStoreRun {
                    address: known_store_addresses[&run[0].pc],
                    stores: run,
                },
            );
        } else {
            index = start + 1;
        }
    }
    runs
}

fn is_wgpipe_address(address: u32) -> bool {
    let high = address & 0xFF00_0000;
    (high == 0x0C00_0000 || high == 0xCC00_0000) && address & 0x00FF_F000 == 0x0000_8000
}

fn emit_wgpipe_store_run(
    output: &mut String,
    run: &WgpipeStoreRun,
) -> Result<(), TranslationError> {
    let stores = &run.stores;
    let byte_count = stores
        .iter()
        .map(|store| store.width as usize)
        .sum::<usize>();
    writeln!(
        output,
        "    const std::array<std::byte, {byte_count}> wgpipe_bytes_{:08X}{{{{",
        stores[0].pc,
    )
    .map_err(|_| TranslationError::Formatting)?;
    for store in stores {
        let source = gpr_rt(store.word);
        for byte in (0..store.width).rev() {
            writeln!(
                output,
                "        static_cast<std::byte>(static_cast<std::uint8_t>(context->gpr[{source}] >> {}u)),",
                byte * 8,
            )
            .map_err(|_| TranslationError::Formatting)?;
        }
    }
    writeln!(
        output,
        "    }}}};\n    if (galaxy::guest_wgpipe_batch_requires_scalar_path()) {{",
    )
    .map_err(|_| TranslationError::Formatting)?;
    for store in stores {
        let source = gpr_rt(store.word);
        let helper = match store.width {
            1 => "guest_store_u8",
            2 => "guest_store_u16",
            4 => "guest_store_u32",
            _ => unreachable!(),
        };
        let source_value = match store.width {
            1 => format!("static_cast<std::uint8_t>(context->gpr[{source}])"),
            2 => format!("static_cast<std::uint16_t>(context->gpr[{source}])"),
            4 => format!("context->gpr[{source}]"),
            _ => unreachable!(),
        };
        writeln!(
            output,
            "        galaxy::{helper}(memory, 0x{:08X}u, {source_value}, services, 0x{:08X}u);",
            run.address, store.pc,
        )
        .map_err(|_| TranslationError::Formatting)?;
    }
    writeln!(
        output,
        "    }} else {{\n        galaxy::guest_write_wgpipe_bytes(memory, 0x{:08X}u, wgpipe_bytes_{:08X}, services, 0x{:08X}u);\n    }}",
        run.address,
        stores[0].pc,
        stores[0].pc,
    )
    .map_err(|_| TranslationError::Formatting)?;
    Ok(())
}

// Helper-family selection adapted from WiiCompiled (GPLv3)
// CxxLinearCodeGenerator.FlatGuestMemory.cs::FlatReadHelper at
// 83463764b8acda394e058b0c689a10b8561fc380. Unlike upstream's
// fault-handler-backed FlatRead, Galaxy's ABI helper checks the whole RAM
// span and keeps the MMIO/locked-cache/fault-PC path.
// https://github.com/patchzyy/Wiicompiled/blob/83463764b8acda394e058b0c689a10b8561fc380/translator/src/Translator.Core/CodeGen/CxxLinearCodeGenerator.FlatGuestMemory.cs
fn integer_guest_load_expression(width: u32, ea: &str, pc: u32, flat_ram_reads: bool) -> String {
    let suffix = match width {
        1 => "u8",
        2 => "u16",
        4 => "u32",
        _ => unreachable!("integer scalar load width is fixed by its PPC opcode"),
    };
    if flat_ram_reads {
        format!(
            "galaxy::guest_load_flat_or_checked_{suffix}(flat_guest_read_base, memory, {ea}, services, 0x{pc:08X}u)"
        )
    } else {
        format!("galaxy::guest_load_{suffix}(memory, {ea}, services, 0x{pc:08X}u)")
    }
}

// Straight-line integer GPR residency.
//
// The translated body names `context->gpr[N]` for every guest register
// operand, so the host compiler treats each operand as an independent 4-byte
// memory access into `PpcContext`. An opaque call in between -- the
// call-return checkpoint after every guest call, every guest load or store --
// forces every later read to be re-issued, so the guest register file is
// effectively reloaded per instruction and the host register allocator is
// never allowed to own it.
//
// This pass renames every *directly indexed* GPR access in the body to a
// function-local variable. The body's own control flow is untouched: the
// locals are loaded once at the single normal entry and written back once
// before each `return`. That is exactly equivalent for straight-line code,
// because nothing between those points can observe `context->gpr[]` except the
// body itself.
//
// Eligibility is deliberately narrow, because three shapes would break that
// equivalence, and each one is cheap to detect in the emitted text:
//
//   * interior entries and `call_return_*` continuations. Both are entered
//     from outside this body, so both are repaired at their own label: an entry
//     loads the locals, a continuation writes them back before the checkpoint
//     that follows it runs.
//   * runtime-indexed GPR access (`context->gpr[reg]`, emitted by the
//     `lmw`/`stmw` register-range loops). A variable index means the resident
//     set cannot be proven complete at that access, so such a body is rejected
//     outright rather than left half-renamed.
//   * a call that receives `context`, which may read or write the register file
//     directly. Guest loads and stores receive `(memory, ...)` only, and the
//     condition-register helpers touch `context->cr`, never `context->gpr`.
//
// `ensure_fpu_available` is deliberately *not* on that list even though it
// takes `context`: its hot path is a read of `context->msr`, and its escalation
// path re-enters at the `fpu_retry_<pc>` alias, which jumps to `label_<pc>` --
// an entry label that already reloads the locals. Measured over 200 shards,
// every one of 33,084 such aliases has that shape.
//
// Returns `None` when the body was left untouched, so the caller can keep the
// original text without re-allocating it.
/// End offset of a `call_return_<8 hex digits>:` label found at or after
/// `from`, or `None`. Scanned by hand rather than with a regex so this file
/// needs no new import.
fn call_return_label_end(haystack: &str, from: usize) -> Option<usize> {
    const PREFIX: &str = "call_return_";
    let mut search = from;
    while let Some(found) = haystack[search..].find(PREFIX) {
        let at = search + found;
        let suffix = at + PREFIX.len();
        let digits = haystack[suffix..]
            .bytes()
            .take_while(|byte| byte.is_ascii_hexdigit())
            .count();
        if digits == 8 && haystack.as_bytes().get(suffix + digits) == Some(&b':') {
            return Some(suffix + digits + 1);
        }
        search = suffix;
    }
    None
}

// A label definition is distinguished from a reference to it by the newline that
// always follows the defining colon. This must NOT be a column-0 test: two of the
// five call_return_ emission sites write the label inline, immediately after a
// preceding statement's `\n`, so an inline definition is never at column 0:
//
//   writeln!(output, "call_return_{entry:08X}:")                      // col 0
//   "\ncall_return_{return_pc:08X}:\n{checkpoint}    goto label_..."   // inline
//
// whereas every reference is `case 0x...u: goto <label>;` and therefore ends in a
// semicolon. The terminator is what separates the two, so the caller passes a
// needle already carrying its trailing newline.
fn find_label_definition(haystack: &str, from: usize, needle: &str) -> Option<usize> {
    debug_assert!(needle.ends_with('\n'), "a label needle must carry its terminator");
    haystack[from..].find(needle).map(|offset| from + offset)
}

fn apply_integer_gpr_residency(
    address: u32,
    body: &str,
    entries: &BTreeSet<u32>,
) -> Option<String> {
    /// A subscript is directly indexed only when it is entirely decimal digits.
    /// That is the whole test: a directly indexed access is an lvalue or an
    /// rvalue of `resident_rN` either way, so no read/write distinction is
    /// required here.
    fn literal_index(spec: &str) -> Option<u32> {
        if spec.is_empty() || !spec.bytes().all(|byte| byte.is_ascii_digit()) {
            return None;
        }
        spec.parse::<u32>().ok().filter(|index| *index < 32)
    }

    // Interior entry labels, as the emitter writes them. An entry outside the
    // function's own range has no label in this body and is skipped rather than
    // treated as an error.
    let entry_labels: Vec<String> = entries
        .iter()
        .filter_map(|entry| {
            entry
                .checked_sub(address)
                .filter(|offset| *offset % 4 == 0)
                .map(|offset| format!("label_{:08X}:", address + offset))
        })
        .collect();
    // A call that can observe the register file directly. Every one of these
    // takes `context` as an argument; no guest load, guest store or
    // condition-register helper does.
    for callee in [
        "call_guest(",
        "call_guest_cached(",
        "native_",
        "branch_checkpoint(",
        "branch_checkpoint_taken(",
        "architectural_interrupt_checkpoint(",
        "enter_guest_function(",
        // A direct guest call. The emitter writes these as
        // `rmge01::fn_XXXXXXXX(context, memory, services)` (`emit_resolved_generated_direct_call`,
        // the tail-call and particle/JPA paths), i.e. the callee is handed the
        // caller's `context` and will read its arguments out of `context->gpr[3..]`
        // and publish its result back into `context->gpr[3]`. Renaming the caller's
        // operand to a local would therefore publish stale arguments and then
        // clobber the callee's result on writeback. Both directions are silent:
        // the C++ still compiles and the module still loads.
        //
        // These were missing. The reason it went unnoticed is structural: the
        // fenced implementation (`lower_resident_integer_direct_calls`, which syncs
        // around direct calls with `emit_resident_gpr_sync`) is gated on
        // `guest_resident_leaf` -- default `false`, and never set by the installer
        // -- while this function is the one reached through the general
        // `guest_resident_integer` option. The only tests that assert the fence
        // call that other function directly, so they passed. See
        // `AgentWork/agent-21/A21-9-*.md`.
        //
        // `guest_resident_integer` is `false` today (A23-7 measured the transform
        // net-negative), so this list entry is dormant rather than load-bearing.
        // It is here because that option is explicitly expected to be revisited
        // once the emitted code has longer call-free straight-line runs, and a
        // silent misexecution is not something a re-enable should have to
        // rediscover.
        //
        // The net-negative result was re-derived structurally rather than trusted
        // (AgentWork/agent-23/52-residency-resolved.md). Counting this function's
        // own operand sets over the real module -- `U` loads at entry,
        // `U * label_` reloads, and `W` stores per `call_return_` and per return --
        // reproduces the earlier `saved` figure to 0.006 % (676,660 against
        // 676,620) and gives `added` of at least 1.17x it. So the net is negative
        // under either estimate of the writeback multiplicity.
        //
        // The reason is CALL DENSITY, not register reuse: the real module averages
        // 5.16 written registers against 4.219 `call_return_` continuations and
        // 1.683 returns per body, so every call publishes the locals and every
        // continuation publishes them again. Extending this transform to more
        // bodies cannot help while a body calls out ~4.2 times. The lever that
        // could is narrowing the fence list above: each entry removed is up to
        // `W` stores saved per call site. That requires proving the callee never
        // observes `context->gpr[]`, which is why the list is conservative.
        //
        // Cheap and correct now. The better fix, when someone wants the coverage
        // this gives up, is to make a direct call a sync point here: publish
        // `store_lines` before it and reload `loads` after it, exactly as
        // `emit_resident_gpr_sync` already does.
        "rmge01::fn_",
        "call_guest_resolved(",
        "call_guest_direct_resolved(",
        // Both of these take a `const PpcContext*` and only read it, so they
        // are safe today. They are listed anyway so that a future edit which
        // gives either one a non-const context cannot silently invalidate the
        // writeback set of every body they appear in.
        "trace_audio_function_entry(",
        "trace_movie_function_entry(",
    ] {
        if body.contains(callee) {
            return None;
        }
    }

    const OPEN: &str = "context->gpr[";
    let mut rewritten = String::with_capacity(body.len() + body.len() / 64);
    let mut cursor = 0usize;
    let mut used = [false; 32];
    let mut renamed = 0usize;
    while let Some(found) = body[cursor..].find(OPEN) {
        let start = cursor + found;
        let after_bracket = start + OPEN.len();
        let Some(close_offset) = body[after_bracket..].find(']') else {
            break;
        };
        let close = after_bracket + close_offset;
        let spec = &body[after_bracket..close];
        let Some(index) = literal_index(spec) else {
            // Runtime-indexed access: reject the whole body rather than emit a
            // mixture of resident and memory register state.
            return None;
        };
        rewritten.push_str(&body[cursor..start]);
        let _ = write!(rewritten, "resident_r{index}");
        used[index as usize] = true;
        cursor = close + 1;
        renamed += 1;
    }
    if renamed == 0 {
        return None;
    }
    rewritten.push_str(&body[cursor..]);

    // Only the registers this body actually names get a local and a load. A
    // typical translated function touches a handful of the 32, so this keeps
    // the added stack traffic proportional to real register use instead of
    // charging every function for all 32. The host compiler removes any local
    // it proves dead.
    // Two renderings of the same reload, because the two places it is emitted
    // are in different scopes:
    //
    //   * at the function's normal entry the locals do not exist yet, so they are
    //     *declared* there;
    //   * at an interior-entry label they already exist, so they are only
    //     *assigned*. Emitting the declaring form there is a redefinition
    //     (C2374/C2086), which is what a body carrying an `entries` set hits the
    //     moment it becomes eligible.
    let mut loads = String::new();
    let mut load_assignments = String::new();
    for index in 0..32u32 {
        if !used[index as usize] {
            continue;
        }
        let _ = writeln!(
            loads,
            "    std::uint32_t resident_r{index} = context->gpr[{index}];"
        );
        let _ = writeln!(
            load_assignments,
            "    resident_r{index} = context->gpr[{index}];"
        );
    }
    // Write back only the registers this body *writes*. A register that is only
    // read still holds the value loaded at entry and can never have diverged:
    // the eligibility rule above rejects every call that receives `context`, so
    // nothing between the load and the return can change `context->gpr[]`
    // behind this body's back. Omitting read-only registers is therefore both
    // correct and narrower -- storing one back would clobber whatever a callee
    // published into it before this function returned.
    let mut store_lines: Vec<String> = Vec::new();
    for index in 0..32u32 {
        if !used[index as usize] {
            continue;
        }
        let mut probe = String::with_capacity(24);
        let _ = write!(probe, "resident_r{index} =");
        // `resident_rN ==` is a comparison, not a store. Reject a doubled '='.
        let assigned = rewritten.match_indices(&probe).any(|(offset, _)| {
            !rewritten[offset + probe.len()..].starts_with('=')
        });
        if assigned {
            store_lines.push(format!("context->gpr[{index}] = resident_r{index};"));
        }
    }
    if store_lines.is_empty() {
        return None;
    }

    // Place the loads at the normal entry, at *function scope*.
    //
    // This function is handed one of two shapes, and they must be distinguished
    // by content rather than assumed:
    //
    //   * `lower_words_with_config` hands over the instruction stream only, so
    //     the text starts with `    {` (or an interior-entry dispatch). The loads
    //     go at offset 0.
    //   * A complete function body starts with its signature line. The loads go
    //     after the prologue, i.e. after the signature and any prologue text,
    //     and before the first instruction block.
    //
    // Getting this wrong is not a formatting nit. Splicing the declarations
    // *inside* the first `    { ... }` instruction block put every `resident_rN`
    // out of scope at that block's closing brace, so the generated module failed
    // to compile outright -- C2065 on every later use, across the whole module.
    // The test bodies all carried signatures while the real generator passed
    // bare instruction streams, which is exactly why the tests stayed green
    // while the module could not build. Do not reintroduce a signature-shaped
    // assumption here; `residency_declarations_land_at_function_scope_not_inside_a_block`
    // pins both shapes.
    let prologue_end = if body.starts_with("void ") || body.starts_with("[[") {
        // A complete body: skip the signature line, then any prologue text the
        // renderer emits (unused-parameter casts), and stop before the first
        // instruction block. A dispatch switch ends at `goto fpu_normal_entry_`,
        // which is the last prologue element when it is present.
        let signature_end = body.find('\n').map_or(0, |newline| newline + 1);
        let mut end = signature_end;
        for cast in [
            "    static_cast<void>(context);\n",
            "    static_cast<void>(memory);\n",
            "    static_cast<void>(services);\n",
        ] {
            if body.get(end..).is_some_and(|rest| rest.starts_with(cast)) {
                end += cast.len();
            }
        }
        if let Some(goto_at) = body.find("    goto fpu_normal_entry_") {
            end = end.max(goto_at);
        }
        end
    } else {
        0
    };
    let (prefix, rest) = rewritten.split_at(prologue_end);

    // Both label kinds need the locals repaired, for the same reason: the host
    // arrived from outside this body, so memory is authoritative and the locals
    // may be stale.
    //
    //   * an interior entry jumps to `label_<entry>` past the prologue, so the
    //     locals have never been loaded at all;
    //   * a `call_return_<pc>` continuation is entered after a nested guest call
    //     ran, and that call updated `context->gpr[]` while the locals kept
    //     their pre-call values.
    //
    // At an entry the locals are loaded; at a continuation they are written
    // back. No other label needs anything, because every other label is a
    // branch target inside this body and a branch does not touch the register
    // file.
    //
    // This is a single left-to-right pass. `reload_cursor` advances past the
    // inserted text, so a label introduced by one insert is never re-matched,
    // and the writeback lines contain no label or gpr access of their own.
    let mut reloaded = String::with_capacity(
        rest.len() + entry_labels.len() * load_assignments.len() + rest.len() / 32,
    );
    let mut reload_cursor = 0usize;
    loop {
        let entry = entry_labels
            .iter()
            .filter_map(|label| {
                find_label_definition(rest, reload_cursor, &format!("{label}\n"))
                    .map(|offset| (offset, &rest[offset..offset + label.len()], false))
            })
            .min_by_key(|(offset, _, _)| *offset);
        let continuation = call_return_label_end(rest, reload_cursor).and_then(|end| {
            rest[..end - 1]
                .rfind("call_return_")
                .map(|name_start| (name_start, &rest[name_start..end], true))
        });
        let next = match (entry, continuation) {
            (Some(a), Some(b)) => Some(if a.0 <= b.0 { a } else { b }),
            (Some(a), None) => Some(a),
            (None, Some(b)) => Some(b),
            (None, None) => None,
        };
        let Some((at, label, is_continuation)) = next else {
            break;
        };
        let after = at + label.len();
        reloaded.push_str(&rest[reload_cursor..after]);
        reloaded.push('\n');
        if is_continuation {
            for line in &store_lines {
                reloaded.push_str(line);
                reloaded.push('\n');
            }
        } else {
            // An interior entry: the locals already exist at function scope, so
            // this must assign, not re-declare them.
            reloaded.push_str(&load_assignments);
        }
        reload_cursor = after;
    }
    reloaded.push_str(&rest[reload_cursor..]);
    let rest = reloaded.as_str();

    let mut output = String::with_capacity(rewritten.len() + loads.len() + rest.len());
    output.push_str(prefix);
    output.push_str(&loads);
    let mut returned = false;
    let mut cursor = 0usize;
    // Two exits with nothing between them would otherwise each carry a full
    // writeback set even though no local changed in between. Measured over 60
    // shards, 1,383 of 2,707 bodies emitted such a duplicate pair -- 44,067
    // redundant stores. They are dead (the same unchanged value written twice,
    // so the compiler removes the first), but they are pure code-size growth on
    // an already 51 MB module, so a set is emitted only when the locals may have
    // moved since the previous one.
    let mut last_writeback = 0usize;
    // Match only a `return;` that stands alone on its line. That is exactly the set
    // of true function exits, and every generated body ends with an indented
    // `return;` alone on its line immediately before the closing brace.
    //
    // A substring search for `return;` is not equivalent: it also matches a
    // `return;` that shares its line with other code, and the splice then lands at
    // the start of that line instead of on the `return;` -- so the writeback runs on
    // paths that never return. Measured over the first 40 shards of the retained
    // module: 81 of 4 045 `return;` occurrences are mid-line, in exactly two shapes,
    // `if ((true) && (galaxy::cr_bit(...))) return;` and `default: return;`.
    // Neither is a function exit, so neither may carry a writeback.
    //
    // This walks lines rather than searching for the token, so a mid-line `return;`
    // is skipped without being able to disturb the cursor. `line_start` is still the
    // start of the `return;` line, so the `last_writeback` bookkeeping below is
    // unchanged.
    while cursor < rest.len() {
        let line_start = cursor;
        let line_end = rest[line_start..]
            .find('\n')
            .map_or(rest.len(), |offset| line_start + offset);
        let line = &rest[line_start..line_end];
        // Explicit rather than `trim_end()`: the exit test must not depend on how a
        // trimming helper classifies whitespace, and this states the real condition
        // -- the line ends with `return;` and everything before it is indentation.
        if !line.ends_with("return;")
            || !line[..line.len() - "return;".len()]
                .bytes()
                .all(|byte| byte == b' ' || byte == b'\t')
        {
            // Copy the skipped line INCLUDING its trailing newline. Advancing the
            // cursor past the newline without emitting it would silently strip every
            // newline before an exit while leaving non-exit lines untouched, which is
            // still valid C++ but destroys the body formatting.
            let copy_end = if line_end < rest.len() { line_end + 1 } else { line_end };
            output.push_str(&rest[cursor..copy_end]);
            cursor = copy_end;
            continue;
        }
        // Everything before `return;` is indentation by construction, so this
        // slice is the indent to reuse for the writeback lines.
        let indent = &line[..line.len() - "return;".len()];
        output.push_str(&rest[cursor..line_start]);
        // A label means control arrived from elsewhere; `resident_r` means a
        // local was named, which covers both a write and a re-read. Either way
        // the previous set no longer describes this exit.
        let since_previous = &rest[last_writeback..line_start];
        if !returned || since_previous.contains(':') || since_previous.contains("resident_r") {
            for line in &store_lines {
                output.push_str(indent);
                output.push_str(line);
                output.push('\n');
            }
            last_writeback = line_start;
        }
        output.push_str(indent);
        output.push_str("return;");
        // Carry the exit line's newline through. `rest[cursor..]` is appended after
        // the loop, so without this the trailing text would be glued onto `return;`.
        if line_end < rest.len() {
            output.push('\n');
            cursor = line_end + 1;
        } else {
            cursor = rest.len();
        }
        returned = true;
    }
    if !returned {
        // A body that faults through every path has no exit to attach the
        // writeback to; keep the original text rather than inventing one.
        return None;
    }
    output.push_str(&rest[cursor..]);
    Some(output)
}

/// Apply straight-line GPR residency to one lowered body when the option is on.
/// Returns the body unchanged otherwise, and also when the body is not eligible,
/// so an ineligible function costs one string scan and no allocation.
fn apply_residency_to_body(
    address: u32,
    body: String,
    entries: &BTreeSet<u32>,
    options: &ModuleTranslationOptions,
) -> String {
    if !options.guest_resident_integer {
        return body;
    }
    match apply_integer_gpr_residency(address, &body, entries) {
        Some(resident) => resident,
        None => body,
    }
}

fn lower_words_with_config(
    address: u32,
    bytes: &[u8],
    entries: &BTreeSet<u32>,
    external_call_return_entries: &BTreeSet<u32>,
    config: LoweringConfig<'_>,
) -> Result<String, TranslationError> {
    let LoweringConfig {
        callable_entries,
        exact_fpu_functions,
        immediate_overrides,
        local_lane_profile,
        flat_ram_reads,
        fused_paired_binary,
        inline_scalar_single_binary,
        typed_region_gpr_mask,
    } = config;
    if local_lane_profile != LocalLaneProfile::None
        && (!entries.is_empty() || immediate_overrides.is_some())
    {
        return Err(local_paired_proof_error(
            address,
            "local lanes require normal entry and literal instructions",
        ));
    }
    let mut lane_facts = [[false; 32]; 2];
    let mut output = String::new();
    if typed_region_gpr_mask != 0 {
        for register in 0..32 {
            if typed_region_gpr_mask & (1u32 << register) != 0 {
                writeln!(
                    output,
                    "    std::uint32_t resident_r{register} = context->gpr[{register}];"
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
        }
    }
    if flat_ram_reads {
        // Each invocation binds its own GuestMemoryV1 for its lifetime. The
        // binding precedes interior-entry dispatch.
        output.push_str(
            "    [[maybe_unused]] const std::byte* flat_guest_read_base = galaxy::guest_flat_read_base(memory);\n",
        );
    }
    if local_lane_profile != LocalLaneProfile::None {
        output.push_str("    std::uint32_t lane0[32]{};\n    std::uint32_t lane1[32]{};\n");
    }
    let function_end = address.wrapping_add(bytes.len() as u32);
    let internal_return_targets = internal_link_return_targets(address, bytes);
    // As in WiiCompiled, sites with the same target set share one
    // LR-continuation dispatch. Only the repeated non-link BCLR switch is
    // shared; call-return labels and checkpoints are unchanged.
    let share_lr_continuation_dispatch = !internal_return_targets.is_empty()
        && bytes
            .chunks_exact(4)
            .filter(|chunk| {
                let word = u32::from_be_bytes((*chunk).try_into().expect("four-byte PPC word"));
                word & 1 == 0 && Ins::new(word, Extensions::gekko_broadway()).op == Opcode::Bclr
            })
            .take(2)
            .count()
            > 1;
    debug_assert!(external_call_return_entries.is_subset(entries));
    let call_return_targets = internal_return_targets
        .union(external_call_return_entries)
        .copied()
        .collect::<BTreeSet<_>>();
    let mut branch_targets = internal_branch_targets(address, bytes);
    branch_targets.extend(call_return_targets.iter().copied());
    // Every potentially suppressed FP instruction gets an interior alias so
    // an exception-7 RFI can restart there. Aliases that are not branch
    // targets go through a guarded reentry stub, so the straight-line path
    // keeps its MSR[FP] check.
    let structural_reentry_targets = branch_targets.clone();
    let fpu_retry_entries = entries
        .iter()
        .copied()
        .filter(|entry| {
            if structural_reentry_targets.contains(entry) {
                return false;
            }
            debug_assert!(*entry > address && *entry < function_end && *entry % 4 == 0);
            let offset = (*entry - address) as usize;
            let word = u32::from_be_bytes(
                bytes[offset..offset + 4]
                    .try_into()
                    .expect("interior entry remains instruction-aligned"),
            );
            opcode_requires_fpu(Ins::new(word, Extensions::gekko_broadway()).op)
        })
        .collect::<BTreeSet<_>>();
    if !entries.is_empty() {
        writeln!(
            output,
            "    if (context->pc != 0x{address:08X}u) [[unlikely]] {{\n    switch (context->pc) {{"
        )
        .map_err(|_| TranslationError::Formatting)?;
        for entry in entries {
            debug_assert!(*entry > address && *entry < function_end && *entry % 4 == 0);
            if !fpu_retry_entries.contains(entry) {
                branch_targets.insert(*entry);
            }
            let destination = if fpu_retry_entries.contains(entry) {
                "fpu_retry"
            } else if external_call_return_entries.contains(entry) {
                "call_return"
            } else {
                "label"
            };
            writeln!(
                output,
                "        case 0x{entry:08X}u: goto {destination}_{entry:08X};"
            )
            .map_err(|_| TranslationError::Formatting)?;
        }
        writeln!(
            output,
            "        default: galaxy::guest_execution_fault(services, context->pc, \"invalid interior function entry\");\n    }}\n    }}"
        )
            .map_err(|_| TranslationError::Formatting)?;
    }
    if !fpu_retry_entries.is_empty() {
        writeln!(output, "    goto fpu_normal_entry_{address:08X};")
            .map_err(|_| TranslationError::Formatting)?;
        for entry in &fpu_retry_entries {
            writeln!(
                output,
                "fpu_retry_{entry:08X}:\n    galaxy::ensure_fpu_available(services, 0x{entry:08X}u, context, memory);\n    goto label_{entry:08X};"
            )
            .map_err(|_| TranslationError::Formatting)?;
        }
        writeln!(output, "fpu_normal_entry_{address:08X}:")
            .map_err(|_| TranslationError::Formatting)?;
    }
    let wgpipe_store_runs = if local_lane_profile == LocalLaneProfile::None {
        discover_wgpipe_store_runs(address, bytes, &branch_targets, immediate_overrides)
    } else {
        BTreeMap::new()
    };
    if typed_region_gpr_mask != 0 && !wgpipe_store_runs.is_empty() {
        return Err(TranslationError::TypedRegionProof {
            address,
            reason: "batched FIFO stores require a separate typed boundary".to_owned(),
        });
    }
    let wgpipe_store_continuations = wgpipe_store_runs
        .values()
        .flat_map(|run| run.stores.iter().skip(1).map(|store| store.pc))
        .collect::<BTreeSet<_>>();
    let mut final_instruction_is_terminal = false;
    // True when the current straight-line path has already checked MSR[FP].
    // Reset at every control-flow label and at any instruction that can
    // transfer control or change architectural state. FP retry aliases use
    // the reentry stubs above and do not reset it.
    let mut fpu_availability_proven = false;
    for (index, chunk) in bytes.chunks_exact(4).enumerate() {
        let pc = address + index as u32 * 4;
        if wgpipe_store_continuations.contains(&pc) {
            continue;
        }
        let word = u32::from_be_bytes(chunk.try_into().expect("four-byte instruction"));
        let instruction = Ins::new(word, Extensions::gekko_broadway());
        let instruction_output_start = output.len();
        if local_lane_profile != LocalLaneProfile::None {
            require_local_paired_opcode(local_lane_profile, pc, word, instruction.op)?;
        }
        let immediate_override = immediate_overrides.and_then(|overrides| overrides.get(&pc));
        if let Some(immediate_override) = immediate_override {
            if !matches!(
                instruction.op,
                Opcode::Addi
                    | Opcode::Addis
                    | Opcode::Lbz
                    | Opcode::Lbzu
                    | Opcode::Lhz
                    | Opcode::Lhzu
                    | Opcode::Lha
                    | Opcode::Lhau
                    | Opcode::Lwz
                    | Opcode::Lwzu
                    | Opcode::Lmw
                    | Opcode::Stb
                    | Opcode::Stbu
                    | Opcode::Sth
                    | Opcode::Sthu
                    | Opcode::Stw
                    | Opcode::Stwu
                    | Opcode::Stmw
                    | Opcode::Lfs
                    | Opcode::Lfsu
                    | Opcode::Lfd
                    | Opcode::Lfdu
                    | Opcode::Stfs
                    | Opcode::Stfsu
                    | Opcode::Stfd
                    | Opcode::Stfdu
            ) {
                return Err(TranslationError::InvalidImmediateOverride {
                    address: pc,
                    kind: immediate_override.kind,
                    opcode: format!("{:?}", instruction.op),
                });
            }
        }
        let has_reentry_label = branch_targets.contains(&pc);
        let has_fpu_retry_label = fpu_retry_entries.contains(&pc);
        if has_reentry_label {
            if typed_region_gpr_mask != 0 {
                emit_resident_gpr_sync(&mut output, typed_region_gpr_mask, false)?;
            }
            writeln!(output, "label_{pc:08X}:").map_err(|_| TranslationError::Formatting)?;
            if typed_region_gpr_mask != 0 {
                emit_resident_gpr_sync(&mut output, typed_region_gpr_mask, true)?;
            }
            fpu_availability_proven = false;
        } else if has_fpu_retry_label {
            writeln!(output, "label_{pc:08X}:").map_err(|_| TranslationError::Formatting)?;
        }
        if opcode_requires_fpu(instruction.op) && !fpu_availability_proven {
            writeln!(
                output,
                "    galaxy::ensure_fpu_available(services, 0x{pc:08X}u, context, memory);"
            )
            .map_err(|_| TranslationError::Formatting)?;
            fpu_availability_proven = true;
        }
        // FunctionAsync lifecycle markers run before the instruction at `pc`,
        // after the previous one has committed, so return-continuation
        // markers survive a non-local OSLoadContext/RFI unwind.
        let function_async_marker = function_async_marker(address, pc);
        if let Some(marker) = function_async_marker {
            writeln!(
                output,
                "    galaxy::function_async_trace_callback(services, galaxy::{marker}, context, memory);"
            )
            .map_err(|_| TranslationError::Formatting)?;
        }
        emit_main_frame_stage_hook(&mut output, address, pc)?;
        if address == 0x8036_7D28
            && function_end == 0x8036_7E84
            && pc == 0x8036_7E64
            && word == 0xE3C1_0038
        {
            writeln!(
                output,
                "    galaxy::apply_rmge01_cinema_vertical(context, memory, services);\n    galaxy::apply_rmge01_hud_anchors(context, memory, services);"
            )
            .map_err(|_| TranslationError::Formatting)?;
        }
        // RMGE01's animation is complete here. Adjust only the identified
        // backing panes before their matrices and hit regions are calculated;
        // the original load/call and continuation still run.
        if address == 0x8036_7D28
            && function_end == 0x8036_7E84
            && pc == 0x8036_7D78
            && word == 0x807F_0004
        {
            writeln!(
                output,
                "    galaxy::apply_rmge01_layout_backings(context, memory, services);"
            )
            .map_err(|_| TranslationError::Formatting)?;
        }
        // Home RSO Pane::CalculateMtx entry. Runtime geometry checks tell its
        // main backing apart from pointers.
        if (address, function_end, pc, word) == (0x8100_00D8, 0x8101_B120, 0x8101_18F8, 0x9421_FF20)
        {
            writeln!(
                output,
                "    galaxy::apply_rmge01_home_backings(context, memory, services, 0x810118F8u);"
            )
            .map_err(|_| TranslationError::Formatting)?;
        }
        // The Home Pane global matrix is complete on every path to this join
        // and children are not calculated yet. The original lbz still runs.
        if (address, function_end, pc, word) == (0x8100_00D8, 0x8101_B120, 0x8101_1B1C, 0x881D_00CF)
        {
            writeln!(
                output,
                "    galaxy::apply_rmge01_home_vertical(context, memory, services, 0x81011B1Cu);"
            )
            .map_err(|_| TranslationError::Formatting)?;
        }
        let typed_pure_access = if typed_region_gpr_mask != 0
            && !has_reentry_label
            && !has_fpu_retry_label
            && function_async_marker.is_none()
            && main_frame_stage_marker(address, pc).is_none()
        {
            typed_region_pure_gpr_access(instruction.op, word)
        } else {
            None
        };
        if typed_region_gpr_mask != 0 && typed_pure_access.is_none() {
            emit_resident_gpr_sync(&mut output, typed_region_gpr_mask, false)?;
        }
        let typed_instruction_start = output.len();
        output.push_str("    {\n");
        final_instruction_is_terminal = false;

        match instruction.op {
            Opcode::Addi => {
                let access = guest_integer_gpr_access(instruction.op, word)
                    .expect("addi GPR roles are classified");
                let target = access.destination;
                let base = access.first_source.unwrap_or(0);
                let expression = if let Some(immediate_override) = immediate_override {
                    if immediate_override.kind != PpcImmediateKind::Low16 {
                        return Err(TranslationError::InvalidImmediateOverride {
                            address: pc,
                            kind: immediate_override.kind,
                            opcode: "Addi".to_owned(),
                        });
                    }
                    gpr_plus_expression(
                        base,
                        &relocated_low16_expression(&immediate_override.expression),
                    )
                } else {
                    gpr_plus_u32_expression(base, signed_immediate(word) as u32)
                };
                writeln!(output, "    context->gpr[{target}] = {expression};",)
                    .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Addis => {
                let access = guest_integer_gpr_access(instruction.op, word)
                    .expect("addis GPR roles are classified");
                let target = access.destination;
                let base = access.first_source.unwrap_or(0);
                let expression = if let Some(immediate_override) = immediate_override {
                    if immediate_override.kind != PpcImmediateKind::HighAdjusted16 {
                        return Err(TranslationError::InvalidImmediateOverride {
                            address: pc,
                            kind: immediate_override.kind,
                            opcode: "Addis".to_owned(),
                        });
                    }
                    gpr_plus_expression(
                        base,
                        &relocated_high_adjusted16_expression(&immediate_override.expression),
                    )
                } else {
                    gpr_plus_u32_expression(base, (signed_immediate(word) as u32) << 16)
                };
                writeln!(output, "    context->gpr[{target}] = {expression};",)
                    .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Ori | Opcode::Oris | Opcode::Xori | Opcode::Xoris => {
                let source = gpr_rt(word);
                let target = gpr_ra(word);
                let shift = if matches!(instruction.op, Opcode::Oris | Opcode::Xoris) {
                    16
                } else {
                    0
                };
                let immediate = (word & 0xFFFF) << shift;
                let operator = if matches!(instruction.op, Opcode::Ori | Opcode::Oris) {
                    '|'
                } else {
                    '^'
                };
                writeln!(
                    output,
                    "    context->gpr[{target}] = context->gpr[{source}] {operator} 0x{immediate:08X}u;"
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Andi_ | Opcode::Andis_ => {
                let source = gpr_rt(word);
                let target = gpr_ra(word);
                let shift = if instruction.op == Opcode::Andis_ {
                    16
                } else {
                    0
                };
                let immediate = (word & 0xFFFF) << shift;
                writeln!(
                    output,
                    "    context->gpr[{target}] = context->gpr[{source}] & 0x{immediate:08X}u;"
                )
                .map_err(|_| TranslationError::Formatting)?;
                writeln!(
                    output,
                    "    galaxy::update_cr0(context, context->gpr[{target}]);"
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Lbz
            | Opcode::Lbzu
            | Opcode::Lhz
            | Opcode::Lhzu
            | Opcode::Lha
            | Opcode::Lhau
            | Opcode::Lwz
            | Opcode::Lwzu => {
                let target = gpr_rt(word);
                let base = gpr_ra(word);
                let update = matches!(
                    instruction.op,
                    Opcode::Lbzu | Opcode::Lhzu | Opcode::Lhau | Opcode::Lwzu
                );
                let ea = d_form_effective_address(
                    &mut output,
                    pc,
                    word,
                    base,
                    update,
                    immediate_override,
                )?;
                let expression = match instruction.op {
                    Opcode::Lbz | Opcode::Lbzu =>
                        integer_guest_load_expression(1, &ea, pc, flat_ram_reads),
                    Opcode::Lhz | Opcode::Lhzu =>
                        integer_guest_load_expression(2, &ea, pc, flat_ram_reads),
                    Opcode::Lha | Opcode::Lhau => format!(
                        "static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>({})))",
                        integer_guest_load_expression(2, &ea, pc, flat_ram_reads)
                    ),
                    Opcode::Lwz | Opcode::Lwzu =>
                        integer_guest_load_expression(4, &ea, pc, flat_ram_reads),
                    _ => unreachable!(),
                };
                writeln!(output, "    context->gpr[{target}] = {expression};")
                    .map_err(|_| TranslationError::Formatting)?;
                // RMGE01 SCGetAspectRatio ends with an lbz of its normalized
                // stack byte. Keep that load and all earlier SC/NAND, CR and
                // stack effects, then select the game's native 4:3 mode at
                // the return value. main.dol (SHA-1 9a71008a...):
                // 0x804D074C = lbz r3,8(r1); the helper also updates the byte.
                if address == 0x804D_0708
                    && function_end == 0x804D_075C
                    && pc == 0x804D_074C
                    && word == 0x8861_0008
                {
                    output.push_str(
                        "    galaxy::apply_experimental_rmge01_native_four_three_aspect_read(context, memory, services);\n",
                    );
                }
                if update {
                    writeln!(output, "    context->gpr[{base}] = {ea};")
                        .map_err(|_| TranslationError::Formatting)?;
                }
            }
            Opcode::Lmw => {
                let target = gpr_rt(word);
                let base = gpr_ra(word);
                let ea = d_form_effective_address(
                    &mut output,
                    pc,
                    word,
                    base,
                    true,
                    immediate_override,
                )?;
                let load = integer_guest_load_expression(
                    4,
                    &format!("{ea} + (reg - {target}u) * 4u"),
                    pc,
                    flat_ram_reads,
                );
                writeln!(
                    output,
                    "    for (std::uint32_t reg = {target}u; reg < 32u; ++reg) {{\n        context->gpr[reg] = {load};\n    }}"
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Stb
            | Opcode::Stbu
            | Opcode::Sth
            | Opcode::Sthu
            | Opcode::Stw
            | Opcode::Stwu => {
                if let Some(run) = wgpipe_store_runs.get(&pc) {
                    emit_wgpipe_store_run(&mut output, run)?;
                } else {
                    let source = gpr_rt(word);
                    let base = gpr_ra(word);
                    let update =
                        matches!(instruction.op, Opcode::Stbu | Opcode::Sthu | Opcode::Stwu);
                    let ea = d_form_effective_address(
                        &mut output,
                        pc,
                        word,
                        base,
                        update,
                        immediate_override,
                    )?;
                    let helper = match instruction.op {
                        Opcode::Stb | Opcode::Stbu => "guest_store_u8",
                        Opcode::Sth | Opcode::Sthu => "guest_store_u16",
                        Opcode::Stw | Opcode::Stwu => "guest_store_u32",
                        _ => unreachable!(),
                    };
                    let value = match instruction.op {
                        Opcode::Stb | Opcode::Stbu => {
                            format!("static_cast<std::uint8_t>(context->gpr[{source}])")
                        }
                        Opcode::Sth | Opcode::Sthu => {
                            format!("static_cast<std::uint16_t>(context->gpr[{source}])")
                        }
                        Opcode::Stw | Opcode::Stwu => format!("context->gpr[{source}]"),
                        _ => unreachable!(),
                    };
                    writeln!(
                        output,
                        "    galaxy::{helper}(memory, {ea}, {value}, services, 0x{pc:08X}u);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                    if update {
                        writeln!(output, "    context->gpr[{base}] = {ea};")
                            .map_err(|_| TranslationError::Formatting)?;
                    }
                }
            }
            Opcode::Stmw => {
                let source = gpr_rt(word);
                let base = gpr_ra(word);
                let ea = d_form_effective_address(
                    &mut output,
                    pc,
                    word,
                    base,
                    true,
                    immediate_override,
                )?;
                writeln!(
                    output,
                    "    for (std::uint32_t reg = {source}u; reg < 32u; ++reg) {{\n        galaxy::guest_store_u32(memory, {ea} + (reg - {source}u) * 4u, context->gpr[reg], services, 0x{pc:08X}u);\n    }}"
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Lbzx
            | Opcode::Lbzux
            | Opcode::Lhzx
            | Opcode::Lhzux
            | Opcode::Lhax
            | Opcode::Lhaux
            | Opcode::Lwzx
            | Opcode::Lwzux => {
                let target = gpr_rt(word);
                let base = gpr_ra(word);
                let index = gpr_rb(word);
                let update = matches!(
                    instruction.op,
                    Opcode::Lbzux | Opcode::Lhzux | Opcode::Lhaux | Opcode::Lwzux
                );
                let ea = if update {
                    emit_indexed_effective_address(&mut output, pc, base, index)?
                } else {
                    indexed_effective_address_expression(base, index)
                };
                let expression = match instruction.op {
                    Opcode::Lbzx | Opcode::Lbzux =>
                        integer_guest_load_expression(1, &ea, pc, flat_ram_reads),
                    Opcode::Lhzx | Opcode::Lhzux =>
                        integer_guest_load_expression(2, &ea, pc, flat_ram_reads),
                    Opcode::Lhax | Opcode::Lhaux => format!(
                        "static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>({})))",
                        integer_guest_load_expression(2, &ea, pc, flat_ram_reads)
                    ),
                    Opcode::Lwzx | Opcode::Lwzux =>
                        integer_guest_load_expression(4, &ea, pc, flat_ram_reads),
                    _ => unreachable!(),
                };
                writeln!(output, "    context->gpr[{target}] = {expression};")
                    .map_err(|_| TranslationError::Formatting)?;
                if update {
                    writeln!(output, "    context->gpr[{base}] = {ea};")
                        .map_err(|_| TranslationError::Formatting)?;
                }
            }
            Opcode::Stbx
            | Opcode::Stbux
            | Opcode::Sthx
            | Opcode::Sthux
            | Opcode::Stwx
            | Opcode::Stwux => {
                if let Some(run) = wgpipe_store_runs.get(&pc) {
                    emit_wgpipe_store_run(&mut output, run)?;
                } else {
                    let source = gpr_rt(word);
                    let base = gpr_ra(word);
                    let index = gpr_rb(word);
                    let update = matches!(
                        instruction.op,
                        Opcode::Stbux | Opcode::Sthux | Opcode::Stwux
                    );
                    let ea = if update {
                        emit_indexed_effective_address(&mut output, pc, base, index)?
                    } else {
                        indexed_effective_address_expression(base, index)
                    };
                    let (helper, value) = match instruction.op {
                        Opcode::Stbx | Opcode::Stbux => (
                            "guest_store_u8",
                            format!("static_cast<std::uint8_t>(context->gpr[{source}])"),
                        ),
                        Opcode::Sthx | Opcode::Sthux => (
                            "guest_store_u16",
                            format!("static_cast<std::uint16_t>(context->gpr[{source}])"),
                        ),
                        Opcode::Stwx | Opcode::Stwux => {
                            ("guest_store_u32", format!("context->gpr[{source}]"))
                        }
                        _ => unreachable!(),
                    };
                    writeln!(
                        output,
                        "    galaxy::{helper}(memory, {ea}, {value}, services, 0x{pc:08X}u);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                    if update {
                        writeln!(output, "    context->gpr[{base}] = {ea};")
                            .map_err(|_| TranslationError::Formatting)?;
                    }
                }
            }
            Opcode::Sthbrx | Opcode::Stwbrx => {
                let source = gpr_rt(word);
                let base = gpr_ra(word);
                let index = gpr_rb(word);
                let ea = emit_indexed_effective_address(&mut output, pc, base, index)?;
                let (helper, value) = if instruction.op == Opcode::Sthbrx {
                    (
                        "guest_store_u16_reversed",
                        format!("static_cast<std::uint16_t>(context->gpr[{source}])"),
                    )
                } else {
                    (
                        "guest_store_u32_reversed",
                        format!("context->gpr[{source}]"),
                    )
                };
                writeln!(
                    output,
                    "    galaxy::{helper}(memory, {ea}, {value}, services, 0x{pc:08X}u);"
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::PsqL
            | Opcode::PsqLu
            | Opcode::PsqLx
            | Opcode::PsqLux
            | Opcode::PsqSt
            | Opcode::PsqStu
            | Opcode::PsqStx
            | Opcode::PsqStux => {
                let register = gpr_rt(word);
                let base = gpr_ra(word);
                let d_form = matches!(
                    instruction.op,
                    Opcode::PsqL | Opcode::PsqLu | Opcode::PsqSt | Opcode::PsqStu
                );
                let update = matches!(
                    instruction.op,
                    Opcode::PsqLu | Opcode::PsqLux | Opcode::PsqStu | Opcode::PsqStux
                );
                if update && base == 0 {
                    return Err(TranslationError::UnsupportedInstruction {
                        address: pc,
                        opcode: format!("{:?}(invalid-ra0)", instruction.op),
                    });
                }

                let (ea, one_element, gqr_index) = if d_form {
                    (
                        emit_effective_address(
                            &mut output,
                            pc,
                            base,
                            paired_single_displacement(word),
                        )?,
                        (word >> 15) & 1,
                        (word >> 12) & 0x7,
                    )
                } else {
                    (
                        emit_indexed_effective_address(&mut output, pc, base, gpr_rb(word))?,
                        (word >> 10) & 1,
                        (word >> 7) & 0x7,
                    )
                };
                let one_element = one_element != 0;
                let is_load = matches!(
                    instruction.op,
                    Opcode::PsqL | Opcode::PsqLu | Opcode::PsqLx | Opcode::PsqLux
                );
                if is_load {
                    writeln!(
                        output,
                        "    galaxy::psq_load(context, {register}u, memory, {ea}, {gqr_index}u, {one_element}, {d_form}, services, 0x{pc:08X}u);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                } else {
                    writeln!(
                        output,
                        "    galaxy::psq_store(context, {register}u, memory, {ea}, {gqr_index}u, {one_element}, {d_form}, services, 0x{pc:08X}u);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                }
                if update {
                    writeln!(output, "    context->gpr[{base}] = {ea};")
                        .map_err(|_| TranslationError::Formatting)?;
                }
            }
            Opcode::Lfs | Opcode::Lfsu | Opcode::Lfd | Opcode::Lfdu => {
                let target = gpr_rt(word);
                let base = gpr_ra(word);
                let update = matches!(instruction.op, Opcode::Lfsu | Opcode::Lfdu);
                let ea = d_form_effective_address(
                    &mut output,
                    pc,
                    word,
                    base,
                    update,
                    immediate_override,
                )?;
                if matches!(instruction.op, Opcode::Lfs | Opcode::Lfsu) {
                    writeln!(
                        output,
                        "    galaxy::load_fpr_single(context, {target}u, memory, {ea}, services, 0x{pc:08X}u);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                    // RMGE01 inverse screen/layout conversions. The selected
                    // canvas also drives cursor drawing and pane hit tests.
                    let layout_constant = match (address, function_end, pc, word) {
                        (0x8036_6658, 0x8036_6758, 0x8036_66A8, 0xC042_1438) => Some((2, false)),
                        (0x8036_6658, 0x8036_6758, 0x8036_66B4, 0xC002_143C) => Some((0, true)),
                        (0x8036_6758, 0x8036_6864, 0x8036_67C4, 0xC002_1438) => Some((0, false)),
                        _ => None,
                    };
                    if let Some((lane, half)) = layout_constant {
                        writeln!(output,
                            "    galaxy::apply_experimental_rmge01_layout_constant(context, services, {lane}u, {half});"
                        ).map_err(|_| TranslationError::Formatting)?;
                    }
                    // As in Dusklight CC0, menu panes stay circular through a
                    // wider root canvas with inverse horizontal child scale.
                    // This lfs loads Galaxy's fixed 608-unit NW4R ortho
                    // canvas; it runs unchanged, then f3 is adjusted for the
                    // wide-aspect XFB path. main.dol: 0x803CA6FC = 0xC06219A8.
                    if address == 0x803C_A6C4
                        && function_end == 0x803C_A76C
                        && pc == 0x803C_A6FC
                        && word == 0xC062_19A8
                    {
                        output.push_str(
                            "    galaxy::apply_experimental_rmge01_nw4r_canvas_width(context, services);\n",
                        );
                    }
                    // RMGE01 CameraContext::getAspect loads a fixed 16:9 float
                    // in its widescreen branch. The original lfs runs, then f1
                    // is replaced with the selected content aspect.
                    // 0x800972B0 = lfs f1,-0x691c(r2).
                    if address == 0x8009_7288
                        && function_end == 0x8009_731C
                        && pc == 0x8009_72B0
                        && word == 0xC022_96E4
                    {
                        output.push_str(
                            "    galaxy::apply_experimental_rmge01_camera_aspect(context, services);\n",
                        );
                    }
                } else {
                    writeln!(
                        output,
                        "    context->fpr_bits[{target}] = galaxy::guest_load_u64(memory, {ea}, services, 0x{pc:08X}u);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                }
                if update {
                    writeln!(output, "    context->gpr[{base}] = {ea};")
                        .map_err(|_| TranslationError::Formatting)?;
                }
            }
            Opcode::Stfs | Opcode::Stfsu | Opcode::Stfd | Opcode::Stfdu => {
                let source = gpr_rt(word);
                let base = gpr_ra(word);
                let update = matches!(instruction.op, Opcode::Stfsu | Opcode::Stfdu);
                let ea = d_form_effective_address(
                    &mut output,
                    pc,
                    word,
                    base,
                    update,
                    immediate_override,
                )?;
                if matches!(instruction.op, Opcode::Stfs | Opcode::Stfsu) {
                    writeln!(
                        output,
                        "    galaxy::store_fpr_single(context, {source}u, memory, {ea}, services, 0x{pc:08X}u);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                } else {
                    writeln!(
                        output,
                        "    galaxy::guest_store_u64(memory, {ea}, context->fpr_bits[{source}], services, 0x{pc:08X}u);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                }
                if update {
                    writeln!(output, "    context->gpr[{base}] = {ea};")
                        .map_err(|_| TranslationError::Formatting)?;
                }
            }
            Opcode::Lfsx
            | Opcode::Lfsux
            | Opcode::Lfdx
            | Opcode::Lfdux
            | Opcode::Stfsx
            | Opcode::Stfsux
            | Opcode::Stfdx
            | Opcode::Stfdux => {
                let register = gpr_rt(word);
                let base = gpr_ra(word);
                let index = gpr_rb(word);
                let update = matches!(
                    instruction.op,
                    Opcode::Lfsux | Opcode::Lfdux | Opcode::Stfsux | Opcode::Stfdux
                );
                let ea = if update {
                    emit_indexed_effective_address(&mut output, pc, base, index)?
                } else {
                    indexed_effective_address_expression(base, index)
                };
                match instruction.op {
                    Opcode::Lfsx | Opcode::Lfsux => {
                        writeln!(
                            output,
                            "    galaxy::load_fpr_single(context, {register}u, memory, {ea}, services, 0x{pc:08X}u);"
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                    }
                    Opcode::Lfdx | Opcode::Lfdux => {
                        writeln!(
                            output,
                            "    context->fpr_bits[{register}] = galaxy::guest_load_u64(memory, {ea}, services, 0x{pc:08X}u);"
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                    }
                    Opcode::Stfsx | Opcode::Stfsux => {
                        writeln!(
                            output,
                            "    galaxy::store_fpr_single(context, {register}u, memory, {ea}, services, 0x{pc:08X}u);"
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                    }
                    Opcode::Stfdx | Opcode::Stfdux => {
                        writeln!(
                            output,
                            "    galaxy::guest_store_u64(memory, {ea}, context->fpr_bits[{register}], services, 0x{pc:08X}u);"
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                    }
                    _ => unreachable!(),
                }
                if update {
                    writeln!(output, "    context->gpr[{base}] = {ea};")
                        .map_err(|_| TranslationError::Formatting)?;
                }
            }
            Opcode::Dcbf
            | Opcode::Dcbi
            | Opcode::Dcbst
            | Opcode::Icbi
            | Opcode::Isync
            | Opcode::Sync => {
                output.push_str("    galaxy::full_memory_fence();\n");
            }
            Opcode::Dcbt => {
                let base = gpr_ra(word);
                let index = gpr_rb(word);
                let ea = emit_indexed_effective_address(&mut output, pc, base, index)?;
                writeln!(output, "    galaxy::cache_prefetch_hint(memory, {ea});")
                    .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Dcbz => {
                let base = gpr_ra(word);
                let index = gpr_rb(word);
                let ea = emit_indexed_effective_address(&mut output, pc, base, index)?;
                writeln!(
                    output,
                    "    galaxy::guest_zero(memory, {ea} & 0xFFFFFFE0u, 32u, services, 0x{pc:08X}u);"
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::DcbzL => {
                let base = gpr_ra(word);
                let index = gpr_rb(word);
                let ea = emit_indexed_effective_address(&mut output, pc, base, index)?;
                writeln!(
                    output,
                    "    galaxy::guest_zero(memory, {ea} & 0xFFFFFFE0u, 32u, services, 0x{pc:08X}u);"
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Fmr | Opcode::Fneg | Opcode::Fabs => {
                let target = gpr_rt(word);
                let source = gpr_rb(word);
                let expression = match instruction.op {
                    Opcode::Fmr => format!("context->fpr_bits[{source}]"),
                    Opcode::Fneg => format!("context->fpr_bits[{source}] ^ 0x8000000000000000ull"),
                    Opcode::Fabs => format!("context->fpr_bits[{source}] & 0x7FFFFFFFFFFFFFFFull"),
                    _ => unreachable!(),
                };
                writeln!(output, "    context->fpr_bits[{target}] = {expression};")
                    .map_err(|_| TranslationError::Formatting)?;
                if record_bit(word) {
                    output.push_str("    galaxy::update_cr1_from_fpscr(context);\n");
                }
            }
            Opcode::Frsp => {
                let target = gpr_rt(word);
                let source = gpr_rb(word);
                writeln!(
                    output,
                    "    const galaxy::PpcFloatResult result = galaxy::ppc_round_f64_to_f32(context->fpr_bits[{source}], context->fpscr);\n    galaxy::ppc_commit_scalar_result(context, {target}u, result, false, {}, services, 0x{pc:08X}u);",
                    record_bit(word)
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Fctiwz => {
                let target = gpr_rt(word);
                let source = gpr_rb(word);
                writeln!(
                    output,
                    "    const galaxy::PpcFloatResult result = galaxy::ppc_f64_to_i32_round_zero(context->fpr_bits[{source}], context->fpscr);\n    galaxy::ppc_commit_scalar_result(context, {target}u, result, false, {}, services, 0x{pc:08X}u);",
                    record_bit(word)
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Fadd
            | Opcode::Fadds
            | Opcode::Fsub
            | Opcode::Fsubs
            | Opcode::Fmul
            | Opcode::Fmuls
            | Opcode::Fdiv
            | Opcode::Fdivs => {
                let target = gpr_rt(word);
                let left = gpr_ra(word);
                let right = if matches!(instruction.op, Opcode::Fmul | Opcode::Fmuls) {
                    fpr_c(word)
                } else {
                    gpr_rb(word)
                };
                let operation = match instruction.op {
                    Opcode::Fadd | Opcode::Fadds => "Add",
                    Opcode::Fsub | Opcode::Fsubs => "Subtract",
                    Opcode::Fmul | Opcode::Fmuls => "Multiply",
                    Opcode::Fdiv | Opcode::Fdivs => "Divide",
                    _ => unreachable!(),
                };
                let single = matches!(
                    instruction.op,
                    Opcode::Fadds | Opcode::Fsubs | Opcode::Fmuls | Opcode::Fdivs
                );
                if single {
                    if inline_scalar_single_binary
                        && matches!(
                            instruction.op,
                            Opcode::Fadds | Opcode::Fsubs | Opcode::Fmuls
                        )
                    {
                        writeln!(
                            output,
                            "    const std::uint64_t left = context->fpr_bits[{left}];\n    const std::uint64_t right = context->fpr_bits[{right}];\n    if (!galaxy::try_commit_widened_scalar_binary<galaxy::PpcFloatBinaryOperation::{operation}>(context, {target}u, left, right, {}, services, 0x{pc:08X}u)) {{\n        const galaxy::PpcFloatResult result = galaxy::ppc_f64_binary_to_f32(galaxy::PpcFloatBinaryOperation::{operation}, context->fpr_bits[{left}], context->fpr_bits[{right}], context->fpscr);\n        galaxy::ppc_commit_scalar_result(context, {target}u, result, true, {}, services, 0x{pc:08X}u);\n    }}",
                            record_bit(word),
                            record_bit(word)
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                    } else {
                        writeln!(
                            output,
                            "    const galaxy::PpcFloatResult result = galaxy::ppc_f64_binary_to_f32(galaxy::PpcFloatBinaryOperation::{operation}, context->fpr_bits[{left}], context->fpr_bits[{right}], context->fpscr);\n    galaxy::ppc_commit_scalar_result(context, {target}u, result, true, {}, services, 0x{pc:08X}u);",
                            record_bit(word)
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                    }
                } else {
                    writeln!(
                        output,
                        "    const galaxy::PpcFloatResult result = galaxy::ppc_f64_binary(galaxy::PpcFloatBinaryOperation::{operation}, context->fpr_bits[{left}], context->fpr_bits[{right}], context->fpscr);\n    galaxy::ppc_commit_scalar_result(context, {target}u, result, false, {}, services, 0x{pc:08X}u);",
                        record_bit(word)
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                }
            }
            Opcode::Fmadd
            | Opcode::Fmadds
            | Opcode::Fmsub
            | Opcode::Fmsubs
            | Opcode::Fnmadd
            | Opcode::Fnmadds
            | Opcode::Fnmsub
            | Opcode::Fnmsubs => {
                let target = gpr_rt(word);
                let multiplicand = gpr_ra(word);
                let addend = gpr_rb(word);
                let multiplier = fpr_c(word);
                let operation = match instruction.op {
                    Opcode::Fmadd | Opcode::Fmadds => "MultiplyAdd",
                    Opcode::Fmsub | Opcode::Fmsubs => "MultiplySubtract",
                    Opcode::Fnmadd | Opcode::Fnmadds => "NegativeMultiplyAdd",
                    Opcode::Fnmsub | Opcode::Fnmsubs => "NegativeMultiplySubtract",
                    _ => unreachable!(),
                };
                let single = matches!(
                    instruction.op,
                    Opcode::Fmadds | Opcode::Fmsubs | Opcode::Fnmadds | Opcode::Fnmsubs
                );
                if single {
                    writeln!(
                        output,
                        "    const std::uint32_t multiplicand = galaxy::require_single_precision_bits(context->fpr_bits[{multiplicand}], services, 0x{pc:08X}u);\n    const std::uint32_t multiplier = galaxy::require_single_precision_bits(context->fpr_bits[{multiplier}], services, 0x{pc:08X}u);\n    const std::uint32_t addend = galaxy::require_single_precision_bits(context->fpr_bits[{addend}], services, 0x{pc:08X}u);\n    const galaxy::PpcFloatResult result = galaxy::ppc_f32_ternary(galaxy::PpcFloatTernaryOperation::{operation}, multiplicand, multiplier, addend, context->fpscr);\n    galaxy::ppc_commit_scalar_result(context, {target}u, result, true, {}, services, 0x{pc:08X}u);",
                        record_bit(word)
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                } else {
                    writeln!(
                        output,
                        "    const galaxy::PpcFloatResult result = galaxy::ppc_f64_ternary(galaxy::PpcFloatTernaryOperation::{operation}, context->fpr_bits[{multiplicand}], context->fpr_bits[{multiplier}], context->fpr_bits[{addend}], context->fpscr);\n    galaxy::ppc_commit_scalar_result(context, {target}u, result, false, {}, services, 0x{pc:08X}u);",
                        record_bit(word)
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                }
            }
            Opcode::Fsel => {
                let target = gpr_rt(word);
                let selector = gpr_ra(word);
                let negative = gpr_rb(word);
                let nonnegative = fpr_c(word);
                writeln!(
                    output,
                    "    const std::uint64_t selector = context->fpr_bits[{selector}];\n    const bool select_nonnegative = !galaxy::f64_is_nan(selector) && ((selector & 0x8000000000000000ull) == 0 || (selector & 0x7FFFFFFFFFFFFFFFull) == 0);\n    context->fpr_bits[{target}] = select_nonnegative ? context->fpr_bits[{nonnegative}] : context->fpr_bits[{negative}];"
                )
                .map_err(|_| TranslationError::Formatting)?;
                if record_bit(word) {
                    output.push_str("    galaxy::update_cr1_from_fpscr(context);\n");
                }
            }
            Opcode::Fres => {
                let target = gpr_rt(word);
                let source = gpr_rb(word);
                writeln!(
                    output,
                    "    const std::uint64_t source = context->fpr_bits[{source}];\n    const galaxy::PpcFloatResult result = galaxy::ppc_f64_reciprocal_estimate(source, context->fpscr);\n    galaxy::ppc_commit_scalar_result(context, {target}u, result, true, {}, services, 0x{pc:08X}u);",
                    record_bit(word)
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Frsqrte => {
                let target = gpr_rt(word);
                let source = gpr_rb(word);
                writeln!(
                    output,
                    "    const galaxy::PpcFloatResult result = galaxy::ppc_f64_reciprocal_sqrt_estimate(context->fpr_bits[{source}], context->fpscr);\n    galaxy::ppc_commit_scalar_result(context, {target}u, result, false, {}, services, 0x{pc:08X}u);",
                    record_bit(word)
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::PsMr | Opcode::PsNeg | Opcode::PsAbs | Opcode::PsNabs => {
                let target = gpr_rt(word);
                let source = gpr_rb(word);
                writeln!(
                    output,
                    "    galaxy::require_paired_single_mode(context, false, services, 0x{pc:08X}u);"
                )
                .map_err(|_| TranslationError::Formatting)?;
                let lane0 = match instruction.op {
                    Opcode::PsMr => format!("context->fpr_bits[{source}]"),
                    Opcode::PsNeg => {
                        format!("context->fpr_bits[{source}] ^ 0x8000000000000000ull")
                    }
                    Opcode::PsAbs => {
                        format!("context->fpr_bits[{source}] & 0x7FFFFFFFFFFFFFFFull")
                    }
                    Opcode::PsNabs => {
                        format!("context->fpr_bits[{source}] | 0x8000000000000000ull")
                    }
                    _ => unreachable!(),
                };
                let lane1 = match instruction.op {
                    Opcode::PsMr => format!("context->ps1_bits[{source}]"),
                    Opcode::PsNeg => {
                        format!("context->ps1_bits[{source}] ^ 0x8000000000000000ull")
                    }
                    Opcode::PsAbs => {
                        format!("context->ps1_bits[{source}] & 0x7FFFFFFFFFFFFFFFull")
                    }
                    Opcode::PsNabs => {
                        format!("context->ps1_bits[{source}] | 0x8000000000000000ull")
                    }
                    _ => unreachable!(),
                };
                writeln!(
                    output,
                    "    const std::uint64_t result_ps0 = {lane0};\n    const std::uint64_t result_ps1 = {lane1};\n    context->fpr_bits[{target}] = result_ps0;\n    context->ps1_bits[{target}] = result_ps1;"
                )
                .map_err(|_| TranslationError::Formatting)?;
                if record_bit(word) {
                    output.push_str("    galaxy::update_cr1_from_fpscr(context);\n");
                }
            }
            Opcode::PsMerge00 | Opcode::PsMerge01 | Opcode::PsMerge10 | Opcode::PsMerge11 => {
                let target = gpr_rt(word);
                let left = gpr_ra(word);
                let right = gpr_rb(word);
                writeln!(
                    output,
                    "    galaxy::require_paired_single_mode(context, false, services, 0x{pc:08X}u);"
                )
                .map_err(|_| TranslationError::Formatting)?;
                let left_lane = if matches!(instruction.op, Opcode::PsMerge10 | Opcode::PsMerge11) {
                    "ps1_bits"
                } else {
                    "fpr_bits"
                };
                let right_lane = if matches!(instruction.op, Opcode::PsMerge01 | Opcode::PsMerge11)
                {
                    "ps1_bits"
                } else {
                    "fpr_bits"
                };
                writeln!(
                    output,
                    "    const std::uint64_t result_ps0 = context->{left_lane}[{left}];\n    const std::uint64_t result_ps1 = context->{right_lane}[{right}];\n    context->fpr_bits[{target}] = result_ps0;\n    context->ps1_bits[{target}] = result_ps1;"
                )
                .map_err(|_| TranslationError::Formatting)?;
                if record_bit(word) {
                    output.push_str("    galaxy::update_cr1_from_fpscr(context);\n");
                }
            }
            Opcode::PsAdd
            | Opcode::PsSub
            | Opcode::PsMul
            | Opcode::PsMuls0
            | Opcode::PsMuls1
            | Opcode::PsDiv => {
                let target = gpr_rt(word);
                let left = gpr_ra(word);
                let right = if matches!(
                    instruction.op,
                    Opcode::PsMul | Opcode::PsMuls0 | Opcode::PsMuls1
                ) {
                    fpr_c(word)
                } else {
                    gpr_rb(word)
                };
                let operation = match instruction.op {
                    Opcode::PsAdd => "Add",
                    Opcode::PsSub => "Subtract",
                    Opcode::PsMul | Opcode::PsMuls0 | Opcode::PsMuls1 => "Multiply",
                    Opcode::PsDiv => "Divide",
                    _ => unreachable!(),
                };
                let right_lane0 = if instruction.op == Opcode::PsMuls1 {
                    "ps1_bits"
                } else {
                    "fpr_bits"
                };
                let right_lane1 = if instruction.op == Opcode::PsMuls0 {
                    "fpr_bits"
                } else {
                    "ps1_bits"
                };
                let arithmetic = if fused_paired_binary {
                    format!(
                        "    galaxy::ppc_commit_paired_binary_result(context, {target}u, galaxy::PpcFloatBinaryOperation::{operation}, left_ps0, left_ps1, right_ps0, right_ps1, {}, services, 0x{pc:08X}u);",
                        record_bit(word)
                    )
                } else {
                    format!(
                        "    const galaxy::PpcFloatResult result_ps0 = galaxy::ppc_f32_binary(galaxy::PpcFloatBinaryOperation::{operation}, left_ps0, right_ps0, context->fpscr);\n    const galaxy::PpcFloatResult result_ps1 = galaxy::ppc_f32_binary(galaxy::PpcFloatBinaryOperation::{operation}, left_ps1, right_ps1, context->fpscr);\n    galaxy::ppc_commit_paired_result(context, {target}u, result_ps0, result_ps1, false, {}, services, 0x{pc:08X}u);",
                        record_bit(word)
                    )
                };
                writeln!(
                    output,
                    "    galaxy::require_paired_single_mode(context, false, services, 0x{pc:08X}u);\n    const std::uint32_t left_ps0 = galaxy::require_single_precision_bits(context->fpr_bits[{left}], services, 0x{pc:08X}u);\n    const std::uint32_t left_ps1 = galaxy::require_single_precision_bits(context->ps1_bits[{left}], services, 0x{pc:08X}u);\n    const std::uint32_t right_ps0 = galaxy::require_single_precision_bits(context->{right_lane0}[{right}], services, 0x{pc:08X}u);\n    const std::uint32_t right_ps1 = galaxy::require_single_precision_bits(context->{right_lane1}[{right}], services, 0x{pc:08X}u);\n{arithmetic}"
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::PsMadd
            | Opcode::PsMsub
            | Opcode::PsNmadd
            | Opcode::PsNmsub
            | Opcode::PsMadds0
            | Opcode::PsMadds1 => {
                let target = gpr_rt(word);
                let multiplicand = gpr_ra(word);
                let addend = gpr_rb(word);
                let multiplier = fpr_c(word);
                let operation = match instruction.op {
                    Opcode::PsMadd | Opcode::PsMadds0 | Opcode::PsMadds1 => "MultiplyAdd",
                    Opcode::PsMsub => "MultiplySubtract",
                    Opcode::PsNmadd => "NegativeMultiplyAdd",
                    Opcode::PsNmsub => "NegativeMultiplySubtract",
                    _ => unreachable!(),
                };
                let multiplier_lane0 = if instruction.op == Opcode::PsMadds1 {
                    "ps1_bits"
                } else {
                    "fpr_bits"
                };
                let multiplier_lane1 = if instruction.op == Opcode::PsMadds0 {
                    "fpr_bits"
                } else {
                    "ps1_bits"
                };
                writeln!(
                    output,
                    "    galaxy::require_paired_single_mode(context, false, services, 0x{pc:08X}u);\n    const std::uint32_t multiplicand_ps0 = galaxy::require_single_precision_bits(context->fpr_bits[{multiplicand}], services, 0x{pc:08X}u);\n    const std::uint32_t multiplicand_ps1 = galaxy::require_single_precision_bits(context->ps1_bits[{multiplicand}], services, 0x{pc:08X}u);\n    const std::uint32_t multiplier_ps0 = galaxy::require_single_precision_bits(context->{multiplier_lane0}[{multiplier}], services, 0x{pc:08X}u);\n    const std::uint32_t multiplier_ps1 = galaxy::require_single_precision_bits(context->{multiplier_lane1}[{multiplier}], services, 0x{pc:08X}u);\n    const std::uint32_t addend_ps0 = galaxy::require_single_precision_bits(context->fpr_bits[{addend}], services, 0x{pc:08X}u);\n    const std::uint32_t addend_ps1 = galaxy::require_single_precision_bits(context->ps1_bits[{addend}], services, 0x{pc:08X}u);\n    const galaxy::PpcFloatResult result_ps0 = galaxy::ppc_f32_ternary(galaxy::PpcFloatTernaryOperation::{operation}, multiplicand_ps0, multiplier_ps0, addend_ps0, context->fpscr);\n    const galaxy::PpcFloatResult result_ps1 = galaxy::ppc_f32_ternary(galaxy::PpcFloatTernaryOperation::{operation}, multiplicand_ps1, multiplier_ps1, addend_ps1, context->fpscr);\n    galaxy::ppc_commit_paired_result(context, {target}u, result_ps0, result_ps1, false, {}, services, 0x{pc:08X}u);",
                    record_bit(word)
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::PsSum0 | Opcode::PsSum1 => {
                let target = gpr_rt(word);
                let left = gpr_ra(word);
                let right = gpr_rb(word);
                let copy = fpr_c(word);
                let status_from_lane1 = instruction.op == Opcode::PsSum1;
                let lane0 = if status_from_lane1 {
                    format!(
                        "galaxy::ppc_f32_passthrough(galaxy::require_single_precision_bits(context->fpr_bits[{copy}], services, 0x{pc:08X}u))"
                    )
                } else {
                    "galaxy::ppc_f32_binary(galaxy::PpcFloatBinaryOperation::Add, left_ps0, right_ps1, context->fpscr)".to_owned()
                };
                let lane1 = if status_from_lane1 {
                    "galaxy::ppc_f32_binary(galaxy::PpcFloatBinaryOperation::Add, left_ps0, right_ps1, context->fpscr)".to_owned()
                } else {
                    format!(
                        "galaxy::ppc_f32_passthrough(galaxy::require_single_precision_bits(context->ps1_bits[{copy}], services, 0x{pc:08X}u))"
                    )
                };
                writeln!(
                    output,
                    "    galaxy::require_paired_single_mode(context, false, services, 0x{pc:08X}u);\n    const std::uint32_t left_ps0 = galaxy::require_single_precision_bits(context->fpr_bits[{left}], services, 0x{pc:08X}u);\n    const std::uint32_t right_ps1 = galaxy::require_single_precision_bits(context->ps1_bits[{right}], services, 0x{pc:08X}u);\n    const galaxy::PpcFloatResult result_ps0 = {lane0};\n    const galaxy::PpcFloatResult result_ps1 = {lane1};\n    galaxy::ppc_commit_paired_result(context, {target}u, result_ps0, result_ps1, {status_from_lane1}, {}, services, 0x{pc:08X}u);",
                    record_bit(word)
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::PsCmpo0 | Opcode::PsCmpo1 | Opcode::PsCmpu0 | Opcode::PsCmpu1 => {
                let field = (word >> 23) & 0x7;
                let left = gpr_ra(word);
                let right = gpr_rb(word);
                let lane = if matches!(instruction.op, Opcode::PsCmpo1 | Opcode::PsCmpu1) {
                    "ps1_bits"
                } else {
                    "fpr_bits"
                };
                let ordered = matches!(instruction.op, Opcode::PsCmpo0 | Opcode::PsCmpo1);
                writeln!(
                    output,
                    "    galaxy::require_paired_single_mode(context, false, services, 0x{pc:08X}u);\n    galaxy::compare_f64(context, {field}u, context->{lane}[{left}], context->{lane}[{right}], {ordered});"
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::PsRes | Opcode::PsRsqrte => {
                let target = gpr_rt(word);
                let source = gpr_rb(word);
                let helper = if instruction.op == Opcode::PsRes {
                    "ppc_f32_reciprocal_estimate"
                } else {
                    "ppc_f32_reciprocal_sqrt_estimate"
                };
                writeln!(
                    output,
                    "    galaxy::require_paired_single_mode(context, false, services, 0x{pc:08X}u);\n    const std::uint32_t source_ps0 = galaxy::require_single_precision_bits(context->fpr_bits[{source}], services, 0x{pc:08X}u);\n    const std::uint32_t source_ps1 = galaxy::require_single_precision_bits(context->ps1_bits[{source}], services, 0x{pc:08X}u);\n    const galaxy::PpcFloatResult result_ps0 = galaxy::{helper}(source_ps0, context->fpscr);\n    const galaxy::PpcFloatResult result_ps1 = galaxy::{helper}(source_ps1, context->fpscr);\n    galaxy::ppc_commit_paired_result(context, {target}u, result_ps0, result_ps1, false, {}, services, 0x{pc:08X}u);",
                    record_bit(word)
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::PsSel => {
                let target = gpr_rt(word);
                let selector = gpr_ra(word);
                let negative = gpr_rb(word);
                let nonnegative = fpr_c(word);
                writeln!(
                    output,
                    "    galaxy::require_paired_single_mode(context, false, services, 0x{pc:08X}u);\n    const std::uint64_t selector_ps0 = context->fpr_bits[{selector}];\n    const std::uint64_t selector_ps1 = context->ps1_bits[{selector}];\n    const bool select_nonnegative_ps0 = !galaxy::f64_is_nan(selector_ps0) && ((selector_ps0 & 0x8000000000000000ull) == 0 || (selector_ps0 & 0x7FFFFFFFFFFFFFFFull) == 0);\n    const bool select_nonnegative_ps1 = !galaxy::f64_is_nan(selector_ps1) && ((selector_ps1 & 0x8000000000000000ull) == 0 || (selector_ps1 & 0x7FFFFFFFFFFFFFFFull) == 0);\n    context->fpr_bits[{target}] = select_nonnegative_ps0 ? context->fpr_bits[{nonnegative}] : context->fpr_bits[{negative}];\n    context->ps1_bits[{target}] = select_nonnegative_ps1 ? context->ps1_bits[{nonnegative}] : context->ps1_bits[{negative}];"
                )
                .map_err(|_| TranslationError::Formatting)?;
                if record_bit(word) {
                    output.push_str("    galaxy::update_cr1_from_fpscr(context);\n");
                }
            }
            Opcode::Mffs => {
                let target = gpr_rt(word);
                writeln!(
                    output,
                    "    context->fpr_bits[{target}] = 0xFFF8000000000000ull | context->fpscr;"
                )
                .map_err(|_| TranslationError::Formatting)?;
                if record_bit(word) {
                    output.push_str("    galaxy::update_cr1_from_fpscr(context);\n");
                }
            }
            Opcode::Mtfsb0 | Opcode::Mtfsb1 => {
                let bit = gpr_rt(word);
                let helper = if instruction.op == Opcode::Mtfsb0 {
                    "clear_fpscr_bit"
                } else {
                    "set_fpscr_bit"
                };
                writeln!(output, "    galaxy::{helper}(context, {bit}u);")
                    .map_err(|_| TranslationError::Formatting)?;
                if record_bit(word) {
                    output.push_str("    galaxy::update_cr1_from_fpscr(context);\n");
                }
            }
            Opcode::Mtfsf => {
                let source = gpr_rb(word);
                let field_mask = (word >> 17) & 0xFF;
                writeln!(
                    output,
                    "    galaxy::write_fpscr_fields(context, static_cast<std::uint32_t>(context->fpr_bits[{source}]), 0x{field_mask:02X}u);"
                )
                .map_err(|_| TranslationError::Formatting)?;
                if record_bit(word) {
                    output.push_str("    galaxy::update_cr1_from_fpscr(context);\n");
                }
            }
            Opcode::Mtfsfi => {
                let field = (word >> 23) & 0x7;
                let immediate = (word >> 12) & 0xF;
                writeln!(
                    output,
                    "    galaxy::write_fpscr_field_immediate(context, {field}u, 0x{immediate:X}u);"
                )
                .map_err(|_| TranslationError::Formatting)?;
                if record_bit(word) {
                    output.push_str("    galaxy::update_cr1_from_fpscr(context);\n");
                }
            }
            Opcode::Mfcr => {
                let target = gpr_rt(word);
                writeln!(output, "    context->gpr[{target}] = context->cr;")
                    .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Mtcrf => {
                let source = gpr_rt(word);
                let mask = (word >> 12) & 0xFF;
                writeln!(
                    output,
                    "    galaxy::write_cr_fields(context, context->gpr[{source}], 0x{mask:02X}u);"
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Mfmsr => {
                let target = gpr_rt(word);
                writeln!(output, "    context->gpr[{target}] = context->msr;")
                    .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Mtmsr => {
                let source = gpr_rt(word);
                let resume_pc = pc.wrapping_add(4);
                writeln!(
                    output,
                    "    context->msr = context->gpr[{source}];\n    std::atomic_thread_fence(std::memory_order_seq_cst);\n    galaxy::architectural_interrupt_checkpoint(services, 0x{pc:08X}u, 0x{resume_pc:08X}u, context, memory);"
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Mfsr => {
                let target = gpr_rt(word);
                let segment = (word >> 16) & 0xF;
                writeln!(
                    output,
                    "    context->gpr[{target}] = context->segment_registers[{segment}];"
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Mtsr => {
                let source = gpr_rt(word);
                let segment = (word >> 16) & 0xF;
                writeln!(
                    output,
                    "    context->segment_registers[{segment}] = context->gpr[{source}];"
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Mftb => {
                let target = gpr_rt(word);
                let time_base_register = spr_number(word);
                writeln!(
                    output,
                    "    context->gpr[{target}] = galaxy::read_spr(context, {time_base_register}u, services, 0x{pc:08X}u);"
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Mfspr => {
                let target = gpr_rt(word);
                let spr = spr_number(word);
                if let Some(expression) = direct_spr_read_expression(spr) {
                    writeln!(output, "    context->gpr[{target}] = {expression};")
                        .map_err(|_| TranslationError::Formatting)?;
                } else {
                    writeln!(
                        output,
                        "    context->gpr[{target}] = galaxy::read_spr(context, {spr}u, services, 0x{pc:08X}u);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                }
            }
            Opcode::Mtspr => {
                let source = gpr_rt(word);
                let spr = spr_number(word);
                if spr == 22 {
                    let resume_pc = pc.wrapping_add(4);
                    writeln!(
                        output,
                        "    galaxy::write_decrementer_and_notify(context, context->gpr[{source}], memory, services, 0x{pc:08X}u, 0x{resume_pc:08X}u);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                } else if spr == 922 || spr == 923 {
                    writeln!(
                        output,
                        "    galaxy::write_spr(context, {spr}u, context->gpr[{source}], memory, services, 0x{pc:08X}u);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                } else if let Some(expression) = direct_spr_write_expression(spr) {
                    writeln!(output, "    {expression} = context->gpr[{source}];")
                        .map_err(|_| TranslationError::Formatting)?;
                } else {
                    writeln!(
                        output,
                        "    galaxy::write_spr(context, {spr}u, context->gpr[{source}], services, 0x{pc:08X}u);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                }
            }
            Opcode::Add
            | Opcode::Addc
            | Opcode::Subf
            | Opcode::Subfc
            | Opcode::Mullw
            | Opcode::Neg => {
                if word & 0x400 != 0 {
                    return Err(TranslationError::UnsupportedInstruction {
                        address: pc,
                        opcode: format!("{:?}(overflow-enable)", instruction.op),
                    });
                }
                let target = gpr_rt(word);
                let left = gpr_ra(word);
                let right = gpr_rb(word);
                match instruction.op {
                    Opcode::Add => {
                        writeln!(
                            output,
                            "    context->gpr[{target}] = {} + {};",
                            gpr_u32_value_expression(left),
                            gpr_u32_value_expression(right)
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                    }
                    Opcode::Addc => {
                        writeln!(
                            output,
                            "    const std::uint64_t result = static_cast<std::uint64_t>({}) + {};",
                            gpr_u32_value_expression(left),
                            gpr_u32_value_expression(right)
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                        writeln!(
                            output,
                            "    context->gpr[{target}] = static_cast<std::uint32_t>(result);"
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                        output
                            .push_str("    galaxy::set_xer_ca(context, result > 0xFFFFFFFFull);\n");
                    }
                    Opcode::Subf => {
                        writeln!(
                            output,
                            "    context->gpr[{target}] = {} - {};",
                            gpr_u32_value_expression(right),
                            gpr_u32_value_expression(left)
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                    }
                    Opcode::Subfc => {
                        writeln!(
                            output,
                            "    const std::uint32_t left_value = {};",
                            gpr_u32_value_expression(left)
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                        writeln!(
                            output,
                            "    const std::uint32_t right_value = {};",
                            gpr_u32_value_expression(right)
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                        writeln!(
                            output,
                            "    context->gpr[{target}] = right_value - left_value;"
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                        output.push_str(
                            "    galaxy::set_xer_ca(context, right_value >= left_value);\n",
                        );
                    }
                    Opcode::Mullw => {
                        writeln!(
                            output,
                            "    const std::int64_t result = static_cast<std::int64_t>({}) * {};",
                            gpr_s32_value_expression(left),
                            gpr_s32_value_expression(right)
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                        writeln!(
                            output,
                            "    context->gpr[{target}] = static_cast<std::uint32_t>(result);"
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                    }
                    Opcode::Neg => {
                        writeln!(
                            output,
                            "    context->gpr[{target}] = 0u - {};",
                            gpr_u32_value_expression(left)
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                    }
                    _ => unreachable!(),
                }
                if record_bit(word) {
                    writeln!(
                        output,
                        "    galaxy::update_cr0(context, context->gpr[{target}]);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                }
            }
            Opcode::Adde | Opcode::Addze | Opcode::Subfe | Opcode::Subfze => {
                if word & 0x400 != 0 {
                    return Err(TranslationError::UnsupportedInstruction {
                        address: pc,
                        opcode: format!("{:?}(overflow-enable)", instruction.op),
                    });
                }
                let target = gpr_rt(word);
                let left = gpr_ra(word);
                let right = gpr_rb(word);
                output.push_str(
                    "    const std::uint64_t carry_in = galaxy::xer_ca(context) ? 1ull : 0ull;\n",
                );
                let expression = match instruction.op {
                    Opcode::Adde => format!(
                        "static_cast<std::uint64_t>(context->gpr[{left}]) + context->gpr[{right}] + carry_in"
                    ),
                    Opcode::Addze => {
                        format!("static_cast<std::uint64_t>(context->gpr[{left}]) + carry_in")
                    }
                    Opcode::Subfe => format!(
                        "static_cast<std::uint64_t>(context->gpr[{left}] ^ 0xFFFFFFFFu) + context->gpr[{right}] + carry_in"
                    ),
                    Opcode::Subfze => {
                        format!(
                            "static_cast<std::uint64_t>(context->gpr[{left}] ^ 0xFFFFFFFFu) + carry_in"
                        )
                    }
                    _ => unreachable!(),
                };
                writeln!(output, "    const std::uint64_t result = {expression};")
                    .map_err(|_| TranslationError::Formatting)?;
                writeln!(
                    output,
                    "    context->gpr[{target}] = static_cast<std::uint32_t>(result);"
                )
                .map_err(|_| TranslationError::Formatting)?;
                output.push_str("    galaxy::set_xer_ca(context, result > 0xFFFFFFFFull);\n");
                if record_bit(word) {
                    writeln!(
                        output,
                        "    galaxy::update_cr0(context, context->gpr[{target}]);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                }
            }
            Opcode::Mulhw | Opcode::Mulhwu | Opcode::Divw | Opcode::Divwu => {
                if matches!(instruction.op, Opcode::Divw | Opcode::Divwu) && word & 0x400 != 0 {
                    return Err(TranslationError::UnsupportedInstruction {
                        address: pc,
                        opcode: format!("{:?}(overflow-enable)", instruction.op),
                    });
                }
                let target = gpr_rt(word);
                let left = gpr_ra(word);
                let right = gpr_rb(word);
                match instruction.op {
                    Opcode::Mulhw => {
                        writeln!(
                            output,
                            "    const std::int64_t product = static_cast<std::int64_t>(static_cast<std::int32_t>(context->gpr[{left}])) * static_cast<std::int32_t>(context->gpr[{right}]);"
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                        writeln!(
                            output,
                            "    context->gpr[{target}] = static_cast<std::uint32_t>(static_cast<std::uint64_t>(product) >> 32);"
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                    }
                    Opcode::Mulhwu => {
                        writeln!(
                            output,
                            "    const std::uint64_t product = static_cast<std::uint64_t>(context->gpr[{left}]) * context->gpr[{right}];"
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                        writeln!(
                            output,
                            "    context->gpr[{target}] = static_cast<std::uint32_t>(product >> 32);"
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                    }
                    Opcode::Divw => {
                        writeln!(
                            output,
                            "    context->gpr[{target}] = galaxy::divide_signed_word(context->gpr[{left}], context->gpr[{right}], services, 0x{pc:08X}u);"
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                    }
                    Opcode::Divwu => {
                        writeln!(
                            output,
                            "    context->gpr[{target}] = galaxy::divide_unsigned_word(context->gpr[{left}], context->gpr[{right}], services, 0x{pc:08X}u);"
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                    }
                    _ => unreachable!(),
                }
                if record_bit(word) {
                    writeln!(
                        output,
                        "    galaxy::update_cr0(context, context->gpr[{target}]);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                }
            }
            Opcode::Addic | Opcode::Addic_ | Opcode::Subfic | Opcode::Mulli => {
                let target = gpr_rt(word);
                let source = gpr_ra(word);
                let immediate = signed_immediate(word) as u32;
                match instruction.op {
                    Opcode::Addic | Opcode::Addic_ => {
                        writeln!(
                            output,
                            "    const std::uint64_t result = static_cast<std::uint64_t>({}) + 0x{immediate:08X}u;",
                            gpr_u32_value_expression(source)
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                        writeln!(
                            output,
                            "    context->gpr[{target}] = static_cast<std::uint32_t>(result);"
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                        output
                            .push_str("    galaxy::set_xer_ca(context, result > 0xFFFFFFFFull);\n");
                        if instruction.op == Opcode::Addic_ {
                            writeln!(
                                output,
                                "    galaxy::update_cr0(context, context->gpr[{target}]);"
                            )
                            .map_err(|_| TranslationError::Formatting)?;
                        }
                    }
                    Opcode::Subfic => {
                        writeln!(
                            output,
                            "    const std::uint32_t source_value = {};",
                            gpr_u32_value_expression(source)
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                        writeln!(
                            output,
                            "    context->gpr[{target}] = 0x{immediate:08X}u - source_value;"
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                        writeln!(
                            output,
                            "    galaxy::set_xer_ca(context, 0x{immediate:08X}u >= source_value);"
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                    }
                    Opcode::Mulli => {
                        writeln!(
                            output,
                            "    const std::int64_t result = static_cast<std::int64_t>({}) * static_cast<std::int32_t>(0x{immediate:08X}u);",
                            gpr_s32_value_expression(source)
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                        writeln!(
                            output,
                            "    context->gpr[{target}] = static_cast<std::uint32_t>(result);"
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                    }
                    _ => unreachable!(),
                }
            }
            Opcode::Or | Opcode::Orc | Opcode::And | Opcode::Andc | Opcode::Xor | Opcode::Nor => {
                let access = guest_integer_gpr_access(instruction.op, word);
                let source = access.map_or_else(
                    || gpr_rt(word),
                    |access| access.first_source.expect("or source"),
                );
                let target = access.map_or_else(|| gpr_ra(word), |access| access.destination);
                let rhs = access.map_or_else(
                    || gpr_rb(word),
                    |access| access.second_source.expect("or second source"),
                );
                let expression = match instruction.op {
                    // `or rA,rS,rS` IS the PowerPC register-to-register move.
                    // Emitting `gpr[a] | gpr[a]` costs a redundant read of the
                    // same 4-byte context slot plus a real OR on every `mr` in
                    // the stream (253 occurrences in one 15k-line shard of the
                    // installed module). `x | x` is exactly `x` for unsigned
                    // operands with no trap representation, so a plain copy is
                    // the same architectural result. The record_bit CR0 update
                    // below still consumes gpr[target] unchanged.
                    Opcode::Or if source == rhs => format!("context->gpr[{source}]"),
                    Opcode::Or => format!("context->gpr[{source}] | context->gpr[{rhs}]"),
                    Opcode::Orc => format!("context->gpr[{source}] | ~context->gpr[{rhs}]"),
                    Opcode::And => format!("context->gpr[{source}] & context->gpr[{rhs}]"),
                    Opcode::Andc => format!("context->gpr[{source}] & ~context->gpr[{rhs}]"),
                    Opcode::Xor => format!("context->gpr[{source}] ^ context->gpr[{rhs}]"),
                    Opcode::Nor => format!("~(context->gpr[{source}] | context->gpr[{rhs}])"),
                    _ => unreachable!(),
                };
                writeln!(output, "    context->gpr[{target}] = {expression};")
                    .map_err(|_| TranslationError::Formatting)?;
                if record_bit(word) {
                    writeln!(
                        output,
                        "    galaxy::update_cr0(context, context->gpr[{target}]);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                }
            }
            Opcode::Crand
            | Opcode::Crandc
            | Opcode::Creqv
            | Opcode::Crnand
            | Opcode::Crnor
            | Opcode::Cror
            | Opcode::Crorc
            | Opcode::Crxor => {
                let target = gpr_rt(word);
                let left = gpr_ra(word);
                let right = gpr_rb(word);
                let expression = match instruction.op {
                    Opcode::Crand => "left && right",
                    Opcode::Crandc => "left && !right",
                    Opcode::Creqv => "left == right",
                    Opcode::Crnand => "!(left && right)",
                    Opcode::Crnor => "!(left || right)",
                    Opcode::Cror => "left || right",
                    Opcode::Crorc => "left || !right",
                    Opcode::Crxor => "left != right",
                    _ => unreachable!(),
                };
                writeln!(
                    output,
                    "    const bool left = galaxy::cr_bit(context, {left}u);\n    const bool right = galaxy::cr_bit(context, {right}u);\n    galaxy::set_cr_bit(context, {target}u, {expression});"
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Cntlzw | Opcode::Extsb | Opcode::Extsh => {
                let source = gpr_rt(word);
                let target = gpr_ra(word);
                let expression = match instruction.op {
                    Opcode::Cntlzw => format!(
                        "static_cast<std::uint32_t>(std::countl_zero(context->gpr[{source}]))"
                    ),
                    Opcode::Extsb => format!(
                        "static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(context->gpr[{source}])))"
                    ),
                    Opcode::Extsh => format!(
                        "static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(context->gpr[{source}])))"
                    ),
                    _ => unreachable!(),
                };
                writeln!(output, "    context->gpr[{target}] = {expression};")
                    .map_err(|_| TranslationError::Formatting)?;
                if record_bit(word) {
                    writeln!(
                        output,
                        "    galaxy::update_cr0(context, context->gpr[{target}]);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                }
            }
            Opcode::Slw | Opcode::Srw => {
                let source = gpr_rt(word);
                let target = gpr_ra(word);
                let shift_register = gpr_rb(word);
                writeln!(
                    output,
                    "    const std::uint32_t shift = context->gpr[{shift_register}] & 0x3Fu;"
                )
                .map_err(|_| TranslationError::Formatting)?;
                let operator = if instruction.op == Opcode::Slw {
                    "<<"
                } else {
                    ">>"
                };
                writeln!(
                    output,
                    "    context->gpr[{target}] = shift < 32u ? context->gpr[{source}] {operator} shift : 0u;"
                )
                .map_err(|_| TranslationError::Formatting)?;
                if record_bit(word) {
                    writeln!(
                        output,
                        "    galaxy::update_cr0(context, context->gpr[{target}]);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                }
            }
            Opcode::Sraw => {
                let source_register = gpr_rt(word);
                let target = gpr_ra(word);
                let shift_register = gpr_rb(word);
                writeln!(
                    output,
                    "    const std::uint32_t source = context->gpr[{source_register}];"
                )
                .map_err(|_| TranslationError::Formatting)?;
                writeln!(
                    output,
                    "    const std::uint32_t shift = context->gpr[{shift_register}] & 0x3Fu;"
                )
                .map_err(|_| TranslationError::Formatting)?;
                output.push_str(
                    "    if (shift == 0u) {\n\
                     \x20       context->gpr[",
                );
                write!(output, "{target}").map_err(|_| TranslationError::Formatting)?;
                output.push_str(
                    "] = source;\n\
                     \x20       galaxy::set_xer_ca(context, false);\n\
                     \x20   } else if (shift < 32u) {\n\
                     \x20       context->gpr[",
                );
                write!(output, "{target}").map_err(|_| TranslationError::Formatting)?;
                output.push_str(
                    "] = galaxy::arithmetic_shift_right(source, shift);\n\
                     \x20       const std::uint32_t shifted_mask = (1u << shift) - 1u;\n\
                     \x20       galaxy::set_xer_ca(context, (source & 0x80000000u) != 0 && (source & shifted_mask) != 0);\n\
                     \x20   } else {\n\
                     \x20       context->gpr[",
                );
                write!(output, "{target}").map_err(|_| TranslationError::Formatting)?;
                output.push_str(
                    "] = (source & 0x80000000u) != 0 ? 0xFFFFFFFFu : 0u;\n\
                     \x20       galaxy::set_xer_ca(context, (source & 0x80000000u) != 0);\n\
                     \x20   }\n",
                );
                if record_bit(word) {
                    writeln!(
                        output,
                        "    galaxy::update_cr0(context, context->gpr[{target}]);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                }
            }
            Opcode::Srawi => {
                let source = gpr_rt(word);
                let target = gpr_ra(word);
                let shift = (word >> 11) & 0x1F;
                writeln!(
                    output,
                    "    const std::uint32_t source = context->gpr[{source}];"
                )
                .map_err(|_| TranslationError::Formatting)?;
                writeln!(
                    output,
                    "    context->gpr[{target}] = galaxy::arithmetic_shift_right(source, {shift}u);"
                )
                .map_err(|_| TranslationError::Formatting)?;
                if shift == 0 {
                    output.push_str("    galaxy::set_xer_ca(context, false);\n");
                } else {
                    let shifted_mask = (1_u32 << shift) - 1;
                    writeln!(
                        output,
                        "    galaxy::set_xer_ca(context, (source & 0x80000000u) != 0 && (source & 0x{shifted_mask:08X}u) != 0);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                }
                if record_bit(word) {
                    writeln!(
                        output,
                        "    galaxy::update_cr0(context, context->gpr[{target}]);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                }
            }
            Opcode::Rlwinm => {
                let access = guest_integer_gpr_access(instruction.op, word)
                    .expect("rlwinm GPR roles are classified");
                let source = access.first_source.expect("rlwinm source");
                let target = access.destination;
                let shift = (word >> 11) & 0x1F;
                let mask_begin = (word >> 6) & 0x1F;
                let mask_end = (word >> 1) & 0x1F;
                let mask = rotate_mask(mask_begin, mask_end);
                // Only the shift == 0 form is folded: `rotl(v, 0) & mask` is
                // exactly `v & mask`, so the rotate disappears and
                // `clrlwi`/`clrrwi`-style masks become a single `and`
                // (7,115 sites module-wide).
                //
                // The rotate itself is NOT removable for shift != 0. A
                // pre-rotated-mask rewrite was tried here and is WRONG for
                // shift != 0: rotating `v` by any amount other than `shift`
                // moves `v`'s bits relative to a mask whose position is fixed
                // by MB/ME, so only `rotl(v, shift) & mask` reproduces the ISA.
                // Verified exhaustively over all 32x32x32 (SH, MB, ME) triples
                // and 12 operand patterns: the pre-rotated form mismatched
                // 319,448 of 393,216 encodings, while this form matched all of
                // them. Do not "optimize" the rotate amount again.
                if shift == 0 {
                    writeln!(
                        output,
                        "    context->gpr[{target}] = (context->gpr[{source}] & 0x{mask:08X}u);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                } else {
                    writeln!(
                        output,
                        "    context->gpr[{target}] = std::rotl(context->gpr[{source}], {shift}) & 0x{mask:08X}u;"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                }
                if record_bit(word) {
                    writeln!(
                        output,
                        "    galaxy::update_cr0(context, context->gpr[{target}]);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                }
            }
            Opcode::Rlwimi => {
                let source = gpr_rt(word);
                let target = gpr_ra(word);
                let shift = (word >> 11) & 0x1F;
                let mask_begin = (word >> 6) & 0x1F;
                let mask_end = (word >> 1) & 0x1F;
                let mask = rotate_mask(mask_begin, mask_end);
                // Same shift == 0 fold as `Rlwinm`, and the same warning: the
                // rotate amount must stay exactly `shift`. See the comment on
                // the `Rlwinm` arm for the exhaustive equivalence result.
                if shift == 0 {
                    writeln!(
                        output,
                        "    context->gpr[{target}] = (context->gpr[{target}] & 0x{:08X}u) | (context->gpr[{source}] & 0x{mask:08X}u);",
                        !mask
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                } else {
                    writeln!(
                        output,
                        "    context->gpr[{target}] = (std::rotl(context->gpr[{source}], {shift}) & 0x{mask:08X}u) | (context->gpr[{target}] & 0x{:08X}u);",
                        !mask
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                }
                if record_bit(word) {
                    writeln!(
                        output,
                        "    galaxy::update_cr0(context, context->gpr[{target}]);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                }
            }
            Opcode::Rlwnm => {
                let source = gpr_rt(word);
                let target = gpr_ra(word);
                let shift_register = gpr_rb(word);
                let mask_begin = (word >> 6) & 0x1F;
                let mask_end = (word >> 1) & 0x1F;
                let mask = rotate_mask(mask_begin, mask_end);
                writeln!(
                    output,
                    "    context->gpr[{target}] = std::rotl(context->gpr[{source}], static_cast<int>(context->gpr[{shift_register}] & 0x1Fu)) & 0x{mask:08X}u;"
                )
                .map_err(|_| TranslationError::Formatting)?;
                if record_bit(word) {
                    writeln!(
                        output,
                        "    galaxy::update_cr0(context, context->gpr[{target}]);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                }
            }
            Opcode::Cmpi | Opcode::Cmpli | Opcode::Cmp | Opcode::Cmpl => {
                if word & 0x0020_0000 != 0 {
                    return Err(TranslationError::UnsupportedInstruction {
                        address: pc,
                        opcode: format!("{:?}(64-bit)", instruction.op),
                    });
                }
                let field = (word >> 23) & 0x7;
                let left = gpr_ra(word);
                let (helper, right) = match instruction.op {
                    Opcode::Cmpi => (
                        "compare_signed",
                        format!(
                            "static_cast<std::int32_t>(0x{:08X}u)",
                            signed_immediate(word) as u32
                        ),
                    ),
                    Opcode::Cmpli => ("compare_unsigned", format!("0x{:08X}u", word & 0xFFFF)),
                    Opcode::Cmp => (
                        "compare_signed",
                        format!("static_cast<std::int32_t>(context->gpr[{}])", gpr_rb(word)),
                    ),
                    Opcode::Cmpl => (
                        "compare_unsigned",
                        format!("context->gpr[{}]", gpr_rb(word)),
                    ),
                    _ => unreachable!(),
                };
                let left_expression = if matches!(instruction.op, Opcode::Cmpi | Opcode::Cmp) {
                    format!("static_cast<std::int32_t>(context->gpr[{left}])")
                } else {
                    format!("context->gpr[{left}]")
                };
                writeln!(
                    output,
                    "    galaxy::{helper}(context, {field}u, {left_expression}, {right});"
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Fcmpu | Opcode::Fcmpo => {
                let field = (word >> 23) & 0x7;
                let left = gpr_ra(word);
                let right = gpr_rb(word);
                let ordered = instruction.op == Opcode::Fcmpo;
                writeln!(
                    output,
                    "    galaxy::compare_f64(context, {field}u, context->fpr_bits[{left}], context->fpr_bits[{right}], {ordered});"
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Sc => {
                writeln!(
                    output,
                    "    galaxy::dispatch_system_call(services, 0x{pc:08X}u, 0x{word:08X}u, context, memory);"
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Twi => {
                let trap_options = gpr_rt(word);
                let source = gpr_ra(word);
                let immediate = signed_immediate(word) as u32;
                writeln!(
                    output,
                    "    galaxy::trap_word_immediate(services, 0x{pc:08X}u, 0x{word:08X}u, {trap_options}u, context->gpr[{source}], 0x{immediate:08X}u, context, memory);"
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Rfi => {
                writeln!(
                    output,
                    "    galaxy::dispatch_return_from_interrupt(services, 0x{pc:08X}u, context, memory);\n    return;"
                )
                .map_err(|_| TranslationError::Formatting)?;
                final_instruction_is_terminal = true;
            }
            Opcode::B => {
                let target = branch_target(pc, word);
                let link = word & 1 != 0;
                if link {
                    writeln!(output, "    context->lr = 0x{:08X}u;", pc + 4)
                        .map_err(|_| TranslationError::Formatting)?;
                }
                if target >= address && target < function_end {
                    if target <= pc {
                        writeln!(
                            output,
                            "    galaxy::branch_checkpoint_taken(services, 0x{pc:08X}u, 0x{target:08X}u, context, memory);"
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                    }
                    writeln!(output, "    goto label_{target:08X};")
                        .map_err(|_| TranslationError::Formatting)?;
                    final_instruction_is_terminal = true;
                } else {
                    if !link
                        && audited_native_tail_call(address, pc, target)
                        && callable_entries
                            .and_then(|entries| entries.get(&target))
                            .is_some()
                    {
                        let owner = callable_entries
                            .and_then(|entries| entries.get(&target))
                            .copied()
                            .expect("audited native tail call target was checked");
                        emit_resolved_generated_direct_call(&mut output, target, owner, pc)?;
                    } else {
                        emit_direct_guest_call_with_exact_fpu(
                            &mut output,
                            target,
                            pc,
                            callable_entries,
                            exact_fpu_functions,
                        )?;
                    }
                    if link {
                        emit_call_return_checkpoint(&mut output, pc)?;
                    } else {
                        output.push_str("    return;\n");
                        final_instruction_is_terminal = true;
                    }
                }
            }
            Opcode::Bc => {
                // Dusklight's PC JAudio port drops this timing-based safety
                // kill: host DSP cadence can trip its starvation test and cut
                // off active voices. Only the kill block at this opcode is
                // bypassed; the surrounding JAudio work stays translated.
                // Source: dusklight/libs/JSystem/src/JAudio2/JASAiCtrl.cpp,
                // JASDriver::updateDSP, CC0-1.0, commit ad979d3dae092d0f5cbdaf49eabca7b4f1db4838.
                if address == 0x8049_44D0 && pc == 0x8049_452C && word == 0x4182_0054 {
                    output.push_str("    goto label_80494580;\n");
                    final_instruction_is_terminal = true;
                } else {
                    let target = conditional_branch_target(pc, word);
                    let link = word & 1 != 0;
                    let bo = (word >> 21) & 0x1F;
                    let bi = (word >> 16) & 0x1F;
                    let unconditional = branch_is_unconditional(bo);
                    let condition = emit_branch_condition(&mut output, bo, bi)?;
                    if link {
                        writeln!(output, "    context->lr = 0x{:08X}u;", pc + 4)
                            .map_err(|_| TranslationError::Formatting)?;
                    }
                    if target >= address && target < function_end {
                        if unconditional {
                            if target <= pc {
                                writeln!(
                                output,
                                "    galaxy::branch_checkpoint_taken(services, 0x{pc:08X}u, 0x{target:08X}u, context, memory);"
                            )
                            .map_err(|_| TranslationError::Formatting)?;
                            }
                            writeln!(output, "    goto label_{target:08X};")
                                .map_err(|_| TranslationError::Formatting)?;
                            final_instruction_is_terminal = true;
                        } else {
                            if target <= pc {
                                writeln!(
                                output,
                                "    if ({condition}) {{ galaxy::branch_checkpoint_taken(services, 0x{pc:08X}u, 0x{target:08X}u, context, memory); goto label_{target:08X}; }}"
                            )
                            .map_err(|_| TranslationError::Formatting)?;
                            } else {
                                writeln!(output, "    if ({condition}) goto label_{target:08X};")
                                    .map_err(|_| TranslationError::Formatting)?;
                            }
                        }
                    } else if link {
                        if unconditional {
                            emit_direct_guest_call_with_exact_fpu(
                                &mut output,
                                target,
                                pc,
                                callable_entries,
                                exact_fpu_functions,
                            )?;
                            emit_call_return_checkpoint(&mut output, pc)?;
                        } else {
                            writeln!(output, "    if ({condition}) {{")
                                .map_err(|_| TranslationError::Formatting)?;
                            emit_direct_guest_call_with_exact_fpu(
                                &mut output,
                                target,
                                pc,
                                callable_entries,
                                exact_fpu_functions,
                            )?;
                            emit_call_return_checkpoint(&mut output, pc)?;
                            output.push_str("    }\n");
                        }
                    } else if unconditional {
                        emit_direct_guest_call_with_exact_fpu(
                            &mut output,
                            target,
                            pc,
                            callable_entries,
                            exact_fpu_functions,
                        )?;
                        output.push_str("    return;\n");
                        final_instruction_is_terminal = true;
                    } else {
                        writeln!(output, "    if ({condition}) {{")
                            .map_err(|_| TranslationError::Formatting)?;
                        emit_direct_guest_call_with_exact_fpu(
                            &mut output,
                            target,
                            pc,
                            callable_entries,
                            exact_fpu_functions,
                        )?;
                        output.push_str("        return;\n    }\n");
                    }
                }
            }
            Opcode::Bcctr => {
                let bo = (word >> 21) & 0x1F;
                let bi = (word >> 16) & 0x1F;
                let link = word & 1 != 0;
                if bo & 0x4 == 0 {
                    return Err(TranslationError::UnsupportedInstruction {
                        address: pc,
                        opcode: "Bcctr(ctr-decrement)".to_owned(),
                    });
                }
                let condition = emit_branch_condition(&mut output, bo, bi)?;
                let unconditional = branch_is_unconditional(bo);
                output.push_str(
                    "    const std::uint32_t branch_target = context->ctr & 0xFFFFFFFCu;\n",
                );
                if link {
                    writeln!(output, "    context->lr = 0x{:08X}u;", pc + 4)
                        .map_err(|_| TranslationError::Formatting)?;
                    if unconditional {
                        emit_cached_guest_call(&mut output, "branch_target", pc)?;
                        emit_call_return_checkpoint(&mut output, pc)?;
                    } else {
                        writeln!(output, "    if ({condition}) {{")
                            .map_err(|_| TranslationError::Formatting)?;
                        emit_cached_guest_call(&mut output, "branch_target", pc)?;
                        emit_call_return_checkpoint(&mut output, pc)?;
                        output.push_str("    }\n");
                    }
                } else if unconditional {
                    emit_cached_guest_call(&mut output, "branch_target", pc)?;
                    output.push_str("    return;\n");
                    final_instruction_is_terminal = true;
                } else {
                    writeln!(output, "    if ({condition}) {{")
                        .map_err(|_| TranslationError::Formatting)?;
                    emit_cached_guest_call(&mut output, "branch_target", pc)?;
                    output.push_str("        return;\n    }\n");
                }
            }
            Opcode::Bclr => {
                // As in Dusklight m_Do_graphic.cpp::updateRenderSize (commit
                // ad979d3, CC0 1.0), perspective, HUD and pointer space derive
                // from one logical width. RMGE01's MR::getScreenWidth has one
                // final blr at 0x803F6B70; its body runs unchanged and only
                // the r3 result changes for the ultrawide policy.
                if address == 0x803F_6B44
                    && function_end == 0x803F_6B74
                    && pc == 0x803F_6B70
                    && word == 0x4E80_0020
                {
                    output.push_str(
                        "    galaxy::apply_experimental_rmge01_ultrawide_screen_width(context, services);\n",
                    );
                }
                let bo = (word >> 21) & 0x1F;
                let bi = (word >> 16) & 0x1F;
                let link = word & 1 != 0;
                let unconditional = branch_is_unconditional(bo);
                let condition = emit_branch_condition(&mut output, bo, bi)?;
                if link {
                    output.push_str(
                        "    const std::uint32_t branch_target = context->lr & 0xFFFFFFFCu;\n",
                    );
                    writeln!(output, "    context->lr = 0x{:08X}u;", pc + 4)
                        .map_err(|_| TranslationError::Formatting)?;
                    if unconditional {
                        emit_cached_guest_call(&mut output, "branch_target", pc)?;
                        emit_call_return_checkpoint(&mut output, pc)?;
                    } else {
                        writeln!(output, "    if ({condition}) {{")
                            .map_err(|_| TranslationError::Formatting)?;
                        emit_cached_guest_call(&mut output, "branch_target", pc)?;
                        emit_call_return_checkpoint(&mut output, pc)?;
                        output.push_str("    }\n");
                    }
                } else if unconditional {
                    if internal_return_targets.is_empty() {
                        output.push_str("    return;\n");
                    } else if share_lr_continuation_dispatch {
                        output.push_str("    goto lr_continuation_dispatch;\n");
                    } else {
                        output.push_str("    switch (context->lr & 0xFFFFFFFCu) {\n");
                        for target in &internal_return_targets {
                            writeln!(
                                output,
                                "    case 0x{target:08X}u: goto call_return_{target:08X};"
                            )
                            .map_err(|_| TranslationError::Formatting)?;
                        }
                        output.push_str("    default: return;\n    }\n");
                    }
                    final_instruction_is_terminal = true;
                } else {
                    if internal_return_targets.is_empty() {
                        writeln!(output, "    if ({condition}) return;")
                            .map_err(|_| TranslationError::Formatting)?;
                    } else if share_lr_continuation_dispatch {
                        writeln!(
                            output,
                            "    if ({condition}) goto lr_continuation_dispatch;"
                        )
                        .map_err(|_| TranslationError::Formatting)?;
                    } else {
                        writeln!(output, "    if ({condition}) {{")
                            .map_err(|_| TranslationError::Formatting)?;
                        output.push_str("        switch (context->lr & 0xFFFFFFFCu) {\n");
                        for target in &internal_return_targets {
                            writeln!(
                                output,
                                "        case 0x{target:08X}u: goto call_return_{target:08X};"
                            )
                            .map_err(|_| TranslationError::Formatting)?;
                        }
                        output.push_str("        default: return;\n        }\n    }\n");
                    }
                }
            }
            _ => {
                return Err(TranslationError::UnsupportedInstruction {
                    address: pc,
                    opcode: format!("{:?}", instruction.op),
                });
            }
        }
        if local_lane_profile != LocalLaneProfile::None {
            cache_local_paired_instruction(
                &mut output,
                instruction_output_start,
                &mut lane_facts,
                local_lane_profile,
                pc,
                word,
                instruction.op,
            )?;
        }
        if (address, function_end, pc, word) == (0x803F_6D84, 0x803F_6E30, 0x803F_6DB8, 0x3800_0340)
        {
            output.push_str(
                "    galaxy::apply_experimental_rmge01_efb_screen_width(context, services);\n",
            );
        }
        // Fit the wide UI into narrower custom aspects without changing the
        // 3D camera. Projection height and both Y conversions form an inverse
        // pair; guest operations run first and geometry hooks follow the
        // instruction block.
        let mut post_block_hooks = String::new();
        let layout_vertical_fit = match (address, function_end, pc, word) {
            (0x803C_A6C4, 0x803C_A76C, 0x803C_A708, 0xEC21_1028) => Some((1, false)),
            (0x8036_6658, 0x8036_6758, 0x8036_6728, 0xFC00_0050) => Some((0, false)),
            // Inverse fit of the f0 loaded at 0x803667FC, applied after the
            // labelled 0x80366804 lfd so its join also observes the fit.
            (0x8036_6758, 0x8036_6864, 0x8036_6804, 0xC85F_91D0) => Some((0, true)),
            _ => None,
        };
        if let Some((lane, inverse)) = layout_vertical_fit {
            writeln!(post_block_hooks,
                "    galaxy::apply_experimental_rmge01_layout_vertical_fit(context, services, {lane}u, {inverse});"
            ).map_err(|_| TranslationError::Formatting)?;
        }
        let home_geometry = match (address, function_end, pc, word) {
            (0x8036_26CC, 0x8036_284C, 0x8036_27F0, 0xEC63_2028) => Some((0, 3)),
            (0x8036_26CC, 0x8036_284C, 0x8036_27BC, 0xEC21_1028) => Some((1, 1)),
            (0x8100_00D8, 0x8101_B120, 0x8100_1DCC, 0xC024_0004) => Some((2, 1)),
            (0x8100_00D8, 0x8101_B120, 0x8100_1DD0, 0xC004_0008) => Some((3, 0)),
            (0x8100_00D8, 0x8101_B120, 0x8100_1DE0, 0xC006_0020) => Some((2, 0)),
            (0x8100_00D8, 0x8101_B120, 0x8100_1DEC, 0xC005_0024) => Some((3, 0)),
            _ => None,
        };
        if let Some((kind, lane)) = home_geometry {
            // Game kinds 0 and 1 have fixed lanes; Home RSO kinds name theirs.
            if kind < 2 {
                writeln!(
                    post_block_hooks,
                    "    galaxy::apply_experimental_rmge01_home_geometry(context, services, {kind}u);"
                )
            } else {
                writeln!(
                    post_block_hooks,
                    "    galaxy::apply_experimental_rmge01_home_geometry(context, services, {kind}u, {lane}u);"
                )
            }
            .map_err(|_| TranslationError::Formatting)?;
        }
        // These hooks change the canonical context after the guest operation,
        // so local-lane lowering must reload that lane.
        let changed_layout_lane = home_geometry
            .map(|(_, lane)| lane)
            .or(layout_vertical_fit.map(|(lane, _)| lane))
            .or(match (address, function_end, pc, word) {
                (0x8036_6658, 0x8036_6758, 0x8036_66A8, 0xC042_1438) => Some(2),
                (0x8036_6658, 0x8036_6758, 0x8036_66B4, 0xC002_143C)
                | (0x8036_6758, 0x8036_6864, 0x8036_67C4, 0xC002_1438) => Some(0),
                _ => None,
            });
        if let Some(lane) = changed_layout_lane {
            lane_facts[0][lane] = false;
            lane_facts[1][lane] = false;
        }
        if let Some(access) = typed_pure_access {
            cache_typed_region_instruction(&mut output, typed_instruction_start, pc, word, access)?;
        }
        output.push_str("    }\n");
        output.push_str(&post_block_hooks);
        if typed_region_gpr_mask != 0 && typed_pure_access.is_none() {
            emit_resident_gpr_sync(&mut output, typed_region_gpr_mask, true)?;
        }
        // The diagnostic callbacks above are host service boundaries when
        // active, so the MSR[FP] guard is re-established after one.
        if function_async_marker.is_some()
            || main_frame_stage_marker(address, pc).is_some()
            || opcode_invalidates_fpu_availability_proof(instruction.op)
        {
            fpu_availability_proven = false;
        }
    }
    if !final_instruction_is_terminal
        && (function_can_fall_through_from(address, bytes, address)
            || entries
                .iter()
                .any(|entry| function_can_fall_through_from(address, bytes, *entry)))
    {
        return Err(TranslationError::MissingReturn { address });
    }
    if !call_return_targets.is_empty() {
        // Internal linked calls reach these labels through the non-link
        // BCLR/LR dispatch above; external call-return continuations reach
        // them when the host resumes a stack unwound by OSLoadContext/RFI.
        // Other interior entries use label_* and add no return boundary. An
        // address with both kinds is safe as a checkpoint: it only observes an
        // already published event at the instruction boundary.
        output.push_str("    return;\n");
        if share_lr_continuation_dispatch {
            output
                .push_str("lr_continuation_dispatch:\n    switch (context->lr & 0xFFFFFFFCu) {\n");
            for target in &internal_return_targets {
                writeln!(
                    output,
                    "    case 0x{target:08X}u: goto call_return_{target:08X};"
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            output.push_str("    default: return;\n    }\n");
        }
        for return_pc in &call_return_targets {
            writeln!(output, "call_return_{return_pc:08X}:")
                .map_err(|_| TranslationError::Formatting)?;
            emit_call_return_checkpoint(&mut output, return_pc.wrapping_sub(4))?;
            writeln!(output, "    goto label_{return_pc:08X};")
                .map_err(|_| TranslationError::Formatting)?;
        }
    }
    Ok(output)
}

fn local_paired_proof_error(address: u32, reason: &str) -> TranslationError {
    TranslationError::LocalPairedProof {
        address,
        reason: reason.to_owned(),
    }
}

fn require_local_paired_opcode(
    profile: LocalLaneProfile,
    pc: u32,
    word: u32,
    op: Opcode,
) -> Result<(), TranslationError> {
    let admitted = match profile {
        LocalLaneProfile::None => false,
        LocalLaneProfile::PsmtxConcat => match op {
            Opcode::PsqL | Opcode::PsqSt => (word & 0xF000) == 0,
            Opcode::PsMuls0
            | Opcode::PsMuls1
            | Opcode::PsMul
            | Opcode::PsMadds0
            | Opcode::PsMadds1
            | Opcode::PsMadd
            | Opcode::Stwu
            | Opcode::Stfd
            | Opcode::Lfd
            | Opcode::Addi
            | Opcode::Addis => true,
            Opcode::Bclr => word == 0x4E80_0020,
            _ => false,
        },
        LocalLaneProfile::PsvecCross => match op {
            Opcode::PsqL | Opcode::PsqSt => (word & 0x7000) == 0,
            Opcode::Lfs
            | Opcode::PsMerge00
            | Opcode::PsMerge01
            | Opcode::PsMerge10
            | Opcode::PsMerge11
            | Opcode::PsMul
            | Opcode::PsMuls0
            | Opcode::PsMsub
            | Opcode::PsNeg => true,
            Opcode::Bclr => word == 0x4E80_0020,
            _ => false,
        },
        LocalLaneProfile::PsvecNormalize => match op {
            Opcode::PsqL | Opcode::PsqSt => (word & 0x7000) == 0,
            Opcode::Lfs
            | Opcode::PsMul
            | Opcode::PsMadd
            | Opcode::PsSum0
            | Opcode::Frsqrte
            | Opcode::Fmuls
            | Opcode::Fnmsubs
            | Opcode::PsMuls0 => true,
            Opcode::Bclr => word == 0x4E80_0020,
            _ => false,
        },
    };
    if admitted {
        Ok(())
    } else {
        Err(local_paired_proof_error(
            pc,
            "opcode, branch, or quantization is outside the proved straight-line subset",
        ))
    }
}

// Only exact widened-binary32 producers establish a cached lane. The ordinary
// emitter still emits every memory operation and commit; this pass only
// substitutes validated input expressions.
fn cache_local_paired_instruction(
    output: &mut String,
    start: usize,
    facts: &mut [[bool; 32]; 2],
    profile: LocalLaneProfile,
    pc: u32,
    word: u32,
    op: Opcode,
) -> Result<(), TranslationError> {
    let mut instruction = output.split_off(start);
    let expected_uses = match op {
        Opcode::PsMuls0 | Opcode::PsMuls1 | Opcode::PsMul => 4,
        Opcode::PsMadds0 | Opcode::PsMadds1 | Opcode::PsMadd | Opcode::PsMsub => 6,
        Opcode::PsSum0 | Opcode::PsSum1 | Opcode::Fnmsubs => 3,
        _ => 0,
    };
    let mut uses = 0;
    for (lane, field) in ["fpr_bits", "ps1_bits"].iter().enumerate() {
        for (register, known) in facts[lane].iter().enumerate() {
            let operand = format!("galaxy::require_single_precision_bits(context->{field}[{register}], services, 0x{pc:08X}u)");
            let count = instruction.matches(&operand).count();
            if count != 0 {
                if !known {
                    return Err(local_paired_proof_error(
                        pc,
                        "operand has no exact binary32 producer on this path",
                    ));
                }
                uses += count;
                instruction = instruction.replace(&operand, &format!("lane{lane}[{register}]"));
            }
        }
    }
    if uses != expected_uses || instruction.contains("require_single_precision_bits") {
        return Err(local_paired_proof_error(
            pc,
            "rendered operand contract changed",
        ));
    }
    let target = gpr_rt(word) as usize;
    if profile == LocalLaneProfile::PsmtxConcat
        && matches!(op, Opcode::PsMadd | Opcode::PsMadds0 | Opcode::PsMadds1)
    {
        let original = format!(
            "    const galaxy::PpcFloatResult result_ps0 = galaxy::ppc_f32_ternary(galaxy::PpcFloatTernaryOperation::MultiplyAdd, multiplicand_ps0, multiplier_ps0, addend_ps0, context->fpscr);\n    const galaxy::PpcFloatResult result_ps1 = galaxy::ppc_f32_ternary(galaxy::PpcFloatTernaryOperation::MultiplyAdd, multiplicand_ps1, multiplier_ps1, addend_ps1, context->fpscr);\n    galaxy::ppc_commit_paired_result(context, {target}u, result_ps0, result_ps1, false, {}, services, 0x{pc:08X}u);", record_bit(word)
        );
        if instruction.matches(&original).count() != 1 {
            return Err(local_paired_proof_error(
                pc,
                "paired ternary commit contract changed",
            ));
        }
        let packed = format!(
            "    galaxy::ppc_commit_paired_ternary_result(context, {target}u, galaxy::PpcFloatTernaryOperation::MultiplyAdd, multiplicand_ps0, multiplicand_ps1, multiplier_ps0, multiplier_ps1, addend_ps0, addend_ps1, {}, services, 0x{pc:08X}u);", record_bit(word)
        );
        instruction = instruction.replace(&original, &packed);
    }
    output.push_str(&instruction);
    let paired_result = matches!(
        op,
        Opcode::PsMuls0
            | Opcode::PsMuls1
            | Opcode::PsMul
            | Opcode::PsMadds0
            | Opcode::PsMadds1
            | Opcode::PsMadd
            | Opcode::PsMsub
            | Opcode::PsSum0
            | Opcode::PsSum1
    );
    if op == Opcode::PsqL || paired_result {
        for (lane, field) in ["fpr_bits", "ps1_bits"].iter().enumerate() {
            let producer = if op == Opcode::PsqL
                || instruction.contains("ppc_commit_paired_binary_result")
                || instruction.contains("ppc_commit_paired_ternary_result")
            {
                format!("context->{field}[{target}]")
            } else {
                format!("result_ps{lane}.bits")
            };
            writeln!(output, "    lane{lane}[{target}] = galaxy::narrow_f64_to_f32_bits({producer}, services, 0x{pc:08X}u);")
                .map_err(|_| TranslationError::Formatting)?;
            facts[lane][target] = true;
        }
    } else if matches!(
        profile,
        LocalLaneProfile::PsvecCross | LocalLaneProfile::PsvecNormalize
    ) && op == Opcode::Lfs
    {
        writeln!(
            output,
            "    lane0[{target}] = galaxy::narrow_f64_to_f32_bits(context->fpr_bits[{target}], services, 0x{pc:08X}u);"
        )
        .map_err(|_| TranslationError::Formatting)?;
        writeln!(output, "    lane1[{target}] = lane0[{target}];")
            .map_err(|_| TranslationError::Formatting)?;
        facts[0][target] = true;
        facts[1][target] = true;
    } else if profile == LocalLaneProfile::PsvecCross
        && matches!(
            op,
            Opcode::PsMerge00 | Opcode::PsMerge01 | Opcode::PsMerge10 | Opcode::PsMerge11
        )
    {
        let left = gpr_ra(word) as usize;
        let right = gpr_rb(word) as usize;
        let sources = match op {
            Opcode::PsMerge00 => [(0usize, left), (0usize, right)],
            Opcode::PsMerge01 => [(0usize, left), (1usize, right)],
            Opcode::PsMerge10 => [(1usize, left), (0usize, right)],
            Opcode::PsMerge11 => [(1usize, left), (1usize, right)],
            _ => unreachable!(),
        };
        if !facts[sources[0].0][sources[0].1] || !facts[sources[1].0][sources[1].1] {
            return Err(local_paired_proof_error(
                pc,
                "paired merge source has no exact binary32 producer",
            ));
        }
        writeln!(
            output,
            "    lane0[{target}] = lane{}[{}];",
            sources[0].0, sources[0].1
        )
        .map_err(|_| TranslationError::Formatting)?;
        writeln!(
            output,
            "    lane1[{target}] = lane{}[{}];",
            sources[1].0, sources[1].1
        )
        .map_err(|_| TranslationError::Formatting)?;
        facts[0][target] = true;
        facts[1][target] = true;
    } else if profile == LocalLaneProfile::PsvecNormalize
        && matches!(op, Opcode::Fmuls | Opcode::Fnmsubs)
    {
        writeln!(
            output,
            "    lane0[{target}] = galaxy::narrow_f64_to_f32_bits(result.bits, services, 0x{pc:08X}u);"
        )
        .map_err(|_| TranslationError::Formatting)?;
        writeln!(output, "    lane1[{target}] = lane0[{target}];")
            .map_err(|_| TranslationError::Formatting)?;
        facts[0][target] = true;
        facts[1][target] = true;
    } else if profile == LocalLaneProfile::PsvecNormalize && op == Opcode::Frsqrte {
        // frsqrte writes binary64; the following scalar single multiply
        // produces an exact widened binary32 again.
        facts[0][target] = false;
    } else if op == Opcode::Lfd {
        // A stack restore may load arbitrary binary64. PS1 is untouched.
        facts[0][target] = false;
    }
    Ok(())
}

// Checks symbolically that every memory access lies inside the initial RAM
// spans checked by the emitted guard. Roots identify initial registers; root
// zero is an absolute address.
fn validate_psmtx_local_memory_contract(bytes: &[u8]) -> Result<(), TranslationError> {
    let address = 0x804B_5F3C;
    let mut registers: [Option<(u32, i64)>; 32] = [None; 32];
    for register in [1, 3, 4, 5] {
        registers[register] = Some((register as u32, 0));
    }
    let mut returned = false;
    for (index, chunk) in bytes.chunks_exact(4).enumerate() {
        let pc = address + index as u32 * 4;
        let word = u32::from_be_bytes(chunk.try_into().expect("four-byte instruction"));
        let op = Ins::new(word, Extensions::gekko_broadway()).op;
        require_local_paired_opcode(LocalLaneProfile::PsmtxConcat, pc, word, op)?;
        let target = gpr_rt(word) as usize;
        let base = gpr_ra(word) as usize;
        if matches!(op, Opcode::Addi | Opcode::Addis) {
            let (root, offset) = if base == 0 {
                (0, 0)
            } else {
                registers[base]
                    .ok_or_else(|| local_paired_proof_error(pc, "unproved address arithmetic"))?
            };
            let immediate =
                i64::from(signed_immediate(word)) * if op == Opcode::Addis { 65536 } else { 1 };
            let updated = offset + immediate;
            registers[target] = Some((
                root,
                if root == 0 {
                    i64::from(updated as u32)
                } else {
                    updated
                },
            ));
        } else if matches!(
            op,
            Opcode::PsqL | Opcode::PsqSt | Opcode::Stwu | Opcode::Stfd | Opcode::Lfd
        ) {
            let (root, offset) = registers[base]
                .ok_or_else(|| local_paired_proof_error(pc, "memory base has no guarded root"))?;
            let displacement = if matches!(op, Opcode::PsqL | Opcode::PsqSt) {
                paired_single_displacement(word)
            } else {
                signed_immediate(word)
            };
            let start = offset + i64::from(displacement);
            let end = start + if op == Opcode::Stwu { 4 } else { 8 };
            let write = matches!(op, Opcode::PsqSt | Opcode::Stwu | Opcode::Stfd);
            let covered = match root {
                1 => start >= -64 && end <= 0,
                3 | 4 => !write && start >= 0 && end <= 48,
                5 => write && start >= 0 && end <= 48,
                0 => !write && start >= 0x8069_E148 && end <= 0x8069_E150,
                _ => false,
            };
            if !covered {
                return Err(local_paired_proof_error(
                    pc,
                    "memory access leaves the guarded read/write spans",
                ));
            }
            if op == Opcode::Stwu {
                if base != 1 || root != 1 || start != -64 {
                    return Err(local_paired_proof_error(
                        pc,
                        "stack update differs from the guarded frame",
                    ));
                }
                registers[base] = Some((root, start));
            }
        } else if op == Opcode::Bclr {
            if (index + 1) * 4 != bytes.len() || registers[1] != Some((1, 0)) {
                return Err(local_paired_proof_error(
                    pc,
                    "return is not terminal with the original stack restored",
                ));
            }
            returned = true;
        }
    }
    if !returned || bytes.len() % 4 != 0 {
        return Err(local_paired_proof_error(
            address,
            "missing complete terminal return",
        ));
    }
    Ok(())
}

// The cross-product guard checks two 12-byte input spans and one 12-byte
// output span. This leaf has scalar loads, paired loads/stores and no stack
// frame, so it has its own verifier.
fn validate_psvec_cross_local_memory_contract(bytes: &[u8]) -> Result<(), TranslationError> {
    let address = 0x804B_6CB8;
    if bytes.len() != 0x3C || bytes.len() % 4 != 0 {
        return Err(local_paired_proof_error(
            address,
            "cross-product function size changed",
        ));
    }
    let mut accesses = Vec::new();
    let mut returned = false;
    for (index, chunk) in bytes.chunks_exact(4).enumerate() {
        let pc = address + index as u32 * 4;
        let word = u32::from_be_bytes(chunk.try_into().expect("four-byte instruction"));
        let op = Ins::new(word, Extensions::gekko_broadway()).op;
        require_local_paired_opcode(LocalLaneProfile::PsvecCross, pc, word, op)?;
        match op {
            Opcode::Lfs => {
                let root = gpr_ra(word);
                let start = i64::from(signed_immediate(word));
                accesses.push((root, start, start + 4, false));
            }
            Opcode::PsqL | Opcode::PsqSt => {
                if ((word >> 12) & 0x7) != 0 {
                    return Err(local_paired_proof_error(
                        pc,
                        "cross-product PSQ operation no longer uses GQR0",
                    ));
                }
                let root = gpr_ra(word);
                let start = i64::from(paired_single_displacement(word));
                let size = if ((word >> 15) & 1) != 0 { 4 } else { 8 };
                accesses.push((root, start, start + size, op == Opcode::PsqSt));
            }
            Opcode::Bclr => {
                if (index + 1) * 4 != bytes.len() {
                    return Err(local_paired_proof_error(pc, "return is not terminal"));
                }
                returned = true;
            }
            _ => {}
        }
    }
    let expected = vec![
        (4, 0, 8, false),
        (3, 8, 12, false),
        (3, 0, 8, false),
        (4, 8, 12, false),
        (5, 0, 4, true),
        (5, 4, 12, true),
    ];
    if !returned || accesses != expected {
        return Err(local_paired_proof_error(
            address,
            "cross-product memory access contract changed",
        ));
    }
    Ok(())
}

// Normalize reads one 12-byte vector and two adjacent scalar constants and
// writes one 12-byte vector. Separate from CrossProduct because it also
// checks scalar-single producer facts.
fn validate_psvec_normalize_local_memory_contract(bytes: &[u8]) -> Result<(), TranslationError> {
    let address = 0x804B_6BCC;
    if bytes.len() != 0x44 || bytes.len() % 4 != 0 {
        return Err(local_paired_proof_error(
            address,
            "normalize function size changed",
        ));
    }
    let mut accesses = Vec::new();
    let mut returned = false;
    for (index, chunk) in bytes.chunks_exact(4).enumerate() {
        let pc = address + index as u32 * 4;
        let word = u32::from_be_bytes(chunk.try_into().expect("four-byte instruction"));
        let op = Ins::new(word, Extensions::gekko_broadway()).op;
        require_local_paired_opcode(LocalLaneProfile::PsvecNormalize, pc, word, op)?;
        match op {
            Opcode::Lfs => {
                let root = gpr_ra(word);
                let start = i64::from(signed_immediate(word));
                accesses.push((root, start, start + 4, false));
            }
            Opcode::PsqL | Opcode::PsqSt => {
                if ((word >> 12) & 0x7) != 0 {
                    return Err(local_paired_proof_error(
                        pc,
                        "normalize PSQ operation no longer uses GQR0",
                    ));
                }
                let root = gpr_ra(word);
                let start = i64::from(paired_single_displacement(word));
                let size = if ((word >> 15) & 1) != 0 { 4 } else { 8 };
                accesses.push((root, start, start + size, op == Opcode::PsqSt));
            }
            Opcode::Bclr => {
                if (index + 1) * 4 != bytes.len() {
                    return Err(local_paired_proof_error(pc, "return is not terminal"));
                }
                returned = true;
            }
            _ => {}
        }
    }
    let expected = vec![
        (3, 0, 8, false),
        (3, 8, 12, false),
        (2, 0x24E8, 0x24EC, false),
        (2, 0x24EC, 0x24F0, false),
        (4, 0, 8, true),
        (4, 8, 12, true),
    ];
    if !returned || accesses != expected {
        return Err(local_paired_proof_error(
            address,
            "normalize memory access contract changed",
        ));
    }
    Ok(())
}

fn emit_call_return_checkpoint(output: &mut String, call_pc: u32) -> Result<(), TranslationError> {
    writeln!(
        output,
        "    galaxy::call_return_checkpoint(services, 0x{call_pc:08X}u, 0x{:08X}u, context, memory);",
        call_pc.wrapping_add(4)
    )
    .map_err(|_| TranslationError::Formatting)
}

fn should_emit_resource_helper_return_trace(pc: u32) -> bool {
    matches!(
        pc,
        0x8041_4104
            | 0x8041_4110
            | 0x8041_4124
            | 0x8041_4148
            | 0x8041_4158
            | 0x8041_41A4
            | 0x8041_41B0
            | 0x8041_41C8
            | 0x8041_41E4
            | 0x8041_41FC
            | 0x8041_4268
            | 0x8041_4298
            | 0x8041_42AC
            | 0x8041_4350
            | 0x8041_436C
            | 0x8041_43EC
            | 0x8041_43F8
            | 0x8041_4410
            | 0x8041_449C
            | 0x8041_4514
            | 0x8041_4568
            | 0x8041_458C
            | 0x8041_45C4
            | 0x8041_45E0
            | 0x8041_45F0
            | 0x8041_4660
            | 0x8041_4688
            | 0x8041_46A0
            | 0x8041_46D0
            | 0x8049_2678
    )
}

#[cfg(test)]
fn emit_direct_guest_call(
    output: &mut String,
    target: u32,
    pc: u32,
    callable_entries: Option<&BTreeMap<u32, u32>>,
) -> Result<(), TranslationError> {
    emit_direct_guest_call_with_exact_fpu(output, target, pc, callable_entries, None)
}

fn emit_direct_guest_call_with_exact_fpu(
    output: &mut String,
    target: u32,
    pc: u32,
    callable_entries: Option<&BTreeMap<u32, u32>>,
    exact_fpu_functions: Option<&BTreeSet<u32>>,
) -> Result<(), TranslationError> {
    // The five direct WPADRead calls to the translated memmove body. Their
    // volatile arguments are saved before the call for the optional
    // native-input causality check; the post-call checkpoint then verifies
    // the copied guest bytes.
    if target == 0x8000_4338
        && matches!(
            pc,
            0x804D_945C | 0x804D_947C | 0x804D_949C | 0x804D_94B0 | 0x804D_94C4
        )
    {
        writeln!(
            output,
            "    galaxy::native_input_trace_copy_pre_checkpoint(services, 0x{pc:08X}u, context, memory);"
        )
        .map_err(|_| TranslationError::Formatting)?;
    }
    fn emit_post_call_hook(
        output: &mut String,
        target: u32,
        pc: u32,
    ) -> Result<(), TranslationError> {
        if target == 0x8045_14EC && pc == 0x8038_D30C {
            writeln!(
                output,
                "    galaxy::trace_thp_video_decode_result(services, context, memory, 0x{pc:08X}u, context->gpr[31]);"
            )
            .map_err(|_| TranslationError::Formatting)?;
        }
        if should_emit_resource_helper_return_trace(pc) {
            writeln!(
                output,
                "    galaxy::trace_resource_helper_return(services, context, memory, 0x{target:08X}u, 0x{pc:08X}u, \"direct\");"
            )
            .map_err(|_| TranslationError::Formatting)?;
        }
        if pc == 0x803A_BDF0 {
            output.push_str(
                "    galaxy::trace_function_async_gameplay_pointer_consumer(services, context, memory);\n",
            );
        }
        Ok(())
    }

    if let Some(owner) = callable_entries
        .and_then(|entries| entries.get(&target))
        .copied()
    {
        let requires_exact_fpu_restart =
            exact_fpu_functions.is_some_and(|functions| functions.contains(&target));
        let native_helper = if requires_exact_fpu_restart {
            None
        } else {
            native_pure_helper(target)
        };
        if !requires_exact_fpu_restart
            && emit_audited_inline_wrapper_call(output, target, callable_entries)?
        {
            emit_post_call_hook(output, target, pc)
        } else if let Some(helper) = native_helper {
            writeln!(
                output,
                "    galaxy::{}(context, memory, services, 0x{target:08X}u);",
                helper.symbol
            )
            .map_err(|_| TranslationError::Formatting)
        } else if target == 0x8039_8D3C && pc == 0x8039_8E7C {
            // The worker's translated call runs normally. The begin marker is
            // observed before the call; completion is emitted at return PC
            // 0x80398E80 by lower_words_with_options so an OSLoadContext/RFI
            // transfer cannot strand it on an unwound C++ stack.
            writeln!(
                output,
                "    galaxy::function_async_trace_callback(services, galaxy::kFunctionAsyncTraceWorkerBegin, context, memory);\n    context->pc = 0x{target:08X}u;\n    rmge01::fn_{owner:08X}(context, memory, services);"
            )
            .map_err(|_| TranslationError::Formatting)?;
            emit_post_call_hook(output, target, pc)
        } else if requires_host_guest_call_at(target, pc) {
            if publishes_movie_boundary_target_pc(target, pc) {
                writeln!(output, "    context->pc = 0x{target:08X}u;")
                    .map_err(|_| TranslationError::Formatting)?;
            }
            writeln!(
                output,
                "    galaxy::call_guest_resolved(services, 0x{target:08X}u, &rmge01::fn_{owner:08X}, context, memory, 0x{pc:08X}u);"
            )
            .map_err(|_| TranslationError::Formatting)?;
            emit_post_call_hook(output, target, pc)
        } else {
            emit_resolved_generated_direct_call(output, target, owner, pc)?;
            emit_post_call_hook(output, target, pc)
        }
    } else {
        writeln!(
            output,
            "    galaxy::call_guest(services, 0x{target:08X}u, context, memory, 0x{pc:08X}u);"
        )
        .map_err(|_| TranslationError::Formatting)
    }
}

// Function entries recorded by the runtime's route-marker diagnostic. Only
// these use the direct-resolved helper: its normal path is a native call, and
// it crosses the host boundary only when NativeServicesV1::runtime_flags has
// the route trace bit or, for the three Mario-control entries, the
// relative-input anchor bit.
fn should_observe_route_marker_generated_direct_call(target: u32) -> bool {
    matches!(
        target,
        0x802A_FD80
            | 0x802B_0D0C
            | 0x802B_0E18
            | 0x802B_0FE4
            | 0x802B_2A28
            | 0x802B_321C
            | 0x802C_AC98
            | 0x802C_AF9C
            | 0x8038_D2E4
            | 0x8038_D34C
            | 0x8038_D868
    )
}

fn emit_resolved_generated_direct_call(
    output: &mut String,
    target: u32,
    owner: u32,
    pc: u32,
) -> Result<(), TranslationError> {
    if should_observe_route_marker_generated_direct_call(target) {
        writeln!(
            output,
            "    galaxy::call_guest_direct_resolved(services, 0x{target:08X}u, &rmge01::fn_{owner:08X}, context, memory, 0x{pc:08X}u);"
        )
    } else {
        writeln!(
            output,
            "    context->pc = 0x{target:08X}u;\n    rmge01::fn_{owner:08X}(context, memory, services);"
        )
    }
    .map_err(|_| TranslationError::Formatting)
}

fn emit_audited_inline_wrapper_call(
    output: &mut String,
    target: u32,
    callable_entries: Option<&BTreeMap<u32, u32>>,
) -> Result<bool, TranslationError> {
    enum Wrapper {
        AddR5ThenTail { addend: u32, tail: u32 },
        SceneObjAccessor { field_offset: u32 },
        SceneObjHolderAccessor { field_offset: u32 },
        LoadR4ThenTail { load_pc: u32, tail: u32 },
        LoadF1R3Plus8ThenTail { load_pc: u32, tail: u32 },
        TailOnly { tail: u32 },
    }

    let wrapper = match target {
        // Tiny table thunks in hot actor/audio paths. Exact generated bodies:
        // `addi r3, r5, 0x58; b <tail>` and `b <tail>`.
        0x8001_7DC8 => Some(Wrapper::AddR5ThenTail {
            addend: 0x58,
            tail: 0x8043_79B4,
        }),
        // Scene object holder accessors. Exact generated bodies:
        // 0x80344AA8: lwz r3, -0x3A78(r13); lwz r3, 0x24(r3);
        //             b 0x8039D44C
        // 0x8039D440: lwz r3, 0xAC(r3); lwz r3, 8(r3); blr
        // 0x8039D44C: lwz r3, 0xAC(r3); lwz r3, 0x10(r3); blr
        0x8034_4AA8 => Some(Wrapper::SceneObjHolderAccessor { field_offset: 0x10 }),
        0x8039_D440 => Some(Wrapper::SceneObjAccessor { field_offset: 0x08 }),
        0x8039_D44C => Some(Wrapper::SceneObjAccessor { field_offset: 0x10 }),
        // NPC/message delegate wrappers in RMGE01. Each wrapper is exactly
        // `lwz r3, 0(r4); b <shared implementation>`.
        0x8027_4A1C | 0x8027_4A24 => Some(Wrapper::LoadR4ThenTail {
            load_pc: target,
            tail: 0x8027_3ADC,
        }),
        0x8027_4A2C => Some(Wrapper::LoadR4ThenTail {
            load_pc: 0x8027_4A2C,
            tail: 0x8027_3C24,
        }),
        0x8027_4A34 | 0x8027_4A3C => Some(Wrapper::LoadR4ThenTail {
            load_pc: target,
            tail: 0x8027_3938,
        }),
        0x8027_4A48 => Some(Wrapper::LoadR4ThenTail {
            load_pc: 0x8027_4A48,
            tail: 0x8027_3790,
        }),
        // J3D frame getter wrapper: `lfs f1, 8(r3); b 0x80433590`.
        0x8044_0CDC => Some(Wrapper::LoadF1R3Plus8ThenTail {
            load_pc: 0x8044_0CDC,
            tail: 0x8043_3590,
        }),
        0x8049_5694 => Some(Wrapper::TailOnly { tail: 0x8049_5EA0 }),
        _ => None,
    };

    let Some(wrapper) = wrapper else {
        return Ok(false);
    };
    let Some(entries) = callable_entries else {
        return Ok(false);
    };
    let tail = match wrapper {
        Wrapper::AddR5ThenTail { tail, .. }
        | Wrapper::LoadR4ThenTail { tail, .. }
        | Wrapper::LoadF1R3Plus8ThenTail { tail, .. }
        | Wrapper::TailOnly { tail } => tail,
        Wrapper::SceneObjAccessor { .. } | Wrapper::SceneObjHolderAccessor { .. } => 0,
    };
    let owner = if tail != 0 {
        let Some(owner) = entries.get(&tail).copied() else {
            return Ok(false);
        };
        owner
    } else {
        0
    };

    match wrapper {
        Wrapper::AddR5ThenTail { addend, .. } => {
            writeln!(
                output,
                "    context->gpr[3] = context->gpr[5] + 0x{addend:08X}u;"
            )
            .map_err(|_| TranslationError::Formatting)?;
        }
        Wrapper::SceneObjAccessor { field_offset } => {
            writeln!(
                output,
                "    context->pc = 0x{target:08X}u;\n    context->gpr[3] = galaxy::guest_load_u32(memory, context->gpr[3] + 0x000000ACu, services, 0x{target:08X}u);\n    context->gpr[3] = galaxy::guest_load_u32(memory, context->gpr[3] + 0x{field_offset:08X}u, services, 0x{:08X}u);",
                target + 4
            )
            .map_err(|_| TranslationError::Formatting)?;
            return Ok(true);
        }
        Wrapper::SceneObjHolderAccessor { field_offset } => {
            writeln!(
                output,
                "    context->gpr[3] = galaxy::guest_load_u32(memory, context->gpr[13] + 0xFFFFC588u, services, 0x80344AA8u);\n    context->gpr[3] = galaxy::guest_load_u32(memory, context->gpr[3] + 0x00000024u, services, 0x80344AACu);\n    context->pc = 0x8039D44Cu;\n    context->gpr[3] = galaxy::guest_load_u32(memory, context->gpr[3] + 0x000000ACu, services, 0x8039D44Cu);\n    context->gpr[3] = galaxy::guest_load_u32(memory, context->gpr[3] + 0x{field_offset:08X}u, services, 0x8039D450u);"
            )
            .map_err(|_| TranslationError::Formatting)?;
            return Ok(true);
        }
        Wrapper::LoadR4ThenTail { load_pc, .. } => {
            writeln!(
                output,
                "    context->gpr[3] = galaxy::guest_load_u32(memory, context->gpr[4], services, 0x{load_pc:08X}u);"
            )
            .map_err(|_| TranslationError::Formatting)?;
        }
        Wrapper::LoadF1R3Plus8ThenTail { load_pc, .. } => {
            writeln!(
                output,
                "    galaxy::load_fpr_single(context, 1u, memory, context->gpr[3] + 0x00000008u, services, 0x{load_pc:08X}u);"
            )
            .map_err(|_| TranslationError::Formatting)?;
        }
        Wrapper::TailOnly { .. } => {}
    }
    writeln!(
        output,
        "    context->pc = 0x{tail:08X}u;\n    rmge01::fn_{owner:08X}(context, memory, services);"
    )
    .map_err(|_| TranslationError::Formatting)?;
    Ok(true)
}

fn audited_inline_wrapper_uses_fpu(target: u32) -> bool {
    target == 0x8044_0CDC
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct NativePureHelper {
    symbol: &'static str,
    uses_fpu: bool,
}

impl NativePureHelper {
    const fn integer(symbol: &'static str) -> Self {
        Self {
            symbol,
            uses_fpu: false,
        }
    }

    const fn fpu(symbol: &'static str) -> Self {
        Self {
            symbol,
            uses_fpu: true,
        }
    }
}

fn native_pure_helper(target: u32) -> Option<NativePureHelper> {
    match target {
        0x8000_72B4 => Some(NativePureHelper::integer(
            "native_read_next_char_utf16_800072B4",
        )),
        0x8009_7278 => Some(NativePureHelper::integer("native_add_12_80097278")),
        0x8001_CF80 => Some(NativePureHelper::fpu("native_vec_add_8001CF80")),
        0x8001_6F80 | 0x8001_8BC4 => Some(NativePureHelper::fpu("native_vec_set_from_fprs")),
        0x8001_FD6C => Some(NativePureHelper::fpu("native_vec_scale_8001FD6C")),
        0x803A_33AC => Some(NativePureHelper::integer("native_noop_803A33AC")),
        0x803A_387C => Some(NativePureHelper::fpu("native_mtx_scale_803A387C")),
        0x8034_4A54 => Some(NativePureHelper::integer(
            "native_indexed_word_load_80344A54",
        )),
        // WPad update functions are not replaced; native input enters at the
        // raw-report boundary. 0x803E4D24 is not substituted: its body
        // tail-calls the FP routine at 0x804B6BCC, and only that generated
        // boundary can raise exception 7 after the wrapper's `mr`.
        0x803E_595C => Some(NativePureHelper::fpu(
            "native_vec3_abs_le_threshold_803E595C",
        )),
        0x8044_3D78 => Some(NativePureHelper::integer("native_jpa_list_remove_80443D78")),
        0x8044_3E24 => Some(NativePureHelper::integer("native_jpa_list_append_80443E24")),
        0x8044_65A0 => Some(NativePureHelper::fpu("native_jpa_alpha_callback_804465A0")),
        0x8044_65CC => Some(NativePureHelper::fpu("native_jpa_scale_callback_804465CC")),
        0x8044_7264 => Some(NativePureHelper::integer(
            "native_jpa_actor_flags_mask_80447264",
        )),
        0x8044_727C => Some(NativePureHelper::integer("native_jpa_random_i16_8044727C")),
        0x8044_8D54 => Some(NativePureHelper::integer("native_jpa_actor_check_80448D54")),
        0x8044_8D94 => Some(NativePureHelper::integer("native_jpa_actor_check_80448D94")),
        0x804B_5EDC => Some(NativePureHelper::fpu("native_psmtx_identity_804B5EDC")),
        0x804B_5F08 => Some(NativePureHelper::fpu("native_psmtx_copy_804B5F08")),
        0x804B_63D8 => Some(NativePureHelper::fpu("native_psmtx_trans_apply_804B63D8")),
        0x804B_6424 => Some(NativePureHelper::fpu("native_psmtx_scale_804B6424")),
        0x804B_6BCC => Some(NativePureHelper::fpu("native_psvec_normalize_804B6BCC")),
        0x804B_6CB8 => Some(NativePureHelper::fpu("native_psvec_cross_product")),
        0x804B_D28C => Some(NativePureHelper::integer(
            "native_gx_set_chan_color_804BD28C",
        )),
        // Stateful functions with nested calls or interruptible loops are not
        // substituted: a hand-written replacement would skip call-return
        // checkpoints and resume-visible CR/GPR effects.
        0x804B_E1D8 => Some(NativePureHelper::fpu("native_gx_load_pos_mtx_imm_804BE1D8")),
        0x804B_E59C => Some(NativePureHelper::integer("native_gx_set_array_804BE59C")),
        0x8042_E100 => Some(NativePureHelper::integer("native_stride6_offset8_8042E100")),
        0x8051_73E0 => Some(NativePureHelper::integer("native_ptmf_scall_805173E0")),
        target
            if (0x8051_74FC..=0x8051_7538).contains(&target)
                && ((target - 0x8051_74FC) & 0x3) == 0 =>
        {
            Some(NativePureHelper::integer("native_savegpr_805174FC"))
        }
        target
            if (0x8051_7548..=0x8051_7584).contains(&target)
                && ((target - 0x8051_7548) & 0x3) == 0 =>
        {
            Some(NativePureHelper::integer("native_restgpr_80517548"))
        }
        _ => None,
    }
}

// The MoviePlayerSimple draw/stop boundaries publish their target PC before
// the host boundary.
fn publishes_movie_boundary_target_pc(target: u32, pc: u32) -> bool {
    (target == 0x8038_D1CC && pc == 0x8036_FDD4)
        || (target == 0x8037_0398 && matches!(pc, 0x8036_FEC8 | 0x8037_0278))
}

fn requires_host_guest_call_at(target: u32, pc: u32) -> bool {
    requires_host_guest_call(target)
        || (target == 0x804B_9EC8 && pc == 0x8038_5AD8)
        || (target == 0x803A_29B8 && pc == 0x8034_BF8C)
        || (target == 0x8038_D1CC && pc == 0x8036_FDD4)
        || (target == 0x8037_0398 && matches!(pc, 0x8036_FEC8 | 0x8037_0278))
}

fn requires_host_guest_call(target: u32) -> bool {
    if !(0x8000_0000..0x8180_0000).contains(&target) {
        return true;
    }
    if (target >> 16) == 0x8041 {
        return true;
    }
    matches!(
        target,
        // Runtime OS/device/input/bring-up intercepts.
        0x0000_0000
            | 0x8038_5034 // Mouse pointer position/depth transaction; original body kept.
            | 0x8038_52BC // Restore previous position before velocity/history update.
            | 0x8038_BC80
            | 0x8038_D2E4 // Bounded THP video wrapper HLE; interior entry stays AOT.
            | 0x8039_81A0
            | 0x8039_9058
            | 0x8039_90A8
            | 0x8039_9144
            | 0x8039_9280
            | 0x8039_B9E0
            | 0x8039_EF50
            | 0x803C_E128
            | 0x803F_81AC
            | 0x8040_77BC
            | 0x8040_9F48
            | 0x8040_A2B4
            | 0x8040_ABF4
            | 0x8040_ACE0
            | 0x8040_CA60
            | 0x8041_071C
            | 0x8041_8440
            | 0x8045_06D8 // KPADRead: synthetic pointer/status host boundary
            | 0x804A_095C
            | 0x804A_096C
            | 0x804A_2FD8
            | 0x804A_3734
            | 0x804A_379C
            | 0x804A_381C
            | 0x804A_3E94
            | 0x804A_3F24
            | 0x804A_6E20
            | 0x804A_84F8
            | 0x804A_8A0C
            | 0x804A_B488
            | 0x804A_BFBC
            | 0x804A_C380
            | 0x804B_1E38
            | 0x804B_1E7C
            | 0x804B_2730
            | 0x804B_EF5C
            | 0x804B_F2CC
            | 0x804C_EE44
            | 0x804E_CA00
            | 0x804F_5500
            | 0x8052_B490
    )
}

fn emit_cached_guest_call(
    output: &mut String,
    target_expression: &str,
    pc: u32,
) -> Result<(), TranslationError> {
    writeln!(
        output,
        "    static std::uint32_t cached_target_{pc:08X} = 0u;"
    )
    .map_err(|_| TranslationError::Formatting)?;
    writeln!(
        output,
        "    static galaxy::NativeGameFunction cached_function_{pc:08X} = nullptr;"
    )
    .map_err(|_| TranslationError::Formatting)?;
    writeln!(
        output,
        "    galaxy::call_guest_cached(services, {target_expression}, &cached_target_{pc:08X}, &cached_function_{pc:08X}, context, memory, 0x{pc:08X}u);"
    )
    .map_err(|_| TranslationError::Formatting)
}

fn native_function_body(address: u32) -> Option<&'static str> {
    match address {
        0x8000_72B4 => Some(
            "    galaxy::native_read_next_char_utf16_800072B4(context, memory, services, 0x800072B4u);\n    return;\n",
        ),
        0x8009_7278 => Some(
            "    galaxy::native_add_12_80097278(context, memory, services, 0x80097278u);\n    return;\n",
        ),
        0x803A_33AC => Some("    galaxy::native_noop_803A33AC(context, memory, services, 0x803A33ACu);\n    return;\n"),
        0x803A_387C => Some(
            "    galaxy::native_mtx_scale_803A387C(context, memory, services, 0x803A387Cu);\n    return;\n",
        ),
        0x8001_CF80 => Some(
            "    galaxy::native_vec_add_8001CF80(context, memory, services, 0x8001CF80u);\n    return;\n",
        ),
        0x8001_FD6C => Some(
            "    galaxy::native_vec_scale_8001FD6C(context, memory, services, 0x8001FD6Cu);\n    return;\n",
        ),
        0x8034_4A54 => Some(
            "    galaxy::native_indexed_word_load_80344A54(context, memory, services, 0x80344A54u);\n    return;\n",
        ),
        // WPad update functions stay translated; native device bytes enter
        // through the raw-report boundary.
        0x803E_595C => Some(
            "    galaxy::native_vec3_abs_le_threshold_803E595C(context, memory, services, 0x803E595Cu);\n    return;\n",
        ),
        0x8044_3D78 => Some(
            "    galaxy::native_jpa_list_remove_80443D78(context, memory, services, 0x80443D78u);\n    return;\n",
        ),
        0x8044_3E24 => Some(
            "    galaxy::native_jpa_list_append_80443E24(context, memory, services, 0x80443E24u);\n    return;\n",
        ),
        0x8044_65A0 => Some(
            "    galaxy::native_jpa_alpha_callback_804465A0(context, memory, services, 0x804465A0u);\n    return;\n",
        ),
        0x8044_65CC => Some(
            "    galaxy::native_jpa_scale_callback_804465CC(context, memory, services, 0x804465CCu);\n    return;\n",
        ),
        0x8044_7264 => Some(
            "    galaxy::native_jpa_actor_flags_mask_80447264(context, memory, services, 0x80447264u);\n    return;\n",
        ),
        0x8044_727C => Some(
            "    galaxy::native_jpa_random_i16_8044727C(context, memory, services, 0x8044727Cu);\n    return;\n",
        ),
        0x8044_8D54 => Some(
            "    galaxy::native_jpa_actor_check_80448D54(context, memory, services, 0x80448D54u);\n    return;\n",
        ),
        0x8044_8D94 => Some(
            "    galaxy::native_jpa_actor_check_80448D94(context, memory, services, 0x80448D94u);\n    return;\n",
        ),
        0x804B_5EDC => Some(
            "    galaxy::native_psmtx_identity_804B5EDC(context, memory, services, 0x804B5EDCu);\n    return;\n",
        ),
        0x804B_5F08 => Some(
            "    galaxy::native_psmtx_copy_804B5F08(context, memory, services, 0x804B5F08u);\n    return;\n",
        ),
        0x804B_63D8 => Some(
            "    galaxy::native_psmtx_trans_apply_804B63D8(context, memory, services, 0x804B63D8u);\n    return;\n",
        ),
        0x804B_6424 => Some(
            "    galaxy::native_psmtx_scale_804B6424(context, memory, services, 0x804B6424u);\n    return;\n",
        ),
        0x804B_6BCC => Some(
            "    galaxy::native_psvec_normalize_804B6BCC(context, memory, services, 0x804B6BCCu);\n    return;\n",
        ),
        0x804B_E1D8 => Some(
            "    galaxy::native_gx_load_pos_mtx_imm_804BE1D8(context, memory, services, 0x804BE1D8u);\n    return;\n",
        ),
        0x804B_E59C => Some(
            "    galaxy::native_gx_set_array_804BE59C(context, memory, services, 0x804BE59Cu);\n    return;\n",
        ),
        0x8042_E100 => Some(
            "    galaxy::native_stride6_offset8_8042E100(context, memory, services, 0x8042E100u);\n    return;\n",
        ),
        0x8051_73E0 => Some(
            "    galaxy::native_ptmf_scall_805173E0(context, memory, services, 0x805173E0u);\n    return;\n",
        ),
        0x8051_74FC => Some(
            "    galaxy::native_savegpr_805174FC(context, memory, services, context->pc);\n    return;\n",
        ),
        0x8051_7548 => Some(
            "    galaxy::native_restgpr_80517548(context, memory, services, context->pc);\n    return;\n",
        ),
        _ => None,
    }
}

fn should_emit_audio_trace_hook(address: u32) -> bool {
    matches!(
        address,
        0x8002_5FC0
            | 0x8002_6158
            | 0x8002_6334
            | 0x8002_6C34
            | 0x8002_6D54
            | 0x8002_A72C
            | 0x8002_A744
            | 0x8002_A770
            | 0x8002_FC48
            | 0x8002_FD04
            | 0x8003_3238
            | 0x8003_34EC
            | 0x8039_4B78
            | 0x8039_4E54
            | 0x8039_4E64
            | 0x8039_B1C4
            | 0x8039_B1E8
            | 0x8039_B938
            | 0x8039_E3D4
            | 0x803F_3088
            | 0x803F_3160
            | 0x803F_8638
            | 0x803F_8860
            | 0x803F_88B8
            | 0x803F_8910
            | 0x803F_8980
            | 0x803F_89D8
            | 0x803F_9358
            | 0x803F_9580
            | 0x803F_9868
            | 0x803F_9DEC
            | 0x8048_78BC
            | 0x8048_9C1C
            | 0x8048_9CEC
            | 0x8048_D8CC
            | 0x8048_D970
            | 0x8049_2A08
            | 0x8049_2A78
            | 0x8049_45CC
            | 0x8049_5038
            | 0x8049_5150
            | 0x8049_51BC
            | 0x8049_5354
            | 0x8049_559C
            | 0x8049_58E0
            | 0x8049_5900
            | 0x8049_5964
            | 0x8049_5970
            | 0x8049_5980
            | 0x8049_599C
            | 0x8049_5AB4
            | 0x8049_80F0
            | 0x8049_8830
            | 0x8049_9208
            | 0x8049_9358
            | 0x8049_94F8
            | 0x8049_AEF8
            | 0x8049_EFF4
            | 0x8049_F130
            | 0x8049_F170
    )
}

fn should_sample_audio_trace_hook(address: u32) -> bool {
    matches!(
        address,
        0x8002_6C34 | 0x8002_FC48 | 0x8049_9358 | 0x8049_94F8 | 0x8049_F130 | 0x8049_F170
    )
}

fn emit_audio_trace_hook(output: &mut String, address: u32) -> Result<(), TranslationError> {
    if !should_emit_audio_trace_hook(address) {
        return Ok(());
    }
    if should_sample_audio_trace_hook(address) {
        writeln!(
            output,
            "    if (galaxy::trace_audio_function_entries_enabled()) {{\n        static std::atomic<std::uint32_t> trace_audio_entry_count{{0u}};\n        const std::uint32_t trace_audio_entry_index =\n            trace_audio_entry_count.fetch_add(1u, std::memory_order_relaxed);\n        if (trace_audio_entry_index < 32u ||\n            (trace_audio_entry_index % 512u) == 0u) {{\n            galaxy::trace_audio_function_entry(\n                services, context, memory, 0x{address:08X}u);\n        }}\n    }}"
        )
        .map_err(|_| TranslationError::Formatting)?;
    } else {
        writeln!(
            output,
            "    galaxy::trace_audio_function_entry(services, context, memory, 0x{address:08X}u);"
        )
        .map_err(|_| TranslationError::Formatting)?;
    }
    Ok(())
}

fn should_emit_main_frame_trace_hook(address: u32) -> bool {
    matches!(
        address,
        // The first six entries mark the frame cadence; the rest are the
        // direct subcall boundaries inside GameSystem::frameLoop's work phase.
        0x8039_9AF0
            | 0x8039_9B58
            | 0x8039_D3D4
            | 0x8039_DBE4
            | 0x803B_462C
            | 0x803B_5478
            | 0x803B_7818
            | 0x8039_B9B0
            | 0x8038_5758
            | 0x8039_B9B8
            | 0x8039_A288
            | 0x8039_FD20
            | 0x8039_FEE0
            | 0x8039_FF14
            | 0x803A_007C
            | 0x803A_00FC
    )
}

fn emit_main_frame_trace_hook(output: &mut String, address: u32) -> Result<(), TranslationError> {
    if !should_emit_main_frame_trace_hook(address) {
        return Ok(());
    }
    writeln!(
        output,
        "    if (context->pc == 0x{address:08X}u) {{\n        galaxy::trace_main_frame_function_entry(services, 0x{address:08X}u);\n    }}"
    )
    .map_err(|_| TranslationError::Formatting)
}

// Continuations after direct calls in the GameScene execute/draw3D path
// reached from the main-frame virtual call. They are continuation PCs because
// a call-return checkpoint can resume there after an OS context transfer,
// skipping any "after call" C++ statement. Diagnostic only; they do not
// affect guest dispatch. 803A0154 is a shared join, not proof that a mode-3
// call completed: pair it with the matching 803A0150 begin in the same guest
// frame/thread.
const END_FRAME_SPAN_LABELS: [u32; 8] = [
    0x803A_0140,
    0x803A_0144,
    0x803A_0148,
    0x803A_014C,
    0x803A_0150,
    0x803A_0154,
    0x803A_093C,
    0x803A_0940,
];

fn insert_end_frame_span_markers(
    files: &mut [GeneratedSource],
    shard_source_bytes: usize,
) -> Result<(), TranslationError> {
    // Runs after ordinary sharding so diagnostic bytes cannot change later
    // shard membership. All eight labels must be in one shard.
    let mut shard_index = None;
    for pc in END_FRAME_SPAN_LABELS {
        let label = format!("\nlabel_{pc:08X}:\n");
        let matches = files
            .iter()
            .enumerate()
            .filter(|(_, file)| {
                file.name.starts_with("functions_")
                    && file.name.ends_with(".cpp")
                    && file.contents.contains(&label)
            })
            .collect::<Vec<_>>();
        if matches.len() != 1 || matches[0].1.contents.matches(&label).count() != 1 {
            return Err(TranslationError::EndFrameSpanTrace(format!(
                "label {pc:08X} missing or ambiguous"
            )));
        }
        let index = matches[0].0;
        if shard_index.is_some_and(|expected| expected != index) {
            return Err(TranslationError::EndFrameSpanTrace(
                "selected labels span multiple shards".to_owned(),
            ));
        }
        shard_index = Some(index);
    }
    let file = &mut files[shard_index.expect("nonempty fixed marker list")];
    let mut traced = file.contents.clone();
    for pc in END_FRAME_SPAN_LABELS {
        let label = format!("\nlabel_{pc:08X}:\n");
        let hook = format!(
            "{label}    galaxy::trace_main_frame_stage(services, \"__galaxy_main_frame_stage:end-frame-{pc:08X}\");\n"
        );
        traced = traced.replacen(&label, &hook, 1);
    }
    if traced.len() > shard_source_bytes {
        return Err(TranslationError::EndFrameSpanTrace(format!(
            "fixed shard {} requires {} bytes, ceiling {}",
            file.name,
            traced.len(),
            shard_source_bytes
        )));
    }
    file.contents = traced;
    Ok(())
}

// Fixed diagnostic call metadata, not a replacement for guest instructions.
// After markers occur at both normal and resumed checkpoint completion sites.
// They deliberately do not occur at shared labels 8033E8A4/8033E8C0, which can
// also be reached without executing the preceding call.
const SCENE_UPDATE_CALLS: [(u32, u32); 8] = [
    (0x8033_E860, 0x803F_7E40),
    (0x8033_E86C, 0x8037_1174),
    (0x8033_E880, 0x803A_29B8),
    (0x8033_E898, 0x8034_3DC4),
    (0x8033_E8A0, 0x8034_3D2C),
    (0x8033_E8AC, 0x803A_29B8),
    (0x8033_E8BC, 0x8034_4858),
    (0x8033_E8C0, 0x8034_3DCC),
];

fn insert_scene_update_span_markers(
    files: &mut [GeneratedSource],
    shard_source_bytes: usize,
) -> Result<(), TranslationError> {
    const OWNER: &str = "\nvoid fn_8033E844(";
    let owners = files
        .iter()
        .enumerate()
        .filter(|(_, file)| {
            file.name.starts_with("functions_")
                && file.name.ends_with(".cpp")
                && file.contents.contains(OWNER)
        })
        .map(|(index, _)| index)
        .collect::<Vec<_>>();
    if owners.len() != 1 || files[owners[0]].contents.matches(OWNER).count() != 1 {
        return Err(TranslationError::SceneUpdateSpanTrace(
            "owner missing or ambiguous".to_owned(),
        ));
    }
    let file = &mut files[owners[0]];
    let start = file.contents.find(OWNER).expect("validated owner");
    let end = file.contents[start + OWNER.len()..]
        .find("\nvoid fn_")
        .map_or(file.contents.len(), |offset| start + OWNER.len() + offset);
    let original = &file.contents[start..end];
    let mut traced = original.to_owned();
    for (call_pc, target) in SCENE_UPDATE_CALLS {
        let return_pc = call_pc + 4;
        let checkpoint = format!(
            "    galaxy::call_return_checkpoint(services, 0x{call_pc:08X}u, 0x{return_pc:08X}u, context, memory);\n"
        );
        let call = format!(
            "    context->lr = 0x{return_pc:08X}u;\n    context->pc = 0x{target:08X}u;\n    rmge01::fn_{target:08X}(context, memory, services);\n"
        );
        let normal = format!("\n    {{\n{call}{checkpoint}    }}\n");
        let resumed = format!(
            "\ncall_return_{return_pc:08X}:\n{checkpoint}    goto label_{return_pc:08X};\n"
        );
        if original.matches(&normal).count() != 1
            || original.matches(&resumed).count() != 1
            || original.matches(&checkpoint).count() != 2
            || original
                .matches(&format!("\nlabel_{return_pc:08X}:\n"))
                .count()
                != 1
        {
            return Err(TranslationError::SceneUpdateSpanTrace(format!(
                "call {call_pc:08X} lacks unique normal/resumed checkpoint anchors"
            )));
        }
        let before = format!(
            "    galaxy::trace_main_frame_stage(services, \"__galaxy_main_frame_stage:scene-update-before-{call_pc:08X}\");\n"
        );
        let after = format!(
            "    galaxy::trace_main_frame_stage(services, \"__galaxy_main_frame_stage:scene-update-after-{return_pc:08X}\");\n"
        );
        traced = traced.replacen(
            &normal,
            &format!("\n    {{\n{before}{call}{checkpoint}{after}    }}\n"),
            1,
        );
        traced = traced.replacen(
            &resumed,
            &format!(
                "\ncall_return_{return_pc:08X}:\n{checkpoint}{after}    goto label_{return_pc:08X};\n"
            ),
            1,
        );
    }
    // Only commit after all anchors and the final original-shard ceiling have
    // passed. Never regroup later functions to conceal diagnostic growth.
    let updated = format!(
        "{}{}{}",
        &file.contents[..start],
        traced,
        &file.contents[end..]
    );
    if updated.len() > shard_source_bytes {
        return Err(TranslationError::SceneUpdateSpanTrace(format!(
            "fixed shard {} requires {} bytes, ceiling {}",
            file.name,
            updated.len(),
            shard_source_bytes
        )));
    }
    file.contents = updated;
    Ok(())
}

// Opt-in diagnostic coverage for the statically resolved children of the hot
// particle-direction routine. The three indirect callbacks already pass
// through call_guest_cached and therefore already reach the aggregate
// direct-edge profiler. These ten edges ordinarily remain bare C++ calls.
// Routing only their generated call expressions through the inline resolved
// helper preserves the original translated callee and the separate mandatory
// return checkpoint while making the existing bounded profiler observe them.
const PARTICLE_DIRECTION_DIRECT_CALLS: [(u32, u32); 10] = [
    (0x803A_3BC0, 0x803E_595C),
    (0x803A_3BD0, 0x803E_4D24),
    (0x803A_3BE0, 0x804B_6CB8),
    (0x803A_3BEC, 0x803E_595C),
    (0x803A_3BFC, 0x803E_4D24),
    (0x803A_3C0C, 0x804B_6CB8),
    (0x803A_3C14, 0x803E_4D24),
    (0x803A_3CB8, 0x804B_5F3C),
    (0x803A_3CC4, 0x804B_E1D8),
    (0x803A_3CFC, 0x804B_DEA8),
];

fn insert_particle_direction_edge_profile(
    files: &mut [GeneratedSource],
    shard_source_bytes: usize,
) -> Result<(), TranslationError> {
    const OWNER: &str = "\nvoid fn_803A3B6C(";
    let owners = files
        .iter()
        .enumerate()
        .filter(|(_, file)| {
            file.name.starts_with("functions_")
                && file.name.ends_with(".cpp")
                && file.contents.contains(OWNER)
        })
        .map(|(index, _)| index)
        .collect::<Vec<_>>();
    if owners.len() != 1 || files[owners[0]].contents.matches(OWNER).count() != 1 {
        return Err(TranslationError::ParticleDirectionEdgeProfile(
            "owner missing or ambiguous".to_owned(),
        ));
    }

    let file = &mut files[owners[0]];
    let start = file.contents.find(OWNER).expect("validated owner");
    let end = file.contents[start + OWNER.len()..]
        .find("\nvoid fn_")
        .map_or(file.contents.len(), |offset| start + OWNER.len() + offset);
    let original = &file.contents[start..end];
    let mut profiled = original.to_owned();

    for (call_pc, target) in PARTICLE_DIRECTION_DIRECT_CALLS {
        let return_pc = call_pc + 4;
        let call = format!(
            "    context->lr = 0x{return_pc:08X}u;\n    context->pc = 0x{target:08X}u;\n    rmge01::fn_{target:08X}(context, memory, services);\n"
        );
        let checkpoint = format!(
            "    galaxy::call_return_checkpoint(services, 0x{call_pc:08X}u, 0x{return_pc:08X}u, context, memory);\n"
        );
        if original.matches(&call).count() != 1
            || original.matches(&checkpoint).count() != 2
            || original
                .matches(&format!("\ncall_return_{return_pc:08X}:\n"))
                .count()
                != 1
        {
            return Err(TranslationError::ParticleDirectionEdgeProfile(format!(
                "call {call_pc:08X}->{target:08X} lacks unique direct-call/resume anchors"
            )));
        }
        let replacement = format!(
            "    context->lr = 0x{return_pc:08X}u;\n    galaxy::call_guest_direct_resolved(services, 0x{target:08X}u, &rmge01::fn_{target:08X}, context, memory, 0x{call_pc:08X}u);\n"
        );
        profiled = profiled.replacen(&call, &replacement, 1);
    }

    let updated = format!(
        "{}{}{}",
        &file.contents[..start],
        profiled,
        &file.contents[end..]
    );
    if updated.len() > shard_source_bytes {
        return Err(TranslationError::ParticleDirectionEdgeProfile(format!(
            "fixed shard {} requires {} bytes, ceiling {}",
            file.name,
            updated.len(),
            shard_source_bytes
        )));
    }
    file.contents = updated;
    Ok(())
}

// Opt-in diagnostic coverage for every statically resolved child of the hot
// NW4R material-setup function. Keeping this list explicit makes regenerated
// code fail closed if the supported DOL, lowering shape, or call graph changes.
const NW4R_MATERIAL_SETUP_DIRECT_CALLS: [(u32, u32); 77] = [
    (0x8001_312C, 0x804B_BAD4),
    (0x8001_317C, 0x804B_BAF8),
    (0x8001_319C, 0x804B_BAF8),
    (0x8001_31FC, 0x804B_BAF8),
    (0x8001_3298, 0x8001_56DC),
    (0x8001_32D4, 0x804B_B9FC),
    (0x8001_332C, 0x804B_9A70),
    (0x8001_33B4, 0x804B_9848),
    (0x8001_3414, 0x8001_13E0),
    (0x8001_3430, 0x804B_E2A0),
    (0x8001_3474, 0x8001_4138),
    (0x8001_34B0, 0x804B_C0EC),
    (0x8001_34BC, 0x8001_4224),
    (0x8001_34C8, 0x804B_C488),
    (0x8001_34D4, 0x804B_C40C),
    (0x8001_3518, 0x804B_D2EC),
    (0x8001_3544, 0x804B_D2EC),
    (0x8001_3570, 0x804B_D2EC),
    (0x8001_359C, 0x804B_D350),
    (0x8001_35C8, 0x804B_D350),
    (0x8001_35F4, 0x804B_D350),
    (0x8001_3620, 0x804B_D350),
    (0x8001_3680, 0x804B_D488),
    (0x8001_36AC, 0x804B_D488),
    (0x8001_36C4, 0x804B_D488),
    (0x8001_36DC, 0x804B_D488),
    (0x8001_36F4, 0x804B_D488),
    (0x8001_3714, 0x804B_D724),
    (0x8001_37A0, 0x804B_D5C8),
    (0x8001_37B4, 0x804B_D44C),
    (0x8001_37D4, 0x804B_D15C),
    (0x8001_37F8, 0x804B_D1DC),
    (0x8001_3808, 0x804B_D3AC),
    (0x8001_3828, 0x804B_D19C),
    (0x8001_384C, 0x804B_D234),
    (0x8001_385C, 0x804B_D3FC),
    (0x8001_38A0, 0x804B_CC28),
    (0x8001_38FC, 0x804B_D5C8),
    (0x8001_3914, 0x804B_D15C),
    (0x8001_392C, 0x804B_D19C),
    (0x8001_3954, 0x804B_D5C8),
    (0x8001_396C, 0x804B_D15C),
    (0x8001_3984, 0x804B_D19C),
    (0x8001_39A8, 0x804B_D5C8),
    (0x8001_39C0, 0x804B_D15C),
    (0x8001_39D8, 0x804B_D19C),
    (0x8001_39EC, 0x804B_D5C8),
    (0x8001_3A04, 0x804B_D15C),
    (0x8001_3A1C, 0x804B_D19C),
    (0x8001_3A2C, 0x804B_D3AC),
    (0x8001_3A3C, 0x804B_D3FC),
    (0x8001_3A74, 0x804B_D5C8),
    (0x8001_3AA8, 0x804B_D15C),
    (0x8001_3AC0, 0x804B_D19C),
    (0x8001_3ACC, 0x804B_D3AC),
    (0x8001_3AD8, 0x804B_D3FC),
    (0x8001_3BA4, 0x804B_D5C8),
    (0x8001_3BBC, 0x804B_D15C),
    (0x8001_3BD4, 0x804B_D19C),
    (0x8001_3BF8, 0x804B_D5C8),
    (0x8001_3C10, 0x804B_D15C),
    (0x8001_3C28, 0x804B_D19C),
    (0x8001_3C58, 0x804B_D1DC),
    (0x8001_3C74, 0x804B_D234),
    (0x8001_3C7C, 0x804B_CFD4),
    (0x8001_3C8C, 0x804B_D44C),
    (0x8001_3CA4, 0x804B_D724),
    (0x8001_3D2C, 0x8000_BC0C),
    (0x8001_3D3C, 0x8000_BBA0),
    (0x8001_3D8C, 0x8001_14E8),
    (0x8001_3DB4, 0x804B_CFB4),
    (0x8001_3E28, 0x804B_CEE8),
    (0x8001_3E38, 0x804B_CDE4),
    (0x8001_3EB4, 0x804B_D504),
    (0x8001_3ED0, 0x804B_D504),
    (0x8001_3F34, 0x804B_DA98),
    (0x8001_3F4C, 0x804B_DA98),
];

fn insert_nw4r_material_setup_edge_profile(
    files: &mut [GeneratedSource],
    shard_source_bytes: usize,
) -> Result<(), TranslationError> {
    const OWNER: &str = "\nvoid fn_800130F0(";
    let owners = files
        .iter()
        .enumerate()
        .filter(|(_, file)| {
            file.name.starts_with("functions_")
                && file.name.ends_with(".cpp")
                && file.contents.contains(OWNER)
        })
        .map(|(index, _)| index)
        .collect::<Vec<_>>();
    if owners.len() != 1 || files[owners[0]].contents.matches(OWNER).count() != 1 {
        return Err(TranslationError::Nw4rMaterialSetupEdgeProfile(
            "owner missing or ambiguous".to_owned(),
        ));
    }

    let file = &mut files[owners[0]];
    let start = file.contents.find(OWNER).expect("validated owner");
    let end = file.contents[start + OWNER.len()..]
        .find("\nvoid fn_")
        .map_or(file.contents.len(), |offset| start + OWNER.len() + offset);
    let original = &file.contents[start..end];
    let mut profiled = original.to_owned();

    for (call_pc, target) in NW4R_MATERIAL_SETUP_DIRECT_CALLS {
        let return_pc = call_pc + 4;
        let call = format!(
            "    context->lr = 0x{return_pc:08X}u;\n    context->pc = 0x{target:08X}u;\n    rmge01::fn_{target:08X}(context, memory, services);\n"
        );
        let checkpoint = format!(
            "    galaxy::call_return_checkpoint(services, 0x{call_pc:08X}u, 0x{return_pc:08X}u, context, memory);\n"
        );
        if original.matches(&call).count() != 1
            || original.matches(&checkpoint).count() != 2
            || original
                .matches(&format!("\ncall_return_{return_pc:08X}:\n"))
                .count()
                != 1
        {
            return Err(TranslationError::Nw4rMaterialSetupEdgeProfile(format!(
                "call {call_pc:08X}->{target:08X} lacks unique direct-call/resume anchors"
            )));
        }
        let replacement = format!(
            "    context->lr = 0x{return_pc:08X}u;\n    galaxy::call_guest_direct_resolved(services, 0x{target:08X}u, &rmge01::fn_{target:08X}, context, memory, 0x{call_pc:08X}u);\n"
        );
        profiled = profiled.replacen(&call, &replacement, 1);
    }

    let updated = format!(
        "{}{}{}",
        &file.contents[..start],
        profiled,
        &file.contents[end..]
    );
    if updated.len() > shard_source_bytes {
        return Err(TranslationError::Nw4rMaterialSetupEdgeProfile(format!(
            "fixed shard {} requires {} bytes, ceiling {}",
            file.name,
            updated.len(),
            shard_source_bytes
        )));
    }
    file.contents = updated;
    Ok(())
}

// These are the complete statically resolved child-call sets of the two Pane
// DrawSelf implementations that dominate the save-select Pane profile. Their
// virtual and indirect calls already pass through the aggregate profiler.
const NW4R_PANE_DRAW_SELF_DIRECT_CALLS: [(u32, u32); 17] = [
    (0x8000_DEF4, 0x8001_5674),
    (0x8000_DF1C, 0x8001_5730),
    (0x8000_DF40, 0x8000_CA24),
    (0x8000_DF64, 0x8001_5D74),
    (0x8000_E64C, 0x8000_9DF4),
    (0x8000_E65C, 0x8000_EB28),
    (0x8000_E68C, 0x8001_56DC),
    (0x8000_E6BC, 0x8001_56DC),
    (0x8000_E6E0, 0x8000_98A4),
    (0x8000_E700, 0x8000_E7E4),
    (0x8000_E730, 0x8000_DF9C),
    (0x8000_E760, 0x8000_DF9C),
    (0x8000_E780, 0x8000_86D8),
    (0x8000_E788, 0x8000_88BC),
    (0x8000_E7A0, 0x804B_D504),
    (0x8000_E7C0, 0x8000_A450),
    (0x8000_E7CC, 0x8000_9E50),
];

fn insert_nw4r_pane_draw_self_edge_profile(
    files: &mut [GeneratedSource],
    shard_source_bytes: usize,
) -> Result<(), TranslationError> {
    const OWNERS: [&str; 2] = ["\nvoid fn_8000DEAC(", "\nvoid fn_8000E5FC("];
    let mut shard_index = None;
    for owner in OWNERS {
        let matches = files
            .iter()
            .enumerate()
            .filter(|(_, file)| {
                file.name.starts_with("functions_")
                    && file.name.ends_with(".cpp")
                    && file.contents.contains(owner)
            })
            .map(|(index, _)| index)
            .collect::<Vec<_>>();
        if matches.len() != 1 || files[matches[0]].contents.matches(owner).count() != 1 {
            return Err(TranslationError::Nw4rPaneDrawSelfEdgeProfile(format!(
                "owner {owner} missing or ambiguous"
            )));
        }
        if shard_index.is_some_and(|expected| expected != matches[0]) {
            return Err(TranslationError::Nw4rPaneDrawSelfEdgeProfile(
                "selected owners span multiple shards".to_owned(),
            ));
        }
        shard_index = Some(matches[0]);
    }

    let file = &mut files[shard_index.expect("nonempty fixed owner list")];
    let original_file = file.contents.clone();
    let mut profiled = original_file.clone();
    for (call_pc, target) in NW4R_PANE_DRAW_SELF_DIRECT_CALLS {
        let owner = if call_pc < 0x8000_E000 {
            OWNERS[0]
        } else {
            OWNERS[1]
        };
        let start = original_file.find(owner).expect("validated owner");
        let end = original_file[start + owner.len()..]
            .find("\nvoid fn_")
            .map_or(original_file.len(), |offset| start + owner.len() + offset);
        let original = &original_file[start..end];
        let return_pc = call_pc + 4;
        let call = format!(
            "    context->lr = 0x{return_pc:08X}u;\n    context->pc = 0x{target:08X}u;\n    rmge01::fn_{target:08X}(context, memory, services);\n"
        );
        let checkpoint = format!(
            "    galaxy::call_return_checkpoint(services, 0x{call_pc:08X}u, 0x{return_pc:08X}u, context, memory);\n"
        );
        if original.matches(&call).count() != 1
            || original.matches(&checkpoint).count() != 2
            || original
                .matches(&format!("\ncall_return_{return_pc:08X}:\n"))
                .count()
                != 1
        {
            return Err(TranslationError::Nw4rPaneDrawSelfEdgeProfile(format!(
                "call {call_pc:08X}->{target:08X} lacks unique direct-call/resume anchors"
            )));
        }
        let replacement = format!(
            "    context->lr = 0x{return_pc:08X}u;\n    galaxy::call_guest_direct_resolved(services, 0x{target:08X}u, &rmge01::fn_{target:08X}, context, memory, 0x{call_pc:08X}u);\n"
        );
        if profiled.matches(&call).count() != 1 {
            return Err(TranslationError::Nw4rPaneDrawSelfEdgeProfile(format!(
                "call {call_pc:08X}->{target:08X} became ambiguous during rewrite"
            )));
        }
        profiled = profiled.replacen(&call, &replacement, 1);
    }
    if profiled.len() > shard_source_bytes {
        return Err(TranslationError::Nw4rPaneDrawSelfEdgeProfile(format!(
            "fixed shard {} requires {} bytes, ceiling {}",
            file.name,
            profiled.len(),
            shard_source_bytes
        )));
    }
    file.contents = profiled;
    Ok(())
}

const NW4R_PANE_HOT_CHILD_DIRECT_CALLS: [(u32, u32); 6] = [
    (0x8000_A610, 0x8035_0388),
    (0x8000_A630, 0x8000_887C),
    (0x8000_EB7C, 0x8000_9314),
    (0x8000_EB9C, 0x8000_ECF0),
    (0x8000_EBC4, 0x8000_A1C8),
    (0x8000_EBCC, 0x8000_CA24),
];

fn insert_nw4r_pane_hot_child_edge_profile(
    files: &mut [GeneratedSource],
    shard_source_bytes: usize,
) -> Result<(), TranslationError> {
    const OWNERS: [(&str, std::ops::Range<usize>); 2] =
        [("\nvoid fn_8000A450(", 0..2), ("\nvoid fn_8000EB28(", 2..6)];
    let mut updates = Vec::with_capacity(OWNERS.len());
    for (owner, call_indexes) in OWNERS {
        let matches = files
            .iter()
            .enumerate()
            .filter(|(_, file)| {
                file.name.starts_with("functions_")
                    && file.name.ends_with(".cpp")
                    && file.contents.contains(owner)
            })
            .map(|(index, _)| index)
            .collect::<Vec<_>>();
        if matches.len() != 1 || files[matches[0]].contents.matches(owner).count() != 1 {
            return Err(TranslationError::Nw4rPaneHotChildEdgeProfile(format!(
                "owner {owner} missing or ambiguous"
            )));
        }
        if updates
            .iter()
            .any(|(index, _): &(usize, String)| *index == matches[0])
        {
            return Err(TranslationError::Nw4rPaneHotChildEdgeProfile(
                "selected owners unexpectedly share one shard".to_owned(),
            ));
        }

        let index = matches[0];
        let original_file = &files[index].contents;
        let start = original_file.find(owner).expect("validated owner");
        let end = original_file[start + owner.len()..]
            .find("\nvoid fn_")
            .map_or(original_file.len(), |offset| start + owner.len() + offset);
        let original = &original_file[start..end];
        let mut profiled = original_file.clone();
        for call_index in call_indexes {
            let (call_pc, target) = NW4R_PANE_HOT_CHILD_DIRECT_CALLS[call_index];
            let return_pc = call_pc + 4;
            let call = format!(
                "    context->lr = 0x{return_pc:08X}u;\n    context->pc = 0x{target:08X}u;\n    rmge01::fn_{target:08X}(context, memory, services);\n"
            );
            let checkpoint = format!(
                "    galaxy::call_return_checkpoint(services, 0x{call_pc:08X}u, 0x{return_pc:08X}u, context, memory);\n"
            );
            if original.matches(&call).count() != 1
                || original.matches(&checkpoint).count() != 2
                || original
                    .matches(&format!("\ncall_return_{return_pc:08X}:\n"))
                    .count()
                    != 1
            {
                return Err(TranslationError::Nw4rPaneHotChildEdgeProfile(format!(
                    "call {call_pc:08X}->{target:08X} lacks unique direct-call/resume anchors"
                )));
            }
            let replacement = format!(
                "    context->lr = 0x{return_pc:08X}u;\n    galaxy::call_guest_direct_resolved(services, 0x{target:08X}u, &rmge01::fn_{target:08X}, context, memory, 0x{call_pc:08X}u);\n"
            );
            profiled = profiled.replacen(&call, &replacement, 1);
        }
        if profiled.len() > shard_source_bytes {
            return Err(TranslationError::Nw4rPaneHotChildEdgeProfile(format!(
                "fixed shard {} requires {} bytes, ceiling {}",
                files[index].name,
                profiled.len(),
                shard_source_bytes
            )));
        }
        updates.push((index, profiled));
    }

    for (index, contents) in updates {
        files[index].contents = contents;
    }
    Ok(())
}

// CalcStringRect and the formatter reached by TextWriterBase::Print account
// for nearly all measured time below the two hot Pane DrawSelf children. Keep
// this closure diagnostic-only: every selected PPC call remains statically
// translated and retains its ordinary return checkpoint and interior resume.
const NW4R_TEXT_FORMAT_DIRECT_CALLS: [(u32, u32); 66] = [
    (0x8000_A428, 0x8000_AF00),
    (0x8000_A434, 0x8000_887C),
    (0x8035_04E0, 0x8000_BB0C),
    (0x8035_05C4, 0x8000_BB0C),
    (0x8035_05D0, 0x8035_2B58),
    (0x8035_05E8, 0x8035_2B58),
    (0x8035_0600, 0x8035_2B58),
    (0x8035_0618, 0x8035_2B58),
    (0x8035_0630, 0x8035_2B58),
    (0x8035_0648, 0x8035_2B58),
    (0x8035_0660, 0x8035_2B58),
    (0x8035_0684, 0x8035_2B58),
    (0x8035_069C, 0x8000_BB0C),
    (0x8035_06C0, 0x8000_BB0C),
    (0x8035_06C8, 0x8035_2C70),
    (0x8035_06D4, 0x8035_2D88),
    (0x8035_0714, 0x8000_B020),
    (0x8035_0720, 0x8035_2C70),
    (0x8035_0728, 0x8035_2D88),
    (0x8035_076C, 0x8035_2B58),
    (0x8035_0778, 0x8000_9DBC),
    (0x8035_0784, 0x8035_3A24),
    (0x8035_0818, 0x8000_BB0C),
    (0x8035_0838, 0x8035_15F0),
    (0x8035_0840, 0x8035_3C90),
    (0x8035_0880, 0x8035_3DB4),
    (0x8035_08E4, 0x8035_347C),
    (0x8035_08EC, 0x8035_3ECC),
    (0x8035_092C, 0x8035_2C70),
    (0x8035_0950, 0x8035_3A24),
    (0x8035_095C, 0x8000_9E50),
    (0x8035_096C, 0x8000_9E50),
    (0x8035_0A74, 0x8000_BB0C),
    (0x8035_0AA0, 0x8000_A654),
    (0x8035_0AB8, 0x8035_2EA0),
    (0x8035_0AE0, 0x8000_A654),
    (0x8035_0AF4, 0x8035_2EA0),
    (0x8035_0B00, 0x8035_2C70),
    (0x8035_0B28, 0x8035_2EA0),
    (0x8035_0B38, 0x8035_3DB4),
    (0x8035_0C50, 0x8000_BB0C),
    (0x8035_0C5C, 0x8035_3A24),
    (0x8035_0C70, 0x8035_014C),
    (0x8035_0C80, 0x8035_2FC4),
    (0x8035_0C94, 0x8035_2D88),
    (0x8035_0CB0, 0x8035_2C70),
    (0x8035_0CC4, 0x8000_AEF8),
    (0x8035_0D54, 0x8000_BB0C),
    (0x8035_0DE4, 0x8000_BB0C),
    (0x8035_0E70, 0x8000_BB0C),
    (0x8035_0E7C, 0x8035_2B58),
    (0x8035_0ED4, 0x8035_3A24),
    (0x8035_0EE8, 0x8000_AEF8),
    (0x8035_0EF0, 0x8035_30F0),
    (0x8035_0EFC, 0x8035_2B58),
    (0x8035_0F84, 0x8000_BB0C),
    (0x8035_0FC0, 0x8035_3224),
    (0x8035_0FCC, 0x8000_94F8),
    (0x8035_0FD8, 0x8035_3358),
    (0x8035_0FE8, 0x8035_3DB4),
    (0x8035_0FF4, 0x8035_3C90),
    (0x8035_1000, 0x8035_3DB4),
    (0x8035_101C, 0x8035_2C70),
    (0x8035_1058, 0x8035_3358),
    (0x8035_1068, 0x8035_3224),
    (0x8035_107C, 0x8035_2FC4),
];

fn insert_nw4r_text_format_edge_profile(
    files: &mut [GeneratedSource],
    shard_source_bytes: usize,
) -> Result<(), TranslationError> {
    const OWNERS: [(&str, std::ops::Range<usize>); 2] = [
        ("\nvoid fn_8000A1C8(", 0..2),
        ("\nvoid fn_80350388(", 2..66),
    ];
    let mut updates = Vec::with_capacity(OWNERS.len());
    for (owner, call_indexes) in OWNERS {
        let matches = files
            .iter()
            .enumerate()
            .filter(|(_, file)| {
                file.name.starts_with("functions_")
                    && file.name.ends_with(".cpp")
                    && file.contents.contains(owner)
            })
            .map(|(index, _)| index)
            .collect::<Vec<_>>();
        if matches.len() != 1 || files[matches[0]].contents.matches(owner).count() != 1 {
            return Err(TranslationError::Nw4rTextFormatEdgeProfile(format!(
                "owner {owner} missing or ambiguous"
            )));
        }
        if updates
            .iter()
            .any(|(index, _): &(usize, String)| *index == matches[0])
        {
            return Err(TranslationError::Nw4rTextFormatEdgeProfile(
                "selected owners unexpectedly share one shard".to_owned(),
            ));
        }

        let index = matches[0];
        let original_file = &files[index].contents;
        let start = original_file.find(owner).expect("validated owner");
        let end = original_file[start + owner.len()..]
            .find("\nvoid fn_")
            .map_or(original_file.len(), |offset| start + owner.len() + offset);
        let original = &original_file[start..end];
        let mut profiled = original_file.clone();
        for call_index in call_indexes {
            let (call_pc, target) = NW4R_TEXT_FORMAT_DIRECT_CALLS[call_index];
            let return_pc = call_pc + 4;
            let call = format!(
                "    context->lr = 0x{return_pc:08X}u;\n    context->pc = 0x{target:08X}u;\n    rmge01::fn_{target:08X}(context, memory, services);\n"
            );
            let checkpoint = format!(
                "    galaxy::call_return_checkpoint(services, 0x{call_pc:08X}u, 0x{return_pc:08X}u, context, memory);\n"
            );
            if original.matches(&call).count() != 1
                || original.matches(&checkpoint).count() != 2
                || original
                    .matches(&format!("\ncall_return_{return_pc:08X}:\n"))
                    .count()
                    != 1
            {
                return Err(TranslationError::Nw4rTextFormatEdgeProfile(format!(
                    "call {call_pc:08X}->{target:08X} lacks unique direct-call/resume anchors"
                )));
            }
            let replacement = format!(
                "    context->lr = 0x{return_pc:08X}u;\n    galaxy::call_guest_direct_resolved(services, 0x{target:08X}u, &rmge01::fn_{target:08X}, context, memory, 0x{call_pc:08X}u);\n"
            );
            if profiled.matches(&call).count() != 1 {
                return Err(TranslationError::Nw4rTextFormatEdgeProfile(format!(
                    "call {call_pc:08X}->{target:08X} became ambiguous during rewrite"
                )));
            }
            profiled = profiled.replacen(&call, &replacement, 1);
        }
        if profiled.len() > shard_source_bytes {
            return Err(TranslationError::Nw4rTextFormatEdgeProfile(format!(
                "fixed shard {} requires {} bytes, ceiling {}",
                files[index].name,
                profiled.len(),
                shard_source_bytes
            )));
        }
        updates.push((index, profiled));
    }

    for (index, contents) in updates {
        files[index].contents = contents;
    }
    Ok(())
}

// The dominant children of the text-format closure are still substantial
// translated routines. Profile their complete statically resolved call sets so
// measurements distinguish nested work from their own instruction bodies.
const NW4R_TEXT_LAYOUT_DIRECT_CALLS: [(u32, u32); 16] = [
    (0x8000_95F8, 0x8000_9628),
    (0x8000_9780, 0x804B_BD80),
    (0x8000_97A8, 0x804B_BFD4),
    (0x8000_97B4, 0x804B_C40C),
    (0x8000_97EC, 0x804B_A6B0),
    (0x8000_A914, 0x8000_9DBC),
    (0x8000_ACC4, 0x8000_887C),
    (0x8000_ACD4, 0x8000_887C),
    (0x8000_AF6C, 0x8000_A870),
    (0x8000_B238, 0x8000_AF00),
    (0x8000_B244, 0x8000_887C),
    (0x8000_B4C8, 0x8000_A870),
    (0x8000_B4E0, 0x8000_887C),
    (0x8000_B6D8, 0x8000_A870),
    (0x8000_B6F0, 0x8000_887C),
    (0x8000_B730, 0x8000_9498),
];

fn insert_nw4r_text_layout_edge_profile(
    files: &mut [GeneratedSource],
    shard_source_bytes: usize,
) -> Result<(), TranslationError> {
    const OWNERS: [(&str, std::ops::Range<usize>); 5] = [
        ("\nvoid fn_800094F8(", 0..1),
        ("\nvoid fn_80009628(", 1..5),
        ("\nvoid fn_8000A870(", 5..8),
        ("\nvoid fn_8000AF00(", 8..9),
        ("\nvoid fn_8000B020(", 9..16),
    ];

    let mut grouped = BTreeMap::<usize, Vec<(&str, std::ops::Range<usize>)>>::new();
    for (owner, call_indexes) in OWNERS {
        let matches = files
            .iter()
            .enumerate()
            .filter(|(_, file)| {
                file.name.starts_with("functions_")
                    && file.name.ends_with(".cpp")
                    && file.contents.contains(owner)
            })
            .map(|(index, _)| index)
            .collect::<Vec<_>>();
        if matches.len() != 1 || files[matches[0]].contents.matches(owner).count() != 1 {
            return Err(TranslationError::Nw4rTextLayoutEdgeProfile(format!(
                "owner {owner} missing or ambiguous"
            )));
        }
        grouped
            .entry(matches[0])
            .or_default()
            .push((owner, call_indexes));
    }

    let mut updates = Vec::with_capacity(grouped.len());
    for (index, owners) in grouped {
        let original_file = &files[index].contents;
        let mut profiled = original_file.clone();
        for (owner, call_indexes) in owners {
            let start = original_file.find(owner).expect("validated owner");
            let end = original_file[start + owner.len()..]
                .find("\nvoid fn_")
                .map_or(original_file.len(), |offset| start + owner.len() + offset);
            let original = &original_file[start..end];
            for call_index in call_indexes {
                let (call_pc, target) = NW4R_TEXT_LAYOUT_DIRECT_CALLS[call_index];
                let return_pc = call_pc + 4;
                let call = format!(
                    "    context->lr = 0x{return_pc:08X}u;\n    context->pc = 0x{target:08X}u;\n    rmge01::fn_{target:08X}(context, memory, services);\n"
                );
                let checkpoint = format!(
                    "    galaxy::call_return_checkpoint(services, 0x{call_pc:08X}u, 0x{return_pc:08X}u, context, memory);\n"
                );
                if original.matches(&call).count() != 1
                    || original.matches(&checkpoint).count() != 2
                    || original
                        .matches(&format!("\ncall_return_{return_pc:08X}:\n"))
                        .count()
                        != 1
                {
                    return Err(TranslationError::Nw4rTextLayoutEdgeProfile(format!(
                        "call {call_pc:08X}->{target:08X} lacks unique direct-call/resume anchors"
                    )));
                }
                let replacement = format!(
                    "    context->lr = 0x{return_pc:08X}u;\n    galaxy::call_guest_direct_resolved(services, 0x{target:08X}u, &rmge01::fn_{target:08X}, context, memory, 0x{call_pc:08X}u);\n"
                );
                if profiled.matches(&call).count() != 1 {
                    return Err(TranslationError::Nw4rTextLayoutEdgeProfile(format!(
                        "call {call_pc:08X}->{target:08X} became ambiguous during rewrite"
                    )));
                }
                profiled = profiled.replacen(&call, &replacement, 1);
            }
        }
        if profiled.len() > shard_source_bytes {
            return Err(TranslationError::Nw4rTextLayoutEdgeProfile(format!(
                "fixed shard {} requires {} bytes, ceiling {}",
                files[index].name,
                profiled.len(),
                shard_source_bytes
            )));
        }
        updates.push((index, profiled));
    }

    for (index, contents) in updates {
        files[index].contents = contents;
    }
    Ok(())
}

// The 0x803A3B6C JPA draw child ranks highly in the runtime's dynamic-call
// self profile, but that profiler cannot subtract statically resolved native
// calls. Instrument its complete static closure before treating its local body
// as an optimization target. Also time the exact normal-entry matrix assembly
// between the third normalization and the projection callback. This region has
// no guest calls, while its interior FPU-retry entries deliberately start with
// the local profiler inactive and therefore cannot inherit a stale timestamp.
const JPA_HOT_DRAW_DIRECT_CALLS: [(u32, u32); 10] = [
    (0x803A_3BC0, 0x803E_595C),
    (0x803A_3BD0, 0x803E_4D24),
    (0x803A_3BE0, 0x804B_6CB8),
    (0x803A_3BEC, 0x803E_595C),
    (0x803A_3BFC, 0x803E_4D24),
    (0x803A_3C0C, 0x804B_6CB8),
    (0x803A_3C14, 0x803E_4D24),
    (0x803A_3CB8, 0x804B_5F3C),
    (0x803A_3CC4, 0x804B_E1D8),
    (0x803A_3CFC, 0x804B_DEA8),
];

const JPA_HOT_DRAW_BODY_REGION_START: u32 = 0x803A_3C18;
const JPA_HOT_DRAW_BODY_REGION_END: u32 = 0x803A_3CA8;

fn jpa_hot_draw_body_profile_declarations() -> &'static str {
    "    bool jpa_hot_body_region_active = false;\n    std::uint64_t jpa_hot_body_region_start_cycles = 0u;\n"
}

fn jpa_hot_draw_body_profile_begin() -> &'static str {
    "    jpa_hot_body_region_active = galaxy::profile_generated_direct_edges_active(services);\n    if (jpa_hot_body_region_active) {\n        jpa_hot_body_region_start_cycles = galaxy::direct_edge_profile_ticks();\n    }\n"
}

fn jpa_hot_draw_body_profile_end() -> String {
    let pseudo_target = 0xE000_0000u32 | (JPA_HOT_DRAW_BODY_REGION_END & 0x0FFF_FFFF);
    format!(
        "    if (jpa_hot_body_region_active) {{\n        galaxy::report_direct_edge_profile(services, 0x{JPA_HOT_DRAW_BODY_REGION_START:08X}u, 0x{pseudo_target:08X}u, 0x0001000000000000ull + galaxy::direct_edge_profile_ticks() - jpa_hot_body_region_start_cycles);\n        jpa_hot_body_region_active = false;\n    }}\n"
    )
}

fn insert_jpa_hot_draw_edge_profile(
    files: &mut [GeneratedSource],
    shard_source_bytes: usize,
) -> Result<(), TranslationError> {
    const OWNER: &str = "\nvoid fn_803A3B6C(";
    let matches = files
        .iter()
        .enumerate()
        .filter(|(_, file)| {
            file.name.starts_with("functions_")
                && file.name.ends_with(".cpp")
                && file.contents.contains(OWNER)
        })
        .map(|(index, _)| index)
        .collect::<Vec<_>>();
    if matches.len() != 1 || files[matches[0]].contents.matches(OWNER).count() != 1 {
        return Err(TranslationError::JpaHotDrawEdgeProfile(
            "owner 0x803A3B6C missing or ambiguous".to_owned(),
        ));
    }

    let index = matches[0];
    let original_file = &files[index].contents;
    let start = original_file.find(OWNER).expect("validated owner");
    let end = original_file[start + OWNER.len()..]
        .find("\nvoid fn_")
        .map_or(original_file.len(), |offset| start + OWNER.len() + offset);
    let original = &original_file[start..end];
    let mut profiled = original.to_owned();
    for (call_pc, target) in JPA_HOT_DRAW_DIRECT_CALLS {
        let return_pc = call_pc + 4;
        let call = format!(
            "    context->lr = 0x{return_pc:08X}u;\n    context->pc = 0x{target:08X}u;\n    rmge01::fn_{target:08X}(context, memory, services);\n"
        );
        let checkpoint = format!(
            "    galaxy::call_return_checkpoint(services, 0x{call_pc:08X}u, 0x{return_pc:08X}u, context, memory);\n"
        );
        if original.matches(&call).count() != 1
            || original.matches(&checkpoint).count() != 2
            || original
                .matches(&format!("\ncall_return_{return_pc:08X}:\n"))
                .count()
                != 1
        {
            return Err(TranslationError::JpaHotDrawEdgeProfile(format!(
                "call {call_pc:08X}->{target:08X} lacks unique direct-call/resume anchors"
            )));
        }
        let replacement = format!(
            "    context->lr = 0x{return_pc:08X}u;\n    galaxy::call_guest_direct_resolved(services, 0x{target:08X}u, &rmge01::fn_{target:08X}, context, memory, 0x{call_pc:08X}u);\n"
        );
        if profiled.matches(&call).count() != 1 {
            return Err(TranslationError::JpaHotDrawEdgeProfile(format!(
                "call {call_pc:08X}->{target:08X} became ambiguous during rewrite"
            )));
        }
        profiled = profiled.replacen(&call, &replacement, 1);
    }
    const DECLARATION_ANCHOR: &str = "    static_cast<void>(services);\n";
    let begin_anchor = format!(
        "label_{JPA_HOT_DRAW_BODY_REGION_START:08X}:\n    galaxy::ensure_fpu_available(services, 0x{JPA_HOT_DRAW_BODY_REGION_START:08X}u, context, memory);\n"
    );
    let end_return_pc = JPA_HOT_DRAW_BODY_REGION_END + 4;
    let end_anchor = format!(
        "    {{\n    const std::uint32_t branch_target = context->ctr & 0xFFFFFFFCu;\n    context->lr = 0x{end_return_pc:08X}u;\n"
    );
    if original.matches(DECLARATION_ANCHOR).count() != 1
        || original.matches(&begin_anchor).count() != 1
        || original.matches(&end_anchor).count() != 1
    {
        return Err(TranslationError::JpaHotDrawEdgeProfile(
            "matrix-body region lacks unique declaration/begin/end anchors".to_owned(),
        ));
    }
    profiled = profiled.replacen(
        DECLARATION_ANCHOR,
        &format!(
            "{DECLARATION_ANCHOR}{}",
            jpa_hot_draw_body_profile_declarations()
        ),
        1,
    );
    profiled = profiled.replacen(
        &begin_anchor,
        &format!("{begin_anchor}{}", jpa_hot_draw_body_profile_begin()),
        1,
    );
    profiled = profiled.replacen(
        &end_anchor,
        &format!("{}{end_anchor}", jpa_hot_draw_body_profile_end()),
        1,
    );
    let updated = format!(
        "{}{}{}",
        &original_file[..start],
        profiled,
        &original_file[end..]
    );
    if updated.len() > shard_source_bytes {
        return Err(TranslationError::JpaHotDrawEdgeProfile(format!(
            "fixed shard {} requires {} bytes, ceiling {}",
            files[index].name,
            updated.len(),
            shard_source_bytes
        )));
    }
    files[index].contents = updated;
    Ok(())
}

// The bounded call-self profile cannot subtract direct generated calls. These
// are the complete statically resolved child sets of its three largest
// otherwise-unexplained entries: NW4R FindPaneByName, the Bluetooth USB bulk
// callback, and the HID-host event handler. Their indirect calls already pass
// through call_guest_cached and are visible to the aggregate edge profiler.
// This diagnostic preserves every static callee, return checkpoint, and
// interior resume while measuring the missing direct edges.
// The September 30 eight-VI gameplay edge probe also identified these four
// descendant owners. Observe their complete static child sets before treating
// their inclusive times as self time. The existing window gate keeps the
// diagnostic inactive during startup; every return/resume checkpoint is kept.
const TOP_SELF_CLOSURE_DIRECT_CALLS: [(u32, u32); 75] = [
    (0x8000_C10C, 0x8001_5394),
    (0x804E_BD88, 0x804E_A03C),
    (0x804E_BD90, 0x804E_ABB8),
    (0x804E_BDAC, 0x804E_A03C),
    (0x804E_BDBC, 0x804E_9F54),
    (0x804E_BDD0, 0x804E_A03C),
    (0x804E_BDF4, 0x8000_4338),
    (0x804E_BDFC, 0x8050_804C),
    (0x804E_BE10, 0x8050_821C),
    (0x804E_BE40, 0x804A_392C),
    (0x804E_BE48, 0x804E_A03C),
    (0x804E_BE58, 0x804E_9F54),
    (0x804E_BE8C, 0x804E_A1AC),
    (0x804E_BEB8, 0x804E_41B8),
    (0x804E_BEC8, 0x804E_A03C),
    (0x804F_28FC, 0x804F_0A44),
    (0x804F_2904, 0x804F_0BB8),
    (0x804F_290C, 0x804F_0CBC),
    (0x804F_2914, 0x804F_1C10),
    (0x804F_2928, 0x804F_2AB0),
    (0x804F_294C, 0x804F_2AB0),
    (0x804F_29CC, 0x804E_C970),
    (0x804F_29DC, 0x804F_255C),
    (0x800C_8C9C, 0x800C_9C60),
    (0x800C_8CAC, 0x8044_8710),
    (0x800C_8CBC, 0x8044_8710),
    (0x800C_8CCC, 0x8044_8710),
    (0x800C_8CDC, 0x8044_8710),
    (0x800C_8CEC, 0x8044_8710),
    (0x800C_8CFC, 0x8044_8710),
    (0x800C_8D0C, 0x8044_8710),
    (0x800C_8D1C, 0x8044_8710),
    (0x800C_8D2C, 0x8044_8710),
    (0x8016_54A8, 0x8016_8F8C),
    (0x8016_54B8, 0x8015_B8C4),
    (0x8016_54C0, 0x803D_D8D4),
    (0x8016_54D0, 0x803D_E004),
    (0x8016_54E0, 0x8016_41C4),
    (0x8016_54FC, 0x8016_FAE8),
    (0x8016_5530, 0x8016_6000),
    (0x8016_5540, 0x8016_266C),
    (0x8016_5550, 0x8015_C708),
    (0x8016_5564, 0x803D_C5E8),
    (0x8016_556C, 0x803C_2604),
    (0x8016_5574, 0x803F_8F40),
    (0x8016_557C, 0x8016_C7E4),
    (0x802B_5F00, 0x802C_03FC),
    (0x802B_5F28, 0x802B_7668),
    (0x802B_5F34, 0x802B_7668),
    (0x802B_5F3C, 0x802B_6C74),
    (0x802B_5F44, 0x802C_063C),
    (0x802B_5F4C, 0x802B_FD98),
    (0x802B_5F58, 0x802A_6544),
    (0x802B_5F60, 0x802C_08C0),
    (0x802B_5F74, 0x802C_0A98),
    (0x802B_5F80, 0x802F_40EC),
    (0x802B_5F90, 0x802C_1654),
    (0x802B_5F9C, 0x802C_17B0),
    (0x802B_5FDC, 0x802B_747C),
    (0x802B_5FFC, 0x802B_747C),
    (0x802B_601C, 0x802B_747C),
    (0x802B_6030, 0x802B_F678),
    (0x802B_6048, 0x802F_046C),
    (0x802B_605C, 0x8031_28C0),
    (0x802B_6070, 0x8031_28C0),
    (0x802B_607C, 0x8031_28C0),
    (0x802B_6088, 0x8031_28C0),
    (0x802B_609C, 0x8031_28C0),
    (0x802B_60B0, 0x8031_28C0),
    (0x802B_60BC, 0x8031_28C0),
    (0x802B_60C8, 0x802C_37F4),
    (0x802B_60D4, 0x802C_37F4),
    (0x802B_60D8, 0x803C_9E84),
    (0x8036_61B8, 0x803D_81DC),
    (0x8036_61C8, 0x8036_7174),
];

fn insert_top_self_closure_edge_profile(
    files: &mut [GeneratedSource],
    shard_source_bytes: usize,
) -> Result<(), TranslationError> {
    const OWNERS: [(&str, std::ops::Range<usize>); 7] = [
        ("\nvoid fn_8000C0E0(", 0..1),
        ("\nvoid fn_804EBD50(", 1..15),
        ("\nvoid fn_804F28B0(", 15..23),
        ("\nvoid fn_800C8C64(", 23..33),
        ("\nvoid fn_80165478(", 33..46),
        ("\nvoid fn_802B5EE8(", 46..73),
        ("\nvoid fn_803661A4(", 73..75),
    ];

    let mut grouped = BTreeMap::<usize, Vec<(&str, std::ops::Range<usize>)>>::new();
    for (owner, call_indexes) in OWNERS {
        let matches = files
            .iter()
            .enumerate()
            .filter(|(_, file)| {
                file.name.starts_with("functions_")
                    && file.name.ends_with(".cpp")
                    && file.contents.contains(owner)
            })
            .map(|(index, _)| index)
            .collect::<Vec<_>>();
        if matches.len() != 1 || files[matches[0]].contents.matches(owner).count() != 1 {
            return Err(TranslationError::TopSelfClosureEdgeProfile(format!(
                "owner {owner} missing or ambiguous"
            )));
        }
        grouped
            .entry(matches[0])
            .or_default()
            .push((owner, call_indexes));
    }

    let mut updates = Vec::with_capacity(grouped.len());
    for (index, owners) in grouped {
        let original_file = &files[index].contents;
        let mut profiled = original_file.clone();
        for (owner, call_indexes) in owners {
            let start = original_file.find(owner).expect("validated owner");
            let end = original_file[start + owner.len()..]
                .find("\nvoid fn_")
                .map_or(original_file.len(), |offset| start + owner.len() + offset);
            let original = &original_file[start..end];
            for call_index in call_indexes {
                let (call_pc, target) = TOP_SELF_CLOSURE_DIRECT_CALLS[call_index];
                let return_pc = call_pc + 4;
                let call = format!(
                    "    context->lr = 0x{return_pc:08X}u;\n    context->pc = 0x{target:08X}u;\n    rmge01::fn_{target:08X}(context, memory, services);\n"
                );
                let checkpoint = format!(
                    "    galaxy::call_return_checkpoint(services, 0x{call_pc:08X}u, 0x{return_pc:08X}u, context, memory);\n"
                );
                if original.matches(&call).count() != 1
                    || original.matches(&checkpoint).count() != 2
                    || original
                        .matches(&format!("\ncall_return_{return_pc:08X}:\n"))
                        .count()
                        != 1
                {
                    return Err(TranslationError::TopSelfClosureEdgeProfile(format!(
                        "call {call_pc:08X}->{target:08X} lacks unique direct-call/resume anchors"
                    )));
                }
                let replacement = format!(
                    "    galaxy::call_guest_direct_resolved(services,0x{target:08X}u,&fn_{target:08X},(context->lr=0x{return_pc:08X}u,context),memory,0x{call_pc:08X}u);\n"
                );
                if profiled.matches(&call).count() != 1 {
                    return Err(TranslationError::TopSelfClosureEdgeProfile(format!(
                        "call {call_pc:08X}->{target:08X} became ambiguous during rewrite"
                    )));
                }
                profiled = profiled.replacen(&call, &replacement, 1);
                let profiled_checkpoint = format!(
                    "    galaxy::crcp(services,0x{call_pc:08X}u,0x{return_pc:08X}u,context,memory);\n"
                );
                profiled = profiled.replace(&checkpoint, &profiled_checkpoint);
            }
        }
        const INCLUDE: &str = "#include \"galaxy/crcp.h\"\n";
        if !profiled.starts_with("#include \"functions.h\"\n") || profiled.contains(INCLUDE) {
            return Err(TranslationError::TopSelfClosureEdgeProfile(format!(
                "fixed shard {} lacks a unique generated preamble",
                files[index].name
            )));
        }
        profiled = profiled.replacen(
            "#include \"functions.h\"\n",
            &format!("#include \"functions.h\"\n{INCLUDE}"),
            1,
        );
        if profiled.len() > shard_source_bytes {
            return Err(TranslationError::TopSelfClosureEdgeProfile(format!(
                "fixed shard {} requires {} bytes, ceiling {}",
                files[index].name,
                profiled.len(),
                shard_source_bytes
            )));
        }
        updates.push((index, profiled));
    }

    for (index, contents) in updates {
        files[index].contents = contents;
    }
    Ok(())
}

fn main_frame_stage_marker(function_address: u32, pc: u32) -> Option<&'static str> {
    match (function_address, pc) {
        (0x8033_E7E0, 0x8033_E7F8) => Some("__galaxy_main_frame_stage:after-803CA428"),
        (0x8033_E7E0, 0x8033_E7FC) => Some("__galaxy_main_frame_stage:after-80187E2C"),
        (0x8033_E7E0, 0x8033_E804) => Some("__galaxy_main_frame_stage:after-8033F120"),
        (0x8033_E7E0, 0x8033_E80C) => Some("__galaxy_main_frame_stage:after-80344894-4F"),
        (0x8033_E7E0, 0x8033_E814) => Some("__galaxy_main_frame_stage:after-80344894-50"),
        (0x8033_E7E0, 0x8033_E81C) => Some("__galaxy_main_frame_stage:after-8033EDF8"),
        (0x8033_E7E0, 0x8033_E824) => Some("__galaxy_main_frame_stage:after-8033EE8C"),
        (0x8033_E7E0, 0x8033_E82C) => Some("__galaxy_main_frame_stage:after-8033F040"),
        (0x8033_E7E0, 0x8033_E830) => Some("__galaxy_main_frame_stage:after-803CA474"),
        (0x8033_EE8C, 0x8033_EEB8) => Some("__galaxy_main_frame_stage:draw3d-after-803F7E40"),
        (0x8033_EE8C, 0x8033_EED8) => Some("__galaxy_main_frame_stage:draw3d-after-803A29B8"),
        (0x8033_EE8C, 0x8033_EF00) => Some("__galaxy_main_frame_stage:draw3d-after-803CA5F4"),
        (0x8033_EE8C, 0x8033_EF1C) => Some("__galaxy_main_frame_stage:draw3d-after-804BDC78-1"),
        (0x8033_EE8C, 0x8033_EF38) => Some("__galaxy_main_frame_stage:draw3d-after-80343E44"),
        (0x8033_EE8C, 0x8033_EF5C) => Some("__galaxy_main_frame_stage:draw3d-after-804BDC78-2"),
        (0x8033_EE8C, 0x8033_EF70) => Some("__galaxy_main_frame_stage:draw3d-after-80344224"),
        (0x8033_EE8C, 0x8033_EF90) => Some("__galaxy_main_frame_stage:draw3d-after-804BDC78-3"),
        (0x8033_EE8C, 0x8033_EFA8) => Some("__galaxy_main_frame_stage:draw3d-after-8034419C"),
        (0x8033_EE8C, 0x8033_EFB0) => Some("__galaxy_main_frame_stage:draw3d-after-80344990-18"),
        (0x8033_EE8C, 0x8033_EFB8) => Some("__galaxy_main_frame_stage:draw3d-after-80344894-26"),
        (0x8033_EE8C, 0x8033_EFC0) => Some("__galaxy_main_frame_stage:draw3d-after-80344894-47"),
        (0x8033_EE8C, 0x8033_EFC8) => Some("__galaxy_main_frame_stage:draw3d-after-80344894-4C"),
        (0x8033_EE8C, 0x8033_EFD0) => Some("__galaxy_main_frame_stage:draw3d-after-80344894-2F"),
        (0x8033_EE8C, 0x8033_EFD8) => Some("__galaxy_main_frame_stage:draw3d-after-80344894-2D"),
        (0x8033_EE8C, 0x8033_EFDC) => Some("__galaxy_main_frame_stage:draw3d-after-8034424C"),
        (0x8033_EE8C, 0x8033_F000) => Some("__galaxy_main_frame_stage:draw3d-after-803C7360-2"),
        (0x8033_EE8C, 0x8033_F014) => Some("__galaxy_main_frame_stage:draw3d-after-803D8A44"),
        (0x8033_EE8C, 0x8033_F028) => Some("__galaxy_main_frame_stage:draw3d-after-803CA76C"),
        // 8034424C fans out to the eight fixed 80344954 categories.  The
        // continuations are deliberately traced here, rather than instrumenting
        // a broad function entry, so the bounded main-frame trace attributes a
        // category's complete native body to its exact static call site.
        (0x8034_424C, 0x8034_4260) => Some("__galaxy_main_frame_stage:draw3d-80344954-category-1D"),
        (0x8034_424C, 0x8034_4268) => Some("__galaxy_main_frame_stage:draw3d-80344954-category-19"),
        (0x8034_424C, 0x8034_4270) => Some("__galaxy_main_frame_stage:draw3d-80344954-category-1A"),
        (0x8034_424C, 0x8034_4278) => Some("__galaxy_main_frame_stage:draw3d-80344954-category-1B"),
        (0x8034_424C, 0x8034_4280) => Some("__galaxy_main_frame_stage:draw3d-80344954-category-1C"),
        (0x8034_424C, 0x8034_4288) => Some("__galaxy_main_frame_stage:draw3d-80344954-category-22"),
        (0x8034_424C, 0x8034_4290) => Some("__galaxy_main_frame_stage:draw3d-80344954-category-17"),
        (0x8034_424C, 0x8034_4298) => Some("__galaxy_main_frame_stage:draw3d-80344954-category-16"),
        // The category-47 particle PTMF path spends almost all of its retained
        // time inside 800C9248. These are its two existing generated resume
        // points, so the markers separate exact child calls without adding a
        // new guest-code boundary or changing restart/fault behavior.
        (0x800C_9248, 0x800C_9260) => {
            Some("__galaxy_main_frame_stage:particle-800C9248-after-803C73CC")
        }
        (0x800C_9248, 0x800C_926C) => {
            Some("__galaxy_main_frame_stage:particle-800C9248-after-800C95C4")
        }
        (0x800C_95C4, 0x800C_95F0) => {
            Some("__galaxy_main_frame_stage:particle-800C95C4-after-psmtx-copy")
        }
        (0x800C_95C4, 0x800C_95F8) => {
            Some("__galaxy_main_frame_stage:particle-800C95C4-after-psmtx-identity")
        }
        (0x800C_95C4, 0x800C_960C) => {
            Some("__galaxy_main_frame_stage:particle-800C95C4-after-80448790-array-0")
        }
        (0x800C_95C4, 0x800C_9614) => {
            Some("__galaxy_main_frame_stage:particle-800C95C4-after-gx-set-array-0")
        }
        (0x800C_95C4, 0x800C_9628) => {
            Some("__galaxy_main_frame_stage:particle-800C95C4-after-80448790-array-1")
        }
        (0x800C_95C4, 0x800C_9630) => {
            Some("__galaxy_main_frame_stage:particle-800C95C4-after-gx-set-array-1")
        }
        (0x8044_8790, 0x8044_87D8) => {
            Some("__galaxy_main_frame_stage:particle-80448790-after-preparation")
        }
        (0x8044_8790, 0x8044_891C) => {
            Some("__galaxy_main_frame_stage:particle-80448790-after-gx-setup")
        }
        (0x8044_8790, 0x8044_8974) => {
            Some("__galaxy_main_frame_stage:particle-80448790-after-callback-loop")
        }
        _ => None,
    }
}

fn emit_main_frame_stage_hook(
    output: &mut String,
    function_address: u32,
    pc: u32,
) -> Result<(), TranslationError> {
    let Some(marker) = main_frame_stage_marker(function_address, pc) else {
        return Ok(());
    };
    writeln!(
        output,
        "    galaxy::trace_main_frame_stage(services, \"{marker}\");"
    )
    .map_err(|_| TranslationError::Formatting)
}

fn should_emit_jutvideo_mq_trace_hook(address: u32) -> bool {
    matches!(
        address,
        0x8041_9554 | 0x804A_88E4 | 0x804A_89AC | 0x804A_8A88
    )
}

fn emit_jutvideo_mq_trace_hook(output: &mut String, address: u32) -> Result<(), TranslationError> {
    if !should_emit_jutvideo_mq_trace_hook(address) {
        return Ok(());
    }
    writeln!(
        output,
        "    if (context->pc == 0x{address:08X}u) {{\n        galaxy::trace_jutvideo_mq_function_entry(services, context, memory, 0x{address:08X}u);\n    }}"
    )
    .map_err(|_| TranslationError::Formatting)
}

fn should_emit_file_select_trace_hook(address: u32) -> bool {
    matches!(
        address,
        0x8017_85EC
            | 0x8017_866C
            | 0x8017_8A14
            | 0x8017_8E64
            | 0x8017_8F5C
            | 0x8017_AB10
            | 0x8017_AB94
            | 0x8017_BA08
            | 0x8017_BA84
            | 0x8017_BB40
            | 0x8017_C7B4
            | 0x8017_C874
            | 0x8017_C984
            | 0x8017_CA08
            | 0x8017_CA60
            | 0x8017_CAC4
            | 0x8017_CB1C
            | 0x8017_CD18
            | 0x8017_CF1C
            | 0x8017_D024
            | 0x8017_D08C
            | 0x8017_D148
            | 0x8017_D900
            | 0x8017_D9DC
            | 0x8017_DBAC
            | 0x8017_DD04
            | 0x8017_DD80
            | 0x8017_DF14
            | 0x8017_E0C4
            | 0x8017_E294
            | 0x8036_DA0C
            | 0x8036_E558
            | 0x8036_ED14
            | 0x8036_EE48
            | 0x8036_F054
            | 0x8036_FB54
            | 0x8034_BA38
            | 0x8034_BA9C
            | 0x8034_BAA4
            | 0x8034_BAAC
            | 0x8034_C1A0
            | 0x8034_C254
            | 0x8034_C874
            | 0x8034_C87C
            | 0x8034_C8E4
            | 0x8034_C914
            | 0x8036_6244
            | 0x803A_E664
            | 0x803D_42E8
            | 0x803D_42EC
            | 0x803F_B5BC
            | 0x803F_B680
            | 0x803F_BAAC
            | 0x803F_BB08
            | 0x803F_C76C
            | 0x803F_C794
            | 0x803B_49C8
            | 0x803B_49EC
            | 0x803B_49B0
            | 0x803B_49BC
            | 0x803B_4A20
            | 0x803B_4A54
            | 0x803B_4D84
            | 0x803B_4DC8
            | 0x803B_4F84
            | 0x803B_4FA8
            | 0x803B_64C0
            | 0x803B_64D0
            | 0x803B_64F8
            | 0x803B_55EC
            | 0x803B_5654
            | 0x803B_56BC
            | 0x803B_5498
            | 0x803B_5958
            | 0x803B_596C
            | 0x803B_59B0
            | 0x803B_5A48
            | 0x803B_5ADC
            | 0x803B_5E30
            | 0x803B_5FC0
            | 0x803B_5FC8
            | 0x803B_5FD0
            | 0x803B_5FD8
            | 0x803B_6018
            | 0x803B_6020
            | 0x803B_8C64
            | 0x803B_8ED8
            | 0x803B_90E0
            | 0x803B_9550
            | 0x803B_96F4
            | 0x803B_9AC4
            | 0x803B_9B30
            | 0x803B_9EAC
            | 0x803B_A560
            | 0x803B_A6CC
            | 0x803B_A724
            | 0x803B_B128
            | 0x803B_B27C
            | 0x803B_B2E0
            | 0x803B_B370
            | 0x803B_B464
            | 0x803B_B4E0
            | 0x803B_B56C
            | 0x803B_B5F4
            | 0x803B_B66C
            | 0x803B_B6B8
            | 0x803B_B718
    )
}

fn should_emit_movie_trace_hook(address: u32) -> bool {
    matches!(
        address,
        0x800B_E334
            | 0x800B_E3A4
            | 0x800B_E450
            | 0x800B_E4DC
            | 0x800B_E510
            | 0x800B_E544
            | 0x800B_E568
            | 0x800B_E674
            | 0x800B_E764
            | 0x800B_E7A4
            | 0x800B_E8C0
            | 0x800B_E9D8
            | 0x800B_EAE8
            | 0x800B_EAF8
            | 0x800B_EBC0
            | 0x800B_EBD4
            | 0x800B_ECE8
            | 0x800B_ED60
            | 0x800B_ED7C
            | 0x800B_EDA0
            | 0x800B_EDB0
            | 0x800B_EDE8
            | 0x800B_EE40
            | 0x800B_EE44
            | 0x800B_EE48
            | 0x800B_EEA0
            | 0x800B_EEBC
            | 0x800B_EEEC
            | 0x800B_EF58
            | 0x800B_EF84
            | 0x800B_EFA4
            | 0x800C_21DC
            | 0x800C_2310
            | 0x800C_23F8
            | 0x800C_250C
            | 0x800C_2588
            | 0x800C_25B8
            | 0x800C_25C8
            | 0x800C_264C
            | 0x800C_27DC
            | 0x800C_297C
            | 0x800C_29E4
            | 0x800C_2A28
            | 0x800C_2AD8
            | 0x800C_2B0C
            | 0x800C_2B90
            | 0x800C_2C4C
            | 0x800C_2E20
            | 0x800C_2E54
            | 0x800C_2ED8
            | 0x800C_2F48
            | 0x800C_3100
            | 0x800C_3108
            | 0x800C_3200
            | 0x800C_3290
            | 0x800C_3348
            | 0x800C_33C4
            | 0x800C_33CC
            | 0x800C_3430
            | 0x800C_3488
            | 0x800C_34E4
            | 0x800C_3578
            | 0x800C_3590
            | 0x800C_3598
            | 0x800C_35A0
            | 0x800C_35A8
            | 0x800C_35B0
            | 0x800C_35B8
            | 0x8033_EE8C
            | 0x8033_F040
            | 0x8034_02D0
            | 0x8034_0328
            | 0x8034_0388
            | 0x8034_0400
            | 0x8034_0454
            | 0x8034_048C
            | 0x8034_04B0
            | 0x8034_0530
            | 0x8034_05CC
            | 0x8034_0650
            | 0x8034_09BC
            | 0x8034_DB5C
            | 0x8034_DBA4
            | 0x8034_DC04
            | 0x8034_DC4C
            | 0x8034_DC98
            | 0x8034_DCC4
            | 0x8034_DD40
            | 0x8034_DDB0
            | 0x8034_DDD4
            | 0x8034_DE0C
            | 0x8034_DE18
            | 0x8034_DE24
            | 0x8034_DE28
            | 0x8034_DE3C
            | 0x8034_DE54
            | 0x8034_DE7C
            | 0x8034_DEC4
            | 0x8034_DED0
            | 0x8034_DF34
            | 0x8034_DF3C
            | 0x8034_DF44
            | 0x8034_DF94
            | 0x8034_E30C
            | 0x8034_E38C
            | 0x8034_E400
            | 0x8034_E410
            | 0x8034_E458
            | 0x8034_E470
            | 0x8034_E490
            | 0x8034_E524
            | 0x8034_E538
            | 0x8034_E5CC
            | 0x8034_E658
            | 0x8034_E674
            | 0x8034_E69C
            | 0x8034_E708
            | 0x8034_E74C
            | 0x8034_E75C
            | 0x8034_E7A8
            | 0x8034_E7B0
            | 0x8034_E800
            | 0x8034_E804
            | 0x8034_E85C
            | 0x8034_E878
            | 0x8034_E8F8
            | 0x8034_E914
            | 0x8034_E948
            | 0x8034_E970
            | 0x8034_EA34
            | 0x8034_EA7C
            | 0x8034_EAC8
            | 0x8034_EAEC
            | 0x8034_EB14
            | 0x8034_EB50
            | 0x8034_EB60
            | 0x8034_EBBC
            | 0x8034_EBD0
            | 0x8039_8C94
            | 0x8039_8D3C
            | 0x8039_8E00
            | 0x8039_9058
            | 0x8039_9144
            | 0x8039_9280
            | 0x8039_9390
            | 0x8003_17A0
            | 0x8039_4D2C
            | 0x8039_AFA8
            | 0x8039_C284
            | 0x8039_C548
            | 0x8039_C5FC
            | 0x8039_C730
            | 0x803A_29B0
            | 0x803F_DCC4
            | 0x803F_DDBC
            | 0x8040_E048
            | 0x8040_EDA8
            | 0x8040_EE7C
            | 0x8041_2EF8
            | 0x8041_3124
            | 0x8041_40D4
            | 0x8041_4178
            | 0x8041_421C
            | 0x8041_42D4
            | 0x8041_4628
            | 0x8041_4700
            | 0x8041_4740
            | 0x8041_47D0
            | 0x8041_5454
            | 0x8041_5674
            | 0x8041_5880
            | 0x8041_5E84
            | 0x804A_AEA0
            | 0x804B_F844
            | 0x8003_379C
            | 0x8049_251C
            | 0x8049_2644
            | 0x8049_26D0
            | 0x8049_DADC
            | 0x8037_11FC
            | 0x8037_1240
            | 0x8037_12D4
            | 0x8037_132C
            | 0x8037_15D8
            | 0x8037_163C
            | 0x8037_16B4
            | 0x8037_16D8
            | 0x8037_172C
            | 0x8037_175C
            | 0x8037_1E44
            | 0x8037_1E5C
            | 0x8037_1E98
            | 0x8037_1F04
            | 0x8037_1F7C
            | 0x8037_1FDC
            | 0x8037_1FE8
            | 0x8037_2118
            | 0x8037_2128
            | 0x8037_2248
            | 0x8037_2280
            | 0x8037_22AC
            | 0x8037_22DC
            | 0x8037_23CC
            | 0x8037_2510
            | 0x8037_2DB0
            | 0x8037_2F28
            | 0x8038_CFB8
            | 0x8038_D1CC
            | 0x8038_D264
            | 0x8038_D2AC
            | 0x8038_D2C8
            | 0x8038_D2E4
            | 0x8038_D34C
            | 0x8038_D420
            | 0x8038_D494
            | 0x8038_D56C
            | 0x8038_D6FC
            | 0x8038_D718
            | 0x8038_D7B8
            | 0x8038_D7FC
            | 0x8038_D80C
            | 0x8038_D868
            | 0x8038_D8C8
            | 0x8038_D924
            | 0x8038_D980
            | 0x8038_D9F4
            | 0x8038_DA44
            | 0x8038_DA98
            | 0x8038_DAF4
            | 0x8038_DB74
            | 0x8038_DC44
            | 0x8038_DCC0
            | 0x8045_2B74
            | 0x8045_3258
            | 0x8045_3B10
            | 0x8045_4E64
            | 0x8045_4E84
            | 0x8045_5B64
            | 0x8045_5BC4
            | 0x8045_6138
            | 0x8045_621C
            | 0x8045_622C
    )
}

fn emit_module(
    function: FunctionRange,
    body: &str,
    requires_exact_fpu_restart: bool,
) -> Result<String, TranslationError> {
    let mut output = String::new();
    writeln!(
        output,
        "#include \"galaxy/native_api.h\"\n#include \"galaxy/ppc_float.h\"\n\n#include <bit>\n#include <cstring>\n\nnamespace {{"
    )
    .map_err(|_| TranslationError::Formatting)?;
    output.push_str("const galaxy::NativeServicesV1* g_services = nullptr;\n\n");
    writeln!(
        output,
        "void translated_{:08X}(galaxy::PpcContext* __restrict context, galaxy::GuestMemoryV1* memory, const galaxy::NativeServicesV1* services) {{",
        function.address
    )
    .map_err(|_| TranslationError::Formatting)?;
    output.push_str("    static_cast<void>(context);\n");
    output.push_str("    static_cast<void>(memory);\n");
    output.push_str("    static_cast<void>(services);\n");
    emit_audio_trace_hook(&mut output, function.address)?;
    emit_main_frame_trace_hook(&mut output, function.address)?;
    emit_jutvideo_mq_trace_hook(&mut output, function.address)?;
    if should_emit_file_select_trace_hook(function.address) {
        writeln!(
            output,
            "    galaxy::trace_file_select_function_entry(services, context, memory, context->pc);"
        )
        .map_err(|_| TranslationError::Formatting)?;
    }
    if should_emit_movie_trace_hook(function.address) {
        writeln!(
            output,
            "    galaxy::trace_movie_function_entry(services, context, memory, context->pc);"
        )
        .map_err(|_| TranslationError::Formatting)?;
    }
    let native_body = if requires_exact_fpu_restart {
        None
    } else {
        native_function_body(function.address)
    };
    if let Some(native_body) = native_body {
        output.push_str(native_body);
    } else {
        output.push_str(body);
    }
    output.push_str("}\n\n");
    output.push_str(
        "galaxy::GameModuleManifestV1 make_manifest() {\n\
         \x20   galaxy::GameModuleManifestV1 manifest{};\n\
         \x20   std::memcpy(manifest.game_id, \"RMGE01\", 7);\n",
    );
    writeln!(
        output,
        "    std::memcpy(manifest.main_dol_sha1, \"{RMGE01_DOL_SHA1}\", 41);"
    )
    .map_err(|_| TranslationError::Formatting)?;
    writeln!(
        output,
        "    manifest.guest_entry_point = 0x{:08X}u;",
        function.address
    )
    .map_err(|_| TranslationError::Formatting)?;
    writeln!(
        output,
        "    manifest.translated_function_count = 1;\n    manifest.translated_instruction_count = {};\n    return manifest;\n}}\n\nconst galaxy::GameModuleManifestV1 kManifest = make_manifest();\n\n}}  // namespace\n",
        function.size / 4
    )
    .map_err(|_| TranslationError::Formatting)?;
    output.push_str(
        "extern \"C\" const galaxy::GameModuleManifestV1* galaxy_module_manifest() {\n\
         \x20   return &kManifest;\n\
         }\n\n\
         extern \"C\" bool galaxy_module_init(const galaxy::NativeServicesV1* services) {\n\
         \x20   if (services == nullptr || services->abi_version != galaxy::kNativeAbiVersion ||\n\
         \x20       services->struct_size < sizeof(galaxy::NativeServicesV1) ||\n\
         \x20       services->decrementer_written == nullptr) {\n\
         \x20       return false;\n\
         \x20   }\n\
         \x20   g_services = services;\n\
         \x20   return true;\n\
         }\n\n",
    );
    writeln!(
        output,
        "extern \"C\" void galaxy_module_entry(galaxy::PpcContext* context, galaxy::GuestMemoryV1* memory) {{\n    translated_{0:08X}(context, memory, g_services);\n}}\n\nextern \"C\" galaxy::NativeGameFunction galaxy_lookup_function(std::uint32_t guest_address) {{\n    return guest_address == 0x{0:08X}u ? &translated_{0:08X} : nullptr;\n}}",
        function.address
    )
    .map_err(|_| TranslationError::Formatting)?;
    Ok(output)
}

const FUNCTION_SHARD_PREFIX: &str = "#include \"functions.h\"\n\nnamespace rmge01 {\n\n";
const FUNCTION_SHARD_SUFFIX: &str = "}  // namespace rmge01\n";

fn render_sharded_function_definition(
    function: FunctionRange,
    body: &str,
    owners_with_interior_entries: &BTreeSet<u32>,
    exact_fpu_functions: &BTreeSet<u32>,
) -> Result<String, TranslationError> {
    let mut source = String::new();
    writeln!(
        source,
        "void fn_{:08X}(galaxy::PpcContext* __restrict context, galaxy::GuestMemoryV1* memory, const galaxy::NativeServicesV1* services) {{",
        function.address
    )
    .map_err(|_| TranslationError::Formatting)?;
    source.push_str("    static_cast<void>(context);\n");
    source.push_str("    static_cast<void>(memory);\n");
    source.push_str("    static_cast<void>(services);\n");
    emit_audio_trace_hook(&mut source, function.address)?;
    emit_main_frame_trace_hook(&mut source, function.address)?;
    emit_jutvideo_mq_trace_hook(&mut source, function.address)?;
    if should_emit_file_select_trace_hook(function.address) {
        writeln!(
            source,
            "    galaxy::trace_file_select_function_entry(services, context, memory, context->pc);"
        )
        .map_err(|_| TranslationError::Formatting)?;
    }
    if should_emit_movie_trace_hook(function.address) {
        writeln!(
            source,
            "    galaxy::trace_movie_function_entry(services, context, memory, context->pc);"
        )
        .map_err(|_| TranslationError::Formatting)?;
    }
    let native_body = if exact_fpu_functions.contains(&function.address) {
        None
    } else {
        native_function_body(function.address)
    };
    match native_body {
        Some(native_body) if owners_with_interior_entries.contains(&function.address) => {
            // The optimized helper is valid at the function's real entry only.
            // Resume aliases retain the exact translated body and its pc dispatch.
            writeln!(
                source,
                "    if (context->pc == 0x{:08X}u) {{",
                function.address
            )
            .map_err(|_| TranslationError::Formatting)?;
            source.push_str(native_body);
            // The lambda gives the exact fallback an independent label namespace.
            source.push_str("    } else {\n        [&]() {\n");
            source.push_str(body);
            source.push_str("        }();\n        return;\n    }\n");
        }
        Some(native_body) => source.push_str(native_body),
        None => source.push_str(body),
    }
    source.push_str("}\n\n");
    Ok(source)
}

fn install_typed_region_80165478(
    files: &mut Vec<GeneratedSource>,
    shard_names: &mut Vec<String>,
    shard_source_bytes: usize,
    body: &str,
) -> Result<(), TranslationError> {
    const ADDRESS: u32 = 0x8016_5478;
    const CALL: &str =
        "    context->pc = 0x80165478u;\n    rmge01::fn_80165478(context, memory, services);";
    const TYPED_CALL: &str =
        "    context->pc = 0x80165478u;\n    rmge01::fn_80165478_typed(context, memory, services);";
    let header = files
        .iter_mut()
        .find(|file| file.name == "functions.h")
        .ok_or(TranslationError::TypedRegionProof {
            address: ADDRESS,
            reason: "generated public function header is missing".to_owned(),
        })?;
    let header_end = "\n}  // namespace rmge01\n";
    if header.contents.matches(header_end).count() != 1 {
        return Err(TranslationError::TypedRegionProof {
            address: ADDRESS,
            reason: "generated header namespace shape changed".to_owned(),
        });
    }
    header.contents = header.contents.replacen(
        header_end,
        "void fn_80165478_typed(galaxy::PpcContext*, galaxy::GuestMemoryV1*, const galaxy::NativeServicesV1*);\n\n}  // namespace rmge01\n",
        1,
    );

    let mut rewritten = 0usize;
    for file in files
        .iter_mut()
        .filter(|file| file.name.starts_with("functions_") && file.name.ends_with(".cpp"))
    {
        rewritten += file.contents.matches(CALL).count();
        file.contents = file.contents.replace(CALL, TYPED_CALL);
        if file.contents.len() > shard_source_bytes {
            return Err(TranslationError::FunctionExceedsShardSourceBudget {
                address: ADDRESS,
                generated_bytes: file.contents.len(),
                limit_bytes: shard_source_bytes,
            });
        }
    }
    if rewritten == 0 {
        return Err(TranslationError::TypedRegionProof {
            address: ADDRESS,
            reason: "no canonical generated direct caller was selected".to_owned(),
        });
    }
    let contents = format!(
        "// WiiCompiled-derived typed GPR residency pilot; public entries use fn_80165478.\n#include \"functions.h\"\n\nnamespace rmge01 {{\nvoid fn_80165478_typed(galaxy::PpcContext* context, galaxy::GuestMemoryV1* memory, const galaxy::NativeServicesV1* services) {{\n    if (context->pc != 0x80165478u) {{\n        fn_80165478(context, memory, services);\n        return;\n    }}\n{body}}}\n}}  // namespace rmge01\n"
    );
    if contents.len() > shard_source_bytes {
        return Err(TranslationError::FunctionExceedsShardSourceBudget {
            address: ADDRESS,
            generated_bytes: contents.len(),
            limit_bytes: shard_source_bytes,
        });
    }
    let name = "typed_region_80165478.cpp".to_owned();
    shard_names.push(name.clone());
    files.push(GeneratedSource { name, contents });
    Ok(())
}

fn render_psmtx_local_experiment(experiment: &PsmtxLocalExperiment) -> String {
    let mut source = String::from(
        "// Exact install-time PSMTX local-lane lowering with the original static fallback.\n#include \"functions.h\"\n#include \"galaxy/ppc_paired_float.h\"\n#include <cstdio>\n\nnamespace rmge01 {\nvoid fn_804B5F3C_exact(galaxy::PpcContext* __restrict, galaxy::GuestMemoryV1*, const galaxy::NativeServicesV1*);\nnamespace {\n",
    );
    source.push_str(PSMTX_LOCAL_GUARD_SOURCE);
    if experiment.trace_guard {
        source.push_str(PSMTX_LOCAL_TRACE_SOURCE);
    }
    source.push_str("} // namespace\n\nvoid fn_804B5F3C(galaxy::PpcContext* __restrict context, galaxy::GuestMemoryV1* memory, const galaxy::NativeServicesV1* services) {\n");
    if experiment.trace_guard {
        // Flush before computing the guard: a diagnostic callback must not run
        // between the last proof and a cached operand use.
        source.push_str("    flush_psmtx_guard_counts(services);\n");
    }
    source.push_str(
        "    const std::uint32_t reason = psmtx_local_guard(context, memory, services);\n",
    );
    if experiment.trace_guard {
        source.push_str("    if (galaxy::main_frame_trace_window_active(services)) {\n        psmtx_counts.reasons[reason].fetch_add(1u, std::memory_order_relaxed);\n        psmtx_counts.pending.store(true, std::memory_order_release);\n    }\n");
    }
    source.push_str("    if (reason != 0u) {\n        fn_804B5F3C_exact(context, memory, services);\n        return;\n    }\n");
    source.push_str(&experiment.body);
    source.push_str("}\n} // namespace rmge01\n");
    source
}

fn render_psvec_cross_local_experiment(experiment: &PsvecCrossLocalExperiment) -> String {
    let mut source = String::from(
        "// Exact install-time PSVECCrossProduct local-lane lowering with the original static fallback.\n#include \"functions.h\"\n#include <cstdio>\n\nnamespace rmge01 {\nvoid fn_804B6CB8_exact(galaxy::PpcContext* __restrict, galaxy::GuestMemoryV1*, const galaxy::NativeServicesV1*);\nnamespace {\n",
    );
    source.push_str(PSVEC_CROSS_LOCAL_GUARD_SOURCE);
    if experiment.trace_guard {
        source.push_str(PSVEC_CROSS_LOCAL_TRACE_SOURCE);
    }
    source.push_str("} // namespace\n\nvoid fn_804B6CB8(galaxy::PpcContext* __restrict context, galaxy::GuestMemoryV1* memory, const galaxy::NativeServicesV1* services) {\n");
    if experiment.trace_guard {
        // Flush before computing the guard: a diagnostic callback must not run
        // between the last proof and a cached operand use.
        source.push_str("    flush_psvec_cross_guard_counts(services);\n");
    }
    source.push_str(
        "    const std::uint32_t reason = psvec_cross_local_guard(context, memory, services);\n",
    );
    if experiment.trace_guard {
        source.push_str("    if (galaxy::main_frame_trace_window_active(services)) {\n        psvec_cross_counts.pending = true;\n        ++psvec_cross_counts.reasons[reason];\n    }\n");
    }
    source.push_str("    if (reason != 0u) {\n        fn_804B6CB8_exact(context, memory, services);\n        return;\n    }\n");
    source.push_str(&experiment.body);
    source.push_str("}\n} // namespace rmge01\n");
    source
}

fn render_psvec_normalize_local_experiment(experiment: &PsvecNormalizeLocalExperiment) -> String {
    let mut source = String::from(
        "// Exact install-time PSVECNormalize local-lane lowering with the original static fallback.\n#include \"functions.h\"\n#include <cstdio>\n\nnamespace rmge01 {\nvoid fn_804B6BCC_exact(galaxy::PpcContext* __restrict, galaxy::GuestMemoryV1*, const galaxy::NativeServicesV1*);\nnamespace {\n",
    );
    source.push_str(PSVEC_NORMALIZE_LOCAL_GUARD_SOURCE);
    if experiment.trace_guard {
        source.push_str(PSVEC_NORMALIZE_LOCAL_TRACE_SOURCE);
    }
    source.push_str("} // namespace\n\nvoid fn_804B6BCC(galaxy::PpcContext* __restrict context, galaxy::GuestMemoryV1* memory, const galaxy::NativeServicesV1* services) {\n");
    if experiment.trace_guard {
        source.push_str("    flush_psvec_normalize_guard_counts(services);\n");
    }
    source.push_str(
        "    const std::uint32_t reason = psvec_normalize_local_guard(context, memory, services);\n",
    );
    if experiment.trace_guard {
        source.push_str("    if (galaxy::main_frame_trace_window_active(services)) {\n        psvec_normalize_counts.reasons[reason].fetch_add(1u, std::memory_order_relaxed);\n        psvec_normalize_counts.pending.store(true, std::memory_order_release);\n    }\n");
    }
    source.push_str("    if (reason != 0u) {\n        fn_804B6BCC_exact(context, memory, services);\n        return;\n    }\n");
    source.push_str(&experiment.body);
    source.push_str("}\n} // namespace rmge01\n");
    source
}

// These are hand-written proof predicates over the existing native ABI, not
// a matrix implementation. The arithmetic body is emitted from the user's
// verified dump by the ordinary instruction lowerer.
const PSMTX_LOCAL_GUARD_SOURCE: &str = r#"
bool psmtx_plain_ram_span(galaxy::GuestMemoryV1* memory,
    std::uint32_t address, std::uint32_t size) {
    const std::uint32_t alias = address >> 28u;
    if (alias != 0u && alias != 1u && alias != 8u &&
        alias != 9u && alias != 12u && alias != 13u) return false;
    const std::uint32_t physical = address & 0x1FFFFFFFu;
    const std::uint64_t end = static_cast<std::uint64_t>(physical) + size;
    const bool mem1 = physical < 0x01800000u && end <= 0x01800000ull;
    const bool mem2 = physical >= 0x10000000u && end <= 0x14000000ull;
    return (mem1 || mem2) && galaxy::resolve_guest_fast(memory, address, size) != nullptr;
}
bool psmtx_write_has_no_external_callback(galaxy::GuestMemoryV1* memory,
    std::uint32_t address, std::uint32_t size) {
    if (memory->notify_write == nullptr) return true;
    if (memory->dirty_page_words == nullptr) return false;
    std::uint32_t first_page = 0u;
    std::uint32_t last_page = 0u;
    return galaxy::guest_dirty_page_range_fast(address, size,
        memory->dirty_tracked_base, memory->dirty_tracked_size,
        memory->dirty_page_shift, memory->dirty_page_word_count,
        &first_page, &last_page);
}
std::uint32_t psmtx_local_guard(const galaxy::PpcContext* context,
    galaxy::GuestMemoryV1* memory, const galaxy::NativeServicesV1* services) {
    if (context == nullptr || memory == nullptr) return 1u;
    if (context->pc != 0x804B5F3Cu) return 2u;
    if ((context->msr & galaxy::kMsrFloatingPointAvailable) == 0u) return 3u;
    if ((context->hid2 & 0xA0000000u) != 0xA0000000u) return 4u;
    if (context->gqr[0] != 0u) return 5u;
    if ((context->fpscr & 0x000000F8u) != 0u) return 6u;
    if (context->gpr[1] < 0x40u) return 7u;
    if (services != nullptr && services->log != nullptr &&
        (galaxy::trace_fileloader_stack_enabled() ||
         galaxy::trace_fileloader_temp_writes_enabled() ||
         galaxy::trace_u32_store_enabled() ||
         galaxy::trace_audio_control_writes_enabled())) return 8u;
    const std::uint32_t frame = context->gpr[1] - 0x40u;
    if (!psmtx_plain_ram_span(memory, context->gpr[3], 48u)) return 9u;
    if (!psmtx_plain_ram_span(memory, context->gpr[4], 48u)) return 10u;
    if (!psmtx_plain_ram_span(memory, context->gpr[5], 48u)) return 11u;
    if (!psmtx_plain_ram_span(memory, frame, 0x40u)) return 12u;
    if (!psmtx_plain_ram_span(memory, 0x8069E148u, 8u)) return 13u;
    if (!psmtx_write_has_no_external_callback(memory, frame, 0x40u)) return 14u;
    if (!psmtx_write_has_no_external_callback(memory, context->gpr[5], 48u)) return 15u;
    return 0u;
}
"#;

// This is a proof predicate over the native ABI. Arithmetic remains ordinary
// translator output derived from the verified game executable.
const PSVEC_CROSS_LOCAL_GUARD_SOURCE: &str = r#"
bool psvec_cross_plain_ram_span(galaxy::GuestMemoryV1* memory,
    std::uint32_t address, std::uint32_t size) {
    const std::uint32_t alias = address >> 28u;
    if (alias != 0u && alias != 1u && alias != 8u &&
        alias != 9u && alias != 12u && alias != 13u) return false;
    const std::uint32_t physical = address & 0x1FFFFFFFu;
    const std::uint64_t end = static_cast<std::uint64_t>(physical) + size;
    const bool mem1 = physical < 0x01800000u && end <= 0x01800000ull;
    const bool mem2 = physical >= 0x10000000u && end <= 0x14000000ull;
    return (mem1 || mem2) &&
        galaxy::resolve_guest_fast(memory, address, size) != nullptr;
}
bool psvec_cross_write_has_no_external_callback(galaxy::GuestMemoryV1* memory,
    std::uint32_t address, std::uint32_t size) {
    if (memory->notify_write == nullptr) return true;
    if (memory->dirty_page_words == nullptr) return false;
    std::uint32_t first_page = 0u;
    std::uint32_t last_page = 0u;
    return galaxy::guest_dirty_page_range_fast(address, size,
        memory->dirty_tracked_base, memory->dirty_tracked_size,
        memory->dirty_page_shift, memory->dirty_page_word_count,
        &first_page, &last_page);
}
std::uint32_t psvec_cross_local_guard(const galaxy::PpcContext* context,
    galaxy::GuestMemoryV1* memory, const galaxy::NativeServicesV1* services) {
    if (context == nullptr || memory == nullptr) return 1u;
    if (context->pc != 0x804B6CB8u) return 2u;
    if ((context->msr & galaxy::kMsrFloatingPointAvailable) == 0u) return 3u;
    if ((context->hid2 & 0xA0000000u) != 0xA0000000u) return 4u;
    if (context->gqr[0] != 0u) return 5u;
    if ((context->fpscr & 0x000000F8u) != 0u) return 6u;
    if (services != nullptr && services->log != nullptr &&
        (galaxy::trace_fileloader_stack_enabled() ||
         galaxy::trace_fileloader_temp_writes_enabled() ||
         galaxy::trace_u32_store_enabled() ||
         galaxy::trace_audio_control_writes_enabled())) return 7u;
    if (!psvec_cross_plain_ram_span(memory, context->gpr[3], 12u)) return 8u;
    if (!psvec_cross_plain_ram_span(memory, context->gpr[4], 12u)) return 9u;
    if (!psvec_cross_plain_ram_span(memory, context->gpr[5], 12u)) return 10u;
    if (!psvec_cross_write_has_no_external_callback(
            memory, context->gpr[5], 12u)) return 11u;
    return 0u;
}
"#;

// Proof predicate for the normal entry only. The exact generated function
// remains the owner of every interior/FPU-retry entry and every rejected case.
const PSVEC_NORMALIZE_LOCAL_GUARD_SOURCE: &str = r#"
bool psvec_normalize_plain_ram_span(galaxy::GuestMemoryV1* memory,
    std::uint32_t address, std::uint32_t size) {
    const std::uint32_t alias = address >> 28u;
    if (alias != 0u && alias != 1u && alias != 8u &&
        alias != 9u && alias != 12u && alias != 13u) return false;
    const std::uint32_t physical = address & 0x1FFFFFFFu;
    const std::uint64_t end = static_cast<std::uint64_t>(physical) + size;
    const bool mem1 = physical < 0x01800000u && end <= 0x01800000ull;
    const bool mem2 = physical >= 0x10000000u && end <= 0x14000000ull;
    return (mem1 || mem2) &&
        galaxy::resolve_guest_fast(memory, address, size) != nullptr;
}
bool psvec_normalize_write_has_no_external_callback(galaxy::GuestMemoryV1* memory,
    std::uint32_t address, std::uint32_t size) {
    if (memory->notify_write == nullptr) return true;
    if (memory->dirty_page_words == nullptr) return false;
    std::uint32_t first_page = 0u;
    std::uint32_t last_page = 0u;
    return galaxy::guest_dirty_page_range_fast(address, size,
        memory->dirty_tracked_base, memory->dirty_tracked_size,
        memory->dirty_page_shift, memory->dirty_page_word_count,
        &first_page, &last_page);
}
std::uint32_t psvec_normalize_local_guard(const galaxy::PpcContext* context,
    galaxy::GuestMemoryV1* memory, const galaxy::NativeServicesV1* services) {
    if (context == nullptr || memory == nullptr) return 1u;
    if (context->pc != 0x804B6BCCu) return 2u;
    if ((context->msr & galaxy::kMsrFloatingPointAvailable) == 0u) return 3u;
    if ((context->hid2 & 0xA0000000u) != 0xA0000000u) return 4u;
    if (context->gqr[0] != 0u) return 5u;
    if ((context->fpscr & 0x000000F8u) != 0u) return 6u;
    if (services != nullptr && services->log != nullptr &&
        (galaxy::trace_fileloader_stack_enabled() ||
         galaxy::trace_fileloader_temp_writes_enabled() ||
         galaxy::trace_u32_store_enabled() ||
         galaxy::trace_audio_control_writes_enabled())) return 7u;
    if (!psvec_normalize_plain_ram_span(memory, context->gpr[3], 12u)) return 8u;
    if (!psvec_normalize_plain_ram_span(memory, context->gpr[4], 12u)) return 9u;
    if (!psvec_normalize_plain_ram_span(
            memory, context->gpr[2] + 0x000024E8u, 8u)) return 10u;
    if (!psvec_normalize_write_has_no_external_callback(
            memory, context->gpr[4], 12u)) return 11u;
    return 0u;
}
"#;

// No callbacks occur while the trace window is active. Counts are process-wide
// so a guest continuation resumed on another host worker can flush the complete
// window on its next Normalize call. Absence of a later call is reported as
// absence, never as a fabricated zero.
const PSVEC_CROSS_LOCAL_TRACE_SOURCE: &str = r#"
struct PsvecCrossGuardCounts { bool pending{}; std::uint64_t reasons[12]{}; };
thread_local PsvecCrossGuardCounts psvec_cross_counts;
void flush_psvec_cross_guard_counts(const galaxy::NativeServicesV1* services) {
    if (!psvec_cross_counts.pending || galaxy::main_frame_trace_window_active(services) ||
        services == nullptr || services->log == nullptr) return;
    char message[768]{};
    const auto* c = psvec_cross_counts.reasons;
    std::snprintf(message, sizeof(message),
        "[psvec-cross-local-guard] scope=thread-main-frame-window flush=next-call "
        "eligible=%llu missing=%llu interior=%llu fpu=%llu hid2=%llu gqr=%llu "
        "fp-exception=%llu trace=%llu lhs-ram=%llu rhs-ram=%llu dst-ram=%llu dst-callback=%llu",
        static_cast<unsigned long long>(c[0]), static_cast<unsigned long long>(c[1]),
        static_cast<unsigned long long>(c[2]), static_cast<unsigned long long>(c[3]),
        static_cast<unsigned long long>(c[4]), static_cast<unsigned long long>(c[5]),
        static_cast<unsigned long long>(c[6]), static_cast<unsigned long long>(c[7]),
        static_cast<unsigned long long>(c[8]), static_cast<unsigned long long>(c[9]),
        static_cast<unsigned long long>(c[10]), static_cast<unsigned long long>(c[11]));
    psvec_cross_counts = {};
    services->log(services->user, galaxy::LogLevelV1::Trace, message);
}
"#;

const PSVEC_NORMALIZE_LOCAL_TRACE_SOURCE: &str = r#"
struct PsvecNormalizeGuardCounts {
    std::atomic_bool pending{false};
    std::atomic_uint64_t reasons[12]{};
};
PsvecNormalizeGuardCounts psvec_normalize_counts;
void flush_psvec_normalize_guard_counts(const galaxy::NativeServicesV1* services) {
    if (galaxy::main_frame_trace_window_active(services) || services == nullptr ||
        services->log == nullptr ||
        !psvec_normalize_counts.pending.exchange(false, std::memory_order_acquire)) return;
    char message[768]{};
    std::uint64_t c[12]{};
    for (std::size_t index = 0; index < 12; ++index) {
        c[index] = psvec_normalize_counts.reasons[index].exchange(
            0u, std::memory_order_relaxed);
    }
    std::snprintf(message, sizeof(message),
        "[psvec-normalize-local-guard] scope=process-main-frame-window flush=next-call "
        "eligible=%llu missing=%llu interior=%llu fpu=%llu hid2=%llu gqr=%llu "
        "fp-exception=%llu trace=%llu src-ram=%llu dst-ram=%llu constants-ram=%llu dst-callback=%llu",
        static_cast<unsigned long long>(c[0]), static_cast<unsigned long long>(c[1]),
        static_cast<unsigned long long>(c[2]), static_cast<unsigned long long>(c[3]),
        static_cast<unsigned long long>(c[4]), static_cast<unsigned long long>(c[5]),
        static_cast<unsigned long long>(c[6]), static_cast<unsigned long long>(c[7]),
        static_cast<unsigned long long>(c[8]), static_cast<unsigned long long>(c[9]),
        static_cast<unsigned long long>(c[10]), static_cast<unsigned long long>(c[11]));
    services->log(services->user, galaxy::LogLevelV1::Trace, message);
}
"#;

// No callbacks or formatted output occur inside the requested trace window.
// Counts are process-wide because a translated continuation can resume on a
// different host worker. They flush on the next leaf call after the window
// closes. No such call means no summary, never a fabricated zero count.
const PSMTX_LOCAL_TRACE_SOURCE: &str = r#"
struct PsmtxGuardCounts {
    std::atomic_bool pending{false};
    std::atomic_uint64_t reasons[16]{};
};
PsmtxGuardCounts psmtx_counts;
void flush_psmtx_guard_counts(const galaxy::NativeServicesV1* services) {
    if (galaxy::main_frame_trace_window_active(services) || services == nullptr ||
        services->log == nullptr ||
        !psmtx_counts.pending.exchange(false, std::memory_order_acquire)) return;
    char message[1024]{};
    std::uint64_t c[16]{};
    for (std::size_t index = 0; index < 16; ++index) {
        c[index] = psmtx_counts.reasons[index].exchange(
            0u, std::memory_order_relaxed);
    }
    std::snprintf(message, sizeof(message),
        "[psmtx-local-guard] scope=process-main-frame-window flush=next-call "
        "eligible=%llu missing=%llu interior=%llu fpu=%llu hid2=%llu gqr=%llu "
        "fp-exception=%llu stack-wrap=%llu trace=%llu lhs-ram=%llu rhs-ram=%llu "
        "dst-ram=%llu stack-ram=%llu constant-ram=%llu stack-callback=%llu dst-callback=%llu",
        static_cast<unsigned long long>(c[0]), static_cast<unsigned long long>(c[1]),
        static_cast<unsigned long long>(c[2]), static_cast<unsigned long long>(c[3]),
        static_cast<unsigned long long>(c[4]), static_cast<unsigned long long>(c[5]),
        static_cast<unsigned long long>(c[6]), static_cast<unsigned long long>(c[7]),
        static_cast<unsigned long long>(c[8]), static_cast<unsigned long long>(c[9]),
        static_cast<unsigned long long>(c[10]), static_cast<unsigned long long>(c[11]),
        static_cast<unsigned long long>(c[12]), static_cast<unsigned long long>(c[13]),
        static_cast<unsigned long long>(c[14]), static_cast<unsigned long long>(c[15]));
    services->log(services->user, galaxy::LogLevelV1::Trace, message);
}
"#;

fn push_rendered_function_shard(
    files: &mut Vec<GeneratedSource>,
    shard_names: &mut Vec<String>,
    mut source: String,
) {
    source.push_str(FUNCTION_SHARD_SUFFIX);
    let name = format!("functions_{:04}.cpp", shard_names.len());
    shard_names.push(name.clone());
    files.push(GeneratedSource {
        name,
        contents: source,
    });
}

#[cfg(test)]
fn emit_sharded_module(
    functions: &[(FunctionRange, String)],
    aliases: &[(u32, u32)],
    shard_size: usize,
    guest_entry_point: u32,
) -> Result<Vec<GeneratedSource>, TranslationError> {
    emit_sharded_module_with_exact_fpu_and_source_budget(
        functions,
        aliases,
        shard_size,
        DEFAULT_MODULE_SHARD_SOURCE_KIB * KIBIBYTE,
        guest_entry_point,
        &BTreeSet::new(),
    )
}

#[cfg(test)]
fn emit_sharded_module_with_source_budget(
    functions: &[(FunctionRange, String)],
    aliases: &[(u32, u32)],
    shard_size: usize,
    shard_source_bytes: usize,
    guest_entry_point: u32,
) -> Result<Vec<GeneratedSource>, TranslationError> {
    emit_sharded_module_with_exact_fpu_and_source_budget(
        functions,
        aliases,
        shard_size,
        shard_source_bytes,
        guest_entry_point,
        &BTreeSet::new(),
    )
}

#[cfg(test)]
fn emit_sharded_module_with_exact_fpu(
    functions: &[(FunctionRange, String)],
    aliases: &[(u32, u32)],
    shard_size: usize,
    guest_entry_point: u32,
    exact_fpu_functions: &BTreeSet<u32>,
) -> Result<Vec<GeneratedSource>, TranslationError> {
    emit_sharded_module_with_exact_fpu_and_source_budget(
        functions,
        aliases,
        shard_size,
        DEFAULT_MODULE_SHARD_SOURCE_KIB * KIBIBYTE,
        guest_entry_point,
        exact_fpu_functions,
    )
}

#[cfg(test)]
fn emit_sharded_module_with_exact_fpu_and_source_budget(
    functions: &[(FunctionRange, String)],
    aliases: &[(u32, u32)],
    shard_size: usize,
    shard_source_bytes: usize,
    guest_entry_point: u32,
    exact_fpu_functions: &BTreeSet<u32>,
) -> Result<Vec<GeneratedSource>, TranslationError> {
    emit_sharded_module_with_experiment(
        functions,
        aliases,
        shard_size,
        shard_source_bytes,
        guest_entry_point,
        exact_fpu_functions,
        LocalPairedExperiments::default(),
    )
}

fn validate_function_shard_source_budget(
    files: &[GeneratedSource],
    limit_bytes: usize,
) -> Result<(), TranslationError> {
    // Packing uses original definitions to keep stable grouping. All later
    // includes and instrumentation must still fit the final emitted ceiling.
    for file in files {
        if file.name.starts_with("functions_")
            && file.name.ends_with(".cpp")
            && file.contents.len() > limit_bytes
        {
            return Err(TranslationError::ShardExceedsSourceBudget {
                name: file.name.clone(),
                generated_bytes: file.contents.len(),
                limit_bytes,
            });
        }
    }
    Ok(())
}

fn emit_sharded_module_with_experiment(
    functions: &[(FunctionRange, String)],
    aliases: &[(u32, u32)],
    shard_size: usize,
    shard_source_bytes: usize,
    guest_entry_point: u32,
    exact_fpu_functions: &BTreeSet<u32>,
    experiments: LocalPairedExperiments<'_>,
) -> Result<Vec<GeneratedSource>, TranslationError> {
    if shard_size == 0 {
        return Err(TranslationError::InvalidShardSize);
    }
    if shard_source_bytes == 0 {
        return Err(TranslationError::InvalidShardSourceBudget);
    }
    if functions.is_empty() {
        return Err(TranslationError::NoTranslatableFunctions);
    }

    // Lookup rows: function starts dispatch to their own body; interior alias
    // entries dispatch to the owning function's body (whose pc-dispatch
    // prologue routes to the matching label).
    let mut entries = functions
        .iter()
        .map(|(function, _)| (function.address, function.address))
        .chain(aliases.iter().copied())
        .collect::<Vec<_>>();
    entries.sort_unstable_by_key(|(address, _)| *address);
    let mut files = Vec::new();
    let owners_with_interior_entries = aliases
        .iter()
        .map(|(_, owner)| *owner)
        .collect::<BTreeSet<_>>();
    let mut header = String::from(
        "#pragma once\n\n#include \"galaxy/native_api.h\"\n#include \"galaxy/ppc_float.h\"\n\nnamespace rmge01 {\n\n",
    );
    for (function, _) in functions {
        writeln!(
            header,
            "void fn_{0:08X}(galaxy::PpcContext* __restrict context, galaxy::GuestMemoryV1* memory, const galaxy::NativeServicesV1* services);",
            function.address
        )
        .map_err(|_| TranslationError::Formatting)?;
    }
    header.push_str("\n}  // namespace rmge01\n");
    files.push(GeneratedSource {
        name: "functions.h".to_owned(),
        contents: header,
    });

    let fixed_shard_bytes = FUNCTION_SHARD_PREFIX.len() + FUNCTION_SHARD_SUFFIX.len();
    let mut shard_names = Vec::new();
    let mut source = String::from(FUNCTION_SHARD_PREFIX);
    let mut projected_source_bytes = fixed_shard_bytes;
    let mut functions_in_shard = 0_usize;
    for (function, body) in functions {
        let rendered = render_sharded_function_definition(
            *function,
            body,
            &owners_with_interior_entries,
            exact_fpu_functions,
        )?;
        let generated_bytes = fixed_shard_bytes.checked_add(rendered.len()).ok_or(
            TranslationError::FunctionExceedsShardSourceBudget {
                address: function.address,
                generated_bytes: usize::MAX,
                limit_bytes: shard_source_bytes,
            },
        )?;
        if generated_bytes > shard_source_bytes {
            return Err(TranslationError::FunctionExceedsShardSourceBudget {
                address: function.address,
                generated_bytes,
                limit_bytes: shard_source_bytes,
            });
        }

        let exceeds_count = functions_in_shard >= shard_size;
        let exceeds_source = match projected_source_bytes.checked_add(rendered.len()) {
            Some(size) => size > shard_source_bytes,
            None => true,
        };
        if functions_in_shard != 0 && (exceeds_count || exceeds_source) {
            push_rendered_function_shard(&mut files, &mut shard_names, source);
            source = String::from(FUNCTION_SHARD_PREFIX);
            projected_source_bytes = fixed_shard_bytes;
            functions_in_shard = 0;
        }
        // Keep grouping based on the original full rendered definitions. The
        // experimental normal-entry body lives in a separate TU; only this
        // definition's symbol changes, so later shards never slide.
        if experiments.psmtx.is_some() && function.address == 0x804B_5F3C {
            source.push_str(&rendered.replacen("void fn_804B5F3C(", "void fn_804B5F3C_exact(", 1));
        } else if experiments.psvec_cross.is_some() && function.address == 0x804B_6CB8 {
            source.push_str(&rendered.replacen("void fn_804B6CB8(", "void fn_804B6CB8_exact(", 1));
        } else if experiments.psvec_normalize.is_some() && function.address == 0x804B_6BCC {
            source.push_str(&rendered.replacen("void fn_804B6BCC(", "void fn_804B6BCC_exact(", 1));
        } else {
            source.push_str(&rendered);
        }
        if source.len() + FUNCTION_SHARD_SUFFIX.len() > shard_source_bytes {
            return Err(local_paired_proof_error(
                function.address,
                "fixed shard cannot hold the renamed fallback within its original source ceiling",
            ));
        }
        projected_source_bytes += rendered.len();
        functions_in_shard += 1;
    }
    if functions_in_shard != 0 {
        push_rendered_function_shard(&mut files, &mut shard_names, source);
    }
    // Include the optional display policy only in its RMGE01 owner
    // shards. Do this after packing so it cannot move any other function or
    // invalidate byte-identical objects from the previous proven build.
    for file in &mut files {
        if file.name.starts_with("functions_")
            && file.name.ends_with(".cpp")
            && file.contents.contains("void fn_80367D28(")
        {
            file.contents = file.contents.replacen(
                "#include \"functions.h\"\n",
                "#include \"functions.h\"\n#include \"galaxy/layout_aspect_cinema.h\"\n",
                1,
            );
        }
        if file.name.starts_with("functions_")
            && file.name.ends_with(".cpp")
            && (file.contents.contains("void fn_803F6B44(")
                || file.contents.contains("void fn_803F6D84(")
                || file.contents.contains("void fn_803CA6C4(")
                || file.contents.contains("void fn_804D0708(")
                || file.contents.contains("void fn_80366658(")
                || file.contents.contains("void fn_80366758(")
                || file.contents.contains("void fn_803626CC(")
                || file.contents.contains("void fn_80097288("))
        {
            file.contents = file.contents.replacen(
                "#include \"functions.h\"\n",
                "#include \"functions.h\"\n#include \"galaxy/experimental_ultrawide_aspect.h\"\n",
                1,
            );
        }
    }
    if let Some(experiment) = experiments.psmtx {
        if !functions
            .iter()
            .any(|(function, _)| function.address == 0x804B_5F3C)
        {
            return Err(local_paired_proof_error(
                0x804B_5F3C,
                "local-lane fallback is missing",
            ));
        }
        let contents = render_psmtx_local_experiment(experiment);
        if contents.len() > shard_source_bytes {
            return Err(TranslationError::FunctionExceedsShardSourceBudget {
                address: 0x804B_5F3C,
                generated_bytes: contents.len(),
                limit_bytes: shard_source_bytes,
            });
        }
        let name = "psmtx_local_lanes.cpp".to_owned();
        shard_names.push(name.clone());
        files.push(GeneratedSource { name, contents });
    }
    if let Some(experiment) = experiments.psvec_cross {
        if !functions
            .iter()
            .any(|(function, _)| function.address == 0x804B_6CB8)
        {
            return Err(local_paired_proof_error(
                0x804B_6CB8,
                "cross-product local-lane fallback is missing",
            ));
        }
        let contents = render_psvec_cross_local_experiment(experiment);
        if contents.len() > shard_source_bytes {
            return Err(TranslationError::FunctionExceedsShardSourceBudget {
                address: 0x804B_6CB8,
                generated_bytes: contents.len(),
                limit_bytes: shard_source_bytes,
            });
        }
        let name = "psvec_cross_local_lanes.cpp".to_owned();
        shard_names.push(name.clone());
        files.push(GeneratedSource { name, contents });
    }
    if let Some(experiment) = experiments.psvec_normalize {
        if !functions
            .iter()
            .any(|(function, _)| function.address == 0x804B_6BCC)
        {
            return Err(local_paired_proof_error(
                0x804B_6BCC,
                "normalize local-lane fallback is missing",
            ));
        }
        let contents = render_psvec_normalize_local_experiment(experiment);
        if contents.len() > shard_source_bytes {
            return Err(TranslationError::FunctionExceedsShardSourceBudget {
                address: 0x804B_6BCC,
                generated_bytes: contents.len(),
                limit_bytes: shard_source_bytes,
            });
        }
        if experiments.psmtx.is_some() {
            // Production-default PSMTX already owns a compact generated TU.
            // Keep the second local-lane leaf there so opting into Normalize
            // does not force a CMake/PCH regeneration or slide function shards.
            let shared = files
                .iter_mut()
                .find(|file| file.name == "psmtx_local_lanes.cpp")
                .expect("PSMTX experiment source must already exist");
            let combined = shared
                .contents
                .len()
                .checked_add(1)
                .and_then(|size| size.checked_add(contents.len()))
                .ok_or(TranslationError::FunctionExceedsShardSourceBudget {
                    address: 0x804B_6BCC,
                    generated_bytes: usize::MAX,
                    limit_bytes: shard_source_bytes,
                })?;
            if combined > shard_source_bytes {
                return Err(TranslationError::FunctionExceedsShardSourceBudget {
                    address: 0x804B_6BCC,
                    generated_bytes: combined,
                    limit_bytes: shard_source_bytes,
                });
            }
            shared.contents.push('\n');
            shared.contents.push_str(&contents);
        } else {
            let name = "psvec_normalize_local_lanes.cpp".to_owned();
            shard_names.push(name.clone());
            files.push(GeneratedSource { name, contents });
        }
    }
    if let Some(body) = experiments.typed_region_80165478 {
        if !functions
            .iter()
            .any(|(function, _)| function.address == 0x8016_5478)
        {
            return Err(TranslationError::TypedRegionProof {
                address: 0x8016_5478,
                reason: "ordinary public fallback is missing".to_owned(),
            });
        }
        install_typed_region_80165478(&mut files, &mut shard_names, shard_source_bytes, body)?;
    }

    let instruction_count: u64 = functions
        .iter()
        .map(|(function, _)| u64::from(function.size / 4))
        .sum();
    let mut module = String::from(
        "#include \"functions.h\"\n\n#include <array>\n#include <cstddef>\n#include <cstring>\n\nnamespace {\n\nconst galaxy::NativeServicesV1* g_services = nullptr;\n\nstruct FunctionRecord {\n    std::uint32_t address;\n    galaxy::NativeGameFunction function;\n};\n\n",
    );
    writeln!(
        module,
        "constexpr std::array<FunctionRecord, {}> kFunctions{{{{",
        entries.len()
    )
    .map_err(|_| TranslationError::Formatting)?;
    for (address, owner) in &entries {
        writeln!(module, "    {{0x{address:08X}u, &rmge01::fn_{owner:08X}}},")
            .map_err(|_| TranslationError::Formatting)?;
    }
    module.push_str("}};\n\ngalaxy::GameModuleManifestV1 make_manifest() {\n");
    module.push_str("    galaxy::GameModuleManifestV1 manifest{};\n");
    module.push_str("    std::memcpy(manifest.game_id, \"RMGE01\", 7);\n");
    writeln!(
        module,
        "    std::memcpy(manifest.main_dol_sha1, \"{RMGE01_DOL_SHA1}\", 41);"
    )
    .map_err(|_| TranslationError::Formatting)?;
    writeln!(
        module,
        "    manifest.guest_entry_point = 0x{guest_entry_point:08X}u;\n    manifest.translated_function_count = {}u;\n    manifest.translated_instruction_count = {instruction_count}ull;\n    return manifest;\n}}\n\nconst galaxy::GameModuleManifestV1 kManifest = make_manifest();\n\n}}  // namespace\n",
        functions.len()
    )
    .map_err(|_| TranslationError::Formatting)?;
    module.push_str(
        "extern \"C\" const galaxy::GameModuleManifestV1* galaxy_module_manifest() {\n\
         \x20   return &kManifest;\n\
         }\n\n\
         extern \"C\" bool galaxy_module_init(const galaxy::NativeServicesV1* services) {\n\
         \x20   if (services == nullptr || services->abi_version != galaxy::kNativeAbiVersion ||\n\
         \x20       services->struct_size < sizeof(galaxy::NativeServicesV1) ||\n\
         \x20       services->decrementer_written == nullptr) {\n\
         \x20       return false;\n\
         \x20   }\n\
         \x20   g_services = services;\n\
         \x20   return true;\n\
         }\n\n\
         extern \"C\" galaxy::NativeGameFunction galaxy_lookup_function(std::uint32_t guest_address) {\n\
         \x20   std::size_t low = 0;\n\
         \x20   std::size_t high = kFunctions.size();\n\
         \x20   while (low < high) {\n\
         \x20       const std::size_t middle = low + (high - low) / 2;\n\
         \x20       if (kFunctions[middle].address < guest_address) {\n\
         \x20           low = middle + 1;\n\
         \x20       } else {\n\
         \x20           high = middle;\n\
         \x20       }\n\
         \x20   }\n\
         \x20   return low < kFunctions.size() && kFunctions[low].address == guest_address\n\
         \x20              ? kFunctions[low].function\n\
         \x20              : nullptr;\n\
         }\n\n\
         extern \"C\" void galaxy_module_entry(\n\
         \x20   galaxy::PpcContext* context,\n\
         \x20   galaxy::GuestMemoryV1* memory) {\n\
         \x20   galaxy::NativeGameFunction entry = galaxy_lookup_function(kManifest.guest_entry_point);\n\
         \x20   if (entry == nullptr) {\n\
         \x20       if (g_services != nullptr && g_services->fatal != nullptr) {\n\
         \x20           g_services->fatal(g_services->user, kManifest.guest_entry_point, \"native module entry is unavailable\");\n\
         \x20       }\n\
         \x20       std::abort();\n\
         \x20   }\n\
         \x20   context->pc = kManifest.guest_entry_point;\n\
         \x20   entry(context, memory, g_services);\n\
         }\n",
    );
    files.push(GeneratedSource {
        name: "module.cpp".to_owned(),
        contents: module,
    });

    let mut cmake = String::from(
        "cmake_minimum_required(VERSION 3.24)\n\
         project(RMGE01NativeModule LANGUAGES CXX)\n\n\
         if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)\n\
         \x20   message(FATAL_ERROR \"RMGE01 native module requires Windows x64\")\n\
         endif()\n\
         if(NOT DEFINED GALAXY_RUNTIME_INCLUDE OR\n\
         \x20  NOT DEFINED GALAXY_PPC_FLOAT_LIBRARY OR\n\
         \x20  NOT DEFINED GALAXY_SOFTFLOAT_LIBRARY)\n\
         \x20   message(FATAL_ERROR \"Set the Nebula runtime include and float libraries\")\n\
         endif()\n\n\
         set(CMAKE_CXX_STANDARD 20)\n\
         set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\
         set(CMAKE_CXX_EXTENSIONS OFF)\n\n\
         # The complete module has hundreds of translation units; MSVC LTCG can exhaust\n\
         # install-time host memory. Keep whole-program optimization an explicit opt-in.\n\
         option(GALAXY_MODULE_ENABLE_LTCG \"Enable Release LTCG for the generated RMGE01 game module\" OFF)\n\
         option(GALAXY_MODULE_USE_PCH \"Precompile the shared generated RMGE01 function declarations\" ON)\n\
         set(GALAXY_MODULE_COMPILE_JOBS \"1\" CACHE STRING \"Maximum concurrent generated-module compiler processes\")\n\
         if(NOT GALAXY_MODULE_COMPILE_JOBS MATCHES \"^([1-9]|[1-5][0-9]|6[0-4])$\")\n\
         \x20   message(FATAL_ERROR \"GALAXY_MODULE_COMPILE_JOBS must be an integer from 1 to 64\")\n\
         endif()\n\
         if(CMAKE_GENERATOR MATCHES \"Ninja\")\n\
         \x20   set_property(GLOBAL PROPERTY JOB_POOLS \"galaxy_module_compile=${GALAXY_MODULE_COMPILE_JOBS}\")\n\
         endif()\n\
         set(GALAXY_NATIVE_ISA \"AUTO\" CACHE STRING \"Native CPU target: AUTO, SSE2, or AVX2\")\n\
         set_property(CACHE GALAXY_NATIVE_ISA PROPERTY STRINGS AUTO SSE2 AVX2)\n\n\
         if(MSVC)\n\
         \x20   if(NOT CMAKE_CXX_COMPILER_ID STREQUAL \"Clang\")\n\
         \x20       string(REGEX REPLACE \"(^| )/Ob[0-9]\" \"\\\\1/Ob3\" CMAKE_CXX_FLAGS_RELEASE \"${CMAKE_CXX_FLAGS_RELEASE}\")\n\
         \x20   endif()\n\
         \x20   string(TOUPPER \"${GALAXY_NATIVE_ISA}\" GALAXY_NATIVE_ISA)\n\
         \x20   if(NOT GALAXY_NATIVE_ISA MATCHES \"^(AUTO|SSE2|AVX2)$\")\n\
         \x20       message(FATAL_ERROR \"GALAXY_NATIVE_ISA must be AUTO, SSE2, or AVX2\")\n\
         \x20   endif()\n\
         \x20   set(galaxy_native_isa_effective \"${GALAXY_NATIVE_ISA}\")\n\
         \x20   if(GALAXY_NATIVE_ISA STREQUAL \"AUTO\")\n\
         \x20       if(CMAKE_CROSSCOMPILING)\n\
         \x20           set(galaxy_native_isa_effective \"SSE2\")\n\
         \x20       else()\n\
         \x20           include(CheckCXXSourceRuns)\n\
         \x20           check_cxx_source_runs(\n\
         \x20               \"#include <intrin.h>\n\
         \x20               int main() {\n\
         \x20                   int cpu[4] = {};\n\
         \x20                   __cpuid(cpu, 0);\n\
         \x20                   if (cpu[0] < 7) return 1;\n\
         \x20                   __cpuidex(cpu, 1, 0);\n\
         \x20                   constexpr int required_ecx = (1 << 12) | (1 << 27) | (1 << 28);\n\
         \x20                   if ((cpu[2] & required_ecx) != required_ecx) return 2;\n\
         \x20                   if ((_xgetbv(0) & 0x6u) != 0x6u) return 3;\n\
         \x20                   __cpuidex(cpu, 7, 0);\n\
         \x20                   return (cpu[1] & (1 << 5)) != 0 ? 0 : 4;\n\
         \x20               }\"\n\
         \x20               GALAXY_HOST_CAN_RUN_AVX2_FMA\n\
         \x20           )\n\
         \x20           if(GALAXY_HOST_CAN_RUN_AVX2_FMA)\n\
         \x20               set(galaxy_native_isa_effective \"AVX2\")\n\
         \x20           else()\n\
         \x20               set(galaxy_native_isa_effective \"SSE2\")\n\
         \x20           endif()\n\
         \x20       endif()\n\
         \x20   endif()\n\
         \x20   message(STATUS \"Galaxy module CPU target: ${galaxy_native_isa_effective} (requested ${GALAXY_NATIVE_ISA})\")\n\
         endif()\n\n\
         add_library(RMGE01_game SHARED\n\
         \x20   module.cpp\n",
    );
    // Largest shards first: Ninja starts them in this order, so the slowest
    // compiles do not trail at the end of a parallel build.
    let shard_size_of = |name: &String| {
        files
            .iter()
            .find(|file| &file.name == name)
            .map_or(0, |file| file.contents.len())
    };
    let mut ordered_shards: Vec<&String> = shard_names.iter().collect();
    ordered_shards.sort_by_key(|name| std::cmp::Reverse(shard_size_of(name)));
    for name in ordered_shards {
        writeln!(cmake, "    {name}").map_err(|_| TranslationError::Formatting)?;
    }
    cmake.push_str(
        ")\n\
         target_include_directories(RMGE01_game PRIVATE \"${GALAXY_RUNTIME_INCLUDE}\")\n\
         target_link_libraries(RMGE01_game PRIVATE\n\
         \x20   \"${GALAXY_PPC_FLOAT_LIBRARY}\"\n\
         \x20   \"${GALAXY_SOFTFLOAT_LIBRARY}\"\n\
         )\n\
         target_compile_definitions(RMGE01_game PRIVATE GALAXY_BUILDING_GAME_MODULE=1)\n\
         set_target_properties(RMGE01_game PROPERTIES PREFIX \"\")\n\
         if(GALAXY_MODULE_USE_PCH)\n\
         \x20   target_precompile_headers(RMGE01_game PRIVATE \"${CMAKE_CURRENT_SOURCE_DIR}/functions.h\")\n\
         endif()\n\
         if(CMAKE_GENERATOR MATCHES \"Ninja\")\n\
         \x20   set_property(TARGET RMGE01_game PROPERTY JOB_POOL_COMPILE galaxy_module_compile)\n\
         endif()\n\
         if(MSVC)\n\
         \x20   # Guest address preservation can emit intentional dead blocks after unconditional branches.\n\
         \x20   # AUTO keeps old x64 hosts on SSE2 and selects AVX2/FMA only when safe.\n\
         \x20   # /fp:precise remains mandatory; never add /fp:fast.\n\
         \x20   if(CMAKE_CXX_COMPILER_ID STREQUAL \"Clang\")\n\
         \x20       # clang-cl keeps MSVC's semantics here: no FMA contraction, wrapping\n\
         \x20       # signed arithmetic and no type-based alias analysis.\n\
         \x20       target_compile_options(RMGE01_game PRIVATE /EHsc /GS- /GR- -w -ffp-contract=off -fwrapv -fno-strict-aliasing -fno-slp-vectorize)\n\
         \x20       # The block above selects and REPORTS `galaxy_native_isa_effective`, and the\n\
         \x20       # module compatibility key includes GALAXY_NATIVE_ISA -- but /arch:AVX2 below\n\
         \x20       # is an MSVC cl.exe spelling, and Setup drives this graph with clang-cl where\n\
         \x20       # CMAKE_CXX_COMPILER_ID is \"Clang\". Without the flag in this branch an AVX2\n\
         \x20       # host is told \"Galaxy module CPU target: AVX2\" while every generated\n\
         \x20       # translation unit is compiled for the SSE2 baseline: two hosts sharing one\n\
         \x20       # module key can ship different machine code, and the translated code loses\n\
         \x20       # VEX encoding (three-operand, non-destructive), the wider register file and\n\
         \x20       # vector integer compares. clang-cl accepts the GCC-style -mavx2 (verified\n\
         \x20       # against the pinned toolchain: a probe compiles to vpsrlvd/vpand on ymm with\n\
         \x20       # it and to none without). The FMA flag is deliberately omitted -- FMA changes\n\
         \x20       # results and -ffp-contract=off already forbids contraction, so the target\n\
         \x20       # must not advertise the feature. AVX-512 flags are deliberately omitted too --\n\
         \x20       # the host probe above is the\n\
         \x20       # the AVX2+OSXSAVE test, which says nothing about AVX-512 availability.\n\
         \x20       if(galaxy_native_isa_effective STREQUAL \"AVX2\")\n\
         \x20           target_compile_options(RMGE01_game PRIVATE -mavx2)\n\
         \x20       endif()\n\
         \x20   else()\n\
         \x20       target_compile_options(RMGE01_game PRIVATE /W4 /WX /wd4702 /permissive- /EHsc /GS- /GR- /Oi /favor:INTEL64)\n\
         \x20   endif()\n\
         \x20   if(galaxy_native_isa_effective STREQUAL \"AVX2\" AND NOT CMAKE_CXX_COMPILER_ID STREQUAL \"Clang\")\n\
         \x20       target_compile_options(RMGE01_game PRIVATE /arch:AVX2)\n\
         \x20   endif()\n\
         \x20   option(GALAXY_MODULE_DEBUG_INFO \"Emit a PDB for the generated RMGE01 game module\" OFF)\n\
         \x20   if(GALAXY_MODULE_DEBUG_INFO)\n\
         \x20       target_compile_options(RMGE01_game PRIVATE /Zi)\n\
         \x20       target_link_options(RMGE01_game PRIVATE /DEBUG)\n\
         \x20   endif()\n\
         \x20   if(GALAXY_MODULE_ENABLE_LTCG AND NOT CMAKE_CXX_COMPILER_ID STREQUAL \"Clang\")\n\
         \x20       target_compile_options(RMGE01_game PRIVATE $<$<CONFIG:Release>:/GL> $<$<CONFIG:Release>:/Gw>)\n\
         \x20       target_link_options(RMGE01_game PRIVATE $<$<CONFIG:Release>:/LTCG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)\n\
         \x20   endif()\n\
         endif()\n",
    );
    files.push(GeneratedSource {
        name: "CMakeLists.txt".to_owned(),
        contents: cmake,
    });
    validate_function_shard_source_budget(&files, shard_source_bytes)?;
    Ok(files)
}

fn gpr_rt(word: u32) -> u32 {
    (word >> 21) & 0x1F
}

fn gpr_ra(word: u32) -> u32 {
    (word >> 16) & 0x1F
}

fn gpr_rb(word: u32) -> u32 {
    (word >> 11) & 0x1F
}

fn fpr_c(word: u32) -> u32 {
    (word >> 6) & 0x1F
}

fn signed_immediate(word: u32) -> i32 {
    (word as u16 as i16) as i32
}

fn cpp_u32_literal(value: u32) -> String {
    format!("0x{value:08X}u")
}

fn gpr_expression(index: u32) -> String {
    format!("context->gpr[{index}]")
}

// Arithmetic value operands read all 32 GPRs, including r0. Only specific
// encodings (addi/addis and effective-address RA=0) substitute literal zero;
// the addressing helpers below implement those exceptions. For example,
// li r0,7; add r3,r0,r4 must add 7, not silently discard the first operand.
fn gpr_u32_value_expression(index: u32) -> String {
    gpr_expression(index)
}

fn gpr_s32_value_expression(index: u32) -> String {
    format!("static_cast<std::int32_t>({})", gpr_expression(index))
}

fn gpr_plus_u32_expression(base: u32, value: u32) -> String {
    match (base, value) {
        (0, _) => cpp_u32_literal(value),
        (_, 0) => gpr_expression(base),
        _ => format!("{} + {}", gpr_expression(base), cpp_u32_literal(value)),
    }
}

fn gpr_plus_expression(base: u32, value: &str) -> String {
    if base == 0 {
        value.to_owned()
    } else {
        format!("{} + ({value})", gpr_expression(base))
    }
}

fn relocated_low16_expression(expression: &str) -> String {
    format!(
        "static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(({expression}) & 0xFFFFu)))"
    )
}

fn relocated_high_adjusted16_expression(expression: &str) -> String {
    format!("(({expression}) + 0x8000u) & 0xFFFF0000u")
}

fn effective_address_expression(base: u32, displacement: i32) -> String {
    gpr_plus_u32_expression(base, displacement as u32)
}

fn indexed_effective_address_expression(base: u32, index: u32) -> String {
    if base == 0 {
        gpr_expression(index)
    } else {
        format!("{} + {}", gpr_expression(base), gpr_expression(index))
    }
}

fn paired_single_displacement(word: u32) -> i32 {
    ((word << 20) as i32) >> 20
}

fn spr_number(word: u32) -> u32 {
    ((word >> 16) & 0x1F) | ((word >> 6) & 0x3E0)
}

fn direct_spr_read_expression(spr: u32) -> Option<String> {
    match spr {
        1 => Some("context->xer".to_owned()),
        8 => Some("context->lr".to_owned()),
        9 => Some("context->ctr".to_owned()),
        22 | 268 | 269 => None,
        912..=919 => Some(format!("context->gqr[{}]", spr - 912)),
        920 => Some("context->hid2".to_owned()),
        _ => Some(format!("context->spr[{spr}]")),
    }
}

fn direct_spr_write_expression(spr: u32) -> Option<String> {
    match spr {
        1 => Some("context->xer".to_owned()),
        8 => Some("context->lr".to_owned()),
        9 => Some("context->ctr".to_owned()),
        22 | 284 | 285 => None,
        912..=919 => Some(format!("context->gqr[{}]", spr - 912)),
        920 => Some("context->hid2".to_owned()),
        _ => Some(format!("context->spr[{spr}]")),
    }
}

fn record_bit(word: u32) -> bool {
    word & 1 != 0
}

fn branch_target(pc: u32, word: u32) -> u32 {
    let displacement = (((word & 0x03FF_FFFC) as i32) << 6) >> 6;
    if word & 2 != 0 {
        displacement as u32
    } else {
        pc.wrapping_add(displacement as u32)
    }
}

fn audited_native_tail_call(function: u32, pc: u32, target: u32) -> bool {
    // RMGE01's Vec::normalize wrapper only copies r3 to r4 before tail-calling
    // the translated paired-single implementation. The outer host invocation
    // still performs its normal completion pump after this native call returns.
    //
    // The NameObj dispatch helpers are tiny typed-accessor wrappers used during
    // scene transition.  They load the owning NameObj list pointer and tail-call
    // the shared dispatch loop at 0x802617B4; bypassing the host call dispatcher
    // here avoids thousands of RMGE01-local wrapper hops without touching OS or
    // device intercepts.
    matches!(
        (function, pc, target),
        (0x803E_4D24, 0x803E_4D28, 0x804B_6BCC)
            | (0x8026_C0A0, 0x8026_C0A4, 0x8026_17B4)
            | (0x8026_C0A8, 0x8026_C0AC, 0x8026_17B4)
            | (0x8026_C0E4, 0x8026_C0E8, 0x8026_17B4)
    )
}

fn conditional_branch_target(pc: u32, word: u32) -> u32 {
    let displacement = (word as u16 as i16) as i32 & !3;
    if word & 2 != 0 {
        displacement as u32
    } else {
        pc.wrapping_add(displacement as u32)
    }
}

fn branch_is_unconditional(bo: u32) -> bool {
    bo & 0x14 == 0x14
}

fn emit_branch_condition(
    _output: &mut String,
    bo: u32,
    bi: u32,
) -> Result<String, TranslationError> {
    // BO bits (bits 21-25 of the bc/bclr/bcctr word):
    //   BO[0] (0x10) = do not test CR
    //   BO[1] (0x08) = test CR bit == 1 (rather than == 0)
    //   BO[2] (0x04) = do not decrement/test CTR
    //   BO[3] (0x02) = test CTR == 0 after decrementing (rather than != 0)
    //
    // The previous form always built `(ctr_condition) && (cr_condition)` and
    // substituted the literal `true` for the operand the encoding leaves
    // untested. That is 79,297 sites emitting `(true) && (...)` and 1,129
    // emitting `(...) && (true)` module-wide. The compiler folds the literal
    // away, so this is chiefly an emitter/text-size fix, but it also removes
    // the last place an untested operand is still materialised.
    //
    // CTR is decremented and tested in one expression so the counter-controlled
    // loop-back edge carries a single read-modify-write of `context->ctr`
    // instead of a separate store followed by a reload (1,129 such loops
    // module-wide). Pre-decrement yields exactly the architectural value the
    // instruction tests, and the post-instruction CTR is identical on both the
    // taken and the fall-through path. All three call sites (`Bc`, `Bcctr`,
    // `Bclr`) interpolate the result into an `if (...)`, and `Bcctr` rejects
    // BO[2] == 0 outright, so the decrement is always evaluated exactly once.
    let ctr_condition = if bo & 0x4 != 0 {
        None
    } else {
        Some(format!(
            "--context->ctr {} 0u",
            if bo & 0x2 == 0 { "!=" } else { "==" }
        ))
    };
    let cr_condition = if bo & 0x10 != 0 {
        None
    } else if bo & 0x8 != 0 {
        Some(format!("galaxy::cr_bit(context, {bi}u)"))
    } else {
        Some(format!("!galaxy::cr_bit(context, {bi}u)"))
    };
    Ok(match (ctr_condition, cr_condition) {
        (None, None) => "true".to_owned(),
        (Some(ctr), None) => ctr,
        (None, Some(cr)) => cr,
        (Some(ctr), Some(cr)) => format!("({ctr}) && ({cr})"),
    })
}

fn internal_branch_targets(address: u32, bytes: &[u8]) -> std::collections::BTreeSet<u32> {
    let end = address.wrapping_add(bytes.len() as u32);
    bytes
        .chunks_exact(4)
        .enumerate()
        .filter_map(|(index, chunk)| {
            let pc = address + index as u32 * 4;
            let word = u32::from_be_bytes(chunk.try_into().expect("four-byte instruction"));
            let instruction = Ins::new(word, Extensions::gekko_broadway());
            if matches!(instruction.op, Opcode::B | Opcode::Bc) {
                let target = if instruction.op == Opcode::B {
                    branch_target(pc, word)
                } else {
                    conditional_branch_target(pc, word)
                };
                (target >= address && target < end).then_some(target)
            } else {
                None
            }
        })
        .collect()
}

fn direct_branch_targets(address: u32, bytes: &[u8]) -> BTreeSet<u32> {
    bytes
        .chunks_exact(4)
        .enumerate()
        .filter_map(|(index, chunk)| {
            let pc = address + index as u32 * 4;
            let word = u32::from_be_bytes(chunk.try_into().expect("four-byte instruction"));
            let instruction = Ins::new(word, Extensions::gekko_broadway());
            match instruction.op {
                Opcode::B => Some(branch_target(pc, word)),
                Opcode::Bc => Some(conditional_branch_target(pc, word)),
                _ => None,
            }
        })
        .collect()
}

#[cfg(test)]
fn function_can_fall_through(address: u32, bytes: &[u8]) -> bool {
    function_can_fall_through_from(address, bytes, address)
}

fn function_can_fall_through_from(address: u32, bytes: &[u8], entry: u32) -> bool {
    let instruction_count = bytes.len() / 4;
    let end = address.wrapping_add(bytes.len() as u32);
    if instruction_count == 0
        || entry < address
        || entry >= end
        || entry.wrapping_sub(address) % 4 != 0
    {
        return true;
    }

    let mut pending = vec![((entry - address) / 4) as usize];
    let mut visited = vec![false; instruction_count];
    while let Some(index) = pending.pop() {
        if index >= instruction_count || visited[index] {
            continue;
        }
        visited[index] = true;

        let pc = address.wrapping_add(index as u32 * 4);
        let offset = index * 4;
        let word = u32::from_be_bytes(
            bytes[offset..offset + 4]
                .try_into()
                .expect("four-byte instruction"),
        );
        let instruction = Ins::new(word, Extensions::gekko_broadway());
        let next = index + 1;

        match instruction.op {
            Opcode::B => {
                let target = branch_target(pc, word);
                if target >= address && target < end {
                    pending.push(((target - address) / 4) as usize);
                }
                if word & 1 != 0 && next < instruction_count {
                    pending.push(next);
                }
            }
            Opcode::Bc => {
                let target = conditional_branch_target(pc, word);
                if target >= address && target < end {
                    pending.push(((target - address) / 4) as usize);
                }
                if word & 1 != 0 {
                    if next < instruction_count {
                        pending.push(next);
                    }
                } else if !branch_is_unconditional((word >> 21) & 0x1F) {
                    if next == instruction_count {
                        return true;
                    }
                    pending.push(next);
                }
            }
            Opcode::Bcctr | Opcode::Bclr => {
                let link = word & 1 != 0;
                let unconditional = branch_is_unconditional((word >> 21) & 0x1F);
                if link {
                    if next < instruction_count {
                        pending.push(next);
                    }
                } else if !unconditional {
                    if next == instruction_count {
                        return true;
                    }
                    pending.push(next);
                }
            }
            Opcode::Rfi => {}
            _ => {
                if next == instruction_count {
                    return true;
                }
                pending.push(next);
            }
        }
    }
    false
}

fn internal_link_return_targets(address: u32, bytes: &[u8]) -> std::collections::BTreeSet<u32> {
    let end = address.wrapping_add(bytes.len() as u32);
    bytes
        .chunks_exact(4)
        .enumerate()
        .filter_map(|(index, chunk)| {
            let pc = address + index as u32 * 4;
            let word = u32::from_be_bytes(chunk.try_into().expect("four-byte instruction"));
            let instruction = Ins::new(word, Extensions::gekko_broadway());
            if !matches!(instruction.op, Opcode::B | Opcode::Bc) || word & 1 == 0 {
                return None;
            }
            let target = if instruction.op == Opcode::B {
                branch_target(pc, word)
            } else {
                conditional_branch_target(pc, word)
            };
            let return_address = pc.wrapping_add(4);
            (target >= address && target < end && return_address >= address && return_address < end)
                .then_some(return_address)
        })
        .collect()
}

fn emit_effective_address(
    output: &mut String,
    pc: u32,
    base: u32,
    displacement: i32,
) -> Result<String, TranslationError> {
    let name = format!("ea_{pc:08X}");
    let expression = effective_address_expression(base, displacement);
    writeln!(output, "    const std::uint32_t {name} = {expression};")
        .map_err(|_| TranslationError::Formatting)?;
    Ok(name)
}

fn emit_effective_address_expression(
    output: &mut String,
    pc: u32,
    base: u32,
    expression: &str,
) -> Result<String, TranslationError> {
    let name = format!("ea_{pc:08X}");
    let expression = gpr_plus_expression(base, expression);
    writeln!(output, "    const std::uint32_t {name} = {expression};")
        .map_err(|_| TranslationError::Formatting)?;
    Ok(name)
}

fn d_form_effective_address(
    output: &mut String,
    pc: u32,
    word: u32,
    base: u32,
    update: bool,
    immediate_override: Option<&PpcImmediateOverride>,
) -> Result<String, TranslationError> {
    let Some(immediate_override) = immediate_override else {
        return if update {
            emit_effective_address(output, pc, base, signed_immediate(word))
        } else {
            Ok(effective_address_expression(base, signed_immediate(word)))
        };
    };
    if immediate_override.kind != PpcImmediateKind::Low16 {
        return Err(TranslationError::InvalidImmediateOverride {
            address: pc,
            kind: immediate_override.kind,
            opcode: "D-form memory instruction".to_owned(),
        });
    }
    let displacement = relocated_low16_expression(&immediate_override.expression);
    if update {
        emit_effective_address_expression(output, pc, base, &displacement)
    } else {
        Ok(gpr_plus_expression(base, &displacement))
    }
}

fn emit_indexed_effective_address(
    output: &mut String,
    pc: u32,
    base: u32,
    index: u32,
) -> Result<String, TranslationError> {
    let name = format!("ea_{pc:08X}");
    let expression = indexed_effective_address_expression(base, index);
    writeln!(output, "    const std::uint32_t {name} = {expression};")
        .map_err(|_| TranslationError::Formatting)?;
    Ok(name)
}

fn rotate_mask(begin: u32, end: u32) -> u32 {
    let from_begin = u32::MAX >> begin;
    let through_end = u32::MAX << (31 - end);
    if begin <= end {
        from_begin & through_end
    } else {
        from_begin | through_end
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // Exact DATA/sys/main.dol RMGE01 0x80165478..0x80165594 words. The
    // fixture is independent of game assets on the test machine.
    fn typed_region_80165478_fixture() -> (Vec<u8>, BTreeSet<u32>) {
        let words: [u32; 71] = [
            0x9421FFF0, 0x7C0802A6, 0x90010014, 0x93E1000C, 0x7C7F1B78, 0x80830048, 0x2C040000,
            0x41820028, 0x88030069, 0x2C000000, 0x4082001C, 0x7C832378, 0x48003AE5, 0x807F004C,
            0x2C030000, 0x41820008, 0x4BFF640D, 0x7FE3FB78, 0x48278415, 0x2C030000, 0x4182000C,
            0x7FE3FB78, 0x48278B35, 0x807F0054, 0x2C030000, 0x41820008, 0x4BFFECE5, 0x881F0068,
            0x2C000000, 0x40820094, 0x807F0050, 0x2C030000, 0x41820008, 0x4800A5ED, 0x881F0068,
            0x2C000000, 0x40820078, 0x819F0000, 0x7FE3FB78, 0x818C0048, 0x7D8903A6, 0x4E800421,
            0x881F0068, 0x2C000000, 0x40820058, 0x7FE3FB78, 0x48000AD1, 0x807F0060, 0x2C030000,
            0x41820008, 0x4BFFD12D, 0x807F0088, 0x2C030000, 0x41820008, 0x4BFF71B9, 0x801F0084,
            0x2C000000, 0x4182000C, 0x7FE3FB78, 0x48277085, 0x7FE3FB78, 0x4825D099, 0x7FE3FB78,
            0x482939CD, 0x7FE3FB78, 0x48007269, 0x80010014, 0x83E1000C, 0x7C0803A6, 0x38210010,
            0x4E800020,
        ];
        let entries = BTreeSet::from([
            0x8016_54AC,
            0x8016_54BC,
            0x8016_54C4,
            0x8016_54D4,
            0x8016_54E4,
            0x8016_5500,
            0x8016_5520,
            0x8016_5534,
            0x8016_5544,
            0x8016_5554,
            0x8016_5568,
            0x8016_5570,
            0x8016_5578,
            0x8016_5580,
        ]);
        (
            words.into_iter().flat_map(u32::to_be_bytes).collect(),
            entries,
        )
    }

    #[test]
    fn exact_typed_region_records_actual_words_and_keeps_all_boundaries() {
        let (bytes, entries) = typed_region_80165478_fixture();
        let mask = typed_region_80165478_mask(&bytes, &entries, &entries).unwrap();
        assert_ne!(mask & (1 << 1), 0);
        assert_ne!(mask & (1 << 3), 0);
        assert_ne!(mask & (1 << 31), 0);
        let ordinary = lower_words_with_config(
            0x8016_5478,
            &bytes,
            &entries,
            &entries,
            LoweringConfig::default(),
        )
        .unwrap();
        let typed = lower_words_with_config(
            0x8016_5478,
            &bytes,
            &entries,
            &entries,
            LoweringConfig {
                typed_region_gpr_mask: mask,
                ..LoweringConfig::default()
            },
        )
        .unwrap();
        assert!(!ordinary.contains("resident_r"));
        // `or r31,r3,r3` is the move encoding, so the resident rewrite sees the
        // collapsed copy form. cache_typed_region_instruction accounts for that
        // one missing operand in its count proof.
        assert!(typed.contains("resident_r31 = resident_r3;"));
        assert!(!typed.contains("resident_r31 = resident_r3 | resident_r3;"));
        assert!(typed.contains("context->gpr[1] = resident_r1;"));
        assert!(typed.contains("resident_r1 = context->gpr[1];"));
        assert!(typed.contains("galaxy::call_guest_cached(services, branch_target"));
        for entry in entries {
            assert!(typed.contains(&format!(
                "case 0x{entry:08X}u: goto call_return_{entry:08X};"
            )));
            assert!(typed.contains(&format!("call_return_{entry:08X}:")));
        }
        let mut corrupted = bytes;
        corrupted[0] ^= 1;
        assert!(matches!(
            typed_region_80165478_mask(&corrupted, &BTreeSet::new(), &BTreeSet::new()),
            Err(TranslationError::TypedRegionProof { .. })
        ));
    }

    #[test]
    fn typed_region_rewrites_only_exact_canonical_static_calls() {
        let mut files = vec![
            GeneratedSource {
                name: "functions.h".to_owned(),
                contents: "namespace rmge01 {\n\n}  // namespace rmge01\n".to_owned(),
            },
            GeneratedSource {
                name: "functions_0000.cpp".to_owned(),
                contents: "    context->pc = 0x80165478u;\n    rmge01::fn_80165478(context, memory, services);\n    galaxy::call_guest_cached(services, target, cache, function, context, memory, pc);\n".to_owned(),
            },
            GeneratedSource {
                name: "module.cpp".to_owned(),
                contents: "{0x80165478u, &rmge01::fn_80165478}".to_owned(),
            },
        ];
        let mut shards = vec!["functions_0000.cpp".to_owned()];
        install_typed_region_80165478(&mut files, &mut shards, 4096, "    return;\n").unwrap();
        assert!(files[1].contents.contains("rmge01::fn_80165478_typed("));
        assert!(files[2].contents.contains("&rmge01::fn_80165478}"));
        assert!(files[0].contents.contains("void fn_80165478_typed("));
        assert!(shards.contains(&"typed_region_80165478.cpp".to_owned()));
    }

    #[test]
    fn flat_ram_integer_load_selection_preserves_default_and_guest_effect_order() {
        let words: [u32; 6] = [
            (34 << 26) | (3 << 21) | (4 << 16) | 1,      // lbz r3,1(r4)
            (42 << 26) | (5 << 21) | (6 << 16) | 0xFFFE, // lha r5,-2(r6)
            (33 << 26) | (7 << 21) | (8 << 16) | 4,      // lwzu r7,4(r8)
            (31 << 26) | (9 << 21) | (10 << 16) | (11 << 11) | (23 << 1), // lwzx
            (46 << 26) | (24 << 21) | (1 << 16) | 8,     // lmw r24,8(r1)
            0x4E80_0020,                                 // blr
        ];
        let bytes = words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect::<Vec<_>>();
        let entries = BTreeSet::from([0x8000_4004]);
        let ordinary = lower_words_with_entries(0x8000_4000, &bytes, &entries).unwrap();
        let disabled = lower_words_with_config(
            0x8000_4000,
            &bytes,
            &entries,
            &BTreeSet::new(),
            LoweringConfig::default(),
        )
        .unwrap();
        assert_eq!(
            ordinary, disabled,
            "default generated body must be unchanged"
        );
        let flat = lower_words_with_config(
            0x8000_4000,
            &bytes,
            &entries,
            &BTreeSet::new(),
            LoweringConfig {
                flat_ram_reads: true,
                ..LoweringConfig::default()
            },
        )
        .unwrap();
        assert!(flat.starts_with(
            "    [[maybe_unused]] const std::byte* flat_guest_read_base = galaxy::guest_flat_read_base(memory);\n"
        ));
        assert!(flat.contains("case 0x80004004u: goto label_80004004;"));
        assert!(flat.contains("guest_load_flat_or_checked_u8(flat_guest_read_base, memory, context->gpr[4] + 0x00000001u, services, 0x80004000u)"));
        assert!(flat.contains("static_cast<std::int16_t>(galaxy::guest_load_flat_or_checked_u16(flat_guest_read_base, memory,"));
        assert!(flat.contains("guest_load_flat_or_checked_u32(flat_guest_read_base, memory, ea_80004008, services, 0x80004008u)"));
        let load = flat
            .find("context->gpr[7] = galaxy::guest_load_flat_or_checked_u32")
            .unwrap();
        let update = flat.find("context->gpr[8] = ea_80004008;").unwrap();
        assert!(
            load < update,
            "update-form RA must commit after a successful load"
        );
        assert!(flat.contains("guest_load_flat_or_checked_u32(flat_guest_read_base, memory, context->gpr[10] + context->gpr[11], services, 0x8000400Cu)"));
        assert!(flat.contains("guest_load_flat_or_checked_u32(flat_guest_read_base, memory, ea_80004010 + (reg - 24u) * 4u, services, 0x80004010u)"));
        assert_eq!(flat.matches("guest_load_flat_or_checked_").count(), 5);
        assert!(!flat.contains("guest_store_flat"));
    }

    #[test]
    fn flat_ram_integer_loads_leave_fp_and_checked_store_paths_unchanged() {
        let words: [u32; 4] = [
            (32 << 26) | (3 << 21) | (4 << 16), // lwz r3,0(r4)
            (48 << 26) | (1 << 21) | (2 << 16), // lfs f1,0(r2)
            (36 << 26) | (3 << 21) | (4 << 16), // stw r3,0(r4)
            0x4E80_0020,
        ];
        let bytes = words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect::<Vec<_>>();
        let flat = lower_words_with_config(
            0x8000_4000,
            &bytes,
            &BTreeSet::new(),
            &BTreeSet::new(),
            LoweringConfig {
                flat_ram_reads: true,
                ..LoweringConfig::default()
            },
        )
        .unwrap();
        assert_eq!(flat.matches("guest_load_flat_or_checked_").count(), 1);
        assert!(flat.contains("galaxy::load_fpr_single(context, 1u, memory,"));
        assert!(flat.contains("galaxy::guest_store_u32(memory,"));
    }

    #[test]
    fn resident_integer_leaf_uses_typed_read_before_write_and_writeback() {
        let words: [u32; 4] = [
            (14 << 26) | (3 << 21) | (4 << 16) | 5, // addi r3,r4,5
            (14 << 26) | (3 << 21) | (3 << 16) | 2, // addi r3,r3,2
            (24 << 26) | (3 << 21) | (5 << 16),     // ori r5,r3,0
            0x4E80_0020,                            // blr
        ];
        let bytes = words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect::<Vec<_>>();
        let output =
            lower_resident_integer_leaf(0x8000_4000, &bytes, &BTreeSet::new(), &BTreeSet::new())
                .expect("straight-line integer leaf is eligible");
        assert!(output.contains("resident_r4 = context->gpr[4]"));
        assert!(output.contains("resident_r3 = 0u"));
        assert!(!output.contains("resident_r3 = context->gpr[3]"));
        assert!(output.contains("context->gpr[3] = resident_r3"));
        assert!(output.contains("context->gpr[5] = resident_r5"));
        assert!(!output.contains("context->gpr[4] = resident_r4"));
        assert!(output.ends_with("    return;\n"));
    }

    #[test]
    fn resident_integer_leaf_falls_back_for_interior_entries_and_memory() {
        let words: [u32; 2] = [(14 << 26) | (3 << 21) | (4 << 16) | 5, 0x4E80_0020];
        let bytes = words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect::<Vec<_>>();
        let entries = BTreeSet::from([0x8000_4004]);
        assert!(
            lower_resident_integer_leaf(0x8000_4000, &bytes, &entries, &BTreeSet::new(),).is_none()
        );
        let mut with_load = bytes.clone();
        with_load.splice(0..4, ((32u32 << 26) | (3 << 21) | (4 << 16)).to_be_bytes());
        assert!(lower_resident_integer_leaf(
            0x8000_4000,
            &with_load,
            &BTreeSet::new(),
            &BTreeSet::new(),
        )
        .is_none());
    }

    fn resident_call_fixture(known_callee: bool, extra_entry: bool) -> (String, u32) {
        let address = 0x8000_4000u32;
        let target = 0x8000_5000u32;
        let call_pc = address + 8;
        let displacement = target.wrapping_sub(call_pc) & 0x03FF_FFFC;
        let words = [
            0x7C08_02A6,                            // mflr r0
            (14 << 26) | (4 << 21) | (5 << 16) | 1, // addi r4,r5,1
            0x4800_0001 | displacement,             // bl target
            (14 << 26) | (6 << 21) | (3 << 16) | 2, // addi r6,r3,2
            0x7C08_03A6,                            // mtlr r0
            0x4E80_0020,                            // blr
        ];
        let bytes = words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect::<Vec<_>>();
        let mut entries = BTreeSet::from([call_pc + 4]);
        if extra_entry {
            entries.insert(address + 16);
        }
        let mut contracts = BTreeMap::new();
        if known_callee {
            let callee_words = [(14u32 << 26) | (3 << 21) | (4 << 16) | 5, 0x4E80_0020];
            let callee_bytes = callee_words
                .iter()
                .flat_map(|word| word.to_be_bytes())
                .collect::<Vec<_>>();
            let (_, contract) = analyze_resident_integer_leaf(
                target,
                &callee_bytes,
                &BTreeSet::new(),
                &BTreeSet::new(),
            )
            .expect("pure integer callee has an exact contract");
            contracts.insert(target, contract);
        }
        let mut callable = BTreeMap::new();
        if known_callee {
            callable.insert(target, target);
        }
        let output = lower_resident_integer_direct_calls(
            address,
            &bytes,
            &entries,
            &BTreeSet::from([call_pc + 4]),
            &callable,
            &BTreeSet::new(),
            &contracts,
        )
        .expect("formatting succeeds")
        .expect("linked straight-line integer caller is eligible");
        (output, call_pc)
    }

    #[test]
    fn resident_direct_call_uses_verified_contract_and_public_resume_checkpoint() {
        let (output, call_pc) = resident_call_fixture(true, true);
        let call_site = output
            .find("rmge01::fn_80005000(context, memory, services);")
            .expect("proven direct callee remains a native call");
        let pre_call = &output[..call_site];
        assert!(pre_call.ends_with("    context->gpr[3] = resident_r3;\n    context->gpr[4] = resident_r4;\n    context->pc = 0x80005000u;\n    "));
        assert!(!pre_call.contains("    context->gpr[5] = resident_r5;"));
        let after_call = &output[call_site..];
        assert!(after_call.starts_with(
            "rmge01::fn_80005000(context, memory, services);\n    resident_r3 = context->gpr[3];"
        ));
        assert!(after_call.contains("    context->gpr[5] = resident_r5;\n    context->gpr[6] = resident_r6;\n    galaxy::call_return_checkpoint"));
        assert!(output.contains("case 0x8000400Cu: goto call_return_8000400C;"));
        assert!(output.contains("case 0x80004010u: goto label_80004010;"));
        assert!(output.contains("call_return_8000400C:\n    galaxy::call_return_checkpoint(services, 0x80004008u, 0x8000400Cu, context, memory);\n    resident_r0 = context->gpr[0];"));
        assert_eq!(
            output
                .matches(&format!("0x{call_pc:08X}u, 0x8000400Cu, context, memory"))
                .count(),
            2
        );
    }

    #[test]
    fn resident_unknown_direct_call_is_a_complete_context_fence() {
        let (output, _) = resident_call_fixture(false, false);
        let call_site = output
            .find("galaxy::call_guest(services, 0x80005000u")
            .expect("unresolved call retains host dispatch");
        let pre_call = &output[..call_site];
        assert!(pre_call.ends_with("    context->gpr[6] = resident_r6;\n    "));
        let after_call = &output[call_site..];
        assert!(after_call.contains("    resident_r0 = context->gpr[0];"));
        assert!(after_call.contains("    resident_r6 = context->gpr[6];"));
    }

    #[test]
    fn resident_stack_accesses_keep_checked_fault_and_update_order() {
        let address = 0x8000_4000u32;
        let target = 0x8000_5000u32;
        let call_pc = address + 12;
        let displacement = target.wrapping_sub(call_pc) & 0x03FF_FFFC;
        let words = [
            0x9421_FFF0u32,             // stwu r1,-16(r1)
            0x7C08_02A6,                // mflr r0
            0x9001_0014,                // stw r0,20(r1)
            0x4800_0001 | displacement, // bl target
            0x8001_0014,                // lwz r0,20(r1)
            0x7C08_03A6,                // mtlr r0
            0x3821_0010,                // addi r1,r1,16
            0x4E80_0020,                // blr
        ];
        let bytes = words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect::<Vec<_>>();
        let return_pc = call_pc + 4;
        let entries = BTreeSet::from([return_pc]);
        let output = lower_resident_integer_direct_calls(
            address,
            &bytes,
            &entries,
            &entries,
            &BTreeMap::new(),
            &BTreeSet::new(),
            &BTreeMap::new(),
        )
        .expect("formatting succeeds")
        .expect("checked stack memory is eligible");
        let store = output.find("galaxy::guest_store_u32(memory, ea_80004000, context->gpr[1], services, 0x80004000u);").unwrap();
        let update = output.find("context->gpr[1] = ea_80004000;").unwrap();
        assert!(
            store < update,
            "stack pointer update follows successful store"
        );
        assert!(output[..store].contains("context->gpr[1] = resident_r1;"));
        assert!(output[update..].contains("resident_r1 = context->gpr[1];"));
        let load = output
            .find("context->gpr[0] = galaxy::guest_load_u32(memory")
            .unwrap();
        assert!(output[..load].contains("context->gpr[0] = resident_r0;"));
        assert!(output[load..].contains("resident_r0 = context->gpr[0];"));
        assert!(output.contains("case 0x80004010u: goto call_return_80004010;"));
    }

    #[test]
    fn resident_direct_call_rejects_checked_memory_and_missing_resume_entry() {
        let address = 0x8000_4000;
        let words = [0x8064_0000u32, 0x4800_0FFDu32, 0x4E80_0020u32];
        let bytes = words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect::<Vec<_>>();
        let entries = BTreeSet::from([address + 8]);
        assert!(lower_resident_integer_direct_calls(
            address,
            &bytes,
            &entries,
            &entries,
            &BTreeMap::new(),
            &BTreeSet::new(),
            &BTreeMap::new(),
        )
        .expect("classified fallback")
        .is_none());
        let call_words = [0x4800_1001u32, 0x4E80_0020u32];
        let call_bytes = call_words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect::<Vec<_>>();
        assert!(lower_resident_integer_direct_calls(
            address,
            &call_bytes,
            &BTreeSet::new(),
            &BTreeSet::new(),
            &BTreeMap::new(),
            &BTreeSet::new(),
            &BTreeMap::new(),
        )
        .expect("unregistered public continuation falls back")
        .is_none());
    }

    fn lower_instruction_fixture(address: u32, words: &[u32]) -> String {
        let bytes = words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect::<Vec<_>>();
        lower_words(address, &bytes).expect("instruction fixture lowers")
    }

    #[test]
    fn rlwinm_rotate_amount_matches_the_encoding() {
        // Guards the rewrite that was tried and reverted: a pre-rotated-mask
        // form (`rotl(v, 32-SH) & (MASK <<< (32-SH))`) is NOT equivalent to
        // `rotl(v, SH) & MASK` for SH != 0, because rotating `v` by anything
        // other than SH moves its bits relative to a mask whose position MB/ME
        // fixes. An exhaustive check over all 32x32x32 (SH, MB, ME) triples
        // mismatched 319,448 of 393,216 encodings, so the emitted rotate amount
        // and mask must stay exactly `SH` and `rotate_mask(MB, ME)`.
        //
        // SH is bits 11-15, MB bits 6-10, ME bits 1-5.
        let rlwinm = |sh: u32, mb: u32, me: u32| {
            (21_u32 << 26) | (0 << 21) | (0 << 16) | (sh << 11) | (mb << 6) | (me << 1)
        };
        // slwi r0,r0,2  -> SH=2, MB=0, ME=29, MASK=0xFFFFFFFC
        let rotated = lower_instruction_fixture(
            0x8000_4000,
            &[rlwinm(2, 0, 29), 0x4E80_0020],
        );
        assert!(
            rotated.contains("std::rotl(context->gpr[0], 2) & 0xFFFFFFFCu;"),
            "{rotated}"
        );
        // clrrwi r3,r4,31 -> SH=0, MB=0, ME=0, MASK=0x80000000
        let zero_shift = lower_instruction_fixture(
            0x8000_4000,
            &[rlwinm(0, 0, 0), 0x4E80_0020],
        );
        assert!(
            zero_shift.contains("context->gpr[0] = (context->gpr[0] & 0x80000000u);"),
            "{zero_shift}"
        );
        assert!(
            !zero_shift.contains("std::rotl"),
            "shift == 0 must not emit a rotate: {zero_shift}"
        );
    }

    #[test]
    fn shared_integer_operands_preserve_legacy_default_cpp_text() {
        // These are exact words from the verified 0x803A3B6C RMGE01 stream.
        // The expected lines are the pre-refactor emitter spellings, including
        // RA=0 literal semantics, unsigned hex formatting, and register order.
        let words = [
            0x3961_0060, // addi r11,r1,0x60
            0x3fe0_805c, // addis r31,r0,0x805c
            0x7c7d_1b78, // or r29,r3,r3 (mr)
            0x5400_103a, // rlwinm r0,r0,2,0,29 (slwi)
            0x4e80_0020, // blr
        ];
        let output = lower_instruction_fixture(0x803a_3b6c, &words);
        for old_line in [
            "    context->gpr[11] = context->gpr[1] + 0x00000060u;",
            "    context->gpr[31] = 0x805C0000u;",
            // `or r29,r3,r3` is the move encoding: it must lower to a copy, not
            // to a self-or that re-reads the same 4 MB-context slot twice.
            "    context->gpr[29] = context->gpr[3];",
            // `rlwinm r0,r0,2,0,29` (slwi r0,r0,2): shift != 0, so the rotate
            // stays exactly `shift` and only the mask is applied. The mask is
            // the original MB..ME run (0xFFFFFFFC), not a pre-rotated one.
            "    context->gpr[0] = std::rotl(context->gpr[0], 2) & 0xFFFFFFFCu;",
        ] {
            assert_eq!(output.matches(old_line).count(), 1, "{old_line}");
        }
        assert_eq!(
            output
                .matches("    context->gpr[29] = context->gpr[3] | context->gpr[3];")
                .count(),
            0,
            "the self-or spelling must not survive for a register move"
        );
        assert!(!output.contains("resident_r"));
    }

    /// `or rA,rS,rS` is the PowerPC register-to-register move and must lower to
    /// a plain copy. A genuine `or` with distinct operands keeps the bitwise
    /// form, and the recording (`or.`) form keeps the copy while still
    /// publishing CR0 from the moved value.
    #[test]
    fn register_move_lowering_is_exactly_a_copy() {
        for (word, expected, forbidden) in [
            (
                0x7ca5_2b78u32, // or r5,r5,r5
                "    context->gpr[5] = context->gpr[5];",
                "context->gpr[5] = context->gpr[5] | context->gpr[5];",
            ),
            (
                0x7d27_4b78u32, // or r7,r9,r9
                "    context->gpr[7] = context->gpr[9];",
                "context->gpr[7] = context->gpr[9] | context->gpr[9];",
            ),
        ] {
            let output = lower_instruction_fixture(0x8040_0000, &[word, 0x4e80_0020]);
            assert!(output.contains(expected), "{expected}\n{output}");
            assert!(!output.contains(forbidden), "{forbidden}\n{output}");
        }

        // or r5,r4,r5 -> rS=4, rA=5, rB=5: still not a move.
        let distinct = lower_instruction_fixture(0x8040_0000, &[0x7c85_2b78, 0x4e80_0020]);
        assert!(distinct.contains("    context->gpr[5] = context->gpr[4] | context->gpr[5];"));

        // or. r7,r9,r9 must keep the copy and still record CR0.
        let recorded = lower_instruction_fixture(0x8040_0000, &[0x7d27_4b79, 0x4e80_0020]);
        assert!(recorded.contains("    context->gpr[7] = context->gpr[9];"));
        assert!(recorded.contains("    galaxy::update_cr0(context, context->gpr[7]);"));
    }

    #[test]
    fn pc_jaudio_safety_kill_guard_preserves_later_updates_and_exact_opcode_gate() {
        let mut words = vec![0x6000_0000; 63];
        words[0x5C / 4] = 0x4182_0054;
        words[62] = 0x4E80_0020;
        let output = lower_instruction_fixture(0x8049_44D0, &words);
        assert!(output.contains("    goto label_80494580;"));
        assert!(output.contains("label_80494580:"));
        // The emitter no longer materialises the untested CTR/CR operand, so the
        // guard is a bare CR test. Assert the condition and target rather than
        // the removed `(true) &&` wrapper (79,297 sites emitted it module-wide;
        // the compiler folded it away, but the text is gone now). BO[1] selects
        // the tested polarity, so the two encodings must still differ.
        assert!(
            !output.contains("if (!galaxy::cr_bit(context, 2u)) goto label_80494580;")
        );

        words[0x5C / 4] = 0x4082_0054;
        let unmatched_opcode = lower_instruction_fixture(0x8049_44D0, &words);
        assert!(unmatched_opcode
            .contains("if (!galaxy::cr_bit(context, 2u)) goto label_80494580;"));
    }

    #[test]
    fn rmge01_ultrawide_width_hook_preserves_translated_return_shape() {
        let mut words = vec![0x6000_0000; 12];
        words[11] = 0x4E80_0020;
        let output = lower_instruction_fixture(0x803F_6B44, &words);
        let hook = "galaxy::apply_experimental_rmge01_ultrawide_screen_width(context, services);";
        assert_eq!(output.matches(hook).count(), 1);
        assert!(output.contains(&format!("{hook}\n    return;")));
        assert!(!lower_instruction_fixture(0x803F_6B40, &words).contains(hook));
        words.pop();
        words[10] = 0x4E80_0020;
        assert!(!lower_instruction_fixture(0x803F_6B44, &words).contains(hook));
    }

    #[test]
    fn rmge01_ultrawide_nw4r_hook_preserves_original_lfs() {
        let mut words = vec![0x6000_0000; 0xA8 / 4];
        words[(0x803C_A6FC - 0x803C_A6C4) / 4] = 0xC062_19A8;
        *words.last_mut().unwrap() = 0x4E80_0020;
        let hook = "galaxy::apply_experimental_rmge01_nw4r_canvas_width(context, services);";
        let output = lower_instruction_fixture(0x803C_A6C4, &words);
        assert_eq!(output.matches(hook).count(), 1);
        assert!(output.contains(&format!(
            "galaxy::load_fpr_single(context, 3u, memory, context->gpr[2] + 0x000019A8u, services, 0x803CA6FCu);\n    {hook}"
        )));
        assert!(!lower_instruction_fixture(0x803C_A6C0, &words).contains(hook));
        words.pop();
        *words.last_mut().unwrap() = 0x4E80_0020;
        assert!(!lower_instruction_fixture(0x803C_A6C4, &words).contains(hook));
        words.push(0x6000_0000);
        *words.last_mut().unwrap() = 0x4E80_0020;
        words[(0x803C_A6FC - 0x803C_A6C4) / 4] = 0xC062_19AC;
        assert!(!lower_instruction_fixture(0x803C_A6C4, &words).contains(hook));
    }

    #[test]
    fn rmge01_layout_geometry_hooks_require_exact_owner_range_and_boundaries() {
        let mut words = vec![0x6000_0000; 0x15C / 4];
        words[(0x8036_7D78 - 0x8036_7D28) / 4] = 0x807F_0004;
        words[(0x8036_7E64 - 0x8036_7D28) / 4] = 0xE3C1_0038;
        *words.last_mut().unwrap() = 0x4E80_0020;
        let backing = "galaxy::apply_rmge01_layout_backings(context, memory, services);";
        let hud = "galaxy::apply_rmge01_hud_anchors(context, memory, services);";
        let cinema = "galaxy::apply_rmge01_cinema_vertical(context, memory, services);";
        let output = lower_instruction_fixture(0x8036_7D28, &words);
        assert_eq!(output.matches(backing).count(), 1);
        assert_eq!(output.matches(hud).count(), 1);
        assert_eq!(output.matches(cinema).count(), 1);
        assert!(output.find(cinema).unwrap() < output.find(hud).unwrap());
        assert!(output.find(backing).unwrap() < output.find(hud).unwrap());
        assert!(!lower_instruction_fixture(0x8036_7D24, &words).contains(backing));
        words[(0x8036_7D78 - 0x8036_7D28) / 4] = 0x807F_0008;
        assert!(!lower_instruction_fixture(0x8036_7D28, &words).contains(backing));
        words.pop();
        *words.last_mut().unwrap() = 0x4E80_0020;
        let shortened = lower_instruction_fixture(0x8036_7D28, &words);
        assert!(
            !shortened.contains(backing) && !shortened.contains(hud) && !shortened.contains(cinema)
        );
    }

    #[test]
    fn rmge01_layout_conversions_hook_only_canonical_load_sites() {
        for (start, length, loads) in [
            (
                0x8036_6658,
                0x100,
                vec![(0x50, 0xC042_1438), (0x5C, 0xC002_143C)],
            ),
            (0x8036_6758, 0x10C, vec![(0x6C, 0xC002_1438)]),
        ] {
            let mut words = vec![0x6000_0000; length / 4];
            for &(offset, word) in &loads {
                words[offset / 4] = word;
            }
            *words.last_mut().unwrap() = 0x4E80_0020;
            let hook = "galaxy::apply_experimental_rmge01_layout_constant(";
            let output = lower_instruction_fixture(start, &words);
            assert_eq!(output.matches(hook).count(), loads.len());
            assert_eq!(
                output.matches("galaxy::load_fpr_single(").count(),
                loads.len()
            );
            assert!(!lower_instruction_fixture(start - 4, &words).contains(hook));
            for &(offset, _) in &loads {
                words[offset / 4] = 0xC002_1440;
            }
            assert!(!lower_instruction_fixture(start, &words).contains(hook));
        }
    }

    #[test]
    fn rmge01_layout_fit_runs_after_original_operation() {
        for (start, length, offset, word, original) in [
            (
                0x803C_A6C4,
                0xA8,
                0x44,
                0xEC21_1028,
                "galaxy::ppc_commit_scalar_result(context, 1u",
            ),
            (
                0x8036_6658,
                0x100,
                0xD0,
                0xFC00_0050,
                "context->fpr_bits[0] = context->fpr_bits[0] ^",
            ),
            (
                0x8036_6758,
                0x10C,
                0xAC,
                0xC85F_91D0,
                "context->fpr_bits[2] = galaxy::guest_load_u64(memory",
            ),
        ] {
            let mut words = vec![0x6000_0000; length / 4];
            words[offset / 4] = word;
            *words.last_mut().unwrap() = 0x4E80_0020;
            let hook = "galaxy::apply_experimental_rmge01_layout_vertical_fit(";
            let output = lower_instruction_fixture(start, &words);
            assert_eq!(output.matches(hook).count(), 1);
            assert!(output.find(original).unwrap() < output.find(hook).unwrap());
            assert!(!lower_instruction_fixture(start - 4, &words).contains(hook));
            words[offset / 4] = 0x6000_0000;
            assert!(!lower_instruction_fixture(start, &words).contains(hook));
        }
    }

    #[test]
    fn rmge01_screen_to_efb_uses_exact_selected_width_site() {
        let mut words = vec![0x6000_0000; 0xAC / 4];
        words[0x34 / 4] = 0x3800_0340;
        *words.last_mut().unwrap() = 0x4E80_0020;
        let hook = "galaxy::apply_experimental_rmge01_efb_screen_width(";
        let output = lower_instruction_fixture(0x803F_6D84, &words);
        assert_eq!(output.matches(hook).count(), 1);
        assert!(
            output.find("context->gpr[0] = 0x00000340u;").unwrap() < output.find(hook).unwrap()
        );
        assert!(!lower_instruction_fixture(0x803F_6D80, &words).contains(hook));
        words[0x34 / 4] = 0x3800_0260;
        assert!(!lower_instruction_fixture(0x803F_6D84, &words).contains(hook));
        words[0x34 / 4] = 0x3800_0340;
        words.pop();
        *words.last_mut().unwrap() = 0x4E80_0020;
        assert!(!lower_instruction_fixture(0x803F_6D84, &words).contains(hook));
    }

    #[test]
    fn rmge01_home_backings_run_before_exact_pane_matrix_calculation() {
        let start = 0x8100_00D8;
        let offset = (0x8101_18F8 - start) as usize;
        let mut words = vec![0x6000_0000; 0x1B048 / 4];
        words[offset / 4] = 0x9421_FF20;
        *words.last_mut().unwrap() = 0x4E80_0020;
        let hook = "galaxy::apply_rmge01_home_backings(context, memory, services, 0x810118F8u);";
        let output = lower_instruction_fixture(start, &words);
        assert_eq!(output.matches(hook).count(), 1);
        assert!(
            output.find(hook).unwrap() < output.find("const std::uint32_t ea_810118F8").unwrap()
        );
        assert!(!lower_instruction_fixture(start - 4, &words).contains(hook));
        words[offset / 4] = 0x9421_FF10;
        assert!(!lower_instruction_fixture(start, &words).contains(hook));
        words[offset / 4] = 0x9421_FF20;
        words.pop();
        *words.last_mut().unwrap() = 0x4E80_0020;
        assert!(!lower_instruction_fixture(start, &words).contains(hook));
    }

    #[test]
    fn rmge01_home_vertical_runs_at_exact_fresh_global_matrix_join() {
        let start = 0x8100_00D8;
        let offset = (0x8101_1B1C - start) as usize;
        let mut words = vec![0x6000_0000; 0x1B048 / 4];
        words[offset / 4] = 0x881D_00CF;
        *words.last_mut().unwrap() = 0x4E80_0020;
        let hook = "galaxy::apply_rmge01_home_vertical(context, memory, services, 0x81011B1Cu);";
        let output = lower_instruction_fixture(start, &words);
        assert!(output.contains(hook));
        assert!(
            output.find(hook).unwrap() < output.find("context->gpr[29] + 0x000000CFu").unwrap()
        );
        assert!(!lower_instruction_fixture(start - 4, &words).contains(hook));
        words[offset / 4] = 0x881D_00CE;
        assert!(!lower_instruction_fixture(start, &words).contains(hook));
        words[offset / 4] = 0x881D_00CF;
        words.pop();
        *words.last_mut().unwrap() = 0x4E80_0020;
        assert!(!lower_instruction_fixture(start, &words).contains(hook));
    }

    #[test]
    fn rmge01_home_geometry_runs_after_exact_original_operations() {
        for (start, length, sites) in [
            (
                0x8036_26CC,
                0x180,
                vec![(0x124, 0xEC63_2028), (0xF0, 0xEC21_1028)],
            ),
            (
                0x8100_00D8,
                0x1B048,
                vec![
                    (0x1CF4, 0xC024_0004),
                    (0x1CF8, 0xC004_0008),
                    (0x1D08, 0xC006_0020),
                    (0x1D14, 0xC005_0024),
                ],
            ),
        ] {
            let mut words = vec![0x6000_0000; length / 4];
            for &(offset, word) in &sites {
                words[offset / 4] = word;
            }
            *words.last_mut().unwrap() = 0x4E80_0020;
            let hook = "galaxy::apply_experimental_rmge01_home_geometry(";
            let output = lower_instruction_fixture(start, &words);
            assert_eq!(output.matches(hook).count(), sites.len());
            for prefix in output.split(hook).take(sites.len()) {
                assert!(
                    prefix.contains("galaxy::ppc_commit_scalar_result(")
                        || prefix.contains("galaxy::load_fpr_single(")
                );
            }
            assert!(!lower_instruction_fixture(start - 4, &words).contains(hook));
            for &(offset, _) in &sites {
                words[offset / 4] = 0x6000_0000;
            }
            assert!(!lower_instruction_fixture(start, &words).contains(hook));
        }
    }

    #[test]
    fn rmge01_camera_aspect_hook_preserves_original_lfs() {
        let mut words = vec![0x6000_0000; (0x8009_731C - 0x8009_7288) / 4];
        words[(0x8009_72B0 - 0x8009_7288) / 4] = 0xC022_96E4;
        *words.last_mut().unwrap() = 0x4E80_0020;
        let hook = "galaxy::apply_experimental_rmge01_camera_aspect(context, services);";
        let output = lower_instruction_fixture(0x8009_7288, &words);
        assert_eq!(output.matches(hook).count(), 1);
        assert!(output.contains(&format!(
            "galaxy::load_fpr_single(context, 1u, memory, context->gpr[2] + 0xFFFF96E4u, services, 0x800972B0u);\n    {hook}"
        )));
        assert!(!lower_instruction_fixture(0x8009_7284, &words).contains(hook));
        words[(0x8009_72B0 - 0x8009_7288) / 4] = 0xC022_96E8;
        assert!(!lower_instruction_fixture(0x8009_7288, &words).contains(hook));
        words[(0x8009_72B0 - 0x8009_7288) / 4] = 0xC022_96E4;
        words.pop();
        // Test the wrong extent with a valid returning body, not MissingReturn.
        *words.last_mut().unwrap() = 0x4E80_0020;
        let shortened = lower_instruction_fixture(0x8009_7288, &words);
        assert!(!shortened.contains(hook));
        assert!(shortened.contains(
            "galaxy::load_fpr_single(context, 1u, memory, context->gpr[2] + 0xFFFF96E4u, services, 0x800972B0u);"
        ));
    }

    #[test]
    fn rmge01_native_four_three_hook_preserves_original_sc_load() {
        let mut words = vec![0x6000_0000; 0x54 / 4];
        words[(0x804D_074C - 0x804D_0708) / 4] = 0x8861_0008;
        *words.last_mut().unwrap() = 0x4E80_0020;
        let hook = "galaxy::apply_experimental_rmge01_native_four_three_aspect_read(context, memory, services);";
        let output = lower_instruction_fixture(0x804D_0708, &words);
        assert_eq!(output.matches(hook).count(), 1);
        assert!(output.contains(&format!(
            "context->gpr[3] = galaxy::guest_load_u8(memory, context->gpr[1] + 0x00000008u, services, 0x804D074Cu);\n    {hook}"
        )));
        assert!(!lower_instruction_fixture(0x804D_0704, &words).contains(hook));
        words[(0x804D_074C - 0x804D_0708) / 4] = 0x8861_0009;
        assert!(!lower_instruction_fixture(0x804D_0708, &words).contains(hook));
        words[(0x804D_074C - 0x804D_0708) / 4] = 0x8861_0008;
        words.pop();
        *words.last_mut().unwrap() = 0x4E80_0020;
        let shortened = lower_instruction_fixture(0x804D_0708, &words);
        assert!(!shortened.contains(hook));
        assert!(shortened.contains(
            "context->gpr[3] = galaxy::guest_load_u8(memory, context->gpr[1] + 0x00000008u, services, 0x804D074Cu);"
        ));
    }

    #[test]
    fn statically_proven_wgpipe_stores_emit_one_fail_closed_batch() {
        let address = 0x8000_4000;
        let output = lower_instruction_fixture(
            address,
            &[
                (15 << 26) | (3 << 21) | 0xCC01,             // lis r3, 0xCC01
                (38 << 26) | (4 << 21) | (3 << 16) | 0x8000, // stb r4, -0x8000(r3)
                (44 << 26) | (5 << 21) | (3 << 16) | 0x8000, // sth r5, -0x8000(r3)
                (36 << 26) | (6 << 21) | (3 << 16) | 0x8000, // stw r6, -0x8000(r3)
                0x4E80_0020,
            ],
        );
        assert_eq!(output.matches("guest_write_wgpipe_bytes").count(), 1);
        assert!(output.contains("const std::array<std::byte, 7> wgpipe_bytes_80004004"));
        assert!(output
            .contains("static_cast<std::byte>(static_cast<std::uint8_t>(context->gpr[6] >> 0u))"));
        assert!(output.contains(
            "guest_store_u16(memory, 0xCC008000u, static_cast<std::uint16_t>(context->gpr[5])"
        ));
        assert!(output.contains("guest_wgpipe_batch_requires_scalar_path()"));
        assert!(output.contains(
            "guest_write_wgpipe_bytes(memory, 0xCC008000u, wgpipe_bytes_80004004, services, 0x80004004u)"
        ));
        assert!(!output.contains("guest_try_write_wgpipe_bytes"));
        for (helper, pc) in [
            ("guest_store_u8", "80004004"),
            ("guest_store_u16", "80004008"),
            ("guest_store_u32", "8000400C"),
        ] {
            assert!(output.contains(helper), "missing scalar fallback {helper}");
            assert!(output.contains(&format!("0x{pc}u")), "missing PC {pc}");
        }
        for shift in [24, 16, 8, 0] {
            assert!(output.contains(&format!("context->gpr[6] >> {shift}u")));
        }
    }

    #[test]
    fn wgpipe_batch_filter_rejects_dynamic_object_stores_updates_and_cross_entry_runs() {
        let address = 0x8000_5000;
        let dynamic_same_address = lower_instruction_fixture(
            address,
            &[
                (38 << 26) | (4 << 21) | (3 << 16),
                (44 << 26) | (5 << 21) | (3 << 16),
                (36 << 26) | (6 << 21) | (3 << 16),
                0x4E80_0020,
            ],
        );
        assert!(!dynamic_same_address.contains("guest_wgpipe"));

        let different_offsets = lower_instruction_fixture(
            address,
            &[
                (36 << 26) | (4 << 21) | (3 << 16),
                (36 << 26) | (5 << 21) | (3 << 16) | 4,
                (36 << 26) | (6 << 21) | (3 << 16) | 8,
                0x4E80_0020,
            ],
        );
        assert!(!different_offsets.contains("guest_wgpipe"));

        let updates = lower_instruction_fixture(
            address,
            &[
                (37 << 26) | (4 << 21) | (3 << 16) | 4, // stwu r4, 4(r3)
                (37 << 26) | (5 << 21) | (3 << 16) | 4, // stwu r5, 4(r3)
                0x4E80_0020,
            ],
        );
        assert!(!updates.contains("guest_wgpipe"));

        let words: [u32; 5] = [
            (15 << 26) | (3 << 21) | 0xCC01,
            (36 << 26) | (4 << 21) | (3 << 16) | 0x8000,
            (36 << 26) | (5 << 21) | (3 << 16) | 0x8000,
            (36 << 26) | (6 << 21) | (3 << 16) | 0x8000,
            0x4E80_0020,
        ];
        let bytes = words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect::<Vec<_>>();
        let with_entry = lower_words_with_entries(address, &bytes, &BTreeSet::from([address + 8]))
            .expect("interior-entry fixture lowers");
        assert!(with_entry.contains("label_80005008:"));
        assert!(!with_entry.contains("guest_wgpipe"));
    }

    #[test]
    fn wgpipe_batch_generated_locals_remain_inside_one_instruction_scope() {
        let address = 0x8000_6000;
        let output = lower_instruction_fixture(
            address,
            &[
                (15 << 26) | (3 << 21) | 0x0C01,
                (38 << 26) | (4 << 21) | (3 << 16) | 0x8000,
                (36 << 26) | (5 << 21) | (3 << 16) | 0x8000,
                0x4E80_0020,
            ],
        );
        let batch = output
            .find("const std::array<std::byte, 5> wgpipe_bytes_80006004")
            .expect("batch declaration emitted");
        let enclosing_open = output[..batch]
            .rfind("    {\n")
            .expect("instruction block opens");
        let enclosing_close = output[batch..]
            .find("    }\n")
            .map(|offset| batch + offset)
            .expect("instruction block closes");
        assert!(output[enclosing_open..enclosing_close].contains("guest_write_wgpipe_bytes"));
        assert!(output[enclosing_close..].contains("    }\n    }\n    {\n"));
    }

    // Independent instruction-shaped fixtures, not a retail function dump.
    fn local_lane_fixture_words() -> Vec<u32> {
        vec![
            (37 << 26) | (1 << 21) | (1 << 16) | 0xFFC0, // stack frame
            (56 << 26) | (1 << 21) | (3 << 16),          // paired load f1
            (56 << 26) | (2 << 21) | (4 << 16) | 8,      // paired load f2
            (4 << 26) | (3 << 21) | (1 << 16) | (2 << 6) | (12 << 1), // muls0
            (4 << 26) | (4 << 21) | (2 << 16) | (3 << 11) | (1 << 6) | (15 << 1), // madds1
            (60 << 26) | (4 << 21) | (5 << 16),          // ordered paired store
            (50 << 26) | (1 << 21) | (1 << 16) | 8,      // arbitrary lower-lane restore
            (14 << 26) | (1 << 21) | (1 << 16) | 64,     // restore stack
            0x4E80_0020,
        ]
    }

    // Straight-line instruction-shaped fixture for the normalize profile. It
    // exercises paired sums, scalar-single producers and the binary64 estimate
    // invalidation without relying on a handwritten mathematical replacement.
    fn normalize_local_lane_fixture_words() -> Vec<u32> {
        vec![
            0xE043_0000, // psq_l f2, 0(r3), 0, qr0
            0xE063_8008, // psq_l f3, 8(r3), 1, qr0
            0x10A2_00B2, // ps_mul f5, f2, f2
            0xC002_24E8, // lfs f0, 0x24e8(r2)
            0xC022_24EC, // lfs f1, 0x24ec(r2)
            0x1083_28FA, // ps_madd f4, f3, f3, f5
            0x1084_28D4, // ps_sum0 f4, f4, f5, f3
            0xFCA0_2034, // frsqrte f5, f4
            0xECC5_0172, // fmuls f6, f5, f5
            0xEC05_0032, // fmuls f0, f5, f0
            0xECC6_093C, // fnmsubs f6, f6, f4, f1
            0xECA6_0032, // fmuls f5, f6, f0
            0x1042_0158, // ps_muls0 f2, f2, f5
            0x1063_0158, // ps_muls0 f3, f3, f5
            0xF044_0000, // psq_st f2, 0(r4), 0, qr0
            0xF064_8008, // psq_st f3, 8(r4), 1, qr0
            0x4E80_0020,
        ]
    }

    fn lower_local_lane_fixture(words: &[u32]) -> Result<String, TranslationError> {
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        lower_words_with_config(
            0x804B_5F3C,
            &bytes,
            &BTreeSet::new(),
            &BTreeSet::new(),
            LoweringConfig {
                local_lane_profile: LocalLaneProfile::PsmtxConcat,
                ..LoweringConfig::default()
            },
        )
    }

    fn lower_normalize_local_lane_fixture(words: &[u32]) -> Result<String, TranslationError> {
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        lower_words_with_config(
            0x804B_6BCC,
            &bytes,
            &BTreeSet::new(),
            &BTreeSet::new(),
            LoweringConfig {
                local_lane_profile: LocalLaneProfile::PsvecNormalize,
                ..LoweringConfig::default()
            },
        )
    }

    #[test]
    fn normalize_local_lanes_preserve_exact_operations_and_scalar_producer_facts() {
        let words = normalize_local_lane_fixture_words();
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        validate_psvec_normalize_local_memory_contract(&bytes)
            .expect("normalize accesses remain inside guarded spans");
        let ordinary = lower_words(0x804B_6BCC, &bytes).unwrap();
        let cached = lower_normalize_local_lane_fixture(&words).unwrap();
        assert_eq!(
            ordinary.matches("require_single_precision_bits").count(),
            24
        );
        assert!(!cached.contains("require_single_precision_bits"));
        assert!(cached.contains("lane0[5] = galaxy::narrow_f64_to_f32_bits(result.bits"));
        assert!(cached.contains("const std::uint32_t multiplicand = lane0[6];"));
        assert!(cached.contains("const std::uint32_t right_ps0 = lane0[5];"));
        assert!(cached.contains("lane1[5] = lane0[5];"));
        for needle in [
            "galaxy::psq_load",
            "galaxy::ppc_f64_reciprocal_sqrt_estimate",
            "galaxy::ppc_f64_binary_to_f32",
            "galaxy::ppc_f32_ternary",
            "galaxy::ppc_commit_paired_result",
            "galaxy::ppc_commit_scalar_result",
            "galaxy::psq_store",
        ] {
            assert_eq!(
                ordinary.matches(needle).count(),
                cached.matches(needle).count(),
                "architectural operation count changed for {needle}"
            );
        }

        let mut changed_access = words.clone();
        changed_access[15] = 0xF064_800C;
        let changed_bytes: Vec<u8> = changed_access
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        assert!(matches!(
            validate_psvec_normalize_local_memory_contract(&changed_bytes),
            Err(TranslationError::LocalPairedProof { .. })
        ));

        let mut consumes_duplicated_ps1 = words;
        consumes_duplicated_ps1[12] = 0x1042_0172; // ps_mul f2, f2, f5
        let duplicated = lower_normalize_local_lane_fixture(&consumes_duplicated_ps1).unwrap();
        assert!(duplicated.contains("const std::uint32_t right_ps1 = lane1[5];"));
    }

    #[test]
    fn local_lane_cache_preserves_instruction_effects_and_exact_producers() {
        let words = local_lane_fixture_words();
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        validate_psmtx_local_memory_contract(&bytes).expect("all accesses have guarded roots");
        let ordinary = lower_words(0x804B_5F3C, &bytes).unwrap();
        let cached = lower_local_lane_fixture(&words).unwrap();
        assert_eq!(
            ordinary.matches("require_single_precision_bits").count(),
            10
        );
        assert!(!cached.contains("require_single_precision_bits"));
        assert_eq!(cached.matches("narrow_f64_to_f32_bits").count(), 8);
        assert!(cached.contains("const std::uint32_t right_ps1 = lane0[2];"));
        assert!(cached.contains("const std::uint32_t multiplier_ps0 = lane1[1];"));
        let effects = |text: &str| {
            text.lines()
                .filter(|line| {
                    line.contains("galaxy::psq_")
                        || line.contains("galaxy::guest_")
                        || line.contains("context->gpr[")
                        || line.contains("ensure_fpu_available")
                })
                .map(str::to_owned)
                .collect::<Vec<_>>()
        };
        assert_eq!(
            effects(&ordinary),
            effects(&cached),
            "ordered memory and mode effects remain exact"
        );
        assert_eq!(
            cached.matches("ppc_commit_paired_ternary_result").count(),
            1
        );
        assert_eq!(cached.matches("ppc_f32_ternary").count(), 0);
        assert!(cached.contains("lane0[4] = galaxy::narrow_f64_to_f32_bits(context->fpr_bits[4]"));
        assert!(cached.contains("lane1[4] = galaxy::narrow_f64_to_f32_bits(context->ps1_bits[4]"));
        let default_config = lower_words_with_config(
            0x804B_5F3C,
            &bytes,
            &BTreeSet::new(),
            &BTreeSet::new(),
            LoweringConfig::default(),
        )
        .unwrap();
        assert_eq!(
            ordinary, default_config,
            "disabled lowering preserves exact source"
        );
    }

    #[test]
    fn fused_local_lane_binary_caches_the_committed_destination() {
        let words = local_lane_fixture_words();
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let cached = lower_words_with_config(
            0x804B_5F3C,
            &bytes,
            &BTreeSet::new(),
            &BTreeSet::new(),
            LoweringConfig {
                local_lane_profile: LocalLaneProfile::PsmtxConcat,
                fused_paired_binary: true,
                ..LoweringConfig::default()
            },
        )
        .unwrap();
        assert_eq!(cached.matches("ppc_commit_paired_binary_result").count(), 1);
        assert_eq!(cached.matches("ppc_f32_binary").count(), 0);
        assert_eq!(
            cached.matches("ppc_commit_paired_ternary_result").count(),
            1
        );
        assert!(cached.contains("lane0[3] = galaxy::narrow_f64_to_f32_bits(context->fpr_bits[3]"));
        assert!(cached.contains("lane1[3] = galaxy::narrow_f64_to_f32_bits(context->ps1_bits[3]"));
        let capture = cached
            .find("const std::uint32_t right_ps1 = lane0[2];")
            .unwrap();
        let commit = cached.find("ppc_commit_paired_binary_result").unwrap();
        let cache = cached.find("lane0[3] =").unwrap();
        assert!(capture < commit && commit < cache);
    }

    #[test]
    fn local_lane_cache_rejects_missing_restored_and_quantized_facts() {
        let mut missing = local_lane_fixture_words();
        missing.remove(1);
        assert!(matches!(
            lower_local_lane_fixture(&missing),
            Err(TranslationError::LocalPairedProof { .. })
        ));
        let mut restored = local_lane_fixture_words();
        restored.insert(3, (50 << 26) | (1 << 21) | (1 << 16) | 8);
        assert!(matches!(
            lower_local_lane_fixture(&restored),
            Err(TranslationError::LocalPairedProof { .. })
        ));
        let mut quantized = local_lane_fixture_words();
        quantized[1] |= 1 << 12;
        assert!(matches!(
            lower_local_lane_fixture(&quantized),
            Err(TranslationError::LocalPairedProof { .. })
        ));
        let mut branch = local_lane_fixture_words();
        branch[3] = 0x4800_0005; // linked branch cannot preserve local facts
        assert!(matches!(
            lower_local_lane_fixture(&branch),
            Err(TranslationError::LocalPairedProof { .. })
        ));
        let bytes: Vec<u8> = local_lane_fixture_words()
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        assert!(lower_words_with_config(
            0x804B_5F3C,
            &bytes,
            &BTreeSet::from([0x804B_5F40]),
            &BTreeSet::new(),
            LoweringConfig {
                local_lane_profile: LocalLaneProfile::PsmtxConcat,
                ..LoweringConfig::default()
            }
        )
        .is_err());
    }

    #[test]
    fn local_lane_memory_proof_rejects_uncovered_spans_and_stack_changes() {
        for (index, replacement) in [
            (1, (56 << 26) | (1 << 21) | (3 << 16) | 44), // eight-byte read leaves lhs
            (5, (60 << 26) | (4 << 21) | (3 << 16)),      // write into read-only lhs guard
            (6, (50 << 26) | (1 << 21) | (1 << 16) | 60), // stack read crosses end
            (7, (14 << 26) | (1 << 21) | (1 << 16) | 32), // incorrect final SP
            (0, (37 << 26) | (1 << 21) | (1 << 16) | 0xFFE0), // other frame size
        ] {
            let mut words = local_lane_fixture_words();
            words[index] = replacement;
            let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
            assert!(matches!(
                validate_psmtx_local_memory_contract(&bytes),
                Err(TranslationError::LocalPairedProof { .. })
            ));
        }
    }

    #[test]
    fn local_lane_experiment_preserves_original_shards_and_full_retry_fallback() {
        let words = local_lane_fixture_words();
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let target = 0x804B_5F3C;
        let retry_entries = BTreeSet::from([
            target + 4,
            target + 8,
            target + 12,
            target + 16,
            target + 20,
            target + 24,
        ]);
        let body = lower_words_with_entries(target, &bytes, &retry_entries).unwrap();
        let functions = vec![
            (
                FunctionRange {
                    address: target - 256,
                    size: 4,
                },
                format!("// {}\n    return;\n", "padding".repeat(1200)),
            ),
            (
                FunctionRange {
                    address: target,
                    size: bytes.len() as u32,
                },
                body.clone(),
            ),
            (
                FunctionRange {
                    address: target + 256,
                    size: 4,
                },
                format!("// {}\n    return;\n", "padding".repeat(1200)),
            ),
            (
                FunctionRange {
                    address: target + 512,
                    size: 4,
                },
                "    return;\n".to_owned(),
            ),
        ];
        let aliases: Vec<_> = retry_entries.iter().map(|entry| (*entry, target)).collect();
        let exact = BTreeSet::from([target]);
        let plain = emit_sharded_module_with_exact_fpu_and_source_budget(
            &functions, &aliases, 64, 16384, target, &exact,
        )
        .unwrap();
        let disabled = emit_sharded_module_with_experiment(
            &functions,
            &aliases,
            64,
            16384,
            target,
            &exact,
            LocalPairedExperiments::default(),
        )
        .unwrap();
        assert_eq!(
            plain
                .iter()
                .map(|file| (&file.name, &file.contents))
                .collect::<Vec<_>>(),
            disabled
                .iter()
                .map(|file| (&file.name, &file.contents))
                .collect::<Vec<_>>()
        );
        let experiment = PsmtxLocalExperiment {
            body: lower_local_lane_fixture(&words).unwrap(),
            trace_guard: false,
        };
        let optimized = emit_sharded_module_with_experiment(
            &functions,
            &aliases,
            64,
            16384,
            target,
            &exact,
            LocalPairedExperiments {
                psmtx: Some(&experiment),
                ..Default::default()
            },
        )
        .unwrap();
        for original in &plain {
            let updated = optimized
                .iter()
                .find(|file| file.name == original.name)
                .unwrap();
            let expected = if original.name == "CMakeLists.txt" {
                original.contents.replace(
                    ")\ntarget_include_directories(RMGE01_game",
                    "    psmtx_local_lanes.cpp\n)\ntarget_include_directories(RMGE01_game",
                )
            } else {
                original
                    .contents
                    .replacen("void fn_804B5F3C(", "void fn_804B5F3C_exact(", 1)
            };
            // functions.h keeps the public canonical declaration exactly.
            assert_eq!(
                updated.contents,
                if original.name == "functions.h" {
                    original.contents.clone()
                } else {
                    expected
                }
            );
        }
        let fallback = optimized
            .iter()
            .find(|file| {
                file.contents.contains("void fn_804B5F3C_exact(")
                    && file.name.starts_with("functions_")
            })
            .unwrap();
        assert!(
            fallback.contents.contains(&body),
            "all interior dispatch and FPU retry labels are retained verbatim"
        );
        let extra = optimized
            .iter()
            .find(|file| file.name == "psmtx_local_lanes.cpp")
            .unwrap();
        assert!(extra
            .contents
            .contains("fn_804B5F3C_exact(context, memory, services);"));
        assert!(!extra.contents.contains("thread_local"));
        assert!(!extra.contents.contains("services->log("));
        let traced = render_psmtx_local_experiment(&PsmtxLocalExperiment {
            body: experiment.body,
            trace_guard: true,
        });
        assert!(traced.contains("std::atomic_uint64_t reasons[16]"));
        assert!(!traced.contains("thread_local PsmtxGuardCounts"));
        assert!(traced.contains("scope=process-main-frame-window"));
        assert!(
            traced
                .find("    flush_psmtx_guard_counts(services);")
                .unwrap()
                < traced
                    .find("    const std::uint32_t reason = psmtx_local_guard")
                    .unwrap()
        );
    }

    #[test]
    fn local_lane_experiment_never_regroups_to_hide_source_budget_growth() {
        let function = FunctionRange {
            address: 0x804B_5F3C,
            size: 4,
        };
        let body = format!("// {}\n    return;\n", "x".repeat(9000));
        let exact = BTreeSet::from([function.address]);
        let rendered =
            render_sharded_function_definition(function, &body, &BTreeSet::new(), &exact).unwrap();
        let budget = FUNCTION_SHARD_PREFIX.len() + rendered.len() + FUNCTION_SHARD_SUFFIX.len();
        let functions = [(function, body)];
        assert!(emit_sharded_module_with_exact_fpu_and_source_budget(
            &functions,
            &[],
            64,
            budget,
            function.address,
            &exact
        )
        .is_ok());
        let experiment = PsmtxLocalExperiment {
            body: "    return;\n".to_owned(),
            trace_guard: false,
        };
        assert!(matches!(
            emit_sharded_module_with_experiment(
                &functions,
                &[],
                64,
                budget,
                function.address,
                &exact,
                LocalPairedExperiments {
                    psmtx: Some(&experiment),
                    ..Default::default()
                }
            ),
            Err(TranslationError::LocalPairedProof { .. })
        ));
    }

    #[test]
    fn psvec_cross_experiment_keeps_the_exact_static_fallback_and_lookup_owner() {
        let target = 0x804B_6CB8;
        let function = FunctionRange {
            address: target,
            size: 0x3C,
        };
        let exact_body = "    // exact cross body and interior dispatch\n    return;\n".to_owned();
        let functions = [(function, exact_body.clone())];
        let aliases = [(target + 4, target), (target + 0x34, target)];
        let exact = BTreeSet::from([target]);
        let experiment = PsvecCrossLocalExperiment {
            body: "    // cached cross body\n    return;\n".to_owned(),
            trace_guard: false,
        };
        let output = emit_sharded_module_with_experiment(
            &functions,
            &aliases,
            64,
            16384,
            target,
            &exact,
            LocalPairedExperiments {
                psvec_cross: Some(&experiment),
                ..Default::default()
            },
        )
        .unwrap();
        let fallback = output
            .iter()
            .find(|file| file.name.starts_with("functions_"))
            .unwrap();
        assert!(fallback.contents.contains("void fn_804B6CB8_exact("));
        assert!(fallback.contents.contains(&exact_body));
        assert!(!fallback.contents.contains("void fn_804B6CB8("));
        let wrapper = output
            .iter()
            .find(|file| file.name == "psvec_cross_local_lanes.cpp")
            .unwrap();
        assert!(wrapper.contents.contains("void fn_804B6CB8("));
        assert!(wrapper
            .contents
            .contains("fn_804B6CB8_exact(context, memory, services);"));
        assert!(wrapper.contents.contains("// cached cross body"));
        assert!(wrapper.contents.contains("context->fpscr & 0x000000F8u"));
        assert!(!wrapper
            .contents
            .contains("thread_local PsvecCrossGuardCounts"));
        let traced = render_psvec_cross_local_experiment(&PsvecCrossLocalExperiment {
            body: experiment.body,
            trace_guard: true,
        });
        assert!(traced.contains("thread_local PsvecCrossGuardCounts"));
        assert!(
            traced
                .find("    flush_psvec_cross_guard_counts(services);")
                .unwrap()
                < traced
                    .find("    const std::uint32_t reason = psvec_cross_local_guard")
                    .unwrap()
        );
        let module = output
            .iter()
            .find(|file| file.name == "module.cpp")
            .unwrap();
        assert_eq!(module.contents.matches("&rmge01::fn_804B6CB8").count(), 3);
        assert!(!module.contents.contains("fn_804B6CB8_exact"));
        let cmake = output
            .iter()
            .find(|file| file.name == "CMakeLists.txt")
            .unwrap();
        assert!(cmake.contents.contains("    psvec_cross_local_lanes.cpp\n"));
    }

    #[test]
    fn psvec_normalize_experiment_keeps_exact_retry_fallback_and_guard() {
        let target = 0x804B_6BCC;
        let words = normalize_local_lane_fixture_words();
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let retry_entries = (target + 4..=target + 0x3C)
            .step_by(4)
            .collect::<BTreeSet<_>>();
        let function = FunctionRange {
            address: target,
            size: 0x44,
        };
        let exact_body = lower_words_with_entries(target, &bytes, &retry_entries).unwrap();
        let functions = [(function, exact_body.clone())];
        let aliases = retry_entries
            .iter()
            .map(|entry| (*entry, target))
            .collect::<Vec<_>>();
        let exact = BTreeSet::from([target]);
        let experiment = PsvecNormalizeLocalExperiment {
            body: "    // cached normalize body\n    return;\n".to_owned(),
            trace_guard: false,
        };
        let output = emit_sharded_module_with_experiment(
            &functions,
            &aliases,
            64,
            16384,
            target,
            &exact,
            LocalPairedExperiments {
                psvec_normalize: Some(&experiment),
                ..Default::default()
            },
        )
        .unwrap();
        let fallback = output
            .iter()
            .find(|file| file.name.starts_with("functions_"))
            .unwrap();
        assert!(fallback.contents.contains("void fn_804B6BCC_exact("));
        assert!(fallback.contents.contains(&exact_body));
        // Each retained retry name appears once in the entry switch and once
        // on its exact label.
        assert_eq!(fallback.contents.matches("fpu_retry_804B6B").count(), 24);
        assert_eq!(fallback.contents.matches("fpu_retry_804B6C").count(), 6);
        assert!(!fallback.contents.contains("void fn_804B6BCC("));
        let wrapper = output
            .iter()
            .find(|file| file.name == "psvec_normalize_local_lanes.cpp")
            .unwrap();
        assert!(wrapper.contents.contains("void fn_804B6BCC("));
        assert!(wrapper
            .contents
            .contains("fn_804B6BCC_exact(context, memory, services);"));
        assert!(wrapper.contents.contains("context->gpr[2] + 0x000024E8u"));
        assert!(!wrapper
            .contents
            .contains("thread_local PsvecNormalizeGuardCounts"));
        let traced = render_psvec_normalize_local_experiment(&PsvecNormalizeLocalExperiment {
            body: experiment.body,
            trace_guard: true,
        });
        assert!(traced.contains("std::atomic_uint64_t reasons[12]"));
        assert!(!traced.contains("thread_local PsvecNormalizeGuardCounts"));
        assert!(
            traced
                .find("    flush_psvec_normalize_guard_counts(services);")
                .unwrap()
                < traced
                    .find("    const std::uint32_t reason = psvec_normalize_local_guard")
                    .unwrap()
        );
        let module = output
            .iter()
            .find(|file| file.name == "module.cpp")
            .unwrap();
        assert_eq!(
            module.contents.matches("&rmge01::fn_804B6BCC").count(),
            retry_entries.len() + 1
        );
        assert!(!module.contents.contains("fn_804B6BCC_exact"));
    }

    #[test]
    fn lowers_immediate_operations_and_return() {
        let words = [0x3860_0001_u32, 0x3C80_FFFF, 0x6063_0010, 0x4E80_0020];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words(0x8000_4000, &bytes).expect("supported function");
        assert!(output.contains("context->gpr[3] = 0x00000001u;"));
        assert!(output.contains("context->gpr[4] = 0xFFFF0000u;"));
        assert!(output.contains("context->gpr[3] = context->gpr[3] | 0x00000010u;"));
        assert!(output.contains("    return;\n"));
    }

    #[test]
    fn lowers_symbolic_hi_and_lo_relocations_without_runtime_linking() {
        let words = [0x3C60_0000_u32, 0x3863_0000, 0x4E80_0020];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let mut overrides = BTreeMap::new();
        overrides.insert(
            0x8000_4000,
            PpcImmediateOverride {
                kind: PpcImmediateKind::HighAdjusted16,
                expression: "rso_section_1 + 0x00000010u".to_owned(),
            },
        );
        overrides.insert(
            0x8000_4004,
            PpcImmediateOverride {
                kind: PpcImmediateKind::Low16,
                expression: "rso_section_1 + 0x00000010u".to_owned(),
            },
        );

        let output = lower_ppc_code_range_with_entries_and_immediate_overrides(
            0x8000_4000,
            &bytes,
            &BTreeSet::new(),
            &overrides,
        )
        .expect("symbolic relocation pair lowers");
        assert!(output.contains("(rso_section_1 + 0x00000010u) + 0x8000u"));
        assert!(
            output.contains("static_cast<std::int16_t>((rso_section_1 + 0x00000010u) & 0xFFFFu)")
        );
        assert!(!output.contains("0x00000000u;"));
    }

    #[test]
    fn lowers_zero_base_and_zero_displacement_without_redundant_expressions() {
        let words = [
            0x3860_0001_u32, // addi r3, r0, 1
            0x3883_0000,     // addi r4, r3, 0
            0x80A4_0000,     // lwz r5, 0(r4)
            0x90A6_0000,     // stw r5, 0(r6)
            0x4E80_0020,
        ];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words(0x8000_4000, &bytes).expect("supported function");
        assert!(output.contains("context->gpr[3] = 0x00000001u;"));
        assert!(output.contains("context->gpr[4] = context->gpr[3];"));
        assert!(output.contains(
            "context->gpr[5] = galaxy::guest_load_u32(memory, context->gpr[4], services, 0x80004008u);"
        ));
        assert!(output.contains(
            "galaxy::guest_store_u32(memory, context->gpr[6], context->gpr[5], services, 0x8000400Cu);"
        ));
        assert!(!output.contains("ea_80004008"));
        assert!(!output.contains("ea_8000400C"));
        assert!(!output.contains("0u +"));
        assert!(!output.contains("+ 0x00000000u"));
    }

    #[test]
    fn rejects_unsupported_instruction() {
        let error = lower_words(0x8000_4000, &0x0000_0000_u32.to_be_bytes())
            .expect_err("invalid opcode is not implemented");
        assert!(matches!(
            error,
            TranslationError::UnsupportedInstruction { .. }
        ));
    }

    #[test]
    fn lowers_stack_frame_and_link_register_sequence() {
        let words = [
            0x9421_FFF0_u32,
            0x7C08_02A6,
            0x9001_0014,
            0x8001_0014,
            0x7C08_03A6,
            0x3821_0010,
            0x4E80_0020,
        ];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words(0x8000_4000, &bytes).expect("supported stack frame");
        assert!(output.contains("guest_store_u32"));
        assert!(output.contains("context->gpr[1] = ea_80004000;"));
        assert!(output.contains("context->gpr[0] = context->lr;"));
        assert!(output.contains("context->lr = context->gpr[0];"));
    }

    #[test]
    fn computes_powerpc_rotate_masks() {
        assert_eq!(rotate_mask(0, 31), 0xFFFF_FFFF);
        assert_eq!(rotate_mask(16, 31), 0x0000_FFFF);
        assert_eq!(rotate_mask(0, 15), 0xFFFF_0000);
        assert_eq!(rotate_mask(24, 7), 0xFF00_00FF);
    }

    #[test]
    fn lowers_internal_and_external_direct_branches() {
        let internal_words = [0x4800_0008_u32, 0x3860_0001, 0x4E80_0020];
        let internal_bytes: Vec<u8> = internal_words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        let internal =
            lower_words(0x8000_4000, &internal_bytes).expect("internal branch supported");
        assert!(internal.contains("goto label_80004008;"));
        assert!(internal.contains("label_80004008:"));

        let external_words = [0x4800_1001_u32, 0x4E80_0020];
        let external_bytes: Vec<u8> = external_words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        let external = lower_words(0x8000_4000, &external_bytes).expect("external call supported");
        assert!(external.contains("context->lr = 0x80004004u;"));
        assert!(external.contains("galaxy::call_guest"));
        assert!(external.contains(
            "galaxy::call_return_checkpoint(services, 0x80004000u, 0x80004004u, context, memory);"
        ));
    }

    #[test]
    fn linked_branch_forms_checkpoint_the_exact_continuation() {
        let conditional_words = [0x4182_1001_u32, 0x4E80_0020];
        let conditional_bytes: Vec<u8> = conditional_words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        let conditional = lower_words(0x8000_4000, &conditional_bytes)
            .expect("conditional external linked branch is supported");
        assert!(conditional.contains("context->lr = 0x80004004u;"));
        let conditional_call = conditional
            .find("galaxy::call_guest(services")
            .expect("taken conditional path calls its target");
        let conditional_checkpoint = conditional
            .find(
                "galaxy::call_return_checkpoint(services, 0x80004000u, 0x80004004u, context, memory);",
            )
            .expect("taken conditional path checks published events after return");
        let conditional_if = conditional[..conditional_call]
            .rfind("    if (")
            .expect("conditional call remains guarded");
        let conditional_close = conditional[conditional_checkpoint..]
            .find("    }\n")
            .map(|offset| conditional_checkpoint + offset)
            .expect("conditional call guard closes after the checkpoint");
        assert!(conditional_if < conditional_call);
        assert!(conditional_call < conditional_checkpoint);
        assert!(conditional_checkpoint < conditional_close);

        for opcode in [0x4E80_0421_u32, 0x4E80_0021] {
            let words = [opcode, 0x4E80_0020];
            let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
            let output =
                lower_words(0x8000_5000, &bytes).expect("indirect linked branch is supported");
            let target_capture = output
                .find("const std::uint32_t branch_target")
                .expect("indirect linked branch captures its target");
            let lr_write = output
                .find("context->lr = 0x80005004u;")
                .expect("indirect linked branch writes its return address");
            assert!(target_capture < lr_write);
            assert_eq!(
                output
                    .matches(
                        "galaxy::call_return_checkpoint(services, 0x80005000u, 0x80005004u, context, memory);"
                    )
                    .count(),
                1
            );
        }

        let unlinked = lower_words(0x8000_6000, &0x4E80_0420_u32.to_be_bytes())
            .expect("unlinked indirect branch is supported");
        assert!(!unlinked.contains("call_return_checkpoint"));

        let unlinked_tail = lower_words(0x8000_7000, &0x4800_1000_u32.to_be_bytes())
            .expect("unlinked direct tail branch is supported");
        assert!(!unlinked_tail.contains("call_return_checkpoint"));

        let final_link = lower_words(0x8000_8000, &0x4800_1001_u32.to_be_bytes())
            .expect("final linked branch is supported");
        assert!(final_link.contains(
            "galaxy::call_return_checkpoint(services, 0x80008000u, 0x80008004u, context, memory);"
        ));
    }

    #[test]
    fn directly_lowers_only_audited_rmge01_tail_calls() {
        let words = [0x7C64_1B78_u32, 0x480D_1EA4];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let callable_entries = BTreeMap::from([(0x804B_6BCC, 0x804B_6BCC)]);
        let output =
            lower_words_for_module(0x803E_4D24, &bytes, &BTreeSet::new(), &callable_entries)
                .expect("audited native tail call is supported");
        assert!(output.contains("context->pc = 0x804B6BCCu;"));
        assert!(output.contains("rmge01::fn_804B6BCC(context, memory, services);"));
        assert!(!output.contains("galaxy::call_guest"));

        let callable_entries = BTreeMap::from([(0x8026_17B4, 0x8026_17B4)]);
        for (function, field_offset) in [
            (0x8026_C0A0_u32, 0x08_u32),
            (0x8026_C0A8_u32, 0x0C_u32),
            (0x8026_C0E4_u32, 0x10_u32),
        ] {
            let branch_pc = function + 4;
            let tail_branch =
                0x4800_0000_u32 | (0x8026_17B4_u32.wrapping_sub(branch_pc) & 0x03FF_FFFC);
            let name_obj_words = [0x8063_0000_u32 | field_offset, tail_branch];
            let name_obj_bytes: Vec<u8> = name_obj_words
                .iter()
                .flat_map(|word| word.to_be_bytes())
                .collect();
            let name_obj = lower_words_for_module(
                function,
                &name_obj_bytes,
                &BTreeSet::new(),
                &callable_entries,
            )
            .expect("audited NameObj tail call is supported");
            assert!(name_obj.contains(&format!(
                "context->gpr[3] = galaxy::guest_load_u32(memory, context->gpr[3] + 0x{field_offset:08X}u"
            )));
            assert!(name_obj.contains("context->pc = 0x802617B4u;"));
            assert!(name_obj.contains("rmge01::fn_802617B4(context, memory, services);"));
            assert!(!name_obj.contains("galaxy::call_guest"));
            assert!(!name_obj.contains("call_return_checkpoint"));
        }

        let ordinary = lower_words(0x8000_4000, &0x4800_1000_u32.to_be_bytes())
            .expect("ordinary external tail call is supported");
        assert!(ordinary.contains("galaxy::call_guest"));
        assert!(!ordinary.contains("rmge01::fn_"));
    }

    #[test]
    fn directly_inlines_only_audited_rmge01_wrapper_calls() {
        let callable_entries = BTreeMap::from([
            (0x8001_7DC8, 0x8001_7DC8),
            (0x8043_79B4, 0x8043_79B4),
            (0x8027_4A2C, 0x8027_4A2C),
            (0x8027_3C24, 0x8027_3C24),
            (0x8044_0CDC, 0x8044_0CDC),
            (0x8043_3590, 0x8043_3590),
            (0x8049_5694, 0x8049_5694),
            (0x8049_5EA0, 0x8049_5EA0),
            (0x8034_4AA8, 0x8034_4AA8),
            (0x8039_D440, 0x8039_D440),
            (0x8039_D44C, 0x8039_D44C),
        ]);

        let mut add_tail = String::new();
        emit_direct_guest_call(
            &mut add_tail,
            0x8001_7DC8,
            0x8000_4000,
            Some(&callable_entries),
        )
        .expect("add-and-tail wrapper inline call is supported");
        assert!(add_tail.contains("context->gpr[3] = context->gpr[5] + 0x00000058u;"));
        assert!(add_tail.contains("context->pc = 0x804379B4u;"));
        assert!(add_tail.contains("rmge01::fn_804379B4(context, memory, services);"));
        assert!(!add_tail.contains("rmge01::fn_80017DC8"));
        assert!(!add_tail.contains("galaxy::call_guest"));

        let mut npc = String::new();
        emit_direct_guest_call(&mut npc, 0x8027_4A2C, 0x8000_4000, Some(&callable_entries))
            .expect("NPC wrapper inline call is supported");
        assert!(npc.contains(
            "context->gpr[3] = galaxy::guest_load_u32(memory, context->gpr[4], services, 0x80274A2Cu);"
        ));
        assert!(npc.contains("context->pc = 0x80273C24u;"));
        assert!(npc.contains("rmge01::fn_80273C24(context, memory, services);"));
        assert!(!npc.contains("rmge01::fn_80274A2C"));
        assert!(!npc.contains("galaxy::call_guest"));

        let mut j3d = String::new();
        emit_direct_guest_call(&mut j3d, 0x8044_0CDC, 0x8000_4000, Some(&callable_entries))
            .expect("J3D wrapper inline call is supported");
        assert!(j3d.contains(
            "galaxy::load_fpr_single(context, 1u, memory, context->gpr[3] + 0x00000008u, services, 0x80440CDCu);"
        ));
        assert!(j3d.contains("context->pc = 0x80433590u;"));
        assert!(j3d.contains("rmge01::fn_80433590(context, memory, services);"));
        assert!(!j3d.contains("rmge01::fn_80440CDC"));
        assert!(!j3d.contains("galaxy::call_guest"));

        let mut tail_only = String::new();
        emit_direct_guest_call(
            &mut tail_only,
            0x8049_5694,
            0x8000_4000,
            Some(&callable_entries),
        )
        .expect("tail-only wrapper inline call is supported");
        assert!(tail_only.contains("context->pc = 0x80495EA0u;"));
        assert!(tail_only.contains("rmge01::fn_80495EA0(context, memory, services);"));
        assert!(!tail_only.contains("rmge01::fn_80495694"));
        assert!(!tail_only.contains("galaxy::call_guest"));

        let mut scene_holder = String::new();
        emit_direct_guest_call(
            &mut scene_holder,
            0x8034_4AA8,
            0x8000_4000,
            Some(&callable_entries),
        )
        .expect("scene object holder accessor inline call is supported");
        assert!(scene_holder.contains(
            "context->gpr[3] = galaxy::guest_load_u32(memory, context->gpr[13] + 0xFFFFC588u, services, 0x80344AA8u);"
        ));
        assert!(scene_holder.contains(
            "context->gpr[3] = galaxy::guest_load_u32(memory, context->gpr[3] + 0x00000024u, services, 0x80344AACu);"
        ));
        assert!(scene_holder.contains("context->pc = 0x8039D44Cu;"));
        assert!(scene_holder.contains(
            "context->gpr[3] = galaxy::guest_load_u32(memory, context->gpr[3] + 0x000000ACu, services, 0x8039D44Cu);"
        ));
        assert!(scene_holder.contains(
            "context->gpr[3] = galaxy::guest_load_u32(memory, context->gpr[3] + 0x00000010u, services, 0x8039D450u);"
        ));
        assert!(!scene_holder.contains("rmge01::fn_80344AA8"));
        assert!(!scene_holder.contains("rmge01::fn_8039D44C"));
        assert!(!scene_holder.contains("galaxy::call_guest"));

        let mut scene_obj = String::new();
        emit_direct_guest_call(
            &mut scene_obj,
            0x8039_D440,
            0x8000_4000,
            Some(&callable_entries),
        )
        .expect("scene object accessor inline call is supported");
        assert!(scene_obj.contains("context->pc = 0x8039D440u;"));
        assert!(scene_obj.contains(
            "context->gpr[3] = galaxy::guest_load_u32(memory, context->gpr[3] + 0x000000ACu, services, 0x8039D440u);"
        ));
        assert!(scene_obj.contains(
            "context->gpr[3] = galaxy::guest_load_u32(memory, context->gpr[3] + 0x00000008u, services, 0x8039D444u);"
        ));
        assert!(!scene_obj.contains("rmge01::fn_8039D440"));
        assert!(!scene_obj.contains("galaxy::call_guest"));

        let fallback_entries = BTreeMap::from([(0x8027_4A2C, 0x8027_4A2C)]);
        let mut fallback = String::new();
        emit_direct_guest_call(
            &mut fallback,
            0x8027_4A2C,
            0x8000_4000,
            Some(&fallback_entries),
        )
        .expect("wrapper without audited tail target falls back");
        assert!(fallback.contains("context->pc = 0x80274A2Cu;"));
        assert!(fallback.contains("rmge01::fn_80274A2C(context, memory, services);"));
    }

    #[test]
    fn fpu_functions_bypass_entry_only_native_call_substitutions() {
        let callable_entries = BTreeMap::from([
            (0x8001_CF80, 0x8001_CF80),
            (0x8044_0CDC, 0x8044_0CDC),
            (0x8043_3590, 0x8043_3590),
        ]);
        let exact_fpu_functions = BTreeSet::from([0x8001_CF80, 0x8044_0CDC]);

        let mut pure_helper = String::new();
        emit_direct_guest_call_with_exact_fpu(
            &mut pure_helper,
            0x8001_CF80,
            0x8000_4000,
            Some(&callable_entries),
            Some(&exact_fpu_functions),
        )
        .expect("FP pure-helper target falls back to exact generated function");
        assert!(pure_helper.contains("context->pc = 0x8001CF80u;"));
        assert!(pure_helper.contains("rmge01::fn_8001CF80(context, memory, services);"));
        assert!(!pure_helper.contains("native_vec_add_8001CF80"));

        let mut inline_wrapper = String::new();
        emit_direct_guest_call_with_exact_fpu(
            &mut inline_wrapper,
            0x8044_0CDC,
            0x8000_4000,
            Some(&callable_entries),
            Some(&exact_fpu_functions),
        )
        .expect("FP inline-wrapper target falls back to exact generated function");
        assert!(inline_wrapper.contains("context->pc = 0x80440CDCu;"));
        assert!(inline_wrapper.contains("rmge01::fn_80440CDC(context, memory, services);"));
        assert!(!inline_wrapper.contains("galaxy::load_fpr_single"));
        assert!(!inline_wrapper.contains("rmge01::fn_80433590"));
    }

    fn assert_module_init_requires_decrementer_write_service(source: &str) {
        let (_, init) = source
            .split_once(
                "extern \"C\" bool galaxy_module_init(const galaxy::NativeServicesV1* services) {",
            )
            .expect("generated module exports its initialization contract");
        let (rejected_contract, _) = init
            .split_once("return false;")
            .expect("generated module initialization has a rejection path");

        assert!(rejected_contract.contains("services->decrementer_written == nullptr"));
    }

    #[test]
    fn generated_single_module_init_requires_decrementer_write_service() {
        let function = FunctionRange {
            address: 0x8000_4000,
            size: 4,
        };
        let source = emit_module(function, "    return;\n", false)
            .expect("single-function module generation succeeds");

        assert_module_init_requires_decrementer_write_service(&source);
    }

    #[test]
    fn generated_sharded_module_init_requires_decrementer_write_service() {
        let function = FunctionRange {
            address: 0x8000_4000,
            size: 4,
        };
        let files = emit_sharded_module(
            &[(function, "    return;\n".to_owned())],
            &[],
            1,
            function.address,
        )
        .expect("sharded module generation succeeds");
        let module = files
            .iter()
            .find(|file| file.name == "module.cpp")
            .expect("sharded module lookup source exists");

        assert_module_init_requires_decrementer_write_service(&module.contents);
    }

    #[test]
    fn module_lowers_direct_calls_to_resolved_native_functions() {
        let words = [0x4800_1001_u32, 0x4E80_0020];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let callable_entries = BTreeMap::from([(0x8000_5000, 0x8000_5000)]);
        let output =
            lower_words_for_module(0x8000_4000, &bytes, &BTreeSet::new(), &callable_entries)
                .expect("direct call is supported");
        assert!(output.contains("context->lr = 0x80004004u;"));
        assert!(output.contains("context->pc = 0x80005000u;"));
        assert!(output.contains("rmge01::fn_80005000(context, memory, services);"));
        assert!(!output.contains("galaxy::call_guest_direct_resolved"));

        const GENERATED_DIRECT_ROUTE_MARKERS: [u32; 11] = [
            0x802A_FD80,
            0x802B_0D0C,
            0x802B_0E18,
            0x802B_0FE4,
            0x802B_2A28,
            0x802B_321C,
            0x802C_AC98,
            0x802C_AF9C,
            0x8038_D2E4,
            0x8038_D34C,
            0x8038_D868,
        ];
        for target in GENERATED_DIRECT_ROUTE_MARKERS {
            let source = target - 0x1000;
            let marker_entries = BTreeMap::from([(target, target)]);
            for (branch_word, linked) in [(0x4800_1001_u32, true), (0x4800_1000_u32, false)] {
                let marker_words = [branch_word, 0x4E80_0020];
                let marker_bytes: Vec<u8> = marker_words
                    .iter()
                    .flat_map(|word| word.to_be_bytes())
                    .collect();
                let marker = lower_words_for_module(
                    source,
                    &marker_bytes,
                    &BTreeSet::new(),
                    &marker_entries,
                )
                .expect("route-marker direct call is supported");
                let helper = if target == 0x8038_D2E4 {
                    "call_guest_resolved"
                } else {
                    "call_guest_direct_resolved"
                };
                assert!(marker.contains(&format!(
                    "galaxy::{helper}(services, 0x{target:08X}u, &rmge01::fn_{target:08X}, context, memory, 0x{source:08X}u);"
                )));
                assert!(!marker.contains(&format!("context->pc = 0x{target:08X}u;")));
                assert_eq!(
                    marker.contains("galaxy::call_return_checkpoint"),
                    linked,
                    "only a linked generated-direct route marker returns to a call checkpoint"
                );
            }
        }

        let interior_words = [0x4800_1005_u32, 0x4E80_0020];
        let interior_bytes: Vec<u8> = interior_words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        let callable_entries = BTreeMap::from([(0x8000_5004, 0x8000_5000)]);
        let interior = lower_words_for_module(
            0x8000_4000,
            &interior_bytes,
            &BTreeSet::new(),
            &callable_entries,
        )
        .expect("interior direct call is supported");
        assert!(interior.contains("context->pc = 0x80005004u;"));
        assert!(interior.contains("rmge01::fn_80005000(context, memory, services);"));

        let host_words = [0x484A_7FBD_u32, 0x4E80_0020]; // bl 0x804ABFBC
        let host_bytes: Vec<u8> = host_words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        let callable_entries = BTreeMap::from([(0x804A_BFBC, 0x804A_BFBC)]);
        let host = lower_words_for_module(
            0x8000_4000,
            &host_bytes,
            &BTreeSet::new(),
            &callable_entries,
        )
        .expect("host-intercepted direct call is supported");
        assert!(host
            .contains("galaxy::call_guest_resolved(services, 0x804ABFBCu, &rmge01::fn_804ABFBC"));

        let kpad_words = [0x4844_C6D9_u32, 0x4E80_0020]; // bl 0x804506D8
        let kpad_bytes: Vec<u8> = kpad_words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        let callable_entries = BTreeMap::from([(0x8045_06D8, 0x8045_06D8)]);
        let kpad = lower_words_for_module(
            0x8000_4000,
            &kpad_bytes,
            &BTreeSet::new(),
            &callable_entries,
        )
        .expect("direct KPADRead call is host-routed");
        assert!(kpad
            .contains("galaxy::call_guest_resolved(services, 0x804506D8u, &rmge01::fn_804506D8"));

        for (pc, target) in [
            (0x8000_4000u32, 0x8038_5034u32),
            (0x8000_4000, 0x8038_52BC),
            (0x8038_5AD8, 0x804B_9EC8),
            (0x8034_BF8C, 0x803A_29B8),
            (0x8036_FDD4, 0x8038_D1CC),
            (0x8036_FEC8, 0x8037_0398),
            (0x8037_0278, 0x8037_0398),
        ] {
            let words = [0x4800_0001 | ((target - pc) & 0x03FF_FFFC), 0x4E80_0020];
            let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
            let entries = BTreeMap::from([(target, target)]);
            let output = lower_words_for_module(pc, &bytes, &BTreeSet::new(), &entries)
                .expect("mouse position/depth transactions preserve resolved host boundary");
            assert!(output.contains(&format!(
                "galaxy::call_guest_resolved(services, 0x{target:08X}u, &rmge01::fn_{target:08X}"
            )));
            assert!(output.contains("call_return_checkpoint"));
            assert_eq!(
                output.contains(&format!(
                    "context->pc = 0x{target:08X}u;\n    galaxy::call_guest_resolved"
                )),
                publishes_movie_boundary_target_pc(target, pc)
            );
        }

        let async_main_words = [0x4839_50A9_u32, 0x4E80_0020]; // bl 0x803990A8
        let async_main_bytes: Vec<u8> = async_main_words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        let callable_entries = BTreeMap::from([(0x8039_90A8, 0x8039_90A8)]);
        let async_main = lower_words_for_module(
            0x8000_4000,
            &async_main_bytes,
            &BTreeSet::new(),
            &callable_entries,
        )
        .expect("FunctionAsyncExecutor::startOnMainThread direct call is host-routed");
        assert!(async_main
            .contains("galaxy::call_guest_resolved(services, 0x803990A8u, &rmge01::fn_803990A8"));

        let utf16_words = [0x4800_0009_u32, 0x4E80_0020]; // bl 0x800072B4
        let utf16_bytes: Vec<u8> = utf16_words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        let callable_entries = BTreeMap::from([(0x8000_72B4, 0x8000_72B4)]);
        let utf16 = lower_words_for_module(
            0x8000_72AC,
            &utf16_bytes,
            &BTreeSet::new(),
            &callable_entries,
        )
        .expect("UTF-16 reader native helper direct call is supported");
        assert!(utf16.contains(
            "galaxy::native_read_next_char_utf16_800072B4(context, memory, services, 0x800072B4u);"
        ));
        assert!(!utf16.contains("galaxy::call_guest_resolved"));

        for (source, target, expected, description) in [
            (
                0x8009_7270,
                0x8009_7278,
                "galaxy::native_add_12_80097278(context, memory, services, 0x80097278u);",
                "add-12 native helper direct call is supported",
            ),
            (
                0x8034_4A4C,
                0x8034_4A54,
                "galaxy::native_indexed_word_load_80344A54(context, memory, services, 0x80344A54u);",
                "indexed-word-load native helper direct call is supported",
            ),
            // WPad input helper direct-call cases removed: those addresses now
            // recompile to their real game functions (no host shim).
        ] {
            let helper_words = [0x4800_0009_u32, 0x4E80_0020];
            let helper_bytes: Vec<u8> = helper_words
                .iter()
                .flat_map(|word| word.to_be_bytes())
                .collect();
            let callable_entries = BTreeMap::from([(target, target)]);
            let lowered = lower_words_for_module(
                source,
                &helper_bytes,
                &BTreeSet::new(),
                &callable_entries,
            )
            .expect(description);
            assert!(lowered.contains(expected));
            assert!(!lowered.contains("galaxy::call_guest_resolved"));
        }

        let stride_words = [0x4800_0009_u32, 0x4E80_0020]; // bl 0x8042E100
        let stride_bytes: Vec<u8> = stride_words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        let callable_entries = BTreeMap::from([(0x8042_E100, 0x8042_E100)]);
        let stride = lower_words_for_module(
            0x8042_E0F8,
            &stride_bytes,
            &BTreeSet::new(),
            &callable_entries,
        )
        .expect("stride-six native helper direct call is supported");
        assert!(stride.contains(
            "galaxy::native_stride6_offset8_8042E100(context, memory, services, 0x8042E100u);"
        ));
        assert!(!stride.contains("galaxy::call_guest_resolved"));

        let helper_words = [0x4800_0009_u32, 0x4E80_0020]; // bl 0x804B6CB8
        let helper_bytes: Vec<u8> = helper_words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        let callable_entries = BTreeMap::from([(0x804B_6CB8, 0x804B_6CB8)]);
        let helper = lower_words_for_module(
            0x804B_6CB0,
            &helper_bytes,
            &BTreeSet::new(),
            &callable_entries,
        )
        .expect("native pure helper direct call is supported");
        assert!(helper.contains(
            "galaxy::native_psvec_cross_product(context, memory, services, 0x804B6CB8u);"
        ));
        assert!(!helper.contains("galaxy::call_guest_resolved"));

        let noop_words = [0x4800_0009_u32, 0x4E80_0020]; // bl 0x803A33AC
        let noop_bytes: Vec<u8> = noop_words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        let callable_entries = BTreeMap::from([(0x803A_33AC, 0x803A_33AC)]);
        let noop = lower_words_for_module(
            0x803A_33A4,
            &noop_bytes,
            &BTreeSet::new(),
            &callable_entries,
        )
        .expect("JPA no-op native helper direct call is supported");
        assert!(
            noop.contains("galaxy::native_noop_803A33AC(context, memory, services, 0x803A33ACu);")
        );
        assert!(!noop.contains("galaxy::call_guest_resolved"));

        let mtx_scale_words = [0x4800_0009_u32, 0x4E80_0020]; // bl 0x803A387C
        let mtx_scale_bytes: Vec<u8> = mtx_scale_words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        let callable_entries = BTreeMap::from([(0x803A_387C, 0x803A_387C)]);
        let mtx_scale = lower_words_for_module(
            0x803A_3874,
            &mtx_scale_bytes,
            &BTreeSet::new(),
            &callable_entries,
        )
        .expect("native matrix-scale helper direct call is supported");
        assert!(mtx_scale.contains(
            "galaxy::native_mtx_scale_803A387C(context, memory, services, 0x803A387Cu);"
        ));
        assert!(!mtx_scale.contains("galaxy::call_guest_direct_resolved"));
        assert!(!mtx_scale.contains("galaxy::call_guest_resolved"));

        let jpa_draw_words = [0x4800_0009_u32, 0x4E80_0020]; // bl 0x803A3B6C
        let jpa_draw_bytes: Vec<u8> = jpa_draw_words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        let callable_entries = BTreeMap::from([(0x803A_3B6C, 0x803A_3B6C)]);
        let jpa_draw = lower_words_for_module(
            0x803A_3B64,
            &jpa_draw_bytes,
            &BTreeSet::new(),
            &callable_entries,
        )
        .expect("JPA draw exact generated direct call is supported");
        assert!(jpa_draw.contains("context->pc = 0x803A3B6Cu;"));
        assert!(jpa_draw.contains("rmge01::fn_803A3B6C(context, memory, services);"));
        assert!(jpa_draw.contains(
            "galaxy::call_return_checkpoint(services, 0x803A3B64u, 0x803A3B68u, context, memory);"
        ));
        assert!(!jpa_draw.contains("native_jpa_draw_particle_803A3B6C"));
        assert!(!jpa_draw.contains("galaxy::call_guest_direct_resolved"));
        assert!(!jpa_draw.contains("galaxy::call_guest_resolved"));

        let normalize_words = [0x4800_0009_u32, 0x4E80_0020]; // bl 0x804B6BCC
        let normalize_bytes: Vec<u8> = normalize_words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        let callable_entries = BTreeMap::from([(0x804B_6BCC, 0x804B_6BCC)]);
        let normalize = lower_words_for_module(
            0x804B_6BC4,
            &normalize_bytes,
            &BTreeSet::new(),
            &callable_entries,
        )
        .expect("native PS vector normalize direct call is supported");
        assert!(normalize.contains(
            "galaxy::native_psvec_normalize_804B6BCC(context, memory, services, 0x804B6BCCu);"
        ));
        assert!(!normalize.contains("galaxy::call_guest_resolved"));

        let normalize_wrapper_words = [0x4800_0009_u32, 0x4E80_0020]; // bl 0x803E4D24
        let normalize_wrapper_bytes: Vec<u8> = normalize_wrapper_words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        let callable_entries = BTreeMap::from([(0x803E_4D24, 0x803E_4D24)]);
        let normalize_wrapper = lower_words_for_module(
            0x803E_4D1C,
            &normalize_wrapper_bytes,
            &BTreeSet::new(),
            &callable_entries,
        )
        .expect("exact in-place normalize wrapper direct call is supported");
        assert!(normalize_wrapper.contains("context->pc = 0x803E4D24u;"));
        assert!(normalize_wrapper.contains("rmge01::fn_803E4D24(context, memory, services);"));
        assert!(!normalize_wrapper.contains("native_vec_normalize_in_place_803E4D24"));
        assert!(!normalize_wrapper.contains("galaxy::call_guest_resolved"));

        let vec_threshold_words = [0x4800_0009_u32, 0x4E80_0020]; // bl 0x803E595C
        let vec_threshold_bytes: Vec<u8> = vec_threshold_words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        let callable_entries = BTreeMap::from([(0x803E_595C, 0x803E_595C)]);
        let vec_threshold = lower_words_for_module(
            0x803E_5954,
            &vec_threshold_bytes,
            &BTreeSet::new(),
            &callable_entries,
        )
        .expect("native vector-threshold helper direct call is supported");
        assert!(vec_threshold.contains(
            "galaxy::native_vec3_abs_le_threshold_803E595C(context, memory, services, 0x803E595Cu);"
        ));
        assert!(!vec_threshold.contains("galaxy::call_guest_resolved"));

        for (source, target, expected, description) in [
            (
                0x8001_CF78,
                0x8001_CF80,
                "galaxy::native_vec_add_8001CF80(context, memory, services, 0x8001CF80u);",
                "native vector-add helper direct call is supported",
            ),
            (
                0x8001_FD64,
                0x8001_FD6C,
                "galaxy::native_vec_scale_8001FD6C(context, memory, services, 0x8001FD6Cu);",
                "native vector-scale helper direct call is supported",
            ),
        ] {
            let vec_words = [0x4800_0009_u32, 0x4E80_0020];
            let vec_bytes: Vec<u8> = vec_words
                .iter()
                .flat_map(|word| word.to_be_bytes())
                .collect();
            let callable_entries = BTreeMap::from([(target, target)]);
            let lowered =
                lower_words_for_module(source, &vec_bytes, &BTreeSet::new(), &callable_entries)
                    .expect(description);
            assert!(lowered.contains(expected));
            assert!(!lowered.contains("galaxy::call_guest_resolved"));
        }

        let gx_color_words = [0x4800_0009_u32, 0x4E80_0020]; // bl 0x804BD28C
        let gx_color_bytes: Vec<u8> = gx_color_words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        let callable_entries = BTreeMap::from([(0x804B_D28C, 0x804B_D28C)]);
        let gx_color = lower_words_for_module(
            0x804B_D284,
            &gx_color_bytes,
            &BTreeSet::new(),
            &callable_entries,
        )
        .expect("GX channel color native helper direct call is supported");
        assert!(gx_color.contains(
            "galaxy::native_gx_set_chan_color_804BD28C(context, memory, services, 0x804BD28Cu);"
        ));
        assert!(!gx_color.contains("galaxy::call_guest_resolved"));

        for (source, target, expected, description) in [
            (
                0x804B_5ED4,
                0x804B_5EDC,
                "galaxy::native_psmtx_identity_804B5EDC(context, memory, services, 0x804B5EDCu);",
                "PSMTXIdentity native helper direct call is supported",
            ),
            (
                0x804B_5F00,
                0x804B_5F08,
                "galaxy::native_psmtx_copy_804B5F08(context, memory, services, 0x804B5F08u);",
                "PSMTXCopy native helper direct call is supported",
            ),
        ] {
            let matrix_words = [0x4800_0009_u32, 0x4E80_0020];
            let matrix_bytes: Vec<u8> = matrix_words
                .iter()
                .flat_map(|word| word.to_be_bytes())
                .collect();
            let callable_entries = BTreeMap::from([(target, target)]);
            let lowered =
                lower_words_for_module(source, &matrix_bytes, &BTreeSet::new(), &callable_entries)
                    .expect(description);
            assert!(lowered.contains(expected));
            assert!(!lowered.contains("galaxy::call_guest_resolved"));
        }

        for (source, target, expected, description) in [
            (
                0x804B_A6A8,
                0x804B_A6B0,
                "rmge01::fn_804BA6B0(context, memory, services);",
                "GX primitive-begin exact generated direct call is supported",
            ),
            (
                0x804B_DEA0,
                0x804B_DEA8,
                "rmge01::fn_804BDEA8(context, memory, services);",
                "GX begin exact generated direct call is supported",
            ),
            (
                0x804B_E1D0,
                0x804B_E1D8,
                "galaxy::native_gx_load_pos_mtx_imm_804BE1D8(context, memory, services, 0x804BE1D8u);",
                "GX position-matrix load native helper direct call is supported",
            ),
            (
                0x804B_E594,
                0x804B_E59C,
                "galaxy::native_gx_set_array_804BE59C(context, memory, services, 0x804BE59Cu);",
                "GX set-array native helper direct call is supported",
            ),
        ] {
            let gx_words = [0x4800_0009_u32, 0x4E80_0020];
            let gx_bytes: Vec<u8> = gx_words
                .iter()
                .flat_map(|word| word.to_be_bytes())
                .collect();
            let callable_entries = BTreeMap::from([(target, target)]);
            let lowered = lower_words_for_module(
                source,
                &gx_bytes,
                &BTreeSet::new(),
                &callable_entries,
            )
            .expect(description);
            assert!(lowered.contains(expected));
            assert!(!lowered.contains("galaxy::call_guest_resolved"));
            if matches!(target, 0x804B_A6B0 | 0x804B_DEA8) {
                assert!(!lowered.contains("native_gx_begin_"));
                assert!(lowered.contains("galaxy::call_return_checkpoint"));
            }
        }

        let jpa_calc_child_words = [0x4800_0009_u32, 0x4E80_0020]; // bl 0x80443904
        let jpa_calc_child_bytes: Vec<u8> = jpa_calc_child_words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        let callable_entries = BTreeMap::from([(0x8044_3904, 0x8044_3904)]);
        let jpa_calc_child = lower_words_for_module(
            0x8044_38FC,
            &jpa_calc_child_bytes,
            &BTreeSet::new(),
            &callable_entries,
        )
        .expect("JPA calc_c exact generated direct call is supported");
        assert!(jpa_calc_child.contains("context->pc = 0x80443904u;"));
        assert!(jpa_calc_child.contains("rmge01::fn_80443904(context, memory, services);"));
        assert!(jpa_calc_child.contains(
            "galaxy::call_return_checkpoint(services, 0x804438FCu, 0x80443900u, context, memory);"
        ));
        assert!(!jpa_calc_child.contains("native_jpa_calc_child_func_list_80443904"));
        assert!(!jpa_calc_child.contains("galaxy::call_guest_resolved"));

        for (source, target, expected, description) in [
            (
                0x8044_3D70,
                0x8044_3D78,
                "galaxy::native_jpa_list_remove_80443D78(context, memory, services, 0x80443D78u);",
                "JPA list-remove native helper direct call is supported",
            ),
            (
                0x8044_3E1C,
                0x8044_3E24,
                "galaxy::native_jpa_list_append_80443E24(context, memory, services, 0x80443E24u);",
                "JPA list-append native helper direct call is supported",
            ),
        ] {
            let list_words = [0x4800_0009_u32, 0x4E80_0020];
            let list_bytes: Vec<u8> = list_words
                .iter()
                .flat_map(|word| word.to_be_bytes())
                .collect();
            let callable_entries = BTreeMap::from([(target, target)]);
            let lowered =
                lower_words_for_module(source, &list_bytes, &BTreeSet::new(), &callable_entries)
                    .expect(description);
            assert!(lowered.contains(expected));
            assert!(!lowered.contains("galaxy::call_guest_resolved"));
        }

        for (source, target, expected, description) in [
            (
                0x8044_725C,
                0x8044_7264,
                "galaxy::native_jpa_actor_flags_mask_80447264(context, memory, services, 0x80447264u);",
                "JPA actor-flag mask native helper direct call is supported",
            ),
            (
                0x8044_7274,
                0x8044_727C,
                "galaxy::native_jpa_random_i16_8044727C(context, memory, services, 0x8044727Cu);",
                "JPA random native helper direct call is supported",
            ),
        ] {
            let jpa_words = [0x4800_0009_u32, 0x4E80_0020];
            let jpa_bytes: Vec<u8> = jpa_words
                .iter()
                .flat_map(|word| word.to_be_bytes())
                .collect();
            let callable_entries = BTreeMap::from([(target, target)]);
            let lowered =
                lower_words_for_module(source, &jpa_bytes, &BTreeSet::new(), &callable_entries)
                    .expect(description);
            assert!(lowered.contains(expected));
            assert!(!lowered.contains("galaxy::call_guest_resolved"));
        }

        let savegpr_words = [0x4800_0009_u32, 0x4E80_0020]; // bl 0x80517538
        let savegpr_bytes: Vec<u8> = savegpr_words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        let callable_entries = BTreeMap::from([(0x8051_7538, 0x8051_74FC)]);
        let savegpr = lower_words_for_module(
            0x8051_7530,
            &savegpr_bytes,
            &BTreeSet::new(),
            &callable_entries,
        )
        .expect("savegpr interior native helper direct call is supported");
        assert!(savegpr
            .contains("galaxy::native_savegpr_805174FC(context, memory, services, 0x80517538u);"));
        assert!(!savegpr.contains("rmge01::fn_805174FC"));
        assert!(!savegpr.contains("galaxy::call_guest_resolved"));

        let restgpr_words = [0x4800_0009_u32, 0x4E80_0020]; // bl 0x80517584
        let restgpr_bytes: Vec<u8> = restgpr_words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        let callable_entries = BTreeMap::from([(0x8051_7584, 0x8051_7548)]);
        let restgpr = lower_words_for_module(
            0x8051_757C,
            &restgpr_bytes,
            &BTreeSet::new(),
            &callable_entries,
        )
        .expect("restgpr interior native helper direct call is supported");
        assert!(restgpr
            .contains("galaxy::native_restgpr_80517548(context, memory, services, 0x80517584u);"));
        assert!(!restgpr.contains("rmge01::fn_80517548"));
        assert!(!restgpr.contains("galaxy::call_guest_resolved"));
    }

    #[test]
    fn emits_pre_copy_trace_only_for_the_audited_wpad_memmove_edges() {
        let callable_entries = BTreeMap::from([(0x8000_4338, 0x8000_4338)]);
        let mut traced = String::new();
        emit_direct_guest_call(
            &mut traced,
            0x8000_4338,
            0x804D_94B0,
            Some(&callable_entries),
        )
        .expect("audited WPAD memmove call lowers");
        const TRACE: &str =
            "galaxy::native_input_trace_copy_pre_checkpoint(services, 0x804D94B0u, context, memory);";
        assert!(traced.contains(TRACE));
        assert!(
            traced.find(TRACE).expect("pre-copy trace is present")
                < traced
                    .find("context->pc = 0x80004338u;")
                    .expect("direct memmove body")
        );

        let mut ordinary = String::new();
        emit_direct_guest_call(
            &mut ordinary,
            0x8000_4338,
            0x804D_94B4,
            Some(&callable_entries),
        )
        .expect("ordinary direct memmove call lowers");
        assert!(!ordinary.contains("native_input_trace_copy_pre_checkpoint"));
    }

    #[test]
    fn restartability_sensitive_direct_calls_use_exact_generated_functions() {
        for (target, retired_helper) in [
            (0x8000_4338_u32, "native_memmove"),
            (0x8000_7D2C_u32, "native_resfont_get_char_width_80007D2C"),
            (0x8000_C53C_u32, "native_particle_list_apply_8000C53C"),
            (0x8001_5394_u32, "native_strncmp_eq_16_80015394"),
            (0x8001_8A98_u32, "native_vec_zero"),
            (0x8001_8B8C_u32, "native_vec_copy_12"),
            (0x8001_8BA0_u32, "native_vec_add"),
            (0x8001_CF64_u32, "native_vec_copy_12"),
            (0x800C_9078_u32, "native_ptmf_wrapper_800C9078"),
            (0x800C_9248_u32, "rmge01::fn_803C73CC"),
            (0x800C_9AAC_u32, "native_ptmf_wrapper_800C9AAC"),
            (0x8026_1254_u32, "native_vtable_flag_dispatch_80261254"),
            (0x8026_1454_u32, "native_movement_group_dispatch_80261454"),
            (0x8026_1494_u32, "native_movement_group_dispatch_80261494"),
            (0x8026_1B40_u32, "native_ptmf_wrapper_80261B40"),
            (0x8026_1B70_u32, "native_ptmf_wrapper_80261B70"),
            (
                0x8026_C0A0_u32,
                "native_name_object_dispatch_offset8_8026C0A0",
            ),
            (
                0x8026_C0A8_u32,
                "native_name_object_dispatch_offsetc_8026C0A8",
            ),
            (
                0x8026_C0E4_u32,
                "native_name_object_dispatch_offset10_8026C0E4",
            ),
            (0x803A_3B6C_u32, "native_jpa_draw_particle_803A3B6C"),
            (0x803A_35DC_u32, "native_jpa_vec_copy_803A35DC"),
            (0x803A_35E8_u32, "native_jpa_vec_copy_803A35E8"),
            (0x803A_A870_u32, "native_ring_vec_fetch"),
            (0x803D_3C64_u32, "native_name_hash"),
            (0x8044_3904_u32, "native_jpa_calc_child_func_list_80443904"),
            (
                0x8044_1400_u32,
                "native_jpa_resource_manager_get_resource_80441400",
            ),
            (0x8044_432C_u32, "native_jpa_regist_child_prm_env_8044432C"),
            (0x8044_592C_u32, "native_jpa_emitter_callback_8044592C"),
            (0x8044_8710_u32, "native_actor_list_apply_80448710"),
            (0x8048_78BC_u32, "native_audio_interleave_i16_804878BC"),
            (0x8048_8344_u32, "native_callback_list_process_80488344"),
            (0x8049_45CC_u32, "native_audio_ring_output_804945CC"),
            (0x8049_4B2C_u32, "native_audio_message_loop_80494B2C"),
            (0x8049_559C_u32, "native_audio_voice_update_8049559C"),
            (0x8049_5C3C_u32, "native_jas_dsp_set_bus_connect_80495C3C"),
            (0x8049_5C5C_u32, "native_audio_voice_flag_mask_80495C5C"),
            (0x8049_6A60_u32, "native_dsp_running_check_80496A60"),
            (0x804A_2EF4_u32, "native_cache_maintenance_range"),
            (0x804A_2F20_u32, "native_cache_maintenance_range"),
            (0x804A_2F50_u32, "native_cache_maintenance_range"),
            (0x804A_2F80_u32, "native_cache_maintenance_range"),
            (0x804A_2FAC_u32, "native_cache_maintenance_range"),
            (0x804B_5F3C_u32, "native_psmtx_concat_804B5F3C"),
            (0x804B_A6B0_u32, "native_gx_begin_804BA6B0"),
            (0x804B_A7FC_u32, "native_gx_send_zero_primitive_804BA7FC"),
            (0x804B_DEA8_u32, "native_gx_begin_804BDEA8"),
            (0x8051_6E80_u32, "native_strlen_80516E80"),
            (0x8051_E830_u32, "native_strcmp_8051E830"),
            (0x8051_EA04_u32, "native_strstr"),
        ] {
            let source = target - 8;
            let words = [0x4800_0009_u32, 0x4E80_0020];
            let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
            let callable_entries = BTreeMap::from([(target, target)]);
            let lowered =
                lower_words_for_module(source, &bytes, &BTreeSet::new(), &callable_entries)
                    .expect("restartability-sensitive direct call lowers");

            assert!(lowered.contains(&format!("context->pc = 0x{target:08X}u;")));
            assert!(lowered.contains(&format!(
                "rmge01::fn_{target:08X}(context, memory, services);"
            )));
            assert!(lowered.contains(&format!(
                "galaxy::call_return_checkpoint(services, 0x{source:08X}u, 0x{:08X}u, context, memory);",
                source + 4
            )));
            assert!(!lowered.contains(retired_helper));
            assert!(!lowered.contains("galaxy::call_guest_resolved"));
        }
    }

    #[test]
    fn module_emits_passive_function_async_causality_boundaries() {
        let worker_entries = BTreeMap::from([(0x8039_8D3C, 0x8039_8D3C)]);
        let mut worker = String::new();
        emit_direct_guest_call(&mut worker, 0x8039_8D3C, 0x8039_8E7C, Some(&worker_entries))
            .expect("FunctionAsync worker execute call is supported");
        let begin_index = worker
            .find("galaxy::function_async_trace_callback(services, galaxy::kFunctionAsyncTraceWorkerBegin, context, memory);")
            .expect("worker begin observation is emitted");
        let call_index = worker
            .find("rmge01::fn_80398D3C(context, memory, services);")
            .expect("real translated worker execute call remains present");
        assert!(begin_index < call_index);
        assert!(worker.contains("context->pc = 0x80398D3Cu;"));
        assert!(!worker.contains("call_function_async_worker_execute"));
        assert!(!worker.contains("kFunctionAsyncTraceWorkerComplete"));

        let mut main_thread = String::new();
        emit_direct_guest_call(
            &mut main_thread,
            0x8039_8D3C,
            0x8039_8F2C,
            Some(&worker_entries),
        )
        .expect("main-thread FunctionAsync execute call is supported");
        assert!(main_thread.contains("context->pc = 0x80398D3Cu;"));
        assert!(main_thread.contains("rmge01::fn_80398D3C(context, memory, services);"));
        assert!(!main_thread.contains("call_function_async_worker_execute"));
        assert!(!main_thread.contains("kFunctionAsyncTraceWorkerBegin"));
        assert!(!main_thread.contains("kFunctionAsyncTraceWorkerComplete"));

        let pointer_entries = BTreeMap::from([(0x803A_A6F4, 0x803A_A6F4)]);
        let mut pointer = String::new();
        emit_direct_guest_call(
            &mut pointer,
            0x803A_A6F4,
            0x803A_BDF0,
            Some(&pointer_entries),
        )
        .expect("WPadPointer gameplay status call is supported");
        let call_index = pointer
            .find("rmge01::fn_803AA6F4(context, memory, services);")
            .expect("translated WPad status call remains present");
        let trace_index = pointer
            .find("galaxy::trace_function_async_gameplay_pointer_consumer")
            .expect("gameplay pointer consumer trace hook is present");
        assert!(call_index < trace_index);

        let mut unrelated = String::new();
        emit_direct_guest_call(
            &mut unrelated,
            0x803A_A6F4,
            0x803A_BE00,
            Some(&pointer_entries),
        )
        .expect("unrelated WPad status call is supported");
        assert!(!unrelated.contains("trace_function_async_gameplay_pointer_consumer"));
    }

    #[test]
    fn function_async_worker_complete_marker_survives_call_return_resume() {
        const FUNCTION: u32 = 0x8039_8E00;
        const CALL_PC: u32 = 0x8039_8E7C;
        const RETURN_PC: u32 = 0x8039_8E80;
        const TARGET: u32 = 0x8039_8D3C;
        const SEND_CALL_PC: u32 = 0x8039_8E8C;
        const SEND_RETURN_PC: u32 = 0x8039_8E90;
        const SEND_TARGET: u32 = 0x804A_88E4;

        let mut words = [0x6000_0000_u32; 0x26];
        words[((CALL_PC - FUNCTION) / 4) as usize] =
            0x4800_0001 | (TARGET.wrapping_sub(CALL_PC) & 0x03FF_FFFC);
        words[((SEND_CALL_PC - FUNCTION) / 4) as usize] =
            0x4800_0001 | (SEND_TARGET.wrapping_sub(SEND_CALL_PC) & 0x03FF_FFFC);
        words[((0x8039_8E94 - FUNCTION) / 4) as usize] = 0x4E80_0020;
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let return_entries = BTreeSet::from([RETURN_PC, SEND_RETURN_PC]);
        let callable_entries = BTreeMap::from([(TARGET, TARGET), (SEND_TARGET, SEND_TARGET)]);

        let lowered = lower_words_for_module_with_call_return_entries(
            FUNCTION,
            &bytes,
            &return_entries,
            &return_entries,
            &callable_entries,
        )
        .expect("FunctionAsync worker loop lowers with an exact return alias");

        let dequeued_index = lowered
            .find("galaxy::function_async_trace_callback(services, galaxy::kFunctionAsyncTraceWorkerDequeued, context, memory);")
            .expect("worker dequeue observation is emitted after r29 is loaded");
        let begin_index = lowered
            .find("galaxy::function_async_trace_callback(services, galaxy::kFunctionAsyncTraceWorkerBegin, context, memory);")
            .expect("worker begin observation is emitted");
        let call_index = lowered
            .find("rmge01::fn_80398D3C(context, memory, services);")
            .expect("real translated worker call is emitted");
        let normal_checkpoint_index = lowered
            .find("galaxy::call_return_checkpoint(services, 0x80398E7Cu, 0x80398E80u, context, memory);")
            .expect("normal call-return checkpoint is emitted");
        let continuation_index = lowered
            .find("label_80398E80:")
            .expect("worker return continuation is emitted");
        let complete_index = lowered
            .find("galaxy::function_async_trace_callback(services, galaxy::kFunctionAsyncTraceWorkerComplete, context, memory);")
            .expect("worker completion observation is emitted");
        let send_call_index = lowered
            .find("rmge01::fn_804A88E4(context, memory, services);")
            .expect("real translated completion-message send remains present");
        let send_checkpoint_index = lowered
            .find("galaxy::call_return_checkpoint(services, 0x80398E8Cu, 0x80398E90u, context, memory);")
            .expect("completion-message call-return checkpoint is emitted");
        let send_continuation_index = lowered
            .find("label_80398E90:")
            .expect("completion-message return continuation is emitted");
        let posted_index = lowered
            .find("galaxy::function_async_trace_callback(services, galaxy::kFunctionAsyncTraceDoneMessagePosted, context, memory);")
            .expect("completion-message result observation is emitted");
        let end_flag_index = lowered
            .find("galaxy::function_async_trace_callback(services, galaxy::kFunctionAsyncTraceEndFlagPublished, context, memory);")
            .expect("committed end-flag observation is emitted");
        assert!(dequeued_index < begin_index);
        assert!(begin_index < call_index);
        assert!(call_index < normal_checkpoint_index);
        assert!(normal_checkpoint_index < continuation_index);
        assert!(continuation_index < complete_index);
        assert!(complete_index < send_call_index);
        assert!(send_call_index < send_checkpoint_index);
        assert!(send_checkpoint_index < send_continuation_index);
        assert!(send_continuation_index < posted_index);
        assert!(posted_index < end_flag_index);
        for marker in [
            "kFunctionAsyncTraceWorkerDequeued",
            "kFunctionAsyncTraceWorkerBegin",
            "kFunctionAsyncTraceWorkerComplete",
            "kFunctionAsyncTraceDoneMessagePosted",
            "kFunctionAsyncTraceEndFlagPublished",
        ] {
            assert_eq!(lowered.matches(marker).count(), 1, "duplicate {marker}");
        }
        assert_eq!(
            lowered.matches("kFunctionAsyncTraceWorkerComplete").count(),
            1
        );
        assert!(lowered.contains("case 0x80398E80u: goto call_return_80398E80;"));
        assert!(lowered.contains("case 0x80398E90u: goto call_return_80398E90;"));
        assert!(lowered.contains(
            "call_return_80398E80:\n    galaxy::call_return_checkpoint(services, 0x80398E7Cu, 0x80398E80u, context, memory);\n    goto label_80398E80;"
        ));
        assert!(lowered.contains(
            "call_return_80398E90:\n    galaxy::call_return_checkpoint(services, 0x80398E8Cu, 0x80398E90u, context, memory);\n    goto label_80398E90;"
        ));
        assert!(!lowered.contains("call_function_async_worker_execute"));
    }

    #[test]
    fn function_async_reaped_marker_follows_destructor_return() {
        const FUNCTION: u32 = 0x8039_9144;
        const CALL_PC: u32 = 0x8039_9264;
        const RETURN_PC: u32 = 0x8039_9268;
        const TARGET: u32 = 0x8039_8CE4;

        let mut words = vec![0x6000_0000_u32; 0x4A];
        words[((CALL_PC - FUNCTION) / 4) as usize] =
            0x4800_0001 | (TARGET.wrapping_sub(CALL_PC) & 0x03FF_FFFC);
        words[((RETURN_PC - FUNCTION) / 4) as usize] = 0x4E80_0020;
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let return_entries = BTreeSet::from([RETURN_PC]);
        let callable_entries = BTreeMap::from([(TARGET, TARGET)]);

        let lowered = lower_words_for_module_with_call_return_entries(
            FUNCTION,
            &bytes,
            &return_entries,
            &return_entries,
            &callable_entries,
        )
        .expect("FunctionAsync waitForEnd lowers through the destructor return");

        let call_index = lowered
            .find("rmge01::fn_80398CE4(context, memory, services);")
            .expect("real translated exec-info destructor remains present");
        let checkpoint_index = lowered
            .find("galaxy::call_return_checkpoint(services, 0x80399264u, 0x80399268u, context, memory);")
            .expect("destructor call-return checkpoint is emitted");
        let continuation_index = lowered
            .find("label_80399268:")
            .expect("post-destructor continuation is emitted");
        let reaped_index = lowered
            .find("galaxy::function_async_trace_callback(services, galaxy::kFunctionAsyncTraceReaped, context, memory);")
            .expect("reaped observation is emitted after destructor/free");
        assert!(call_index < checkpoint_index);
        assert!(checkpoint_index < continuation_index);
        assert!(continuation_index < reaped_index);
        assert_eq!(lowered.matches("kFunctionAsyncTraceReaped").count(), 1);
        assert!(lowered.contains("case 0x80399268u: goto call_return_80399268;"));
        assert!(lowered.contains(
            "call_return_80399268:\n    galaxy::call_return_checkpoint(services, 0x80399264u, 0x80399268u, context, memory);\n    goto label_80399268;"
        ));
    }

    #[test]
    fn function_async_exec_info_publication_is_observed_before_unlock() {
        const FUNCTION: u32 = 0x8039_9390;
        const STORE_PC: u32 = 0x8039_9420;
        const UNLOCK_CALL_PC: u32 = 0x8039_9424;
        const UNLOCK_RETURN_PC: u32 = 0x8039_9428;
        const UNLOCK_TARGET: u32 = 0x804A_9548;

        let mut words = [0x6000_0000_u32; 0x2A];
        // stw r30, 0x000C(r4): the exact vector-slot publication.
        words[((STORE_PC - FUNCTION) / 4) as usize] = 0x93C4_000C;
        words[((UNLOCK_CALL_PC - FUNCTION) / 4) as usize] =
            0x4800_0001 | (UNLOCK_TARGET.wrapping_sub(UNLOCK_CALL_PC) & 0x03FF_FFFC);
        words[((0x8039_9434 - FUNCTION) / 4) as usize] = 0x4E80_0020;
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let return_entries = BTreeSet::from([UNLOCK_RETURN_PC]);
        let callable_entries = BTreeMap::from([(UNLOCK_TARGET, UNLOCK_TARGET)]);

        let lowered = lower_words_for_module_with_call_return_entries(
            FUNCTION,
            &bytes,
            &return_entries,
            &return_entries,
            &callable_entries,
        )
        .expect("FunctionAsync exec-info publication lowers through mutex unlock");

        let store_index = lowered
            .find("guest_store_u32(memory, context->gpr[4] + 0x0000000Cu, context->gpr[30]")
            .expect("real executor vector-slot store remains present");
        let published_index = lowered
            .find("galaxy::function_async_trace_callback(services, galaxy::kFunctionAsyncTraceExecInfoPublished, context, memory);")
            .expect("exec-info publication observation is emitted");
        let unlock_index = lowered
            .find("rmge01::fn_804A9548(context, memory, services);")
            .expect("real mutex unlock call remains present");
        assert!(store_index < published_index);
        assert!(published_index < unlock_index);
        assert_eq!(
            lowered
                .matches("kFunctionAsyncTraceExecInfoPublished")
                .count(),
            1
        );
    }

    #[test]
    fn default_psmtx_lowering_allows_unrelated_partial_module_selection() {
        let partial = [FunctionRange {
            address: 0x8000_4000,
            size: 4,
        }];
        assert!(matches!(
            selected_psmtx_local_target(&partial, false),
            Ok(None)
        ));
        assert!(matches!(
            selected_psmtx_local_target(&partial, true),
            Err(TranslationError::LocalPairedProof { .. })
        ));

        let selected = [FunctionRange {
            address: 0x804B_5F3C,
            size: 0x134,
        }];
        assert!(matches!(
            selected_psmtx_local_target(&selected, false),
            Ok(Some(FunctionRange {
                address: 0x804B_5F3C,
                size: 0x134
            }))
        ));
    }

    #[test]
    fn emits_audited_native_function_body_replacements() {
        let replacements = [
            (
                FunctionRange {
                    address: 0x8000_72B4,
                    size: 0x1C,
                },
                "galaxy::native_read_next_char_utf16_800072B4(context, memory, services, 0x800072B4u);",
            ),
            (
                FunctionRange {
                    address: 0x8009_7278,
                    size: 0x08,
                },
                "galaxy::native_add_12_80097278(context, memory, services, 0x80097278u);",
            ),
            (
                FunctionRange {
                    address: 0x803A_33AC,
                    size: 0x04,
                },
                "galaxy::native_noop_803A33AC(context, memory, services, 0x803A33ACu);",
            ),
            (
                FunctionRange {
                    address: 0x803A_387C,
                    size: 0x6C,
                },
                "galaxy::native_mtx_scale_803A387C(context, memory, services, 0x803A387Cu);",
            ),
            (
                FunctionRange {
                    address: 0x8001_CF80,
                    size: 0x24,
                },
                "galaxy::native_vec_add_8001CF80(context, memory, services, 0x8001CF80u);",
            ),
            (
                FunctionRange {
                    address: 0x8001_FD6C,
                    size: 0x28,
                },
                "galaxy::native_vec_scale_8001FD6C(context, memory, services, 0x8001FD6Cu);",
            ),
            (
                FunctionRange {
                    address: 0x8034_4A54,
                    size: 0x0C,
                },
                "galaxy::native_indexed_word_load_80344A54(context, memory, services, 0x80344A54u);",
            ),
            // WPad input body-replacement cases removed (reverted to real game
            // functions; truly-native input belongs at the raw-report boundary).
            (
                FunctionRange {
                    address: 0x803E_595C,
                    size: 0x78,
                },
                "galaxy::native_vec3_abs_le_threshold_803E595C(context, memory, services, 0x803E595Cu);",
            ),
            (
                FunctionRange {
                    address: 0x8044_3D78,
                    size: 0xAC,
                },
                "galaxy::native_jpa_list_remove_80443D78(context, memory, services, 0x80443D78u);",
            ),
            (
                FunctionRange {
                    address: 0x8044_3E24,
                    size: 0x50,
                },
                "galaxy::native_jpa_list_append_80443E24(context, memory, services, 0x80443E24u);",
            ),
            (
                FunctionRange {
                    address: 0x8044_65A0,
                    size: 0x2C,
                },
                "galaxy::native_jpa_alpha_callback_804465A0(context, memory, services, 0x804465A0u);",
            ),
            (
                FunctionRange {
                    address: 0x8044_65CC,
                    size: 0x28,
                },
                "galaxy::native_jpa_scale_callback_804465CC(context, memory, services, 0x804465CCu);",
            ),
            (
                FunctionRange {
                    address: 0x8044_7264,
                    size: 0x18,
                },
                "galaxy::native_jpa_actor_flags_mask_80447264(context, memory, services, 0x80447264u);",
            ),
            (
                FunctionRange {
                    address: 0x8044_727C,
                    size: 0x28,
                },
                "galaxy::native_jpa_random_i16_8044727C(context, memory, services, 0x8044727Cu);",
            ),
            (
                FunctionRange {
                    address: 0x804B_5EDC,
                    size: 0x2C,
                },
                "galaxy::native_psmtx_identity_804B5EDC(context, memory, services, 0x804B5EDCu);",
            ),
            (
                FunctionRange {
                    address: 0x804B_5F08,
                    size: 0x34,
                },
                "galaxy::native_psmtx_copy_804B5F08(context, memory, services, 0x804B5F08u);",
            ),
            (
                FunctionRange {
                    address: 0x804B_63D8,
                    size: 0x4C,
                },
                "galaxy::native_psmtx_trans_apply_804B63D8(context, memory, services, 0x804B63D8u);",
            ),
            (
                FunctionRange {
                    address: 0x804B_6424,
                    size: 0x28,
                },
                "galaxy::native_psmtx_scale_804B6424(context, memory, services, 0x804B6424u);",
            ),
            (
                FunctionRange {
                    address: 0x804B_6BCC,
                    size: 0x44,
                },
                "galaxy::native_psvec_normalize_804B6BCC(context, memory, services, 0x804B6BCCu);",
            ),
            (
                FunctionRange {
                    address: 0x8042_E100,
                    size: 0x10,
                },
                "galaxy::native_stride6_offset8_8042E100(context, memory, services, 0x8042E100u);",
            ),
            (
                FunctionRange {
                    address: 0x804B_E1D8,
                    size: 0x50,
                },
                "galaxy::native_gx_load_pos_mtx_imm_804BE1D8(context, memory, services, 0x804BE1D8u);",
            ),
            (
                FunctionRange {
                    address: 0x804B_E59C,
                    size: 0x28,
                },
                "galaxy::native_gx_set_array_804BE59C(context, memory, services, 0x804BE59Cu);",
            ),
            (
                FunctionRange {
                    address: 0x8051_73E0,
                    size: 0x28,
                },
                "galaxy::native_ptmf_scall_805173E0(context, memory, services, 0x805173E0u);",
            ),
            (
                FunctionRange {
                    address: 0x8051_74FC,
                    size: 0x4C,
                },
                "galaxy::native_savegpr_805174FC(context, memory, services, context->pc);",
            ),
            (
                FunctionRange {
                    address: 0x8051_7548,
                    size: 0x4C,
                },
                "galaxy::native_restgpr_80517548(context, memory, services, context->pc);",
            ),
        ];

        for (function, expected) in replacements {
            let files = emit_sharded_module(
                &[(
                    function,
                    "    /* original guest body marker */\n".to_owned(),
                )],
                &[],
                32,
                function.address,
            )
            .expect("native body replacement module emits");
            let source = files
                .iter()
                .find(|file| file.name == "functions_0000.cpp")
                .expect("function shard exists");
            assert!(source.contents.contains(expected));
            assert!(!source.contents.contains("original guest body marker"));
        }
    }

    #[test]
    fn fpu_function_body_keeps_exact_guarded_translation() {
        let function = FunctionRange {
            address: 0x8001_CF80,
            size: 0x24,
        };
        let exact_body =
            "    galaxy::ensure_fpu_available(services, 0x8001CF80u, context, memory);\n    /* exact FP body marker */\n    return;\n"
                .to_owned();
        let files = emit_sharded_module_with_exact_fpu(
            &[(function, exact_body)],
            &[],
            32,
            function.address,
            &BTreeSet::from([function.address]),
        )
        .expect("exact guarded FP module emits");
        let source = files
            .iter()
            .find(|file| file.name == "functions_0000.cpp")
            .expect("function shard exists");
        assert!(source.contents.contains("exact FP body marker"));
        assert!(source.contents.contains("ensure_fpu_available"));
        assert!(!source.contents.contains("native_vec_add_8001CF80"));
    }

    #[test]
    fn fpu_native_substitution_metadata_is_complete_and_transitive_wrapper_is_retired() {
        for address in [
            0x8001_6F80_u32,
            0x8001_8BC4,
            0x8001_CF80,
            0x8001_FD6C,
            0x803A_387C,
            0x803E_595C,
            0x8044_0CDC,
            0x8044_65A0,
            0x8044_65CC,
            0x804B_5EDC,
            0x804B_5F08,
            0x804B_63D8,
            0x804B_6424,
            0x804B_6BCC,
            0x804B_6CB8,
            0x804B_E1D8,
        ] {
            assert!(
                native_substitution_uses_fpu(address),
                "0x{address:08X} must retain exact generated FP boundaries"
            );
        }
        for address in [0x8000_72B4_u32, 0x803A_33AC, 0x8044_727C, 0x804B_E59C] {
            assert!(!native_substitution_uses_fpu(address));
        }

        assert_eq!(native_pure_helper(0x803E_4D24), None);
        assert_eq!(native_function_body(0x803E_4D24), None);
    }

    #[test]
    fn restartability_sensitive_functions_are_never_replaced() {
        for address in [
            0x8000_4338_u32,
            0x8000_7D2C,
            0x8000_C53C_u32,
            0x8001_5394,
            0x8001_8A98,
            0x8001_8B8C,
            0x8001_8BA0,
            0x8001_CF64,
            0x800C_8C64,
            0x800C_9078,
            0x800C_9248,
            0x800C_9AAC,
            0x8026_1254,
            0x8026_1454,
            0x8026_1494,
            0x8026_1B40,
            0x8026_1B70,
            0x8026_C0A0,
            0x8026_C0A8,
            0x8026_C0E4,
            0x803A_3B6C,
            0x803A_35DC,
            0x803A_35E8,
            0x803A_A870,
            0x803D_3C64,
            0x8044_3904,
            0x8044_1400,
            0x8044_432C,
            0x8044_592C,
            0x8044_8710,
            0x8048_78BC,
            0x8048_8344,
            0x8049_45CC,
            0x8049_4B2C,
            0x8049_559C,
            0x8049_5C3C,
            0x8049_5C5C,
            0x8049_6A60,
            0x804A_2EF4,
            0x804A_2F20,
            0x804A_2F50,
            0x804A_2F80,
            0x804A_2FAC,
            0x804B_5F3C,
            0x804B_A6B0,
            0x804B_A7FC,
            0x804B_DEA8,
            0x8051_6E80,
            0x8051_E830,
            0x8051_EA04,
        ] {
            assert_eq!(native_pure_helper(address), None);
            assert_eq!(native_function_body(address), None);

            let marker = format!("    /* exact generated body {address:08X} */\n");
            let function = FunctionRange { address, size: 4 };
            let files =
                emit_sharded_module(&[(function, marker.clone())], &[], 32, function.address)
                    .expect("restartability-sensitive exact body emits");
            let source = files
                .iter()
                .find(|file| file.name == "functions_0000.cpp")
                .expect("function shard exists");
            assert!(source.contents.contains(&marker));
        }
    }

    #[test]
    fn preserves_exact_translated_body_for_aliases_of_native_replacements() {
        let function = FunctionRange {
            address: 0x8000_72B4,
            size: 0x1C,
        };
        let exact_body = concat!(
            "    if (context->pc != 0x800072B4u) [[unlikely]] {\n",
            "        switch (context->pc) {\n",
            "        case 0x800072C0u: goto label_800072C0;\n",
            "        default: galaxy::guest_execution_fault(services, context->pc, ",
            "\"invalid interior function entry\");\n",
            "        }\n",
            "    }\n",
            "label_800072C0:\n",
            "    /* exact alias body marker */\n",
        );

        let files = emit_sharded_module(
            &[(function, exact_body.to_owned())],
            &[(0x8000_72C0, function.address)],
            32,
            function.address,
        )
        .expect("native replacement with an interior alias emits");
        let source = files
            .iter()
            .find(|file| file.name == "functions_0000.cpp")
            .expect("function shard exists");

        assert!(source
            .contents
            .contains("if (context->pc == 0x800072B4u) {"));
        assert!(source
            .contents
            .contains("galaxy::native_read_next_char_utf16_800072B4"));
        assert!(source
            .contents
            .contains("case 0x800072C0u: goto label_800072C0;"));
        assert!(source.contents.contains("exact alias body marker"));
        let module = files
            .iter()
            .find(|file| file.name == "module.cpp")
            .expect("module lookup source exists");
        assert!(module
            .contents
            .contains("{0x800072C0u, &rmge01::fn_800072B4}"));
    }

    #[test]
    fn keeps_effect_commit_and_actor_list_exact_when_children_are_present() {
        let effect_commit = FunctionRange {
            address: 0x800C_8C64,
            size: 0xE8,
        };
        let partial = emit_sharded_module(
            &[(
                effect_commit,
                "    /* original effect body marker */\n".to_owned(),
            )],
            &[],
            32,
            effect_commit.address,
        )
        .expect("partial effect module emits");
        let partial_source = partial
            .iter()
            .find(|file| file.name == "functions_0000.cpp")
            .expect("partial effect shard exists");
        assert!(partial_source
            .contents
            .contains("original effect body marker"));

        let full = emit_sharded_module(
            &[
                (
                    effect_commit,
                    "    /* original effect body marker */\n".to_owned(),
                ),
                (
                    FunctionRange {
                        address: 0x800C_9C60,
                        size: 0xF4,
                    },
                    "    /* child sweep body marker */\n".to_owned(),
                ),
                (
                    FunctionRange {
                        address: 0x8044_8710,
                        size: 0x80,
                    },
                    "    /* child draw-list body marker */\n".to_owned(),
                ),
                (
                    FunctionRange {
                        address: 0x8044_8D54,
                        size: 0x40,
                    },
                    "    /* actor check 80448D54 marker */\n".to_owned(),
                ),
                (
                    FunctionRange {
                        address: 0x8044_8D94,
                        size: 0x98,
                    },
                    "    /* actor check 80448D94 marker */\n".to_owned(),
                ),
            ],
            &[],
            32,
            effect_commit.address,
        )
        .expect("full effect module emits");
        let full_source = full
            .iter()
            .find(|file| file.name == "functions_0000.cpp")
            .expect("full effect shard exists");
        assert!(!full_source
            .contents
            .contains("native_actor_list_apply_80448710"));
        assert!(full_source.contents.contains(
            "galaxy::native_jpa_actor_check_80448D54(context, memory, services, 0x80448D54u);"
        ));
        assert!(full_source.contents.contains(
            "galaxy::native_jpa_actor_check_80448D94(context, memory, services, 0x80448D94u);"
        ));
        assert!(full_source.contents.contains("original effect body marker"));
        assert!(full_source.contents.contains("child sweep body marker"));
        assert!(full_source.contents.contains("child draw-list body marker"));
        assert!(!full_source.contents.contains("actor check 80448D54 marker"));
        assert!(!full_source.contents.contains("actor check 80448D94 marker"));
    }

    #[test]
    fn lowers_an_interior_entry_behind_a_pc_dispatch_prologue() {
        let words = [0x3860_0001_u32, 0x3880_0002, 0x4E80_0020];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words_with_entries(0x8000_4000, &bytes, &BTreeSet::from([0x8000_4004]))
            .expect("interior entry is supported");
        assert!(output.starts_with("    if (context->pc != 0x80004000u) [[unlikely]] {\n"));
        assert!(output.contains("    switch (context->pc) {\n"));
        assert!(output.contains("case 0x80004004u: goto label_80004004;"));
        assert!(output.contains(
            "default: galaxy::guest_execution_fault(services, context->pc, \"invalid interior function entry\");"
        ));
        assert!(output.contains("context->gpr[3] = 0x00000001u;"));
        assert!(output.contains("label_80004004:\n"));
        assert!(output.contains("context->gpr[4] = 0x00000002u;"));
    }

    #[test]
    fn external_call_return_alias_replays_the_missing_checkpoint() {
        let words = [
            0x4800_1001_u32, // bl 0x80005000
            0x3863_0001,     // architectural return continuation
            0x4E80_0020,     // blr
        ];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let entries = BTreeSet::from([0x8000_4004]);
        let call_return_entries = BTreeSet::from([0x8000_4004]);
        let output = lower_words_for_module_with_call_return_entries(
            0x8000_4000,
            &bytes,
            &entries,
            &call_return_entries,
            &BTreeMap::new(),
        )
        .expect("external call-return continuation is supported");

        assert!(output.contains("case 0x80004004u: goto call_return_80004004;"));
        assert!(!output.contains("case 0x80004004u: goto label_80004004;"));
        assert!(output.contains(
            "call_return_80004004:\n    galaxy::call_return_checkpoint(services, 0x80004000u, 0x80004004u, context, memory);\n    goto label_80004004;"
        ));
    }

    #[test]
    fn finds_cross_function_direct_branches_to_interior_addresses() {
        let source = FunctionRange {
            address: 0x8000_4000,
            size: 8,
        };
        let destination = FunctionRange {
            address: 0x8000_5000,
            size: 12,
        };
        let words = [0x4800_1005_u32, 0x4E80_0020];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let targets = direct_branch_targets(source.address, &bytes);
        assert_eq!(targets, BTreeSet::from([0x8000_5004]));
        assert_eq!(
            mapped_function_containing(&[source, destination], 0x8000_5004),
            Some(destination)
        );
    }

    #[test]
    fn exposes_backward_branch_targets_as_interrupt_resume_entries() {
        let bytes = [
            0x38, 0x63, 0x00, 0x01, // addi r3, r3, 1
            0x38, 0x84, 0x00, 0x01, // addi r4, r4, 1
            0x42, 0x00, 0xFF, 0xFC, // bdnz 0x80004004
            0x4E, 0x80, 0x00, 0x20, // blr
        ];
        assert_eq!(
            backward_branch_resume_entries(0x8000_4000, &bytes),
            BTreeSet::from([0x8000_4004])
        );
    }

    #[test]
    fn exposes_only_architectural_interrupt_continuations_as_aliases() {
        let mtmsr = (31_u32 << 26) | (4 << 21) | (146 << 1);
        let mtspr_dec = (31_u32 << 26) | (7 << 21) | (22 << 16) | (467 << 1);
        let mtspr_lr = (31_u32 << 26) | (8 << 21) | (8 << 16) | (467 << 1);
        let words = [mtmsr, 0x3863_0001, mtspr_dec, mtspr_lr, 0x4E80_0020];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let sections = [DolSection {
            name: "text0".to_owned(),
            kind: DolSectionKind::Text,
            index: 0,
            file_offset: 0,
            address: 0x8000_4000,
            size: bytes.len() as u32,
        }];
        let functions = [FunctionRange {
            address: 0x8000_4000,
            size: bytes.len() as u32,
        }];
        let expected_entries = BTreeSet::from([0x8000_4004, 0x8000_400C]);

        assert_eq!(
            architectural_interrupt_resume_entries(0x8000_4000, &bytes),
            expected_entries
        );
        assert_eq!(
            discover_alias_entries(&sections, &bytes, &functions),
            vec![(0x8000_4004, 0x8000_4000), (0x8000_400C, 0x8000_4000)]
        );

        let output = lower_words_with_entries(0x8000_4000, &bytes, &expected_entries)
            .expect("architectural interrupt continuations lower as exact aliases");
        assert!(output.contains("case 0x80004004u: goto label_80004004;"));
        assert!(output.contains("case 0x8000400Cu: goto label_8000400C;"));
        assert!(output.contains("label_80004004:"));
        assert!(output.contains("label_8000400C:"));
        assert!(!output.contains("case 0x80004010u:"));

        assert!(
            architectural_interrupt_resume_entries(0x8000_5000, &mtmsr.to_be_bytes()).is_empty()
        );
    }

    #[test]
    fn lowers_taken_backward_branches_with_exact_resume_targets() {
        let conditional_words = [0x38A5_0001_u32, 0x4200_FFFC, 0x4E80_0020];
        let conditional_bytes: Vec<u8> = conditional_words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        let conditional = lower_words(0x8000_4000, &conditional_bytes)
            .expect("backward conditional branch is supported");
        assert!(conditional.contains(
            "galaxy::branch_checkpoint_taken(services, 0x80004004u, 0x80004000u, context, memory); goto label_80004000;"
        ));

        let unconditional_words = [0x38A5_0001_u32, 0x4BFF_FFFC];
        let unconditional_bytes: Vec<u8> = unconditional_words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        let unconditional = lower_words(0x8000_5000, &unconditional_bytes)
            .expect("backward unconditional branch is supported");
        assert!(unconditional.contains(
            "galaxy::branch_checkpoint_taken(services, 0x80005004u, 0x80005000u, context, memory);"
        ));
    }

    #[test]
    fn finds_continuations_after_calls_to_rfi_trampolines() {
        let words = [0x4800_1001_u32, 0x3860_0001, 0x4E80_0020];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        assert_eq!(
            direct_link_calls(0x8000_4000, &bytes),
            vec![(0x8000_5000, 0x8000_4004)]
        );
        assert!(function_contains_rfi(&0x4C00_0064_u32.to_be_bytes()));
    }

    #[test]
    fn every_link_call_form_emits_its_exact_lr_continuation_alias() {
        let words = [
            0x4800_1001_u32, // bl 0x80005000
            0x4E80_0421,     // bctrl
            0x4E80_0021,     // blrl
            0x4E80_0020,     // blr
        ];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let sections = [DolSection {
            name: "text0".to_owned(),
            kind: DolSectionKind::Text,
            index: 0,
            file_offset: 0,
            address: 0x8000_4000,
            size: bytes.len() as u32,
        }];
        let functions = [FunctionRange {
            address: 0x8000_4000,
            size: bytes.len() as u32,
        }];
        let expected = BTreeSet::from([0x8000_4004, 0x8000_4008, 0x8000_400C]);

        assert_eq!(
            call_return_continuations(&sections, &bytes, &functions),
            expected
        );
        let aliases = discover_alias_entries(&sections, &bytes, &functions)
            .into_iter()
            .collect::<BTreeMap<_, _>>();
        for continuation in expected {
            assert_eq!(aliases.get(&continuation), Some(&0x8000_4000));
        }
    }

    #[test]
    fn finds_continuations_after_context_save_calls() {
        let words = [0x4800_1001_u32, 0x3860_0001, 0x4E80_0020];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let save_routines = BTreeSet::from([0x8000_5000]);
        let continuations = direct_link_calls(0x8000_4000, &bytes)
            .into_iter()
            .filter_map(|(target, return_address)| {
                save_routines.contains(&target).then_some(return_address)
            })
            .collect::<BTreeSet<_>>();
        assert_eq!(continuations, BTreeSet::from([0x8000_4004]));
    }

    #[test]
    fn finds_interior_function_entries_stored_in_initialized_data() {
        let functions = [
            FunctionRange {
                address: 0x8000_4000,
                size: 8,
            },
            FunctionRange {
                address: 0x8000_5000,
                size: 12,
            },
        ];
        let words = [0x8000_4000_u32, 0x8000_5004, 0x8000_5005, 0xDEAD_BEEF];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        assert_eq!(
            interior_code_pointers(&bytes, &functions),
            BTreeSet::from([0x8000_5004])
        );
    }

    #[test]
    fn lowers_internal_link_and_local_lr_return_dispatch() {
        let words = [0x4800_0009_u32, 0x4E80_0020, 0x3863_0001, 0x4E80_0020];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words(0x8000_4000, &bytes).expect("internal linked branch is supported");
        assert!(output.contains("context->lr = 0x80004004u;"));
        assert!(output.contains("goto label_80004008;"));
        assert!(output.contains("label_80004004:"));
        assert!(output.contains(
            "galaxy::call_return_checkpoint(services, 0x80004000u, 0x80004004u, context, memory);"
        ));
        assert!(output.contains("case 0x80004004u: goto call_return_80004004;"));
        assert!(output.contains(
            "call_return_80004004:\n    galaxy::call_return_checkpoint(services, 0x80004000u, 0x80004004u, context, memory);\n    goto label_80004004;"
        ));
        assert!(!output.contains("B(internal_link)"));
    }

    #[test]
    fn multiple_internal_returns_share_one_complete_lr_dispatch() {
        let words = [0x4800_0009_u32, 0x4E80_0020, 0x3863_0001, 0x4E80_0020];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words(0x8000_4000, &bytes).expect("internal return fixture lowers");
        assert_eq!(output.matches("goto lr_continuation_dispatch;").count(), 2);
        assert_eq!(output.matches("lr_continuation_dispatch:\n").count(), 1);
        assert_eq!(
            output.matches("switch (context->lr & 0xFFFFFFFCu)").count(),
            1
        );
        assert_eq!(
            output
                .matches("case 0x80004004u: goto call_return_80004004;")
                .count(),
            1
        );
        assert!(
            output.find("lr_continuation_dispatch:").unwrap()
                < output.find("call_return_80004004:\n").unwrap()
        );
    }

    #[test]
    fn lowers_declared_indirect_call_return_as_a_native_resume_boundary() {
        // bctrl; addi r3,r3,1; blr. The callee can unwind the native caller
        // through OSLoadContext/RFI, so the continuation at +4 must perform
        // the same checkpoint before resuming its precompiled label.
        let words = [0x4E80_0421_u32, 0x3863_0001, 0x4E80_0020];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let entries = BTreeSet::from([0x8000_4004]);
        let output = lower_ppc_code_range_with_entries_call_returns_and_immediate_overrides(
            0x8000_4000,
            &bytes,
            &entries,
            &entries,
            &BTreeMap::new(),
        )
        .expect("declared bctrl return continuation lowers");

        assert!(output.contains("case 0x80004004u: goto call_return_80004004;"));
        assert!(output.contains(
            "call_return_80004004:\n    galaxy::call_return_checkpoint(services, 0x80004000u, 0x80004004u, context, memory);\n    goto label_80004004;"
        ));
        assert!(matches!(
            lower_ppc_code_range_with_entries_call_returns_and_immediate_overrides(
                0x8000_4000,
                &bytes,
                &BTreeSet::new(),
                &entries,
                &BTreeMap::new(),
            ),
            Err(TranslationError::CallReturnEntryNotDeclared {
                address: 0x8000_4004
            })
        ));
    }

    #[test]
    fn internal_call_return_checkpoint_bypasses_alternate_and_resume_entries() {
        let words = [0x4182_0008_u32, 0x4800_0009, 0x4E80_0020, 0x4E80_0020];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words_with_entries(0x8000_4000, &bytes, &BTreeSet::from([0x8000_4008]))
            .expect("internal linked call with an alternate continuation path is supported");

        assert!(output.contains("case 0x80004008u: goto label_80004008;"));
        // The untested CTR/CR operand is no longer materialised, so the guard is
        // a bare CR test rather than `(true) && (...)`.
        assert!(
            output.contains("if (galaxy::cr_bit(context, 2u)) goto label_80004008;"),
            "{output}"
        );
        assert!(output.contains("case 0x80004008u: goto call_return_80004008;"));
        assert!(output.contains("label_80004008:\n    {"));
        // The two BLRs share one LR dispatch; alternate branch/public entries
        // still bypass call-return checkpoints while linked returns use them.
        assert_eq!(output.matches("goto lr_continuation_dispatch;").count(), 2);
        assert!(output.contains(
            "    return;\nlr_continuation_dispatch:\n    switch (context->lr & 0xFFFFFFFCu) {\n    case 0x80004008u: goto call_return_80004008;\n    default: return;\n    }\ncall_return_80004008:\n    galaxy::call_return_checkpoint(services, 0x80004004u, 0x80004008u, context, memory);\n    goto label_80004008;"
        ));
        assert_eq!(output.matches("call_return_checkpoint").count(), 1);
    }

    #[test]
    fn lowers_compare_and_conditional_branch() {
        let words = [0x2C03_0000_u32, 0x4182_0008, 0x3860_0001, 0x4E80_0020];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words(0x8000_4000, &bytes).expect("compare and branch supported");
        assert!(output.contains("galaxy::compare_signed(context, 0u"));
        assert!(output.contains("galaxy::cr_bit(context, 2u)"));
        assert!(output.contains("goto label_8000400C;"));
    }

    #[test]
    fn arithmetic_reads_r0_while_literal_base_encodings_keep_zero() {
        // ISA register values differ from the literal-zero RA encoding used
        // by addi/addis and address generation. Seed r0 with 7 so replacing a
        // value operand with zero changes the program: add r3,r0,r4 adds 7.
        let xform = |rt: u32, ra: u32, rb: u32, xo: u32| {
            (31_u32 << 26) | (rt << 21) | (ra << 16) | (rb << 11) | (xo << 1)
        };
        let dform = |op: u32, rt: u32, imm: u16| {
            (op << 26) | (rt << 21) | u32::from(imm)
        };
        for (word, required) in [
            (xform(3, 0, 4, 266), "context->gpr[3] = context->gpr[0] + context->gpr[4];"),
            (xform(3, 4, 0, 266), "context->gpr[3] = context->gpr[4] + context->gpr[0];"),
            (xform(0, 0, 4, 266), "context->gpr[0] = context->gpr[0] + context->gpr[4];"),
            (xform(3, 0, 4, 10), "const std::uint64_t result = static_cast<std::uint64_t>(context->gpr[0]) + context->gpr[4];"),
            (xform(3, 4, 0, 10), "const std::uint64_t result = static_cast<std::uint64_t>(context->gpr[4]) + context->gpr[0];"),
            (xform(3, 0, 4, 40), "context->gpr[3] = context->gpr[4] - context->gpr[0];"),
            (xform(3, 4, 0, 40), "context->gpr[3] = context->gpr[0] - context->gpr[4];"),
            (xform(3, 0, 4, 8), "const std::uint32_t left_value = context->gpr[0];"),
            (xform(0, 4, 0, 8), "const std::uint32_t right_value = context->gpr[0];"),
            (xform(3, 0, 4, 235), "const std::int64_t result = static_cast<std::int64_t>(static_cast<std::int32_t>(context->gpr[0])) * static_cast<std::int32_t>(context->gpr[4]);"),
            (xform(3, 4, 0, 235), "const std::int64_t result = static_cast<std::int64_t>(static_cast<std::int32_t>(context->gpr[4])) * static_cast<std::int32_t>(context->gpr[0]);"),
            (xform(0, 0, 0, 104), "context->gpr[0] = 0u - context->gpr[0];"),
            (dform(12, 3, 7), "const std::uint64_t result = static_cast<std::uint64_t>(context->gpr[0]) + 0x00000007u;"),
            (dform(13, 0, 7), "const std::uint64_t result = static_cast<std::uint64_t>(context->gpr[0]) + 0x00000007u;"),
            (dform(8, 0, 0xfffb), "const std::uint32_t source_value = context->gpr[0];"),
            (dform(7, 3, 0xfff9), "const std::int64_t result = static_cast<std::int64_t>(static_cast<std::int32_t>(context->gpr[0])) * static_cast<std::int32_t>(0xFFFFFFF9u);"),
        ] {
            let output = lower_instruction_fixture(
                0x8000_4000,
                &[0x3800_0007, word, 0x4e80_0020], // li r0,7; instruction; blr
            );
            assert!(output.contains("context->gpr[0] = 0x00000007u;"), "{output}");
            assert!(output.contains(required), "word={word:08x}: {output}");
        }

        // These RA=0 exceptions still ignore the seeded r0; preserve their
        // established lowering while repairing arithmetic value operands.
        let output = lower_instruction_fixture(
            0x8000_4000,
            &[0x3800_0007, dform(14, 3, 9), dform(15, 4, 0x1234), 0x4e80_0020],
        );
        assert!(output.contains("context->gpr[3] = 0x00000009u;"), "{output}");
        assert!(output.contains("context->gpr[4] = 0x12340000u;"), "{output}");
    }

    #[test]
    fn lowers_integer_carry_shift_and_insert_operations() {
        let andi = (28_u32 << 26) | (3 << 21) | (4 << 16) | 0x00FF;
        let addc = (31_u32 << 26) | (5 << 21) | (3 << 16) | (4 << 11) | (10 << 1);
        let srawi = (31_u32 << 26) | (5 << 21) | (6 << 16) | (1 << 11) | (824 << 1);
        let rlwimi = (20_u32 << 26) | (6 << 21) | (7 << 16) | (8 << 11) | (7 << 1);
        let words = [andi, addc, srawi, rlwimi, 0x4E80_0020];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words(0x8000_4000, &bytes).expect("integer family supported");
        assert!(output.contains("context->gpr[4] = context->gpr[3] & 0x000000FFu;"));
        assert!(output.contains("galaxy::set_xer_ca(context, result > 0xFFFFFFFFull);"));
        assert!(output.contains("galaxy::arithmetic_shift_right(source, 1u);"));
        // `rlwimi r7,r6,SH=8,MB=0,ME=7` is `rotl(rA, 8) & 0xFF000000` per the ISA:
        // the field mask is rotate_mask(0, 7) == 0x000000FF and the ISA rotates the
        // source by SH=8 before applying it. The rotate amount really is SH, and
        // the cleared half of the insert really is 0x00FFFFFF.
        //
        // An earlier revision of this assertion expected the *pre-rotated-mask*
        // form `rotl(gpr[6], 24) & 0x00FF0000u`. That is an equivalent-looking
        // rewrite which is wrong for shift != 0; the proof is recorded on the
        // Rlwinm/Rlwimi emitter (exhaustively checked over all 32x32x32 SH/MB/ME
        // triples, where the pre-rotated form mismatched 319,448 of 393,216
        // encodings and this form matched all of them). Do not restore it, and do
        // not "optimize" the rotate amount back to `32 - SH`.
        assert!(
            output.contains("std::rotl(context->gpr[6], 8) & 0xFF000000u"),
            "{output}"
        );
        assert!(output.contains("context->gpr[7] & 0x00FFFFFFu"));
    }

    #[test]
    fn preserves_carry_inputs_when_subtract_targets_alias_sources() {
        let subfc = (31_u32 << 26) | (4 << 21) | (27 << 16) | (4 << 11) | (8 << 1);
        let subfic = (8_u32 << 26) | (4 << 21) | (4 << 16) | 1;
        let words = [subfc, subfic, 0x4E80_0020];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words(0x8000_4000, &bytes).expect("carry-producing subtracts supported");
        assert!(output.contains("const std::uint32_t left_value = context->gpr[27];"));
        assert!(output.contains("const std::uint32_t right_value = context->gpr[4];"));
        assert!(output.contains("context->gpr[4] = right_value - left_value;"));
        assert!(output.contains("galaxy::set_xer_ca(context, right_value >= left_value);"));
        assert!(output.contains("const std::uint32_t source_value = context->gpr[4];"));
        assert!(output.contains("context->gpr[4] = 0x00000001u - source_value;"));
    }

    #[test]
    fn lowers_floating_memory_operations() {
        let lfs = (48_u32 << 26) | (1 << 21) | (3 << 16);
        let stfd = (54_u32 << 26) | (1 << 21) | (4 << 16) | 8;
        let words = [lfs, stfd, 0x4E80_0020];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words(0x8000_4000, &bytes).expect("floating memory supported");
        assert!(output.contains("galaxy::load_fpr_single(context, 1u"));
        assert!(output.contains("galaxy::guest_store_u64"));
    }

    #[test]
    fn coalesces_fpu_guards_across_straight_line_integer_and_fpu_instructions() {
        let words = [
            0x3860_0001_u32, // addi r3, r0, 1
            0xEC21_182A,     // fadds f1, f1, f3
            0x3884_0004,     // addi r4, r4, 4
            0xE025_00AC,     // psq_l f1, 172(r5), 0, 0
            0x4E80_0020,     // blr
        ];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words(0x8000_4000, &bytes).expect("mixed integer/FP body supported");

        assert_eq!(output.matches("galaxy::ensure_fpu_available(").count(), 1);
        assert!(output
            .contains("galaxy::ensure_fpu_available(services, 0x80004004u, context, memory);"));
        assert!(!output
            .contains("galaxy::ensure_fpu_available(services, 0x8000400Cu, context, memory);"));
        assert!(!output
            .contains("galaxy::ensure_fpu_available(services, 0x80004000u, context, memory);"));
    }

    #[test]
    fn fpu_guard_restarts_after_mtmsr_boundary() {
        let words = [
            0xEC21_182A_u32, // fadds f1, f1, f3
            0x7C60_0124,     // mtmsr r3
            0xE025_00AC,     // psq_l f1, 172(r5), 0, 0
            0x4E80_0020,     // blr
        ];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words(0x8000_4000, &bytes)
            .expect("mtmsr boundary with exact FP guards is supported");

        assert_eq!(output.matches("galaxy::ensure_fpu_available(").count(), 2);
        assert!(output
            .contains("galaxy::ensure_fpu_available(services, 0x80004000u, context, memory);"));
        assert!(output
            .contains("galaxy::ensure_fpu_available(services, 0x80004008u, context, memory);"));
    }

    #[test]
    fn interior_fpu_retry_requires_exact_static_continuation_aliases() {
        let words = [
            0x3860_0001_u32, // addi r3, r0, 1
            0xEC21_182A,     // fadds f1, f1, f3
            0xE025_00AC,     // psq_l f1, 172(r5), 0, 0
            0x4E80_0020,     // blr
        ];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let sections = [DolSection {
            name: "text0".to_owned(),
            kind: DolSectionKind::Text,
            index: 0,
            file_offset: 0,
            address: 0x8000_4000,
            size: bytes.len() as u32,
        }];
        let functions = [FunctionRange {
            address: 0x8000_4000,
            size: bytes.len() as u32,
        }];

        let aliases = discover_alias_entries(&sections, &bytes, &functions);
        assert_eq!(
            aliases,
            vec![(0x8000_4004, 0x8000_4000), (0x8000_4008, 0x8000_4000)]
        );
        let entries = aliases
            .iter()
            .map(|(entry, _)| *entry)
            .collect::<BTreeSet<_>>();

        let output = lower_words_with_entries(0x8000_4000, &bytes, &entries)
            .expect("interior FP retry aliases lower into the owning static body");
        assert!(output
            .contains("galaxy::ensure_fpu_available(services, 0x80004004u, context, memory);"));
        assert!(output
            .contains("galaxy::ensure_fpu_available(services, 0x80004008u, context, memory);"));
        assert!(output.contains("case 0x80004004u: goto fpu_retry_80004004;"));
        assert!(output.contains("case 0x80004008u: goto fpu_retry_80004008;"));
        assert!(output.contains("fpu_retry_80004004:"));
        assert!(output.contains("fpu_retry_80004008:"));
        // The entry at 0x80004004 needs a normal-path guard plus its exact
        // restart stub. The following alias restarts through its own stub,
        // while the ordinary fallthrough keeps the established MSR[FP] proof.
        assert_eq!(output.matches("galaxy::ensure_fpu_available(").count(), 3);
    }

    #[test]
    fn lowers_paired_single_quantized_memory_operations() {
        let words = [
            0xE025_00AC_u32,
            0xE543_5010,
            0xF123_8008,
            0x13E1_000E,
            0x4E80_0020,
        ];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words(0x8000_4000, &bytes).expect("paired-single memory is supported");
        assert!(
            output.contains("galaxy::psq_load(context, 1u, memory, ea_80004000, 0u, false, true")
        );
        assert!(
            output.contains("galaxy::psq_load(context, 10u, memory, ea_80004004, 5u, false, true")
        );
        assert!(output.contains("context->gpr[3] = ea_80004004;"));
        assert!(
            output.contains("galaxy::psq_store(context, 9u, memory, ea_80004008, 0u, true, true")
        );
        assert!(output
            .contains("galaxy::psq_store(context, 31u, memory, ea_8000400C, 0u, false, false"));
    }

    #[test]
    fn lowers_paired_single_bitwise_moves_and_merges() {
        let words = [
            0x1020_0090_u32,
            0x10E0_3850,
            0x1040_0420,
            0x1040_0C60,
            0x1040_04A0,
            0x10AA_14E0,
            0x4E80_0020,
        ];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words(0x8000_4000, &bytes).expect("paired-single moves are supported");
        assert!(output.contains("context->ps1_bits[1] = result_ps1;"));
        assert!(output.contains("context->ps1_bits[7] ^ 0x8000000000000000ull"));
        assert!(output.contains("context->fpr_bits[0]"));
        assert!(output.contains("context->ps1_bits[1]"));
        assert!(output.contains("require_paired_single_mode"));
    }

    #[test]
    fn lowers_scalar_and_paired_floating_arithmetic() {
        let words = [
            0xEC21_182A_u32,
            0xFC21_1828,
            0x1006_382A,
            0x1000_0032,
            0x1000_02D8,
            0x4E80_0020,
        ];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words(0x8000_4000, &bytes).expect("floating arithmetic is supported");
        assert!(output.contains("PpcFloatBinaryOperation::Add"));
        assert!(output.contains("PpcFloatBinaryOperation::Subtract"));
        assert!(output.contains("PpcFloatBinaryOperation::Multiply"));
        assert!(output.contains("ppc_commit_scalar_result"));
        assert!(output.contains("ppc_commit_paired_result"));
        assert!(output.contains("ppc_f64_binary_to_f32"));
    }

    #[test]
    fn lowers_floating_round_and_integer_conversion() {
        let words = [0xFC20_1018_u32, 0xFC60_181E, 0x4E80_0020];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words(0x8000_4000, &bytes).expect("floating conversions are supported");
        assert!(output.contains("ppc_round_f64_to_f32(context->fpr_bits[2]"));
        assert!(output.contains("result, false, false, services"));
        assert!(output.contains("ppc_f64_to_i32_round_zero(context->fpr_bits[3]"));
    }

    #[test]
    fn inline_scalar_single_binary_preserves_fallback_and_public_entries() {
        assert!(!ModuleTranslationOptions::default().inline_scalar_single_binary);
        let fadds = (59_u32 << 26) | (1 << 21) | (1 << 16) | (2 << 11) | (21 << 1) | 1;
        let fsubs = (59_u32 << 26) | (2 << 21) | (1 << 16) | (2 << 11) | (20 << 1);
        let fmuls = (59_u32 << 26) | (3 << 21) | (1 << 16) | (2 << 6) | (25 << 1);
        let fdivs = (59_u32 << 26) | (3 << 21) | (1 << 16) | (2 << 11) | (18 << 1);
        let fmadds = (59_u32 << 26) | (3 << 21) | (1 << 16) | (2 << 11) | (2 << 6) | (29 << 1);
        let fadd = (63_u32 << 26) | (3 << 21) | (1 << 16) | (2 << 11) | (21 << 1);
        let ps_add = (4_u32 << 26) | (3 << 21) | (1 << 16) | (2 << 11) | (21 << 1);
        let words = [
            fadds,
            fsubs,
            fmuls,
            fdivs,
            fmadds,
            fadd,
            ps_add,
            0x4E80_0020,
        ];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let entries = BTreeSet::from([0x8000_4004, 0x8000_4008]);
        let original = lower_words_with_entries(0x8000_4000, &bytes, &entries).unwrap();
        assert!(!original.contains("try_commit_widened_scalar_binary"));
        let optimized = lower_words_with_config(
            0x8000_4000,
            &bytes,
            &entries,
            &BTreeSet::new(),
            LoweringConfig {
                inline_scalar_single_binary: true,
                ..LoweringConfig::default()
            },
        )
        .unwrap();
        assert_eq!(
            optimized
                .matches("try_commit_widened_scalar_binary")
                .count(),
            3
        );
        assert_eq!(optimized.matches("ppc_f64_binary_to_f32(").count(), 4);
        assert_eq!(optimized.matches("ppc_f32_ternary(").count(), 1);
        assert_eq!(optimized.matches("ppc_f64_binary(").count(), 1);
        assert_eq!(optimized.matches("ppc_commit_paired_result(").count(), 1);
        for pc in ["80004004", "80004008"] {
            assert!(optimized.contains(&format!("case 0x{pc}u: goto fpu_retry_{pc};")));
            assert!(optimized.contains(&format!("fpu_retry_{pc}:")));
            assert!(optimized.contains(&format!("ensure_fpu_available(services, 0x{pc}u")));
        }
        let capture = optimized.find("const std::uint64_t right =").unwrap();
        let call = optimized.find("try_commit_widened_scalar_binary").unwrap();
        assert!(capture < call);
        assert!(optimized.contains("context, 1u, left, right, true, services, 0x80004000u"));
        assert!(optimized.contains("context, 2u, left, right, false, services, 0x80004004u"));
        assert!(optimized.contains("PpcFloatBinaryOperation::Multiply, context->fpr_bits[1], context->fpr_bits[2], context->fpscr"));
    }

    #[test]
    fn fused_paired_binary_preserves_operand_capture_and_public_entries() {
        let words = [0x1006_382B_u32, 0x1000_0032, 0x4E80_0020];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let entries = BTreeSet::from([0x8000_4004]);
        let original = lower_words_with_entries(0x8000_4000, &bytes, &entries).unwrap();
        assert!(!original.contains("ppc_commit_paired_binary_result"));
        let fused = lower_words_with_config(
            0x8000_4000,
            &bytes,
            &entries,
            &BTreeSet::new(),
            LoweringConfig {
                fused_paired_binary: true,
                ..LoweringConfig::default()
            },
        )
        .unwrap();
        assert!(fused.contains("case 0x80004004u: goto fpu_retry_80004004;"));
        assert!(fused.contains("fpu_retry_80004004:"));
        assert!(fused.contains("galaxy::ensure_fpu_available(services, 0x80004004u"));
        assert_eq!(fused.matches("require_single_precision_bits").count(), 8);
        assert_eq!(fused.matches("require_paired_single_mode").count(), 2);
        assert_eq!(fused.matches("ppc_commit_paired_binary_result").count(), 2);
        assert!(!fused.contains("ppc_f32_binary("));
        let capture = fused.find("const std::uint32_t right_ps1").unwrap();
        let call = fused.find("ppc_commit_paired_binary_result").unwrap();
        assert!(capture < call);
        assert!(fused.contains("right_ps0, right_ps1, true, services, 0x80004000u"));
        assert!(fused.contains("context->fpr_bits[0]"));
        assert!(fused.contains("context->ps1_bits[0]"));
    }

    #[test]
    fn lowers_fused_select_and_paired_sum_operations() {
        let fmadds = (59_u32 << 26) | (1 << 21) | (2 << 16) | (4 << 11) | (3 << 6) | (29 << 1);
        let fsel = (63_u32 << 26) | (5 << 21) | (6 << 16) | (8 << 11) | (7 << 6) | (23 << 1);
        let ps_madd = (4_u32 << 26) | (9 << 21) | (10 << 16) | (12 << 11) | (11 << 6) | (29 << 1);
        let ps_sum1 = (4_u32 << 26) | (13 << 21) | (14 << 16) | (16 << 11) | (15 << 6) | (11 << 1);
        let words = [fmadds, fsel, ps_madd, ps_sum1, 0x4E80_0020];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output =
            lower_words(0x8000_4000, &bytes).expect("fused and selection operations supported");
        assert!(output.contains("PpcFloatTernaryOperation::MultiplyAdd"));
        assert!(output.contains("select_nonnegative"));
        assert!(output.contains("multiplier_ps1"));
        assert!(output.contains("ppc_commit_paired_result(context, 13u"));
        assert!(output.contains("true, false, services"));
    }

    #[test]
    fn lowers_paired_compare_and_fpscr_operations() {
        let ps_cmpo0 = (4_u32 << 26) | (2 << 23) | (3 << 16) | (4 << 11) | (32 << 1);
        let mffs = (63_u32 << 26) | (5 << 21) | (583 << 1);
        let mtfsb1 = (63_u32 << 26) | (3 << 21) | (38 << 1);
        let mtfsf = (63_u32 << 26) | (0xA5 << 17) | (6 << 11) | (711 << 1);
        let words = [ps_cmpo0, mffs, mtfsb1, mtfsf, 0x4E80_0020];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words(0x8000_4000, &bytes).expect("FPSCR operations are supported");
        assert!(output.contains("require_paired_single_mode"));
        assert!(output.contains("compare_f64(context, 2u"));
        assert!(output.contains("0xFFF8000000000000ull | context->fpscr"));
        assert!(output.contains("set_fpscr_bit(context, 3u)"));
        assert!(output.contains("write_fpscr_fields"));
        assert!(output.contains("0xA5u"));
    }

    #[test]
    fn lowers_scalar_and_paired_estimate_operations() {
        let fres = (59_u32 << 26) | (1 << 21) | (2 << 11) | (24 << 1);
        let frsqrte = (63_u32 << 26) | (3 << 21) | (4 << 11) | (26 << 1);
        let ps_res = (4_u32 << 26) | (5 << 21) | (6 << 11) | (24 << 1);
        let ps_rsqrte = (4_u32 << 26) | (7 << 21) | (8 << 11) | (26 << 1);
        let ps_sel = (4_u32 << 26) | (9 << 21) | (10 << 16) | (12 << 11) | (11 << 6) | (23 << 1);
        let words = [fres, frsqrte, ps_res, ps_rsqrte, ps_sel, 0x4E80_0020];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words(0x8000_4000, &bytes).expect("estimate instructions are supported");
        assert!(output.contains("ppc_f32_reciprocal_estimate"));
        assert!(output.contains("ppc_f64_reciprocal_estimate"));
        assert!(output.contains("ppc_f64_reciprocal_sqrt_estimate"));
        assert!(output.contains("ppc_f32_reciprocal_sqrt_estimate"));
        assert!(output.contains("select_nonnegative_ps0"));
    }

    #[test]
    fn scalar_fres_captures_double_operand_and_preserves_alias_commit() {
        // Record-form fres f2,f2: source capture must precede the aliased write.
        let fres = (59_u32 << 26) | (2 << 21) | (2 << 11) | (24 << 1) | 1;
        let words = [fres, 0x4E80_0020];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words(0x8000_4000, &bytes).expect("scalar fres accepts double input");
        let capture = output
            .find("const std::uint64_t source = context->fpr_bits[2]")
            .unwrap();
        let estimate = output
            .find("ppc_f64_reciprocal_estimate(source, context->fpscr)")
            .unwrap();
        let commit = output
            .find("ppc_commit_scalar_result(context, 2u, result, true, true")
            .unwrap();
        assert!(capture < estimate && estimate < commit);
        assert!(!output.contains("require_single_precision_bits"));
    }

    #[test]
    fn extracts_signed_paired_single_displacements() {
        assert_eq!(paired_single_displacement(0x0000_07FF), 2047);
        assert_eq!(paired_single_displacement(0x0000_0800), -2048);
        assert_eq!(paired_single_displacement(0xFFFF_FFFF), -1);
    }

    #[test]
    fn lowers_gqr_and_hid2_special_register_access() {
        let mfspr_gqr5 =
            (31_u32 << 26) | (3 << 21) | ((917 & 0x1F) << 16) | ((917 & 0x3E0) << 6) | (339 << 1);
        let mtspr_gqr5 =
            (31_u32 << 26) | (4 << 21) | ((917 & 0x1F) << 16) | ((917 & 0x3E0) << 6) | (467 << 1);
        let mfspr_hid2 =
            (31_u32 << 26) | (5 << 21) | ((920 & 0x1F) << 16) | ((920 & 0x3E0) << 6) | (339 << 1);
        let mtspr_dma_u =
            (31_u32 << 26) | (6 << 21) | ((922 & 0x1F) << 16) | ((922 & 0x3E0) << 6) | (467 << 1);
        let mtspr_dma_l =
            (31_u32 << 26) | (7 << 21) | ((923 & 0x1F) << 16) | ((923 & 0x3E0) << 6) | (467 << 1);
        let words = [
            mfspr_gqr5,
            mtspr_gqr5,
            mfspr_hid2,
            mtspr_dma_u,
            mtspr_dma_l,
            0x4E80_0020,
        ];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words(0x8000_4000, &bytes).expect("GQR and HID2 are supported");
        assert!(output.contains("context->gpr[3] = context->gqr[5];"));
        assert!(output.contains("context->gqr[5] = context->gpr[4];"));
        assert!(output.contains("context->gpr[5] = context->hid2;"));
        assert!(
            output.contains("galaxy::write_spr(context, 922u, context->gpr[6], memory, services")
        );
        assert!(
            output.contains("galaxy::write_spr(context, 923u, context->gpr[7], memory, services")
        );
    }

    #[test]
    fn lowers_machine_state_time_base_and_exceptions() {
        let mfmsr = (31_u32 << 26) | (3 << 21) | (83 << 1);
        let mtmsr = (31_u32 << 26) | (4 << 21) | (146 << 1);
        let mfsr = (31_u32 << 26) | (5 << 21) | (7 << 16) | (595 << 1);
        let mtsr = (31_u32 << 26) | (5 << 21) | (8 << 16) | (210 << 1);
        let mftb =
            (31_u32 << 26) | (6 << 21) | ((268 & 0x1F) << 16) | ((268 & 0x3E0) << 6) | (371 << 1);
        let mtspr_dec = (31_u32 << 26) | (7 << 21) | (22 << 16) | (467 << 1);
        let twi = (3_u32 << 26) | (4 << 21) | (7 << 16) | 0xFFFF;
        let words = [
            mfmsr,
            mtmsr,
            mfsr,
            mtsr,
            mftb,
            mtspr_dec,
            0x4400_0002,
            twi,
            0x4E80_0020,
        ];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output =
            lower_words(0x8000_4000, &bytes).expect("machine state instructions are supported");
        assert!(output.contains("context->gpr[3] = context->msr;"));
        assert!(output.contains("context->msr = context->gpr[4];"));
        assert!(output.contains(
            "galaxy::architectural_interrupt_checkpoint(services, 0x80004004u, 0x80004008u, context, memory);"
        ));
        assert!(output.contains("context->segment_registers[7]"));
        assert!(output.contains("context->segment_registers[8] = context->gpr[5]"));
        assert!(output.contains("read_spr(context, 268u"));
        assert!(output.contains(
            "galaxy::write_decrementer_and_notify(context, context->gpr[7], memory, services, 0x80004014u, 0x80004018u);"
        ));
        assert!(!output.contains("galaxy::write_spr(context, 22u, context->gpr[7]"));
        assert!(output.contains("dispatch_system_call"));
        assert!(output.contains("trap_word_immediate"));
        assert!(output.contains("0xFFFFFFFFu"));
    }

    #[test]
    fn lowers_cache_hint_and_interrupt_return() {
        let dcbt = (31_u32 << 26) | (3 << 16) | (4 << 11) | (278 << 1);
        let words = [dcbt, 0x1006_1FEC, 0x4C00_0064, 0x6000_0000];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words(0x8000_4000, &bytes).expect("cache hint and rfi are supported");
        assert!(output.contains("cache_prefetch_hint"));
        assert!(output.contains("guest_zero"));
        assert!(output.contains("dispatch_return_from_interrupt"));
        assert!(output.ends_with("    }\n"));
    }

    #[test]
    fn validates_reachable_fallthrough_instead_of_padding() {
        let vector_words = [0x4C00_0064_u32, 0x6000_0000];
        let vector_bytes: Vec<u8> = vector_words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        assert!(!function_can_fall_through(0x8000_4000, &vector_bytes));
        lower_words(0x8000_4000, &vector_bytes).expect("padding after rfi is unreachable");

        let thunk = 0x4800_0061_u32.to_be_bytes();
        assert!(!function_can_fall_through(0x8000_4000, &thunk));
        lower_words(0x8000_4000, &thunk).expect("final linked vector branch returns to host");

        let fallthrough = 0x3860_0001_u32.to_be_bytes();
        assert!(function_can_fall_through(0x8000_4000, &fallthrough));
        assert!(matches!(
            lower_words(0x8000_4000, &fallthrough),
            Err(TranslationError::MissingReturn { .. })
        ));
    }

    #[test]
    fn lowers_indirect_ctr_branch() {
        let output =
            lower_words(0x8000_4000, &0x4E80_0420_u32.to_be_bytes()).expect("bctr supported");
        assert!(output.contains("branch_target = context->ctr & 0xFFFFFFFCu;"));
        assert!(output.contains("cached_target_80004000"));
        assert!(output.contains("galaxy::call_guest_cached"));
        assert!(output.contains("return;"));
    }

    #[test]
    fn lowers_jpa_indirect_callbacks_through_exact_generic_dispatch() {
        let bctrl_then_blr = [0x4E80_0421_u32, 0x4E80_0020u32]
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect::<Vec<_>>();

        let direction =
            lower_words(0x803A_3BB4, &bctrl_then_blr).expect("JPA direction callback bctrl lowers");
        assert!(direction.contains("cached_target_803A3BB4"));
        assert!(direction.contains("galaxy::call_guest_cached"));
        assert!(direction.contains(
            "galaxy::call_return_checkpoint(services, 0x803A3BB4u, 0x803A3BB8u, context, memory);"
        ));
        assert!(!direction.contains("native_jpa_direction_callback"));

        let projection = lower_words(0x803A_3CA8, &bctrl_then_blr)
            .expect("JPA projection callback bctrl lowers");
        assert!(projection.contains("cached_target_803A3CA8"));
        assert!(projection.contains("galaxy::call_guest_cached"));
        assert!(projection.contains(
            "galaxy::call_return_checkpoint(services, 0x803A3CA8u, 0x803A3CACu, context, memory);"
        ));
        assert!(!projection.contains("native_jpa_projection_callback"));

        let draw =
            lower_words(0x803A_3CE4, &bctrl_then_blr).expect("JPA draw callback bctrl lowers");
        assert!(draw.contains("cached_target_803A3CE4"));
        assert!(draw.contains("galaxy::call_guest_cached"));
        assert!(draw.contains(
            "galaxy::call_return_checkpoint(services, 0x803A3CE4u, 0x803A3CE8u, context, memory);"
        ));
        assert!(!draw.contains("native_jpa_draw_callback"));

        let particle_list = lower_words(0x8044_3420, &bctrl_then_blr)
            .expect("JPA particle-list draw callback bctrl lowers");
        assert!(particle_list.contains("cached_target_80443420"));
        assert!(particle_list.contains("galaxy::call_guest_cached"));
        assert!(particle_list.contains(
            "galaxy::call_return_checkpoint(services, 0x80443420u, 0x80443424u, context, memory);"
        ));
        assert!(!particle_list.contains("native_jpa_draw_particle_list_callback"));

        let particle_list_alt = lower_words(0x8044_33BC, &bctrl_then_blr)
            .expect("JPA alternate particle-list draw callback bctrl lowers");
        assert!(particle_list_alt.contains("cached_target_804433BC"));
        assert!(particle_list_alt.contains("galaxy::call_guest_cached"));
        assert!(particle_list_alt.contains(
            "galaxy::call_return_checkpoint(services, 0x804433BCu, 0x804433C0u, context, memory);"
        ));
        assert!(!particle_list_alt.contains("native_jpa_draw_particle_list_callback"));
    }

    #[test]
    fn generated_module_ltcg_is_explicit_opt_in() {
        let function = FunctionRange {
            address: 0x8000_4000,
            size: 8,
        };
        let files = emit_sharded_module(
            &[(function, "    return;\n".to_owned())],
            &[],
            1,
            function.address,
        )
        .expect("module generation succeeds");
        let cmake = files
            .iter()
            .find(|file| file.name == "CMakeLists.txt")
            .expect("generated CMake file");

        assert!(cmake.contents.contains(
            "option(GALAXY_MODULE_ENABLE_LTCG \"Enable Release LTCG for the generated RMGE01 game module\" OFF)"
        ));
        assert!(!cmake.contents.contains(
            "option(GALAXY_MODULE_ENABLE_LTCG \"Enable Release LTCG for the generated RMGE01 game module\" ON)"
        ));
        assert!(cmake
            .contents
            .contains("if(GALAXY_MODULE_ENABLE_LTCG AND NOT CMAKE_CXX_COMPILER_ID STREQUAL \"Clang\")"));
        assert!(cmake
            .contents
            .contains("-ffp-contract=off -fwrapv -fno-strict-aliasing"));
        assert!(cmake
            .contents
            .contains("$<$<CONFIG:Release>:/GL> $<$<CONFIG:Release>:/Gw>"));
        assert!(cmake
            .contents
            .contains("$<$<CONFIG:Release>:/LTCG> $<$<CONFIG:Release>:/OPT:REF>"));
    }

    #[test]
    fn generated_module_precompiles_the_shared_header_by_default() {
        let function = FunctionRange {
            address: 0x8000_4000,
            size: 8,
        };
        let files = emit_sharded_module(
            &[(function, "    return;\n".to_owned())],
            &[],
            1,
            function.address,
        )
        .expect("module generation succeeds");
        let cmake = files
            .iter()
            .find(|file| file.name == "CMakeLists.txt")
            .expect("generated CMake file");

        assert!(cmake.contents.contains(
            "option(GALAXY_MODULE_USE_PCH \"Precompile the shared generated RMGE01 function declarations\" ON)"
        ));
        assert!(!cmake.contents.contains(
            "option(GALAXY_MODULE_USE_PCH \"Precompile the shared generated RMGE01 function declarations\" OFF)"
        ));
        assert!(cmake.contents.contains("if(GALAXY_MODULE_USE_PCH)"));
        assert!(cmake.contents.contains(
            "target_precompile_headers(RMGE01_game PRIVATE \"${CMAKE_CURRENT_SOURCE_DIR}/functions.h\")"
        ));
    }

    #[test]
    fn public_translate_module_wrapper_matches_the_explicit_default_budget() {
        let _: fn(&Path, usize, usize) -> Result<TranslationModule, TranslationError> =
            translate_module;
        let _: fn(&Path, usize, usize, usize) -> Result<TranslationModule, TranslationError> =
            translate_module_with_source_budget;

        let invalid_default = translate_module(Path::new("unused"), 1, 0)
            .expect_err("the compatibility wrapper must preserve invalid shard-size behavior");
        let invalid_explicit = translate_module_with_source_budget(
            Path::new("unused"),
            1,
            0,
            DEFAULT_MODULE_SHARD_SOURCE_KIB,
        )
        .expect_err("the explicit default must preserve invalid shard-size behavior");
        assert!(matches!(
            invalid_default,
            TranslationError::InvalidShardSize
        ));
        assert!(matches!(
            invalid_explicit,
            TranslationError::InvalidShardSize
        ));

        let temp = tempfile::tempdir().expect("temporary directory is available");
        let missing_input = temp.path().join("missing-rmge01");
        let default_error = translate_module(&missing_input, 1, 1)
            .expect_err("missing input must fail through the compatibility wrapper");
        let explicit_error = translate_module_with_source_budget(
            &missing_input,
            1,
            1,
            DEFAULT_MODULE_SHARD_SOURCE_KIB,
        )
        .expect_err("missing input must fail through the explicit default");
        assert!(matches!(&default_error, TranslationError::Game(_)));
        assert!(matches!(&explicit_error, TranslationError::Game(_)));
        assert_eq!(default_error.to_string(), explicit_error.to_string());
    }

    #[test]
    fn sharded_module_respects_count_and_rendered_source_limits_losslessly() {
        assert_eq!(DEFAULT_MODULE_SHARD_SOURCE_KIB, 384);
        let functions = (0_u32..5)
            .map(|index| {
                (
                    FunctionRange {
                        address: 0x8100_0000 + index * 8,
                        size: 8,
                    },
                    format!("    /* body-{index} {} */\n    return;\n", "x".repeat(48)),
                )
            })
            .collect::<Vec<_>>();
        let owners = BTreeSet::new();
        let exact_fpu = BTreeSet::new();
        let first = render_sharded_function_definition(
            functions[0].0,
            &functions[0].1,
            &owners,
            &exact_fpu,
        )
        .expect("first function renders");
        let second = render_sharded_function_definition(
            functions[1].0,
            &functions[1].1,
            &owners,
            &exact_fpu,
        )
        .expect("second function renders");
        let source_budget =
            FUNCTION_SHARD_PREFIX.len() + FUNCTION_SHARD_SUFFIX.len() + first.len() + second.len();

        let files = emit_sharded_module_with_source_budget(
            &functions,
            &[],
            3,
            source_budget,
            functions[0].0.address,
        )
        .expect("source-bounded module emits");
        let repeated = emit_sharded_module_with_source_budget(
            &functions,
            &[],
            3,
            source_budget,
            functions[0].0.address,
        )
        .expect("repeated source-bounded module emits");
        assert_eq!(
            files
                .iter()
                .map(|file| (&file.name, &file.contents))
                .collect::<Vec<_>>(),
            repeated
                .iter()
                .map(|file| (&file.name, &file.contents))
                .collect::<Vec<_>>(),
            "identical input must produce byte-identical shard boundaries"
        );

        let shards = files
            .iter()
            .filter(|file| file.name.starts_with("functions_") && file.name.ends_with(".cpp"))
            .collect::<Vec<_>>();
        assert_eq!(
            shards.len(),
            3,
            "the byte ceiling must split after two functions"
        );
        assert!(
            shards
                .iter()
                .all(|shard| shard.contents.len() <= source_budget),
            "every complete shard file must stay within the rendered-source ceiling"
        );
        let observed_addresses = shards
            .iter()
            .flat_map(|shard| {
                let mut addresses = functions
                    .iter()
                    .filter_map(|(function, _)| {
                        let token = format!("void fn_{:08X}(", function.address);
                        shard
                            .contents
                            .find(&token)
                            .map(|position| (position, function.address))
                    })
                    .collect::<Vec<_>>();
                addresses.sort_unstable_by_key(|(position, _)| *position);
                addresses
                    .into_iter()
                    .map(|(_, address)| address)
                    .collect::<Vec<_>>()
            })
            .collect::<Vec<_>>();
        assert_eq!(
            observed_addresses,
            functions
                .iter()
                .map(|(function, _)| function.address)
                .collect::<Vec<_>>(),
            "partitioning must preserve every function exactly once and in order"
        );
        for (function, _) in &functions {
            let token = format!("void fn_{:08X}(", function.address);
            assert_eq!(
                shards
                    .iter()
                    .map(|shard| shard.contents.matches(&token).count())
                    .sum::<usize>(),
                1,
                "each translated function must have exactly one definition"
            );
        }

        let cmake = files
            .iter()
            .find(|file| file.name == "CMakeLists.txt")
            .expect("generated CMake file");
        for shard in &shards {
            assert!(cmake.contents.contains(&format!("    {}\n", shard.name)));
        }
        assert!(!cmake.contents.contains("functions_0003.cpp"));

        let count_bounded = emit_sharded_module_with_source_budget(
            &functions,
            &[],
            2,
            DEFAULT_MODULE_SHARD_SOURCE_KIB * KIBIBYTE,
            functions[0].0.address,
        )
        .expect("count-bounded module emits");
        assert_eq!(
            count_bounded
                .iter()
                .filter(|file| {
                    file.name.starts_with("functions_") && file.name.ends_with(".cpp")
                })
                .count(),
            3,
            "the existing shard-size argument remains a hard function-count ceiling"
        );
    }

    #[test]
    fn final_aspect_include_cannot_escape_shard_source_ceiling() {
        for address in [0x803F_6B44, 0x803C_A6C4, 0x804D_0708, 0x8009_7288] {
            let functions = [(
                FunctionRange { address, size: 4 },
                "    return;\n".to_owned(),
            )];
            let emit =
                |limit| emit_sharded_module_with_source_budget(&functions, &[], 1, limit, address);
            let files = emit(4096).expect("aspect owner emits with sufficient space");
            let shard = files
                .iter()
                .find(|file| file.name == "functions_0000.cpp")
                .unwrap();
            assert!(shard
                .contents
                .contains("#include \"galaxy/experimental_ultrawide_aspect.h\""));
            let full_bytes = shard.contents.len();
            // The final file must pass exactly at the ceiling, but reject one
            // byte less even though its pre-include function fits that budget.
            let exact = emit(full_bytes).expect("exact final ceiling admits shard");
            assert_eq!(
                exact
                    .iter()
                    .find(|file| file.name == shard.name)
                    .unwrap()
                    .contents,
                shard.contents
            );
            let error = emit(full_bytes - 1).expect_err("added include exceeds final ceiling");
            assert!(matches!(error, TranslationError::ShardExceedsSourceBudget {
                name, generated_bytes, limit_bytes,
            } if name == "functions_0000.cpp" && generated_bytes == full_bytes
                && limit_bytes == full_bytes - 1));
        }
    }

    #[test]
    fn sharded_module_hard_fails_an_oversize_rendered_function() {
        let function = FunctionRange {
            address: 0x8100_1000,
            size: 8,
        };
        let body = "    /* deliberately large body */\n    return;\n".to_owned();
        let rendered =
            render_sharded_function_definition(function, &body, &BTreeSet::new(), &BTreeSet::new())
                .expect("function renders");
        let generated_bytes =
            FUNCTION_SHARD_PREFIX.len() + rendered.len() + FUNCTION_SHARD_SUFFIX.len();
        let limit_bytes = generated_bytes - 1;
        let error = emit_sharded_module_with_source_budget(
            &[(function, body)],
            &[],
            64,
            limit_bytes,
            function.address,
        )
        .expect_err("an oversize function must fail instead of escaping the resource bound");
        assert!(matches!(
            error,
            TranslationError::FunctionExceedsShardSourceBudget {
                address,
                generated_bytes: actual,
                limit_bytes: limit,
            } if address == function.address
                && actual == generated_bytes
                && limit == limit_bytes
        ));

        let zero_budget = emit_sharded_module_with_source_budget(
            &[(function, "    return;\n".to_owned())],
            &[],
            64,
            0,
            function.address,
        )
        .expect_err("a zero source budget must fail before partitioning");
        assert!(matches!(
            zero_budget,
            TranslationError::InvalidShardSourceBudget
        ));
    }

    #[test]
    fn generated_module_bounds_ninja_compile_concurrency() {
        let function = FunctionRange {
            address: 0x8100_2000,
            size: 8,
        };
        let files = emit_sharded_module(
            &[(function, "    return;\n".to_owned())],
            &[],
            1,
            function.address,
        )
        .expect("module generation succeeds");
        let cmake = files
            .iter()
            .find(|file| file.name == "CMakeLists.txt")
            .expect("generated CMake file");

        assert!(cmake.contents.contains(
            "set(GALAXY_MODULE_COMPILE_JOBS \"1\" CACHE STRING \"Maximum concurrent generated-module compiler processes\")"
        ));
        assert!(cmake.contents.contains(
            "if(NOT GALAXY_MODULE_COMPILE_JOBS MATCHES \"^([1-9]|[1-5][0-9]|6[0-4])$\")"
        ));
        assert!(cmake
            .contents
            .contains("GALAXY_MODULE_COMPILE_JOBS must be an integer from 1 to 64"));
        assert!(cmake.contents.contains(
            "set_property(GLOBAL PROPERTY JOB_POOLS \"galaxy_module_compile=${GALAXY_MODULE_COMPILE_JOBS}\")"
        ));
        assert!(cmake.contents.contains(
            "set_property(TARGET RMGE01_game PROPERTY JOB_POOL_COMPILE galaxy_module_compile)"
        ));
        assert!(cmake.contents.contains(
            "string(REGEX REPLACE \"(^| )/Ob[0-9]\" \"\\\\1/Ob3\" CMAKE_CXX_FLAGS_RELEASE"
        ));
        assert!(!cmake.contents.contains("/Od"));
        assert!(!cmake.contents.lines().any(|line| {
            line.contains("target_compile_options(RMGE01_game") && line.contains("/fp:fast")
        }));
        assert!(cmake.contents.contains(
            "option(GALAXY_MODULE_ENABLE_LTCG \"Enable Release LTCG for the generated RMGE01 game module\" OFF)"
        ));
    }

    #[test]
    fn generated_module_keeps_strict_msvc_warnings_except_guest_dead_code() {
        let function = FunctionRange {
            address: 0x8000_4000,
            size: 8,
        };
        let files = emit_sharded_module(
            &[(function, "    return;\n".to_owned())],
            &[(0x8000_4004, function.address)],
            1,
            0x8000_403C,
        )
        .expect("module generation succeeds");
        let cmake = files
            .iter()
            .find(|file| file.name == "CMakeLists.txt")
            .expect("generated CMake file");

        assert!(cmake
            .contents
            .contains("/W4 /WX /wd4702 /permissive- /EHsc"));
        assert!(cmake
            .contents
            .contains("Guest address preservation can emit intentional dead blocks"));
        assert!(cmake
            .contents
            .contains("set(GALAXY_NATIVE_ISA \"AUTO\" CACHE STRING"));
        assert!(cmake.contents.contains("GALAXY_HOST_CAN_RUN_AVX2_FMA"));
        assert!(cmake
            .contents
            .contains("if(galaxy_native_isa_effective STREQUAL \"AVX2\")"));
        assert!(!cmake.contents.contains("/Oi /arch:AVX2 /favor:INTEL64"));
        // The clang-cl branch must act on the ISA this template selects and
        // reports. Setup compiles the module with clang-cl, where
        // CMAKE_CXX_COMPILER_ID is "Clang" and /arch:AVX2 is not a spelling the
        // compiler honours, so -mavx2 has to be present here and /arch:AVX2 has
        // to stay confined to the non-Clang branch. This assertion exists
        // because the flag was silently absent once already: the template chose
        // and announced AVX2 while emitting SSE2 machine code. Never -mfma
        // (guest float results must stay bit-identical to SoftFloat) and never
        // -mavx512* (the host probe is the AVX2+OSXSAVE test).
        assert!(cmake
            .contents
            .contains("target_compile_options(RMGE01_game PRIVATE -mavx2)"));
        assert!(cmake.contents.contains(
            "if(galaxy_native_isa_effective STREQUAL \"AVX2\" AND NOT CMAKE_CXX_COMPILER_ID STREQUAL \"Clang\")"));
        assert!(!cmake.contents.contains("-mfma"));
        assert!(!cmake.contents.contains("-mavx512"));
        let module = files
            .iter()
            .find(|file| file.name == "module.cpp")
            .expect("generated module file");
        assert!(module
            .contents
            .contains("manifest.guest_entry_point = 0x8000403Cu;"));
        assert!(module
            .contents
            .contains("constexpr std::array<FunctionRecord, 2>"));
        assert!(module
            .contents
            .contains("{0x80004004u, &rmge01::fn_80004000}"));
        assert!(module
            .contents
            .contains("manifest.translated_function_count = 1u;"));
        let shard = files
            .iter()
            .find(|file| file.name == "functions_0000.cpp")
            .expect("function shard exists");
        assert!(!shard.contents.contains("trace_audio_function_entry"));
    }

    #[test]
    fn generated_module_traces_known_audio_function_entries() {
        let function = FunctionRange {
            address: 0x803F_9DEC,
            size: 0x120,
        };
        let files = emit_sharded_module(
            &[(function, "    return;\n".to_owned())],
            &[],
            1,
            function.address,
        )
        .expect("module generation succeeds");
        let shard = files
            .iter()
            .find(|file| file.name == "functions_0000.cpp")
            .expect("function shard exists");
        assert!(shard.contents.contains(
            "galaxy::trace_audio_function_entry(services, context, memory, 0x803F9DECu);"
        ));
    }

    #[test]
    fn generated_module_traces_only_verified_main_frame_entries() {
        for address in [
            0x8039_9AF0,
            0x8039_9B58,
            0x8039_D3D4,
            0x8039_DBE4,
            0x803B_462C,
            0x803B_5478,
            0x803B_7818,
            0x8039_B9B0,
            0x8038_5758,
            0x8039_B9B8,
            0x8039_A288,
            0x8039_FD20,
            0x8039_FEE0,
            0x8039_FF14,
            0x803A_007C,
            0x803A_00FC,
        ] {
            let function = FunctionRange {
                address,
                size: 0x20,
            };
            let files = emit_sharded_module(
                &[(function, "    return;\n".to_owned())],
                &[],
                1,
                function.address,
            )
            .expect("module generation succeeds");
            let shard = files
                .iter()
                .find(|file| file.name == "functions_0000.cpp")
                .expect("function shard exists");
            assert!(
                shard.contents.contains(&format!(
                    "if (context->pc == 0x{address:08X}u) {{\n        galaxy::trace_main_frame_function_entry(services, 0x{address:08X}u);"
                )),
                "verified main-frame entry 0x{address:08X} must trace only a true function entry"
            );
        }

        let unrelated = FunctionRange {
            address: 0x8039_9AF4,
            size: 0x20,
        };
        let files = emit_sharded_module(
            &[(unrelated, "    return;\n".to_owned())],
            &[],
            1,
            unrelated.address,
        )
        .expect("module generation succeeds");
        let shard = files
            .iter()
            .find(|file| file.name == "functions_0000.cpp")
            .expect("function shard exists");
        assert!(
            !shard.contents.contains("trace_main_frame_function_entry("),
            "nearby non-entry address must not be traced"
        );
    }

    fn scene_update_span_fixture() -> Vec<GeneratedSource> {
        let start = 0x8033_E844;
        let mut words = [0x6000_0000u32; 0x98 / 4];
        *words.last_mut().unwrap() = 0x4E80_0020;
        for (pc, target) in SCENE_UPDATE_CALLS {
            words[((pc - start) / 4) as usize] =
                0x4800_0001 | (target.wrapping_sub(pc) & 0x03FF_FFFC);
        }
        // Synthetic bypasses exercise the shared labels without embedding a
        // game function body. Neither bypass may execute a return marker.
        words[((0x8033_E89C - start) / 4) as usize] = 0x4800_0008;
        words[((0x8033_E8B4 - start) / 4) as usize] = 0x4800_000C;
        let bytes = words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect::<Vec<_>>();
        let entries = SCENE_UPDATE_CALLS
            .into_iter()
            .map(|(pc, _)| pc + 4)
            .collect::<BTreeSet<_>>();
        let callable = SCENE_UPDATE_CALLS
            .into_iter()
            .map(|(_, target)| (target, target))
            .collect::<BTreeMap<_, _>>();
        let body = lower_words_for_module_with_call_return_entries_and_exact_fpu(
            start,
            &bytes,
            &entries,
            &entries,
            &callable,
            &BTreeSet::new(),
        )
        .unwrap();
        let aliases = entries.iter().map(|pc| (*pc, start)).collect::<Vec<_>>();
        emit_sharded_module(
            &[
                (
                    FunctionRange {
                        address: start,
                        size: 0x98,
                    },
                    body,
                ),
                (
                    FunctionRange {
                        address: 0x8033_F000,
                        size: 4,
                    },
                    "    return;\n".to_owned(),
                ),
            ],
            &aliases,
            1,
            start,
        )
        .unwrap()
    }

    #[test]
    fn scene_update_trace_preserves_normal_resumed_and_shared_join_paths() {
        assert!(!ModuleTranslationOptions::default().trace_scene_update_spans);
        let plain = scene_update_span_fixture();
        let mut traced = plain.clone();
        insert_scene_update_span_markers(&mut traced, 384 * KIBIBYTE).unwrap();
        let mut changed = 0;
        for (before, after) in plain.iter().zip(&traced) {
            assert_eq!(before.name, after.name);
            if before.contents == after.contents {
                continue;
            }
            changed += 1;
            let mut stripped = after.contents.clone();
            for (call_pc, _) in SCENE_UPDATE_CALLS {
                let return_pc = call_pc + 4;
                let pre = format!(
                    "    galaxy::trace_main_frame_stage(services, \"__galaxy_main_frame_stage:scene-update-before-{call_pc:08X}\");\n"
                );
                let post = format!(
                    "    galaxy::trace_main_frame_stage(services, \"__galaxy_main_frame_stage:scene-update-after-{return_pc:08X}\");\n"
                );
                assert_eq!(stripped.matches(&pre).count(), 1);
                assert_eq!(stripped.matches(&post).count(), 2);
                assert!(!stripped.contains(&format!("label_{return_pc:08X}:\n{post}")));
                assert!(stripped.contains(&format!(
                    "call_return_{return_pc:08X}:\n    galaxy::call_return_checkpoint(services, 0x{call_pc:08X}u, 0x{return_pc:08X}u, context, memory);\n{post}    goto label_{return_pc:08X};"
                )));
                stripped = stripped.replace(&pre, "").replace(&post, "");
            }
            assert!(stripped.contains("goto label_8033E8A4;"));
            assert!(stripped.contains("goto label_8033E8C0;"));
            assert_eq!(stripped, before.contents);
        }
        assert_eq!(plain.len(), traced.len());
        assert_eq!(changed, 1);
    }

    #[test]
    fn scene_update_trace_rejects_partial_or_changed_sources_transactionally() {
        assert!(matches!(
            translate_module_with_options(
                Path::new("not-opened"),
                256,
                64,
                384,
                ModuleTranslationOptions {
                    trace_scene_update_spans: true,
                    ..Default::default()
                },
            ),
            Err(TranslationError::SceneUpdateSpanTrace(_))
        ));
        for variant in 0..5 {
            let mut files = scene_update_span_fixture();
            let index = files
                .iter()
                .position(|file| {
                    file.name.starts_with("functions_")
                        && file.name.ends_with(".cpp")
                        && file.contents.contains("\nvoid fn_8033E844(")
                })
                .unwrap();
            let original_owner = files[index].contents.clone();
            let ceiling = files[index].contents.len();
            match variant {
                0 => {
                    files[index].contents = files[index].contents.replace(
                        "    context->pc = 0x803F7E40u;",
                        "    context->pc = 0x803F7E44u;",
                    )
                }
                1 => {
                    files[index].contents = files[index]
                        .contents
                        .replace("call_return_8033E864:", "missing_8033E864:")
                }
                2 => {
                    let duplicate = files[index].clone();
                    files.push(duplicate);
                }
                3 => {
                    files[index].contents = files[index]
                        .contents
                        .replace("    goto label_8033E8C4;", "    goto label_8033E8C0;")
                }
                _ => (),
            }
            if matches!(variant, 0 | 1 | 3) {
                assert_ne!(
                    files[index].contents, original_owner,
                    "variant {variant} must alter its intended source anchor"
                );
            }
            let before = files
                .iter()
                .map(|file| file.contents.clone())
                .collect::<Vec<_>>();
            let result = insert_scene_update_span_markers(
                &mut files,
                if variant == 4 {
                    ceiling
                } else {
                    384 * KIBIBYTE
                },
            );
            assert!(result.is_err(), "variant {variant} unexpectedly succeeded");
            assert_eq!(
                before,
                files
                    .iter()
                    .map(|file| file.contents.clone())
                    .collect::<Vec<_>>()
            );
        }
    }

    fn particle_direction_edge_profile_fixture() -> Vec<GeneratedSource> {
        let start = 0x803A_3B6C;
        let end = 0x803A_3D0C;
        let mut words = vec![0x6000_0000u32; ((end - start) / 4) as usize];
        *words.last_mut().unwrap() = 0x4E80_0020;
        for (pc, target) in PARTICLE_DIRECTION_DIRECT_CALLS {
            words[((pc - start) / 4) as usize] =
                0x4800_0001 | (target.wrapping_sub(pc) & 0x03FF_FFFC);
        }
        let bytes = words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect::<Vec<_>>();
        let entries = PARTICLE_DIRECTION_DIRECT_CALLS
            .into_iter()
            .map(|(pc, _)| pc + 4)
            .collect::<BTreeSet<_>>();
        let callable = PARTICLE_DIRECTION_DIRECT_CALLS
            .into_iter()
            .map(|(_, target)| (target, target))
            .collect::<BTreeMap<_, _>>();
        let exact_fpu = callable.keys().copied().collect::<BTreeSet<_>>();
        let body = lower_words_for_module_with_call_return_entries_and_exact_fpu(
            start, &bytes, &entries, &entries, &callable, &exact_fpu,
        )
        .unwrap();
        let aliases = entries.iter().map(|pc| (*pc, start)).collect::<Vec<_>>();
        emit_sharded_module(
            &[(
                FunctionRange {
                    address: start,
                    size: end - start,
                },
                body,
            )],
            &aliases,
            1,
            start,
        )
        .unwrap()
    }

    #[test]
    fn particle_direction_edge_profile_wraps_only_static_calls() {
        assert!(!ModuleTranslationOptions::default().profile_particle_direction_edges);
        let plain = particle_direction_edge_profile_fixture();
        let mut profiled = plain.clone();
        insert_particle_direction_edge_profile(&mut profiled, 384 * KIBIBYTE).unwrap();
        let mut changed = 0;
        for (before, after) in plain.iter().zip(&profiled) {
            assert_eq!(before.name, after.name);
            if before.contents == after.contents {
                continue;
            }
            changed += 1;
            let mut stripped = after.contents.clone();
            for (call_pc, target) in PARTICLE_DIRECTION_DIRECT_CALLS {
                let return_pc = call_pc + 4;
                let original = format!(
                    "    context->lr = 0x{return_pc:08X}u;\n    context->pc = 0x{target:08X}u;\n    rmge01::fn_{target:08X}(context, memory, services);\n"
                );
                let wrapper = format!(
                    "    context->lr = 0x{return_pc:08X}u;\n    galaxy::call_guest_direct_resolved(services, 0x{target:08X}u, &rmge01::fn_{target:08X}, context, memory, 0x{call_pc:08X}u);\n"
                );
                assert_eq!(stripped.matches(&wrapper).count(), 1);
                assert_eq!(
                    stripped
                        .matches(&format!("call_return_{return_pc:08X}:"))
                        .count(),
                    1
                );
                assert_eq!(
                    stripped
                        .matches(&format!(
                            "galaxy::call_return_checkpoint(services, 0x{call_pc:08X}u, 0x{return_pc:08X}u, context, memory);"
                        ))
                        .count(),
                    2
                );
                stripped = stripped.replacen(&wrapper, &original, 1);
            }
            assert_eq!(stripped, before.contents);
        }
        assert_eq!(plain.len(), profiled.len());
        assert_eq!(changed, 1);
    }

    #[test]
    fn particle_direction_edge_profile_fails_closed_transactionally() {
        assert!(matches!(
            translate_module_with_options(
                Path::new("not-opened"),
                256,
                64,
                384,
                ModuleTranslationOptions {
                    profile_particle_direction_edges: true,
                    ..Default::default()
                },
            ),
            Err(TranslationError::ParticleDirectionEdgeProfile(_))
        ));
        for variant in 0..4 {
            let mut files = particle_direction_edge_profile_fixture();
            let index = files
                .iter()
                .position(|file| {
                    file.name.starts_with("functions_")
                        && file.name.ends_with(".cpp")
                        && file.contents.contains("\nvoid fn_803A3B6C(")
                })
                .unwrap();
            let ceiling = files[index].contents.len();
            let original_owner = files[index].contents.clone();
            match variant {
                0 => {
                    files[index].contents = files[index].contents.replace(
                        "rmge01::fn_803E595C(context, memory, services);",
                        "rmge01::fn_803E5960(context, memory, services);",
                    )
                }
                1 => {
                    files[index].contents = files[index]
                        .contents
                        .replace("call_return_803A3BC4:", "missing_803A3BC4:")
                }
                2 => files.push(files[index].clone()),
                _ => (),
            }
            if matches!(variant, 0 | 1) {
                assert_ne!(
                    files[index].contents, original_owner,
                    "variant {variant} must alter its intended source anchor"
                );
            }
            let before = files
                .iter()
                .map(|file| file.contents.clone())
                .collect::<Vec<_>>();
            let result = insert_particle_direction_edge_profile(
                &mut files,
                if variant == 3 {
                    ceiling
                } else {
                    384 * KIBIBYTE
                },
            );
            assert!(result.is_err(), "variant {variant} unexpectedly succeeded");
            assert_eq!(
                before,
                files
                    .iter()
                    .map(|file| file.contents.clone())
                    .collect::<Vec<_>>()
            );
        }
    }

    fn nw4r_material_setup_edge_profile_fixture() -> Vec<GeneratedSource> {
        let start = 0x8001_30F0;
        let end = 0x8001_3F90;
        let mut words = vec![0x6000_0000u32; ((end - start) / 4) as usize];
        *words.last_mut().unwrap() = 0x4E80_0020;
        for (pc, target) in NW4R_MATERIAL_SETUP_DIRECT_CALLS {
            words[((pc - start) / 4) as usize] =
                0x4800_0001 | (target.wrapping_sub(pc) & 0x03FF_FFFC);
        }
        let bytes = words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect::<Vec<_>>();
        let entries = NW4R_MATERIAL_SETUP_DIRECT_CALLS
            .into_iter()
            .map(|(pc, _)| pc + 4)
            .collect::<BTreeSet<_>>();
        let callable = NW4R_MATERIAL_SETUP_DIRECT_CALLS
            .into_iter()
            .map(|(_, target)| (target, target))
            .collect::<BTreeMap<_, _>>();
        let exact_fpu = callable.keys().copied().collect::<BTreeSet<_>>();
        let body = lower_words_for_module_with_call_return_entries_and_exact_fpu(
            start, &bytes, &entries, &entries, &callable, &exact_fpu,
        )
        .unwrap();
        let aliases = entries.iter().map(|pc| (*pc, start)).collect::<Vec<_>>();
        emit_sharded_module(
            &[(
                FunctionRange {
                    address: start,
                    size: end - start,
                },
                body,
            )],
            &aliases,
            1,
            start,
        )
        .unwrap()
    }

    #[test]
    fn nw4r_material_setup_edge_profile_wraps_exact_static_call_set() {
        assert!(!ModuleTranslationOptions::default().profile_nw4r_material_setup_edges);
        let plain = nw4r_material_setup_edge_profile_fixture();
        let mut profiled = plain.clone();
        insert_nw4r_material_setup_edge_profile(&mut profiled, 384 * KIBIBYTE).unwrap();
        let mut changed = 0;
        let mut wrapped = 0;
        for (before, after) in plain.iter().zip(&profiled) {
            assert_eq!(before.name, after.name);
            if before.contents == after.contents {
                continue;
            }
            changed += 1;
            let mut stripped = after.contents.clone();
            for (call_pc, target) in NW4R_MATERIAL_SETUP_DIRECT_CALLS {
                let return_pc = call_pc + 4;
                let original = format!(
                    "    context->lr = 0x{return_pc:08X}u;\n    context->pc = 0x{target:08X}u;\n    rmge01::fn_{target:08X}(context, memory, services);\n"
                );
                let wrapper = format!(
                    "    context->lr = 0x{return_pc:08X}u;\n    galaxy::call_guest_direct_resolved(services, 0x{target:08X}u, &rmge01::fn_{target:08X}, context, memory, 0x{call_pc:08X}u);\n"
                );
                assert_eq!(stripped.matches(&wrapper).count(), 1);
                assert_eq!(
                    stripped
                        .matches(&format!("call_return_{return_pc:08X}:"))
                        .count(),
                    1
                );
                assert_eq!(
                    stripped
                        .matches(&format!(
                            "galaxy::call_return_checkpoint(services, 0x{call_pc:08X}u, 0x{return_pc:08X}u, context, memory);"
                        ))
                        .count(),
                    2
                );
                stripped = stripped.replacen(&wrapper, &original, 1);
                wrapped += 1;
            }
            assert_eq!(stripped, before.contents);
        }
        assert_eq!(plain.len(), profiled.len());
        assert_eq!(changed, 1);
        assert_eq!(wrapped, NW4R_MATERIAL_SETUP_DIRECT_CALLS.len());
    }

    #[test]
    fn nw4r_material_setup_edge_profile_fails_closed_transactionally() {
        assert!(matches!(
            translate_module_with_options(
                Path::new("not-opened"),
                256,
                64,
                384,
                ModuleTranslationOptions {
                    profile_nw4r_material_setup_edges: true,
                    ..Default::default()
                },
            ),
            Err(TranslationError::Nw4rMaterialSetupEdgeProfile(_))
        ));
        for variant in 0..4 {
            let mut files = nw4r_material_setup_edge_profile_fixture();
            let index = files
                .iter()
                .position(|file| {
                    file.name.starts_with("functions_")
                        && file.name.ends_with(".cpp")
                        && file.contents.contains("\nvoid fn_800130F0(")
                })
                .unwrap();
            let ceiling = files[index].contents.len();
            let original_owner = files[index].contents.clone();
            match variant {
                0 => {
                    files[index].contents = files[index].contents.replace(
                        "rmge01::fn_804BBAD4(context, memory, services);",
                        "rmge01::fn_804BBAD8(context, memory, services);",
                    )
                }
                1 => {
                    files[index].contents = files[index]
                        .contents
                        .replace("call_return_80013130:", "missing_80013130:")
                }
                2 => files.push(files[index].clone()),
                _ => (),
            }
            if matches!(variant, 0 | 1) {
                assert_ne!(
                    files[index].contents, original_owner,
                    "variant {variant} must alter its intended source anchor"
                );
            }
            let before = files
                .iter()
                .map(|file| file.contents.clone())
                .collect::<Vec<_>>();
            let result = insert_nw4r_material_setup_edge_profile(
                &mut files,
                if variant == 3 {
                    ceiling
                } else {
                    384 * KIBIBYTE
                },
            );
            assert!(result.is_err(), "variant {variant} unexpectedly succeeded");
            assert_eq!(
                before,
                files
                    .iter()
                    .map(|file| file.contents.clone())
                    .collect::<Vec<_>>()
            );
        }
    }

    fn nw4r_pane_draw_self_edge_profile_fixture(shard_size: usize) -> Vec<GeneratedSource> {
        let callable = NW4R_PANE_DRAW_SELF_DIRECT_CALLS
            .into_iter()
            .map(|(_, target)| (target, target))
            .collect::<BTreeMap<_, _>>();
        let exact_fpu = callable.keys().copied().collect::<BTreeSet<_>>();
        let mut aliases = Vec::new();
        let functions = [(0x8000_DEAC, 0x8000_DF88), (0x8000_E5FC, 0x8000_E7E4)]
            .into_iter()
            .map(|(start, end)| {
                let calls = NW4R_PANE_DRAW_SELF_DIRECT_CALLS
                    .into_iter()
                    .filter(|(pc, _)| *pc >= start && *pc < end)
                    .collect::<Vec<_>>();
                let mut words = vec![0x6000_0000u32; ((end - start) / 4) as usize];
                *words.last_mut().unwrap() = 0x4E80_0020;
                for (pc, target) in &calls {
                    words[((pc - start) / 4) as usize] =
                        0x4800_0001 | (target.wrapping_sub(*pc) & 0x03FF_FFFC);
                }
                let bytes = words
                    .iter()
                    .flat_map(|word| word.to_be_bytes())
                    .collect::<Vec<_>>();
                let entries = calls.iter().map(|(pc, _)| pc + 4).collect::<BTreeSet<_>>();
                aliases.extend(entries.iter().map(|pc| (*pc, start)));
                let body = lower_words_for_module_with_call_return_entries_and_exact_fpu(
                    start, &bytes, &entries, &entries, &callable, &exact_fpu,
                )
                .unwrap();
                (
                    FunctionRange {
                        address: start,
                        size: end - start,
                    },
                    body,
                )
            })
            .collect::<Vec<_>>();
        emit_sharded_module(&functions, &aliases, shard_size, 0x8000_DEAC).unwrap()
    }

    #[test]
    fn nw4r_pane_draw_self_edge_profile_wraps_exact_static_call_set() {
        assert!(!ModuleTranslationOptions::default().profile_nw4r_pane_draw_self_edges);
        let plain = nw4r_pane_draw_self_edge_profile_fixture(2);
        let mut profiled = plain.clone();
        insert_nw4r_pane_draw_self_edge_profile(&mut profiled, 384 * KIBIBYTE).unwrap();
        let mut changed = 0;
        let mut wrapped = 0;
        for (before, after) in plain.iter().zip(&profiled) {
            assert_eq!(before.name, after.name);
            if before.contents == after.contents {
                continue;
            }
            changed += 1;
            let mut stripped = after.contents.clone();
            for (call_pc, target) in NW4R_PANE_DRAW_SELF_DIRECT_CALLS {
                let return_pc = call_pc + 4;
                let original = format!(
                    "    context->lr = 0x{return_pc:08X}u;\n    context->pc = 0x{target:08X}u;\n    rmge01::fn_{target:08X}(context, memory, services);\n"
                );
                let wrapper = format!(
                    "    context->lr = 0x{return_pc:08X}u;\n    galaxy::call_guest_direct_resolved(services, 0x{target:08X}u, &rmge01::fn_{target:08X}, context, memory, 0x{call_pc:08X}u);\n"
                );
                assert_eq!(stripped.matches(&wrapper).count(), 1);
                assert_eq!(
                    stripped
                        .matches(&format!("call_return_{return_pc:08X}:"))
                        .count(),
                    1
                );
                assert_eq!(
                    stripped
                        .matches(&format!(
                            "galaxy::call_return_checkpoint(services, 0x{call_pc:08X}u, 0x{return_pc:08X}u, context, memory);"
                        ))
                        .count(),
                    2
                );
                stripped = stripped.replacen(&wrapper, &original, 1);
                wrapped += 1;
            }
            assert_eq!(stripped, before.contents);
        }
        assert_eq!(plain.len(), profiled.len());
        assert_eq!(changed, 1);
        assert_eq!(wrapped, NW4R_PANE_DRAW_SELF_DIRECT_CALLS.len());
    }

    #[test]
    fn nw4r_pane_draw_self_edge_profile_fails_closed_transactionally() {
        assert!(matches!(
            translate_module_with_options(
                Path::new("not-opened"),
                256,
                64,
                384,
                ModuleTranslationOptions {
                    profile_nw4r_pane_draw_self_edges: true,
                    ..Default::default()
                },
            ),
            Err(TranslationError::Nw4rPaneDrawSelfEdgeProfile(_))
        ));
        for variant in 0..5 {
            let mut files = nw4r_pane_draw_self_edge_profile_fixture(2);
            let index = files
                .iter()
                .position(|file| {
                    file.name.starts_with("functions_")
                        && file.name.ends_with(".cpp")
                        && file.contents.contains("\nvoid fn_8000DEAC(")
                })
                .unwrap();
            let ceiling = files[index].contents.len();
            let original_owner = files[index].contents.clone();
            match variant {
                0 => {
                    files[index].contents = files[index].contents.replace(
                        "rmge01::fn_80015674(context, memory, services);",
                        "rmge01::fn_80015678(context, memory, services);",
                    )
                }
                1 => {
                    files[index].contents = files[index]
                        .contents
                        .replace("call_return_8000DEF8:", "missing_8000DEF8:")
                }
                2 => files.push(files[index].clone()),
                3 => files = nw4r_pane_draw_self_edge_profile_fixture(1),
                _ => (),
            }
            if matches!(variant, 0 | 1) {
                assert_ne!(
                    files[index].contents, original_owner,
                    "variant {variant} must alter its intended source anchor"
                );
            }
            let before = files
                .iter()
                .map(|file| file.contents.clone())
                .collect::<Vec<_>>();
            let result = insert_nw4r_pane_draw_self_edge_profile(
                &mut files,
                if variant == 4 {
                    ceiling
                } else {
                    384 * KIBIBYTE
                },
            );
            assert!(result.is_err(), "variant {variant} unexpectedly succeeded");
            assert_eq!(
                before,
                files
                    .iter()
                    .map(|file| file.contents.clone())
                    .collect::<Vec<_>>()
            );
        }
    }

    fn nw4r_pane_hot_child_edge_profile_fixture() -> Vec<GeneratedSource> {
        let callable = NW4R_PANE_HOT_CHILD_DIRECT_CALLS
            .into_iter()
            .map(|(_, target)| (target, target))
            .collect::<BTreeMap<_, _>>();
        let exact_fpu = callable.keys().copied().collect::<BTreeSet<_>>();
        let mut aliases = Vec::new();
        let functions = [(0x8000_A450, 0x8000_A654), (0x8000_EB28, 0x8000_ECF0)]
            .into_iter()
            .map(|(start, end)| {
                let calls = NW4R_PANE_HOT_CHILD_DIRECT_CALLS
                    .into_iter()
                    .filter(|(pc, _)| *pc >= start && *pc < end)
                    .collect::<Vec<_>>();
                let mut words = vec![0x6000_0000u32; ((end - start) / 4) as usize];
                *words.last_mut().unwrap() = 0x4E80_0020;
                for (pc, target) in &calls {
                    words[((pc - start) / 4) as usize] =
                        0x4800_0001 | (target.wrapping_sub(*pc) & 0x03FF_FFFC);
                }
                let bytes = words
                    .iter()
                    .flat_map(|word| word.to_be_bytes())
                    .collect::<Vec<_>>();
                let entries = calls.iter().map(|(pc, _)| pc + 4).collect::<BTreeSet<_>>();
                aliases.extend(entries.iter().map(|pc| (*pc, start)));
                let body = lower_words_for_module_with_call_return_entries_and_exact_fpu(
                    start, &bytes, &entries, &entries, &callable, &exact_fpu,
                )
                .unwrap();
                (
                    FunctionRange {
                        address: start,
                        size: end - start,
                    },
                    body,
                )
            })
            .collect::<Vec<_>>();
        emit_sharded_module(&functions, &aliases, 1, 0x8000_A450).unwrap()
    }

    #[test]
    fn nw4r_pane_hot_child_edge_profile_wraps_both_shards_transactionally() {
        assert!(!ModuleTranslationOptions::default().profile_nw4r_pane_hot_child_edges);
        let plain = nw4r_pane_hot_child_edge_profile_fixture();
        let mut profiled = plain.clone();
        insert_nw4r_pane_hot_child_edge_profile(&mut profiled, 384 * KIBIBYTE).unwrap();
        let mut changed = 0;
        let mut wrapped = 0;
        for (before, after) in plain.iter().zip(&profiled) {
            assert_eq!(before.name, after.name);
            if before.contents == after.contents {
                continue;
            }
            changed += 1;
            let mut stripped = after.contents.clone();
            for (call_pc, target) in NW4R_PANE_HOT_CHILD_DIRECT_CALLS {
                let return_pc = call_pc + 4;
                let original = format!(
                    "    context->lr = 0x{return_pc:08X}u;\n    context->pc = 0x{target:08X}u;\n    rmge01::fn_{target:08X}(context, memory, services);\n"
                );
                let wrapper = format!(
                    "    context->lr = 0x{return_pc:08X}u;\n    galaxy::call_guest_direct_resolved(services, 0x{target:08X}u, &rmge01::fn_{target:08X}, context, memory, 0x{call_pc:08X}u);\n"
                );
                if stripped.contains(&wrapper) {
                    assert_eq!(stripped.matches(&wrapper).count(), 1);
                    stripped = stripped.replacen(&wrapper, &original, 1);
                    wrapped += 1;
                }
            }
            assert_eq!(stripped, before.contents);
        }
        assert_eq!(changed, 2);
        assert_eq!(wrapped, NW4R_PANE_HOT_CHILD_DIRECT_CALLS.len());

        let mut broken = plain.clone();
        let index = broken
            .iter()
            .position(|file| {
                file.name.starts_with("functions_")
                    && file.name.ends_with(".cpp")
                    && file.contents.contains("\nvoid fn_8000EB28(")
            })
            .unwrap();
        broken[index].contents = broken[index]
            .contents
            .replace("call_return_8000EB80:", "missing_8000EB80:");
        let before = broken
            .iter()
            .map(|file| file.contents.clone())
            .collect::<Vec<_>>();
        assert!(insert_nw4r_pane_hot_child_edge_profile(&mut broken, 384 * KIBIBYTE).is_err());
        assert_eq!(
            before,
            broken
                .iter()
                .map(|file| file.contents.clone())
                .collect::<Vec<_>>()
        );
        assert!(matches!(
            translate_module_with_options(
                Path::new("not-opened"),
                256,
                64,
                384,
                ModuleTranslationOptions {
                    profile_nw4r_pane_hot_child_edges: true,
                    ..Default::default()
                },
            ),
            Err(TranslationError::Nw4rPaneHotChildEdgeProfile(_))
        ));
    }

    fn nw4r_text_format_edge_profile_fixture() -> Vec<GeneratedSource> {
        let callable = NW4R_TEXT_FORMAT_DIRECT_CALLS
            .into_iter()
            .map(|(_, target)| (target, target))
            .collect::<BTreeMap<_, _>>();
        let exact_fpu = callable.keys().copied().collect::<BTreeSet<_>>();
        let mut aliases = Vec::new();
        let functions = [(0x8000_A1C8, 0x8000_A450), (0x8035_0388, 0x8035_10EC)]
            .into_iter()
            .map(|(start, end)| {
                let calls = NW4R_TEXT_FORMAT_DIRECT_CALLS
                    .into_iter()
                    .filter(|(pc, _)| *pc >= start && *pc < end)
                    .collect::<Vec<_>>();
                let mut words = vec![0x6000_0000u32; ((end - start) / 4) as usize];
                *words.last_mut().unwrap() = 0x4E80_0020;
                for (pc, target) in &calls {
                    words[((pc - start) / 4) as usize] =
                        0x4800_0001 | (target.wrapping_sub(*pc) & 0x03FF_FFFC);
                }
                let bytes = words
                    .iter()
                    .flat_map(|word| word.to_be_bytes())
                    .collect::<Vec<_>>();
                let entries = calls.iter().map(|(pc, _)| pc + 4).collect::<BTreeSet<_>>();
                aliases.extend(entries.iter().map(|pc| (*pc, start)));
                let body = lower_words_for_module_with_call_return_entries_and_exact_fpu(
                    start, &bytes, &entries, &entries, &callable, &exact_fpu,
                )
                .unwrap();
                (
                    FunctionRange {
                        address: start,
                        size: end - start,
                    },
                    body,
                )
            })
            .collect::<Vec<_>>();
        emit_sharded_module(&functions, &aliases, 1, 0x8000_A1C8).unwrap()
    }

    #[test]
    fn nw4r_text_format_edge_profile_wraps_both_shards_transactionally() {
        assert!(!ModuleTranslationOptions::default().profile_nw4r_text_format_edges);
        let plain = nw4r_text_format_edge_profile_fixture();
        let mut profiled = plain.clone();
        insert_nw4r_text_format_edge_profile(&mut profiled, 384 * KIBIBYTE).unwrap();
        let mut changed = 0;
        let mut wrapped = 0;
        for (before, after) in plain.iter().zip(&profiled) {
            assert_eq!(before.name, after.name);
            if before.contents == after.contents {
                continue;
            }
            changed += 1;
            let mut stripped = after.contents.clone();
            for (call_pc, target) in NW4R_TEXT_FORMAT_DIRECT_CALLS {
                let return_pc = call_pc + 4;
                let original = format!(
                    "    context->lr = 0x{return_pc:08X}u;\n    context->pc = 0x{target:08X}u;\n    rmge01::fn_{target:08X}(context, memory, services);\n"
                );
                let wrapper = format!(
                    "    context->lr = 0x{return_pc:08X}u;\n    galaxy::call_guest_direct_resolved(services, 0x{target:08X}u, &rmge01::fn_{target:08X}, context, memory, 0x{call_pc:08X}u);\n"
                );
                if stripped.contains(&wrapper) {
                    assert_eq!(stripped.matches(&wrapper).count(), 1);
                    stripped = stripped.replacen(&wrapper, &original, 1);
                    wrapped += 1;
                }
            }
            assert_eq!(stripped, before.contents);
        }
        assert_eq!(changed, 2);
        assert_eq!(wrapped, NW4R_TEXT_FORMAT_DIRECT_CALLS.len());

        let mut broken = plain.clone();
        let index = broken
            .iter()
            .position(|file| {
                file.name.starts_with("functions_")
                    && file.name.ends_with(".cpp")
                    && file.contents.contains("\nvoid fn_80350388(")
            })
            .unwrap();
        broken[index].contents = broken[index]
            .contents
            .replace("call_return_803504E4:", "missing_803504E4:");
        let before = broken
            .iter()
            .map(|file| file.contents.clone())
            .collect::<Vec<_>>();
        assert!(insert_nw4r_text_format_edge_profile(&mut broken, 384 * KIBIBYTE).is_err());
        assert_eq!(
            before,
            broken
                .iter()
                .map(|file| file.contents.clone())
                .collect::<Vec<_>>()
        );
        assert!(matches!(
            translate_module_with_options(
                Path::new("not-opened"),
                256,
                64,
                384,
                ModuleTranslationOptions {
                    profile_nw4r_text_format_edges: true,
                    ..Default::default()
                },
            ),
            Err(TranslationError::Nw4rTextFormatEdgeProfile(_))
        ));
    }

    fn nw4r_text_layout_edge_profile_fixture() -> Vec<GeneratedSource> {
        let callable = NW4R_TEXT_LAYOUT_DIRECT_CALLS
            .into_iter()
            .map(|(_, target)| (target, target))
            .collect::<BTreeMap<_, _>>();
        let exact_fpu = callable.keys().copied().collect::<BTreeSet<_>>();
        let mut aliases = Vec::new();
        let functions = [
            (0x8000_94F8, 0x8000_9628),
            (0x8000_9628, 0x8000_98A4),
            (0x8000_A870, 0x8000_AF00),
            (0x8000_AF00, 0x8000_B020),
            (0x8000_B020, 0x8000_B76C),
        ]
        .into_iter()
        .map(|(start, end)| {
            let calls = NW4R_TEXT_LAYOUT_DIRECT_CALLS
                .into_iter()
                .filter(|(pc, _)| *pc >= start && *pc < end)
                .collect::<Vec<_>>();
            let mut words = vec![0x6000_0000u32; ((end - start) / 4) as usize];
            *words.last_mut().unwrap() = 0x4E80_0020;
            for (pc, target) in &calls {
                words[((pc - start) / 4) as usize] =
                    0x4800_0001 | (target.wrapping_sub(*pc) & 0x03FF_FFFC);
            }
            let bytes = words
                .iter()
                .flat_map(|word| word.to_be_bytes())
                .collect::<Vec<_>>();
            let entries = calls.iter().map(|(pc, _)| pc + 4).collect::<BTreeSet<_>>();
            aliases.extend(entries.iter().map(|pc| (*pc, start)));
            let body = lower_words_for_module_with_call_return_entries_and_exact_fpu(
                start, &bytes, &entries, &entries, &callable, &exact_fpu,
            )
            .unwrap();
            (
                FunctionRange {
                    address: start,
                    size: end - start,
                },
                body,
            )
        })
        .collect::<Vec<_>>();
        emit_sharded_module(&functions, &aliases, 64, 0x8000_94F8).unwrap()
    }

    #[test]
    fn nw4r_text_layout_edge_profile_groups_shared_shards_transactionally() {
        assert!(!ModuleTranslationOptions::default().profile_nw4r_text_layout_edges);
        let plain = nw4r_text_layout_edge_profile_fixture();
        let mut profiled = plain.clone();
        insert_nw4r_text_layout_edge_profile(&mut profiled, 384 * KIBIBYTE).unwrap();
        let mut changed = 0;
        let mut wrapped = 0;
        for (before, after) in plain.iter().zip(&profiled) {
            assert_eq!(before.name, after.name);
            if before.contents == after.contents {
                continue;
            }
            changed += 1;
            let mut stripped = after.contents.clone();
            for (call_pc, target) in NW4R_TEXT_LAYOUT_DIRECT_CALLS {
                let return_pc = call_pc + 4;
                let original = format!(
                    "    context->lr = 0x{return_pc:08X}u;\n    context->pc = 0x{target:08X}u;\n    rmge01::fn_{target:08X}(context, memory, services);\n"
                );
                let wrapper = format!(
                    "    context->lr = 0x{return_pc:08X}u;\n    galaxy::call_guest_direct_resolved(services, 0x{target:08X}u, &rmge01::fn_{target:08X}, context, memory, 0x{call_pc:08X}u);\n"
                );
                if stripped.contains(&wrapper) {
                    assert_eq!(stripped.matches(&wrapper).count(), 1);
                    stripped = stripped.replacen(&wrapper, &original, 1);
                    wrapped += 1;
                }
            }
            assert_eq!(stripped, before.contents);
        }
        assert_eq!(changed, 1);
        assert_eq!(wrapped, NW4R_TEXT_LAYOUT_DIRECT_CALLS.len());

        let mut broken = plain.clone();
        let index = broken
            .iter()
            .position(|file| {
                file.name.starts_with("functions_")
                    && file.name.ends_with(".cpp")
                    && file.contents.contains("\nvoid fn_8000B020(")
            })
            .unwrap();
        broken[index].contents = broken[index]
            .contents
            .replace("call_return_8000B23C:", "missing_8000B23C:");
        let before = broken
            .iter()
            .map(|file| file.contents.clone())
            .collect::<Vec<_>>();
        assert!(insert_nw4r_text_layout_edge_profile(&mut broken, 384 * KIBIBYTE).is_err());
        assert_eq!(
            before,
            broken
                .iter()
                .map(|file| file.contents.clone())
                .collect::<Vec<_>>()
        );
        assert!(matches!(
            translate_module_with_options(
                Path::new("not-opened"),
                256,
                64,
                384,
                ModuleTranslationOptions {
                    profile_nw4r_text_layout_edges: true,
                    ..Default::default()
                },
            ),
            Err(TranslationError::Nw4rTextLayoutEdgeProfile(_))
        ));
    }

    fn jpa_hot_draw_edge_profile_fixture() -> Vec<GeneratedSource> {
        let callable = JPA_HOT_DRAW_DIRECT_CALLS
            .into_iter()
            .map(|(_, target)| (target, target))
            .collect::<BTreeMap<_, _>>();
        let exact_fpu = callable.keys().copied().collect::<BTreeSet<_>>();
        let start = 0x803A_3B6C;
        let end = 0x803A_3D18;
        let mut words = vec![0x6000_0000u32; ((end - start) / 4) as usize];
        *words.last_mut().unwrap() = 0x4E80_0020;
        for (pc, target) in JPA_HOT_DRAW_DIRECT_CALLS {
            words[((pc - start) / 4) as usize] =
                0x4800_0001 | (target.wrapping_sub(pc) & 0x03FF_FFFC);
        }
        // Preserve the two anchors unique to the measured real region: its
        // first FPU instruction and the indirect projection callback which
        // ends the region. Other fixture instructions intentionally remain
        // NOPs so this test covers structure rather than game behavior.
        words[((JPA_HOT_DRAW_BODY_REGION_START - start) / 4) as usize] = 0xC07D_0144;
        words[((JPA_HOT_DRAW_BODY_REGION_END - start) / 4) as usize] = 0x4E80_0421;
        let bytes = words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect::<Vec<_>>();
        let mut entries = JPA_HOT_DRAW_DIRECT_CALLS
            .into_iter()
            .map(|(pc, _)| pc + 4)
            .collect::<BTreeSet<_>>();
        entries.insert(JPA_HOT_DRAW_BODY_REGION_END + 4);
        let aliases = entries.iter().map(|pc| (*pc, start)).collect::<Vec<_>>();
        let body = lower_words_for_module_with_call_return_entries_and_exact_fpu(
            start, &bytes, &entries, &entries, &callable, &exact_fpu,
        )
        .unwrap();
        emit_sharded_module(
            &[
                (
                    FunctionRange {
                        address: start - 0x0C,
                        size: 4,
                    },
                    "    return;\n".to_owned(),
                ),
                (
                    FunctionRange {
                        address: start,
                        size: end - start,
                    },
                    body,
                ),
            ],
            &aliases,
            64,
            start - 0x0C,
        )
        .unwrap()
    }

    #[test]
    fn jpa_hot_draw_edge_profile_is_exact_reversible_and_transactional() {
        assert!(!ModuleTranslationOptions::default().profile_jpa_hot_draw_edges);
        let plain = jpa_hot_draw_edge_profile_fixture();
        let mut profiled = plain.clone();
        insert_jpa_hot_draw_edge_profile(&mut profiled, 384 * KIBIBYTE).unwrap();
        let mut changed = 0;
        let mut wrapped = 0;
        for (before, after) in plain.iter().zip(&profiled) {
            assert_eq!(before.name, after.name);
            if before.contents == after.contents {
                continue;
            }
            changed += 1;
            let mut stripped = after.contents.clone();
            for (call_pc, target) in JPA_HOT_DRAW_DIRECT_CALLS {
                let return_pc = call_pc + 4;
                let original = format!(
                    "    context->lr = 0x{return_pc:08X}u;\n    context->pc = 0x{target:08X}u;\n    rmge01::fn_{target:08X}(context, memory, services);\n"
                );
                let wrapper = format!(
                    "    context->lr = 0x{return_pc:08X}u;\n    galaxy::call_guest_direct_resolved(services, 0x{target:08X}u, &rmge01::fn_{target:08X}, context, memory, 0x{call_pc:08X}u);\n"
                );
                if stripped.contains(&wrapper) {
                    assert_eq!(stripped.matches(&wrapper).count(), 1);
                    stripped = stripped.replacen(&wrapper, &original, 1);
                    wrapped += 1;
                }
            }
            let declarations = jpa_hot_draw_body_profile_declarations();
            let begin = jpa_hot_draw_body_profile_begin();
            let end = jpa_hot_draw_body_profile_end();
            assert_eq!(stripped.matches(declarations).count(), 1);
            assert_eq!(stripped.matches(begin).count(), 1);
            assert_eq!(stripped.matches(&end).count(), 1);
            stripped = stripped.replacen(declarations, "", 1);
            stripped = stripped.replacen(begin, "", 1);
            stripped = stripped.replacen(&end, "", 1);
            assert_eq!(stripped, before.contents);
        }
        assert_eq!(changed, 1);
        assert_eq!(wrapped, JPA_HOT_DRAW_DIRECT_CALLS.len());

        let mut broken = plain.clone();
        let index = broken
            .iter()
            .position(|file| {
                file.name.starts_with("functions_")
                    && file.name.ends_with(".cpp")
                    && file.contents.contains("\nvoid fn_803A3B6C(")
            })
            .unwrap();
        let (call_pc, target) = JPA_HOT_DRAW_DIRECT_CALLS[0];
        let return_pc = call_pc + 4;
        let call = format!(
            "    context->lr = 0x{return_pc:08X}u;\n    context->pc = 0x{target:08X}u;\n    rmge01::fn_{target:08X}(context, memory, services);\n"
        );
        assert_eq!(broken[index].contents.matches(&call).count(), 1);
        broken[index].contents = broken[index].contents.replacen(
            &call,
            "    // deliberately missing direct-call anchor\n",
            1,
        );
        let before = broken
            .iter()
            .map(|file| file.contents.clone())
            .collect::<Vec<_>>();
        assert!(insert_jpa_hot_draw_edge_profile(&mut broken, 384 * KIBIBYTE).is_err());
        assert_eq!(
            before,
            broken
                .iter()
                .map(|file| file.contents.clone())
                .collect::<Vec<_>>()
        );
        assert!(matches!(
            translate_module_with_options(
                Path::new("not-opened"),
                256,
                64,
                384,
                ModuleTranslationOptions {
                    profile_jpa_hot_draw_edges: true,
                    ..Default::default()
                },
            ),
            Err(TranslationError::JpaHotDrawEdgeProfile(_))
        ));
    }

    fn top_self_closure_edge_profile_fixture() -> Vec<GeneratedSource> {
        let callable = TOP_SELF_CLOSURE_DIRECT_CALLS
            .into_iter()
            .map(|(_, target)| (target, target))
            .collect::<BTreeMap<_, _>>();
        let exact_fpu = callable.keys().copied().collect::<BTreeSet<_>>();
        let mut aliases = Vec::new();
        let functions = [
            (0x8000_C0E0, 0x8000_C18C, 0..1),
            (0x804E_BD50, 0x804E_BEEC, 1..15),
            (0x804F_28B0, 0x804F_29FC, 15..23),
            (0x800C_8C64, 0x800C_8D4C, 23..33),
            (0x8016_5478, 0x8016_5594, 33..46),
            (0x802B_5EE8, 0x802B_6110, 46..73),
            (0x8036_61A4, 0x8036_61E0, 73..75),
        ]
        .into_iter()
        .map(|(start, end, call_indexes)| {
            let mut words = vec![0x6000_0000u32; ((end - start) / 4) as usize];
            *words.last_mut().unwrap() = 0x4E80_0020;
            let mut entries = BTreeSet::new();
            for call_index in call_indexes {
                let (pc, target) = TOP_SELF_CLOSURE_DIRECT_CALLS[call_index];
                words[((pc - start) / 4) as usize] =
                    0x4800_0001 | (target.wrapping_sub(pc) & 0x03FF_FFFC);
                entries.insert(pc + 4);
                aliases.push((pc + 4, start));
            }
            let bytes = words
                .iter()
                .flat_map(|word| word.to_be_bytes())
                .collect::<Vec<_>>();
            let body = lower_words_for_module_with_call_return_entries_and_exact_fpu(
                start, &bytes, &entries, &entries, &callable, &exact_fpu,
            )
            .unwrap();
            (
                FunctionRange {
                    address: start,
                    size: end - start,
                },
                body,
            )
        })
        .collect::<Vec<_>>();
        emit_sharded_module(&functions, &aliases, 1, 0x8000_C0E0).unwrap()
    }

    #[test]
    fn top_self_closure_edge_profile_is_exact_reversible_and_transactional() {
        assert!(!ModuleTranslationOptions::default().profile_top_self_closure_edges);
        let plain = top_self_closure_edge_profile_fixture();
        let mut profiled = plain.clone();
        insert_top_self_closure_edge_profile(&mut profiled, 384 * KIBIBYTE).unwrap();
        let mut changed = 0;
        let mut wrapped = 0;
        for (before, after) in plain.iter().zip(&profiled) {
            assert_eq!(before.name, after.name);
            if before.contents == after.contents {
                continue;
            }
            changed += 1;
            let mut stripped = after.contents.clone();
            const INCLUDE: &str = "#include \"galaxy/crcp.h\"\n";
            assert_eq!(stripped.matches(INCLUDE).count(), 1);
            stripped = stripped.replacen(INCLUDE, "", 1);
            for (call_pc, target) in TOP_SELF_CLOSURE_DIRECT_CALLS {
                let return_pc = call_pc + 4;
                let original = format!(
                    "    context->lr = 0x{return_pc:08X}u;\n    context->pc = 0x{target:08X}u;\n    rmge01::fn_{target:08X}(context, memory, services);\n"
                );
                let wrapper = format!(
                    "    galaxy::call_guest_direct_resolved(services,0x{target:08X}u,&fn_{target:08X},(context->lr=0x{return_pc:08X}u,context),memory,0x{call_pc:08X}u);\n"
                );
                if stripped.contains(&wrapper) {
                    assert_eq!(stripped.matches(&wrapper).count(), 1);
                    stripped = stripped.replacen(&wrapper, &original, 1);
                    let checkpoint = format!(
                        "    galaxy::call_return_checkpoint(services, 0x{call_pc:08X}u, 0x{return_pc:08X}u, context, memory);\n"
                    );
                    let profiled_checkpoint = format!(
                        "    galaxy::crcp(services,0x{call_pc:08X}u,0x{return_pc:08X}u,context,memory);\n"
                    );
                    assert_eq!(stripped.matches(&profiled_checkpoint).count(), 2);
                    stripped = stripped.replace(&profiled_checkpoint, &checkpoint);
                    wrapped += 1;
                }
            }
            assert_eq!(stripped, before.contents);
        }
        assert_eq!(changed, 7);
        assert_eq!(wrapped, TOP_SELF_CLOSURE_DIRECT_CALLS.len());

        let mut broken = plain.clone();
        let index = broken
            .iter()
            .position(|file| {
                file.name.starts_with("functions_")
                    && file.name.ends_with(".cpp")
                    && file.contents.contains("\nvoid fn_804F28B0(")
            })
            .unwrap();
        let (call_pc, target) = TOP_SELF_CLOSURE_DIRECT_CALLS[15];
        let return_pc = call_pc + 4;
        let call = format!(
            "    context->lr = 0x{return_pc:08X}u;\n    context->pc = 0x{target:08X}u;\n    rmge01::fn_{target:08X}(context, memory, services);\n"
        );
        assert_eq!(broken[index].contents.matches(&call).count(), 1);
        broken[index].contents = broken[index].contents.replacen(
            &call,
            "    // deliberately missing direct-call anchor\n",
            1,
        );
        let before = broken
            .iter()
            .map(|file| file.contents.clone())
            .collect::<Vec<_>>();
        assert!(insert_top_self_closure_edge_profile(&mut broken, 384 * KIBIBYTE).is_err());
        assert_eq!(
            before,
            broken
                .iter()
                .map(|file| file.contents.clone())
                .collect::<Vec<_>>()
        );
        assert!(matches!(
            translate_module_with_options(
                Path::new("not-opened"),
                256,
                64,
                384,
                ModuleTranslationOptions {
                    profile_top_self_closure_edges: true,
                    ..Default::default()
                },
            ),
            Err(TranslationError::TopSelfClosureEdgeProfile(_))
        ));
    }

    fn end_frame_span_fixture() -> Vec<GeneratedSource> {
        // Synthetic ordinary lowerings exercise actual interior dispatch and
        // call-return checkpoint labels without embedding game implementation.
        let functions = [(0x803A_00FC, 0x803A_0158), (0x803A_08A0, 0x803A_0944)]
            .into_iter()
            .map(|(start, end)| {
                let mut words = vec![0x6000_0000u32; ((end - start) / 4) as usize];
                *words.last_mut().unwrap() = 0x4E80_0020;
                let bytes = words
                    .iter()
                    .flat_map(|word| word.to_be_bytes())
                    .collect::<Vec<_>>();
                let entries = END_FRAME_SPAN_LABELS
                    .into_iter()
                    .filter(|pc| *pc >= start && *pc < end)
                    .collect::<BTreeSet<_>>();
                let body = lower_words_with_options(
                    start, &bytes, &entries, &entries, None, None, None, false,
                )
                .unwrap();
                (
                    FunctionRange {
                        address: start,
                        size: end - start,
                    },
                    body,
                )
            })
            .chain(std::iter::once((
                FunctionRange {
                    address: 0x803A_1000,
                    size: 4,
                },
                "    return;\n".to_owned(),
            )))
            .collect::<Vec<_>>();
        emit_sharded_module(&functions, &[], 2, 0x803A_00FC).unwrap()
    }

    #[test]
    fn end_frame_span_trace_preserves_shards_and_resume_paths() {
        assert!(!ModuleTranslationOptions::default().trace_end_frame_spans);
        let plain = end_frame_span_fixture();
        let mut traced = plain.clone();
        insert_end_frame_span_markers(&mut traced, 384 * KIBIBYTE).unwrap();
        assert_eq!(plain.len(), traced.len());
        let mut changed = 0;
        for (before, after) in plain.iter().zip(&traced) {
            assert_eq!(before.name, after.name);
            if before.contents != after.contents {
                changed += 1;
                let mut stripped = after.contents.clone();
                for pc in END_FRAME_SPAN_LABELS {
                    let hook = format!("    galaxy::trace_main_frame_stage(services, \"__galaxy_main_frame_stage:end-frame-{pc:08X}\");\n");
                    assert_eq!(stripped.matches(&hook).count(), 1);
                    assert!(stripped.contains(&format!("label_{pc:08X}:\n{hook}")));
                    assert!(stripped.contains(&format!("goto label_{pc:08X};")));
                    assert!(stripped.contains(&format!("call_return_{pc:08X}:")));
                    stripped = stripped.replace(&hook, "");
                }
                assert_eq!(stripped, before.contents);
            }
        }
        assert_eq!(changed, 1);
    }

    #[test]
    fn end_frame_span_trace_rejects_partial_missing_ambiguous_and_oversize_sources() {
        let error = translate_module_with_options(
            Path::new("not-opened"),
            256,
            64,
            384,
            ModuleTranslationOptions {
                trace_end_frame_spans: true,
                ..Default::default()
            },
        )
        .unwrap_err();
        assert!(matches!(error, TranslationError::EndFrameSpanTrace(_)));
        let original = end_frame_span_fixture();
        for mode in 0..4 {
            let mut files = original.clone();
            let index = files
                .iter()
                .position(|file| file.contents.contains("\nlabel_803A0140:\n"))
                .unwrap();
            let budget = files[index].contents.len();
            match mode {
                0 => {
                    files[index].contents = files[index]
                        .contents
                        .replace("\nlabel_803A0140:\n", "\nmissing:\n")
                }
                1 => files[index].contents.push_str("\nlabel_803A0140:\n"),
                2 => {
                    files[index].contents = files[index]
                        .contents
                        .replace("\nlabel_803A0140:\n", "\nmissing:\n");
                    files.push(GeneratedSource {
                        name: "functions_9999.cpp".to_owned(),
                        contents: "\nlabel_803A0140:\n".to_owned(),
                    });
                }
                _ => (),
            }
            let before = files
                .iter()
                .map(|file| file.contents.clone())
                .collect::<Vec<_>>();
            assert!(insert_end_frame_span_markers(
                &mut files,
                if mode == 3 { budget } else { 384 * KIBIBYTE }
            )
            .is_err());
            assert_eq!(
                before,
                files
                    .iter()
                    .map(|file| file.contents.clone())
                    .collect::<Vec<_>>()
            );
        }
    }

    #[test]
    fn main_frame_stage_markers_are_exact_game_scene_continuations() {
        let expected = [
            (
                0x8033_E7E0,
                0x8033_E7F8,
                "__galaxy_main_frame_stage:after-803CA428",
            ),
            (
                0x8033_E7E0,
                0x8033_E7FC,
                "__galaxy_main_frame_stage:after-80187E2C",
            ),
            (
                0x8033_E7E0,
                0x8033_E804,
                "__galaxy_main_frame_stage:after-8033F120",
            ),
            (
                0x8033_E7E0,
                0x8033_E80C,
                "__galaxy_main_frame_stage:after-80344894-4F",
            ),
            (
                0x8033_E7E0,
                0x8033_E814,
                "__galaxy_main_frame_stage:after-80344894-50",
            ),
            (
                0x8033_E7E0,
                0x8033_E81C,
                "__galaxy_main_frame_stage:after-8033EDF8",
            ),
            (
                0x8033_E7E0,
                0x8033_E824,
                "__galaxy_main_frame_stage:after-8033EE8C",
            ),
            (
                0x8033_E7E0,
                0x8033_E82C,
                "__galaxy_main_frame_stage:after-8033F040",
            ),
            (
                0x8033_E7E0,
                0x8033_E830,
                "__galaxy_main_frame_stage:after-803CA474",
            ),
            (
                0x8033_EE8C,
                0x8033_EEB8,
                "__galaxy_main_frame_stage:draw3d-after-803F7E40",
            ),
            (
                0x8033_EE8C,
                0x8033_EED8,
                "__galaxy_main_frame_stage:draw3d-after-803A29B8",
            ),
            (
                0x8033_EE8C,
                0x8033_EF00,
                "__galaxy_main_frame_stage:draw3d-after-803CA5F4",
            ),
            (
                0x8033_EE8C,
                0x8033_EF1C,
                "__galaxy_main_frame_stage:draw3d-after-804BDC78-1",
            ),
            (
                0x8033_EE8C,
                0x8033_EF38,
                "__galaxy_main_frame_stage:draw3d-after-80343E44",
            ),
            (
                0x8033_EE8C,
                0x8033_EF5C,
                "__galaxy_main_frame_stage:draw3d-after-804BDC78-2",
            ),
            (
                0x8033_EE8C,
                0x8033_EF70,
                "__galaxy_main_frame_stage:draw3d-after-80344224",
            ),
            (
                0x8033_EE8C,
                0x8033_EF90,
                "__galaxy_main_frame_stage:draw3d-after-804BDC78-3",
            ),
            (
                0x8033_EE8C,
                0x8033_EFA8,
                "__galaxy_main_frame_stage:draw3d-after-8034419C",
            ),
            (
                0x8033_EE8C,
                0x8033_EFB0,
                "__galaxy_main_frame_stage:draw3d-after-80344990-18",
            ),
            (
                0x8033_EE8C,
                0x8033_EFB8,
                "__galaxy_main_frame_stage:draw3d-after-80344894-26",
            ),
            (
                0x8033_EE8C,
                0x8033_EFC0,
                "__galaxy_main_frame_stage:draw3d-after-80344894-47",
            ),
            (
                0x8033_EE8C,
                0x8033_EFC8,
                "__galaxy_main_frame_stage:draw3d-after-80344894-4C",
            ),
            (
                0x8033_EE8C,
                0x8033_EFD0,
                "__galaxy_main_frame_stage:draw3d-after-80344894-2F",
            ),
            (
                0x8033_EE8C,
                0x8033_EFD8,
                "__galaxy_main_frame_stage:draw3d-after-80344894-2D",
            ),
            (
                0x8033_EE8C,
                0x8033_EFDC,
                "__galaxy_main_frame_stage:draw3d-after-8034424C",
            ),
            (
                0x8033_EE8C,
                0x8033_F000,
                "__galaxy_main_frame_stage:draw3d-after-803C7360-2",
            ),
            (
                0x8033_EE8C,
                0x8033_F014,
                "__galaxy_main_frame_stage:draw3d-after-803D8A44",
            ),
            (
                0x8033_EE8C,
                0x8033_F028,
                "__galaxy_main_frame_stage:draw3d-after-803CA76C",
            ),
            (
                0x8034_424C,
                0x8034_4260,
                "__galaxy_main_frame_stage:draw3d-80344954-category-1D",
            ),
            (
                0x8034_424C,
                0x8034_4268,
                "__galaxy_main_frame_stage:draw3d-80344954-category-19",
            ),
            (
                0x8034_424C,
                0x8034_4270,
                "__galaxy_main_frame_stage:draw3d-80344954-category-1A",
            ),
            (
                0x8034_424C,
                0x8034_4278,
                "__galaxy_main_frame_stage:draw3d-80344954-category-1B",
            ),
            (
                0x8034_424C,
                0x8034_4280,
                "__galaxy_main_frame_stage:draw3d-80344954-category-1C",
            ),
            (
                0x8034_424C,
                0x8034_4288,
                "__galaxy_main_frame_stage:draw3d-80344954-category-22",
            ),
            (
                0x8034_424C,
                0x8034_4290,
                "__galaxy_main_frame_stage:draw3d-80344954-category-17",
            ),
            (
                0x8034_424C,
                0x8034_4298,
                "__galaxy_main_frame_stage:draw3d-80344954-category-16",
            ),
            (
                0x800C_9248,
                0x800C_9260,
                "__galaxy_main_frame_stage:particle-800C9248-after-803C73CC",
            ),
            (
                0x800C_9248,
                0x800C_926C,
                "__galaxy_main_frame_stage:particle-800C9248-after-800C95C4",
            ),
            (
                0x800C_95C4,
                0x800C_95F0,
                "__galaxy_main_frame_stage:particle-800C95C4-after-psmtx-copy",
            ),
            (
                0x800C_95C4,
                0x800C_95F8,
                "__galaxy_main_frame_stage:particle-800C95C4-after-psmtx-identity",
            ),
            (
                0x800C_95C4,
                0x800C_960C,
                "__galaxy_main_frame_stage:particle-800C95C4-after-80448790-array-0",
            ),
            (
                0x800C_95C4,
                0x800C_9614,
                "__galaxy_main_frame_stage:particle-800C95C4-after-gx-set-array-0",
            ),
            (
                0x800C_95C4,
                0x800C_9628,
                "__galaxy_main_frame_stage:particle-800C95C4-after-80448790-array-1",
            ),
            (
                0x800C_95C4,
                0x800C_9630,
                "__galaxy_main_frame_stage:particle-800C95C4-after-gx-set-array-1",
            ),
            (
                0x8044_8790,
                0x8044_87D8,
                "__galaxy_main_frame_stage:particle-80448790-after-preparation",
            ),
            (
                0x8044_8790,
                0x8044_891C,
                "__galaxy_main_frame_stage:particle-80448790-after-gx-setup",
            ),
            (
                0x8044_8790,
                0x8044_8974,
                "__galaxy_main_frame_stage:particle-80448790-after-callback-loop",
            ),
        ];
        for (function_address, pc, marker) in expected {
            assert_eq!(main_frame_stage_marker(function_address, pc), Some(marker));
            let mut output = String::new();
            emit_main_frame_stage_hook(&mut output, function_address, pc)
                .expect("stage trace emits");
            assert_eq!(
                output,
                format!("    galaxy::trace_main_frame_stage(services, \"{marker}\");\n")
            );
        }
        assert_eq!(main_frame_stage_marker(0x8033_E7E0, 0x8033_E7F4), None);
        assert_eq!(main_frame_stage_marker(0x8033_EE8C, 0x8033_EEBC), None);
        assert_eq!(main_frame_stage_marker(0x8034_424C, 0x8034_4264), None);
        assert_eq!(main_frame_stage_marker(0x800C_9248, 0x800C_9264), None);
        assert_eq!(main_frame_stage_marker(0x800C_9280, 0x800C_9260), None);
        assert_eq!(main_frame_stage_marker(0x800C_95C4, 0x800C_9608), None);
        assert_eq!(main_frame_stage_marker(0x8044_8790, 0x8044_8968), None);
        assert_eq!(main_frame_stage_marker(0x8033_E844, 0x8033_E7F8), None);
    }

    #[test]
    fn generated_module_traces_only_verified_jutvideo_message_queue_entries() {
        for address in [0x8041_9554, 0x804A_88E4, 0x804A_89AC, 0x804A_8A88] {
            let function = FunctionRange {
                address,
                size: 0x20,
            };
            let files = emit_sharded_module(
                &[(function, "    return;\n".to_owned())],
                &[],
                1,
                function.address,
            )
            .expect("module generation succeeds");
            let shard = files
                .iter()
                .find(|file| file.name == "functions_0000.cpp")
                .expect("function shard exists");
            assert!(
                shard.contents.contains(&format!(
                    "if (context->pc == 0x{address:08X}u) {{\n        galaxy::trace_jutvideo_mq_function_entry(services, context, memory, 0x{address:08X}u);"
                )),
                "verified JUTVideo message-queue entry 0x{address:08X} must trace only its true function entry"
            );
        }

        let unrelated = FunctionRange {
            address: 0x804A_88E8,
            size: 0x20,
        };
        let files = emit_sharded_module(
            &[(unrelated, "    return;\n".to_owned())],
            &[],
            1,
            unrelated.address,
        )
        .expect("module generation succeeds");
        let shard = files
            .iter()
            .find(|file| file.name == "functions_0000.cpp")
            .expect("function shard exists");
        assert!(
            !shard.contents.contains("trace_jutvideo_mq_function_entry("),
            "nearby non-entry address must not be traced"
        );
    }

    #[test]
    fn generated_module_traces_verified_rmge01_jaudio_entries_only() {
        for address in [
            0x8049_2A08,
            0x8049_5038,
            0x8049_5150,
            0x8049_559C,
            0x8049_5900,
            0x8049_80F0,
            0x8049_9208,
            0x8049_AEF8,
        ] {
            let function = FunctionRange {
                address,
                size: 0x20,
            };
            let files = emit_sharded_module(
                &[(function, "    return;\n".to_owned())],
                &[],
                1,
                function.address,
            )
            .expect("module generation succeeds");
            let shard = files
                .iter()
                .find(|file| file.name == "functions_0000.cpp")
                .expect("function shard exists");
            assert!(
                shard
                    .contents
                    .contains(&format!("memory, 0x{address:08X}u);")),
                "verified RMGE01 JAudio entry 0x{address:08X} must be traced"
            );
        }

        for address in [
            0x8002_7024,
            0x8002_AA0C,
            0x803F_4720,
            0x803F_B3FC,
            0x8049_727C,
            0x8049_A2F4,
            0x8049_B448,
        ] {
            let foreign_layout_address = FunctionRange {
                address,
                size: 0x08,
            };
            let files = emit_sharded_module(
                &[(foreign_layout_address, "    return;\n".to_owned())],
                &[],
                1,
                foreign_layout_address.address,
            )
            .expect("module generation succeeds");
            let shard = files
                .iter()
                .find(|file| file.name == "functions_0000.cpp")
                .expect("function shard exists");
            assert!(
                !shard.contents.contains("trace_audio_function_entry("),
                "foreign-layout address 0x{address:08X} is unrelated in RMGE01"
            );
        }
    }

    #[test]
    fn generated_module_traces_verified_rmge01_audio_control_path() {
        for address in [
            0x8002_5FC0,
            0x8002_6158,
            0x8002_6334,
            0x8002_6C34,
            0x8002_FC48,
            0x8002_FD04,
            0x8003_3238,
            0x8003_34EC,
            0x8039_4B78,
            0x8039_4E54,
            0x8039_4E64,
            0x8039_B1C4,
            0x8039_B1E8,
            0x8039_B938,
            0x8039_E3D4,
            0x8048_9C1C,
            0x8048_9CEC,
            0x8048_D8CC,
            0x8048_D970,
            0x8049_8830,
            0x8049_9358,
            0x8049_94F8,
            0x8049_EFF4,
            0x8049_F130,
            0x8049_F170,
        ] {
            let function = FunctionRange {
                address,
                size: 0x20,
            };
            let files = emit_sharded_module(
                &[(function, "    return;\n".to_owned())],
                &[],
                1,
                function.address,
            )
            .expect("module generation succeeds");
            let shard = files
                .iter()
                .find(|file| file.name == "functions_0000.cpp")
                .expect("function shard exists");
            assert!(
                shard
                    .contents
                    .contains(&format!("memory, 0x{address:08X}u);")),
                "verified RMGE01 audio control-path entry 0x{address:08X} must be traced"
            );
            assert_eq!(
                shard.contents.contains("trace_audio_entry_count"),
                should_sample_audio_trace_hook(address),
                "RMGE01 audio control-path entry 0x{address:08X} sampling classification drifted"
            );
        }
    }

    #[test]
    fn generated_module_traces_known_file_select_function_entries() {
        let function = FunctionRange {
            address: 0x8017_D148,
            size: 0xC4,
        };
        let files = emit_sharded_module(
            &[(function, "    return;\n".to_owned())],
            &[],
            1,
            function.address,
        )
        .expect("module generation succeeds");
        let shard = files
            .iter()
            .find(|file| file.name == "functions_0000.cpp")
            .expect("function shard exists");
        assert!(shard.contents.contains(
            "galaxy::trace_file_select_function_entry(services, context, memory, context->pc);"
        ));
    }

    #[test]
    fn file_select_trace_hooks_keep_the_verified_selector_callbacks_exact() {
        // These are the RMGE01 FileSelector/FileSelectItem entries consumed by
        // the strict native route.  In particular, onPoint/onSelect receive the
        // item in r4; the old control-loop labels must never become selection
        // evidence again.
        for address in [
            0x8017_85EC, // FileSelectItem::onPointing
            0x8017_866C, // FileSelectItem::offPointing
            0x8017_8A14, // FileSelectItem::control
            0x8017_8E64, // FileSelectItem::updatePointing
            0x8017_8F5C, // FileSelectItem::updateRotate
            0x8017_AB94, // FileSelector::control
            0x8017_BA08, // FileSelector::onPoint(FileSelectItem *)
            0x8017_BA84, // FileSelector::onSelect(FileSelectItem *)
            0x8017_CA60, // FileSelector::exeFileSelectStart
            0x8017_CAC4, // FileSelector::exeFileSelect
            0x8017_CB1C, // FileSelector::exeFileConfirmStart
            0x8017_CD18, // FileSelector::exeFileConfirm
            0x803B_4D84, // GameSequenceFunction::startGameDataLoadSequence
        ] {
            assert!(
                should_emit_file_select_trace_hook(address),
                "verified file-select trace address 0x{address:08X} was omitted"
            );
        }

        for address in [
            0x8017_92D4, // historical, incorrect FileSelectItem label
            0x8017_93CC, // historical, incorrect FileSelectItem label
            0x8017_BDD4, // FileSelector::calcBasePos, not onSelect
            0x8017_BFC4, // interior calcBasePos address, not onSelect
            0x803B_62E8, // SaveDataBannerCreator setup, not game-data load
        ] {
            assert!(
                !should_emit_file_select_trace_hook(address),
                "obsolete file-select trace address 0x{address:08X} was restored"
            );
        }
    }

    #[test]
    fn generated_module_traces_file_select_play_button_gate_entries() {
        let function = FunctionRange {
            address: 0x8034_BA9C,
            size: 0x8,
        };
        let files = emit_sharded_module(
            &[(function, "    return;\n".to_owned())],
            &[],
            1,
            function.address,
        )
        .expect("module generation succeeds");
        let shard = files
            .iter()
            .find(|file| file.name == "functions_0000.cpp")
            .expect("function shard exists");
        assert!(shard.contents.contains(
            "galaxy::trace_file_select_function_entry(services, context, memory, context->pc);"
        ));
    }

    #[test]
    fn generated_module_traces_known_movie_function_entries() {
        let function = FunctionRange {
            address: 0x8038_D718,
            size: 0xA0,
        };
        let files = emit_sharded_module(
            &[(function, "    return;\n".to_owned())],
            &[],
            1,
            function.address,
        )
        .expect("module generation succeeds");
        let shard = files
            .iter()
            .find(|file| file.name == "functions_0000.cpp")
            .expect("function shard exists");
        assert!(shard.contents.contains(
            "galaxy::trace_movie_function_entry(services, context, memory, context->pc);"
        ));
    }

    #[test]
    fn sharded_module_keeps_file_selector_create_translated() {
        let functions = [
            FunctionRange {
                address: 0x8017_D148,
                size: 0x84,
            },
            FunctionRange {
                address: 0x8017_B8C0,
                size: 0x04,
            },
            FunctionRange {
                address: 0x8017_BB40,
                size: 0x04,
            },
            FunctionRange {
                address: 0x8016_5824,
                size: 0x04,
            },
            FunctionRange {
                address: 0x8036_DA1C,
                size: 0x04,
            },
            FunctionRange {
                address: 0x803B_4DC8,
                size: 0x04,
            },
            FunctionRange {
                address: 0x803B_4F84,
                size: 0x04,
            },
            FunctionRange {
                address: 0x803B_4FA8,
                size: 0x04,
            },
            FunctionRange {
                address: 0x803D_C6EC,
                size: 0x04,
            },
        ];
        let bodies = functions
            .iter()
            .copied()
            .map(|function| (function, "    return;\n".to_owned()))
            .collect::<Vec<_>>();
        let files =
            emit_sharded_module(&bodies, &[], 32, 0x8017_D148).expect("module generation succeeds");
        let shard = files
            .iter()
            .find(|file| {
                file.name.starts_with("functions_") && file.contents.contains("void fn_8017D148")
            })
            .expect("function shard exists");

        assert!(shard.contents.contains("void fn_8017D148"));
        assert!(shard.contents.contains("    return;\n"));
        assert!(!shard.contents.contains("mii_select_start_first_nerve"));
        assert!(!shard.contents.contains("mii_create_wait_nerve"));
        assert!(!shard.contents.contains("context->pc = 0x8036DA1Cu;"));
    }

    #[test]
    fn sharded_module_keeps_mii_select_functions_translated() {
        let functions = [
            FunctionRange {
                address: 0x8036_DA98,
                size: 0x5C,
            },
            FunctionRange {
                address: 0x8036_DA0C,
                size: 0x08,
            },
            FunctionRange {
                address: 0x8036_DA14,
                size: 0x08,
            },
            FunctionRange {
                address: 0x8036_E2D0,
                size: 0xC0,
            },
            FunctionRange {
                address: 0x8036_F054,
                size: 0x12C,
            },
        ];
        let bodies = functions
            .into_iter()
            .map(|function| (function, "    return;\n".to_owned()))
            .collect::<Vec<_>>();
        let files =
            emit_sharded_module(&bodies, &[], 1, 0x8036_DA98).expect("module generation succeeds");
        let combined = files
            .iter()
            .map(|file| file.contents.as_str())
            .collect::<Vec<_>>()
            .join("\n");
        assert!(combined.contains("void fn_8036DA98"));
        assert!(combined.contains("void fn_8036DA0C"));
        assert!(combined.contains("void fn_8036DA14"));
        assert!(combined.contains("void fn_8036E2D0"));
        assert!(combined.contains("void fn_8036F054"));
        assert!(!combined.contains("native_mii_select_"));
    }

    // Task A: divw/divwu must call the deterministic helper (not panic or UB).
    // The generated C++ helper returns the hardware-correct Broadway result
    // instead of calling guest_execution_fault, so both the code generator
    // and the helper are tested end-to-end.
    #[test]
    fn lowers_divw_to_helper_call() {
        // divw r5, r3, r4  ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ encoding 0x7CA3_23D6
        let word: u32 = 0x7CA3_23D6;
        // Append blr so the function has a valid terminator.
        let full: Vec<u8> = word
            .to_be_bytes()
            .iter()
            .copied()
            .chain(0x4E80_0020_u32.to_be_bytes())
            .collect();
        let output = lower_words(0x8000_4000, &full).expect("divw is supported");
        assert!(
            output.contains("galaxy::divide_signed_word"),
            "divw emits the deterministic signed helper"
        );
        // Ensure OE=1 variant (divwo) is rejected (it has bit 10 set).
        let divwo: u32 = word | 0x400;
        let full_oe: Vec<u8> = divwo
            .to_be_bytes()
            .iter()
            .copied()
            .chain(0x4E80_0020_u32.to_be_bytes())
            .collect();
        let err =
            lower_words(0x8000_4000, &full_oe).expect_err("divwo with OE=1 is not yet supported");
        assert!(
            matches!(err, TranslationError::UnsupportedInstruction { .. }),
            "divwo OE=1 raises UnsupportedInstruction"
        );
    }

    #[test]
    fn lowers_divwu_to_helper_call() {
        // divwu r5, r3, r4 ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ encoding 0x7CA3_2396
        let word: u32 = 0x7CA3_2396;
        let full: Vec<u8> = word
            .to_be_bytes()
            .iter()
            .copied()
            .chain(0x4E80_0020_u32.to_be_bytes())
            .collect();
        let output = lower_words(0x8000_4000, &full).expect("divwu is supported");
        assert!(
            output.contains("galaxy::divide_unsigned_word"),
            "divwu emits the deterministic unsigned helper"
        );
    }
}

// Straight-line integer GPR residency.
//
// Its own module so the block stays independent of the file's other test
// organization: these tests call private items and build bodies in the exact
// shape the emitter produces.
#[cfg(test)]
mod residency_tests {
    use super::*;

    const FN: u32 = 0x8000_4000;

    /// One lowered body in the shape `lower_words_with_config` emits, including
    /// the signature line and the interior-entry dispatch a real body has.
    fn residency_body(dispatch: &str, instructions: &str) -> String {
        format!(
            "void fn_80004000(galaxy::PpcContext* __restrict context, galaxy::GuestMemoryV1* memory, const galaxy::NativeServicesV1* services) {{\n    static_cast<void>(context);\n{dispatch}{instructions}}}\n"
        )
    }

    fn residency_dispatch() -> String {
        String::from(
            "    if (context->pc != 0x80004000u) [[unlikely]] {\n    switch (context->pc) {\n        case 0x80004008u: goto label_80004008;\n        default: galaxy::guest_execution_fault(services, context->pc, \"invalid interior function entry\");\n    }\n    }\n    goto fpu_normal_entry_80004000;\nfpu_normal_entry_80004000:\nlabel_80004008:\n",
        )
    }

    fn residency_entries() -> BTreeSet<u32> {
        [0x8000_4008_u32].into_iter().collect()
    }

    /// A body with no labels: the prologue is the only entry.
    fn straight_body() -> String {
        residency_body(
            "",
            "    {\n    context->gpr[3] = 0x80000000u;\n    }\n    {\n    context->gpr[0] = galaxy::guest_load_u16(memory, context->gpr[3] + 0x000030E4u, services, 0x80004004u);\n    }\n    {\n    return;\n    }\n",
        )
    }

    /// A body in the shape `render_sharded_function_definition` actually emits,
    /// prologue casts and all.
    ///
    /// This exists because the synthetic bodies above omit the three
    /// `static_cast<void>` lines, and that omission hid a real defect: the
    /// declaration block was spliced in after the signature but *before* those
    /// casts, which put every `resident_rN` inside the first instruction block
    /// instead of at function scope. The synthetic test passed; the generated
    /// module did not compile (C2065 on every later use). Any test that asserts
    /// where the declarations land must use this shape.
    fn shard_shaped_body() -> String {
        format!(
            "void fn_80004000(galaxy::PpcContext* __restrict context, galaxy::GuestMemoryV1* memory, const galaxy::NativeServicesV1* services) {{\n    static_cast<void>(context);\n    static_cast<void>(memory);\n    static_cast<void>(services);\n{}}}",
            shard_instructions()
        )
    }

    /// Exactly what the shard renderer hands `apply_residency_to_body`: the
    /// instruction stream alone, with no signature and no prologue.
    ///
    /// This is the shape the real module generator passes. `residency_body`
    /// above wraps instructions in a signature, so it can never detect a
    /// placement bug that depends on where the prologue ends -- which is how the
    /// whole module came to be uncompilable while every test passed.
    fn shard_instructions() -> &'static str {
        "    {\n    context->gpr[3] = 0x80000000u;\n    }\n    {\n    context->gpr[0] = galaxy::guest_load_u16(memory, context->gpr[3] + 0x000030E4u, services, 0x80004004u);\n    }\n    {\n    return;\n    }\n"
    }

    #[test]
    fn residency_declarations_land_at_function_scope_not_inside_a_block() {
        // Regression test for the placement defect above. The declarations must
        // precede the body's first `{`, because that brace opens the first
        // instruction block and anything declared inside it is out of scope for
        // every later block.
        //
        // Both input shapes must be covered, because the defect existed precisely
        // because only the signature-bearing shape was tested:
        //
        //   1. a complete body, as the unit tests and one internal caller pass;
        //   2. a bare instruction stream, which is what the real shard renderer
        //      passes -- the shape the module generator actually uses.
        let cases: [(&str, String); 2] = [
            ("signature-shaped body", shard_shaped_body()),
            ("bare instruction stream", shard_instructions().to_owned()),
        ];
        for (label, body) in cases {
            let resident = apply_integer_gpr_residency(FN, &body, &BTreeSet::new())
                .unwrap_or_else(|| panic!("{label} is eligible for residency"));

            let declaration = resident
                .find("std::uint32_t resident_r0 = context->gpr[0];")
                .unwrap_or_else(|| panic!("{label}: the register is declared"));
            let first_block = resident
                .find("    {\n")
                .unwrap_or_else(|| panic!("{label}: the body has at least one instruction block"));
            assert!(
                declaration < first_block,
                "{label}: the declarations must precede the first instruction block, else \
                 every later use is out of scope and the module does not compile"
            );

            let tail = &resident[first_block..];
            assert!(
                tail.contains("resident_r0") || tail.contains("resident_r3"),
                "{label}: the instruction blocks reference the locals"
            );
        }

        // For the signature-bearing shape the declarations must additionally stay
        // inside the function, i.e. after the signature line.
        let resident = apply_integer_gpr_residency(FN, &shard_shaped_body(), &BTreeSet::new())
            .expect("signature-shaped body is eligible");
        let signature = resident.find("services) {\n").unwrap();
        let declaration = resident
            .find("std::uint32_t resident_r0 = context->gpr[0];")
            .unwrap();
        assert!(signature < declaration);
    }

    #[test]
    fn residency_renames_direct_register_access_and_writes_back_written_values() {
        let resident = apply_integer_gpr_residency(FN, &straight_body(), &BTreeSet::new())
            .expect("a straight-line body with one return is eligible");

        // Declaration and load for every register the body names, placed after
        // the signature and before the first instruction.
        assert!(resident.contains("    std::uint32_t resident_r0 = context->gpr[0];\n"));
        assert!(resident.contains("    std::uint32_t resident_r3 = context->gpr[3];\n"));
        let signature = resident.find("services) {\n").unwrap();
        let declaration = resident.find("std::uint32_t resident_r0").unwrap();
        let first_body_access = resident.find("resident_r3 = 0x80000000u;").unwrap();
        assert!(signature < declaration && declaration < first_body_access);

        // Every operand was renamed, and no direct `context->gpr[N]` access
        // survives in the instruction stream.
        assert!(resident
            .contains("resident_r0 = galaxy::guest_load_u16(memory, resident_r3 + 0x000030E4u"));
        assert!(!resident.contains("context->gpr[3] = 0x80000000u"));

        // The writeback covers exactly the registers this body writes. r0 is
        // written by the load, so it is included; a register the body only
        // reads keeps its loaded value and must not be restored over whatever a
        // callee published into it.
        assert!(resident.contains("    context->gpr[3] = resident_r3;\n    return;"));
        assert!(resident.contains("context->gpr[0] = resident_r0;"));
        assert!(!resident.contains("context->gpr[4] = resident_r4;"));
    }

    #[test]
    fn residency_writeback_is_not_spliced_onto_a_mid_line_return() {
        // Regression test. The exit finder used to search for the substring
        // `return;` and splice the writeback at the start of *that line*, so a
        // `return;` sharing its line with other code got the stores hoisted above
        // it and they then ran on a path that never returns -- the function paid
        // the writeback and kept using its locals.
        //
        // Both shapes below are live in the retained module (81 of 4 045 `return;`
        // occurrences over the first 40 shards are mid-line);
        // `functions_0000.cpp:23` and `functions_0002.cpp:9464` are the two forms.
        // The suite passed against the defect because every existing body has its
        // only `return;` alone on a line.
        let body = residency_body(
            "",
            "    {\n    context->gpr[3] = 0x1u;\n    }\n    {\n    if ((true) && (!galaxy::cr_bit(context, 2u))) return;\n    }\n    {\n    return;\n    }\n",
        );
        let resident = apply_integer_gpr_residency(FN, &body, &BTreeSet::new())
            .expect("a body whose exit is unconditional is eligible");

        // The mid-line return survives verbatim: it is not an exit, so nothing may
        // be inserted at the start of its line.
        assert!(
            resident.contains("    if ((true) && (!galaxy::cr_bit(context, 2u))) return;\n"),
            "the mid-line return must be preserved exactly"
        );
        assert!(
            !resident.contains("resident_r3;\n    if ((true)"),
            "the writeback must not precede the mid-line return"
        );

        // Exactly one writeback, and it introduces the real exit.
        assert_eq!(
            resident.matches("context->gpr[3] = resident_r3;").count(),
            1,
            "one writeback for one true exit"
        );
        assert!(
            resident.contains("    context->gpr[3] = resident_r3;\n    return;\n"),
            "the writeback must immediately precede the standalone exit"
        );

        // The `default: return;` form of the same defect, reached through a switch
        // dispatch rather than a conditional expression.
        let switch_body = residency_body(
            "",
            "    {\n    context->gpr[3] = 0x1u;\n    }\n    {\n    switch (context->gpr[4]) {\n    default: return;\n    }\n    }\n    {\n    return;\n    }\n",
        );
        let switch_resident = apply_integer_gpr_residency(FN, &switch_body, &BTreeSet::new())
            .expect("a body whose exit is unconditional is eligible");
        assert!(
            switch_resident.contains("    default: return;\n"),
            "the mid-line `default: return;` must be preserved exactly"
        );
        assert!(
            !switch_resident.contains("resident_r3;\n    default: return;"),
            "the writeback must not precede `default: return;`"
        );
        assert_eq!(
            switch_resident
                .matches("context->gpr[3] = resident_r3;")
                .count(),
            1,
            "one writeback for one true exit, switch form"
        );

        // A body whose only `return;` is mid-line has no exit to attach the
        // writeback to, so it is refused rather than mis-spliced.
        let no_exit = residency_body(
            "",
            "    {\n    context->gpr[3] = 0x1u;\n    }\n    {\n    if ((true) && (!galaxy::cr_bit(context, 2u))) return;\n    }\n",
        );
        assert!(
            apply_integer_gpr_residency(FN, &no_exit, &BTreeSet::new()).is_none(),
            "a body with no standalone exit is ineligible"
        );
    }

    #[test]
    fn residency_keeps_a_comparison_from_counting_as_a_write() {
        let body = residency_body(
            "",
            "    {\n    context->gpr[3] = 0x1u;\n    }\n    {\n    if (context->gpr[5] == 0x2u) { context->gpr[3] = 0x3u; }\n    }\n    {\n    return;\n    }\n",
        );
        let resident = apply_integer_gpr_residency(FN, &body, &BTreeSet::new()).unwrap();
        assert!(resident.contains("resident_r5 == 0x2u"));
        assert!(resident.contains("context->gpr[3] = resident_r3;"));
        assert!(!resident.contains("context->gpr[5] = resident_r5;"));
    }

    #[test]
    fn residency_reloads_the_local_set_at_an_interior_entry() {
        // An interior entry jumps past the prologue, so the locals must be
        // reloaded at that label: the host re-entered the function from outside
        // it, which makes memory authoritative there.
        //
        // The reload must be an *assignment*, not a declaration. The locals are
        // already declared at function scope by the prologue, so re-declaring
        // them here is C2374/C2086 -- and it is at function scope, so the
        // redefinition is an error, not a shadow.
        let body = residency_body(
            &residency_dispatch(),
            "    {\n    context->gpr[3] = 0x1u;\n    }\n    {\n    return;\n    }\n",
        );
        let resident = apply_integer_gpr_residency(FN, &body, &residency_entries())
            .expect("an interior-entry body is eligible once its entry reloads the locals");

        let label = resident
            .find("label_80004008:\n")
            .expect("the entry label is present");
        let after_label = &resident[label..];
        let reload = after_label
            .find("    resident_r3 = context->gpr[3];")
            .expect("the entry label reloads the register the body uses");
        assert!(
            !after_label.contains("std::uint32_t resident_r3 = context->gpr[3];\n    resident_r3 = context->gpr[3];"),
            "the label must not re-declare the local"
        );
        // The reload must be the first thing after the label, and must come
        // before any renamed instruction.
        assert!(
            reload < 64,
            "reload should be adjacent to the label, got offset {reload}"
        );
        assert!(after_label.find("resident_r3 = 0x1u;").unwrap() > reload);

        // Exactly one declaring occurrence in the whole body, and it sits in the
        // prologue rather than at the label.
        assert_eq!(
            resident
                .matches("std::uint32_t resident_r3 = context->gpr[3];")
                .count(),
            1,
            "the local is declared exactly once"
        );
        assert!(resident.find("std::uint32_t resident_r3 = context->gpr[3];").unwrap() < label);

        // The exit still publishes the register the body writes.
        assert!(resident.contains("context->gpr[3] = resident_r3;"));
    }

    #[test]
    fn residency_declines_runtime_indexed_register_access() {
        let body = residency_body(
            "",
            "    {\n    for (std::uint32_t reg = 14u; reg < 32u; ++reg) {\n        context->gpr[reg] = 0u;\n    }\n    }\n    {\n    return;\n    }\n",
        );
        assert!(apply_integer_gpr_residency(FN, &body, &BTreeSet::new()).is_none());
    }

    #[test]
    fn residency_declines_calls_that_can_observe_the_register_file() {
        // `call_return_checkpoint` is deliberately absent from this list. It is no
        // longer a rejection: a body containing it stays eligible, because
        // `apply_integer_gpr_residency` writes the locals back at every
        // `call_return_<pc>` continuation, so the checkpoint observes a register
        // file that already agrees with the locals. See
        // `residency_publishes_the_local_set_before_an_observing_call`.
        //
        // The callees below stay rejected because this body has no
        // `call_return_` continuation at all, so nothing publishes the locals
        // before one of them runs and each could observe a stale register file.
        for callee in [
            "galaxy::call_guest_cached(services, target, &cached_target_80004008, &cached_function_80004008, context, memory, 0x80004004u);",
            "galaxy::native_savegpr_805174FC(context, memory, services, 0x80517534u);",
            "galaxy::architectural_interrupt_checkpoint(services, 0x80004004u, context, memory);",
        ] {
            let body = residency_body(
                "",
                &format!(
                    "    {{\n    context->gpr[3] = 0x1u;\n    }}\n    {{\n    {callee}\n    }}\n    {{\n    return;\n    }}\n"
                ),
            );
            assert!(
                apply_integer_gpr_residency(FN, &body, &BTreeSet::new()).is_none(),
                "{callee} must decline residency"
            );
        }
    }

    #[test]
    fn residency_publishes_the_local_set_before_an_observing_call() {
        // The counterpart to the test above. A checkpoint reached from a
        // `call_return_` continuation is eligible, and the writeback must land
        // before the checkpoint runs or the callback would observe stale
        // `context->gpr[]` values -- the failure the rejection list exists to
        // prevent, now prevented by the continuation writeback instead.
        let body = residency_body(
            "",
            "    {\n    context->gpr[3] = 0x1u;\n    }\n    {\n    return;\n    }\ncall_return_80004008:\n    galaxy::call_return_checkpoint(services, 0x80004004u, 0x80004008u, context, memory);\n    return;\n",
        );
        let resident = apply_integer_gpr_residency(FN, &body, &BTreeSet::new())
            .expect("a checkpoint reached from a continuation stays eligible");

        let checkpoint = resident
            .find("galaxy::call_return_checkpoint(")
            .expect("the checkpoint is retained");
        let writeback = resident
            .find("context->gpr[3] = resident_r3;")
            .expect("the written register is published");
        assert!(
            writeback < checkpoint,
            "writeback must precede the checkpoint so it cannot observe a stale register file"
        );
    }

    #[test]
    fn residency_declines_a_body_with_no_return() {
        let body = residency_body(
            "",
            "    {\n    context->gpr[3] = 0x1u;\n    }\n    {\n    galaxy::guest_execution_fault(services, 0x80004004u, \"x\");\n    }\n",
        );
        assert!(apply_integer_gpr_residency(FN, &body, &BTreeSet::new()).is_none());
    }

    #[test]
    fn residency_writes_back_the_locals_at_a_call_return_continuation() {
        // A continuation is entered after a nested guest call, so the locals are
        // stale there and must be written back -- before the checkpoint that
        // follows the label runs, so the checkpoint sees the real register file.
        //
        // The nested call is a guest load rather than the `rmge01::fn_` direct call
        // this fixture used to carry. That is deliberate: a direct call is now a
        // decline (see `residency_declines_a_body_with_a_direct_guest_call`), so
        // keeping one here would make the body ineligible and this test would stop
        // exercising the continuation writeback at all. The load is not on the
        // reject list and still produces the post-call `call_return_` continuation
        // this test is about.
        let body = residency_body(
            "",
            "    {\n    context->gpr[3] = 0x1u;\n    }\n    {\n    context->lr = 0x80004004u;\n    context->gpr[0] = galaxy::guest_load_u32(memory, context->gpr[3], services, 0x80005000u);\n    galaxy::call_return_checkpoint(services, 0x80004000u, 0x80004004u, context, memory);\n    }\nlabel_80004004:\n    {\n    return;\n    }\ncall_return_80004004:\n    galaxy::call_return_checkpoint(services, 0x80004000u, 0x80004004u, context, memory);\n    goto label_80004004;\n",
        );
        let resident = apply_integer_gpr_residency(FN, &body, &BTreeSet::new())
            .expect("a body with a call-return continuation is eligible");

        let label = resident
            .find("call_return_80004004:\n")
            .expect("the continuation label is present");
        let after = &resident[label..];
        let writeback = after
            .find("context->gpr[3] = resident_r3;")
            .expect("the continuation writes back the register the body writes");
        let checkpoint = after
            .find("galaxy::call_return_checkpoint(")
            .expect("the checkpoint is present");
        assert!(
            writeback < checkpoint,
            "the writeback must precede the checkpoint so it sees the real register file"
        );
    }

    /// A body that hands the caller's `context` to another guest function must not
    /// be renamed. The callee reads its arguments out of `context->gpr[3..]` and
    /// publishes its result into `context->gpr[3]`; renaming the caller's operands
    /// to locals would send stale arguments and then clobber the result on
    /// writeback. Both failures are silent -- the C++ compiles and the module
    /// loads -- so the decline has to be pinned by a test rather than by review.
    ///
    /// This guards the entry added to the decline list for
    /// `AgentWork/agent-21/A21-9-*`. `guest_resident_integer` is off today, so a
    /// regression here would not show up until someone re-enables it.
    #[test]
    fn residency_declines_a_body_with_a_direct_guest_call() {
        for call in [
            "    context->pc = 0x80005000u;\n    rmge01::fn_80005000(context, memory, services);\n",
            "    galaxy::call_guest_resolved(services, 0x80005000u, &rmge01::fn_80005000, context, memory, 0x80004000u);\n",
            "    galaxy::call_guest_direct_resolved(services, 0x80005000u, &rmge01::fn_80005000, context, memory, 0x80004000u);\n",
        ] {
            let body = residency_body(
                "",
                &format!(
                    "    {{\n    context->gpr[3] = 0x1u;\n    }}\n    {{\n{call}    }}\n    {{\n    context->gpr[4] = context->gpr[3];\n    }}\n    {{\n    return;\n    }}\n"
                ),
            );
            assert!(
                apply_integer_gpr_residency(FN, &body, &BTreeSet::new()).is_none(),
                "a body containing `{call}` must be declined: the callee sees `context`"
            );
        }
    }

    #[test]
    fn residency_is_opt_in_and_preserves_the_body_otherwise() {
        let body = straight_body();
        let options = ModuleTranslationOptions::default();
        assert!(!options.guest_resident_integer, "residency is opt-in");
        assert_eq!(
            apply_residency_to_body(FN, body.clone(), &BTreeSet::new(), &options),
            body
        );
        let enabled = ModuleTranslationOptions {
            guest_resident_integer: true,
            ..ModuleTranslationOptions::default()
        };
        assert!(
            apply_residency_to_body(FN, body, &BTreeSet::new(), &enabled)
                .contains("resident_r3")
        );
    }
}
