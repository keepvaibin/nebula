//! Mods' files as the game reads them: each file a mod changes or adds is cooked as the disc's were at import and
//! written to a store of its own, which the game reads over game.pak (runtime/src/disc_files.cpp). The plan names them,
//! one a line: the game id, then `path<TAB>source file`.

use super::{archive, audio_data, cook_entry, cooks, place, store, Stats, StoreItem, ALIGN};
use anyhow::{bail, Context, Result};
use std::collections::BTreeMap;
use std::fs::{self, File};
use std::io::Write;
use std::path::{Path, PathBuf};

/// Where the store keeps the disc's wave layout.
pub const WAVE_LAYOUT: &str = "NebulaData/WaveLayout.txt";

pub struct Change {
    pub path: String,
    pub source: PathBuf,
}

/// The game's store a plan names (`base<TAB>path`, after the game id), if it does.
fn plan_base(text: &str) -> Option<PathBuf> {
    text.lines().nth(1)?.strip_prefix("base\t").map(PathBuf::from)
}

/// The plan's game id and files.
pub fn read_plan(text: &str) -> Result<(String, Vec<Change>)> {
    let mut lines = text.lines();
    let game_id = lines.next().context("the plan is empty")?.trim().to_string();
    if game_id.len() != 6 {
        bail!("the plan's game id is {game_id:?}");
    }
    let mut changes = Vec::new();
    for line in lines.filter(|line| !line.is_empty() && !line.starts_with("base\t")) {
        let (path, source) = line.split_once('\t').with_context(|| format!("the plan's line {line:?}"))?;
        changes.push(Change { path: path.trim_start_matches('/').to_string(), source: PathBuf::from(source) });
    }
    Ok((game_id, changes))
}

/// One file of a mod as store items. A wave archive needs the sound archive that lays it out.
fn cook_change(change: &Change, bytes: Vec<u8>, waves: &BTreeMap<String, Vec<audio_data::WavePlace>>, stats: &mut Stats) -> Result<Vec<StoreItem>> {
    if cooks(&change.path) || archive::is_compressed(&bytes) {
        return cook_entry(&change.path, bytes, stats);
    }
    if bytes.is_empty() {
        bail!("the mod's file is empty");
    }
    if change.path.to_ascii_lowercase().ends_with(".aw") {
        let places = waves
            .get(&audio_data::wave_archive_key(&change.path))
            .context("its wave layout is not known: the mod brings no sound archive, and the game was imported before Nebula kept the layout (import it again)")?;
        let cooked = audio_data::wave_archive(&bytes, places).context("wave archive")?;
        return Ok(vec![StoreItem { path: String::new(), order: 0, bytes: Some(cooked) }]);
    }
    Ok(vec![StoreItem { path: String::new(), order: 0, bytes: Some(bytes) }])
}

/// Writes the store. A file that cannot be cooked is left out and named, so that the rest of a mod still loads; its
/// paths and reasons are returned.
pub fn write_overlay(game_id: &str, changes: &[Change], base: Option<&Path>, output: &Path, stats: &mut Stats) -> Result<Vec<(String, String)>> {
    if let Some(parent) = output.parent() {
        fs::create_dir_all(parent).with_context(|| format!("{} cannot be created", parent.display()))?;
    }
    let temporary = output.with_extension("importing");
    let mut pak = File::create(&temporary).with_context(|| format!("{} cannot be created", temporary.display()))?;
    pak.write_all(&[0u8; store::HEADER as usize])?;
    let mut at = store::HEADER.div_ceil(ALIGN) * ALIGN;
    // The game's layout (a store from before it kept one has none), unless a mod brings its own sound archive.
    let mut waves = BTreeMap::new();
    if let Some(bytes) = base.and_then(|base| store::read_file(base, WAVE_LAYOUT).ok()) {
        waves = audio_data::parse_layout_text(&String::from_utf8_lossy(&bytes)).context("the game's wave layout")?;
    }
    for change in changes.iter().filter(|c| c.path.to_ascii_lowercase().ends_with("audiores/smr.szs")) {
        let bytes = fs::read(&change.source).with_context(|| format!("{} cannot be read", change.source.display()))?;
        waves = audio_data::wave_layout(&bytes).context("the mod's sound archive")?;
    }
    let mut placed = Vec::new();
    let mut failed = Vec::new();
    for change in changes {
        let result = fs::read(&change.source)
            .with_context(|| format!("{} cannot be read", change.source.display()))
            .and_then(|bytes| {
                stats.bytes_in += bytes.len() as u64;
                cook_change(change, bytes, &waves, stats)
            });
        match result {
            Ok(items) => placed.extend(place(&mut pak, &mut at, &change.path, items, stats)?),
            Err(error) => failed.push((change.path.clone(), format!("{error:#}"))),
        }
    }
    if placed.is_empty() {
        let _ = fs::remove_file(&temporary);
        bail!("no file of the mods could be used");
    }
    let mut id = [0u8; 8];
    id[..6].copy_from_slice(game_id.as_bytes());
    let end = store::write_index(&mut pak, at, &placed, &id)?;
    pak.set_len(end)?;
    pak.sync_all().context("the mods' store cannot be flushed to storage")?;
    drop(pak);
    fs::rename(&temporary, output).with_context(|| format!("{} cannot be replaced", output.display()))?;
    Ok(failed)
}

/// A plan file into a store: the C entry of Android and the command of the desktop program.
pub fn cook_plan(plan: &Path, output: &Path) -> Result<(Stats, Vec<(String, String)>)> {
    let text = fs::read_to_string(plan).with_context(|| format!("{} cannot be read", plan.display()))?;
    let (game_id, changes) = read_plan(&text)?;
    let mut stats = Stats::default();
    let failed = write_overlay(&game_id, &changes, plan_base(&text).as_deref(), output, &mut stats)?;
    Ok((stats, failed))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_plan_names_its_game_and_files() {
        let (id, changes) = read_plan("RMGE01\n/StageData/A.arc\tC:\\m\\A.arc\nObjectData/B.arc\t/m/B.arc\n").unwrap();
        assert_eq!(id, "RMGE01");
        assert_eq!(changes.len(), 2);
        assert_eq!(changes[0].path, "StageData/A.arc");
        assert_eq!(changes[1].source, PathBuf::from("/m/B.arc"));
        assert!(read_plan("").is_err());
        assert!(read_plan("short\n").is_err());
        let with_base = "RMGE01\nbase\tD:/g/game.pak\nStageData/A.arc\t/m/A.arc\n";
        assert_eq!(plan_base(with_base), Some(PathBuf::from("D:/g/game.pak")));
        assert_eq!(read_plan(with_base).unwrap().1.len(), 1);
    }

    #[test]
    fn a_store_holds_a_plain_file_and_a_bad_one_is_named() {
        let dir = std::env::temp_dir().join(format!("nebula_overlay_{}", std::process::id()));
        fs::create_dir_all(&dir).unwrap();
        let good = dir.join("note.txt");
        fs::write(&good, b"hello").unwrap();
        let changes = vec![
            Change { path: "StageData/note.txt".into(), source: good },
            Change { path: "StageData/missing.txt".into(), source: dir.join("missing.txt") },
        ];
        let output = dir.join("mods.pak");
        let mut stats = Stats::default();
        let failed = write_overlay("RMGE01", &changes, None, &output, &mut stats).unwrap();
        assert_eq!(failed.len(), 1);
        assert_eq!(failed[0].0, "StageData/missing.txt");
        assert_eq!(store::read_file(&output, "StageData/note.txt").unwrap(), b"hello");
        let _ = fs::remove_dir_all(&dir);
    }
}
