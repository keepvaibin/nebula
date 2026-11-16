//! A store read back and checked: the index `store.rs` writes, as the game reads it (runtime/src/asset_store.cpp) and
//! further, because a store that passes the game's own check can still send it to the wrong bytes. `nebula-import verify`
//! runs every check here; `nebula-import list` walks the tree the index describes. Both open the file read-only and touch
//! only the index, so they run on a 4 GB store in under a second.

use anyhow::{bail, Context, Result};
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::path::Path;

use super::store::{path_hash, read_header, HEADER, VERSION};

/// A node's size in the index.
pub const NODE: u64 = 40;
/// A hash entry's size in the index.
pub const HASH: u64 = 16;
/// The game reads the index in one piece; a larger one is not a store (runtime/src/asset_store.cpp).
const INDEX_LIMIT: u64 = 1 << 30;

/// One entry of the node table.
#[derive(Clone, Copy, Debug)]
pub struct Node {
    pub position: u64,
    pub size: u64,
    pub name: u32,
    pub parent: u32,
    pub flags: u32,
    pub first: u32,
    pub children: u32,
}

impl Node {
    pub fn folder(&self) -> bool {
        self.flags & 1 != 0
    }
    pub fn archive(&self) -> bool {
        self.flags & 2 != 0
    }
    /// A file's NEBULA_ASSET_KIND_*, bits 8-15.
    pub fn kind(&self) -> u32 {
        (self.flags >> 8) & 0xFF
    }
}

/// A store's header, node table, hash table and names, held in memory. The files' bytes stay on disk.
pub struct Index {
    pub size: u64,
    pub version: u32,
    pub count: u32,
    pub nodes_at: u64,
    pub hashes_at: u64,
    pub names_at: u64,
    pub names_size: u64,
    pub game_id: String,
    pub revision: u8,
    pub nodes: Vec<Node>,
    /// (hash, node), sorted by hash where the store is one this build wrote.
    pub hashes: Vec<(u64, u32)>,
    names: Vec<u8>,
}

impl Index {
    /// Reads the index of a store. Fails only when the header does not describe an index inside the file; a table that is
    /// short or out of order still opens, so `verify` can name it.
    pub fn open(path: &Path) -> Result<Index> {
        let mut pak = File::open(path).with_context(|| format!("{} cannot be read", path.display()))?;
        let size = pak.metadata()?.len();
        if size < HEADER {
            bail!("{} is {size} bytes, shorter than a store's {HEADER}-byte header", path.display());
        }
        // The magic, then this build's version: a store of another version is imported again, not read half-way.
        let header = read_header(&mut pak, path)?;
        let word = |at: usize| u64::from_le_bytes(header[at..at + 8].try_into().unwrap());
        let half = |at: usize| u32::from_le_bytes(header[at..at + 4].try_into().unwrap());
        let (version, count) = (half(8), half(12));
        let (nodes_at, hashes_at, names_at, names_size) = (word(16), word(24), word(32), word(40));
        if nodes_at < HEADER || nodes_at >= size || size - nodes_at > INDEX_LIMIT {
            bail!("{}: the index at {nodes_at} is not inside the file", path.display());
        }
        let mut index = vec![0u8; (size - nodes_at) as usize];
        pak.seek(SeekFrom::Start(nodes_at))?;
        pak.read_exact(&mut index)?;
        let tables = count as usize;
        let mut nodes = Vec::with_capacity(tables);
        let mut hashes = Vec::with_capacity(tables);
        if count > 0 && u64::from(count) * NODE <= index.len() as u64 {
            for entry in index.chunks_exact(NODE as usize).take(tables) {
                nodes.push(Node {
                    position: u64::from_le_bytes(entry[0..8].try_into().unwrap()),
                    size: u64::from_le_bytes(entry[8..16].try_into().unwrap()),
                    name: u32::from_le_bytes(entry[16..20].try_into().unwrap()),
                    parent: u32::from_le_bytes(entry[20..24].try_into().unwrap()),
                    flags: u32::from_le_bytes(entry[24..28].try_into().unwrap()),
                    first: u32::from_le_bytes(entry[28..32].try_into().unwrap()),
                    children: u32::from_le_bytes(entry[32..36].try_into().unwrap()),
                });
            }
        }
        let hashes_len = u64::from(count) * HASH;
        if count > 0 && hashes_at >= nodes_at && hashes_at - nodes_at + hashes_len <= index.len() as u64 {
            let at = (hashes_at - nodes_at) as usize;
            for entry in index[at..].chunks_exact(HASH as usize).take(tables) {
                hashes.push((
                    u64::from_le_bytes(entry[0..8].try_into().unwrap()),
                    u32::from_le_bytes(entry[8..12].try_into().unwrap()),
                ));
            }
        }
        let names = if names_at >= nodes_at && names_at < size {
            index[(names_at - nodes_at) as usize..].to_vec()
        } else {
            Vec::new()
        };
        let game_id = String::from_utf8_lossy(&header[48..54]).trim_end_matches('\0').to_string();
        Ok(Index { size, version, count, nodes_at, hashes_at, names_at, names_size, game_id, revision: header[55], nodes, hashes, names })
    }

