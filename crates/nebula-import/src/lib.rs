//! The native game's data from the player's own disc image: `game.pak`, the game partition's decrypted data at its
//! own offsets (sparse elsewhere), built in one pass straight from the disc. It is what `nebula-recomp-cli`'s
//! `extract` and `build-pak` make on Windows, with the same checks (game ID, main.dol SHA-1, TMD, extents), without an
//! extracted copy in between. Android's setup calls it through JNI (`Importer.java`).

use anyhow::{bail, Context, Result};
use nebula_recomp_core::{RMGE01_DOL_SHA1, RMGE01_GAME_ID};
use nod::{Disc, OpenOptions, PartitionKind};
use sha1::{Digest, Sha1};
use std::fs::{self, File};
use std::io::{self, Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::sync::Arc;

mod jni;

const BOOT_BIN_SIZE: usize = 0x440;
const BI2_BIN_SIZE: usize = 0x2000;
const APPLOADER_HEADER_SIZE: usize = 0x20;
const BI2_BIN_OFFSET: u64 = 0x440;
const APPLOADER_OFFSET: u64 = 0x2440;
const TMD_SIZE: usize = 0x208;
const TMD_SIGNATURE_TYPE: u32 = 0x0001_0001;
const TMD_CONTENT_COUNT_OFFSET: usize = 0x1de;
const TMD_CONTENT_RECORD_OFFSET: usize = 0x1e4;
const TMD_CONTENT_RECORD_SIZE: usize = 36;
const RMGE01_PARTITION_DATA_BYTES: u64 = 0xff7c_0000;
const COPY_CHUNK: usize = 1 << 20;

/// What a disc image is, as the setup shows it before importing.
#[derive(Debug, Clone)]
pub struct Identity {
    pub format: String,
    pub game_id: String,
    pub title: String,
    pub supported: bool,
    pub message: String,
}

/// A disc image read with positional reads, so nod's clones of the stream never share a file position.
#[derive(Clone)]
pub struct DiscFile {
    file: Arc<File>,
    position: u64,
}

impl DiscFile {
    pub fn new(file: File) -> Self {
        DiscFile { file: Arc::new(file), position: 0 }
    }

    fn read_at(&self, buffer: &mut [u8], offset: u64) -> io::Result<usize> {
        #[cfg(unix)]
        {
            use std::os::unix::fs::FileExt;
            self.file.read_at(buffer, offset)
        }
        #[cfg(windows)]
        {
            use std::os::windows::fs::FileExt;
            self.file.seek_read(buffer, offset)
        }
    }
}

impl Read for DiscFile {
    fn read(&mut self, buffer: &mut [u8]) -> io::Result<usize> {
        let read = self.read_at(buffer, self.position)?;
        self.position += read as u64;
        Ok(read)
    }
}

impl Seek for DiscFile {
    fn seek(&mut self, from: SeekFrom) -> io::Result<u64> {
        let length = self.file.metadata()?.len();
        let target = match from {
            SeekFrom::Start(offset) => Some(offset),
            SeekFrom::End(delta) => length.checked_add_signed(delta),
            SeekFrom::Current(delta) => self.position.checked_add_signed(delta),
        };
        self.position = target.ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "seek before the start"))?;
        Ok(self.position)
    }
}

fn open_disc(disc: DiscFile) -> Result<Disc> {
    Disc::new_stream_with_options(Box::new(disc), &OpenOptions { rebuild_encryption: false, validate_hashes: true })
        .map_err(|error| anyhow::anyhow!("this is not a readable GameCube or Wii disc image: {error}"))
}

/// The disc's game and whether Nebula can play it.
pub fn identify(disc: DiscFile) -> Result<Identity> {
    let disc = open_disc(disc)?;
    let header = disc.header();
    let format = disc.meta().format.to_string();
    let game_id = header.game_id_str().to_owned();
    let title = header.game_title_str().to_owned();
    let (supported, message) = if !header.is_wii() {
        (false, format!("{title} is a GameCube game, not Super Mario Galaxy."))
    } else if game_id != RMGE01_GAME_ID {
        (false, format!("{title} ({game_id}) is not the North American Super Mario Galaxy (RMGE01)."))
    } else {
        (true, format!("Super Mario Galaxy ({game_id}), {format}"))
    };
    Ok(Identity { format, game_id, title, supported, message })
}

fn read_u32_be(data: &[u8], offset: usize) -> Result<u32> {
    let bytes = data.get(offset..offset + 4).context("header field outside its data")?;
    Ok(u32::from_be_bytes(bytes.try_into().unwrap()))
}

