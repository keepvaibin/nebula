//! The game partition of a Super Mario Galaxy disc (RMGE01, RMGP01 or RMGJ01) as the native game reads it: `game.pak`,
//! the partition's decrypted data at its own offsets, with the gaps left sparse. Android's setup calls it through JNI
//! (`jni.rs`); Windows Setup runs the `nebula-import` program (`main.rs`).

use anyhow::{anyhow, bail, Context, Result};
use nod::{Disc, OpenOptions, PartitionBase, PartitionKind};
use sha1::{Digest, Sha1};
use std::borrow::Cow;
use std::collections::HashSet;
use std::fs::{self, File};
use std::io::{self, Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::sync::Arc;

mod jni;

/// Super Mario Galaxy, North America.
pub const RMGE01_GAME_ID: &str = "RMGE01";
/// SHA-1 of RMGE01's main.dol.
pub const RMGE01_DOL_SHA1: &str = "9a71008ae1ee9010e267fa67d1f0b0d4f0e895dd";

/// A release the native game plays: its game id, its main.dol's SHA-1 (the one Petari's build of it matches,
/// config/<id>/build.sha1) and its region's name. Each has one disc revision, 0.
pub struct Release {
    pub game_id: &'static str,
    pub dol_sha1: &'static str,
    pub region: &'static str,
}

pub const RELEASES: [Release; 3] = [
    Release { game_id: RMGE01_GAME_ID, dol_sha1: RMGE01_DOL_SHA1, region: "USA" },
    Release { game_id: "RMGP01", dol_sha1: "9bd1daa520bd1f573b6a4b82e03336e9c51b511f", region: "Europe" },
    Release { game_id: "RMGJ01", dol_sha1: "604d0b47d446caec0aab9ac370014f132e1b4016", region: "Japan" },
];
const RELEASE_REVISION: u8 = 0;

/// The release a game id names, if the native game plays it.
pub fn release(game_id: &str) -> Option<&'static Release> {
    RELEASES.iter().find(|release| release.game_id == game_id)
}

const BOOT_BIN_SIZE: usize = 0x440;
const BI2_BIN_SIZE: usize = 0x2000;
const APPLOADER_HEADER_SIZE: usize = 0x20;
const BI2_BIN_OFFSET: u64 = 0x440;
const APPLOADER_OFFSET: u64 = 0x2440;
const TMD_SIZE: usize = 0x208;
const TMD_SIGNATURE_TYPE: u32 = 0x0001_0001;
const TMD_CONTENT_COUNT_OFFSET: usize = 0x1de;
const TMD_CONTENT_RECORD_OFFSET: usize = 0x1e4;
const TMD_CONTENT_RECORD_SIZE: usize = 36;
/// The game partition's size in RMGE01's title metadata (the tests' disc); every release's is whole clusters.
#[cfg(test)]
const RMGE01_PARTITION_DATA_BYTES: u64 = 0xff7c_0000;
const PARTITION_CLUSTER: u64 = 0x8000;
const COPY_CHUNK: usize = 1 << 20;

/// The content folder's one file besides game.pak: the settings overlay draws its window frame from this archive.
const FILE_SELECT_ARCHIVE: &str = "LayoutData/FileSelect.arc";

/// What a disc image is, as Android's setup shows it before importing.
#[derive(Debug, Clone)]
pub struct Identity {
    pub format: String,
    pub game_id: String,
    pub title: String,
    pub supported: bool,
    pub message: String,
}

/// What a disc image or extracted folder is, as Windows Setup shows it before importing.
#[derive(Debug, Clone)]
pub struct InputIdentity {
    /// The disc image format, or `folder`.
    pub format: String,
    pub game_id: String,
    pub title: String,
    pub revision: u8,
    /// Read for the releases the native game plays.
    pub main_dol_sha1: Option<String>,
    /// The game partition's language folders (`UsEnglish`, `EuGerman`...); empty when its file table was not read.
    pub languages: Vec<String>,
    pub supported: bool,
    pub message: String,
}

impl InputIdentity {
    fn new(
        format: String,
        game_id: String,
        title: String,
        revision: u8,
        main_dol_sha1: Option<String>,
        languages: Vec<String>,
    ) -> Self {
        let known = release(&game_id);
        let message = match known {
            None if game_id.starts_with("RMG") => format!(
                "This is Super Mario Galaxy for {} ({game_id}, revision {revision}){}. Nebula plays the releases for \
                 North America (RMGE01), Europe (RMGP01) and Japan (RMGJ01).",
                region_name(&game_id),
                in_languages(&languages)
            ),
            None => format!(
                "This is {title} ({game_id}), not Super Mario Galaxy. Nebula plays Super Mario Galaxy for North \
                 America (RMGE01), Europe (RMGP01) and Japan (RMGJ01)."
            ),
            Some(known) if revision != RELEASE_REVISION || main_dol_sha1.as_deref() != Some(known.dol_sha1) => format!(
                "This {game_id} copy is revision {revision} with main.dol SHA-1 {}. Nebula supports only revision \
                 {RELEASE_REVISION} (main.dol SHA-1 {}). The image may be modified, patched or a different revision.",
                main_dol_sha1.as_deref().unwrap_or("unknown"),
                known.dol_sha1
            ),
            Some(_) => String::new(),
        };
        InputIdentity {
            supported: message.is_empty(),
            message: match known {
                Some(known) if message.is_empty() => format!(
                    "Supported: Super Mario Galaxy ({}, {game_id}, revision {revision}){}.",
                    known.region,
                    in_languages(&languages)
                ),
                _ => message,
            },
            format,
            game_id,
            title,
            revision,
            main_dol_sha1,
            languages,
        }
    }

    /// One JSON object: format, gameId, title, revision, mainDolSha1 (null when not read), languages, supported,
    /// message.
    pub fn to_json(&self) -> String {
        let languages: Vec<String> = self.languages.iter().map(|language| json_string(language)).collect();
        format!(
            "{{\"format\":{},\"gameId\":{},\"title\":{},\"revision\":{},\"mainDolSha1\":{},\"languages\":[{}],\
             \"supported\":{},\"message\":{}}}",
            json_string(&self.format),
            json_string(&self.game_id),
            json_string(&self.title),
            self.revision,
            self.main_dol_sha1.as_deref().map_or_else(|| "null".to_owned(), json_string),
            languages.join(","),
            self.supported,
            json_string(&self.message)
        )
    }
}

