use std::{
    io,
    io::{Read, Seek, SeekFrom},
    mem::size_of,
};

use adler::adler32_slice;
use miniz_oxide::{inflate, inflate::core::inflate_flags};
use zerocopy::{little_endian::*, FromBytes, FromZeros, Immutable, IntoBytes, KnownLayout};
use zstd::zstd_safe::WriteBuf;

use crate::{
    io::{
        block::{Block, BlockIO, DiscStream, GCZ_MAGIC},
        MagicBytes,
    },
    static_assert,
    util::read::{read_box_slice, read_from},
    Compression, DiscMeta, Error, Format, PartitionInfo, Result, ResultContext,
};

/// GCZ header (little endian)
#[derive(Clone, Debug, PartialEq, FromBytes, IntoBytes, Immutable, KnownLayout)]
#[repr(C, align(4))]
struct GCZHeader {
    magic: MagicBytes,
    disc_type: U32,
    compressed_size: U64,
    disc_size: U64,
    block_size: U32,
    block_count: U32,
}

static_assert!(size_of::<GCZHeader>() == 32);

pub struct DiscIOGCZ {
    inner: Box<dyn DiscStream>,
    header: GCZHeader,
    block_map: Box<[U64]>,
    block_hashes: Box<[U32]>,
    block_buf: Box<[u8]>,
    data_offset: u64,
}

impl Clone for DiscIOGCZ {
    fn clone(&self) -> Self {
        Self {
            inner: self.inner.clone(),
            header: self.header.clone(),
            block_map: self.block_map.clone(),
            block_hashes: self.block_hashes.clone(),
            block_buf: <[u8]>::new_box_zeroed_with_elems(self.block_buf.len()).unwrap(),
            data_offset: self.data_offset,
        }
    }
}

impl DiscIOGCZ {
    pub fn new(mut inner: Box<dyn DiscStream>) -> Result<Box<Self>> {
        // Read header
        inner.seek(SeekFrom::Start(0)).context("Seeking to start")?;
        let header: GCZHeader = read_from(inner.as_mut()).context("Reading GCZ header")?;
        if header.magic != GCZ_MAGIC {
            return Err(Error::DiscFormat("Invalid GCZ magic".to_string()));
        }

        // Read block map and hashes
        let block_count = header.block_count.get();
        let block_map = read_box_slice(inner.as_mut(), block_count as usize)
            .context("Reading GCZ block map")?;
        let block_hashes = read_box_slice(inner.as_mut(), block_count as usize)
            .context("Reading GCZ block hashes")?;

        // header + block_count * (u64 + u32)
        let data_offset = size_of::<GCZHeader>() as u64 + block_count as u64 * 12;
        let block_buf = <[u8]>::new_box_zeroed_with_elems(header.block_size.get() as usize)?;
        Ok(Box::new(Self { inner, header, block_map, block_hashes, block_buf, data_offset }))
    }
}

impl BlockIO for DiscIOGCZ {
    fn read_block_internal(
        &mut self,
        out: &mut [u8],
        block: u32,
        _partition: Option<&PartitionInfo>,
    ) -> io::Result<Block> {
        if block >= self.header.block_count.get() {
            // Out of bounds
            return Ok(Block::Zero);
        }

        // Find block offset and size
        let mut file_offset = self.block_map[block as usize].get();
        let mut compressed = true;
        if file_offset & (1 << 63) != 0 {
            file_offset &= !(1 << 63);
            compressed = false;
        }
        let compressed_size =
            ((self.block_map.get(block as usize + 1).unwrap_or(&self.header.compressed_size).get()
                & !(1 << 63))
                - file_offset) as usize;
        if compressed_size > self.block_buf.len() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!(
                    "Compressed block size exceeds block size: {} > {}",
                    compressed_size,
                    self.block_buf.len()
                ),
            ));
        } else if !compressed && compressed_size != self.block_buf.len() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!(
                    "Uncompressed block size does not match block size: {} != {}",
                    compressed_size,
                    self.block_buf.len()
                ),
            ));
        }

        // Read block
        self.inner.seek(SeekFrom::Start(self.data_offset + file_offset))?;
        self.inner.read_exact(&mut self.block_buf[..compressed_size])?;

        // Verify block checksum
        let checksum = adler32_slice(&self.block_buf[..compressed_size]);
        let expected_checksum = self.block_hashes[block as usize].get();
        if checksum != expected_checksum {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!(
                    "Block checksum mismatch: {:#010x} != {:#010x}",
                    checksum, expected_checksum
                ),
            ));
        }

        if compressed {
            // Decompress block
            let mut decompressor = inflate::core::DecompressorOxide::new();
            let input = &self.block_buf[..compressed_size];
            let (status, in_size, out_size) = inflate::core::decompress(
                &mut decompressor,
                input,
                out,
                0,
                inflate_flags::TINFL_FLAG_PARSE_ZLIB_HEADER
                    | inflate_flags::TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF,
            );
            if status != inflate::TINFLStatus::Done
                || in_size != compressed_size
                || out_size != self.block_buf.len()
            {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidData,
                    format!(
                        "Deflate decompression failed: {:?} (in: {}, out: {})",
                        status, in_size, out_size
                    ),
                ));
            }
        } else {
            // Copy uncompressed block
            out.copy_from_slice(self.block_buf.as_slice());
        }
        Ok(Block::Raw)
    }

    fn block_size_internal(&self) -> u32 { self.header.block_size.get() }

    fn meta(&self) -> DiscMeta {
        DiscMeta {
            format: Format::Gcz,
            compression: Compression::Deflate,
            block_size: Some(self.header.block_size.get()),
            lossless: true,
            disc_size: Some(self.header.disc_size.get()),
            ..Default::default()
        }
    }
}
