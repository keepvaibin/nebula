use crate::{
    game::{load_verified_game, GameError},
    DolImage, DolSectionKind, RMGE01_DOL_SHA1, RMGE01_GAME_ID,
};
use serde::Serialize;
use sha2::{Digest, Sha256};
use std::path::Path;
use thiserror::Error;

pub const RMGE01_BOOT_IMAGE_VERSION: u32 = 1;
pub const RMGE01_BOOT_IMAGE_HEADER_SIZE: usize = 160;
pub const RMGE01_BOOT_IMAGE_SECTION_RECORD_SIZE: usize = 24;
pub const RMGE01_BOOT_IMAGE_DIGEST_OFFSET: usize = 112;
pub const RMGE01_BOOT_IMAGE_DIGEST_SIZE: usize = 32;
pub const RMGE01_BOOT_IMAGE_SECTION_COUNT: usize = 10;
pub const RMGE01_BOOT_IMAGE_PAYLOAD_SIZE: usize = 6_283_008;
pub const RMGE01_BOOT_IMAGE_FILE_SIZE: usize = 6_283_408;

const BOOT_IMAGE_MAGIC: &[u8; 8] = b"GRBTIMG\0";
const BOOT_IMAGE_GAME_ID: &[u8; 8] = b"RMGE01\0\0";

pub const RMGE01_BOOT_IMAGE_DIGEST_SHA256: &str =
    "c60c7f413fd67af505c1fb75a75b83f463d10645c0fa98f747a32a84c225f226";
