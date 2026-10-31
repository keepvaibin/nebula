//! The data the game keeps in its executable rather than in disc files (B-05), cooked into files of the cooked disc's
//! NebulaData folder: the story-progress and galaxy-information tables (BCSV) and the error-message archive as main.dol
//! holds them (the archive cooked like any other), and for the North American release the Grand galaxies' comet
//! schedules and the event flags as native tables. The game reads them as files (Game/System/NebulaDiscTables.cpp).

use super::{cook_file, Stats};
use anyhow::{bail, Context, Result};

/// The folder the files go in, at the disc's root.
pub const FOLDER: &str = "NebulaData";

struct Blob {
    name: &'static str,
    address: u32,
    size: u32,
}

/// The three blocks every supported release keeps at these addresses (the Korean release, not supported, elsewhere).
const BLOBS: [Blob; 3] = [
    Blob { name: "StoryProgressTable.bcsv", address: 0x8053_C620, size: 0x1E0 },
    Blob { name: "GalaxyInfoTable.bcsv", address: 0x8053_C800, size: 0xD20 },
    Blob { name: "ErrorMessageArchive.arc", address: 0x8053_DFE0, size: 0x9AC0 },
];

/// RMGE01's comet schedules (GalaxyCometTimePaper: name, category; 8 bytes each) and event flags (GameEventFlag: 20
/// bytes each).
const COMET_TABLES: [(u32, u32); 6] =
    [(0x8053_AEF0, 0x88), (0x8053_AF78, 0x88), (0x8053_B000, 0x88), (0x8053_B088, 0xC8), (0x8053_B150, 0xC8), (0x8053_B218, 0xC8)];
const EVENT_FLAGS: (u32, u32) = (0x8053_B320, 0xEB0);

/// No string (a null name).
const NONE: u32 = u32::MAX;

fn be32(bytes: &[u8], at: usize) -> u32 {
    u32::from_be_bytes(bytes[at..at + 4].try_into().unwrap())
}

/// main.dol's bytes at a load address, `size` of them, when one section holds them all.
fn at_address(dol: &[u8], address: u32, size: u32) -> Option<&[u8]> {
    if dol.len() < 0x100 {
        return None;
    }
    (0..18).find_map(|s| {
        let (offset, base, length) = (be32(dol, s * 4), be32(dol, 0x48 + s * 4), be32(dol, 0x90 + s * 4));
        if length == 0 || address < base || address - base >= length || length - (address - base) < size {
            return None;
        }
        let start = (offset + (address - base)) as usize;
        dol.get(start..start + size as usize)
    })
}

/// The NUL-terminated string at a load address, its bytes as main.dol has them (Shift-JIS for the Japanese names;
/// None for 0).
fn string(dol: &[u8], address: u32) -> Result<Option<Vec<u8>>> {
    if address == 0 {
        return Ok(None);
    }
    let bytes = at_address(dol, address, 1).with_context(|| format!("a string at {address:#x} outside main.dol"))?;
    let base = bytes.as_ptr() as usize - dol.as_ptr() as usize;
    let end = dol[base..].iter().position(|b| *b == 0).context("a string without its end")?;
    Ok(Some(dol[base..base + end].to_vec()))
}

/// A native table's strings, after its entries: each a NUL-terminated byte string, named by its offset in the file.
struct Strings {
    pool: Vec<u8>,
}

impl Strings {
    fn add(&mut self, text: Option<Vec<u8>>) -> u32 {
        match text {
            None => NONE,
            Some(text) => {
                let at = self.pool.len() as u32;
                self.pool.extend_from_slice(&text);
                self.pool.push(0);
                at
            }
        }
    }

    /// The file: the head, then the strings, offsets in the head rebased past it (`offsets` lists where they sit).
    fn finish(self, mut head: Vec<u8>, offsets: &[usize]) -> Vec<u8> {
        let base = head.len() as u32;
        for &at in offsets {
            let value = u32::from_le_bytes(head[at..at + 4].try_into().unwrap());
            if value != NONE {
                head[at..at + 4].copy_from_slice(&(value + base).to_le_bytes());
            }
        }
        head.extend_from_slice(&self.pool);
        head
    }
}

/// The comet schedules: u32 tables (6); per table u32 entries, then the entries (u32 name, s32 category: 8 bytes,
/// little-endian); the names after all tables, each an offset from the file's start (u32::MAX: none).
fn comet_schedules(dol: &[u8]) -> Result<Vec<u8>> {
    let mut head = (COMET_TABLES.len() as u32).to_le_bytes().to_vec();
    let mut strings = Strings { pool: Vec::new() };
    let mut offsets = Vec::new();
    for (address, size) in COMET_TABLES {
        let table = at_address(dol, address, size).context("a comet schedule outside main.dol")?;
        head.extend_from_slice(&(size / 8).to_le_bytes());
        for entry in table.chunks_exact(8) {
            offsets.push(head.len());
            head.extend_from_slice(&strings.add(string(dol, be32(entry, 0))?).to_le_bytes());
            head.extend_from_slice(&entry[4..8].iter().rev().copied().collect::<Vec<u8>>());
        }
    }
    Ok(strings.finish(head, &offsets))
}