/// The region of a Super Mario Galaxy game id (its fourth letter), as GameTDB lists the releases.
fn region_name(game_id: &str) -> String {
    match game_id.as_bytes().get(3) {
        Some(b'E') => "USA".to_owned(),
        Some(b'P') => "Europe".to_owned(),
        Some(b'J') => "Japan".to_owned(),
        Some(b'K') => "Korea".to_owned(),
        Some(letter) => format!("region {}", *letter as char),
        None => "an unknown region".to_owned(),
    }
}

/// ", in English, French and Spanish" for a disc's language folders ("" for none).
fn in_languages(folders: &[String]) -> String {
    let names: Vec<&str> = folders.iter().map(|folder| language_name(folder)).collect();
    match names.len() {
        0 => String::new(),
        1 => format!(", in {}", names[0]),
        n => format!(", in {} and {}", names[..n - 1].join(", "), names[n - 1]),
    }
}

/// A language folder's language ("EuGerman": German), or the folder's own name.
fn language_name(folder: &str) -> &str {
    match folder {
        "UsEnglish" | "EuEnglish" => "English",
        "UsFrench" | "EuFrench" => "French",
        "UsSpanish" | "EuSpanish" => "Spanish",
        "EuGerman" => "German",
        "EuItalian" => "Italian",
        "EuDutch" => "Dutch",
        "JpJapanese" => "Japanese",
        "KrKorean" => "Korean",
        "CnSimpChinese" => "Simplified Chinese",
        "TwTradChinese" => "Traditional Chinese",
        other => other,
    }
}

/// A folder the game reads one language from: a region the game knows (Language.cpp) and the language's name.
fn is_language_folder(name: &str) -> bool {
    let regions = ["Us", "Eu", "Jp", "Kr", "Cn", "Tw"];
    regions.iter().any(|region| {
        name.strip_prefix(region).is_some_and(|rest| {
            rest.chars().next().is_some_and(|first| first.is_ascii_uppercase())
                && rest.chars().all(|c| c.is_ascii_alphabetic())
        })
    })
}

/// The language folders at the root of a game partition's file table, in the table's order.
pub fn language_folders(fst: &[u8]) -> Vec<String> {
    let Ok(table) = nod::Fst::new(fst) else {
        return Vec::new();
    };
    let mut folders = Vec::new();
    // Each directory at the root is followed by its nodes, up to the index it gives: skipped.
    let mut next_root_node = 1usize;
    for (index, node, name) in table.iter() {
        if index == 0 || index < next_root_node {
            continue;
        }
        next_root_node = if node.is_dir() { node.length() as usize } else { index + 1 };
        if let (true, Ok(name)) = (node.is_dir(), name) {
            if is_language_folder(&name) {
                folders.push(name.into_owned());
            }
        }
    }
    folders
}

/// `text` as a JSON string literal.
fn json_string(text: &str) -> String {
    let mut out = String::with_capacity(text.len() + 2);
    out.push('"');
    for c in text.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            c if (c as u32) < 0x20 => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out.push('"');
    out
}

/// A disc image read with positional reads, so nod's clones of the stream never share a file position.
#[derive(Clone)]
pub struct DiscFile {
    file: Arc<File>,
    position: u64,
}

impl DiscFile {
    pub fn new(file: File) -> Self {
        DiscFile { file: Arc::new(file), position: 0 }
    }

    fn read_at(&self, buffer: &mut [u8], offset: u64) -> io::Result<usize> {
        #[cfg(unix)]
        {
            use std::os::unix::fs::FileExt;
            self.file.read_at(buffer, offset)
        }
        #[cfg(windows)]
        {
            use std::os::windows::fs::FileExt;
            self.file.seek_read(buffer, offset)
        }
    }
}

impl Read for DiscFile {
    fn read(&mut self, buffer: &mut [u8]) -> io::Result<usize> {
        let read = self.read_at(buffer, self.position)?;
        self.position += read as u64;
        Ok(read)
    }
}

impl Seek for DiscFile {
    fn seek(&mut self, from: SeekFrom) -> io::Result<u64> {
        let length = self.file.metadata()?.len();
        let target = match from {
            SeekFrom::Start(offset) => Some(offset),
            SeekFrom::End(delta) => length.checked_add_signed(delta),
            SeekFrom::Current(delta) => self.position.checked_add_signed(delta),
        };
        self.position = target.ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "seek before the start"))?;
        Ok(self.position)
    }
}

fn open_disc(disc: DiscFile) -> Result<Disc> {
    Disc::new_stream_with_options(Box::new(disc), &OpenOptions { rebuild_encryption: false, validate_hashes: true })
        .map_err(|error| anyhow!("this is not a readable GameCube or Wii disc image: {error}"))
}

/// The disc's game and whether Nebula can play it.
pub fn identify(disc: DiscFile) -> Result<Identity> {
    let disc = open_disc(disc)?;
    let header = disc.header();
    let format = disc.meta().format.to_string();
    let game_id = header.game_id_str().to_owned();
    let title = header.game_title_str().to_owned();
    let (supported, message) = if !header.is_wii() {
        (false, format!("{title} is a GameCube game, not Super Mario Galaxy."))
    } else if release(&game_id).is_none() {
        (
            false,
            format!(
                "{title} ({game_id}) is not Super Mario Galaxy for North America (RMGE01), Europe (RMGP01) or Japan \
                 (RMGJ01)."
            ),
        )
    } else {
        (true, format!("Super Mario Galaxy ({game_id}), {format}"))
    };
    Ok(Identity { format, game_id, title, supported, message })
}

