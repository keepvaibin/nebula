use std::{
    fs, io,
    io::{Read, Seek},
    path::Path,
};

use dyn_clone::DynClone;
use zerocopy::transmute_ref;

use crate::{
    array_ref,
    disc::{
        hashes::HashTable,
        wii::{WiiPartitionHeader, HASHES_SIZE, SECTOR_DATA_SIZE},
        DiscHeader, PartitionHeader, PartitionKind, GCN_MAGIC, SECTOR_SIZE, WII_MAGIC,
    },
    io::{
        aes_decrypt, aes_encrypt, split::SplitFileReader, DiscMeta, Format, KeyBytes, MagicBytes,
    },
    util::{lfg::LaggedFibonacci, read::read_from},
    Error, Result, ResultContext,
};

/// Required trait bounds for reading disc images.
pub trait DiscStream: Read + Seek + DynClone + Send + Sync {}

impl<T> DiscStream for T where T: Read + Seek + DynClone + Send + Sync + ?Sized {}

dyn_clone::clone_trait_object!(DiscStream);

/// Block I/O trait for reading disc images.
pub trait BlockIO: DynClone + Send + Sync {
    /// Reads a block from the disc image.
    fn read_block_internal(
        &mut self,
        out: &mut [u8],
        block: u32,
        partition: Option<&PartitionInfo>,
    ) -> io::Result<Block>;

    /// Reads a full block from the disc image, combining smaller blocks if necessary.
    fn read_block(
        &mut self,
        out: &mut [u8],
        block: u32,
        partition: Option<&PartitionInfo>,
    ) -> io::Result<Block> {
        let block_size_internal = self.block_size_internal();
        let block_size = self.block_size();
        if block_size_internal == block_size {
            self.read_block_internal(out, block, partition)
        } else {
            let mut offset = 0usize;
            let mut result = None;
            let mut block_idx =
                ((block as u64 * block_size as u64) / block_size_internal as u64) as u32;
            while offset < block_size as usize {
                let block = self.read_block_internal(
                    &mut out[offset..offset + block_size_internal as usize],
                    block_idx,
                    partition,
                )?;
                if result.is_none() {
                    result = Some(block);
                } else if result != Some(block) {
                    if block == Block::Zero {
                        out[offset..offset + block_size_internal as usize].fill(0);
                    } else {
                        return Err(io::Error::new(
                            io::ErrorKind::InvalidData,
                            "Inconsistent block types in split block",
                        ));
                    }
                }
                offset += block_size_internal as usize;
                block_idx += 1;
            }
            Ok(result.unwrap_or_default())
        }
    }

    /// The format's block size in bytes. Can be smaller than the sector size (0x8000).
    fn block_size_internal(&self) -> u32;

    /// The block size used for processing. Must be a multiple of the sector size (0x8000).
    fn block_size(&self) -> u32 { self.block_size_internal().max(SECTOR_SIZE as u32) }

    /// Returns extra metadata included in the disc file format, if any.
    fn meta(&self) -> DiscMeta;
}

dyn_clone::clone_trait_object!(BlockIO);

/// Creates a new [`BlockIO`] instance from a stream.
pub fn new(mut stream: Box<dyn DiscStream>) -> Result<Box<dyn BlockIO>> {
    let io: Box<dyn BlockIO> = match detect(stream.as_mut()).context("Detecting file type")? {
        Some(Format::Iso) => crate::io::iso::DiscIOISO::new(stream)?,
        Some(Format::Ciso) => crate::io::ciso::DiscIOCISO::new(stream)?,
        Some(Format::Gcz) => {
            #[cfg(feature = "compress-zlib")]
            {
                crate::io::gcz::DiscIOGCZ::new(stream)?
            }
            #[cfg(not(feature = "compress-zlib"))]
            return Err(Error::DiscFormat("GCZ support is disabled".to_string()));
        }
        Some(Format::Nfs) => {
            return Err(Error::DiscFormat("NFS requires a filesystem path".to_string()))
        }
        Some(Format::Wbfs) => crate::io::wbfs::DiscIOWBFS::new(stream)?,
        Some(Format::Wia | Format::Rvz) => crate::io::wia::DiscIOWIA::new(stream)?,
        Some(Format::Tgc) => crate::io::tgc::DiscIOTGC::new(stream)?,
        None => return Err(Error::DiscFormat("Unknown disc format".to_string())),
    };
    check_block_size(io.as_ref())?;
    Ok(io)
}

