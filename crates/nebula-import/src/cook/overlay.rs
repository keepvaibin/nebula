//! Mods' files as the game reads them: each file a mod changes or adds is cooked as the disc's were at import and
//! written to a store of its own, which the game reads over game.pak (runtime/src/disc_files.cpp). The plan names them,
//! one a line: the game id, then `path<TAB>source file`.

use super::{archive, audio_data, cook_entry, cooks, place, store, Stats, StoreItem, ALIGN};
use anyhow::{bail, Context, Result};
use std::collections::BTreeMap;
use std::fs::{self, File};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};

/// Where the store keeps the disc's wave layout.
pub const WAVE_LAYOUT: &str = "NebulaData/WaveLayout.txt";

pub struct Change {
    pub path: String,
    pub source: PathBuf,
    /// Added to the file at `path`, not in its place (cook::merge).
    pub merge: bool,
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
    for line in lines.filter(|line| !line.is_empty() && !line.starts_with("base\t") && !line.starts_with("=material\t")) {
        let (path, source) = line.split_once('\t').with_context(|| format!("the plan's line {line:?}"))?;
        let (merge, path) = path.strip_prefix('+').map_or((false, path), |rest| (true, rest));
        changes.push(Change { path: path.trim_start_matches('/').to_string(), source: PathBuf::from(source), merge });
    }
    Ok((game_id, changes))
}

