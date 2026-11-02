use std::{
    io,
    io::{Read, Seek, SeekFrom},
    mem::size_of,
};

use zerocopy::{little_endian::*, FromBytes, Immutable, IntoBytes, KnownLayout};

use crate::{
    disc::SECTOR_SIZE,
    io::{
        block::{Block, BlockIO, DiscStream, PartitionInfo, CISO_MAGIC},
        nkit::NKitHeader,
        Format, MagicBytes,
    },
    static_assert,
    util::read::read_from,
    DiscMeta, Error, Result, ResultContext,
};

pub const CISO_MAP_SIZE: usize = SECTOR_SIZE - 8;

/// CISO header (little endian)
#[derive(Clone, Debug, PartialEq, FromBytes, IntoBytes, Immutable, KnownLayout)]
#[repr(C, align(4))]
struct CISOHeader {
    magic: MagicBytes,
    block_size: U32,
    block_present: [u8; CISO_MAP_SIZE],
}

static_assert!(size_of::<CISOHeader>() == SECTOR_SIZE);

#[derive(Clone)]
pub struct DiscIOCISO {
    inner: Box<dyn DiscStream>,
    header: CISOHeader,
    block_map: [u16; CISO_MAP_SIZE],
    nkit_header: Option<NKitHeader>,
}

impl DiscIOCISO {
    pub fn new(mut inner: Box<dyn DiscStream>) -> Result<Box<Self>> {
        // Read header
        inner.seek(SeekFrom::Start(0)).context("Seeking to start")?;
        let header: CISOHeader = read_from(inner.as_mut()).context("Reading CISO header")?;
        if header.magic != CISO_MAGIC {
            return Err(Error::DiscFormat("Invalid CISO magic".to_string()));
        }

        // Build block map
        let mut block_map = [0u16; CISO_MAP_SIZE];
        let mut block = 0u16;
        for (presence, out) in header.block_present.iter().zip(block_map.iter_mut()) {
            if *presence == 1 {
                *out = block;
                block += 1;
            } else {
                *out = u16::MAX;
            }
        }
        let file_size = SECTOR_SIZE as u64 + block as u64 * header.block_size.get() as u64;
        let len = inner.seek(SeekFrom::End(0)).context("Determining stream length")?;
        if file_size > len {
            return Err(Error::DiscFormat(format!(
                "CISO file size mismatch: expected at least {} bytes, got {}",
                file_size, len
            )));
        }

        // Read NKit header if present (after CISO data)
        let nkit_header = if len > file_size + 4 {
            inner.seek(SeekFrom::Start(file_size)).context("Seeking to NKit header")?;
            NKitHeader::try_read_from(inner.as_mut(), header.block_size.get(), true)
        } else {
            None
        };

        Ok(Box::new(Self { inner, header, block_map, nkit_header }))
    }
}

impl BlockIO for DiscIOCISO {
    fn read_block_internal(
        &mut self,
        out: &mut [u8],
        block: u32,
        _partition: Option<&PartitionInfo>,
    ) -> io::Result<Block> {
        if block >= CISO_MAP_SIZE as u32 {
            // Out of bounds
            return Ok(Block::Zero);
        }

        // Find the block in the map
        let phys_block = self.block_map[block as usize];
        if phys_block == u16::MAX {
            // Check if block is junk data
            if self.nkit_header.as_ref().and_then(|h| h.is_junk_block(block)).unwrap_or(false) {
                return Ok(Block::Junk);
            };

            // Otherwise, read zeroes
            return Ok(Block::Zero);
        }

        // Read block
        let file_offset = size_of::<CISOHeader>() as u64
            + phys_block as u64 * self.header.block_size.get() as u64;
        self.inner.seek(SeekFrom::Start(file_offset))?;
        self.inner.read_exact(out)?;
        Ok(Block::Raw)
    }

    fn block_size_internal(&self) -> u32 { self.header.block_size.get() }

    fn meta(&self) -> DiscMeta {
        let mut result = DiscMeta {
            format: Format::Ciso,
            block_size: Some(self.header.block_size.get()),
            ..Default::default()
        };
        if let Some(nkit_header) = &self.nkit_header {
            nkit_header.apply(&mut result);
        }
        result
    }
}