    /// A node's own name, without its folders.
    pub fn name(&self, node: &Node) -> String {
        let start = node.name as usize;
        if start >= self.names.len() {
            return String::new();
        }
        let end = self.names[start..].iter().position(|&b| b == 0).map_or(self.names.len(), |n| start + n);
        String::from_utf8_lossy(&self.names[start..end]).into_owned()
    }

    /// Every node's path from the root, in node order. Breadth-first order puts every parent before its children, so one
    /// pass builds them all; a node whose parent is not before it keeps its own name and `verify` says why.
    pub fn paths(&self) -> Vec<String> {
        let mut out = vec![String::new(); self.nodes.len()];
        for (index, node) in self.nodes.iter().enumerate() {
            if index == 0 {
                continue;
            }
            let parent = node.parent as usize;
            let name = self.name(node);
            out[index] = match parent {
                0 => name,
                _ if parent < index => format!("{}/{}", out[parent], name),
                _ => name,
            };
        }
        out
    }
}

/// The checks a verify ran, and what failed.
struct Checks {
    ran: usize,
    problems: Vec<String>,
}

impl Checks {
    fn check(&mut self, ok: bool, problem: impl Into<String>) {
        self.ran += 1;
        if !ok {
            let problem = problem.into();
            self.problems.push(problem);
        }
    }
}

/// What a verify found.
pub struct Report {
    pub store: Index,
    pub checks: usize,
    pub problems: Vec<String>,
    pub files: u64,
    pub folders: u64,
    pub archives: u64,
    pub bytes: u64,
    pub biggest: Option<(u64, String)>,
}