/// Creates a new [`BlockIO`] instance from a filesystem path.
pub fn open(filename: &Path) -> Result<Box<dyn BlockIO>> {
    let path_result = fs::canonicalize(filename);
    if let Err(err) = path_result {
        return Err(Error::Io(format!("Failed to open {}", filename.display()), err));
    }
    let path = path_result.as_ref().unwrap();
    let meta = fs::metadata(path);
    if let Err(err) = meta {
        return Err(Error::Io(format!("Failed to open {}", filename.display()), err));
    }
    if !meta.unwrap().is_file() {
        return Err(Error::DiscFormat(format!("Input is not a file: {}", filename.display())));
    }
    let mut stream = Box::new(SplitFileReader::new(filename)?);
    let io: Box<dyn BlockIO> = match detect(stream.as_mut()).context("Detecting file type")? {
        Some(Format::Iso) => crate::io::iso::DiscIOISO::new(stream)?,
        Some(Format::Ciso) => crate::io::ciso::DiscIOCISO::new(stream)?,
        Some(Format::Gcz) => {
            #[cfg(feature = "compress-zlib")]
            {
                crate::io::gcz::DiscIOGCZ::new(stream)?
            }
            #[cfg(not(feature = "compress-zlib"))]
            return Err(Error::DiscFormat("GCZ support is disabled".to_string()));
        }
        Some(Format::Nfs) => match path.parent() {
            Some(parent) if parent.is_dir() => {
                crate::io::nfs::DiscIONFS::new(path.parent().unwrap())?
            }
            _ => {
                return Err(Error::DiscFormat("Failed to locate NFS parent directory".to_string()));
            }
        },
        Some(Format::Tgc) => crate::io::tgc::DiscIOTGC::new(stream)?,
        Some(Format::Wbfs) => crate::io::wbfs::DiscIOWBFS::new(stream)?,
        Some(Format::Wia | Format::Rvz) => crate::io::wia::DiscIOWIA::new(stream)?,
        None => return Err(Error::DiscFormat("Unknown disc format".to_string())),
    };
    check_block_size(io.as_ref())?;
    Ok(io)
}

pub const CISO_MAGIC: MagicBytes = *b"CISO";
pub const GCZ_MAGIC: MagicBytes = [0x01, 0xC0, 0x0B, 0xB1];
pub const NFS_MAGIC: MagicBytes = *b"EGGS";
pub const TGC_MAGIC: MagicBytes = [0xae, 0x0f, 0x38, 0xa2];
pub const WBFS_MAGIC: MagicBytes = *b"WBFS";
pub const WIA_MAGIC: MagicBytes = *b"WIA\x01";
pub const RVZ_MAGIC: MagicBytes = *b"RVZ\x01";

pub fn detect<R: Read + ?Sized>(stream: &mut R) -> io::Result<Option<Format>> {
    let data: [u8; 0x20] = match read_from(stream) {
        Ok(magic) => magic,
        Err(e) if e.kind() == io::ErrorKind::UnexpectedEof => return Ok(None),
        Err(e) => return Err(e),
    };
    let out = match *array_ref!(data, 0, 4) {
        CISO_MAGIC => Some(Format::Ciso),
        GCZ_MAGIC => Some(Format::Gcz),
        NFS_MAGIC => Some(Format::Nfs),
        TGC_MAGIC => Some(Format::Tgc),
        WBFS_MAGIC => Some(Format::Wbfs),
        WIA_MAGIC => Some(Format::Wia),
        RVZ_MAGIC => Some(Format::Rvz),
        _ if *array_ref!(data, 0x18, 4) == WII_MAGIC || *array_ref!(data, 0x1C, 4) == GCN_MAGIC => {
            Some(Format::Iso)
        }
        _ => None,
    };
    Ok(out)
}

