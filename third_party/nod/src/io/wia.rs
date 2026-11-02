use std::{
    io,
    io::{Read, Seek, SeekFrom},
    mem::size_of,
};

use zerocopy::{big_endian::*, FromBytes, Immutable, IntoBytes, KnownLayout};

use crate::{
    disc::{
        hashes::hash_bytes,
        wii::{HASHES_SIZE, SECTOR_DATA_SIZE},
        SECTOR_SIZE,
    },
    io::{
        block::{Block, BlockIO, DiscStream, PartitionInfo, RVZ_MAGIC, WIA_MAGIC},
        nkit::NKitHeader,
        Compression, Format, HashBytes, KeyBytes, MagicBytes,
    },
    static_assert,
    util::{
        lfg::LaggedFibonacci,
        read::{read_box_slice, read_from, read_u16_be, read_vec},
        take_seek::TakeSeekExt,
    },
    DiscMeta, Error, Result, ResultContext,
};

/// This struct is stored at offset 0x0 and is 0x48 bytes long. The wit source code says its format
/// will never be changed.
#[derive(Clone, Debug, PartialEq, FromBytes, IntoBytes, Immutable, KnownLayout)]
#[repr(C, align(4))]
pub struct WIAFileHeader {
    pub magic: MagicBytes,
    /// The WIA format version.
    ///
    /// A short note from the wit source code about how version numbers are encoded:
    ///
    /// ```c
    /// //-----------------------------------------------------
    /// // Format of version number: AABBCCDD = A.BB | A.BB.CC
    /// // If D != 0x00 && D != 0xff => append: 'beta' D
    /// //-----------------------------------------------------
    /// ```
    pub version: U32,
    /// If the reading program supports the version of WIA indicated here, it can read the file.
    ///
    /// [version](Self::version) can be higher than `version_compatible`.
    pub version_compatible: U32,
    /// The size of the [WIADisc] struct.
    pub disc_size: U32,
    /// The SHA-1 hash of the [WIADisc] struct.
    ///
    /// The number of bytes to hash is determined by [disc_size](Self::disc_size).
    pub disc_hash: HashBytes,
    /// The original size of the ISO.
    pub iso_file_size: U64,
    /// The size of this file.
    pub wia_file_size: U64,
    /// The SHA-1 hash of this struct, up to but not including `file_head_hash` itself.
    pub file_head_hash: HashBytes,
}

static_assert!(size_of::<WIAFileHeader>() == 0x48);

impl WIAFileHeader {
    pub fn validate(&self) -> Result<()> {
        // Check magic
        if self.magic != WIA_MAGIC && self.magic != RVZ_MAGIC {
            return Err(Error::DiscFormat(format!("Invalid WIA/RVZ magic: {:#X?}", self.magic)));
        }
        // Check file head hash
        let bytes = self.as_bytes();
        verify_hash(&bytes[..bytes.len() - size_of::<HashBytes>()], &self.file_head_hash)?;
        // Check version compatibility
        if self.version_compatible.get() < 0x30000 {
            return Err(Error::DiscFormat(format!(
                "WIA/RVZ version {:#X} is not supported",
                self.version_compatible
            )));
        }
        Ok(())
    }

    pub fn is_rvz(&self) -> bool { self.magic == RVZ_MAGIC }
}

/// Disc kind
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DiscKind {
    /// GameCube disc
    GameCube,
    /// Wii disc
    Wii,
}

impl TryFrom<u32> for DiscKind {
    type Error = Error;

    fn try_from(value: u32) -> Result<Self> {
        match value {
            1 => Ok(Self::GameCube),
            2 => Ok(Self::Wii),
            v => Err(Error::DiscFormat(format!("Invalid disc type {}", v))),
        }
    }
}

/// Compression type
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WIACompression {
    /// No compression.
    None,
    /// (WIA only) See [WIASegment]
    Purge,
    /// BZIP2 compression
    Bzip2,
    /// LZMA compression
    Lzma,
    /// LZMA2 compression
    Lzma2,
    /// (RVZ only) Zstandard compression
    Zstandard,
}

impl TryFrom<u32> for WIACompression {
    type Error = Error;

    fn try_from(value: u32) -> Result<Self> {
        match value {
            0 => Ok(Self::None),
            1 => Ok(Self::Purge),
            2 => Ok(Self::Bzip2),
            3 => Ok(Self::Lzma),
            4 => Ok(Self::Lzma2),
            5 => Ok(Self::Zstandard),
            v => Err(Error::DiscFormat(format!("Invalid compression type {}", v))),
        }
    }
}

const DISC_HEAD_SIZE: usize = 0x80;

