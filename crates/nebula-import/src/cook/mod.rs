//! The cooker (D-01): the game's files converted once, at import or on update, into what the native game reads.
//! Compressed files and archives are expanded, and every texture is a texture record (texture.rs, formats.rs). The
//! result is written as game.pak again: the partition's system files at their places, the file table with the cooked
//! files' new places and sizes, the files after it.

pub mod archive;
pub mod dol;
pub mod formats;
pub mod texture;

use anyhow::{bail, Context, Result};
use std::collections::BTreeMap;
use std::fs::{self, File};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::Path;

/// What a cook did: images by disc format, files rewritten, bytes in and out.
#[derive(Default, Debug)]
pub struct Stats {
    pub images: BTreeMap<String, u64>,
    pub cooked_files: u64,
    pub expanded_files: u64,
    pub bytes_in: u64,
    pub bytes_out: u64,
}

impl Stats {
    fn note(&mut self, image: &texture::Image) {
        *self.images.entry(format!("{:?}", image.format)).or_default() += 1;
    }

    pub fn summary(&self) -> String {
        let images: Vec<String> = self.images.iter().map(|(k, v)| format!("{k} {v}")).collect();
        format!(
            "{} files cooked, {} expanded, {} -> {} bytes; images: {}",
            self.cooked_files,
            self.expanded_files,
            self.bytes_in,
            self.bytes_out,
            images.join(", ")
        )
    }
}

fn extension(name: &str) -> String {
    name.rsplit_once('.').map_or(String::new(), |(_, e)| e.to_ascii_lowercase())
}

/// Whether the cooker reads a file of this name (the others are copied as they are).
pub fn cooks(name: &str) -> bool {
    matches!(extension(name).as_str(), "arc" | "szs" | "bti" | "bmd" | "bdl" | "tpl" | "brfnt" | "jpc")
}

/// The picture font keeps its sheets as the disc has them: Nebula's controller icons extend it with sheets painted in
/// the same format (Game/Screen/NebulaIcons.cpp), until they are painted as texture records.
fn is_picture_font(name: &str) -> bool {
    name.rsplit('/').next().is_some_and(|file| file.eq_ignore_ascii_case("PictureFont.brfnt"))
}

/// One file cooked: Some(new bytes), or None when it stays as it is.
pub fn cook_file(name: &str, bytes: Vec<u8>, stats: &mut Stats) -> Result<Option<Vec<u8>>> {
    let mut changed = false;
    let mut bytes = bytes;
    if archive::is_compressed(&bytes) {
        bytes = archive::expand(&bytes).with_context(|| format!("{name} cannot be expanded"))?;
        stats.expanded_files += 1;
        changed = true;
    }
    let cooked = if archive::is_archive(&bytes) {
        Some(archive::rebuild(&bytes, &mut |member, data| {
            cook_file(&format!("{name}/{member}"), data, stats)
        })?)
    } else if bytes.starts_with(b"J3D2bmd3") || bytes.starts_with(b"J3D2bdl4") {
        Some(formats::j3d_model(&bytes, stats).with_context(|| format!("{name}: model"))?)
    } else if bytes.starts_with(b"JPAC2-10") {
        Some(formats::jpc(&bytes, stats).with_context(|| format!("{name}: particles"))?)
    } else if bytes.starts_with(&[0x00, 0x20, 0xAF, 0x30]) {
        Some(formats::tpl(&bytes, stats).with_context(|| format!("{name}: layout textures"))?)
    } else if bytes.starts_with(b"RFNT") && !is_picture_font(name) {
        Some(formats::font(&bytes, stats).with_context(|| format!("{name}: font"))?)
    } else if extension(name) == "bti" && !texture::is_record(bytes.get(0x20..).unwrap_or(&[])) {
        Some(formats::bti(&bytes, stats).with_context(|| format!("{name}: texture"))?)
    } else {
        None
    };
    if cooked.is_some() {
        stats.cooked_files += 1;
    }
    Ok(match cooked {
        Some(cooked) => Some(cooked),
        None if changed => Some(bytes),
        None => None,
    })
}

