//! Mods' files as the game reads them: each file a mod changes or adds is cooked as the disc's were at import and
//! written to a store of its own, which the game reads over game.pak (runtime/src/asset_store.cpp). The plan names them,
//! one a line: the game id, then `path<TAB>source file`; `%mods` and `%patch` lines give the patches by key
//! (overlay_patches), whose facts go in `<store>.facts`.

use super::overlay_patches::{apply_patches, facts_text, read_patches, PlanPatches};
use super::patch::Fact;
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
    for line in lines.filter(|line| !line.is_empty() && !line.starts_with("base\t") && !line.starts_with("=material\t") && !line.starts_with('%')) {
        let (path, source) = line.split_once('\t').with_context(|| format!("the plan's line {line:?}"))?;
        let (merge, path) = path.strip_prefix('+').map_or((false, path), |rest| (true, rest));
        changes.push(Change { path: path.trim_start_matches('/').to_string(), source: PathBuf::from(source), merge });
    }
    Ok((game_id, changes))
}

/// Where a mod's model file goes and the game model it stands in for (models contract 2, G-M1): `X.bdl.glb` and
/// `X.bmd.glb` that file; a plain `X.glb` in an archive whichever of `X.bdl` and `X.bmd` the game's archive has. A model
/// outside an archive is a new model, standing in for nothing (a mod's `assets/<path>.glb`, the store's
/// `Luma/<id>/<path>.glb`): it becomes an object archive of its own laid out as the game's, `<path>.arc/X.bmd` (or
/// `X.bdl` when named so), its clips inside, which the game loads as it loads its own. Without the game's store the
/// place is the one its name gives and its joints are not numbered by the game's (a Note says so).
fn model_place(change: &Change, base: Option<&Path>) -> Result<(String, Option<super::gltf::GameModel>)> {
    use super::refuse::{note, refuse};
    let lower = change.path.to_ascii_lowercase();
    let without_modern = &change.path[..change.path.rfind('.').unwrap_or(change.path.len())];
    let explicit = [".bdl.gltf", ".bdl.glb", ".bmd.gltf", ".bmd.glb"].iter().any(|e| lower.ends_with(e));
    let (folder, file) = change.path.rsplit_once('/').unwrap_or(("", &change.path));
    let stem = file.split('.').next().unwrap_or(file);
    let written = if explicit { without_modern.to_string() } else { format!("{}.bmd", &change.path[..change.path.len() - file.len() + stem.len()]) };
    if !folder.to_ascii_lowercase().ends_with(".arc") {
        let ending = if lower.ends_with(".bdl.gltf") || lower.ends_with(".bdl.glb") { "bdl" } else { "bmd" };
        let archive = &change.path[..change.path.len() - file.len() + stem.len()];
        return Ok((format!("{archive}.arc/{stem}.{ending}"), None));
    }
    let Some(base) = base else {
        note("Cook.Model.GameUnknown", &[("file", change.path.clone())]);
        return Ok((written, None));
    };
    let models: Vec<(String, Vec<u8>)> = store::read_folder(base, folder)
        .unwrap_or_default()
        .into_iter()
        .filter(|(name, _)| {
            let name = name.to_ascii_lowercase();
            !name.contains('/') && (name.ends_with(".bdl") || name.ends_with(".bmd"))
        })
        .collect();
    let wanted: Vec<&(String, Vec<u8>)> = if explicit {
        let name = written.rsplit('/').next().unwrap_or(&written).to_ascii_lowercase();
        models.iter().filter(|(n, _)| n.to_ascii_lowercase() == name).collect()
    } else {
        models.iter().filter(|(n, _)| n.split('.').next().unwrap_or(n).eq_ignore_ascii_case(stem)).collect()
    };
    match wanted.as_slice() {
        [] => {
            let names: Vec<&str> = models.iter().map(|(n, _)| n.as_str()).collect();
            Err(refuse("Cook.Model.NoGameModel", &[("archive", folder.to_string()), ("models", names.join(", "))]))
        }
        [(name, block)] => {
            let path = format!("{folder}/{name}");
            let game = super::gltf::GameModel::read(&path, block);
            if game.is_none() {
                note("Cook.Model.GameUnknown", &[("file", change.path.clone())]);
            }
            Ok((path, game))
        }
        _ => Err(refuse("Cook.Model.TwoGameModels", &[("name", stem.to_string()), ("archive", folder.to_string())])),
    }
}

/// The model an animation file in game archive `folder` moves: its one model, else the one named as the archive
/// (`Kuribo.arc/Kuribo.bdl`).
fn archive_model(folder: &str, base: &Path) -> Result<super::gltf::GameModel> {
    use super::refuse::refuse;
    let models: Vec<(String, Vec<u8>)> = store::read_folder(base, folder)
        .unwrap_or_default()
        .into_iter()
        .filter(|(name, _)| {
            let name = name.to_ascii_lowercase();
            !name.contains('/') && (name.ends_with(".bdl") || name.ends_with(".bmd"))
        })
        .collect();
    let archive = folder.rsplit('/').next().unwrap_or(folder);
    let archive = &archive[..archive.len() - ".arc".len()];
    let named: Vec<&(String, Vec<u8>)> = models.iter().filter(|(n, _)| n.split('.').next().unwrap_or(n).eq_ignore_ascii_case(archive)).collect();
    let (name, block) = match (models.as_slice(), named.as_slice()) {
        ([], _) => return Err(refuse("Cook.Model.NoGameModel", &[("archive", folder.to_string()), ("models", String::new())])),
        ([one], _) => one,
        (_, [one]) => *one,
        _ => {
            let names: Vec<&str> = models.iter().map(|(n, _)| n.as_str()).collect();
            return Err(refuse("Cook.Anim.WhichModel", &[("archive", folder.to_string()), ("models", names.join(", "))]));
        }
    };
    let path = format!("{folder}/{name}");
    super::gltf::GameModel::read(&path, block).ok_or_else(|| refuse("Cook.Model.NoGameModel", &[("archive", folder.to_string()), ("models", name.clone())]))
}

/// A new model's level of detail (models contract 6: `<path>Middle.glb` or `<path>Low.glb` outside an archive, the
/// endings with their capitals as LodCtrl names them) whose base model `<path>` is none of the mod's files: refused
/// as `Cook.Model.LevelAlone` {file, model}. The store compares paths without case, so the base is found so.
fn level_alone(change: &Change, changes: &[Change]) -> Result<()> {
    let lower = change.path.to_ascii_lowercase();
    let (folder, file) = change.path.rsplit_once('/').map_or(("", change.path.as_str()), |(f, n)| (f, n));
    if folder.to_ascii_lowercase().ends_with(".arc") || !(lower.ends_with(".glb") || lower.ends_with(".gltf")) {
        return Ok(());
    }
    let stem = file.split('.').next().unwrap_or(file);
    let Some(base) = ["Middle", "Low"].iter().find_map(|level| stem.strip_suffix(level).filter(|b| !b.is_empty())) else {
        return Ok(());
    };
    let prefix = if folder.is_empty() { String::new() } else { format!("{folder}/") };
    let wanted = format!("{prefix}{base}").to_ascii_lowercase();
    let is_base = |c: &Change| {
        let path = c.path.to_ascii_lowercase();
        !c.merge && [".glb", ".gltf", ".bmd.glb", ".bmd.gltf", ".bdl.glb", ".bdl.gltf"].iter().any(|e| path == format!("{wanted}{e}"))
    };
    if changes.iter().any(is_base) {
        return Ok(());
    }
    Err(super::refuse::refuse("Cook.Model.LevelAlone", &[("file", change.path.clone()), ("model", format!("{prefix}{base}.glb"))]))
}

