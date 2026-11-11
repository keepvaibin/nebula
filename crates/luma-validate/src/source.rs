//! One view of a mod's entries, from a `.luma` (file or bytes) or a folder laid out the same way.

use crate::zip::{self, Bytes, EntryKind, FileBytes};
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

#[derive(Clone, Debug)]
pub(crate) struct Item {
    /// The name, `/`-separated; for a name that is not UTF-8, its lossy form (and `utf8` is false).
    pub name: String,
    pub utf8: bool,
    pub kind: EntryKind,
    /// The size it states (a zip) or has (a folder).
    pub size: u64,
}

pub(crate) enum Backing<'a> {
    Zip { bytes: Box<dyn Bytes + 'a>, entries: Vec<zip::Entry> },
    Folder(PathBuf),
}

pub(crate) struct Mod<'a> {
    /// The name to show for the mod before its id is known: the file or folder name.
    pub shown: String,
    pub items: Vec<Item>,
    pub backing: Backing<'a>,
    /// Entries read in full by the rules that need them, by name.
    pub kept: BTreeMap<String, Vec<u8>>,
}

/// Why a mod could not be opened at all.
pub(crate) enum OpenError {
    /// The container's problem (L100, L101, L105).
    Zip(zip::Problem),
    /// The file or folder cannot be read: the phrase says why, the text is the path it happened at.
    Unreadable(&'static str, String),
}

impl<'a> Mod<'a> {
    pub fn open(source: &crate::Source<'a>) -> Result<Mod<'a>, (String, OpenError)> {
        match source {
            crate::Source::File(path) => {
                let shown = path.file_name().map_or_else(|| path.display().to_string(), |n| n.to_string_lossy().into_owned());
                let bytes = FileBytes::open(path).map_err(|e| (shown.clone(), OpenError::Unreadable(io_phrase(&e, "Open.File"), path.display().to_string())))?;
                Self::from_zip(shown, Box::new(bytes))
            }
            crate::Source::Bytes(data, name) => {
                Self::from_zip(name.to_string(), Box::new(*data))
            }
            crate::Source::Folder(path) => {
                let shown = path.file_name().map_or_else(|| path.display().to_string(), |n| n.to_string_lossy().into_owned());
                let mut items = Vec::new();
                walk(path, "", &mut items, 0).map_err(|(why, at)| (shown.clone(), OpenError::Unreadable(why, at)))?;
                items.sort_by(|a, b| a.name.cmp(&b.name));
                Ok(Mod { shown, items, backing: Backing::Folder(path.to_path_buf()), kept: BTreeMap::new() })
            }
        }
    }

    fn from_zip(shown: String, mut bytes: Box<dyn Bytes + 'a>) -> Result<Mod<'a>, (String, OpenError)> {
        let entries = zip::read_directory(bytes.as_mut()).map_err(|p| (shown.clone(), OpenError::Zip(p)))?;
        let items = entries
            .iter()
            .map(|e| {
                let name = match &e.name {
                    Some(n) => n.trim_end_matches('/').to_string(),
                    None => String::from_utf8_lossy(&e.raw_name).trim_end_matches('/').to_string(),
                };
                Item { name, utf8: e.name.is_some(), kind: e.kind.clone(), size: e.unpacked }
            })
            .collect();
        Ok(Mod { shown, items, backing: Backing::Zip { bytes, entries }, kept: BTreeMap::new() })
    }

    pub fn is_zip(&self) -> bool {
        matches!(self.backing, Backing::Zip { .. })
    }

    /// Reads entry `index` in full, at most `limit` bytes.
    pub fn read(&mut self, index: usize, limit: u64) -> Result<Vec<u8>, ReadError> {
        let item = &self.items[index];
        if item.size > limit {
            return Err(ReadError::OverLimit);
        }
        match &mut self.backing {
            Backing::Zip { bytes, entries } => match zip::unpack(bytes.as_mut(), &entries[index], true, limit) {
                zip::Unpacked::Done { bytes: Some(data), .. } => Ok(data),
                zip::Unpacked::OverLimit => Err(ReadError::OverLimit),
                _ => Err(ReadError::Broken),
            },
            Backing::Folder(root) => {
                let path = root.join(item.name.replace('/', std::path::MAIN_SEPARATOR_STR));
                std::fs::read(path).map_err(|_| ReadError::Broken)
            }
        }
    }

    /// Reads entry `index` (once) and keeps it for the other rules.
    pub fn keep(&mut self, index: usize, limit: u64) -> Result<&[u8], ReadError> {
        let name = self.items[index].name.clone();
        if !self.kept.contains_key(&name) {
            let data = self.read(index, limit)?;
            self.kept.insert(name.clone(), data);
        }
        Ok(&self.kept[&name])
    }

    pub fn index_of(&self, name: &str) -> Option<usize> {
        self.items.iter().position(|i| i.name == name && i.kind == EntryKind::File)
    }
}

#[derive(Debug, PartialEq, Eq)]
pub(crate) enum ReadError {
    OverLimit,
    Broken,
}

/// The phrase for an I/O error: the two a modder can act on by name, else the general one given.
fn io_phrase(e: &std::io::Error, general: &'static str) -> &'static str {
    match e.kind() {
        std::io::ErrorKind::NotFound => "Open.NotFound",
        std::io::ErrorKind::PermissionDenied => "Open.Denied",
        _ => general,
    }
}

/// Lists a folder: files, folders, and links (never followed). Depth is bounded so a loop of junctions cannot hang.
/// An error is the phrase saying why and the path it happened at.
fn walk(root: &Path, prefix: &str, items: &mut Vec<Item>, depth: u32) -> Result<(), (&'static str, String)> {
    if depth > 64 {
        return Err(("Open.TooDeep", prefix.to_string()));
    }
    let dir = if prefix.is_empty() { root.to_path_buf() } else { root.join(prefix) };
    let entries = std::fs::read_dir(&dir).map_err(|e| (io_phrase(&e, "Open.Folder"), dir.display().to_string()))?;
    for entry in entries {
        let entry = entry.map_err(|e| (io_phrase(&e, "Open.Folder"), dir.display().to_string()))?;
        let os_name = entry.file_name();
        let (part, utf8) = match os_name.to_str() {
            Some(s) => (s.to_string(), true),
            None => (os_name.to_string_lossy().into_owned(), false),
        };
        let name = if prefix.is_empty() { part.clone() } else { format!("{prefix}/{part}") };
        let meta = std::fs::symlink_metadata(entry.path()).map_err(|e| (io_phrase(&e, "Open.Folder"), entry.path().display().to_string()))?;
        let ft = meta.file_type();
        let kind = if ft.is_symlink() || is_reparse_point(&meta) {
            EntryKind::Link
        } else if ft.is_dir() {
            EntryKind::Folder
        } else if ft.is_file() {
            EntryKind::File
        } else {
            EntryKind::Device
        };
        items.push(Item { name: name.clone(), utf8, kind: kind.clone(), size: meta.len() });
        if kind == EntryKind::Folder {
            walk(root, &name, items, depth + 1)?;
        }
    }
    Ok(())
}

#[cfg(windows)]
fn is_reparse_point(meta: &std::fs::Metadata) -> bool {
    use std::os::windows::fs::MetadataExt;
    meta.file_attributes() & 0x400 != 0
}

#[cfg(not(windows))]
fn is_reparse_point(_: &std::fs::Metadata) -> bool {
    false
}