/// This struct is stored at offset 0x48, immediately after [WIAFileHeader].
#[derive(Clone, Debug, PartialEq, FromBytes, IntoBytes, Immutable, KnownLayout)]
#[repr(C, align(4))]
pub struct WIADisc {
    /// The disc type. (1 = GameCube, 2 = Wii)
    pub disc_type: U32,
    /// The compression type.
    pub compression: U32,
    /// The compression level used by the compressor.
    ///
    /// The possible values are compressor-specific.
    ///
    /// RVZ only:
    /// > This is signed (instead of unsigned) to support negative compression levels in
    /// > [Zstandard](WIACompression::Zstandard) (RVZ only).
    pub compression_level: I32,
    /// The size of the chunks that data is divided into.
    ///
    /// WIA only:
    /// > Must be a multiple of 2 MiB.
    ///
    /// RVZ only:
    /// > Chunk sizes smaller than 2 MiB are supported. The following applies when using a chunk size
    /// > smaller than 2 MiB:
    /// > - The chunk size must be at least 32 KiB and must be a power of two. (Just like with WIA,
    /// >   sizes larger than 2 MiB do not have to be a power of two, they just have to be an integer
    /// >   multiple of 2 MiB.)
    /// > - For Wii partition data, each chunk contains one [WIAExceptionList] which contains
    /// >   exceptions for that chunk (and no other chunks). Offset 0 refers to the first hash of the
    /// >   current chunk, not the first hash of the full 2 MiB of data.
    pub chunk_size: U32,
    /// The first 0x80 bytes of the disc image.
    pub disc_head: [u8; DISC_HEAD_SIZE],
    /// The number of [WIAPartition] structs.
    pub num_partitions: U32,
    /// The size of one [WIAPartition] struct.
    ///
    /// If this is smaller than the size of [WIAPartition], fill the missing bytes with 0x00.
    pub partition_type_size: U32,
    /// The offset in the file where the [WIAPartition] structs are stored (uncompressed).
    pub partition_offset: U64,
    /// The SHA-1 hash of the [WIAPartition] structs.
    ///
    /// The number of bytes to hash is determined by `num_partitions * partition_type_size`.
    pub partition_hash: HashBytes,
    /// The number of [WIARawData] structs.
    pub num_raw_data: U32,
    /// The offset in the file where the [WIARawData] structs are stored (compressed).
    pub raw_data_offset: U64,
    /// The total compressed size of the [WIARawData] structs.
    pub raw_data_size: U32,
    /// The number of [WIAGroup] structs.
    pub num_groups: U32,
    /// The offset in the file where the [WIAGroup] structs are stored (compressed).
    pub group_offset: U64,
    /// The total compressed size of the [WIAGroup] structs.
    pub group_size: U32,
    /// The number of used bytes in the [compr_data](Self::compr_data) array.
    pub compr_data_len: u8,
    /// Compressor specific data.
    ///
    /// If the compression method is [None](WIACompression::None), [Purge](WIACompression::Purge),
    /// [Bzip2](WIACompression::Bzip2), or [Zstandard](WIACompression::Zstandard) (RVZ only),
    /// [compr_data_len](Self::compr_data_len) is 0. If the compression method is
    /// [Lzma](WIACompression::Lzma) or [Lzma2](WIACompression::Lzma2), the compressor specific data is
    /// stored in the format used by the 7-Zip SDK. It needs to be converted if you are using e.g.
    /// liblzma.
    ///
    /// For [Lzma](WIACompression::Lzma), the data is 5 bytes long. The first byte encodes the `lc`,
    /// `pb`, and `lp` parameters, and the four other bytes encode the dictionary size in little
    /// endian.
    pub compr_data: [u8; 7],
}

static_assert!(size_of::<WIADisc>() == 0xDC);

impl WIADisc {
    pub fn validate(&self) -> Result<()> {
        DiscKind::try_from(self.disc_type.get())?;
        WIACompression::try_from(self.compression.get())?;
        if self.partition_type_size.get() != size_of::<WIAPartition>() as u32 {
            return Err(Error::DiscFormat(format!(
                "WIA/RVZ partition type size is {}, expected {}",
                self.partition_type_size.get(),
                size_of::<WIAPartition>()
            )));
        }
        Ok(())
    }

    pub fn compression(&self) -> WIACompression {
        WIACompression::try_from(self.compression.get()).unwrap()
    }
}

