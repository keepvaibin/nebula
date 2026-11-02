//! Disc type related logic (GameCube, Wii)

use std::{
    borrow::Cow,
    ffi::CStr,
    fmt::{Debug, Display, Formatter},
    io,
    io::{BufRead, Seek},
    mem::size_of,
    str::from_utf8,
};

use dyn_clone::DynClone;
use zerocopy::{big_endian::*, FromBytes, Immutable, IntoBytes, KnownLayout};

use crate::{io::MagicBytes, static_assert, Result};

pub(crate) mod fst;
pub(crate) mod gcn;
pub(crate) mod hashes;
pub(crate) mod reader;
pub(crate) mod streams;
pub mod wii;

pub use fst::{Fst, Node, NodeKind};
pub use streams::{FileStream, OwnedFileStream, WindowedStream};
pub use wii::{SignedHeader, Ticket, TicketLimit, TmdHeader, REGION_SIZE};

/// Size in bytes of a disc sector. (32 KiB)
pub const SECTOR_SIZE: usize = 0x8000;

/// Magic bytes for Wii discs. Located at offset 0x18.
pub const WII_MAGIC: MagicBytes = [0x5D, 0x1C, 0x9E, 0xA3];

/// Magic bytes for GameCube discs. Located at offset 0x1C.
pub const GCN_MAGIC: MagicBytes = [0xC2, 0x33, 0x9F, 0x3D];

/// Shared GameCube & Wii disc header.
///
/// This header is always at the start of the disc image and within each Wii partition.
#[derive(Clone, Debug, PartialEq, FromBytes, IntoBytes, Immutable, KnownLayout)]
#[repr(C, align(4))]
pub struct DiscHeader {
    /// Game ID (e.g. GM8E01 for Metroid Prime)
    pub game_id: [u8; 6],
    /// Used in multi-disc games
    pub disc_num: u8,
    /// Disc version
    pub disc_version: u8,
    /// Audio streaming enabled
    pub audio_streaming: u8,
    /// Audio streaming buffer size
    pub audio_stream_buf_size: u8,
    /// Padding
    _pad1: [u8; 14],
    /// If this is a Wii disc, this will be 0x5D1C9EA3
    pub wii_magic: MagicBytes,
    /// If this is a GameCube disc, this will be 0xC2339F3D
    pub gcn_magic: MagicBytes,
    /// Game title
    pub game_title: [u8; 64],
    /// If 1, disc omits partition hashes
    pub no_partition_hashes: u8,
    /// If 1, disc omits partition encryption
    pub no_partition_encryption: u8,
    /// Padding
    _pad2: [u8; 926],
}

static_assert!(size_of::<DiscHeader>() == 0x400);

impl DiscHeader {
    /// Game ID as a string.
    #[inline]
    pub fn game_id_str(&self) -> &str { from_utf8(&self.game_id).unwrap_or("[invalid]") }

    /// Game title as a string.
    #[inline]
    pub fn game_title_str(&self) -> &str {
        CStr::from_bytes_until_nul(&self.game_title)
            .ok()
            .and_then(|c| c.to_str().ok())
            .unwrap_or("[invalid]")
    }

    /// Whether this is a GameCube disc.
    #[inline]
    pub fn is_gamecube(&self) -> bool { self.gcn_magic == GCN_MAGIC }

    /// Whether this is a Wii disc.
    #[inline]
    pub fn is_wii(&self) -> bool { self.wii_magic == WII_MAGIC }
}

/// A header describing the contents of a disc partition.
///
/// **GameCube**: Always follows the disc header.
///
/// **Wii**: Follows the disc header within each partition.
#[derive(Clone, Debug, PartialEq, FromBytes, IntoBytes, Immutable, KnownLayout)]
#[repr(C, align(4))]
pub struct PartitionHeader {
    /// Debug monitor offset
    pub debug_mon_offset: U32,
    /// Debug monitor load address
    pub debug_load_address: U32,
    /// Padding
    _pad1: [u8; 0x18],
    /// Offset to main DOL (Wii: >> 2)
    pub dol_offset: U32,
    /// Offset to file system table (Wii: >> 2)
    pub fst_offset: U32,
    /// File system size (Wii: >> 2)
    pub fst_size: U32,
    /// File system max size (Wii: >> 2)
    pub fst_max_size: U32,
    /// File system table load address
    pub fst_memory_address: U32,
    /// User data offset
    pub user_offset: U32,
    /// User data size
    pub user_size: U32,
    /// Padding
    _pad2: [u8; 4],
}

static_assert!(size_of::<PartitionHeader>() == 0x40);

impl PartitionHeader {
    /// Offset within the partition to the main DOL.
    #[inline]
    pub fn dol_offset(&self, is_wii: bool) -> u64 {
        if is_wii {
            self.dol_offset.get() as u64 * 4
        } else {
            self.dol_offset.get() as u64
        }
    }

    /// Offset within the partition to the file system table (FST).
    #[inline]
    pub fn fst_offset(&self, is_wii: bool) -> u64 {
        if is_wii {
            self.fst_offset.get() as u64 * 4
        } else {
            self.fst_offset.get() as u64
        }
    }