fn read_u16_be(data: &[u8], offset: usize) -> Result<u16> {
    let bytes = data.get(offset..offset + 2).context("header field outside its data")?;
    Ok(u16::from_be_bytes(bytes.try_into().unwrap()))
}

fn read_u64_be(data: &[u8], offset: usize) -> Result<u64> {
    let bytes = data.get(offset..offset + 8).context("header field outside its data")?;
    Ok(u64::from_be_bytes(bytes.try_into().unwrap()))
}

fn validate_tmd(tmd: &[u8]) -> Result<u64> {
    if tmd.len() != TMD_SIZE || read_u32_be(tmd, 0)? != TMD_SIGNATURE_TYPE {
        bail!("the game partition's title metadata is not RMGE01's");
    }
    if read_u16_be(tmd, TMD_CONTENT_COUNT_OFFSET)? != 1
        || TMD_CONTENT_RECORD_OFFSET + TMD_CONTENT_RECORD_SIZE != tmd.len()
    {
        bail!("the game partition's title metadata does not hold exactly one content");
    }
    let id = read_u32_be(tmd, TMD_CONTENT_RECORD_OFFSET)?;
    let index = read_u16_be(tmd, TMD_CONTENT_RECORD_OFFSET + 4)?;
    let kind = read_u16_be(tmd, TMD_CONTENT_RECORD_OFFSET + 6)?;
    let size = read_u64_be(tmd, TMD_CONTENT_RECORD_OFFSET + 8)?;
    if id != 0 || index != 0 || kind != 3 || size != RMGE01_PARTITION_DATA_BYTES {
        bail!("the game partition's content record is not RMGE01's");
    }
    Ok(size)
}

struct Extent {
    label: String,
    start: u64,
    end: u64,
}

/// The furthest end of non-overlapping extents inside the partition: game.pak's logical size.
fn validate_extents(mut extents: Vec<Extent>, partition_size: u64) -> Result<u64> {
    extents.retain(|extent| extent.start != extent.end);
    extents.sort_by(|a, b| a.start.cmp(&b.start).then(a.end.cmp(&b.end)));
    for extent in &extents {
        if extent.end < extent.start || extent.end > partition_size {
            bail!("{} lies outside the game partition", extent.label);
        }
    }
    for pair in extents.windows(2) {
        if pair[0].end > pair[1].start {
            bail!("{} and {} overlap on the disc", pair[0].label, pair[1].label);
        }
    }
    Ok(extents.last().map_or(0, |extent| extent.end))
}

/// A sibling of `output` that `game.pak` is written to before it replaces the destination.
fn temporary_path(output: &Path) -> Result<PathBuf> {
    let name = output.file_name().context("the destination has no file name")?.to_string_lossy();
    Ok(output.with_file_name(format!(".{name}.importing")))
}