/// What a modern file of a mod becomes: each file's place in the game, with the game's ending, and bytes the cooker reads.
/// Empty: it isn't one. A model's animations go beside it.
fn modern_files(change: &Change, bytes: &[u8]) -> Result<Vec<(String, Vec<u8>)>> {
    let lower = change.path.to_ascii_lowercase();
    let renamed = |ending: &str| {
        let dot = change.path.rfind('.').unwrap_or(change.path.len());
        format!("{}{ending}", &change.path[..dot])
    };
    // Name.kcl.glb: solid ground, not something to see.
    if lower.ends_with(".kcl.glb") || lower.ends_with(".kcl.gltf") {
        let (collision, attributes) = super::gltf::to_collision(bytes, change.source.parent())?;
        let stem = &change.path[..lower.find(".kcl.").unwrap_or(change.path.len())];
        return Ok(vec![(format!("{stem}.kcl"), collision), (format!("{stem}.pa"), attributes)]);
    }
    if lower.ends_with(".gltf") || lower.ends_with(".glb") {
        let (model, animations) = super::gltf::to_files(bytes, change.source.parent())?;
        let folder = change.path.rfind('/').map_or("", |at| &change.path[..at + 1]);
        let mut out = vec![(renamed(".bmd"), model)];
        out.extend(animations.into_iter().map(|(name, bck)| (format!("{folder}{name}.bck"), bck)));
        return Ok(out);
    }
    if lower.ends_with(".bcsv.json") {
        return Ok(vec![(change.path[..change.path.len() - ".json".len()].to_string(), super::merge::table_from_json(bytes)?)]);
    }
    if lower.ends_with(".png") {
        // The game's interface keeps its pictures in another form, in a folder of that name.
        if lower.contains("/timg/") {
            return Ok(vec![(renamed(".tpl"), super::gltf::png_to_tpl(bytes)?)]);
        }
        return Ok(vec![(renamed(".bti"), super::gltf::png_to_bti(bytes)?)]);
    }
    if lower.ends_with(".wav") || lower.ends_with(".ogg") {
        return Ok(vec![(renamed(".ast"), super::modern_audio::to_stream(&change.path, bytes)?)]);
    }
    Ok(Vec::new())
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
    let progress = PROGRESS.lock().unwrap_or_else(|e| e.into_inner()).clone();
    for (done, change) in changes.iter().enumerate().filter(|(_, c)| !c.merge) {
        if let Some(file) = &progress {
            let _ = fs::write(file, format!("{done} {}\n", changes.len()));
        }
        let lower = change.path.to_ascii_lowercase();
        super::material::MOD_FILE_NEEDS_PASSES.set(lower.contains("mario") || lower.contains("luigi") || lower.contains("flexible"));
        let result = fs::read(&change.source)
            .with_context(|| format!("{} cannot be read", change.source.display()))
            .and_then(|bytes| {
                stats.bytes_in += bytes.len() as u64;
                let modern = modern_files(change, &bytes)?;
                if modern.is_empty() {
                    return Ok(vec![(change.path.clone(), cook_change(change, bytes, &waves, stats)?)]);
                }
                let mut cooked = Vec::with_capacity(modern.len());
                for (path, converted) in modern {
                    // A stream is in the game's form already.
                    let items = if path.ends_with(".ast") {
                        vec![StoreItem { path: String::new(), order: 0, bytes: Some(converted) }]
                    } else {
                        cook_entry(&path, converted, stats)?
                    };
                    cooked.push((path, items));
                }
                Ok(cooked)
            });
        match result {
            Ok(files) => {
                for (path, items) in files {
                    placed.extend(place(&mut pak, &mut at, &path, items, stats)?);
                }
            }
            Err(error) => failed.push((change.path.clone(), format!("{error:#}"))),
        }
    }
    apply_merges(changes, base, &mut pak, &temporary, &mut at, &mut placed, &mut failed, stats)?;
    complete_archives(changes, base, &mut pak, &mut at, &mut placed, &mut failed, stats)?;
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

/// What a merged file holds so far: a mod's file of that name in this store, else the game's.
fn merge_start(path: &str, base: Option<&Path>, temporary: &Path, pak: &mut File, placed: &[store::Placed]) -> Result<Vec<u8>> {
    if let Some(mine) = placed.iter().find(|p| !p.folder && p.path.eq_ignore_ascii_case(path)) {
        pak.flush()?;
        let mut reader = File::open(temporary)?;
        reader.seek(SeekFrom::Start(mine.position))?;
        let mut bytes = vec![0u8; mine.size as usize];
        reader.read_exact(&mut bytes)?;
        return Ok(bytes);
    }
    store::read_file(base.context("the mod adds to a file of the game, and the game isn't known")?, path)
}

/// Each merge in its mod's order: a mod's names before its particles, which are numbered from them. A merge that can't
/// apply is left out and named.
#[allow(clippy::too_many_arguments)]
fn apply_merges(changes: &[Change], base: Option<&Path>, pak: &mut File, temporary: &Path, at: &mut u64, placed: &mut Vec<store::Placed>,
                failed: &mut Vec<(String, String)>, stats: &mut Stats) -> Result<()> {
    let mut merges: Vec<&Change> = changes.iter().filter(|c| c.merge).collect();
    merges.sort_by_key(|c| c.path.to_ascii_lowercase().ends_with(".jpc"));
    let mut merged: BTreeMap<String, Vec<u8>> = BTreeMap::new();
    let mut first_particle: BTreeMap<PathBuf, u16> = BTreeMap::new();
    let lower = |text: &str| text.to_ascii_lowercase();
    for change in merges {
        let result = (|| -> Result<()> {
            let bytes = fs::read(&change.source).with_context(|| format!("{} cannot be read", change.source.display()))?;
            if let Some(table) = lower(&change.path).strip_suffix(".json") {
                let table_key = table.to_string();
                let path = change.path[..table.len()].to_string();
                let current = match merged.remove(&table_key) {
                    Some(current) => current,
                    None => merge_start(&path, base, temporary, pak, placed)?,
                };
                let mut rows = super::merge::read_rows(&bytes)?;
                if table_key.ends_with("particlenames.bcsv") {
                    let folder = change.source.parent().map(Path::to_path_buf).unwrap_or_default();
                    let first = super::merge::row_count(&current)?;
                    first_particle.insert(folder, u16::try_from(first).context("too many particles")?);
                    for (i, row) in rows.iter_mut().enumerate() {
                        if let Some(fields) = row.as_object_mut() {
                            fields.insert("id".to_string(), serde_json::Value::from(first + i));
                        }
                    }
                }
                let current = super::merge::edit_rows(&current, &bytes)?;
                merged.insert(table_key, super::merge::add_rows(&current, &rows)?);
            } else if lower(&change.path).ends_with(".jpc") {
                let folder = change.source.parent().map(Path::to_path_buf).unwrap_or_default();
                let first = *first_particle.get(&folder).context("new particles need their names: ParticleNames.bcsv.json beside them")?;
                let key = lower(&change.path);
                let current = match merged.remove(&key) {
                    Some(current) => current,
                    None => merge_start(&change.path, base, temporary, pak, placed)?,
                };
                let cooked = super::cook_file(&change.path, bytes, stats)?.context("not a particle file")?;
                merged.insert(key, super::merge::add_particles(&current, &cooked, first)?);
            } else {
                bail!("only tables (.json) and particles (.jpc) can be added to");
            }
            Ok(())
        })();
        if let Err(error) = result {
            failed.push((format!("merge/{}", change.path), format!("{error:#}")));
        }
    }
    // The cased path a mod's file or the first merge used.
    for (key, bytes) in merged {
        let path = placed
            .iter()
            .find(|p| p.path.eq_ignore_ascii_case(&key))
            .map(|p| p.path.clone())
            .or_else(|| changes.iter().filter(|c| c.merge).map(|c| c.path.trim_end_matches(".json").to_string()).find(|p| p.eq_ignore_ascii_case(&key)))
            .unwrap_or(key);
        placed.retain(|p| !p.path.eq_ignore_ascii_case(&path));
        placed.extend(place(pak, at, &path, vec![StoreItem { path: String::new(), order: 0, bytes: Some(bytes) }], stats)?);
    }
    Ok(())
}

/// The archive a path inside one is in: the part of the path up to the first `.arc/`. Archives are read as whole
/// folders, so one a mod changes part of needs the rest of it in this store too.
fn archive_of(path: &str) -> Option<&str> {
    let lower = path.to_ascii_lowercase();
    lower.find(".arc/").map(|at| &path[..at + 4])
}

/// A mod changing some files inside an archive: the rest come from the game's.
fn complete_archives(changes: &[Change], base: Option<&Path>, pak: &mut File, at: &mut u64, placed: &mut Vec<store::Placed>,
                     failed: &mut Vec<(String, String)>, stats: &mut Stats) -> Result<()> {
    // Checking a mod with no game at hand: the player's game completes it.
    if base.is_none() {
        return Ok(());
    }
    let whole: std::collections::BTreeSet<String> =
        changes.iter().filter(|c| !c.merge && c.path.to_ascii_lowercase().ends_with(".arc")).map(|c| c.path.to_ascii_lowercase()).collect();
    let mut partial = BTreeMap::new();
    for item in placed.iter() {
        if let Some(archive) = archive_of(&item.path) {
            if !whole.contains(&archive.to_ascii_lowercase()) {
                partial.entry(archive.to_ascii_lowercase()).or_insert_with(|| archive.to_string());
            }
        }
    }
    for archive in partial.into_values() {
        let Some(base) = base else {
            failed.push((archive, "the mod changes part of an archive, and the game isn't known".to_string()));
            continue;
        };
        // A new archive of the mod's own has nothing to come from.
        let Ok(members) = store::read_folder(base, &archive) else { continue };
        for (relative, bytes) in members {
            let path = format!("{archive}/{relative}");
            if !placed.iter().any(|p| p.path.eq_ignore_ascii_case(&path)) {
                placed.extend(place(pak, at, &path, vec![StoreItem { path: String::new(), order: 0, bytes: Some(bytes) }], stats)?);
            }
        }
    }
    Ok(())
}

/// While set, how far a cook has got goes in this file: "<done> <of>".
static PROGRESS: std::sync::Mutex<Option<std::path::PathBuf>> = std::sync::Mutex::new(None);

/// A plan file into a store: the C entry of Android and the command of the desktop program.
/// As `cook_plan_inner`, and what couldn't be cooked goes in a file beside the store: the game logs each.
pub fn cook_plan(plan: &Path, output: &Path) -> Result<(Stats, Vec<(String, String)>)> {
    let (stats, failed) = cook_plan_inner(plan, output)?;
    let mut words = String::new();
    for (path, reason) in &failed {
        words.push_str(&format!("{path}: {reason}\n"));
    }
    fs::write(output.with_extension("left_out"), words).ok();
    Ok((stats, failed))
}

fn cook_plan_inner(plan: &Path, output: &Path) -> Result<(Stats, Vec<(String, String)>)> {
    let text = fs::read_to_string(plan).with_context(|| format!("{} cannot be read", plan.display()))?;
    let (game_id, changes) = read_plan(&text)?;
    let mut stats = Stats::default();
    let mut written = BTreeMap::new();
    for line in text.lines().filter_map(|line| line.strip_prefix("=material\t")) {
        let (name, source) = line.split_once('\t').with_context(|| format!("the plan's line {line:?}"))?;
        let words = fs::read_to_string(source).with_context(|| format!("{source} cannot be read"))?;
        written.insert(name.to_string(), (super::material::written_shader_id(name, &words), words));
    }
    let shaders_written: Vec<(u32, String)> = written.values().cloned().collect();
    *super::material::WRITTEN_SHADERS.lock().unwrap_or_else(|e| e.into_inner()) = Some(written);
    *PROGRESS.lock().unwrap_or_else(|e| e.into_inner()) = Some(output.with_extension("progress"));
    // A material this build lacks takes its number from its key, as in a .luma's own shaders.
    *super::material::MOD_MATERIALS.lock().unwrap_or_else(|e| e.into_inner()) = Some(std::collections::BTreeMap::new());
    let result = write_overlay(&game_id, &changes, plan_base(&text).as_deref(), output, &mut stats);
    let materials = super::material::MOD_MATERIALS.lock().unwrap_or_else(|e| e.into_inner()).take().unwrap_or_default();
    let _ = fs::remove_file(output.with_extension("progress"));
    *super::material::WRITTEN_SHADERS.lock().unwrap_or_else(|e| e.into_inner()) = None;
    let failed = result?;
    // What those materials need, for shaders the mod didn't bring or brought for another build.
    let sources = output.with_extension("sources");
    let _ = fs::remove_dir_all(&sources);
    fs::create_dir_all(&sources).with_context(|| format!("{} cannot be created", sources.display()))?;
    for (name, text) in super::shader_source::HEADERS {
        fs::write(sources.join(name), text)?;
    }
    for (id, words) in &shaders_written {
        fs::write(sources.join(format!("m{id:08x}.hlsl")), words)?;
    }
    for (id, record) in &materials {
        if let Ok(source) = super::shader_source::material_source(record) {
            fs::write(sources.join(format!("m{id:08x}.hlsl")), source)?;
        }
    }
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
            Change { path: "StageData/note.txt".into(), source: good, merge: false },
            Change { path: "StageData/missing.txt".into(), source: dir.join("missing.txt"), merge: false },
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
