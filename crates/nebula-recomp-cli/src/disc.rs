//! Identification and extraction of user-supplied game input.
//!
//! Disc images (ISO, RVZ, WIA, WBFS, CISO, GCZ) are read with the `nod`
//! library; extraction writes to a new directory.

use crate::generate::resolve_game_data_root;
use anyhow::{bail, Context, Result};
use nebula_recomp_core::{RMGE01_DOL_SHA1, RMGE01_GAME_ID};
use nod::{Disc, Fst, OpenOptions, PartitionKind};
use serde::Serialize;
use sha1::{Digest, Sha1};
use std::{
    fs,
    io::{BufRead, Write},
    path::{Component, Path},
};

/// The only supported disc revision of RMGE01.
const RMGE01_REVISION: u8 = 0;

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Identity {
    /// `folder` for an extracted game, otherwise the disc image format.
    pub format: String,
    pub game_id: String,
    pub title: String,
    pub revision: u8,
    pub main_dol_sha1: Option<String>,
    pub supported: bool,
    pub message: String,
}

/// Identify an ISO/RVZ/other disc image or an extracted game folder.
pub fn identify(input: &Path) -> Result<Identity> {
    if input.is_dir() {
        return identify_folder(input);
    }
    if !input.is_file() {
        bail!("{} does not exist", input.display());
    }
    let disc = Disc::new(input).map_err(|error| {
        anyhow::anyhow!(
            "{} is not a readable GameCube/Wii disc image: {error}",
            input.display()
        )
    })?;
    let header = disc.header();
    let format = disc.meta().format.to_string();
    let game_id = header.game_id_str().to_owned();
    let title = header.game_title_str().to_owned();
    let revision = header.disc_version;
    if !header.is_wii() {
        return Ok(verdict(format, game_id, title, revision, None));
    }
    let main_dol_sha1 = if game_id == RMGE01_GAME_ID {
        let mut partition = disc
            .open_partition_kind(PartitionKind::Data)
            .context("failed to open the disc's game partition")?;
        let meta = partition
            .meta()
            .context("failed to read the game partition")?;
        Some(format!("{:x}", Sha1::digest(meta.raw_dol.as_ref())))
    } else {
        None
    };
    Ok(verdict(format, game_id, title, revision, main_dol_sha1))
}

fn identify_folder(input: &Path) -> Result<Identity> {
    let data = resolve_game_data_root(input)?;
    let boot = fs::read(data.join("sys").join("boot.bin"))
        .with_context(|| format!("{} has no readable sys/boot.bin", data.display()))?;
    if boot.len() < 0x60 {
        bail!("{} has a truncated sys/boot.bin", data.display());
    }
    let game_id = String::from_utf8_lossy(&boot[0..6]).into_owned();
    let title_end = boot[0x20..0x60]
        .iter()
        .position(|b| *b == 0)
        .unwrap_or(0x40);
    let title = String::from_utf8_lossy(&boot[0x20..0x20 + title_end]).into_owned();
    let dol = fs::read(data.join("sys").join("main.dol"))
        .with_context(|| format!("{} has no readable sys/main.dol", data.display()))?;
    let sha1 = format!("{:x}", Sha1::digest(&dol));
    Ok(verdict(
        "folder".to_owned(),
        game_id,
        title,
        boot[7],
        Some(sha1),
    ))
}

fn verdict(
    format: String,
    game_id: String,
    title: String,
    revision: u8,
    main_dol_sha1: Option<String>,
) -> Identity {
    let message = if game_id != RMGE01_GAME_ID {
        if game_id.starts_with("RMG") {
            format!(
                "This is {title} ({game_id}), a region Nebula does not support. Nebula \
                 supports only the North American release, RMGE01."
            )
        } else {
            format!(
                "This is {title} ({game_id}), not Super Mario Galaxy (RMGE01). Nebula \
                 supports only the North American release of Super Mario Galaxy."
            )
        }
    } else if revision != RMGE01_REVISION || main_dol_sha1.as_deref() != Some(RMGE01_DOL_SHA1) {
        format!(
            "This RMGE01 copy is revision {revision} with main.dol SHA-1 {}. Nebula \
             supports only revision {RMGE01_REVISION} (main.dol SHA-1 {RMGE01_DOL_SHA1}). \
             The image may be modified, patched or a different revision.",
            main_dol_sha1.as_deref().unwrap_or("unknown")
        )
    } else {
        String::new()
    };
    Identity {
        supported: message.is_empty(),
        message: if message.is_empty() {
            "Supported: Super Mario Galaxy (USA, RMGE01, revision 0).".to_owned()
        } else {
            message
        },
        format,
        game_id,
        title,
        revision,
        main_dol_sha1,
    }
}

