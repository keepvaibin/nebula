use std::{
    io,
    io::{Read, Seek, SeekFrom},
    mem::size_of,
};

use zerocopy::{big_endian::*, FromBytes, Immutable, IntoBytes, KnownLayout};

use crate::{
    io::{
        block::{Block, BlockIO, DiscStream, PartitionInfo, WBFS_MAGIC},
        nkit::NKitHeader,
        DiscMeta, Format, MagicBytes,
    },
    util::read::{read_box_slice, read_from},
    Error, Result, ResultContext,
};

#[derive(Debug, Clone, PartialEq, FromBytes, IntoBytes, Immutable, KnownLayout)]
#[repr(C, align(4))]
struct WBFSHeader {
    magic: MagicBytes,
    num_sectors: U32,
    sector_size_shift: u8,
    block_size_shift: u8,
    _pad: [u8; 2],
}

impl WBFSHeader {
    fn sector_size(&self) -> u32 { 1 << self.sector_size_shift }

    fn block_size(&self) -> u32 { 1 << self.block_size_shift }

    fn max_blocks(&self) -> u32 { NUM_WII_SECTORS >> (self.block_size_shift - 15) }
}

const DISC_HEADER_SIZE: usize = 0x100;
const NUM_WII_SECTORS: u32 = 143432 * 2; // Double layer discs

#[derive(Clone)]
pub struct DiscIOWBFS {
    inner: Box<dyn DiscStream>,
    /// WBFS header
    header: WBFSHeader,
    /// Map of Wii LBAs to WBFS LBAs
    block_map: Box<[U16]>,
    /// Optional NKit header
    nkit_header: Option<NKitHeader>,
}

impl DiscIOWBFS {
    pub fn new(mut inner: Box<dyn DiscStream>) -> Result<Box<Self>> {
        inner.seek(SeekFrom::Start(0)).context("Seeking to start")?;
        let header: WBFSHeader = read_from(inner.as_mut()).context("Reading WBFS header")?;
        if header.magic != WBFS_MAGIC {
            return Err(Error::DiscFormat("Invalid WBFS magic".to_string()));
        }
        let file_len = inner.seek(SeekFrom::End(0)).context("Determining stream length")?;
        let expected_file_len = header.num_sectors.get() as u64 * header.sector_size() as u64;
        if file_len != expected_file_len {
            return Err(Error::DiscFormat(format!(
                "Invalid WBFS file size: {}, expected {}",
                file_len, expected_file_len
            )));
        }

        inner
            .seek(SeekFrom::Start(size_of::<WBFSHeader>() as u64))
            .context("Seeking to WBFS disc table")?;
        let disc_table: Box<[u8]> =
            read_box_slice(inner.as_mut(), header.sector_size() as usize - size_of::<WBFSHeader>())
                .context("Reading WBFS disc table")?;
        if disc_table[0] != 1 {
            return Err(Error::DiscFormat("WBFS doesn't contain a disc".to_string()));
        }
        if disc_table[1../*max_disc as usize*/].iter().any(|&x| x != 0) {
            return Err(Error::DiscFormat("Only single WBFS discs are supported".to_string()));
        }

        // Read WBFS LBA map
        inner
            .seek(SeekFrom::Start(header.sector_size() as u64 + DISC_HEADER_SIZE as u64))
            .context("Seeking to WBFS LBA table")?; // Skip header
        let block_map: Box<[U16]> = read_box_slice(inner.as_mut(), header.max_blocks() as usize)
            .context("Reading WBFS LBA table")?;

        // Read NKit header if present (always at 0x10000)
        inner.seek(SeekFrom::Start(0x10000)).context("Seeking to NKit header")?;
        let nkit_header = NKitHeader::try_read_from(inner.as_mut(), header.block_size(), true);

        Ok(Box::new(Self { inner, header, block_map, nkit_header }))
    }
}

impl BlockIO for DiscIOWBFS {
    fn read_block_internal(
        &mut self,
        out: &mut [u8],
        block: u32,
        _partition: Option<&PartitionInfo>,
    ) -> io::Result<Block> {
        let block_size = self.header.block_size();
        if block >= self.header.max_blocks() {
            return Ok(Block::Zero);
        }

        // Find the block in the map
        let phys_block = self.block_map[block as usize].get();
        if phys_block == 0 {
            // Check if block is junk data
            if self.nkit_header.as_ref().and_then(|h| h.is_junk_block(block)).unwrap_or(false) {
                return Ok(Block::Junk);
            }

            // Otherwise, read zeroes
            return Ok(Block::Zero);
        }

        // Read block
        let block_start = block_size as u64 * phys_block as u64;
        self.inner.seek(SeekFrom::Start(block_start))?;
        self.inner.read_exact(out)?;
        Ok(Block::Raw)
    }

    fn block_size_internal(&self) -> u32 { self.header.block_size() }

    fn meta(&self) -> DiscMeta {
        let mut result = DiscMeta {
            format: Format::Wbfs,
            block_size: Some(self.header.block_size()),
            ..Default::default()
        };
        if let Some(nkit_header) = &self.nkit_header {
            nkit_header.apply(&mut result);
        }
        result
    }
}