/// The game of a disc image or extracted folder, with its disc revision and main.dol's SHA-1.
pub fn identify_input(input: &Path) -> Result<InputIdentity> {
    if input.is_dir() {
        return identify_folder(&data_root(input)?);
    }
    if !input.is_file() {
        bail!("{} does not exist", input.display());
    }
    let file = File::open(input).with_context(|| format!("{} cannot be opened", input.display()))?;
    let disc = Disc::new_stream(Box::new(DiscFile::new(file)))
        .map_err(|error| anyhow!("{} is not a readable GameCube/Wii disc image: {error}", input.display()))?;
    let header = disc.header();
    let game_id = header.game_id_str().to_owned();
    let mut main_dol_sha1 = None;
    let mut languages = Vec::new();
    if header.is_wii() && game_id.starts_with("RMG") {
        // Another release's languages are read for its message; a partition that cannot be read leaves them out.
        let opened = disc.open_partition_kind(PartitionKind::Data);
        if release(&game_id).is_some() {
            let meta = opened.context("the disc's game partition cannot be opened")?.meta().context("the game partition cannot be read")?;
            main_dol_sha1 = Some(sha1_hex(&meta.raw_dol));
            languages = language_folders(&meta.raw_fst);
        } else if let Ok(meta) = opened.and_then(|mut partition| partition.meta()) {
            languages = language_folders(&meta.raw_fst);
        }
    }
    Ok(InputIdentity::new(
        disc.meta().format.to_string(),
        game_id,
        header.game_title_str().to_owned(),
        header.disc_version,
        main_dol_sha1,
        languages,
    ))
}

fn identify_folder(data: &Path) -> Result<InputIdentity> {
    let boot = fs::read(data.join("sys").join("boot.bin"))
        .with_context(|| format!("{} has no readable sys/boot.bin", data.display()))?;
    if boot.len() < 0x60 {
        bail!("{} has a truncated sys/boot.bin", data.display());
    }
    let title_end = boot[0x20..0x60].iter().position(|byte| *byte == 0).unwrap_or(0x40);
    let dol = fs::read(data.join("sys").join("main.dol"))
        .with_context(|| format!("{} has no readable sys/main.dol", data.display()))?;
    let languages = fs::read(data.join("sys").join("fst.bin")).map(|fst| language_folders(&fst)).unwrap_or_default();
    Ok(InputIdentity::new(
        "folder".to_owned(),
        String::from_utf8_lossy(&boot[..6]).into_owned(),
        String::from_utf8_lossy(&boot[0x20..0x20 + title_end]).into_owned(),
        boot[7],
        Some(sha1_hex(&dol)),
        languages,
    ))
}

/// The partition folder of an extracted game (Dolphin's "Extract Entire Disc" layout): `input/DATA` or `input`.
fn data_root(input: &Path) -> Result<PathBuf> {
    let nested = input.join("DATA");
    if nested.join("sys").join("main.dol").is_file() {
        return Ok(nested);
    }
    if input.join("sys").join("main.dol").is_file() {
        return Ok(input.to_owned());
    }
    bail!("{} is not an extracted Wii game folder: expected DATA/sys/main.dol or sys/main.dol", input.display())
}

fn sha1_hex(bytes: &[u8]) -> String {
    format!("{:x}", Sha1::digest(bytes))
}

fn read_u32_be(data: &[u8], offset: usize) -> Result<u32> {
    let bytes = data.get(offset..offset + 4).context("header field outside its data")?;
    Ok(u32::from_be_bytes(bytes.try_into().unwrap()))
}

fn read_u16_be(data: &[u8], offset: usize) -> Result<u16> {
    let bytes = data.get(offset..offset + 2).context("header field outside its data")?;
    Ok(u16::from_be_bytes(bytes.try_into().unwrap()))
}

fn read_u64_be(data: &[u8], offset: usize) -> Result<u64> {
    let bytes = data.get(offset..offset + 8).context("header field outside its data")?;
    Ok(u64::from_be_bytes(bytes.try_into().unwrap()))
}

/// The partition's data size from its title metadata: one content record, of type 3, of whole clusters.
fn validate_tmd(tmd: &[u8]) -> Result<u64> {
    if tmd.len() != TMD_SIZE || read_u32_be(tmd, 0)? != TMD_SIGNATURE_TYPE {
        bail!("the game partition's title metadata is not a Wii game's");
    }
    if read_u16_be(tmd, TMD_CONTENT_COUNT_OFFSET)? != 1
        || TMD_CONTENT_RECORD_OFFSET + TMD_CONTENT_RECORD_SIZE != tmd.len()
    {
        bail!("the game partition's title metadata does not hold exactly one content");
    }
    let id = read_u32_be(tmd, TMD_CONTENT_RECORD_OFFSET)?;
    let index = read_u16_be(tmd, TMD_CONTENT_RECORD_OFFSET + 4)?;
    let kind = read_u16_be(tmd, TMD_CONTENT_RECORD_OFFSET + 6)?;
    let size = read_u64_be(tmd, TMD_CONTENT_RECORD_OFFSET + 8)?;
    if id != 0 || index != 0 || kind != 3 || size == 0 || size % PARTITION_CLUSTER != 0 {
        bail!("the game partition's content record is not a Wii game's");
    }
    Ok(size)
}

struct Extent {
    label: String,
    start: u64,
    end: u64,
}

/// The furthest end of non-overlapping extents inside the partition: game.pak's logical size.
fn validate_extents(mut extents: Vec<Extent>, partition_size: u64) -> Result<u64> {
    extents.retain(|extent| extent.start != extent.end);
    extents.sort_by(|a, b| a.start.cmp(&b.start).then(a.end.cmp(&b.end)));
    for extent in &extents {
        if extent.end < extent.start || extent.end > partition_size {
            bail!("{} lies outside the game partition", extent.label);
        }
    }
    for pair in extents.windows(2) {
        if pair[0].end > pair[1].start {
            bail!("{} and {} overlap on the disc", pair[0].label, pair[1].label);
        }
    }
    Ok(extents.last().map_or(0, |extent| extent.end))
}

/// A file of the game partition's file table.
struct GameFile {
    /// Its path in the file table, separated by `/`.
    path: String,
    offset: u64,
    length: u64,
}

