//! The cooker (D-01): the game's files converted once, at import or on update, into what the native game reads.
//! Compressed files and archives are expanded, and every texture is a texture record (texture.rs, formats.rs). The
//! result is written as game.pak again: the partition's system files at their places, the file table with the cooked
//! files' new places and sizes, the files after it.

pub mod archive;
pub mod audio_data;
pub mod dol;
pub mod ghost;
pub mod gltf;
pub mod modern_audio;
pub mod formats;
pub mod material;
pub mod kcl;
pub mod merge;
pub mod mesh;
pub mod native_data;
pub mod overlay;
pub mod shader_source;
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
    matches!(extension(name).as_str(), "arc" | "szs" | "gst" | "bti" | "bmd" | "bdl" | "bmt" | "cit" | "bas" | "brs" | "bme" | "bct" | "csw" | "bck" | "bca" | "btk" | "brk" | "bpk" | "bpa" | "btp" | "bva" | "blk" | "bla" | "bxk" | "bxa" | "tpl" | "brfnt" | "jpc" | "bcsv" | "tbl" | "pa" | "bcam" | "banmt" | "canm" | "thp" | "ast")
}

/// A disc file only a console reads: the channel banner, the HOME menu's loadable module and the module list.
fn console_only(path: &str) -> bool {
    path == "opening.bnr" || path.starts_with("ModuleData/")
}

/// An archive member left behind by the tools that made the game; no code opens one.
fn tool_leftover(file: &str) -> bool {
    matches!(extension(file).as_str(), "ini" | "prj" | "xnim" | "dvw" | "dvmat" | "dck" | "dtp" | "backup" | "csv#")
}

/// What a source file (or a part of one) becomes in the store: bytes at a path below it, or a folder with no files.
pub struct StoreItem {
    pub path: String,
    /// Where it comes in the store's bytes against the others of its source file.
    pub order: usize,
    pub bytes: Option<Vec<u8>>,
}

/// Archives inside archives past this are refused (the game's own go one level down): each level can expand again.
const ARCHIVE_DEPTH_MAX: usize = 8;

/// One source file as the store holds it: itself cooked, or, an archive, a folder of its members cooked, side by side.
pub fn cook_entry(name: &str, bytes: Vec<u8>, stats: &mut Stats) -> Result<Vec<StoreItem>> {
    cook_entry_at(name, bytes, 0, stats)
}

fn cook_entry_at(name: &str, bytes: Vec<u8>, depth: usize, stats: &mut Stats) -> Result<Vec<StoreItem>> {
    let mut bytes = bytes;
    if archive::is_compressed(&bytes) {
        bytes = archive::expand(&bytes).with_context(|| format!("{name} cannot be expanded"))?;
        stats.expanded_files += 1;
    }
    if !archive::is_archive(&bytes) {
        let cooked = cook_file(name, bytes.clone(), stats)?.unwrap_or(bytes);
        return Ok(vec![StoreItem { path: String::new(), order: 0, bytes: Some(cooked) }]);
    }
    if depth >= ARCHIVE_DEPTH_MAX {
        bail!("{name}: archives inside archives more than {ARCHIVE_DEPTH_MAX} deep");
    }
    let mut items = Vec::new();
    for member in archive::members(&bytes).with_context(|| format!("{name}: archive"))? {
        let file = member.path.rsplit('/').next().unwrap_or(&member.path);
        if tool_leftover(file) {
            continue;
        }
        for item in cook_entry_at(&format!("{name}/{file}"), member.bytes, depth + 1, stats)? {
            let path = if item.path.is_empty() { member.path.clone() } else { format!("{}/{}", member.path, item.path) };
            items.push(StoreItem { path, order: member.offset, bytes: item.bytes });
        }
    }
    if items.is_empty() {
        items.push(StoreItem { path: String::new(), order: 0, bytes: None });
    }
    stats.cooked_files += 1;
    Ok(items)
}

/// Writes a source file's items at `at` in their order, each on a boundary; their places, in the items' own order.
fn place(pak: &mut File, at: &mut u64, path: &str, items: Vec<StoreItem>, stats: &mut Stats) -> Result<Vec<store::Placed>> {
    let mut by_order: Vec<usize> = (0..items.len()).collect();
    by_order.sort_by_key(|i| items[*i].order);
    let mut places = vec![(0u64, 0u64); items.len()];
    for i in by_order {
        if let Some(bytes) = &items[i].bytes {
            pak.seek(SeekFrom::Start(*at))?;
            pak.write_all(bytes).with_context(|| format!("{path}/{} cannot be written", items[i].path))?;
            places[i] = (*at, bytes.len() as u64);
            stats.bytes_out += bytes.len() as u64;
            *at = (*at + bytes.len() as u64).div_ceil(ALIGN) * ALIGN;
        }
    }
    Ok(items
        .iter()
        .zip(places)
        .map(|(item, (position, size))| store::Placed {
            path: if item.path.is_empty() { path.to_string() } else { format!("{path}/{}", item.path) },
            position,
            size,
            folder: item.bytes.is_none(),
        })
        .collect())
}