#[derive(Clone, Debug, PartialEq, FromBytes, IntoBytes, Immutable, KnownLayout)]
#[repr(C, align(4))]
pub struct WIAPartitionData {
    /// The sector on the disc at which this data starts.
    /// One sector is 32 KiB (or 31 KiB excluding hashes).
    pub first_sector: U32,
    /// The number of sectors on the disc covered by this struct.
    /// One sector is 32 KiB (or 31 KiB excluding hashes).
    pub num_sectors: U32,
    /// The index of the first [WIAGroup] struct that points to the data covered by this struct.
    /// The other [WIAGroup] indices follow sequentially.
    pub group_index: U32,
    /// The number of [WIAGroup] structs used for this data.
    pub num_groups: U32,
}

static_assert!(size_of::<WIAPartitionData>() == 0x10);

impl WIAPartitionData {
    pub fn contains(&self, sector: u32) -> bool {
        let start = self.first_sector.get();
        sector >= start && sector < start + self.num_sectors.get()
    }
}

/// This struct is used for keeping track of Wii partition data that on the actual disc is encrypted
/// and hashed. This does not include the unencrypted area at the beginning of partitions that
/// contains the ticket, TMD, certificate chain, and H3 table. So for a typical game partition,
/// `pd[0].first_sector * 0x8000` would be 0x0F820000, not 0x0F800000.
///
/// Wii partition data is stored decrypted and with hashes removed. For each 0x8000 bytes on the
/// disc, 0x7C00 bytes are stored in the WIA file (prior to compression). If the hashes are desired,
/// the reading program must first recalculate the hashes as done when creating a Wii disc image
/// from scratch (see <https://wiibrew.org/wiki/Wii_Disc>), and must then apply the hash exceptions
/// which are stored along with the data (see the [WIAExceptionList] section).
#[derive(Clone, Debug, PartialEq, FromBytes, IntoBytes, Immutable, KnownLayout)]
#[repr(C, align(4))]
pub struct WIAPartition {
    /// The title key for this partition (128-bit AES), which can be used for re-encrypting the
    /// partition data.
    ///
    /// This key can be used directly, without decrypting it using the Wii common key.
    pub partition_key: KeyBytes,
    /// To quote the wit source code: `segment 0 is small and defined for management data (boot ..
    /// fst). segment 1 takes the remaining data.`
    ///
    /// The point at which wit splits the two segments is the FST end offset rounded up to the next
    /// 2 MiB. Giving the first segment a size which is not a multiple of 2 MiB is likely a bad idea
    /// (unless the second segment has a size of 0).
    pub partition_data: [WIAPartitionData; 2],
}

static_assert!(size_of::<WIAPartition>() == 0x30);

/// This struct is used for keeping track of disc data that is not stored as [WIAPartition].
/// The data is stored as is (other than compression being applied).
///
/// The first [WIARawData] has `raw_data_offset` set to 0x80 and `raw_data_size` set to 0x4FF80,
/// but despite this, it actually contains 0x50000 bytes of data. (However, the first 0x80 bytes
/// should be read from [WIADisc] instead.) This should be handled by rounding the offset down to
/// the previous multiple of 0x8000 (and adding the equivalent amount to the size so that the end
/// offset stays the same), not by special casing the first [WIARawData].
#[derive(Clone, Debug, PartialEq, FromBytes, IntoBytes, Immutable, KnownLayout)]
#[repr(C, align(4))]
pub struct WIARawData {
    /// The offset on the disc at which this data starts.
    pub raw_data_offset: U64,
    /// The number of bytes on the disc covered by this struct.
    pub raw_data_size: U64,
    /// The index of the first [WIAGroup] struct that points to the data covered by this struct.
    /// The other [WIAGroup] indices follow sequentially.
    pub group_index: U32,
    /// The number of [WIAGroup] structs used for this data.
    pub num_groups: U32,
}

impl WIARawData {
    pub fn start_offset(&self) -> u64 { self.raw_data_offset.get() & !(SECTOR_SIZE as u64 - 1) }

    pub fn start_sector(&self) -> u32 { (self.start_offset() / SECTOR_SIZE as u64) as u32 }

    pub fn end_offset(&self) -> u64 { self.raw_data_offset.get() + self.raw_data_size.get() }

    pub fn end_sector(&self) -> u32 { (self.end_offset() / SECTOR_SIZE as u64) as u32 }

    pub fn contains(&self, sector: u32) -> bool {
        sector >= self.start_sector() && sector < self.end_sector()
    }
}