/// The files of a file table, in partition order.
fn file_table(fst: &[u8]) -> Result<Vec<GameFile>> {
    let table = nod::Fst::new(fst).map_err(|error| anyhow!("the file table is invalid: {error}"))?;
    let mut files = Vec::new();
    // The directories holding the current node, each with the index after its last node.
    let mut parents: Vec<(String, usize)> = Vec::new();
    for (index, node, name) in table.iter() {
        while parents.last().is_some_and(|(_, end)| index >= *end) {
            parents.pop();
        }
        let name = name.map(Cow::into_owned).unwrap_or_else(|_| format!("file {index}"));
        if node.is_dir() {
            parents.push((name, node.length() as usize));
        } else if node.is_file() {
            let mut path = String::new();
            for (parent, _) in &parents {
                path.push_str(parent);
                path.push('/');
            }
            path.push_str(&name);
            files.push(GameFile { path, offset: node.offset(true), length: node.length() });
        }
    }
    files.sort_by_key(|file| file.offset);
    Ok(files)
}

/// Rejects a path component that is not one plain name on Windows: separators, `.` and `..`, characters Windows
/// refuses, a trailing dot or space (Windows drops it, so two names would open one file) and device names.
fn check_component(component: &str, path: &str) -> Result<()> {
    if component.is_empty()
        || component == "."
        || component == ".."
        || component.ends_with(' ')
        || component.ends_with('.')
        || component.chars().any(|c| c <= '\u{1f}' || matches!(c, '<' | '>' | ':' | '"' | '/' | '\\' | '|' | '?' | '*'))
    {
        bail!("the file table lists {path:?}, which is not a plain relative path");
    }
    let stem = component.split('.').next().unwrap_or(component).to_ascii_uppercase();
    let number = stem.strip_prefix("COM").or_else(|| stem.strip_prefix("LPT"));
    let device = matches!(stem.as_str(), "CON" | "PRN" | "AUX" | "NUL")
        || number.is_some_and(|n| matches!(n, "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" | "¹" | "²" | "³"));
    if device {
        bail!("the file table lists {path:?}, which names a Windows device");
    }
    Ok(())
}

/// `relative` (`/`-separated) under `root`.
fn join_relative(root: &Path, relative: &str) -> PathBuf {
    relative.split('/').fold(root.to_path_buf(), |path, component| path.join(component))
}

/// `relative` (`/`-separated) under `root`, reached through folders that are not links. `checked` holds the folders
/// already looked at.
fn folder_path(root: &Path, relative: &str, checked: &mut HashSet<PathBuf>) -> Result<PathBuf> {
    let mut path = root.to_path_buf();
    for component in relative.split('/') {
        check_component(component, relative)?;
        path.push(component);
        if checked.contains(&path) {
            continue;
        }
        let metadata = fs::symlink_metadata(&path).with_context(|| format!("{} cannot be read", path.display()))?;
        if metadata.file_type().is_symlink() {
            bail!("{} is a link", path.display());
        }
        if metadata.is_dir() {
            checked.insert(path.clone());
        }
    }
    Ok(path)
}

/// A regular file of an extracted partition, read whole.
fn read_folder_file(data: &Path, relative: &str, checked: &mut HashSet<PathBuf>) -> Result<Box<[u8]>> {
    let path = folder_path(data, relative, checked)?;
    if !path.is_file() {
        bail!("{} is not a file", path.display());
    }
    Ok(fs::read(&path).with_context(|| format!("{} cannot be read", path.display()))?.into_boxed_slice())
}

/// The system files of a game partition.
struct SystemFiles {
    boot: Box<[u8]>,
    bi2: Box<[u8]>,
    apploader: Box<[u8]>,
    dol: Box<[u8]>,
    fst: Box<[u8]>,
    tmd: Box<[u8]>,
}

/// Where the partition's files are read from.
enum Source {
    /// A disc image's game partition, decrypted.
    Disc(Box<dyn PartitionBase>),
    /// An extracted partition's folder; the files are in its `files` folder.
    Folder(PathBuf),
}

impl Source {
    /// A reader at the start of `file`.
    fn open(&mut self, file: &GameFile) -> Result<Box<dyn Read + '_>> {
        match self {
            Source::Disc(partition) => {
                partition
                    .seek(SeekFrom::Start(file.offset))
                    .with_context(|| format!("{} cannot be read from the disc", file.path))?;
                Ok(Box::new(partition))
            }
            Source::Folder(data) => {
                let path = join_relative(&data.join("files"), &file.path);
                Ok(Box::new(File::open(&path).with_context(|| format!("{} cannot be opened", path.display()))?))
            }
        }
    }

    fn origin(&self) -> &'static str {
        match self {
            Source::Disc(_) => "the disc",
            Source::Folder(_) => "the game folder",
        }
    }
}

/// A release's game partition (RELEASES), checked: its system files at their offsets, the files of its file table, and
/// where to read them.
pub struct Partition {
    system: Vec<(&'static str, u64, Box<[u8]>)>,
    files: Vec<GameFile>,
    source: Source,
    /// game.pak's logical size: the end of the last file.
    size: u64,
}

impl Partition {
    /// The game partition of a disc image, or of an extracted folder (Dolphin's "Extract Entire Disc" layout).
    pub fn open(input: &Path) -> Result<Partition> {
        if input.is_dir() {
            return Partition::from_folder(&data_root(input)?);
        }
        if !input.is_file() {
            bail!("{} does not exist", input.display());
        }
        let file = File::open(input).with_context(|| format!("{} cannot be opened", input.display()))?;
        Partition::from_disc(DiscFile::new(file))
    }

    /// The game partition of a disc image.
    pub fn from_disc(disc: DiscFile) -> Result<Partition> {
        let disc = open_disc(disc)?;
        let header = disc.header();
        if !header.is_wii() || release(header.game_id_str()).is_none() {
            bail!("this disc is not Super Mario Galaxy for North America (RMGE01), Europe (RMGP01) or Japan (RMGJ01)");
        }
        let mut partition =
            disc.open_partition_kind(PartitionKind::Data).context("the disc's game partition cannot be opened")?;
        let meta = *partition.meta().context("the game partition cannot be read")?;
        let tmd = meta.raw_tmd.context("the game partition has no title metadata")?;
        let system = SystemFiles {
            boot: meta.raw_boot,
            bi2: meta.raw_bi2,
            apploader: meta.raw_apploader,
            dol: meta.raw_dol,
            fst: meta.raw_fst,
            tmd,
        };
        Partition::new(system, Source::Disc(partition))
    }