/// A file of the partition's file table: its node's index, path, place and size.
pub struct PakFile {
    pub node: usize,
    pub path: String,
    pub offset: u64,
    pub length: u64,
}

/// The partition's file table's files, by node.
pub fn pak_files(fst: &[u8]) -> Result<Vec<PakFile>> {
    let count = u32::from_be_bytes(fst.get(8..12).context("an empty file table")?.try_into().unwrap()) as usize;
    let names = count * 12;
    if names > fst.len() {
        bail!("the file table is shorter than its node count");
    }
    let name = |n: usize| -> Result<String> {
        let at = names + (u32::from_be_bytes(fst[n * 12..n * 12 + 4].try_into().unwrap()) & 0x00FF_FFFF) as usize;
        let end = fst.get(at..).context("a file name past the table")?.iter().position(|b| *b == 0).context("a file name")?;
        Ok(String::from_utf8_lossy(&fst[at..at + end]).into_owned())
    };
    let mut files = Vec::new();
    let mut folders: Vec<(String, usize)> = Vec::new();
    for n in 1..count {
        while folders.last().is_some_and(|(_, end)| n >= *end) {
            folders.pop();
        }
        let word = |o: usize| u32::from_be_bytes(fst[n * 12 + o..n * 12 + o + 4].try_into().unwrap());
        let label = name(n)?;
        if fst[n * 12] != 0 {
            folders.push((label, word(8) as usize));
        } else {
            let mut path: String = folders.iter().map(|(f, _)| format!("{f}/")).collect();
            path.push_str(&label);
            files.push(PakFile { node: n, path, offset: (word(4) as u64) << 2, length: word(8) as u64 });
        }
    }
    Ok(files)
}

/// The system area of a game partition: boot.bin, bi2.bin, the apploader, main.dol and the file table at their
/// offsets, up to `end` (the file table's reserve included).
pub struct SystemArea {
    pub bytes: Vec<u8>,
    pub fst_offset: usize,
    pub fst_size: usize,
}

impl SystemArea {
    pub fn fst(&self) -> &[u8] {
        &self.bytes[self.fst_offset..self.fst_offset + self.fst_size]
    }
}

fn be32(bytes: &[u8], at: usize) -> u32 {
    u32::from_be_bytes(bytes[at..at + 4].try_into().unwrap())
}

const ALIGN: u64 = 32;

