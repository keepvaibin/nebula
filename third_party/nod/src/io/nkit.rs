use std::{
    io,
    io::{Read, Seek, SeekFrom},
};

use crate::{
    disc::DL_DVD_SIZE,
    io::MagicBytes,
    util::read::{read_from, read_u16_be, read_u32_be, read_u64_be, read_vec},
    DiscMeta,
};

#[allow(unused)]
#[repr(u16)]
enum NKitHeaderFlags {
    Size = 0x1,
    Crc32 = 0x2,
    Md5 = 0x4,
    Sha1 = 0x8,
    Xxhash64 = 0x10,
    Key = 0x20,
    Encrypted = 0x40,
    ExtraData = 0x80,
    IndexFile = 0x100,
}

const NKIT_HEADER_V1_FLAGS: u16 = NKitHeaderFlags::Crc32 as u16
    | NKitHeaderFlags::Md5 as u16
    | NKitHeaderFlags::Sha1 as u16
    | NKitHeaderFlags::Xxhash64 as u16;

const fn calc_header_size(version: u8, flags: u16, key_len: u32) -> usize {
    let mut size = 8;
    if version >= 2 {
        // header size + flags
        size += 4;
    }
    if flags & NKitHeaderFlags::Size as u16 != 0 {
        size += 8;
    }
    if flags & NKitHeaderFlags::Crc32 as u16 != 0 {
        size += 4;
    }
    if flags & NKitHeaderFlags::Md5 as u16 != 0 {
        size += 16;
    }
    if flags & NKitHeaderFlags::Sha1 as u16 != 0 {
        size += 20;
    }
    if flags & NKitHeaderFlags::Xxhash64 as u16 != 0 {
        size += 8;
    }
    if flags & NKitHeaderFlags::Key as u16 != 0 {
        size += key_len as usize + 2;
    }
    size
}

#[allow(unused)]
#[derive(Debug, Clone)]
pub struct NKitHeader {
    pub version: u8,
    pub flags: u16,
    pub size: Option<u64>,
    pub crc32: Option<u32>,
    pub md5: Option<[u8; 16]>,
    pub sha1: Option<[u8; 20]>,
    pub xxhash64: Option<u64>,
    /// Bitstream of blocks that are junk data
    pub junk_bits: Option<Vec<u8>>,
    pub block_size: u32,
}

const VERSION_PREFIX: [u8; 7] = *b"NKIT  v";

impl NKitHeader {
    pub fn try_read_from<R>(reader: &mut R, block_size: u32, has_junk_bits: bool) -> Option<Self>
    where R: Read + Seek + ?Sized {
        let magic: MagicBytes = read_from(reader).ok()?;
        if magic == *b"NKIT" {
            reader.seek(SeekFrom::Current(-4)).ok()?;
            match NKitHeader::read_from(reader, block_size, has_junk_bits) {
                Ok(header) => Some(header),
                Err(e) => {
                    log::warn!("Failed to read NKit header: {}", e);
                    None
                }
            }
        } else {
            None
        }
    }

    pub fn read_from<R>(reader: &mut R, block_size: u32, has_junk_bits: bool) -> io::Result<Self>
    where R: Read + ?Sized {
        let version_string: [u8; 8] = read_from(reader)?;
        if version_string[0..7] != VERSION_PREFIX
            || version_string[7] < b'1'
            || version_string[7] > b'9'
        {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "Invalid NKit header version string",
            ));
        }
        let version = version_string[7] - b'0';
        let header_size = match version {
            1 => calc_header_size(version, NKIT_HEADER_V1_FLAGS, 0) as u16,
            2 => read_u16_be(reader)?,
            _ => {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidData,
                    format!("Unsupported NKit header version: {}", version),
                ));
            }
        };

        let mut remaining_header_size = header_size as usize - 8;
        if version >= 2 {
            // We read the header size already
            remaining_header_size -= 2;
        }
        let header_bytes = read_vec(reader, remaining_header_size)?;
        let mut inner = &header_bytes[..];

        let flags = if version == 1 { NKIT_HEADER_V1_FLAGS } else { read_u16_be(&mut inner)? };
        let size = (flags & NKitHeaderFlags::Size as u16 != 0)
            .then(|| read_u64_be(&mut inner))
            .transpose()?;
        let crc32 = (flags & NKitHeaderFlags::Crc32 as u16 != 0)
            .then(|| read_u32_be(&mut inner))
            .transpose()?;
        let md5 = (flags & NKitHeaderFlags::Md5 as u16 != 0)
            .then(|| read_from::<[u8; 16], _>(&mut inner))
            .transpose()?;
        let sha1 = (flags & NKitHeaderFlags::Sha1 as u16 != 0)
            .then(|| read_from::<[u8; 20], _>(&mut inner))
            .transpose()?;
        let xxhash64 = (flags & NKitHeaderFlags::Xxhash64 as u16 != 0)
            .then(|| read_u64_be(&mut inner))
            .transpose()?;

        let junk_bits = if has_junk_bits {
            let n = DL_DVD_SIZE.div_ceil(block_size as u64).div_ceil(8);
            Some(read_vec(reader, n as usize)?)
        } else {
            None
        };

        Ok(Self { version, flags, size, crc32, md5, sha1, xxhash64, junk_bits, block_size })
    }

    pub fn is_junk_block(&self, block: u32) -> Option<bool> {
        self.junk_bits
            .as_ref()
            .and_then(|v| v.get((block / 8) as usize))
            .map(|&b| b & (1 << (7 - (block & 7))) != 0)
    }

    pub fn apply(&self, meta: &mut DiscMeta) {
        meta.needs_hash_recovery |= self.junk_bits.is_some();
        meta.lossless |= self.size.is_some() && self.junk_bits.is_some();
        meta.disc_size = meta.disc_size.or(self.size);
        meta.crc32 = self.crc32;
        meta.md5 = self.md5;
        meta.sha1 = self.sha1;
        meta.xxhash64 = self.xxhash64;
    }
}
