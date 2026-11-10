//! Mods' files as the game reads them: each file a mod changes or adds is cooked as the disc's were at import and
//! written to a store of its own, which the game reads over game.pak (runtime/src/asset_store.cpp). The plan names them,
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

/// What a modern file of a mod becomes: each file's place in the game, with the game's ending, its bytes, and whether
/// they are the store's already (else the cooker reads them). Empty: it isn't one. A model's animations go beside it.
fn modern_files(change: &Change, bytes: &[u8]) -> Result<Vec<(String, Vec<u8>, bool)>> {
    let lower = change.path.to_ascii_lowercase();
    let renamed = |ending: &str| {
        let dot = change.path.rfind('.').unwrap_or(change.path.len());
        format!("{}{ending}", &change.path[..dot])
    };
    // Name.kcl.glb: solid ground, not something to see.
    if lower.ends_with(".kcl.glb") || lower.ends_with(".kcl.gltf") {
        let (collision, attributes) = super::gltf::to_collision(bytes, change.source.parent())?;
        let stem = &change.path[..lower.find(".kcl.").unwrap_or(change.path.len())];
        return Ok(vec![(format!("{stem}.kcl"), collision, false), (format!("{stem}.pa"), attributes, false)]);
    }
    // Name.canm.glb: a cutscene's camera, from a glTF camera's animation.
    if lower.ends_with(".canm.glb") || lower.ends_with(".canm.gltf") {
        let stem = &change.path[..lower.rfind(".canm.").unwrap_or(change.path.len())];
        return Ok(vec![(format!("{stem}.canm"), super::gltf::to_camera(bytes, change.source.parent())?, false)]);
    }
    if lower.ends_with(".gltf") || lower.ends_with(".glb") {
        let file = change.path.rsplit('/').next().unwrap_or(&change.path);
        let stem = file.split('.').next().unwrap_or(file);
        let (model, animations) = super::gltf::to_files(bytes, change.source.parent(), stem)?;
        let folder = change.path.rfind('/').map_or("", |at| &change.path[..at + 1]);
        let without_modern_extension = &change.path[..change.path.rfind('.').unwrap_or(change.path.len())];
        let native_path = if lower.ends_with(".bdl.gltf") || lower.ends_with(".bdl.glb") ||
            lower.ends_with(".bmd.gltf") || lower.ends_with(".bmd.glb") {
            without_modern_extension.to_string()
        } else {
            renamed(".bmd")
        };
        let mut out = vec![(native_path, model, true)];
        out.extend(animations.into_iter().map(|(file, clip)| (format!("{folder}{file}"), clip, true)));
        return Ok(out);
    }
    // A table as JSON: X.bcsv.json, a camera table X.bcam.json, or a stage's placement table (its name has no ending:
    // StageData/MyGalaxy.arc/jmp/placement/common/objinfo.json), as `lumalink extract` writes them.
    if lower.ends_with(".bcsv.json") || lower.ends_with(".bcam.json") || (lower.ends_with(".json") && lower.contains(".arc/jmp/")) {
        return Ok(vec![(change.path[..change.path.len() - ".json".len()].to_string(), super::table::TableIr::from_json(bytes)?.to_ntbl(), true)]);
    }
    if lower.ends_with(".png") {
        // The game's interface keeps its pictures in another form, in a folder of that name.
        if lower.contains("/timg/") {
            return Ok(vec![(renamed(".tpl"), super::gltf::png_to_tpl(bytes)?, false)]);
        }
        return Ok(vec![(renamed(".bti"), super::gltf::png_to_bti(bytes)?, false)]);
    }
    if super::modern_audio::is_stream_source(&lower) {
        return Ok(vec![(renamed(".ast"), super::modern_audio::to_stream(&change.path, bytes)?, true)]);
    }
    Ok(Vec::new())
}