/// This struct points directly to the actual disc data, stored compressed.
///
/// The data is interpreted differently depending on whether the [WIAGroup] is referenced by a
/// [WIAPartitionData] or a [WIARawData] (see the [WIAPartition] section for details).
///
/// A [WIAGroup] normally contains chunk_size bytes of decompressed data
/// (or `chunk_size / 0x8000 * 0x7C00` for Wii partition data when not counting hashes), not
/// counting any [WIAExceptionList] structs. However, the last [WIAGroup] of a [WIAPartitionData]
/// or [WIARawData] contains less data than that if `num_sectors * 0x8000` (for [WIAPartitionData])
/// or `raw_data_size` (for [WIARawData]) is not evenly divisible by `chunk_size`.
#[derive(Clone, Debug, PartialEq, FromBytes, IntoBytes, Immutable, KnownLayout)]
#[repr(C, align(4))]
pub struct WIAGroup {
    /// The offset in the file where the compressed data is stored.
    ///
    /// Stored as a `u32`, divided by 4.
    pub data_offset: U32,
    /// The size of the compressed data, including any [WIAExceptionList] structs. 0 is a special
    /// case meaning that every byte of the decompressed data is 0x00 and the [WIAExceptionList]
    /// structs (if there are supposed to be any) contain 0 exceptions.
    pub data_size: U32,
}

/// Compared to [WIAGroup], [RVZGroup] changes the meaning of the most significant bit of
/// [data_size](Self::data_size) and adds one additional attribute.
#[derive(Clone, Debug, PartialEq, FromBytes, IntoBytes, Immutable, KnownLayout)]
#[repr(C, align(4))]
pub struct RVZGroup {
    /// The offset in the file where the compressed data is stored, divided by 4.
    pub data_offset: U32,
    /// The most significant bit is 1 if the data is compressed using the compression method
    /// indicated in [WIADisc], and 0 if it is not compressed. The lower 31 bits are the size of
    /// the compressed data, including any [WIAExceptionList] structs. The lower 31 bits being 0 is
    /// a special case meaning that every byte of the decompressed and unpacked data is 0x00 and
    /// the [WIAExceptionList] structs (if there are supposed to be any) contain 0 exceptions.
    pub data_size_and_flag: U32,
    /// The size after decompressing but before decoding the RVZ packing.
    /// If this is 0, RVZ packing is not used for this group.
    pub rvz_packed_size: U32,
}

impl RVZGroup {
    pub fn data_size(&self) -> u32 { self.data_size_and_flag.get() & 0x7FFFFFFF }

    pub fn is_compressed(&self) -> bool { self.data_size_and_flag.get() & 0x80000000 != 0 }
}

impl From<&WIAGroup> for RVZGroup {
    fn from(value: &WIAGroup) -> Self {
        Self {
            data_offset: value.data_offset,
            data_size_and_flag: U32::new(value.data_size.get() | 0x80000000),
            rvz_packed_size: U32::new(0),
        }
    }
}

/// This struct represents a 20-byte difference between the recalculated hash data and the original
/// hash data. (See also [WIAExceptionList])
///
/// When recalculating hashes for a [WIAGroup] with a size which is not evenly divisible by 2 MiB
/// (with the size of the hashes included), the missing bytes should be treated as zeroes for the
/// purpose of hashing. (wit's writing code seems to act as if the reading code does not assume that
/// these missing bytes are zero, but both wit's and Dolphin's reading code treat them as zero.
/// Dolphin's writing code assumes that the reading code treats them as zero.)
///
/// wit's writing code only outputs [WIAException] structs for mismatches in the actual hash
/// data, not in the padding data (which normally only contains zeroes). Dolphin's writing code
/// outputs [WIAException] structs for both hash data and padding data. When Dolphin needs to
/// write [WIAException] structs for a padding area which is 32 bytes long, it writes one which
/// covers the first 20 bytes of the padding area and one which covers the last 20 bytes of the
/// padding area, generating 12 bytes of overlap between the [WIAException] structs.
#[derive(Clone, Debug, PartialEq, FromBytes, IntoBytes, Immutable, KnownLayout)]
#[repr(C, align(2))]
pub struct WIAException {
    /// The offset among the hashes. The offsets 0x0000-0x0400 here map to the offsets 0x0000-0x0400
    /// in the full 2 MiB of data, the offsets 0x0400-0x0800 here map to the offsets 0x8000-0x8400
    /// in the full 2 MiB of data, and so on.
    ///
    /// The offsets start over at 0 for each new [WIAExceptionList].
    pub offset: U16,
    /// The hash that the automatically generated hash at the given offset needs to be replaced
    /// with.
    ///
    /// The replacement should happen after calculating all hashes for the current 2 MiB of data
    /// but before encrypting the hashes.
    pub hash: HashBytes,
}

