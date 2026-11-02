use std::{
    io,
    io::{Read, Seek, SeekFrom},
};

use crate::{
    disc::SECTOR_SIZE,
    io::{
        block::{Block, BlockIO, DiscStream, PartitionInfo},
        Format,
    },
    DiscMeta, Result, ResultContext,
};

#[derive(Clone)]
pub struct DiscIOISO {
    inner: Box<dyn DiscStream>,
    stream_len: u64,
}

impl DiscIOISO {
    pub fn new(mut inner: Box<dyn DiscStream>) -> Result<Box<Self>> {
        let stream_len = inner.seek(SeekFrom::End(0)).context("Determining stream length")?;
        inner.seek(SeekFrom::Start(0)).context("Seeking to start")?;
        Ok(Box::new(Self { inner, stream_len }))
    }
}

impl BlockIO for DiscIOISO {
    fn read_block_internal(
        &mut self,
        out: &mut [u8],
        block: u32,
        _partition: Option<&PartitionInfo>,
    ) -> io::Result<Block> {
        let offset = block as u64 * SECTOR_SIZE as u64;
        if offset >= self.stream_len {
            // End of file
            return Ok(Block::Zero);
        }

        self.inner.seek(SeekFrom::Start(offset))?;
        if offset + SECTOR_SIZE as u64 > self.stream_len {
            // If the last block is not a full sector, fill the rest with zeroes
            let read = (self.stream_len - offset) as usize;
            self.inner.read_exact(&mut out[..read])?;
            out[read..].fill(0);
        } else {
            self.inner.read_exact(out)?;
        }
        Ok(Block::Raw)
    }

    fn block_size_internal(&self) -> u32 { SECTOR_SIZE as u32 }

    fn meta(&self) -> DiscMeta {
        DiscMeta {
            format: Format::Iso,
            lossless: true,
            disc_size: Some(self.stream_len),
            ..Default::default()
        }
    }
}
