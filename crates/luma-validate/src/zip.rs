//! Reads a zip's directory and its entries without unpacking anything to disk (luma-format 2). The directory is read
//! by hand, not through a zip library, so that every rule of section 2 can be checked and named: links and devices
//! (from the Unix mode in the external attributes), encryption, multi-disk, Zip64, sizes that lie, entries whose
//! data overlap (a zip bomb). Never panics: every offset is checked against the file's length.

use std::io::{Read, Seek, SeekFrom};

/// Where a `.luma`'s bytes come from: a file, or bytes in memory (the game's import may pass either).
pub trait Bytes {
    fn len(&self) -> u64;
    fn is_empty(&self) -> bool {
        self.len() == 0
    }
    /// Reads exactly `out.len()` bytes at `offset`; false when they are not all there.
    fn read_at(&mut self, offset: u64, out: &mut [u8]) -> bool;
}

impl Bytes for &[u8] {
    fn len(&self) -> u64 {
        <[u8]>::len(self) as u64
    }
    fn read_at(&mut self, offset: u64, out: &mut [u8]) -> bool {
        let Ok(start) = usize::try_from(offset) else { return false };
        match start.checked_add(out.len()).and_then(|end| self.get(start..end)) {
            Some(src) => {
                out.copy_from_slice(src);
                true
            }
            None => false,
        }
    }
}

pub struct FileBytes {
    file: std::fs::File,
    len: u64,
}

impl FileBytes {
    pub fn open(path: &std::path::Path) -> std::io::Result<Self> {
        let file = std::fs::File::open(path)?;
        let len = file.metadata()?.len();
        Ok(Self { file, len })
    }
}