/// Checks a store the way the game loads it, then through its index: tables that meet, nodes that agree on their parents,
/// extents inside the file, and every node's path hashing to the entry the game will search for.
pub fn verify(path: &Path) -> Result<Report> {
    let store = Index::open(path)?;
    let (mut checks, mut files, mut folders, mut archives, mut bytes) = (Checks { ran: 0, problems: Vec::new() }, 0u64, 0u64, 0u64, 0u64);
    let mut biggest: Option<(u64, String)> = None;
    let count = u64::from(store.count);

    checks.check(store.version == VERSION, format!("the store is version {}, this build reads version {VERSION}", store.version));
    checks.check(store.count > 0, "the store holds no nodes");
    checks.check(store.nodes.len() as u32 == store.count, format!("the node table is {} of {} records", store.nodes.len(), store.count));
    checks.check(store.hashes.len() as u32 == store.count, format!("the hash table is {} of {} records", store.hashes.len(), store.count));
    checks.check(store.hashes_at == store.nodes_at + count * NODE, "the hash table does not follow the node table");
    checks.check(store.names_at == store.hashes_at + count * HASH, "the names do not follow the hash table");
    checks.check(store.names_size > 0, "the names are empty");
    checks.check(store.names_at + store.names_size <= store.size, "the names run past the end of the file");
    checks.check(
        store.names_at + store.names_size == store.size,
        format!("the index ends at {}, the file is {} bytes", store.names_at + store.names_size, store.size),
    );
    checks.check(store.names.last() == Some(&0), "the names do not end with a terminator");
    checks.check(
        store.nodes.first().map_or(false, |root| root.name == 0 && root.parent == 0 && root.folder()),
        "node 0 is not an empty root folder",
    );

    let paths = store.paths();
    let label = |index: usize| match index {
        0 => "the root".to_string(),
        _ => format!("{} ({index})", paths[index]),
    };
    let mut children_of = vec![0u32; store.nodes.len()];
    for (index, node) in store.nodes.iter().enumerate() {
        let at = label(index);
        checks.check(u64::from(node.name) < store.names_size, format!("{at}: the name is outside the names"));
        checks.check(
            (node.name as usize) < store.names.len() && store.names[node.name as usize..].contains(&0),
            format!("{at}: the name is not terminated"),
        );
        if index != 0 {
            checks.check(
                (node.parent as usize) < store.nodes.len() && node.parent as usize != index,
                format!("{at}: its parent {} is not another node", node.parent),
            );
            if (node.parent as usize) < store.nodes.len() {
                children_of[node.parent as usize] += 1;
            }
        }
        if node.folder() {
            checks.check(
                u64::from(node.first) + u64::from(node.children) <= count,
                format!("{at}: the children run past the table"),
            );
            folders += 1;
            archives += u64::from(node.archive());
        } else {
            checks.check(node.children == 0 && node.first == 0, format!("{at}: a file holds children"));
            let end = node.position.saturating_add(node.size);
            checks.check(
                node.position >= HEADER && end <= store.nodes_at,
                format!("{at}: bytes {}-{end} are outside the files", node.position),
            );
            files += 1;
            bytes += node.size;
            if biggest.as_ref().map_or(true, |(size, _)| node.size > *size) {
                biggest = Some((node.size, paths[index].clone()));
            }
        }
    }
    for (index, node) in store.nodes.iter().enumerate() {
        if index == 0 || !node.folder() {
            continue;
        }
        let children = children_of[index];
        checks.check(children == node.children, format!("{}: says {} children, {children} point at it", label(index), node.children));
    }
    for (index, node) in store.nodes.iter().enumerate() {
        let parent = node.parent as usize;
        if index == 0 || parent >= store.nodes.len() {
            continue;
        }
        let folder = store.nodes[parent];
        let at = label(index);
        checks.check(folder.folder(), format!("{at}: its parent is a file"));
        checks.check(
            (index as u64) >= u64::from(folder.first) && (index as u64) - u64::from(folder.first) < u64::from(folder.children),
            format!("{at}: its parent does not list it"),
        );
    }

    checks.check(store.hashes.windows(2).all(|pair| pair[0].0 < pair[1].0), "the hash table is not sorted with distinct hashes");
    let mut seen = vec![false; store.nodes.len()];
    for (hash, node) in &store.hashes {
        checks.check((*node as usize) < store.nodes.len(), format!("hash {hash:016x} names node {node}, which is not in the table"));
        if let Some(slot) = seen.get_mut(*node as usize) {
            *slot = true;
        }
    }
    checks.check(seen.iter().all(|&s| s), "a node is not in the hash table");
    for (index, path) in paths.iter().enumerate() {
        let wanted = path_hash(path);
        let found = store.hashes.binary_search_by_key(&wanted, |entry| entry.0);
        checks.check(
            matches!(found, Ok(at) if store.hashes[at].1 as usize == index),
            match found {
                Ok(_) => format!("{path} ({index}): its hash names another node"),
                Err(_) => format!("{path} ({index}): no hash entry hashes to its path"),
            },
        );
    }
    Ok(Report { store, checks: checks.ran, problems: checks.problems, files, folders, archives, bytes, biggest })
}

/// Prints the index: every node's path under `prefix`, a folder's with a trailing '/'. Writing through a buffer keeps a
/// 4 GB store's 31,000 paths one syscall each at most, and a closed pipe ends the list the way it ends `ls`.
pub fn list(path: &Path, prefix: &str, long: bool) -> Result<()> {
    use std::io::Write;
    let store = Index::open(path)?;
    if store.count == 0 || store.nodes.len() as u32 != store.count {
        bail!("{}: the node table is {} of {} records; run verify", path.display(), store.nodes.len(), store.count);
    }
    let paths = store.paths();
    let wanted = prefix.to_ascii_lowercase();
    let mut out = std::io::BufWriter::with_capacity(1 << 16, std::io::stdout().lock());
    for (index, node) in store.nodes.iter().enumerate() {
        if index == 0 || !paths[index].to_ascii_lowercase().starts_with(&wanted) {
            continue;
        }
        if node.folder() {
            writeln!(out, "{}", if long { format!("{:<10} {}/", "folder", paths[index]) } else { format!("{}/", paths[index]) })?;
        } else if long {
            writeln!(out, "{:>10} {}", node.size, paths[index])?;
        } else {
            writeln!(out, "{}", paths[index])?;
        }
    }
    out.flush()?;
    Ok(())
}

