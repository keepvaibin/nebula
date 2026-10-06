mod boot_image;
mod dol;
mod dsp;
mod dsp_extract;
mod dsp_timing;
mod fst;
mod function_map;
mod game;
pub mod guest_abi_effect;
pub mod guest_cfg_liveness;
pub mod guest_ppc_effect;
mod rso;
mod sel;
mod translate;

pub use boot_image::{
    build_rmge01_boot_image, parse_rmge01_boot_image, BootImageArtifact, BootImageError,
    BootImageManifest, BootImageSection, BootImageSectionKind, RMGE01_BOOT_IMAGE_DIGEST_SHA256,
    RMGE01_BOOT_IMAGE_FILE_SHA256, RMGE01_BOOT_IMAGE_FILE_SIZE, RMGE01_BOOT_IMAGE_HEADER_SIZE,
    RMGE01_BOOT_IMAGE_PAYLOAD_SIZE, RMGE01_BOOT_IMAGE_SECTION_COUNT,
    RMGE01_BOOT_IMAGE_SECTION_RECORD_SIZE, RMGE01_BOOT_IMAGE_VERSION,
};
pub use dol::{DolImage, DolSection, DolSectionKind};
pub use dsp::{
    audit_dsp_program, data_memory_region, decode_dsp_instruction, decode_dsp_stream,
    dsp_program_identity_from_words_be, instruction_memory_region,
    lower_dsp_program_from_entry_vectors, lower_dsp_program_from_entry_vectors_with_static_memory,
    lower_dsp_program_from_entry_vectors_with_static_memory_and_raw_provenance,
    lower_dsp_program_to_cpp, lower_dsp_program_to_cpp_at_entry, DecodedDspInstruction,
    DspAccumulator, DspAccumulatorMoveSource, DspAccumulatorOperand, DspAccumulatorOperation,
    DspAccumulatorPart, DspAccumulatorShiftKind, DspAddressOperation, DspAddressRegister,
    DspAddressUpdate, DspAxPart, DspAxRegister, DspCondition, DspDataMemoryRegion, DspDecodeError,
    DspImmediateOperation, DspIndexRegister, DspInstruction, DspInstructionMemoryRegion,
    DspLogicOperand, DspLogicOperation, DspLoweringError, DspMemoryError,
    DspMemoryTransferOperation, DspModeOperation, DspMultiplyAccumulatorAction,
    DspParallelLoadStoreOrder, DspParallelOperation, DspParallelPrimary, DspProductMoveMode,
    DspProductOperation, DspProgramAudit, DspProgramIdentity, DspRegister, DspRegisterHalf,
    DspRegisterTransferOperation, DspStackRegister, DspStaticMemoryImages, DspStatusBitOperation,
    DspStatusFlag, DspWrapRegister, GeneratedDspSource, DSP_COEF_WORDS, DSP_DRAM_WORDS,
    DSP_GENERATED_LOWERING_CONTRACT_VERSION, DSP_IRAM_WORDS, DSP_IROM_WORDS,
};
pub use dsp_extract::{
    extract_dsp_ucode_candidate_in_dol, extract_rmge01_dsp_ucode_candidate, read_rmge01_ax_ucode,
    scan_dsp_ucode_candidates_in_dol, scan_rmge01_dsp_ucode_candidates, DspUcodeCandidate,
    DspUcodeDescriptorFormat, DspUcodePayload, DspUcodeScan, RMGE01_AX_UCODE_ADDRESS,
    RMGE01_AX_UCODE_BYTE_LEN, RMGE01_AX_UCODE_SHA256,
};
pub use dsp_timing::{
    require_dsp_timing, require_production_dsp_timing_execution, DspTimingBindingError,
    DspTimingBundleKey, DspTimingCaptureProvenance, DspTimingContextKey, DspTimingContractBinding,
    DspTimingEvidence, DspTimingExecutionError, DspTimingHardwareLoopOutcome,
    DspTimingInstructionKey, DspTimingInstructionOutcome, DspTimingInterruptOutcome, DspTimingKey,
    DspTimingKeyError, DspTimingMemoryAccessKey, DspTimingMemoryDirection, DspTimingMemoryRegion,
    DspTimingMemorySlot, DspTimingOutcomeKey, DspTimingPredecessorKey, DspTimingProfile,
    DspTimingProfileDocumentError, DspTimingProfileError, DspTimingProfileKind,
    DspTimingResolutionError, DspTimingRule, DspTimingTarget, ValidatedDspTimingProfile,
    DSP_TIMING_CAPTURE_SCHEMA_VERSION, DSP_TIMING_CONTRACT_VERSION,
    DSP_TIMING_PROFILE_SCHEMA_VERSION,
};
pub use fst::{parse_fst, FstError, FstFile};
pub use function_map::{rmge01_function_map, FunctionRange};
pub use game::{
    analyze_input, ExecutableAudit, FunctionAudit, GameAnalysis, GameInputKind, InputAssets,
    RMGE01_DOL_SHA1, RMGE01_GAME_ID,
};
pub use rso::{
    parse_rso, RsoCodeLoweringMetadata, RsoError, RsoExport, RsoImage, RsoImport, RsoImportBinding,
    RsoLinkError, RsoLinkLayout, RsoNativeSidecarError, RsoNativeSidecarSource, RsoRelocation,
    RsoRelocationError, RsoRelocationType, RsoSection,
};
pub use sel::{parse_sel, ResolvedSelSymbol, SelError, SelImage, SelSymbol};
pub use translate::{
    lower_ppc_code_range, lower_ppc_code_range_with_entries_and_immediate_overrides,
    translate_module, translate_module_with_options, translate_module_with_source_budget,
    translate_one, translation_coverage, GeneratedSource, ModuleTranslationOptions,
    PpcImmediateKind, PpcImmediateOverride, TranslationCoverage, TranslationError,
    TranslationModule, TranslationUnit, DEFAULT_MODULE_SHARD_SOURCE_KIB,
};
