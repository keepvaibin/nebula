//! The cooker (D-01): the game's files converted once, at import or on update, into what the native game reads.
//! Compressed files and archives are expanded, and every texture is a texture record (texture.rs, formats.rs). The
//! result is written as game.pak again: the partition's system files at their places, the file table with the cooked
//! files' new places and sizes, the files after it.

pub mod archive;
pub mod audio_data;
pub mod beside;
pub mod distance;
pub mod dol;
pub mod ghost;
pub mod gltf;
pub mod modern_audio;
pub mod formats;
pub mod flac;
pub mod fur;
pub mod material;
pub mod kcl;
pub mod layout_anim;
pub mod layout_json;
pub mod layout_patch;
pub mod post_shader;
pub mod camera;
pub mod message;
pub mod message_merge;
pub mod mii_parts;
pub mod merge;
pub mod clip;
#[cfg(feature = "proof-tools")]
mod proof_tests;
#[cfg(feature = "proof-tools")]
pub use proof_tests::{clip_census, model_census};
pub mod programs;
pub mod hit_record;
pub mod curves;
pub mod mesh_order;
pub mod model_ir;
pub mod limits;
pub mod model_write;
pub mod pipeline_list;
pub mod native_data;
pub mod effects;
pub mod overlay;
pub mod overlay_patches;
pub mod pack_texture;
pub mod particle_json;
pub mod particles;
pub mod placement_args;
pub mod clip_events;
pub mod font_cook;
pub mod ui_layout;
pub mod sound;
pub mod movie_cook;
pub mod sound_effect;
pub mod sound_sequence;
pub mod sound_stream;
pub mod store;
pub mod table;
pub mod table_keys;
pub mod patch;
pub mod refuse;
pub mod surface_shader;
pub mod table_columns;
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
    /// Disc files and archive members the store leaves out: only a console or the game's tools read them.
    pub left_out: Vec<LeftOut>,
    /// The pipelines the files cooked draw with (NebulaData/Pipelines.bin).
    pub pipelines: pipeline_list::Pipelines,
}

impl Stats {
    fn note(&mut self, image: &texture::Image) {
        *self.images.entry(format!("{:?}", image.format)).or_default() += 1;
    }

    /// Adds another cook's counts (a file cooked on a thread of its own).
    fn merge(&mut self, other: Stats) {
        for (format, count) in other.images {
            *self.images.entry(format).or_default() += count;
        }
        self.cooked_files += other.cooked_files;
        self.expanded_files += other.expanded_files;
        self.bytes_in += other.bytes_in;
        self.bytes_out += other.bytes_out;
        self.left_out.extend(other.left_out);
        self.pipelines.merge(other.pipelines);
    }

