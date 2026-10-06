//! Install-time parser for Wii RSO shared objects.
//!
//! This module deliberately parses only the on-disc, big-endian container. It
//! never executes, decodes at runtime, or embeds RSO bytes in the product. A
//! future static RSO lowering stage must consume this validated metadata and
//! emit native source from the user's dump during installation.

use crate::{
    sel::ResolvedSelSymbol,
    translate::{
        lower_ppc_code_range_with_entries_and_immediate_overrides,
        lower_ppc_code_range_with_entries_call_returns_and_immediate_overrides, PpcImmediateKind,
        PpcImmediateOverride, TranslationError,
    },
    RMGE01_DOL_SHA1,
};
use powerpc::{Extensions, Ins, Opcode};
use serde::Serialize;
use sha1::{Digest, Sha1};
use std::{
    collections::{BTreeMap, BTreeSet},
    fmt::Write,
    ops::Range,
};
use thiserror::Error;

const RSO_HEADER_SIZE: usize = 0x58;
const SECTION_INFO_SIZE: usize = 8;
const RELOCATION_SIZE: usize = 12;
const EXPORT_SIZE: usize = 16;
const IMPORT_SIZE: usize = 12;

#[derive(Debug, Error, PartialEq, Eq)]
pub enum RsoError {
    #[error("RSO is too small for its 0x58-byte header: {actual} bytes")]
    TruncatedHeader { actual: usize },
    #[error("RSO section-info offset is 0x{actual:08X}; expected 0x00000058")]
    InvalidSectionInfoOffset { actual: u32 },
    #[error("RSO has no sections")]
    MissingSections,
    #[error(
        "RSO declares {count} sections, which cannot fit in its {file_size}-byte section table"
    )]
    SectionCountOutOfRange { count: u32, file_size: usize },
    #[error(
        "RSO {table} has byte size {size}, which is not a multiple of entry size {entry_size}"
    )]
    MisalignedTableSize {
        table: &'static str,
        size: u32,
        entry_size: usize,
    },
    #[error("RSO {table} range offset=0x{offset:08X} size=0x{size:08X} is outside a {file_size}-byte file")]
    TableOutOfRange {
        table: &'static str,
        offset: u32,
        size: u32,
        file_size: usize,
    },
    #[error("RSO section {index} range offset=0x{offset:08X} size=0x{size:08X} is outside a {file_size}-byte file")]
    SectionOutOfRange {
        index: u32,
        offset: u32,
        size: u32,
        file_size: usize,
    },
    #[error("RSO file-backed sections {first} and {second} overlap")]
    OverlappingSections { first: u32, second: u32 },
    #[error("RSO string '{field}' starts at 0x{offset:08X}, outside a {file_size}-byte file")]
    StringOutOfRange {
        field: &'static str,
        offset: u32,
        file_size: usize,
    },
    #[error(
        "RSO string '{field}' at 0x{offset:08X} has no terminating NUL within its allowed range"
    )]
    UnterminatedString { field: &'static str, offset: u32 },
    #[error("RSO string '{field}' at 0x{offset:08X} is not valid UTF-8")]
    InvalidUtf8String { field: &'static str, offset: u32 },
    #[error("RSO relocation type {actual} is unsupported")]
    UnsupportedRelocationType { actual: u8 },
    #[error("RSO {table} relocation {index} patches 0x{offset:08X}, which is not a writable word inside a file-backed section")]
    RelocationOffsetOutsideSection {
        table: &'static str,
        index: u32,
        offset: u32,
    },
    #[error("RSO internal relocation {index} refers to section {section}, but only {section_count} sections exist")]
    InvalidInternalRelocationSection {
        index: u32,
        section: u32,
        section_count: usize,
    },
    #[error("RSO internal relocation {index} refers to offset 0x{offset:08X} beyond section {section} size 0x{section_size:08X}")]
    InvalidInternalRelocationOffset {
        index: u32,
        section: u32,
        offset: u32,
        section_size: u32,
    },
    #[error("RSO external relocation {index} refers to import {import}, but only {import_count} imports exist")]
    InvalidExternalRelocationImport {
        index: u32,
        import: u32,
        import_count: usize,
    },
    #[error("RSO external relocation {index} has nonzero symbol offset 0x{offset:08X}")]
    InvalidExternalRelocationOffset { index: u32, offset: u32 },
    #[error("RSO import {index} starts its relocation range at 0x{offset:08X}, outside the 0x{external_size:08X}-byte external relocation table")]
    InvalidImportRelocationOffset {
        index: u32,
        offset: u32,
        external_size: u32,
    },
    #[error("RSO import {index} relocation range contains relocation {relocation_index} for import {actual_import}")]
    ImportRelocationOwnershipMismatch {
        index: u32,
        relocation_index: u32,
        actual_import: u32,
    },
    #[error(
        "RSO export {index} refers to section {section}, but only {section_count} sections exist"
    )]
    InvalidExportSection {
        index: u32,
        section: u32,
        section_count: usize,
    },
    #[error("RSO export {index} refers to offset 0x{offset:08X} beyond section {section} size 0x{section_size:08X}")]
    InvalidExportOffset {
        index: u32,
        section: u32,
        offset: u32,
        section_size: u32,
    },
    #[error("RSO declares BSS size 0x{declared:08X}, but its BSS sections total 0x{observed:08X}")]
    BssSizeMismatch { declared: u32, observed: u32 },
}

/// Errors while resolving a structurally-valid RSO import table through a
/// structurally-valid SEL table.  Link failure is installation failure: the
/// runtime must never invent an address or replace a missing module routine.
#[derive(Debug, Error, PartialEq, Eq)]
pub enum RsoLinkError {
    #[error("RSO import '{name}' has no matching SEL linker symbol")]
    MissingImport { name: String },
    #[error(
        "RSO import '{name}' has {count} matching SEL linker symbols; resolution is ambiguous"
    )]
    AmbiguousImport { name: String, count: usize },
}

/// Explicit installer-time section addresses used to apply a validated RSO's
/// relocations. Runtime code never consumes this representation.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RsoLinkLayout {
    pub section_addresses: Vec<u32>,
}

/// The installer-only information needed to lower one RSO code section into
/// native source.  It has no on-disc payload: runtime code receives the
/// generated source only, never relocation records or PPC instruction bytes.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RsoCodeLoweringMetadata {
    pub entries: BTreeSet<u32>,
    pub immediate_overrides: BTreeMap<u32, PpcImmediateOverride>,
}

/// Installer-produced source for a native dynamic-RSO sidecar.  Both strings
/// are written only beneath the caller's generated output directory; neither
/// retains the source RSO payload in the repository or asks the runtime to
/// decode PPC instructions.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RsoNativeSidecarSource {
    pub cpp: String,
    pub cmake: String,
    pub rso_sha1: String,
    pub code_section: u32,
    pub code_size: u32,
    pub entry_count: usize,
    pub symbolic_immediate_count: usize,
}

/// A hard failure while emitting a statically recompiled dynamic-RSO sidecar.
/// A missing dispatch entry, unsupported relocation, or unsupported PPC form
/// is an installation failure.  It must never turn into a runtime fallback.
#[derive(Debug, Error)]
pub enum RsoNativeSidecarError {
    #[error(transparent)]
    Relocation(#[from] RsoRelocationError),
    #[error(transparent)]
    Translation(#[from] TranslationError),
    #[error("RSO code section {section} does not have file-backed code")]
    CodeSectionNotFileBacked { section: u32 },
    #[error("RSO {field} section {section} is missing, not file-backed, or has an invalid offset")]
    InvalidSpecialSection { field: &'static str, section: u8 },
    #[error("RSO code section {section} byte range overflows host memory")]
    CodeSectionRangeOverflow { section: u32 },
    #[error("RSO code section {section} range is outside its {input_len}-byte input")]
    CodeSectionOutsideInput { section: u32, input_len: usize },
    #[error("lowered RSO code section {section} omitted its required interior-entry dispatch")]
    MissingInteriorDispatch { section: u32 },
    #[error(
        "lowered RSO code section {section} has an unrecognized interior-entry dispatch: {detail}"
    )]
    InvalidInteriorDispatch { section: u32, detail: String },
    #[error(
        "lowered RSO code section {section} has an unrecognized internal-return dispatch: {detail}"
    )]
    InvalidInternalReturnDispatch { section: u32, detail: String },
    #[error(
        "lowered RSO code section {section} has a canonical address range that overflows 32 bits"
    )]
    CanonicalAddressOverflow { section: u32 },
    #[error(
        "RSO external import '{name}' at binding index {index} resolves to 0x{address:08X}, inside the canonical code range 0x{code_start:08X}..0x{code_end:08X}; a disjoint installer layout is required"
    )]
    ExternalBindingOverlapsCodeRange {
        index: u32,
        name: String,
        address: u32,
        code_start: u32,
        code_end: u32,
    },
    #[error("failed to format generated native RSO sidecar source")]
    Formatting,
}