/// The game's sound names in the store `base` (AudioRes/SMR, cooked), read once per store: None when it has none.
fn game_sound_names(base: &Path) -> Option<std::sync::Arc<std::collections::HashSet<String>>> {
    type Known = Option<(std::path::PathBuf, Option<std::sync::Arc<std::collections::HashSet<String>>>)>;
    static LAST: std::sync::Mutex<Known> = std::sync::Mutex::new(None);
    let mut last = LAST.lock().unwrap_or_else(|e| e.into_inner());
    if let Some((path, names)) = last.as_ref() {
        if path == base {
            return names.clone();
        }
    }
    let names = store::read_file(base, "AudioRes/SMR.szs")
        .ok()
        .and_then(|nsnd| super::sound::sound_names(&nsnd).ok())
        .map(|list| std::sync::Arc::new(list.into_iter().collect()));
    *last = Some((base.to_path_buf(), names.clone()));
    names
}

/// What a modern file of a mod becomes: each file's place in the game, with the game's ending, its bytes, and whether
/// they are the store's already (else the cooker reads them). Empty: it isn't one. A model's animations go beside it.
fn modern_files(change: &Change, bytes: &[u8], base: Option<&Path>) -> Result<Vec<(String, Vec<u8>, bool)>> {
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
    // Clip.anim.json in a game archive: the clip's frame events, Clip.events beside it (animation contract 5).
    if super::clip_events::is_events_source(&lower) {
        let (folder, clip, path) = super::clip_events::place(&change.path)?;
        let members = base.and_then(|base| store::read_folder(base, &folder).ok()).unwrap_or_default();
        let frames = super::clip_events::lead_frames(&members, &clip);
        if frames.is_none() {
            let archive = folder.clone();
            if base.is_some() {
                super::refuse::note("Cook.Events.ClipUnknown", &[("file", change.path.clone()), ("clip", clip.clone()), ("archive", archive)]);
            } else {
                super::refuse::note("Cook.Events.GameUnknown", &[("file", change.path.clone())]);
            }
        }
        let (table, facts) = super::clip_events::cook(bytes, &change.path, frames)?;
        super::refuse::record(super::refuse::CookFact::Events { path: path.clone(), source: change.path.clone(), facts });
        return Ok(vec![(path, table, true)]);
    }
    // Name.anim.glb in a game archive: clips of the archive's model (animation contract 2).
    if lower.ends_with(".anim.glb") || lower.ends_with(".anim.gltf") {
        let folder = change.path.rsplit_once('/').map_or("", |(f, _)| f);
        if !folder.to_ascii_lowercase().ends_with(".arc") {
            return Err(super::refuse::refuse("Cook.Anim.NotInArchive", &[("file", change.path.clone())]));
        }
        let base = base.ok_or_else(|| super::refuse::refuse("Cook.Anim.NoGame", &[("file", change.path.clone())]))?;
        let game = archive_model(folder, base)?;
        let (clips, made) = super::gltf::to_clip_files(bytes, change.source.parent(), &change.path, &game)?;
        super::refuse::record(super::refuse::CookFact::Model { path: game.path.clone(), source: change.path.clone(), facts: made });
        return Ok(clips.into_iter().map(|(file, clip)| (format!("{folder}/{file}"), clip, true)).collect());
    }
    if lower.ends_with(".gltf") || lower.ends_with(".glb") {
        let file = change.path.rsplit('/').next().unwrap_or(&change.path);
        let stem = file.split('.').next().unwrap_or(file);
        let (native_path, game) = model_place(change, base)?;
        let (model, animations, made) = super::gltf::to_files_for(bytes, change.source.parent(), stem, game.as_ref())?;
        super::refuse::record(super::refuse::CookFact::Model { path: native_path.clone(), source: change.path.clone(), facts: made });
        // Clips go beside the model in its archive.
        let folder = native_path.rfind('/').map_or(String::new(), |at| native_path[..at + 1].to_string());
        let mut out = vec![(native_path, model, true)];
        out.extend(animations.into_iter().map(|(file, clip)| (format!("{folder}{file}"), clip, true)));
        return Ok(out);
    }
    // Name.layout.json in an archive (or its blyt/): the game's layout file blyt/Name.brlyt, which the store cooks as
    // the game's (layout contract 1). Its pictures' textures are the archive's timg/<texture>.png (or .tpl).
    if lower.ends_with(".layout.json") {
        let (folder, file) = change.path.rsplit_once('/').unwrap_or(("", &change.path));
        let in_blyt = folder.to_ascii_lowercase().ends_with(".arc/blyt");
        let archive = if in_blyt { &folder[..folder.len() - "/blyt".len()] } else { folder };
        if !archive.to_ascii_lowercase().ends_with(".arc") {
            return Err(super::refuse::refuse("Cook.Layout.NotInArchive", &[("file", change.path.clone())]));
        }
        let stem = &file[..file.len() - ".layout.json".len()];
        // The archive's folder on disk, when the file is read from one (a mod folder, or the game's unpacked mods).
        let on_disk = change.source.parent().map(|p| if in_blyt { p.parent().unwrap_or(p) } else { p }).filter(|p| p.is_dir());
        let missing_texture = |name: &str| -> Option<String> {
            let timg = on_disk?.join("timg");
            let wanted = [format!("{name}.png"), format!("{name}.tpl")];
            let found = fs::read_dir(&timg).ok().is_some_and(|entries| {
                entries.flatten().any(|e| wanted.iter().any(|w| e.file_name().to_string_lossy().eq_ignore_ascii_case(w)))
            });
            (!found).then(|| format!("{archive}/timg/{name}.png"))
        };
        // A text naming this mod's own font (Luma/<id>/<path>): the mod's folder, when the file is read from one, has its
        // source in a layer's assets (layout contract 2a). Another mod's font is that mod's to have.
        let mod_root = change.source.ancestors().skip(1).find(|a| a.join("luma.json").is_file());
        let mod_id = mod_root
            .and_then(|root| fs::read(root.join("luma.json")).ok())
            .and_then(|b| serde_json::from_slice::<serde_json::Value>(&b).ok())
            .and_then(|v| v["id"].as_str().map(str::to_string));
        let missing_font = |font: &str| -> Option<String> {
            let (root, id) = (mod_root?, mod_id.as_deref()?);
            let (owner, path) = font.strip_prefix("Luma/")?.split_once('/')?;
            if !owner.eq_ignore_ascii_case(id) {
                return None;
            }
            let file = format!("{path}.font.json");
            let found = fs::read_dir(root.join("layers")).ok()?.flatten().any(|layer| layer.path().join("assets").join(&file).is_file());
            (!found).then(|| format!("assets/{file}"))
        };
        let (brlyt, facts) = super::layout_json::to_brlyt_with(bytes, &missing_texture, &missing_font)?;
        let path = format!("{archive}/blyt/{stem}.brlyt");
        super::refuse::record(super::refuse::CookFact::Layout { path: path.clone(), source: change.path.clone(), facts });
        return Ok(vec![(path, brlyt, false)]);
    }
    // Name.sound.json in a mod's assets (Luma/<id>/...): a sound effect or voice line, Name.nsfx, played by its path
    // without the ending (sound contract 1, 9); a stream, Name.ast (8); a sequence, Name.nseq (10). With `replace` a
    // stream or sequence also takes the game's file's place. The sound file it names is read beside it.
    if lower.ends_with(".sound.json") {
        if !lower.starts_with("luma/") {
            return Err(super::refuse::refuse("Cook.Sound.NotAsset", &[("file", change.path.clone())]));
        }
        let folder = change.source.parent().map(Path::to_path_buf);
        let file = |name: &str| -> Option<Vec<u8>> { fs::read(folder.as_ref()?.join(name)).ok() };
        // The game's sound names, for `replace` (layer 2): from the store's cooked sound archive, when there is one.
        let names = base.and_then(game_sound_names);
        let game_sound = |name: &str| -> Option<bool> { names.as_ref().map(|n| n.contains(name)) };
        let game_file = |path: String| -> Option<bool> { base.and_then(|b| store::has_file(b, &path).ok()) };
        let game_stream = |name: &str| game_file(format!("AudioRes/Stream/{name}.ast"));
        let game_sequence = |name: &str| game_file(format!("AudioRes/Seqs/JaiSeq.arc/{name}"));
        let around = super::sound_effect::Around { file: &file, game_sound: &game_sound, game_stream: &game_stream, game_sequence: &game_sequence };
        let (made, facts) = super::sound_effect::cook_sound(bytes, &around)?;
        let stem = &change.path[..change.path.len() - ".sound.json".len()];
        let files: Vec<(String, Vec<u8>, bool)> = made
            .into_iter()
            .map(|(place, bytes)| match place {
                super::sound_effect::Made::Own(ending) => (format!("{stem}{ending}"), bytes, true),
                super::sound_effect::Made::Game(path) => (path, bytes, true),
            })
            .collect();
        super::refuse::record(super::refuse::CookFact::Sound { path: files[0].0.clone(), source: change.path.clone(), facts });
        return Ok(files);
    }
    // Name.movie.json in a mod's assets: a movie, Name.nmov (AV1), its subtitles Name.subs beside it, and with `replace`
    // the same movie at the game's MovieData/<name>.thp (movies contract). The files it names are read beside it.
    if super::movie_cook::is_movie_source(&lower) {
        if !lower.starts_with("luma/") {
            return Err(super::refuse::refuse("Cook.Movie.NotAsset", &[("file", change.path.clone())]));
        }
        let folder = change.source.parent().map(Path::to_path_buf);
        let file = |name: &str| -> Option<Vec<u8>> { fs::read(folder.as_ref()?.join(name)).ok() };
        let game_movie = |name: &str| base.and_then(|b| store::has_file(b, &format!("MovieData/{name}.thp")).ok());
        let (made, facts) = super::movie_cook::cook(bytes, &super::movie_cook::Around { file: &file, game_movie: &game_movie })?;
        let stem = &change.path[..change.path.len() - ".movie.json".len()];
        let files: Vec<(String, Vec<u8>, bool)> = made
            .into_iter()
            .map(|(place, bytes)| match place {
                super::movie_cook::Made::Own(ending) => (format!("{stem}{ending}"), bytes, true),
                super::movie_cook::Made::Game(path) => (path, bytes, true),
            })
            .collect();
        super::refuse::record(super::refuse::CookFact::Movie { path: files[0].0.clone(), source: change.path.clone(), facts });
        return Ok(files);
    }
    // Name.font.json in a mod's assets: a font, Luma/<id>/<path>.brfnt (the store cooks it as the game's fonts) with
    // its kerning (.kern) and fallback (.fallback) beside it (layout contract 2a). The font file it names is read beside it.
    if super::font_cook::is_font_source(&lower) {
        if !lower.starts_with("luma/") {
            return Err(super::refuse::refuse("Cook.Font.NotAsset", &[("file", change.path.clone())]));
        }
        let folder = change.source.parent().map(Path::to_path_buf);
        let file = |name: &str| -> Option<Vec<u8>> { fs::read(folder.as_ref()?.join(name)).ok() };
        let (files, facts) = super::font_cook::cook(bytes, &change.path, &file)?;
        super::refuse::record(super::refuse::CookFact::Font { path: files[0].0.clone(), source: change.path.clone(), facts });
        return Ok(files);
    }
    // A table as JSON: X.bcsv.json, a camera table X.bcam.json, or a stage's placement table (its name has no ending:
    // StageData/MyGalaxy.arc/jmp/placement/common/objinfo.json), as `lumalink extract` writes them.
    if lower.ends_with(".bcsv.json") || lower.ends_with(".bcam.json") || (lower.ends_with(".json") && lower.contains(".arc/jmp/")) {
        // A stage's placement rows may give their objects' arguments by name (stages contract 1).
        let expanded = if lower.contains(".arc/jmp/") { super::placement_args::expand_table(bytes)? } else { None };
        let table = super::table::TableIr::from_json(expanded.as_deref().unwrap_or(bytes))?;
        return Ok(vec![(change.path[..change.path.len() - ".json".len()].to_string(), table.to_ntbl(), true)]);
    }
    if lower.ends_with(".png") {
        // The game's interface keeps its pictures in another form, in a folder of that name.
        if lower.contains("/timg/") {
            return Ok(vec![(renamed(".tpl"), super::gltf::png_to_tpl(bytes, &change.path)?, false)]);
        }
        return Ok(vec![(renamed(".bti"), super::gltf::png_to_bti(bytes, &change.path)?, false)]);
    }
    if super::modern_audio::is_stream_source(&lower) {
        return Ok(vec![(renamed(".ast"), super::modern_audio::to_stream(&change.path, bytes)?, true)]);
    }
    Ok(Vec::new())
}