    pub fn summary(&self) -> String {
        let images: Vec<String> = self.images.iter().map(|(k, v)| format!("{k} {v}")).collect();
        #[allow(unused_mut)]
        let mut summary = format!(
            "{} files cooked, {} expanded, {} left out, {} -> {} bytes; images: {}",
            self.cooked_files,
            self.expanded_files,
            self.left_out.len(),
            self.bytes_in,
            self.bytes_out,
            images.join(", ")
        );
        #[cfg(feature = "proof-tools")]
        summary.push_str(&format!(
            "; tables proved: {}, reads agreeing: {}",
            table::proof::TABLES.load(std::sync::atomic::Ordering::Relaxed),
            table::proof::READS.load(std::sync::atomic::Ordering::Relaxed)
        ));
        #[cfg(feature = "proof-tools")]
        summary.push_str(&format!(
            "; collision models proved: {}, octree leaves agreeing: {}",
            kcl::proof::MODELS.load(std::sync::atomic::Ordering::Relaxed),
            kcl::proof::LEAVES.load(std::sync::atomic::Ordering::Relaxed)
        ));
        #[cfg(feature = "proof-tools")]
        summary.push_str(&format!(
            "; camera clips proved: {}, reads agreeing: {}",
            camera::proof::CLIPS.load(std::sync::atomic::Ordering::Relaxed),
            camera::proof::READS.load(std::sync::atomic::Ordering::Relaxed)
        ));
        #[cfg(feature = "proof-tools")]
        summary.push_str(&format!(
            "; message files proved: {}, reads agreeing: {}",
            message::proof::FILES.load(std::sync::atomic::Ordering::Relaxed),
            message::proof::READS.load(std::sync::atomic::Ordering::Relaxed)
        ));
        summary
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
        || fur::is_fur_text(name)
}

/// Each file of the player's disc as imported: path, size and XXH64 of its bytes, and main.dol's as `sys/main.dol`. A
/// whole-disc mod is read against it, so only what the mod changed is taken, with no second copy of the game.
pub const DISC_FILES_PATH: &str = "NebulaData/DiscFiles.tsv";

/// The disc record's text: a line per file, `path<TAB>size<TAB>hash` (16 hex digits), in disc order.
pub fn disc_files_table(rows: &[(String, u64, u64)]) -> Vec<u8> {
    let mut text = String::with_capacity(rows.len() * 64);
    for (path, size, hash) in rows {
        text.push_str(&format!("{path}\t{size}\t{hash:016x}\n"));
    }
    text.into_bytes()
}

/// The disc record by lower-case path: (size, hash).
pub fn read_disc_files(text: &str) -> std::collections::HashMap<String, (u64, u64)> {
    text.lines()
        .filter_map(|line| {
            let mut parts = line.split('\t');
            let (path, size, hash) = (parts.next()?, parts.next()?.parse().ok()?, u64::from_str_radix(parts.next()?, 16).ok()?);
            Some((path.to_ascii_lowercase(), (size, hash)))
        })
        .collect()
}

/// A disc file only a console reads: the channel banner, the HOME menu (its module, its library's files and each
/// language's layout), the module list, and the save data's banner and icon for the console's data manager.
pub fn console_only(path: &str) -> bool {
    path == "opening.bnr"
        || path.starts_with("ModuleData/")
        || path.starts_with("HomeButton2/")
        || path.ends_with("/LayoutData/HomeButton.arc")
        || path == "ObjectData/SaveIconBanner.arc"
}

/// A file the store leaves out, and why (a phrase of the validator's text, `Validator.<why>`).
#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub struct LeftOut {
    pub path: String,
    pub why: &'static str,
}

/// Why a disc file is left out (`console_only`).
pub const CONSOLE_ONLY: &str = "Cook.LeftOut.Console";
/// Why an archive member is left out (`tool_leftover`).
pub const TOOL_LEFTOVER: &str = "Cook.LeftOut.Tool";