impl Bytes for FileBytes {
    fn len(&self) -> u64 {
        self.len
    }
    fn read_at(&mut self, offset: u64, out: &mut [u8]) -> bool {
        if offset.checked_add(out.len() as u64).map_or(true, |end| end > self.len) {
            return false;
        }
        self.file.seek(SeekFrom::Start(offset)).is_ok() && self.file.read_exact(out).is_ok()
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Method {
    Stored,
    Deflate,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum EntryKind {
    File,
    Folder,
    Link,
    Device,
}

#[derive(Clone, Debug)]
pub struct Entry {
    /// The name as stored, decoded as UTF-8 (None when it is not UTF-8; `raw_name` keeps the bytes).
    pub name: Option<String>,
    pub raw_name: Vec<u8>,
    pub kind: EntryKind,
    /// None when the method is one a `.luma` may not use; `method_found` names it.
    pub method: Option<Method>,
    pub method_found: u16,
    pub encrypted: bool,
    pub crc: u32,
    pub packed: u64,
    pub unpacked: u64,
    /// Where the entry's data starts, after its local header (None when the local header is not readable).
    pub data_at: Option<u64>,
    /// The local header's own name differs from the directory's.
    pub local_name_differs: bool,
}

/// A problem with the zip as a whole or with one entry's structure. `what` is a phrase id.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Problem {
    /// Not a zip that can be opened: L100 with the phrase.
    NotZip(&'static str),
    /// Spans several disks: L101.
    MultiDisk,
    /// More entries than allowed: L105.
    TooManyEntries(u64),
}

pub const MAX_ENTRIES: u64 = 200_000;
const EOCD: u32 = 0x0605_4b50;
const EOCD64: u32 = 0x0606_4b50;
const EOCD64_LOCATOR: u32 = 0x0706_4b50;
const CENTRAL: u32 = 0x0201_4b50;
const LOCAL: u32 = 0x0403_4b50;

fn u16_at(b: &[u8], at: usize) -> u16 {
    b.get(at..at + 2).map_or(0, |s| u16::from_le_bytes([s[0], s[1]]))
}
fn u32_at(b: &[u8], at: usize) -> u32 {
    b.get(at..at + 4).map_or(0, |s| u32::from_le_bytes([s[0], s[1], s[2], s[3]]))
}
fn u64_at(b: &[u8], at: usize) -> u64 {
    b.get(at..at + 8).map_or(0, |s| u64::from_le_bytes(s.try_into().unwrap_or([0; 8])))
}

/// Reads the directory. Each entry's own problems (names, kinds, methods) are left to the rules to report, so that
/// every one is reported; only problems that stop the reading are returned as errors.
pub fn read_directory(bytes: &mut dyn Bytes) -> Result<Vec<Entry>, Problem> {
    let len = bytes.len();
    if len < 22 {
        return Err(Problem::NotZip("Zip.TooShort"));
    }
    // The end record is in the last 22 + 65535 bytes (its comment is at most 65535 long).
    let tail_len = len.min(22 + 65_535) as usize;
    let tail_at = len - tail_len as u64;
    let mut tail = vec![0u8; tail_len];
    if !bytes.read_at(tail_at, &mut tail) {
        return Err(Problem::NotZip("Zip.Unreadable"));
    }
    let Some(eocd) = (0..=tail_len - 22).rev().find(|&i| u32_at(&tail, i) == EOCD && i + 22 + u16_at(&tail, i + 20) as usize == tail_len)
    else {
        return Err(Problem::NotZip("Zip.NoEnd"));
    };
    let disk = u16_at(&tail, eocd + 4);
    let cd_disk = u16_at(&tail, eocd + 6);
    let mut entries_here = u16_at(&tail, eocd + 8) as u64;
    let mut entries_total = u16_at(&tail, eocd + 10) as u64;
    let mut cd_size = u32_at(&tail, eocd + 12) as u64;
    let mut cd_at = u32_at(&tail, eocd + 16) as u64;
    let mut disk = disk as u32;
    let mut cd_disk = cd_disk as u32;
    if entries_here == 0xffff || entries_total == 0xffff || cd_size == 0xffff_ffff || cd_at == 0xffff_ffff {
        // Zip64: the locator sits just before the end record.
        let locator_at = tail_at + eocd as u64;
        if locator_at < 20 {
            return Err(Problem::NotZip("Zip.BadZip64"));
        }
        let mut locator = [0u8; 20];
        if !bytes.read_at(locator_at - 20, &mut locator) || u32_at(&locator, 0) != EOCD64_LOCATOR {
            return Err(Problem::NotZip("Zip.BadZip64"));
        }
        if u32_at(&locator, 4) != 0 || u32_at(&locator, 16) > 1 {
            return Err(Problem::MultiDisk);
        }
        let record_at = u64_at(&locator, 8);
        let mut record = [0u8; 56];
        if !bytes.read_at(record_at, &mut record) || u32_at(&record, 0) != EOCD64 {
            return Err(Problem::NotZip("Zip.BadZip64"));
        }
        disk = u32_at(&record, 16);
        cd_disk = u32_at(&record, 20);
        entries_here = u64_at(&record, 24);
        entries_total = u64_at(&record, 32);
        cd_size = u64_at(&record, 40);
        cd_at = u64_at(&record, 48);
    }
    if disk != 0 || cd_disk != 0 || entries_here != entries_total {
        return Err(Problem::MultiDisk);
    }
    if entries_total > MAX_ENTRIES {
        return Err(Problem::TooManyEntries(entries_total));
    }
    if cd_at.checked_add(cd_size).map_or(true, |end| end > len) || cd_size > 64 * 1024 * 1024 {
        return Err(Problem::NotZip("Zip.BadDirectory"));
    }
    let mut cd = vec![0u8; cd_size as usize];
    if !bytes.read_at(cd_at, &mut cd) {
        return Err(Problem::NotZip("Zip.BadDirectory"));
    }
    let mut entries = Vec::with_capacity(entries_total as usize);
    let mut at = 0usize;
    for _ in 0..entries_total {
        if u32_at(&cd, at) != CENTRAL || at + 46 > cd.len() {
            return Err(Problem::NotZip("Zip.BadDirectory"));
        }
        let made_by_os = (u16_at(&cd, at + 4) >> 8) as u8;
        let flags = u16_at(&cd, at + 8);
        let method_found = u16_at(&cd, at + 10);
        let crc = u32_at(&cd, at + 16);
        let mut packed = u32_at(&cd, at + 20) as u64;
        let mut unpacked = u32_at(&cd, at + 24) as u64;
        let name_len = u16_at(&cd, at + 28) as usize;
        let extra_len = u16_at(&cd, at + 30) as usize;
        let comment_len = u16_at(&cd, at + 32) as usize;
        let start_disk = u16_at(&cd, at + 34);
        let external = u32_at(&cd, at + 38);
        let mut local_at = u32_at(&cd, at + 42) as u64;
        let name_at = at + 46;
        let extra_at = name_at + name_len;
        let next = extra_at + extra_len + comment_len;
        if next > cd.len() {
            return Err(Problem::NotZip("Zip.BadDirectory"));
        }
        let raw_name = cd[name_at..extra_at].to_vec();
        // Zip64 sizes and offset, in that order, only for the fields that are all ones.
        let mut e = extra_at;
        while e + 4 <= extra_at + extra_len {
            let id = u16_at(&cd, e);
            let size = u16_at(&cd, e + 2) as usize;
            if id == 0x0001 {
                let mut f = e + 4;
                if unpacked == 0xffff_ffff {
                    unpacked = u64_at(&cd, f);
                    f += 8;
                }
                if packed == 0xffff_ffff {
                    packed = u64_at(&cd, f);
                    f += 8;
                }
                if local_at == 0xffff_ffff {
                    local_at = u64_at(&cd, f);
                }
            }
            e += 4 + size;
        }
        if start_disk != 0 && start_disk != 0xffff {
            return Err(Problem::MultiDisk);
        }
        let is_folder_name = raw_name.last() == Some(&b'/');
        // Unix mode in the high half of the external attributes when made on Unix (3) or macOS (19). A link or a device
        // there is one whatever the entry says made it: unpackers that honour the high half do not all ask (a link
        // marked as made on MS-DOS would otherwise pass, and could point outside the mod's folder).
        let unix = made_by_os == 3 || made_by_os == 19;
        let high = (external >> 16) & 0o170000;
        let mode = if unix || matches!(high, 0o120000 | 0o020000 | 0o060000 | 0o010000 | 0o140000) { high } else { 0 };
        let kind = match mode {
            0o120000 => EntryKind::Link,
            0o020000 | 0o060000 | 0o010000 | 0o140000 => EntryKind::Device,
            0o040000 => EntryKind::Folder,
            _ if is_folder_name => EntryKind::Folder,
            _ => EntryKind::File,
        };
        let method = match method_found {
            0 => Some(Method::Stored),
            8 => Some(Method::Deflate),
            _ => None,
        };
        // Bit 0: encrypted; bit 6: strong encryption; method 99: AES.
        let encrypted = flags & 0x41 != 0 || method_found == 99;
        let mut local = [0u8; 30];
        let (data_at, local_name_differs) = if bytes.read_at(local_at, &mut local) && u32_at(&local, 0) == LOCAL {
            let local_name_len = u16_at(&local, 26) as u64;
            let local_extra_len = u16_at(&local, 28) as u64;
            let mut local_name = vec![0u8; local_name_len as usize];
            let same = bytes.read_at(local_at + 30, &mut local_name) && local_name == raw_name;
            (Some(local_at + 30 + local_name_len + local_extra_len), !same)
        } else {
            (None, false)
        };
        entries.push(Entry {
            name: String::from_utf8(raw_name.clone()).ok(),
            raw_name,
            kind,
            method,
            method_found,
            encrypted,
            crc,
            packed,
            unpacked,
            data_at,
            local_name_differs,
        });
        at = next;
    }
    if backslash_form(&entries) {
        for e in &mut entries {
            if let Some(name) = &mut e.name {
                *name = name.replace('\\', "/");
                if name.ends_with('/') && e.kind == EntryKind::File {
                    e.kind = EntryKind::Folder;
                }
            }
        }
    }
    Ok(entries)
}

/// Whether the archive writes "\" between folders throughout: some name has "\" and none has "/" (raw names, as
/// stored). .NET Framework's ZipFile, and so Windows PowerShell's Compress-Archive, write such archives. Its names
/// are read with "\" as "/" (`name`; `raw_name` stays as stored), and every name rule then runs on what is read, so
/// nothing gets in that "/" would not let in; the validator says so in a note (L108). An archive mixing the two keeps
/// its names as stored, refused (L102 Name.Backslash).
pub fn backslash_form(entries: &[Entry]) -> bool {
    entries.iter().all(|e| !e.raw_name.contains(&b'/')) && entries.iter().any(|e| e.raw_name.contains(&b'\\'))
}

/// The data ranges of two entries overlap (a zip bomb reuses one entry's data for many). Returns the pairs found,
/// by index, the first overlap of each entry only.
pub fn overlaps(entries: &[Entry]) -> Vec<(usize, usize)> {
    let mut ranges: Vec<(u64, u64, usize)> = entries
        .iter()
        .enumerate()
        .filter(|(_, e)| e.kind == EntryKind::File && e.packed > 0)
        .filter_map(|(i, e)| e.data_at.map(|start| (start, start.saturating_add(e.packed), i)))
        .collect();
    ranges.sort();
    ranges.windows(2).filter(|w| w[1].0 < w[0].1).map(|w| (w[0].2, w[1].2)).collect()
}

/// What unpacking one entry found.
#[derive(Debug, PartialEq, Eq)]
pub enum Unpacked {
    /// The bytes (only when asked to keep them), the count and whether the CRC matched.
    Done { bytes: Option<Vec<u8>>, size: u64, crc_ok: bool },
    /// More bytes than `limit` came out: stopped there.
    OverLimit,
    /// The data is not readable or not valid deflate.
    Broken,
}

/// Unpacks an entry, streaming, keeping the bytes only when `keep` (up to `limit` bytes, never more). The size and
/// the CRC are counted over every byte, so a stated size that lies is found without storing anything.
pub fn unpack(bytes: &mut dyn Bytes, entry: &Entry, keep: bool, limit: u64) -> Unpacked {
    use miniz_oxide::inflate::stream::{inflate, InflateState};
    use miniz_oxide::{DataFormat, MZFlush, MZStatus};
    let (Some(data_at), Some(method)) = (entry.data_at, entry.method) else { return Unpacked::Broken };
    let mut hasher = crc32fast::Hasher::new();
    let mut kept = Vec::new();
    let mut size = 0u64;
    let mut emit = |chunk: &[u8], size: &mut u64| -> bool {
        *size += chunk.len() as u64;
        if *size > limit {
            return false;
        }
        hasher.update(chunk);
        if keep {
            kept.extend_from_slice(chunk);
        }
        true
    };
    const CHUNK: u64 = 1 << 16;
    let mut read = 0u64;
    let mut input = vec![0u8; CHUNK as usize];
    match method {
        Method::Stored => {
            while read < entry.packed {
                let n = (entry.packed - read).min(CHUNK) as usize;
                if !bytes.read_at(data_at + read, &mut input[..n]) {
                    return Unpacked::Broken;
                }
                read += n as u64;
                if !emit(&input[..n], &mut size) {
                    return Unpacked::OverLimit;
                }
            }
        }
        Method::Deflate => {
            let mut state = InflateState::new_boxed(DataFormat::Raw);
            let mut output = vec![0u8; 1 << 16];
            let mut pending: Vec<u8> = Vec::new();
            // Set when a call made no progress: give it more packed bytes, never the same ones again.
            let mut starved = false;
            loop {
                if (pending.is_empty() || starved) && read < entry.packed {
                    let n = (entry.packed - read).min(CHUNK) as usize;
                    if !bytes.read_at(data_at + read, &mut input[..n]) {
                        return Unpacked::Broken;
                    }
                    read += n as u64;
                    pending.extend_from_slice(&input[..n]);
                }
                // Never MZFlush::Finish: on a first call it makes miniz_oxide inflate into `output` alone, with no
                // window to wrap, so an entry over 64 KiB whose packed bytes came in one read failed as damaged. The
                // stream's own end (StreamEnd) ends it.
                let result = inflate(&mut state, &pending, &mut output, MZFlush::None);
                pending.drain(..result.bytes_consumed);
                if !emit(&output[..result.bytes_written], &mut size) {
                    return Unpacked::OverLimit;
                }
                let progress = result.bytes_consumed > 0 || result.bytes_written > 0;
                starved = !progress;
                match result.status {
                    Ok(MZStatus::StreamEnd) => break,
                    // No progress with every packed byte given: the stream ends before its end mark.
                    Ok(_) | Err(miniz_oxide::MZError::Buf) if progress || read < entry.packed => {}
                    _ => return Unpacked::Broken,
                }
            }
        }
    }
    let crc_ok = hasher.finalize() == entry.crc;
    Unpacked::Done { bytes: keep.then_some(kept), size, crc_ok }
}

#[cfg(test)]
#[path = "zip_inflate_tests.rs"]
mod inflate_tests;