/// Each [WIAGroup] of Wii partition data contains one or more [WIAExceptionList] structs before
/// the actual data, one for each 2 MiB of data in the [WIAGroup]. The number of [WIAExceptionList]
/// structs per [WIAGroup] is always `chunk_size / 0x200000`, even for a [WIAGroup] which contains
/// less data than normal due to it being at the end of a partition.
///
/// For memory management reasons, programs which read WIA files might place a limit on how many
/// exceptions there can be in a [WIAExceptionList]. Dolphin's reading code has a limit of
/// `52 × 64 = 3328` (unless the compression method is [None](WIACompression::None) or
/// [Purge](WIACompression::Purge), in which case there is no limit), which is enough to cover all
/// hashes and all padding. wit's reading code seems to be written as if `47 × 64 = 3008` is the
/// maximum it needs to be able to handle, which is enough to cover all hashes but not any padding.
/// However, because wit allocates more memory than needed, it seems to be possible to exceed 3008
/// by some amount without problems. It should be safe for writing code to assume that reading code
/// can handle at least 3328 exceptions per [WIAExceptionList].
///
/// Somewhat ironically, there are exceptions to how [WIAExceptionList] structs are handled:
///
/// For the compression method [Purge](WIACompression::Purge), the [WIAExceptionList] structs are
/// stored uncompressed (in other words, before the first [WIASegment]). For
/// [Bzip2](WIACompression::Bzip2), [Lzma](WIACompression::Lzma) and [Lzma2](WIACompression::Lzma2), they are
/// compressed along with the rest of the data.
///
/// For the compression methods [None](WIACompression::None) and [Purge](WIACompression::Purge), if the
/// end offset of the last [WIAExceptionList] is not evenly divisible by 4, padding is inserted
/// after it so that the data afterwards will start at a 4 byte boundary. This padding is not
/// inserted for the other compression methods.
type WIAExceptionList = Box<[WIAException]>;

#[derive(Clone)]
pub enum Decompressor {
    None,
    #[cfg(feature = "compress-bzip2")]
    Bzip2,
    #[cfg(feature = "compress-lzma")]
    Lzma(Box<[u8]>),
    #[cfg(feature = "compress-lzma")]
    Lzma2(Box<[u8]>),
    #[cfg(feature = "compress-zstd")]
    Zstandard,
}

impl Decompressor {
    pub fn new(disc: &WIADisc) -> Result<Self> {
        let _data = &disc.compr_data[..disc.compr_data_len as usize];
        match disc.compression() {
            WIACompression::None => Ok(Self::None),
            #[cfg(feature = "compress-bzip2")]
            WIACompression::Bzip2 => Ok(Self::Bzip2),
            #[cfg(feature = "compress-lzma")]
            WIACompression::Lzma => Ok(Self::Lzma(Box::from(_data))),
            #[cfg(feature = "compress-lzma")]
            WIACompression::Lzma2 => Ok(Self::Lzma2(Box::from(_data))),
            #[cfg(feature = "compress-zstd")]
            WIACompression::Zstandard => Ok(Self::Zstandard),
            comp => Err(Error::DiscFormat(format!("Unsupported WIA/RVZ compression: {:?}", comp))),
        }
    }

    pub fn wrap<'a, R>(&mut self, reader: R) -> io::Result<Box<dyn Read + 'a>>
    where R: Read + 'a {
        Ok(match self {
            Decompressor::None => Box::new(reader),
            #[cfg(feature = "compress-bzip2")]
            Decompressor::Bzip2 => Box::new(bzip2::read::BzDecoder::new(reader)),
            #[cfg(feature = "compress-lzma")]
            Decompressor::Lzma(data) => {
                use crate::util::compress::{lzma_props_decode, new_lzma_decoder};
                let options = lzma_props_decode(data)?;
                Box::new(new_lzma_decoder(reader, &options)?)
            }
            #[cfg(feature = "compress-lzma")]
            Decompressor::Lzma2(data) => {
                use crate::util::compress::{lzma2_props_decode, new_lzma2_decoder};
                let options = lzma2_props_decode(data)?;
                Box::new(new_lzma2_decoder(reader, &options)?)
            }
            #[cfg(feature = "compress-zstd")]
            Decompressor::Zstandard => Box::new(zstd::stream::Decoder::new(reader)?),
        })
    }
}

pub struct DiscIOWIA {
    inner: Box<dyn DiscStream>,
    header: WIAFileHeader,
    disc: WIADisc,
    partitions: Box<[WIAPartition]>,
    raw_data: Box<[WIARawData]>,
    groups: Box<[RVZGroup]>,
    nkit_header: Option<NKitHeader>,
    decompressor: Decompressor,
    group: u32,
    group_data: Vec<u8>,
    exception_lists: Vec<WIAExceptionList>,
}

impl Clone for DiscIOWIA {
    fn clone(&self) -> Self {
        Self {
            inner: self.inner.clone(),
            header: self.header.clone(),
            disc: self.disc.clone(),
            partitions: self.partitions.clone(),
            raw_data: self.raw_data.clone(),
            groups: self.groups.clone(),
            nkit_header: self.nkit_header.clone(),
            decompressor: self.decompressor.clone(),
            group: u32::MAX,
            group_data: Vec::new(),
            exception_lists: Vec::new(),
        }
    }
}