/// An archive member left behind by the tools that made the game; no code opens one.
fn tool_leftover(file: &str) -> bool {
    matches!(extension(file).as_str(), "ini" | "prj" | "xnim" | "dvw" | "dvmat" | "dck" | "dtp" | "backup" | "csv#")
        || file.eq_ignore_ascii_case("collisionversion")
        || file.eq_ignore_ascii_case("debugmoveinfo")
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
            stats.left_out.push(LeftOut { path: format!("{name}/{file}"), why: TOOL_LEFTOVER });
            continue;
        }
        for item in cook_entry_at(&format!("{name}/{file}"), member.bytes, depth + 1, stats)? {
            // The Mii parts file holds the native parts under their own name.
            let member_path = if mii_parts::is_source(file) {
                match member.path.rsplit_once('/') {
                    Some((folder, _)) => format!("{folder}/{}", mii_parts::NAME),
                    None => mii_parts::NAME.to_string(),
                }
            } else {
                member.path.clone()
            };
            let path = if item.path.is_empty() { member_path } else { format!("{member_path}/{}", item.path) };
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
    // A texture's name starts with its file's stem; the names table keeps the file too (texture::name_record).
    let _naming = texture::NamingFile::set(name);
    let stem = name.rsplit('/').next().unwrap_or(name);
    let stem = stem.split('.').next().unwrap_or(stem);
    let cooked = if native_data::is_model_file(&bytes) {
        let source = model_ir::Source::of(&bytes);
        let ir = model_ir::read_file(&bytes, source, stats).with_context(|| format!("{name}: model"))?;
        Some(model_write::write(&ir, stem, &programs::written_or_unknown(), &mut stats.pipelines).with_context(|| format!("{name}: native model"))?)
    } else if native_data::is_animation_file(&bytes) {
        Some(clip::write(&clip::read(&bytes).with_context(|| format!("{name}: animation"))?))
    } else if bytes.starts_with(b"JPAC2-10") {
        let mut ir = particles::read_jpc(&bytes, stats).with_context(|| format!("{name}: particles"))?;
        particles::name_textures(&mut ir, stem);
        stats.pipelines.add_particles(&ir);
        Some(particles::write(&ir))
    } else if bytes.starts_with(&[0x00, 0x20, 0xAF, 0x30]) {
        let textured = formats::tpl(&bytes, stem, stats).with_context(|| format!("{name}: layout textures"))?;
        Some(native_data::tpl_host(&textured).with_context(|| format!("{name}: layout textures (host order)"))?)
    } else if bytes.starts_with(b"RLYT") {
        let ir = ui_layout::read_layout(&bytes).with_context(|| format!("{name}: layout"))?;
        Some(ui_layout::write_layout(&ir).with_context(|| format!("{name}: cooked layout"))?)
    } else if bytes.starts_with(b"RLAN") {
        let ir = ui_layout::read_animation(&bytes).with_context(|| format!("{name}: layout animation"))?;
        Some(ui_layout::write_animation(&ir).with_context(|| format!("{name}: cooked layout animation"))?)
    } else if bytes.starts_with(b"STRM") && extension(name) == "ast" {
        Some(audio_data::ast(&bytes).with_context(|| format!("{name}: stream"))?)
    } else if bytes.starts_with(b"THP\0") {
        Some(thp::movie(&bytes).with_context(|| format!("{name}: movie"))?)
    } else if bytes.starts_with(b"AA_<") || bytes.starts_with(b"NSND") {
        Some(sound::archive(&bytes).with_context(|| format!("{name}: audio archive"))?)
    } else if sound::is_sequence_path(name) {
        Some(sound::sequence(&bytes).with_context(|| format!("{name}: sequence"))?)
    } else if extension(name) == "bme" {
        Some(sound::me_program(&bytes).with_context(|| format!("{name}: ME program"))?)
    } else if bytes.starts_with(b"MESGbmg1") {
        Some(message::cook(&bytes).with_context(|| format!("{name}: messages"))?)
    } else if matches!(extension(name).as_str(), "bcsv" | "tbl" | "pa" | "bcam" | "banmt") {
        table::cook(&bytes, true).with_context(|| format!("{name}: table"))?
    } else if placement_table_name(name) && table::is_bcsv(&bytes) {
        table::cook(&bytes, false).with_context(|| format!("{name}: placement table"))?
    } else if extension(name) == "gst" {
        Some(ghost::cook(&bytes).with_context(|| format!("{name}: ghost"))?)
    } else if extension(name) == "canm" {
        Some(camera::cook(&bytes).with_context(|| format!("{name}: camera animation"))?)
    } else if extension(name) == "bas" {
        Some(native_data::bas(&bytes).with_context(|| format!("{name}: sound animation"))?)
    } else if extension(name) == "cit" {
        Some(native_data::cit(&bytes).with_context(|| format!("{name}: chord table"))?)
    } else if extension(name) == "brs" || (extension(name) == "bmt" && !bytes.starts_with(b"J3D")) {
        Some(native_data::rhythm(&bytes, &extension(name)).with_context(|| format!("{name}: rhythm library file"))?)
    } else if matches!(extension(name).as_str(), "bct" | "csw") {
        Some(native_data::speaker(&bytes, &extension(name)).with_context(|| format!("{name}: speaker file"))?)
    } else if extension(name) == "kcl" {
        Some(kcl::cook(&bytes).with_context(|| format!("{name}: collision"))?)
    } else if fur::is_fur_text(name) {
        Some(fur::cook(&bytes).with_context(|| format!("{name}: fur"))?)
    } else if bytes.starts_with(b"RFNT") {
        let textured = formats::font(&bytes, stem, stats).with_context(|| format!("{name}: font"))?;
        Some(native_data::font_native(&textured).with_context(|| format!("{name}: font (native layout)"))?)
    } else if mii_parts::is_source(name) {
        Some(mii_parts::cook(&bytes, stats).with_context(|| format!("{name}: Mii parts"))?)
    } else if extension(name) == "bti" {
        // A texture file whose image is a record already keeps it; every one's header turns host order.
        let textured = if texture::is_record(bytes.get(0x20..).unwrap_or(&[])) {
            bytes.clone()
        } else {
            formats::bti(&bytes, stem, stats).with_context(|| format!("{name}: texture"))?
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

/// Held by every cook that fills the shared lists (pipeline_list, texture::NAMES): the store's, the mods' store and the
/// run time's one file (cook_c), so none meets another's half-written.
pub static COOK_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

/// Writes a cooked game.pak at `output` from a system area and a reader of the files' bytes. `progress(done, total)`
/// reports the files' bytes read and returns false to stop.
pub fn write_cooked_pak(
    system: &SystemArea,
    read: &mut dyn FnMut(&PakFile) -> Result<Vec<u8>>,
    output: &Path,
    progress: &mut dyn FnMut(u64, u64) -> bool,
    stats: &mut Stats,
) -> Result<u64> {
    let _cook = COOK_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let files = pak_files(system.fst())?;
    programs::prepare_written_files(&files, read)?;
    // This cook's list, from none: each file's is merged into it as the file is written.
    stats.pipelines = pipeline_list::Pipelines::default();
    // Every named texture's row, for NebulaData/TextureNames.tsv; collecting ends with this cook, however it ends.
    struct Collecting;
    impl Drop for Collecting {
        fn drop(&mut self) {
            *texture::NAMES.lock().unwrap_or_else(|e| e.into_inner()) = None;
            hit_record::twins_clear();  // c83
        }
    }
    let _collecting = Collecting;
    *texture::NAMES.lock().unwrap_or_else(|e| e.into_inner()) = Some(Vec::new());
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
    let mut disc_rows = Vec::with_capacity(files.len() + 1);
    // The files are read here, one after another, and cooked on every core at once; each is written in its turn,
    // so game.pak is the same, byte for byte, as one cooked a file at a time.
    let waves = &waves;
    let cook_one = move |path: &str, bytes: Vec<u8>, stats: &mut Stats| -> Result<(Vec<StoreItem>, u64)> {
        let hash = texture::xxh64(&bytes);
        stats.bytes_in += bytes.len() as u64;
        // Whatever its name, a compressed file is expanded here: the game has no decompressor.
        let items = if cooks(path) || archive::is_compressed(&bytes) {
            cook_entry(path, bytes, stats)?
        } else if let Some(places) = waves.get(&audio_data::wave_archive_key(path)) {
            stats.cooked_files += 1;
            let cooked = audio_data::wave_archive(&bytes, places).with_context(|| format!("{path}: wave archive"))?;
            vec![StoreItem { path: String::new(), order: 0, bytes: Some(cooked) }]
        } else {
            vec![StoreItem { path: String::new(), order: 0, bytes: Some(bytes) }]
        };
        Ok((items, hash))
    };
    let workers = std::thread::available_parallelism().map_or(1, |n| n.get()).clamp(1, 16);
    // What is read and not yet written stays within this, the file being written next always let in.
    const IN_FLIGHT_BYTES: u64 = 256 << 20;
    type Cooked = (usize, Result<(Vec<StoreItem>, u64)>, Stats);
    let (job_tx, job_rx) = std::sync::mpsc::channel::<(usize, &PakFile, Vec<u8>)>();
    let (done_tx, done_rx) = std::sync::mpsc::channel::<Cooked>();
    let job_rx = std::sync::Mutex::new(job_rx);
    let job_rx = &job_rx;
    let cook_one = &cook_one;
    std::thread::scope(|scope| -> Result<()> {
        for _ in 0..workers {
            let done_tx = done_tx.clone();
            scope.spawn(move || loop {
                let job = job_rx.lock().unwrap_or_else(|e| e.into_inner()).recv();
                let Ok((index, file, bytes)) = job else { break };
                let mut stats = Stats::default();
                let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| cook_one(&file.path, bytes, &mut stats)))
                    .unwrap_or_else(|_| Err(anyhow::anyhow!("{} could not be cooked", file.path)));
                if done_tx.send((index, result, stats)).is_err() {
                    break;
                }
            });
        }
        drop(done_tx);
        // Ends with the job sender dropped, however it ends: the workers then stop and the scope can end.
        let job_tx = job_tx;
        let mut ready: BTreeMap<usize, (Result<(Vec<StoreItem>, u64)>, Stats)> = BTreeMap::new();
        let (mut next_read, mut next_write, mut in_flight) = (0usize, 0usize, 0u64);
        while next_write < order.len() {
            while next_read < order.len() && (next_read == next_write || in_flight < IN_FLIGHT_BYTES) {
                let file = order[next_read];
                if console_only(&file.path) {
                    let left_out = vec![LeftOut { path: file.path.clone(), why: CONSOLE_ONLY }];
                    ready.insert(next_read, (Ok((Vec::new(), 0)), Stats { left_out, ..Stats::default() }));
                } else {
                    let bytes = read(file)?;
                    if bytes.len() as u64 != file.length {
                        bail!("{} is {} bytes, not the file table's {}", file.path, bytes.len(), file.length);
                    }
                    in_flight += file.length;
                    job_tx.send((next_read, file, bytes)).map_err(|_| anyhow::anyhow!("the cook's workers stopped"))?;
                }
                next_read += 1;
                // Each one cooked meanwhile is written as soon as its turn comes.
                while let Ok((index, result, stats)) = done_rx.try_recv() {
                    ready.insert(index, (result, stats));
                }
                if ready.contains_key(&next_write) {
                    break;
                }
            }
            while !ready.contains_key(&next_write) {
                let (index, result, stats) = done_rx.recv().map_err(|_| anyhow::anyhow!("the cook's workers stopped"))?;
                ready.insert(index, (result, stats));
            }
            while let Some((result, file_stats)) = ready.remove(&next_write) {
                let file = order[next_write];
                stats.merge(file_stats);
                if console_only(&file.path) {
                    done += file.length;
                } else {
                    let (items, hash) = result?;
                    in_flight -= file.length;
                    disc_rows.push((file.path.clone(), file.length, hash));
                    placed.extend(place(&mut pak, &mut at, &file.path, items, stats)?.into_iter().map(|p| (file.node, p)));
                    done += file.length;
                    if !progress(done, total) {
                        bail!("the import was cancelled");
                    }
                }
                next_write += 1;
            }
        }
        Ok(())
    })?;
    // A folder lists its files in the file table's order, whatever order their bytes are in.
    placed.sort_by_key(|(node, _)| *node);
    let mut placed: Vec<store::Placed> = placed.into_iter().map(|(_, file)| file).collect();
    // The tables main.dol holds, as files of their own folder (B-05).
    let dol = system_dol(system)?;
    disc_rows.push(("sys/main.dol".to_string(), dol.len() as u64, texture::xxh64(dol)));
    let item = StoreItem { path: String::new(), order: 0, bytes: Some(disc_files_table(&disc_rows)) };
    placed.extend(place(&mut pak, &mut at, DISC_FILES_PATH, vec![item], stats)?);
    let game_id = String::from_utf8_lossy(&system.bytes[..6]).into_owned();
    let extra = dol::tables(dol, &game_id, stats)?;
    for (name, items) in extra {
        placed.extend(place(&mut pak, &mut at, &format!("{}/{name}", dol::FOLDER), items, stats)?);
    }
    // Where the disc's waves lie: a cooked archive no longer says, and mods' wave archives need it (overlay.rs).
    let layout = audio_data::layout_text(&waves).into_bytes();
    let item = StoreItem { path: String::new(), order: 0, bytes: Some(layout) };
    placed.extend(place(&mut pak, &mut at, overlay::WAVE_LAYOUT, vec![item], stats)?);
    let item = StoreItem { path: String::new(), order: 0, bytes: Some(stats.pipelines.checked_bytes()?) };
    placed.extend(place(&mut pak, &mut at, pipeline_list::PATH, vec![item], stats)?);
    // The textures' names: what mod import turns a Dolphin pack's files into, and what the dump names files by.
    let mut rows = texture::NAMES.lock().unwrap_or_else(|e| e.into_inner()).take().unwrap_or_default();
    let item = StoreItem { path: String::new(), order: 0, bytes: Some(texture::names_table(&mut rows)) };
    placed.extend(place(&mut pak, &mut at, texture::NAMES_PATH, vec![item], stats)?);
    // Ray tracing's tables: each stage's numbers and the particle effects' light (rt_materials.h set_rt_tables).
    for (path, text) in [(RT_STAGES_PATH, RT_STAGES), (RT_EFFECTS_PATH, RT_EFFECTS)] {
        let item = StoreItem { path: String::new(), order: 0, bytes: Some(text.as_bytes().to_vec()) };
        placed.extend(place(&mut pak, &mut at, path, vec![item], stats)?);
    }
    // Nebula's placeholder model, the object archive a mod's missing class draws (contract references 4).
    let item = StoreItem { path: PLACEHOLDER_MODEL.to_string(), order: 0, bytes: Some(placeholder_model()?) };
    placed.extend(place(&mut pak, &mut at, PLACEHOLDER_ARCHIVE, vec![item], stats)?);
    // And its chime, which a mod's sound name no file has plays (contract references 4 and 5.1).
    let item = StoreItem { path: String::new(), order: 0, bytes: Some(placeholder_sound()?) };
    placed.extend(place(&mut pak, &mut at, PLACEHOLDER_SOUND, vec![item], stats)?);
    let at = store::write_index(&mut pak, at, &placed, system.bytes[..8].try_into().unwrap())?;
    pak.set_len(at)?;
    pak.sync_all().context("game.pak cannot be flushed to storage")?;
    Ok(at)
}

/// Ray tracing's tables, as the renderer finds them in the pak (runtime/include/galaxy/render/rt_materials.h
/// kRtStagesPath, kRtEffectsPath).
const RT_STAGES_PATH: &str = "NebulaData/RtStages.tsv";
const RT_EFFECTS_PATH: &str = "NebulaData/RtEffects.tsv";
const RT_STAGES: &str = include_str!("../../data/rt_stages.tsv");
const RT_EFFECTS: &str = include_str!("../../data/rt_effects.tsv");

/// Nebula's placeholder archive in the store (contract references 4): the game draws its model for a mod's missing class
/// (LumaRefs.cpp), and the start check (3.13, mod_start_check.cpp) turns mods off when a store lacks it.
pub const PLACEHOLDER_ARCHIVE: &str = "ObjectData/NebulaPlaceholder.arc";
/// The model in it, as the game's model manager finds an archive's model by the archive's name.
pub const PLACEHOLDER_MODEL: &str = "NebulaPlaceholder.bmd";

/// Nebula's placeholder sound in the store (contract references 4, `nebula:placeholder/sound`): the audio service plays it
/// for a mod's sound name no file has (runtime/src/luma_audio.cpp kPlaceholderSound), and the start check (3.13) turns
/// mods off when a store lacks it.
pub const PLACEHOLDER_SOUND: &str = "NebulaData/PlaceholderSound.nsfx";

/// The placeholder's chime as the mixer plays a mod's effect (NSFX), from data/placeholder/sound.wav (make_placeholder.py
/// writes it: 120 ms, two soft notes), cooked as a mod's `.sound.json` naming it is, carried in the importer so every
/// import cooks the same bytes.
pub fn placeholder_sound() -> Result<Vec<u8>> {
    let wav = include_bytes!("../../../../data/placeholder/sound.wav");
    let file = |name: &str| (name == "sound.wav").then(|| wav.to_vec());
    let none = |_: &str| None;
    let around = sound_effect::Around { file: &file, game_sound: &none, game_stream: &none, game_sequence: &none };
    let (nsfx, _) = sound_effect::cook(br#"{"kind": "effect", "file": "sound.wav"}"#, &around).context("the placeholder's sound")?;
    Ok(nsfx)
}

/// The placeholder's cooked model, from data/placeholder/model.gltf and its face.png (make_placeholder.py writes both),
/// carried in the importer so every import cooks the same bytes.
pub fn placeholder_model() -> Result<Vec<u8>> {
    let mut source: serde_json::Value =
        serde_json::from_slice(include_bytes!("../../../../data/placeholder/model.gltf")).context("the placeholder's model.gltf")?;
    let face = include_bytes!("../../../../data/placeholder/face.png");
    for image in source["images"].as_array_mut().context("the placeholder's model.gltf has no images")? {
        if image["uri"] != "face.png" {
            bail!("the placeholder's model.gltf names an image other than face.png");
        }
        image["uri"] = serde_json::Value::String(format!("data:image/png;base64,{}", gltf::base64_text(face)));
    }
    let (model, _) = gltf::to_files(&serde_json::to_vec(&source)?, None, "NebulaPlaceholder").context("the placeholder's model")?;
    Ok(model)
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

#[cfg(test)]
mod disc_record_tests {
    #[test]
    fn the_placeholder_sound_cooks_from_nebulas_data() {
        let nsfx = super::placeholder_sound().unwrap();
        assert_eq!(&nsfx[..4], super::sound_effect::MAGIC);
        let word = |at: usize| u32::from_le_bytes(nsfx[at..at + 4].try_into().unwrap());
        // 48 kHz mono, 120 ms (5760 frames), no loop: one chime, then it ends.
        assert_eq!((word(8), word(12), word(16), word(20), word(24)), (48_000, 1, 5_760, u32::MAX, 0));
        // The same bytes each time: every import cooks the same placeholder.
        assert_eq!(nsfx, super::placeholder_sound().unwrap());
    }

    #[test]
    fn the_placeholder_model_cooks_from_nebulas_data() {
        assert_eq!([&b"Man"[..], b"Ma", b"M", b""].map(super::gltf::base64_text), ["TWFu", "TWE=", "TQ==", ""]);
        let model = super::placeholder_model().unwrap();
        assert!(model.len() > 1024, "{} bytes", model.len());
        // The same bytes each time: every import cooks the same placeholder.
        assert_eq!(model, super::placeholder_model().unwrap());
    }

    #[test]
    fn the_disc_record_reads_back_by_path() {
        let rows = vec![("StageData/A.arc".to_string(), 12u64, 0x0123_4567_89ab_cdefu64), ("sys/main.dol".to_string(), 4, 7)];
        let text = String::from_utf8(super::disc_files_table(&rows)).unwrap();
        let read = super::read_disc_files(&text);
        assert_eq!(read.len(), 2);
        assert_eq!(read["stagedata/a.arc"], (12, 0x0123_4567_89ab_cdef));
        assert_eq!(read["sys/main.dol"], (4, 7));
    }
}