/// One file of a mod as store items, cooked as the game's own are: by its name (`cooks`) or, whatever its name, by
/// what its bytes are (cook_file), as a member of an archive is at import. A mod's file can be one member
/// (`X.arc/jmp/placement/common/objinfo`, `X.arc/x.kcl`, `X.arc/blyt/x.brlyt`), so the name alone does not decide. A
/// file only a console reads is refused. A wave archive needs the sound archive that lays it out.
fn cook_change(change: &Change, bytes: Vec<u8>, waves: &BTreeMap<String, Vec<audio_data::WavePlace>>, stats: &mut Stats) -> Result<Vec<StoreItem>> {
    if super::console_only(change.path.trim_start_matches('/')) {
        bail!("only the console reads this file (its banner, HOME menu or module list); Nebula has none");
    }
    // A mod's file named as an archive that isn't one would fail only where the game opens it.
    if change.path.to_ascii_lowercase().ends_with(".arc") {
        let expanded = if archive::is_compressed(&bytes) { archive::expand(&bytes).context("its compression")? } else { bytes.clone() };
        if !archive::is_archive(&expanded) {
            bail!("it is named as an archive but isn't one");
        }
    }
    if cooks(&change.path) || archive::is_compressed(&bytes) || super::sound::is_sequence_path(&change.path) {
        return cook_entry(&change.path, bytes, stats);
    }
    if bytes.is_empty() {
        bail!("the mod's file is empty");
    }
    if !change.path.to_ascii_lowercase().ends_with(".aw") {
        return cook_entry(&change.path, bytes, stats);
    }
    let places = waves
        .get(&audio_data::wave_archive_key(&change.path))
        .context("its wave layout is not known: the mod brings no sound archive, and the game was imported before Nebula kept the layout (import it again)")?;
    let cooked = audio_data::wave_archive(&bytes, places).context("wave archive")?;
    Ok(vec![StoreItem { path: String::new(), order: 0, bytes: Some(cooked) }])
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
    // Two of the files may be one place in the game under two names (StageData/X.arc and StageData/S/X.arc, or two
    // mods' cases): the later one stands, the other is named, and the rest of the store is still written.
    let mut last_of: BTreeMap<String, usize> = BTreeMap::new();
    for (i, change) in changes.iter().enumerate().filter(|(_, c)| !c.merge) {
        last_of.insert(store::store_path(change.path.trim_start_matches('/')).to_ascii_lowercase(), i);
    }
    let progress = PROGRESS.lock().unwrap_or_else(|e| e.into_inner()).clone();
    for (done, change) in changes.iter().enumerate().filter(|(_, c)| !c.merge) {
        let key = store::store_path(change.path.trim_start_matches('/')).to_ascii_lowercase();
        if last_of.get(&key) != Some(&done) {
            let later = &changes[last_of[&key]].path;
            failed.push((change.path.clone(), format!("the same file of the game as {later}, which is used instead")));
            continue;
        }
        if let Some(file) = &progress {
            let _ = fs::write(file, format!("{done} {}\n", changes.len()));
        }
        let result = fs::read(&change.source)
            .with_context(|| format!("{} cannot be read", change.source.display()))
            .and_then(|bytes| {
                stats.bytes_in += bytes.len() as u64;
                let modern = modern_files(change, &bytes)?;
                if modern.is_empty() {
                    return Ok(vec![(change.path.clone(), cook_change(change, bytes, &waves, stats)?)]);
                }
                let mut cooked = Vec::with_capacity(modern.len());
                for (path, converted, done) in modern {
                    let items = if done {
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
        // Nothing is written beside the store now, so each file's reason goes with the error.
        let why: Vec<String> = failed.iter().map(|(path, reason)| format!("{path}: {reason}")).collect();
        if why.is_empty() {
            bail!("no file of the mods could be used");
        }
        bail!("no file of the mods could be used: {}", why.join("; "));
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
    if let Some(mine) = placed.iter().find(|p| !p.folder && store::same_path(&p.path, path)) {
        pak.flush()?;
        let mut reader = File::open(temporary)?;
        reader.seek(SeekFrom::Start(mine.position))?;
        let mut bytes = vec![0u8; mine.size as usize];
        reader.read_exact(&mut bytes)?;
        return Ok(bytes);
    }
    store::read_file(base.context("the mod adds to a file of the game, and the game isn't known")?, path)
}

/// Every language folder a game of the series has messages in.
const MESSAGE_LANGUAGES: [&str; 14] = [
    "UsEnglish", "UsFrench", "UsSpanish", "EuEnglish", "EuFrench", "EuGerman", "EuItalian", "EuSpanish", "EuDutch", "JpJapanese",
    "KrKorean", "CnSimpChinese", "TwTradChinese", "UsPortuguese",
];

/// The languages a messages merge is for: `<Language>/MessageData/Message.arc/message.bmg.json` one, and
/// `MessageData/messages.json` each the game has. None: the path is no messages merge.
fn message_languages(path: &str, base: Option<&Path>, placed: &[store::Placed]) -> Result<Option<Vec<String>>> {
    let lower = path.trim_start_matches('/').to_ascii_lowercase();
    if lower == "messagedata/messages.json" {
        let has = |language: &str| {
            let file = format!("{language}/MessageData/Message.arc/message.bmg");
            placed.iter().any(|p| store::same_path(&p.path, &file)) || base.is_some_and(|base| store::read_file(base, &file).is_ok())
        };
        let found: Vec<String> = MESSAGE_LANGUAGES.iter().filter(|l| has(l)).map(|l| l.to_string()).collect();
        if found.is_empty() {
            bail!("the game's languages aren't known: it has no <Language>/MessageData/Message.arc");
        }
        return Ok(Some(found));
    }
    let parts: Vec<&str> = path.trim_start_matches('/').split('/').collect();
    if parts.len() == 4 && lower.ends_with("/messagedata/message.arc/message.bmg.json") {
        return Ok(Some(vec![parts[0].to_string()]));
    }
    Ok(None)
}

/// Each merge in its mod's order: a mod's names before its particles, which are numbered from them. A merge that can't
/// apply is left out and named.
#[allow(clippy::too_many_arguments)]
fn apply_merges(changes: &[Change], base: Option<&Path>, pak: &mut File, temporary: &Path, at: &mut u64, placed: &mut Vec<store::Placed>,
                failed: &mut Vec<(String, String)>, stats: &mut Stats) -> Result<()> {
    let mut merges: Vec<&Change> = changes.iter().filter(|c| c.merge).collect();
    merges.sort_by_key(|c| {
        let path = c.path.to_ascii_lowercase();
        path.ends_with(".jpc") || path.ends_with(".jpc.json")
    });
    // Each file merged so far, by its place in the store (StageData/X.arc/y and StageData/X/y, in any case, are one):
    // a merge starts from it and replaces it only when the merge works, so one that fails loses no other's.
    let mut merged: BTreeMap<String, Vec<u8>> = BTreeMap::new();
    let mut first_particle: BTreeMap<PathBuf, u16> = BTreeMap::new();
    let mut cased: BTreeMap<String, String> = BTreeMap::new();
    let lower = |text: &str| text.to_ascii_lowercase();
    let place_of = |text: &str| store::store_path(text.trim_start_matches('/')).to_ascii_lowercase();
    for change in merges {
        let result = (|| -> Result<()> {
            // A picture beside particles written as JSON is one of their textures: their cook reads it.
            let path = lower(&change.path);
            if [".png", ".jpg", ".jpeg"].iter().any(|ending| path.ends_with(ending)) {
                return Ok(());
            }
            let bytes = fs::read(&change.source).with_context(|| format!("{} cannot be read", change.source.display()))?;
            if let Some(languages) = message_languages(&change.path, base, placed)? {
                // Every language or none: a merge that fails in one is left out of all.
                let mut done = Vec::new();
                for language in languages {
                    let folder = format!("{language}/MessageData/Message.arc");
                    let (file_path, ids_path) = (format!("{folder}/message.bmg"), format!("{folder}/messageid.tbl"));
                    let mut current = |path: &str| match merged.get(&place_of(path)) {
                        Some(bytes) => Ok(bytes.clone()),
                        None => merge_start(path, base, temporary, pak, placed),
                    };
                    let (file, ids) = (current(&file_path)?, current(&ids_path)?);
                    let (file, ids) = super::message_merge::merge(&file, &ids, &bytes, &language).with_context(|| language.clone())?;
                    done.push((file_path, file));
                    done.push((ids_path, ids));
                }
                for (path, bytes) in done {
                    merged.insert(place_of(&path), bytes);
                    cased.insert(place_of(&path), path);
                }
            } else if lower(&change.path).ends_with(".jpc.json") {
                // Particles written as JSON: cooked over the game's particles (`like`, its textures by name), then added as a
                // disc file's are.
                let folder = change.source.parent().map(Path::to_path_buf).unwrap_or_default();
                let first = *first_particle.get(&folder).context("new particles need their names: ParticleNames.bcsv.json beside them")?;
                let jpc_path = change.path[..change.path.len() - ".json".len()].to_string();
                let key = place_of(&jpc_path);
                let current = match merged.get(&key) {
                    Some(current) => current.clone(),
                    None => merge_start(&jpc_path, base, temporary, pak, placed)?,
                };
                let names_path = format!("{}/ParticleNames.bcsv", jpc_path.rsplit_once('/').map_or("", |(folder, _)| folder));
                let names = match merged.get(&place_of(&names_path)) {
                    Some(names) => names.clone(),
                    None => merge_start(&names_path, base, temporary, pak, placed)?,
                };
                let game = super::particles::read_nptc(&current)?;
                let mine = super::particle_json::read(&bytes, &folder, &game, &super::particle_json::names_index(&names)?)?;
                merged.insert(key, super::merge::add_particles(&current, &super::particles::write(&mine), first)?);
            } else if let Some(table) = lower(&change.path).strip_suffix(".json") {
                let path = change.path[..table.len()].to_string();
                let table_key = place_of(&path);
                let current = match merged.get(&table_key) {
                    Some(current) => current.clone(),
                    None => merge_start(&path, base, temporary, pak, placed)?,
                };
                let mut rows = super::table::read_rows(&bytes)?;
                // New particles' names: the particles beside them start after the game's (and other mods') last name.
                let mut names_start = None;
                if table_key.ends_with("particlenames.bcsv") {
                    let folder = change.source.parent().map(Path::to_path_buf).unwrap_or_default();
                    let first = super::table::row_count(&current)?;
                    names_start = Some((folder, u16::try_from(first).context("too many particles")?));
                    for (i, row) in rows.iter_mut().enumerate() {
                        if let Some(fields) = row.as_object_mut() {
                            fields.insert("id".to_string(), serde_json::Value::from(first + i));
                        }
                    }
                }
                let (table, unmatched) = super::table::merge(&current, &bytes, &rows)?;
                // Applied all the same: a row matching nothing is named in the log, not refused.
                for words in unmatched {
                    failed.push((format!("merge/{}", change.path), words));
                }
                if let Some((folder, first)) = names_start {
                    first_particle.insert(folder, first);
                }
                merged.insert(table_key, table);
            } else if lower(&change.path).ends_with(".jpc") {
                let folder = change.source.parent().map(Path::to_path_buf).unwrap_or_default();
                let first = *first_particle.get(&folder).context("new particles need their names: ParticleNames.bcsv.json beside them")?;
                let key = place_of(&change.path);
                let current = match merged.get(&key) {
                    Some(current) => current.clone(),
                    None => merge_start(&change.path, base, temporary, pak, placed)?,
                };
                let cooked = super::cook_file(&change.path, bytes, stats)?.context("not a particle file")?;
                merged.insert(key, super::merge::add_particles(&current, &cooked, first)?);
            } else {
                bail!("only tables (.json), messages (message.bmg.json, messages.json) and particles (.jpc, .jpc.json) can be added to");
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
            .find(|p| store::same_path(&p.path, &key))
            .map(|p| p.path.clone())
            .or_else(|| changes.iter().filter(|c| c.merge).map(|c| c.path.trim_end_matches(".json").to_string()).find(|p| store::same_path(p, &key)))
            .or_else(|| cased.get(&key).cloned())
            .unwrap_or(key);
        placed.retain(|p| !store::same_path(&p.path, &path));
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
            if !placed.iter().any(|p| store::same_path(&p.path, &path)) {
                placed.extend(place(pak, at, &path, vec![StoreItem { path: String::new(), order: 0, bytes: Some(bytes) }], stats)?);
            }
        }
    }
    Ok(())
}

/// While set, how far a cook has got goes in this file: "<done> <of>".
static PROGRESS: std::sync::Mutex<Option<std::path::PathBuf>> = std::sync::Mutex::new(None);

/// A plan file into a store: the C entry of Android and the command of the desktop program.
/// As `cook_plan_inner`, and what couldn't be cooked goes in a file beside the store: the game logs each. When nothing
/// could be, why goes in `<store>.why`: the desktop's cooker has no console and Android's stderr goes nowhere.
pub fn cook_plan(plan: &Path, output: &Path) -> Result<(Stats, Vec<(String, String)>)> {
    let why = output.with_extension("why");
    fs::remove_file(&why).ok();
    let cooked = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| cook_plan_inner(plan, output)))
        .unwrap_or_else(|panic| {
            let text = panic
                .downcast_ref::<&str>()
                .map(|t| t.to_string())
                .or_else(|| panic.downcast_ref::<String>().cloned())
                .unwrap_or_default();
            Err(anyhow::anyhow!("the converter stopped: {text}"))
        });
    let (stats, failed) = match cooked {
        Ok(done) => done,
        Err(error) => {
            fs::write(&why, format!("{error:#}")).ok();
            return Err(error);
        }
    };
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
        written.insert(name.to_string(), words);
    }
    *super::programs::MOD_WRITTEN.lock().unwrap_or_else(|e| e.into_inner()) = Some(written);
    *super::programs::MOD_TEXTS.lock().unwrap_or_else(|e| e.into_inner()) = Some(BTreeMap::new());
    *PROGRESS.lock().unwrap_or_else(|e| e.into_inner()) = Some(output.with_extension("progress"));
    let result = write_overlay(&game_id, &changes, plan_base(&text).as_deref(), output, &mut stats);
    let texts = super::programs::MOD_TEXTS.lock().unwrap_or_else(|e| e.into_inner()).take().unwrap_or_default();
    let _ = fs::remove_file(output.with_extension("progress"));
    *super::programs::MOD_WRITTEN.lock().unwrap_or_else(|e| e.into_inner()) = None;
    let failed = result?;
    // What those materials need, for shaders the mod didn't bring or brought for another build.
    let sources = output.with_extension("sources");
    let _ = fs::remove_dir_all(&sources);
    fs::create_dir_all(&sources).with_context(|| format!("{} cannot be created", sources.display()))?;
    for (name, text) in super::programs::HEADERS {
        fs::write(sources.join(name), text)?;
    }
    for (name, text) in &texts {
        fs::write(sources.join(name), text)?;
    }
    Ok((stats, failed))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn modern_models_can_replace_either_native_model_ending() {
        let bytes = br#"{"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],
            "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],
            "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],
            "bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],
            "buffers":[{"byteLength":42,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIA"}]}"#;
        for (path, expected) in [("ObjectData/Coin.arc/coin.bdl.gltf", "ObjectData/Coin.arc/coin.bdl"),
                                 ("ObjectData/Test.arc/test.bmd.gltf", "ObjectData/Test.arc/test.bmd"),
                                 ("ObjectData/Test.arc/test.gltf", "ObjectData/Test.arc/test.bmd")] {
            let files = modern_files(&Change { path: path.into(), source: "fixture.gltf".into(), merge: false }, bytes).unwrap();
            assert_eq!(files[0].0, expected);
            assert!(files[0].2);
            assert_eq!(&files[0].1[..4], b"NMDL");
        }
    }

    #[test]
    fn a_failed_cook_says_why_beside_the_store() {
        let dir = tempfile::tempdir().unwrap();
        let out = dir.path().join("mods.pak");
        fs::write(out.with_extension("why"), "an older failure").unwrap();
        assert!(cook_plan(&dir.path().join("missing.plan"), &out).is_err());
        let why = fs::read_to_string(out.with_extension("why")).unwrap();
        assert!(why.contains("missing.plan") && why.contains("cannot be read"), "{why}");
        // When every file fails, each one's reason is in it.
        let plan = dir.path().join("mods.plan");
        fs::write(&plan, format!("RMGE01\nStageData/A.arc\t{}\n", dir.path().join("gone.arc").display())).unwrap();
        assert!(cook_plan(&plan, &out).is_err());
        let why = fs::read_to_string(out.with_extension("why")).unwrap();
        assert!(why.contains("no file of the mods could be used: StageData/A.arc: "), "{why}");
    }

    #[test]
    fn one_game_file_under_two_names_keeps_the_later_and_names_the_other() {
        let dir = tempfile::tempdir().unwrap();
        let (a, b) = (dir.path().join("a.bin"), dir.path().join("b.bin"));
        fs::write(&a, b"first").unwrap();
        fs::write(&b, b"second").unwrap();
        let changes = vec![
            Change { path: "StageData/Galaxy/ZoneA.bin".into(), source: a, merge: false },
            Change { path: "StageData/galaxy/zonea.bin".into(), source: b, merge: false },
        ];
        let out = dir.path().join("mods.pak");
        let failed = write_overlay("RMGE01", &changes, None, &out, &mut Stats::default()).unwrap();
        assert_eq!(failed.len(), 1, "{failed:?}");
        assert!(failed[0].0 == "StageData/Galaxy/ZoneA.bin" && failed[0].1.contains("used instead"));
        assert_eq!(store::read_file(&out, "StageData/galaxy/zonea.bin").unwrap(), b"second");
    }

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

    #[test]
    fn messages_merge_into_every_language_then_one() {
        let dir = tempfile::tempdir().unwrap();
        let base = dir.path().join("game.pak");
        let (file, ids) = super::super::message_merge::tests::game();
        let mut pak = File::create(&base).unwrap();
        pak.write_all(&vec![0u8; 0x100]).unwrap();
        let mut placed = Vec::new();
        for language in ["UsEnglish", "UsFrench"] {
            for (name, bytes) in [("message.bmg", &file), ("messageid.tbl", &ids)] {
                let position = pak.stream_position().unwrap();
                pak.write_all(bytes).unwrap();
                placed.push(store::Placed { path: format!("{language}/MessageData/Message.arc/{name}"), position, size: bytes.len() as u64, folder: false });
            }
        }
        let end = pak.stream_position().unwrap();
        store::write_index(&mut pak, end, &placed, b"RMGE01\0\0").unwrap();
        drop(pak);
        let all = dir.path().join("messages.json");
        fs::write(&all, br#"{"lines":{"Mod_Hello":{"default":"Hello","UsFrench":"Bonjour"}},"flows":{"Game_A":"Mod_Hello"}}"#).unwrap();
        let french = dir.path().join("message.bmg.json");
        fs::write(&french, br#"{"lines":{"Mod_Bye":"Au revoir"},"flows":{"Mod_Hello":"Mod_Bye"}}"#).unwrap();
        let broken = dir.path().join("broken.json");
        fs::write(&broken, br#"{"lines":{"Mod_X":{"French":"x"}}}"#).unwrap();
        let changes = vec![
            Change { path: "MessageData/messages.json".into(), source: all, merge: true },
            Change { path: "UsFrench/MessageData/Message.arc/message.bmg.json".into(), source: french, merge: true },
            Change { path: "MessageData/messages.json".into(), source: broken, merge: true },
        ];
        let output = dir.path().join("mods.pak");
        let failed = write_overlay("RMGE01", &changes, Some(&base), &output, &mut Stats::default()).unwrap();
        // The broken merge has words in French only: English has none, so no language takes it.
        assert_eq!(failed.len(), 1, "{failed:?}");
        assert!(failed[0].1.contains("UsEnglish"), "{failed:?}");
        for (language, hello, bye) in [("UsEnglish", "Hello", None), ("UsFrench", "Bonjour", Some("Au revoir"))] {
            let folder = format!("{language}/MessageData/Message.arc");
            let file = store::read_file(&output, &format!("{folder}/message.bmg")).unwrap();
            let ids = store::read_file(&output, &format!("{folder}/messageid.tbl")).unwrap();
            let nmsg = super::super::message::Nmsg::from_nmsg(&file).unwrap();
            let words: Vec<String> = (0..nmsg.messages.len()).map(|i| super::super::message_merge::tests::words(&nmsg, i)).collect();
            assert!(words.contains(&hello.to_string()), "{language}: {words:?}");
            assert_eq!(words.iter().any(|w| w == "Au revoir"), bye.is_some(), "{language}: {words:?}");
            assert_eq!(super::super::table::row_count(&ids).unwrap(), 3 + usize::from(bye.is_some()), "{language}");
            // Game_A's talk goes on to the new line.
            let next = nmsg.nodes[0].next as usize;
            assert_eq!(words[nmsg.nodes[next].index as usize], hello, "{language}");
        }
    }

    #[test]
    fn a_failed_merge_loses_no_other_and_both_names_of_a_table_are_one() {
        let dir = tempfile::tempdir().unwrap();
        let base = dir.path().join("game.pak");
        let table = super::super::table::TableIr::from_json(br#"{"rows":[{"name":"Game"}]}"#).unwrap().to_ntbl();
        let mut pak = File::create(&base).unwrap();
        pak.write_all(&vec![0u8; 0x100]).unwrap();
        let position = pak.stream_position().unwrap();
        pak.write_all(&table).unwrap();
        let placed = vec![store::Placed { path: "ObjectData/Things/Things.bcsv".into(), position, size: table.len() as u64, folder: false }];
        let end = pak.stream_position().unwrap();
        store::write_index(&mut pak, end, &placed, b"RMGE01\0\0").unwrap();
        drop(pak);
        let file = |name: &str, text: &str| {
            let path = dir.path().join(name);
            fs::write(&path, text).unwrap();
            path
        };
        let changes = vec![
            Change { path: "ObjectData/Things.arc/Things.bcsv.json".into(), source: file("a.json", r#"{"rows":[{"name":"A"}]}"#), merge: true },
            // The same table by its store name, in another case.
            Change { path: "objectdata/things/things.bcsv.json".into(), source: file("b.json", r#"{"rows":[{"name":"B"}]}"#), merge: true },
            // A column the table lacks: this merge fails, the two before it stand.
            Change {
                path: "ObjectData/Things.arc/Things.bcsv.json".into(),
                source: file("c.json", r#"{"change":[{"where":{"name":"A"},"set":{"nope":1}}]}"#),
                merge: true,
            },
        ];
        let output = dir.path().join("mods.pak");
        let failed = write_overlay("RMGE01", &changes, Some(&base), &output, &mut Stats::default()).unwrap();
        assert_eq!(failed.len(), 1, "{failed:?}");
        assert!(failed[0].1.contains("nope"), "{failed:?}");
        let merged = store::read_file(&output, "ObjectData/Things.arc/Things.bcsv").unwrap();
        assert_eq!(super::super::table::row_count(&merged).unwrap(), 3);
    }

    /// A disc table with one row: one long column.
    fn one_row_table() -> Vec<u8> {
        let mut out = Vec::new();
        for word in [1u32, 1, 16 + 12, 4] {
            out.extend_from_slice(&word.to_be_bytes());
        }
        out.extend_from_slice(&0x1234_5678u32.to_be_bytes());
        out.extend_from_slice(&u32::MAX.to_be_bytes());
        out.extend_from_slice(&[0, 0, 0, 0]);
        out.extend_from_slice(&7u32.to_be_bytes());
        out
    }

    #[test]
    fn a_member_file_is_cooked_by_what_it_is_and_a_console_file_is_refused() {
        let dir = std::env::temp_dir().join(format!("nebula_overlay_members_{}", std::process::id()));
        fs::create_dir_all(&dir).unwrap();
        let table = dir.join("objinfo");
        fs::write(&table, one_row_table()).unwrap();
        let banner = dir.join("opening.bnr");
        fs::write(&banner, b"IMET").unwrap();
        // A placement table has no ending that `cooks` knows: its bytes say it is one.
        let placement = "StageData/TestGalaxy.arc/jmp/placement/common/objinfo";
        assert!(!cooks(placement));
        let changes = vec![
            Change { path: placement.into(), source: table, merge: false },
            Change { path: "opening.bnr".into(), source: banner, merge: false },
        ];
        let output = dir.join("mods.pak");
        let mut stats = Stats::default();
        let failed = write_overlay("RMGE01", &changes, None, &output, &mut stats).unwrap();
        assert_eq!(failed.len(), 1);
        assert_eq!(failed[0].0, "opening.bnr");
        assert!(failed[0].1.contains("console"));
        let cooked = store::read_file(&output, placement).unwrap();
        assert_eq!(&cooked[..4], super::super::table::MAGIC);
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn a_new_stage_s_tables_can_be_json() {
        let dir = std::env::temp_dir().join(format!("nebula_overlay_stage_json_{}", std::process::id()));
        fs::create_dir_all(&dir).unwrap();
        let objinfo = dir.join("objinfo.json");
        fs::write(&objinfo, br#"{ "columns": { "pos_x": "float" }, "rows": [ { "name": "Kuribo", "l_id": 1, "pos_x": 100 } ] }"#).unwrap();
        let camera = dir.join("cameraparam.bcam.json");
        fs::write(&camera, br#"{ "rows": [ { "id": "c:0000", "num1": 0 } ] }"#).unwrap();
        let changes = vec![
            Change { path: "StageData/NewGalaxy.arc/jmp/placement/common/objinfo.json".into(), source: objinfo, merge: false },
            Change { path: "StageData/NewGalaxy.arc/camera/cameraparam.bcam.json".into(), source: camera, merge: false },
        ];
        let output = dir.join("mods.pak");
        let mut stats = Stats::default();
        let failed = write_overlay("RMGE01", &changes, None, &output, &mut stats).unwrap();
        assert!(failed.is_empty(), "{failed:?}");
        for path in ["StageData/NewGalaxy.arc/jmp/placement/common/objinfo", "StageData/NewGalaxy.arc/camera/cameraparam.bcam"] {
            let cooked = store::read_file(&output, path).unwrap();
            assert_eq!(&cooked[..4], super::super::table::MAGIC, "{path}");
        }
        let _ = fs::remove_dir_all(&dir);
    }
}
