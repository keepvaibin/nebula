//! The files a cooked source reads beside it: a model's buffers and images, the file a sound, movie or font names, a
//! screen pass's or surface shader's HLSL, a layout's textures and fonts. They come from disk, or, while a `Packed`
//! reading is held on this thread, from a packed mod's entries under a folder that holds nothing on disk: a `.luma`
//! is checked in place, with no file of it written (a check costs the same for each entry however many it has).
//! Inside that folder only the entries are seen; a path outside it is read from disk as always.

use std::cell::RefCell;
use std::collections::{BTreeSet, HashMap};
use std::io;
use std::path::{Component, Path, PathBuf};
use std::rc::Rc;

/// A packed mod's entries as a folder: by lowercase path (the names a mod has differ in more than case, L103), each
/// folder's children in their own case.
struct Packed {
    root: PathBuf,
    files: HashMap<String, String>,
    folders: HashMap<String, BTreeSet<String>>,
    read: Box<dyn Fn(&str) -> Option<Vec<u8>>>,
}

thread_local! {
    static PACKED: RefCell<Option<Rc<Packed>>> = const { RefCell::new(None) };
}

/// While held, paths under `root` are the entries `names` (relative, `/` between parts) and nothing else; `read` gives
/// an entry's bytes by its name, None when they do not read. Dropping it gives the earlier reading back.
pub struct Reading(Option<Rc<Packed>>);

impl Drop for Reading {
    fn drop(&mut self) {
        let earlier = self.0.take();
        PACKED.with(|p| *p.borrow_mut() = earlier);
    }
}

pub fn packed(root: &Path, names: impl IntoIterator<Item = String>, read: impl Fn(&str) -> Option<Vec<u8>> + 'static) -> Reading {
    let mut files = HashMap::new();
    let mut folders: HashMap<String, BTreeSet<String>> = HashMap::new();
    folders.insert(String::new(), BTreeSet::new());
    for name in names {
        let mut folder = String::new();
        let parts: Vec<&str> = name.split('/').collect();
        for (i, part) in parts.iter().enumerate() {
            folders.entry(folder.clone()).or_default().insert((*part).to_string());
            if !folder.is_empty() {
                folder.push('/');
            }
            folder.push_str(&part.to_lowercase());
            if i + 1 < parts.len() {
                folders.entry(folder.clone()).or_default();
            }
        }
        files.entry(folder).or_insert(name);
    }
    let packed = Rc::new(Packed { root: root.to_path_buf(), files, folders, read: Box::new(read) });
    Reading(PACKED.with(|p| p.borrow_mut().replace(packed)))
}

/// Where a path lies in the held reading: None when no reading is held or the path is outside its folder; Some(None)
/// for a path that climbs out of it from inside (nothing there); else its lowercase relative path.
fn inside(path: &Path) -> Option<(Rc<Packed>, Option<String>)> {
    let packed = PACKED.with(|p| p.borrow().clone())?;
    let rest = path.strip_prefix(&packed.root).ok()?;
    let mut parts: Vec<String> = Vec::new();
    for component in rest.components() {
        match component {
            Component::Normal(part) => parts.push(part.to_string_lossy().to_lowercase()),
            Component::CurDir => {}
            Component::ParentDir => {
                if parts.pop().is_none() {
                    return Some((packed, None));
                }
            }
            Component::Prefix(_) | Component::RootDir => return Some((packed, None)),
        }
    }
    Some((packed, Some(parts.join("/"))))
}

fn not_found() -> io::Error {
    io::Error::new(io::ErrorKind::NotFound, "no such entry")
}

/// A file's bytes, as `std::fs::read`.
pub fn read(path: &Path) -> io::Result<Vec<u8>> {
    match inside(path) {
        None => std::fs::read(path),
        Some((packed, at)) => {
            let name = at.and_then(|at| packed.files.get(&at).cloned()).ok_or_else(not_found)?;
            (packed.read)(&name).ok_or_else(|| io::Error::new(io::ErrorKind::InvalidData, "the entry does not read"))
        }
    }
}

/// Whether a path is a file, as `Path::is_file`.
pub fn is_file(path: &Path) -> bool {
    match inside(path) {
        None => path.is_file(),
        Some((packed, at)) => at.is_some_and(|at| packed.files.contains_key(&at)),
    }
}

/// Whether a path is a folder, as `Path::is_dir`.
pub fn is_dir(path: &Path) -> bool {
    match inside(path) {
        None => path.is_dir(),
        Some((packed, at)) => at.is_some_and(|at| packed.folders.contains_key(&at)),
    }
}

/// The paths of what a folder holds, files and folders, as `std::fs::read_dir` gives them (in no set order).
pub fn list(folder: &Path) -> io::Result<Vec<PathBuf>> {
    match inside(folder) {
        None => Ok(std::fs::read_dir(folder)?.flatten().map(|e| e.path()).collect()),
        Some((packed, at)) => {
            let children = at.and_then(|at| packed.folders.get(&at)).ok_or_else(not_found)?;
            Ok(children.iter().map(|name| folder.join(name)).collect())
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_packed_reading_is_a_folder_of_its_entries_and_nothing_else() {
        let dir = tempfile::tempdir().unwrap();
        let outside = dir.path().join("outside.txt");
        std::fs::write(&outside, b"disk").unwrap();
        let root = dir.path().join("packed");
        std::fs::create_dir(&root).unwrap();
        std::fs::write(root.join("on-disk.txt"), b"never seen").unwrap();
        let names = ["layers/base/files/Model.gltf", "layers/base/files/Model.bin", "layers/base/assets/sfx/Jump.wav", "layers/base/files/broken.bin"];
        {
            let _reading = packed(&root, names.iter().map(|n| n.to_string()), |name| (name != "layers/base/files/broken.bin").then(|| name.as_bytes().to_vec()));
            let files = root.join("layers").join("base").join("files");
            assert_eq!(read(&files.join("Model.bin")).unwrap(), b"layers/base/files/Model.bin");
            // Any case, and through `.` and `..` inside the folder.
            assert_eq!(read(&files.join("MODEL.BIN")).unwrap(), b"layers/base/files/Model.bin");
            assert_eq!(read(&files.join("./../files/Model.gltf")).unwrap(), b"layers/base/files/Model.gltf");
            assert_eq!(read(&files.join("broken.bin")).unwrap_err().kind(), io::ErrorKind::InvalidData);
            // Nothing on disk under the folder, nothing above it from inside, and disk outside it.
            assert_eq!(read(&root.join("on-disk.txt")).unwrap_err().kind(), io::ErrorKind::NotFound);
            assert!(read(&root.join("..").join("outside.txt")).is_err());
            assert_eq!(read(&outside).unwrap(), b"disk");
            assert!(is_file(&files.join("Model.gltf")) && !is_file(&files) && is_dir(&files) && is_dir(&root) && !is_dir(&files.join("Model.gltf")));
            assert!(is_file(&outside) && !is_file(&root.join("on-disk.txt")));
            let mut layer: Vec<PathBuf> = list(&root.join("layers").join("base")).unwrap();
            layer.sort();
            assert_eq!(layer, vec![root.join("layers").join("base").join("assets"), root.join("layers").join("base").join("files")]);
            assert!(list(&root.join("layers").join("none")).is_err());
        }
        // Dropped: the folder is the disk's again.
        assert_eq!(read(&root.join("on-disk.txt")).unwrap(), b"never seen");
    }
}