/// Prints what `verify` found.
pub fn print_report(path: &Path, report: &Report) {
    let store = &report.store;
    println!("{}: {} bytes, version {}, {} nodes, game {}", path.display(), store.size, store.version, store.count, store.game_id);
    println!(
        "index: nodes at {}, hashes at {}, names at {} ({} bytes); {} folders ({} archives), {} files, {} bytes of files",
        store.nodes_at, store.hashes_at, store.names_at, store.names_size, report.folders, report.archives, report.files, report.bytes
    );
    if let Some((size, path)) = &report.biggest {
        println!("biggest file: {path} ({size} bytes)");
    }
    println!("checks: {} ran, {} failed", report.checks, report.problems.len());
    for problem in &report.problems {
        println!("FAIL {problem}");
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::cook::store::{write_index, Placed};
    use std::io::Write;

    /// A store written by store.rs, which is the other end of D-01: two files in an archive's folder and one beside it.
    fn written() -> (tempfile::TempDir, std::path::PathBuf) {
        let directory = tempfile::tempdir().unwrap();
        let path = directory.path().join("game.pak");
        let mut pak = File::create(&path).unwrap();
        pak.write_all(&vec![0u8; 0x200]).unwrap();
        pak.seek(SeekFrom::Start(0x100)).unwrap();
        pak.write_all(b"sub").unwrap();
        pak.seek(SeekFrom::Start(0x140)).unwrap();
        pak.write_all(b"aa").unwrap();
        let file = |path: &str, position, size| Placed { path: path.to_string(), position, size, folder: false };
        write_index(
            &mut pak,
            0x200,
            &[file("x.arc/a.bin", 0x140, 2), file("x.arc/sub/b.bin", 0x100, 3), file("y.bin", 0x100, 1)],
            b"RMGE01\0\0",
        )
        .unwrap();
        (directory, path)
    }

    #[test]
    fn a_store_this_crate_writes_passes_every_check() {
        let (_directory, path) = written();
        let report = verify(&path).unwrap();
        assert!(report.problems.is_empty(), "{:?}", report.problems);
        assert_eq!(report.files, 3);
        assert_eq!(report.bytes, 6);
        assert_eq!(report.store.count, 6);
        assert!(report.checks > report.store.count as usize);
    }

    #[test]
    fn the_tree_gives_back_the_paths_the_cook_put_in() {
        let (_directory, path) = written();
        let mut paths = Index::open(&path).unwrap().paths();
        paths.sort();
        assert_eq!(paths, ["", "x", "x/a.bin", "x/sub", "x/sub/b.bin", "y.bin"]);
    }

    #[test]
    fn a_hash_entry_pointing_at_the_wrong_node_is_named() {
        let (_directory, path) = written();
        let store = Index::open(&path).unwrap();
        let mut pak = File::options().write(true).open(&path).unwrap();
        pak.seek(SeekFrom::Start(store.hashes_at + 8)).unwrap();
        pak.write_all(&0u32.to_le_bytes()).unwrap();
        pak.flush().unwrap();
        let report = verify(&path).unwrap();
        assert!(!report.problems.is_empty());
        assert!(report.problems.iter().any(|problem| problem.contains("its hash names another node") || problem.contains("not in the hash table")));
    }

    #[test]
    fn a_file_whose_bytes_are_outside_the_files_is_named() {
        let (_directory, path) = written();
        let store = Index::open(&path).unwrap();
        let mut pak = File::options().write(true).open(&path).unwrap();
        // The last node is the file this store puts deepest; its position past the index is a store the game would read as
        // bytes that are not the file's.
        pak.seek(SeekFrom::Start(store.nodes_at + (u64::from(store.count) - 1) * NODE)).unwrap();
        let mut record = [0u8; 40];
        record[..8].copy_from_slice(&u64::MAX.to_le_bytes());
        pak.write_all(&record).unwrap();
        pak.flush().unwrap();
        let report = verify(&path).unwrap();
        assert!(report.problems.iter().any(|problem| problem.contains("outside the files") || problem.contains("not terminated")));
    }
}