fn verify_hash(buf: &[u8], expected: &HashBytes) -> Result<()> {
    let out = hash_bytes(buf);
    if out != *expected {
        let mut got_bytes = [0u8; 40];
        let got = base16ct::lower::encode_str(&out, &mut got_bytes).unwrap(); // Safe: fixed buffer size
        let mut expected_bytes = [0u8; 40];
        let expected = base16ct::lower::encode_str(expected, &mut expected_bytes).unwrap(); // Safe: fixed buffer size
        return Err(Error::DiscFormat(format!(
            "WIA/RVZ hash mismatch: {}, expected {}",
            got, expected
        )));
    }
    Ok(())
}

impl DiscIOWIA {
    pub fn new(mut inner: Box<dyn DiscStream>) -> Result<Box<Self>> {
        // Load & verify file header
        inner.seek(SeekFrom::Start(0)).context("Seeking to start")?;
        let header: WIAFileHeader =
            read_from(inner.as_mut()).context("Reading WIA/RVZ file header")?;
        header.validate()?;
        let is_rvz = header.is_rvz();
        // log::debug!("Header: {:?}", header);

        // Load & verify disc header
        let mut disc_buf: Vec<u8> = read_vec(inner.as_mut(), header.disc_size.get() as usize)
            .context("Reading WIA/RVZ disc header")?;
        verify_hash(&disc_buf, &header.disc_hash)?;
        disc_buf.resize(size_of::<WIADisc>(), 0);
        let disc = WIADisc::read_from_bytes(disc_buf.as_slice()).unwrap();
        disc.validate()?;
        // if !options.rebuild_hashes {
        //     // If we're not rebuilding hashes, disable partition hashes in disc header
        //     disc.disc_head[0x60] = 1;
        // }
        // if !options.rebuild_encryption {
        //     // If we're not re-encrypting, disable partition encryption in disc header
        //     disc.disc_head[0x61] = 1;
        // }
        // log::debug!("Disc: {:?}", disc);

        // Read NKit header if present (after disc header)
        let nkit_header = NKitHeader::try_read_from(inner.as_mut(), disc.chunk_size.get(), false);

        // Load & verify partition headers
        inner
            .seek(SeekFrom::Start(disc.partition_offset.get()))
            .context("Seeking to WIA/RVZ partition headers")?;
        let partitions: Box<[WIAPartition]> =
            read_box_slice(inner.as_mut(), disc.num_partitions.get() as usize)
                .context("Reading WIA/RVZ partition headers")?;
        verify_hash(partitions.as_ref().as_bytes(), &disc.partition_hash)?;
        // log::debug!("Partitions: {:?}", partitions);

        // Create decompressor
        let mut decompressor = Decompressor::new(&disc)?;

        // Load raw data headers
        let raw_data: Box<[WIARawData]> = {
            inner
                .seek(SeekFrom::Start(disc.raw_data_offset.get()))
                .context("Seeking to WIA/RVZ raw data headers")?;
            let mut reader = decompressor
                .wrap(inner.as_mut().take(disc.raw_data_size.get() as u64))
                .context("Creating WIA/RVZ decompressor")?;
            read_box_slice(&mut reader, disc.num_raw_data.get() as usize)
                .context("Reading WIA/RVZ raw data headers")?
        };
        // Validate raw data alignment
        for (idx, rd) in raw_data.iter().enumerate() {
            let start_offset = rd.start_offset();
            let end_offset = rd.end_offset();
            if (start_offset % SECTOR_SIZE as u64) != 0 || (end_offset % SECTOR_SIZE as u64) != 0 {
                return Err(Error::DiscFormat(format!(
                    "WIA/RVZ raw data {} not aligned to sector: {:#X}..{:#X}",
                    idx, start_offset, end_offset
                )));
            }
        }
        // log::debug!("Raw data: {:?}", raw_data);

        // Load group headers
        let groups = {
            inner
                .seek(SeekFrom::Start(disc.group_offset.get()))
                .context("Seeking to WIA/RVZ group headers")?;
            let mut reader = decompressor
                .wrap(inner.as_mut().take(disc.group_size.get() as u64))
                .context("Creating WIA/RVZ decompressor")?;
            if is_rvz {
                read_box_slice(&mut reader, disc.num_groups.get() as usize)
                    .context("Reading WIA/RVZ group headers")?
            } else {
                let wia_groups: Box<[WIAGroup]> =
                    read_box_slice(&mut reader, disc.num_groups.get() as usize)
                        .context("Reading WIA/RVZ group headers")?;
                wia_groups.iter().map(RVZGroup::from).collect()
            }
            // log::debug!("Groups: {:?}", groups);
        };

        Ok(Box::new(Self {
            header,
            disc,
            partitions,
            raw_data,
            groups,
            inner,
            nkit_header,
            decompressor,
            group: u32::MAX,
            group_data: vec![],
            exception_lists: vec![],
        }))
    }
}