/// Writes a cooked game.pak at `output` from a system area and a reader of the files' bytes. `progress(done, total)`
/// reports the files' bytes read and returns false to stop.
pub fn write_cooked_pak(
    system: &SystemArea,
    read: &mut dyn FnMut(&PakFile) -> Result<Vec<u8>>,
    output: &Path,
    progress: &mut dyn FnMut(u64, u64) -> bool,
    stats: &mut Stats,
) -> Result<u64> {
    let files = pak_files(system.fst())?;
    let total: u64 = files.iter().map(|f| f.length).sum();
    let mut fst = system.fst().to_vec();
    let mut pak = File::create(output).with_context(|| format!("{} cannot be created", output.display()))?;
    pak.write_all(&system.bytes)?;
    let mut at = (system.bytes.len() as u64).div_ceil(ALIGN) * ALIGN;
    let mut done = 0u64;
    if !progress(0, total) {
        bail!("the import was cancelled");
    }
    // In disc order, so a reader of the source reads forwards.
    let mut order: Vec<&PakFile> = files.iter().collect();
    order.sort_by_key(|f| f.offset);
    for file in order {
        let bytes = read(file)?;
        if bytes.len() as u64 != file.length {
            bail!("{} is {} bytes, not the file table's {}", file.path, bytes.len(), file.length);
        }
        stats.bytes_in += file.length;
        let out = if cooks(&file.path) {
            cook_file(&file.path, bytes.clone(), stats)?.unwrap_or(bytes)
        } else {
            bytes
        };
        if at >> 2 > u32::MAX as u64 || out.len() > u32::MAX as usize {
            bail!("the cooked game is larger than a file table can address");
        }
        pak.seek(SeekFrom::Start(at))?;
        pak.write_all(&out).with_context(|| format!("{} cannot be written", file.path))?;
        fst[file.node * 12 + 4..file.node * 12 + 8].copy_from_slice(&((at >> 2) as u32).to_be_bytes());
        fst[file.node * 12 + 8..file.node * 12 + 12].copy_from_slice(&(out.len() as u32).to_be_bytes());
        stats.bytes_out += out.len() as u64;
        at = (at + out.len() as u64).div_ceil(ALIGN) * ALIGN;
        done += file.length;
        if !progress(done, total) {
            bail!("the import was cancelled");
        }
    }
    // The tables main.dol holds, as files of their own folder after the disc's (B-05): the file table grows, so it
    // moves after the files and boot.bin names its new place.
    let dol = system_dol(system)?;
    let game_id = String::from_utf8_lossy(&system.bytes[..6]).into_owned();
    let extra = dol::tables(dol, &game_id, stats)?;
    let mut placed = Vec::with_capacity(extra.len());
    for (_, bytes) in &extra {
        pak.seek(SeekFrom::Start(at))?;
        pak.write_all(bytes)?;
        placed.push(at);
        at = (at + bytes.len() as u64).div_ceil(ALIGN) * ALIGN;
    }
    let fst = append_folder(&fst, dol::FOLDER, &extra, &placed)?;
    let fst_at = at;
    pak.seek(SeekFrom::Start(fst_at))?;
    pak.write_all(&fst)?;
    at = (at + fst.len() as u64).div_ceil(ALIGN) * ALIGN;
    let words = |n: u64| ((n >> 2) as u32).to_be_bytes();
    pak.seek(SeekFrom::Start(0x424))?;
    pak.write_all(&words(fst_at))?;
    pak.write_all(&words(fst.len() as u64))?;
    pak.write_all(&words(fst.len() as u64))?;
    pak.set_len(at)?;
    pak.sync_all().context("game.pak cannot be flushed to storage")?;
    Ok(at)
}

/// main.dol in a system area (its header's sections give its length).
fn system_dol(system: &SystemArea) -> Result<&[u8]> {
    let start = (be32(&system.bytes, 0x420) as usize) << 2;
    let header = system.bytes.get(start..start + 0x100).context("main.dol lies past the system area")?;
    let mut end = 0x100usize;
    for s in 0..18 {
        let (offset, size) = (be32(header, s * 4) as usize, be32(header, 0x90 + s * 4) as usize);
        if size != 0 {
            end = end.max(offset + size);
        }
    }
    system.bytes.get(start..start + end).context("main.dol runs past the system area")
}

/// A file table with a folder of `files` added at the root, after every other node: their places `placed`.
fn append_folder(fst: &[u8], folder: &str, files: &[(String, Vec<u8>)], placed: &[u64]) -> Result<Vec<u8>> {
    let count = be32(fst, 8) as usize;
    let names_at = count * 12;
    let new_count = count + 1 + files.len();
    let mut out = Vec::with_capacity(fst.len() + (1 + files.len()) * 12 + 64);
    out.extend_from_slice(&fst[..names_at]);
    out[8..12].copy_from_slice(&(new_count as u32).to_be_bytes());
    // Names: the table's own, then the folder's and its files'.
    let mut names = fst[names_at..].to_vec();
    while names.last() == Some(&0) && names.len() > 1 && names[names.len() - 2] == 0 {
        names.pop();
    }
    let add_name = |names: &mut Vec<u8>, name: &str| -> u32 {
        let at = names.len() as u32;
        names.extend_from_slice(name.as_bytes());
        names.push(0);
        at
    };
    let folder_name = add_name(&mut names, folder);
    out.extend_from_slice(&(0x0100_0000 | folder_name).to_be_bytes());
    out.extend_from_slice(&0u32.to_be_bytes());
    out.extend_from_slice(&(new_count as u32).to_be_bytes());
    for ((name, bytes), at) in files.iter().zip(placed) {
        let name = add_name(&mut names, name);
        out.extend_from_slice(&name.to_be_bytes());
        out.extend_from_slice(&((at >> 2) as u32).to_be_bytes());
        out.extend_from_slice(&(bytes.len() as u32).to_be_bytes());
    }
    if names.len() > 0x00FF_FFFF {
        bail!("the file table's names outgrow it");
    }
    out.extend_from_slice(&names);
    out.resize((out.len() + 3) & !3, 0);
    Ok(out)
}

