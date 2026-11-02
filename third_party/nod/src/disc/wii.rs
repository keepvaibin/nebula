use std::{
    ffi::CStr,
    io,
    io::{BufRead, Read, Seek, SeekFrom},
    mem::size_of,
};

use sha1::{Digest, Sha1};
use zerocopy::{big_endian::*, FromBytes, FromZeros, Immutable, IntoBytes, KnownLayout};

use super::{
    gcn::{read_part_meta, PartitionGC},
    DiscHeader, FileStream, Node, PartitionBase, PartitionMeta, SECTOR_SIZE,
};
use crate::{
    array_ref,
    disc::streams::OwnedFileStream,
    io::{
        aes_decrypt,
        block::{Block, BlockIO, PartitionInfo},
        KeyBytes,
    },
    static_assert,
    util::{div_rem, read::read_box_slice},
    Error, OpenOptions, Result, ResultContext,
};

/// Size in bytes of the hashes block in a Wii disc sector
pub(crate) const HASHES_SIZE: usize = 0x400;

/// Size in bytes of the data block in a Wii disc sector (excluding hashes)
pub(crate) const SECTOR_DATA_SIZE: usize = SECTOR_SIZE - HASHES_SIZE; // 0x7C00

/// Size of the disc region info (region.bin)
pub const REGION_SIZE: usize = 0x20;

/// Offset of the disc region info
pub const REGION_OFFSET: u64 = 0x4E000;

// ppki (Retail)
const RVL_CERT_ISSUER_PPKI_TICKET: &str = "Root-CA00000001-XS00000003";

/// Nebula's copy: this library holds no key. The program sets the retail common key the player gave before it opens a Wii
/// disc image.
static COMMON_KEY: std::sync::RwLock<Option<KeyBytes>> = std::sync::RwLock::new(None);

/// The retail common key, used by every disc image opened after this.
pub fn set_common_key(key: KeyBytes) {
    *COMMON_KEY.write().unwrap_or_else(|poisoned| poisoned.into_inner()) = Some(key);
}

#[derive(Debug, PartialEq, FromBytes, IntoBytes, Immutable, KnownLayout)]
#[repr(C, align(4))]
pub(crate) struct WiiPartEntry {
    pub(crate) offset: U32,
    pub(crate) kind: U32,
}

static_assert!(size_of::<WiiPartEntry>() == 8);

impl WiiPartEntry {
    pub(crate) fn offset(&self) -> u64 { (self.offset.get() as u64) << 2 }
}

pub(crate) const WII_PART_GROUP_OFF: u64 = 0x40000;

#[derive(Debug, PartialEq, FromBytes, IntoBytes, Immutable, KnownLayout)]
#[repr(C, align(4))]
pub(crate) struct WiiPartGroup {
    pub(crate) part_count: U32,
    pub(crate) part_entry_off: U32,
}

static_assert!(size_of::<WiiPartGroup>() == 8);

impl WiiPartGroup {
    pub(crate) fn part_entry_off(&self) -> u64 { (self.part_entry_off.get() as u64) << 2 }
}

/// Signed blob header
#[derive(Debug, Clone, PartialEq, FromBytes, IntoBytes, Immutable, KnownLayout)]
#[repr(C, align(4))]
pub struct SignedHeader {
    /// Signature type, always 0x00010001 (RSA-2048)
    pub sig_type: U32,
    /// RSA-2048 signature
    pub sig: [u8; 256],
    _pad: [u8; 60],
}

static_assert!(size_of::<SignedHeader>() == 0x140);

/// Ticket limit
#[derive(Debug, Clone, PartialEq, Default, FromBytes, IntoBytes, Immutable, KnownLayout)]
#[repr(C, align(4))]
pub struct TicketLimit {
    /// Limit type
    pub limit_type: U32,
    /// Maximum value for the limit
    pub max_value: U32,
}

static_assert!(size_of::<TicketLimit>() == 8);