fn check_block_size(io: &dyn BlockIO) -> Result<()> {
    if io.block_size_internal() < SECTOR_SIZE as u32
        && SECTOR_SIZE as u32 % io.block_size_internal() != 0
    {
        return Err(Error::DiscFormat(format!(
            "Sector size {} is not divisible by block size {}",
            SECTOR_SIZE,
            io.block_size_internal(),
        )));
    }
    if io.block_size() % SECTOR_SIZE as u32 != 0 {
        return Err(Error::DiscFormat(format!(
            "Block size {} is not a multiple of sector size {}",
            io.block_size(),
            SECTOR_SIZE
        )));
    }
    Ok(())
}

/// Wii partition information.
#[derive(Debug, Clone)]
pub struct PartitionInfo {
    /// The partition index.
    pub index: usize,
    /// The kind of disc partition.
    pub kind: PartitionKind,
    /// The start sector of the partition.
    pub start_sector: u32,
    /// The start sector of the partition's (encrypted) data.
    pub data_start_sector: u32,
    /// The end sector of the partition's (encrypted) data.
    pub data_end_sector: u32,
    /// The AES key for the partition, also known as the "title key".
    pub key: KeyBytes,
    /// The Wii partition header.
    pub header: Box<WiiPartitionHeader>,
    /// The disc header within the partition.
    pub disc_header: Box<DiscHeader>,
    /// The partition header within the partition.
    pub partition_header: Box<PartitionHeader>,
    /// The hash table for the partition, if rebuilt.
    pub hash_table: Option<HashTable>,
}

/// The block kind returned by [`BlockIO::read_block`].
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum Block {
    /// Raw data or encrypted Wii partition data
    Raw,
    /// Decrypted Wii partition data
    PartDecrypted {
        /// Whether the sector has its hash block intact
        has_hashes: bool,
    },
    /// Wii partition junk data
    Junk,
    /// All zeroes
    #[default]
    Zero,
}

impl Block {
    /// Decrypts the block's data (if necessary) and writes it to the output buffer.
    pub(crate) fn decrypt(
        self,
        out: &mut [u8; SECTOR_SIZE],
        data: &[u8],
        abs_sector: u32,
        partition: &PartitionInfo,
    ) -> io::Result<()> {
        let part_sector = abs_sector - partition.data_start_sector;
        match self {
            Block::Raw => {
                out.copy_from_slice(block_sector::<SECTOR_SIZE>(data, abs_sector)?);
                decrypt_sector(out, partition);
            }
            Block::PartDecrypted { has_hashes } => {
                out.copy_from_slice(block_sector::<SECTOR_SIZE>(data, abs_sector)?);
                if !has_hashes {
                    rebuild_hash_block(out, part_sector, partition);
                }
            }
            Block::Junk => {
                generate_junk(out, part_sector, Some(partition), &partition.disc_header);
                rebuild_hash_block(out, part_sector, partition);
            }
            Block::Zero => {
                out.fill(0);
                rebuild_hash_block(out, part_sector, partition);
            }
        }
        Ok(())
    }

    /// Encrypts the block's data (if necessary) and writes it to the output buffer.
    pub(crate) fn encrypt(
        self,
        out: &mut [u8; SECTOR_SIZE],
        data: &[u8],
        abs_sector: u32,
        partition: &PartitionInfo,
    ) -> io::Result<()> {
        let part_sector = abs_sector - partition.data_start_sector;
        match self {
            Block::Raw => {
                out.copy_from_slice(block_sector::<SECTOR_SIZE>(data, abs_sector)?);
            }
            Block::PartDecrypted { has_hashes } => {
                out.copy_from_slice(block_sector::<SECTOR_SIZE>(data, abs_sector)?);
                if !has_hashes {
                    rebuild_hash_block(out, part_sector, partition);
                }
                encrypt_sector(out, partition);
            }
            Block::Junk => {
                generate_junk(out, part_sector, Some(partition), &partition.disc_header);
                rebuild_hash_block(out, part_sector, partition);
                encrypt_sector(out, partition);
            }
            Block::Zero => {
                out.fill(0);
                rebuild_hash_block(out, part_sector, partition);
                encrypt_sector(out, partition);
            }
        }
        Ok(())
    }