    /// Size of the file system table (FST).
    #[inline]
    pub fn fst_size(&self, is_wii: bool) -> u64 {
        if is_wii {
            self.fst_size.get() as u64 * 4
        } else {
            self.fst_size.get() as u64
        }
    }

    /// Maximum size of the file system table (FST) across multi-disc games.
    #[inline]
    pub fn fst_max_size(&self, is_wii: bool) -> u64 {
        if is_wii {
            self.fst_max_size.get() as u64 * 4
        } else {
            self.fst_max_size.get() as u64
        }
    }
}

/// Apploader header.
#[derive(Debug, PartialEq, Clone, FromBytes, IntoBytes, Immutable, KnownLayout)]
#[repr(C, align(4))]
pub struct ApploaderHeader {
    /// Apploader build date
    pub date: [u8; 16],
    /// Entry point
    pub entry_point: U32,
    /// Apploader size
    pub size: U32,
    /// Apploader trailer size
    pub trailer_size: U32,
    /// Padding
    _pad: [u8; 4],
}

impl ApploaderHeader {
    /// Apploader build date as a string.
    #[inline]
    pub fn date_str(&self) -> Option<&str> {
        CStr::from_bytes_until_nul(&self.date).ok().and_then(|c| c.to_str().ok())
    }
}

/// Maximum number of text sections in a DOL.
pub const DOL_MAX_TEXT_SECTIONS: usize = 7;
/// Maximum number of data sections in a DOL.
pub const DOL_MAX_DATA_SECTIONS: usize = 11;

/// Dolphin executable (DOL) header.
#[derive(Debug, Clone, FromBytes, Immutable, KnownLayout)]
pub struct DolHeader {
    /// Text section offsets
    pub text_offs: [U32; DOL_MAX_TEXT_SECTIONS],
    /// Data section offsets
    pub data_offs: [U32; DOL_MAX_DATA_SECTIONS],
    /// Text section addresses
    pub text_addrs: [U32; DOL_MAX_TEXT_SECTIONS],
    /// Data section addresses
    pub data_addrs: [U32; DOL_MAX_DATA_SECTIONS],
    /// Text section sizes
    pub text_sizes: [U32; DOL_MAX_TEXT_SECTIONS],
    /// Data section sizes
    pub data_sizes: [U32; DOL_MAX_DATA_SECTIONS],
    /// BSS address
    pub bss_addr: U32,
    /// BSS size
    pub bss_size: U32,
    /// Entry point
    pub entry_point: U32,
    /// Padding
    _pad: [u8; 0x1C],
}

static_assert!(size_of::<DolHeader>() == 0x100);

/// The kind of disc partition.
#[derive(Debug, PartialEq, Eq, Copy, Clone)]
pub enum PartitionKind {
    /// Data partition.
    Data,
    /// Update partition.
    Update,
    /// Channel partition.
    Channel,
    /// Other partition kind.
    Other(u32),
}

impl Display for PartitionKind {
    #[inline]
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Data => write!(f, "Data"),
            Self::Update => write!(f, "Update"),
            Self::Channel => write!(f, "Channel"),
            Self::Other(v) => {
                let bytes = v.to_be_bytes();
                write!(f, "Other ({:08X}, {})", v, String::from_utf8_lossy(&bytes))
            }
        }
    }
}

impl PartitionKind {
    /// Returns the directory name for the partition kind.
    #[inline]
    pub fn dir_name(&self) -> Cow<str> {
        match self {
            Self::Data => Cow::Borrowed("DATA"),
            Self::Update => Cow::Borrowed("UPDATE"),
            Self::Channel => Cow::Borrowed("CHANNEL"),
            Self::Other(v) => {
                let bytes = v.to_be_bytes();
                Cow::Owned(format!("P-{}", String::from_utf8_lossy(&bytes)))
            }
        }
    }
}

impl From<u32> for PartitionKind {
    #[inline]
    fn from(v: u32) -> Self {
        match v {
            0 => Self::Data,
            1 => Self::Update,
            2 => Self::Channel,
            v => Self::Other(v),
        }
    }
}

/// An open disc partition.
pub trait PartitionBase: DynClone + BufRead + Seek + Send + Sync {
    /// Reads the partition header and file system table.
    fn meta(&mut self) -> Result<Box<PartitionMeta>>;

    /// Seeks the partition stream to the specified file system node
    /// and returns a windowed stream.
    ///
    /// # Examples
    ///
    /// Basic usage:
    /// ```no_run
    /// use std::io::Read;
    ///
    /// use nod::{Disc, PartitionKind};
    ///
    /// fn main() -> nod::Result<()> {
    ///     let disc = Disc::new("path/to/file.iso")?;
    ///     let mut partition = disc.open_partition_kind(PartitionKind::Data)?;
    ///     let meta = partition.meta()?;
    ///     let fst = meta.fst()?;
    ///     if let Some((_, node)) = fst.find("/MP3/Worlds.txt") {
    ///         let mut s = String::new();
    ///         partition
    ///             .open_file(node)
    ///             .expect("Failed to open file stream")
    ///             .read_to_string(&mut s)
    ///             .expect("Failed to read file");
    ///         println!("{}", s);
    ///     }
    ///     Ok(())
    /// }
    /// ```
    fn open_file(&mut self, node: Node) -> io::Result<FileStream>;