/// Builds `game.pak` at `output` from the disc. `progress(done, total)` reports bytes copied and returns false to stop.
/// The destination is replaced only when the whole image was written and flushed.
pub fn build_pak(disc: DiscFile, output: &Path, progress: &mut dyn FnMut(u64, u64) -> bool) -> Result<u64> {
    let disc = open_disc(disc)?;
    let header = disc.header();
    if !header.is_wii() || header.game_id_str() != RMGE01_GAME_ID {
        bail!("this disc is not the North American Super Mario Galaxy (RMGE01)");
    }
    let mut partition =
        disc.open_partition_kind(PartitionKind::Data).context("the disc's game partition cannot be opened")?;
    let meta = partition.meta().context("the game partition cannot be read")?;
    let boot = meta.raw_boot.as_ref();
    let bi2 = meta.raw_bi2.as_ref();
    let apploader = meta.raw_apploader.as_ref();
    let dol = meta.raw_dol.as_ref();
    let fst = meta.raw_fst.as_ref();
    let tmd = meta.raw_tmd.as_deref().context("the game partition has no title metadata")?;

    if boot.len() != BOOT_BIN_SIZE || &boot[..RMGE01_GAME_ID.len()] != RMGE01_GAME_ID.as_bytes() {
        bail!("the game partition's boot header is not RMGE01's");
    }
    if bi2.len() != BI2_BIN_SIZE || apploader.len() < APPLOADER_HEADER_SIZE {
        bail!("the game partition's system files have unexpected sizes");
    }
    let apploader_size =
        APPLOADER_HEADER_SIZE + read_u32_be(apploader, 0x14)? as usize + read_u32_be(apploader, 0x18)? as usize;
    if apploader.len() != apploader_size {
        bail!("the game partition's apploader has an unexpected size");
    }
    let dol_sha1 = format!("{:x}", Sha1::digest(dol));
    if dol_sha1 != RMGE01_DOL_SHA1 {
        bail!("main.dol differs from Super Mario Galaxy's (SHA-1 {dol_sha1}): the disc image may be damaged");
    }
    let dol_offset = u64::from(read_u32_be(boot, 0x420)?) << 2;
    let fst_offset = u64::from(read_u32_be(boot, 0x424)?) << 2;
    let fst_size = u64::from(read_u32_be(boot, 0x428)?) << 2;
    let fst_max = u64::from(read_u32_be(boot, 0x42c)?) << 2;
    if dol_offset == 0 || fst_offset == 0 || fst_size != fst.len() as u64 || fst_max < fst_size {
        bail!("the game partition's boot header does not match its file table");
    }
    let partition_size = validate_tmd(tmd)?;

    let table = nod::Fst::new(&meta.raw_fst).map_err(|error| anyhow::anyhow!("the file table is invalid: {error}"))?;
    let mut files: Vec<(u64, u64, String)> = Vec::new();
    for (index, node, name) in table.iter() {
        if node.is_file() {
            let name = name.map(|n| n.into_owned()).unwrap_or_else(|_| format!("file {index}"));
            files.push((node.offset(true), node.length(), name));
        }
    }
    let system: [(&str, u64, &[u8]); 5] = [
        ("boot.bin", 0, boot),
        ("bi2.bin", BI2_BIN_OFFSET, bi2),
        ("apploader.img", APPLOADER_OFFSET, apploader),
        ("main.dol", dol_offset, dol),
        ("fst.bin", fst_offset, fst),
    ];
    let mut extents: Vec<Extent> = system
        .iter()
        .map(|(label, start, bytes)| Extent { label: label.to_string(), start: *start, end: start + bytes.len() as u64 })
        .collect();
    if fst_max > fst_size {
        extents.push(Extent { label: "the file table's reserve".into(), start: fst_offset + fst_size, end: fst_offset + fst_max });
    }
    for (offset, length, name) in &files {
        extents.push(Extent { label: name.clone(), start: *offset, end: offset + length });
    }
    let logical_size = validate_extents(extents, partition_size)?;
    let total: u64 = files.iter().map(|(_, length, _)| length).sum::<u64>()
        + system.iter().map(|(_, _, bytes)| bytes.len() as u64).sum::<u64>();

    if let Some(parent) = output.parent() {
        fs::create_dir_all(parent).with_context(|| format!("{} cannot be created", parent.display()))?;
    }
    let temporary = temporary_path(output)?;
    let result = (|| -> Result<()> {
        let mut pak = File::create(&temporary).with_context(|| format!("{} cannot be created", temporary.display()))?;
        // Unwritten ranges stay holes: the file takes the game's 3.3 GB, not the partition's 4.3 GB.
        pak.set_len(logical_size).context("game.pak cannot be sized")?;
        let mut done = 0u64;
        if !progress(0, total) {
            bail!("the import was cancelled");
        }
        for (label, offset, bytes) in system {
            pak.seek(SeekFrom::Start(offset))?;
            pak.write_all(bytes).with_context(|| format!("{label} cannot be written"))?;
            done += bytes.len() as u64;
        }
        let mut buffer = vec![0u8; COPY_CHUNK];
        files.sort_by_key(|(offset, _, _)| *offset);
        for (offset, length, name) in &files {
            partition.seek(SeekFrom::Start(*offset)).with_context(|| format!("{name} cannot be read from the disc"))?;
            pak.seek(SeekFrom::Start(*offset))?;
            let mut left = *length;
            while left > 0 {
                let chunk = left.min(COPY_CHUNK as u64) as usize;
                partition
                    .read_exact(&mut buffer[..chunk])
                    .with_context(|| format!("{name} cannot be read from the disc"))?;
                pak.write_all(&buffer[..chunk]).with_context(|| format!("{name} cannot be written"))?;
                left -= chunk as u64;
                done += chunk as u64;
                if !progress(done, total) {
                    bail!("the import was cancelled");
                }
            }
        }
        pak.sync_all().context("game.pak cannot be flushed to storage")?;
        if pak.metadata()?.len() != logical_size {
            bail!("game.pak ended at an unexpected size");
        }
        Ok(())
    })();
    if let Err(error) = result {
        let _ = fs::remove_file(&temporary);
        return Err(error);
    }
    fs::rename(&temporary, output).with_context(|| format!("{} cannot be replaced", output.display()))?;
    Ok(logical_size)
}
