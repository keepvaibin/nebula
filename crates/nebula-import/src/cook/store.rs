//! The game's asset store: game.pak as the game reads it (D-01). A 64-byte header, the files, then the index:
//! little-endian, 64-bit offsets, no disc layout. Read by runtime/src/asset_store.cpp.
//!
//! Header: magic, version u32, node count u32, nodes u64, hashes u64, names u64, names' size u64, game id 6 bytes, disc
//! number, revision.
//! Node, 40 bytes: position u64 and size u64 (a file's bytes; a folder's, from its first file's first byte to its last
//! one's end), name u32, parent u32, flags u32 (bit 0: folder; bit 1: an archive's folder, one block; bits 8-15: a
//! file's resource_kind), first child u32,
//! child count u32, reserved u32. Node 0 is the root; a folder's children are consecutive nodes. An archive of the
//! source is a folder whose files lie side by side, so the game has it as one block. Paths are store_path's: the cook
//! and the reads here take disc or mod paths.
//! Hash, 16 bytes: FNV-1a 64 of the lower-case path without a leading '/', node u32, reserved u32; sorted by hash.

use anyhow::{bail, Result};
use std::collections::{BTreeMap, VecDeque};
use std::fs::File;
use std::io::{Seek, SeekFrom, Write};

pub const MAGIC: &[u8; 8] = b"NBSTORE\0";
/// The store's contents' form, from the one file the game, Setup, the launcher and Android read: a store of another
/// version is imported again.
pub const VERSION: u32 = include!("../../../../runtime/include/galaxy/store_version.inc");
/// The files start past this.
pub const HEADER: u64 = 64;

/// A file in the store: its path from the root, with '/', and where its bytes are; or a folder with nothing in it.
pub struct Placed {
    pub path: String,
    pub position: u64,
    pub size: u64,
    pub folder: bool,
}

struct Node {
    name: String,
    parent: usize,
    file: Option<(u64, u64)>,
    archive: bool,
    children: Vec<usize>,
}

/// The store's name for a path of the disc or of a mod, where any part may be an archive ("X.arc/y"): an archive or
/// a compressed file loses its container's extension, and an archive in a stage's folder (StageData/<S>/<S>Scenario.arc)
/// leaves that folder for StageData (StageData/<S>Scenario), whose StageData/<S> is the stage's archive. A store path
/// maps to itself.
pub fn store_path(path: &str) -> String {
    let body = path.trim_start_matches('/');
    let mut out = path[..path.len() - body.len()].to_string();
    for (index, (part, _)) in store_parts(body).iter().enumerate() {
        if index != 0 {
            out.push('/');
        }
        out.push_str(part);
    }
    out
}

/// store_path's parts of a path without its leading '/', each with whether it is an archive (one block: a mods' store
/// replaces it whole, where a plain folder of theirs only adds to the game's).
fn store_parts(path: &str) -> Vec<(&str, bool)> {
    let is_archive = |part: &str| {
        let lower = part.to_ascii_lowercase();
        part.len() > 4 && (lower.ends_with(".arc") || lower.ends_with(".szs"))
    };
    let parts: Vec<&str> = path.split('/').collect();
    let stage_folder = parts.len() > 2 && parts[0].eq_ignore_ascii_case("StageData") && !is_archive(parts[1]) && is_archive(parts[2]);
    let mut out = Vec::with_capacity(parts.len());
    for (index, part) in parts.iter().enumerate() {
        if index == 1 && stage_folder {
            continue;
        }
        let archive = is_archive(part);
        out.push((if archive { &part[..part.len() - 4] } else { part }, archive));
    }
    out
}

/// Whether two disc, mod or store paths name the same place in a store.
pub fn same_path(a: &str, b: &str) -> bool {
    store_path(a).eq_ignore_ascii_case(&store_path(b))
}

/// What the game makes of a file, by its name: a node's flags bits 8-15 (nebula_game_api.h NEBULA_ASSET_KIND_*).
pub fn resource_kind(path: &str) -> u32 {
    let name = path.rsplit('/').next().unwrap_or(path);
    let Some((_, extension)) = name.rsplit_once('.') else { return 0 };
    match extension.to_ascii_lowercase().as_str() {
        "bdl" | "bmd" => 1,
        "bck" | "bca" => 2,
        "btk" => 3,
        "bpk" => 4,
        "btp" => 5,
        "blk" => 6,
        "brk" => 7,
        "bas" => 8,
        "bmt" => 9,
        "bva" => 10,
        "banmt" => 11,
        "bti" => 12,
        "brlyt" => 13,
        "brlan" => 14,
        _ => 0,
    }
}