    fn from_folder(data: &Path) -> Result<Partition> {
        let mut checked = HashSet::new();
        let mut read = |relative: &str| read_folder_file(data, relative, &mut checked);
        let system = SystemFiles {
            boot: read("sys/boot.bin")?,
            bi2: read("sys/bi2.bin")?,
            apploader: read("sys/apploader.img")?,
            dol: read("sys/main.dol")?,
            fst: read("sys/fst.bin")?,
            tmd: read("tmd.bin")?,
        };
        let partition = Partition::new(system, Source::Folder(data.to_path_buf()))?;
        // Each file once (Windows names compare without case), a regular file of the listed size.
        let mut names = HashSet::new();
        for file in &partition.files {
            if !names.insert(file.path.to_lowercase()) {
                bail!("the file table lists {} twice", file.path);
            }
            let path = folder_path(data, &format!("files/{}", file.path), &mut checked)?;
            let metadata = fs::metadata(&path).with_context(|| format!("{} cannot be read", path.display()))?;
            if !metadata.is_file() || metadata.len() != file.length {
                bail!("{} is not the file table's {}-byte file", path.display(), file.length);
            }
        }
        Ok(partition)
    }

    fn new(system: SystemFiles, source: Source) -> Result<Partition> {
        let SystemFiles { boot, bi2, apploader, dol, fst, tmd } = system;
        let known = (boot.len() == BOOT_BIN_SIZE)
            .then(|| std::str::from_utf8(&boot[..6]).ok().and_then(release))
            .flatten()
            .context("the game partition's boot header is not Super Mario Galaxy's (RMGE01, RMGP01 or RMGJ01)")?;
        if bi2.len() != BI2_BIN_SIZE || apploader.len() < APPLOADER_HEADER_SIZE {
            bail!("the game partition's system files have unexpected sizes");
        }
        let apploader_size =
            APPLOADER_HEADER_SIZE + read_u32_be(&apploader, 0x14)? as usize + read_u32_be(&apploader, 0x18)? as usize;
        if apploader.len() != apploader_size {
            bail!("the game partition's apploader has an unexpected size");
        }
        let dol_sha1 = sha1_hex(&dol);
        if dol_sha1 != known.dol_sha1 {
            bail!(
                "main.dol differs from Super Mario Galaxy's ({}, SHA-1 {dol_sha1}): the disc image may be damaged",
                known.game_id
            );
        }
        // Wii partition offsets and sizes are words: the boot header holds them divided by 4.
        let dol_offset = u64::from(read_u32_be(&boot, 0x420)?) << 2;
        let fst_offset = u64::from(read_u32_be(&boot, 0x424)?) << 2;
        let fst_size = u64::from(read_u32_be(&boot, 0x428)?) << 2;
        let fst_max = u64::from(read_u32_be(&boot, 0x42c)?) << 2;
        if dol_offset == 0 || fst_offset == 0 || fst_size != fst.len() as u64 || fst_max < fst_size {
            bail!("the game partition's boot header does not match its file table");
        }
        let partition_size = validate_tmd(&tmd)?;

        let files = file_table(&fst)?;
        let system = vec![
            ("boot.bin", 0, boot),
            ("bi2.bin", BI2_BIN_OFFSET, bi2),
            ("apploader.img", APPLOADER_OFFSET, apploader),
            ("main.dol", dol_offset, dol),
            ("fst.bin", fst_offset, fst),
        ];
        let mut extents: Vec<Extent> = system
            .iter()
            .map(|(label, start, bytes)| Extent {
                label: label.to_string(),
                start: *start,
                end: start + bytes.len() as u64,
            })
            .collect();
        // The boot header reserves room for a larger file table; no file may lie there.
        if fst_max > fst_size {
            extents.push(Extent {
                label: "the file table's reserve".into(),
                start: fst_offset + fst_size,
                end: fst_offset + fst_max,
            });
        }
        for file in &files {
            extents.push(Extent { label: file.path.clone(), start: file.offset, end: file.offset + file.length });
        }
        let size = validate_extents(extents, partition_size)?;
        Ok(Partition { system, files, source, size })
    }

    /// Writes game.pak at `output`. `progress(done, total)` reports bytes copied and returns false to stop. The
    /// destination is replaced only when the whole image was written and flushed.
    pub fn write_pak(&mut self, output: &Path, progress: &mut dyn FnMut(u64, u64) -> bool) -> Result<u64> {
        if let Some(parent) = output.parent() {
            fs::create_dir_all(parent).with_context(|| format!("{} cannot be created", parent.display()))?;
        }
        let name = output.file_name().context("the destination has no file name")?.to_string_lossy();
        let temporary = output.with_file_name(format!(".{name}.importing"));
        if let Err(error) = self.write_pak_to(&temporary, progress) {
            let _ = fs::remove_file(&temporary);
            return Err(error);
        }
        fs::rename(&temporary, output).with_context(|| format!("{} cannot be replaced", output.display()))?;
        Ok(self.size)
    }