fn read_exception_lists<R>(
    reader: &mut R,
    in_partition: bool,
    chunk_size: u32,
) -> io::Result<Vec<WIAExceptionList>>
where
    R: Read + ?Sized,
{
    if !in_partition {
        return Ok(vec![]);
    }

    // One exception list for each 2 MiB of data
    let num_exception_list = (chunk_size as usize).div_ceil(0x200000);
    // log::debug!("Num exception list: {:?}", num_exception_list);
    let mut exception_lists = Vec::with_capacity(num_exception_list);
    for i in 0..num_exception_list {
        let num_exceptions = read_u16_be(reader)?;
        let exceptions: Box<[WIAException]> = read_box_slice(reader, num_exceptions as usize)?;
        if !exceptions.is_empty() {
            log::debug!("Exception list {}: {:?}", i, exceptions);
        }
        exception_lists.push(exceptions);
    }
    Ok(exception_lists)
}

impl BlockIO for DiscIOWIA {
    fn read_block_internal(
        &mut self,
        out: &mut [u8],
        sector: u32,
        partition: Option<&PartitionInfo>,
    ) -> io::Result<Block> {
        let chunk_size = self.disc.chunk_size.get();
        let sectors_per_chunk = chunk_size / SECTOR_SIZE as u32;

        let (group_index, group_sector, partition_offset) = if let Some(partition) = partition {
            // Find the partition
            let Some(wia_part) = self.partitions.get(partition.index) else {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    format!("Couldn't find WIA/RVZ partition index {}", partition.index),
                ));
            };