/// FNV-1a 64 of a path, letters in lower case.
pub fn path_hash(path: &str) -> u64 {
    let mut hash = 0xCBF2_9CE4_8422_2325u64;
    for byte in path.bytes() {
        hash = (hash ^ byte.to_ascii_lowercase() as u64).wrapping_mul(0x0000_0100_0000_01B3);
    }
    hash
}

/// Writes the index at `at` and the header at 0; returns the store's size.
pub fn write_index(pak: &mut File, at: u64, files: &[Placed], game_id: &[u8; 8]) -> Result<u64> {
    let mut nodes = vec![Node { name: String::new(), parent: 0, file: None, archive: false, children: Vec::new() }];
    let mut folders: BTreeMap<String, usize> = BTreeMap::new();
    for file in files {
        let mut parent = 0usize;
        let mut prefix = String::new();
        let mut parts = store_parts(file.path.trim_start_matches('/')).into_iter().peekable();
        while let Some((part, archive)) = parts.next() {
            if !prefix.is_empty() {
                prefix.push('/');
            }
            prefix.push_str(&part.to_ascii_lowercase());
            if parts.peek().is_some() || file.folder {
                parent = match folders.get(&prefix) {
                    Some(&node) => node,
                    None => {
                        nodes.push(Node { name: part.to_string(), parent, file: None, archive: false, children: Vec::new() });
                        let node = nodes.len() - 1;
                        nodes[parent].children.push(node);
                        folders.insert(prefix.clone(), node);
                        node
                    }
                };
                nodes[parent].archive |= archive;
            } else {
                nodes.push(Node { name: part.to_string(), parent, file: Some((file.position, file.size)), archive: false, children: Vec::new() });
                let node = nodes.len() - 1;
                nodes[parent].children.push(node);
            }
        }
    }

    // Breadth first, so a folder's children are consecutive.
    let mut order = Vec::with_capacity(nodes.len());
    let mut new_index = vec![0u32; nodes.len()];
    let mut queue = VecDeque::from([0usize]);
    while let Some(node) = queue.pop_front() {
        new_index[node] = order.len() as u32;
        order.push(node);
        queue.extend(nodes[node].children.iter().copied());
    }

    // Each folder's range: its children came after it, so backwards each is known before its parent.
    let mut ranges: Vec<Option<(u64, u64)>> = nodes.iter().map(|n| n.file.map(|(at, size)| (at, at + size))).collect();
    for &node in order.iter().rev().filter(|n| **n != 0) {
        if let Some((start, end)) = ranges[node] {
            let parent = &mut ranges[nodes[node].parent];
            *parent = Some(parent.map_or((start, end), |(s, e)| (s.min(start), e.max(end))));
        }
    }

    let mut names = vec![0u8];
    let mut table = Vec::with_capacity(order.len() * 40);
    let mut hashes = Vec::with_capacity(order.len());
    let mut paths: Vec<String> = vec![String::new(); nodes.len()];
    for &node in &order {
        let n = &nodes[node];
        if node != 0 {
            let parent = &paths[n.parent];
            paths[node] = if parent.is_empty() { n.name.clone() } else { format!("{parent}/{}", n.name) };
        }
        let name_at = if n.name.is_empty() {
            0
        } else {
            let at = names.len() as u32;
            names.extend_from_slice(n.name.as_bytes());
            names.push(0);
            at
        };
        let (start, end) = ranges[node].unwrap_or((0, 0));
        let flags = if n.file.is_some() { resource_kind(&n.name) << 8 } else { 1 | (u32::from(n.archive) << 1) };
        let first = n.children.first().map_or(0, |c| new_index[*c]);
        table.extend_from_slice(&start.to_le_bytes());
        table.extend_from_slice(&(end - start).to_le_bytes());
        table.extend_from_slice(&name_at.to_le_bytes());
        table.extend_from_slice(&new_index[n.parent].to_le_bytes());
        table.extend_from_slice(&flags.to_le_bytes());
        table.extend_from_slice(&first.to_le_bytes());
        table.extend_from_slice(&(n.children.len() as u32).to_le_bytes());
        table.extend_from_slice(&0u32.to_le_bytes());
        hashes.push((path_hash(&paths[node]), new_index[node]));
    }
    hashes.sort_unstable();
    if hashes.windows(2).any(|w| w[0].0 == w[1].0) {
        bail!("two paths in the store have the same hash");
    }

    let nodes_at = at;
    let hashes_at = nodes_at + table.len() as u64;
    let names_at = hashes_at + hashes.len() as u64 * 16;
    pak.seek(SeekFrom::Start(nodes_at))?;
    pak.write_all(&table)?;
    for (hash, node) in &hashes {
        pak.write_all(&hash.to_le_bytes())?;
        pak.write_all(&node.to_le_bytes())?;
        pak.write_all(&0u32.to_le_bytes())?;
    }
    pak.write_all(&names)?;
    let end = names_at + names.len() as u64;

    let mut header = Vec::with_capacity(HEADER as usize);
    header.extend_from_slice(MAGIC);
    header.extend_from_slice(&VERSION.to_le_bytes());
    header.extend_from_slice(&(order.len() as u32).to_le_bytes());
    header.extend_from_slice(&nodes_at.to_le_bytes());
    header.extend_from_slice(&hashes_at.to_le_bytes());
    header.extend_from_slice(&names_at.to_le_bytes());
    header.extend_from_slice(&(names.len() as u64).to_le_bytes());
    header.extend_from_slice(game_id);
    header.resize(HEADER as usize, 0);
    pak.seek(SeekFrom::Start(0))?;
    pak.write_all(&header)?;
    Ok(end)
}

