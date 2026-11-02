use std::{
    io,
    io::{Read, Seek, SeekFrom},
    mem::size_of,
};

use zerocopy::{big_endian::U32, FromBytes, Immutable, IntoBytes, KnownLayout};

use crate::{
    disc::SECTOR_SIZE,
    io::{
        block::{Block, BlockIO, DiscStream, PartitionInfo, TGC_MAGIC},
        Format, MagicBytes,
    },
    util::read::{read_box_slice, read_from},
    DiscHeader, DiscMeta, Error, Node, PartitionHeader, Result, ResultContext,
};

/// TGC header (big endian)
#[derive(Clone, Debug, PartialEq, FromBytes, IntoBytes, Immutable, KnownLayout)]
#[repr(C, align(4))]
struct TGCHeader {
    /// Magic bytes
    magic: MagicBytes,
    /// TGC version
    version: U32,
    /// Offset to the start of the GCM header
    header_offset: U32,
    /// Size of the GCM header
    header_size: U32,
    /// Offset to the FST
    fst_offset: U32,
    /// Size of the FST
    fst_size: U32,
    /// Maximum size of the FST across discs
    fst_max_size: U32,
    /// Offset to the DOL
    dol_offset: U32,
    /// Size of the DOL
    dol_size: U32,
    /// Offset to user data
    user_offset: U32,
    /// Size of user data
    user_size: U32,
    /// Offset to the banner
    banner_offset: U32,
    /// Size of the banner
    banner_size: U32,
    /// Original user data offset in the GCM
    gcm_user_offset: U32,
}

#[derive(Clone)]
pub struct DiscIOTGC {
    inner: Box<dyn DiscStream>,
    stream_len: u64,
    header: TGCHeader,
    fst: Box<[u8]>,
}

impl DiscIOTGC {
    pub fn new(mut inner: Box<dyn DiscStream>) -> Result<Box<Self>> {
        let stream_len = inner.seek(SeekFrom::End(0)).context("Determining stream length")?;
        inner.seek(SeekFrom::Start(0)).context("Seeking to start")?;

        // Read header
        let header: TGCHeader = read_from(inner.as_mut()).context("Reading TGC header")?;
        if header.magic != TGC_MAGIC {
            return Err(Error::DiscFormat("Invalid TGC magic".to_string()));
        }

        // Read FST and adjust offsets
        inner
            .seek(SeekFrom::Start(header.fst_offset.get() as u64))
            .context("Seeking to TGC FST")?;
        let mut fst = read_box_slice(inner.as_mut(), header.fst_size.get() as usize)
            .context("Reading TGC FST")?;
        let (root_node, _) = Node::ref_from_prefix(&fst)
            .map_err(|_| Error::DiscFormat("Invalid TGC FST".to_string()))?;
        let node_count = root_node.length() as usize;
        let (nodes, _) = <[Node]>::mut_from_prefix_with_elems(&mut fst, node_count)
            .map_err(|_| Error::DiscFormat("Invalid TGC FST".to_string()))?;
        for node in nodes {
            if node.is_file() {
                node.offset = node.offset - header.gcm_user_offset
                    + (header.user_offset - header.header_offset);
            }
        }

        Ok(Box::new(Self { inner, stream_len, header, fst }))
    }
}

impl BlockIO for DiscIOTGC {
    fn read_block_internal(
        &mut self,
        out: &mut [u8],
        block: u32,
        _partition: Option<&PartitionInfo>,
    ) -> io::Result<Block> {
        let offset = self.header.header_offset.get() as u64 + block as u64 * SECTOR_SIZE as u64;
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

        // Adjust internal GCM header
        if block == 0 {
            let partition_header = PartitionHeader::mut_from_bytes(
                &mut out[size_of::<DiscHeader>()
                    ..size_of::<DiscHeader>() + size_of::<PartitionHeader>()],
            )
            .unwrap();
            partition_header.dol_offset = self.header.dol_offset - self.header.header_offset;
            partition_header.fst_offset = self.header.fst_offset - self.header.header_offset;
        }

        // Copy modified FST to output
        if offset + out.len() as u64 > self.header.fst_offset.get() as u64
            && offset < self.header.fst_offset.get() as u64 + self.header.fst_size.get() as u64
        {
            let out_offset = (self.header.fst_offset.get() as u64).saturating_sub(offset) as usize;
            let fst_offset = offset.saturating_sub(self.header.fst_offset.get() as u64) as usize;
            let copy_len =
                (out.len() - out_offset).min(self.header.fst_size.get() as usize - fst_offset);
            out[out_offset..out_offset + copy_len]
                .copy_from_slice(&self.fst[fst_offset..fst_offset + copy_len]);
        }

        Ok(Block::Raw)
    }

    fn block_size_internal(&self) -> u32 { SECTOR_SIZE as u32 }

    fn meta(&self) -> DiscMeta {
        DiscMeta {
            format: Format::Tgc,
            lossless: true,
            disc_size: Some(self.stream_len - self.header.header_offset.get() as u64),
            ..Default::default()
        }
    }
}
