//! The cooker (D-01): the game's files converted once, at import or on update, into what the native game reads.
//! Compressed files and archives are expanded, and every texture is a texture record (texture.rs, formats.rs). The
//! result is written as game.pak again: the partition's system files at their places, the file table with the cooked
//! files' new places and sizes, the files after it.

pub mod archive;
pub mod audio_data;
pub mod dol;
pub mod formats;
pub mod material;
pub mod native_data;
pub mod store;
pub mod texture;
pub mod thp;

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

/// A name the stages' placement tables take: no extension, or a numeric one (commonpathpointinfo.2).
fn placement_table_name(name: &str) -> bool {
    let file = name.rsplit('/').next().unwrap_or(name);
    match file.rsplit_once('.') {
        None => true,
        Some((_, ext)) => !ext.is_empty() && ext.bytes().all(|b| b.is_ascii_digit()),
    }
}

/// Whether the cooker reads a file of this name (the others are copied as they are).
pub fn cooks(name: &str) -> bool {
    matches!(extension(name).as_str(), "arc" | "szs" | "bti" | "bmd" | "bdl" | "bmt" | "cit" | "bas" | "brs" | "bme" | "bct" | "csw" | "bck" | "bca" | "btk" | "brk" | "bpk" | "bpa" | "btp" | "bva" | "blk" | "bla" | "bxk" | "bxa" | "tpl" | "brfnt" | "jpc" | "bcsv" | "tbl" | "pa" | "bcam" | "banmt" | "canm" | "thp" | "ast")
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
        let rebuilt = archive::rebuild(&bytes, &mut |member, data| cook_file(&format!("{name}/{member}"), data, stats))?;
        Some(native_data::rarc_host(&rebuilt).with_context(|| format!("{name}: archive (host order)"))?)
    } else if bytes.starts_with(b"J3D2bmd3") || bytes.starts_with(b"J3D2bdl4") {
        let textured = formats::j3d_model(&bytes, stats).with_context(|| format!("{name}: model"))?;
        Some(material::cook_model(&textured).with_context(|| format!("{name}: native model materials"))?)
    } else if native_data::is_j3d_model(&bytes) {
        Some(material::cook_model(&bytes).with_context(|| format!("{name}: native material table"))?)
    } else if native_data::is_j3d_animation(&bytes) {
        Some(native_data::j3d_animation(&bytes).with_context(|| format!("{name}: animation"))?)
    } else if bytes.starts_with(b"JPAC2-10") {
        let textured = formats::jpc(&bytes, stats).with_context(|| format!("{name}: particles"))?;
        Some(native_data::jpc_host(&textured).with_context(|| format!("{name}: particles (host order)"))?)
    } else if bytes.starts_with(&[0x00, 0x20, 0xAF, 0x30]) {
        let textured = formats::tpl(&bytes, stats).with_context(|| format!("{name}: layout textures"))?;
        Some(native_data::tpl_host(&textured).with_context(|| format!("{name}: layout textures (host order)"))?)
    } else if bytes.starts_with(b"RLYT") || bytes.starts_with(b"RLAN") {
        Some(material::layout::cook(&bytes).with_context(|| format!("{name}: native layout materials"))?)
    } else if bytes.starts_with(b"STRM") && extension(name) == "ast" {
        Some(audio_data::ast(&bytes).with_context(|| format!("{name}: stream"))?)
    } else if bytes.starts_with(b"THP\0") {
        Some(thp::movie(&bytes).with_context(|| format!("{name}: movie"))?)
    } else if bytes.starts_with(b"AA_<") {
        Some(native_data::baa(&bytes).with_context(|| format!("{name}: audio archive"))?)
    } else if bytes.starts_with(b"MESGbmg1") {
        Some(native_data::bmg(&bytes).with_context(|| format!("{name}: messages"))?)
    } else if matches!(extension(name).as_str(), "bcsv" | "tbl" | "pa" | "bcam" | "banmt") {
        native_data::bcsv(&bytes, true).with_context(|| format!("{name}: table"))?
    } else if placement_table_name(name) && native_data::bcsv(&bytes, false).ok().flatten().is_some()
    {
        native_data::bcsv(&bytes, false)?
    } else if extension(name) == "canm" {
        Some(native_data::canm(&bytes).with_context(|| format!("{name}: camera animation"))?)
    } else if extension(name) == "bas" {
        Some(native_data::bas(&bytes).with_context(|| format!("{name}: sound animation"))?)
    } else if extension(name) == "cit" {
        Some(native_data::cit(&bytes).with_context(|| format!("{name}: chord table"))?)
    } else if matches!(extension(name).as_str(), "brs" | "bme") || (extension(name) == "bmt" && !bytes.starts_with(b"J3D")) {
        Some(native_data::rhythm(&bytes, &extension(name)).with_context(|| format!("{name}: rhythm library file"))?)
    } else if matches!(extension(name).as_str(), "bct" | "csw") {
        Some(native_data::speaker(&bytes, &extension(name)).with_context(|| format!("{name}: speaker file"))?)
    } else if extension(name) == "kcl" {
        Some(native_data::kcl(&bytes).with_context(|| format!("{name}: collision"))?)
    } else if bytes.starts_with(b"RFNT") {
        let textured = formats::font(&bytes, stats).with_context(|| format!("{name}: font"))?;
        Some(native_data::font_native(&textured).with_context(|| format!("{name}: font (native layout)"))?)
    } else if name.rsplit('/').next().is_some_and(|file| file.eq_ignore_ascii_case("RFL_Res.dat")) {
        let textured = formats::rfl_res(&bytes, stats).with_context(|| format!("{name}: Mii parts"))?;
        Some(native_data::rfl_res_host(&textured).with_context(|| format!("{name}: Mii parts (host order)"))?)
    } else if extension(name) == "bti" {
        // A texture file whose image is a record already keeps it; every one's header turns host order.
        let textured = if texture::is_record(bytes.get(0x20..).unwrap_or(&[])) {
            bytes.clone()
        } else {
            formats::bti(&bytes, stats).with_context(|| format!("{name}: texture"))?
        };
        Some(native_data::bti_host(&textured).with_context(|| format!("{name}: texture (host order)"))?)
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
    let mut pak = File::create(output).with_context(|| format!("{} cannot be created", output.display()))?;
    pak.write_all(&[0u8; store::HEADER as usize])?;
    let mut at = store::HEADER.div_ceil(ALIGN) * ALIGN;
    let mut placed = Vec::with_capacity(files.len());
    let mut done = 0u64;
    if !progress(0, total) {
        bail!("the import was cancelled");
    }
    // AW files have no header: the audio archive's wave systems say which of their ranges hold PCM16.
    let mut pcm16 = std::collections::BTreeMap::new();
    for file in files.iter().filter(|f| f.path.ends_with("AudioRes/SMR.szs")) {
        pcm16 = audio_data::pcm16_ranges(&read(file)?).context("the audio archive's wave systems")?;
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
        // Whatever its name, a compressed file is expanded here: the game has no decompressor.
        let out = if cooks(&file.path) || archive::is_compressed(&bytes) {
            cook_file(&file.path, bytes.clone(), stats)?.unwrap_or(bytes)
        } else if let Some(ranges) = pcm16.get(&audio_data::wave_archive_key(&file.path)) {
            stats.cooked_files += 1;
            audio_data::wave_archive(&bytes, ranges).with_context(|| format!("{}: wave archive", file.path))?
        } else {
            bytes
        };
        pak.seek(SeekFrom::Start(at))?;
        pak.write_all(&out).with_context(|| format!("{} cannot be written", file.path))?;
        placed.push((file.node, store::Placed { path: file.path.clone(), position: at, size: out.len() as u64 }));
        stats.bytes_out += out.len() as u64;
        at = (at + out.len() as u64).div_ceil(ALIGN) * ALIGN;
        done += file.length;
        if !progress(done, total) {
            bail!("the import was cancelled");
        }
    }
    // A folder lists its files in the file table's order, whatever order their bytes are in.
    placed.sort_by_key(|(node, _)| *node);
    let mut placed: Vec<store::Placed> = placed.into_iter().map(|(_, file)| file).collect();
    // The tables main.dol holds, as files of their own folder (B-05).
    let dol = system_dol(system)?;
    let game_id = String::from_utf8_lossy(&system.bytes[..6]).into_owned();
    let extra = dol::tables(dol, &game_id, stats)?;
    for (name, bytes) in &extra {
        pak.seek(SeekFrom::Start(at))?;
        pak.write_all(bytes)?;
        placed.push(store::Placed { path: format!("{}/{name}", dol::FOLDER), position: at, size: bytes.len() as u64 });
        at = (at + bytes.len() as u64).div_ceil(ALIGN) * ALIGN;
    }
    let at = store::write_index(&mut pak, at, &placed, system.bytes[..8].try_into().unwrap())?;
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
    let bytes = store::read_file(pak_path, FILE_SELECT_ARCHIVE)?;
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

/// A mod's AW file (`size` bytes at `data`) with the PCM16 ranges the audio archive (`archive_size` bytes at `archive`)
/// names for it in host order. Returns 1 with the bytes in `*out` (`*out_size` of them, freed by nebula_cook_free), 0 when
/// it has none, -1 when either cannot be read.
///
/// # Safety
/// `name` is a NUL-terminated string, `data` and `archive` hold their sizes, `out` and `out_size` are writable.
#[no_mangle]
pub unsafe extern "C" fn nebula_cook_wave_archive(
    name: *const std::ffi::c_char,
    data: *const u8,
    size: usize,
    archive: *const u8,
    archive_size: usize,
    out: *mut *mut u8,
    out_size: *mut usize,
) -> i32 {
    let result = std::panic::catch_unwind(|| -> Result<Option<Vec<u8>>> {
        let name = std::ffi::CStr::from_ptr(name).to_string_lossy().into_owned();
        let ranges = audio_data::pcm16_ranges(std::slice::from_raw_parts(archive, archive_size))?;
        match ranges.get(&audio_data::wave_archive_key(&name)) {
            Some(list) => Ok(Some(audio_data::wave_archive(std::slice::from_raw_parts(data, size), list)?)),
            None => Ok(None),
        }
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