/// Hard failures while applying already-validated RSO relocation records.
/// Static recompilation must not guess a target, truncate a displacement, or
/// manufacture a linker trampoline.
#[derive(Debug, Error, PartialEq, Eq)]
pub enum RsoRelocationError {
    #[error("RSO relocation input is {actual} bytes, but the parsed container requires at least {required} bytes")]
    InputTooSmall { actual: usize, required: usize },
    #[error("RSO link layout provides {provided} section addresses, but the RSO has {expected} sections")]
    SectionAddressCount { provided: usize, expected: usize },
    #[error("RSO import binding index {index} is outside the {import_count}-entry import table")]
    BindingIndexOutOfRange { index: u32, import_count: usize },
    #[error("RSO import binding index {index} appears more than once")]
    DuplicateBinding { index: u32 },
    #[error("RSO import '{name}' at index {index} has no resolved SEL binding")]
    MissingBinding { index: u32, name: String },
    #[error("RSO {table} relocation {index} target address overflows 32 bits")]
    TargetAddressOverflow { table: &'static str, index: u32 },
    #[error("RSO {table} relocation {index} patch address cannot be mapped to a section runtime address")]
    PatchAddressUnavailable { table: &'static str, index: u32 },
    #[error("RSO {table} relocation {index} uses {relocation:?}, which static linking does not yet implement")]
    UnsupportedRelocation {
        table: &'static str,
        index: u32,
        relocation: RsoRelocationType,
    },
    #[error("RSO {table} relocation {index} has a REL24 displacement {displacement}, outside the signed 26-bit branch range")]
    Rel24OutOfRange {
        table: &'static str,
        index: u32,
        displacement: i64,
    },
    #[error("RSO {table} relocation {index} has unaligned REL24 displacement {displacement}")]
    Rel24Misaligned {
        table: &'static str,
        index: u32,
        displacement: i64,
    },
    #[error("RSO {table} relocation {index} uses REL24 on an absolute branch instruction")]
    Rel24AbsoluteBranch { table: &'static str, index: u32 },
    #[error("RSO {table} relocation {index} at 0x{offset:08X} uses {relocation:?} in a static code immediate that has no native lowering")]
    UnsupportedCodeImmediate {
        table: &'static str,
        index: u32,
        offset: u32,
        relocation: RsoRelocationType,
    },
    #[error("RSO code section {section} has more than one symbolic immediate at native PC 0x{address:08X}")]
    DuplicateCodeImmediate { section: u32, address: u32 },
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum RsoRelocationType {
    None,
    Addr32,
    Addr24,
    Addr16,
    Addr16Lo,
    Addr16Hi,
    Addr16Ha,
    Addr14,
    Addr14BrTaken,
    Addr14BrNotTaken,
    Rel24,
    Rel14,
}

impl TryFrom<u8> for RsoRelocationType {
    type Error = RsoError;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(Self::None),
            1 => Ok(Self::Addr32),
            2 => Ok(Self::Addr24),
            3 => Ok(Self::Addr16),
            4 => Ok(Self::Addr16Lo),
            5 => Ok(Self::Addr16Hi),
            6 => Ok(Self::Addr16Ha),
            7 => Ok(Self::Addr14),
            8 => Ok(Self::Addr14BrTaken),
            9 => Ok(Self::Addr14BrNotTaken),
            10 => Ok(Self::Rel24),
            11 => Ok(Self::Rel14),
            actual => Err(RsoError::UnsupportedRelocationType { actual }),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct RsoSection {
    pub index: u32,
    pub file_offset: Option<u32>,
    pub size: u32,
}

impl RsoSection {
    #[must_use]
    pub fn file_range(&self) -> Option<Range<u64>> {
        self.file_offset.map(|offset| {
            let start = u64::from(offset);
            start..start + u64::from(self.size)
        })
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct RsoRelocation {
    pub file_offset: u32,
    pub symbol_index: u32,
    pub relocation_type: RsoRelocationType,
    pub symbol_offset: u32,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct RsoExport {
    pub name: String,
    pub section_index: u32,
    pub section_offset: u32,
    pub elf_hash: u32,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct RsoImport {
    pub name: String,
    pub symbol_offset: u32,
    /// Byte offset of this import's first relocation inside the external
    /// relocation table. The next import's offset (or table end) bounds the
    /// range, so the table is validated without executing any RSO code.
    pub relocation_offset: u32,
}

/// An installer-time, fully resolved RSO import.  This is metadata only; it
/// contains no RSO payload and never authorizes runtime symbol lookup.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct RsoImportBinding {
    pub import_index: u32,
    pub name: String,
    pub guest_address: u32,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct RsoImage {
    pub module_name: Option<String>,
    pub version: u32,
    pub bss_size: u32,
    pub prolog_section: u8,
    pub epilog_section: u8,
    pub unresolved_section: u8,
    pub prolog_offset: u32,
    pub epilog_offset: u32,
    pub unresolved_offset: u32,
    pub sections: Vec<RsoSection>,
    pub internal_relocations: Vec<RsoRelocation>,
    pub external_relocations: Vec<RsoRelocation>,
    pub exports: Vec<RsoExport>,
    pub imports: Vec<RsoImport>,
}

impl RsoImage {
    #[must_use]
    pub fn section(&self, index: u32) -> Option<&RsoSection> {
        self.sections.get(usize::try_from(index).ok()?)
    }

    /// Resolves every import exactly once through the installer-provided SEL
    /// metadata.  Missing or duplicate names are hard failures because either
    /// condition would make a relocation non-deterministic.
    pub fn bind_imports(
        &self,
        symbols: &[ResolvedSelSymbol],
    ) -> Result<Vec<RsoImportBinding>, RsoLinkError> {
        let mut symbols_by_name = BTreeMap::<&str, Vec<&ResolvedSelSymbol>>::new();
        for symbol in symbols {
            symbols_by_name
                .entry(symbol.name.as_str())
                .or_default()
                .push(symbol);
        }

        self.imports
            .iter()
            .enumerate()
            .map(|(index, import)| {
                let Some(candidates) = symbols_by_name.get(import.name.as_str()) else {
                    return Err(RsoLinkError::MissingImport {
                        name: import.name.clone(),
                    });
                };
                let [symbol] = candidates.as_slice() else {
                    return Err(RsoLinkError::AmbiguousImport {
                        name: import.name.clone(),
                        count: candidates.len(),
                    });
                };
                Ok(RsoImportBinding {
                    import_index: u32::try_from(index).expect("import index fits u32"),
                    name: import.name.clone(),
                    guest_address: symbol.guest_address,
                })
            })
            .collect()
    }

    /// Applies the RSO relocation records to an installer-owned copy of the
    /// input bytes using explicit section addresses and exact SEL bindings.
    /// The result is transient installation data; this method neither emits
    /// nor executes guest instructions at runtime.
    pub fn apply_relocations(
        &self,
        input: &[u8],
        layout: &RsoLinkLayout,
        bindings: &[RsoImportBinding],
    ) -> Result<Vec<u8>, RsoRelocationError> {
        let required = self
            .sections
            .iter()
            .filter_map(|section| section.file_range().map(|range| range.end))
            .max()
            .unwrap_or(0);
        let required = usize::try_from(required).expect("RSO input range fits usize");
        if input.len() < required {
            return Err(RsoRelocationError::InputTooSmall {
                actual: input.len(),
                required,
            });
        }
        if layout.section_addresses.len() != self.sections.len() {
            return Err(RsoRelocationError::SectionAddressCount {
                provided: layout.section_addresses.len(),
                expected: self.sections.len(),
            });
        }

        let mut import_addresses = vec![None; self.imports.len()];
        for binding in bindings {
            let index = usize::try_from(binding.import_index).unwrap_or(usize::MAX);
            let Some(slot) = import_addresses.get_mut(index) else {
                return Err(RsoRelocationError::BindingIndexOutOfRange {
                    index: binding.import_index,
                    import_count: self.imports.len(),
                });
            };
            if slot.replace(binding.guest_address).is_some() {
                return Err(RsoRelocationError::DuplicateBinding {
                    index: binding.import_index,
                });
            }
        }
        for (index, import) in self.imports.iter().enumerate() {
            if import_addresses[index].is_none() {
                return Err(RsoRelocationError::MissingBinding {
                    index: u32::try_from(index).expect("import index fits u32"),
                    name: import.name.clone(),
                });
            }
        }

        let mut linked = input.to_vec();
        for (index, relocation) in self.internal_relocations.iter().enumerate() {
            let relocation_index = u32::try_from(index).expect("relocation index fits u32");
            let symbol_index = usize::try_from(relocation.symbol_index).expect("validated index");
            let symbol_address = layout.section_addresses[symbol_index]
                .checked_add(relocation.symbol_offset)
                .ok_or(RsoRelocationError::TargetAddressOverflow {
                    table: "internal",
                    index: relocation_index,
                })?;
            apply_relocation(
                &mut linked,
                self,
                "internal",
                relocation_index,
                relocation,
                symbol_address,
                layout,
            )?;
        }
        for (index, relocation) in self.external_relocations.iter().enumerate() {
            let relocation_index = u32::try_from(index).expect("relocation index fits u32");
            let symbol_index = usize::try_from(relocation.symbol_index).expect("validated index");
            let symbol_address = import_addresses[symbol_index].expect("all imports checked");
            apply_relocation(
                &mut linked,
                self,
                "external",
                relocation_index,
                relocation,
                symbol_address,
                layout,
            )?;
        }
        Ok(linked)
    }

    /// Produces the precise symbolic immediate expressions and address-taken
    /// entries needed to lower one file-backed RSO code section. All metadata
    /// is derived during installation; a runtime native function receives no
    /// relocation records or PPC bytes.
    pub fn code_section_lowering_metadata(
        &self,
        input: &[u8],
        section_index: u32,
        layout: &RsoLinkLayout,
        bindings: &[RsoImportBinding],
    ) -> Result<RsoCodeLoweringMetadata, RsoRelocationError> {
        if layout.section_addresses.len() != self.sections.len() {
            return Err(RsoRelocationError::SectionAddressCount {
                provided: layout.section_addresses.len(),
                expected: self.sections.len(),
            });
        }
        let code_section =
            self.section(section_index)
                .ok_or(RsoRelocationError::PatchAddressUnavailable {
                    table: "code-section",
                    index: section_index,
                })?;
        let code_file_offset =
            code_section
                .file_offset
                .ok_or(RsoRelocationError::PatchAddressUnavailable {
                    table: "code-section",
                    index: section_index,
                })?;
        let code_end = usize::try_from(code_file_offset)
            .expect("u32 fits usize")
            .checked_add(usize::try_from(code_section.size).expect("u32 fits usize"))
            .expect("validated section range fits usize");
        if input.len() < code_end {
            return Err(RsoRelocationError::InputTooSmall {
                actual: input.len(),
                required: code_end,
            });
        }

        let mut import_addresses = vec![None; self.imports.len()];
        for binding in bindings {
            let index = usize::try_from(binding.import_index).unwrap_or(usize::MAX);
            let Some(slot) = import_addresses.get_mut(index) else {
                return Err(RsoRelocationError::BindingIndexOutOfRange {
                    index: binding.import_index,
                    import_count: self.imports.len(),
                });
            };
            if slot.replace(binding.guest_address).is_some() {
                return Err(RsoRelocationError::DuplicateBinding {
                    index: binding.import_index,
                });
            }
        }
        for (index, import) in self.imports.iter().enumerate() {
            if import_addresses[index].is_none() {
                return Err(RsoRelocationError::MissingBinding {
                    index: u32::try_from(index).expect("import index fits u32"),
                    name: import.name.clone(),
                });
            }
        }

        let code_address = layout.section_addresses
            [usize::try_from(section_index).expect("validated section index")];
        let mut entries = BTreeSet::new();
        let mut entry_offsets = vec![
            (u32::from(self.prolog_section), self.prolog_offset),
            (u32::from(self.epilog_section), self.epilog_offset),
            (u32::from(self.unresolved_section), self.unresolved_offset),
        ];
        for export in &self.exports {
            entry_offsets.push((export.section_index, export.section_offset));
        }
        for (index, relocation) in self.internal_relocations.iter().enumerate() {
            if matches!(
                relocation.relocation_type,
                RsoRelocationType::Addr32
                    | RsoRelocationType::Addr16Lo
                    | RsoRelocationType::Addr16Ha
            ) && relocation.symbol_index == section_index
            {
                // Address constructors can take code pointers through a full word
                // or through HA/LO immediates. Admit exactly the target produced by
                // the linker, including the original unsigned patch addend.
                let full_word = relocation.relocation_type == RsoRelocationType::Addr32;
                let patch = usize::try_from(relocation.file_offset).expect("u32 fits usize");
                let required = patch.checked_add(if full_word { 4 } else { 2 }).ok_or(
                    RsoRelocationError::PatchAddressUnavailable {
                        table: "internal",
                        index: u32::try_from(index).expect("relocation index fits u32"),
                    },
                )?;
                if input.len() < required {
                    return Err(RsoRelocationError::InputTooSmall {
                        actual: input.len(),
                        required,
                    });
                }
                let target = code_address
                    .checked_add(relocation.symbol_offset)
                    .and_then(|address| {
                        address.checked_add(if full_word {
                            read_u32_at(input, patch)
                        } else {
                            u32::from(read_u16_at(input, patch))
                        })
                    })
                    .ok_or(RsoRelocationError::TargetAddressOverflow {
                        table: "internal",
                        index: u32::try_from(index).expect("relocation index fits u32"),
                    })?;
                entry_offsets.push((section_index, target - code_address));
            }
        }
        for (entry_section, offset) in entry_offsets {
            if entry_section == section_index
                && offset != 0
                && offset < code_section.size
                && offset % 4 == 0
            {
                let entry = code_address.checked_add(offset).ok_or(
                    RsoRelocationError::TargetAddressOverflow {
                        table: "code-entry",
                        index: section_index,
                    },
                )?;
                entries.insert(entry);
            }
        }

        let mut immediate_overrides = BTreeMap::new();
        let mut add_immediate = |table: &'static str,
                                 index: usize,
                                 relocation: &RsoRelocation,
                                 symbol_expression: String|
         -> Result<(), RsoRelocationError> {
            let patch = relocation.file_offset;
            let code_file_end = u64::from(code_file_offset) + u64::from(code_section.size);
            if u64::from(patch) < u64::from(code_file_offset) || u64::from(patch) >= code_file_end {
                return Ok(());
            }
            let relocation_index = u32::try_from(index).expect("relocation index fits u32");
            let kind = match relocation.relocation_type {
                RsoRelocationType::Addr16Lo => PpcImmediateKind::Low16,
                RsoRelocationType::Addr16Ha => PpcImmediateKind::HighAdjusted16,
                // Relative branch and address-word relocations were already
                // applied to the canonical installer image.  They do not
                // rewrite a D-form/addi immediate in the generated body.
                RsoRelocationType::Addr16 | RsoRelocationType::Addr16Hi => {
                    return Err(RsoRelocationError::UnsupportedCodeImmediate {
                        table,
                        index: relocation_index,
                        offset: patch,
                        relocation: relocation.relocation_type,
                    });
                }
                _ => return Ok(()),
            };
            if patch % 4 != 2 {
                return Err(RsoRelocationError::UnsupportedCodeImmediate {
                    table,
                    index: relocation_index,
                    offset: patch,
                    relocation: relocation.relocation_type,
                });
            }
            let patch_offset = usize::try_from(patch).expect("u32 fits usize");
            let addend = u32::from(read_u16_at(input, patch_offset));
            let expression = if addend == 0 {
                symbol_expression
            } else {
                format!("({symbol_expression}) + 0x{addend:04X}u")
            };
            let instruction_offset = patch
                .checked_sub(code_file_offset)
                .and_then(|relative| relative.checked_sub(2))
                .expect("patch range and alignment were validated");
            let pc = code_address.checked_add(instruction_offset).ok_or(
                RsoRelocationError::TargetAddressOverflow {
                    table,
                    index: relocation_index,
                },
            )?;
            if immediate_overrides
                .insert(pc, PpcImmediateOverride { kind, expression })
                .is_some()
            {
                return Err(RsoRelocationError::DuplicateCodeImmediate {
                    section: section_index,
                    address: pc,
                });
            }
            Ok(())
        };
        for (index, relocation) in self.internal_relocations.iter().enumerate() {
            let target_section = relocation.symbol_index;
            let expression = format!(
                "rso_section_{target_section} + 0x{:08X}u",
                relocation.symbol_offset
            );
            add_immediate("internal", index, relocation, expression)?;
        }
        for (index, relocation) in self.external_relocations.iter().enumerate() {
            let target = import_addresses
                [usize::try_from(relocation.symbol_index).expect("validated import index")]
            .expect("all imports checked");
            add_immediate("external", index, relocation, format!("0x{target:08X}u"))?;
        }
        Ok(RsoCodeLoweringMetadata {
            entries,
            immediate_overrides,
        })
    }

    /// Lowers one validated RSO text section into a separately compiled native
    /// sidecar.  Relocation records and PPC bytes exist only in this
    /// installer-time method.  The emitted sidecar receives the loader's
    /// section addresses, then executes only the already generated C++ body.
    pub fn generate_native_sidecar(
        &self,
        input: &[u8],
        code_section: u32,
        layout: &RsoLinkLayout,
        bindings: &[RsoImportBinding],
    ) -> Result<RsoNativeSidecarSource, RsoNativeSidecarError> {
        let section =
            self.section(code_section)
                .ok_or(RsoNativeSidecarError::CodeSectionNotFileBacked {
                    section: code_section,
                })?;
        let file_offset =
            section
                .file_offset
                .ok_or(RsoNativeSidecarError::CodeSectionNotFileBacked {
                    section: code_section,
                })?;
        let start = usize::try_from(file_offset).expect("u32 fits usize");
        let end = start
            .checked_add(usize::try_from(section.size).expect("u32 fits usize"))
            .ok_or(RsoNativeSidecarError::CodeSectionRangeOverflow {
                section: code_section,
            })?;
        if end > input.len() {
            return Err(RsoNativeSidecarError::CodeSectionOutsideInput {
                section: code_section,
                input_len: input.len(),
            });
        }
        let canonical_address = *layout
            .section_addresses
            .get(usize::try_from(code_section).expect("u32 fits usize"))
            .ok_or(RsoRelocationError::SectionAddressCount {
                provided: layout.section_addresses.len(),
                expected: self.sections.len(),
            })?;
        let canonical_end = canonical_address.checked_add(section.size).ok_or(
            RsoNativeSidecarError::CanonicalAddressOverflow {
                section: code_section,
            },
        )?;
        // The PPC lowerer distinguishes a statically linked RSO-local branch
        // from a native DOL call by its resolved target address. A synthetic
        // installer layout must therefore never cover a DOL import: doing so
        // would turn an external REL24 into a local `goto`. Refuse such a
        // layout before emitting any source instead of guessing a trampoline
        // or accepting semantically different code.
        for binding in bindings {
            if binding.guest_address >= canonical_address && binding.guest_address < canonical_end {
                return Err(RsoNativeSidecarError::ExternalBindingOverlapsCodeRange {
                    index: binding.import_index,
                    name: binding.name.clone(),
                    address: binding.guest_address,
                    code_start: canonical_address,
                    code_end: canonical_end,
                });
            }
        }

        let linked = self.apply_relocations(input, layout, bindings)?;
        let metadata =
            self.code_section_lowering_metadata(input, code_section, layout, bindings)?;
        // Normal native calls must return to their C++ caller frame. Keep a
        // finite, installer-derived entry set for that synchronous path;
        // RFI uses the separate full instruction-boundary resume map below.
        let preliminary_body = lower_ppc_code_range_with_entries_and_immediate_overrides(
            canonical_address,
            &linked[start..end],
            &metadata.entries,
            &metadata.immediate_overrides,
        )?;
        let mut call_entries = metadata.entries.clone();
        call_entries.extend(collect_call_return_entries(
            &preliminary_body,
            canonical_address,
            section.size,
            code_section,
        )?);
        // Every aligned word in this validated code section has a compiled
        // label. This map is used only after RFI has discarded the native
        // caller stack; it never grants an ordinary guest call a new target.
        let mut resume_entries = BTreeSet::new();
        let first_resume_address = canonical_address.checked_add(4).ok_or(
            RsoNativeSidecarError::CanonicalAddressOverflow {
                section: code_section,
            },
        )?;
        for address in (first_resume_address..canonical_end).step_by(4) {
            resume_entries.insert(address);
        }
        // The generic lowerer can infer direct linked-call returns. Linked
        // indirect calls (bctrl/blrl) need their return continuations declared
        // explicitly, otherwise an RFI unwinding that native caller frame can
        // resume at a label without performing its required checkpoint.
        let indirect_call_returns =
            linked_indirect_call_return_entries(canonical_address, &linked[start..end]);
        let body = lower_ppc_code_range_with_entries_call_returns_and_immediate_overrides(
            canonical_address,
            &linked[start..end],
            &resume_entries,
            &indirect_call_returns,
            &metadata.immediate_overrides,
        )?;
        let runtime_body = rewrite_dynamic_pc_literals(
            &body,
            canonical_address,
            section.size,
            &resume_entries,
            code_section,
        )?;
        let rso_sha1 = format!("{:x}", Sha1::digest(input));
        let cpp = render_native_sidecar_cpp(
            self,
            NativeSidecarRenderParams {
                code_section,
                code_file_offset: file_offset,
                canonical_address,
                call_entries: &call_entries,
                symbolic_immediate_count: metadata.immediate_overrides.len(),
                body: &runtime_body,
                rso_sha1: &rso_sha1,
            },
        )?;
        Ok(RsoNativeSidecarSource {
            cpp,
            cmake: native_sidecar_cmake(),
            rso_sha1,
            code_section,
            code_size: section.size,
            entry_count: call_entries.len() + 1,
            symbolic_immediate_count: metadata.immediate_overrides.len(),
        })
    }
}

/// Finds return PCs for bctrl/blrl instructions in a statically linked RSO
/// text range. The decoder is used only at installation; the emitted sidecar
/// contains native labels and never retains these PPC words.
fn linked_indirect_call_return_entries(canonical_address: u32, bytes: &[u8]) -> BTreeSet<u32> {
    let canonical_end = canonical_address
        .checked_add(u32::try_from(bytes.len()).expect("RSO code length fits u32"))
        .expect("caller validates canonical RSO code range");
    bytes
        .chunks_exact(4)
        .enumerate()
        .filter_map(|(index, chunk)| {
            let pc = canonical_address + u32::try_from(index).expect("RSO word index fits u32") * 4;
            let word = u32::from_be_bytes(chunk.try_into().expect("four-byte instruction"));
            let instruction = Ins::new(word, Extensions::gekko_broadway());
            let return_pc = pc.wrapping_add(4);
            (matches!(instruction.op, Opcode::Bcctr | Opcode::Bclr)
                && word & 1 != 0
                && return_pc < canonical_end)
                .then_some(return_pc)
        })
        .collect()
}

fn collect_call_return_entries(
    body: &str,
    canonical_address: u32,
    code_size: u32,
    code_section: u32,
) -> Result<BTreeSet<u32>, RsoNativeSidecarError> {
    let canonical_end = canonical_address.checked_add(code_size).ok_or(
        RsoNativeSidecarError::CanonicalAddressOverflow {
            section: code_section,
        },
    )?;
    let mut entries = BTreeSet::new();
    for line in body.lines() {
        let Some(address) = line
            .strip_prefix("call_return_")
            .and_then(|rest| rest.strip_suffix(':'))
        else {
            continue;
        };
        if address.len() != 8 || !address.bytes().all(|byte| byte.is_ascii_hexdigit()) {
            return Err(RsoNativeSidecarError::InvalidInternalReturnDispatch {
                section: code_section,
                detail: format!("malformed call-return label `{line}`"),
            });
        }
        let value =
            u32::from_str_radix(address, 16).expect("eight hexadecimal source digits always parse");
        if value < canonical_address || value >= canonical_end {
            return Err(RsoNativeSidecarError::InvalidInternalReturnDispatch {
                section: code_section,
                detail: format!("call-return label 0x{value:08X} is outside the code range"),
            });
        }
        entries.insert(value);
    }
    Ok(entries)
}

fn rewrite_dynamic_pc_literals(
    body: &str,
    canonical_address: u32,
    code_size: u32,
    entries: &BTreeSet<u32>,
    code_section: u32,
) -> Result<String, RsoNativeSidecarError> {
    let canonical_end = canonical_address.checked_add(code_size).ok_or(
        RsoNativeSidecarError::CanonicalAddressOverflow {
            section: code_section,
        },
    )?;
    let call_return_entries =
        collect_call_return_entries(body, canonical_address, code_size, code_section)?;
    let dispatch_rewritten = if entries.is_empty() {
        body.to_owned()
    } else {
        let expected_start = format!(
            "    if (context->pc != 0x{canonical_address:08X}u) [[unlikely]] {{\n    switch (context->pc) {{"
        );
        if !body.starts_with(&expected_start) {
            return Err(RsoNativeSidecarError::MissingInteriorDispatch {
                section: code_section,
            });
        }
        let dispatch_end_marker =
            "        default: galaxy::guest_execution_fault(services, context->pc, \"invalid interior function entry\");\n    }\n    }\n";
        let dispatch_end = body
            .find(dispatch_end_marker)
            .map(|index| index + dispatch_end_marker.len())
            .ok_or(RsoNativeSidecarError::MissingInteriorDispatch {
                section: code_section,
            })?;
        let dispatch_cases_end = dispatch_end - dispatch_end_marker.len();
        let dispatch_cases = body[expected_start.len()..dispatch_cases_end]
            .strip_prefix('\n')
            .ok_or_else(|| RsoNativeSidecarError::InvalidInteriorDispatch {
                section: code_section,
                detail: "interior-entry switch does not start on the next line".to_owned(),
            })?;
        let mut dispatch_lines = dispatch_cases.lines();
        let mut destinations = Vec::with_capacity(entries.len());
        for entry in entries {
            let line = dispatch_lines.next().ok_or_else(|| {
                RsoNativeSidecarError::InvalidInteriorDispatch {
                    section: code_section,
                    detail: format!("missing case for 0x{entry:08X}"),
                }
            })?;
            let prefix = format!("        case 0x{entry:08X}u: goto ");
            let destination = line
                .strip_prefix(&prefix)
                .and_then(|rest| rest.strip_suffix(';'))
                .ok_or_else(|| RsoNativeSidecarError::InvalidInteriorDispatch {
                    section: code_section,
                    detail: format!("malformed case `{line}` for 0x{entry:08X}"),
                })?;
            let label_destination = format!("label_{entry:08X}");
            let call_return_destination = format!("call_return_{entry:08X}");
            let fpu_retry_destination = format!("fpu_retry_{entry:08X}");
            if destination != label_destination
                && destination != call_return_destination
                && destination != fpu_retry_destination
            {
                return Err(RsoNativeSidecarError::InvalidInteriorDispatch {
                    section: code_section,
                    detail: format!("unrecognized target `{destination}` for 0x{entry:08X}"),
                });
            }
            // The sidecar turns every statically discovered linked-call
            // continuation into a flat-dispatch checkpoint. For all other
            // entries, preserve the lowerer's exact destination: in
            // particular, an exception-7 retry must enter fpu_retry_* so it
            // revalidates MSR[FP] before reaching the instruction label.
            if call_return_entries.contains(entry) {
                if destination == fpu_retry_destination {
                    return Err(RsoNativeSidecarError::InvalidInteriorDispatch {
                        section: code_section,
                        detail: format!(
                            "0x{entry:08X} is both an FPU retry and a linked-call continuation"
                        ),
                    });
                }
                destinations.push(call_return_destination);
            } else {
                if destination == call_return_destination {
                    return Err(RsoNativeSidecarError::InvalidInteriorDispatch {
                        section: code_section,
                        detail: format!(
                            "0x{entry:08X} targets an undiscovered linked-call continuation"
                        ),
                    });
                }
                destinations.push(destination.to_owned());
            }
        }
        if let Some(line) = dispatch_lines.next() {
            return Err(RsoNativeSidecarError::InvalidInteriorDispatch {
                section: code_section,
                detail: format!("unexpected extra case `{line}`"),
            });
        }
        let mut rewritten = String::new();
        rewritten.push_str("    if (context->pc != rso_pc(0x00000000u)) [[unlikely]] {\n");
        // Dispatch validated instruction offsets once. A linear relocated-address
        // comparison for every label makes each Home-menu re-entry O(code size).
        rewritten.push_str("        switch (context->pc - rso_pc(0x00000000u)) {\n");
        for (entry, destination) in entries.iter().zip(destinations) {
            let offset = entry.checked_sub(canonical_address).ok_or(
                RsoNativeSidecarError::CanonicalAddressOverflow {
                    section: code_section,
                },
            )?;
            writeln!(rewritten, "        case {offset}u: goto {destination};",)
                .map_err(|_| RsoNativeSidecarError::Formatting)?;
        }
        rewritten.push_str(
            "        default: galaxy::guest_execution_fault(services, context->pc, \"invalid dynamic RSO interior entry\");\n        }\n    }\n",
        );
        rewritten.push_str(&body[dispatch_end..]);
        rewritten
    };

    // Every lowerer-generated BCLR return switch contains the same finite
    // set of direct-call continuations. Rather than reproduce that table at
    // every return site, expose the LR as a precompiled re-entry PC. The
    // sidecar dispatcher accepts only the explicit continuation labels above;
    // an unknown dynamic target remains a hard execution fault.
    let return_switch_marker = "switch (context->lr & 0xFFFFFFFCu) {\n";
    let mut return_switch_rewritten = String::with_capacity(dispatch_rewritten.len());
    let mut remaining = dispatch_rewritten.as_str();
    loop {
        let Some(switch_start) = remaining.find(return_switch_marker) else {
            return_switch_rewritten.push_str(remaining);
            break;
        };
        let line_start = remaining[..switch_start]
            .rfind('\n')
            .map_or(0, |index| index + 1);
        let indentation = &remaining[line_start..switch_start];
        if !indentation.bytes().all(|byte| byte == b' ') {
            return Err(RsoNativeSidecarError::InvalidInternalReturnDispatch {
                section: code_section,
                detail: "switch indentation is not spaces".to_owned(),
            });
        }
        let cases_start = switch_start + return_switch_marker.len();
        let switch_end = format!("{indentation}default: return;\n{indentation}}}\n");
        let end_relative = remaining[cases_start..].find(&switch_end).ok_or(
            RsoNativeSidecarError::InvalidInternalReturnDispatch {
                section: code_section,
                detail: format!("missing `{indentation}default: return` terminator"),
            },
        )?;
        let case_prefix = format!("{indentation}case 0x");
        for line in remaining[cases_start..cases_start + end_relative].lines() {
            let Some(rest) = line.strip_prefix(&case_prefix) else {
                return Err(RsoNativeSidecarError::InvalidInternalReturnDispatch {
                    section: code_section,
                    detail: format!("unexpected return case `{line}`"),
                });
            };
            let Some((address, destination)) = rest.split_once("u: goto call_return_") else {
                return Err(RsoNativeSidecarError::InvalidInternalReturnDispatch {
                    section: code_section,
                    detail: format!("unexpected return target `{line}`"),
                });
            };
            if address.len() != 8
                || !address.bytes().all(|byte| byte.is_ascii_hexdigit())
                || !destination.ends_with(';')
                || !call_return_entries.contains(
                    &u32::from_str_radix(address, 16)
                        .expect("eight hexadecimal source digits always parse"),
                )
            {
                return Err(RsoNativeSidecarError::InvalidInternalReturnDispatch {
                    section: code_section,
                    detail: format!("malformed return case `{line}`"),
                });
            }
        }
        return_switch_rewritten.push_str(&remaining[..line_start]);
        writeln!(
            return_switch_rewritten,
            "{indentation}context->pc = context->lr & 0xFFFFFFFCu;\n{indentation}*resume_rso_return = true;\n{indentation}return;"
        )
        .map_err(|_| RsoNativeSidecarError::Formatting)?;
        remaining = &remaining[cases_start + end_relative + switch_end.len()..];
    }

    let mut rewritten = String::with_capacity(return_switch_rewritten.len());
    let bytes = return_switch_rewritten.as_bytes();
    let mut copied = 0usize;
    let mut cursor = 0usize;
    while cursor + 11 <= bytes.len() {
        if bytes[cursor] == b'0'
            && bytes[cursor + 1] == b'x'
            && bytes[cursor + 10] == b'u'
            && bytes[cursor + 2..cursor + 10]
                .iter()
                .all(u8::is_ascii_hexdigit)
        {
            let literal = &return_switch_rewritten[cursor + 2..cursor + 10];
            let value = u32::from_str_radix(literal, 16)
                .expect("eight hexadecimal source digits always parse");
            if value >= canonical_address && value < canonical_end {
                rewritten.push_str(&return_switch_rewritten[copied..cursor]);
                let offset = value - canonical_address;
                write!(rewritten, "rso_pc(0x{offset:08X}u)")
                    .map_err(|_| RsoNativeSidecarError::Formatting)?;
                cursor += 11;
                copied = cursor;
                continue;
            }
        }
        cursor += 1;
    }
    rewritten.push_str(&return_switch_rewritten[copied..]);
    Ok(rewritten)
}

struct NativeSidecarRenderParams<'a> {
    code_section: u32,
    code_file_offset: u32,
    canonical_address: u32,
    call_entries: &'a BTreeSet<u32>,
    symbolic_immediate_count: usize,
    body: &'a str,
    rso_sha1: &'a str,
}

fn render_native_sidecar_cpp(
    image: &RsoImage,
    params: NativeSidecarRenderParams<'_>,
) -> Result<String, RsoNativeSidecarError> {
    let NativeSidecarRenderParams {
        code_section,
        code_file_offset,
        canonical_address,
        call_entries,
        symbolic_immediate_count,
        body,
        rso_sha1,
    } = params;
    let section_count = image.sections.len();
    let code_size = image
        .section(code_section)
        .expect("caller validated code section")
        .size;
    let special_file_offset = |field: &'static str, section_index: u8, offset: u32| {
        let section = image.sections.get(usize::from(section_index)).ok_or(
            RsoNativeSidecarError::InvalidSpecialSection {
                field,
                section: section_index,
            },
        )?;
        let base = section
            .file_offset
            .ok_or(RsoNativeSidecarError::InvalidSpecialSection {
                field,
                section: section_index,
            })?;
        if offset >= section.size {
            return Err(RsoNativeSidecarError::InvalidSpecialSection {
                field,
                section: section_index,
            });
        }
        base.checked_add(offset)
            .ok_or(RsoNativeSidecarError::InvalidSpecialSection {
                field,
                section: section_index,
            })
    };
    let prolog_file_offset =
        special_file_offset("prolog", image.prolog_section, image.prolog_offset)?;
    let epilog_file_offset =
        special_file_offset("epilog", image.epilog_section, image.epilog_offset)?;
    let unresolved_file_offset = special_file_offset(
        "unresolved",
        image.unresolved_section,
        image.unresolved_offset,
    )?;
    let mut source = String::new();
    source.push_str("#include \"galaxy/native_api.h\"\n#include \"galaxy/ppc_float.h\"\n");
    // The aspect header depends on the ABI and float declarations above.
    if body.contains("galaxy::apply_experimental_rmge01_home_geometry(") {
        source.push_str("#include \"galaxy/experimental_ultrawide_aspect.h\"\n");
    }
    source.push_str(
        "\n#include <array>\n#include <cstdio>\n#include <cstdint>\n#include <cstdlib>\n#include <cstring>\n\nnamespace {\n\n",
    );
    if body.contains("galaxy::apply_rmge01_home_vertical(") {
        source.insert_str(0, "#include \"galaxy/layout_aspect_home.h\"\n");
    }
    if body.contains("galaxy::apply_rmge01_home_backings(") {
        source.insert_str(0, "#include \"galaxy/layout_aspect.h\"\n");
    }
    writeln!(
        source,
        "constexpr std::uint32_t kSectionCount = {section_count}u;"
    )
    .map_err(|_| RsoNativeSidecarError::Formatting)?;
    writeln!(
        source,
        "constexpr std::uint32_t kCodeSection = {code_section}u;\nconstexpr std::uint32_t kCodeFileOffset = 0x{code_file_offset:08X}u;\nconstexpr std::uint32_t kCodeSize = 0x{code_size:08X}u;\nconstexpr std::uint32_t kCanonicalCodeAddress = 0x{canonical_address:08X}u;"
    )
    .map_err(|_| RsoNativeSidecarError::Formatting)?;
    source.push_str("constexpr std::uint32_t kNoFileOffset = 0xFFFFFFFFu;\n\n");
    writeln!(
        source,
        "constexpr std::array<std::uint32_t, {section_count}> kSectionSizes{{{{"
    )
    .map_err(|_| RsoNativeSidecarError::Formatting)?;
    for section in &image.sections {
        writeln!(source, "    0x{:08X}u,", section.size)
            .map_err(|_| RsoNativeSidecarError::Formatting)?;
    }
    source.push_str("}};\n");
    writeln!(
        source,
        "constexpr std::array<std::uint32_t, {section_count}> kFileOffsets{{{{"
    )
    .map_err(|_| RsoNativeSidecarError::Formatting)?;
    for section in &image.sections {
        writeln!(
            source,
            "    0x{:08X}u,",
            section.file_offset.unwrap_or(u32::MAX)
        )
        .map_err(|_| RsoNativeSidecarError::Formatting)?;
    }
    source.push_str("}};\n\n");
    writeln!(
        source,
        "struct LoadedRsoLayout {{\n    std::array<std::uint32_t, {section_count}> sections{{}};\n}};\n"
    )
    .map_err(|_| RsoNativeSidecarError::Formatting)?;

    source.push_str(
        "bool load_rso_layout(\n    std::uint32_t rso_base,\n    galaxy::PpcContext* context,\n    galaxy::GuestMemoryV1* memory,\n    const galaxy::NativeServicesV1* services,\n    LoadedRsoLayout* layout) {\n    if (context == nullptr || memory == nullptr || services == nullptr ||\n        rso_base < 0x90000000u || rso_base >= 0x94000000u) {\n        return false;\n    }\n    const auto load = [&](std::uint32_t address) {\n        return galaxy::guest_load_u32(memory, address, services, context->pc);\n    };\n    if (load(rso_base + 0x08u) != kSectionCount ||\n        load(rso_base + 0x0Cu) != rso_base + 0x58u) {\n        return false;\n    }\n",
    );
    writeln!(
        source,
        "    if (load(rso_base + 0x24u) != rso_base + 0x{prolog_file_offset:08X}u ||\n        load(rso_base + 0x28u) != rso_base + 0x{epilog_file_offset:08X}u ||\n        load(rso_base + 0x2Cu) != rso_base + 0x{unresolved_file_offset:08X}u) {{\n        return false;\n    }}",
        prolog_file_offset = prolog_file_offset,
        epilog_file_offset = epilog_file_offset,
        unresolved_file_offset = unresolved_file_offset,
    )
    .map_err(|_| RsoNativeSidecarError::Formatting)?;
    source.push_str(
        "    for (std::uint32_t index = 0u; index < kSectionCount; ++index) {\n        const std::uint32_t info = rso_base + 0x58u + index * 8u;\n        const std::uint32_t address = load(info);\n        if (load(info + 4u) != kSectionSizes[index]) {\n            return false;\n        }\n        if (kFileOffsets[index] != kNoFileOffset) {\n            if (address != rso_base + kFileOffsets[index]) {\n                return false;\n            }\n        } else if (kSectionSizes[index] != 0u &&\n                   (address < 0x90000000u || address >= 0x94000000u ||\n                    (address & 3u) != 0u)) {\n            return false;\n        }\n        layout->sections[index] = address;\n    }\n    return layout->sections[kCodeSection] == rso_base + kCodeFileOffset;\n}\n\n",
    );

    source.push_str(
        "bool is_static_rso_call_entry(\n    std::uint32_t guest_pc,\n    const LoadedRsoLayout& layout) {\n    const std::uint32_t code_start = layout.sections[kCodeSection];\n    if (guest_pc < code_start || guest_pc - code_start >= kCodeSize ||\n        (guest_pc & 3u) != 0u) {\n        return false;\n    }\n    const std::uint32_t canonical_pc =\n        kCanonicalCodeAddress + (guest_pc - code_start);\n    switch (canonical_pc) {\n",
    );
    writeln!(source, "    case 0x{canonical_address:08X}u:")
        .map_err(|_| RsoNativeSidecarError::Formatting)?;
    for entry in call_entries {
        writeln!(source, "    case 0x{entry:08X}u:")
            .map_err(|_| RsoNativeSidecarError::Formatting)?;
    }
    source.push_str("        return true;\n    default:\n        return false;\n    }\n}\n\n");
    source.push_str(
        "bool is_static_rso_resume_entry(\n    std::uint32_t guest_pc,\n    const LoadedRsoLayout& layout) {\n    const std::uint32_t code_start = layout.sections[kCodeSection];\n    return guest_pc >= code_start && guest_pc - code_start < kCodeSize &&\n           (guest_pc & 3u) == 0u;\n}\n\n",
    );
    source.push_str(
        "bool trace_static_home_button_rso_enabled() {\n    static const bool enabled = [] {\n        const char* const value = std::getenv(\"GALAXY_TRACE_HOME_BUTTON_RSO_SIDECAR\");\n        return value != nullptr && std::strcmp(value, \"1\") == 0;\n    }();\n    return enabled;\n}\n\nvoid trace_static_home_button_rso_state(\n    const char* phase,\n    const galaxy::PpcContext* context,\n    const galaxy::NativeServicesV1* services,\n    bool returned_to_native_caller) {\n    if (!trace_static_home_button_rso_enabled() || context == nullptr ||\n        services == nullptr || services->log == nullptr) {\n        return;\n    }\n    char message[192]{};\n    std::snprintf(\n        message,\n        sizeof(message),\n        \"HomeButton RSO sidecar %s pc=0x%08X lr=0x%08X r3=0x%08X r12=0x%08X r31=0x%08X return=%u\",\n        phase,\n        context->pc,\n        context->lr,\n        context->gpr[3],\n        context->gpr[12],\n        context->gpr[31],\n        returned_to_native_caller ? 1u : 0u);\n    services->log(services->user, galaxy::LogLevelV1::Warning, message);\n}\n\n",
    );

    source.push_str(
        "void execute_static_rso(\n    galaxy::PpcContext* context,\n    galaxy::GuestMemoryV1* memory,\n    const galaxy::NativeServicesV1* services,\n    const LoadedRsoLayout& layout,\n    bool* resume_rso_return) {\n    *resume_rso_return = false;\n",
    );
    for index in 0..section_count {
        writeln!(
            source,
            "    [[maybe_unused]] const std::uint32_t rso_section_{index} = layout.sections[{index}u];"
        )
        .map_err(|_| RsoNativeSidecarError::Formatting)?;
    }
    writeln!(
        source,
        "    const auto rso_pc = [&](std::uint32_t offset) {{ return rso_section_{code_section} + offset; }};"
    )
    .map_err(|_| RsoNativeSidecarError::Formatting)?;
    source.push_str(body);
    source.push_str("}\n\n");

    source.push_str(
        "galaxy::NativeRsoModuleManifestV1 make_manifest() {\n    galaxy::NativeRsoModuleManifestV1 manifest{};\n    std::memcpy(manifest.game_id, \"RMGE01\", 7);\n",
    );
    writeln!(
        source,
        "    std::memcpy(manifest.main_dol_sha1, \"{RMGE01_DOL_SHA1}\", 41);\n    std::memcpy(manifest.rso_sha1, \"{rso_sha1}\", 41);\n    manifest.section_count = kSectionCount;\n    manifest.code_section = kCodeSection;\n    manifest.code_file_offset = kCodeFileOffset;\n    manifest.code_size = kCodeSize;\n    manifest.entry_count = {}u;\n    manifest.symbolic_immediate_count = {}u;\n    return manifest;\n}}\n\nconst galaxy::NativeRsoModuleManifestV1 kManifest = make_manifest();\n\n}}  // namespace\n\n",
        call_entries.len() + 1,
        symbolic_immediate_count,
    )
    .map_err(|_| RsoNativeSidecarError::Formatting)?;
    source.push_str(
        "static bool run_static_home_button_rso_call(\n    std::uint32_t guest_address,\n    std::uint32_t rso_base,\n    galaxy::PpcContext* context,\n    galaxy::GuestMemoryV1* memory,\n    const galaxy::NativeServicesV1* services) {\n    LoadedRsoLayout layout{};\n    if (!load_rso_layout(rso_base, context, memory, services, &layout)) {\n        return false;\n    }\n    context->pc = guest_address;\n    if (!is_static_rso_call_entry(context->pc, layout)) {\n        galaxy::guest_execution_fault(\n            services, context->pc, \"invalid static Home Button RSO call entry\");\n        return false;\n    }\n    bool returned_to_native_caller = false;\n    execute_static_rso(\n        context, memory, services, layout, &returned_to_native_caller);\n    return true;\n}\n\nstatic bool run_static_home_button_rso_resume(\n    std::uint32_t guest_address,\n    std::uint32_t rso_base,\n    galaxy::PpcContext* context,\n    galaxy::GuestMemoryV1* memory,\n    const galaxy::NativeServicesV1* services) {\n    LoadedRsoLayout layout{};\n    if (!load_rso_layout(rso_base, context, memory, services, &layout)) {\n        return false;\n    }\n    context->pc = guest_address;\n    for (;;) {\n        if (!is_static_rso_resume_entry(context->pc, layout)) {\n            galaxy::guest_execution_fault(\n                services, context->pc, \"invalid static Home Button RSO resume entry\");\n            return false;\n        }\n        bool resume_rso_return = false;\n        execute_static_rso(\n            context, memory, services, layout, &resume_rso_return);\n        if (!resume_rso_return) {\n            return true;\n        }\n        const std::uint32_t code_start = layout.sections[kCodeSection];\n        if (context->pc < code_start ||\n            context->pc - code_start >= kCodeSize) {\n            return true;\n        }\n    }\n}\n\nextern \"C\" GALAXY_MODULE_EXPORT const galaxy::NativeRsoModuleManifestV1* galaxy_home_button_rso_manifest() {\n    return &kManifest;\n}\n\nextern \"C\" GALAXY_MODULE_EXPORT bool galaxy_home_button_rso_try_call(\n    std::uint32_t guest_address,\n    std::uint32_t rso_base,\n    galaxy::PpcContext* context,\n    galaxy::GuestMemoryV1* memory,\n    const galaxy::NativeServicesV1* services) {\n    return run_static_home_button_rso_call(\n        guest_address, rso_base, context, memory, services);\n}\n\nextern \"C\" GALAXY_MODULE_EXPORT bool galaxy_home_button_rso_resume(\n    std::uint32_t guest_address,\n    std::uint32_t rso_base,\n    galaxy::PpcContext* context,\n    galaxy::GuestMemoryV1* memory,\n    const galaxy::NativeServicesV1* services) {\n    return run_static_home_button_rso_resume(\n        guest_address, rso_base, context, memory, services);\n}\n",
    );
    let trace_include_marker = "#include <array>\n";
    if !source.contains(trace_include_marker) {
        return Err(RsoNativeSidecarError::Formatting);
    }
    source = source.replacen(
        trace_include_marker,
        "#include <array>\n#include <atomic>\n",
        1,
    );

    let trace_output_marker = r#"    char message[192]{};
"#;
    let trace_output_replacement = r#"    static std::atomic<std::uint32_t> trace_count{0u};
    if (trace_count.fetch_add(1u, std::memory_order_relaxed) >= 32u) {
        return;
    }
    char message[192]{};
"#;
    if !source.contains(trace_output_marker) {
        return Err(RsoNativeSidecarError::Formatting);
    }
    source = source.replacen(trace_output_marker, trace_output_replacement, 1);

    let trace_environment_marker = r#"        const char* const value = std::getenv("GALAXY_TRACE_HOME_BUTTON_RSO_SIDECAR");
        return value != nullptr && std::strcmp(value, "1") == 0;
"#;
    let trace_environment_replacement = r#"        char* value = nullptr;
        std::size_t value_size = 0u;
        if (_dupenv_s(
                &value, &value_size, "GALAXY_TRACE_HOME_BUTTON_RSO_SIDECAR") != 0 ||
            value == nullptr) {
            return false;
        }
        const bool result = std::strcmp(value, "1") == 0;
        std::free(value);
        return result;
"#;
    if !source.contains(trace_environment_marker) {
        return Err(RsoNativeSidecarError::Formatting);
    }
    source = source.replacen(trace_environment_marker, trace_environment_replacement, 1);

    let call_trace_marker = r#"    bool returned_to_native_caller = false;
    execute_static_rso(
        context, memory, services, layout, &returned_to_native_caller);
    return true;
"#;
    let call_trace_replacement = r#"    trace_static_home_button_rso_state("call-enter", context, services, false);
    const std::uint32_t native_caller_return = context->lr & 0xFFFFFFFCu;
    for (;;) {
        bool returned_to_native_caller = false;
        execute_static_rso(
            context, memory, services, layout, &returned_to_native_caller);
        if (!returned_to_native_caller ||
            context->pc == native_caller_return) {
            trace_static_home_button_rso_state(
                "call-exit", context, services, returned_to_native_caller);
            return true;
        }
        const std::uint32_t code_start = layout.sections[kCodeSection];
        if (context->pc < code_start ||
            context->pc - code_start >= kCodeSize ||
            !is_static_rso_resume_entry(context->pc, layout)) {
            galaxy::guest_execution_fault(
                services,
                context->pc,
                "invalid static Home Button RSO local return");
            return false;
        }
    }
"#;
    if !source.contains(call_trace_marker) {
        return Err(RsoNativeSidecarError::Formatting);
    }
    source = source.replacen(call_trace_marker, call_trace_replacement, 1);

    let resume_wrapper_marker = r#"    context->pc = guest_address;
    for (;;) {
        if (!is_static_rso_resume_entry(context->pc, layout)) {
            galaxy::guest_execution_fault(
                services, context->pc, "invalid static Home Button RSO resume entry");
            return false;
        }
        bool resume_rso_return = false;
        execute_static_rso(
            context, memory, services, layout, &resume_rso_return);
        if (!resume_rso_return) {
            return true;
        }
        const std::uint32_t code_start = layout.sections[kCodeSection];
        if (context->pc < code_start ||
            context->pc - code_start >= kCodeSize) {
            return true;
        }
    }
"#;
    let resume_wrapper_replacement = r#"    context->pc = guest_address;
    if (!is_static_rso_resume_entry(context->pc, layout)) {
        galaxy::guest_execution_fault(
            services, context->pc, "invalid static Home Button RSO resume entry");
        return false;
    }
    bool returned_to_flat_dispatch = false;
    execute_static_rso(
        context, memory, services, layout, &returned_to_flat_dispatch);
    return true;
"#;
    if !source.contains(resume_wrapper_marker) {
        return Err(RsoNativeSidecarError::Formatting);
    }
    source = source.replacen(resume_wrapper_marker, resume_wrapper_replacement, 1);
    Ok(source)
}

fn native_sidecar_cmake() -> String {
    r##"cmake_minimum_required(VERSION 3.24)
project(RMGE01HomeButtonNative LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "RMGE01 native sidecar requires Windows x64")
endif()
if(NOT DEFINED GALAXY_RUNTIME_INCLUDE OR
   NOT DEFINED GALAXY_PPC_FLOAT_LIBRARY OR
   NOT DEFINED GALAXY_SOFTFLOAT_LIBRARY)
    message(FATAL_ERROR "Set the Nebula runtime include and float libraries")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(GALAXY_NATIVE_ISA "AUTO" CACHE STRING "Native CPU target: AUTO, SSE2, or AVX2")
set_property(CACHE GALAXY_NATIVE_ISA PROPERTY STRINGS AUTO SSE2 AVX2)
if(MSVC)
    string(TOUPPER "${GALAXY_NATIVE_ISA}" GALAXY_NATIVE_ISA)
    if(NOT GALAXY_NATIVE_ISA MATCHES "^(AUTO|SSE2|AVX2)$")
        message(FATAL_ERROR "GALAXY_NATIVE_ISA must be AUTO, SSE2, or AVX2")
    endif()
    set(galaxy_native_isa_effective "${GALAXY_NATIVE_ISA}")
    if(GALAXY_NATIVE_ISA STREQUAL "AUTO")
        if(CMAKE_CROSSCOMPILING)
            set(galaxy_native_isa_effective "SSE2")
        else()
            include(CheckCXXSourceRuns)
            check_cxx_source_runs(
                "#include <intrin.h>
                int main() {
                    int cpu[4] = {};
                    __cpuid(cpu, 0);
                    if (cpu[0] < 7) return 1;
                    __cpuidex(cpu, 1, 0);
                    constexpr int required_ecx = (1 << 12) | (1 << 27) | (1 << 28);
                    if ((cpu[2] & required_ecx) != required_ecx) return 2;
                    if ((_xgetbv(0) & 0x6u) != 0x6u) return 3;
                    __cpuidex(cpu, 7, 0);
                    return (cpu[1] & (1 << 5)) != 0 ? 0 : 4;
                }"
                GALAXY_HOST_CAN_RUN_AVX2_FMA
            )
            if(GALAXY_HOST_CAN_RUN_AVX2_FMA)
                set(galaxy_native_isa_effective "AVX2")
            else()
                set(galaxy_native_isa_effective "SSE2")
            endif()
        endif()
    endif()
    message(STATUS "Galaxy Home Button sidecar CPU target: ${galaxy_native_isa_effective} (requested ${GALAXY_NATIVE_ISA})")
endif()

add_library(RMGE01_home_button SHARED home_button_native.cpp)
target_include_directories(RMGE01_home_button PRIVATE "${GALAXY_RUNTIME_INCLUDE}")
target_link_libraries(RMGE01_home_button PRIVATE
    "${GALAXY_PPC_FLOAT_LIBRARY}"
    "${GALAXY_SOFTFLOAT_LIBRARY}"
)
target_compile_definitions(RMGE01_home_button PRIVATE GALAXY_BUILDING_GAME_MODULE=1)
set_target_properties(RMGE01_home_button PROPERTIES PREFIX "")
if(MSVC)
    # AUTO keeps old x64 hosts on SSE2 and selects AVX2/FMA only when safe.
    # /fp:precise remains mandatory; never add /fp:fast.
    target_compile_options(RMGE01_home_button PRIVATE /W4 /WX /wd4702 /permissive- /EHsc /GS- /GR- /Oi /bigobj /favor:INTEL64)
    if(galaxy_native_isa_effective STREQUAL "AVX2")
        target_compile_options(RMGE01_home_button PRIVATE /arch:AVX2)
    endif()
endif()
"##
    .to_owned()
}

fn apply_relocation(
    linked: &mut [u8],
    image: &RsoImage,
    table: &'static str,
    index: u32,
    relocation: &RsoRelocation,
    symbol_address: u32,
    layout: &RsoLinkLayout,
) -> Result<(), RsoRelocationError> {
    let patch_offset = usize::try_from(relocation.file_offset).expect("u32 fits usize");
    let patch_address = image
        .sections
        .iter()
        .find_map(|section| {
            let range = section.file_range()?;
            let patch = u64::from(relocation.file_offset);
            (range.start <= patch && patch < range.end).then(|| {
                let relative = patch - range.start;
                layout.section_addresses[usize::try_from(section.index).expect("index fits")]
                    .checked_add(u32::try_from(relative).expect("section offset fits u32"))
            })
        })
        .flatten()
        .ok_or(RsoRelocationError::PatchAddressUnavailable { table, index })?;

    match relocation.relocation_type {
        RsoRelocationType::None => Ok(()),
        RsoRelocationType::Addr32 => {
            let addend = read_u32_at(linked, patch_offset);
            let value = symbol_address
                .checked_add(addend)
                .ok_or(RsoRelocationError::TargetAddressOverflow { table, index })?;
            write_u32_at(linked, patch_offset, value);
            Ok(())
        }
        RsoRelocationType::Addr16 | RsoRelocationType::Addr16Lo => {
            let addend = u32::from(read_u16_at(linked, patch_offset));
            let value = symbol_address
                .checked_add(addend)
                .ok_or(RsoRelocationError::TargetAddressOverflow { table, index })?;
            write_u16_at(linked, patch_offset, value as u16);
            Ok(())
        }
        RsoRelocationType::Addr16Hi | RsoRelocationType::Addr16Ha => {
            let addend = u32::from(read_u16_at(linked, patch_offset));
            let value = symbol_address
                .checked_add(addend)
                .ok_or(RsoRelocationError::TargetAddressOverflow { table, index })?;
            let high = if relocation.relocation_type == RsoRelocationType::Addr16Ha {
                value.wrapping_add(0x8000u32) >> 16
            } else {
                value >> 16
            };
            write_u16_at(linked, patch_offset, high as u16);
            Ok(())
        }
        RsoRelocationType::Rel24 => {
            let instruction = read_u32_at(linked, patch_offset);
            if instruction & 0x2u32 != 0 {
                return Err(RsoRelocationError::Rel24AbsoluteBranch { table, index });
            }
            let addend = i64::from(((instruction & 0x03FF_FFFCu32) as i32) << 6 >> 6);
            let displacement = i64::from(symbol_address) + addend - i64::from(patch_address);
            if displacement % 4 != 0 {
                return Err(RsoRelocationError::Rel24Misaligned {
                    table,
                    index,
                    displacement,
                });
            }
            if !(-0x0200_0000..=0x01FF_FFFC).contains(&displacement) {
                return Err(RsoRelocationError::Rel24OutOfRange {
                    table,
                    index,
                    displacement,
                });
            }
            let value =
                (instruction & 0xFC00_0003u32) | ((displacement as i32 as u32) & 0x03FF_FFFCu32);
            write_u32_at(linked, patch_offset, value);
            Ok(())
        }
        relocation => Err(RsoRelocationError::UnsupportedRelocation {
            table,
            index,
            relocation,
        }),
    }
}

fn read_u16_at(data: &[u8], offset: usize) -> u16 {
    u16::from_be_bytes(
        data[offset..offset + 2]
            .try_into()
            .expect("validated patch range"),
    )
}

fn write_u16_at(data: &mut [u8], offset: usize, value: u16) {
    data[offset..offset + 2].copy_from_slice(&value.to_be_bytes());
}

fn read_u32_at(data: &[u8], offset: usize) -> u32 {
    u32::from_be_bytes(
        data[offset..offset + 4]
            .try_into()
            .expect("validated patch range"),
    )
}

fn write_u32_at(data: &mut [u8], offset: usize, value: u32) {
    data[offset..offset + 4].copy_from_slice(&value.to_be_bytes());
}

pub fn parse_rso(data: &[u8]) -> Result<RsoImage, RsoError> {
    if data.len() < RSO_HEADER_SIZE {
        return Err(RsoError::TruncatedHeader { actual: data.len() });
    }

    let section_count = usize::try_from(read_u32(data, 0x08)).expect("u32 fits usize");
    if section_count == 0 {
        return Err(RsoError::MissingSections);
    }
    let section_info_offset = read_u32(data, 0x0C);
    if usize::try_from(section_info_offset).expect("u32 fits usize") != RSO_HEADER_SIZE {
        return Err(RsoError::InvalidSectionInfoOffset {
            actual: section_info_offset,
        });
    }

    let module_name_offset = read_u32(data, 0x10);
    let module_name_size = read_u32(data, 0x14);
    let version = read_u32(data, 0x18);
    let bss_size = read_u32(data, 0x1C);
    let prolog_section = data[0x20];
    let epilog_section = data[0x21];
    let unresolved_section = data[0x22];
    let prolog_offset = read_u32(data, 0x24);
    let epilog_offset = read_u32(data, 0x28);
    let unresolved_offset = read_u32(data, 0x2C);
    let internal_relocation_offset = read_u32(data, 0x30);
    let internal_relocation_size = read_u32(data, 0x34);
    let external_relocation_offset = read_u32(data, 0x38);
    let external_relocation_size = read_u32(data, 0x3C);
    let export_offset = read_u32(data, 0x40);
    let export_size = read_u32(data, 0x44);
    let export_name_offset = read_u32(data, 0x48);
    let import_offset = read_u32(data, 0x4C);
    let import_size = read_u32(data, 0x50);
    let import_name_offset = read_u32(data, 0x54);

    let section_table_size = section_count
        .checked_mul(SECTION_INFO_SIZE)
        .and_then(|size| u32::try_from(size).ok())
        .ok_or(RsoError::SectionCountOutOfRange {
            count: u32::try_from(section_count).expect("original u32 count"),
            file_size: data.len(),
        })?;
    let sections_raw = checked_table(
        data,
        "section-info",
        section_info_offset,
        section_table_size,
        SECTION_INFO_SIZE,
    )?;
    let mut sections = Vec::with_capacity(section_count);
    for index in 0..section_count {
        let offset = read_u32(sections_raw, index * SECTION_INFO_SIZE);
        let size = read_u32(sections_raw, index * SECTION_INFO_SIZE + 4);
        if offset == 0 && size == 0 {
            sections.push(RsoSection {
                index: u32::try_from(index).expect("section index fits u32"),
                file_offset: None,
                size,
            });
            continue;
        }
        if offset == 0 {
            sections.push(RsoSection {
                index: u32::try_from(index).expect("section index fits u32"),
                file_offset: None,
                size,
            });
            continue;
        }
        checked_table(data, "section", offset, size, 1).map_err(|_| {
            RsoError::SectionOutOfRange {
                index: u32::try_from(index).expect("section index fits u32"),
                offset,
                size,
                file_size: data.len(),
            }
        })?;
        sections.push(RsoSection {
            index: u32::try_from(index).expect("section index fits u32"),
            file_offset: Some(offset),
            size,
        });
    }
    validate_non_overlapping_sections(&sections)?;
    let observed_bss_size = sections
        .iter()
        .filter(|section| section.file_offset.is_none())
        .try_fold(0_u32, |total, section| total.checked_add(section.size))
        .ok_or(RsoError::BssSizeMismatch {
            declared: bss_size,
            observed: u32::MAX,
        })?;
    if observed_bss_size != bss_size {
        return Err(RsoError::BssSizeMismatch {
            declared: bss_size,
            observed: observed_bss_size,
        });
    }

    let module_name = if module_name_offset == 0 {
        None
    } else {
        let module_name_offset = checked_offset(data, "module-name", module_name_offset)?;
        Some(read_sized_string(
            data,
            "module-name",
            module_name_offset,
            usize::try_from(module_name_size).expect("u32 fits usize"),
        )?)
    };
    let exports_raw = checked_table(
        data,
        "export-table",
        export_offset,
        export_size,
        EXPORT_SIZE,
    )?;
    let imports_raw = checked_table(
        data,
        "import-table",
        import_offset,
        import_size,
        IMPORT_SIZE,
    )?;
    let import_name_base = checked_offset(data, "import-name-table", import_name_offset)?;
    let export_name_base = checked_offset(data, "export-name-table", export_name_offset)?;

    let mut imports = Vec::with_capacity(imports_raw.len() / IMPORT_SIZE);
    for (index, entry) in imports_raw.chunks_exact(IMPORT_SIZE).enumerate() {
        let name_relative = read_u32(entry, 0);
        let name_offset = checked_add_offset(data, import_name_base, name_relative, "import-name")?;
        imports.push(RsoImport {
            name: read_string(data, "import-name", name_offset, data.len() - name_offset)?,
            symbol_offset: read_u32(entry, 4),
            relocation_offset: read_u32(entry, 8),
        });
        let _ = index;
    }

    let mut exports = Vec::with_capacity(exports_raw.len() / EXPORT_SIZE);
    for (index, entry) in exports_raw.chunks_exact(EXPORT_SIZE).enumerate() {
        let name_relative = read_u32(entry, 0);
        let name_offset = checked_add_offset(data, export_name_base, name_relative, "export-name")?;
        let section_index = read_u32(entry, 8);
        let section = sections
            .get(usize::try_from(section_index).unwrap_or(usize::MAX))
            .ok_or(RsoError::InvalidExportSection {
                index: u32::try_from(index).expect("export index fits u32"),
                section: section_index,
                section_count: sections.len(),
            })?;
        let section_offset = read_u32(entry, 4);
        if section_offset > section.size {
            return Err(RsoError::InvalidExportOffset {
                index: u32::try_from(index).expect("export index fits u32"),
                section: section_index,
                offset: section_offset,
                section_size: section.size,
            });
        }
        exports.push(RsoExport {
            name: read_string(data, "export-name", name_offset, data.len() - name_offset)?,
            section_index,
            section_offset,
            elf_hash: read_u32(entry, 12),
        });
    }

    let internal_relocations = parse_relocations(
        data,
        "internal",
        internal_relocation_offset,
        internal_relocation_size,
    )?;
    let external_relocations = parse_relocations(
        data,
        "external",
        external_relocation_offset,
        external_relocation_size,
    )?;
    validate_relocations(
        &sections,
        &internal_relocations,
        &external_relocations,
        &imports,
        external_relocation_size,
    )?;

    Ok(RsoImage {
        module_name,
        version,
        bss_size,
        prolog_section,
        epilog_section,
        unresolved_section,
        prolog_offset,
        epilog_offset,
        unresolved_offset,
        sections,
        internal_relocations,
        external_relocations,
        exports,
        imports,
    })
}

fn checked_table<'a>(
    data: &'a [u8],
    table: &'static str,
    offset: u32,
    size: u32,
    entry_size: usize,
) -> Result<&'a [u8], RsoError> {
    if usize::try_from(size).expect("u32 fits usize") % entry_size != 0 {
        return Err(RsoError::MisalignedTableSize {
            table,
            size,
            entry_size,
        });
    }
    let start = usize::try_from(offset).expect("u32 fits usize");
    let size = usize::try_from(size).expect("u32 fits usize");
    let Some(end) = start.checked_add(size) else {
        return Err(RsoError::TableOutOfRange {
            table,
            offset,
            size: u32::try_from(size).expect("original u32 size"),
            file_size: data.len(),
        });
    };
    if end > data.len() {
        return Err(RsoError::TableOutOfRange {
            table,
            offset,
            size: u32::try_from(size).expect("original u32 size"),
            file_size: data.len(),
        });
    }
    Ok(&data[start..end])
}

fn checked_offset(data: &[u8], field: &'static str, offset: u32) -> Result<usize, RsoError> {
    let offset = usize::try_from(offset).expect("u32 fits usize");
    if offset >= data.len() {
        return Err(RsoError::StringOutOfRange {
            field,
            offset: u32::try_from(offset).expect("original u32 offset"),
            file_size: data.len(),
        });
    }
    Ok(offset)
}

fn checked_add_offset(
    data: &[u8],
    base: usize,
    relative: u32,
    field: &'static str,
) -> Result<usize, RsoError> {
    let Some(offset) = base.checked_add(usize::try_from(relative).expect("u32 fits usize")) else {
        return Err(RsoError::StringOutOfRange {
            field,
            offset: relative,
            file_size: data.len(),
        });
    };
    if offset >= data.len() {
        return Err(RsoError::StringOutOfRange {
            field,
            offset: u32::try_from(offset).unwrap_or(u32::MAX),
            file_size: data.len(),
        });
    }
    Ok(offset)
}

fn read_string(
    data: &[u8],
    field: &'static str,
    offset: usize,
    maximum: usize,
) -> Result<String, RsoError> {
    if offset >= data.len() {
        return Err(RsoError::StringOutOfRange {
            field,
            offset: u32::try_from(offset).unwrap_or(u32::MAX),
            file_size: data.len(),
        });
    }
    let maximum = maximum.min(data.len() - offset);
    let bytes = &data[offset..offset + maximum];
    let Some(nul) = bytes.iter().position(|byte| *byte == 0) else {
        return Err(RsoError::UnterminatedString {
            field,
            offset: u32::try_from(offset).unwrap_or(u32::MAX),
        });
    };
    std::str::from_utf8(&bytes[..nul])
        .map(ToOwned::to_owned)
        .map_err(|_| RsoError::InvalidUtf8String {
            field,
            offset: u32::try_from(offset).unwrap_or(u32::MAX),
        })
}

fn read_sized_string(
    data: &[u8],
    field: &'static str,
    offset: usize,
    size: usize,
) -> Result<String, RsoError> {
    let end = offset.checked_add(size).ok_or(RsoError::StringOutOfRange {
        field,
        offset: u32::try_from(offset).unwrap_or(u32::MAX),
        file_size: data.len(),
    })?;
    if end > data.len() {
        return Err(RsoError::StringOutOfRange {
            field,
            offset: u32::try_from(offset).unwrap_or(u32::MAX),
            file_size: data.len(),
        });
    }
    std::str::from_utf8(&data[offset..end])
        .map(ToOwned::to_owned)
        .map_err(|_| RsoError::InvalidUtf8String {
            field,
            offset: u32::try_from(offset).unwrap_or(u32::MAX),
        })
}

fn parse_relocations(
    data: &[u8],
    table: &'static str,
    offset: u32,
    size: u32,
) -> Result<Vec<RsoRelocation>, RsoError> {
    let entries = checked_table(data, table, offset, size, RELOCATION_SIZE)?;
    entries
        .chunks_exact(RELOCATION_SIZE)
        .map(|entry| {
            let symbol_index =
                (u32::from(entry[4]) << 16) | (u32::from(entry[5]) << 8) | u32::from(entry[6]);
            Ok(RsoRelocation {
                file_offset: read_u32(entry, 0),
                symbol_index,
                relocation_type: RsoRelocationType::try_from(entry[7])?,
                symbol_offset: read_u32(entry, 8),
            })
        })
        .collect()
}

fn validate_relocations(
    sections: &[RsoSection],
    internal: &[RsoRelocation],
    external: &[RsoRelocation],
    imports: &[RsoImport],
    external_relocation_size: u32,
) -> Result<(), RsoError> {
    for (index, relocation) in internal.iter().enumerate() {
        validate_relocation_patch_site(sections, "internal", index, relocation)?;
        let section = sections
            .get(usize::try_from(relocation.symbol_index).unwrap_or(usize::MAX))
            .ok_or(RsoError::InvalidInternalRelocationSection {
                index: u32::try_from(index).expect("relocation index fits u32"),
                section: relocation.symbol_index,
                section_count: sections.len(),
            })?;
        if relocation.symbol_offset > section.size {
            return Err(RsoError::InvalidInternalRelocationOffset {
                index: u32::try_from(index).expect("relocation index fits u32"),
                section: relocation.symbol_index,
                offset: relocation.symbol_offset,
                section_size: section.size,
            });
        }
    }
    for (index, relocation) in external.iter().enumerate() {
        validate_relocation_patch_site(sections, "external", index, relocation)?;
        if usize::try_from(relocation.symbol_index).unwrap_or(usize::MAX) >= imports.len() {
            return Err(RsoError::InvalidExternalRelocationImport {
                index: u32::try_from(index).expect("relocation index fits u32"),
                import: relocation.symbol_index,
                import_count: imports.len(),
            });
        }
        if relocation.symbol_offset != 0 {
            return Err(RsoError::InvalidExternalRelocationOffset {
                index: u32::try_from(index).expect("relocation index fits u32"),
                offset: relocation.symbol_offset,
            });
        }
    }
    validate_import_relocation_ranges(imports, external, external_relocation_size)?;
    Ok(())
}

fn validate_import_relocation_ranges(
    imports: &[RsoImport],
    external: &[RsoRelocation],
    external_relocation_size: u32,
) -> Result<(), RsoError> {
    let relocation_size = u32::try_from(RELOCATION_SIZE).expect("size fits u32");
    for (index, import) in imports.iter().enumerate() {
        if import.relocation_offset % relocation_size != 0
            || import.relocation_offset > external_relocation_size
        {
            return Err(RsoError::InvalidImportRelocationOffset {
                index: u32::try_from(index).expect("import index fits u32"),
                offset: import.relocation_offset,
                external_size: external_relocation_size,
            });
        }
        if index != 0 && import.relocation_offset < imports[index - 1].relocation_offset {
            return Err(RsoError::InvalidImportRelocationOffset {
                index: u32::try_from(index).expect("import index fits u32"),
                offset: import.relocation_offset,
                external_size: external_relocation_size,
            });
        }
    }
    if let Some(first) = imports.first() {
        if first.relocation_offset != 0 {
            return Err(RsoError::InvalidImportRelocationOffset {
                index: 0,
                offset: first.relocation_offset,
                external_size: external_relocation_size,
            });
        }
    }
    for (index, import) in imports.iter().enumerate() {
        let range_end = imports
            .get(index + 1)
            .map_or(external_relocation_size, |next| next.relocation_offset);
        let first =
            usize::try_from(import.relocation_offset).expect("u32 fits usize") / RELOCATION_SIZE;
        let end = usize::try_from(range_end).expect("u32 fits usize") / RELOCATION_SIZE;
        for (relative_index, relocation) in external[first..end].iter().enumerate() {
            if relocation.symbol_index != u32::try_from(index).expect("import index fits u32") {
                return Err(RsoError::ImportRelocationOwnershipMismatch {
                    index: u32::try_from(index).expect("import index fits u32"),
                    relocation_index: u32::try_from(first + relative_index)
                        .expect("relocation index fits u32"),
                    actual_import: relocation.symbol_index,
                });
            }
        }
    }
    Ok(())
}

fn validate_relocation_patch_site(
    sections: &[RsoSection],
    table: &'static str,
    index: usize,
    relocation: &RsoRelocation,
) -> Result<(), RsoError> {
    let offset = u64::from(relocation.file_offset);
    let width = match relocation.relocation_type {
        RsoRelocationType::Addr16
        | RsoRelocationType::Addr16Lo
        | RsoRelocationType::Addr16Hi
        | RsoRelocationType::Addr16Ha => 2,
        // Branch/address words and unsupported kinds retain their full-word
        // validation. Admission never assumes an unknown relocation is narrow.
        _ => 4,
    };
    let valid = sections.iter().any(|section| {
        section.file_range().is_some_and(|range| {
            range.start <= offset
                && offset
                    .checked_add(width)
                    .is_some_and(|end| end <= range.end)
        })
    });
    if !valid {
        return Err(RsoError::RelocationOffsetOutsideSection {
            table,
            index: u32::try_from(index).expect("relocation index fits u32"),
            offset: u32::try_from(offset).expect("original u32 offset"),
        });
    }
    Ok(())
}

fn validate_non_overlapping_sections(sections: &[RsoSection]) -> Result<(), RsoError> {
    let mut ranges = BTreeSet::new();
    for section in sections {
        let Some(range) = section.file_range() else {
            continue;
        };
        for (other_start, other_end, other_index) in &ranges {
            if range.start < *other_end && *other_start < range.end {
                return Err(RsoError::OverlappingSections {
                    first: *other_index,
                    second: section.index,
                });
            }
        }
        ranges.insert((range.start, range.end, section.index));
    }
    Ok(())
}

fn read_u32(data: &[u8], offset: usize) -> u32 {
    u32::from_be_bytes(
        data[offset..offset + 4]
            .try_into()
            .expect("caller checked RSO range"),
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn native_sidecar_cmake_selects_a_supported_target_isa() {
        let cmake = native_sidecar_cmake();
        assert!(cmake.contains(
            "set(GALAXY_NATIVE_ISA \"AUTO\" CACHE STRING \"Native CPU target: AUTO, SSE2, or AVX2\")"
        ));
        assert!(cmake.contains("GALAXY_HOST_CAN_RUN_AVX2_FMA"));
        assert!(cmake.contains(
            "message(STATUS \"Galaxy Home Button sidecar CPU target: ${galaxy_native_isa_effective} (requested ${GALAXY_NATIVE_ISA})\")"
        ));
        assert!(cmake.contains("if(galaxy_native_isa_effective STREQUAL \"AVX2\")"));
        assert!(cmake.contains("target_compile_options(RMGE01_home_button PRIVATE /arch:AVX2)"));
        assert!(!cmake.contains("/bigobj /arch:AVX2 /favor:INTEL64"));
    }

    fn write_u32(data: &mut [u8], offset: usize, value: u32) {
        data[offset..offset + 4].copy_from_slice(&value.to_be_bytes());
    }

    fn synthetic_rso() -> Vec<u8> {
        let mut data = vec![0_u8; 0x220];
        write_u32(&mut data, 0x08, 2);
        write_u32(&mut data, 0x0C, 0x58);
        write_u32(&mut data, 0x10, 0x80);
        write_u32(&mut data, 0x14, 8);
        write_u32(&mut data, 0x18, 1);
        write_u32(&mut data, 0x1C, 0);
        data[0x20] = 1;
        write_u32(&mut data, 0x24, 0);
        write_u32(&mut data, 0x30, 0x140);
        write_u32(&mut data, 0x34, 12);
        write_u32(&mut data, 0x38, 0x14C);
        write_u32(&mut data, 0x3C, 12);
        write_u32(&mut data, 0x40, 0x160);
        write_u32(&mut data, 0x44, 16);
        write_u32(&mut data, 0x48, 0x180);
        write_u32(&mut data, 0x4C, 0x190);
        write_u32(&mut data, 0x50, 12);
        write_u32(&mut data, 0x54, 0x1A0);
        write_u32(&mut data, 0x58, 0);
        write_u32(&mut data, 0x5C, 0);
        write_u32(&mut data, 0x60, 0x100);
        write_u32(&mut data, 0x64, 0x40);
        data[0x80..0x88].copy_from_slice(b"test.rso");
        // Internal relocation: patch 0x104 to section 1 + 0x10.
        write_u32(&mut data, 0x140, 0x104);
        data[0x144..0x147].copy_from_slice(&[0, 0, 1]);
        data[0x147] = 1;
        write_u32(&mut data, 0x148, 0x10);
        // External relocation: patch 0x108 to import zero.
        write_u32(&mut data, 0x108, 0x4800_0000);
        write_u32(&mut data, 0x14C, 0x108);
        data[0x150..0x153].copy_from_slice(&[0, 0, 0]);
        data[0x153] = 10;
        write_u32(&mut data, 0x154, 0);
        // Export and import names have relative offset zero.
        write_u32(&mut data, 0x160, 0);
        write_u32(&mut data, 0x164, 0);
        write_u32(&mut data, 0x168, 1);
        write_u32(&mut data, 0x16C, 0x1234_5678);
        data[0x180..0x184].copy_from_slice(b"init");
        data[0x184] = 0;
        write_u32(&mut data, 0x190, 0);
        write_u32(&mut data, 0x194, 0);
        write_u32(&mut data, 0x198, 0);
        data[0x1A0..0x1A4].copy_from_slice(b"host");
        data[0x1A4] = 0;
        data
    }

    #[test]
    fn parses_valid_relocatable_module() {
        let image = parse_rso(&synthetic_rso()).expect("synthetic RSO parses");
        assert_eq!(image.module_name.as_deref(), Some("test.rso"));
        assert_eq!(image.sections.len(), 2);
        assert_eq!(image.exports[0].name, "init");
        assert_eq!(image.imports[0].name, "host");
        assert_eq!(
            image.internal_relocations[0].relocation_type,
            RsoRelocationType::Addr32
        );
        assert_eq!(
            image.external_relocations[0].relocation_type,
            RsoRelocationType::Rel24
        );
    }

    #[test]
    fn rejects_section_count_that_cannot_fit_the_header_table() {
        let mut input = synthetic_rso();
        write_u32(&mut input, 0x08, u32::MAX);
        assert_eq!(
            parse_rso(&input),
            Err(RsoError::SectionCountOutOfRange {
                count: u32::MAX,
                file_size: input.len(),
            })
        );
    }

    #[test]
    fn sidecar_reports_invalid_special_sections_instead_of_panicking() {
        let input = synthetic_rso();
        let mut image = parse_rso(&input).expect("synthetic RSO parses");
        let render = |image: &RsoImage| {
            render_native_sidecar_cpp(
                image,
                NativeSidecarRenderParams {
                    code_section: 1,
                    code_file_offset: 0x100,
                    canonical_address: 0x8000_0100,
                    call_entries: &BTreeSet::new(),
                    symbolic_immediate_count: 0,
                    body: "",
                    rso_sha1: "0000000000000000000000000000000000000000",
                },
            )
        };
        assert!(matches!(
            render(&image),
            Err(RsoNativeSidecarError::InvalidSpecialSection {
                field: "epilog",
                section: 0,
            })
        ));
        image.epilog_section = 1;
        image.epilog_offset = image.sections[1].size;
        assert!(matches!(
            render(&image),
            Err(RsoNativeSidecarError::InvalidSpecialSection {
                field: "epilog",
                section: 1,
            })
        ));
    }

    #[test]
    fn rejects_unknown_relocation_type() {
        let mut data = synthetic_rso();
        data[0x147] = 0xFF;
        assert_eq!(
            parse_rso(&data),
            Err(RsoError::UnsupportedRelocationType { actual: 0xFF })
        );
    }

    #[test]
    fn rejects_relocation_outside_file_backed_sections() {
        let mut data = synthetic_rso();
        write_u32(&mut data, 0x140, 0x80);
        assert!(matches!(
            parse_rso(&data),
            Err(RsoError::RelocationOffsetOutsideSection {
                table: "internal",
                ..
            })
        ));
    }

    #[test]
    fn rejects_overlapping_file_backed_sections() {
        let mut data = synthetic_rso();
        write_u32(&mut data, 0x60, 0x100);
        write_u32(&mut data, 0x64, 0x40);
        write_u32(&mut data, 0x58, 0x100);
        write_u32(&mut data, 0x5C, 0x10);
        assert!(matches!(
            parse_rso(&data),
            Err(RsoError::OverlappingSections { .. })
        ));
    }

    #[test]
    fn rejects_import_relocation_ranges_that_do_not_start_at_the_table_head() {
        let imports = vec![RsoImport {
            name: "host".to_owned(),
            symbol_offset: 0,
            relocation_offset: 12,
        }];
        assert_eq!(
            validate_import_relocation_ranges(&imports, &[], 12),
            Err(RsoError::InvalidImportRelocationOffset {
                index: 0,
                offset: 12,
                external_size: 12,
            })
        );
    }

    #[test]
    fn rejects_external_relocation_that_crosses_an_import_range() {
        let imports = vec![
            RsoImport {
                name: "first".to_owned(),
                symbol_offset: 0,
                relocation_offset: 0,
            },
            RsoImport {
                name: "second".to_owned(),
                symbol_offset: 0,
                relocation_offset: 12,
            },
        ];
        let relocation = RsoRelocation {
            file_offset: 0x100,
            symbol_index: 1,
            relocation_type: RsoRelocationType::Rel24,
            symbol_offset: 0,
        };
        assert_eq!(
            validate_import_relocation_ranges(&imports, &[relocation.clone(), relocation], 24),
            Err(RsoError::ImportRelocationOwnershipMismatch {
                index: 0,
                relocation_index: 0,
                actual_import: 1,
            })
        );
    }

    #[test]
    fn binds_each_import_to_one_sel_symbol() {
        let image = parse_rso(&synthetic_rso()).expect("synthetic RSO parses");
        let symbols = vec![ResolvedSelSymbol {
            name: "host".to_owned(),
            guest_address: 0x804A_3F24,
            section_index: 2,
            elf_hash: 0x1234_5678,
        }];
        assert_eq!(
            image
                .bind_imports(&symbols)
                .expect("single matching symbol binds"),
            vec![RsoImportBinding {
                import_index: 0,
                name: "host".to_owned(),
                guest_address: 0x804A_3F24,
            }]
        );
    }

    #[test]
    fn rejects_missing_or_ambiguous_import_bindings() {
        let image = parse_rso(&synthetic_rso()).expect("synthetic RSO parses");
        assert_eq!(
            image.bind_imports(&[]),
            Err(RsoLinkError::MissingImport {
                name: "host".to_owned(),
            })
        );
        let duplicates = vec![
            ResolvedSelSymbol {
                name: "host".to_owned(),
                guest_address: 0x804A_3F24,
                section_index: 2,
                elf_hash: 1,
            },
            ResolvedSelSymbol {
                name: "host".to_owned(),
                guest_address: 0x804A_3F28,
                section_index: 2,
                elf_hash: 2,
            },
        ];
        assert_eq!(
            image.bind_imports(&duplicates),
            Err(RsoLinkError::AmbiguousImport {
                name: "host".to_owned(),
                count: 2,
            })
        );
    }

    #[test]
    fn applies_internal_and_external_relocations_without_runtime_execution() {
        let input = synthetic_rso();
        let image = parse_rso(&input).expect("synthetic RSO parses");
        let linked = image
            .apply_relocations(
                &input,
                &RsoLinkLayout {
                    section_addresses: vec![0, 0x8000_0100],
                },
                &[RsoImportBinding {
                    import_index: 0,
                    name: "host".to_owned(),
                    guest_address: 0x8000_0200,
                }],
            )
            .expect("all synthetic relocations link");
        assert_eq!(read_u32_at(&linked, 0x104), 0x8000_0110);
        assert_eq!(read_u32_at(&linked, 0x108), 0x4800_00F8);
    }

    #[test]
    fn code_lowering_metadata_tracks_address_taken_entries_without_rewriting_branches() {
        let input = synthetic_rso();
        let image = parse_rso(&input).expect("synthetic RSO parses");
        let metadata = image
            .code_section_lowering_metadata(
                &input,
                1,
                &RsoLinkLayout {
                    section_addresses: vec![0, 0x8000_0100],
                },
                &[RsoImportBinding {
                    import_index: 0,
                    name: "host".to_owned(),
                    guest_address: 0x8000_0200,
                }],
            )
            .expect("address word and branch relocation metadata is representable");
        assert_eq!(metadata.entries, BTreeSet::from([0x8000_0110]));
        assert!(metadata.immediate_overrides.is_empty());
    }

    #[test]
    fn relocation_patch_bounds_use_the_actual_field_width() {
        for (kind, expected) in [(3u8, 0x0110u16), (4, 0x0110), (5, 0x8000), (6, 0x8000)] {
            let mut input = synthetic_rso();
            write_u32(&mut input, 0x140, 0x13E);
            input[0x147] = kind;
            let image = parse_rso(&input).expect("final in-section halfword is valid");
            let linked = image
                .apply_relocations(
                    &input,
                    &RsoLinkLayout {
                        section_addresses: vec![0, 0x8000_0100],
                    },
                    &[RsoImportBinding {
                        import_index: 0,
                        name: "host".to_owned(),
                        guest_address: 0x8000_0200,
                    }],
                )
                .expect("two-byte patch does not need bytes from the following table");
            assert_eq!(read_u16_at(&linked, 0x13E), expected);
            assert_eq!(&linked[0x140..0x14C], &input[0x140..0x14C]);

            write_u32(&mut input, 0x140, 0x13F);
            assert!(matches!(
                parse_rso(&input),
                Err(RsoError::RelocationOffsetOutsideSection {
                    table: "internal",
                    offset: 0x13F,
                    ..
                })
            ));

            // The external relocation table uses the same width policy.
            let mut input = synthetic_rso();
            write_u32(&mut input, 0x14C, 0x13E);
            input[0x153] = kind;
            parse_rso(&input).expect("external final halfword is valid");
            write_u32(&mut input, 0x14C, 0x13F);
            assert!(matches!(
                parse_rso(&input),
                Err(RsoError::RelocationOffsetOutsideSection {
                    table: "external",
                    offset: 0x13F,
                    ..
                })
            ));
        }
        let mut input = synthetic_rso();
        write_u32(&mut input, 0x140, 0x13E); // ADDR32 still needs four bytes.
        assert!(matches!(
            parse_rso(&input),
            Err(RsoError::RelocationOffsetOutsideSection { .. })
        ));
        write_u32(&mut input, 0x140, 0x13C);
        parse_rso(&input).expect("final full word is valid");
    }

    #[test]
    fn address_taken_entries_include_the_original_addr32_addend() {
        let layout = RsoLinkLayout {
            section_addresses: vec![0, 0x8000_0100],
        };
        let bindings = [RsoImportBinding {
            import_index: 0,
            name: "host".to_owned(),
            guest_address: 0x8000_0200,
        }];
        for (symbol_offset, addend, target, expected_entries) in [
            (0x10, 4, 0x8000_0114, BTreeSet::from([0x8000_0114])),
            (0, 4, 0x8000_0104, BTreeSet::from([0x8000_0104])),
            (0x10, 3, 0x8000_0113, BTreeSet::new()),
            (0x10, 0x30, 0x8000_0140, BTreeSet::new()),
        ] {
            let mut input = synthetic_rso();
            write_u32(&mut input, 0x148, symbol_offset);
            write_u32(&mut input, 0x104, addend);
            let image = parse_rso(&input).unwrap();
            let linked = image.apply_relocations(&input, &layout, &bindings).unwrap();
            assert_eq!(read_u32_at(&linked, 0x104), target);
            let metadata = image
                .code_section_lowering_metadata(&input, 1, &layout, &bindings)
                .unwrap();
            assert_eq!(metadata.entries, expected_entries);
        }
        let mut input = synthetic_rso();
        write_u32(&mut input, 0x104, u32::MAX);
        let image = parse_rso(&input).unwrap();
        assert!(matches!(
            image.code_section_lowering_metadata(&input, 1, &layout, &bindings),
            Err(RsoRelocationError::TargetAddressOverflow { .. })
        ));
    }

    #[test]
    fn address_taken_halfword_entries_follow_linker_targets_and_bounds() {
        let layout = RsoLinkLayout {
            section_addresses: vec![0, 0x8000_0100],
        };
        let bindings = [RsoImportBinding {
            import_index: 0,
            name: "host".to_owned(),
            guest_address: 0x8000_0200,
        }];
        for kind in [4u8, 6u8] {
            for (addend, expected_entries) in [
                (4u32, BTreeSet::from([0x8000_0114])),
                (3, BTreeSet::new()),
                (0x30, BTreeSet::new()),
            ] {
                let mut input = synthetic_rso();
                write_u32(&mut input, 0x104, 0x3C60_0000 | addend);
                write_u32(&mut input, 0x140, 0x106);
                input[0x147] = kind;
                let image = parse_rso(&input).unwrap();
                let metadata = image
                    .code_section_lowering_metadata(&input, 1, &layout, &bindings)
                    .unwrap();
                assert_eq!(metadata.entries, expected_entries);
                let linked = image.apply_relocations(&input, &layout, &bindings).unwrap();
                let target = 0x8000_0110u32 + addend;
                let expected = if kind == 4 {
                    target as u16
                } else {
                    (target.wrapping_add(0x8000) >> 16) as u16
                };
                assert_eq!(read_u16_at(&linked, 0x106), expected);
            }
            let mut input = synthetic_rso();
            write_u32(&mut input, 0x104, 0x3C60_0004);
            write_u32(&mut input, 0x140, 0x106);
            input[0x147] = kind;
            let mut image = parse_rso(&input).unwrap();
            // A data relocation cannot admit an ordinary code call.
            image.internal_relocations[0].symbol_index = 0;
            assert!(image
                .code_section_lowering_metadata(&input, 1, &layout, &bindings)
                .unwrap()
                .entries
                .is_empty());
            image.internal_relocations[0].symbol_index = 1;
            image.internal_relocations[0].file_offset = (input.len() - 1) as u32;
            assert!(matches!(
                image.code_section_lowering_metadata(&input, 1, &layout, &bindings),
                Err(RsoRelocationError::InputTooSmall { .. })
            ));
        }
    }

    #[test]
    fn native_sidecar_rejects_a_canonical_layout_that_aliases_an_import() {
        let input = synthetic_rso();
        let image = parse_rso(&input).expect("synthetic RSO parses");
        let error = image
            .generate_native_sidecar(
                &input,
                1,
                &RsoLinkLayout {
                    section_addresses: vec![0, 0x8000_0100],
                },
                &[RsoImportBinding {
                    import_index: 0,
                    name: "host".to_owned(),
                    // This address lies inside section 1's 0x80000100..140
                    // canonical range. Accepting it would let an external
                    // REL24 be lowered as an RSO-local control-flow edge.
                    guest_address: 0x8000_0120,
                }],
            )
            .expect_err("overlapping imported target must fail before lowering");
        assert!(matches!(
            error,
            RsoNativeSidecarError::ExternalBindingOverlapsCodeRange {
                index: 0,
                address: 0x8000_0120,
                code_start: 0x8000_0100,
                code_end: 0x8000_0140,
                ..
            }
        ));
    }

    #[test]
    fn code_lowering_metadata_emits_symbolic_high_adjusted_immediate() {
        let mut input = synthetic_rso();
        // lis r3, 0. The relocation's halfword starts at byte 0x106 rather
        // than the instruction word's byte 0x104.
        write_u32(&mut input, 0x104, 0x3C60_0000);
        write_u32(&mut input, 0x140, 0x106);
        input[0x147] = 6; // R_PPC_ADDR16_HA
        let image = parse_rso(&input).expect("synthetic RSO parses");
        let metadata = image
            .code_section_lowering_metadata(
                &input,
                1,
                &RsoLinkLayout {
                    section_addresses: vec![0, 0x8000_0100],
                },
                &[RsoImportBinding {
                    import_index: 0,
                    name: "host".to_owned(),
                    guest_address: 0x8000_0200,
                }],
            )
            .expect("high-adjusted immediate metadata is representable");
        assert_eq!(metadata.entries, BTreeSet::from([0x8000_0110]));
        assert_eq!(
            metadata.immediate_overrides.get(&0x8000_0104),
            Some(&PpcImmediateOverride {
                kind: PpcImmediateKind::HighAdjusted16,
                expression: "rso_section_1 + 0x00000010u".to_owned(),
            })
        );
    }

    #[test]
    fn dynamic_pc_rewrite_handles_flat_and_nested_return_dispatches() {
        let body = concat!(
            "    switch (context->lr & 0xFFFFFFFCu) {\n",
            "    case 0x80000020u: goto call_return_80000020;\n",
            "    default: return;\n",
            "    }\n",
            "    if (galaxy::cr_bit(context, 2u)) {\n",
            "        switch (context->lr & 0xFFFFFFFCu) {\n",
            "        case 0x80000040u: goto call_return_80000040;\n",
            "        default: return;\n",
            "        }\n",
            "    }\n",
            "    context->pc = 0x80000008u;\n",
            "call_return_80000020:\n",
            "call_return_80000040:\n"
        );
        let rewritten = rewrite_dynamic_pc_literals(body, 0x8000_0000, 0x100, &BTreeSet::new(), 1)
            .expect("known lowerer return-dispatch forms rewrite");

        assert!(!rewritten.contains("switch (context->lr & 0xFFFFFFFCu)"));
        assert!(!rewritten.contains("case rso_pc("));
        assert!(rewritten.contains(
            "    context->pc = context->lr & 0xFFFFFFFCu;\n    *resume_rso_return = true;\n    return;"
        ));
        assert!(rewritten.contains(
            "        context->pc = context->lr & 0xFFFFFFFCu;\n        *resume_rso_return = true;\n        return;"
        ));
        assert!(rewritten.contains("context->pc = rso_pc(0x00000008u);"));
    }

    #[test]
    fn dynamic_pc_rewrite_preserves_guarded_fpu_retry_dispatch() {
        let body = concat!(
            "    if (context->pc != 0x80000000u) [[unlikely]] {\n",
            "    switch (context->pc) {\n",
            "        case 0x80000004u: goto fpu_retry_80000004;\n",
            "        case 0x80000008u: goto label_80000008;\n",
            "        default: galaxy::guest_execution_fault(services, context->pc, \"invalid interior function entry\");\n",
            "    }\n",
            "    }\n",
            "    goto fpu_normal_entry_80000000;\n",
            "fpu_retry_80000004:\n",
            "    galaxy::ensure_fpu_available(services, 0x80000004u, context, memory);\n",
            "    goto label_80000004;\n",
            "fpu_normal_entry_80000000:\n",
            "label_80000004:\n",
            "label_80000008:\n",
        );
        let entries = BTreeSet::from([0x8000_0004, 0x8000_0008]);

        let rewritten = rewrite_dynamic_pc_literals(body, 0x8000_0000, 0x10, &entries, 1)
            .expect("guarded FPU retry dispatch rewrites");

        assert!(rewritten.contains("case 4u: goto fpu_retry_80000004;"));
        assert!(rewritten.contains("switch (context->pc - rso_pc(0x00000000u))"));
        assert!(rewritten.contains("default: galaxy::guest_execution_fault"));
        assert!(!rewritten.contains("if (context->pc == rso_pc("));
        assert!(!rewritten.contains("case 4u: goto label_80000004;"));
        assert!(rewritten.contains("case 8u: goto label_80000008;"));
        assert!(rewritten.contains("fpu_retry_80000004:"));
        assert!(rewritten.contains("galaxy::ensure_fpu_available("));
    }

    #[test]
    fn dynamic_pc_rewrite_hard_fails_a_mismatched_retry_target() {
        let body = concat!(
            "    if (context->pc != 0x80000000u) [[unlikely]] {\n",
            "    switch (context->pc) {\n",
            "        case 0x80000004u: goto fpu_retry_80000008;\n",
            "        default: galaxy::guest_execution_fault(services, context->pc, \"invalid interior function entry\");\n",
            "    }\n",
            "    }\n",
        );
        let entries = BTreeSet::from([0x8000_0004]);

        let error = rewrite_dynamic_pc_literals(body, 0x8000_0000, 0x10, &entries, 1)
            .expect_err("a retry case cannot target a different instruction");

        assert!(matches!(
            error,
            RsoNativeSidecarError::InvalidInteriorDispatch { section: 1, .. }
        ));
    }

    #[test]
    fn records_linked_indirect_call_returns_for_static_rso_resumption() {
        let words = [0x4E80_0421_u32, 0x3863_0001, 0x4E80_0020]; // bctrl; addi; blr
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_be_bytes()).collect();
        assert_eq!(
            linked_indirect_call_return_entries(0x8000_4000, &bytes),
            BTreeSet::from([0x8000_4004])
        );
    }

    #[test]
    fn home_backing_hook_uses_relocated_pc_and_own_header() {
        let mut input = synthetic_rso();
        input[0x21] = 1;
        input[0x22] = 1;
        let image = parse_rso(&input).expect("synthetic RSO parses");
        let body = rewrite_dynamic_pc_literals(
            "    galaxy::apply_rmge01_home_backings(context, memory, services, 0x80000108u);\n    galaxy::apply_rmge01_home_vertical(context, memory, services, 0x8000010Cu);\n",
            0x8000_0100,
            0x10,
            &BTreeSet::new(),
            1,
        )
        .expect("native hook fault identity relocates with the sidecar");
        assert!(body.contains("rso_pc(0x00000008u)"));
        let source = render_native_sidecar_cpp(
            &image,
            NativeSidecarRenderParams {
                code_section: 1,
                code_file_offset: 0x100,
                canonical_address: 0x8000_0100,
                call_entries: &BTreeSet::new(),
                symbolic_immediate_count: 0,
                body: &body,
                rso_sha1: "0000000000000000000000000000000000000000",
            },
        )
        .expect("sidecar renders the maintained native hook");
        assert!(source.contains("#include \"galaxy/layout_aspect.h\""));
        assert!(source.contains("#include \"galaxy/layout_aspect_home.h\""));
        assert!(body.contains("rso_pc(0x0000000Cu)"));
        assert!(!source.contains("services, 0x8000010Cu"));
        assert!(!source.contains("services, 0x80000108u"));
    }

    #[test]
    fn sidecar_trace_is_opt_in_and_uses_the_secure_windows_environment_api() {
        let mut input = synthetic_rso();
        input[0x21] = 1;
        input[0x22] = 1;
        let image = parse_rso(&input).expect("synthetic RSO parses");
        let source = render_native_sidecar_cpp(
            &image,
            NativeSidecarRenderParams {
                code_section: 1,
                code_file_offset: 0x100,
                canonical_address: 0x8000_0100,
                call_entries: &BTreeSet::new(),
                symbolic_immediate_count: 0,
                body: "",
                rso_sha1: "0000000000000000000000000000000000000000",
            },
        )
        .expect("sidecar source renders");

        assert!(source.contains("GALAXY_TRACE_HOME_BUTTON_RSO_SIDECAR"));
        assert!(source.contains("#include <atomic>"));
        assert!(source.contains("_dupenv_s("));
        assert!(!source.contains("std::getenv("));
        assert!(source.contains("trace_count.fetch_add(1u, std::memory_order_relaxed) >= 32u"));
        assert!(source.contains("\"call-enter\""));
        assert!(source.contains("\"call-exit\""));
        assert!(source.contains("bool returned_to_flat_dispatch = false;"));
        assert!(source
            .contains("const std::uint32_t native_caller_return = context->lr & 0xFFFFFFFCu;"));
        assert!(source.contains("\"invalid static Home Button RSO local return\""));
        assert!(!source
            .contains("for (;;) {\n        if (!is_static_rso_resume_entry(context->pc, layout))"));
    }

    #[test]
    fn rejects_external_rel24_that_needs_an_uninvented_trampoline() {
        let input = synthetic_rso();
        let image = parse_rso(&input).expect("synthetic RSO parses");
        assert!(matches!(
            image.apply_relocations(
                &input,
                &RsoLinkLayout {
                    section_addresses: vec![0, 0x8000_0100],
                },
                &[RsoImportBinding {
                    import_index: 0,
                    name: "host".to_owned(),
                    guest_address: 0x8400_0000,
                }],
            ),
            Err(RsoRelocationError::Rel24OutOfRange {
                table: "external",
                index: 0,
                ..
            })
        ));
    }
}
