use crate::{
    game::{load_verified_game, GameError},
    guest_abi_effect::{
        analyze_guest_abi, GuestAbiContract, GuestBoundaryFlags, GuestEffectBlock,
        GuestInstructionEffect, GuestRegisterSet, GuestResidencyBoundarySync,
    },
    guest_ppc_effect::{guest_integer_gpr_access, GuestGprAccess},
    ppc_dataflow::{AbiFunctionInput, ModuleAbi, Regs},
    residency::{AbiResidency, LeanCallContract},
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
    /// `fast_float` without `native_float` has no helpers to switch.
    #[error("fast_float requires native_float")]
    FastFloatRequiresNativeFloat,
    /// A module option whose lowering lives in another option's helpers.
    #[error("module option {option} requires {requires}")]
    OptionRequires {
        option: &'static str,
        requires: &'static str,
    },
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

#[derive(Clone, Debug, PartialEq, Eq)]
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
    /// Flat RAM stores through the host's writable guest view, with the same
    /// dirty-page publication as the checked path. Gather-pipe stores append
    /// to the host's live burst buffer in place; devices and unmapped spans
    /// keep the checked store.
    pub flat_ram_writes: bool,
    /// Native IEEE arithmetic for scalar and paired floating point. Ordinary
    /// operands under nearest-even rounding compute in place; everything else
    /// (non-finite or subnormal operands, binary32 rounding midpoints,
    /// non-default rounding modes) takes the exact helper. FPSCR status bits
    /// are not maintained on the native path.
    pub native_float: bool,
    /// `bctrl`/`bclrl` call a published native entry directly. Targets that
    /// own a host transaction, pending device events and live external
    /// interrupt dispatch all keep the cached service boundary.
    pub direct_indirect_calls: bool,
    /// Experimental fused paired-single arithmetic/commit helper. Public
    /// entries, operand validation, FPSCR and precise FP faults are unaffected.
    pub fused_paired_binary: bool,
    /// Four-register indexed paired boundary with library-local result packing.
    /// Opt-in while its cohorts are still being qualified.
    pub indexed_paired_binary: bool,
    /// Compact paired ternary boundary; opt-in pending broader qualification.
    pub indexed_paired_ternary: bool,
    /// Experimental paired ternary calculation/commit boundary. Both lane
    /// operands are captured before a destination or status can be written.
    pub fused_paired_ternary: bool,
    /// Experimental inline widened-single scalar add/subtract/multiply.
    /// Unhandled inputs, modes and results use the original lowering.
    pub inline_scalar_single_binary: bool,
    /// Straight-line integer GPR residency across a whole function body.
    ///
    /// This is a function-wide property, unlike the other options. The translated body names `context->gpr[N]`
    /// for every guest register operand, so the host compiler cannot keep the guest register file in registers.
    /// Each operand is a separate 4-byte access into `PpcContext`, and every opaque call (call-return checkpoint,
    /// guest load or store) forces later reads to be reissued. The generated module therefore does roughly one
    /// `PpcContext` access per guest instruction.
    ///
    /// With this option, every directly indexed GPR lives in a function-local variable, so the host register
    /// allocator owns the guest register file across the straight-line body. Only function exit writes the locals
    /// back. See `crate::residency::apply_register_residency` for the rule, and for why interior entries and
    /// runtime-indexed GPR access are excluded.
    pub guest_resident_integer: bool,
    /// Fast float: the module is compiled with `GALAXY_FAST_FLOAT=1`. The native float helpers run plain IEEE
    /// arithmetic (fused multiply-add, one rounding to binary32 for single forms, 25-bit multiplier rounding,
    /// saturating fctiwz), with no exact fallback and no FPSCR status. This follows Dolphin/WiiCompiled accuracy,
    /// not bit-exact Gekko results. The exact fallback calls were what kept guest floats in memory instead of
    /// host registers. The module exports `galaxy_module_float_flags`, so the runtime can select flush-to-zero on
    /// the simulation thread. Requires `native_float`.
    pub fast_float: bool,
    /// Also keeps the FPR/PS1 file in host locals (requires `guest_resident_integer`). Off by default: the exact
    /// float fallbacks force spills around every helper call.
    pub guest_resident_float: bool,
    /// `lfs`/`stfs` and the binary32 lanes of `psq_l`/`psq_st` convert with
    /// the host's `cvtss2sd`/`cvtsd2ss` (`fast::*_hw` helpers) instead of the
    /// integer widen/narrow bit slices. Not bit-exact: signalling NaNs are
    /// quieted, `stfs` of a value outside binary32 rounds instead of slicing,
    /// and under the fast-float MXCSR subnormals read and write as zero.
    /// Requires `flat_ram_reads` and `flat_ram_writes`: the conversions live
    /// in the flat-view helpers, and the checked helpers keep the exact ones.
    pub hw_float_conversions: bool,
    /// Checkpoint tests read a module-global snapshot of the inline gate
    /// (`galaxy::g_module_checkpoint_gate`, defined in `module.cpp` and
    /// exported as `galaxy_module_checkpoint_gate`) instead of re-reading the
    /// service table after every call. The runtime keeps the snapshot current
    /// with `galaxy::refresh_module_checkpoint_gate`; an empty snapshot sends
    /// every checkpoint to the complete service-table policy.
    pub checkpoint_globals: bool,
    /// A fastmem module that assumes the flat view everywhere: `stmw` with
    /// rS <= 24 stores register by register through the view instead of the
    /// checked multiple-store helper, `dcbz` zeroes its line through the view
    /// (`fast::zero_line`), and `functions.h` defines `GALAXY_FLAT_VIEW_ONLY`
    /// so the inlined lean host bodies drop their null-view check and region
    /// fallback (`lean_guest_span`, `cstring::view`). Requires `fast_float`
    /// (which makes the module fastmem) and both flat RAM directions.
    pub flat_view_only: bool,
    /// `bl _savegpr_N` right after `addi r11,r1,d` (CodeWarrior's prologue)
    /// stores through `fast::store_stack`: r11 addresses the caller's own
    /// frame, so the gather-pipe test goes. Requires `flat_ram_writes`.
    pub stack_r11_stores: bool,
    /// HID2[PSE] and HID2[LSQE] are taken as always set (RMGE01 sets them in
    /// `__init_hardware` and never clears them; a paired or quantized
    /// instruction with either clear is illegal on the hardware): `functions.h`
    /// defines `GALAXY_ASSUME_PAIRED_SINGLES`, which removes the HID2 test from
    /// every psq and paired fast path and writes ps1 for every single result.
    pub assume_paired_singles: bool,
    /// Calls to translated functions and indirect calls synchronize the
    /// resident registers under the PowerPC EABI contract
    /// (`crate::ppc_dataflow::ModuleAbi`): before the call only the written
    /// locals a callee may read (arguments, r1/r2/r13, non-volatile
    /// registers, LR and the callee's recorded extras) are stored; after it
    /// only the results and the registers the callee may clobber are
    /// reloaded. Functions the analysis cannot vouch for (host
    /// substitutions, unmodelled forms, local `bl` subroutines) keep full
    /// synchronization at their calls and when called, and the list is
    /// written to `abi_contract.json`. Requires `guest_resident_integer`.
    pub abi_call_sync: bool,
    /// A `blr` of a function the ABI analysis vouches for stores only the
    /// registers a caller may observe (r3/r4, f1/f2, the non-volatile
    /// registers, LR and the module's non-ABI extras) instead of every
    /// written local. Requires `guest_resident_integer`.
    pub abi_exit_sync: bool,
    /// Fast-float module whose single-precision and paired-single arithmetic
    /// rounds its operands to binary32 and computes in binary32
    /// (`functions.h` defines `GALAXY_SINGLE_FLOAT_ARITHMETIC`), instead of
    /// computing in binary64 with the 25-bit multiplier rounding and
    /// narrowing. A chain of single operations then stays in binary32
    /// registers. Requires `fast_float`.
    pub single_float_arithmetic: bool,
    /// `fres`, `frsqrte`, `ps_res`, `ps_rsqrte`, `fsel`, `ps_sel`, `fcmpu`,
    /// `fcmpo` and `ps_cmp*` lower to the `galaxy::fast` forms: correctly
    /// rounded reciprocals instead of the exact estimate tables, no FPSCR
    /// update, no commit helper, CR field only for the compares. Requires
    /// `fast_float`.
    pub fast_float_forms: bool,
    /// Every direct-dispatch `bctrl`/`bclrl`/`bctr` site caches the last
    /// published direct entry it called (`fast::call_guest_indirect_site`): a
    /// repeat target skips the service-table loads and the 10 MB table
    /// access. The event and suspension gates are still tested on every
    /// call. Requires `direct_indirect_calls`.
    pub bctrl_site_cache: bool,
    /// Guest addresses of translated functions, most-run first. The module's
    /// linker places them at the start of the code section, in this order
    /// (`function_order.txt`, `/ORDER`); functions the list does not name
    /// follow in the default order.
    pub function_order: Option<&'static [u32]>,
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
            flat_ram_writes: false,
            native_float: false,
            direct_indirect_calls: false,
            fused_paired_binary: false,
            indexed_paired_binary: false,
            indexed_paired_ternary: false,
            fused_paired_ternary: false,
            inline_scalar_single_binary: false,
            guest_resident_integer: false,
            fast_float: false,
            guest_resident_float: false,
            hw_float_conversions: false,
            checkpoint_globals: false,
            flat_view_only: false,
            stack_r11_stores: false,
            assume_paired_singles: false,
            abi_call_sync: false,
            abi_exit_sync: false,
            single_float_arithmetic: false,
            fast_float_forms: false,
            bctrl_site_cache: false,
            function_order: None,
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
        (function_requires_fpu(bytes) || native_substitution_uses_fpu(function.address))
            && lean_sdk_math_symbol(function.address).is_none(),
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

/// Option combinations that have no lowering. Checked before the game is read.
pub fn validate_module_options(options: &ModuleTranslationOptions) -> Result<(), TranslationError> {
    if options.fast_float && !options.native_float {
        return Err(TranslationError::FastFloatRequiresNativeFloat);
    }
    if options.hw_float_conversions && !(options.flat_ram_reads && options.flat_ram_writes) {
        return Err(TranslationError::OptionRequires {
            option: "hw_float_conversions",
            requires: "flat_ram_reads and flat_ram_writes",
        });
    }
    if options.flat_view_only
        && !(options.fast_float && options.flat_ram_reads && options.flat_ram_writes)
    {
        return Err(TranslationError::OptionRequires {
            option: "flat_view_only",
            requires: "fast_float (a fastmem module), flat_ram_reads and flat_ram_writes",
        });
    }
    if options.stack_r11_stores && !options.flat_ram_writes {
        return Err(TranslationError::OptionRequires {
            option: "stack_r11_stores",
            requires: "flat_ram_writes",
        });
    }
    if options.abi_call_sync && !options.guest_resident_integer {
        return Err(TranslationError::OptionRequires {
            option: "abi_call_sync",
            requires: "guest_resident_integer",
        });
    }
    if options.abi_exit_sync && !options.guest_resident_integer {
        return Err(TranslationError::OptionRequires {
            option: "abi_exit_sync",
            requires: "guest_resident_integer",
        });
    }
    if options.single_float_arithmetic && !options.fast_float {
        return Err(TranslationError::OptionRequires {
            option: "single_float_arithmetic",
            requires: "fast_float",
        });
    }
    if options.fast_float_forms && !options.fast_float {
        return Err(TranslationError::OptionRequires {
            option: "fast_float_forms",
            requires: "fast_float",
        });
    }
    if options.bctrl_site_cache && !options.direct_indirect_calls {
        return Err(TranslationError::OptionRequires {
            option: "bctrl_site_cache",
            requires: "direct_indirect_calls",
        });
    }
    Ok(())
}

pub fn translate_module_with_options(
    input: &Path,
    count: usize,
    shard_size: usize,
    shard_source_kib: usize,
    options: ModuleTranslationOptions,
) -> Result<TranslationModule, TranslationError> {
    validate_module_options(&options)?;
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
    // Functions that execute FP instructions stay on the generic native float lowering. Hand-written substitutions
    // for them use exact out-of-line helpers, which are slower (module benchmark, SDK matrix library).
    let mut exact_fpu_functions = BTreeSet::new();
    for function in &ranges {
        let bytes = function_bytes(&loaded.dol.sections, &loaded.dol_bytes, *function).ok_or(
            TranslationError::FunctionOutsideText {
                address: function.address,
            },
        )?;
        if (function_requires_fpu(bytes) || native_substitution_uses_fpu(function.address))
            && lean_sdk_math_symbol(function.address).is_none()
        {
            exact_fpu_functions.insert(function.address);
        }
    }
    // Translated leaves with no checkpoint of their own: linked calls to them
    // need no call-return checkpoint. Host substitutions are not included.
    let mut checkpoint_free_leaves = BTreeSet::new();
    for function in &ranges {
        let bytes = function_bytes(&loaded.dol.sections, &loaded.dol_bytes, *function).ok_or(
            TranslationError::FunctionOutsideText {
                address: function.address,
            },
        )?;
        if native_function_body(function.address).is_none()
            && native_pure_helper(function.address).is_none()
            && is_checkpoint_free_leaf(function.address, bytes)
        {
            checkpoint_free_leaves.insert(function.address);
        }
    }
    let call_return_entries =
        call_return_continuations(&loaded.dol.sections, &loaded.dol_bytes, &ranges);
    let device_pointer_globals =
        discover_device_pointer_globals(&loaded.dol.sections, &loaded.dol_bytes, &ranges);
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
    let module_abi = (options.abi_call_sync || options.abi_exit_sync)
        .then(|| module_abi_facts(&loaded.dol.sections, &loaded.dol_bytes, &ranges))
        .transpose()?;
    // As in WiiCompiled, caller residency narrows a call boundary only for a callee with a fully known contract:
    // the integer leaves decoded below. Substitutions, FP bodies and leaves with interior entries stay full fences.
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
        let abi = module_abi
            .as_ref()
            .map(|module_abi| function_abi_residency(module_abi, function.address, &options));
        let abi = abi.as_ref();
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
                    &checkpoint_free_leaves,
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
                            &checkpoint_free_leaves,
                            &device_pointer_globals,
                            &options,
                        )?,
                        entries,
                        &options,
                        abi,
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
                    &checkpoint_free_leaves,
                    &device_pointer_globals,
                    &options,
                )?,
                entries,
                &options,
                abi,
            )
        };
        *body = apply_transfer_returns(std::mem::take(body));
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
                let memory_plan = validate_psmtx_local_memory_contract(bytes)?;
                let body = lower_words_with_config(
                    function.address,
                    bytes,
                    &BTreeSet::new(),
                    &BTreeSet::new(),
                    LoweringConfig {
                        local_lane_profile: LocalLaneProfile::PsmtxConcat,
                        // Applies only inside the guarded matrix leaf. The global
                        // fused-paired option is unchanged.
                        fused_paired_binary: true,
                        ..LoweringConfig::default()
                    },
                )?;
                Some(PsmtxLocalExperiment {
                    body: lower_psmtx_proven_memory(body, bytes, &memory_plan)?,
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
    // The checkpoint-globals spellings are longer; the packer must see them.
    let definition_rewrite = options
        .checkpoint_globals
        .then_some(rewrite_checkpoint_globals as fn(&str) -> String);
    let mut source_files = emit_sharded_module_with_definition_rewrite(
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
        definition_rewrite,
    )?;
    apply_module_options(&mut source_files, &options);
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
    if let Some(module_abi) = &module_abi {
        source_files.push(GeneratedSource {
            name: ABI_CONTRACT_FILE.to_owned(),
            contents: module_abi.manifest_json(),
        });
    }
    if let Some(order) = options.function_order {
        add_function_order(&mut source_files, order);
    }
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

// Floating-point and paired-single opcodes. Under eager FPU no MSR[FP] guard is emitted for them. The
// predicate only keeps functions that execute FP instructions on the generic native float lowering instead
// of a hand-written substitution, which would use the slower out-of-line exact helpers.
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

fn function_requires_fpu(bytes: &[u8]) -> bool {
    bytes.chunks_exact(4).any(|chunk| {
        let word = u32::from_be_bytes(chunk.try_into().expect("four-byte instruction"));
        opcode_requires_fpu(Ins::new(word, Extensions::gekko_broadway()).op)
    })
}

// Some entry/direct-call substitutions touch FPR/PS/FPSCR directly or fold
// in a tail-called FP routine even when the wrapper has no FP opcode. Those
// substitutions use the exact out-of-line float helpers and are slower than
// the generic native lowering, so the translated bodies are kept.
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

/// A function whose translated body has no checkpoint, callback or transfer
/// point: every branch is a forward branch inside the function or a return,
/// and there is no call, indirect branch, system call, trap, rfi, mtmsr, or SPR
/// write other than XER, LR, CTR and the GQRs. A linked call to it returns
/// with no event observed or raised, so the call needs no call-return
/// checkpoint.
fn is_checkpoint_free_leaf(address: u32, bytes: &[u8]) -> bool {
    let end = address.wrapping_add(bytes.len() as u32);
    !bytes.is_empty()
        && bytes.chunks_exact(4).enumerate().all(|(index, chunk)| {
            let pc = address + index as u32 * 4;
            let word = u32::from_be_bytes(chunk.try_into().expect("four-byte instruction"));
            let link = word & 1 != 0;
            match Ins::new(word, Extensions::gekko_broadway()).op {
                Opcode::B => {
                    let target = branch_target(pc, word);
                    !link && target > pc && target < end
                }
                Opcode::Bc => {
                    let target = conditional_branch_target(pc, word);
                    !link && target > pc && target < end
                }
                Opcode::Bclr => !link,
                Opcode::Mtspr => matches!(spr_number(word), 1 | 8 | 9 | 912..=919),
                Opcode::Bcctr
                | Opcode::Sc
                | Opcode::Rfi
                | Opcode::Tw
                | Opcode::Twi
                | Opcode::Mtmsr
                | Opcode::Mtsr
                | Opcode::Mtsrin
                | Opcode::Illegal => false,
                _ => true,
            }
        })
}

/// How one instruction treats CR field `field` for `cr_field_dead_from`.
enum CrFieldStep {
    /// Reads the field, or cannot be followed statically.
    Live,
    /// Writes the whole field before reading it.
    Kill,
    /// Leaves the function; the field is dead there iff `dead`.
    Exit {
        dead: bool,
    },
    Next,
    Jump(u32),
    /// Falls through and may also branch to the target.
    Fork(u32),
    /// Falls through, and a taken exit leaves the field dead (a conditional
    /// return or tail branch).
    NextAfterDeadExit,
}

/// CR fields the PPC ABI leaves undefined after a call: CR0 and CR5-CR7. CR1
/// carries the varargs float flag into the callee and CR2-CR4 are preserved.
fn cr_field_dead_across_call(field: u32) -> bool {
    matches!(field, 0 | 5 | 6 | 7)
}

/// CR fields undefined at a function return: everything but CR2-CR4.
fn cr_field_dead_at_return(field: u32) -> bool {
    !matches!(field, 2..=4)
}

fn cr_field_step(pc: u32, word: u32, field: u32, start: u32, end: u32) -> CrFieldStep {
    let opcode = word >> 26;
    let bit_in_field = |bit: u32| bit / 4 == field;
    let crf_d = (word >> 23) & 7;
    let link = word & 1 != 0;
    match Ins::new(word, Extensions::gekko_broadway()).op {
        Opcode::Cmpi | Opcode::Cmpli | Opcode::Cmp | Opcode::Cmpl => {
            if crf_d == field {
                CrFieldStep::Kill
            } else {
                CrFieldStep::Next
            }
        }
        Opcode::Mcrxr => {
            if crf_d == field {
                CrFieldStep::Kill
            } else {
                CrFieldStep::Next
            }
        }
        Opcode::Mtcrf => {
            let fxm = (word >> 12) & 0xFF;
            if fxm & (0x80 >> field) != 0 {
                CrFieldStep::Kill
            } else {
                CrFieldStep::Next
            }
        }
        Opcode::Mfcr => CrFieldStep::Live,
        Opcode::Mcrf => {
            if (word >> 18) & 7 == field {
                CrFieldStep::Live
            } else if crf_d == field {
                CrFieldStep::Kill
            } else {
                CrFieldStep::Next
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
            if bit_in_field((word >> 16) & 0x1F) || bit_in_field((word >> 11) & 0x1F) {
                CrFieldStep::Live
            } else {
                CrFieldStep::Next
            }
        }
        // Integer record forms write CR0 from their result.
        Opcode::Andi_ | Opcode::Andis_ | Opcode::Addic_ | Opcode::Stwcx_ => {
            if field == 0 {
                CrFieldStep::Kill
            } else {
                CrFieldStep::Next
            }
        }
        Opcode::Add
        | Opcode::Addc
        | Opcode::Adde
        | Opcode::Addme
        | Opcode::Addze
        | Opcode::Subf
        | Opcode::Subfc
        | Opcode::Subfe
        | Opcode::Subfme
        | Opcode::Subfze
        | Opcode::Neg
        | Opcode::Mullw
        | Opcode::Mulhw
        | Opcode::Mulhwu
        | Opcode::Divw
        | Opcode::Divwu
        | Opcode::And
        | Opcode::Andc
        | Opcode::Or
        | Opcode::Orc
        | Opcode::Xor
        | Opcode::Nand
        | Opcode::Nor
        | Opcode::Eqv
        | Opcode::Slw
        | Opcode::Srw
        | Opcode::Sraw
        | Opcode::Srawi
        | Opcode::Cntlzw
        | Opcode::Extsb
        | Opcode::Extsh
        | Opcode::Rlwinm
        | Opcode::Rlwimi
        | Opcode::Rlwnm => {
            if field == 0 && link {
                CrFieldStep::Kill
            } else {
                CrFieldStep::Next
            }
        }
        Opcode::B => {
            let target = branch_target(pc, word);
            if link {
                CrFieldStep::Exit {
                    dead: cr_field_dead_across_call(field),
                }
            } else if word & 2 == 0 && target >= start && target < end {
                CrFieldStep::Jump(target)
            } else {
                CrFieldStep::Exit {
                    dead: cr_field_dead_across_call(field),
                }
            }
        }
        Opcode::Bc => {
            let bo = (word >> 21) & 0x1F;
            let bi = (word >> 16) & 0x1F;
            if bo & 0x10 == 0 && bit_in_field(bi) {
                return CrFieldStep::Live;
            }
            if link || word & 2 != 0 {
                return CrFieldStep::Live;
            }
            let target = conditional_branch_target(pc, word);
            let always = bo & 0x14 == 0x14;
            match (target >= start && target < end, always) {
                (true, true) => CrFieldStep::Jump(target),
                (true, false) => CrFieldStep::Fork(target),
                (false, true) => CrFieldStep::Exit {
                    dead: cr_field_dead_across_call(field),
                },
                (false, false) => {
                    if cr_field_dead_across_call(field) {
                        CrFieldStep::NextAfterDeadExit
                    } else {
                        CrFieldStep::Live
                    }
                }
            }
        }
        Opcode::Bclr => {
            let bo = (word >> 21) & 0x1F;
            let bi = (word >> 16) & 0x1F;
            if bo & 0x10 == 0 && bit_in_field(bi) {
                return CrFieldStep::Live;
            }
            if link {
                return CrFieldStep::Live;
            }
            if bo & 0x14 == 0x14 {
                CrFieldStep::Exit {
                    dead: cr_field_dead_at_return(field),
                }
            } else if cr_field_dead_at_return(field) {
                CrFieldStep::NextAfterDeadExit
            } else {
                CrFieldStep::Live
            }
        }
        Opcode::Bcctr | Opcode::Sc | Opcode::Rfi | Opcode::Tw | Opcode::Twi | Opcode::Illegal => {
            CrFieldStep::Live
        }
        _ if opcode == 19 => {
            // Remaining XL forms: isync is the only other one the game uses.
            if word == 0x4C00_012C {
                CrFieldStep::Next
            } else {
                CrFieldStep::Live
            }
        }
        _ => CrFieldStep::Next,
    }
}

/// Whether CR field `field` is dead on entry to `from`: every path from there
/// writes the whole field before it reads any of it, or leaves the function
/// where the ABI leaves the field undefined. Any path the walk cannot follow
/// (an indirect branch, a system call, more than 256 instructions) counts as
/// a read.
fn cr_field_dead_from(address: u32, bytes: &[u8], from: u32, field: u32) -> bool {
    const BUDGET: usize = 256;
    let end = address.wrapping_add(bytes.len() as u32);
    let mut pending = vec![from];
    let mut visited = BTreeSet::new();
    while let Some(start) = pending.pop() {
        let mut pc = start;
        loop {
            if !visited.insert(pc) {
                break;
            }
            if visited.len() > BUDGET || pc < address || pc >= end {
                return false;
            }
            let offset = (pc - address) as usize;
            let word = u32::from_be_bytes(
                bytes[offset..offset + 4]
                    .try_into()
                    .expect("four-byte instruction"),
            );
            match cr_field_step(pc, word, field, address, end) {
                CrFieldStep::Live => return false,
                CrFieldStep::Kill => break,
                CrFieldStep::Exit { dead } => {
                    if dead {
                        break;
                    }
                    return false;
                }
                CrFieldStep::Next | CrFieldStep::NextAfterDeadExit => pc += 4,
                CrFieldStep::Jump(target) => pc = target,
                CrFieldStep::Fork(target) => {
                    pending.push(target);
                    pc += 4;
                }
            }
        }
    }
    true
}

/// Integer compares whose CR field only feeds the conditional branch right
/// after them: `bc` (no CTR, no link, relative) at pc + 4 tests one LT/GT/EQ
/// bit of that field, nothing else enters the branch, and the field is dead
/// on both of its successors. The branch then tests the operands directly and
/// the compare writes no CR. Keyed by the branch's pc: (compare pc, the
/// tested bit's predicate as a C++ expression).
fn fusible_compare_branches(
    address: u32,
    bytes: &[u8],
    branch_targets: &BTreeSet<u32>,
) -> BTreeMap<u32, (u32, String)> {
    let end = address.wrapping_add(bytes.len() as u32);
    let words: Vec<u32> = bytes
        .chunks_exact(4)
        .map(|chunk| u32::from_be_bytes(chunk.try_into().expect("four-byte instruction")))
        .collect();
    let mut fused = BTreeMap::new();
    for (index, pair) in words.windows(2).enumerate() {
        let (compare, branch) = (pair[0], pair[1]);
        let compare_pc = address + index as u32 * 4;
        let branch_pc = compare_pc + 4;
        let op = Ins::new(compare, Extensions::gekko_broadway()).op;
        if !matches!(
            op,
            Opcode::Cmpi | Opcode::Cmpli | Opcode::Cmp | Opcode::Cmpl
        ) || compare & 0x0020_0000 != 0
            || Ins::new(branch, Extensions::gekko_broadway()).op != Opcode::Bc
            || branch & 3 != 0
            || branch_targets.contains(&branch_pc)
        {
            continue;
        }
        let field = (compare >> 23) & 7;
        let bo = (branch >> 21) & 0x1F;
        let bi = (branch >> 16) & 0x1F;
        if bo & 0x14 != 0x04 || bi / 4 != field || bi % 4 == 3 {
            continue;
        }
        let target = conditional_branch_target(branch_pc, branch);
        let taken_dead = if target >= address && target < end {
            cr_field_dead_from(address, bytes, target, field)
        } else {
            cr_field_dead_across_call(field)
        };
        if !taken_dead || !cr_field_dead_from(address, bytes, branch_pc + 4, field) {
            continue;
        }
        let ra = gpr_ra(compare);
        let (left, right) = match op {
            Opcode::Cmpi => (
                format!("static_cast<std::int32_t>(context->gpr[{ra}])"),
                format!(
                    "static_cast<std::int32_t>(0x{:08X}u)",
                    signed_immediate(compare) as u32
                ),
            ),
            Opcode::Cmp => (
                format!("static_cast<std::int32_t>(context->gpr[{ra}])"),
                format!(
                    "static_cast<std::int32_t>(context->gpr[{}])",
                    gpr_rb(compare)
                ),
            ),
            Opcode::Cmpli => (
                format!("context->gpr[{ra}]"),
                format!("0x{:08X}u", compare & 0xFFFF),
            ),
            _ => (
                format!("context->gpr[{ra}]"),
                format!("context->gpr[{}]", gpr_rb(compare)),
            ),
        };
        let relation = match bi % 4 {
            0 => "<",
            1 => ">",
            _ => "==",
        };
        fused.insert(
            branch_pc,
            (compare_pc, format!("{left} {relation} {right}")),
        );
    }
    fused
}

/// Integer record forms whose CR0 is set from their GPR result.
fn records_cr0_from_result(word: u32) -> bool {
    match Ins::new(word, Extensions::gekko_broadway()).op {
        Opcode::Andi_ | Opcode::Andis_ | Opcode::Addic_ => true,
        Opcode::Add
        | Opcode::Addc
        | Opcode::Adde
        | Opcode::Addme
        | Opcode::Addze
        | Opcode::Subf
        | Opcode::Subfc
        | Opcode::Subfe
        | Opcode::Subfme
        | Opcode::Subfze
        | Opcode::Neg
        | Opcode::Mullw
        | Opcode::Mulhw
        | Opcode::Mulhwu
        | Opcode::Divw
        | Opcode::Divwu
        | Opcode::And
        | Opcode::Andc
        | Opcode::Or
        | Opcode::Orc
        | Opcode::Xor
        | Opcode::Nand
        | Opcode::Nor
        | Opcode::Eqv
        | Opcode::Slw
        | Opcode::Srw
        | Opcode::Sraw
        | Opcode::Srawi
        | Opcode::Cntlzw
        | Opcode::Extsb
        | Opcode::Extsh
        | Opcode::Rlwinm
        | Opcode::Rlwimi
        | Opcode::Rlwnm => word & 1 != 0,
        _ => false,
    }
}

/// Record forms whose CR0 only feeds the conditional branch right after
/// them, under the same rules as `fusible_compare_branches`: the branch then
/// tests the recorded GPR and the record form writes no CR.
fn fusible_record_branches(
    address: u32,
    bytes: &[u8],
    branch_targets: &BTreeSet<u32>,
) -> BTreeSet<u32> {
    let end = address.wrapping_add(bytes.len() as u32);
    let words: Vec<u32> = bytes
        .chunks_exact(4)
        .map(|chunk| u32::from_be_bytes(chunk.try_into().expect("four-byte instruction")))
        .collect();
    let mut fused = BTreeSet::new();
    for (index, pair) in words.windows(2).enumerate() {
        let (record, branch) = (pair[0], pair[1]);
        let record_pc = address + index as u32 * 4;
        let branch_pc = record_pc + 4;
        if !records_cr0_from_result(record)
            || Ins::new(branch, Extensions::gekko_broadway()).op != Opcode::Bc
            || branch & 3 != 0
            || branch_targets.contains(&branch_pc)
        {
            continue;
        }
        let bo = (branch >> 21) & 0x1F;
        let bi = (branch >> 16) & 0x1F;
        if bo & 0x14 != 0x04 || bi > 2 {
            continue;
        }
        let target = conditional_branch_target(branch_pc, branch);
        let taken_dead = if target >= address && target < end {
            cr_field_dead_from(address, bytes, target, 0)
        } else {
            cr_field_dead_across_call(0)
        };
        if taken_dead && cr_field_dead_from(address, bytes, branch_pc + 4, 0) {
            fused.insert(record_pc);
        }
    }
    fused
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
    // Indirect linked calls (bctrl/blrl: virtual dispatch, callbacks) can reach any address-taken function,
    // including the OSLockMutex/OSSleepThread OSLoadContext chain, which static taint cannot follow.
    // Functions containing one are tainted, and their indirect return sites become continuations.
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
    // analyze_resident_integer_leaf already rejects any word it cannot classify and pushes one entry per
    // accepted instruction, in order. The opcode match below must therefore accept every word it accepted.
    // This asserts that instead of relying on it. Falling out of the match at `_ => return None` leaves a
    // half-written body; the caller reads None as "not eligible" and re-lowers with the ordinary emitter.
    // The output would still be correct, but this tier would silently lose coverage. An edit that adds an
    // opcode to one match and not the other must fail in tests.
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
#[allow(clippy::too_many_arguments)]
fn lower_resident_integer_direct_calls(
    address: u32,
    bytes: &[u8],
    entries: &BTreeSet<u32>,
    call_return_entries: &BTreeSet<u32>,
    callable_entries: &BTreeMap<u32, u32>,
    exact_fpu_functions: &BTreeSet<u32>,
    leaf_contracts: &BTreeMap<u32, GuestAbiContract>,
    checkpoint_free_callees: &BTreeSet<u32>,
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
                let lean_contract = lean_body_call_contract(
                    target,
                    pc,
                    Some(callable_entries),
                    Some(exact_fpu_functions),
                );
                let (flush_gpr, reload_gpr) = if let Some(contract) = lean_contract {
                    (
                        contract.gpr_reads | contract.gpr_writes,
                        contract.gpr_writes,
                    )
                } else {
                    let sync = verified_owner.map_or_else(GuestResidencyBoundarySync::full, |_| {
                        GuestResidencyBoundarySync::from_optional_callee_contract(
                            leaf_contracts.get(&target),
                        )
                    });
                    (sync.flush.gpr, sync.reload.gpr)
                };
                emit_resident_gpr_sync(&mut output, flush_gpr & local_gpr_mask, false)?;
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
                emit_resident_gpr_sync(&mut output, reload_gpr & local_gpr_mask, true)?;
                if call_return_checkpoint_needed(
                    target,
                    pc,
                    Some(callable_entries),
                    Some(exact_fpu_functions),
                    Some(checkpoint_free_callees),
                ) {
                    // The checkpoint can inspect or change the whole context, so
                    // linked calls do a full flush/reload even for a pure callee.
                    emit_resident_gpr_sync(&mut output, local_gpr_mask, false)?;
                    emit_call_return_checkpoint(&mut output, pc)?;
                    emit_resident_gpr_sync(&mut output, local_gpr_mask, true)?;
                }
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
        &BTreeSet::new(),
        &BTreeMap::new(),
        &ModuleTranslationOptions::default(),
    )
}

#[allow(clippy::too_many_arguments)]
fn lower_words_for_module_with_call_return_entries_and_exact_fpu_flat_reads(
    address: u32,
    bytes: &[u8],
    entries: &BTreeSet<u32>,
    call_return_entries: &BTreeSet<u32>,
    callable_entries: &BTreeMap<u32, u32>,
    exact_fpu_functions: &BTreeSet<u32>,
    checkpoint_free_callees: &BTreeSet<u32>,
    device_pointer_globals: &BTreeMap<i32, u32>,
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
            checkpoint_free_callees: Some(checkpoint_free_callees),
            device_pointer_globals: Some(device_pointer_globals),
            flat_ram_reads: options.flat_ram_reads,
            flat_ram_writes: options.flat_ram_writes,
            native_float: options.native_float,
            direct_indirect_calls: options.direct_indirect_calls,
            fused_paired_binary: options.fused_paired_binary,
            indexed_paired_binary: options.indexed_paired_binary,
            indexed_paired_ternary: options.indexed_paired_ternary,
            fused_paired_ternary: options.fused_paired_ternary,
            inline_scalar_single_binary: options.inline_scalar_single_binary,
            hw_float_conversions: options.hw_float_conversions,
            flat_view_only: options.flat_view_only,
            stack_r11_stores: options.stack_r11_stores,
            abi_call_markers: options.abi_call_sync,
            guest_return_macro: options.abi_exit_sync,
            fast_float_forms: options.fast_float_forms,
            bctrl_site_cache: options.bctrl_site_cache,
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
            checkpoint_free_callees: None,
            immediate_overrides,
            local_lane_profile: LocalLaneProfile::None,
            flat_ram_reads,
            flat_ram_writes: false,
            native_float: false,
            direct_indirect_calls: false,
            fused_paired_binary: false,
            indexed_paired_binary: false,
            indexed_paired_ternary: false,
            fused_paired_ternary: false,
            inline_scalar_single_binary: false,
            typed_region_gpr_mask: 0,
            device_pointer_globals: None,
            hw_float_conversions: false,
            flat_view_only: false,
            stack_r11_stores: false,
            abi_call_markers: false,
            guest_return_macro: false,
            fast_float_forms: false,
            bctrl_site_cache: false,
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
    checkpoint_free_callees: Option<&'a BTreeSet<u32>>,
    immediate_overrides: Option<&'a BTreeMap<u32, PpcImmediateOverride>>,
    local_lane_profile: LocalLaneProfile,
    flat_ram_reads: bool,
    flat_ram_writes: bool,
    native_float: bool,
    direct_indirect_calls: bool,
    fused_paired_binary: bool,
    indexed_paired_binary: bool,
    indexed_paired_ternary: bool,
    fused_paired_ternary: bool,
    inline_scalar_single_binary: bool,
    typed_region_gpr_mask: u32,
    device_pointer_globals: Option<&'a BTreeMap<i32, u32>>,
    hw_float_conversions: bool,
    flat_view_only: bool,
    stack_r11_stores: bool,
    /// Mark linked calls with `GALAXY_ABI_CALL(0x<pc>u);` (--abi-call-sync).
    abi_call_markers: bool,
    /// Lower `blr` to `GALAXY_GUEST_RETURN;` (--abi-exit-sync).
    guest_return_macro: bool,
    /// The `galaxy::fast` estimate, select and compare forms
    /// (--fast-float-forms).
    fast_float_forms: bool,
    /// Per-site direct entry cache at indirect calls (--bctrl-site-cache).
    bctrl_site_cache: bool,
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

        advance_constant_gprs(
            &mut constants,
            *word,
            *opcode,
            immediate_overrides.is_some_and(|overrides| overrides.contains_key(pc)),
        );
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

/// One instruction's effect on the local constant-GPR tracking shared by the
/// WGPIPE store runs and the static device-address discovery: every GPR the
/// instruction defines is dropped, then lis/addi/addis, ori/oris/xori/xoris
/// and `mr` chains propagate a known value; any branch drops everything.
fn advance_constant_gprs(
    constants: &mut [Option<u32>; 32],
    word: u32,
    opcode: Opcode,
    overridden: bool,
) {
    let prior = *constants;
    for definition in Ins::new(word, Extensions::gekko_broadway()).defs() {
        if let Argument::GPR(register) = definition {
            constants[register.0 as usize] = None;
        }
    }
    if !overridden {
        match opcode {
            Opcode::Addi | Opcode::Addis => {
                let target = gpr_rt(word) as usize;
                let base = gpr_ra(word);
                let base = if base == 0 {
                    Some(0u32)
                } else {
                    prior[base as usize]
                };
                let scale = if opcode == Opcode::Addis { 65_536 } else { 1 };
                constants[target] = base.map(|value| {
                    value.wrapping_add((signed_immediate(word).wrapping_mul(scale)) as u32)
                });
            }
            Opcode::Ori | Opcode::Oris | Opcode::Xori | Opcode::Xoris => {
                let source = prior[gpr_rt(word) as usize];
                let immediate = u32::from((word & 0xFFFF) as u16)
                    << if matches!(opcode, Opcode::Oris | Opcode::Xoris) {
                        16
                    } else {
                        0
                    };
                constants[gpr_ra(word) as usize] = source.map(|value| {
                    if matches!(opcode, Opcode::Ori | Opcode::Oris) {
                        value | immediate
                    } else {
                        value ^ immediate
                    }
                });
            }
            Opcode::Or if gpr_rt(word) == gpr_rb(word) => {
                constants[gpr_ra(word) as usize] = prior[gpr_rt(word) as usize];
            }
            _ => {}
        }
    }
    if Ins::new(word, Extensions::gekko_broadway()).is_branch() {
        constants.fill(None);
    }
}

/// Whether the flat guest view maps this address as RAM (MEM1 at its three
/// aliases, MEM2 at its three). Everything else faults under fastmem.
fn is_flat_ram_address(address: u32) -> bool {
    let offset = address & 0x0FFF_FFFF;
    match address & 0xF000_0000 {
        0x0000_0000 | 0x8000_0000 | 0xC000_0000 => offset < 0x0180_0000,
        0x1000_0000 | 0x9000_0000 | 0xD000_0000 => offset < 0x0400_0000,
        _ => false,
    }
}

/// SDA (r13-relative) words that every store in the program sets to one
/// device register base: GXInit's `__cpReg`, `__piReg`, `__peReg` and
/// `__memReg`. A load of one of these reads that constant. Keyed by the r13
/// displacement (r13 holds `_SDA_BASE_` for the whole program).
fn discover_device_pointer_globals(
    sections: &[DolSection],
    dol_bytes: &[u8],
    functions: &[FunctionRange],
) -> BTreeMap<i32, u32> {
    let mut slots: BTreeMap<i32, Option<u32>> = BTreeMap::new();
    for function in functions {
        let Some(bytes) = function_bytes(sections, dol_bytes, *function) else {
            continue;
        };
        let joins = internal_branch_targets(function.address, bytes);
        let mut constants: [Option<u32>; 32] = [None; 32];
        for (index, chunk) in bytes.chunks_exact(4).enumerate() {
            let pc = function.address + index as u32 * 4;
            let word = u32::from_be_bytes(chunk.try_into().expect("four-byte instruction"));
            let opcode = Ins::new(word, Extensions::gekko_broadway()).op;
            if joins.contains(&pc) {
                constants.fill(None);
            }
            let stores_r13 = gpr_ra(word) == 13
                && matches!(
                    opcode,
                    Opcode::Stw
                        | Opcode::Stwu
                        | Opcode::Sth
                        | Opcode::Sthu
                        | Opcode::Stb
                        | Opcode::Stbu
                        | Opcode::Stmw
                        | Opcode::Stfs
                        | Opcode::Stfsu
                        | Opcode::Stfd
                        | Opcode::Stfdu
                        | Opcode::PsqSt
                        | Opcode::PsqStu
                );
            if stores_r13 {
                let displacement = signed_immediate(word);
                let value = (opcode == Opcode::Stw)
                    .then(|| constants[gpr_rt(word) as usize])
                    .flatten()
                    .filter(|value| is_device_register_base(*value));
                let slot = slots.entry(displacement).or_insert(value);
                if *slot != value {
                    *slot = None;
                }
            }
            advance_constant_gprs(&mut constants, word, opcode, false);
        }
    }
    slots
        .into_iter()
        .filter_map(|(displacement, value)| value.map(|value| (displacement, value)))
        .collect()
}

/// A Hollywood/Flipper register address (cached or uncached physical alias)
/// other than the write-gather pipe.
fn is_device_register_base(address: u32) -> bool {
    matches!(address >> 24, 0x0C | 0x0D | 0xCC | 0xCD) && !is_wgpipe_address(address)
}

#[derive(Clone, Copy, PartialEq, Eq)]
struct DeviceScanState {
    constants: [Option<u32>; 32],
}

impl DeviceScanState {
    const UNKNOWN: Self = Self {
        constants: [None; 32],
    };

    fn meet(&mut self, other: &Self) -> bool {
        let mut changed = false;
        for (mine, theirs) in self.constants.iter_mut().zip(other.constants.iter()) {
            if mine.is_some() && *mine != *theirs {
                *mine = None;
                changed = true;
            }
        }
        changed
    }
}

/// The flat store helper for a D-form store from `base`: r1 (the stack
/// pointer) always addresses RAM, so its stores skip the write-gather-pipe
/// test (`fast::store_stack`).
fn flat_store_helper(base: u32) -> &'static str {
    if base == 1 {
        "store_stack"
    } else {
        "store"
    }
}

/// Integer loads and stores whose effective address is outside the RAM aliases of the flat guest view:
/// device registers (0xCC00xxxx, 0xCD00xxxx), the locked cache (0xE000xxxx) and anything unmapped. Under
/// fastmem such a site takes a host page fault on every execution. These sites lower to the checked helper,
/// which reaches the device directly and also admits RAM. A misclassified site is slower, never wrong. The
/// write-gather pipe keeps its flat form: `fast::store` tests for it inline and appends to the gather buffer.
///
/// GPR constants (lis/addi/ori chains, `mr`, and loads of the SDA device pointer globals) are propagated over
/// the function's control-flow graph and met at joins. A call clobbers the volatile GPRs. Interior entries that
/// no direct branch or call return explains (jump-table and other address-taken labels) start unknown. An
/// indexed access with a device-register constant on one side and an unknown index on the other
/// (`__IPCRegs[index]`) is a device access.
fn discover_static_device_access_pcs(
    address: u32,
    bytes: &[u8],
    entries: &BTreeSet<u32>,
    immediate_overrides: Option<&BTreeMap<u32, PpcImmediateOverride>>,
    device_pointer_globals: Option<&BTreeMap<i32, u32>>,
) -> BTreeSet<u32> {
    let words: Vec<u32> = bytes
        .chunks_exact(4)
        .map(|chunk| u32::from_be_bytes(chunk.try_into().expect("four-byte instruction")))
        .collect();
    let count = words.len();
    let end = address.wrapping_add(bytes.len() as u32);
    let index_of = |target: u32| {
        (target >= address && target < end && (target - address) % 4 == 0)
            .then(|| ((target - address) / 4) as usize)
    };
    let mut explained = BTreeSet::new();
    let mut successors: Vec<Vec<usize>> = Vec::with_capacity(count);
    for (index, &word) in words.iter().enumerate() {
        let pc = address + index as u32 * 4;
        let instruction = Ins::new(word, Extensions::gekko_broadway());
        let link = word & 1 != 0;
        let next = (index + 1 < count).then_some(index + 1);
        let bo = (word >> 21) & 0x1F;
        let mut edges = Vec::new();
        match instruction.op {
            Opcode::B => {
                if link {
                    edges.extend(next);
                } else if let Some(target) = index_of(branch_target(pc, word)) {
                    edges.push(target);
                    explained.insert(address + target as u32 * 4);
                }
            }
            Opcode::Bc => {
                if let Some(target) = index_of(conditional_branch_target(pc, word)) {
                    edges.push(target);
                    explained.insert(address + target as u32 * 4);
                }
                if link || !branch_is_unconditional(bo) {
                    edges.extend(next);
                }
            }
            Opcode::Bclr | Opcode::Bcctr => {
                if link || !branch_is_unconditional(bo) {
                    edges.extend(next);
                }
            }
            Opcode::Rfi => {}
            _ => edges.extend(next),
        }
        if link && instruction.is_branch() {
            if let Some(next) = next {
                explained.insert(address + next as u32 * 4);
            }
        }
        successors.push(edges);
    }

    let mut states: Vec<Option<DeviceScanState>> = vec![None; count];
    let mut pending = Vec::new();
    if count != 0 {
        states[0] = Some(DeviceScanState::UNKNOWN);
        pending.push(0usize);
    }
    for entry in entries {
        if explained.contains(entry) {
            continue;
        }
        if let Some(index) = index_of(*entry) {
            states[index] = Some(DeviceScanState::UNKNOWN);
            pending.push(index);
        }
    }
    let transfer = |state: &DeviceScanState, pc: u32, word: u32| {
        let instruction = Ins::new(word, Extensions::gekko_broadway());
        let opcode = instruction.op;
        let overridden = immediate_overrides.is_some_and(|overrides| overrides.contains_key(&pc));
        let mut next = *state;
        advance_constant_gprs(&mut next.constants, word, opcode, overridden);
        if instruction.is_branch() {
            // advance_constant_gprs drops everything at a branch for the
            // straight-line WGPIPE scan; branches define no GPR.
            next.constants = state.constants;
        }
        if !overridden && opcode == Opcode::Lwz && gpr_ra(word) == 13 {
            if let Some(value) =
                device_pointer_globals.and_then(|globals| globals.get(&signed_immediate(word)))
            {
                next.constants[gpr_rt(word) as usize] = Some(*value);
            }
        }
        if (instruction.is_branch() && word & 1 != 0) || opcode == Opcode::Sc {
            for volatile in [0usize, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12] {
                next.constants[volatile] = None;
            }
        }
        next
    };
    while let Some(index) = pending.pop() {
        let Some(state) = states[index] else {
            continue;
        };
        let pc = address + index as u32 * 4;
        let out = transfer(&state, pc, words[index]);
        for &successor in &successors[index] {
            match &mut states[successor] {
                Some(existing) => {
                    if existing.meet(&out) {
                        pending.push(successor);
                    }
                }
                slot @ None => {
                    *slot = Some(out);
                    pending.push(successor);
                }
            }
        }
    }

    let mut pcs = BTreeSet::new();
    for (index, &word) in words.iter().enumerate() {
        let Some(state) = states[index] else {
            continue;
        };
        let pc = address + index as u32 * 4;
        if immediate_overrides.is_some_and(|overrides| overrides.contains_key(&pc)) {
            continue;
        }
        let constants = &state.constants;
        let base = |register: u32| {
            if register == 0 {
                Some(0u32)
            } else {
                constants[register as usize]
            }
        };
        let opcode = Ins::new(word, Extensions::gekko_broadway()).op;
        let device = match opcode {
            Opcode::Lbz
            | Opcode::Lbzu
            | Opcode::Lhz
            | Opcode::Lhzu
            | Opcode::Lha
            | Opcode::Lhau
            | Opcode::Lwz
            | Opcode::Lwzu
            | Opcode::Stb
            | Opcode::Stbu
            | Opcode::Sth
            | Opcode::Sthu
            | Opcode::Stw
            | Opcode::Stwu => base(gpr_ra(word))
                .map(|value| value.wrapping_add(signed_immediate(word) as u32))
                .is_some_and(|ea| !is_flat_ram_address(ea) && !is_wgpipe_address(ea)),
            Opcode::Lbzx
            | Opcode::Lbzux
            | Opcode::Lhzx
            | Opcode::Lhzux
            | Opcode::Lhax
            | Opcode::Lhaux
            | Opcode::Lwzx
            | Opcode::Lwzux
            | Opcode::Stbx
            | Opcode::Stbux
            | Opcode::Sthx
            | Opcode::Sthux
            | Opcode::Stwx
            | Opcode::Stwux => match (base(gpr_ra(word)), constants[gpr_rb(word) as usize]) {
                (Some(left), Some(right)) => {
                    let ea = left.wrapping_add(right);
                    !is_flat_ram_address(ea) && !is_wgpipe_address(ea)
                }
                (Some(known), None) | (None, Some(known)) => is_device_register_base(known),
                (None, None) => false,
            },
            _ => false,
        };
        if device {
            pcs.insert(pc);
        }
    }
    pcs
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

/// Flag-based interrupt return: every checkpoint whose cold path the residency pass did not already rewrite
/// becomes the macro form, whose cold block tests `kPendingEventTransferBit` and returns. Every architectural
/// interrupt checkpoint (mtmsr) and system call is followed by the same test. A resident body's cold blocks
/// carry the test from the residency templates. Its remaining `galaxy::call_return_checkpoint(` occurrences
/// are none, so this pass only touches non-resident text.
pub fn apply_transfer_returns(body: String) -> String {
    let mut out = body
        .replace(
            "galaxy::call_return_checkpoint(services,",
            "GALAXY_CALL_RETURN_CHECKPOINT(services,",
        )
        .replace(
            "galaxy::branch_checkpoint_taken_budgeted(services,",
            "GALAXY_BRANCH_CHECKPOINT_TAKEN_BUDGETED(services,",
        )
        .replace(
            "galaxy::branch_checkpoint_taken(services,",
            "GALAXY_BRANCH_CHECKPOINT_TAKEN(services,",
        );
    for marker in [
        "galaxy::architectural_interrupt_checkpoint(services,",
        "galaxy::dispatch_system_call(services,",
        "galaxy::write_decrementer_and_notify(context,",
    ] {
        let mut rebuilt = String::with_capacity(out.len() + 256);
        let mut cursor = 0usize;
        while let Some(found) = out[cursor..].find(marker) {
            let at = cursor + found;
            let Some(end) = out[at..].find(");") else {
                break;
            };
            let stop = at + end + 2;
            rebuilt.push_str(&out[cursor..stop]);
            rebuilt.push_str(" GALAXY_RETURN_IF_TRANSFER_PENDING(services);");
            cursor = stop;
        }
        rebuilt.push_str(&out[cursor..]);
        out = rebuilt;
    }
    out
}

/// Development aid: `NEBULA_RESIDENCY_ADDRESS_RANGE=hexlow-hexhigh` limits the
/// residency pass to functions whose start address lies in that range, so a
/// miscompiled function can be bisected without touching the module options.
fn residency_address_selected(address: u32) -> bool {
    use std::sync::OnceLock;
    static RANGE: OnceLock<Option<(u32, u32)>> = OnceLock::new();
    let range = RANGE.get_or_init(|| {
        let text = std::env::var("NEBULA_RESIDENCY_ADDRESS_RANGE").ok()?;
        let (low, high) = text.split_once('-')?;
        Some((
            u32::from_str_radix(low.trim(), 16).ok()?,
            u32::from_str_radix(high.trim(), 16).ok()?,
        ))
    });
    range.map_or(true, |(low, high)| address >= low && address <= high)
}

/// Apply guest register residency (`crate::residency`) to one lowered body
/// when the option is on. Returns the body unchanged otherwise, and also when
/// the body has a shape the pass does not handle.
fn apply_residency_to_body(
    address: u32,
    body: String,
    entries: &BTreeSet<u32>,
    options: &ModuleTranslationOptions,
    abi: Option<&AbiResidency>,
) -> String {
    let _ = entries;
    if !options.guest_resident_integer || !residency_address_selected(address) {
        return body;
    }
    match crate::residency::apply_register_residency_with_abi(
        &body,
        options.guest_resident_float,
        abi,
    ) {
        Some(resident) => resident,
        None => body,
    }
}

/// The module manifest of the EABI analysis (`--abi-call-sync`,
/// `--abi-exit-sync`): the opt-out list and every non-ABI fact.
pub const ABI_CONTRACT_FILE: &str = "abi_contract.json";

/// EABI facts of every translated function (see `crate::ppc_dataflow`).
fn module_abi_facts(
    sections: &[DolSection],
    dol_bytes: &[u8],
    ranges: &[FunctionRange],
) -> Result<ModuleAbi, TranslationError> {
    let mut words_by_function = Vec::with_capacity(ranges.len());
    for function in ranges {
        let bytes = function_bytes(sections, dol_bytes, *function).ok_or(
            TranslationError::FunctionOutsideText {
                address: function.address,
            },
        )?;
        words_by_function.push(
            bytes
                .chunks_exact(4)
                .map(|chunk| u32::from_be_bytes(chunk.try_into().expect("four-byte instruction")))
                .collect::<Vec<_>>(),
        );
    }
    let jump_targets = discover_jump_targets(sections, dol_bytes, ranges, &words_by_function);
    let inputs = ranges
        .iter()
        .zip(&words_by_function)
        .map(|(function, words)| AbiFunctionInput {
            start: function.address,
            words,
            native: abi_native_function(function.address),
            jump_targets: jump_targets
                .get(&function.address)
                .unwrap_or(&crate::ppc_dataflow::NO_JUMP_TARGETS),
        })
        .collect::<Vec<_>>();
    let address_taken = discover_address_taken_functions(sections, dol_bytes, ranges);
    Ok(ModuleAbi::analyze(
        &inputs,
        &address_taken,
        &abi_inline_call,
    ))
}

/// The register effects of a call to one of CodeWarrior's register save and
/// restore helpers, which the analysis treats as part of the caller's own
/// prologue or epilogue: `bl _savegpr_N` reads r11 and rN..r31 (it stores
/// them below r11), `bl _restgpr_N` reads r11 and writes rN..r31 (the
/// translator lowers both in place); `bl _savefpr_N` / `bl _restfpr_N` do the
/// same for fN..f31 (they stay calls in the generated code, where an
/// unmarked call keeps full synchronization).
pub(crate) fn abi_inline_call(target: u32) -> Option<(Regs, Regs)> {
    let frame = Regs::gpr(11);
    if let Some((first, save)) = gpr_save_restore_entry(target) {
        let run = Regs {
            gpr: u32::MAX << first,
            ..Regs::default()
        };
        return Some(if save {
            (frame.union(run), Regs::default())
        } else {
            (frame, run)
        });
    }
    let (first, save) = fpr_save_restore_entry(target)?;
    let run = Regs {
        fpr: u32::MAX << first,
        ..Regs::default()
    };
    Some(if save {
        (frame.union(run), Regs::default())
    } else {
        (frame, run)
    })
}

/// `_savefpr_N` (`stfd fN..f31` below r11) and `_restfpr_N` (`lfd`) entry
/// points of RMGE01's `__save_fpr` / `__restore_fpr`: (first register, save).
fn fpr_save_restore_entry(target: u32) -> Option<(u32, bool)> {
    match target {
        0x8051_7464..=0x8051_74A8 if (target - 0x8051_7464) % 4 == 0 => {
            Some((14 + (target - 0x8051_7464) / 4, true))
        }
        0x8051_74B0..=0x8051_74F4 if (target - 0x8051_74B0) % 4 == 0 => {
            Some((14 + (target - 0x8051_74B0) / 4, false))
        }
        _ => None,
    }
}

/// The in-function targets of every function's computed jumps, from the
/// image's data: each data word that points inside a function past its start
/// is a possible target (`JumpTargets::any`; over-approximate, an unrelated
/// word only adds a target). A `bctr` whose table address the function
/// builds with `lis`/`addi` (or `ori`) and indexes with `lwzx` (CodeWarrior's
/// switch) gets the case labels of that table alone: the run of consecutive
/// data words from the table address that point inside the function.
fn discover_jump_targets(
    sections: &[DolSection],
    dol_bytes: &[u8],
    functions: &[FunctionRange],
    words_by_function: &[Vec<u32>],
) -> BTreeMap<u32, crate::ppc_dataflow::JumpTargets> {
    let mut sorted: Vec<FunctionRange> = functions.to_vec();
    sorted.sort_unstable_by_key(|function| function.address);
    // Data address -> (owning function, word) for every interior pointer.
    let mut pointers: BTreeMap<u32, (u32, u32)> = BTreeMap::new();
    for section in sections {
        if section.kind == DolSectionKind::Text {
            continue;
        }
        let Some(bytes) = dol_bytes.get(section.file_range()) else {
            continue;
        };
        for (index, chunk) in bytes.chunks_exact(4).enumerate() {
            let word = u32::from_be_bytes(chunk.try_into().expect("four-byte word"));
            if word % 4 != 0 {
                continue;
            }
            let owner = sorted.partition_point(|function| function.address <= word);
            let Some(function) = owner.checked_sub(1).map(|index| sorted[index]) else {
                continue;
            };
            if word > function.address && word < function.address + function.size {
                pointers.insert(section.address + index as u32 * 4, (function.address, word));
            }
        }
    }
    let mut targets: BTreeMap<u32, crate::ppc_dataflow::JumpTargets> = BTreeMap::new();
    for (owner, word) in pointers.values() {
        targets.entry(*owner).or_default().any.push(*word);
    }
    for jump_targets in targets.values_mut() {
        jump_targets.any.sort_unstable();
        jump_targets.any.dedup();
    }
    let table_run = |table: u32, owner: u32| -> Vec<u32> {
        let mut run = Vec::new();
        let mut address = table;
        while let Some((function, word)) = pointers.get(&address) {
            if *function != owner {
                break;
            }
            run.push(*word);
            address = address.wrapping_add(4);
        }
        run
    };
    for (function, words) in functions.iter().zip(words_by_function) {
        let has_computed_jump = words.iter().enumerate().any(|(index, word)| {
            crate::ppc_dataflow::effect(function.address + index as u32 * 4, *word)
                .is_some_and(|effect| effect.flow == crate::ppc_dataflow::Flow::IndirectJump)
        });
        if !has_computed_jump {
            continue;
        }
        let jump_targets = targets.entry(function.address).or_default();
        let mut labels: BTreeSet<u32> = words
            .iter()
            .enumerate()
            .filter_map(|(index, word)| {
                let pc = function.address + index as u32 * 4;
                match crate::ppc_dataflow::effect(pc, *word)?.flow {
                    crate::ppc_dataflow::Flow::Branch(target)
                    | crate::ppc_dataflow::Flow::CondBranch(target) => Some(target),
                    _ => None,
                }
            })
            .chain(jump_targets.any.iter().copied())
            .collect();
        // Addresses the code builds inside the function are computed-jump
        // targets too, and labels for the tracking: rescan until stable.
        loop {
            let scan = scan_computed_jumps(*function, words, &labels, &table_run);
            if scan.built.iter().all(|address| labels.contains(address)) {
                jump_targets.by_site = scan.by_site;
                jump_targets.loaded = scan.loaded;
                jump_targets.any.extend(scan.built);
                jump_targets.any.sort_unstable();
                jump_targets.any.dedup();
                break;
            }
            labels.extend(scan.built);
        }
    }
    targets
}

/// What one pass of `scan_computed_jumps` found.
struct ComputedJumpScan {
    by_site: BTreeMap<u32, Vec<u32>>,
    loaded: BTreeSet<u32>,
    built: Vec<u32>,
}

/// Straight-line tracking over one function, reset at every label and call:
/// constants (lis/addi/ori), switch index bounds (`cmplwi rX,N` then `bgt`,
/// kept through `slwi rY,rX,2`), table entries (`lwzx` from a constant base),
/// pointers loaded from memory, and what reaches CTR before each `bctr`.
fn scan_computed_jumps(
    function: FunctionRange,
    words: &[u32],
    labels: &BTreeSet<u32>,
    table_run: &dyn Fn(u32, u32) -> Vec<u32>,
) -> ComputedJumpScan {
    let interior = |address: u32| {
        address % 4 == 0 && address > function.address && address < function.address + function.size
    };
    let mut scan = ComputedJumpScan {
        by_site: BTreeMap::new(),
        loaded: BTreeSet::new(),
        built: Vec::new(),
    };
    let mut constant: [Option<u32>; 32] = [None; 32];
    let mut bound: [Option<u32>; 32] = [None; 32];
    // (table address, entries when bounded)
    let mut table_entry: [Option<(u32, Option<u32>)>; 32] = [None; 32];
    let mut loaded = [false; 32];
    let mut ctr_table: Option<(u32, Option<u32>)> = None;
    let mut ctr_loaded = false;
    let mut compare: Option<(u32, usize, u32)> = None;
    for (index, word) in words.iter().enumerate() {
        let pc = function.address + index as u32 * 4;
        if labels.contains(&pc) {
            constant = [None; 32];
            bound = [None; 32];
            table_entry = [None; 32];
            loaded = [false; 32];
            ctr_table = None;
            ctr_loaded = false;
            compare = None;
        }
        let rd = gpr_rt(*word) as usize;
        let ra = gpr_ra(*word) as usize;
        let rb = ((*word >> 11) & 31) as usize;
        let immediate = *word & 0xFFFF;
        let primary = *word >> 26;
        let extended = (*word >> 1) & 0x3FF;
        let pending_compare = compare.take();
        let mut known = None;
        let mut entry = None;
        match primary {
            // cmplwi crfD, rA, N (the range check of a switch)
            10 if (*word >> 21) & 1 == 0 => {
                compare = Some(((*word >> 23) & 7, ra, immediate));
            }
            // bgt crfD, default: the index is at most N on the fall-through.
            16 if (*word >> 21) & 30 == 12 => {
                if let Some((field, register, limit)) = pending_compare {
                    if (*word >> 16) & 31 == field * 4 + 1 {
                        bound[register] = Some(limit);
                    }
                }
            }
            // lis rD, hi / addis rD, rA, hi
            15 => {
                known = if ra == 0 {
                    Some(immediate << 16)
                } else {
                    constant[ra].map(|base| base.wrapping_add(immediate << 16))
                };
            }
            // addi rD, rA, lo
            14 if ra != 0 => {
                known = constant[ra]
                    .map(|base| base.wrapping_add(immediate as u16 as i16 as i32 as u32));
            }
            // ori rA, rS, lo: the destination is rA.
            24 => {
                let value = constant[rd].map(|base| base | immediate);
                if let Some(address) = value.filter(|address| interior(*address)) {
                    scan.built.push(address);
                }
                constant[ra] = value;
                bound[ra] = None;
                table_entry[ra] = None;
                loaded[ra] = false;
                continue;
            }
            // slwi rA, rS, 2 (rlwinm rA, rS, 2, 0, 29): a scaled index.
            21 if *word & 0xFFFE == (2 << 11) | (29 << 1) => {
                let value = bound[rd];
                bound[ra] = value;
                constant[ra] = None;
                table_entry[ra] = None;
                loaded[ra] = false;
                continue;
            }
            // lwzx rD, rA, rB: an entry of a table at a known address.
            31 if extended == 23 => {
                entry = match (constant[ra], constant[rb]) {
                    (Some(table), _) => Some((table, bound[rb].map(|limit| limit + 1))),
                    (None, Some(table)) => Some((table, bound[ra].map(|limit| limit + 1))),
                    (None, None) => None,
                };
            }
            // mtctr rS
            31 if extended == 467 && ((*word >> 11) & 0x3FF) == (9 << 5) => {
                ctr_table = table_entry[rd];
                ctr_loaded = loaded[rd];
                continue;
            }
            _ => {}
        }
        let Some(effect) = crate::ppc_dataflow::effect(pc, *word) else {
            constant = [None; 32];
            bound = [None; 32];
            table_entry = [None; 32];
            loaded = [false; 32];
            ctr_table = None;
            ctr_loaded = false;
            continue;
        };
        if effect.flow == crate::ppc_dataflow::Flow::IndirectJump {
            if let Some((table, entries)) = ctr_table {
                let mut run = table_run(table, function.address);
                if let Some(entries) = entries {
                    run.truncate(entries as usize);
                }
                if !run.is_empty() {
                    scan.by_site.insert(pc, run);
                }
            }
            if ctr_loaded {
                scan.loaded.insert(pc);
            }
        }
        if effect.writes.ctr {
            ctr_table = None;
            ctr_loaded = false;
        }
        for register in 0..32 {
            if effect.writes.gpr & (1 << register) != 0 {
                constant[register] = None;
                bound[register] = None;
                table_entry[register] = None;
                loaded[register] = false;
            }
        }
        if matches!(
            effect.flow,
            crate::ppc_dataflow::Flow::Call(_) | crate::ppc_dataflow::Flow::IndirectCall
        ) {
            constant = [None; 32];
            bound = [None; 32];
            table_entry = [None; 32];
            loaded = [false; 32];
            ctr_table = None;
            ctr_loaded = false;
        }
        if let Some(value) = known {
            if interior(value) {
                scan.built.push(value);
            }
            constant[rd] = Some(value);
        }
        if let Some(table) = entry {
            table_entry[rd] = Some(table);
        }
        // lwz / lwzu / lwzx / lwzux: a word loaded from memory.
        if primary == 32 || primary == 33 || (primary == 31 && (extended == 23 || extended == 55)) {
            loaded[rd] = true;
        }
    }
    scan.built.sort_unstable();
    scan.built.dedup();
    scan
}

/// A function whose generated body is not its guest code: a host
/// substitution or a pure helper the caller invokes directly.
fn abi_native_function(address: u32) -> bool {
    native_function_body(address).is_some() || native_pure_helper(address).is_some()
}

/// The residency plan of one function under the module's EABI facts.
fn function_abi_residency(
    module_abi: &ModuleAbi,
    address: u32,
    options: &ModuleTranslationOptions,
) -> AbiResidency {
    AbiResidency {
        call_sites: if options.abi_call_sync {
            module_abi.call_sites(address)
        } else {
            BTreeMap::new()
        },
        exit: if options.abi_exit_sync {
            module_abi.exit_stores(address)
        } else {
            None
        },
    }
}

/// Possible indirect-call targets: functions whose start address appears as
/// a word in the image's data (vtables, callback and jump tables, data
/// pointers) or is built in code by `lis` followed by `addi`/`ori` on the
/// same register. Over-approximate: a spurious match only keeps a function in
/// the set. Instruction words are not data: every `lwz` encodes a value in
/// 0x80000000..0x83FFFFFF and would match some function start.
fn discover_address_taken_functions(
    sections: &[DolSection],
    dol_bytes: &[u8],
    functions: &[FunctionRange],
) -> BTreeSet<u32> {
    let starts: BTreeSet<u32> = functions.iter().map(|function| function.address).collect();
    let mut taken = BTreeSet::new();
    for section in sections {
        let Some(bytes) = dol_bytes.get(section.file_range()) else {
            continue;
        };
        let mut high: [Option<u32>; 32] = [None; 32];
        for chunk in bytes.chunks_exact(4) {
            let word = u32::from_be_bytes(chunk.try_into().expect("four-byte word"));
            if section.kind != DolSectionKind::Text {
                if starts.contains(&word) {
                    taken.insert(word);
                }
                continue;
            }
            let rd = gpr_rt(word) as usize;
            let ra = gpr_ra(word) as usize;
            let immediate = word & 0xFFFF;
            let value = match word >> 26 {
                // lis rD, hi
                15 if ra == 0 => {
                    high[rd] = Some(immediate << 16);
                    continue;
                }
                // addi rD, rA, lo
                14 if ra != 0 => {
                    high[ra].map(|base| base.wrapping_add(immediate as u16 as i16 as i32 as u32))
                }
                // ori rA, rS, lo
                24 => high[rd].map(|base| base | immediate),
                _ => None,
            };
            if let Some(value) = value.filter(|value| starts.contains(value)) {
                taken.insert(value);
            }
        }
    }
    taken
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
        checkpoint_free_callees,
        immediate_overrides,
        local_lane_profile,
        flat_ram_reads,
        flat_ram_writes,
        native_float,
        direct_indirect_calls,
        fused_paired_binary,
        indexed_paired_binary,
        indexed_paired_ternary,
        fused_paired_ternary,
        inline_scalar_single_binary,
        typed_region_gpr_mask,
        device_pointer_globals,
        hw_float_conversions,
        flat_view_only,
        stack_r11_stores,
        abi_call_markers,
        guest_return_macro,
        fast_float_forms,
        bctrl_site_cache,
    } = config;
    // Suffix of the flat-view float load/store helpers: `_hw` converts binary32
    // lanes with the host's cvtss2sd/cvtsd2ss.
    let float_conversion = if hw_float_conversions { "_hw" } else { "" };
    // The paired/scalar local-lane proofs own their exact arithmetic text;
    // native float only replaces the general lowering.
    let native_float = native_float && local_lane_profile == LocalLaneProfile::None;
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
    if flat_ram_reads || flat_ram_writes {
        // Each invocation binds its own GuestMemoryV1 for its lifetime. The
        // binding precedes interior-entry dispatch. The writable view and the
        // read view alias the same shared MEM1/MEM2 sections.
        output.push_str(
            "    [[maybe_unused]] std::byte* const flat_guest_base = galaxy::fast::flat_base(memory);\n    [[maybe_unused]] const std::byte* flat_guest_read_base = flat_guest_base;\n",
        );
    }
    // Budgeted backward-branch checkpoints (galaxy::kBranchCheckpointBudget):
    // the full checkpoint test runs every N backward branches of this
    // activation; the other iterations cost a decrement and a branch.
    output.push_str(
        "    [[maybe_unused]] std::uint32_t checkpoint_budget__ = galaxy::kBranchCheckpointBudget;\n",
    );
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
    if !entries.is_empty() {
        writeln!(
            output,
            "    if (context->pc != 0x{address:08X}u) [[unlikely]] {{\n    switch (context->pc) {{"
        )
        .map_err(|_| TranslationError::Formatting)?;
        for entry in entries {
            debug_assert!(*entry > address && *entry < function_end && *entry % 4 == 0);
            branch_targets.insert(*entry);
            let destination = if external_call_return_entries.contains(entry) {
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
    let wgpipe_store_runs = if local_lane_profile == LocalLaneProfile::None {
        discover_wgpipe_store_runs(address, bytes, &branch_targets, immediate_overrides)
    } else {
        BTreeMap::new()
    };
    let fused_compare_branches = if local_lane_profile == LocalLaneProfile::None
        && immediate_overrides.is_none()
        && typed_region_gpr_mask == 0
    {
        fusible_compare_branches(address, bytes, &branch_targets)
    } else {
        BTreeMap::new()
    };
    let fused_compares = fused_compare_branches
        .values()
        .map(|(compare_pc, _)| *compare_pc)
        .collect::<BTreeSet<_>>();
    let fused_records = if local_lane_profile == LocalLaneProfile::None
        && immediate_overrides.is_none()
        && typed_region_gpr_mask == 0
    {
        fusible_record_branches(address, bytes, &branch_targets)
    } else {
        BTreeSet::new()
    };
    // Branch pc -> the GPR its fused record form wrote.
    let mut fused_record_registers: BTreeMap<u32, u32> = BTreeMap::new();
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
    let static_device_pcs =
        if (flat_ram_reads || flat_ram_writes) && local_lane_profile == LocalLaneProfile::None {
            discover_static_device_access_pcs(
                address,
                bytes,
                entries,
                immediate_overrides,
                device_pointer_globals,
            )
        } else {
            BTreeSet::new()
        };
    let mut final_instruction_is_terminal = false;
    for (index, chunk) in bytes.chunks_exact(4).enumerate() {
        let pc = address + index as u32 * 4;
        if wgpipe_store_continuations.contains(&pc) {
            continue;
        }
        let word = u32::from_be_bytes(chunk.try_into().expect("four-byte instruction"));
        let instruction = Ins::new(word, Extensions::gekko_broadway());
        // A statically known device address never goes through the flat view:
        // the checked helper reaches the device without a host page fault.
        let static_device = static_device_pcs.contains(&pc);
        let flat_ram_reads = flat_ram_reads && !static_device;
        let flat_ram_writes = flat_ram_writes && !static_device;
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
        if has_reentry_label {
            if typed_region_gpr_mask != 0 {
                emit_resident_gpr_sync(&mut output, typed_region_gpr_mask, false)?;
            }
            writeln!(output, "label_{pc:08X}:").map_err(|_| TranslationError::Formatting)?;
            if typed_region_gpr_mask != 0 {
                emit_resident_gpr_sync(&mut output, typed_region_gpr_mask, true)?;
            }
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
            output.push_str(&site_hook_call(pc, "CinemaAndHud", 0));
        }
        // RMGE01's animation is complete here. Adjust only the identified
        // backing panes before their matrices and hit regions are calculated;
        // the original load/call and continuation still run.
        if address == 0x8036_7D28
            && function_end == 0x8036_7E84
            && pc == 0x8036_7D78
            && word == 0x807F_0004
        {
            output.push_str(&site_hook_call(pc, "LayoutBackings", 0));
        }
        // MultiSceneActor::calcAnim has set its model's base matrix and scale;
        // r3 still holds the J3DModel and r31 the actor.
        if (address, function_end, pc, word) == (0x8034_103C, 0x8034_10C8, 0x8034_10AC, 0x807F_0034)
        {
            output.push_str(&site_hook_call(pc, "MultiSceneModel", 0));
        }
        // Home RSO Pane::CalculateMtx entry. Runtime geometry checks tell its
        // main backing apart from pointers.
        if (address, function_end, pc, word) == (0x8100_00D8, 0x8101_B120, 0x8101_18F8, 0x9421_FF20)
        {
            output.push_str(&site_hook_call(pc, "HomeBackings", 0));
        }
        // The Home Pane global matrix is complete on every path to this join
        // and children are not calculated yet. The original lbz still runs.
        if (address, function_end, pc, word) == (0x8100_00D8, 0x8101_B120, 0x8101_1B1C, 0x881D_00CF)
        {
            output.push_str(&site_hook_call(pc, "HomeVertical", 0));
        }
        let typed_pure_access = if typed_region_gpr_mask != 0
            && !has_reentry_label
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
        if rmge01_native_screen_width_call(address, function_end, pc, word) {
            output.push_str(&site_hook_call(pc, "NativeScreenWidthEnter", 0));
        }

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
                    output.push_str(&site_hook_call(pc, "FourThreeAspectRead", 0));
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
                if flat_ram_reads || target > 16 {
                    // One statement per register (no runtime-indexed GPR access),
                    // so the register residency pass can keep the body resident.
                    for reg in target..32 {
                        let offset = (reg - target) * 4;
                        let load = integer_guest_load_expression(
                            4,
                            &format!("{ea} + 0x{offset:08X}u"),
                            pc,
                            flat_ram_reads,
                        );
                        writeln!(output, "    context->gpr[{reg}] = {load};")
                            .map_err(|_| TranslationError::Formatting)?;
                    }
                } else {
                    writeln!(output,
                        "    galaxy::guest_load_multiple_gprs<{target}u>(context, memory, {ea}, services, 0x{pc:08X}u);")
                        .map_err(|_| TranslationError::Formatting)?;
                }
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
                    let (helper, width) = match instruction.op {
                        Opcode::Stb | Opcode::Stbu => ("guest_store_u8", "std::uint8_t"),
                        Opcode::Sth | Opcode::Sthu => ("guest_store_u16", "std::uint16_t"),
                        Opcode::Stw | Opcode::Stwu => ("guest_store_u32", "std::uint32_t"),
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
                    if flat_ram_writes {
                        let store = flat_store_helper(base);
                        writeln!(
                            output,
                            "    galaxy::fast::{store}<{width}>(flat_guest_base, memory, {ea}, {value}, services, 0x{pc:08X}u);"
                        )
                    } else {
                        writeln!(
                            output,
                            "    galaxy::{helper}(memory, {ea}, {value}, services, 0x{pc:08X}u);"
                        )
                    }
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
                // A flat-view-only module stores every stmw register by
                // register through the view, like rS > 24 always does.
                if source <= 24 && !(flat_view_only && flat_ram_writes) {
                    writeln!(output,
                        "    galaxy::guest_store_multiple_gprs<{source}u>(context, memory, {ea}, services, 0x{pc:08X}u);")
                        .map_err(|_| TranslationError::Formatting)?;
                } else {
                    // One statement per register (no runtime-indexed GPR access),
                    // so the register residency pass can keep the body resident.
                    for reg in source..32 {
                        let offset = (reg - source) * 4;
                        if flat_ram_writes {
                            let store = flat_store_helper(base);
                            writeln!(output,
                                "    galaxy::fast::{store}<std::uint32_t>(flat_guest_base, memory, {ea} + 0x{offset:08X}u, context->gpr[{reg}], services, 0x{pc:08X}u);")
                        } else {
                            writeln!(output,
                                "    galaxy::guest_store_u32(memory, {ea} + 0x{offset:08X}u, context->gpr[{reg}], services, 0x{pc:08X}u);")
                        }
                        .map_err(|_| TranslationError::Formatting)?;
                    }
                }
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
                    let (helper, width, value) = match instruction.op {
                        Opcode::Stbx | Opcode::Stbux => (
                            "guest_store_u8",
                            "std::uint8_t",
                            format!("static_cast<std::uint8_t>(context->gpr[{source}])"),
                        ),
                        Opcode::Sthx | Opcode::Sthux => (
                            "guest_store_u16",
                            "std::uint16_t",
                            format!("static_cast<std::uint16_t>(context->gpr[{source}])"),
                        ),
                        Opcode::Stwx | Opcode::Stwux => (
                            "guest_store_u32",
                            "std::uint32_t",
                            format!("context->gpr[{source}]"),
                        ),
                        _ => unreachable!(),
                    };
                    if flat_ram_writes {
                        writeln!(
                            output,
                            "    galaxy::fast::store<{width}>(flat_guest_base, memory, {ea}, {value}, services, 0x{pc:08X}u);"
                        )
                    } else {
                        writeln!(
                            output,
                            "    galaxy::{helper}(memory, {ea}, {value}, services, 0x{pc:08X}u);"
                        )
                    }
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
                if is_load && flat_ram_reads {
                    writeln!(
                        output,
                        "    galaxy::fast::psq_load{float_conversion}(flat_guest_read_base, context, {register}u, memory, {ea}, {gqr_index}u, {one_element}, {d_form}, services, 0x{pc:08X}u);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                } else if is_load {
                    writeln!(
                        output,
                        "    galaxy::psq_load(context, {register}u, memory, {ea}, {gqr_index}u, {one_element}, {d_form}, services, 0x{pc:08X}u);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                } else if flat_ram_writes {
                    writeln!(
                        output,
                        "    galaxy::fast::psq_store{float_conversion}(flat_guest_base, context, {register}u, memory, {ea}, {gqr_index}u, {one_element}, {d_form}, services, 0x{pc:08X}u);"
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
                    if flat_ram_reads {
                        writeln!(
                            output,
                            "    galaxy::fast::load_fpr_single{float_conversion}(flat_guest_read_base, context, {target}u, memory, {ea}, services, 0x{pc:08X}u);"
                        )
                    } else {
                        writeln!(
                            output,
                            "    galaxy::load_fpr_single(context, {target}u, memory, {ea}, services, 0x{pc:08X}u);"
                        )
                    }
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
                        output.push_str(&site_hook_call(
                            pc,
                            "LayoutConstant",
                            (lane as u32) | (u32::from(half) << 8),
                        ));
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
                        output.push_str(&site_hook_call(pc, "Nw4rCanvasWidth", 0));
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
                        output.push_str(&site_hook_call(pc, "CameraAspect", 0));
                    }
                } else if flat_ram_reads {
                    writeln!(
                        output,
                        "    context->fpr_bits[{target}] = galaxy::fast::load_u64(flat_guest_read_base, memory, {ea}, services, 0x{pc:08X}u);"
                    )
                    .map_err(|_| TranslationError::Formatting)?;
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
                    if flat_ram_writes {
                        writeln!(
                            output,
                            "    galaxy::fast::store_fpr_single{float_conversion}(flat_guest_base, context, {source}u, memory, {ea}, services, 0x{pc:08X}u);"
                        )
                    } else {
                        writeln!(
                            output,
                            "    galaxy::store_fpr_single(context, {source}u, memory, {ea}, services, 0x{pc:08X}u);"
                        )
                    }
                    .map_err(|_| TranslationError::Formatting)?;
                } else if flat_ram_writes {
                    let store = flat_store_helper(base);
                    writeln!(
                        output,
                        "    galaxy::fast::{store}<std::uint64_t>(flat_guest_base, memory, {ea}, context->fpr_bits[{source}], services, 0x{pc:08X}u);"
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
                        if flat_ram_reads {
                            writeln!(
                                output,
                                "    galaxy::fast::load_fpr_single{float_conversion}(flat_guest_read_base, context, {register}u, memory, {ea}, services, 0x{pc:08X}u);"
                            )
                        } else {
                            writeln!(
                                output,
                                "    galaxy::load_fpr_single(context, {register}u, memory, {ea}, services, 0x{pc:08X}u);"
                            )
                        }
                        .map_err(|_| TranslationError::Formatting)?;
                    }
                    Opcode::Lfdx | Opcode::Lfdux => {
                        if flat_ram_reads {
                            writeln!(
                                output,
                                "    context->fpr_bits[{register}] = galaxy::fast::load_u64(flat_guest_read_base, memory, {ea}, services, 0x{pc:08X}u);"
                            )
                        } else {
                            writeln!(
                                output,
                                "    context->fpr_bits[{register}] = galaxy::guest_load_u64(memory, {ea}, services, 0x{pc:08X}u);"
                            )
                        }
                        .map_err(|_| TranslationError::Formatting)?;
                    }
                    Opcode::Stfsx | Opcode::Stfsux => {
                        if flat_ram_writes {
                            writeln!(
                                output,
                                "    galaxy::fast::store_fpr_single{float_conversion}(flat_guest_base, context, {register}u, memory, {ea}, services, 0x{pc:08X}u);"
                            )
                        } else {
                            writeln!(
                                output,
                                "    galaxy::store_fpr_single(context, {register}u, memory, {ea}, services, 0x{pc:08X}u);"
                            )
                        }
                        .map_err(|_| TranslationError::Formatting)?;
                    }
                    Opcode::Stfdx | Opcode::Stfdux => {
                        if flat_ram_writes {
                            writeln!(
                                output,
                                "    galaxy::fast::store<std::uint64_t>(flat_guest_base, memory, {ea}, context->fpr_bits[{register}], services, 0x{pc:08X}u);"
                            )
                        } else {
                            writeln!(
                                output,
                                "    galaxy::guest_store_u64(memory, {ea}, context->fpr_bits[{register}], services, 0x{pc:08X}u);"
                            )
                        }
                        .map_err(|_| TranslationError::Formatting)?;
                    }
                    _ => unreachable!(),
                }
                if update {
                    writeln!(output, "    context->gpr[{base}] = {ea};")
                        .map_err(|_| TranslationError::Formatting)?;
                }
            }
            Opcode::Dcbf | Opcode::Dcbst => {
                // Flush-based coherency: the flushed line is what the GP and
                // DSP observe, so this is where dirty tracking is published.
                let base = gpr_ra(word);
                let index = gpr_rb(word);
                let ea = emit_indexed_effective_address(&mut output, pc, base, index)?;
                writeln!(output, "    galaxy::fast::dcache_flush_line(memory, {ea});")
                    .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Dcbi | Opcode::Icbi => {
                // Every store is already in RAM: invalidating a line has no
                // effect the guest can observe, and no code is modified.
            }
            Opcode::Isync | Opcode::Sync => {
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
                if flat_view_only && flat_ram_writes {
                    writeln!(
                        output,
                        "    galaxy::fast::zero_line(flat_guest_base, memory, {ea}, services, 0x{pc:08X}u);"
                    )
                } else {
                    writeln!(
                        output,
                        "    galaxy::guest_zero(memory, {ea} & 0xFFFFFFE0u, 32u, services, 0x{pc:08X}u);"
                    )
                }
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
                if native_float {
                    writeln!(
                        output,
                        "    galaxy::fast::scalar_frsp(context, {target}u, {source}u, {}, services, 0x{pc:08X}u);",
                        record_bit(word)
                    )
                } else {
                    writeln!(
                        output,
                        "    const galaxy::PpcFloatResult result = galaxy::ppc_round_f64_to_f32(context->fpr_bits[{source}], context->fpscr);\n    galaxy::ppc_commit_scalar_result(context, {target}u, result, false, {}, services, 0x{pc:08X}u);",
                        record_bit(word)
                    )
                }
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::Fctiwz => {
                let target = gpr_rt(word);
                let source = gpr_rb(word);
                if native_float {
                    writeln!(
                        output,
                        "    galaxy::fast::fctiwz(context, {target}u, {source}u, {}, services, 0x{pc:08X}u);",
                        record_bit(word)
                    )
                } else {
                    writeln!(
                        output,
                        "    const galaxy::PpcFloatResult result = galaxy::ppc_f64_to_i32_round_zero(context->fpr_bits[{source}], context->fpscr);\n    galaxy::ppc_commit_scalar_result(context, {target}u, result, false, {}, services, 0x{pc:08X}u);",
                        record_bit(word)
                    )
                }
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
                if native_float {
                    writeln!(
                        output,
                        "    galaxy::fast::scalar_binary<galaxy::PpcFloatBinaryOperation::{operation}, {single}>(context, {target}u, {left}u, {right}u, {}, services, 0x{pc:08X}u);",
                        record_bit(word)
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                } else if single {
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
                if native_float {
                    writeln!(
                        output,
                        "    galaxy::fast::scalar_ternary<galaxy::PpcFloatTernaryOperation::{operation}, {single}>(context, {target}u, {multiplicand}u, {multiplier}u, {addend}u, {}, services, 0x{pc:08X}u);",
                        record_bit(word)
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                } else if single && local_lane_profile != LocalLaneProfile::None {
                    // This guarded local-lane proof already establishes exact
                    // binary32 sources and consumes these captured expressions.
                    writeln!(
                        output,
                        "    const std::uint32_t multiplicand = galaxy::require_single_precision_bits(context->fpr_bits[{multiplicand}], services, 0x{pc:08X}u);\n    const std::uint32_t multiplier = galaxy::require_single_precision_bits(context->fpr_bits[{multiplier}], services, 0x{pc:08X}u);\n    const std::uint32_t addend = galaxy::require_single_precision_bits(context->fpr_bits[{addend}], services, 0x{pc:08X}u);\n    const galaxy::PpcFloatResult result = galaxy::ppc_f32_ternary(galaxy::PpcFloatTernaryOperation::{operation}, multiplicand, multiplier, addend, context->fpscr);\n    galaxy::ppc_commit_scalar_result(context, {target}u, result, true, {}, services, 0x{pc:08X}u);",
                        record_bit(word)
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                } else if single {
                    writeln!(
                        output,
                        "    galaxy::ppc_commit_scalar_single_ternary(context, {target}u, galaxy::PpcFloatTernaryOperation::{operation}, {multiplicand}u, {multiplier}u, {addend}u, {}, services, 0x{pc:08X}u);",
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
                if fast_float_forms {
                    writeln!(
                        output,
                        "    context->fpr_bits[{target}] = galaxy::fast::select_nonnegative(context->fpr_bits[{selector}]) ? context->fpr_bits[{nonnegative}] : context->fpr_bits[{negative}];"
                    )
                } else {
                    writeln!(
                        output,
                        "    const std::uint64_t selector = context->fpr_bits[{selector}];\n    const bool select_nonnegative = !galaxy::f64_is_nan(selector) && ((selector & 0x8000000000000000ull) == 0 || (selector & 0x7FFFFFFFFFFFFFFFull) == 0);\n    context->fpr_bits[{target}] = select_nonnegative ? context->fpr_bits[{nonnegative}] : context->fpr_bits[{negative}];"
                    )
                }
                .map_err(|_| TranslationError::Formatting)?;
                if record_bit(word) {
                    output.push_str("    galaxy::update_cr1_from_fpscr(context);\n");
                }
            }
            Opcode::Fres if fast_float_forms => {
                let target = gpr_rt(word);
                let source = gpr_rb(word);
                writeln!(
                    output,
                    "    {{ const std::uint64_t estimate = galaxy::fast::reciprocal_single_bits(context->fpr_bits[{source}]); context->fpr_bits[{target}] = estimate; if (galaxy::fast::duplicates_single(context)) context->ps1_bits[{target}] = estimate; }}"
                )
                .map_err(|_| TranslationError::Formatting)?;
                if record_bit(word) {
                    output.push_str("    galaxy::update_cr1_from_fpscr(context);\n");
                }
            }
            Opcode::Frsqrte if fast_float_forms => {
                let target = gpr_rt(word);
                let source = gpr_rb(word);
                writeln!(
                    output,
                    "    context->fpr_bits[{target}] = galaxy::fast::reciprocal_sqrt_bits(context->fpr_bits[{source}]);"
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
                if native_float {
                    let lane = match instruction.op {
                        Opcode::PsMuls0 => "Lane0",
                        Opcode::PsMuls1 => "Lane1",
                        _ => "Each",
                    };
                    writeln!(
                        output,
                        "    galaxy::fast::paired_binary<galaxy::PpcFloatBinaryOperation::{operation}, galaxy::fast::PairedLane::{lane}>(context, {target}u, {left}u, {right}u, {}, services, 0x{pc:08X}u);",
                        record_bit(word)
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                } else if indexed_paired_binary && !fused_paired_binary {
                    let operation_index = match instruction.op {
                        Opcode::PsAdd => 0u32,
                        Opcode::PsSub => 1,
                        Opcode::PsMul | Opcode::PsMuls0 | Opcode::PsMuls1 => 2,
                        Opcode::PsDiv => 3,
                        _ => unreachable!(),
                    };
                    let descriptor = target
                        | (left << 5)
                        | (right << 10)
                        | (operation_index << 15)
                        | (u32::from(right_lane0 == "ps1_bits") << 17)
                        | (u32::from(right_lane1 == "ps1_bits") << 18)
                        | (u32::from(record_bit(word)) << 19);
                    writeln!(output,
                        "    galaxy::ppc_execute_indexed_paired_binary(context, 0x{descriptor:05X}u, services, 0x{pc:08X}u);")
                        .map_err(|_| TranslationError::Formatting)?;
                } else {
                    writeln!(
                    output,
                    "    galaxy::require_paired_single_mode(context, false, services, 0x{pc:08X}u);\n    const std::uint32_t left_ps0 = galaxy::require_single_precision_bits(context->fpr_bits[{left}], services, 0x{pc:08X}u);\n    const std::uint32_t left_ps1 = galaxy::require_single_precision_bits(context->ps1_bits[{left}], services, 0x{pc:08X}u);\n    const std::uint32_t right_ps0 = galaxy::require_single_precision_bits(context->{right_lane0}[{right}], services, 0x{pc:08X}u);\n    const std::uint32_t right_ps1 = galaxy::require_single_precision_bits(context->{right_lane1}[{right}], services, 0x{pc:08X}u);\n{arithmetic}"
                )
                .map_err(|_| TranslationError::Formatting)?;
                }
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
                let arithmetic = if fused_paired_ternary {
                    format!(
                        "    galaxy::ppc_commit_paired_ternary_result(context, {target}u, galaxy::PpcFloatTernaryOperation::{operation}, multiplicand_ps0, multiplicand_ps1, multiplier_ps0, multiplier_ps1, addend_ps0, addend_ps1, {}, services, 0x{pc:08X}u);",
                        record_bit(word)
                    )
                } else {
                    format!(
                        "    const galaxy::PpcFloatResult result_ps0 = galaxy::ppc_f32_ternary(galaxy::PpcFloatTernaryOperation::{operation}, multiplicand_ps0, multiplier_ps0, addend_ps0, context->fpscr);\n    const galaxy::PpcFloatResult result_ps1 = galaxy::ppc_f32_ternary(galaxy::PpcFloatTernaryOperation::{operation}, multiplicand_ps1, multiplier_ps1, addend_ps1, context->fpscr);\n    galaxy::ppc_commit_paired_result(context, {target}u, result_ps0, result_ps1, false, {}, services, 0x{pc:08X}u);",
                        record_bit(word)
                    )
                };
                if native_float {
                    let lane = match instruction.op {
                        Opcode::PsMadds0 => "Lane0",
                        Opcode::PsMadds1 => "Lane1",
                        _ => "Each",
                    };
                    writeln!(
                        output,
                        "    galaxy::fast::paired_ternary<galaxy::PpcFloatTernaryOperation::{operation}, galaxy::fast::PairedLane::{lane}>(context, {target}u, {multiplicand}u, {multiplier}u, {addend}u, {}, services, 0x{pc:08X}u);",
                        record_bit(word)
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                } else if indexed_paired_ternary && !fused_paired_ternary {
                    let operation_index = match instruction.op {
                        Opcode::PsMadd | Opcode::PsMadds0 | Opcode::PsMadds1 => 0u32,
                        Opcode::PsMsub => 1,
                        Opcode::PsNmadd => 2,
                        Opcode::PsNmsub => 3,
                        _ => unreachable!(),
                    };
                    let descriptor = target
                        | (multiplicand << 5)
                        | (multiplier << 10)
                        | (addend << 15)
                        | (operation_index << 20)
                        | (u32::from(multiplier_lane0 == "ps1_bits") << 22)
                        | (u32::from(multiplier_lane1 == "ps1_bits") << 23)
                        | (u32::from(record_bit(word)) << 24);
                    writeln!(output,
                        "    galaxy::ppc_execute_indexed_paired_ternary(context, 0x{descriptor:07X}u, services, 0x{pc:08X}u);")
                        .map_err(|_| TranslationError::Formatting)?;
                } else {
                    writeln!(
                    output,
                    "    galaxy::require_paired_single_mode(context, false, services, 0x{pc:08X}u);\n    const std::uint32_t multiplicand_ps0 = galaxy::require_single_precision_bits(context->fpr_bits[{multiplicand}], services, 0x{pc:08X}u);\n    const std::uint32_t multiplicand_ps1 = galaxy::require_single_precision_bits(context->ps1_bits[{multiplicand}], services, 0x{pc:08X}u);\n    const std::uint32_t multiplier_ps0 = galaxy::require_single_precision_bits(context->{multiplier_lane0}[{multiplier}], services, 0x{pc:08X}u);\n    const std::uint32_t multiplier_ps1 = galaxy::require_single_precision_bits(context->{multiplier_lane1}[{multiplier}], services, 0x{pc:08X}u);\n    const std::uint32_t addend_ps0 = galaxy::require_single_precision_bits(context->fpr_bits[{addend}], services, 0x{pc:08X}u);\n    const std::uint32_t addend_ps1 = galaxy::require_single_precision_bits(context->ps1_bits[{addend}], services, 0x{pc:08X}u);\n{arithmetic}"
                )
                .map_err(|_| TranslationError::Formatting)?;
                }
            }
            Opcode::PsSum0 | Opcode::PsSum1 => {
                let target = gpr_rt(word);
                let left = gpr_ra(word);
                let right = gpr_rb(word);
                let copy = fpr_c(word);
                let status_from_lane1 = instruction.op == Opcode::PsSum1;
                if native_float {
                    writeln!(
                        output,
                        "    galaxy::fast::paired_sum<{status_from_lane1}>(context, {target}u, {left}u, {right}u, {copy}u, {}, services, 0x{pc:08X}u);",
                        record_bit(word)
                    )
                    .map_err(|_| TranslationError::Formatting)?;
                } else {
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
                if fast_float_forms {
                    writeln!(
                        output,
                        "    galaxy::require_paired_single_mode(context, false, services, 0x{pc:08X}u);\n    galaxy::fast::compare_f64_cr(context, {field}u, context->{lane}[{left}], context->{lane}[{right}]);"
                    )
                } else {
                    writeln!(
                        output,
                        "    galaxy::require_paired_single_mode(context, false, services, 0x{pc:08X}u);\n    galaxy::compare_f64(context, {field}u, context->{lane}[{left}], context->{lane}[{right}], {ordered});"
                    )
                }
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::PsRes | Opcode::PsRsqrte if fast_float_forms => {
                let target = gpr_rt(word);
                let source = gpr_rb(word);
                let estimate = if instruction.op == Opcode::PsRes {
                    "reciprocal_single_bits"
                } else {
                    "reciprocal_sqrt_single_bits"
                };
                writeln!(
                    output,
                    "    galaxy::require_paired_single_mode(context, false, services, 0x{pc:08X}u);\n    {{ const std::uint64_t estimate_ps0 = galaxy::fast::{estimate}(context->fpr_bits[{source}]); const std::uint64_t estimate_ps1 = galaxy::fast::{estimate}(context->ps1_bits[{source}]); context->fpr_bits[{target}] = estimate_ps0; context->ps1_bits[{target}] = estimate_ps1; }}"
                )
                .map_err(|_| TranslationError::Formatting)?;
                if record_bit(word) {
                    output.push_str("    galaxy::update_cr1_from_fpscr(context);\n");
                }
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
                    "    galaxy::require_paired_single_mode(context, false, services, 0x{pc:08X}u);\n    const std::uint32_t source_ps0 = galaxy::require_single_precision_bits(context->fpr_bits[{source}], services, 0x{pc:08X}u);\n    const std::uint32_t source_ps1 = galaxy::require_single_precision_bits(context->ps1_bits[{source}], services, 0x{pc:08X}u);\n    const galaxy::PpcFloatResult result_ps0 = galaxy::{helper}(source_ps0, context->fpscr);\n    const galaxy::PpcFloatResult result_ps1 = galaxy::{helper}(source_ps1, context->fpscr);\n    galaxy::ppc_commit_paired_estimate_result(context, {target}u, result_ps0, result_ps1, {}, services, 0x{pc:08X}u);",
                    record_bit(word)
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            Opcode::PsSel if fast_float_forms => {
                let target = gpr_rt(word);
                let selector = gpr_ra(word);
                let negative = gpr_rb(word);
                let nonnegative = fpr_c(word);
                writeln!(
                    output,
                    "    galaxy::require_paired_single_mode(context, false, services, 0x{pc:08X}u);\n    {{ const std::uint64_t selected_ps0 = galaxy::fast::select_nonnegative(context->fpr_bits[{selector}]) ? context->fpr_bits[{nonnegative}] : context->fpr_bits[{negative}]; const std::uint64_t selected_ps1 = galaxy::fast::select_nonnegative(context->ps1_bits[{selector}]) ? context->ps1_bits[{nonnegative}] : context->ps1_bits[{negative}]; context->fpr_bits[{target}] = selected_ps0; context->ps1_bits[{target}] = selected_ps1; }}"
                )
                .map_err(|_| TranslationError::Formatting)?;
                if record_bit(word) {
                    output.push_str("    galaxy::update_cr1_from_fpscr(context);\n");
                }
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
                    // `or rA,rS,rS` is the PowerPC register move. Emitting `gpr[a] | gpr[a]` costs a redundant read of the
                    // same 4-byte context slot plus an OR on every `mr` in the stream. `x | x` equals `x` for unsigned
                    // operands, so a plain copy gives the same result. The record_bit CR0 update below still reads
                    // gpr[target] unchanged.
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
                // Only the shift == 0 form is folded: `rotl(v, 0) & mask` equals `v & mask`, so the rotate disappears
                // and `clrlwi`/`clrrwi`-style masks become a single `and`.
                //
                // The rotate is not removable for shift != 0. Rotating `v` by any amount other than `shift` moves its
                // bits relative to a mask whose position MB/ME fixes, so only `rotl(v, shift) & mask` matches the ISA.
                // A pre-rotated-mask form differs from the ISA on most encodings. Do not change the rotate amount.
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
                // Same shift == 0 fold as `Rlwinm`. The rotate amount must stay exactly `shift`; see the `Rlwinm` arm.
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
            Opcode::Cmpi | Opcode::Cmpli | Opcode::Cmp | Opcode::Cmpl
                if fused_compares.contains(&pc) =>
            {
                // The next branch tests this compare's operands; the CR field
                // is dead after it, so no CR write is needed.
                output.push_str("    // compare fused into the next branch\n");
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
                if fast_float_forms {
                    writeln!(
                        output,
                        "    galaxy::fast::compare_f64_cr(context, {field}u, context->fpr_bits[{left}], context->fpr_bits[{right}]);"
                    )
                } else {
                    writeln!(
                        output,
                        "    galaxy::compare_f64(context, {field}u, context->fpr_bits[{left}], context->fpr_bits[{right}], {ordered});"
                    )
                }
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
                let gpr_save_restore = if link {
                    gpr_save_restore_entry(target)
                } else {
                    None
                };
                if let Some((first, save)) = gpr_save_restore {
                    // r11 addresses the caller's frame when the only way here
                    // is straight from `addi r11,r1,d` (CodeWarrior's prologue).
                    let r11_on_stack = stack_r11_stores
                        && save
                        && !has_reentry_label
                        && pc > address
                        && r11_is_stack_frame_pointer(word_at(bytes, address, pc - 4));
                    emit_gpr_save_restore(
                        &mut output,
                        first,
                        save,
                        pc,
                        flat_ram_reads,
                        flat_ram_writes,
                        r11_on_stack,
                    )?;
                } else if target >= address && target < function_end {
                    if target <= pc {
                        writeln!(
                            output,
                            "    if (--checkpoint_budget__ == 0u) [[unlikely]] {{ checkpoint_budget__ = galaxy::kBranchCheckpointBudget; galaxy::branch_checkpoint_taken_budgeted(services, 0x{pc:08X}u, 0x{target:08X}u, context, memory); }}"
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
                        if link {
                            emit_abi_call_marker(&mut output, abi_call_markers, pc)?;
                        }
                        emit_direct_guest_call_with_exact_fpu(
                            &mut output,
                            target,
                            pc,
                            callable_entries,
                            exact_fpu_functions,
                        )?;
                    }
                    if link {
                        if call_return_checkpoint_needed(
                            target,
                            pc,
                            callable_entries,
                            exact_fpu_functions,
                            checkpoint_free_callees,
                        ) {
                            emit_call_return_checkpoint(&mut output, pc)?;
                        }
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
                    let fused_predicate = fused_compare_branches
                        .get(&pc)
                        .map(|(_, predicate)| predicate.clone())
                        .or_else(|| {
                            fused_record_registers
                                .get(&pc)
                                .map(|register| match bi % 4 {
                                    0 => format!(
                                        "static_cast<std::int32_t>(context->gpr[{register}]) < 0"
                                    ),
                                    1 => format!(
                                        "static_cast<std::int32_t>(context->gpr[{register}]) > 0"
                                    ),
                                    _ => format!("context->gpr[{register}] == 0u"),
                                })
                        });
                    let condition = match fused_predicate {
                        Some(predicate) if bo & 0x08 != 0 => format!("({predicate})"),
                        Some(predicate) => format!("!({predicate})"),
                        None => emit_branch_condition(&mut output, bo, bi)?,
                    };
                    if link {
                        writeln!(output, "    context->lr = 0x{:08X}u;", pc + 4)
                            .map_err(|_| TranslationError::Formatting)?;
                    }
                    if target >= address && target < function_end {
                        if unconditional {
                            if target <= pc {
                                writeln!(
                                output,
                                "    if (--checkpoint_budget__ == 0u) [[unlikely]] {{ checkpoint_budget__ = galaxy::kBranchCheckpointBudget; galaxy::branch_checkpoint_taken_budgeted(services, 0x{pc:08X}u, 0x{target:08X}u, context, memory); }}"
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
                                "    if ({condition}) {{ if (--checkpoint_budget__ == 0u) [[unlikely]] {{ checkpoint_budget__ = galaxy::kBranchCheckpointBudget; galaxy::branch_checkpoint_taken_budgeted(services, 0x{pc:08X}u, 0x{target:08X}u, context, memory); }} goto label_{target:08X}; }}"
                            )
                            .map_err(|_| TranslationError::Formatting)?;
                            } else {
                                writeln!(output, "    if ({condition}) goto label_{target:08X};")
                                    .map_err(|_| TranslationError::Formatting)?;
                            }
                        }
                    } else if link {
                        let checkpoint = call_return_checkpoint_needed(
                            target,
                            pc,
                            callable_entries,
                            exact_fpu_functions,
                            checkpoint_free_callees,
                        );
                        if unconditional {
                            emit_abi_call_marker(&mut output, abi_call_markers, pc)?;
                            emit_direct_guest_call_with_exact_fpu(
                                &mut output,
                                target,
                                pc,
                                callable_entries,
                                exact_fpu_functions,
                            )?;
                            if checkpoint {
                                emit_call_return_checkpoint(&mut output, pc)?;
                            }
                        } else {
                            writeln!(output, "    if ({condition}) {{")
                                .map_err(|_| TranslationError::Formatting)?;
                            emit_abi_call_marker(&mut output, abi_call_markers, pc)?;
                            emit_direct_guest_call_with_exact_fpu(
                                &mut output,
                                target,
                                pc,
                                callable_entries,
                                exact_fpu_functions,
                            )?;
                            if checkpoint {
                                emit_call_return_checkpoint(&mut output, pc)?;
                            }
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
                        emit_abi_call_marker(&mut output, abi_call_markers, pc)?;
                        emit_indirect_guest_call(
                            &mut output,
                            "branch_target",
                            pc,
                            direct_indirect_calls,
                            bctrl_site_cache,
                        )?;
                        emit_call_return_checkpoint(&mut output, pc)?;
                    } else {
                        writeln!(output, "    if ({condition}) {{")
                            .map_err(|_| TranslationError::Formatting)?;
                        emit_abi_call_marker(&mut output, abi_call_markers, pc)?;
                        emit_indirect_guest_call(
                            &mut output,
                            "branch_target",
                            pc,
                            direct_indirect_calls,
                            bctrl_site_cache,
                        )?;
                        emit_call_return_checkpoint(&mut output, pc)?;
                        output.push_str("    }\n");
                    }
                } else if unconditional {
                    emit_indirect_guest_call(
                        &mut output,
                        "branch_target",
                        pc,
                        direct_indirect_calls,
                        bctrl_site_cache,
                    )?;
                    output.push_str("    return;\n");
                    final_instruction_is_terminal = true;
                } else {
                    writeln!(output, "    if ({condition}) {{")
                        .map_err(|_| TranslationError::Formatting)?;
                    emit_indirect_guest_call(
                        &mut output,
                        "branch_target",
                        pc,
                        direct_indirect_calls,
                        bctrl_site_cache,
                    )?;
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
                    output.push_str(&site_hook_call(pc, "UltrawideScreenWidth", 0));
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
                        emit_abi_call_marker(&mut output, abi_call_markers, pc)?;
                        emit_indirect_guest_call(
                            &mut output,
                            "branch_target",
                            pc,
                            direct_indirect_calls,
                            bctrl_site_cache,
                        )?;
                        emit_call_return_checkpoint(&mut output, pc)?;
                    } else {
                        writeln!(output, "    if ({condition}) {{")
                            .map_err(|_| TranslationError::Formatting)?;
                        emit_abi_call_marker(&mut output, abi_call_markers, pc)?;
                        emit_indirect_guest_call(
                            &mut output,
                            "branch_target",
                            pc,
                            direct_indirect_calls,
                            bctrl_site_cache,
                        )?;
                        emit_call_return_checkpoint(&mut output, pc)?;
                        output.push_str("    }\n");
                    }
                } else if unconditional {
                    if internal_return_targets.is_empty() {
                        output.push_str(if guest_return_macro {
                            "    GALAXY_GUEST_RETURN;\n"
                        } else {
                            "    return;\n"
                        });
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
                        let statement = if guest_return_macro {
                            "GALAXY_GUEST_RETURN;"
                        } else {
                            "return;"
                        };
                        writeln!(output, "    if ({condition}) {statement}")
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
        if fused_records.contains(&pc) {
            // The next branch tests the recorded register; drop the CR0 write.
            const RECORD: &str = "    galaxy::update_cr0(context, context->gpr[";
            let emitted = &output[instruction_output_start..];
            if emitted.matches(RECORD).count() == 1 {
                let at = instruction_output_start + emitted.find(RECORD).expect("counted above");
                let register = output[at + RECORD.len()..]
                    .split(']')
                    .next()
                    .and_then(|digits| digits.parse::<u32>().ok());
                let line_end = output[at..].find('\n').map(|offset| at + offset + 1);
                if let (Some(register), Some(line_end)) = (register, line_end) {
                    output.replace_range(
                        at..line_end,
                        "    // CR0 record fused into the next branch\n",
                    );
                    fused_record_registers.insert(pc + 4, register);
                }
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
            output.push_str(&site_hook_call(pc, "EfbScreenWidth", 0));
        }
        // Fit the wide UI into narrower custom aspects without changing the
        // 3D camera. Projection height and both Y conversions form an inverse
        // pair; guest operations run first and geometry hooks follow the
        // instruction block.
        let mut post_block_hooks = String::new();
        if rmge01_native_screen_width_call(address, function_end, pc, word) {
            post_block_hooks.push_str(&site_hook_call(pc, "NativeScreenWidthExit", 0));
        }
        let layout_vertical_fit = match (address, function_end, pc, word) {
            (0x803C_A6C4, 0x803C_A76C, 0x803C_A708, 0xEC21_1028) => Some((1, false)),
            (0x8036_6658, 0x8036_6758, 0x8036_6728, 0xFC00_0050) => Some((0, false)),
            // Inverse fit of the f0 loaded at 0x803667FC, applied after the
            // labelled 0x80366804 lfd so its join also observes the fit.
            (0x8036_6758, 0x8036_6864, 0x8036_6804, 0xC85F_91D0) => Some((0, true)),
            _ => None,
        };
        if let Some((lane, inverse)) = layout_vertical_fit {
            post_block_hooks.push_str(&site_hook_call(
                pc,
                "LayoutVerticalFit",
                (lane as u32) | (u32::from(inverse) << 8),
            ));
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
            let argument = if kind < 2 {
                kind as u32
            } else {
                (kind as u32) | ((lane as u32) << 8)
            };
            post_block_hooks.push_str(&site_hook_call(pc, "HomeGeometry", argument));
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
        // Capture both original sources before updating either destination.
        // target == right can otherwise overwrite lane0 needed by merge00/10.
        writeln!(
            output,
            "    const std::uint32_t cached_merge_ps0 = lane{}[{}];\n    const std::uint32_t cached_merge_ps1 = lane{}[{}];\n    lane0[{target}] = cached_merge_ps0;\n    lane1[{target}] = cached_merge_ps1;",
            sources[0].0, sources[0].1, sources[1].0, sources[1].1
        )
        .map_err(|_| TranslationError::Formatting)?;
        facts[0][target] = true;
        facts[1][target] = true;
    } else if profile == LocalLaneProfile::PsvecCross && op == Opcode::PsNeg {
        let source = gpr_rb(word) as usize;
        // Negation changes only the sign bit and preserves exact binary32
        // provenance. Unknown source lanes must invalidate old target facts;
        // they must never cause a read of an uninitialized local cache entry.
        let known = [facts[0][source], facts[1][source]];
        for lane in 0..2 {
            if known[lane] {
                writeln!(
                    output,
                    "    lane{lane}[{target}] = lane{lane}[{source}] ^ 0x80000000u;"
                )
                .map_err(|_| TranslationError::Formatting)?;
            }
            facts[lane][target] = known[lane];
        }
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
fn validate_psmtx_local_memory_contract(
    bytes: &[u8],
) -> Result<BTreeMap<u32, (u32, i64)>, TranslationError> {
    let address = 0x804B_5F3C;
    let mut registers: [Option<(u32, i64)>; 32] = [None; 32];
    for register in [1, 3, 4, 5] {
        registers[register] = Some((register as u32, 0));
    }
    let mut accesses = BTreeMap::new();
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
            accesses.insert(pc, (root, start));
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
    Ok(accesses)
}

// The symbolic verifier supplies each pointer offset; never infer non-aliasing
// from the API arguments. The canonical context and original instruction order
// remain live throughout this leaf. Entry guards exclude callbacks, traces and
// enabled FP exceptions, and the verifier excludes calls/checkpoints/state writes.
fn lower_psmtx_proven_memory(
    mut body: String,
    bytes: &[u8],
    accesses: &BTreeMap<u32, (u32, i64)>,
) -> Result<String, TranslationError> {
    fn replace_once(
        body: &mut String,
        pc: u32,
        old: &str,
        new: &str,
    ) -> Result<(), TranslationError> {
        if body.matches(old).count() != 1 {
            return Err(local_paired_proof_error(
                pc,
                "proved matrix memory emission changed",
            ));
        }
        *body = body.replacen(old, new, 1);
        Ok(())
    }
    let arrays = "    std::uint32_t lane0[32]{};\n    std::uint32_t lane1[32]{};\n";
    replace_once(&mut body, 0x804B_5F3C, arrays, "")?;
    for (index, chunk) in bytes.chunks_exact(4).enumerate() {
        let pc = 0x804B_5F3C + index as u32 * 4;
        let word = u32::from_be_bytes(chunk.try_into().expect("four-byte instruction"));
        let op = Ins::new(word, Extensions::gekko_broadway()).op;
        for guard in [
            format!("    galaxy::ensure_fpu_available(services, 0x{pc:08X}u, context, memory);\n"),
            format!(
                "    galaxy::require_paired_single_mode(context, false, services, 0x{pc:08X}u);\n"
            ),
        ] {
            if body.matches(&guard).count() > 1 {
                return Err(local_paired_proof_error(
                    pc,
                    "duplicate matrix guard emission",
                ));
            }
            body = body.replace(&guard, "");
        }
        let Some(&(root, start)) = accesses.get(&pc) else {
            continue;
        };
        let (pointer, offset) = match root {
            1 => ("frame", start + 64),
            3 => ("input0", start),
            4 => ("input1", start),
            5 => ("output", start),
            0 => ("constant", start - 0x8069_E148),
            _ => return Err(local_paired_proof_error(pc, "unproved matrix pointer root")),
        };
        let pointer = format!("{pointer} + {offset}u");
        let register = gpr_rt(word);
        let ea = if matches!(op, Opcode::PsqL | Opcode::PsqSt | Opcode::Stwu) {
            format!("ea_{pc:08X}")
        } else {
            effective_address_expression(gpr_ra(word), signed_immediate(word))
        };
        let (old, new) = match op {
            Opcode::PsqL => (
                format!("    galaxy::psq_load(context, {register}u, memory, {ea}, 0u, false, true, services, 0x{pc:08X}u);"),
                format!("    load_pair({register}u, {pointer});"),
            ),
            Opcode::PsqSt => (
                format!("    galaxy::psq_store(context, {register}u, memory, {ea}, 0u, false, true, services, 0x{pc:08X}u);"),
                format!("    store_pair({register}u, {pointer}, {ea});"),
            ),
            Opcode::Stwu => (
                format!("    galaxy::guest_store_u32(memory, {ea}, context->gpr[{register}], services, 0x{pc:08X}u);"),
                format!("    store_value({pointer}, context->gpr[{register}], {ea});"),
            ),
            Opcode::Stfd => (
                format!("    galaxy::guest_store_u64(memory, {ea}, context->fpr_bits[{register}], services, 0x{pc:08X}u);"),
                format!("    store_value({pointer}, context->fpr_bits[{register}], {ea});"),
            ),
            Opcode::Lfd => (
                format!("galaxy::guest_load_u64(memory, {ea}, services, 0x{pc:08X}u)"),
                format!("galaxy::guest_flat_load_bytes<std::uint64_t>({pointer}, 0u)"),
            ),
            _ => return Err(local_paired_proof_error(pc, "unexpected proved memory opcode")),
        };
        replace_once(&mut body, pc, &old, &new)?;
        if op == Opcode::PsqL {
            for (lane, field) in ["fpr_bits", "ps1_bits"].iter().enumerate() {
                replace_once(&mut body, pc,
                    &format!("    lane{lane}[{register}] = galaxy::narrow_f64_to_f32_bits(context->{field}[{register}], services, 0x{pc:08X}u);\n"), "")?;
            }
        }
    }
    Ok(format!("{PSMTX_PROVEN_MEMORY_SOURCE}{body}"))
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

/// `addi r11,r1,d`: r11 then addresses the current stack frame, never the
/// write-gather pipe.
fn r11_is_stack_frame_pointer(word: u32) -> bool {
    word >> 26 == 14 && gpr_rt(word) == 11 && gpr_ra(word) == 1
}

/// The big-endian instruction word at `pc` inside the function at `address`.
fn word_at(bytes: &[u8], address: u32, pc: u32) -> u32 {
    let offset = (pc - address) as usize;
    u32::from_be_bytes(
        bytes[offset..offset + 4]
            .try_into()
            .expect("four-byte PPC word"),
    )
}

/// CodeWarrior's register save/restore helpers: `_savegpr_N` at
/// 0x805174FC + 4(N-14) is `stw rN..r31, -0x80+4N(r11)` then `blr`;
/// `_restgpr_N` at 0x80517548 + 4(N-14) is the matching `lwz` run. A linked
/// call to entry N returns (first register, is-save).
fn gpr_save_restore_entry(target: u32) -> Option<(u32, bool)> {
    match target {
        0x8051_74FC..=0x8051_7540 if (target - 0x8051_74FC) % 4 == 0 => {
            Some((14 + (target - 0x8051_74FC) / 4, true))
        }
        0x8051_7548..=0x8051_758C if (target - 0x8051_7548) % 4 == 0 => {
            Some((14 + (target - 0x8051_7548) / 4, false))
        }
        _ => None,
    }
}

/// `bl _savegpr_N` / `bl _restgpr_N` lowered inline as the helper's own
/// store/load run (one statement per register, so the residency pass keeps the
/// registers in host locals). The helpers contain no call, branch or
/// interrupt boundary, so the call-return checkpoint is not needed.
#[allow(clippy::too_many_arguments)]
fn emit_gpr_save_restore(
    output: &mut String,
    first: u32,
    save: bool,
    pc: u32,
    flat_ram_reads: bool,
    flat_ram_writes: bool,
    r11_on_stack: bool,
) -> Result<(), TranslationError> {
    let store = if r11_on_stack { "store_stack" } else { "store" };
    for reg in first..32 {
        let ea = format!(
            "context->gpr[11] + 0x{:08X}u",
            0xFFFF_FF80u32.wrapping_add(reg * 4)
        );
        if save {
            if flat_ram_writes {
                writeln!(
                    output,
                    "    galaxy::fast::{store}<std::uint32_t>(flat_guest_base, memory, {ea}, context->gpr[{reg}], services, 0x{pc:08X}u);"
                )
            } else {
                writeln!(
                    output,
                    "    galaxy::guest_store_u32(memory, {ea}, context->gpr[{reg}], services, 0x{pc:08X}u);"
                )
            }
        } else {
            writeln!(
                output,
                "    context->gpr[{reg}] = {};",
                integer_guest_load_expression(4, &ea, pc, flat_ram_reads)
            )
        }
        .map_err(|_| TranslationError::Formatting)?;
    }
    Ok(())
}

/// `--abi-call-sync`: announces a linked call to the residency pass, which
/// synchronizes it under the EABI contract when the module analysis vouches
/// for the call (`crate::residency::AbiResidency`). A no-op macro otherwise.
fn emit_abi_call_marker(
    output: &mut String,
    enabled: bool,
    call_pc: u32,
) -> Result<(), TranslationError> {
    if enabled {
        writeln!(output, "    GALAXY_ABI_CALL(0x{call_pc:08X}u);")
            .map_err(|_| TranslationError::Formatting)?;
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
        // No LodDistance hook after LodCtrl::update's distance call: which
        // model shows is game state, so nothing may change it (the runtime
        // keeps a no-op for modules generated before 1.0.1).
        // TalkTextFormer::updateTalking reveals one step of a talk balloon's
        // (TalkBalloon, former at r31 + 0x24) or Rosalina's storybook text
        // (RosettaMonologue, former at r30 + 0x24) per frame; the runtime
        // repeats it for the text speed setting. Movie subtitles keep the
        // movie's pace.
        if target == 0x802A_367C && (pc == 0x8029_D1A8 || pc == 0x8028_2B38) {
            output.push_str(&site_hook_call(pc, "TextStep", u32::from(pc == 0x8028_2B38)));
        }
        // SCGetLanguage (every caller calls it directly): the runtime answers r3 with the
        // language setting, so the game, its HOME Menu and its message files follow it.
        if target == 0x804D_0830 {
            output.push_str(&site_hook_call(pc, "Language", 0));
        }
        Ok(())
    }

    if let Some(owner) = callable_entries
        .and_then(|entries| entries.get(&target))
        .copied()
    {
        let requires_exact_fpu_restart =
            exact_fpu_functions.is_some_and(|functions| functions.contains(&target));
        let native_helper = if requires_exact_fpu_restart || is_wpad_read_copy(target, pc) {
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
            if target == 0x804A_381C && pc == 0x804A_B414 {
                // Optional runtime protocol1 can return only after the full
                // original load/RFI restored this same SelectThread frame.
                // It must execute the original OSSaveContext return checkpoint
                // and epilogue, not the abandoned OSLoadContext fallthrough.
                output.push_str("    if (context->pc == 0x804AB30Cu) goto call_return_804AB30C;\n");
            }
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

/// RVL SDK paired-single math, and J3D and JMath matrix, vector and keyframe routines with lean host
/// bodies (`galaxy::lean_*` in native_api.h). They use fast float and affect only ABI-visible state.
/// Eager FPU (ABI 25) means translated code never raises exception 7, so these need no exact-FPU restart.
/// They replace both the body and direct calls.
/// The two calls in CollisionShadow::drawAndCaptureTex (0x80313AF4) that size Mario's shadow-softening
/// circle in logical screen units are `bl TDDraw::setup` (2D camera from MR::getScreenWidth) and
/// `bl TDDraw::fix2Dpos`. The circle lands in a fixed 160x160 EFB region, so these keep the game's 16:9
/// width under a widened aspect. Without that, the circle narrows at 32:9 and leaves the sides of the
/// shadow dark.
/// One runtime-owned display/level-of-detail site (ABI 27). The module only marks the guest PC.
/// galaxy::site_hook calls the runtime, which decides what each SiteHookKind does, so policy changes need
/// no module rebuild.
fn site_hook_call(pc: u32, kind: &str, argument: u32) -> String {
    format!(
        "    galaxy::site_hook(services, context, memory, 0x{pc:08X}u, galaxy::SiteHookKind::{kind}, {argument}u);\n"
    )
}

fn rmge01_native_screen_width_call(address: u32, function_end: u32, pc: u32, word: u32) -> bool {
    matches!(
        (address, function_end, pc, word),
        (0x8031_3AF4, 0x8031_3E5C, 0x8031_3CFC, 0x480E_CC8D)
            | (0x8031_3AF4, 0x8031_3E5C, 0x8031_3D80, 0x480E_EFB1)
    )
}

fn lean_sdk_math_symbol(address: u32) -> Option<&'static str> {
    match address {
        0x804B_5EDC => Some("lean_psmtx_identity"),
        0x804B_5F08 => Some("lean_psmtx_copy"),
        0x804B_5F3C => Some("lean_psmtx_concat"),
        0x804B_6008 => Some("lean_psmtx_inverse"),
        0x804B_61C8 => Some("lean_psmtx_rot_trig"),
        0x804B_63A4 => Some("lean_psmtx_trans"),
        0x804B_63D8 => Some("lean_psmtx_trans_apply"),
        0x804B_6424 => Some("lean_psmtx_scale"),
        0x804B_644C => Some("lean_psmtx_scale_apply"),
        0x804B_64A4 => Some("lean_psmtx_quat"),
        0x804B_683C => Some("lean_psmtx_mult_vec"),
        0x804B_6890 => Some("lean_psmtx_mult_vec_sr"),
        0x804B_6B68 => Some("lean_psvec_add"),
        0x804B_6B8C => Some("lean_psvec_subtract"),
        0x804B_6BB0 => Some("lean_psvec_scale"),
        0x804B_6BCC => Some("lean_psvec_normalize"),
        0x804B_6C54 => Some("lean_psvec_mag"),
        0x804B_6C98 => Some("lean_psvec_dot"),
        0x804B_6CB8 => Some("lean_psvec_cross"),
        0x804B_6CF4 => Some("lean_psvec_distance"),
        0x804B_6D48 => Some("lean_psquat_multiply"),
        0x804B_6DA4 => Some("lean_psquat_dot"),
        // J3D / JMath / SDK C-math (tier 2, section A).
        0x8042_4978 => Some("lean_j3d_mtx33_copy"),
        0x8042_49A4 => Some("lean_j3d_mtx33_copy_from34"),
        0x8042_29B8 => Some("lean_j3d_psmtx_array_concat"),
        0x8042_22AC => Some("lean_j3d_calc_inverse_transpose"),
        0x8042_2374 => Some("lean_j3d_translate_rotate_mtx_info"),
        0x8042_2464 => Some("lean_j3d_translate_rotate_mtx"),
        0x8042_2514 => Some("lean_j3d_texture_mtx"),
        0x8042_25C8 => Some("lean_j3d_texture_mtx_old"),
        0x8042_27DC => Some("lean_j3d_scale_nrm_mtx"),
        0x8042_2840 => Some("lean_j3d_scale_nrm_mtx33"),
        0x8042_2894 => Some("lean_j3d_mtx_proj_concat"),
        0x8044_0EF8 => Some("lean_jma_vec_scale_add"),
        0x8044_0F1C => Some("lean_jma_vec_lerp"),
        0x8044_0DFC => Some("lean_jma_quat_lerp"),
        0x8043_3B88 => Some("lean_j3d_hermite_s16"),
        0x8043_3BDC => Some("lean_j3d_keyframe_f32"),
        0x804B_6100 => Some("lean_psmtx_inv_xpose"),
        0x804B_6B24 => Some("lean_psmtx44_copy"),
        0x804B_68E4 => Some("lean_psmtx_mult_vec_array_sr"),
        // Tier 2, section B (pure composites).
        0x8043_39B8 => Some("lean_j3d_keyframe_s16"),
        0x8044_0F48 => Some("lean_jma_mtx_apply_scale"),
        0x803A_3164 => Some("lean_psmtx_rot_rad"),
        0x804B_6328 => Some("lean_psmtx_rot_axis_rad"),
        0x804B_6548 => Some("lean_c_mtx_look_at"),
        0x804B_6F90 => Some("lean_c_quat_slerp"),
        // GX immediate XF loads (one batched gather-pipe write per command).
        0x804B_E1D8 => Some("native_gx_load_pos_mtx_imm_804BE1D8"),
        0x804B_E2A0 => Some("lean_gx_load_tex_mtx_imm"),
        0x804B_B8A8 => Some("lean_gx_load_light_obj_imm"),
        // Tier 3: JGeometry TVec3<f32> operators, TUtil<f32> and JMath copies.
        0x8001_8B14 => Some("lean_tvec3_copy"),
        0x8001_8B8C => Some("lean_tvec3_copy"),
        0x8001_CF64 => Some("lean_tvec3_copy"),
        0x8004_E9F0 => Some("lean_tvec3_copy"),
        0x8003_CD3C => Some("lean_jmath_ps_copy12"),
        0x8038_623C => Some("lean_jmath_ps_copy16"),
        0x8001_8BA0 => Some("lean_tvec3_add_assign"),
        0x8001_CF80 => Some("lean_tvec3_add_assign"),
        0x8001_FD94 => Some("lean_tvec3_sub_assign"),
        0x8002_04F0 => Some("lean_tvec3_sub_assign"),
        0x8002_5F68 => Some("lean_tvec3_sub3"),
        0x8001_DC74 => Some("lean_psvec_subtract"),
        0x8001_DDF8 => Some("lean_tvec3_plus"),
        0x8001_8BD4 => Some("lean_tvec3_minus"),
        0x8001_8B28 => Some("lean_tvec3_times"),
        0x8002_3404 => Some("lean_tvec3_scale_assign"),
        0x8001_FD6C => Some("lean_tvec3_scale_assign"),
        0x8005_4D64 => Some("lean_tvec3_scale_from"),
        0x8002_0514 => Some("lean_tvec3_negate"),
        0x8001_6F70 => Some("lean_tvec3_splat"),
        0x8004_E9E0 => Some("lean_tvec3_splat"),
        0x8001_6F80 => Some("lean_tvec3_set3"),
        0x8001_8BC4 => Some("lean_tvec3_set3"),
        0x8001_8A98 => Some("lean_tvec3_zero"),
        0x8003_71D8 => Some("lean_tvec3_from_ints"),
        0x8001_CF44 => Some("lean_tvec3_dot"),
        0x8007_743C => Some("lean_tvec3_squared"),
        0x800D_76B0 => Some("lean_tvec3_squared_distance"),
        0x8001_6FBC => Some("lean_jgeom_sqrt"),
        0x8002_0530 => Some("lean_jgeom_inv_sqrt"),
        // MSL string routines and two RMGE01 wrappers of them.
        0x8051_6E80 => Some("lean_strlen"),
        0x8051_E830 => Some("lean_strcmp"),
        0x8051_E94C => Some("lean_strncmp"),
        0x8051_EA04 => Some("lean_strstr"),
        0x8052_2E8C => Some("lean_strcasecmp"),
        0x803F_D4A0 => Some("lean_mr_is_equal_string"),
        0x8001_5394 => Some("lean_lyt_equals_res_name"),
        // GD display-list writers and memcpy.
        0x8042_0B3C => Some("lean_gd_write_u32"),
        0x8042_CDC4 => Some("lean_gd_write_u32"),
        0x8042_F3B0 => Some("lean_gd_write_u32"),
        0x8044_A498 => Some("lean_gd_pad_curr32"),
        0x8000_4338 => Some("lean_memcpy"),
        _ => None,
    }
}

/// Resident registers each lean host body reads or writes in `context`. A call site of a listed body
/// synchronizes only those registers and has no call-return checkpoint, since the body is a leaf with no
/// checkpoint, callback or transfer. An unlisted body is a full synchronization point with the usual
/// checkpoint. `lean_call_contracts_cover_the_bodies` checks these entries against the bodies in native_api.h.
pub(crate) fn lean_call_contract(symbol: &str) -> Option<LeanCallContract> {
    const fn mask(registers: &[u32]) -> u32 {
        let mut bits = 0u32;
        let mut index = 0;
        while index < registers.len() {
            bits |= 1u32 << registers[index];
            index += 1;
        }
        bits
    }
    // (GPRs read, GPRs written, FPRs read, FPRs written)
    let (gpr_reads, gpr_writes, fpr_reads, fpr_writes): (&[u32], &[u32], &[u32], &[u32]) =
        match symbol {
            "lean_psmtx_identity" => (&[3], &[], &[], &[]),
            "lean_psmtx_copy"
            | "lean_psmtx44_copy"
            | "lean_psmtx_quat"
            | "lean_psvec_normalize"
            | "lean_j3d_mtx33_copy"
            | "lean_j3d_mtx33_copy_from34"
            | "lean_j3d_translate_rotate_mtx_info"
            | "lean_j3d_scale_nrm_mtx"
            | "lean_j3d_scale_nrm_mtx33"
            | "lean_tvec3_copy"
            | "lean_jmath_ps_copy12"
            | "lean_jmath_ps_copy16"
            | "lean_tvec3_add_assign"
            | "lean_tvec3_sub_assign"
            | "lean_tvec3_negate" => (&[3, 4], &[], &[], &[]),
            "lean_psmtx_concat"
            | "lean_psmtx_mult_vec"
            | "lean_psmtx_mult_vec_sr"
            | "lean_psvec_add"
            | "lean_psvec_subtract"
            | "lean_psvec_cross"
            | "lean_psquat_multiply"
            | "lean_j3d_texture_mtx"
            | "lean_j3d_texture_mtx_old"
            | "lean_j3d_mtx_proj_concat"
            | "lean_gx_load_tex_mtx_imm"
            | "lean_tvec3_sub3"
            | "lean_tvec3_plus"
            | "lean_tvec3_minus" => (&[3, 4, 5], &[], &[], &[]),
            "lean_psmtx_mult_vec_array_sr"
            | "lean_j3d_psmtx_array_concat"
            | "lean_c_mtx_look_at"
            | "lean_tvec3_from_ints" => (&[3, 4, 5, 6], &[], &[], &[]),
            "lean_psmtx_inverse" | "lean_psmtx_inv_xpose" | "lean_j3d_calc_inverse_transpose" => {
                (&[3, 4], &[3], &[], &[])
            }
            "lean_psmtx_rot_trig" => (&[3, 4], &[], &[1, 2], &[]),
            "lean_psmtx_rot_rad" => (&[3, 4], &[4], &[1, 2], &[1, 2]),
            "lean_psmtx_trans" | "lean_psmtx_scale" => (&[3], &[], &[1, 2, 3], &[]),
            "lean_psmtx_trans_apply" | "lean_psmtx_scale_apply" | "lean_jma_mtx_apply_scale" => {
                (&[3, 4], &[], &[1, 2, 3], &[])
            }
            "lean_psvec_scale"
            | "lean_psmtx_rot_axis_rad"
            | "lean_tvec3_times"
            | "lean_tvec3_scale_from" => (&[3, 4], &[], &[1], &[]),
            "lean_tvec3_scale_assign" | "lean_tvec3_splat" => (&[3], &[], &[1], &[]),
            "lean_tvec3_set3" => (&[3], &[], &[1, 2, 3], &[]),
            "lean_tvec3_zero" => (&[3], &[], &[], &[]),
            "lean_psvec_mag" | "lean_tvec3_squared" => (&[3], &[], &[], &[1]),
            "lean_psvec_dot"
            | "lean_psvec_distance"
            | "lean_psquat_dot"
            | "lean_tvec3_dot"
            | "lean_tvec3_squared_distance" => (&[3, 4], &[], &[], &[1]),
            "lean_jma_vec_scale_add"
            | "lean_jma_vec_lerp"
            | "lean_jma_quat_lerp"
            | "lean_c_quat_slerp" => (&[3, 4, 5], &[], &[1], &[]),
            "lean_j3d_translate_rotate_mtx" => (&[3, 4, 5, 6], &[], &[1, 2, 3], &[]),
            "lean_j3d_keyframe_f32" | "lean_j3d_keyframe_s16" => (&[3, 4], &[], &[1], &[1]),
            "lean_j3d_hermite_s16" => (&[3, 4, 5, 6, 7, 8], &[], &[1], &[1]),
            "lean_jgeom_sqrt" | "lean_jgeom_inv_sqrt" => (&[], &[], &[1], &[1]),
            "lean_gx_load_light_obj_imm" => (&[2, 3, 4], &[], &[], &[]),
            "lean_strlen" => (&[3], &[3], &[], &[]),
            "lean_strcmp"
            | "lean_strstr"
            | "lean_strcasecmp"
            | "lean_mr_is_equal_string"
            | "lean_lyt_equals_res_name" => (&[3, 4], &[3], &[], &[]),
            "lean_strncmp" => (&[3, 4, 5], &[3], &[], &[]),
            "lean_gd_write_u32" => (&[3, 13], &[], &[], &[]),
            "lean_gd_pad_curr32" => (&[13], &[], &[], &[]),
            "lean_memcpy" => (&[3, 4, 5], &[], &[], &[]),
            _ => return None,
        };
    Some(LeanCallContract {
        gpr_reads: mask(gpr_reads),
        gpr_writes: mask(gpr_writes),
        fpr_reads: mask(fpr_reads),
        fpr_writes: mask(fpr_writes),
    })
}

/// Whether a linked direct call to `target` keeps its call-return checkpoint:
/// lean host bodies and translated checkpoint-free leaves need none.
fn call_return_checkpoint_needed(
    target: u32,
    pc: u32,
    callable_entries: Option<&BTreeMap<u32, u32>>,
    exact_fpu_functions: Option<&BTreeSet<u32>>,
    checkpoint_free_callees: Option<&BTreeSet<u32>>,
) -> bool {
    if lean_body_call_contract(target, pc, callable_entries, exact_fpu_functions).is_some() {
        return false;
    }
    let translated_leaf = checkpoint_free_callees.is_some_and(|leaves| leaves.contains(&target))
        && callable_entries.is_some_and(|entries| entries.get(&target) == Some(&target))
        && !requires_host_guest_call_at(target, pc)
        && !is_wpad_read_copy(target, pc);
    !translated_leaf
}

/// The five direct WPADRead calls to memcpy, whose call-return checkpoint
/// verifies the copied bytes for the native input trace.
fn is_wpad_read_copy(target: u32, pc: u32) -> bool {
    target == 0x8000_4338
        && matches!(
            pc,
            0x804D_945C | 0x804D_947C | 0x804D_949C | 0x804D_94B0 | 0x804D_94C4
        )
}

/// The register contract of a linked direct call that
/// `emit_direct_guest_call_with_exact_fpu` lowers to a lean host body: a
/// callable target outside the exact-FPU set (no lean target is an audited
/// inline wrapper).
fn lean_body_call_contract(
    target: u32,
    pc: u32,
    callable_entries: Option<&BTreeMap<u32, u32>>,
    exact_fpu_functions: Option<&BTreeSet<u32>>,
) -> Option<LeanCallContract> {
    if !callable_entries.is_some_and(|entries| entries.contains_key(&target))
        || exact_fpu_functions.is_some_and(|functions| functions.contains(&target))
        || is_wpad_read_copy(target, pc)
    {
        return None;
    }
    lean_sdk_math_symbol(target).and_then(lean_call_contract)
}

fn native_pure_helper(target: u32) -> Option<NativePureHelper> {
    if let Some(symbol) = lean_sdk_math_symbol(target) {
        return Some(NativePureHelper::integer(symbol));
    }
    match target {
        0x8000_72B4 => Some(NativePureHelper::integer(
            "native_read_next_char_utf16_800072B4",
        )),
        0x8009_7278 => Some(NativePureHelper::integer("native_add_12_80097278")),
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
        0x804B_D28C => Some(NativePureHelper::integer(
            "native_gx_set_chan_color_804BD28C",
        )),
        // Stateful functions with nested calls or interruptible loops are not
        // substituted: a hand-written replacement would skip call-return
        // checkpoints and resume-visible CR/GPR effects.
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

// GameScene::draw3D's call from GameScene::draw and draw3D's closing
// MR::clearZBuffer call bracket the 3D scene; the renderer marks both points
// in the GX stream (jitter, motion vectors, upscaler input).
fn marks_scene_boundary(target: u32, pc: u32) -> bool {
    (target == 0x8033_EE8C && pc == 0x8033_E820) || (target == 0x803C_A76C && pc == 0x8033_F024)
}

fn requires_host_guest_call_at(target: u32, pc: u32) -> bool {
    requires_host_guest_call(target)
        || marks_scene_boundary(target, pc)
        || (target == 0x804A_B20C && pc == 0x804A_B468)
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
            | 0x8049_61E0 // DspBoot: host JAudio mixer (no DSP task)
            | 0x8049_62C0 // DSPSendCommands2: JAudio DSP commands as host calls
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
            | 0x804A_B20C // SelectThread: host scheduler on the cooperative path
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

// Indirect call sites whose host-side transaction is keyed on the calling
// instruction rather than the callee. These always keep the audited cached
// service boundary even when direct dispatch is enabled.
fn indirect_call_site_requires_host(pc: u32) -> bool {
    matches!(
        pc,
        0x804A_B468 // OSYieldThread -> SelectThread RFI return protocol
            | 0x8038_5AD8 // Pointer field capture at the PE boundary
            | 0x8034_BF8C // Retained mouse press: FileSelect pane selection
            | 0x8036_FDD4 // MoviePlayerSimple movie-content marker
            | 0x8036_FEC8 // MoviePlayerSimple stop-owned marker
            | 0x8037_0278 // MoviePlayerSimple stop-owned marker
            | 0x8039_D400 // GameSystemSceneController::drawScene scene marker
    )
}

// `bctrl`/`bclrl`: the direct table call keeps the generated per-site cache
// as its fallback so a withdrawn or null entry pays exactly the old cost.
fn emit_indirect_guest_call(
    output: &mut String,
    target_expression: &str,
    pc: u32,
    direct: bool,
    site_cache: bool,
) -> Result<(), TranslationError> {
    if !direct || indirect_call_site_requires_host(pc) {
        return emit_cached_guest_call(output, target_expression, pc);
    }
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
    if site_cache {
        // --bctrl-site-cache: the last published direct entry this site called.
        writeln!(
            output,
            "    static std::uint32_t site_target_{pc:08X} = galaxy::fast::kNoIndirectSite;\n    static galaxy::NativeGameFunction site_function_{pc:08X} = nullptr;\n    galaxy::fast::call_guest_indirect_site(services, {target_expression}, &cached_target_{pc:08X}, &cached_function_{pc:08X}, &site_target_{pc:08X}, &site_function_{pc:08X}, context, memory, 0x{pc:08X}u);"
        )
        .map_err(|_| TranslationError::Formatting)?;
        return Ok(());
    }
    writeln!(
        output,
        "    galaxy::fast::call_guest_indirect(services, {target_expression}, &cached_target_{pc:08X}, &cached_function_{pc:08X}, context, memory, 0x{pc:08X}u);"
    )
    .map_err(|_| TranslationError::Formatting)
}

fn native_function_body(address: u32) -> Option<&'static str> {
    match address {
        // RVL SDK math library: lean host bodies (see lean_sdk_math_symbol).
        0x804B_5EDC => Some(
            "    galaxy::lean_psmtx_identity(context, memory, services, 0x804B5EDCu);\n    return;\n",
        ),
        0x8042_0B3C => Some(
            "    galaxy::lean_gd_write_u32(context, memory, services, 0x80420B3Cu);\n    return;\n",
        ),
        0x8042_CDC4 => Some(
            "    galaxy::lean_gd_write_u32(context, memory, services, 0x8042CDC4u);\n    return;\n",
        ),
        0x8042_F3B0 => Some(
            "    galaxy::lean_gd_write_u32(context, memory, services, 0x8042F3B0u);\n    return;\n",
        ),
        0x8044_A498 => Some(
            "    galaxy::lean_gd_pad_curr32(context, memory, services, 0x8044A498u);\n    return;\n",
        ),
        0x8000_4338 => Some(
            "    galaxy::lean_memcpy(context, memory, services, 0x80004338u);\n    return;\n",
        ),
        0x8051_6E80 => Some(
            "    galaxy::lean_strlen(context, memory, services, 0x80516E80u);\n    return;\n",
        ),
        0x8051_E830 => Some(
            "    galaxy::lean_strcmp(context, memory, services, 0x8051E830u);\n    return;\n",
        ),
        0x8051_E94C => Some(
            "    galaxy::lean_strncmp(context, memory, services, 0x8051E94Cu);\n    return;\n",
        ),
        0x8051_EA04 => Some(
            "    galaxy::lean_strstr(context, memory, services, 0x8051EA04u);\n    return;\n",
        ),
        0x8052_2E8C => Some(
            "    galaxy::lean_strcasecmp(context, memory, services, 0x80522E8Cu);\n    return;\n",
        ),
        0x803F_D4A0 => Some(
            "    galaxy::lean_mr_is_equal_string(context, memory, services, 0x803FD4A0u);\n    return;\n",
        ),
        0x8001_5394 => Some(
            "    galaxy::lean_lyt_equals_res_name(context, memory, services, 0x80015394u);\n    return;\n",
        ),
        0x8001_8B14 => Some(
            "    galaxy::lean_tvec3_copy(context, memory, services, 0x80018B14u);\n    return;\n",
        ),
        0x8001_8B8C => Some(
            "    galaxy::lean_tvec3_copy(context, memory, services, 0x80018B8Cu);\n    return;\n",
        ),
        0x8001_CF64 => Some(
            "    galaxy::lean_tvec3_copy(context, memory, services, 0x8001CF64u);\n    return;\n",
        ),
        0x8004_E9F0 => Some(
            "    galaxy::lean_tvec3_copy(context, memory, services, 0x8004E9F0u);\n    return;\n",
        ),
        0x8003_CD3C => Some(
            "    galaxy::lean_jmath_ps_copy12(context, memory, services, 0x8003CD3Cu);\n    return;\n",
        ),
        0x8038_623C => Some(
            "    galaxy::lean_jmath_ps_copy16(context, memory, services, 0x8038623Cu);\n    return;\n",
        ),
        0x8001_8BA0 => Some(
            "    galaxy::lean_tvec3_add_assign(context, memory, services, 0x80018BA0u);\n    return;\n",
        ),
        0x8001_CF80 => Some(
            "    galaxy::lean_tvec3_add_assign(context, memory, services, 0x8001CF80u);\n    return;\n",
        ),
        0x8001_FD94 => Some(
            "    galaxy::lean_tvec3_sub_assign(context, memory, services, 0x8001FD94u);\n    return;\n",
        ),
        0x8002_04F0 => Some(
            "    galaxy::lean_tvec3_sub_assign(context, memory, services, 0x800204F0u);\n    return;\n",
        ),
        0x8002_5F68 => Some(
            "    galaxy::lean_tvec3_sub3(context, memory, services, 0x80025F68u);\n    return;\n",
        ),
        0x8001_DC74 => Some(
            "    galaxy::lean_psvec_subtract(context, memory, services, 0x8001DC74u);\n    return;\n",
        ),
        0x8001_DDF8 => Some(
            "    galaxy::lean_tvec3_plus(context, memory, services, 0x8001DDF8u);\n    return;\n",
        ),
        0x8001_8BD4 => Some(
            "    galaxy::lean_tvec3_minus(context, memory, services, 0x80018BD4u);\n    return;\n",
        ),
        0x8001_8B28 => Some(
            "    galaxy::lean_tvec3_times(context, memory, services, 0x80018B28u);\n    return;\n",
        ),
        0x8002_3404 => Some(
            "    galaxy::lean_tvec3_scale_assign(context, memory, services, 0x80023404u);\n    return;\n",
        ),
        0x8001_FD6C => Some(
            "    galaxy::lean_tvec3_scale_assign(context, memory, services, 0x8001FD6Cu);\n    return;\n",
        ),
        0x8005_4D64 => Some(
            "    galaxy::lean_tvec3_scale_from(context, memory, services, 0x80054D64u);\n    return;\n",
        ),
        0x8002_0514 => Some(
            "    galaxy::lean_tvec3_negate(context, memory, services, 0x80020514u);\n    return;\n",
        ),
        0x8001_6F70 => Some(
            "    galaxy::lean_tvec3_splat(context, memory, services, 0x80016F70u);\n    return;\n",
        ),
        0x8004_E9E0 => Some(
            "    galaxy::lean_tvec3_splat(context, memory, services, 0x8004E9E0u);\n    return;\n",
        ),
        0x8001_6F80 => Some(
            "    galaxy::lean_tvec3_set3(context, memory, services, 0x80016F80u);\n    return;\n",
        ),
        0x8001_8BC4 => Some(
            "    galaxy::lean_tvec3_set3(context, memory, services, 0x80018BC4u);\n    return;\n",
        ),
        0x8001_8A98 => Some(
            "    galaxy::lean_tvec3_zero(context, memory, services, 0x80018A98u);\n    return;\n",
        ),
        0x8003_71D8 => Some(
            "    galaxy::lean_tvec3_from_ints(context, memory, services, 0x800371D8u);\n    return;\n",
        ),
        0x8001_CF44 => Some(
            "    galaxy::lean_tvec3_dot(context, memory, services, 0x8001CF44u);\n    return;\n",
        ),
        0x8007_743C => Some(
            "    galaxy::lean_tvec3_squared(context, memory, services, 0x8007743Cu);\n    return;\n",
        ),
        0x800D_76B0 => Some(
            "    galaxy::lean_tvec3_squared_distance(context, memory, services, 0x800D76B0u);\n    return;\n",
        ),
        0x8001_6FBC => Some(
            "    galaxy::lean_jgeom_sqrt(context, memory, services, 0x80016FBCu);\n    return;\n",
        ),
        0x8002_0530 => Some(
            "    galaxy::lean_jgeom_inv_sqrt(context, memory, services, 0x80020530u);\n    return;\n",
        ),
        0x804B_5F08 => Some(
            "    galaxy::lean_psmtx_copy(context, memory, services, 0x804B5F08u);\n    return;\n",
        ),
        0x804B_5F3C => Some(
            "    galaxy::lean_psmtx_concat(context, memory, services, 0x804B5F3Cu);\n    return;\n",
        ),
        0x804B_6008 => Some(
            "    galaxy::lean_psmtx_inverse(context, memory, services, 0x804B6008u);\n    return;\n",
        ),
        0x804B_61C8 => Some(
            "    galaxy::lean_psmtx_rot_trig(context, memory, services, 0x804B61C8u);\n    return;\n",
        ),
        0x804B_63A4 => Some(
            "    galaxy::lean_psmtx_trans(context, memory, services, 0x804B63A4u);\n    return;\n",
        ),
        0x804B_63D8 => Some(
            "    galaxy::lean_psmtx_trans_apply(context, memory, services, 0x804B63D8u);\n    return;\n",
        ),
        0x804B_6424 => Some(
            "    galaxy::lean_psmtx_scale(context, memory, services, 0x804B6424u);\n    return;\n",
        ),
        0x804B_644C => Some(
            "    galaxy::lean_psmtx_scale_apply(context, memory, services, 0x804B644Cu);\n    return;\n",
        ),
        0x804B_64A4 => Some(
            "    galaxy::lean_psmtx_quat(context, memory, services, 0x804B64A4u);\n    return;\n",
        ),
        0x804B_683C => Some(
            "    galaxy::lean_psmtx_mult_vec(context, memory, services, 0x804B683Cu);\n    return;\n",
        ),
        0x804B_6890 => Some(
            "    galaxy::lean_psmtx_mult_vec_sr(context, memory, services, 0x804B6890u);\n    return;\n",
        ),
        0x804B_6B68 => Some(
            "    galaxy::lean_psvec_add(context, memory, services, 0x804B6B68u);\n    return;\n",
        ),
        0x804B_6B8C => Some(
            "    galaxy::lean_psvec_subtract(context, memory, services, 0x804B6B8Cu);\n    return;\n",
        ),
        0x804B_6BB0 => Some(
            "    galaxy::lean_psvec_scale(context, memory, services, 0x804B6BB0u);\n    return;\n",
        ),
        0x804B_6BCC => Some(
            "    galaxy::lean_psvec_normalize(context, memory, services, 0x804B6BCCu);\n    return;\n",
        ),
        0x804B_6C54 => Some(
            "    galaxy::lean_psvec_mag(context, memory, services, 0x804B6C54u);\n    return;\n",
        ),
        0x804B_6C98 => Some(
            "    galaxy::lean_psvec_dot(context, memory, services, 0x804B6C98u);\n    return;\n",
        ),
        0x804B_6CB8 => Some(
            "    galaxy::lean_psvec_cross(context, memory, services, 0x804B6CB8u);\n    return;\n",
        ),
        0x804B_6CF4 => Some(
            "    galaxy::lean_psvec_distance(context, memory, services, 0x804B6CF4u);\n    return;\n",
        ),
        0x804B_6D48 => Some(
            "    galaxy::lean_psquat_multiply(context, memory, services, 0x804B6D48u);\n    return;\n",
        ),
        0x804B_6DA4 => Some(
            "    galaxy::lean_psquat_dot(context, memory, services, 0x804B6DA4u);\n    return;\n",
        ),
        0x804B_E2A0 => Some(
            "    galaxy::lean_gx_load_tex_mtx_imm(context, memory, services, 0x804BE2A0u);\n    return;\n",
        ),
        0x804B_B8A8 => Some(
            "    galaxy::lean_gx_load_light_obj_imm(context, memory, services, 0x804BB8A8u);\n    return;\n",
        ),
        0x8042_4978 => Some(
            "    galaxy::lean_j3d_mtx33_copy(context, memory, services, 0x80424978u);\n    return;\n",
        ),
        0x8042_49A4 => Some(
            "    galaxy::lean_j3d_mtx33_copy_from34(context, memory, services, 0x804249A4u);\n    return;\n",
        ),
        0x8042_29B8 => Some(
            "    galaxy::lean_j3d_psmtx_array_concat(context, memory, services, 0x804229B8u);\n    return;\n",
        ),
        0x8042_22AC => Some(
            "    galaxy::lean_j3d_calc_inverse_transpose(context, memory, services, 0x804222ACu);\n    return;\n",
        ),
        0x8042_2374 => Some(
            "    galaxy::lean_j3d_translate_rotate_mtx_info(context, memory, services, 0x80422374u);\n    return;\n",
        ),
        0x8042_2464 => Some(
            "    galaxy::lean_j3d_translate_rotate_mtx(context, memory, services, 0x80422464u);\n    return;\n",
        ),
        0x8042_2514 => Some(
            "    galaxy::lean_j3d_texture_mtx(context, memory, services, 0x80422514u);\n    return;\n",
        ),
        0x8042_25C8 => Some(
            "    galaxy::lean_j3d_texture_mtx_old(context, memory, services, 0x804225C8u);\n    return;\n",
        ),
        0x8042_27DC => Some(
            "    galaxy::lean_j3d_scale_nrm_mtx(context, memory, services, 0x804227DCu);\n    return;\n",
        ),
        0x8042_2840 => Some(
            "    galaxy::lean_j3d_scale_nrm_mtx33(context, memory, services, 0x80422840u);\n    return;\n",
        ),
        0x8042_2894 => Some(
            "    galaxy::lean_j3d_mtx_proj_concat(context, memory, services, 0x80422894u);\n    return;\n",
        ),
        0x8044_0EF8 => Some(
            "    galaxy::lean_jma_vec_scale_add(context, memory, services, 0x80440EF8u);\n    return;\n",
        ),
        0x8044_0F1C => Some(
            "    galaxy::lean_jma_vec_lerp(context, memory, services, 0x80440F1Cu);\n    return;\n",
        ),
        0x8044_0DFC => Some(
            "    galaxy::lean_jma_quat_lerp(context, memory, services, 0x80440DFCu);\n    return;\n",
        ),
        0x8043_3B88 => Some(
            "    galaxy::lean_j3d_hermite_s16(context, memory, services, 0x80433B88u);\n    return;\n",
        ),
        0x8043_3BDC => Some(
            "    galaxy::lean_j3d_keyframe_f32(context, memory, services, 0x80433BDCu);\n    return;\n",
        ),
        0x804B_6100 => Some(
            "    galaxy::lean_psmtx_inv_xpose(context, memory, services, 0x804B6100u);\n    return;\n",
        ),
        0x804B_6B24 => Some(
            "    galaxy::lean_psmtx44_copy(context, memory, services, 0x804B6B24u);\n    return;\n",
        ),
        0x804B_68E4 => Some(
            "    galaxy::lean_psmtx_mult_vec_array_sr(context, memory, services, 0x804B68E4u);\n    return;\n",
        ),
        0x8043_39B8 => Some(
            "    galaxy::lean_j3d_keyframe_s16(context, memory, services, 0x804339B8u);\n    return;\n",
        ),
        0x8044_0F48 => Some(
            "    galaxy::lean_jma_mtx_apply_scale(context, memory, services, 0x80440F48u);\n    return;\n",
        ),
        0x803A_3164 => Some(
            "    galaxy::lean_psmtx_rot_rad(context, memory, services, 0x803A3164u);\n    return;\n",
        ),
        0x804B_6328 => Some(
            "    galaxy::lean_psmtx_rot_axis_rad(context, memory, services, 0x804B6328u);\n    return;\n",
        ),
        0x804B_6548 => Some(
            "    galaxy::lean_c_mtx_look_at(context, memory, services, 0x804B6548u);\n    return;\n",
        ),
        0x804B_6F90 => Some(
            "    galaxy::lean_c_quat_slerp(context, memory, services, 0x804B6F90u);\n    return;\n",
        ),
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

/// Fast float: every translation unit that includes `galaxy/native_api.h`
/// directly defines `GALAXY_FAST_FLOAT` first (the shards include it through
/// `functions.h`), and the module unit exports the float flags the runtime
/// reads before it starts guest code.
fn apply_fast_float_mode(source_files: &mut [GeneratedSource]) {
    const DEFINE: &str = "#define GALAXY_FAST_FLOAT 1\n#define GALAXY_FASTMEM 1\n";
    const INCLUDE: &str = "#include \"galaxy/native_api.h\"";
    const PRAGMA: &str = "#pragma once\n";
    for file in source_files.iter_mut() {
        if file.contents.contains(INCLUDE) && !file.contents.contains(DEFINE) {
            if let Some(rest) = file.contents.strip_prefix(PRAGMA) {
                file.contents = format!("{PRAGMA}{DEFINE}{rest}");
            } else {
                file.contents = format!("{DEFINE}{}", file.contents);
            }
        }
        if file.contents.contains("galaxy_module_manifest() {")
            && !file.contents.contains("galaxy_module_float_flags()")
        {
            file.contents.push_str(
                "\nextern \"C\" std::uint32_t galaxy_module_float_flags() {\n\
                 \x20   return galaxy::kModuleFloatFlagFastFloat;\n\
                 }\n",
            );
        }
        if file.contents.contains("galaxy_module_manifest() {")
            && !file.contents.contains("galaxy_module_transfer_flags()")
        {
            file.contents.push_str(
                "\nextern \"C\" std::uint32_t galaxy_module_transfer_flags() {\n\
                 \x20   return galaxy::kModuleTransferFlagFlagReturn |\n\
                 \x20       galaxy::kModuleTransferFlagHostSelectThread;\n\
                 }\n",
            );
        }
    }
}

/// `--checkpoint-globals`: every checkpoint test of the generated sources
/// reads the module's gate snapshot (`*_g` helpers and `_G` macros in
/// `galaxy/native_api.h`). Every unit that includes the runtime header
/// directly (`functions.h` for the shards) enables the helpers first, and
/// `module.cpp` defines the snapshot and exports it.
/// The rewrite is textual over the final sources, so it covers every lowering
/// path (resident and non-resident bodies, shared cold blocks, continuation
/// stubs, the local-lane translation units) the same way.
fn apply_checkpoint_globals(source_files: &mut [GeneratedSource]) {
    const DEFINE: &str = "#define GALAXY_MODULE_CHECKPOINT_GLOBALS 1\n";
    const INCLUDE: &str = "#include \"galaxy/native_api.h\"";
    const PRAGMA: &str = "#pragma once\n";
    for file in source_files.iter_mut() {
        // Like the fast-float define: every unit that includes the runtime
        // header directly (the shards include it through functions.h).
        if file.contents.contains(INCLUDE) && !file.contents.contains(DEFINE) {
            file.contents = match file.contents.strip_prefix(PRAGMA) {
                Some(rest) => format!("{PRAGMA}{DEFINE}{rest}"),
                None => format!("{DEFINE}{}", file.contents),
            };
        }
        file.contents = rewrite_checkpoint_globals(&file.contents);
        if file.contents.contains("galaxy_module_manifest() {")
            && !file.contents.contains("galaxy_module_checkpoint_gate()")
        {
            file.contents.push_str(
                "\ngalaxy::ModuleCheckpointGateV1 galaxy::g_module_checkpoint_gate{};\n\n\
                 extern \"C\" galaxy::ModuleCheckpointGateV1* galaxy_module_checkpoint_gate() {\n\
                 \x20   return &galaxy::g_module_checkpoint_gate;\n\
                 }\n",
            );
        }
    }
}

// ---------------------------------------------------------------------------
// Code-generation probes (crate::asm_probe)
// ---------------------------------------------------------------------------

/// One synthetic probe function through the module's per-function pipeline:
/// lowering with the module options, residency and transfer returns.
pub(crate) fn lower_probe_function(
    address: u32,
    bytes: &[u8],
    callable_entries: &BTreeMap<u32, u32>,
    exact_fpu_functions: &BTreeSet<u32>,
    checkpoint_free_leaves: &BTreeSet<u32>,
    options: &ModuleTranslationOptions,
    module_abi: Option<&ModuleAbi>,
) -> Result<String, TranslationError> {
    let entries = BTreeSet::new();
    let abi = module_abi.map(|module_abi| function_abi_residency(module_abi, address, options));
    let body = lower_words_for_module_with_call_return_entries_and_exact_fpu_flat_reads(
        address,
        bytes,
        &entries,
        &BTreeSet::new(),
        callable_entries,
        exact_fpu_functions,
        checkpoint_free_leaves,
        &BTreeMap::new(),
        options,
    )?;
    Ok(apply_transfer_returns(apply_residency_to_body(
        address,
        body,
        &entries,
        options,
        abi.as_ref(),
    )))
}

/// The module's exact-FPU classification for a probe function.
pub(crate) fn probe_function_needs_fpu(address: u32, bytes: &[u8]) -> bool {
    (function_requires_fpu(bytes) || native_substitution_uses_fpu(address))
        && lean_sdk_math_symbol(address).is_none()
}

/// The module's checkpoint-free-leaf classification for a probe function.
pub(crate) fn probe_is_checkpoint_free_leaf(address: u32, bytes: &[u8]) -> bool {
    native_function_body(address).is_none()
        && native_pure_helper(address).is_none()
        && is_checkpoint_free_leaf(address, bytes)
}

/// The sharded module emitter over probe functions, one per shard, no aliases.
pub(crate) fn render_probe_module(
    functions: &[(FunctionRange, String)],
    exact_fpu_functions: &BTreeSet<u32>,
) -> Result<Vec<GeneratedSource>, TranslationError> {
    let entry = functions
        .first()
        .map(|(function, _)| function.address)
        .ok_or(TranslationError::NoTranslatableFunctions)?;
    // One function per shard, so no probe is inlined into another: in the
    // game module most calls cross shards.
    emit_sharded_module_with_experiment(
        functions,
        &[],
        1,
        usize::MAX / 2,
        entry,
        exact_fpu_functions,
        LocalPairedExperiments::default(),
    )
}

/// The module-wide option passes, as `translate_module_with_options` runs them.
pub(crate) fn module_option_passes(
    source_files: &mut [GeneratedSource],
    options: &ModuleTranslationOptions,
) {
    apply_module_options(source_files, options);
}

/// The module-wide passes over the generated sources, in their fixed order.
/// Each is a no-op unless its option is on.
fn apply_module_options(source_files: &mut [GeneratedSource], options: &ModuleTranslationOptions) {
    if options.fast_float {
        apply_fast_float_mode(source_files);
    }
    if options.checkpoint_globals {
        apply_checkpoint_globals(source_files);
    }
    if options.flat_view_only {
        define_before_runtime_header(source_files, "GALAXY_FLAT_VIEW_ONLY");
    }
    if options.assume_paired_singles {
        define_before_runtime_header(source_files, "GALAXY_ASSUME_PAIRED_SINGLES");
    }
    if options.single_float_arithmetic {
        define_before_runtime_header(source_files, "GALAXY_SINGLE_FLOAT_ARITHMETIC");
    }
}

/// Linker order file written next to the module sources.
const FUNCTION_ORDER_FILE: &str = "function_order.txt";

/// Writes `function_order.txt`, the module's functions in `order` by their
/// MSVC-ABI names, and links the module with `/ORDER` so the functions the
/// game runs most share pages and cache lines. Addresses the module does not
/// define are skipped.
fn add_function_order(source_files: &mut Vec<GeneratedSource>, order: &[u32]) {
    let defined: BTreeSet<u32> = source_files
        .iter()
        .find(|file| file.name == "functions.h")
        .map(|header| {
            header
                .contents
                .lines()
                .filter_map(|line| {
                    let rest = line.strip_prefix("void fn_")?;
                    if rest.as_bytes().get(8) != Some(&b'(') {
                        return None;
                    }
                    u32::from_str_radix(rest.get(..8)?, 16).ok()
                })
                .collect()
        })
        .unwrap_or_default();
    let mut listed = BTreeSet::new();
    let mut contents = String::new();
    for &address in order {
        if defined.contains(&address) && listed.insert(address) {
            // void rmge01::fn_X(galaxy::PpcContext* __restrict, galaxy::GuestMemoryV1*,
            //                   const galaxy::NativeServicesV1*)
            contents.push_str(&format!(
                "?fn_{address:08X}@rmge01@@YAXPEIAUPpcContext@galaxy@@PEAUGuestMemoryV1@3@PEBUNativeServicesV1@3@@Z\n"
            ));
        }
    }
    let Some(cmake) = source_files
        .iter_mut()
        .find(|file| file.name == "CMakeLists.txt")
    else {
        return;
    };
    if contents.is_empty() {
        return;
    }
    cmake.contents.push_str(
        "if(MSVC)\n\
         \x20   # Most-run functions first, in the order of function_order.txt.\n\
         \x20   target_link_options(RMGE01_game PRIVATE \"/ORDER:@${CMAKE_CURRENT_SOURCE_DIR}/function_order.txt\")\n\
         endif()\n",
    );
    source_files.push(GeneratedSource {
        name: FUNCTION_ORDER_FILE.to_owned(),
        contents,
    });
}

/// `#define {macro} 1` in every unit that includes the runtime header
/// directly (`functions.h` for the shards), ahead of the include, for the
/// module-wide header switches: `GALAXY_FLAT_VIEW_ONLY` (`--flat-view-only`)
/// and `GALAXY_ASSUME_PAIRED_SINGLES` (`--assume-paired-singles`).
fn define_before_runtime_header(source_files: &mut [GeneratedSource], macro_name: &str) {
    let define = format!("#define {macro_name} 1\n");
    const INCLUDE: &str = "#include \"galaxy/native_api.h\"";
    const PRAGMA: &str = "#pragma once\n";
    for file in source_files.iter_mut() {
        if file.contents.contains(INCLUDE) && !file.contents.contains(&define) {
            file.contents = match file.contents.strip_prefix(PRAGMA) {
                Some(rest) => format!("{PRAGMA}{define}{rest}"),
                None => format!("{define}{}", file.contents),
            };
        }
    }
}

/// The checkpoint call sites of one generated source, switched to the module
/// gate snapshot. Every replaced spelling is a complete call or macro name.
fn rewrite_checkpoint_globals(source: &str) -> String {
    const REWRITES: [(&str, &str); 7] = [
        (
            "galaxy::call_return_checkpoint_due(services)",
            "galaxy::call_return_checkpoint_due_g(services)",
        ),
        (
            "galaxy::branch_checkpoint_due_budgeted(services)",
            "galaxy::branch_checkpoint_due_budgeted_g(services)",
        ),
        (
            "galaxy::branch_checkpoint_due(services)",
            "galaxy::branch_checkpoint_due_g(services)",
        ),
        (
            "GALAXY_CALL_RETURN_CHECKPOINT(services,",
            "GALAXY_CALL_RETURN_CHECKPOINT_G(services,",
        ),
        (
            "GALAXY_BRANCH_CHECKPOINT_TAKEN_BUDGETED(services,",
            "GALAXY_BRANCH_CHECKPOINT_TAKEN_BUDGETED_G(services,",
        ),
        (
            "GALAXY_BRANCH_CHECKPOINT_TAKEN(services,",
            "GALAXY_BRANCH_CHECKPOINT_TAKEN_G(services,",
        ),
        (
            "GALAXY_RETURN_IF_TRANSFER_PENDING(services)",
            "GALAXY_RETURN_IF_TRANSFER_PENDING_G(services)",
        ),
    ];
    let mut out = source.to_owned();
    for (from, to) in REWRITES {
        if out.contains(from) {
            out = out.replace(from, to);
        }
    }
    out
}

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

// CalcStringRect and the formatter reached through TextWriterBase::Print are the main cost below the two
// hot Pane DrawSelf children. This closure is diagnostic only: every selected PPC call stays statically
// translated, with its ordinary return checkpoint and interior resume.
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

// The dominant children of the text-format closure are substantial translated routines. Profile their full
// statically resolved call sets, so nested work is separated from their own instruction bodies.
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

// The 0x803A3B6C JPA draw child ranks highly in the runtime's dynamic-call self profile, but that profiler
// cannot subtract statically resolved native calls. Instrument its complete static closure before treating
// its local body as an optimization target. Also time the exact normal-entry matrix assembly between the
// third normalization and the projection callback. This region has no guest calls. Its interior FPU-retry
// entries start with the local profiler inactive, so they cannot inherit a stale timestamp.
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
    let begin_anchor = format!("label_{JPA_HOT_DRAW_BODY_REGION_START:08X}:\n");
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

// The bounded call-self profile cannot subtract direct generated calls. These are the complete statically
// resolved child sets of its three largest unexplained entries: NW4R FindPaneByName, the Bluetooth USB bulk
// callback and the HID-host event handler. Their indirect calls already pass through call_guest_cached and
// are visible to the aggregate edge profiler. This diagnostic keeps every static callee, return checkpoint
// and interior resume while measuring the missing direct edges. A gameplay edge probe also identified these
// four descendant owners. Their complete static child sets are observed before their inclusive times are read
// as self time. The window gate keeps the diagnostic inactive during startup; every return and resume
// checkpoint is kept.
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
        // 8034424C fans out to the eight fixed 80344954 categories. The continuations are traced here, rather than
        // by instrumenting a broad function entry, so the bounded main-frame trace attributes each category's
        // complete native body to its exact static call site.
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

// These are hand-written proof predicates over the existing native ABI, not a matrix implementation. The
// arithmetic body is emitted from the user's dump by the ordinary instruction lowerer.
const PSMTX_PROVEN_MEMORY_SOURCE: &str = r#"
    // Entry proof covers every access in this straight-line leaf. There are
    // no guest calls, checkpoints, mapping changes or observable write callbacks.
    // Keep loads/stores at their original instruction positions: buffers may alias.
    std::byte* const input0 = galaxy::resolve_guest_fast(memory, context->gpr[3], 48u);
    std::byte* const input1 = galaxy::resolve_guest_fast(memory, context->gpr[4], 48u);
    std::byte* const output = galaxy::resolve_guest_fast(memory, context->gpr[5], 48u);
    std::byte* const frame = galaxy::resolve_guest_fast(memory, context->gpr[1] - 64u, 64u);
    std::byte* const constant = galaxy::resolve_guest_fast(memory, 0x8069E148u, 8u);
    std::uint32_t lane0[32]{};
    std::uint32_t lane1[32]{};
    const auto load_pair = [&](unsigned target, const std::byte* direct) {
        lane0[target] = galaxy::guest_flat_load_bytes<std::uint32_t>(direct, 0u);
        context->fpr_bits[target] = galaxy::widen_f32_bits(lane0[target]);
        lane1[target] = galaxy::guest_flat_load_bytes<std::uint32_t>(direct, 4u);
        context->ps1_bits[target] = galaxy::widen_f32_bits(lane1[target]);
    };
    const auto store_value = [&](std::byte* direct, auto value, std::uint32_t address) {
        using T = decltype(value);
        T swapped;
        if constexpr (sizeof(T) == 4u) swapped = galaxy::byte_swap_u32(value);
        else swapped = galaxy::byte_swap_u64(value);
        std::memcpy(direct, &swapped, sizeof(T));
        galaxy::guest_notify_write(memory, address, sizeof(T));
    };
    const auto store_pair = [&](unsigned reg, std::byte* direct, std::uint32_t address) {
        const auto first = galaxy::byte_swap_u32(galaxy::narrow_paired_single_ftz(context->fpr_bits[reg]));
        std::memcpy(direct, &first, 4u);
        std::uint32_t shared_page = 0u;
        const bool combined = memory->notify_write != nullptr && memory->dirty_page_words != nullptr &&
            galaxy::guest_tracker_single_page(address, 8u, memory->dirty_tracked_base,
                memory->dirty_tracked_size, memory->dirty_page_shift, memory->dirty_page_word_count, shared_page);
        if (memory->notify_write != nullptr && !combined) {
            galaxy::guest_notify_write(memory, address, 4u);
            const auto second = galaxy::byte_swap_u32(galaxy::narrow_paired_single_ftz(context->ps1_bits[reg]));
            std::memcpy(direct + 4u, &second, 4u);
            galaxy::guest_notify_write(memory, address + 4u, 4u);
            return;
        }
        const auto second = galaxy::byte_swap_u32(galaxy::narrow_paired_single_ftz(context->ps1_bits[reg]));
        std::memcpy(direct + 4u, &second, 4u);
        if (combined) galaxy::guest_notify_write_pair_admitted(memory, address, shared_page);
        else if (!galaxy::guest_notify_write_pair_same_page(memory, address)) {
            galaxy::guest_notify_write(memory, address, 4u);
            galaxy::guest_notify_write(memory, address + 4u, 4u);
        }
    };
"#;

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
    // Proven direct memory lowering retains diagnostic accesses in the fallback.
    if (galaxy::trace_fileloader_stack_enabled() || galaxy::trace_fileloader_temp_writes_enabled() ||
        galaxy::trace_u32_store_enabled() || galaxy::trace_audio_control_writes_enabled()) return 8u;
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

// This is a proof predicate over the native ABI. Arithmetic remains ordinary translator output derived from
// the game executable.
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
    emit_sharded_module_with_definition_rewrite(
        functions,
        aliases,
        shard_size,
        shard_source_bytes,
        guest_entry_point,
        exact_fpu_functions,
        experiments,
        None,
    )
}

/// `definition_rewrite` is a module option's textual pass over each rendered
/// function definition (`--checkpoint-globals`), applied before packing so
/// the shard budget measures the final text.
#[allow(clippy::too_many_arguments)]
fn emit_sharded_module_with_definition_rewrite(
    functions: &[(FunctionRange, String)],
    aliases: &[(u32, u32)],
    shard_size: usize,
    shard_source_bytes: usize,
    guest_entry_point: u32,
    exact_fpu_functions: &BTreeSet<u32>,
    experiments: LocalPairedExperiments<'_>,
    definition_rewrite: Option<fn(&str) -> String>,
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
    let paired_ternary_include = if functions
        .iter()
        .any(|(_, body)| body.contains("galaxy::ppc_commit_paired_ternary_result("))
    {
        "#include \"galaxy/ppc_paired_float.h\"\n"
    } else {
        ""
    };
    let mut header = format!(
        "#pragma once\n\n#include \"galaxy/native_api.h\"\n#include \"galaxy/ppc_float.h\"\n{paired_ternary_include}\nnamespace rmge01 {{\n\n",
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
        let rendered = match definition_rewrite {
            Some(rewrite) => rewrite(&rendered),
            None => rendered,
        };
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
        "#include \"functions.h\"\n\n#include <array>\n#include <cstddef>\n#include <cstring>\n\nnamespace {\n\nconst galaxy::NativeServicesV1* g_services = nullptr;\n\nusing FunctionRecord = galaxy::FunctionRecordV1;\n\n",
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
    // Optional capability, separate from the ABI23 service layout. Old modules
    // omit it and the runtime declines the return protocol. Only this complete
    // RMGE01 generator emits the corresponding exact SelectThread branch.
    module.push_str(
        "\nextern \"C\" GALAXY_MODULE_EXPORT std::uint32_t galaxy_yield_rfi_return_protocol() { return 1u; }\n\nextern \"C\" GALAXY_MODULE_EXPORT std::size_t galaxy_module_function_records(const galaxy::FunctionRecordV1** records) {\n    if (records == nullptr) {\n        return 0u;\n    }\n    *records = kFunctions.data();\n    return kFunctions.size();\n}\n",
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
         \x20       # Forward contraction policy through the MSVC-style driver; keep wrapping\n\
         \x20       # signed arithmetic and no type-based alias analysis.\n\
         \x20       target_compile_options(RMGE01_game PRIVATE /EHsc /GS- /GR- -w /clang:-ffp-contract=off -fwrapv -fno-strict-aliasing)\n\
         \x20       # clang-cl spelling of the AVX2 target (/arch:AVX2 below is for cl.exe).\n\
         \x20       # AVX-512 is not used: the host probe only tests AVX2 and OSXSAVE.\n\
         \x20       if(galaxy_native_isa_effective STREQUAL \"AVX2\")\n\
         \x20           target_compile_options(RMGE01_game PRIVATE -mavx2)\n\
         \x20           # FMA3 turns galaxy::fused_multiply_add (fused guest instructions)\n\
         \x20           # into single instructions. -ffp-contract=off still keeps separate\n\
         \x20           # multiplies and adds unfused. Every AVX2 CPU also has FMA3.\n\
         \x20           target_compile_options(RMGE01_game PRIVATE -mfma)\n\
         \x20           # Fastmem loads and stores use movbe (every AVX2 CPU has it).\n\
         \x20           target_compile_options(RMGE01_game PRIVATE -mmovbe)\n\
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
    // The CTR and CR tests are emitted only when the encoding tests them. An untested operand is omitted,
    // not replaced with a literal true.
    //
    // CTR is decremented and tested in one expression, so a counter-controlled loop-back edge does one
    // read-modify-write of `context->ctr`. The pre-decrement value is the value the instruction tests, and the
    // post-instruction CTR is the same on the taken and fall-through paths. All three call sites (`Bc`, `Bcctr`,
    // `Bclr`) put the result in an `if (...)`. `Bcctr` rejects BO[2] == 0, so the decrement is always evaluated
    // exactly once.
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
mod function_order_tests {
    use super::*;

    fn sources() -> Vec<GeneratedSource> {
        vec![
            GeneratedSource {
                name: "functions.h".to_owned(),
                contents: "#pragma once\n\nnamespace rmge01 {\n\n\
                    void fn_80001000(galaxy::PpcContext* __restrict context, galaxy::GuestMemoryV1* memory, const galaxy::NativeServicesV1* services);\n\
                    void fn_80002000(galaxy::PpcContext* __restrict context, galaxy::GuestMemoryV1* memory, const galaxy::NativeServicesV1* services);\n\
                    void fn_80003000_exact(galaxy::PpcContext* __restrict context, galaxy::GuestMemoryV1* memory, const galaxy::NativeServicesV1* services);\n\n\
                    }  // namespace rmge01\n"
                    .to_owned(),
            },
            GeneratedSource {
                name: "CMakeLists.txt".to_owned(),
                contents: "add_library(RMGE01_game SHARED module.cpp)\n".to_owned(),
            },
        ]
    }

    #[test]
    fn order_file_lists_defined_functions_in_rank_order() {
        let mut files = sources();
        add_function_order(
            &mut files,
            &[0x8000_2000, 0x8000_3000, 0x8000_1000, 0x8000_2000],
        );
        let order = files
            .iter()
            .find(|file| file.name == FUNCTION_ORDER_FILE)
            .expect("order file");
        assert_eq!(
            order.contents,
            "?fn_80002000@rmge01@@YAXPEIAUPpcContext@galaxy@@PEAUGuestMemoryV1@3@PEBUNativeServicesV1@3@@Z\n\
             ?fn_80001000@rmge01@@YAXPEIAUPpcContext@galaxy@@PEAUGuestMemoryV1@3@PEBUNativeServicesV1@3@@Z\n"
        );
        let cmake = &files[1].contents;
        assert!(cmake.contains(
            "target_link_options(RMGE01_game PRIVATE \"/ORDER:@${CMAKE_CURRENT_SOURCE_DIR}/function_order.txt\")"
        ));
    }

    #[test]
    fn no_listed_function_leaves_the_module_unchanged() {
        let mut files = sources();
        let before = files
            .iter()
            .map(|file| file.contents.clone())
            .collect::<Vec<_>>();
        add_function_order(&mut files, &[0x8000_3000, 0x8000_4000]);
        assert_eq!(files.len(), 2);
        assert_eq!(
            files
                .iter()
                .map(|file| file.contents.clone())
                .collect::<Vec<_>>(),
            before
        );
    }
}

#[cfg(test)]
// Instruction words are spelled field by field, zero fields included.
#[allow(clippy::identity_op)]
mod tests {
    use super::*;

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

    // Optional compilation seam: public synthetic words, no retail game code.
    #[test]
    fn multi_register_public_cpp_fixture() {
        let mut cpp =
            String::from("#include \"galaxy/native_api.h\"\nnamespace public_synthetic {\n");
        for first in 0u32..32 {
            let pc = 0x8000_4000 + first * 12;
            let words = [
                (46u32 << 26) | (first << 21) | (1 << 16) | 8,
                (47u32 << 26) | (first << 21) | (1 << 16) | 0xFFF8,
                0x4E80_0020,
            ];
            let bytes = words
                .iter()
                .flat_map(|word| word.to_be_bytes())
                .collect::<Vec<_>>();
            let body = lower_words(pc, &bytes).unwrap();
            assert!(body.contains("return;"));
            writeln!(cpp, "void case_{first}(galaxy::PpcContext* context, galaxy::GuestMemoryV1* memory, const galaxy::NativeServicesV1* services) {{\n{body}}}").unwrap();
        }
        cpp.push_str("}\n");
        if let Some(path) = std::env::var_os("GALAXY_MULTI_GPR_CPP_FIXTURE") {
            let mut file = std::fs::OpenOptions::new()
                .write(true)
                .create_new(true)
                .open(path)
                .unwrap();
            std::io::Write::write_all(&mut file, cpp.as_bytes()).unwrap();
        }
    }

    #[test]
    fn multi_register_default_lowering_uses_checked_span_helpers() {
        for first in 0u32..32 {
            let words = [
                (46u32 << 26) | (first << 21) | (1 << 16) | 8,
                (47u32 << 26) | (first << 21) | (1 << 16) | 0xFFF8,
                0x4E80_0020,
            ];
            let bytes = words
                .iter()
                .flat_map(|word| word.to_be_bytes())
                .collect::<Vec<_>>();
            let body = lower_words(0x8000_4000, &bytes).unwrap();
            assert_eq!(body.contains("guest_load_multiple_gprs"), first <= 16);
            assert_eq!(body.contains("guest_store_multiple_gprs"), first <= 24);
            // Large register suffixes lower to one statement per register, so no
            // runtime-indexed GPR access remains for the residency pass.
            assert!(!body.contains("for (std::uint32_t reg"));
            assert!(!body.contains("context->gpr[reg]"));
            if first > 16 {
                assert_eq!(
                    body.matches("galaxy::guest_load_u32(memory, ea_80004000 + 0x")
                        .count(),
                    (32 - first) as usize
                );
            }
            if first > 24 {
                assert_eq!(
                    body.matches("galaxy::guest_store_u32(memory, ea_80004004 + 0x")
                        .count(),
                    (32 - first) as usize
                );
            }
            assert!(body.contains("context->gpr[1] + 0xFFFFFFF8u"));
        }
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
            "    [[maybe_unused]] std::byte* const flat_guest_base = galaxy::fast::flat_base(memory);\n    [[maybe_unused]] const std::byte* flat_guest_read_base = flat_guest_base;\n"
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
        assert!(flat.contains("context->gpr[24] = galaxy::guest_load_flat_or_checked_u32(flat_guest_read_base, memory, ea_80004010 + 0x00000000u, services, 0x80004010u)"));
        assert!(flat.contains("context->gpr[31] = galaxy::guest_load_flat_or_checked_u32(flat_guest_read_base, memory, ea_80004010 + 0x0000001Cu, services, 0x80004010u)"));
        assert_eq!(flat.matches("guest_load_flat_or_checked_").count(), 12);
        assert!(!flat.contains("guest_store_flat"));
    }

    #[test]
    fn transfer_returns_follow_every_checkpoint_and_interrupt_boundary() {
        let body = "call_return_80004008:\n    galaxy::call_return_checkpoint(services, 0x80004004u, 0x80004008u, context, memory);\n    if (--checkpoint_budget__ == 0u) [[unlikely]] { checkpoint_budget__ = galaxy::kBranchCheckpointBudget; galaxy::branch_checkpoint_taken_budgeted(services, 0x80004010u, 0x80004000u, context, memory); }\n    context->msr = context->gpr[3];\n    std::atomic_thread_fence(std::memory_order_seq_cst);\n    galaxy::architectural_interrupt_checkpoint(services, 0x80004014u, 0x80004018u, context, memory);\n    if (const std::uint64_t counted__ = galaxy::call_return_checkpoint_due(services); counted__ != 0u) [[unlikely]] { galaxy::call_return_checkpoint_counted(services, 0x80004020u, 0x80004024u, context, memory, counted__); GALAXY_RETURN_IF_TRANSFER_PENDING(services); }\n".to_owned();
        let out = apply_transfer_returns(body);
        assert!(out.contains("call_return_80004008:\n    GALAXY_CALL_RETURN_CHECKPOINT(services, 0x80004004u, 0x80004008u, context, memory);\n"));
        assert!(out.contains("GALAXY_BRANCH_CHECKPOINT_TAKEN_BUDGETED(services, 0x80004010u, 0x80004000u, context, memory); }"));
        assert!(out.contains("galaxy::architectural_interrupt_checkpoint(services, 0x80004014u, 0x80004018u, context, memory); GALAXY_RETURN_IF_TRANSFER_PENDING(services);\n"));
        // The resident split is left alone.
        assert!(out.contains("galaxy::call_return_checkpoint_counted(services, 0x80004020u, 0x80004024u, context, memory, counted__); GALAXY_RETURN_IF_TRANSFER_PENDING(services); }"));
        assert_eq!(out.matches("GALAXY_RETURN_IF_TRANSFER_PENDING").count(), 2);
    }

    #[test]
    fn statically_known_device_addresses_bypass_the_flat_view() {
        let words: [u32; 10] = [
            (15 << 26) | (3 << 21) | 0xCC00,             // lis r3,0xCC00
            (40 << 26) | (0 << 21) | (3 << 16) | 0x5000, // lhz r0,0x5000(r3)  DSP mailbox
            (15 << 26) | (4 << 21) | 0xCD00,             // lis r4,0xCD00
            (32 << 26) | (5 << 21) | (4 << 16) | 0x0004, // lwz r5,4(r4)       IPC
            (36 << 26) | (5 << 21) | (3 << 16) | 0x3000, // stw r5,0x3000(r3)  PI
            (15 << 26) | (6 << 21) | 0x8000,             // lis r6,0x8000
            (32 << 26) | (7 << 21) | (6 << 16) | 0x00E4, // lwz r7,0xE4(r6)    RAM
            (15 << 26) | (8 << 21) | 0xCC01,             // lis r8,0xCC01
            (36 << 26) | (9 << 21) | (8 << 16) | 0x8000, // stw r9,-0x8000(r8) WGPIPE
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
                flat_ram_writes: true,
                ..LoweringConfig::default()
            },
        )
        .unwrap();
        assert!(flat.contains(
            "galaxy::guest_load_u16(memory, context->gpr[3] + 0x00005000u, services, 0x80004004u)"
        ));
        assert!(flat.contains(
            "galaxy::guest_load_u32(memory, context->gpr[4] + 0x00000004u, services, 0x8000400Cu)"
        ));
        assert!(flat.contains(
            "galaxy::guest_store_u32(memory, context->gpr[3] + 0x00003000u, context->gpr[5], services, 0x80004010u)"
        ));
        assert!(flat.contains(
            "galaxy::guest_load_flat_or_checked_u32(flat_guest_read_base, memory, context->gpr[6] + 0x000000E4u, services, 0x80004018u)"
        ));
        assert!(flat.contains(
            "galaxy::fast::store<std::uint32_t>(flat_guest_base, memory, context->gpr[8]"
        ));
        assert_eq!(flat.matches("guest_load_flat_or_checked_").count(), 1);
        assert_eq!(flat.matches("galaxy::fast::store<").count(), 1);
    }

    fn device_pcs(words: &[u32], entries: &[u32], globals: &BTreeMap<i32, u32>) -> BTreeSet<u32> {
        let bytes = words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect::<Vec<_>>();
        discover_static_device_access_pcs(
            0x8000_5000,
            &bytes,
            &entries.iter().copied().collect(),
            None,
            Some(globals),
        )
    }

    #[test]
    fn device_constants_survive_loop_joins_and_indexed_register_files() {
        // IPC control poll: the base is set before the loop head.
        let poll = [
            (15 << 26) | (3 << 21) | 0xCD00,              // lis r3,0xCD00
            (32 << 26) | (3 << 16) | 0x0004,              // lwz r0,4(r3)
            (28 << 26) | (0 << 16) | 0x0004,              // andi. r0,r0,4
            (16 << 26) | (12 << 21) | (2 << 16) | 0xFFF8, // beq 0x80005004
            0x4E80_0020,
        ];
        assert_eq!(
            device_pcs(&poll, &[], &BTreeMap::new()),
            BTreeSet::from([0x8000_5004])
        );
        // IPCWriteReg: __IPCRegs[index] = value.
        let indexed = [
            (21 << 26) | (3 << 21) | (2 << 11) | (29 << 1), // rlwinm r0,r3,2,0,29
            (15 << 26) | (3 << 21) | 0xCD00,                // lis r3,0xCD00
            (31 << 26) | (4 << 21) | (3 << 16) | (151 << 1), // stwx r4,r3,r0
            0x4E80_0020,
        ];
        assert_eq!(
            device_pcs(&indexed, &[], &BTreeMap::new()),
            BTreeSet::from([0x8000_5008])
        );
        // An address-taken label no branch explains starts unknown.
        let entered = [
            (15 << 26) | (3 << 21) | 0xCD00, // lis r3,0xCD00
            (32 << 26) | (3 << 16) | 0x0004, // lwz r0,4(r3)
            0x4E80_0020,
        ];
        assert!(device_pcs(&entered, &[0x8000_5004], &BTreeMap::new()).is_empty());
    }

    #[test]
    fn device_pointer_globals_and_joins_follow_every_path() {
        // __cpReg is a device base until a call clobbers the volatile copy.
        let globals = BTreeMap::from([(-0x1DF4, 0xCC00_0000)]);
        let global = [
            (32 << 26) | (4 << 21) | (13 << 16) | 0xE20C, // lwz r4,-0x1DF4(r13)
            (44 << 26) | (5 << 21) | (4 << 16) | 0x0002,  // sth r5,2(r4)
            (18 << 26) | 0x0100 | 1,                      // bl
            (44 << 26) | (5 << 21) | (4 << 16) | 0x0002,  // sth r5,2(r4)
            0x4E80_0020,
        ];
        assert_eq!(
            device_pcs(&global, &[], &globals),
            BTreeSet::from([0x8000_5004])
        );
        assert!(device_pcs(&global, &[], &BTreeMap::new()).is_empty());
        let joins = [
            (11 << 26) | (3 << 16),                       // cmpwi r3,0
            (16 << 26) | (12 << 21) | (2 << 16) | 0x000C, // beq 0x80005010
            (15 << 26) | (4 << 21) | 0xCD00,              // lis r4,0xCD00
            (18 << 26) | 0x0008,                          // b 0x80005014
            (32 << 26) | (4 << 21) | (1 << 16),           // lwz r4,0(r1)
            (32 << 26) | (4 << 16) | 0x0004,              // lwz r0,4(r4): RAM on one path
            (15 << 26) | (5 << 21) | 0xCC00,              // lis r5,0xCC00
            (16 << 26) | (12 << 21) | (2 << 16) | 0x0008, // beq 0x80005024
            0x6000_0000,                                  // nop
            (40 << 26) | (5 << 16) | 0x2002,              // lhz r0,0x2002(r5): VI on both
            0x4E80_0020,
        ];
        assert_eq!(
            device_pcs(&joins, &[], &BTreeMap::new()),
            BTreeSet::from([0x8000_5024])
        );
    }

    /// lfs, stfs, lfsx, stfsx, psq_l, psq_st and blr: the float memory forms
    /// `--hw-float-conversions` switches.
    fn float_memory_sequence() -> Vec<u8> {
        let words: [u32; 7] = [
            (48 << 26) | (1 << 21) | (3 << 16) | 0x0008, // lfs f1,8(r3)
            (52 << 26) | (1 << 21) | (3 << 16) | 0x000C, // stfs f1,12(r3)
            (31 << 26) | (2 << 21) | (3 << 16) | (4 << 11) | (535 << 1), // lfsx f2,r3,r4
            (31 << 26) | (2 << 21) | (3 << 16) | (4 << 11) | (663 << 1), // stfsx f2,r3,r4
            (56 << 26) | (3 << 21) | (5 << 16),          // psq_l f3,0(r5),0,0
            (60 << 26) | (3 << 21) | (5 << 16) | 0x0008, // psq_st f3,8(r5),0,0
            0x4E80_0020,
        ];
        words.iter().flat_map(|word| word.to_be_bytes()).collect()
    }

    fn lower_float_memory_sequence(hw_float_conversions: bool) -> String {
        lower_words_with_config(
            0x8000_4000,
            &float_memory_sequence(),
            &BTreeSet::new(),
            &BTreeSet::new(),
            LoweringConfig {
                flat_ram_reads: true,
                flat_ram_writes: true,
                native_float: true,
                hw_float_conversions,
                ..LoweringConfig::default()
            },
        )
        .unwrap()
    }

    #[test]
    fn hw_float_conversions_off_keeps_the_integer_conversion_helpers() {
        let out = lower_float_memory_sequence(false);
        for line in [
            "    galaxy::fast::load_fpr_single(flat_guest_read_base, context, 1u, memory, context->gpr[3] + 0x00000008u, services, 0x80004000u);\n",
            "    galaxy::fast::store_fpr_single(flat_guest_base, context, 1u, memory, context->gpr[3] + 0x0000000Cu, services, 0x80004004u);\n",
            "    galaxy::fast::load_fpr_single(flat_guest_read_base, context, 2u, memory, context->gpr[3] + context->gpr[4], services, 0x80004008u);\n",
            "    galaxy::fast::store_fpr_single(flat_guest_base, context, 2u, memory, context->gpr[3] + context->gpr[4], services, 0x8000400Cu);\n",
            "    galaxy::fast::psq_load(flat_guest_read_base, context, 3u, memory, ea_80004010, 0u, false, true, services, 0x80004010u);\n",
            "    galaxy::fast::psq_store(flat_guest_base, context, 3u, memory, ea_80004014, 0u, false, true, services, 0x80004014u);\n",
        ] {
            assert!(out.contains(line), "missing {line}in\n{out}");
        }
        assert!(!out.contains("_hw("), "{out}");
    }

    #[test]
    fn hw_float_conversions_on_switches_every_binary32_memory_form() {
        let out = lower_float_memory_sequence(true);
        for line in [
            "    galaxy::fast::load_fpr_single_hw(flat_guest_read_base, context, 1u, memory, context->gpr[3] + 0x00000008u, services, 0x80004000u);\n",
            "    galaxy::fast::store_fpr_single_hw(flat_guest_base, context, 1u, memory, context->gpr[3] + 0x0000000Cu, services, 0x80004004u);\n",
            "    galaxy::fast::load_fpr_single_hw(flat_guest_read_base, context, 2u, memory, context->gpr[3] + context->gpr[4], services, 0x80004008u);\n",
            "    galaxy::fast::store_fpr_single_hw(flat_guest_base, context, 2u, memory, context->gpr[3] + context->gpr[4], services, 0x8000400Cu);\n",
            "    galaxy::fast::psq_load_hw(flat_guest_read_base, context, 3u, memory, ea_80004010, 0u, false, true, services, 0x80004010u);\n",
            "    galaxy::fast::psq_store_hw(flat_guest_base, context, 3u, memory, ea_80004014, 0u, false, true, services, 0x80004014u);\n",
        ] {
            assert!(out.contains(line), "missing {line}in\n{out}");
        }
        // The flag changes nothing but the helper names.
        assert_eq!(out.replace("_hw(", "("), lower_float_memory_sequence(false));
    }

    #[test]
    fn hw_float_conversions_leave_the_checked_helpers_alone() {
        // Without the flat view every float access takes the checked helper,
        // which has no hardware form.
        let out = lower_words_with_config(
            0x8000_4000,
            &float_memory_sequence(),
            &BTreeSet::new(),
            &BTreeSet::new(),
            LoweringConfig {
                hw_float_conversions: true,
                ..LoweringConfig::default()
            },
        )
        .unwrap();
        assert_eq!(
            out.matches("    galaxy::load_fpr_single(context, ").count(),
            2
        );
        assert_eq!(
            out.matches("    galaxy::store_fpr_single(context, ")
                .count(),
            2
        );
        assert!(!out.contains("_hw("), "{out}");
    }

    fn lower_with(words: &[u32], config: LoweringConfig<'_>) -> String {
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        lower_words_with_config(
            0x8000_4000,
            &bytes,
            &BTreeSet::new(),
            &BTreeSet::new(),
            config,
        )
        .unwrap()
    }

    fn flat_config(flat_view_only: bool, stack_r11_stores: bool) -> LoweringConfig<'static> {
        LoweringConfig {
            flat_ram_reads: true,
            flat_ram_writes: true,
            flat_view_only,
            stack_r11_stores,
            ..LoweringConfig::default()
        }
    }

    /// fsel f3,f1,f2,f4; frsqrte f5,f2; fres f6,f2; fcmpo cr1,f5,f6;
    /// ps_res f7,f2; ps_rsqrte f8,f2; ps_sel f9,f1,f2,f4; ps_cmpu0 cr2,f1,f2;
    /// blr
    const FLOAT_FORMS_SEQUENCE: [u32; 9] = [
        (63 << 26) | (3 << 21) | (1 << 16) | (4 << 11) | (2 << 6) | (23 << 1),
        (63 << 26) | (5 << 21) | (2 << 11) | (26 << 1),
        (59 << 26) | (6 << 21) | (2 << 11) | (24 << 1),
        (63 << 26) | (1 << 23) | (5 << 16) | (6 << 11) | (32 << 1),
        (4 << 26) | (7 << 21) | (2 << 11) | (24 << 1),
        (4 << 26) | (8 << 21) | (2 << 11) | (26 << 1),
        (4 << 26) | (9 << 21) | (1 << 16) | (4 << 11) | (2 << 6) | (23 << 1),
        (4 << 26) | (2 << 23) | (1 << 16) | (2 << 11),
        0x4E80_0020,
    ];

    #[test]
    fn fast_float_forms_off_keeps_the_exact_estimates_and_compares() {
        let out = lower_with(
            &FLOAT_FORMS_SEQUENCE,
            LoweringConfig {
                native_float: true,
                ..flat_config(false, false)
            },
        );
        assert!(
            out.contains(
                "galaxy::ppc_f64_reciprocal_sqrt_estimate(context->fpr_bits[2], context->fpscr);"
            ),
            "{out}"
        );
        assert!(
            out.contains("galaxy::ppc_f64_reciprocal_estimate(source, context->fpscr);"),
            "{out}"
        );
        assert!(out.contains("    galaxy::compare_f64(context, 1u, context->fpr_bits[5], context->fpr_bits[6], true);\n"), "{out}");
        assert!(out.contains("galaxy::compare_f64(context, 2u, context->fpr_bits[1], context->fpr_bits[2], false);"), "{out}");
        assert!(out.contains("galaxy::f64_is_nan(selector)"), "{out}");
        assert!(
            out.contains("galaxy::ppc_commit_paired_estimate_result("),
            "{out}"
        );
        for form in [
            "fast::select_nonnegative",
            "fast::reciprocal_",
            "fast::compare_f64_cr",
            "fast::duplicates_single",
        ] {
            assert!(!out.contains(form), "{form}{out}");
        }
    }

    #[test]
    fn fast_float_forms_write_the_register_file_without_fpscr_or_commits() {
        let out = lower_with(
            &FLOAT_FORMS_SEQUENCE,
            LoweringConfig {
                native_float: true,
                fast_float_forms: true,
                ..flat_config(false, false)
            },
        );
        for line in [
            "    context->fpr_bits[3] = galaxy::fast::select_nonnegative(context->fpr_bits[1]) ? context->fpr_bits[2] : context->fpr_bits[4];\n",
            "    context->fpr_bits[5] = galaxy::fast::reciprocal_sqrt_bits(context->fpr_bits[2]);\n",
            "    { const std::uint64_t estimate = galaxy::fast::reciprocal_single_bits(context->fpr_bits[2]); context->fpr_bits[6] = estimate; if (galaxy::fast::duplicates_single(context)) context->ps1_bits[6] = estimate; }\n",
            "    galaxy::fast::compare_f64_cr(context, 1u, context->fpr_bits[5], context->fpr_bits[6]);\n",
            "    { const std::uint64_t estimate_ps0 = galaxy::fast::reciprocal_single_bits(context->fpr_bits[2]); const std::uint64_t estimate_ps1 = galaxy::fast::reciprocal_single_bits(context->ps1_bits[2]); context->fpr_bits[7] = estimate_ps0; context->ps1_bits[7] = estimate_ps1; }\n",
            "    { const std::uint64_t estimate_ps0 = galaxy::fast::reciprocal_sqrt_single_bits(context->fpr_bits[2]); const std::uint64_t estimate_ps1 = galaxy::fast::reciprocal_sqrt_single_bits(context->ps1_bits[2]); context->fpr_bits[8] = estimate_ps0; context->ps1_bits[8] = estimate_ps1; }\n",
            "    { const std::uint64_t selected_ps0 = galaxy::fast::select_nonnegative(context->fpr_bits[1]) ? context->fpr_bits[2] : context->fpr_bits[4]; const std::uint64_t selected_ps1 = galaxy::fast::select_nonnegative(context->ps1_bits[1]) ? context->ps1_bits[2] : context->ps1_bits[4]; context->fpr_bits[9] = selected_ps0; context->ps1_bits[9] = selected_ps1; }\n",
            "    galaxy::fast::compare_f64_cr(context, 2u, context->fpr_bits[1], context->fpr_bits[2]);\n",
        ] {
            assert!(out.contains(line), "{line}{out}");
        }
        assert!(!out.contains("fpscr"), "{out}");
        assert!(!out.contains("commit"), "{out}");
        // Every float operand stays in host locals under float residency: the
        // forms are no synchronization point.
        let resident = crate::residency::apply_register_residency_with_abi(&out, true, None)
            .expect("eligible");
        assert!(
            !resident.contains("context->fpr_bits[5] = fpr[5]; galaxy::fast::compare_f64_cr"),
            "{resident}"
        );
        assert!(
            resident.contains("galaxy::fast::compare_f64_cr(context, 1u, fpr[5], fpr[6]);"),
            "{resident}"
        );
        assert!(
            resident.contains("if (galaxy::fast::duplicates_single(context)) ps1[6] = estimate;"),
            "{resident}"
        );
    }

    #[test]
    fn bctrl_site_cache_follows_its_flag() {
        let config = |bctrl_site_cache| LoweringConfig {
            direct_indirect_calls: true,
            bctrl_site_cache,
            ..flat_config(false, false)
        };
        // bctrl; blr
        let words = [0x4E80_0421, 0x4E80_0020];
        let off = lower_with(&words, config(false));
        assert!(off.contains("    galaxy::fast::call_guest_indirect(services, branch_target, &cached_target_80004000, &cached_function_80004000, context, memory, 0x80004000u);\n"), "{off}");
        assert!(!off.contains("site_"), "{off}");
        let on = lower_with(&words, config(true));
        assert!(on.contains("    static std::uint32_t site_target_80004000 = galaxy::fast::kNoIndirectSite;\n    static galaxy::NativeGameFunction site_function_80004000 = nullptr;\n    galaxy::fast::call_guest_indirect_site(services, branch_target, &cached_target_80004000, &cached_function_80004000, &site_target_80004000, &site_function_80004000, context, memory, 0x80004000u);\n"), "{on}");
        assert!(!on.contains("galaxy::fast::call_guest_indirect("), "{on}");
        assert!(matches!(
            validate_module_options(&ModuleTranslationOptions {
                bctrl_site_cache: true,
                ..ModuleTranslationOptions::default()
            }),
            Err(TranslationError::OptionRequires {
                requires: "direct_indirect_calls",
                ..
            })
        ));
    }

    #[test]
    fn float_options_need_fast_float() {
        for options in [
            ModuleTranslationOptions {
                single_float_arithmetic: true,
                ..ModuleTranslationOptions::default()
            },
            ModuleTranslationOptions {
                fast_float_forms: true,
                ..ModuleTranslationOptions::default()
            },
        ] {
            assert!(matches!(
                validate_module_options(&options),
                Err(TranslationError::OptionRequires {
                    requires: "fast_float",
                    ..
                })
            ));
            assert!(validate_module_options(&ModuleTranslationOptions {
                native_float: true,
                fast_float: true,
                ..options
            })
            .is_ok());
        }
    }

    /// bl 0x80005000; bctrl; beqlr; blr
    const ABI_SEQUENCE: [u32; 4] = [
        (18 << 26) | 0x1000 | 1,
        0x4E80_0421,
        0x4D82_0020,
        0x4E80_0020,
    ];

    #[test]
    fn abi_markers_follow_their_flags_and_change_nothing_else() {
        let entries = BTreeMap::from([(0x8000_5000, 0x8000_5000)]);
        let config = |abi_call_markers, guest_return_macro| LoweringConfig {
            callable_entries: Some(&entries),
            direct_indirect_calls: true,
            abi_call_markers,
            guest_return_macro,
            ..flat_config(false, false)
        };
        let off = lower_with(&ABI_SEQUENCE, config(false, false));
        assert!(!off.contains("GALAXY_ABI_CALL"), "{off}");
        assert!(!off.contains("GALAXY_GUEST_RETURN"), "{off}");
        let on = lower_with(&ABI_SEQUENCE, config(true, true));
        assert!(on.contains("    context->lr = 0x80004004u;\n    GALAXY_ABI_CALL(0x80004000u);\n    context->pc = 0x80005000u;\n    rmge01::fn_80005000(context, memory, services);\n"), "{on}");
        assert!(
            on.contains("    context->lr = 0x80004008u;\n    GALAXY_ABI_CALL(0x80004004u);\n"),
            "{on}"
        );
        assert!(on.contains(") GALAXY_GUEST_RETURN;\n"), "{on}");
        assert!(on.contains("\n    GALAXY_GUEST_RETURN;\n"), "{on}");
        let unmarked = on
            .replace("    GALAXY_ABI_CALL(0x80004000u);\n", "")
            .replace("    GALAXY_ABI_CALL(0x80004004u);\n", "")
            .replace("GALAXY_GUEST_RETURN;", "return;");
        assert_eq!(unmarked, off);
        // Each flag alone.
        let calls = lower_with(&ABI_SEQUENCE, config(true, false));
        assert!(calls.contains("GALAXY_ABI_CALL") && !calls.contains("GALAXY_GUEST_RETURN"));
        let exits = lower_with(&ABI_SEQUENCE, config(false, true));
        assert!(!exits.contains("GALAXY_ABI_CALL") && exits.contains("GALAXY_GUEST_RETURN"));
    }

    #[test]
    fn abi_sync_options_need_register_residency() {
        for options in [
            ModuleTranslationOptions {
                abi_call_sync: true,
                ..ModuleTranslationOptions::default()
            },
            ModuleTranslationOptions {
                abi_exit_sync: true,
                ..ModuleTranslationOptions::default()
            },
        ] {
            assert!(matches!(
                validate_module_options(&options),
                Err(TranslationError::OptionRequires {
                    requires: "guest_resident_integer",
                    ..
                })
            ));
            assert!(validate_module_options(&ModuleTranslationOptions {
                guest_resident_integer: true,
                ..options
            })
            .is_ok());
        }
    }

    #[test]
    fn address_taken_functions_come_from_data_words_and_lis_pairs() {
        let functions = [
            FunctionRange {
                address: 0x8000_4000,
                size: 8,
            },
            FunctionRange {
                address: 0x8000_5000,
                size: 8,
            },
            FunctionRange {
                address: 0x8000_6000,
                size: 8,
            },
        ];
        // text: lis r12,0x8000; addi r12,r12,0x5000; blr; lwz r0,0x4000(r0)
        // (an instruction word equal to 0x80004000, not a pointer);
        // data: 0x80006000
        let text: Vec<u8> = [0x3D80_8000u32, 0x398C_5000, 0x4E80_0020, 0x8000_4000]
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        let data = 0x8000_6000u32.to_be_bytes();
        let mut image = text.clone();
        image.extend_from_slice(&data);
        let sections = [
            DolSection {
                name: "text0".to_owned(),
                kind: DolSectionKind::Text,
                index: 0,
                file_offset: 0,
                address: 0x8000_3000,
                size: text.len() as u32,
            },
            DolSection {
                name: "data0".to_owned(),
                kind: DolSectionKind::Data,
                index: 1,
                file_offset: text.len() as u32,
                address: 0x8040_0000,
                size: 4,
            },
        ];
        assert_eq!(
            discover_address_taken_functions(&sections, &image, &functions),
            BTreeSet::from([0x8000_5000, 0x8000_6000])
        );
    }

    #[test]
    fn fpr_save_and_restore_helpers_are_part_of_the_caller() {
        let frame = Regs::gpr(11);
        let all = Regs {
            fpr: u32::MAX << 14,
            ..Regs::default()
        };
        // _savefpr_14 reads r11 and f14..f31; _restfpr_14 writes f14..f31.
        assert_eq!(
            abi_inline_call(0x8051_7464),
            Some((frame.union(all), Regs::default()))
        );
        assert_eq!(abi_inline_call(0x8051_74B0), Some((frame, all)));
        // _savefpr_31 / _restfpr_31: f31 alone.
        assert_eq!(
            abi_inline_call(0x8051_74A8),
            Some((frame.union(Regs::fpr(31)), Regs::default()))
        );
        assert_eq!(abi_inline_call(0x8051_74F4), Some((frame, Regs::fpr(31))));
        // The blr after each run is not an entry.
        assert_eq!(abi_inline_call(0x8051_74AC), None);
        assert_eq!(abi_inline_call(0x8051_74F8), None);
    }

    #[test]
    fn a_switch_bctr_gets_the_case_labels_of_its_own_bounded_table() {
        const F: u32 = 0x8000_4000;
        const G: u32 = 0x8000_4040;
        let functions = [
            FunctionRange {
                address: F,
                size: 0x40,
            },
            FunctionRange {
                address: G,
                size: 0x10,
            },
        ];
        let f_words = vec![
            0x2803_0001, // cmplwi r3,1
            0x4181_002C, // bgt F+0x30 (default)
            0x3C80_8040, // lis r4,0x8040
            0x5460_103A, // slwi r0,r3,2
            0x3884_0000, // addi r4,r4,0
            0x7C84_002E, // lwzx r4,r4,r0
            0x7C89_03A6, // mtctr r4
            0x4E80_0420, // bctr
            0x3860_0000, // F+0x20 case 0: li r3,0
            0x4E80_0020,
            0x3860_0001, // F+0x28 case 1: li r3,1
            0x4E80_0020,
            0x3860_0002, // F+0x30 default (and a case of another table)
            0x4E80_0020,
            0x4E80_0020,
            0x4E80_0020,
        ];
        let g_words = vec![
            0x8183_0000, // lwz r12,0(r3)
            0x818C_0034, // lwz r12,0x34(r12)
            0x7D89_03A6, // mtctr r12
            0x4E80_0420, // bctr: a virtual call in tail position
        ];
        let mut image: Vec<u8> = f_words
            .iter()
            .chain(&g_words)
            .flat_map(|word: &u32| word.to_be_bytes())
            .collect();
        let text_len = image.len() as u32;
        // F's table (two cases) followed by a word of another table of F.
        for word in [F + 0x20, F + 0x28, F + 0x30] {
            image.extend_from_slice(&word.to_be_bytes());
        }
        let sections = [
            DolSection {
                name: "text0".to_owned(),
                kind: DolSectionKind::Text,
                index: 0,
                file_offset: 0,
                address: F,
                size: text_len,
            },
            DolSection {
                name: "data0".to_owned(),
                kind: DolSectionKind::Data,
                index: 1,
                file_offset: text_len,
                address: 0x8040_0000,
                size: 12,
            },
        ];
        let targets = discover_jump_targets(&sections, &image, &functions, &[f_words, g_words]);
        let f = &targets[&F];
        assert_eq!(f.any, vec![F + 0x20, F + 0x28, F + 0x30]);
        assert_eq!(
            f.by_site,
            BTreeMap::from([(F + 0x1C, vec![F + 0x20, F + 0x28])])
        );
        let g = &targets[&G];
        assert!(g.by_site.is_empty() && g.any.is_empty());
        assert_eq!(g.loaded, BTreeSet::from([G + 0xC]));
    }

    /// stmw r22,8(r3); stmw r20,-0x30(r1); dcbz r3,r4; blr
    const FLAT_VIEW_SEQUENCE: [u32; 4] = [
        (47 << 26) | (22 << 21) | (3 << 16) | 0x0008,
        (47 << 26) | (20 << 21) | (1 << 16) | 0xFFD0,
        (31 << 26) | (3 << 16) | (4 << 11) | (1014 << 1),
        0x4E80_0020,
    ];

    #[test]
    fn flat_view_only_off_keeps_the_checked_multiple_store_and_dcbz() {
        let out = lower_with(&FLAT_VIEW_SEQUENCE, flat_config(false, false));
        assert!(out.contains("    galaxy::guest_store_multiple_gprs<22u>(context, memory, ea_80004000, services, 0x80004000u);\n"), "{out}");
        assert!(out.contains("    galaxy::guest_store_multiple_gprs<20u>(context, memory, ea_80004004, services, 0x80004004u);\n"), "{out}");
        assert!(out.contains("    galaxy::guest_zero(memory, ea_80004008 & 0xFFFFFFE0u, 32u, services, 0x80004008u);\n"), "{out}");
        assert!(!out.contains("zero_line"));
    }

    #[test]
    fn flat_view_only_stores_every_stmw_register_and_zeroes_through_the_view() {
        let out = lower_with(&FLAT_VIEW_SEQUENCE, flat_config(true, false));
        assert!(!out.contains("guest_store_multiple_gprs"), "{out}");
        assert!(!out.contains("guest_zero"), "{out}");
        // r22..r31 from r3: ordinary flat stores, in register order.
        for reg in 22..32_u32 {
            let line = format!(
                "    galaxy::fast::store<std::uint32_t>(flat_guest_base, memory, ea_80004000 + 0x{:08X}u, context->gpr[{reg}], services, 0x80004000u);\n",
                (reg - 22) * 4
            );
            assert!(out.contains(&line), "{line}{out}");
        }
        // r20..r31 from r1: stack stores.
        for reg in 20..32_u32 {
            let line = format!(
                "    galaxy::fast::store_stack<std::uint32_t>(flat_guest_base, memory, ea_80004004 + 0x{:08X}u, context->gpr[{reg}], services, 0x80004004u);\n",
                (reg - 20) * 4
            );
            assert!(out.contains(&line), "{line}{out}");
        }
        assert!(out.contains(
            "    galaxy::fast::zero_line(flat_guest_base, memory, ea_80004008, services, 0x80004008u);\n"
        ));
        // Without the flat writes the flag has nothing to switch.
        let checked = lower_with(
            &FLAT_VIEW_SEQUENCE,
            LoweringConfig {
                flat_view_only: true,
                ..LoweringConfig::default()
            },
        );
        assert!(checked.contains("guest_store_multiple_gprs<22u>"));
        assert!(checked.contains("galaxy::guest_zero(memory,"));
    }

    #[test]
    fn flat_view_only_defines_the_header_switch() {
        let mut files = vec![
            GeneratedSource {
                name: "functions.h".to_owned(),
                contents: "#pragma once\n\n#include \"galaxy/native_api.h\"\n".to_owned(),
            },
            GeneratedSource {
                name: "functions_0000.cpp".to_owned(),
                contents: "#include \"functions.h\"\n".to_owned(),
            },
        ];
        define_before_runtime_header(&mut files, "GALAXY_FLAT_VIEW_ONLY");
        assert_eq!(
            files[0].contents,
            "#pragma once\n#define GALAXY_FLAT_VIEW_ONLY 1\n\n#include \"galaxy/native_api.h\"\n"
        );
        assert_eq!(files[1].contents, "#include \"functions.h\"\n");
        let once = files.clone();
        define_before_runtime_header(&mut files, "GALAXY_FLAT_VIEW_ONLY");
        assert_eq!(files, once);
        // The paired-singles switch stacks with it.
        define_before_runtime_header(&mut files, "GALAXY_ASSUME_PAIRED_SINGLES");
        assert_eq!(
            files[0].contents,
            "#pragma once\n#define GALAXY_ASSUME_PAIRED_SINGLES 1\n#define GALAXY_FLAT_VIEW_ONLY 1\n\n#include \"galaxy/native_api.h\"\n"
        );
    }

    #[test]
    fn module_options_switch_only_their_own_module_text() {
        let sources = || {
            vec![
                GeneratedSource {
                    name: "functions.h".to_owned(),
                    contents: "#pragma once\n\n#include \"galaxy/native_api.h\"\n".to_owned(),
                },
                GeneratedSource {
                    name: "functions_0000.cpp".to_owned(),
                    contents: "#include \"functions.h\"\n    GALAXY_CALL_RETURN_CHECKPOINT(services, 0x80004004u, 0x80004008u, context, memory);\n".to_owned(),
                },
                GeneratedSource {
                    name: "module.cpp".to_owned(),
                    contents: "#include \"functions.h\"\nextern \"C\" const galaxy::GameModuleManifestV1* galaxy_module_manifest() {\n    return &kManifest;\n}\n".to_owned(),
                },
            ]
        };
        let base = ModuleTranslationOptions::default();
        let mut untouched = sources();
        apply_module_options(&mut untouched, &base);
        assert_eq!(
            untouched,
            sources(),
            "every module switch is off by default"
        );
        // Lowering-only options leave the module text alone.
        for options in [
            ModuleTranslationOptions {
                hw_float_conversions: true,
                ..base
            },
            ModuleTranslationOptions {
                stack_r11_stores: true,
                ..base
            },
            ModuleTranslationOptions {
                abi_call_sync: true,
                abi_exit_sync: true,
                fast_float_forms: true,
                ..base
            },
        ] {
            let mut files = sources();
            apply_module_options(&mut files, &options);
            assert_eq!(files, sources());
        }
        let switched = |options: ModuleTranslationOptions| {
            let mut files = sources();
            apply_module_options(&mut files, &options);
            files
        };
        let flat = switched(ModuleTranslationOptions {
            flat_view_only: true,
            ..base
        });
        assert!(flat[0]
            .contents
            .contains("#define GALAXY_FLAT_VIEW_ONLY 1\n"));
        assert!(!flat[0].contents.contains("GALAXY_ASSUME_PAIRED_SINGLES"));
        assert_eq!(flat[1..], sources()[1..]);
        let paired = switched(ModuleTranslationOptions {
            assume_paired_singles: true,
            ..base
        });
        assert!(paired[0]
            .contents
            .contains("#define GALAXY_ASSUME_PAIRED_SINGLES 1\n"));
        assert!(!paired[0].contents.contains("GALAXY_FLAT_VIEW_ONLY"));
        assert_eq!(paired[1..], sources()[1..]);
        let single = switched(ModuleTranslationOptions {
            single_float_arithmetic: true,
            ..base
        });
        assert!(single[0]
            .contents
            .contains("#define GALAXY_SINGLE_FLOAT_ARITHMETIC 1\n"));
        assert!(!single[0].contents.contains("GALAXY_ASSUME_PAIRED_SINGLES"));
        assert_eq!(single[1..], sources()[1..]);
        let gate = switched(ModuleTranslationOptions {
            checkpoint_globals: true,
            ..base
        });
        assert!(gate[0]
            .contents
            .contains("#define GALAXY_MODULE_CHECKPOINT_GLOBALS 1\n"));
        assert!(gate[1]
            .contents
            .contains("GALAXY_CALL_RETURN_CHECKPOINT_G(services,"));
        assert!(gate[2]
            .contents
            .contains("galaxy_module_checkpoint_gate() {"));
        let all = switched(ModuleTranslationOptions {
            native_float: true,
            fast_float: true,
            checkpoint_globals: true,
            flat_view_only: true,
            assume_paired_singles: true,
            ..base
        });
        for define in [
            "GALAXY_FAST_FLOAT",
            "GALAXY_MODULE_CHECKPOINT_GLOBALS",
            "GALAXY_FLAT_VIEW_ONLY",
            "GALAXY_ASSUME_PAIRED_SINGLES",
        ] {
            assert!(
                all[0].contents.contains(&format!("#define {define} 1\n")),
                "{define}"
            );
        }
    }

    #[test]
    fn flat_view_only_and_stack_r11_stores_require_their_memory_options() {
        let release_like = ModuleTranslationOptions {
            native_float: true,
            fast_float: true,
            flat_ram_reads: true,
            flat_ram_writes: true,
            ..ModuleTranslationOptions::default()
        };
        for (options, option) in [
            (
                ModuleTranslationOptions {
                    flat_view_only: true,
                    fast_float: false,
                    ..release_like
                },
                "flat_view_only",
            ),
            (
                ModuleTranslationOptions {
                    flat_view_only: true,
                    flat_ram_reads: false,
                    ..release_like
                },
                "flat_view_only",
            ),
            (
                ModuleTranslationOptions {
                    stack_r11_stores: true,
                    flat_ram_writes: false,
                    ..release_like
                },
                "stack_r11_stores",
            ),
        ] {
            match validate_module_options(&options) {
                Err(TranslationError::OptionRequires { option: name, .. }) => {
                    assert_eq!(name, option)
                }
                other => panic!("{option}: {other:?}"),
            }
        }
        assert!(validate_module_options(&ModuleTranslationOptions {
            flat_view_only: true,
            stack_r11_stores: true,
            ..release_like
        })
        .is_ok());
    }

    /// addi r11,r1,0x60; bl _savegpr_25; blr
    const SAVEGPR_FROM_FRAME: [u32; 3] = [
        (14 << 26) | (11 << 21) | (1 << 16) | 0x0060,
        0x4851_3525,
        0x4E80_0020,
    ];

    #[test]
    fn stack_r11_stores_off_keeps_the_gather_pipe_test_in_savegpr() {
        let out = lower_with(&SAVEGPR_FROM_FRAME, flat_config(false, false));
        assert_eq!(
            out.matches(
                "galaxy::fast::store<std::uint32_t>(flat_guest_base, memory, context->gpr[11] + "
            )
            .count(),
            7,
            "{out}"
        );
        assert!(!out.contains("store_stack"), "{out}");
    }

    #[test]
    fn stack_r11_stores_store_savegpr_through_the_frame_without_the_pipe_test() {
        let out = lower_with(&SAVEGPR_FROM_FRAME, flat_config(false, true));
        for reg in 25..32_u32 {
            let line = format!(
                "    galaxy::fast::store_stack<std::uint32_t>(flat_guest_base, memory, context->gpr[11] + 0x{:08X}u, context->gpr[{reg}], services, 0x80004004u);\n",
                0xFFFF_FF80u32.wrapping_add(reg * 4)
            );
            assert!(out.contains(&line), "{line}{out}");
        }
        assert!(!out.contains("galaxy::fast::store<"), "{out}");
        // r11 from another register: unproved, the pipe test stays.
        let mut other_base = SAVEGPR_FROM_FRAME;
        other_base[0] = (14 << 26) | (11 << 21) | (3 << 16) | 0x0060; // addi r11,r3,0x60
        let unproved = lower_with(&other_base, flat_config(false, true));
        assert!(!unproved.contains("store_stack"), "{unproved}");
        // The call is an interior entry: r11 may come from elsewhere.
        let mut entries = BTreeSet::new();
        entries.insert(0x8000_4004);
        let bytes: Vec<u8> = SAVEGPR_FROM_FRAME
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        let entered = lower_words_with_config(
            0x8000_4000,
            &bytes,
            &entries,
            &BTreeSet::new(),
            flat_config(false, true),
        )
        .unwrap();
        assert!(!entered.contains("store_stack"), "{entered}");
        // Restores are loads; they never had the pipe test.
        let restore = lower_with(
            &[
                (14 << 26) | (11 << 21) | (1 << 16) | 0x0060,
                0x4851_3575, // bl _restgpr_25
                0x4E80_0020,
            ],
            flat_config(false, true),
        );
        assert!(!restore.contains("store"), "{restore}");
    }

    #[test]
    fn hw_float_conversions_require_both_flat_ram_directions() {
        let mut options = ModuleTranslationOptions {
            hw_float_conversions: true,
            flat_ram_reads: true,
            ..ModuleTranslationOptions::default()
        };
        assert!(matches!(
            validate_module_options(&options),
            Err(TranslationError::OptionRequires {
                option: "hw_float_conversions",
                ..
            })
        ));
        options.flat_ram_writes = true;
        assert!(validate_module_options(&options).is_ok());
        assert!(validate_module_options(&ModuleTranslationOptions::default()).is_ok());
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
        // Flat reads also serve lfs; stores stay checked until flat_ram_writes.
        assert!(flat
            .contains("galaxy::fast::load_fpr_single(flat_guest_read_base, context, 1u, memory,"));
        assert!(flat.contains("galaxy::guest_store_u32(memory,"));
        let writes = lower_words_with_config(
            0x8000_4000,
            &bytes,
            &BTreeSet::new(),
            &BTreeSet::new(),
            LoweringConfig {
                flat_ram_reads: true,
                flat_ram_writes: true,
                ..LoweringConfig::default()
            },
        )
        .unwrap();
        assert!(writes.contains("galaxy::fast::store<std::uint32_t>(flat_guest_base, memory,"));
        assert!(!writes.contains("galaxy::guest_store_u32(memory,"));
    }

    #[test]
    fn stack_pointer_stores_skip_the_gather_pipe_test() {
        // stwu r1,-0x10(r1); stw r0,0x14(r1); stw r0,0x8(r3); stfd f31,0x8(r1); blr
        let words = [
            0x9421_FFF0u32,
            0x9001_0014,
            0x9003_0008,
            0xDBE1_0008,
            0x4E80_0020,
        ];
        let bytes: Vec<u8> = words.iter().flat_map(|w| w.to_be_bytes()).collect();
        let out = lower_words_with_config(
            0x8000_4000,
            &bytes,
            &BTreeSet::new(),
            &BTreeSet::new(),
            LoweringConfig {
                flat_ram_reads: true,
                flat_ram_writes: true,
                ..LoweringConfig::default()
            },
        )
        .unwrap();
        assert_eq!(
            out.matches("galaxy::fast::store_stack<std::uint32_t>(")
                .count(),
            2,
            "{out}"
        );
        assert_eq!(
            out.matches("galaxy::fast::store_stack<std::uint64_t>(")
                .count(),
            1
        );
        assert_eq!(
            out.matches("galaxy::fast::store<std::uint32_t>(").count(),
            1
        );
    }

    #[test]
    fn native_float_and_direct_dispatch_lowering() {
        let words: [u32; 5] = [
            0xFC01_102A, // fadd f0,f1,f2
            0xEC01_00B2, // fmuls f0,f1,f2 (frC = f2)
            0xFC01_10BA, // fmadd f0,f1,f2,f2
            0x1024_102A, // ps_add f1,f4,f2
            0x4E80_0421, // bctrl
        ];
        let bytes = words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect::<Vec<_>>();
        let native = lower_words_with_config(
            0x8000_4000,
            &bytes,
            &BTreeSet::new(),
            &BTreeSet::new(),
            LoweringConfig {
                native_float: true,
                direct_indirect_calls: true,
                ..LoweringConfig::default()
            },
        )
        .unwrap();
        assert!(native.contains(
            "galaxy::fast::scalar_binary<galaxy::PpcFloatBinaryOperation::Add, false>(context, 0u, 1u, 2u, false, services, 0x80004000u);"
        ));
        assert!(native.contains(
            "galaxy::fast::scalar_binary<galaxy::PpcFloatBinaryOperation::Multiply, true>(context, 0u, 1u, 2u, false, services, 0x80004004u);"
        ));
        assert!(native.contains(
            "galaxy::fast::scalar_ternary<galaxy::PpcFloatTernaryOperation::MultiplyAdd, false>(context, 0u, 1u, 2u, 2u, false, services, 0x80004008u);"
        ));
        assert!(native.contains(
            "galaxy::fast::paired_binary<galaxy::PpcFloatBinaryOperation::Add, galaxy::fast::PairedLane::Each>(context, 1u, 4u, 2u, false, services, 0x8000400Cu);"
        ));
        assert!(native.contains(
            "galaxy::fast::call_guest_indirect(services, branch_target, &cached_target_80004010, &cached_function_80004010, context, memory, 0x80004010u);"
        ));
        let checked = lower_words_with_config(
            0x8000_4000,
            &bytes,
            &BTreeSet::new(),
            &BTreeSet::new(),
            LoweringConfig::default(),
        )
        .unwrap();
        assert!(!checked.contains("galaxy::fast::"));
        assert!(checked.contains("galaxy::call_guest_cached(services, branch_target,"));
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
            &BTreeSet::new(),
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
            &BTreeSet::new(),
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
            &BTreeSet::new(),
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
            &BTreeSet::new(),
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
        // The pre-rotated-mask form (`rotl(v, 32-SH) & (MASK <<< (32-SH))`) is not equivalent to `rotl(v, SH) & MASK`
        // for SH != 0. Rotating `v` by anything other than SH moves its bits relative to a mask whose position MB/ME
        // fixes. An exhaustive check over all 32x32x32 (SH, MB, ME) triples mismatched 319,448 of 393,216 encodings.
        // The emitted rotate amount and mask must therefore stay exactly `SH` and `rotate_mask(MB, ME)`.
        //
        // SH is bits 11-15, MB bits 6-10, ME bits 1-5.
        let rlwinm =
            |sh: u32, mb: u32, me: u32| (21_u32 << 26) | (sh << 11) | (mb << 6) | (me << 1);
        // slwi r0,r0,2  -> SH=2, MB=0, ME=29, MASK=0xFFFFFFFC
        let rotated = lower_instruction_fixture(0x8000_4000, &[rlwinm(2, 0, 29), 0x4E80_0020]);
        assert!(
            rotated.contains("std::rotl(context->gpr[0], 2) & 0xFFFFFFFCu;"),
            "{rotated}"
        );
        // clrrwi r3,r4,31 -> SH=0, MB=0, ME=0, MASK=0x80000000
        let zero_shift = lower_instruction_fixture(0x8000_4000, &[rlwinm(0, 0, 0), 0x4E80_0020]);
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
        // Synthetic encodings. The expected lines are the emitter spellings, including RA=0 literal semantics,
        // unsigned hex formatting and register order.
        let words = [
            0x3961_0048, // addi r11,r1,0x48
            0x3fc0_8012, // addis r30,r0,0x8012
            0x7c9c_2378, // or r28,r4,r4 (mr)
            0x54a5_1838, // rlwinm r5,r5,3,0,28 (slwi)
            0x4e80_0020, // blr
        ];
        let output = lower_instruction_fixture(0x8000_2000, &words);
        for old_line in [
            "    context->gpr[11] = context->gpr[1] + 0x00000048u;",
            "    context->gpr[30] = 0x80120000u;",
            // `or r28,r4,r4` is the move encoding: it must lower to a copy, not
            // to a self-or that re-reads the same 4 MB-context slot twice.
            "    context->gpr[28] = context->gpr[4];",
            // `rlwinm r5,r5,3,0,28` (slwi r5,r5,3): shift != 0, so the rotate stays exactly `shift` and only the mask is
            // applied. The mask is the MB..ME run (0xFFFFFFF8).
            "    context->gpr[5] = std::rotl(context->gpr[5], 3) & 0xFFFFFFF8u;",
        ] {
            assert_eq!(output.matches(old_line).count(), 1, "{old_line}");
        }
        assert_eq!(
            output
                .matches("    context->gpr[28] = context->gpr[4] | context->gpr[4];")
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
        // The emitter does not materialise an untested CTR or CR operand, so the guard is a bare
        // CR test. Assert the condition and target instead of a literal `(true) &&` wrapper.
        // BO[1] selects the tested polarity, so the two encodings must still differ.
        assert!(!output.contains("if (!galaxy::cr_bit(context, 2u)) goto label_80494580;"));

        words[0x5C / 4] = 0x4082_0054;
        let unmatched_opcode = lower_instruction_fixture(0x8049_44D0, &words);
        assert!(unmatched_opcode.contains("if (!galaxy::cr_bit(context, 2u)) goto label_80494580;"));
    }

    #[test]
    fn rmge01_ultrawide_width_hook_preserves_translated_return_shape() {
        let mut words = vec![0x6000_0000; 12];
        words[11] = 0x4E80_0020;
        let output = lower_instruction_fixture(0x803F_6B44, &words);
        let hook = "galaxy::site_hook(services, context, memory, 0x803F6B70u, galaxy::SiteHookKind::UltrawideScreenWidth, 0u);";
        assert_eq!(output.matches(hook).count(), 1);
        assert!(output.contains(&format!("{hook}\n    return;")));
        assert!(!lower_instruction_fixture(0x803F_6B40, &words).contains(hook));
        words.pop();
        words[10] = 0x4E80_0020;
        assert!(!lower_instruction_fixture(0x803F_6B44, &words).contains(hook));
    }

    #[test]
    fn rmge01_shadow_softening_calls_keep_the_native_screen_width() {
        // CollisionShadow::drawAndCaptureTex: 0x368 bytes, bl TDDraw::setup at
        // +0x208 and bl TDDraw::fix2Dpos at +0x28C, final blr.
        let mut words = vec![0x6000_0000; 0x368 / 4];
        words[0x208 / 4] = 0x480E_CC8D;
        words[0x28C / 4] = 0x480E_EFB1;
        *words.last_mut().unwrap() = 0x4E80_0020;
        let enter = "galaxy::SiteHookKind::NativeScreenWidthEnter";
        let exit = "galaxy::SiteHookKind::NativeScreenWidthExit";
        let output = lower_instruction_fixture(0x8031_3AF4, &words);
        assert_eq!(output.matches(enter).count(), 2);
        assert_eq!(output.matches(exit).count(), 2);
        // Each bracket opens inside the call's block, before the call, and
        // closes right after the block.
        for callee in ["0x80400988u", "0x80402D30u"] {
            let call = output.find(callee).expect("call is emitted");
            let open = output[..call]
                .rfind(enter)
                .expect("enter precedes the call");
            assert!(!output[open..call].contains(exit));
            let close = output[call..].find(exit).expect("exit follows the call") + call;
            assert!(!output[call..close].contains(enter));
        }
        // Another function at a different address or length gets no bracket.
        assert!(!lower_instruction_fixture(0x8031_3AF0, &words).contains(enter));
        words.pop();
        *words.last_mut().unwrap() = 0x4E80_0020;
        assert!(!lower_instruction_fixture(0x8031_3AF4, &words).contains(enter));
    }

    #[test]
    fn rmge01_ultrawide_nw4r_hook_preserves_original_lfs() {
        let mut words = vec![0x6000_0000; 0xA8 / 4];
        words[(0x803C_A6FC - 0x803C_A6C4) / 4] = 0xC062_19A8;
        *words.last_mut().unwrap() = 0x4E80_0020;
        let hook = "galaxy::site_hook(services, context, memory, 0x803CA6FCu, galaxy::SiteHookKind::Nw4rCanvasWidth, 0u);";
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
    fn rmge01_multi_scene_model_hook_requires_exact_owner_and_instruction() {
        let mut words = vec![0x6000_0000; 0x8C / 4];
        words[(0x8034_10AC - 0x8034_103C) / 4] = 0x807F_0034;
        *words.last_mut().unwrap() = 0x4E80_0020;
        let hook = "galaxy::site_hook(services, context, memory, 0x803410ACu, galaxy::SiteHookKind::MultiSceneModel, 0u);";
        assert_eq!(
            lower_instruction_fixture(0x8034_103C, &words)
                .matches(hook)
                .count(),
            1
        );
        assert!(!lower_instruction_fixture(0x8034_1038, &words).contains(hook));
        words[(0x8034_10AC - 0x8034_103C) / 4] = 0x807F_0030;
        assert!(!lower_instruction_fixture(0x8034_103C, &words).contains(hook));
    }

    #[test]
    fn rmge01_layout_geometry_hooks_require_exact_owner_range_and_boundaries() {
        let mut words = vec![0x6000_0000; 0x15C / 4];
        words[(0x8036_7D78 - 0x8036_7D28) / 4] = 0x807F_0004;
        words[(0x8036_7E64 - 0x8036_7D28) / 4] = 0xE3C1_0038;
        *words.last_mut().unwrap() = 0x4E80_0020;
        let backing = "galaxy::site_hook(services, context, memory, 0x80367D78u, galaxy::SiteHookKind::LayoutBackings, 0u);";
        let hud = "galaxy::site_hook(services, context, memory, 0x80367E64u, galaxy::SiteHookKind::CinemaAndHud, 0u);";
        let output = lower_instruction_fixture(0x8036_7D28, &words);
        assert_eq!(output.matches(backing).count(), 1);
        assert_eq!(output.matches(hud).count(), 1);
        assert!(output.find(backing).unwrap() < output.find(hud).unwrap());
        assert!(!lower_instruction_fixture(0x8036_7D24, &words).contains(backing));
        words[(0x8036_7D78 - 0x8036_7D28) / 4] = 0x807F_0008;
        assert!(!lower_instruction_fixture(0x8036_7D28, &words).contains(backing));
        words.pop();
        *words.last_mut().unwrap() = 0x4E80_0020;
        let shortened = lower_instruction_fixture(0x8036_7D28, &words);
        assert!(!shortened.contains(backing) && !shortened.contains(hud));
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
            let hook = "galaxy::SiteHookKind::LayoutConstant";
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
            let hook = "galaxy::SiteHookKind::LayoutVerticalFit";
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
        let hook = "galaxy::SiteHookKind::EfbScreenWidth";
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
        let hook = "galaxy::site_hook(services, context, memory, 0x810118F8u, galaxy::SiteHookKind::HomeBackings, 0u);";
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
        let hook = "galaxy::site_hook(services, context, memory, 0x81011B1Cu, galaxy::SiteHookKind::HomeVertical, 0u);";
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
            let hook = "galaxy::SiteHookKind::HomeGeometry";
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
        let hook = "galaxy::site_hook(services, context, memory, 0x800972B0u, galaxy::SiteHookKind::CameraAspect, 0u);";
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
        let hook = "galaxy::site_hook(services, context, memory, 0x804D074Cu, galaxy::SiteHookKind::FourThreeAspectRead, 0u);";
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

    #[test]
    fn proven_psmtx_memory_uses_symbolic_offsets_and_preserves_order() {
        let words = local_lane_fixture_words();
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let plan = validate_psmtx_local_memory_contract(&bytes).unwrap();
        let ordinary = lower_local_lane_fixture(&words).unwrap();
        let lowered = lower_psmtx_proven_memory(ordinary.clone(), &bytes, &plan).unwrap();
        assert!(!lowered.contains("galaxy::psq_load("));
        assert!(!lowered.contains("galaxy::psq_store("));
        assert!(!lowered.contains("require_paired_single_mode"));
        assert!(!lowered.contains("ensure_fpu_available"));
        assert!(lowered.contains("guest_notify_write_pair_admitted"));
        assert!(
            lowered.find("load_pair(1u, input0 + 0u)").unwrap()
                < lowered.find("load_pair(2u, input1 + 8u)").unwrap()
        );
        assert!(
            lowered.find("ppc_commit_paired_ternary_result").unwrap()
                < lowered.find("store_pair(4u, output + 0u").unwrap()
        );
        assert_eq!(
            ordinary.matches("ppc_commit_paired_ternary_result").count(),
            lowered.matches("ppc_commit_paired_ternary_result").count()
        );
        assert_eq!(
            ordinary.matches("context->gpr[1] =").count(),
            lowered.matches("context->gpr[1] =").count()
        );

        // The plan is an instruction-derived root proof, not an assumption
        // that an API register still equals its entry value at every access.
        let changed: Vec<u32> = vec![0x3863_0008, 0xE023_0000, 0x4E80_0020];
        let bytes: Vec<u8> = changed.iter().flat_map(|word| word.to_be_bytes()).collect();
        let plan = validate_psmtx_local_memory_contract(&bytes).unwrap();
        assert_eq!(plan.get(&0x804B_5F40), Some(&(3, 8)));
        let body = lower_local_lane_fixture(&changed).unwrap();
        let lowered = lower_psmtx_proven_memory(body, &bytes, &plan).unwrap();
        assert!(lowered.contains("load_pair(1u, input0 + 8u)"));
        assert!(lower_psmtx_proven_memory("changed emitter".into(), &bytes, &plan).is_err());
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
            21
        );
        assert!(ordinary.contains("ppc_commit_scalar_single_ternary"));
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
                ordinary.matches(needle).count()
                    + if matches!(
                        needle,
                        "galaxy::ppc_f32_ternary" | "galaxy::ppc_commit_scalar_result"
                    ) {
                        ordinary
                            .matches("galaxy::ppc_commit_scalar_single_ternary")
                            .count()
                    } else {
                        0
                    },
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
    fn cross_lane_merge_captures_sources_before_aliasing_destination() {
        for (op, xo, lanes) in [
            (Opcode::PsMerge00, 528u32, [0usize, 0usize]),
            (Opcode::PsMerge01, 560, [0, 1]),
            (Opcode::PsMerge10, 592, [1, 0]),
            (Opcode::PsMerge11, 624, [1, 1]),
        ] {
            for target in 0..32 {
                for left in 0..32 {
                    for right in 0..32 {
                        let word =
                            (4 << 26) | (target << 21) | (left << 16) | (right << 11) | (xo << 1);
                        let mut facts = [[true; 32]; 2];
                        let mut output = String::new();
                        cache_local_paired_instruction(
                            &mut output,
                            0,
                            &mut facts,
                            LocalLaneProfile::PsvecCross,
                            0x804B6B40,
                            word,
                            op,
                        )
                        .unwrap();
                        let capture0 = format!(
                            "const std::uint32_t cached_merge_ps0 = lane{}[{left}];",
                            lanes[0]
                        );
                        let capture1 = format!(
                            "const std::uint32_t cached_merge_ps1 = lane{}[{right}];",
                            lanes[1]
                        );
                        let write0 = format!("lane0[{target}] = cached_merge_ps0;");
                        let write1 = format!("lane1[{target}] = cached_merge_ps1;");
                        assert!(output.find(&capture0).unwrap() < output.find(&write0).unwrap());
                        assert!(output.find(&capture1).unwrap() < output.find(&write0).unwrap());
                        assert!(output.find(&write0).unwrap() < output.find(&write1).unwrap());
                        assert!(facts[0][target as usize] && facts[1][target as usize]);
                    }
                }
            }
        }
    }

    #[test]
    fn cross_lane_negation_preserves_known_provenance_and_invalidates_unknown() {
        for source in 0..32 {
            for target in 0..32 {
                for known in [[false, false], [true, false], [false, true], [true, true]] {
                    let word = (4 << 26) | (target << 21) | (source << 11) | (40 << 1);
                    let mut facts = [[true; 32]; 2];
                    facts[0][source] = known[0];
                    facts[1][source] = known[1];
                    let mut output = String::new();
                    cache_local_paired_instruction(
                        &mut output,
                        0,
                        &mut facts,
                        LocalLaneProfile::PsvecCross,
                        0x804B6B40,
                        word as u32,
                        Opcode::PsNeg,
                    )
                    .unwrap();
                    for lane in 0..2 {
                        assert_eq!(facts[lane][target], known[lane]);
                        assert_eq!(
                            output.contains(&format!(
                                "lane{lane}[{target}] = lane{lane}[{source}] ^ 0x80000000u;"
                            )),
                            known[lane]
                        );
                    }
                }
            }
        }
        // Exercise the complete public lowering path, including instruction
        // scopes and subsequent arithmetic consuming the corrected cache.
        let words: [u32; 6] = [
            (56 << 26) | (1 << 21) | (3 << 16),
            (56 << 26) | (2 << 21) | (3 << 16) | 8,
            (4 << 26) | (2 << 21) | (1 << 16) | (2 << 11) | (528 << 1),
            (4 << 26) | (2 << 21) | (2 << 11) | (40 << 1),
            (4 << 26) | (3 << 21) | (2 << 16) | (1 << 6) | (25 << 1),
            0x4E800020,
        ];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let cached = lower_words_with_config(
            0x804B6B3C,
            &bytes,
            &BTreeSet::new(),
            &BTreeSet::new(),
            LoweringConfig {
                local_lane_profile: LocalLaneProfile::PsvecCross,
                ..LoweringConfig::default()
            },
        )
        .unwrap();
        assert!(cached.contains("const std::uint32_t cached_merge_ps1 = lane0[2];"));
        assert!(cached.contains("lane0[2] = lane0[2] ^ 0x80000000u;"));
        assert!(cached.contains("lane1[2] = lane1[2] ^ 0x80000000u;"));
        assert!(!cached.contains("require_single_precision_bits"));
        assert!(cached.contains("const std::uint32_t left_ps0 = lane0[2];"));
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
        assert!(!fallback.contents.contains("fpu_retry_"));
        assert!(!fallback.contents.contains("ensure_fpu_available"));
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
    fn exact_yield_rfi_return_protocol_edges() {
        let entries = BTreeMap::from([(0x804A_B20C, 0x804A_B20C), (0x804A_381C, 0x804A_381C)]);
        for (target, pc, routed, redirected) in [
            (0x804A_B20C, 0x804A_B468, true, false),
            (0x804A_B20C, 0x804A_B7AC, true, false),
            (0x804A_B20C, 0x804A_B448, true, false),
            (0x804A_381C, 0x804A_B414, true, true),
            (0x804A_381C, 0x804A_3788, true, false),
        ] {
            let mut out = String::new();
            emit_direct_guest_call(&mut out, target, pc, Some(&entries)).unwrap();
            assert_eq!(
                out.contains("galaxy::call_guest_resolved"),
                routed,
                "{pc:08X}"
            );
            assert_eq!(
                out.contains("if (context->pc == 0x804AB30Cu) goto call_return_804AB30C;"),
                redirected,
                "{pc:08X}"
            );
        }
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
            (0x803A_387C, 0x803A_387C),
            (0x8044_0CDC, 0x8044_0CDC),
            (0x8043_3590, 0x8043_3590),
        ]);
        let exact_fpu_functions = BTreeSet::from([0x803A_387C, 0x8044_0CDC]);

        let mut pure_helper = String::new();
        emit_direct_guest_call_with_exact_fpu(
            &mut pure_helper,
            0x803A_387C,
            0x8000_4000,
            Some(&callable_entries),
            Some(&exact_fpu_functions),
        )
        .expect("FP pure-helper target falls back to exact generated function");
        assert!(pure_helper.contains("context->pc = 0x803A387Cu;"));
        assert!(pure_helper.contains("rmge01::fn_803A387C(context, memory, services);"));
        assert!(!pure_helper.contains("native_mtx_scale_803A387C"));

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
            (0x8033_E820, 0x8033_EE8C),
            (0x8033_F024, 0x803C_A76C),
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
            // These addresses recompile to the real game functions. There is no host shim for WPad input here.
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
        assert!(
            helper.contains("galaxy::lean_psvec_cross(context, memory, services, 0x804B6CB8u);")
        );
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
        assert!(normalize
            .contains("galaxy::lean_psvec_normalize(context, memory, services, 0x804B6BCCu);"));
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
                "galaxy::lean_tvec3_add_assign(context, memory, services, 0x8001CF80u);",
                "lean vector-add direct call is supported",
            ),
            (
                0x8001_FD64,
                0x8001_FD6C,
                "galaxy::lean_tvec3_scale_assign(context, memory, services, 0x8001FD6Cu);",
                "lean vector-scale direct call is supported",
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
                "galaxy::lean_psmtx_identity(context, memory, services, 0x804B5EDCu);",
                "PSMTXIdentity native helper direct call is supported",
            ),
            (
                0x804B_5F00,
                0x804B_5F08,
                "galaxy::lean_psmtx_copy(context, memory, services, 0x804B5F08u);",
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
        .expect("savegpr interior entry lowers inline");
        // _savegpr_29: stw r29..r31, -12/-8/-4(r11), no call or return boundary.
        assert!(savegpr
            .contains("context->gpr[11] + 0xFFFFFFF4u, context->gpr[29], services, 0x80517530u);"));
        assert!(savegpr
            .contains("context->gpr[11] + 0xFFFFFFFCu, context->gpr[31], services, 0x80517530u);"));
        assert!(!savegpr.contains("context->gpr[28]"));
        assert!(savegpr.contains("context->lr = 0x80517534u;"));
        assert!(!savegpr.contains("native_savegpr"));
        assert!(!savegpr.contains("call_return_checkpoint(services, 0x80517530u"));
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
        .expect("restgpr interior entry lowers inline");
        // _restgpr_29: lwz r29..r31, -12/-8/-4(r11).
        assert!(restgpr.contains("context->gpr[29] = "));
        assert!(restgpr.contains("context->gpr[11] + 0xFFFFFFF4u"));
        assert!(restgpr.contains("context->gpr[31] = "));
        assert!(!restgpr.contains("native_restgpr"));
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
        assert!(ordinary.contains("galaxy::lean_memcpy(context, memory, services, 0x80004338u);"));
        assert!(!traced.contains("lean_memcpy"));
    }

    #[test]
    fn restartability_sensitive_direct_calls_use_exact_generated_functions() {
        for (target, retired_helper) in [
            (0x8000_7D2C_u32, "native_resfont_get_char_width_80007D2C"),
            (0x8000_C53C_u32, "native_particle_list_apply_8000C53C"),
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
            (0x804B_A6B0_u32, "native_gx_begin_804BA6B0"),
            (0x804B_A7FC_u32, "native_gx_send_zero_primitive_804BA7FC"),
            (0x804B_DEA8_u32, "native_gx_begin_804BDEA8"),
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
                "galaxy::lean_tvec3_add_assign(context, memory, services, 0x8001CF80u);",
            ),
            (
                FunctionRange {
                    address: 0x8001_FD6C,
                    size: 0x28,
                },
                "galaxy::lean_tvec3_scale_assign(context, memory, services, 0x8001FD6Cu);",
            ),
            (
                FunctionRange {
                    address: 0x8034_4A54,
                    size: 0x0C,
                },
                "galaxy::native_indexed_word_load_80344A54(context, memory, services, 0x80344A54u);",
            ),
            // WPad input runs the real game functions. Native input enters at the raw-report boundary.
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
                "galaxy::lean_psmtx_identity(context, memory, services, 0x804B5EDCu);",
            ),
            (
                FunctionRange {
                    address: 0x804B_5F08,
                    size: 0x34,
                },
                "galaxy::lean_psmtx_copy(context, memory, services, 0x804B5F08u);",
            ),
            (
                FunctionRange {
                    address: 0x804B_63D8,
                    size: 0x4C,
                },
                "galaxy::lean_psmtx_trans_apply(context, memory, services, 0x804B63D8u);",
            ),
            (
                FunctionRange {
                    address: 0x804B_6424,
                    size: 0x28,
                },
                "galaxy::lean_psmtx_scale(context, memory, services, 0x804B6424u);",
            ),
            (
                FunctionRange {
                    address: 0x804B_6BCC,
                    size: 0x44,
                },
                "galaxy::lean_psvec_normalize(context, memory, services, 0x804B6BCCu);",
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
    fn fpu_function_body_keeps_generic_native_translation() {
        let function = FunctionRange {
            address: 0x803A_387C,
            size: 0x6C,
        };
        let exact_body = "    /* exact FP body marker */\n    return;\n".to_owned();
        let files = emit_sharded_module_with_exact_fpu(
            &[(function, exact_body)],
            &[],
            32,
            function.address,
            &BTreeSet::from([function.address]),
        )
        .expect("FP module emits");
        let source = files
            .iter()
            .find(|file| file.name == "functions_0000.cpp")
            .expect("function shard exists");
        assert!(source.contents.contains("exact FP body marker"));
        assert!(!source.contents.contains("ensure_fpu_available"));
        assert!(!source.contents.contains("native_mtx_scale_803A387C"));
    }

    #[test]
    fn fpu_native_substitution_metadata_is_complete_and_transitive_wrapper_is_retired() {
        for address in [
            0x803A_387C_u32,
            0x803E_595C,
            0x8044_0CDC,
            0x8044_65A0,
            0x8044_65CC,
        ] {
            assert!(
                native_substitution_uses_fpu(address),
                "0x{address:08X} must retain exact generated FP boundaries"
            );
        }
        for address in [0x8000_72B4_u32, 0x803A_33AC, 0x8044_727C, 0x804B_E59C] {
            assert!(!native_substitution_uses_fpu(address));
        }
        // SDK paired-single math has lean host bodies and needs no exact FPU.
        for address in [
            0x804B_5EDC_u32,
            0x804B_5F08,
            0x804B_5F3C,
            0x804B_63D8,
            0x804B_6424,
            0x804B_6BCC,
            0x804B_6CB8,
            0x804B_E1D8,
            0x804B_E2A0,
            0x804B_B8A8,
            0x8001_8B14,
            0x8001_8B8C,
            0x8001_CF64,
            0x8004_E9F0,
            0x8003_CD3C,
            0x8038_623C,
            0x8001_8BA0,
            0x8001_CF80,
            0x8001_FD94,
            0x8002_04F0,
            0x8002_5F68,
            0x8001_DC74,
            0x8001_DDF8,
            0x8001_8BD4,
            0x8001_8B28,
            0x8002_3404,
            0x8001_FD6C,
            0x8005_4D64,
            0x8002_0514,
            0x8001_6F70,
            0x8004_E9E0,
            0x8001_6F80,
            0x8001_8BC4,
            0x8001_8A98,
            0x8003_71D8,
            0x8001_CF44,
            0x8007_743C,
            0x800D_76B0,
            0x8001_6FBC,
            0x8002_0530,
        ] {
            assert!(!native_substitution_uses_fpu(address));
            let symbol = lean_sdk_math_symbol(address).expect("lean SDK math routine");
            assert_eq!(
                native_pure_helper(address),
                Some(NativePureHelper::integer(symbol))
            );
            assert!(native_function_body(address).is_some_and(|body| body.contains(symbol)));
        }

        assert_eq!(native_pure_helper(0x803E_4D24), None);
        assert_eq!(native_function_body(0x803E_4D24), None);
    }

    fn lean_addresses() -> Vec<(u32, &'static str)> {
        (0x8000_0000_u32..0x8060_0000)
            .step_by(4)
            .filter_map(|address| lean_sdk_math_symbol(address).map(|symbol| (address, symbol)))
            .collect()
    }

    #[test]
    fn lean_bodies_have_contracts_and_are_not_inline_wrappers() {
        let addresses = lean_addresses();
        assert!(addresses.len() > 70);
        for (address, symbol) in addresses {
            // The exact paired-single helper keeps the full boundary.
            if symbol == "native_gx_load_pos_mtx_imm_804BE1D8" {
                assert_eq!(lean_call_contract(symbol), None);
                continue;
            }
            assert!(
                lean_call_contract(symbol).is_some(),
                "{symbol} has no contract"
            );
            let entries = BTreeMap::from([(address, address)]);
            let mut output = String::new();
            assert!(
                !emit_audited_inline_wrapper_call(&mut output, address, Some(&entries))
                    .expect("wrapper query"),
                "0x{address:08X} is an audited inline wrapper"
            );
        }
    }

    /// Registers a lean body in native_api.h names literally. A nested lean
    /// call contributes that body's contract; any other use of `context`
    /// fails, except the dynamic FPR reads listed in `dynamic`.
    fn lean_body_register_uses(
        header: &str,
        symbol: &str,
        dynamic: &[(&str, &[u32])],
    ) -> LeanCallContract {
        let start = header
            .find(&format!(" void {symbol}("))
            .unwrap_or_else(|| panic!("{symbol} is not defined in native_api.h"));
        let open = start + header[start..].find('{').expect("body opens");
        let end = open + header[open..].find("\n}\n").expect("body closes");
        let body = &header[open..end];
        let mut uses = LeanCallContract {
            gpr_reads: 0,
            gpr_writes: 0,
            fpr_reads: 0,
            fpr_writes: 0,
        };
        let literal = |text: &str| -> Option<u32> {
            let digits: String = text.chars().take_while(char::is_ascii_digit).collect();
            digits.parse().ok()
        };
        let assigned = |rest: &str| {
            let rest = rest.trim_start();
            rest.starts_with('=') && !rest.starts_with("==")
        };
        let bytes = body.as_bytes();
        let mut search = 0usize;
        while let Some(offset) = body[search..].find("context") {
            let at = search + offset;
            search = at + "context".len();
            if at > 0 && (bytes[at - 1].is_ascii_alphanumeric() || bytes[at - 1] == b'_') {
                continue;
            }
            let after = &body[search..];
            if after.starts_with(|c: char| c.is_ascii_alphanumeric() || c == '_') {
                continue;
            }
            let before = &body[..at];
            if let Some(rest) = after.strip_prefix("->gpr[") {
                let index = literal(rest).expect("literal GPR index");
                let close = rest.find(']').expect("GPR index closes");
                if assigned(&rest[close + 1..]) {
                    uses.gpr_writes |= 1 << index;
                } else {
                    uses.gpr_reads |= 1 << index;
                }
            } else if let Some(rest) = after
                .strip_prefix("->fpr_bits[")
                .or_else(|| after.strip_prefix("->ps1_bits["))
            {
                let index = literal(rest).expect("literal FPR index");
                let close = rest.find(']').expect("FPR index closes");
                if assigned(&rest[close + 1..]) {
                    uses.fpr_writes |= 1 << index;
                } else {
                    uses.fpr_reads |= 1 << index;
                }
            } else if after.starts_with("->gqr[") {
                // Not a resident register.
            } else if before.ends_with("fpr_f32(") {
                match literal(after.trim_start_matches(", ")) {
                    Some(index)
                        if after[2..]
                            .trim_start_matches(char::is_numeric)
                            .starts_with("u)") =>
                    {
                        uses.fpr_reads |= 1 << index;
                    }
                    _ => {
                        let (_, registers) = dynamic
                            .iter()
                            .find(|(name, _)| *name == symbol)
                            .unwrap_or_else(|| panic!("{symbol}: unlisted dynamic FPR read"));
                        for index in *registers {
                            uses.fpr_reads |= 1 << index;
                        }
                    }
                }
            } else if before.ends_with("return_f32(") {
                uses.fpr_writes |= 1 << 1;
            } else if before.ends_with("set_f32(") {
                uses.fpr_writes |= 1 << literal(after.trim_start_matches(", ")).expect("literal");
            } else if let Some(name_start) =
                before.rfind(|c: char| !(c.is_ascii_alphanumeric() || c == '_' || c == '('))
            {
                let callee = before[name_start + 1..].trim_end_matches('(');
                let nested = lean_call_contract(callee)
                    .unwrap_or_else(|| panic!("{symbol}: context passed to {callee}"));
                uses.gpr_reads |= nested.gpr_reads;
                uses.gpr_writes |= nested.gpr_writes;
                uses.fpr_reads |= nested.fpr_reads;
                uses.fpr_writes |= nested.fpr_writes;
            }
        }
        uses
    }

    #[test]
    fn lean_call_contracts_cover_the_bodies() {
        // A Windows checkout with core.autocrlf has CRLF in the working tree.
        let header =
            include_str!("../../../runtime/include/galaxy/native_api.h").replace("\r\n", "\n");
        let dynamic: &[(&str, &[u32])] = &[("lean_psmtx_scale_apply", &[1, 2, 3])];
        let mut checked = BTreeSet::new();
        for (_, symbol) in lean_addresses() {
            let Some(contract) = lean_call_contract(symbol) else {
                continue;
            };
            if !checked.insert(symbol) {
                continue;
            }
            let uses = lean_body_register_uses(&header, symbol, dynamic);
            assert_eq!(
                uses.gpr_reads & !contract.gpr_reads,
                0,
                "{symbol} GPR reads"
            );
            assert_eq!(
                uses.gpr_writes & !contract.gpr_writes,
                0,
                "{symbol} GPR writes"
            );
            assert_eq!(
                uses.fpr_reads & !contract.fpr_reads,
                0,
                "{symbol} FPR reads"
            );
            assert_eq!(
                uses.fpr_writes & !contract.fpr_writes,
                0,
                "{symbol} FPR writes"
            );
        }
        assert!(checked.len() > 60);
    }

    #[test]
    fn checkpoint_free_leaf_predicate() {
        let words = |words: &[u32]| -> Vec<u8> {
            words.iter().flat_map(|word| word.to_be_bytes()).collect()
        };
        let address = 0x8000_4000;
        // lwz r3,0(r4); beq +8; addi r3,r3,1; stw r3,4(r4); blr
        let leaf = words(&[
            0x8064_0000,
            0x4182_0008,
            0x3863_0001,
            0x9064_0004,
            0x4E80_0020,
        ]);
        assert!(is_checkpoint_free_leaf(address, &leaf));
        // mtlr r0 and mtctr r3 are register moves.
        assert!(is_checkpoint_free_leaf(
            address,
            &words(&[0x7C08_03A6, 0x7C69_03A6, 0x4E80_0020])
        ));
        // bl, bctrl, bctr, a backward branch, sc, mtmsr and mtdec are not.
        for body in [
            vec![0x4800_0009, 0x4E80_0020],
            vec![0x4E80_0421, 0x4E80_0020],
            vec![0x4E80_0420],
            vec![0x3863_0001, 0x4BFF_FFFC, 0x4E80_0020],
            vec![0x4400_0002, 0x4E80_0020],
            vec![0x7C60_0124, 0x4E80_0020],
            vec![0x7C76_03A6, 0x4E80_0020],
        ] {
            assert!(
                !is_checkpoint_free_leaf(address, &words(&body)),
                "{body:08X?}"
            );
        }
        // A tail branch out of the function is a call.
        assert!(!is_checkpoint_free_leaf(address, &words(&[0x4800_0100])));
    }

    fn word_bytes(words: &[u32]) -> Vec<u8> {
        words.iter().flat_map(|word| word.to_be_bytes()).collect()
    }

    #[test]
    fn compare_branch_fuses_only_when_the_field_is_dead() {
        let address = 0x8000_4000;
        // cmpwi r3,0; beq +8; cmpwi r4,1; blr: the fall-through rewrites CR0 and
        // the target returns.
        let dead = word_bytes(&[0x2C03_0000, 0x4182_0008, 0x2C04_0001, 0x4E80_0020]);
        let fused = fusible_compare_branches(address, &dead, &BTreeSet::new());
        assert_eq!(
            fused.get(&0x8000_4004),
            Some(&(
                0x8000_4000,
                "static_cast<std::int32_t>(context->gpr[3]) == static_cast<std::int32_t>(0x00000000u)"
                    .to_owned()
            ))
        );
        // A branch reached from elsewhere keeps the CR write.
        assert!(
            fusible_compare_branches(address, &dead, &BTreeSet::from([0x8000_4004])).is_empty()
        );
        // cmpwi r3,0; beq +12; li r3,1; bgt +8; blr; blr: the fall-through reads CR0[GT].
        let live = word_bytes(&[
            0x2C03_0000,
            0x4182_000C,
            0x3860_0001,
            0x4181_0008,
            0x4E80_0020,
            0x4E80_0020,
        ]);
        assert!(fusible_compare_branches(address, &live, &BTreeSet::new()).is_empty());
        // cmplwi cr7,r5,16; bge cr7,+8; blr; blr: an unsigned compare in CR7.
        let unsigned = word_bytes(&[0x2B85_0010, 0x409C_0008, 0x4E80_0020, 0x4E80_0020]);
        assert_eq!(
            fusible_compare_branches(address, &unsigned, &BTreeSet::new())
                .get(&0x8000_4004)
                .map(|(_, predicate)| predicate.as_str()),
            Some("context->gpr[5] < 0x00000010u")
        );
        // CR2 is preserved across returns, so a compare into it stays.
        let callee_saved = word_bytes(&[0x2D03_0000, 0x418A_0008, 0x4E80_0020, 0x4E80_0020]);
        assert!(fusible_compare_branches(address, &callee_saved, &BTreeSet::new()).is_empty());
    }

    #[test]
    fn record_form_branch_tests_the_recorded_register() {
        // rlwinm. r0,r3,0,31,31; beq +8; li r3,1; blr; blr
        let bytes = word_bytes(&[
            0x5460_07FF,
            0x4182_000C,
            0x3860_0001,
            0x4E80_0020,
            0x4E80_0020,
        ]);
        assert_eq!(
            fusible_record_branches(0x8000_4000, &bytes, &BTreeSet::new()),
            BTreeSet::from([0x8000_4000])
        );
        let lowered = lower_words(0x8000_4000, &bytes).expect("lowers");
        assert!(lowered.contains("// CR0 record fused into the next branch"));
        assert!(!lowered.contains("update_cr0"));
        assert!(lowered.contains("if ((context->gpr[0] == 0u)) goto label_80004010;"));
        // mr. r31,r3; blt +8; blr; blr: the signed result is tested.
        let bytes = word_bytes(&[0x7C7F_1B79, 0x4180_0008, 0x4E80_0020, 0x4E80_0020]);
        let lowered = lower_words(0x8000_4000, &bytes).expect("lowers");
        assert!(lowered.contains(
            "if ((static_cast<std::int32_t>(context->gpr[31]) < 0)) goto label_8000400C;"
        ));
        // A later beq reads CR0 again, so the record form keeps its write.
        let bytes = word_bytes(&[
            0x5460_07FF,
            0x4182_000C,
            0x3860_0001,
            0x4182_0008,
            0x4E80_0020,
            0x4E80_0020,
        ]);
        assert!(fusible_record_branches(0x8000_4000, &bytes, &BTreeSet::new()).is_empty());
        assert!(lower_words(0x8000_4000, &bytes)
            .expect("lowers")
            .contains("galaxy::update_cr0(context, context->gpr[0]);"));
    }

    #[test]
    fn fused_compare_branch_lowers_to_one_condition() {
        let bytes = word_bytes(&[0x2C03_0000, 0x4182_0008, 0x2C04_0001, 0x4E80_0020]);
        let lowered = lower_words_for_module_with_call_return_entries_and_exact_fpu_flat_reads(
            0x8000_4000,
            &bytes,
            &BTreeSet::new(),
            &BTreeSet::new(),
            &BTreeMap::new(),
            &BTreeSet::new(),
            &BTreeSet::new(),
            &BTreeMap::new(),
            &ModuleTranslationOptions::default(),
        )
        .expect("lowers");
        assert!(lowered.contains("// compare fused into the next branch"));
        assert!(lowered.contains(
            "if ((static_cast<std::int32_t>(context->gpr[3]) == static_cast<std::int32_t>(0x00000000u))) goto label_8000400C;"
        ));
        // The second compare has no branch after it and still writes CR0.
        assert_eq!(
            lowered
                .matches("galaxy::compare_signed(context, 0u")
                .count(),
            1
        );
    }

    #[test]
    fn calls_to_checkpoint_free_leaves_have_no_call_return_checkpoint() {
        let words = [0x4800_0009_u32, 0x4E80_0020]; // bl +8; blr
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let target = 0x8000_4008_u32;
        let callable_entries = BTreeMap::from([(target, target)]);
        let lowered = |leaves: &BTreeSet<u32>| {
            lower_words_for_module_with_call_return_entries_and_exact_fpu_flat_reads(
                target - 8,
                &bytes,
                &BTreeSet::new(),
                &BTreeSet::new(),
                &callable_entries,
                &BTreeSet::new(),
                leaves,
                &BTreeMap::new(),
                &ModuleTranslationOptions::default(),
            )
            .expect("call lowers")
        };
        let plain = lowered(&BTreeSet::new());
        assert!(plain.contains("rmge01::fn_80004008(context, memory, services);"));
        assert!(plain.contains("galaxy::call_return_checkpoint(services, 0x80004000u, 0x80004004u"));
        let leaf = lowered(&BTreeSet::from([target]));
        assert!(leaf.contains("rmge01::fn_80004008(context, memory, services);"));
        assert!(!leaf.contains("call_return_checkpoint"));
    }

    #[test]
    fn lean_calls_have_no_call_return_checkpoint() {
        let words = [0x4800_0009_u32, 0x4E80_0020]; // bl +8; blr
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        for (target, expected) in [
            (
                0x8001_8BA0_u32,
                "galaxy::lean_tvec3_add_assign(context, memory, services, 0x80018BA0u);",
            ),
            (
                0x804B_5F3C_u32,
                "galaxy::lean_psmtx_concat(context, memory, services, 0x804B5F3Cu);",
            ),
        ] {
            let callable_entries = BTreeMap::from([(target, target)]);
            let lowered =
                lower_words_for_module(target - 8, &bytes, &BTreeSet::new(), &callable_entries)
                    .expect("lean call lowers");
            assert!(lowered.contains(expected));
            assert!(!lowered.contains("call_return_checkpoint"));
        }
        // The exact paired-single GX helper keeps its checkpoint.
        let target = 0x804B_E1D8_u32;
        let callable_entries = BTreeMap::from([(target, target)]);
        let lowered =
            lower_words_for_module(target - 8, &bytes, &BTreeSet::new(), &callable_entries)
                .expect("GX helper call lowers");
        assert!(lowered.contains("galaxy::call_return_checkpoint(services, "));
    }

    #[test]
    fn restartability_sensitive_functions_are_never_replaced() {
        for address in [
            0x8000_7D2C_u32,
            0x8000_C53C_u32,
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
            0x804B_A6B0,
            0x804B_A7FC,
            0x804B_DEA8,
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
        assert!(output.starts_with("    [[maybe_unused]] std::uint32_t checkpoint_budget__ = galaxy::kBranchCheckpointBudget;\n    if (context->pc != 0x80004000u) [[unlikely]] {\n"));
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
            "if (--checkpoint_budget__ == 0u) [[unlikely]] { checkpoint_budget__ = galaxy::kBranchCheckpointBudget; galaxy::branch_checkpoint_taken_budgeted(services, 0x80004004u, 0x80004000u, context, memory); } goto label_80004000;"
        ));

        let unconditional_words = [0x38A5_0001_u32, 0x4BFF_FFFC];
        let unconditional_bytes: Vec<u8> = unconditional_words
            .iter()
            .flat_map(|word| word.to_be_bytes())
            .collect();
        let unconditional = lower_words(0x8000_5000, &unconditional_bytes)
            .expect("backward unconditional branch is supported");
        assert!(unconditional.contains(
            "galaxy::branch_checkpoint_taken_budgeted(services, 0x80005004u, 0x80005000u, context, memory);"
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
        // An untested CTR or CR operand is not emitted, so the guard is a bare CR test.
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
        // CR0 is dead after the branch: the branch tests the operands.
        let words = [0x2C03_0000_u32, 0x4182_0008, 0x3860_0001, 0x4E80_0020];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words(0x8000_4000, &bytes).expect("compare and branch supported");
        assert!(!output.contains("galaxy::compare_signed"));
        assert!(output.contains(
            "if ((static_cast<std::int32_t>(context->gpr[3]) == static_cast<std::int32_t>(0x00000000u))) goto label_8000400C;"
        ));
        // A later bgt reads CR0, so the compare writes it and the branch reads it.
        let words = [
            0x2C03_0000_u32,
            0x4182_000C,
            0x3860_0001,
            0x4181_0008,
            0x4E80_0020,
            0x4E80_0020,
        ];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words(0x8000_4000, &bytes).expect("compare and branch supported");
        assert!(output.contains("galaxy::compare_signed(context, 0u"));
        assert!(output.contains("galaxy::cr_bit(context, 2u)"));
        assert!(output.contains("goto label_80004010;"));
    }

    #[test]
    fn arithmetic_reads_r0_while_literal_base_encodings_keep_zero() {
        // ISA register values differ from the literal-zero RA encoding used
        // by addi/addis and address generation. Seed r0 with 7 so replacing a
        // value operand with zero changes the program: add r3,r0,r4 adds 7.
        let xform = |rt: u32, ra: u32, rb: u32, xo: u32| {
            (31_u32 << 26) | (rt << 21) | (ra << 16) | (rb << 11) | (xo << 1)
        };
        let dform = |op: u32, rt: u32, imm: u16| (op << 26) | (rt << 21) | u32::from(imm);
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
            &[
                0x3800_0007,
                dform(14, 3, 9),
                dform(15, 4, 0x1234),
                0x4e80_0020,
            ],
        );
        assert!(
            output.contains("context->gpr[3] = 0x00000009u;"),
            "{output}"
        );
        assert!(
            output.contains("context->gpr[4] = 0x12340000u;"),
            "{output}"
        );
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
        // `rlwimi r7,r6,SH=8,MB=0,ME=7` is `rotl(rA, 8) & 0xFF000000` per the ISA: the field mask is
        // rotate_mask(0, 7) == 0x000000FF, and the ISA rotates the source by SH=8 before applying it. The rotate
        // amount is SH, and the cleared half of the insert is 0x00FFFFFF.
        //
        // Do not replace the rotate with a pre-rotated mask or with `32 - SH`. That rewrite is wrong for SH != 0.
        // The Rlwinm/Rlwimi emitters are checked exhaustively over all 32x32x32 SH/MB/ME triples.
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
    fn eager_fpu_emits_no_msr_guard_for_mixed_integer_and_fpu_instructions() {
        let words = [
            0x3860_0001_u32, // addi r3, r0, 1
            0xEC21_182A,     // fadds f1, f1, f3
            0x3884_0004,     // addi r4, r4, 4
            0xE025_00AC,     // psq_l f1, 172(r5), 0, 0
            0x4E80_0020,     // blr
        ];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words(0x8000_4000, &bytes).expect("mixed integer/FP body supported");

        assert!(!output.contains("ensure_fpu_available"));
        assert!(!output.contains("fpu_retry"));
    }

    #[test]
    fn eager_fpu_emits_no_msr_guard_after_mtmsr_boundary() {
        let words = [
            0xEC21_182A_u32, // fadds f1, f1, f3
            0x7C60_0124,     // mtmsr r3
            0xE025_00AC,     // psq_l f1, 172(r5), 0, 0
            0x4E80_0020,     // blr
        ];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let output = lower_words(0x8000_4000, &bytes)
            .expect("mtmsr boundary with FP instructions is supported");

        assert!(!output.contains("ensure_fpu_available"));
        // mtmsr remains an interrupt-recognition boundary with its own alias.
        assert!(output.contains("galaxy::architectural_interrupt_checkpoint("));
    }

    #[test]
    fn eager_fpu_adds_no_interior_entries_for_fp_instructions() {
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
        assert!(aliases.is_empty());

        let output = lower_words_with_entries(0x8000_4000, &bytes, &BTreeSet::new())
            .expect("straight-line FP body lowers without interior entries");
        assert!(!output.contains("switch (context->pc)"));
        assert!(!output.contains("ensure_fpu_available"));
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
        assert_eq!(
            optimized
                .matches("ppc_commit_scalar_single_ternary(")
                .count(),
            1
        );
        assert_eq!(optimized.matches("ppc_f64_binary(").count(), 1);
        assert_eq!(optimized.matches("ppc_commit_paired_result(").count(), 1);
        for pc in ["80004004", "80004008"] {
            assert!(optimized.contains(&format!("case 0x{pc}u: goto label_{pc};")));
            assert!(!optimized.contains("ensure_fpu_available"));
        }
        let capture = optimized.find("const std::uint64_t right =").unwrap();
        let call = optimized.find("try_commit_widened_scalar_binary").unwrap();
        assert!(capture < call);
        assert!(optimized.contains("context, 1u, left, right, true, services, 0x80004000u"));
        assert!(optimized.contains("context, 2u, left, right, false, services, 0x80004004u"));
        assert!(optimized.contains("PpcFloatBinaryOperation::Multiply, context->fpr_bits[1], context->fpr_bits[2], context->fpscr"));
    }

    #[test]
    fn scalar_single_fma_boundary_preserves_operands_record_and_retry_entries() {
        for (index, operation) in [
            "MultiplyAdd",
            "MultiplySubtract",
            "NegativeMultiplyAdd",
            "NegativeMultiplySubtract",
        ]
        .iter()
        .enumerate()
        {
            let xo = [29u32, 28u32, 31u32, 30u32][index];
            let word = (59u32 << 26) | (1 << 21) | (1 << 16) | (3 << 11) | (2 << 6) | (xo << 1) | 1;
            let words = [0x6000_0000, word, 0x4E80_0020];
            let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
            let output =
                lower_words_with_entries(0x8000_4000, &bytes, &BTreeSet::from([0x8000_4004]))
                    .unwrap();
            assert!(output.contains(&format!(
                "ppc_commit_scalar_single_ternary(context, 1u, galaxy::PpcFloatTernaryOperation::{operation}, 1u, 2u, 3u, true, services, 0x80004004u)")));
            assert!(!output.contains("ensure_fpu_available"));
            assert!(!output.contains("require_single_precision_bits"));
        }
    }

    #[test]
    fn indexed_paired_binary_keeps_retry_and_decoded_descriptor() {
        let forms = [
            (21u32, Opcode::PsAdd, 0u32, false, true),
            (20, Opcode::PsSub, 1, false, true),
            (25, Opcode::PsMul, 2, false, true),
            (18, Opcode::PsDiv, 3, false, true),
            (12, Opcode::PsMuls0, 2, false, false),
            (13, Opcode::PsMuls1, 2, true, true),
        ];
        for (xo, opcode, operation, lane0, lane1) in forms {
            for rc in [false, true] {
                for (target, left, right) in [(3u32, 1u32, 2u32), (1, 1, 2), (2, 1, 2)] {
                    let multiply = operation == 2;
                    let word = (4u32 << 26)
                        | (target << 21)
                        | (left << 16)
                        | (if multiply { right << 6 } else { right << 11 })
                        | (xo << 1)
                        | u32::from(rc);
                    assert_eq!(Ins::new(word, Extensions::gekko_broadway()).op, opcode);
                    let bytes: Vec<u8> = [word, word, 0x4E80_0020]
                        .iter()
                        .flat_map(|word| word.to_be_bytes())
                        .collect();
                    let entries = BTreeSet::from([0x8000_4004]);
                    let indexed = lower_words_with_config(
                        0x8000_4000,
                        &bytes,
                        &entries,
                        &BTreeSet::new(),
                        LoweringConfig {
                            indexed_paired_binary: true,
                            ..LoweringConfig::default()
                        },
                    )
                    .unwrap();
                    let descriptor = target
                        | (left << 5)
                        | (right << 10)
                        | (operation << 15)
                        | (u32::from(lane0) << 17)
                        | (u32::from(lane1) << 18)
                        | (u32::from(rc) << 19);
                    assert!(indexed.contains(&format!(
                        "context, 0x{descriptor:05X}u, services, 0x80004000u)"
                    )));
                    assert!(indexed.contains(&format!(
                        "context, 0x{descriptor:05X}u, services, 0x80004004u)"
                    )));
                    assert_eq!(
                        indexed.matches("ppc_execute_indexed_paired_binary").count(),
                        2
                    );
                    assert!(!indexed.contains("ppc_f32_binary("));
                    assert!(!indexed.contains("require_single_precision_bits"));
                    assert!(indexed.contains("case 0x80004004u: goto label_80004004;"));
                    assert!(!indexed.contains("ensure_fpu_available"));
                }
            }
        }
    }

    #[test]
    fn indexed_paired_ternary_keeps_retry_and_decoded_descriptor() {
        let forms = [
            (29u32, Opcode::PsMadd, 0u32, false, true),
            (28, Opcode::PsMsub, 1, false, true),
            (31, Opcode::PsNmadd, 2, false, true),
            (30, Opcode::PsNmsub, 3, false, true),
            (14, Opcode::PsMadds0, 0, false, false),
            (15, Opcode::PsMadds1, 0, true, true),
        ];
        for (xo, opcode, operation, lane0, lane1) in forms {
            for rc in [false, true] {
                for (target, a, c, b) in [
                    (3u32, 1u32, 2u32, 4u32),
                    (1, 1, 2, 4),
                    (2, 1, 2, 4),
                    (4, 1, 2, 4),
                    (31, 31, 31, 31),
                ] {
                    let word = (4u32 << 26)
                        | (target << 21)
                        | (a << 16)
                        | (b << 11)
                        | (c << 6)
                        | (xo << 1)
                        | u32::from(rc);
                    assert_eq!(Ins::new(word, Extensions::gekko_broadway()).op, opcode);
                    let bytes: Vec<u8> = [word, word, 0x4E80_0020]
                        .iter()
                        .flat_map(|word| word.to_be_bytes())
                        .collect();
                    let entries = BTreeSet::from([0x8000_4004]);
                    let indexed = lower_words_with_config(
                        0x8000_4000,
                        &bytes,
                        &entries,
                        &BTreeSet::new(),
                        LoweringConfig {
                            indexed_paired_ternary: true,
                            ..LoweringConfig::default()
                        },
                    )
                    .unwrap();
                    let descriptor = target
                        | (a << 5)
                        | (c << 10)
                        | (b << 15)
                        | (operation << 20)
                        | (u32::from(lane0) << 22)
                        | (u32::from(lane1) << 23)
                        | (u32::from(rc) << 24);
                    for pc in [0x8000_4000u32, 0x8000_4004] {
                        assert!(indexed.contains(&format!(
                            "context, 0x{descriptor:07X}u, services, 0x{pc:08X}u)"
                        )));
                    }
                    assert_eq!(
                        indexed
                            .matches("ppc_execute_indexed_paired_ternary")
                            .count(),
                        2
                    );
                    assert!(!indexed.contains("ppc_f32_ternary("));
                    assert!(!indexed.contains("require_single_precision_bits"));
                    assert!(indexed.contains("case 0x80004004u: goto label_80004004;"));
                    assert!(!indexed.contains("ensure_fpu_available"));
                    let control = lower_words_with_entries(0x8000_4000, &bytes, &entries).unwrap();
                    assert_eq!(control.matches("ppc_f32_ternary(").count(), 4);
                }
            }
        }
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
        assert!(fused.contains("case 0x80004004u: goto label_80004004;"));
        assert!(!fused.contains("ensure_fpu_available"));
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
    fn fused_paired_ternary_preserves_alias_capture_broadcast_and_retry_entries() {
        // Destination aliases each source in turn; RC alternates. The lane
        // broadcasts in madds0/madds1 must survive the common helper boundary.
        let forms = [
            (29_u32, "MultiplyAdd", "fpr_bits", "ps1_bits"),
            (28, "MultiplySubtract", "fpr_bits", "ps1_bits"),
            (31, "NegativeMultiplyAdd", "fpr_bits", "ps1_bits"),
            (30, "NegativeMultiplySubtract", "fpr_bits", "ps1_bits"),
            (14, "MultiplyAdd", "fpr_bits", "fpr_bits"),
            (15, "MultiplyAdd", "ps1_bits", "ps1_bits"),
        ];
        let mut words = Vec::new();
        for (index, (xo, operation, lane0, lane1)) in forms.iter().enumerate() {
            let target = [2_u32, 3, 4][index % 3];
            let word = (4_u32 << 26)
                | (target << 21)
                | (2 << 16)
                | (4 << 11)
                | (3 << 6)
                | (xo << 1)
                | (index as u32 & 1);
            words.push(word);
            let bytes: Vec<u8> = [word, 0x4E80_0020]
                .iter()
                .flat_map(|word| word.to_be_bytes())
                .collect();
            let fused = lower_words_with_config(
                0x8000_4000,
                &bytes,
                &BTreeSet::new(),
                &BTreeSet::new(),
                LoweringConfig {
                    fused_paired_ternary: true,
                    ..LoweringConfig::default()
                },
            )
            .unwrap();
            assert_eq!(fused.matches("require_single_precision_bits").count(), 6);
            assert!(fused.contains(&format!("context->{lane0}[3]")));
            assert!(fused.contains(&format!("context->{lane1}[3]")));
            let captures = fused.find("const std::uint32_t addend_ps1").unwrap();
            let call = fused.find("ppc_commit_paired_ternary_result").unwrap();
            assert!(captures < call);
            assert!(fused.contains(&format!(
                "context, {target}u, galaxy::PpcFloatTernaryOperation::{operation}, multiplicand_ps0, multiplicand_ps1, multiplier_ps0, multiplier_ps1, addend_ps0, addend_ps1, {}, services, 0x80004000u",
                index & 1 != 0
            )));
            assert!(!fused.contains("ppc_f32_ternary("));
            let control = lower_words(0x8000_4000, &bytes).unwrap();
            assert!(!control.contains("ppc_commit_paired_ternary_result"));
            assert_eq!(control.matches("ppc_f32_ternary(").count(), 2);
            let function = FunctionRange {
                address: 0x8000_4000,
                size: 8,
            };
            for (body, needs_header) in [(fused, true), (control, false)] {
                let files =
                    emit_sharded_module(&[(function, body)], &[], 1, function.address).unwrap();
                let header = &files
                    .iter()
                    .find(|f| f.name == "functions.h")
                    .unwrap()
                    .contents;
                assert_eq!(
                    header.contains("#include \"galaxy/ppc_paired_float.h\""),
                    needs_header
                );
            }
        }
        words.push(0x4E80_0020);
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let entries = BTreeSet::from([0x8000_4004]);
        let fused = lower_words_with_config(
            0x8000_4000,
            &bytes,
            &entries,
            &BTreeSet::new(),
            LoweringConfig {
                fused_paired_ternary: true,
                ..LoweringConfig::default()
            },
        )
        .unwrap();
        assert!(fused.contains("case 0x80004004u: goto label_80004004;"));
        assert!(!fused.contains("ensure_fpu_available"));
        assert_eq!(fused.matches("ppc_commit_paired_ternary_result").count(), 6);
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
        assert_eq!(
            output.matches("ppc_commit_paired_estimate_result").count(),
            2
        );
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
        assert!(cmake.contents.contains(
            "if(GALAXY_MODULE_ENABLE_LTCG AND NOT CMAKE_CXX_COMPILER_ID STREQUAL \"Clang\")"
        ));
        assert!(cmake
            .contents
            .contains("/clang:-ffp-contract=off -fwrapv -fno-strict-aliasing"));
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
        // The clang-cl branch must act on the ISA this template selects and reports. The module is compiled with
        // clang-cl, where CMAKE_CXX_COMPILER_ID is "Clang" and /arch:AVX2 is not honoured. -mavx2 must therefore be
        // present here, and /arch:AVX2 stays in the non-Clang branch. Without -mavx2 the template would announce AVX2
        // while emitting SSE2 code. -mfma pairs with -mavx2 so galaxy::fused_multiply_add in the native float fast
        // paths become hardware instructions. Contraction stays off. Never use -mavx512* (the host probe is the
        // AVX2+OSXSAVE test).
        assert!(cmake
            .contents
            .contains("target_compile_options(RMGE01_game PRIVATE -mavx2)"));
        assert!(cmake.contents.contains(
            "if(galaxy_native_isa_effective STREQUAL \"AVX2\" AND NOT CMAKE_CXX_COMPILER_ID STREQUAL \"Clang\")"));
        assert!(cmake
            .contents
            .contains("target_compile_options(RMGE01_game PRIVATE -mfma)"));
        assert!(cmake.contents.contains("/clang:-ffp-contract=off"));
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
        // Preserve the two anchors unique to the real region: its first FPU instruction and the indirect projection
        // callback which ends the region. Other fixture instructions remain NOPs, so this test covers structure
        // rather than game behavior.
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
        // These are the RMGE01 FileSelector/FileSelectItem entries consumed by the strict native route. In particular,
        // onPoint/onSelect receive the item in r4. Control-loop labels must never be used as selection input.
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
            0x8017_92D4, // Not a FileSelectItem label.
            0x8017_93CC, // Not a FileSelectItem label.
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

    // divw/divwu must call the deterministic helper. The generated C++ helper returns the Broadway result
    // instead of calling guest_execution_fault, so the code generator and the helper are tested end to end.
    #[test]
    fn lowers_divw_to_helper_call() {
        // divw r5, r3, r4 (encoding 0x7CA3_23D6)
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
        // divwu r5, r3, r4 (encoding 0x7CA3_2396)
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

// Straight-line integer GPR residency tests. They live in their own module because they call private
// items and build bodies in the shape the emitter produces.
#[cfg(test)]
mod residency_tests {
    use super::*;

    const FN: u32 = 0x8000_4000;

    #[test]
    fn residency_is_opt_in_and_preserves_the_body_otherwise() {
        let body = String::from(
            "    {\n    context->gpr[3] = 0x80000000u;\n    }\n    {\n    return;\n    }\n",
        );
        let options = ModuleTranslationOptions::default();
        assert!(!options.guest_resident_integer, "residency is opt-in");
        assert_eq!(
            apply_residency_to_body(FN, body.clone(), &BTreeSet::new(), &options, None),
            body
        );
        let enabled = ModuleTranslationOptions {
            guest_resident_integer: true,
            ..ModuleTranslationOptions::default()
        };
        assert!(
            apply_residency_to_body(FN, body, &BTreeSet::new(), &enabled, None).contains("gr3")
        );
    }

    #[test]
    fn fast_float_mode_defines_the_macro_and_exports_the_float_flags() {
        let mut files = vec![
            GeneratedSource {
                name: "functions.h".to_owned(),
                contents: "#pragma once

#include \"galaxy/native_api.h\"
"
                .to_owned(),
            },
            GeneratedSource {
                name: "functions_0000.cpp".to_owned(),
                contents: "#include \"functions.h\"
"
                .to_owned(),
            },
            GeneratedSource {
                name: "module.cpp".to_owned(),
                contents: "#include \"functions.h\"
extern \"C\" const galaxy::GameModuleManifestV1* galaxy_module_manifest() {
    return &kManifest;
}
"
                .to_owned(),
            },
            GeneratedSource {
                name: "helpers.cpp".to_owned(),
                contents: "#include \"galaxy/native_api.h\"
"
                .to_owned(),
            },
        ];
        apply_fast_float_mode(&mut files);
        assert!(files[0].contents.starts_with(
            "#pragma once
#define GALAXY_FAST_FLOAT 1
#define GALAXY_FASTMEM 1

#include \"galaxy/native_api.h\""
        ));
        assert_eq!(
            files[1].contents,
            "#include \"functions.h\"
"
        );
        assert!(!files[2].contents.contains("GALAXY_FAST_FLOAT"));
        assert!(files[2].contents.ends_with(
            "extern \"C\" std::uint32_t galaxy_module_float_flags() {
    return galaxy::kModuleFloatFlagFastFloat;
}

extern \"C\" std::uint32_t galaxy_module_transfer_flags() {
    return galaxy::kModuleTransferFlagFlagReturn |
        galaxy::kModuleTransferFlagHostSelectThread;
}
"
        ));
        assert!(files[3].contents.starts_with(
            "#define GALAXY_FAST_FLOAT 1
#define GALAXY_FASTMEM 1
#include"
        ));
        // Idempotent.
        let again = files.clone();
        apply_fast_float_mode(&mut files);
        assert_eq!(files[0].contents, again[0].contents);
        assert_eq!(files[2].contents, again[2].contents);
    }

    /// addi r3,r3,-1; bl 0x80004014; cmpwi r3,0; bne 0x80004000; blr: one
    /// call-return checkpoint and one budgeted backward-branch checkpoint.
    /// Returns the non-resident and the resident lowering, after the
    /// transfer-return pass, as the module pipeline produces them.
    fn loop_with_call() -> (String, String) {
        let words: [u32; 5] = [
            (14 << 26) | (3 << 21) | (3 << 16) | 0xFFFF,
            0x4800_0011,
            (11 << 26) | (3 << 16),
            (16 << 26) | (4 << 21) | (2 << 16) | 0xFFF4,
            0x4E80_0020,
        ];
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        let callable = BTreeMap::from([(0x8000_4014_u32, 0x8000_4014_u32)]);
        let body = lower_words_for_module_with_call_return_entries_and_exact_fpu_flat_reads(
            FN,
            &bytes,
            &BTreeSet::new(),
            &BTreeSet::new(),
            &callable,
            &BTreeSet::new(),
            &BTreeSet::new(),
            &BTreeMap::new(),
            &ModuleTranslationOptions::default(),
        )
        .unwrap();
        let resident_options = ModuleTranslationOptions {
            guest_resident_integer: true,
            ..ModuleTranslationOptions::default()
        };
        let resident =
            apply_residency_to_body(FN, body.clone(), &BTreeSet::new(), &resident_options, None);
        (
            apply_transfer_returns(body),
            apply_transfer_returns(resident),
        )
    }

    const SERVICE_TABLE_CHECKPOINTS: [&str; 7] = [
        "galaxy::call_return_checkpoint_due(services)",
        "galaxy::branch_checkpoint_due_budgeted(services)",
        "galaxy::branch_checkpoint_due(services)",
        "GALAXY_CALL_RETURN_CHECKPOINT(services,",
        "GALAXY_BRANCH_CHECKPOINT_TAKEN_BUDGETED(services,",
        "GALAXY_BRANCH_CHECKPOINT_TAKEN(services,",
        "GALAXY_RETURN_IF_TRANSFER_PENDING(services)",
    ];

    #[test]
    fn checkpoint_globals_off_keeps_the_service_table_checkpoints() {
        let (plain, resident) = loop_with_call();
        assert!(plain.contains(
            "    GALAXY_CALL_RETURN_CHECKPOINT(services, 0x80004004u, 0x80004008u, context, memory);\n"
        ));
        assert!(plain.contains("GALAXY_BRANCH_CHECKPOINT_TAKEN_BUDGETED(services, 0x8000400Cu, 0x80004000u, context, memory);"));
        assert!(resident.contains("    if (const std::uint64_t counted__ = galaxy::call_return_checkpoint_due(services); counted__ != 0u) [[unlikely]] { shared_counted__ = counted__; shared_site__ = 0u; goto shared_call_return__; }\n"));
        assert!(resident.contains("galaxy::branch_checkpoint_due_budgeted(services); counted__ != 0u) [[unlikely]] { GALAXY_RETURN_IF_TRANSFER_PENDING(services);"));
        for text in [&plain, &resident] {
            assert!(!text.contains("_g(services)"), "{text}");
            assert!(!text.contains("_G(services"), "{text}");
        }
    }

    #[test]
    fn checkpoint_globals_read_the_module_snapshot_at_every_checkpoint() {
        let (plain, resident) = loop_with_call();
        let plain_g = rewrite_checkpoint_globals(&plain);
        let resident_g = rewrite_checkpoint_globals(&resident);
        assert!(plain_g.contains(
            "    GALAXY_CALL_RETURN_CHECKPOINT_G(services, 0x80004004u, 0x80004008u, context, memory);\n"
        ));
        assert!(plain_g.contains("GALAXY_BRANCH_CHECKPOINT_TAKEN_BUDGETED_G(services, 0x8000400Cu, 0x80004000u, context, memory);"));
        assert!(resident_g.contains("    if (const std::uint64_t counted__ = galaxy::call_return_checkpoint_due_g(services); counted__ != 0u) [[unlikely]] { shared_counted__ = counted__; shared_site__ = 0u; goto shared_call_return__; }\n"));
        assert!(resident_g.contains("galaxy::branch_checkpoint_due_budgeted_g(services); counted__ != 0u) [[unlikely]] { GALAXY_RETURN_IF_TRANSFER_PENDING_G(services);"));
        assert!(resident_g.contains("shared_call_return__:\n    {\n    static constexpr std::uint32_t shared_call_return_sites__[][2] = {{0x80004004u, 0x80004008u}};\n    GALAXY_RETURN_IF_TRANSFER_PENDING_G(services);\n"));
        for (before, after) in [(&plain, &plain_g), (&resident, &resident_g)] {
            for spelling in SERVICE_TABLE_CHECKPOINTS {
                assert!(!after.contains(spelling), "{spelling} left in\n{after}");
            }
            // Only the checkpoint spellings change; everything else, the policy
            // calls in the cold blocks included, is untouched.
            let restored = after
                .replace("_due_g(services)", "_due(services)")
                .replace("_due_budgeted_g(services)", "_due_budgeted(services)")
                .replace("_G(services", "(services");
            assert_eq!(&restored, before);
        }
        // Idempotent.
        assert_eq!(rewrite_checkpoint_globals(&resident_g), resident_g);
    }

    #[test]
    fn checkpoint_globals_define_the_macro_and_export_the_gate() {
        let files = || {
            vec![
                GeneratedSource {
                    name: "functions.h".to_owned(),
                    contents: "#pragma once\n\n#include \"galaxy/native_api.h\"\n".to_owned(),
                },
                GeneratedSource {
                    name: "functions_0000.cpp".to_owned(),
                    contents: "#include \"functions.h\"\n    GALAXY_CALL_RETURN_CHECKPOINT(services, 0x80004004u, 0x80004008u, context, memory);\n".to_owned(),
                },
                GeneratedSource {
                    name: "module.cpp".to_owned(),
                    contents: "#include \"functions.h\"\nextern \"C\" const galaxy::GameModuleManifestV1* galaxy_module_manifest() {\n    return &kManifest;\n}\n".to_owned(),
                },
            ]
        };
        let mut generated = files();
        apply_checkpoint_globals(&mut generated);
        assert_eq!(
            generated[0].contents,
            "#pragma once\n#define GALAXY_MODULE_CHECKPOINT_GLOBALS 1\n\n#include \"galaxy/native_api.h\"\n"
        );
        assert_eq!(
            generated[1].contents,
            "#include \"functions.h\"\n    GALAXY_CALL_RETURN_CHECKPOINT_G(services, 0x80004004u, 0x80004008u, context, memory);\n"
        );
        assert!(generated[2].contents.ends_with(
            "\ngalaxy::ModuleCheckpointGateV1 galaxy::g_module_checkpoint_gate{};\n\n\
             extern \"C\" galaxy::ModuleCheckpointGateV1* galaxy_module_checkpoint_gate() {\n\
             \x20   return &galaxy::g_module_checkpoint_gate;\n\
             }\n"
        ));
        assert!(!generated[2].contents.contains("#define"));
        // Idempotent, and independent of the fast-float pass order.
        let once = generated.clone();
        apply_checkpoint_globals(&mut generated);
        assert_eq!(generated, once);
        let mut fast_first = files();
        apply_fast_float_mode(&mut fast_first);
        apply_checkpoint_globals(&mut fast_first);
        let mut globals_first = files();
        apply_checkpoint_globals(&mut globals_first);
        apply_fast_float_mode(&mut globals_first);
        for file in fast_first.iter().chain(&globals_first) {
            if file.name == "functions.h" {
                assert!(file
                    .contents
                    .contains("#define GALAXY_MODULE_CHECKPOINT_GLOBALS 1\n"));
                assert!(file.contents.contains("#define GALAXY_FAST_FLOAT 1\n"));
            }
        }
        assert_eq!(fast_first[1], globals_first[1]);
        for module in [&fast_first[2], &globals_first[2]] {
            assert!(module.contents.contains("galaxy_module_float_flags() {"));
            assert!(module
                .contents
                .contains("galaxy_module_checkpoint_gate() {"));
        }
    }

    #[test]
    fn checkpoint_globals_pack_shards_by_the_rewritten_text() {
        let site = "    GALAXY_CALL_RETURN_CHECKPOINT(services, 0x80004004u, 0x80004008u, context, memory);\n";
        let functions = [
            (
                FunctionRange {
                    address: 0x8000_4000,
                    size: 8,
                },
                site.repeat(40),
            ),
            (
                FunctionRange {
                    address: 0x8000_4100,
                    size: 8,
                },
                site.repeat(40),
            ),
        ];
        let exact = BTreeSet::new();
        let shard_sizes = |files: &[GeneratedSource]| {
            files
                .iter()
                .filter(|file| file.name.starts_with("functions_") && file.name.ends_with(".cpp"))
                .map(|file| file.contents.len())
                .collect::<Vec<_>>()
        };
        let roomy = emit_sharded_module_with_exact_fpu_and_source_budget(
            &functions,
            &[],
            64,
            1 << 20,
            0x8000_4000,
            &exact,
        )
        .unwrap();
        let one_shard = shard_sizes(&roomy);
        assert_eq!(one_shard.len(), 1);
        // The 80 renamed sites add two bytes each: half of that fits.
        let budget = one_shard[0] + 80;
        // Packing the plain text and renaming afterwards overflows the shard.
        let mut renamed_after = emit_sharded_module_with_exact_fpu_and_source_budget(
            &functions,
            &[],
            64,
            budget,
            0x8000_4000,
            &exact,
        )
        .unwrap();
        assert_eq!(shard_sizes(&renamed_after).len(), 1);
        apply_checkpoint_globals(&mut renamed_after);
        assert!(matches!(
            validate_function_shard_source_budget(&renamed_after, budget),
            Err(TranslationError::ShardExceedsSourceBudget { .. })
        ));
        // Packing the renamed text splits the shard; the module pass then
        // leaves the shards alone and every one fits.
        let mut packed = emit_sharded_module_with_definition_rewrite(
            &functions,
            &[],
            64,
            budget,
            0x8000_4000,
            &exact,
            LocalPairedExperiments::default(),
            Some(rewrite_checkpoint_globals),
        )
        .unwrap();
        assert_eq!(shard_sizes(&packed).len(), 2);
        let before = packed.clone();
        apply_checkpoint_globals(&mut packed);
        validate_function_shard_source_budget(&packed, budget).unwrap();
        for (after, before) in packed.iter().zip(&before) {
            if after.name.starts_with("functions_") && after.name.ends_with(".cpp") {
                assert_eq!(after, before);
                assert!(!after
                    .contents
                    .contains("GALAXY_CALL_RETURN_CHECKPOINT(services,"));
            }
        }
    }
}