/// Room for `size` bytes of a store, when the store is that large: a damaged one may say any size.
fn room(pak: &File, size: u64) -> Result<Vec<u8>> {
    if size > pak.metadata()?.len() {
        bail!("the store is damaged: it says it holds more than it does");
    }
    Ok(vec![0u8; usize::try_from(size)?])
}

/// A file of a store on disk, by path.
pub fn read_file(pak_path: &std::path::Path, path: &str) -> Result<Vec<u8>> {
    use anyhow::Context;
    use std::io::Read;
    let mut pak = File::open(pak_path).with_context(|| format!("{} cannot be read", pak_path.display()))?;
    let mut header = [0u8; HEADER as usize];
    pak.read_exact(&mut header)?;
    if &header[..8] != MAGIC {
        bail!("{} is not an asset store", pak_path.display());
    }
    let word = |at: usize| u64::from_le_bytes(header[at..at + 8].try_into().unwrap());
    let count = u32::from_le_bytes(header[12..16].try_into().unwrap()) as u64;
    let (nodes_at, hashes_at) = (word(16), word(24));
    let wanted = path_hash(&store_path(path));
    let mut hashes = room(&pak, count * 16)?;
    pak.seek(SeekFrom::Start(hashes_at))?;
    pak.read_exact(&mut hashes)?;
    let entry = hashes
        .chunks_exact(16)
        .find(|e| u64::from_le_bytes(e[..8].try_into().unwrap()) == wanted)
        .with_context(|| format!("the store has no {path}"))?;
    let node = u32::from_le_bytes(entry[8..12].try_into().unwrap()) as u64;
    let mut record = [0u8; 16];
    pak.seek(SeekFrom::Start(nodes_at.saturating_add(node * 40)))?;
    pak.read_exact(&mut record)?;
    let position = u64::from_le_bytes(record[..8].try_into().unwrap());
    let size = u64::from_le_bytes(record[8..16].try_into().unwrap());
    let mut bytes = room(&pak, size)?;
    pak.seek(SeekFrom::Start(position))?;
    pak.read_exact(&mut bytes)?;
    Ok(bytes)
}

/// Every file under a folder of a store on disk, by its path from the folder.
pub fn read_folder(pak_path: &std::path::Path, path: &str) -> Result<Vec<(String, Vec<u8>)>> {
    let mut out = Vec::new();
    visit_folder(pak_path, path, &mut |relative, bytes| out.push((relative.to_string(), bytes)))?;
    Ok(out)
}