/// The system area of an existing game.pak (the content format before cooking).
pub fn pak_system_area(pak: &mut File) -> Result<SystemArea> {
    let mut boot = vec![0u8; 0x440];
    pak.seek(SeekFrom::Start(0))?;
    pak.read_exact(&mut boot).context("game.pak is too short for a game partition")?;
    let dol = (be32(&boot, 0x420) as usize) << 2;
    let fst_offset = (be32(&boot, 0x424) as usize) << 2;
    let fst_size = (be32(&boot, 0x428) as usize) << 2;
    let fst_max = (be32(&boot, 0x42C) as usize) << 2;
    if dol == 0 || fst_offset == 0 || fst_max < fst_size {
        bail!("game.pak's boot header does not describe a game partition");
    }
    let mut dol_header = vec![0u8; 0x100];
    pak.seek(SeekFrom::Start(dol as u64))?;
    pak.read_exact(&mut dol_header)?;
    let mut dol_end = dol + 0x100;
    for s in 0..18 {
        let (offset, size) = (be32(&dol_header, s * 4) as usize, be32(&dol_header, 0x90 + s * 4) as usize);
        if size != 0 {
            dol_end = dol_end.max(dol + offset + size);
        }
    }
    let end = (fst_offset + fst_max).max(dol_end);
    let mut bytes = vec![0u8; end];
    pak.seek(SeekFrom::Start(0))?;
    pak.read_exact(&mut bytes).context("game.pak ends inside its system files")?;
    Ok(SystemArea { bytes, fst_offset, fst_size })
}

/// Cooks the content folder `input` (a game.pak from before cooking, and the files beside it) into `output`.
pub fn cook_content(input: &Path, output: &Path, progress: &mut dyn FnMut(u64, u64) -> bool) -> Result<Stats> {
    if output.exists() {
        bail!("{} already exists", output.display());
    }
    let source = input.join("game.pak");
    let mut pak = File::open(&source).with_context(|| format!("{} cannot be opened", source.display()))?;
    let system = pak_system_area(&mut pak)?;
    fs::create_dir_all(output).with_context(|| format!("{} cannot be created", output.display()))?;
    let mut stats = Stats::default();
    let result = (|| {
        let target = output.join("game.pak");
        let mut read = |file: &PakFile| -> Result<Vec<u8>> {
            let mut bytes = vec![0u8; file.length as usize];
            pak.seek(SeekFrom::Start(file.offset))?;
            pak.read_exact(&mut bytes).with_context(|| format!("{} cannot be read from game.pak", file.path))?;
            Ok(bytes)
        };
        write_cooked_pak(&system, &mut read, &target, progress, &mut stats)?;
        write_beside_files(&target, output)
    })();
    if let Err(error) = result {
        let _ = fs::remove_dir_all(output);
        return Err(error);
    }
    Ok(stats)
}

/// The content folder's one file besides game.pak: the settings overlay draws its window frame from this archive.
pub const FILE_SELECT_ARCHIVE: &str = "LayoutData/FileSelect.arc";

