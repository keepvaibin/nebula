use crate::{
    audit_dsp_program,
    game::{load_verified_game, GameError},
    DolImage, DolSectionKind, DspProgramAudit, DSP_DRAM_WORDS, DSP_IRAM_WORDS,
};
use serde::Serialize;
use sha1::{Digest, Sha1};
use sha2::Sha256;
use std::{ops::Range, path::Path};

const PACKED_DESCRIPTOR_WORDS: usize = 7;
const PACKED_DESCRIPTOR_BYTES: usize = PACKED_DESCRIPTOR_WORDS * 4;
const EXPANDED_DESCRIPTOR_WORDS: usize = 8;
const EXPANDED_DESCRIPTOR_BYTES: usize = EXPANDED_DESCRIPTOR_WORDS * 4;
const DSP_MEMORY_BYTES: u32 = (DSP_IRAM_WORDS as u32) * 2;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum DspUcodeDescriptorFormat {
    PackedVectors,
    ExpandedVectors,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct DspUcodeCandidate {
    pub descriptor_format: DspUcodeDescriptorFormat,
    pub descriptor_address: u32,
    pub descriptor_section: String,
    pub iram_mmem_addr: u32,
    pub iram_destination: u32,
    pub iram_byte_len: u32,
    pub iram_word_count: u32,
    pub iram_sha1: String,
    pub iram_audit: Option<DspProgramAudit>,
    pub iram_audit_error: Option<String>,
    pub dram_mmem_addr: u32,
    pub dram_destination: u32,
    pub dram_byte_len: u32,
    pub dram_sha1: Option<String>,
    pub dsp_vector: u16,
    pub dsp_resume_vector: u16,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct DspUcodeScan {
    pub candidate_count: usize,
    pub candidates: Vec<DspUcodeCandidate>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DspUcodePayload {
    pub candidate: DspUcodeCandidate,
    pub iram_bytes: Vec<u8>,
    pub dram_bytes: Option<Vec<u8>>,
}

impl DspUcodePayload {
    pub fn iram_words_be(&self) -> Vec<u16> {
        read_be_u16_words(&self.iram_bytes)
    }
}

pub fn scan_rmge01_dsp_ucode_candidates(input: &Path) -> Result<DspUcodeScan, GameError> {
    let loaded = load_verified_game(input)?;
    Ok(scan_dsp_ucode_candidates_in_dol(
        &loaded.dol,
        &loaded.dol_bytes,
    ))
}

pub fn scan_dsp_ucode_candidates_in_dol(dol: &DolImage, data: &[u8]) -> DspUcodeScan {
    let mut candidates = Vec::new();

    for section in dol
        .sections
        .iter()
        .filter(|section| section.kind == DolSectionKind::Data)
    {
        let range = section.file_range();
        if range.len() < PACKED_DESCRIPTOR_BYTES {
            continue;
        }

        for offset in (range.start..=range.end - PACKED_DESCRIPTOR_BYTES).step_by(4) {
            let descriptor_address = section.address + (offset - range.start) as u32;
            let packed_descriptor = &data[offset..offset + PACKED_DESCRIPTOR_BYTES];
            let packed_words = read_descriptor_words::<PACKED_DESCRIPTOR_WORDS>(packed_descriptor);
            if let Some(candidate) = candidate_from_upload_fields(
                dol,
                data,
                section.name.clone(),
                descriptor_address,
                DspUcodeDescriptorFormat::PackedVectors,
                UploadFields {
                    iram_mmem_addr: packed_words[0],
                    iram_byte_len: packed_words[1],
                    iram_destination: packed_words[2],
                    dram_mmem_addr: packed_words[3],
                    dram_byte_len: packed_words[4],
                    dram_destination: packed_words[5],
                    dsp_vector: (packed_words[6] >> 16) as u16,
                    dsp_resume_vector: packed_words[6] as u16,
                },
            ) {
                candidates.push(candidate);
                continue;
            }

            if offset + EXPANDED_DESCRIPTOR_BYTES > range.end {
                continue;
            }
            let expanded_descriptor = &data[offset..offset + EXPANDED_DESCRIPTOR_BYTES];
            let expanded_words =
                read_descriptor_words::<EXPANDED_DESCRIPTOR_WORDS>(expanded_descriptor);
            let Some(dsp_vector) = u16::try_from(expanded_words[6]).ok() else {
                continue;
            };
            let Some(dsp_resume_vector) = u16::try_from(expanded_words[7]).ok() else {
                continue;
            };
            if let Some(candidate) = candidate_from_upload_fields(
                dol,
                data,
                section.name.clone(),
                descriptor_address,
                DspUcodeDescriptorFormat::ExpandedVectors,
                UploadFields {
                    iram_mmem_addr: expanded_words[0],
                    iram_byte_len: expanded_words[1],
                    iram_destination: expanded_words[2],
                    dram_mmem_addr: expanded_words[3],
                    dram_byte_len: expanded_words[4],
                    dram_destination: expanded_words[5],
                    dsp_vector,
                    dsp_resume_vector,
                },
            ) {
                candidates.push(candidate);
            }
        }
    }

    DspUcodeScan {
        candidate_count: candidates.len(),
        candidates,
    }
}

pub fn extract_rmge01_dsp_ucode_candidate(
    input: &Path,
    index: usize,
) -> Result<Option<DspUcodePayload>, GameError> {
    let loaded = load_verified_game(input)?;
    Ok(extract_dsp_ucode_candidate_in_dol(
        &loaded.dol,
        &loaded.dol_bytes,
        index,
    ))
}

/// Guest address of the AX DSP ucode image that RMGE01 uploads to DSP IRAM.
pub const RMGE01_AX_UCODE_ADDRESS: u32 = 0x805D_8460;
/// Byte length of the RMGE01 AX DSP ucode image.
pub const RMGE01_AX_UCODE_BYTE_LEN: u32 = 0x1F00;
/// SHA-256 of the RMGE01 AX DSP ucode image.
pub const RMGE01_AX_UCODE_SHA256: &str =
    "75c5859f566de81b9c1f539d63c53e629ce5465daaa208ba53714ac451106634";

/// Read RMGE01's AX DSP ucode from the verified DOL, in memory only.
///
/// The image sits at a fixed initialized-data address in the one supported
/// executable; its exact SHA-256 is checked so a mismatch hard-fails.
pub fn read_rmge01_ax_ucode(input: &Path) -> Result<Vec<u8>, GameError> {
    let loaded = load_verified_game(input)?;
    let range = guest_file_range(
        &loaded.dol,
        RMGE01_AX_UCODE_ADDRESS,
        RMGE01_AX_UCODE_BYTE_LEN,
    )
    .ok_or_else(|| {
        GameError::InvalidDspUcode("RMGE01 AX DSP ucode is outside the DOL image".to_owned())
    })?;
    let bytes = loaded.dol_bytes[range].to_vec();
    let digest = Sha256::digest(&bytes);
    let actual: String = digest.iter().map(|byte| format!("{byte:02x}")).collect();
    if actual != RMGE01_AX_UCODE_SHA256 {
        return Err(GameError::InvalidDspUcode(format!(
            "RMGE01 AX DSP ucode SHA-256 {actual} does not match {RMGE01_AX_UCODE_SHA256}"
        )));
    }
    Ok(bytes)
}

pub fn extract_dsp_ucode_candidate_in_dol(
    dol: &DolImage,
    data: &[u8],
    index: usize,
) -> Option<DspUcodePayload> {
    let scan = scan_dsp_ucode_candidates_in_dol(dol, data);
    let candidate = scan.candidates.into_iter().nth(index)?;
    let iram_range = guest_file_range(dol, candidate.iram_mmem_addr, candidate.iram_byte_len)?;
    let dram_bytes = if candidate.dram_byte_len == 0 {
        None
    } else {
        let dram_range = guest_file_range(dol, candidate.dram_mmem_addr, candidate.dram_byte_len)?;
        Some(data[dram_range].to_vec())
    };
    Some(DspUcodePayload {
        iram_bytes: data[iram_range].to_vec(),
        candidate,
        dram_bytes,
    })
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct UploadFields {
    iram_mmem_addr: u32,
    iram_byte_len: u32,
    iram_destination: u32,
    dram_mmem_addr: u32,
    dram_byte_len: u32,
    dram_destination: u32,
    dsp_vector: u16,
    dsp_resume_vector: u16,
}

fn candidate_from_upload_fields(
    dol: &DolImage,
    data: &[u8],
    descriptor_section: String,
    descriptor_address: u32,
    descriptor_format: DspUcodeDescriptorFormat,
    fields: UploadFields,
) -> Option<DspUcodeCandidate> {
    if fields.dsp_vector == 0 && fields.dsp_resume_vector == 0 {
        return None;
    }
    if !valid_dsp_code_vector(fields.dsp_vector) || !valid_dsp_code_vector(fields.dsp_resume_vector)
    {
        return None;
    }
    if !valid_dma_upload_extent(
        fields.iram_mmem_addr,
        fields.iram_byte_len,
        fields.iram_destination,
        DSP_MEMORY_BYTES,
    ) {
        return None;
    }
    let iram_range = guest_file_range(dol, fields.iram_mmem_addr, fields.iram_byte_len)?;
    let iram_bytes = &data[iram_range];
    if iram_bytes.iter().all(|byte| *byte == 0) {
        return None;
    }
    let iram_words = read_be_u16_words(iram_bytes);
    let iram_start_address = u16::try_from(fields.iram_destination / 2).ok()?;
    let (iram_audit, iram_audit_error) = match audit_dsp_program(iram_start_address, &iram_words) {
        Ok(audit) => (Some(audit), None),
        Err(error) => (None, Some(error.to_string())),
    };

    let dram_sha1 = if fields.dram_byte_len == 0 {
        None
    } else {
        if !valid_dma_upload_extent(
            fields.dram_mmem_addr,
            fields.dram_byte_len,
            fields.dram_destination,
            (DSP_DRAM_WORDS as u32) * 2,
        ) {
            return None;
        }
        let dram_range = guest_file_range(dol, fields.dram_mmem_addr, fields.dram_byte_len)?;
        Some(sha1_hex(&data[dram_range]))
    };

    Some(DspUcodeCandidate {
        descriptor_format,
        descriptor_address,
        descriptor_section,
        iram_mmem_addr: fields.iram_mmem_addr,
        iram_destination: fields.iram_destination,
        iram_byte_len: fields.iram_byte_len,
        iram_word_count: fields.iram_byte_len / 2,
        iram_sha1: sha1_hex(iram_bytes),
        iram_audit,
        iram_audit_error,
        dram_mmem_addr: fields.dram_mmem_addr,
        dram_destination: fields.dram_destination,
        dram_byte_len: fields.dram_byte_len,
        dram_sha1,
        dsp_vector: fields.dsp_vector,
        dsp_resume_vector: fields.dsp_resume_vector,
    })
}

fn valid_dsp_code_vector(vector: u16) -> bool {
    matches!(vector & 0xf000, 0x0000 | 0x8000)
}

fn valid_dma_upload_extent(
    source_address: u32,
    byte_len: u32,
    destination: u32,
    limit: u32,
) -> bool {
    byte_len != 0
        && (source_address & 0x1f) == 0
        && (byte_len & 0x1f) == 0
        && (destination & 0x1f) == 0
        && byte_len <= limit
        && destination
            .checked_add(byte_len)
            .is_some_and(|end| end <= limit)
}

fn read_descriptor_words<const N: usize>(bytes: &[u8]) -> [u32; N] {
    let mut words = [0; N];
    for (index, word) in words.iter_mut().enumerate() {
        let offset = index * 4;
        *word = u32::from_be_bytes(bytes[offset..offset + 4].try_into().expect("word-sized"));
    }
    words
}

fn read_be_u16_words(bytes: &[u8]) -> Vec<u16> {
    bytes
        .chunks_exact(2)
        .map(|chunk| u16::from_be_bytes([chunk[0], chunk[1]]))
        .collect()
}

fn guest_file_range(dol: &DolImage, address: u32, len: u32) -> Option<Range<usize>> {
    let end = address.checked_add(len)?;
    let section = dol.sections.iter().find(|section| {
        let section_end = section.address.checked_add(section.size);
        section.address <= address && section_end.is_some_and(|section_end| end <= section_end)
    })?;
    let start = section.file_offset + (address - section.address);
    Some(start as usize..(start + len) as usize)
}

fn sha1_hex(data: &[u8]) -> String {
    format!("{:x}", Sha1::digest(data))
}

#[cfg(test)]
mod tests {
    use super::*;

    const TEXT_FILE_OFFSET: usize = 0x100;
    const DATA_FILE_OFFSET: usize = 0x120;
    const TEXT_ADDRESS: u32 = 0x8000_4000;
    const DATA_ADDRESS: u32 = 0x8050_0000;

    fn synthetic_dol() -> Vec<u8> {
        let mut data = vec![0_u8; DATA_FILE_OFFSET + 0x100];
        write_u32(&mut data, 0x00, TEXT_FILE_OFFSET as u32);
        write_u32(&mut data, 0x48, TEXT_ADDRESS);
        write_u32(&mut data, 0x90, 4);
        write_u32(&mut data, 0x1C, DATA_FILE_OFFSET as u32);
        write_u32(&mut data, 0x64, DATA_ADDRESS);
        write_u32(&mut data, 0xAC, 0x100);
        write_u32(&mut data, 0xD8, 0x8060_0000);
        write_u32(&mut data, 0xDC, 0x1000);
        write_u32(&mut data, 0xE0, TEXT_ADDRESS);
        write_u32(&mut data, TEXT_FILE_OFFSET, 0x4E80_0020);
        data
    }

    fn write_u32(data: &mut [u8], offset: usize, value: u32) {
        data[offset..offset + 4].copy_from_slice(&value.to_be_bytes());
    }

    #[test]
    fn scans_synthetic_dsp_task_upload_descriptor() {
        let mut data = synthetic_dol();
        const IRAM_ADDR: u32 = DATA_ADDRESS + 0x40;
        const DRAM_ADDR: u32 = DATA_ADDRESS + 0x60;
        data[DATA_FILE_OFFSET + 0x40..DATA_FILE_OFFSET + 0x44]
            .copy_from_slice(&[0x00, 0x21, 0x00, 0x00]);
        data[DATA_FILE_OFFSET + 0x60..DATA_FILE_OFFSET + 0x62].copy_from_slice(&[0xab, 0xcd]);
        write_u32(&mut data, DATA_FILE_OFFSET, IRAM_ADDR);
        write_u32(&mut data, DATA_FILE_OFFSET + 4, 0x20);
        write_u32(&mut data, DATA_FILE_OFFSET + 8, 0);
        write_u32(&mut data, DATA_FILE_OFFSET + 12, DRAM_ADDR);
        write_u32(&mut data, DATA_FILE_OFFSET + 16, 0x20);
        write_u32(&mut data, DATA_FILE_OFFSET + 20, 0x100);
        write_u32(&mut data, DATA_FILE_OFFSET + 24, 0);
        write_u32(&mut data, DATA_FILE_OFFSET + 28, 0x10);

        let dol = DolImage::parse(&data).expect("synthetic DOL");
        let scan = scan_dsp_ucode_candidates_in_dol(&dol, &data);

        assert_eq!(scan.candidate_count, 1);
        let candidate = &scan.candidates[0];
        assert_eq!(
            candidate.descriptor_format,
            DspUcodeDescriptorFormat::ExpandedVectors
        );
        assert_eq!(candidate.descriptor_address, DATA_ADDRESS);
        assert_eq!(candidate.descriptor_section, "data0");
        assert_eq!(candidate.iram_mmem_addr, IRAM_ADDR);
        assert_eq!(candidate.iram_destination, 0);
        assert_eq!(candidate.iram_byte_len, 0x20);
        assert_eq!(candidate.iram_word_count, 0x10);
        assert_eq!(
            candidate.iram_sha1,
            sha1_hex(&data[DATA_FILE_OFFSET + 0x40..DATA_FILE_OFFSET + 0x60])
        );
        let audit = candidate.iram_audit.as_ref().expect("IRAM audit");
        assert_eq!(audit.start_address, 0);
        assert_eq!(audit.word_count, 0x10);
        assert_eq!(audit.instruction_count, 0x10);
        assert_eq!(audit.halt_instruction_count, 1);
        assert_eq!(candidate.iram_audit_error, None);
        assert_eq!(candidate.dram_mmem_addr, DRAM_ADDR);
        assert_eq!(candidate.dram_destination, 0x100);
        assert_eq!(candidate.dram_byte_len, 0x20);
        assert_eq!(
            candidate.dram_sha1.as_deref(),
            Some(sha1_hex(&data[DATA_FILE_OFFSET + 0x60..DATA_FILE_OFFSET + 0x80]).as_str())
        );
        assert_eq!(candidate.dsp_vector, 0);
        assert_eq!(candidate.dsp_resume_vector, 0x10);
    }

    #[test]
    fn extracts_selected_candidate_payload_without_serializing_bytes() {
        let mut data = synthetic_dol();
        const IRAM_ADDR: u32 = DATA_ADDRESS + 0x40;
        const DRAM_ADDR: u32 = DATA_ADDRESS + 0x60;
        data[DATA_FILE_OFFSET + 0x40..DATA_FILE_OFFSET + 0x44]
            .copy_from_slice(&[0x00, 0x21, 0x00, 0x00]);
        data[DATA_FILE_OFFSET + 0x60..DATA_FILE_OFFSET + 0x64]
            .copy_from_slice(&[0xab, 0xcd, 0xef, 0x01]);
        write_u32(&mut data, DATA_FILE_OFFSET, IRAM_ADDR);
        write_u32(&mut data, DATA_FILE_OFFSET + 4, 0x20);
        write_u32(&mut data, DATA_FILE_OFFSET + 8, 0);
        write_u32(&mut data, DATA_FILE_OFFSET + 12, DRAM_ADDR);
        write_u32(&mut data, DATA_FILE_OFFSET + 16, 0x20);
        write_u32(&mut data, DATA_FILE_OFFSET + 20, 0x100);
        write_u32(&mut data, DATA_FILE_OFFSET + 24, 0x0010);

        let dol = DolImage::parse(&data).expect("synthetic DOL");
        let payload = extract_dsp_ucode_candidate_in_dol(&dol, &data, 0).expect("selected payload");

        assert_eq!(payload.candidate.iram_mmem_addr, IRAM_ADDR);
        assert_eq!(
            &payload.iram_bytes[..4],
            &data[DATA_FILE_OFFSET + 0x40..DATA_FILE_OFFSET + 0x44]
        );
        assert_eq!(payload.iram_words_be()[0], 0x0021);
        assert_eq!(
            payload.dram_bytes.as_deref().map(|bytes| &bytes[..4]),
            Some(&data[DATA_FILE_OFFSET + 0x60..DATA_FILE_OFFSET + 0x64])
        );
        assert!(extract_dsp_ucode_candidate_in_dol(&dol, &data, 1).is_none());
    }

    #[test]
    fn scans_libogc_style_packed_vector_upload_descriptor() {
        let mut data = synthetic_dol();
        const IRAM_ADDR: u32 = DATA_ADDRESS + 0x40;
        const DRAM_ADDR: u32 = DATA_ADDRESS + 0x60;
        data[DATA_FILE_OFFSET + 0x40..DATA_FILE_OFFSET + 0x44]
            .copy_from_slice(&[0x00, 0x21, 0x00, 0x00]);
        data[DATA_FILE_OFFSET + 0x60..DATA_FILE_OFFSET + 0x62].copy_from_slice(&[0xab, 0xcd]);
        write_u32(&mut data, DATA_FILE_OFFSET, IRAM_ADDR);
        write_u32(&mut data, DATA_FILE_OFFSET + 4, 0x20);
        write_u32(&mut data, DATA_FILE_OFFSET + 8, 0);
        write_u32(&mut data, DATA_FILE_OFFSET + 12, DRAM_ADDR);
        write_u32(&mut data, DATA_FILE_OFFSET + 16, 0x20);
        write_u32(&mut data, DATA_FILE_OFFSET + 20, 0x100);
        write_u32(&mut data, DATA_FILE_OFFSET + 24, 0x0010);

        let dol = DolImage::parse(&data).expect("synthetic DOL");
        let scan = scan_dsp_ucode_candidates_in_dol(&dol, &data);

        assert_eq!(scan.candidate_count, 1);
        let candidate = &scan.candidates[0];
        assert_eq!(
            candidate.descriptor_format,
            DspUcodeDescriptorFormat::PackedVectors
        );
        assert_eq!(candidate.iram_mmem_addr, IRAM_ADDR);
        assert_eq!(candidate.iram_destination, 0);
        assert_eq!(candidate.iram_byte_len, 0x20);
        let audit = candidate.iram_audit.as_ref().expect("IRAM audit");
        assert_eq!(audit.start_address, 0);
        assert_eq!(audit.instruction_count, 0x10);
        assert_eq!(candidate.iram_audit_error, None);
        assert_eq!(candidate.dram_mmem_addr, DRAM_ADDR);
        assert_eq!(candidate.dram_destination, 0x100);
        assert_eq!(candidate.dram_byte_len, 0x20);
        assert_eq!(candidate.dsp_vector, 0);
        assert_eq!(candidate.dsp_resume_vector, 0x10);
    }

    #[test]
    fn reports_decode_errors_without_rejecting_upload_candidates() {
        let mut data = synthetic_dol();
        const IRAM_ADDR: u32 = DATA_ADDRESS + 0x40;
        data[DATA_FILE_OFFSET + 0x40..DATA_FILE_OFFSET + 0x42].copy_from_slice(&[0x03, 0xff]);
        write_u32(&mut data, DATA_FILE_OFFSET, IRAM_ADDR);
        write_u32(&mut data, DATA_FILE_OFFSET + 4, 0x20);
        write_u32(&mut data, DATA_FILE_OFFSET + 8, 0);
        write_u32(&mut data, DATA_FILE_OFFSET + 12, 0);
        write_u32(&mut data, DATA_FILE_OFFSET + 16, 0);
        write_u32(&mut data, DATA_FILE_OFFSET + 20, 0);
        write_u32(&mut data, DATA_FILE_OFFSET + 24, 0x0010);

        let dol = DolImage::parse(&data).expect("synthetic DOL");
        let scan = scan_dsp_ucode_candidates_in_dol(&dol, &data);

        assert_eq!(scan.candidate_count, 1);
        let candidate = &scan.candidates[0];
        assert_eq!(candidate.iram_audit, None);
        assert!(
            candidate
                .iram_audit_error
                .as_deref()
                .is_some_and(|error| error.contains("unsupported DSP opcode 0x03FF")),
            "unexpected audit error: {:?}",
            candidate.iram_audit_error
        );
    }

    #[test]
    fn rejects_unmapped_or_odd_length_descriptors() {
        let mut data = synthetic_dol();
        write_u32(&mut data, DATA_FILE_OFFSET, 0x9000_0000);
        write_u32(&mut data, DATA_FILE_OFFSET + 4, 3);
        write_u32(&mut data, DATA_FILE_OFFSET + 8, 0);

        let dol = DolImage::parse(&data).expect("synthetic DOL");
        let scan = scan_dsp_ucode_candidates_in_dol(&dol, &data);

        assert_eq!(scan.candidate_count, 0);
    }

    #[test]
    fn does_not_report_zero_filled_iram_payloads() {
        let mut data = synthetic_dol();
        write_u32(&mut data, DATA_FILE_OFFSET, DATA_ADDRESS + 0x40);
        write_u32(&mut data, DATA_FILE_OFFSET + 4, 0x20);
        write_u32(&mut data, DATA_FILE_OFFSET + 8, 0);

        let dol = DolImage::parse(&data).expect("synthetic DOL");
        let scan = scan_dsp_ucode_candidates_in_dol(&dol, &data);

        assert_eq!(scan.candidate_count, 0);
    }
}