/// A mod's source file at game path `path` (`source`: where it is read from, for the files it names), cooked alone
/// against the game's store `base` as the overlay cooks it: how many game files it makes; 0 when its kind is none the
/// cook makes (luma_validate's `cooked_source` lists those), an error (a refusal, L804) when the cook refuses it.
pub fn cook_source(path: &str, source: &Path, bytes: &[u8], base: Option<&Path>) -> Result<usize> {
    let change = Change { path: path.trim_start_matches('/').to_string(), source: source.to_path_buf(), merge: false };
    modern_files(&change, bytes, base).map(|made| made.len())
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
/// paths and reasons are returned. The patches by key apply after the files and merges, and their facts go in `facts`.
pub fn write_overlay(game_id: &str, changes: &[Change], patches: &PlanPatches, base: Option<&Path>, output: &Path, facts: &mut Vec<Fact>,
                     stats: &mut Stats) -> Result<Vec<(String, String)>> {
    // The store's header holds a disc id of six letters (RMGE01): checked before anything is written.
    if game_id.len() != 6 || !game_id.bytes().all(|b| b.is_ascii_alphanumeric()) {
        bail!("{game_id:?} is not a game's disc id (six letters and digits, as RMGE01)");
    }
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
                level_alone(change, changes)?;
                // A sound or font file in a mod's assets is what a .sound.json or .font.json reads; not a file of the game.
                if super::sound_effect::is_asset_source(&change.path)
                    || super::font_cook::is_asset_source(&change.path)
                    || super::movie_cook::is_asset_source(&change.path)
                {
                    return Ok(Vec::new());
                }
                let modern = modern_files(change, &bytes, base)?;
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
            Err(error) => {
                if let Some(finding) = super::refuse::refusal_of(&error) {
                    super::refuse::record(super::refuse::CookFact::Refused { path: change.path.clone(), source: change.path.clone(), finding: finding.clone() });
                }
                failed.push((change.path.clone(), format!("{error:#}")));
            }
        }
        // What was said of the file, cooked or not (a refused file's notes say what came before).
        for finding in super::refuse::take_notes() {
            super::refuse::record(super::refuse::CookFact::Noted { path: change.path.clone(), source: change.path.clone(), finding });
        }
    }
    apply_merges(changes, base, &mut pak, &temporary, &mut at, &mut placed, &mut failed, stats)?;
    let patches = effect_table_patches(patches, base, &mut pak, &temporary, &placed, &mut failed)?;
    apply_patches(&patches, base, &mut pak, &temporary, &mut at, &mut placed, &mut failed, facts, stats)?;
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
    // A mod's effects are cooked after every other merge (apply_effects).
    let mut merges: Vec<&Change> = changes.iter().filter(|c| c.merge && super::effects::source_of(&c.path).is_none()).collect();
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
                // Particles as JSON are a layer's assets/effects.json (effects contract 7).
                return Err(super::refuse::refuse("Cook.Effect.OldForm", &[]));
            } else if let Some(table) = lower(&change.path).strip_suffix(".json") {
                let path = change.path[..table.len()].to_string();
                let table_key = place_of(&path);
                let current = match merged.get(&table_key) {
                    Some(current) => current.clone(),
                    None => merge_start(&path, base, temporary, pak, placed)?,
                };
                let mut rows = super::table::read_rows(&bytes)?;
                // A stage's placement rows may give their objects' arguments by name (stages contract 1).
                if lower(&change.path).contains(".arc/jmp/") && super::placement_args::any_args(&rows) {
                    let facts = super::placement_args::objects()?;
                    super::placement_args::expand_rows(&mut rows, "/rows", Some(&facts), true)?;
                }
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
                bail!("only tables (.json), messages (message.bmg.json, messages.json) and particles (.jpc) can be added to");
            }
            Ok(())
        })();
        if let Err(error) = result {
            failed.push((format!("merge/{}", change.path), format!("{error:#}")));
        }
    }
    apply_effects(changes, base, pak, temporary, placed, &mut merged, &mut cased, failed)?;
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

