//! Install-time parser for Wii SEL symbol-link tables.
//!
//! SEL files resolve RSO imports to the main DOL. This parser only consumes
//! installer input; it neither executes RSO code nor retains game content.

use crate::DolSection;
use serde::Serialize;
use std::collections::BTreeMap;
use thiserror::Error;

const HEADER_SIZE: usize = 0x58;
const LINKER_ENTRY_SIZE: usize = 16;
const ABSOLUTE_SECTION: u32 = 0x0000_FFF1;

#[derive(Debug, Error, PartialEq, Eq)]
pub enum SelError {
    #[error("SEL is too small for its 0x58-byte header: {actual} bytes")]
    TruncatedHeader { actual: usize },
    #[error("SEL header length is 0x{actual:08X}; expected 0x00000058")]
    InvalidHeaderLength { actual: u32 },
    #[error("SEL declares a 0x{declared:08X}-byte file but has {actual} physical bytes")]
    DeclaredFileSizeOutOfRange { declared: u32, actual: usize },
    #[error("SEL has non-zero trailing padding at 0x{offset:08X}")]
    NonZeroTrailingPadding { offset: u32 },
    #[error("SEL {table} size 0x{size:08X} is not a multiple of entry size {entry_size}")]
    MisalignedTableSize {
        table: &'static str,
        size: u32,
        entry_size: usize,
    },
    #[error("SEL {table} offset=0x{offset:08X} size=0x{size:08X} is outside its declared 0x{file_size:08X}-byte file")]
    TableOutOfRange {
        table: &'static str,
        offset: u32,
        size: u32,
        file_size: u32,
    },
    #[error("SEL string '{field}' starts at 0x{offset:08X}, outside its declared 0x{file_size:08X}-byte file")]
    StringOutOfRange {
        field: &'static str,
        offset: u32,
        file_size: u32,
    },
    #[error("SEL string '{field}' at 0x{offset:08X} has no terminating NUL before the declared end of file")]
    UnterminatedString { field: &'static str, offset: u32 },
    #[error("SEL string '{field}' at 0x{offset:08X} is not valid UTF-8")]
    InvalidUtf8String { field: &'static str, offset: u32 },
    #[error("SEL symbol '{symbol}' has unsupported DOL section selector 0x{section:08X}")]
    UnsupportedSection { symbol: String, section: u32 },
    #[error("SEL symbol '{symbol}' requires DOL text section {section}, but the DOL has only {available} text sections")]
    MissingDolTextSection {
        symbol: String,
        section: u32,
        available: usize,
    },
    #[error("SEL symbol '{symbol}' offset 0x{offset:08X} is outside DOL text section {section} (size 0x{section_size:08X})")]
    SymbolOffsetOutsideDolSection {
        symbol: String,
        offset: u32,
        section: u32,
        section_size: u32,
    },
    #[error("SEL symbol '{symbol}' address overflows a 32-bit guest address")]
    ResolvedAddressOverflow { symbol: String },
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct SelSymbol {
    pub name: String,
    /// DOL-section-relative byte offset, or an absolute guest address when
    /// `section_index` is `0xFFF1`.
    pub section_offset: u32,
    /// `1` and `2` select the first and second DOL text sections.
    pub section_index: u32,
    pub elf_hash: u32,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct ResolvedSelSymbol {
    pub name: String,
    pub guest_address: u32,
    pub section_index: u32,
    pub elf_hash: u32,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct SelImage {
    pub executable_name: Option<String>,
    pub version: u32,
    pub symbols: Vec<SelSymbol>,
}

impl SelImage {
    /// Resolves SEL linker entries against the validated DOL's text sections.
    /// This method is installer-only and returns metadata, not guest code.
    pub fn resolve_dol_symbols(
        &self,
        sections: &[DolSection],
    ) -> Result<Vec<ResolvedSelSymbol>, SelError> {
        let text_sections = sections
            .iter()
            .filter(|section| section.kind == crate::DolSectionKind::Text)
            .collect::<Vec<_>>();
        self.symbols
            .iter()
            .map(|symbol| {
                let guest_address = if symbol.section_index == ABSOLUTE_SECTION {
                    symbol.section_offset
                } else if (1..=2).contains(&symbol.section_index) {
                    let index =
                        usize::try_from(symbol.section_index - 1).expect("selector fits usize");
                    let Some(section) = text_sections.get(index) else {
                        return Err(SelError::MissingDolTextSection {
                            symbol: symbol.name.clone(),
                            section: symbol.section_index,
                            available: text_sections.len(),
                        });
                    };
                    if symbol.section_offset >= section.size {
                        return Err(SelError::SymbolOffsetOutsideDolSection {
                            symbol: symbol.name.clone(),
                            offset: symbol.section_offset,
                            section: symbol.section_index,
                            section_size: section.size,
                        });
                    }
                    section.address.checked_add(symbol.section_offset).ok_or(
                        SelError::ResolvedAddressOverflow {
                            symbol: symbol.name.clone(),
                        },
                    )?
                } else {
                    return Err(SelError::UnsupportedSection {
                        symbol: symbol.name.clone(),
                        section: symbol.section_index,
                    });
                };
                Ok(ResolvedSelSymbol {
                    name: symbol.name.clone(),
                    guest_address,
                    section_index: symbol.section_index,
                    elf_hash: symbol.elf_hash,
                })
            })
            .collect()
    }

    #[must_use]
    pub fn symbols_by_name(&self) -> BTreeMap<&str, Vec<&SelSymbol>> {
        let mut result = BTreeMap::new();
        for symbol in &self.symbols {
            result
                .entry(symbol.name.as_str())
                .or_insert_with(Vec::new)
                .push(symbol);
        }
        result
    }
}

pub fn parse_sel(data: &[u8]) -> Result<SelImage, SelError> {
    if data.len() < HEADER_SIZE {
        return Err(SelError::TruncatedHeader { actual: data.len() });
    }
    let header_size = read_u32(data, 0x0C);
    if usize::try_from(header_size).expect("u32 fits usize") != HEADER_SIZE {
        return Err(SelError::InvalidHeaderLength {
            actual: header_size,
        });
    }
    let declared_file_size = read_u32(data, 0x54);
    let declared = usize::try_from(declared_file_size).expect("u32 fits usize");
    if declared < HEADER_SIZE || declared > data.len() {
        return Err(SelError::DeclaredFileSizeOutOfRange {
            declared: declared_file_size,
            actual: data.len(),
        });
    }
    if let Some((relative, _)) = data[declared..]
        .iter()
        .enumerate()
        .find(|(_, byte)| **byte != 0)
    {
        return Err(SelError::NonZeroTrailingPadding {
            offset: u32::try_from(declared + relative).unwrap_or(u32::MAX),
        });
    }

    let executable_name_offset = read_u32(data, 0x10);
    let executable_name_size = read_u32(data, 0x14);
    let linker_offset = read_u32(data, 0x40);
    let linker_size = read_u32(data, 0x44);
    let name_table_offset = read_u32(data, 0x48);
    let executable_name = if executable_name_offset == 0 {
        None
    } else {
        Some(read_sized_string(
            data,
            declared,
            "executable-name",
            executable_name_offset,
            usize::try_from(executable_name_size).expect("u32 fits usize"),
        )?)
    };
    let entries = checked_table(
        data,
        declared,
        "linker-table",
        linker_offset,
        linker_size,
        LINKER_ENTRY_SIZE,
    )?;
    let name_table_base = checked_offset(declared, "function-name-table", name_table_offset)?;
    let mut symbols = Vec::with_capacity(entries.len() / LINKER_ENTRY_SIZE);
    for entry in entries.chunks_exact(LINKER_ENTRY_SIZE) {
        let name_offset = checked_add_offset(
            declared,
            name_table_base,
            read_u32(entry, 0),
            "function-name",
        )?;
        symbols.push(SelSymbol {
            name: read_nul_string(data, declared, "function-name", name_offset)?,
            section_offset: read_u32(entry, 4),
            section_index: read_u32(entry, 8),
            elf_hash: read_u32(entry, 12),
        });
    }
    Ok(SelImage {
        executable_name,
        version: read_u32(data, 0x18),
        symbols,
    })
}

fn checked_table<'a>(
    data: &'a [u8],
    declared: usize,
    table: &'static str,
    offset: u32,
    size: u32,
    entry_size: usize,
) -> Result<&'a [u8], SelError> {
    if usize::try_from(size).expect("u32 fits usize") % entry_size != 0 {
        return Err(SelError::MisalignedTableSize {
            table,
            size,
            entry_size,
        });
    }
    let start = usize::try_from(offset).expect("u32 fits usize");
    let length = usize::try_from(size).expect("u32 fits usize");
    let end = start.checked_add(length);
    if end.map_or(true, |end| end > declared) {
        return Err(SelError::TableOutOfRange {
            table,
            offset,
            size,
            file_size: u32::try_from(declared).expect("from u32"),
        });
    }
    Ok(&data[start..end.expect("range checked")])
}

fn checked_offset(declared: usize, field: &'static str, offset: u32) -> Result<usize, SelError> {
    let offset = usize::try_from(offset).expect("u32 fits usize");
    if offset >= declared {
        return Err(SelError::StringOutOfRange {
            field,
            offset: u32::try_from(offset).expect("from u32"),
            file_size: u32::try_from(declared).expect("from u32"),
        });
    }
    Ok(offset)
}

fn checked_add_offset(
    declared: usize,
    base: usize,
    relative: u32,
    field: &'static str,
) -> Result<usize, SelError> {
    let offset = base.checked_add(usize::try_from(relative).expect("u32 fits usize"));
    if offset.map_or(true, |offset| offset >= declared) {
        return Err(SelError::StringOutOfRange {
            field,
            offset: offset
                .and_then(|value| u32::try_from(value).ok())
                .unwrap_or(u32::MAX),
            file_size: u32::try_from(declared).expect("from u32"),
        });
    }
    Ok(offset.expect("range checked"))
}

fn read_sized_string(
    data: &[u8],
    declared: usize,
    field: &'static str,
    offset: u32,
    size: usize,
) -> Result<String, SelError> {
    let start = checked_offset(declared, field, offset)?;
    let end = start.checked_add(size);
    if end.map_or(true, |end| end > declared) {
        return Err(SelError::StringOutOfRange {
            field,
            offset,
            file_size: u32::try_from(declared).expect("from u32"),
        });
    }
    std::str::from_utf8(&data[start..end.expect("range checked")])
        .map(ToOwned::to_owned)
        .map_err(|_| SelError::InvalidUtf8String { field, offset })
}

fn read_nul_string(
    data: &[u8],
    declared: usize,
    field: &'static str,
    offset: usize,
) -> Result<String, SelError> {
    let bytes = &data[offset..declared];
    let Some(length) = bytes.iter().position(|byte| *byte == 0) else {
        return Err(SelError::UnterminatedString {
            field,
            offset: u32::try_from(offset).unwrap_or(u32::MAX),
        });
    };
    std::str::from_utf8(&bytes[..length])
        .map(ToOwned::to_owned)
        .map_err(|_| SelError::InvalidUtf8String {
            field,
            offset: u32::try_from(offset).unwrap_or(u32::MAX),
        })
}

fn read_u32(data: &[u8], offset: usize) -> u32 {
    u32::from_be_bytes(
        data[offset..offset + 4]
            .try_into()
            .expect("caller checked SEL range"),
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    fn write_u32(data: &mut [u8], offset: usize, value: u32) {
        data[offset..offset + 4].copy_from_slice(&value.to_be_bytes());
    }

    fn synthetic_sel() -> Vec<u8> {
        let mut data = vec![0_u8; 0xC0];
        write_u32(&mut data, 0x0C, 0x58);
        write_u32(&mut data, 0x10, 0x58);
        write_u32(&mut data, 0x14, 11);
        write_u32(&mut data, 0x18, 1);
        write_u32(&mut data, 0x40, 0x70);
        write_u32(&mut data, 0x44, 16);
        write_u32(&mut data, 0x48, 0x90);
        write_u32(&mut data, 0x54, 0xB0);
        data[0x58..0x63].copy_from_slice(b"product.elf");
        write_u32(&mut data, 0x74, 0x20);
        write_u32(&mut data, 0x78, 2);
        write_u32(&mut data, 0x7C, 0x1234_5678);
        data[0x90..0x94].copy_from_slice(b"host");
        data[0x94] = 0;
        data
    }

    #[test]
    fn parses_valid_link_table() {
        let image = parse_sel(&synthetic_sel()).expect("synthetic SEL parses");
        assert_eq!(image.executable_name.as_deref(), Some("product.elf"));
        assert_eq!(
            image.symbols,
            vec![SelSymbol {
                name: "host".to_owned(),
                section_offset: 0x20,
                section_index: 2,
                elf_hash: 0x1234_5678
            }]
        );
    }

    #[test]
    fn rejects_nonzero_padding() {
        let mut data = synthetic_sel();
        data[0xB0] = 1;
        assert_eq!(
            parse_sel(&data),
            Err(SelError::NonZeroTrailingPadding { offset: 0xB0 })
        );
    }

    #[test]
    fn resolves_second_text_section() {
        let image = parse_sel(&synthetic_sel()).expect("synthetic SEL parses");
        let sections = vec![
            DolSection {
                name: "text0".to_owned(),
                kind: crate::DolSectionKind::Text,
                index: 0,
                file_offset: 0,
                address: 0x8000_4000,
                size: 0x100,
            },
            DolSection {
                name: "text1".to_owned(),
                kind: crate::DolSectionKind::Text,
                index: 1,
                file_offset: 0x100,
                address: 0x8000_7000,
                size: 0x100,
            },
        ];
        assert_eq!(
            image.resolve_dol_symbols(&sections).expect("resolves")[0].guest_address,
            0x8000_7020
        );
    }
}