/// Wii ticket
#[derive(Debug, Clone, PartialEq, FromBytes, IntoBytes, Immutable, KnownLayout)]
#[repr(C, align(4))]
pub struct Ticket {
    /// Signed blob header
    pub header: SignedHeader,
    /// Signature issuer
    pub sig_issuer: [u8; 64],
    /// ECDH data
    pub ecdh: [u8; 60],
    /// Ticket format version
    pub version: u8,
    _pad1: U16,
    /// Title key (encrypted)
    pub title_key: KeyBytes,
    _pad2: u8,
    /// Ticket ID
    pub ticket_id: [u8; 8],
    /// Console ID
    pub console_id: [u8; 4],
    /// Title ID
    pub title_id: [u8; 8],
    _pad3: U16,
    /// Ticket title version
    pub ticket_title_version: U16,
    /// Permitted titles mask
    pub permitted_titles_mask: U32,
    /// Permit mask
    pub permit_mask: U32,
    /// Title export allowed
    pub title_export_allowed: u8,
    /// Common key index
    pub common_key_idx: u8,
    _pad4: [u8; 48],
    /// Content access permissions
    pub content_access_permissions: [u8; 64],
    _pad5: [u8; 2],
    /// Ticket limits
    pub limits: [TicketLimit; 8],
}

static_assert!(size_of::<Ticket>() == 0x2A4);

impl Ticket {
    /// Decrypts the ticket title key using the appropriate common key
    #[allow(clippy::missing_inline_in_public_items)]
    pub fn decrypt_title_key(&self) -> Result<KeyBytes> {
        let mut iv: KeyBytes = [0; 16];
        iv[..8].copy_from_slice(&self.title_id);
        let cert_issuer_ticket =
            CStr::from_bytes_until_nul(&self.sig_issuer).ok().and_then(|c| c.to_str().ok());
        if cert_issuer_ticket != Some(RVL_CERT_ISSUER_PPKI_TICKET) || self.common_key_idx != 0 {
            return Err(Error::DiscFormat(format!(
                "not a retail Wii disc (issuer {:?}, common key {})",
                cert_issuer_ticket, self.common_key_idx
            )));
        }
        let Some(common_key) = *COMMON_KEY.read().unwrap_or_else(|poisoned| poisoned.into_inner()) else {
            return Err(Error::DiscFormat("no Wii common key was given".to_string()));
        };
        let common_key = &common_key;
        let mut title_key = self.title_key;
        aes_decrypt(common_key, iv, &mut title_key);
        Ok(title_key)
    }
}

/// Title metadata header
#[derive(Debug, Clone, PartialEq, FromBytes, IntoBytes, Immutable, KnownLayout)]
#[repr(C, align(4))]
pub struct TmdHeader {
    /// Signed blob header
    pub header: SignedHeader,
    /// Signature issuer
    pub sig_issuer: [u8; 64],
    /// Version
    pub version: u8,
    /// CA CRL version
    pub ca_crl_version: u8,
    /// Signer CRL version
    pub signer_crl_version: u8,
    /// Is vWii title
    pub is_vwii: u8,
    /// IOS ID
    pub ios_id: [u8; 8],
    /// Title ID
    pub title_id: [u8; 8],
    /// Title type
    pub title_type: u32,
    /// Group ID
    pub group_id: U16,
    _pad1: [u8; 2],
    /// Region
    pub region: U16,
    /// Ratings
    pub ratings: KeyBytes,
    _pad2: [u8; 12],
    /// IPC mask
    pub ipc_mask: [u8; 12],
    _pad3: [u8; 18],
    /// Access flags
    pub access_flags: U32,
    /// Title version
    pub title_version: U16,
    /// Number of contents
    pub num_contents: U16,
    /// Boot index
    pub boot_idx: U16,
    /// Minor version (unused)
    pub minor_version: U16,
}

static_assert!(size_of::<TmdHeader>() == 0x1E4);

pub const H3_TABLE_SIZE: usize = 0x18000;

#[derive(Debug, Clone, PartialEq, FromBytes, IntoBytes, Immutable, KnownLayout)]
#[repr(C, align(4))]
pub struct WiiPartitionHeader {
    pub ticket: Ticket,
    tmd_size: U32,
    tmd_off: U32,
    cert_chain_size: U32,
    cert_chain_off: U32,
    h3_table_off: U32,
    data_off: U32,
    data_size: U32,
}

static_assert!(size_of::<WiiPartitionHeader>() == 0x2C0);

impl WiiPartitionHeader {
    pub fn tmd_size(&self) -> u64 { self.tmd_size.get() as u64 }

    pub fn tmd_off(&self) -> u64 { (self.tmd_off.get() as u64) << 2 }

    pub fn cert_chain_size(&self) -> u64 { self.cert_chain_size.get() as u64 }

    pub fn cert_chain_off(&self) -> u64 { (self.cert_chain_off.get() as u64) << 2 }