/// The patches with every patch of the effect table left out that names a particle nobody has (effects contract 7,
/// G-E6): the game's names and the mods' (after their effects) are known by now. A left-out patch is refused by its
/// phrase, naming the mod's patch file.
fn effect_table_patches(patches: &PlanPatches, base: Option<&Path>, pak: &mut File, temporary: &Path, placed: &[store::Placed],
                        failed: &mut Vec<(String, String)>) -> Result<PlanPatches> {
    use super::effects::{table_unknown, EFFECT_TABLE, PARTICLE_NAMES};
    let of_table = |p: &super::overlay_patches::PatchLine| store::same_path(&store::store_path(p.path.trim_start_matches('/')), &store::store_path(EFFECT_TABLE));
    if !patches.patches.iter().any(of_table) {
        return Ok(patches.clone());
    }
    // Without the names (no game known) the patches go on as they are, and the table's own check says the rest.
    let Ok(names) = merge_start(PARTICLE_NAMES, base, temporary, pak, placed) else { return Ok(patches.clone()) };
    let names = super::particle_json::names_index(&names)?;
    let mut kept = patches.clone();
    kept.patches.retain(|p| {
        if !of_table(p) {
            return true;
        }
        let Some(json) = fs::read(&p.source).ok().and_then(|b| serde_json::from_slice::<serde_json::Value>(&b).ok()) else { return true };
        let Some((at, word, numbered)) = table_unknown(&json, &|name| names.contains_key(name)) else { return true };
        let why = if numbered { "Cook.Effect.TableNoFirst" } else { "Cook.Effect.TableUnknown" };
        let error = super::refuse::refuse(why, &[("where", at), ("word", word)]);
        let path = format!("patch/{}/{}", p.mod_id, p.entry);
        if let Some(finding) = super::refuse::refusal_of(&error) {
            super::refuse::record(super::refuse::CookFact::Refused { path: path.clone(), source: p.entry.clone(), finding: finding.clone() });
        }
        failed.push((path, format!("{error:#}")));
        false
    });
    Ok(kept)
}