            // Sanity check partition sector ranges
            let wia_part_start = wia_part.partition_data[0].first_sector.get();
            let wia_part_end = wia_part.partition_data[1].first_sector.get()
                + wia_part.partition_data[1].num_sectors.get();
            if partition.data_start_sector != wia_part_start
                || partition.data_end_sector != wia_part_end
            {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    format!(
                        "WIA/RVZ partition sector mismatch: {}..{} != {}..{}",
                        wia_part_start,
                        wia_part_end,
                        partition.data_start_sector,
                        partition.data_end_sector
                    ),
                ));
            }

            // Find the partition data for the sector
            let Some(pd) = wia_part.partition_data.iter().find(|pd| pd.contains(sector)) else {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    format!("Couldn't find WIA/RVZ partition data for sector {}", sector),
                ));
            };

            // Find the group index for the sector
            let part_data_sector = sector - pd.first_sector.get();
            let part_group_index = part_data_sector / sectors_per_chunk;
            let part_group_sector = part_data_sector % sectors_per_chunk;
            if part_group_index >= pd.num_groups.get() {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    format!(
                        "WIA/RVZ partition group index out of range: {} >= {}",
                        part_group_index,
                        pd.num_groups.get()
                    ),
                ));
            }

            // Calculate the group offset within the partition
            let part_group_offset =
                (((part_group_index * sectors_per_chunk) + pd.first_sector.get())
                    - wia_part.partition_data[0].first_sector.get()) as u64
                    * SECTOR_DATA_SIZE as u64;
            (pd.group_index.get() + part_group_index, part_group_sector, part_group_offset)
        } else {
            let Some(rd) = self.raw_data.iter().find(|d| d.contains(sector)) else {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    format!("Couldn't find WIA/RVZ raw data for sector {}", sector),
                ));
            };

            // Find the group index for the sector
            let data_sector = sector - (rd.raw_data_offset.get() / SECTOR_SIZE as u64) as u32;
            let group_index = data_sector / sectors_per_chunk;
            let group_sector = data_sector % sectors_per_chunk;
            if group_index >= rd.num_groups.get() {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    format!(
                        "WIA/RVZ raw data group index out of range: {} >= {}",
                        group_index,
                        rd.num_groups.get()
                    ),
                ));
            }

            (rd.group_index.get() + group_index, group_sector, 0)
        };

        // Fetch the group
        let Some(group) = self.groups.get(group_index as usize) else {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("Couldn't find WIA/RVZ group index {}", group_index),
            ));
        };

        // Special case for all-zero data
        if group.data_size() == 0 {
            self.exception_lists.clear();
            return Ok(Block::Zero);
        }

        // Read group data if necessary
        if group_index != self.group {
            let group_data_size = if partition.is_some() {
                // Within a partition, hashes are excluded from the data size
                (sectors_per_chunk * SECTOR_DATA_SIZE as u32) as usize
            } else {
                chunk_size as usize
            };
            self.group_data = Vec::with_capacity(group_data_size);
            let group_data_start = group.data_offset.get() as u64 * 4;
            self.inner.seek(SeekFrom::Start(group_data_start))?;

            let mut reader = (&mut self.inner).take_seek(group.data_size() as u64);
            let uncompressed_exception_lists =
                matches!(self.disc.compression(), WIACompression::None | WIACompression::Purge)
                    || !group.is_compressed();
            if uncompressed_exception_lists {
                self.exception_lists = read_exception_lists(
                    &mut reader,
                    partition.is_some(),
                    self.disc.chunk_size.get(),
                )?;
                // Align to 4
                let rem = reader.stream_position()? % 4;
                if rem != 0 {
                    reader.seek(SeekFrom::Current((4 - rem) as i64))?;
                }
            }
            let mut reader: Box<dyn Read> = if group.is_compressed() {
                self.decompressor.wrap(reader)?
            } else {
                Box::new(reader)
            };
            if !uncompressed_exception_lists {
                self.exception_lists = read_exception_lists(
                    reader.as_mut(),
                    partition.is_some(),
                    self.disc.chunk_size.get(),
                )?;
            }

            if group.rvz_packed_size.get() > 0 {
                // Decode RVZ packed data
                let mut lfg = LaggedFibonacci::default();
                loop {
                    let mut size_bytes = [0u8; 4];
                    match reader.read_exact(&mut size_bytes) {
                        Ok(_) => {}
                        Err(e) if e.kind() == io::ErrorKind::UnexpectedEof => break,
                        Err(e) => {
                            return Err(io::Error::new(e.kind(), "Failed to read RVZ packed size"));
                        }
                    }
                    let size = u32::from_be_bytes(size_bytes);
                    let cur_data_len = self.group_data.len();
                    if size & 0x80000000 != 0 {
                        // Junk data
                        let size = size & 0x7FFFFFFF;
                        lfg.init_with_reader(reader.as_mut())?;
                        lfg.skip(
                            ((partition_offset + cur_data_len as u64) % SECTOR_SIZE as u64)
                                as usize,
                        );
                        self.group_data.resize(cur_data_len + size as usize, 0);
                        lfg.fill(&mut self.group_data[cur_data_len..]);
                    } else {
                        // Real data
                        self.group_data.resize(cur_data_len + size as usize, 0);
                        reader.read_exact(&mut self.group_data[cur_data_len..])?;
                    }
                }
            } else {
                // Read and decompress data
                reader.read_to_end(&mut self.group_data)?;
            }

            self.group = group_index;
        }

        // Read sector from cached group data
        if partition.is_some() {
            let sector_data_start = group_sector as usize * SECTOR_DATA_SIZE;
            out[..HASHES_SIZE].fill(0);
            out[HASHES_SIZE..SECTOR_SIZE].copy_from_slice(
                &self.group_data[sector_data_start..sector_data_start + SECTOR_DATA_SIZE],
            );
            Ok(Block::PartDecrypted { has_hashes: false })
        } else {
            let sector_data_start = group_sector as usize * SECTOR_SIZE;
            out.copy_from_slice(
                &self.group_data[sector_data_start..sector_data_start + SECTOR_SIZE],
            );
            Ok(Block::Raw)
        }
    }

    fn block_size_internal(&self) -> u32 {
        // WIA/RVZ chunks aren't always the full size, so we'll consider the
        // block size to be one sector, and handle the complexity ourselves.
        SECTOR_SIZE as u32
    }

    fn meta(&self) -> DiscMeta {
        let mut result = DiscMeta {
            format: if self.header.is_rvz() { Format::Rvz } else { Format::Wia },
            block_size: Some(self.disc.chunk_size.get()),
            compression: match self.disc.compression() {
                WIACompression::None => Compression::None,
                WIACompression::Purge => Compression::Purge,
                WIACompression::Bzip2 => Compression::Bzip2,
                WIACompression::Lzma => Compression::Lzma,
                WIACompression::Lzma2 => Compression::Lzma2,
                WIACompression::Zstandard => Compression::Zstandard,
            },
            decrypted: true,
            needs_hash_recovery: true,
            lossless: true,
            disc_size: Some(self.header.iso_file_size.get()),
            ..Default::default()
        };
        if let Some(nkit_header) = &self.nkit_header {
            nkit_header.apply(&mut result);
        }
        result
    }
}