    pub fn h3_table_off(&self) -> u64 { (self.h3_table_off.get() as u64) << 2 }

    pub fn h3_table_size(&self) -> u64 { H3_TABLE_SIZE as u64 }

    pub fn data_off(&self) -> u64 { (self.data_off.get() as u64) << 2 }

    pub fn data_size(&self) -> u64 { (self.data_size.get() as u64) << 2 }
}

pub struct PartitionWii {
    io: Box<dyn BlockIO>,
    partition: PartitionInfo,
    block: Block,
    block_buf: Box<[u8]>,
    block_idx: u32,
    sector_buf: Box<[u8; SECTOR_SIZE]>,
    sector: u32,
    pos: u64,
    verify: bool,
    raw_tmd: Box<[u8]>,
    raw_cert_chain: Box<[u8]>,
    raw_h3_table: Box<[u8]>,
}

impl Clone for PartitionWii {
    fn clone(&self) -> Self {
        Self {
            io: self.io.clone(),
            partition: self.partition.clone(),
            block: Block::default(),
            block_buf: <[u8]>::new_box_zeroed_with_elems(self.block_buf.len()).unwrap(),
            block_idx: u32::MAX,
            sector_buf: <[u8; SECTOR_SIZE]>::new_box_zeroed().unwrap(),
            sector: u32::MAX,
            pos: 0,
            verify: self.verify,
            raw_tmd: self.raw_tmd.clone(),
            raw_cert_chain: self.raw_cert_chain.clone(),
            raw_h3_table: self.raw_h3_table.clone(),
        }
    }
}

impl PartitionWii {
    pub fn new(
        inner: Box<dyn BlockIO>,
        disc_header: Box<DiscHeader>,
        partition: &PartitionInfo,
        options: &OpenOptions,
    ) -> Result<Box<Self>> {
        let block_size = inner.block_size();
        let mut reader = PartitionGC::new(inner, disc_header)?;

        // Read TMD, cert chain, and H3 table
        let offset = partition.start_sector as u64 * SECTOR_SIZE as u64;
        reader
            .seek(SeekFrom::Start(offset + partition.header.tmd_off()))
            .context("Seeking to TMD offset")?;
        let raw_tmd: Box<[u8]> = read_box_slice(&mut reader, partition.header.tmd_size() as usize)
            .context("Reading TMD")?;
        reader
            .seek(SeekFrom::Start(offset + partition.header.cert_chain_off()))
            .context("Seeking to cert chain offset")?;
        let raw_cert_chain: Box<[u8]> =
            read_box_slice(&mut reader, partition.header.cert_chain_size() as usize)
                .context("Reading cert chain")?;
        reader
            .seek(SeekFrom::Start(offset + partition.header.h3_table_off()))
            .context("Seeking to H3 table offset")?;
        let raw_h3_table: Box<[u8]> =
            read_box_slice(&mut reader, H3_TABLE_SIZE).context("Reading H3 table")?;

        Ok(Box::new(Self {
            io: reader.into_inner(),
            partition: partition.clone(),
            block: Block::default(),
            block_buf: <[u8]>::new_box_zeroed_with_elems(block_size as usize)?,
            block_idx: u32::MAX,
            sector_buf: <[u8; SECTOR_SIZE]>::new_box_zeroed()?,
            sector: u32::MAX,
            pos: 0,
            verify: options.validate_hashes,
            raw_tmd,
            raw_cert_chain,
            raw_h3_table,
        }))
    }
}

impl BufRead for PartitionWii {
    fn fill_buf(&mut self) -> io::Result<&[u8]> {
        let part_sector = (self.pos / SECTOR_DATA_SIZE as u64) as u32;
        let abs_sector = self.partition.data_start_sector + part_sector;
        if abs_sector >= self.partition.data_end_sector {
            return Ok(&[]);
        }
        let block_idx =
            (abs_sector as u64 * SECTOR_SIZE as u64 / self.block_buf.len() as u64) as u32;

        // Read new block if necessary
        if block_idx != self.block_idx {
            self.block =
                self.io.read_block(self.block_buf.as_mut(), block_idx, Some(&self.partition))?;
            self.block_idx = block_idx;
        }

        // Decrypt sector if necessary
        if abs_sector != self.sector {
            self.block.decrypt(
                self.sector_buf.as_mut(),
                self.block_buf.as_ref(),
                abs_sector,
                &self.partition,
            )?;
            if self.verify {
                verify_hashes(self.sector_buf.as_ref(), part_sector, self.raw_h3_table.as_ref())?;
            }
            self.sector = abs_sector;
        }

        let offset = (self.pos % SECTOR_DATA_SIZE as u64) as usize;
        Ok(&self.sector_buf[HASHES_SIZE + offset..])
    }

