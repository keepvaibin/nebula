use serde::Serialize;
use std::ops::Range;
use thiserror::Error;

const DOL_HEADER_SIZE: usize = 0x100;
const TEXT_SECTION_COUNT: usize = 7;
const DATA_SECTION_COUNT: usize = 11;

#[derive(Debug, Error, PartialEq, Eq)]
pub enum DolError {
    #[error("file is too small for a DOL header: {0} bytes")]
    TruncatedHeader(usize),
    #[error("{section} has a non-four-byte-aligned file offset, address, or size")]
    MisalignedText { section: String },
    #[error("{section} starts inside the DOL header")]
    SectionInsideHeader { section: String },
    #[error("{section} extends past the end of the file")]
    SectionPastEnd { section: String },
    #[error("{section} has an overflowing address range")]
    AddressOverflow { section: String },
    #[error("file ranges overlap: {first} and {second}")]
    FileOverlap { first: String, second: String },
    #[error("guest address ranges overlap: {first} and {second}")]
    AddressOverlap { first: String, second: String },
    #[error("DOL has no executable text sections")]
    MissingText,
    #[error("entry point 0x{entry:08X} is outside all text sections")]
    InvalidEntryPoint { entry: u32 },
    #[error("entry point 0x{entry:08X} is not four-byte aligned")]
    MisalignedEntryPoint { entry: u32 },
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum DolSectionKind {
    Text,
    Data,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct DolSection {
    pub name: String,
    pub kind: DolSectionKind,
    pub index: usize,
    pub file_offset: u32,
    pub address: u32,
    pub size: u32,
}

impl DolSection {
    pub fn file_range(&self) -> Range<usize> {
        let start = self.file_offset as usize;
        start..start + self.size as usize
    }

    pub fn address_range(&self) -> Range<u64> {
        let start = self.address as u64;
        start..start + self.size as u64
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct DolImage {
    pub entry_point: u32,
    pub bss_address: u32,
    pub bss_size: u32,
    pub sections: Vec<DolSection>,
}

impl DolImage {
    pub fn parse(data: &[u8]) -> Result<Self, DolError> {
        if data.len() < DOL_HEADER_SIZE {
            return Err(DolError::TruncatedHeader(data.len()));
        }

        let mut sections = Vec::with_capacity(TEXT_SECTION_COUNT + DATA_SECTION_COUNT);
        read_sections(
            data,
            &mut sections,
            DolSectionKind::Text,
            TEXT_SECTION_COUNT,
            0x00,
            0x48,
            0x90,
        )?;
        read_sections(
            data,
            &mut sections,
            DolSectionKind::Data,
            DATA_SECTION_COUNT,
            0x1C,
            0x64,
            0xAC,
        )?;

        if !sections
            .iter()
            .any(|section| section.kind == DolSectionKind::Text)
        {
            return Err(DolError::MissingText);
        }

        validate_no_overlap(
            &sections,
            |section| {
                let range = section.file_range();
                range.start as u64..range.end as u64
            },
            |first, second| DolError::FileOverlap { first, second },
        )?;
        validate_no_overlap(&sections, DolSection::address_range, |first, second| {
            DolError::AddressOverlap { first, second }
        })?;

        let bss_address = read_u32(data, 0xD8);
        let bss_size = read_u32(data, 0xDC);
        bss_address
            .checked_add(bss_size)
            .ok_or_else(|| DolError::AddressOverflow {
                section: "bss".to_owned(),
            })?;
        let entry_point = read_u32(data, 0xE0);
        if entry_point % 4 != 0 {
            return Err(DolError::MisalignedEntryPoint { entry: entry_point });
        }
        let entry_valid = sections.iter().any(|section| {
            section.kind == DolSectionKind::Text
                && section.address_range().contains(&(entry_point as u64))
        });
        if !entry_valid {
            return Err(DolError::InvalidEntryPoint { entry: entry_point });
        }

        Ok(Self {
            entry_point,
            bss_address,
            bss_size,
            sections,
        })
    }

    pub fn text_sections(&self) -> impl Iterator<Item = &DolSection> {
        self.sections
            .iter()
            .filter(|section| section.kind == DolSectionKind::Text)
    }
}

fn read_sections(
    data: &[u8],
    sections: &mut Vec<DolSection>,
    kind: DolSectionKind,
    count: usize,
    offsets_base: usize,
    addresses_base: usize,
    sizes_base: usize,
) -> Result<(), DolError> {
    for index in 0..count {
        let file_offset = read_u32(data, offsets_base + index * 4);
        let address = read_u32(data, addresses_base + index * 4);
        let size = read_u32(data, sizes_base + index * 4);
        if size == 0 {
            continue;
        }

        let prefix = match kind {
            DolSectionKind::Text => "text",
            DolSectionKind::Data => "data",
        };
        let name = format!("{prefix}{index}");

        if kind == DolSectionKind::Text
            && (file_offset % 4 != 0 || address % 4 != 0 || size % 4 != 0)
        {
            return Err(DolError::MisalignedText { section: name });
        }
        if file_offset < DOL_HEADER_SIZE as u32 {
            return Err(DolError::SectionInsideHeader { section: name });
        }

        let file_end = file_offset
            .checked_add(size)
            .ok_or_else(|| DolError::SectionPastEnd {
                section: name.clone(),
            })?;
        if file_end as usize > data.len() {
            return Err(DolError::SectionPastEnd { section: name });
        }
        address
            .checked_add(size)
            .ok_or_else(|| DolError::AddressOverflow {
                section: name.clone(),
            })?;

        sections.push(DolSection {
            name,
            kind,
            index,
            file_offset,
            address,
            size,
        });
    }
    Ok(())
}

fn validate_no_overlap<F, E>(
    sections: &[DolSection],
    range_for: F,
    error_for: E,
) -> Result<(), DolError>
where
    F: Fn(&DolSection) -> Range<u64>,
    E: Fn(String, String) -> DolError,
{
    for (index, first) in sections.iter().enumerate() {
        for second in sections.iter().skip(index + 1) {
            if ranges_overlap(&range_for(first), &range_for(second)) {
                return Err(error_for(first.name.clone(), second.name.clone()));
            }
        }
    }
    Ok(())
}

fn ranges_overlap(first: &Range<u64>, second: &Range<u64>) -> bool {
    first.start < second.end && second.start < first.end
}

fn read_u32(data: &[u8], offset: usize) -> u32 {
    u32::from_be_bytes(
        data[offset..offset + 4]
            .try_into()
            .expect("checked DOL header"),
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    fn synthetic_dol(words: &[u32]) -> Vec<u8> {
        let mut data = vec![0_u8; DOL_HEADER_SIZE + words.len() * 4];
        write_u32(&mut data, 0x00, DOL_HEADER_SIZE as u32);
        write_u32(&mut data, 0x48, 0x8000_4000);
        write_u32(&mut data, 0x90, (words.len() * 4) as u32);
        write_u32(&mut data, 0xD8, 0x8001_0000);
        write_u32(&mut data, 0xDC, 0x1000);
        write_u32(&mut data, 0xE0, 0x8000_4000);
        for (index, word) in words.iter().enumerate() {
            write_u32(&mut data, DOL_HEADER_SIZE + index * 4, *word);
        }
        data
    }

    fn write_u32(data: &mut [u8], offset: usize, value: u32) {
        data[offset..offset + 4].copy_from_slice(&value.to_be_bytes());
    }

    #[test]
    fn parses_valid_dol() {
        let data = synthetic_dol(&[0x3860_0001, 0x4E80_0020]);
        let dol = DolImage::parse(&data).expect("valid DOL");
        assert_eq!(dol.entry_point, 0x8000_4000);
        assert_eq!(dol.sections.len(), 1);
        assert_eq!(dol.sections[0].size, 8);
    }

    #[test]
    fn rejects_truncated_header() {
        assert_eq!(
            DolImage::parse(&[0; 64]),
            Err(DolError::TruncatedHeader(64))
        );
    }

    #[test]
    fn rejects_entry_outside_text() {
        let mut data = synthetic_dol(&[0x4E80_0020]);
        write_u32(&mut data, 0xE0, 0x9000_0000);
        assert_eq!(
            DolImage::parse(&data),
            Err(DolError::InvalidEntryPoint { entry: 0x9000_0000 })
        );
    }

    #[test]
    fn rejects_unaligned_entries_inside_executable_text() {
        for entry in [0x8000_4001, 0x8000_4002, 0x8000_4003] {
            let mut data = synthetic_dol(&[0x3860_0001, 0x4E80_0020]);
            write_u32(&mut data, 0xE0, entry);
            assert_eq!(
                DolImage::parse(&data),
                Err(DolError::MisalignedEntryPoint { entry })
            );
        }
    }

    #[test]
    fn accepts_aligned_entry_inside_text() {
        let mut data = synthetic_dol(&[0x3860_0001, 0x4E80_0020]);
        write_u32(&mut data, 0xE0, 0x8000_4004);
        assert_eq!(DolImage::parse(&data).unwrap().entry_point, 0x8000_4004);
    }

    #[test]
    fn rejects_overlapping_file_sections() {
        let mut data = synthetic_dol(&[0x4E80_0020, 0x4E80_0020]);
        write_u32(&mut data, 0x1C, DOL_HEADER_SIZE as u32);
        write_u32(&mut data, 0x64, 0x8000_5000);
        write_u32(&mut data, 0xAC, 4);
        assert!(matches!(
            DolImage::parse(&data),
            Err(DolError::FileOverlap { .. })
        ));
    }

    #[test]
    fn permits_bss_to_overlap_initialized_data() {
        let mut data = synthetic_dol(&[0x4E80_0020]);
        write_u32(&mut data, 0x1C, (DOL_HEADER_SIZE + 4) as u32);
        write_u32(&mut data, 0x64, 0x8001_0800);
        write_u32(&mut data, 0xAC, 4);
        data.extend_from_slice(&[1, 2, 3, 4]);

        let dol = DolImage::parse(&data).expect("BSS is cleared before sections are loaded");
        assert_eq!(dol.sections.len(), 2);
    }
}