/// Extract the game partition of a supported disc image into `output/DATA`
/// using the Dolphin "Extract Entire Disc" layout.
pub fn extract(input: &Path, output: &Path) -> Result<()> {
    let identity = identify(input)?;
    if !identity.supported {
        bail!("{}", identity.message);
    }
    if identity.format == "folder" {
        bail!("{} is already an extracted folder", input.display());
    }
    if output.exists() {
        bail!("extraction output {} already exists", output.display());
    }
    let disc = Disc::new_with_options(
        input,
        &OpenOptions {
            rebuild_encryption: false,
            validate_hashes: true,
        },
    )
    .with_context(|| format!("failed to open {}", input.display()))?;
    let mut partition = disc
        .open_partition_kind(PartitionKind::Data)
        .context("failed to open the disc's game partition")?;
    let meta = partition
        .meta()
        .context("failed to read the game partition")?;
    let data = output.join("DATA");
    let sys = data.join("sys");
    fs::create_dir_all(&sys).with_context(|| format!("failed to create {}", sys.display()))?;
    write_new(&sys.join("boot.bin"), meta.raw_boot.as_ref())?;
    write_new(&sys.join("bi2.bin"), meta.raw_bi2.as_ref())?;
    write_new(&sys.join("apploader.img"), meta.raw_apploader.as_ref())?;
    write_new(&sys.join("fst.bin"), meta.raw_fst.as_ref())?;
    write_new(&sys.join("main.dol"), meta.raw_dol.as_ref())?;
    let wii_files = [
        ("ticket.bin", meta.raw_ticket.as_deref()),
        ("tmd.bin", meta.raw_tmd.as_deref()),
        ("cert.bin", meta.raw_cert_chain.as_deref()),
        ("h3.bin", meta.raw_h3_table.as_deref()),
    ];
    for (name, bytes) in wii_files {
        let bytes = bytes.with_context(|| format!("the game partition has no {name}"))?;
        write_new(&data.join(name), bytes)?;
    }

    let fst = Fst::new(&meta.raw_fst)
        .map_err(|error| anyhow::anyhow!("the game partition file table is invalid: {error}"))?;
    let total: u64 = fst
        .iter()
        .filter(|(_, node, _)| node.is_file())
        .map(|(_, node, _)| node.length())
        .sum();
    let files = data.join("files");
    fs::create_dir_all(&files).with_context(|| format!("failed to create {}", files.display()))?;
    let mut done = 0u64;
    let mut reported = 0u64;
    println!("PROGRESS 0 {total}");
    let mut segments: Vec<(String, usize)> = Vec::new();
    for (index, node, name) in fst.iter() {
        let keep = segments.iter().take_while(|(_, end)| *end != index).count();
        segments.truncate(keep);
        let name = name.map_err(|error| {
            anyhow::anyhow!("the game partition has an unreadable file name: {error}")
        })?;
        if !is_plain_name(&name) {
            bail!("the game partition contains an unsafe file name {name:?}");
        }
        let end = if node.is_dir() {
            node.length() as usize
        } else {
            index + 1
        };
        segments.push((name.into_owned(), end));
        let relative: std::path::PathBuf = segments.iter().map(|(name, _)| name.as_str()).collect();
        let path = files.join(&relative);
        if node.is_dir() {
            fs::create_dir_all(&path)
                .with_context(|| format!("failed to create {}", path.display()))?;
            continue;
        }
        let mut reader = partition
            .open_file(node)
            .with_context(|| format!("failed to open {} on the disc", relative.display()))?;
        let mut file = fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&path)
            .with_context(|| format!("failed to create {}", path.display()))?;
        loop {
            let buffer = reader
                .fill_buf()
                .with_context(|| format!("failed to read {} from the disc", relative.display()))?;
            if buffer.is_empty() {
                break;
            }
            file.write_all(buffer)
                .with_context(|| format!("failed to write {}", path.display()))?;
            let length = buffer.len();
            reader.consume(length);
            done += length as u64;
            if done - reported >= 64 << 20 {
                println!("PROGRESS {done} {total}");
                reported = done;
            }
        }
        file.flush()
            .with_context(|| format!("failed to write {}", path.display()))?;
    }
    println!("PROGRESS {total} {total}");
    println!("Extracted {} to {}", identity.game_id, data.display());
    Ok(())
}

/// A single path component with no separators, drive or parent references.
fn is_plain_name(name: &str) -> bool {
    let mut components = Path::new(name).components();
    matches!(components.next(), Some(Component::Normal(_)))
        && components.next().is_none()
        && !name.contains(['/', '\\', ':'])
}

fn write_new(path: &Path, bytes: &[u8]) -> Result<()> {
    let mut file = fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(path)
        .with_context(|| format!("failed to create {}", path.display()))?;
    file.write_all(bytes)
        .with_context(|| format!("failed to write {}", path.display()))
}