    #[inline]
    fn consume(&mut self, amt: usize) { self.pos += amt as u64; }
}

impl Read for PartitionWii {
    #[inline]
    fn read(&mut self, out: &mut [u8]) -> io::Result<usize> {
        let buf = self.fill_buf()?;
        let len = buf.len().min(out.len());
        out[..len].copy_from_slice(&buf[..len]);
        self.consume(len);
        Ok(len)
    }
}

impl Seek for PartitionWii {
    fn seek(&mut self, pos: SeekFrom) -> io::Result<u64> {
        self.pos = match pos {
            SeekFrom::Start(v) => v,
            SeekFrom::End(_) => {
                return Err(io::Error::new(
                    io::ErrorKind::Unsupported,
                    "WiiPartitionReader: SeekFrom::End is not supported".to_string(),
                ));
            }
            SeekFrom::Current(v) => self.pos.saturating_add_signed(v),
        };
        Ok(self.pos)
    }
}

#[inline(always)]
pub(crate) fn as_digest(slice: &[u8; 20]) -> digest::Output<Sha1> { (*slice).into() }

fn verify_hashes(buf: &[u8; SECTOR_SIZE], part_sector: u32, h3_table: &[u8]) -> io::Result<()> {
    let (cluster, sector) = div_rem(part_sector as usize, 8);
    let (group, sub_group) = div_rem(cluster, 8);

    // H0 hashes
    for i in 0..31 {
        let mut hash = Sha1::new();
        hash.update(array_ref![buf, (i + 1) * 0x400, 0x400]);
        let expected = as_digest(array_ref![buf, i * 20, 20]);
        let output = hash.finalize();
        if output != expected {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("Invalid H0 hash! (block {:?}) {:x}\n\texpected {:x}", i, output, expected),
            ));
        }
    }

    // H1 hash
    {
        let mut hash = Sha1::new();
        hash.update(array_ref![buf, 0, 0x26C]);
        let expected = as_digest(array_ref![buf, 0x280 + sector * 20, 20]);
        let output = hash.finalize();
        if output != expected {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!(
                    "Invalid H1 hash! (subgroup {:?}) {:x}\n\texpected {:x}",
                    sector, output, expected
                ),
            ));
        }
    }

    // H2 hash
    {
        let mut hash = Sha1::new();
        hash.update(array_ref![buf, 0x280, 0xA0]);
        let expected = as_digest(array_ref![buf, 0x340 + sub_group * 20, 20]);
        let output = hash.finalize();
        if output != expected {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!(
                    "Invalid H2 hash! (group {:?}) {:x}\n\texpected {:x}",
                    sub_group, output, expected
                ),
            ));
        }
    }

    // H3 hash
    {
        let mut hash = Sha1::new();
        hash.update(array_ref![buf, 0x340, 0xA0]);
        let expected = as_digest(array_ref![h3_table, group * 20, 20]);
        let output = hash.finalize();
        if output != expected {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("Invalid H3 hash! {:x}\n\texpected {:x}", output, expected),
            ));
        }
    }

    Ok(())
}

impl PartitionBase for PartitionWii {
    fn meta(&mut self) -> Result<Box<PartitionMeta>> {
        self.seek(SeekFrom::Start(0)).context("Seeking to partition header")?;
        let mut meta = read_part_meta(self, true)?;
        meta.raw_ticket = Some(Box::from(self.partition.header.ticket.as_bytes()));
        meta.raw_tmd = Some(self.raw_tmd.clone());
        meta.raw_cert_chain = Some(self.raw_cert_chain.clone());
        meta.raw_h3_table = Some(self.raw_h3_table.clone());
        Ok(meta)
    }

    fn open_file(&mut self, node: Node) -> io::Result<FileStream> {
        if !node.is_file() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "Node is not a file".to_string(),
            ));
        }
        FileStream::new(self, node.offset(true), node.length())
    }

    fn into_open_file(self: Box<Self>, node: Node) -> io::Result<OwnedFileStream> {
        if !node.is_file() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "Node is not a file".to_string(),
            ));
        }
        OwnedFileStream::new(self, node.offset(true), node.length())
    }
}