/// files/LayoutData/FileSelect.arc beside a cooked game.pak, copied out of it.
pub fn write_beside_files(pak_path: &Path, output: &Path) -> Result<()> {
    let mut pak = File::open(pak_path)?;
    let system = pak_system_area(&mut pak)?;
    let files = pak_files(system.fst())?;
    let file = files
        .iter()
        .find(|f| f.path.eq_ignore_ascii_case(FILE_SELECT_ARCHIVE))
        .with_context(|| format!("the file table has no {FILE_SELECT_ARCHIVE}"))?;
    let mut bytes = vec![0u8; file.length as usize];
    pak.seek(SeekFrom::Start(file.offset))?;
    pak.read_exact(&mut bytes)?;
    let target = FILE_SELECT_ARCHIVE.split('/').fold(output.join("files"), |p, c| p.join(c));
    fs::create_dir_all(target.parent().unwrap())?;
    fs::write(&target, bytes).with_context(|| format!("{} cannot be written", target.display()))?;
    Ok(())
}

/// The runtime's call into the cooker for a mod's files (runtime/src/cooker_bridge.cpp): `name` (UTF-8, its disc
/// path) and `size` bytes at `data`. Returns 1 with the cooked bytes in `*out` (`*out_size` of them, freed with
/// nebula_cook_free), 0 when the file stays as it is, -1 when it cannot be cooked.
///
/// # Safety
/// `name` is a NUL-terminated string, `data` holds `size` bytes, `out` and `out_size` are writable.
#[no_mangle]
pub unsafe extern "C" fn nebula_cook_file(
    name: *const std::ffi::c_char,
    data: *const u8,
    size: usize,
    out: *mut *mut u8,
    out_size: *mut usize,
) -> i32 {
    let result = std::panic::catch_unwind(|| {
        let name = std::ffi::CStr::from_ptr(name).to_string_lossy().into_owned();
        let bytes = std::slice::from_raw_parts(data, size).to_vec();
        cook_file(&name, bytes, &mut Stats::default())
    });
    match result {
        Ok(Ok(Some(cooked))) => {
            let boxed = cooked.into_boxed_slice();
            *out_size = boxed.len();
            *out = Box::into_raw(boxed) as *mut u8;
            1
        }
        Ok(Ok(None)) => 0,
        _ => -1,
    }
}

/// One of the NebulaData files (cook/dol.rs) of a main.dol (`size` bytes at `dol`) for release `game_id`: the runtime
/// cooks a mod's patched main.dol with it. Returns 1 with the bytes in `*out` (`*out_size` of them, freed with
/// nebula_cook_free), 0 when that release has no such file, -1 when main.dol cannot be read.
///
/// # Safety
/// `game_id` and `name` are NUL-terminated strings, `dol` holds `size` bytes, `out` and `out_size` are writable.
#[no_mangle]
pub unsafe extern "C" fn nebula_cook_dol_table(
    dol: *const u8,
    size: usize,
    game_id: *const std::ffi::c_char,
    name: *const std::ffi::c_char,
    out: *mut *mut u8,
    out_size: *mut usize,
) -> i32 {
    let result = std::panic::catch_unwind(|| {
        let game_id = std::ffi::CStr::from_ptr(game_id).to_string_lossy().into_owned();
        let name = std::ffi::CStr::from_ptr(name).to_string_lossy().into_owned();
        let dol = std::slice::from_raw_parts(dol, size);
        dol::tables(dol, &game_id, &mut Stats::default()).map(|files| files.into_iter().find(|(n, _)| *n == name))
    });
    match result {
        Ok(Ok(Some((_, bytes)))) => {
            let boxed = bytes.into_boxed_slice();
            *out_size = boxed.len();
            *out = Box::into_raw(boxed) as *mut u8;
            1
        }
        Ok(Ok(None)) => 0,
        _ => -1,
    }
}

/// Frees what nebula_cook_file returned.
///
/// # Safety
/// `data` and `size` are a pair nebula_cook_file returned, freed once.
#[no_mangle]
pub unsafe extern "C" fn nebula_cook_free(data: *mut u8, size: usize) {
    if !data.is_null() {
        drop(Box::from_raw(std::ptr::slice_from_raw_parts_mut(data, size)));
    }
}