pub const RMGE01_BOOT_IMAGE_FILE_SHA256: &str =
    "55315b5f2b6ae6296d1021d6fd7b38c036ae8c83a9f72f74d008d9b52f265219";

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum BootImageSectionKind {
    Text,
    Data,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct BootImageSection {
    pub kind: BootImageSectionKind,
    pub index: u32,
    pub guest_address: u32,
    pub size: u32,
    pub image_offset: u64,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct BootImageManifest {
    pub version: u32,
    pub game_id: String,
    pub main_dol_sha1: String,
    pub entry_point: u32,
    pub bss_address: u32,
    pub bss_size: u32,
    pub payload_offset: u64,
    pub payload_size: u64,
    pub image_size: u64,
    pub digest_sha256: String,
    pub sections: Vec<BootImageSection>,
}

#[derive(Clone, Debug)]
pub struct BootImageArtifact {
    pub bytes: Vec<u8>,
    pub manifest: BootImageManifest,
    pub file_sha256: String,
}

#[derive(Debug, Error)]
pub enum BootImageError {
    #[error(transparent)]
    Game(#[from] GameError),
    #[error("boot image size arithmetic overflowed")]
    SizeOverflow,
    #[error("boot image section {section} is outside the validated DOL bytes")]
    SectionOutsideDol { section: String },
    #[error("boot image is truncated: expected at least {expected} bytes, got {actual}")]
    Truncated { expected: usize, actual: usize },
    #[error("boot image magic is invalid")]
    BadMagic,
    #[error("unsupported boot image version {actual}; expected {expected}")]
    WrongVersion { actual: u32, expected: u32 },
    #[error("boot image {field} is invalid")]
    InvalidField { field: &'static str },
    #[error("boot image identity is invalid")]
    WrongIdentity,
    #[error("boot image section table is not canonical")]
    NonCanonicalSections,
    #[error("boot image initialized guest ranges overlap")]
    OverlappingGuestSections,
    #[error("boot image entry point is outside a text section")]
    EntryOutsideText,
    #[error("boot image SHA-256 mismatch: expected {expected}, got {actual}")]
    HashMismatch { expected: String, actual: String },
}

pub fn build_rmge01_boot_image(input: &Path) -> Result<BootImageArtifact, BootImageError> {
    let loaded = load_verified_game(input)?;
    let artifact = build_boot_image_from_dol(&loaded.dol, &loaded.dol_bytes)?;
    if artifact.manifest.digest_sha256 != RMGE01_BOOT_IMAGE_DIGEST_SHA256 {
        return Err(BootImageError::HashMismatch {
            expected: RMGE01_BOOT_IMAGE_DIGEST_SHA256.to_owned(),
            actual: artifact.manifest.digest_sha256,
        });
    }
    if artifact.file_sha256 != RMGE01_BOOT_IMAGE_FILE_SHA256 {
        return Err(BootImageError::HashMismatch {
            expected: RMGE01_BOOT_IMAGE_FILE_SHA256.to_owned(),
            actual: artifact.file_sha256,
        });
    }
    Ok(artifact)
}

fn build_boot_image_from_dol(
    dol: &DolImage,
    dol_bytes: &[u8],
) -> Result<BootImageArtifact, BootImageError> {
    let section_count =
        u32::try_from(dol.sections.len()).map_err(|_| BootImageError::SizeOverflow)?;
    let records_size = dol
        .sections
        .len()
        .checked_mul(RMGE01_BOOT_IMAGE_SECTION_RECORD_SIZE)
        .ok_or(BootImageError::SizeOverflow)?;
    let payload_offset = RMGE01_BOOT_IMAGE_HEADER_SIZE
        .checked_add(records_size)
        .ok_or(BootImageError::SizeOverflow)?;
    let payload_size = dol.sections.iter().try_fold(0_usize, |total, section| {
        total
            .checked_add(section.size as usize)
            .ok_or(BootImageError::SizeOverflow)
    })?;
    let image_size = payload_offset
        .checked_add(payload_size)
        .ok_or(BootImageError::SizeOverflow)?;
    let mut bytes = vec![0_u8; image_size];

    bytes[0..8].copy_from_slice(BOOT_IMAGE_MAGIC);
    write_u32(&mut bytes, 8, RMGE01_BOOT_IMAGE_VERSION);
    write_u32(
        &mut bytes,
        12,
        u32::try_from(RMGE01_BOOT_IMAGE_HEADER_SIZE).map_err(|_| BootImageError::SizeOverflow)?,
    );
    write_u32(
        &mut bytes,
        16,
        u32::try_from(RMGE01_BOOT_IMAGE_SECTION_RECORD_SIZE)
            .map_err(|_| BootImageError::SizeOverflow)?,
    );
    write_u32(&mut bytes, 20, section_count);
    write_u32(&mut bytes, 24, dol.entry_point);
    write_u32(&mut bytes, 28, dol.bss_address);
    write_u32(&mut bytes, 32, dol.bss_size);
    write_u32(&mut bytes, 36, 0);
    write_u64(
        &mut bytes,
        40,
        u64::try_from(payload_offset).map_err(|_| BootImageError::SizeOverflow)?,
    );
    write_u64(
        &mut bytes,
        48,
        u64::try_from(payload_size).map_err(|_| BootImageError::SizeOverflow)?,
    );
    write_u64(
        &mut bytes,
        56,
        u64::try_from(image_size).map_err(|_| BootImageError::SizeOverflow)?,
    );
    bytes[64..72].copy_from_slice(BOOT_IMAGE_GAME_ID);
    bytes[72..112].copy_from_slice(RMGE01_DOL_SHA1.as_bytes());

    let mut next_payload = payload_offset;
    for (record_index, section) in dol.sections.iter().enumerate() {
        let source = dol_bytes.get(section.file_range()).ok_or_else(|| {
            BootImageError::SectionOutsideDol {
                section: section.name.clone(),
            }
        })?;
        let record_offset =
            RMGE01_BOOT_IMAGE_HEADER_SIZE + record_index * RMGE01_BOOT_IMAGE_SECTION_RECORD_SIZE;
        write_u32(&mut bytes, record_offset, section.address);
        write_u32(&mut bytes, record_offset + 4, section.size);
        write_u64(
            &mut bytes,
            record_offset + 8,
            u64::try_from(next_payload).map_err(|_| BootImageError::SizeOverflow)?,
        );
        write_u32(
            &mut bytes,
            record_offset + 16,
            match section.kind {
                DolSectionKind::Text => 0,
                DolSectionKind::Data => 1,
            },
        );
        write_u32(
            &mut bytes,
            record_offset + 20,
            u32::try_from(section.index).map_err(|_| BootImageError::SizeOverflow)?,
        );
        let payload_end = next_payload
            .checked_add(source.len())
            .ok_or(BootImageError::SizeOverflow)?;
        bytes[next_payload..payload_end].copy_from_slice(source);
        next_payload = payload_end;
    }
    debug_assert_eq!(next_payload, image_size);

    let digest = Sha256::digest(&bytes);
    bytes[RMGE01_BOOT_IMAGE_DIGEST_OFFSET
        ..RMGE01_BOOT_IMAGE_DIGEST_OFFSET + RMGE01_BOOT_IMAGE_DIGEST_SIZE]
        .copy_from_slice(&digest);
    let manifest = parse_boot_image_impl(&bytes, false)?;
    let file_sha256 = sha256_hex(&bytes);
    Ok(BootImageArtifact {
        bytes,
        manifest,
        file_sha256,
    })
}

pub fn parse_rmge01_boot_image(bytes: &[u8]) -> Result<BootImageManifest, BootImageError> {
    let manifest = parse_boot_image_impl(bytes, true)?;
    let file_sha256 = sha256_hex(bytes);
    if file_sha256 != RMGE01_BOOT_IMAGE_FILE_SHA256 {
        return Err(BootImageError::HashMismatch {
            expected: RMGE01_BOOT_IMAGE_FILE_SHA256.to_owned(),
            actual: file_sha256,
        });
    }
    Ok(manifest)
}

fn parse_boot_image_impl(
    bytes: &[u8],
    require_canonical_digest: bool,
) -> Result<BootImageManifest, BootImageError> {
    if bytes.len() < RMGE01_BOOT_IMAGE_HEADER_SIZE {
        return Err(BootImageError::Truncated {
            expected: RMGE01_BOOT_IMAGE_HEADER_SIZE,
            actual: bytes.len(),
        });
    }
    if &bytes[0..8] != BOOT_IMAGE_MAGIC {
        return Err(BootImageError::BadMagic);
    }
    let version = read_u32(bytes, 8)?;
    if version != RMGE01_BOOT_IMAGE_VERSION {
        return Err(BootImageError::WrongVersion {
            actual: version,
            expected: RMGE01_BOOT_IMAGE_VERSION,
        });
    }
    if read_u32(bytes, 12)? as usize != RMGE01_BOOT_IMAGE_HEADER_SIZE {
        return Err(BootImageError::InvalidField {
            field: "header_size",
        });
    }
    if read_u32(bytes, 16)? as usize != RMGE01_BOOT_IMAGE_SECTION_RECORD_SIZE {
        return Err(BootImageError::InvalidField {
            field: "section_record_size",
        });
    }
    let section_count = read_u32(bytes, 20)? as usize;
    if section_count == 0 || section_count > 18 {
        return Err(BootImageError::InvalidField {
            field: "section_count",
        });
    }
    if require_canonical_digest && section_count != RMGE01_BOOT_IMAGE_SECTION_COUNT {
        return Err(BootImageError::InvalidField {
            field: "section_count",
        });
    }
    if read_u32(bytes, 36)? != 0 || bytes[144..160].iter().any(|byte| *byte != 0) {
        return Err(BootImageError::InvalidField { field: "reserved" });
    }
    if &bytes[64..72] != BOOT_IMAGE_GAME_ID || &bytes[72..112] != RMGE01_DOL_SHA1.as_bytes() {
        return Err(BootImageError::WrongIdentity);
    }

    let records_size = section_count
        .checked_mul(RMGE01_BOOT_IMAGE_SECTION_RECORD_SIZE)
        .ok_or(BootImageError::SizeOverflow)?;
    let expected_payload_offset = RMGE01_BOOT_IMAGE_HEADER_SIZE
        .checked_add(records_size)
        .ok_or(BootImageError::SizeOverflow)?;
    let payload_offset =
        usize::try_from(read_u64(bytes, 40)?).map_err(|_| BootImageError::SizeOverflow)?;
    let payload_size =
        usize::try_from(read_u64(bytes, 48)?).map_err(|_| BootImageError::SizeOverflow)?;
    let image_size =
        usize::try_from(read_u64(bytes, 56)?).map_err(|_| BootImageError::SizeOverflow)?;
    if payload_offset != expected_payload_offset {
        return Err(BootImageError::InvalidField {
            field: "payload_offset",
        });
    }
    if payload_offset.checked_add(payload_size) != Some(image_size) || image_size != bytes.len() {
        return Err(BootImageError::InvalidField {
            field: "image_size",
        });
    }
    if require_canonical_digest
        && (payload_size != RMGE01_BOOT_IMAGE_PAYLOAD_SIZE
            || image_size != RMGE01_BOOT_IMAGE_FILE_SIZE)
    {
        return Err(BootImageError::InvalidField {
            field: "canonical_size",
        });
    }

    let expected_digest = &bytes[RMGE01_BOOT_IMAGE_DIGEST_OFFSET
        ..RMGE01_BOOT_IMAGE_DIGEST_OFFSET + RMGE01_BOOT_IMAGE_DIGEST_SIZE];
    let mut digest_input = bytes.to_vec();
    digest_input[RMGE01_BOOT_IMAGE_DIGEST_OFFSET
        ..RMGE01_BOOT_IMAGE_DIGEST_OFFSET + RMGE01_BOOT_IMAGE_DIGEST_SIZE]
        .fill(0);
    let actual_digest = Sha256::digest(&digest_input);
    if expected_digest != &actual_digest[..] {
        return Err(BootImageError::HashMismatch {
            expected: hex(expected_digest),
            actual: hex(&actual_digest),
        });
    }
    let digest_sha256 = hex(expected_digest);
    if require_canonical_digest && digest_sha256 != RMGE01_BOOT_IMAGE_DIGEST_SHA256 {
        return Err(BootImageError::HashMismatch {
            expected: RMGE01_BOOT_IMAGE_DIGEST_SHA256.to_owned(),
            actual: digest_sha256,
        });
    }

    let mut sections = Vec::with_capacity(section_count);
    let mut next_payload = payload_offset;
    let mut previous_key: Option<(u32, u32)> = None;
    for record_index in 0..section_count {
        let record_offset =
            RMGE01_BOOT_IMAGE_HEADER_SIZE + record_index * RMGE01_BOOT_IMAGE_SECTION_RECORD_SIZE;
        let guest_address = read_u32(bytes, record_offset)?;
        let size = read_u32(bytes, record_offset + 4)?;
        let image_offset = read_u64(bytes, record_offset + 8)?;
        let kind_raw = read_u32(bytes, record_offset + 16)?;
        let index = read_u32(bytes, record_offset + 20)?;
        let kind = match kind_raw {
            0 if index < 7 => BootImageSectionKind::Text,
            1 if index < 11 => BootImageSectionKind::Data,
            _ => return Err(BootImageError::NonCanonicalSections),
        };
        let key = (kind_raw, index);
        if size == 0 || previous_key.is_some_and(|previous| previous >= key) {
            return Err(BootImageError::NonCanonicalSections);
        }
        if kind == BootImageSectionKind::Text && (guest_address % 4 != 0 || size % 4 != 0) {
            return Err(BootImageError::NonCanonicalSections);
        }
        let image_offset_usize =
            usize::try_from(image_offset).map_err(|_| BootImageError::SizeOverflow)?;
        if image_offset_usize != next_payload {
            return Err(BootImageError::NonCanonicalSections);
        }
        next_payload = next_payload
            .checked_add(size as usize)
            .ok_or(BootImageError::SizeOverflow)?;
        if next_payload > bytes.len() || guest_address.checked_add(size).is_none() {
            return Err(BootImageError::NonCanonicalSections);
        }
        sections.push(BootImageSection {
            kind,
            index,
            guest_address,
            size,
            image_offset,
        });
        previous_key = Some(key);
    }
    if next_payload != bytes.len() {
        return Err(BootImageError::NonCanonicalSections);
    }
    for (position, first) in sections.iter().enumerate() {
        let first_end = u64::from(first.guest_address) + u64::from(first.size);
        for second in sections.iter().skip(position + 1) {
            let second_end = u64::from(second.guest_address) + u64::from(second.size);
            if u64::from(first.guest_address) < second_end
                && u64::from(second.guest_address) < first_end
            {
                return Err(BootImageError::OverlappingGuestSections);
            }
        }
    }

    let entry_point = read_u32(bytes, 24)?;
    if entry_point % 4 != 0
        || !sections.iter().any(|section| {
            section.kind == BootImageSectionKind::Text
                && entry_point >= section.guest_address
                && u64::from(entry_point)
                    < u64::from(section.guest_address) + u64::from(section.size)
        })
    {
        return Err(BootImageError::EntryOutsideText);
    }
    let bss_address = read_u32(bytes, 28)?;
    let bss_size = read_u32(bytes, 32)?;
    if bss_address.checked_add(bss_size).is_none() {
        return Err(BootImageError::InvalidField { field: "bss" });
    }

    Ok(BootImageManifest {
        version,
        game_id: RMGE01_GAME_ID.to_owned(),
        main_dol_sha1: RMGE01_DOL_SHA1.to_owned(),
        entry_point,
        bss_address,
        bss_size,
        payload_offset: payload_offset as u64,
        payload_size: payload_size as u64,
        image_size: image_size as u64,
        digest_sha256,
        sections,
    })
}

fn read_u32(bytes: &[u8], offset: usize) -> Result<u32, BootImageError> {
    let value = bytes
        .get(offset..offset + 4)
        .ok_or(BootImageError::Truncated {
            expected: offset + 4,
            actual: bytes.len(),
        })?;
    Ok(u32::from_le_bytes(value.try_into().expect("four bytes")))
}

fn read_u64(bytes: &[u8], offset: usize) -> Result<u64, BootImageError> {
    let value = bytes
        .get(offset..offset + 8)
        .ok_or(BootImageError::Truncated {
            expected: offset + 8,
            actual: bytes.len(),
        })?;
    Ok(u64::from_le_bytes(value.try_into().expect("eight bytes")))
}

fn write_u32(bytes: &mut [u8], offset: usize, value: u32) {
    bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
}

fn write_u64(bytes: &mut [u8], offset: usize, value: u64) {
    bytes[offset..offset + 8].copy_from_slice(&value.to_le_bytes());
}

fn sha256_hex(bytes: &[u8]) -> String {
    hex(&Sha256::digest(bytes))
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|byte| format!("{byte:02x}")).collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::DolSection;

    fn synthetic() -> (DolImage, Vec<u8>) {
        let dol = DolImage {
            entry_point: 0x8000_4000,
            bss_address: 0x8001_0000,
            bss_size: 0x1000,
            sections: vec![
                DolSection {
                    name: "text0".to_owned(),
                    kind: DolSectionKind::Text,
                    index: 0,
                    file_offset: 0x100,
                    address: 0x8000_4000,
                    size: 8,
                },
                DolSection {
                    name: "data0".to_owned(),
                    kind: DolSectionKind::Data,
                    index: 0,
                    file_offset: 0x108,
                    address: 0x8000_8000,
                    size: 4,
                },
            ],
        };
        let mut bytes = vec![0_u8; 0x10c];
        bytes[0x100..0x10c]
            .copy_from_slice(&[0x38, 0x60, 0x00, 0x01, 0x4e, 0x80, 0x00, 0x20, 1, 2, 3, 4]);
        (dol, bytes)
    }

    fn rehash(image: &mut [u8]) {
        image[RMGE01_BOOT_IMAGE_DIGEST_OFFSET
            ..RMGE01_BOOT_IMAGE_DIGEST_OFFSET + RMGE01_BOOT_IMAGE_DIGEST_SIZE]
            .fill(0);
        let digest = Sha256::digest(&*image);
        image[RMGE01_BOOT_IMAGE_DIGEST_OFFSET
            ..RMGE01_BOOT_IMAGE_DIGEST_OFFSET + RMGE01_BOOT_IMAGE_DIGEST_SIZE]
            .copy_from_slice(&digest);
    }

    #[test]
    fn deterministic_image_contains_only_section_payloads() {
        let (dol, bytes) = synthetic();
        let first = build_boot_image_from_dol(&dol, &bytes).expect("first image");
        let second = build_boot_image_from_dol(&dol, &bytes).expect("second image");
        assert_eq!(first.bytes, second.bytes);
        assert_eq!(first.manifest.sections.len(), 2);
        assert_eq!(first.manifest.payload_size, 12);
        let payload = first.manifest.payload_offset as usize;
        assert_eq!(&first.bytes[payload..], &bytes[0x100..0x10c]);
        assert_eq!(first.bytes.len(), RMGE01_BOOT_IMAGE_HEADER_SIZE + 48 + 12);
        assert_ne!(first.manifest.payload_size, u64::from(dol.bss_size));
    }

    #[test]
    fn rejects_payload_mutation() {
        let (dol, bytes) = synthetic();
        let mut image = build_boot_image_from_dol(&dol, &bytes)
            .expect("image")
            .bytes;
        let last = image.len() - 1;
        image[last] ^= 1;
        assert!(matches!(
            parse_boot_image_impl(&image, false),
            Err(BootImageError::HashMismatch { .. })
        ));
    }

    #[test]
    fn rejects_section_metadata_mutation_even_with_original_payload() {
        let (dol, bytes) = synthetic();
        let mut image = build_boot_image_from_dol(&dol, &bytes)
            .expect("image")
            .bytes;
        write_u32(&mut image, RMGE01_BOOT_IMAGE_HEADER_SIZE, 0x8000_5000);
        assert!(matches!(
            parse_boot_image_impl(&image, false),
            Err(BootImageError::HashMismatch { .. })
        ));
    }

    #[test]
    fn rejects_noncanonical_section_table_after_rehash() {
        let (dol, bytes) = synthetic();
        let mut image = build_boot_image_from_dol(&dol, &bytes)
            .expect("image")
            .bytes;
        write_u64(
            &mut image,
            RMGE01_BOOT_IMAGE_HEADER_SIZE + RMGE01_BOOT_IMAGE_SECTION_RECORD_SIZE + 8,
            (RMGE01_BOOT_IMAGE_HEADER_SIZE + 2 * RMGE01_BOOT_IMAGE_SECTION_RECORD_SIZE) as u64,
        );
        rehash(&mut image);
        assert!(matches!(
            parse_boot_image_impl(&image, false),
            Err(BootImageError::NonCanonicalSections)
        ));
    }

    #[test]
    fn rejects_wrong_version_before_accepting_payload() {
        let (dol, bytes) = synthetic();
        let mut image = build_boot_image_from_dol(&dol, &bytes)
            .expect("image")
            .bytes;
        write_u32(&mut image, 8, RMGE01_BOOT_IMAGE_VERSION + 1);
        assert!(matches!(
            parse_boot_image_impl(&image, false),
            Err(BootImageError::WrongVersion { .. })
        ));
    }

    #[test]
    fn rejects_overlapping_guest_sections_even_after_rehash() {
        let (dol, bytes) = synthetic();
        let mut image = build_boot_image_from_dol(&dol, &bytes)
            .expect("image")
            .bytes;
        let second_record = RMGE01_BOOT_IMAGE_HEADER_SIZE + RMGE01_BOOT_IMAGE_SECTION_RECORD_SIZE;
        write_u32(&mut image, second_record, 0x8000_4004);
        rehash(&mut image);
        assert!(matches!(
            parse_boot_image_impl(&image, false),
            Err(BootImageError::OverlappingGuestSections)
        ));
    }

    #[test]
    fn rejects_unaligned_entry_inside_text_even_after_rehash() {
        let (dol, bytes) = synthetic();
        let mut image = build_boot_image_from_dol(&dol, &bytes)
            .expect("image")
            .bytes;
        write_u32(&mut image, 24, dol.entry_point + 1);
        rehash(&mut image);
        assert!(matches!(
            parse_boot_image_impl(&image, false),
            Err(BootImageError::EntryOutsideText)
        ));
    }

    #[test]
    fn public_parser_rejects_a_self_consistent_non_rmge01_image() {
        let (dol, bytes) = synthetic();
        let image = build_boot_image_from_dol(&dol, &bytes)
            .expect("image")
            .bytes;
        assert!(matches!(
            parse_rmge01_boot_image(&image),
            Err(BootImageError::InvalidField {
                field: "section_count"
            })
        ));
    }
}