    fn write_pak_to(&mut self, path: &Path, progress: &mut dyn FnMut(u64, u64) -> bool) -> Result<()> {
        let Partition { system, files, source, size } = self;
        let total = files.iter().map(|file| file.length).sum::<u64>()
            + system.iter().map(|(_, _, bytes)| bytes.len() as u64).sum::<u64>();
        let mut pak = File::create(path).with_context(|| format!("{} cannot be created", path.display()))?;
        #[cfg(windows)]
        set_sparse(&pak).context("game.pak cannot be made a sparse file (it needs an NTFS drive)")?;
        // Unwritten ranges stay holes: the file takes the game's 3.3 GB on the drive, not the partition's 4.3 GB.
        pak.set_len(*size).context("game.pak cannot be sized")?;
        let mut done = 0u64;
        if !progress(0, total) {
            bail!("the import was cancelled");
        }
        for (label, offset, bytes) in system.iter() {
            pak.seek(SeekFrom::Start(*offset))?;
            pak.write_all(bytes).with_context(|| format!("{label} cannot be written"))?;
            done += bytes.len() as u64;
        }
        let origin = source.origin();
        let mut buffer = vec![0u8; COPY_CHUNK];
        for file in files.iter() {
            let mut reader = source.open(file)?;
            pak.seek(SeekFrom::Start(file.offset))?;
            let mut left = file.length;
            while left > 0 {
                let chunk = left.min(COPY_CHUNK as u64) as usize;
                reader
                    .read_exact(&mut buffer[..chunk])
                    .with_context(|| format!("{} cannot be read from {origin}", file.path))?;
                pak.write_all(&buffer[..chunk]).with_context(|| format!("{} cannot be written", file.path))?;
                left -= chunk as u64;
                done += chunk as u64;
                if !progress(done, total) {
                    bail!("the import was cancelled");
                }
            }
        }
        pak.sync_all().context("game.pak cannot be flushed to storage")?;
        if pak.metadata()?.len() != *size {
            bail!("game.pak ended at an unexpected size");
        }
        Ok(())
    }

    /// Writes the content folder the native game runs from: game.pak and files/LayoutData/FileSelect.arc. `output`
    /// must not exist; it is removed again when the import fails.
    pub fn write_content(&mut self, output: &Path, progress: &mut dyn FnMut(u64, u64) -> bool) -> Result<u64> {
        if output.exists() {
            bail!("{} already exists", output.display());
        }
        fs::create_dir_all(output).with_context(|| format!("{} cannot be created", output.display()))?;
        let result = self.write_content_to(output, progress);
        if result.is_err() {
            let _ = fs::remove_dir_all(output);
        }
        result
    }