/// The event flags: u32 entries, then the entries (u32 name, u8 type, save, condition1, condition2, u32 unused, u32
/// condition3, u32 condition4: 20 bytes, little-endian); the strings after them, offsets from the file's start.
fn event_flags(dol: &[u8]) -> Result<Vec<u8>> {
    let (address, size) = EVENT_FLAGS;
    let table = at_address(dol, address, size).context("the event flags outside main.dol")?;
    let mut head = (size / 0x14).to_le_bytes().to_vec();
    let mut strings = Strings { pool: Vec::new() };
    let mut offsets = Vec::new();
    for e in table.chunks_exact(0x14) {
        offsets.push(head.len());
        head.extend_from_slice(&strings.add(string(dol, be32(e, 0))?).to_le_bytes());
        head.extend_from_slice(&e[4..8]);
        head.extend_from_slice(&be32(e, 8).to_le_bytes());
        offsets.push(head.len());
        head.extend_from_slice(&strings.add(string(dol, be32(e, 12))?).to_le_bytes());
        offsets.push(head.len());
        head.extend_from_slice(&strings.add(string(dol, be32(e, 16))?).to_le_bytes());
    }
    Ok(strings.finish(head, &offsets))
}

/// The NebulaData files of a release's main.dol (`game_id`, boot.bin's six characters), cooked.
pub fn tables(dol: &[u8], game_id: &str, stats: &mut Stats) -> Result<Vec<(String, Vec<u8>)>> {
    if !matches!(game_id, "RMGE01" | "RMGP01" | "RMGJ01") {
        bail!("{game_id} is not a release whose main.dol tables the cooker knows");
    }
    let mut files = Vec::new();
    for blob in &BLOBS {
        let bytes = at_address(dol, blob.address, blob.size)
            .with_context(|| format!("{} ({:#x}, {:#x} bytes) is not in main.dol", blob.name, blob.address, blob.size))?
            .to_vec();
        let name = format!("{FOLDER}/{}", blob.name);
        let bytes = cook_file(&name, bytes.clone(), stats)?.unwrap_or(bytes);
        files.push((blob.name.to_string(), bytes));
    }
    if game_id == "RMGE01" {
        files.push(("GalaxyCometSchedules.bin".to_string(), comet_schedules(dol)?));
        files.push(("GameEventFlags.bin".to_string(), event_flags(dol)?));
    }
    Ok(files)
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A DOL whose data section 7 (slot 7) loads `data` at `base`.
    fn dol(base: u32, data: &[u8]) -> Vec<u8> {
        let mut dol = vec![0u8; 0x100];
        dol[7 * 4..7 * 4 + 4].copy_from_slice(&0x100u32.to_be_bytes());
        dol[0x48 + 7 * 4..0x48 + 7 * 4 + 4].copy_from_slice(&base.to_be_bytes());
        dol[0x90 + 7 * 4..0x90 + 7 * 4 + 4].copy_from_slice(&(data.len() as u32).to_be_bytes());
        dol.extend_from_slice(data);
        dol
    }

    #[test]
    fn strings_resolve_through_the_sections_and_move_past_the_entries() {
        let mut data = vec![0u8; 0x40];
        data[0x20..0x24].copy_from_slice(b"Egg\0");
        // A Shift-JIS name (the event flags' Japanese ones) keeps its bytes: they are not UTF-8.
        data[0x28..0x2D].copy_from_slice(&[0x83, 0x6E, 0x83, 0x60, 0x00]);
        let image = dol(0x8000_0000, &data);
        assert_eq!(string(&image, 0x8000_0020).unwrap().as_deref(), Some(&b"Egg"[..]));
        assert_eq!(string(&image, 0x8000_0028).unwrap().as_deref(), Some(&[0x83u8, 0x6E, 0x83, 0x60][..]));
        assert_eq!(string(&image, 0).unwrap(), None);
        assert!(string(&image, 0x9000_0000).is_err());
        let mut strings = Strings { pool: Vec::new() };
        let mut head = 1u32.to_le_bytes().to_vec();
        head.extend_from_slice(&strings.add(Some(b"Egg".to_vec())).to_le_bytes());
        head.extend_from_slice(&strings.add(None).to_le_bytes());
        let file = strings.finish(head, &[4, 8]);
        assert_eq!(u32::from_le_bytes(file[4..8].try_into().unwrap()), 12);
        assert_eq!(&file[12..16], b"Egg\0");
        assert_eq!(u32::from_le_bytes(file[8..12].try_into().unwrap()), NONE);
    }
}