    /// Copies the block's raw data to the output buffer.
    pub(crate) fn copy_raw(
        self,
        out: &mut [u8; SECTOR_SIZE],
        data: &[u8],
        abs_sector: u32,
        disc_header: &DiscHeader,
    ) -> io::Result<()> {
        match self {
            Block::Raw => {
                out.copy_from_slice(block_sector::<SECTOR_SIZE>(data, abs_sector)?);
            }
            Block::PartDecrypted { .. } => {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidData,
                    "Cannot copy decrypted data as raw",
                ));
            }
            Block::Junk => generate_junk(out, abs_sector, None, disc_header),
            Block::Zero => out.fill(0),
        }
        Ok(())
    }
}

#[inline(always)]
fn block_sector<const N: usize>(data: &[u8], sector_idx: u32) -> io::Result<&[u8; N]> {
    if data.len() % N != 0 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            format!("Expected block size {} to be a multiple of {}", data.len(), N),
        ));
    }
    let rel_sector = sector_idx % (data.len() / N) as u32;
    let offset = rel_sector as usize * N;
    data.get(offset..offset + N)
        .ok_or_else(|| {
            io::Error::new(
                io::ErrorKind::InvalidData,
                format!(
                    "Sector {} out of range (block size {}, sector size {})",
                    rel_sector,
                    data.len(),
                    N
                ),
            )
        })
        .map(|v| unsafe { &*(v as *const [u8] as *const [u8; N]) })
}

fn generate_junk(
    out: &mut [u8; SECTOR_SIZE],
    sector: u32,
    partition: Option<&PartitionInfo>,
    disc_header: &DiscHeader,
) {
    let (pos, offset) = if partition.is_some() {
        (sector as u64 * SECTOR_DATA_SIZE as u64, HASHES_SIZE)
    } else {
        (sector as u64 * SECTOR_SIZE as u64, 0)
    };
    out[..offset].fill(0);
    let mut lfg = LaggedFibonacci::default();
    lfg.fill_sector_chunked(
        &mut out[offset..],
        *array_ref![disc_header.game_id, 0, 4],
        disc_header.disc_num,
        pos,
    );
}

fn rebuild_hash_block(out: &mut [u8; SECTOR_SIZE], part_sector: u32, partition: &PartitionInfo) {
    let Some(hash_table) = partition.hash_table.as_ref() else {
        return;
    };
    let sector_idx = part_sector as usize;
    let h0_hashes: &[u8; 0x26C] =
        transmute_ref!(array_ref![hash_table.h0_hashes, sector_idx * 31, 31]);
    out[0..0x26C].copy_from_slice(h0_hashes);
    let h1_hashes: &[u8; 0xA0] =
        transmute_ref!(array_ref![hash_table.h1_hashes, sector_idx & !7, 8]);
    out[0x280..0x320].copy_from_slice(h1_hashes);
    let h2_hashes: &[u8; 0xA0] =
        transmute_ref!(array_ref![hash_table.h2_hashes, (sector_idx / 8) & !7, 8]);
    out[0x340..0x3E0].copy_from_slice(h2_hashes);
}

fn encrypt_sector(out: &mut [u8; SECTOR_SIZE], partition: &PartitionInfo) {
    aes_encrypt(&partition.key, [0u8; 16], &mut out[..HASHES_SIZE]);
    // Data IV from encrypted hash block
    let iv = *array_ref![out, 0x3D0, 16];
    aes_encrypt(&partition.key, iv, &mut out[HASHES_SIZE..]);
}

fn decrypt_sector(out: &mut [u8; SECTOR_SIZE], partition: &PartitionInfo) {
    // Data IV from encrypted hash block
    let iv = *array_ref![out, 0x3D0, 16];
    aes_decrypt(&partition.key, [0u8; 16], &mut out[..HASHES_SIZE]);
    aes_decrypt(&partition.key, iv, &mut out[HASHES_SIZE..]);
}