/// Each mod's effects (effects.rs), in the plan's order of mods: its `+Luma/<id>/effects.json` changes (one a layer)
/// cooked over the game's particles, their particles added after every merged one, its list of effects written. A mod
/// whose effects are refused adds nothing and is named; the other mods' still apply.
#[allow(clippy::too_many_arguments)]
fn apply_effects(changes: &[Change], base: Option<&Path>, pak: &mut File, temporary: &Path, placed: &[store::Placed],
                 merged: &mut BTreeMap<String, Vec<u8>>, cased: &mut BTreeMap<String, String>, failed: &mut Vec<(String, String)>) -> Result<()> {
    use super::effects::{self, PARTICLES, PARTICLE_NAMES};
    let mut mods: Vec<(String, Vec<&Change>)> = Vec::new();
    for change in changes.iter().filter(|c| c.merge) {
        if let Some(id) = effects::source_of(&change.path) {
            match mods.iter_mut().find(|(known, _)| known == id) {
                Some((_, files)) => files.push(change),
                None => mods.push((id.to_string(), vec![change])),
            }
        }
    }
    if mods.is_empty() {
        return Ok(());
    }
    let place_of = |text: &str| store::store_path(text.trim_start_matches('/')).to_ascii_lowercase();
    // `like` and `replace` start from the game's own particles, whatever an earlier mod did to them.
    let game_read = || -> Result<(super::particles::ParticleIr, std::collections::HashMap<String, u32>)> {
        let Some(base) = base else { return Ok(Default::default()) };
        let particles = store::read_file(base, PARTICLES).context("the game's particles")?;
        let names = store::read_file(base, PARTICLE_NAMES).context("the game's particle names")?;
        Ok((super::particles::read_nptc(&particles)?, super::particle_json::names_index(&names)?))
    };
    // Read once; a game without them refuses each mod's effects, not the store.
    let game_read = game_read().map_err(|e| format!("{e:#}"));
    let mut replaced_by: BTreeMap<String, String> = BTreeMap::new();
    for (id, files) in mods {
        let path = effects::asset_path(&id, "effects.json");
        let source = files.iter().map(|c| c.source.display().to_string()).collect::<Vec<_>>().join(" ");
        let result = (|| -> Result<()> {
            let bytes = files.iter().map(|c| fs::read(&c.source).with_context(|| format!("{} cannot be read", c.source.display()))).collect::<Result<Vec<_>>>()?;
            let mut image = |file: &str| {
                let at = effects::asset_path(&id, file);
                changes.iter().rev().find(|c| !c.merge && store::same_path(c.path.trim_start_matches('/'), &at)).and_then(|c| fs::read(&c.source).ok())
            };
            let (game, game_names) = game_read.as_ref().map_err(|e| anyhow::anyhow!("{e}"))?;
            let game = super::particle_json::Game { particles: game, names: game_names, any_name: false };
            let cooked = effects::cook(&bytes, &mut image, &game)?;
            let mut current = |at: &str| match merged.get(&place_of(at)) {
                Some(bytes) => Ok(bytes.clone()),
                None => merge_start(at, base, temporary, pak, placed),
            };
            let (particles, names) = (current(PARTICLES)?, current(PARTICLE_NAMES)?);
            let applied = effects::apply(&id, &cooked, &particles, &names)?;
            let mut written = vec![(PARTICLES.to_string(), applied.particles), (PARTICLE_NAMES.to_string(), applied.names)];
            // A mod that only replaces has no list (J's reader: no list, no effects of its own).
            if !cooked.effects.is_empty() {
                written.push((effects::list_path(&id), effects::list_text(&cooked)));
            }
            for (at, bytes) in written {
                cased.entry(place_of(&at)).or_insert_with(|| at.clone());
                merged.insert(place_of(&at), bytes);
            }
            for r in &cooked.replaced {
                if let Some(lower) = replaced_by.insert(r.name.clone(), id.clone()) {
                    super::refuse::note("Cook.Effect.Overlap", &[("name", r.name.clone()), ("other", lower), ("winner", id.clone())]);
                }
            }
            for facts in applied.facts {
                super::refuse::record(super::refuse::CookFact::Effect { path: path.clone(), source: source.clone(), facts });
            }
            Ok(())
        })();
        if let Err(error) = result {
            if let Some(finding) = super::refuse::refusal_of(&error) {
                super::refuse::record(super::refuse::CookFact::Refused { path: path.clone(), source: source.clone(), finding: finding.clone() });
            }
            failed.push((path.clone(), format!("{error:#}")));
        }
        for finding in super::refuse::take_notes() {
            super::refuse::record(super::refuse::CookFact::Noted { path: path.clone(), source: source.clone(), finding });
        }
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
/// The patches' facts go in `<store>.facts` on every cook, empty when there are none (so no older cook's survive);
/// the host logs each line.
pub fn cook_plan(plan: &Path, output: &Path) -> Result<(Stats, Vec<(String, String)>)> {
    let why = output.with_extension("why");
    fs::remove_file(&why).ok();
    let facts_file = output.with_extension("facts");
    fs::remove_file(&facts_file).ok();
    let mut facts = Vec::new();
    super::refuse::take_facts();
    super::refuse::take_notes();
    let cooked = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| cook_plan_inner(plan, output, &mut facts)))
        .unwrap_or_else(|panic| {
            let text = panic
                .downcast_ref::<&str>()
                .map(|t| t.to_string())
                .or_else(|| panic.downcast_ref::<String>().cloned())
                .unwrap_or_default();
            Err(anyhow::anyhow!("the converter stopped: {text}"))
        });
    fs::write(&facts_file, facts_text(&facts) + &super::refuse::cook_facts_text(&super::refuse::take_facts())).ok();
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

fn cook_plan_inner(plan: &Path, output: &Path, facts: &mut Vec<Fact>) -> Result<(Stats, Vec<(String, String)>)> {
    let text = fs::read_to_string(plan).with_context(|| format!("{} cannot be read", plan.display()))?;
    let (game_id, changes) = read_plan(&text)?;
    let patches = read_patches(&text)?;
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
    let result = write_overlay(&game_id, &changes, &patches, plan_base(&text).as_deref(), output, facts, &mut stats);
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

    /// A game store holding `files` (path, bytes) at their store paths.
    fn game_store(dir: &Path, files: &[(&str, &[u8])]) -> PathBuf {
        let base = dir.join("game.pak");
        let mut pak = File::create(&base).unwrap();
        pak.write_all(&vec![0u8; 0x100]).unwrap();
        let mut placed = Vec::new();
        for (path, bytes) in files {
            let position = pak.stream_position().unwrap();
            pak.write_all(bytes).unwrap();
            placed.push(store::Placed { path: store::store_path(path), position, size: bytes.len() as u64, folder: false });
        }
        let end = pak.stream_position().unwrap();
        store::write_index(&mut pak, end, &placed, b"RMGE01\0\0").unwrap();
        base
    }

    #[test]
    fn every_kind_the_validator_hands_the_cook_is_cooked() {
        // luma_validate::cooked_source passes these to the cook (L304): each is cooked or refused by name, none ignored.
        let cooked = [
            "ObjectData/Kuribo.arc/Kuribo.glb",
            "ObjectData/Kuribo.arc/Kuribo.gltf",
            "ObjectData/Kuribo.arc/Kuribo.anim.glb",
            "ObjectData/Kuribo.arc/Kuribo.anim.gltf",
            "ObjectData/Kuribo.arc/Ground.kcl.glb",
            "DemoData/Intro.arc/Shot.canm.gltf",
            "LuigiData/New.glb",
            "ObjectData/Kuribo.arc/Kuribo.png",
            "AudioRes/Stream/Song.wav",
            "AudioRes/Stream/Song.ogg",
            "ObjectData/Kuribo.arc/ActorAnimCtrl.bcsv.json",
            "ObjectData/Kuribo.arc/Kuribo.bcam.json",
            "StageData/MyGalaxy.arc/jmp/placement/common/objinfo.json",
            "LayoutData/Hud.arc/Hud.layout.json",
            "AudioRes/Jump.sound.json",
            "ObjectData/Kuribo.arc/Walk.anim.json",
            "LayoutData/Fonts/Title.font.json",
        ];
        let dir = tempfile::tempdir().unwrap();
        for path in cooked {
            assert!(luma_validate::cooked_source(&path.to_lowercase()), "{path}");
            match cook_source(path, &dir.path().join("x"), b"not this", None) {
                Ok(made) => assert!(made > 0, "{path}: made nothing"),
                Err(error) => assert!(super::super::refuse::refusal_of(&error).is_some() || !format!("{error:#}").is_empty(), "{path}"),
            }
        }
        // Kinds the cook does not make stay the validator's (L304 not yet defined) and the cook's answer is nothing.
        for path in ["Fonts/Title.ttf", "MovieData/Intro.webm", "ObjectData/Kuribo.arc/Notes.json"] {
            assert!(!luma_validate::cooked_source(&path.to_lowercase()), "{path}");
            assert_eq!(cook_source(path, &dir.path().join("x"), b"not this", None).unwrap(), 0, "{path}");
        }
    }

    #[test]
    fn a_layout_is_the_archives_layout_file_and_its_textures_are_the_mods() {
        use super::super::refuse::said;
        let dir = tempfile::tempdir().unwrap();
        let archive = dir.path().join("LayoutData/Hud.arc");
        std::fs::create_dir_all(archive.join("blyt")).unwrap();
        let source = br#"{"panes":[{"name":"Icon","type":"picture","size":[32,32],"texture":"Star"}]}"#;
        let change = |path: &str, at: &Path| Change { path: path.to_string(), source: at.to_path_buf(), merge: false };
        // The texture is not in the archive's timg/: refused, naming the file it would be.
        let error = modern_files(&change("LayoutData/Hud.arc/Hud.layout.json", &archive.join("Hud.layout.json")), source, None).unwrap_err();
        let (why, args) = said(&error);
        assert_eq!(why, "Cook.Layout.NoTexture");
        assert!(args.contains(&("uri", "LayoutData/Hud.arc/timg/Star.png".to_string())), "{args:?}");
        // With it (any case), the layout is blyt/<Name>.brlyt of the archive, for the store to cook as the game's.
        std::fs::create_dir_all(archive.join("timg")).unwrap();
        std::fs::write(archive.join("timg/STAR.png"), b"read by the PNG's own cook").unwrap();
        for (path, at) in [("LayoutData/Hud.arc/Hud.layout.json", archive.join("Hud.layout.json")), ("LayoutData/Hud.arc/blyt/Hud.layout.json", archive.join("blyt/Hud.layout.json"))] {
            let made = modern_files(&change(path, &at), source, None).unwrap();
            assert_eq!(made.len(), 1);
            assert_eq!((made[0].0.as_str(), made[0].2), ("LayoutData/Hud.arc/blyt/Hud.brlyt", false));
            super::super::ui_layout::read_layout(&made[0].1).unwrap();
        }
        let facts = super::super::refuse::take_facts();
        assert!(facts.iter().any(|f| matches!(f, super::super::refuse::CookFact::Layout { facts, .. } if facts.pictures == 1 && facts.textures == 1)), "{facts:?}");
        // Read from no folder (a packed mod's entry): the texture cannot be looked for here, and the game's start reads
        // the mod's files from disk.
        assert_eq!(cook_source("LayoutData/Hud.arc/Hud.layout.json", Path::new("layers/base/files/x"), source, None).unwrap(), 1);
        let error = cook_source("LayoutData/Hud.layout.json", &archive, source, None).unwrap_err();
        assert_eq!(said(&error).0, "Cook.Layout.NotInArchive");
    }

    #[test]
    fn a_layout_naming_its_own_mods_font_needs_the_fonts_source() {
        use super::super::refuse::said;
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path().join("my-mod");
        let archive = root.join("layers/base/files/LayoutData/Hud.arc");
        std::fs::create_dir_all(&archive).unwrap();
        std::fs::write(root.join("luma.json"), br#"{"id": "my.mod"}"#).unwrap();
        let text = |font: &str| format!(r#"{{"panes":[{{"name":"A","type":"text","size":[10,10],"font":"{font}"}}]}}"#);
        let path = "LayoutData/Hud.arc/Hud.layout.json";
        let source = archive.join("Hud.layout.json");
        let error = cook_source(path, &source, text("Luma/my.mod/fonts/Title").as_bytes(), None).unwrap_err();
        let (why, args) = said(&error);
        assert_eq!(why, "Cook.Layout.NoFont");
        assert!(args.contains(&("uri", "assets/fonts/Title.font.json".into())), "{args:?}");
        // In any layer's assets; another mod's font is not looked for.
        std::fs::create_dir_all(root.join("layers/extra/assets/fonts")).unwrap();
        std::fs::write(root.join("layers/extra/assets/fonts/Title.font.json"), b"{}").unwrap();
        assert_eq!(cook_source(path, &source, text("Luma/my.mod/fonts/Title").as_bytes(), None).unwrap(), 1);
        assert_eq!(cook_source(path, &source, text("Luma/other.mod/fonts/Title").as_bytes(), None).unwrap(), 1);
    }

    #[test]
    fn a_mods_font_is_cooked_as_the_games_fonts_with_its_kerning_and_fallback_beside_it() {
        use super::super::refuse::{said, take_facts, CookFact};
        let dir = tempfile::tempdir().unwrap();
        std::fs::write(dir.path().join("Title.ttf"), super::super::font_cook::tests::plain_font()).unwrap();
        let change = Change { path: "Luma/my.mod/fonts/Title.font.json".into(), source: dir.path().join("Title.font.json"), merge: false };
        let _ = take_facts();
        let made = modern_files(&change, br#"{"file": "Title.ttf", "size": 32}"#, None).unwrap();
        let places: Vec<(&str, bool)> = made.iter().map(|(p, _, done)| (p.as_str(), *done)).collect();
        assert_eq!(places, [("Luma/my.mod/fonts/Title.brfnt", false), ("Luma/my.mod/fonts/Title.kern", true), ("Luma/my.mod/fonts/Title.fallback", true)]);
        // The store's cook makes the BRFNT the game's font form.
        let items = super::super::cook_entry(&made[0].0, made[0].1.clone(), &mut Stats::default()).unwrap();
        assert!(items.iter().any(|i| i.bytes.as_deref().is_some_and(|b| b.starts_with(b"NFNT"))));
        let facts = take_facts();
        assert!(facts.iter().any(|f| matches!(f, CookFact::Font { path, facts, .. } if path == "Luma/my.mod/fonts/Title.brfnt" && facts.glyphs == 3)), "{facts:?}");
        // A font is a mod's asset, never a game file.
        let error = modern_files(&Change { path: "LayoutData/Title.font.json".into(), ..change }, b"{}", None).unwrap_err();
        assert_eq!(said(&error).0, "Cook.Font.NotAsset");
    }

    #[test]
    fn a_clips_events_go_beside_it_checked_against_the_games_clip() {
        use super::super::refuse::{said, said_notes, take_facts, CookFact};
        use super::super::table::TableIr;
        let dir = tempfile::tempdir().unwrap();
        // The game's walk as the store keeps it: the cooked clip form, 40 frames.
        let mut walk = super::super::clip::MAGIC.to_le_bytes().to_vec();
        walk.extend_from_slice(&[0u8; 12]);
        walk.extend_from_slice(&40i32.to_le_bytes());
        let base = game_store(dir.path(), &[("ObjectData/Kuribo.arc/walk.bck", &walk)]);
        let path = "ObjectData/Kuribo.arc/Walk.anim.json";
        let change = Change { path: path.into(), source: dir.path().join("Walk.anim.json"), merge: false };
        let text = br#"{"events": [{"frame": 39, "event": "last"}, {"frame": 0, "event": "start"}]}"#;
        let _ = take_facts();
        let made = modern_files(&change, text, Some(&base)).unwrap();
        assert_eq!(made.len(), 1);
        let (place, ntbl, done) = &made[0];
        assert_eq!((place.as_str(), *done), ("ObjectData/Kuribo.arc/Walk.events", true));
        assert_eq!(TableIr::from_ntbl(ntbl).unwrap().rows.len(), 2);
        let facts = take_facts();
        assert!(
            facts.iter().any(|f| matches!(f, CookFact::Events { path, facts, .. }
                if path == "ObjectData/Kuribo.arc/Walk.events" && facts.rows == 2 && facts.frames == Some(40))),
            "{facts:?}"
        );
        assert!(said_notes().is_empty());
        // Past the walk's last frame: refused, naming it.
        let error = modern_files(&change, br#"{"events": [{"frame": 40, "event": "a"}]}"#, Some(&base)).unwrap_err();
        assert_eq!(said(&error).0, "Cook.Events.PastEnd");
        // A clip the game's archive lacks, or no game: made, and said that the frames were not checked against a clip.
        let dance = Change { path: "ObjectData/Kuribo.arc/Dance.anim.json".into(), ..change };
        assert_eq!(modern_files(&dance, text, Some(&base)).unwrap().len(), 1);
        assert_eq!(said_notes().iter().map(|(why, _)| *why).collect::<Vec<_>>(), vec!["Cook.Events.ClipUnknown"]);
        assert_eq!(modern_files(&dance, text, None).unwrap().len(), 1);
        assert_eq!(said_notes().iter().map(|(why, _)| *why).collect::<Vec<_>>(), vec!["Cook.Events.GameUnknown"]);
        let _ = take_facts();
    }

    #[test]
    fn an_animation_file_makes_clips_of_its_archives_model() {
        use super::super::refuse::refusal_of;
        let dir = tempfile::tempdir().unwrap();
        // The game's Kuribo: body with head under it.
        let model = br#"{"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"name":"body","mesh":0,"children":[1]},{"name":"head","translation":[0,1,0]}],
            "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],
            "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],
            "bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],
            "buffers":[{"byteLength":42,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIA"}]}"#;
        let (block, _) = super::super::gltf::to_files(model, None, "Kuribo").unwrap();
        // An animation file: the skeleton, and Walk moving the head up over a second (two keys: 0 and 1 s, 1 m and 2 m).
        let data = "AAAAAAAAgD8AAAAAAACAPwAAAAAAAAAAAAAAQAAAAAA="; // times 0, 1; positions (0, 1, 0), (0, 2, 0) as little-endian floats
        let anim = format!(
            r#"{{"asset":{{"version":"2.0"}},"scene":0,"scenes":[{{"nodes":[0]}}],"nodes":[{{"name":"body","children":[1]}},{{"name":"head","translation":[0,1,0]}}],
            "animations":[{{"name":"Walk","samplers":[{{"input":0,"output":1}}],"channels":[{{"sampler":0,"target":{{"node":1,"path":"translation"}}}}]}}],
            "accessors":[{{"bufferView":0,"componentType":5126,"count":2,"type":"SCALAR"}},{{"bufferView":1,"componentType":5126,"count":2,"type":"VEC3"}}],
            "bufferViews":[{{"buffer":0,"byteLength":8}},{{"buffer":0,"byteOffset":8,"byteLength":24}}],
            "buffers":[{{"byteLength":32,"uri":"data:application/octet-stream;base64,{data}"}}]}}"#
        );
        let source = dir.path().join("Kuribo.anim.gltf");
        let one = game_store(dir.path(), &[("ObjectData/Kuribo.arc/kuribo.bdl", &block)]);
        assert_eq!(cook_source("ObjectData/Kuribo.arc/Kuribo.anim.gltf", &source, anim.as_bytes(), Some(&one)).unwrap(), 1);
        // Two models, neither named as the archive: which one is not guessed.
        let two_dir = tempfile::tempdir().unwrap();
        let two = game_store(two_dir.path(), &[("ObjectData/Kuribo.arc/a.bdl", &block), ("ObjectData/Kuribo.arc/b.bdl", &block)]);
        let error = cook_source("ObjectData/Kuribo.arc/Kuribo.anim.gltf", &source, anim.as_bytes(), Some(&two)).unwrap_err();
        assert_eq!(refusal_of(&error).map(|f| f.args.iter().any(|(n, v)| *n == "why" && *v == luma_validate::Arg::Phrase("Cook.Anim.WhichModel"))), Some(true), "{error:#}");
        // Not in a game archive, and no game to read the model from.
        for (path, base, why) in [
            ("ObjectData/Walk.anim.gltf", Some(one.as_path()), "Cook.Anim.NotInArchive"),
            ("ObjectData/Kuribo.arc/Kuribo.anim.gltf", None, "Cook.Anim.NoGame"),
        ] {
            let error = cook_source(path, &source, anim.as_bytes(), base).unwrap_err();
            assert_eq!(refusal_of(&error).map(|f| f.args.iter().any(|(n, v)| *n == "why" && *v == luma_validate::Arg::Phrase(why))), Some(true), "{why}: {error:#}");
        }
    }

    #[test]
    fn a_model_takes_the_place_and_joints_of_the_games_model() {
        use super::super::refuse::{refusal_of, take_notes};
        let dir = tempfile::tempdir().unwrap();
        // The game's Kuribo: joints body and head.
        let source = br#"{"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"name":"body","mesh":0,"children":[1]},{"name":"head"}],
            "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],
            "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],
            "bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],
            "buffers":[{"byteLength":42,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIA"}]}"#;
        let (kuribo, _) = super::super::gltf::to_files(source, None, "Kuribo").unwrap();
        let one = game_store(dir.path(), &[("ObjectData/Kuribo.arc/Kuribo.bdl", &kuribo), ("ObjectData/Kuribo.arc/Kuribo.btk", b"x")]);
        let both = dir.path().join("both");
        fs::create_dir(&both).unwrap();
        let two = game_store(&both, &[("ObjectData/Kuribo.arc/Kuribo.bdl", &kuribo), ("ObjectData/Kuribo.arc/Kuribo.bmd", &kuribo)]);
        let change = |path: &str| Change { path: path.into(), source: "Kuribo.glb".into(), merge: false };
        let why = |error: anyhow::Error| refusal_of(&error).map(|f| (f.args[0].1.clone(), luma_validate::args_text(f))).unwrap();

        let (path, game) = model_place(&change("ObjectData/Kuribo.arc/Kuribo.glb"), Some(&one)).unwrap();
        assert_eq!((path.as_str(), game.unwrap().joints), ("ObjectData/Kuribo.arc/Kuribo.bdl", vec!["body".to_string(), "head".to_string()]));
        // Another name in the archive: not the game's, and a new model goes in assets/.
        let (phrase, args) = why(model_place(&change("ObjectData/Kuribo.arc/Kurib.glb"), Some(&one)).unwrap_err());
        assert_eq!(phrase, luma_validate::Arg::Phrase("Cook.Model.NoGameModel"));
        assert!(args.iter().any(|a| a == "models=T:Kuribo.bdl"), "{args:?}");
        // Both endings: the file's name chooses.
        let (phrase, _) = why(model_place(&change("ObjectData/Kuribo.arc/Kuribo.glb"), Some(&two)).unwrap_err());
        assert_eq!(phrase, luma_validate::Arg::Phrase("Cook.Model.TwoGameModels"));
        let (path, game) = model_place(&change("ObjectData/Kuribo.arc/Kuribo.bmd.glb"), Some(&two)).unwrap();
        assert_eq!((path.as_str(), game.is_some()), ("ObjectData/Kuribo.arc/Kuribo.bmd", true));
        // No game store: the name's place, and a note that its joints are the file's.
        take_notes();
        let (path, game) = model_place(&change("ObjectData/Kuribo.arc/Kuribo.glb"), None).unwrap();
        assert_eq!((path.as_str(), game.is_none()), ("ObjectData/Kuribo.arc/Kuribo.bmd", true));
        assert_eq!(take_notes().len(), 1);
        // Outside an archive: a new model, standing in for nothing, in an archive of its own as the game's are.
        let (path, game) = model_place(&change("ObjectData/Thing.glb"), Some(&one)).unwrap();
        assert_eq!((path.as_str(), game.is_none()), ("ObjectData/Thing.arc/Thing.bmd", true));
        let (path, _) = model_place(&change("Luma/gold.mario/hats/top.bdl.glb"), Some(&one)).unwrap();
        assert_eq!(path, "Luma/gold.mario/hats/top.arc/top.bdl");
        let (path, _) = model_place(&change("Luma/gold.mario/hats/topLow.glb"), None).unwrap();
        assert_eq!(path, "Luma/gold.mario/hats/topLow.arc/topLow.bmd");
        // A level needs its base model among the mod's files (contract 6); the endings keep their capitals.
        let low = change("Luma/gold.mario/hats/topLow.glb");
        let (phrase, args) = why(level_alone(&low, std::slice::from_ref(&low)).unwrap_err());
        assert_eq!(phrase, luma_validate::Arg::Phrase("Cook.Model.LevelAlone"));
        assert!(args.contains(&"model=T:Luma/gold.mario/hats/top.glb".to_string()), "{args:?}");
        let both = [change("Luma/gold.mario/hats/topLow.glb"), change("Luma/gold.mario/hats/TOP.gltf")];
        assert!(level_alone(&both[0], &both).is_ok());
        let yellow = change("Luma/gold.mario/yellow.glb");
        assert!(level_alone(&yellow, std::slice::from_ref(&yellow)).is_ok());
        assert!(level_alone(&change("ObjectData/KuriboLow.arc/KuriboLow.glb"), &[]).is_ok());
        // The cook numbers the joints by the game's; one it lacks is refused by name.
        let swapped = String::from_utf8_lossy(source).replace("\"head\"", "\"neck\"");
        let (_, game) = model_place(&change("ObjectData/Kuribo.arc/Kuribo.glb"), Some(&one)).unwrap();
        let error = super::super::gltf::to_files_for(swapped.as_bytes(), None, "Kuribo", game.as_ref()).unwrap_err();
        let (phrase, args) = why(error);
        assert_eq!(phrase, luma_validate::Arg::Phrase("Cook.Model.MissingJoint"));
        assert!(args.contains(&"joint=T:head".to_string()), "{args:?}");
    }

    #[test]
    fn cook_facts_are_lines_of_the_facts_file() {
        use super::super::refuse::{cook_facts_text, refuse, refusal_of, CookFact};
        let error = refuse("Cook.Model.Joints", &[("count", "300".into()), ("max", "253".into())]);
        let finding = refusal_of(&error).unwrap().clone();
        let facts = cook_facts_text(&[
            CookFact::Refused { path: "ObjectData/A.arc/A.bdl.glb".into(), source: "ObjectData/A.arc/A.bdl.glb".into(), finding },
            CookFact::Model {
                path: "ObjectData/B.arc/B.bdl".into(),
                source: "ObjectData/B.arc/B.glb".into(),
                facts: super::super::gltf::ModelFacts { replaced: "ObjectData/B.arc/B.bdl".into(), joints: 2, vertices: 3, ..Default::default() },
            },
        ]);
        let lines: Vec<Vec<&str>> = facts.lines().map(|l| l.split('\t').collect()).collect();
        assert_eq!((lines[0][0], lines[0][1], lines[0][9]), ("failed", "ObjectData/A.arc/A.bdl.glb", "804"));
        assert!(lines[0][10].contains("300") && lines[0][10].contains("253"), "{:?}", lines[0]);
        assert!(lines[0][11..].contains(&"why=P:Cook.Model.Joints"), "{:?}", lines[0]);
        assert_eq!(lines[1][..5], ["cook.model", "ObjectData/B.arc/B.bdl", "", "ObjectData/B.arc/B.glb", "ObjectData/B.arc/B.bdl"]);
        assert_eq!(lines[1][11..], ["joints=2", "extra_joints=0", "meshes=0", "vertices=3", "materials=0", "textures=0", "clips=0"]);
    }

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
            let files = modern_files(&Change { path: path.into(), source: "fixture.gltf".into(), merge: false }, bytes, None).unwrap();
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
        let failed = write_overlay("RMGE01", &changes, &PlanPatches::default(), None, &out, &mut Vec::new(), &mut Stats::default()).unwrap();
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
        let failed = write_overlay("RMGE01", &changes, &PlanPatches::default(), None, &output, &mut Vec::new(), &mut stats).unwrap();
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
        let failed = write_overlay("RMGE01", &changes, &PlanPatches::default(), Some(&base), &output, &mut Vec::new(), &mut Stats::default()).unwrap();
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
        let failed = write_overlay("RMGE01", &changes, &PlanPatches::default(), Some(&base), &output, &mut Vec::new(), &mut Stats::default()).unwrap();
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
        let failed = write_overlay("RMGE01", &changes, &PlanPatches::default(), None, &output, &mut Vec::new(), &mut stats).unwrap();
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
        let failed = write_overlay("RMGE01", &changes, &PlanPatches::default(), None, &output, &mut Vec::new(), &mut stats).unwrap();
        assert!(failed.is_empty(), "{failed:?}");
        for path in ["StageData/NewGalaxy.arc/jmp/placement/common/objinfo", "StageData/NewGalaxy.arc/camera/cameraparam.bcam"] {
            let cooked = store::read_file(&output, path).unwrap();
            assert_eq!(&cooked[..4], super::super::table::MAGIC, "{path}");
        }
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn a_mods_effects_join_the_games_particles_and_its_list_is_beside_them() {
        use super::super::effects::{tests as fx, PARTICLES, PARTICLE_NAMES};
        let dir = tempfile::tempdir().unwrap();
        let (ir, _) = fx::game();
        let jpc = super::super::particles::write(&ir);
        let names = fx::names_table(&[("Glow", 0), ("Spark", 1)]);
        let base = game_store(dir.path(), &[(PARTICLES, &jpc), (PARTICLE_NAMES, &names)]);
        let file = |name: &str, bytes: &[u8]| {
            let path = dir.path().join(name);
            fs::write(&path, bytes).unwrap();
            path
        };
        let layer_one = br#"{"textures":{"Gold":"textures/gold.png"},"replace":{"Glow":{"emitter":{"rate":9}}},
            "effects":{"aura":{"draw":"BLOOM_EFFECT","particles":[{"like":"Glow","textures":{"sprite":"Gold"}}]}}}"#;
        let changes = vec![
            Change { path: "Luma/gold.mario/textures/gold.png".into(), source: file("gold.png", &fx::png()), merge: false },
            Change { path: "Luma/gold.mario/effects.json".into(), source: file("a1.json", layer_one), merge: true },
            Change { path: "Luma/gold.mario/effects.json".into(), source: file("a2.json", br#"{"effects":{"spark":{"particles":[{"like":"Spark"},{}]}}}"#), merge: true },
            Change { path: "Luma/other.mod/effects.json".into(), source: file("b.json", br#"{"replace":{"Glow":{"emitter":{"rate":7}}}}"#), merge: true },
            Change { path: "Luma/bad.mod/effects.json".into(), source: file("c.json", br#"{"effects":{"x01":{"particles":[{}]}}}"#), merge: true },
        ];
        super::super::refuse::take_facts();
        let output = dir.path().join("mods.pak");
        let failed = write_overlay("RMGE01", &changes, &PlanPatches::default(), Some(&base), &output, &mut Vec::new(), &mut Stats::default()).unwrap();
        // The bad mod's effects are refused whole; the others' stand.
        assert_eq!(failed.len(), 1, "{failed:?}");
        assert!(failed[0].0 == "Luma/bad.mod/effects.json" && failed[0].1.contains("x01"), "{failed:?}");
        assert_eq!(store::read_file(&output, "Luma/gold.mario/effects.txt").unwrap(), b"aura\t4\t1\nspark\t0\t2\n");
        assert!(store::read_file(&output, "Luma/other.mod/effects.txt").is_err());
        let after = super::super::particles::read_nptc(&store::read_file(&output, PARTICLES).unwrap()).unwrap();
        let index = super::super::particle_json::names_index(&store::read_file(&output, PARTICLE_NAMES).unwrap()).unwrap();
        assert_eq!((index["Glow"], index["gold.mario:aura00"], index["gold.mario:spark00"], index["gold.mario:spark01"]), (0, 2, 3, 4));
        assert_eq!(after.resources.len(), 5);
        let find = |n: u32| after.resources.iter().find(|r| r.user_index == n).unwrap();
        let rate = |n: u32| f32::from_le_bytes(find(n).dynamics[0x44..0x48].try_into().unwrap());
        // The later mod's Glow, made from the game's own: its rate, the game's sprite.
        assert_eq!((rate(0), rate(2)), (7.0, 4.0));
        let sprite = |n: u32| super::super::particle_json::Roles::of(&after, find(n)).sprite;
        assert_eq!((sprite(0).as_deref(), sprite(2).as_deref()), (Some("Ring"), Some("Gold")));
        let facts = super::super::refuse::take_facts();
        let lines = super::super::refuse::cook_facts_text(&facts);
        assert!(lines.contains("cook.effect\tLuma/gold.mario/effects.json\t\t"), "{lines}");
        assert!(lines.contains("\tgold.mario:aura\t\t\t\t\t\t\tparticles=2\tgroup=4\tsprite=Gold\t"), "{lines}");
        assert!(lines.contains("\tGlow\t\t\t\t\t\t\tparticles=0\tgroup=0\tsprite=Ring\ttextures=1\treplaced=1"), "{lines}");
        assert!(lines.contains("noted\tLuma/other.mod/effects.json") && lines.contains("why=P:Cook.Effect.Overlap"), "{lines}");
        assert!(lines.contains("failed\tLuma/bad.mod/effects.json") && lines.contains("why=P:Cook.Effect.NameBad"), "{lines}");
    }

    #[test]
    fn an_effect_table_patch_naming_no_particle_is_left_out() {
        use super::super::effects::{tests as fx, EFFECT_TABLE, PARTICLES, PARTICLE_NAMES};
        use super::super::overlay_patches::PatchLine;
        let dir = tempfile::tempdir().unwrap();
        let (ir, _) = fx::game();
        let jpc = super::super::particles::write(&ir);
        let names = fx::names_table(&[("Glow00", 0), ("Spark", 1)]);
        let table = super::super::table::TableIr::from_json(br#"{"rows":[{"UniqueName":"Base","EffectName":"Glow"}]}"#).unwrap().to_ntbl();
        let base = game_store(dir.path(), &[(PARTICLES, &jpc), (PARTICLE_NAMES, &names), (EFFECT_TABLE, &table)]);
        let file = |name: &str, bytes: &[u8]| {
            let path = dir.path().join(name);
            fs::write(&path, bytes).unwrap();
            path
        };
        let patch = |mod_id: &str, name: &str, text: &str| PatchLine {
            mod_id: mod_id.into(),
            entry: format!("layers/base/patches/{EFFECT_TABLE}.json"),
            path: EFFECT_TABLE.into(),
            source: file(name, text.as_bytes()),
        };
        let patches = PlanPatches {
            mods: vec!["good.mod".into(), "bad.mod".into()],
            patches: vec![
                patch("good.mod", "g.json", r#"{"patch":1,"key":["UniqueName"],"add":[{"UniqueName":"Mine","EffectName":"good.mod:aura"}]}"#),
                patch("bad.mod", "b.json", r#"{"patch":1,"key":["UniqueName"],"add":[{"UniqueName":"Nope","EffectName":"Nope"}]}"#),
            ],
        };
        let changes = vec![Change { path: "Luma/good.mod/effects.json".into(), source: file("fx.json", br#"{"effects":{"aura":{"particles":[{}]}}}"#), merge: true }];
        super::super::refuse::take_facts();
        let output = dir.path().join("mods.pak");
        let mut facts = Vec::new();
        let failed = write_overlay("RMGE01", &changes, &patches, Some(&base), &output, &mut facts, &mut Stats::default()).unwrap();
        // The bad mod's patch names no particle: it is left out; the good mod's names its own effect, cooked before it.
        assert_eq!(failed.len(), 1, "{failed:?}");
        assert!(failed[0].0.starts_with("patch/bad.mod/") && failed[0].1.contains("Nope00"), "{failed:?}");
        let lines = super::super::refuse::cook_facts_text(&super::super::refuse::take_facts());
        assert!(lines.contains("why=P:Cook.Effect.TableNoFirst") && lines.contains("where=T:/add/0/EffectName"), "{lines}");
        let after = super::super::table::TableIr::from_ntbl(&store::read_file(&output, EFFECT_TABLE).unwrap()).unwrap();
        assert_eq!(after.rows.len(), 2);
    }
}
