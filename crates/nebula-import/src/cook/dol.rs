//! The data the game keeps in its executable rather than in disc files, cooked into files of the cooked disc's
//! NebulaData folder: the story-progress and galaxy-information tables (BCSV) and the error-message archive as main.dol
//! holds them (the archive cooked like any other). The game reads them as files (Game/System/NebulaDiscTables.cpp).

use super::{cook_entry, Stats, StoreItem};
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
/// The NebulaData files of a release's main.dol (`game_id`, boot.bin's six characters), cooked.
pub fn tables(dol: &[u8], game_id: &str, stats: &mut Stats) -> Result<Vec<(String, Vec<StoreItem>)>> {
    if !matches!(game_id, "RMGE01" | "RMGP01" | "RMGJ01") {
        bail!("{game_id} is not a release whose main.dol tables the cooker knows");
    }
    let mut files = Vec::new();
    for blob in &BLOBS {
        let bytes = at_address(dol, blob.address, blob.size)
            .with_context(|| format!("{} ({:#x}, {:#x} bytes) is not in main.dol", blob.name, blob.address, blob.size))?
            .to_vec();
        let name = format!("{FOLDER}/{}", blob.name);
        files.push((blob.name.to_string(), cook_entry(&name, bytes, stats)?));
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
    fn an_address_resolves_through_the_sections() {
        let image = dol(0x8000_0000, b"abcdefgh");
        assert_eq!(at_address(&image, 0x8000_0002, 3), Some(&b"cde"[..]));
        assert_eq!(at_address(&image, 0x8000_0006, 3), None);
        assert_eq!(at_address(&image, 0x9000_0000, 1), None);
    }
}