    fn write_content_to(&mut self, output: &Path, progress: &mut dyn FnMut(u64, u64) -> bool) -> Result<u64> {
        let (offset, length) = self
            .files
            .iter()
            .find(|file| file.path.eq_ignore_ascii_case(FILE_SELECT_ARCHIVE))
            .map(|file| (file.offset, file.length))
            .with_context(|| format!("the file table has no {FILE_SELECT_ARCHIVE}"))?;
        let pak = output.join("game.pak");
        let size = self.write_pak(&pak, progress)?;
        let mut archive = vec![0u8; length as usize];
        let mut reader = File::open(&pak).with_context(|| format!("{} cannot be opened", pak.display()))?;
        reader.seek(SeekFrom::Start(offset))?;
        reader
            .read_exact(&mut archive)
            .with_context(|| format!("{FILE_SELECT_ARCHIVE} cannot be read from game.pak"))?;
        let target = join_relative(&output.join("files"), FILE_SELECT_ARCHIVE);
        fs::create_dir_all(target.parent().unwrap())?;
        fs::write(&target, archive).with_context(|| format!("{} cannot be written", target.display()))?;
        Ok(size)
    }
}

/// Marks `file` sparse (FSCTL_SET_SPARSE), so ranges never written take no space on the drive.
#[cfg(windows)]
fn set_sparse(file: &File) -> io::Result<()> {
    use std::os::windows::io::AsRawHandle;
    use windows_sys::Win32::System::{Ioctl::FSCTL_SET_SPARSE, IO::DeviceIoControl};
    let mut returned = 0u32;
    // SAFETY: the handle stays open for the call, and the request takes no buffers.
    let succeeded = unsafe {
        DeviceIoControl(
            file.as_raw_handle() as _,
            FSCTL_SET_SPARSE,
            std::ptr::null(),
            0,
            std::ptr::null_mut(),
            0,
            &mut returned,
            std::ptr::null_mut(),
        )
    };
    if succeeded == 0 {
        return Err(io::Error::last_os_error());
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn valid_tmd() -> Vec<u8> {
        let mut tmd = vec![0u8; TMD_SIZE];
        tmd[0..4].copy_from_slice(&TMD_SIGNATURE_TYPE.to_be_bytes());
        tmd[TMD_CONTENT_COUNT_OFFSET..TMD_CONTENT_COUNT_OFFSET + 2].copy_from_slice(&1u16.to_be_bytes());
        tmd[TMD_CONTENT_RECORD_OFFSET + 6..TMD_CONTENT_RECORD_OFFSET + 8].copy_from_slice(&3u16.to_be_bytes());
        tmd[TMD_CONTENT_RECORD_OFFSET + 8..TMD_CONTENT_RECORD_OFFSET + 16]
            .copy_from_slice(&RMGE01_PARTITION_DATA_BYTES.to_be_bytes());
        tmd
    }

    #[test]
    fn tmd_must_hold_rmge01_content_record() {
        assert_eq!(validate_tmd(&valid_tmd()).unwrap(), RMGE01_PARTITION_DATA_BYTES);
        let mut wrong_size = valid_tmd();
        wrong_size[TMD_CONTENT_RECORD_OFFSET + 8..TMD_CONTENT_RECORD_OFFSET + 16]
            .copy_from_slice(&(RMGE01_PARTITION_DATA_BYTES + 0x8000).to_be_bytes());
        assert!(validate_tmd(&wrong_size).is_ok(), "another release's partition size, in whole clusters");
        let mut torn = valid_tmd();
        torn[TMD_CONTENT_RECORD_OFFSET + 8..TMD_CONTENT_RECORD_OFFSET + 16]
            .copy_from_slice(&(RMGE01_PARTITION_DATA_BYTES + 0x10).to_be_bytes());
        assert!(validate_tmd(&torn).is_err());
        let mut two_contents = valid_tmd();
        two_contents[TMD_CONTENT_COUNT_OFFSET..TMD_CONTENT_COUNT_OFFSET + 2].copy_from_slice(&2u16.to_be_bytes());
        assert!(validate_tmd(&two_contents).is_err());
        assert!(validate_tmd(&valid_tmd()[..TMD_SIZE - 4]).is_err());
    }

    fn extent(label: &str, start: u64, end: u64) -> Extent {
        Extent { label: label.into(), start, end }
    }

    #[test]
    fn extents_must_not_overlap_or_leave_the_partition() {
        let size = RMGE01_PARTITION_DATA_BYTES;
        assert_eq!(validate_extents(vec![extent("b", 0x500, 0x600), extent("a", 0, 0x440)], size).unwrap(), 0x600);
        assert!(validate_extents(vec![extent("boot.bin", 0, 0x440), extent("file", 0x400, 0x500)], size)
            .unwrap_err()
            .to_string()
            .contains("overlap"));
        assert!(validate_extents(vec![extent("file", size - 3, size + 1)], size).is_err());
        assert_eq!(validate_extents(vec![extent("empty", size + 8, size + 8)], size).unwrap(), 0);
    }

    enum Node<'a> {
        File(&'a str, u32, u32),
        /// A directory and the number of nodes it holds.
        Directory(&'a str, u32),
    }

    /// A Wii file table: file offsets are stored divided by 4.
    fn fst(nodes: &[Node]) -> Vec<u8> {
        let mut table = vec![1, 0, 0, 0];
        table.extend_from_slice(&0u32.to_be_bytes());
        table.extend_from_slice(&(nodes.len() as u32 + 1).to_be_bytes());
        let mut names = Vec::new();
        for (index, node) in nodes.iter().enumerate() {
            let (kind, name, offset, length) = match *node {
                Node::File(name, offset, length) => (0, name, offset / 4, length),
                Node::Directory(name, children) => (1, name, 0, index as u32 + 2 + children),
            };
            table.push(kind);
            table.extend_from_slice(&(names.len() as u32).to_be_bytes()[1..]);
            table.extend_from_slice(&offset.to_be_bytes());
            table.extend_from_slice(&length.to_be_bytes());
            names.extend_from_slice(name.as_bytes());
            names.push(0);
        }
        table.extend_from_slice(&names);
        table
    }

    #[test]
    fn file_table_gives_full_paths_in_partition_order() {
        let table = fst(&[
            Node::Directory("LayoutData", 2),
            Node::File("FileSelect.arc", 0x9000, 16),
            Node::File("Other.arc", 0x8000, 8),
            Node::File("top.bin", 0x7000, 4),
        ]);
        let files = file_table(&table).unwrap();
        let listed: Vec<(&str, u64, u64)> = files.iter().map(|f| (f.path.as_str(), f.offset, f.length)).collect();
        assert_eq!(
            listed,
            [("top.bin", 0x7000, 4), ("LayoutData/Other.arc", 0x8000, 8), ("LayoutData/FileSelect.arc", 0x9000, 16)]
        );
    }

    #[test]
    fn rejects_names_windows_reads_as_other_files() {
        for path in [
            "../escape.bin",
            "C:/absolute.bin",
            "directory/CON.txt",
            "directory/com1.arc",
            "LPT9/asset.bin",
            "directory/trailing. ",
            "a\\b",
            "",
        ] {
            assert!(path.split('/').try_for_each(|c| check_component(c, path)).is_err(), "accepted {path:?}");
        }
        for prefix in ["COM", "com", "LPT", "lPt"] {
            for digit in ["¹", "²", "³"] {
                for extension in ["", ".arc", ".tar.gz"] {
                    let name = format!("{prefix}{digit}{extension}");
                    assert!(check_component(&name, &name).is_err(), "accepted {name:?}");
                }
            }
        }
        for name in ["COM0.arc", "COM10", "COM¹0.arc", "LPT0", "LPT⁴.arc", "acOM¹.arc", "File.arc", "AudioRes"] {
            assert!(check_component(name, name).is_ok(), "rejected {name:?}");
        }
    }

    /// A partition read from `data/files`, without the checks `Partition::new` makes.
    fn folder_partition(
        data: &Path,
        files: &[(&str, u64, &[u8])],
        system: Vec<(&'static str, u64, Box<[u8]>)>,
    ) -> Partition {
        for (path, _, bytes) in files {
            let target = join_relative(&data.join("files"), path);
            fs::create_dir_all(target.parent().unwrap()).unwrap();
            fs::write(target, bytes).unwrap();
        }
        let files: Vec<GameFile> = files
            .iter()
            .map(|(path, offset, bytes)| GameFile {
                path: path.to_string(),
                offset: *offset,
                length: bytes.len() as u64,
            })
            .collect();
        let size = files.iter().map(|file| file.offset + file.length).max().unwrap_or(0);
        Partition { system, files, source: Source::Folder(data.to_path_buf()), size }
    }

    #[test]
    fn game_pak_holds_each_file_at_its_offset_and_zeros_between() {
        let directory = tempfile::tempdir().unwrap();
        let mut partition = folder_partition(
            &directory.path().join("data"),
            &[("a.bin", 0x10, b"first"), ("dir/b.bin", 0x30, b"second")],
            vec![("boot.bin", 0, Box::from(&b"BOOT"[..]))],
        );
        let output = directory.path().join("out").join("game.pak");
        let mut reports = Vec::new();
        let size = partition
            .write_pak(&output, &mut |done, total| {
                reports.push((done, total));
                true
            })
            .unwrap();
        assert_eq!(size, 0x36);
        let mut expected = vec![0u8; 0x36];
        expected[..4].copy_from_slice(b"BOOT");
        expected[0x10..0x15].copy_from_slice(b"first");
        expected[0x30..0x36].copy_from_slice(b"second");
        assert_eq!(fs::read(&output).unwrap(), expected);
        assert_eq!(reports.first(), Some(&(0, 15)));
        assert_eq!(reports.last(), Some(&(15, 15)));
        #[cfg(windows)]
        {
            use std::os::windows::fs::MetadataExt;
            const FILE_ATTRIBUTE_SPARSE_FILE: u32 = 0x200;
            assert_ne!(fs::metadata(&output).unwrap().file_attributes() & FILE_ATTRIBUTE_SPARSE_FILE, 0);
        }
    }

    #[test]
    fn failed_or_cancelled_write_keeps_the_existing_game_pak() {
        let directory = tempfile::tempdir().unwrap();
        let output = directory.path().join("game.pak");
        fs::write(&output, b"known good").unwrap();
        let data = directory.path().join("data");
        let mut partition = folder_partition(&data, &[("a.bin", 0, b"data")], Vec::new());
        assert!(partition.write_pak(&output, &mut |_, _| false).is_err());
        fs::remove_file(data.join("files").join("a.bin")).unwrap();
        assert!(partition.write_pak(&output, &mut |_, _| true).is_err());
        assert_eq!(fs::read(&output).unwrap(), b"known good");
        let left: Vec<_> = fs::read_dir(directory.path()).unwrap().map(|entry| entry.unwrap().file_name()).collect();
        assert_eq!(left.len(), 2, "a temporary file was left: {left:?}");
    }

    #[test]
    fn content_folder_holds_game_pak_and_file_select_archive() {
        let directory = tempfile::tempdir().unwrap();
        let mut partition = folder_partition(
            &directory.path().join("data"),
            &[("LayoutData/FileSelect.arc", 0x20, b"archive")],
            Vec::new(),
        );
        let output = directory.path().join("content");
        partition.write_content(&output, &mut |_, _| true).unwrap();
        assert_eq!(fs::read(output.join("files").join("LayoutData").join("FileSelect.arc")).unwrap(), b"archive");
        assert_eq!(fs::metadata(output.join("game.pak")).unwrap().len(), 0x27);
        assert!(partition.write_content(&output, &mut |_, _| true).unwrap_err().to_string().contains("already exists"));

        let mut without_archive = folder_partition(&directory.path().join("other"), &[("a.bin", 0, b"x")], Vec::new());
        let failed = directory.path().join("failed");
        assert!(without_archive.write_content(&failed, &mut |_, _| true).is_err());
        assert!(!failed.exists());
    }

    #[test]
    fn input_identity_messages_and_json() {
        let other = InputIdentity::new("ISO".into(), "RMCP01".into(), "Mario \"Kart\"\n".into(), 0, None, Vec::new());
        assert!(!other.supported);
        assert_eq!(
            other.to_json(),
            "{\"format\":\"ISO\",\"gameId\":\"RMCP01\",\"title\":\"Mario \\\"Kart\\\"\\u000a\",\"revision\":0,\
             \"mainDolSha1\":null,\"languages\":[],\"supported\":false,\"message\":\"This is Mario \\\"Kart\\\"\\u000a \
             (RMCP01), not Super Mario Galaxy. Nebula plays Super Mario Galaxy for North America (RMGE01), Europe (RMGP01) \
             and Japan (RMGJ01).\"}"
        );
        let us = vec!["UsEnglish".to_owned(), "UsFrench".to_owned(), "UsSpanish".to_owned()];
        let supported = InputIdentity::new(
            "folder".into(),
            RMGE01_GAME_ID.into(),
            "SUPER MARIO GALAXY".into(),
            0,
            Some(RMGE01_DOL_SHA1.into()),
            us,
        );
        assert!(supported.supported);
        assert_eq!(supported.message, "Supported: Super Mario Galaxy (USA, RMGE01, revision 0), in English, French and Spanish.");
        assert!(supported.to_json().contains("\"languages\":[\"UsEnglish\",\"UsFrench\",\"UsSpanish\"]"));
        let europe: Vec<String> = ["EuEnglish", "EuGerman", "EuFrench"].iter().map(|s| s.to_string()).collect();
        let pal = InputIdentity::new(
            "ISO".into(),
            "RMGP01".into(),
            "x".into(),
            0,
            Some("9bd1daa520bd1f573b6a4b82e03336e9c51b511f".into()),
            europe.clone(),
        );
        assert!(pal.supported);
        assert_eq!(pal.message, "Supported: Super Mario Galaxy (Europe, RMGP01, revision 0), in English, German and French.");
        let japan = InputIdentity::new(
            "ISO".into(),
            "RMGJ01".into(),
            "x".into(),
            0,
            Some("604d0b47d446caec0aab9ac370014f132e1b4016".into()),
            vec!["JpJapanese".to_owned()],
        );
        assert!(japan.supported);
        assert_eq!(japan.message, "Supported: Super Mario Galaxy (Japan, RMGJ01, revision 0), in Japanese.");
        let other_dol =
            InputIdentity::new("ISO".into(), "RMGP01".into(), "x".into(), 0, Some(RMGE01_DOL_SHA1.into()), europe);
        assert!(!other_dol.supported && other_dol.message.starts_with("This RMGP01 copy is revision 0 with main.dol"));
        assert_eq!(
            InputIdentity::new("ISO".into(), "RMGK01".into(), "x".into(), 0, None, Vec::new()).message,
            "This is Super Mario Galaxy for Korea (RMGK01, revision 0). Nebula plays the releases for North America \
             (RMGE01), Europe (RMGP01) and Japan (RMGJ01)."
        );
        let revision = InputIdentity::new(
            "ISO".into(),
            RMGE01_GAME_ID.into(),
            "x".into(),
            1,
            Some(RMGE01_DOL_SHA1.into()),
            Vec::new(),
        );
        assert!(!revision.supported && revision.message.starts_with("This RMGE01 copy is revision 1"));
    }

    #[test]
    fn language_folders_are_the_roots_named_as_the_game_names_them() {
        let table = fst(&[
            Node::Directory("AudioRes", 1),
            Node::File("a.bin", 0, 4),
            Node::Directory("UsEnglish", 2),
            Node::Directory("EuGerman", 0),
            Node::File("m.arc", 4, 4),
            Node::Directory("UsFrench", 0),
            Node::File("UsSpanish", 8, 4),
            Node::Directory("Usb", 0),
            Node::Directory("CnSimpChinese", 0),
        ]);
        assert_eq!(language_folders(&table), ["UsEnglish", "UsFrench", "CnSimpChinese"]);
        assert!(language_folders(&[1, 2, 3]).is_empty());
        assert!(is_language_folder("TwTradChinese") && !is_language_folder("Us") && !is_language_folder("UsEnglish2"));
    }
}