    /// Consumes the partition instance and returns a windowed stream.
    ///
    /// # Examples
    ///
    /// ```no_run
    /// use std::io::Read;
    ///
    /// use nod::{Disc, PartitionKind, OwnedFileStream};
    ///
    /// fn main() -> nod::Result<()> {
    ///     let disc = Disc::new("path/to/file.iso")?;
    ///     let mut partition = disc.open_partition_kind(PartitionKind::Data)?;
    ///     let meta = partition.meta()?;
    ///     let fst = meta.fst()?;
    ///     if let Some((_, node)) = fst.find("/disc.tgc") {
    ///         let file: OwnedFileStream = partition
    ///             .clone() // Clone the Box<dyn PartitionBase>
    ///             .into_open_file(node) // Get an OwnedFileStream
    ///             .expect("Failed to open file stream");
    ///         // Open the inner disc image using the owned stream
    ///         let inner_disc = Disc::new_stream(Box::new(file))
    ///             .expect("Failed to open inner disc");
    ///         // ...
    ///     }
    ///     Ok(())
    /// }
    /// ```
    fn into_open_file(self: Box<Self>, node: Node) -> io::Result<OwnedFileStream>;
}

dyn_clone::clone_trait_object!(PartitionBase);

/// Size of the disc header and partition header (boot.bin)
pub const BOOT_SIZE: usize = size_of::<DiscHeader>() + size_of::<PartitionHeader>();
/// Size of the debug and region information (bi2.bin)
pub const BI2_SIZE: usize = 0x2000;

/// Extra disc partition data. (DOL, FST, etc.)
#[derive(Clone, Debug)]
pub struct PartitionMeta {
    /// Disc and partition header (boot.bin)
    pub raw_boot: Box<[u8; BOOT_SIZE]>,
    /// Debug and region information (bi2.bin)
    pub raw_bi2: Box<[u8; BI2_SIZE]>,
    /// Apploader (apploader.bin)
    pub raw_apploader: Box<[u8]>,
    /// Main binary (main.dol)
    pub raw_dol: Box<[u8]>,
    /// File system table (fst.bin)
    pub raw_fst: Box<[u8]>,
    /// Ticket (ticket.bin, Wii only)
    pub raw_ticket: Option<Box<[u8]>>,
    /// TMD (tmd.bin, Wii only)
    pub raw_tmd: Option<Box<[u8]>>,
    /// Certificate chain (cert.bin, Wii only)
    pub raw_cert_chain: Option<Box<[u8]>>,
    /// H3 hash table (h3.bin, Wii only)
    pub raw_h3_table: Option<Box<[u8]>>,
}

impl PartitionMeta {
    /// A view into the disc header.
    #[inline]
    pub fn header(&self) -> &DiscHeader {
        DiscHeader::ref_from_bytes(&self.raw_boot[..size_of::<DiscHeader>()]).unwrap()
    }

    /// A view into the partition header.
    #[inline]
    pub fn partition_header(&self) -> &PartitionHeader {
        PartitionHeader::ref_from_bytes(&self.raw_boot[size_of::<DiscHeader>()..]).unwrap()
    }

    /// A view into the apploader header.
    #[inline]
    pub fn apploader_header(&self) -> &ApploaderHeader {
        ApploaderHeader::ref_from_prefix(&self.raw_apploader).unwrap().0
    }

    /// A view into the file system table (FST).
    #[inline]
    pub fn fst(&self) -> Result<Fst, &'static str> { Fst::new(&self.raw_fst) }

    /// A view into the DOL header.
    #[inline]
    pub fn dol_header(&self) -> &DolHeader { DolHeader::ref_from_prefix(&self.raw_dol).unwrap().0 }

    /// A view into the ticket. (Wii only)
    #[inline]
    pub fn ticket(&self) -> Option<&Ticket> {
        self.raw_ticket.as_ref().and_then(|v| Ticket::ref_from_bytes(v).ok())
    }

    /// A view into the TMD. (Wii only)
    #[inline]
    pub fn tmd_header(&self) -> Option<&TmdHeader> {
        self.raw_tmd.as_ref().and_then(|v| TmdHeader::ref_from_prefix(v).ok().map(|(v, _)| v))
    }
}

/// The size of a single-layer MiniDVD. (1.4 GB)
///
/// GameCube games and some third-party Wii discs (Datel) use this format.
pub const MINI_DVD_SIZE: u64 = 1_459_978_240;

/// The size of a single-layer DVD. (4.7 GB)
///
/// The vast majority of Wii games use this format.
pub const SL_DVD_SIZE: u64 = 4_699_979_776;

/// The size of a dual-layer DVD. (8.5 GB)
///
/// A few larger Wii games use this format.
/// (Super Smash Bros. Brawl, Metroid Prime Trilogy, etc.)
pub const DL_DVD_SIZE: u64 = 8_511_160_320;