/// One file cooked: Some(new bytes), or None when it stays as it is. An archive is not one file (cook_entry).
pub fn cook_file(name: &str, bytes: Vec<u8>, stats: &mut Stats) -> Result<Option<Vec<u8>>> {
    let mut changed = false;
    let mut bytes = bytes;
    if archive::is_compressed(&bytes) {
        bytes = archive::expand(&bytes).with_context(|| format!("{name} cannot be expanded"))?;
        stats.expanded_files += 1;
        changed = true;
    }
    if archive::is_archive(&bytes) {
        bail!("{name} is an archive");
    }
    let cooked = if bytes.starts_with(b"J3D2bmd3") || bytes.starts_with(b"J3D2bdl4") {
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
    } else if extension(name) == "gst" {
        Some(ghost::host_order(&bytes).with_context(|| format!("{name}: ghost"))?)
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
    // AW files have no header: the audio archive's wave systems say where their waves lie.
    let mut waves = std::collections::BTreeMap::new();
    for file in files.iter().filter(|f| f.path.ends_with("AudioRes/SMR.szs")) {
        waves = audio_data::wave_layout(&read(file)?).context("the audio archive's wave systems")?;
    }
    // In disc order, so a reader of the source reads forwards.
    let mut order: Vec<&PakFile> = files.iter().collect();
    order.sort_by_key(|f| f.offset);
    for file in order {
        if console_only(&file.path) {
            done += file.length;
            continue;
        }
        let bytes = read(file)?;
        if bytes.len() as u64 != file.length {
            bail!("{} is {} bytes, not the file table's {}", file.path, bytes.len(), file.length);
        }
        stats.bytes_in += file.length;
        // Whatever its name, a compressed file is expanded here: the game has no decompressor.
        let items = if cooks(&file.path) || archive::is_compressed(&bytes) {
            cook_entry(&file.path, bytes, stats)?
        } else if let Some(places) = waves.get(&audio_data::wave_archive_key(&file.path)) {
            stats.cooked_files += 1;
            let cooked = audio_data::wave_archive(&bytes, places).with_context(|| format!("{}: wave archive", file.path))?;
            vec![StoreItem { path: String::new(), order: 0, bytes: Some(cooked) }]
        } else {
            vec![StoreItem { path: String::new(), order: 0, bytes: Some(bytes) }]
        };
        placed.extend(place(&mut pak, &mut at, &file.path, items, stats)?.into_iter().map(|p| (file.node, p)));
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
    for (name, items) in extra {
        placed.extend(place(&mut pak, &mut at, &format!("{}/{name}", dol::FOLDER), items, stats)?);
    }
    // Where the disc's waves lie: a cooked archive no longer says, and mods' wave archives need it (overlay.rs).
    let layout = audio_data::layout_text(&waves).into_bytes();
    let item = StoreItem { path: String::new(), order: 0, bytes: Some(layout) };
    placed.extend(place(&mut pak, &mut at, overlay::WAVE_LAYOUT, vec![item], stats)?);
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
    // The boot header's words stay readable in the area; its numbers are compared to the file before any is allocated.
    let end = (fst_offset + fst_max).max(dol_end).max(boot.len());
    if end as u64 > pak.metadata()?.len() {
        bail!("game.pak ends inside its system files");
    }
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
    let source_len = pak.metadata()?.len();
    let result = (|| {
        let target = output.join("game.pak");
        let mut read = |file: &PakFile| -> Result<Vec<u8>> {
            if file.offset + file.length > source_len {
                bail!("{} lies past game.pak's end", file.path);
            }
            let mut bytes = vec![0u8; file.length as usize];
            pak.seek(SeekFrom::Start(file.offset))?;
            pak.read_exact(&mut bytes).with_context(|| format!("{} cannot be read from game.pak", file.path))?;
            Ok(bytes)
        };
        write_cooked_pak(&system, &mut read, &target, progress, &mut stats).map(|_| ())
    })();
    if let Err(error) = result {
        let _ = fs::remove_dir_all(output);
        return Err(error);
    }
    Ok(stats)
}