/// The same, one file at a time, for folders larger than memory.
pub fn visit_folder(pak_path: &std::path::Path, path: &str, visit: &mut dyn FnMut(&str, Vec<u8>)) -> Result<()> {
    use anyhow::Context;
    use std::io::Read;
    let mut pak = File::open(pak_path).with_context(|| format!("{} cannot be read", pak_path.display()))?;
    let mut header = [0u8; HEADER as usize];
    pak.read_exact(&mut header)?;
    if &header[..8] != MAGIC {
        bail!("{} is not an asset store", pak_path.display());
    }
    let word = |at: usize| u64::from_le_bytes(header[at..at + 8].try_into().unwrap());
    let count = u64::from(u32::from_le_bytes(header[12..16].try_into().unwrap()));
    let (nodes_at, hashes_at) = (word(16), word(24));
    let names_at = hashes_at.saturating_add(count * 16);
    let wanted = path_hash(&store_path(path));
    let mut hashes = room(&pak, count * 16)?;
    pak.seek(SeekFrom::Start(hashes_at))?;
    pak.read_exact(&mut hashes)?;
    let entry = hashes
        .chunks_exact(16)
        .find(|e| u64::from_le_bytes(e[..8].try_into().unwrap()) == wanted)
        .with_context(|| format!("the store has no {path}"))?;
    let top = u64::from(u32::from_le_bytes(entry[8..12].try_into().unwrap()));
    let mut folders = vec![(top, String::new())];
    // Each node is read once in a whole store: more means a damaged one whose folders hold themselves.
    let mut read = 0u64;
    while let Some((node, prefix)) = folders.pop() {
        read += 1;
        if read > count {
            bail!("the store is damaged: its folders hold themselves");
        }
        let mut record = [0u8; 40];
        pak.seek(SeekFrom::Start(nodes_at.saturating_add(node * 40)))?;
        pak.read_exact(&mut record)?;
        let first = u64::from(u32::from_le_bytes(record[28..32].try_into().unwrap()));
        let children = u64::from(u32::from_le_bytes(record[32..36].try_into().unwrap()));
        for child in first..first + children {
            let mut item = [0u8; 40];
            pak.seek(SeekFrom::Start(nodes_at.saturating_add(child * 40)))?;
            pak.read_exact(&mut item)?;
            let position = u64::from_le_bytes(item[..8].try_into().unwrap());
            let size = u64::from_le_bytes(item[8..16].try_into().unwrap());
            let name_at = u64::from(u32::from_le_bytes(item[16..20].try_into().unwrap()));
            let folder = u32::from_le_bytes(item[24..28].try_into().unwrap()) & 1 != 0;
            pak.seek(SeekFrom::Start(names_at.saturating_add(name_at)))?;
            let mut name = Vec::new();
            let mut byte = [0u8; 1];
            while pak.read_exact(&mut byte).is_ok() && byte[0] != 0 {
                name.push(byte[0]);
            }
            let relative = format!("{prefix}{}", String::from_utf8_lossy(&name));
            if folder {
                folders.push((child, format!("{relative}/")));
            } else {
                let mut bytes = room(&pak, size)?;
                pak.seek(SeekFrom::Start(position))?;
                pak.read_exact(&mut bytes)?;
                visit(&relative, bytes);
            }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_folder_is_read_with_its_files() {
        let directory = tempfile::tempdir().unwrap();
        let path = directory.path().join("store");
        let mut pak = File::create(&path).unwrap();
        pak.write_all(&vec![0u8; 0x200]).unwrap();
        pak.seek(SeekFrom::Start(0x100)).unwrap();
        pak.write_all(b"sub").unwrap();
        pak.seek(SeekFrom::Start(0x140)).unwrap();
        pak.write_all(b"aa").unwrap();
        let file = |path: &str, position, size| Placed { path: path.to_string(), position, size, folder: false };
        write_index(&mut pak, 0x200, &[file("x.arc/a.bin", 0x140, 2), file("x.arc/sub/b.bin", 0x100, 3), file("y.bin", 0x100, 1)], b"RMGE01\0\0").unwrap();
        drop(pak);
        let mut found = read_folder(&path, "X.arc").unwrap();
        found.sort();
        assert_eq!(found, [("a.bin".to_string(), b"aa".to_vec()), ("sub/b.bin".to_string(), b"sub".to_vec())]);
        assert!(read_folder(&path, "none").is_err());
    }

    #[test]
    fn archives_lose_their_extension_and_a_scenario_leaves_its_stage_folder() {
        for (disc, store) in [
            ("ObjectData/Kuribo.arc", "ObjectData/Kuribo"),
            ("ObjectData/Kuribo.ARC/kuribo.bdl", "ObjectData/Kuribo/kuribo.bdl"),
            ("/StageData/EggStarGalaxy.arc", "/StageData/EggStarGalaxy"),
            ("StageData/EggStarGalaxy.arc/jmp/placement", "StageData/EggStarGalaxy/jmp/placement"),
            ("StageData/EggStarGalaxy/EggStarGalaxyScenario.arc", "StageData/EggStarGalaxyScenario"),
            ("/StageData/EggStarGalaxy/EggStarGalaxyScenario.arc/x.bcsv", "/StageData/EggStarGalaxyScenario/x.bcsv"),
            ("AudioRes/SMR.szs", "AudioRes/SMR"),
            ("NebulaData/ErrorMessageArchive.arc/LayoutData/ErrorMessageWindow.arc/x.brlyt", "NebulaData/ErrorMessageArchive/LayoutData/ErrorMessageWindow/x.brlyt"),
            ("StageData/EggStarGalaxy/jmp/placement", "StageData/EggStarGalaxy/jmp/placement"),
            (".arc", ".arc"),
        ] {
            assert_eq!(store_path(disc), store);
            assert_eq!(store_path(store), store);
        }
        assert_eq!(resource_kind("x/kuribo.bdl"), 1);
        assert_eq!(resource_kind("walk.BCK"), 2);
        assert_eq!(resource_kind("camera/cameraparam.bcam"), 0);
        assert_eq!(resource_kind("flag.bti"), 12);
        assert_eq!(resource_kind("noextension"), 0);
    }

    #[test]
    fn a_files_kind_is_in_its_flags() {
        let directory = tempfile::tempdir().unwrap();
        let path = directory.path().join("store");
        let mut pak = File::create(&path).unwrap();
        let file = |path: &str| Placed { path: path.to_string(), position: 0x100, size: 1, folder: false };
        write_index(&mut pak, 0x200, &[file("o.arc/m.bdl"), file("o.arc/w.bck")], b"RMGE01\0\0").unwrap();
        drop(pak);
        let bytes = std::fs::read(&path).unwrap();
        let nodes = u64::from_le_bytes(bytes[16..24].try_into().unwrap()) as usize;
        let flags = |i: usize| u32::from_le_bytes(bytes[nodes + i * 40 + 24..nodes + i * 40 + 28].try_into().unwrap());
        assert_eq!((flags(1), flags(2), flags(3)), (3, 1 << 8, 2 << 8));
        assert_eq!(read_file(&path, "O.arc/m.bdl").unwrap(), [0]);
    }

    #[test]
    fn a_folder_holds_its_files_range_and_an_empty_one_stays() {
        let directory = tempfile::tempdir().unwrap();
        let path = directory.path().join("store");
        let mut pak = File::create(&path).unwrap();
        let file = |path: &str, position, size| Placed { path: path.to_string(), position, size, folder: false };
        let files = [
            file("x.arc/a.bin", 0x140, 2),
            file("x.arc/sub/b.bin", 0x100, 3),
            Placed { path: "empty.arc".to_string(), position: 0, size: 0, folder: true },
        ];
        write_index(&mut pak, 0x200, &files, b"RMGE01\0\0").unwrap();
        drop(pak);
        let bytes = std::fs::read(&path).unwrap();
        let word = |at: usize| u64::from_le_bytes(bytes[at..at + 8].try_into().unwrap());
        let nodes = word(16) as usize;
        let count = u32::from_le_bytes(bytes[12..16].try_into().unwrap()) as usize;
        // Breadth first: the root, x, empty (both archives), a.bin, sub, b.bin.
        assert_eq!(count, 6);
        let node = |i: usize| (word(nodes + i * 40), word(nodes + i * 40 + 8), u32::from_le_bytes(bytes[nodes + i * 40 + 24..nodes + i * 40 + 28].try_into().unwrap()));
        assert_eq!(node(1), (0x100, 0x42, 3));
        assert_eq!(node(2), (0, 0, 3));
        assert_eq!(node(3), (0x140, 2, 0));
        assert_eq!(node(4), (0x100, 3, 1));
    }

    /// A damaged store is refused: a file larger than the store, a folder that holds itself.
    #[test]
    fn a_damaged_store_is_refused() {
        let directory = tempfile::tempdir().unwrap();
        let path = directory.path().join("store");
        let mut pak = File::create(&path).unwrap();
        pak.write_all(&vec![0u8; 0x200]).unwrap();
        let file = |path: &str, position, size| Placed { path: path.to_string(), position, size, folder: false };
        write_index(&mut pak, 0x200, &[file("x.arc/a.bin", 0x140, 2), file("x.arc/sub/b.bin", 0x100, 3)], b"RMGE01\0\0").unwrap();
        drop(pak);
        let good = std::fs::read(&path).unwrap();
        let nodes = u64::from_le_bytes(good[16..24].try_into().unwrap()) as usize;
        // Breadth first: the root, x, a.bin, sub, b.bin.
        let mut huge = good.clone();
        huge[nodes + 2 * 40 + 8..nodes + 2 * 40 + 16].copy_from_slice(&(1u64 << 40).to_le_bytes());
        std::fs::write(&path, &huge).unwrap();
        assert!(read_file(&path, "x.arc/a.bin").is_err());
        assert!(read_folder(&path, "x.arc").is_err());
        let mut looped = good;
        looped[nodes + 3 * 40 + 28..nodes + 3 * 40 + 32].copy_from_slice(&3u32.to_le_bytes());
        std::fs::write(&path, &looped).unwrap();
        assert!(read_folder(&path, "x.arc").is_err());
    }
}
