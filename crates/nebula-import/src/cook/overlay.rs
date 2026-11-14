//! Mods' files as the game reads them: each file a mod changes or adds is cooked as the disc's were at import and
//! written to a store of its own, which the game reads over game.pak (runtime/src/asset_store.cpp). The plan names them,
//! one a line: the game id, then `path<TAB>source file`; `%mods` and `%patch` lines give the patches by key
//! (overlay_patches), whose facts go in `<store>.facts`.

use super::overlay_patches::{apply_patches, facts_text, read_patches, PlanPatches};
use super::patch::Fact;
use super::{archive, audio_data, cook_entry, cooks, place, store, Stats, StoreItem, ALIGN};
use anyhow::{bail, Context, Result};
use luma_validate::Finding;
use std::collections::BTreeMap;
use std::fs::{self, File};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};

/// Where the store keeps the disc's wave layout.
pub const WAVE_LAYOUT: &str = "NebulaData/WaveLayout.txt";

/// A pack's ray tracing tables in its merge/ folder (RT-MASTER-PLAN 20), lower case: three class its models as they
/// cook (read before them, kept out of the store; hit_record), two are rows the host reads, added to the game's.
const RT_CLASSING: [&str; 3] = ["nebuladata/rtmaterials.tsv", "nebuladata/rtglow.tsv", "nebuladata/rtwater.tsv"];
const RT_ROWS: [&str; 2] = ["nebuladata/rtstages.tsv", "nebuladata/rteffects.tsv"];

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

/// Where a mod's model file goes and the game model it stands in for (docs/modding/content/models.md section 2):
//`X.bdl.glb` and / `X.bmd.glb` that file; a plain `X.glb` in an archive whichever of `X.bdl` and `X.bmd` the game's
//archive has. A model / outside an archive is a new model, standing in for nothing (a mod's `assets/<path>.glb`, the
//store's / `Luma/<id>/<path>.glb`): it becomes an object archive of its own laid out as the game's, `<path>.arc/X.bmd`
//(or / `X.bdl` when named so), its clips inside, which the game loads as it loads its own. Without the game's store the
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

/// A new model's level of detail (models.md section 6: `<path>Middle.glb` or `<path>Low.glb` outside an archive, the
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

/// Where a collision source `Name.kcl.glb` puts `Name.kcl` and `Name.pa`, without the ending: in an archive folder,
/// beside it; elsewhere (a mod's assets/, a folder that is no archive), in `Name.arc`, the archive a model `Name.glb`
/// of the same folder cooks into (model_place). An actor reads its collision from its own archive, so a planet whose
/// model and collision share a name finds both.
fn collision_stem(path: &str) -> String {
    let lower = path.to_ascii_lowercase();
    let stem = &path[..lower.find(".kcl.").unwrap_or(path.len())];
    let (folder, file) = stem.rsplit_once('/').unwrap_or(("", stem));
    if folder.to_ascii_lowercase().ends_with(".arc") {
        return stem.to_string();
    }
    format!("{stem}.arc/{file}")
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
        let stem = collision_stem(&change.path);
        return Ok(vec![(format!("{stem}.kcl"), collision, false), (format!("{stem}.pa"), attributes, false)]);
    }
    // Name.canm.glb: a cutscene's camera, from a glTF camera's animation.
    if lower.ends_with(".canm.glb") || lower.ends_with(".canm.gltf") {
        let stem = &change.path[..lower.rfind(".canm.").unwrap_or(change.path.len())];
        return Ok(vec![(format!("{stem}.canm"), super::gltf::to_camera(bytes, change.source.parent())?, false)]);
    }
    // Clip.anim.json in a game archive: the clip's frame events, Clip.events beside it (animation.md section 2a).
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
    // Name.anim.glb in a game archive: clips of the archive's model (animation.md section 2).
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
    // Name.bdl.model.json (or .bmd, or plain Name.model.json) in a game archive holding {"hide": true}: the game's model
    // kept with all its joints and no meshes, so nothing of it is drawn (`lumalink add character`
    // writes these for a character's own hands and face).
    if lower.ends_with(".model.json") {
        use super::refuse::refuse;
        let stem = &change.path[..change.path.len() - ".model.json".len()];
        let value: serde_json::Value = serde_json::from_slice(bytes).map_err(|_| refuse("Cook.Model.Hide", &[("file", change.path.clone())]))?;
        let hide = value.as_object().filter(|o| o.len() == 1).and_then(|o| o.get("hide")).and_then(serde_json::Value::as_bool) == Some(true);
        if !hide {
            return Err(refuse("Cook.Model.Hide", &[("file", change.path.clone())]));
        }
        let base = base.ok_or_else(|| refuse("Cook.Model.HideNoGame", &[("file", change.path.clone())]))?;
        let as_model = Change { path: format!("{stem}.glb"), source: change.source.clone(), merge: change.merge };
        let (native_path, game) = model_place(&as_model, Some(base))?;
        let game = game.ok_or_else(|| refuse("Cook.Model.HideNoGame", &[("file", change.path.clone())]))?;
        let block = store::read_file(base, &native_path).map_err(|_| refuse("Cook.Model.HideNoGame", &[("file", change.path.clone())]))?;
        let skeleton = super::gltf::skeleton_gltf(&game, &block).ok_or_else(|| refuse("Cook.Model.HideNoGame", &[("file", change.path.clone())]))?;
        let file = stem.rsplit('/').next().unwrap_or(stem);
        let name = file.split('.').next().unwrap_or(file);
        let (model, made) = super::gltf::hidden_files_for(&skeleton, name, &game)?;
        super::refuse::record(super::refuse::CookFact::Model { path: native_path.clone(), source: change.path.clone(), facts: made });
        return Ok(vec![(native_path, model, true)]);
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
    // the game's (screens.md section 1). Its pictures' textures are the archive's timg/<texture>.png (or .tpl).
    if lower.ends_with(".layout.json") {
        let (folder, file) = change.path.rsplit_once('/').unwrap_or(("", &change.path));
        let in_blyt = folder.to_ascii_lowercase().ends_with(".arc/blyt");
        let archive = if in_blyt { &folder[..folder.len() - "/blyt".len()] } else { folder };
        if !archive.to_ascii_lowercase().ends_with(".arc") {
            return Err(super::refuse::refuse("Cook.Layout.NotInArchive", &[("file", change.path.clone())]));
        }
        let stem = &file[..file.len() - ".layout.json".len()];
        // The archive's folder, when the file is read from one (a mod folder, a .luma read in place, or the game's
        // unpacked mods).
        let on_disk = change.source.parent().map(|p| if in_blyt { p.parent().unwrap_or(p) } else { p }).filter(|p| super::beside::is_dir(p));
        let missing_texture = |name: &str| -> Option<String> {
            let timg = on_disk?.join("timg");
            let wanted = [format!("{name}.png"), format!("{name}.tpl")];
            let found = super::beside::list(&timg).ok().is_some_and(|entries| {
                entries.iter().filter_map(|e| e.file_name()).any(|name| wanted.iter().any(|w| name.to_string_lossy().eq_ignore_ascii_case(w)))
            });
            (!found).then(|| format!("{archive}/timg/{name}.png"))
        };
        // A text naming this mod's own font (Luma/<id>/<path>): the mod's folder, when the file is read from one, has its
        // source in a layer's assets (screens.md section 2a). Another mod's font is that mod's to have.
        let mod_root = change.source.ancestors().skip(1).find(|a| super::beside::is_file(&a.join("luma.json")));
        let mod_id = mod_root
            .and_then(|root| super::beside::read(&root.join("luma.json")).ok())
            .and_then(|b| serde_json::from_slice::<serde_json::Value>(&b).ok())
            .and_then(|v| v["id"].as_str().map(str::to_string));
        let missing_font = |font: &str| -> Option<String> {
            let (root, id) = (mod_root?, mod_id.as_deref()?);
            let (owner, path) = font.strip_prefix("Luma/")?.split_once('/')?;
            if !owner.eq_ignore_ascii_case(id) {
                return None;
            }
            let file = format!("{path}.font.json");
            let found = super::beside::list(&root.join("layers")).ok()?.iter().any(|layer| super::beside::is_file(&layer.join("assets").join(&file)));
            (!found).then(|| format!("assets/{file}"))
        };
        let (brlyt, animations, facts) = super::layout_json::to_files(bytes, stem, &missing_texture, &missing_font)?;
        let path = format!("{archive}/blyt/{stem}.brlyt");
        super::refuse::record(super::refuse::CookFact::Layout { path: path.clone(), source: change.path.clone(), facts });
        // Its animations beside it in the archive's anim/, as the game's (layer 3).
        let mut out = vec![(path, brlyt, false)];
        out.extend(animations.into_iter().map(|(file, bytes)| (format!("{archive}/{file}"), bytes, false)));
        return Ok(out);
    }
    // Name.sound.json in a mod's assets (Luma/<id>/...): a sound effect or voice line, Name.nsfx, played by its path
    // without the ending (sound.md sections 1, 9); a stream, Name.ast (8); a sequence, Name.nseq (10). With `replace` a
    // stream or sequence also takes the game's file's place. The sound file it names is read beside it.
    if lower.ends_with(".sound.json") {
        if !lower.starts_with("luma/") {
            return Err(super::refuse::refuse("Cook.Sound.NotAsset", &[("file", change.path.clone())]));
        }
        let folder = change.source.parent().map(Path::to_path_buf);
        let file = |name: &str| -> Option<Vec<u8>> { super::beside::read(&folder.as_ref()?.join(name)).ok() };
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
        let file = |name: &str| -> Option<Vec<u8>> { super::beside::read(&folder.as_ref()?.join(name)).ok() };
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
    // its kerning (.kern) and fallback (.fallback) beside it (screens.md section 2a). The font file it names is read
    // beside it.
    if super::font_cook::is_font_source(&lower) {
        if !lower.starts_with("luma/") {
            return Err(super::refuse::refuse("Cook.Font.NotAsset", &[("file", change.path.clone())]));
        }
        let folder = change.source.parent().map(Path::to_path_buf);
        let file = |name: &str| -> Option<Vec<u8>> { super::beside::read(&folder.as_ref()?.join(name)).ok() };
        let (files, facts) = super::font_cook::cook(bytes, &change.path, &file)?;
        super::refuse::record(super::refuse::CookFact::Font { path: files[0].0.clone(), source: change.path.clone(), facts });
        return Ok(files);
    }
    // post/<name>.json in a mod's assets: a screen pass, Luma/<id>/post/<name>.post, with the HLSL beside it
    // (contracts/draw.md 15).
    if super::post_shader::is_post_source(&change.path) {
        let stem = &change.path[..change.path.len() - ".json".len()];
        let name = stem.rsplit('/').next().unwrap_or(stem);
        // Named as the mod has them: assets/post/<name>.json and .hlsl.
        let (json_file, hlsl_file) = (format!("assets/post/{name}.json"), format!("assets/post/{name}.hlsl"));
        let hlsl = change.source.parent().and_then(|folder| super::beside::read(&folder.join(format!("{name}.hlsl"))).ok());
        let made = super::post_shader::cook(&json_file, bytes, &hlsl_file, hlsl.as_deref())?;
        return Ok(vec![(format!("{stem}.post"), made, true)]);
    }
    // A table as JSON: X.bcsv.json, a camera table X.bcam.json, or a stage's placement table (its name has no ending:
    // StageData/MyGalaxy.arc/jmp/placement/common/objinfo.json), as `lumalink extract` writes them.
    if lower.ends_with(".bcsv.json") || lower.ends_with(".bcam.json") || (lower.ends_with(".json") && lower.contains(".arc/jmp/")) {
        // A stage's placement rows may give their objects' arguments by name (stages.md section 1).
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
/// `mod_waves`: the wave layout of the sound archive the mod brings (mod_wave_layout of its `AudioRes/SMR.szs`), which
/// lays out its wave archives in place of the game's, as write_overlay does.
pub fn cook_source(path: &str, source: &Path, bytes: &[u8], base: Option<&Path>, mod_waves: Option<&BTreeMap<String, Vec<audio_data::WavePlace>>>) -> Result<usize> {
    // Its caller takes what the cook notes (a mod's check).
    let _reading = super::refuse::reading();
    let change = Change { path: path.trim_start_matches('/').to_string(), source: source.to_path_buf(), merge: false };
    // The game's wave layout lays out a wave archive only (cook_change): read for one, so a mod of many other files
    // is not one read and parse of the layout for each of them.
    let game_waves;
    let waves = match mod_waves {
        Some(waves) => waves,
        None => {
            game_waves = if reads_wave_layout(&change.path) {
                base.and_then(|base| store::read_file(base, WAVE_LAYOUT).ok()).and_then(|b| audio_data::parse_layout_text(&String::from_utf8_lossy(&b)).ok()).unwrap_or_default()
            } else {
                BTreeMap::new()
            };
            &game_waves
        }
    };
    match cook_one(&change, bytes.to_vec(), waves, base, &mut Stats::default()) {
        Ok(made) => Ok(made.len()),
        // Every reason the cook gives is said as a finding (L804): its own refusal, or its words for any other fault.
        Err(error) if super::refuse::refusal_of(&error).is_some() => Err(error),
        Err(error) => Err(super::refuse::refuse("Cook.Failed", &[("detail", format!("{error:#}"))])),
    }
}

/// Whether cooking the file at `path` reads the game's wave layout: a wave archive (`.aw`), as cook_change lays it out.
fn reads_wave_layout(path: &str) -> bool {
    path.to_ascii_lowercase().ends_with(".aw")
}

/// Whether `path` (a game path) is the sound archive whose wave systems lay out the wave archives (`AudioRes/SMR.szs`).
pub fn is_sound_archive(path: &str) -> bool {
    path.trim_start_matches('/').eq_ignore_ascii_case("audiores/smr.szs")
}

/// The wave layout of a sound archive a mod brings (`AudioRes/SMR.szs`), for cook_source: refused (L804) when its wave
/// systems do not read or it has none (then no wave archive of the mod could be laid out).
pub fn mod_wave_layout(bytes: &[u8]) -> Result<BTreeMap<String, Vec<audio_data::WavePlace>>> {
    let refused = |detail: String| super::refuse::refuse("Cook.Failed", &[("detail", format!("the mod's sound archive: {detail}"))]);
    let waves = audio_data::wave_layout(bytes).map_err(|error| refused(format!("{error:#}")))?;
    if waves.is_empty() {
        return Err(refused("it has no wave systems".to_string()));
    }
    Ok(waves)
}

/// A mod's merge/ files (`merge: true`) applied as the game's start applies them (run_merges, the one function),
/// against the game's store `base` and the mod's own files at game paths (the rest of `changes`, cooked as the start
/// cooks them): what each merge came to, in the start's order.
pub fn check_merges<'c>(changes: &'c [Change], base: &Path) -> Vec<(&'c Change, MergeOutcome)> {
    // Its own takes read what the cooks note (the check says the outcomes, not the notes).
    let _reading = super::refuse::reading();
    let own = |path: &str| -> Option<Vec<u8>> {
        let change = changes.iter().rev().filter(|c| !c.merge).find(|c| {
            let at = c.path.trim_start_matches('/');
            store::same_path(at, path) || at.to_ascii_lowercase().strip_suffix(".json").is_some_and(|stem| store::same_path(stem, path))
        })?;
        let bytes = fs::read(&change.source).ok()?;
        let made = cook_one(change, bytes, &BTreeMap::new(), Some(base), &mut Stats::default()).ok()?;
        super::refuse::take_notes();
        super::refuse::take_facts();
        made.into_iter().find(|(at, _)| store::same_path(at, path)).and_then(|(_, items)| items.into_iter().find_map(|i| i.bytes))
    };
    let has = |file: &str| own(file).is_some() || store::read_file(base, file).is_ok();
    let mut start = |path: &str| match own(path) {
        Some(bytes) => Ok(bytes),
        None => game_file(path, Some(base)),
    };
    let (mut merged, mut cased) = (BTreeMap::new(), BTreeMap::new());
    let outcomes = run_merges(changes, &mut start, &has, &mut merged, &mut cased, &mut Stats::default());
    super::refuse::take_facts();
    outcomes
}

/// One file of a mod as the overlay cooks it, with the game files it makes: none for a file another file reads (a
/// sound, font or movie in assets/, a screen pass's HLSL), a modern source's files, else the file itself cooked by
/// what it is. The overlay and `cook_source` (the check before a pack) run this one function.
fn cook_one(change: &Change, bytes: Vec<u8>, waves: &BTreeMap<String, Vec<audio_data::WavePlace>>, base: Option<&Path>, stats: &mut Stats) -> Result<Vec<(String, Vec<StoreItem>)>> {
    // A member of a mod's archive the store leaves out (a file of the tools that made the game) is said, never only
    // counted: no file of a mod is dropped without a word.
    let before = stats.left_out.len();
    let cooked = cook_one_files(change, bytes, waves, base, stats);
    for left in &stats.left_out[before..] {
        super::refuse::note("Cook.ToolFile", &[("file", left.path.clone())]);
    }
    cooked
}

fn cook_one_files(change: &Change, bytes: Vec<u8>, waves: &BTreeMap<String, Vec<audio_data::WavePlace>>, base: Option<&Path>, stats: &mut Stats) -> Result<Vec<(String, Vec<StoreItem>)>> {
    // A sound or font file in a mod's assets is what a .sound.json or .font.json reads; not a file of the game.
    if super::sound_effect::is_asset_source(&change.path)
        || super::font_cook::is_asset_source(&change.path)
        || super::movie_cook::is_asset_source(&change.path)
        || super::post_shader::is_asset_source(&change.path)
    {
        return Ok(Vec::new());
    }
    let modern = modern_files(change, &bytes, base)?;
    if modern.is_empty() {
        return Ok(vec![(change.path.clone(), cook_change(change, bytes, waves, stats)?)]);
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
}

/// One file of a mod as store items, cooked as the game's own are: by its name (`cooks`) or, whatever its name, by
/// what its bytes are (cook_file), as a member of an archive is at import. A mod's file can be one member
/// (`X.arc/jmp/placement/common/objinfo`, `X.arc/x.kcl`, `X.arc/blyt/x.brlyt`), so the name alone does not decide. A
/// file only a console reads is refused. A wave archive needs the sound archive that lays it out.
fn cook_change(change: &Change, bytes: Vec<u8>, waves: &BTreeMap<String, Vec<audio_data::WavePlace>>, stats: &mut Stats) -> Result<Vec<StoreItem>> {
    use super::refuse::refuse;
    let file = || [("file", change.path.clone())];
    if super::console_only(change.path.trim_start_matches('/')) {
        return Err(refuse("Cook.ConsoleOnly", &file()));
    }
    // A mod's file named as an archive that isn't one would fail only where the game opens it.
    if change.path.to_ascii_lowercase().ends_with(".arc") {
        let expanded = if archive::is_compressed(&bytes) { archive::expand(&bytes).map_err(|_| refuse("Cook.NotArchive", &file()))? } else { bytes.clone() };
        if !archive::is_archive(&expanded) {
            return Err(refuse("Cook.NotArchive", &file()));
        }
    }
    if cooks(&change.path) || archive::is_compressed(&bytes) || super::sound::is_sequence_path(&change.path) {
        return cook_entry(&change.path, bytes, stats);
    }
    if bytes.is_empty() {
        return Err(refuse("Cook.EmptyFile", &file()));
    }
    if !reads_wave_layout(&change.path) {
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
    let _cook = super::COOK_LOCK.lock().unwrap_or_else(|e| e.into_inner());
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
    for change in changes.iter().filter(|c| is_sound_archive(&c.path)) {
        let bytes = fs::read(&change.source).with_context(|| format!("{} cannot be read", change.source.display()))?;
        waves = audio_data::wave_layout(&bytes).context("the mod's sound archive")?;
    }
    let mut placed = Vec::new();
    let mut failed = Vec::new();
    // The packs' tables that class their models, before the models cook.
    let mut tables = super::hit_record::PackTables::default();
    for change in changes.iter().filter(|c| c.merge) {
        let Some(kind) = RT_CLASSING.iter().position(|p| *p == change.path.to_ascii_lowercase()) else { continue };
        let text = match fs::read_to_string(&change.source) {
            Ok(text) => text,
            Err(error) => {
                failed.push((format!("merge/{}", change.path), format!("{} cannot be read: {error}", change.source.display())));
                continue;
            }
        };
        // A table with a fault is left out; apply_merges says why (run_merges reads it the same way).
        if super::hit_record::pack_table_problem(kind, &text).is_some() {
            continue;
        }
        match kind {
            0 => tables.materials.push(text),
            1 => tables.glow.push(text),
            _ => tables.water.push(text),
        }
    }
    let _tables = super::hit_record::PackTablesInUse::set(tables);
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
                cook_one(change, bytes, &waves, base, stats)
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
    game_file(path, base)
}

/// The game's file a merge adds to; refused by name when the game has none at that path.
fn game_file(path: &str, base: Option<&Path>) -> Result<Vec<u8>> {
    let base = base.context("the mod adds to a file of the game, and the game isn't known")?;
    store::read_file(base, path).map_err(|_| super::refuse::refuse("Cook.Merge.NoBase", &[("file", path.to_string())]))
}

/// Every language folder a game of the series has messages in.
const MESSAGE_LANGUAGES: [&str; 14] = [
    "UsEnglish", "UsFrench", "UsSpanish", "EuEnglish", "EuFrench", "EuGerman", "EuItalian", "EuSpanish", "EuDutch", "JpJapanese",
    "KrKorean", "CnSimpChinese", "TwTradChinese", "UsPortuguese",
];

/// The languages a messages merge is for: `<Language>/MessageData/Message.arc/message.bmg.json` one, and
/// `MessageData/messages.json` each the game has. None: the path is no messages merge.
fn message_languages(path: &str, has_file: &dyn Fn(&str) -> bool) -> Result<Option<Vec<String>>> {
    let lower = path.trim_start_matches('/').to_ascii_lowercase();
    if lower == "messagedata/messages.json" {
        let has = |language: &str| has_file(&format!("{language}/MessageData/Message.arc/message.bmg"));
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

/// What one merge/ file came to: nothing of it applied (refused, with why), or applied with what was said of it.
pub enum MergeOutcome {
    Refused(Finding),
    Applied(Vec<Finding>),
}

/// An error of a cook as the finding it is: its own refusal, or `Cook.Failed` with its words (as cook_source says it).
fn finding_of(error: &anyhow::Error) -> Finding {
    match super::refuse::refusal_of(error) {
        Some(finding) => finding.clone(),
        None => super::refuse::refusal_of(&super::refuse::refuse("Cook.Failed", &[("detail", format!("{error:#}"))])).cloned().expect("a refusal"),
    }
}

/// The merges of `changes` (every mod's effects left out: apply_effects), each in its mod's order, a mod's names before
/// its particles, which are numbered from them: what each came to. `start` gives a file as it stands before any merge
/// (a mod's file of it in this store, else the game's), `has` whether the game or a mod has a file (which languages a
/// messages merge is for). The game's start (apply_merges) and the check before a pack (check_merges) run this one
/// function. `merged` holds each file merged so far by its place in the store (StageData/X.arc/y and StageData/X/y, in
/// any case, are one): a merge starts from it and replaces it only when the merge works, so one that fails loses no
/// other's. `cased` keeps the path a merge of messages or particles wrote first.
fn run_merges<'c>(changes: &'c [Change], start: &mut dyn FnMut(&str) -> Result<Vec<u8>>, has: &dyn Fn(&str) -> bool,
                  merged: &mut BTreeMap<String, Vec<u8>>, cased: &mut BTreeMap<String, String>, stats: &mut Stats) -> Vec<(&'c Change, MergeOutcome)> {
    let mut merges: Vec<&Change> = changes.iter().filter(|c| c.merge && super::effects::source_of(&c.path).is_none()).collect();
    merges.sort_by_key(|c| {
        let path = c.path.to_ascii_lowercase();
        path.ends_with(".jpc") || path.ends_with(".jpc.json")
    });
    let mut first_particle: BTreeMap<PathBuf, u16> = BTreeMap::new();
    let lower = |text: &str| text.to_ascii_lowercase();
    let place_of = |text: &str| store::store_path(text.trim_start_matches('/')).to_ascii_lowercase();
    let mut outcomes = Vec::new();
    for change in merges {
        super::refuse::take_notes();
        let result = (|| -> Result<()> {
            // What merge/ may hold is the validator's one list (L309): the branches below take each kind it allows.
            let path = lower(&change.path);
            if !luma_validate::mergeable(&path) {
                bail!("only tables (.json; ray tracing's NebulaData/Rt*.tsv), messages (message.bmg.json, messages.json) and particles (.jpc, .jpc.json) can be added to");
            }
            // A pack's tables that class its models: read before they cooked (write_overlay), no file of the store. A
            // table write_overlay leaves out for its fault is said here.
            if let Some(kind) = RT_CLASSING.iter().position(|p| *p == path) {
                let text = fs::read_to_string(&change.source).with_context(|| format!("{} cannot be read", change.source.display()))?;
                if let Some(problem) = super::hit_record::pack_table_problem(kind, &text) {
                    bail!("{problem}");
                }
                return Ok(());
            }
            let bytes = fs::read(&change.source).with_context(|| format!("{} cannot be read", change.source.display()))?;
            if let Some(languages) = message_languages(&change.path, has)? {
                // Every language or none: a merge that fails in one is left out of all.
                let mut done = Vec::new();
                for language in languages {
                    let folder = format!("{language}/MessageData/Message.arc");
                    let (file_path, ids_path) = (format!("{folder}/message.bmg"), format!("{folder}/messageid.tbl"));
                    let mut current = |path: &str| match merged.get(&place_of(path)) {
                        Some(bytes) => Ok(bytes.clone()),
                        None => start(path),
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
                // Particles as JSON are a layer's assets/effects.json (effects.md section 7).
                return Err(super::refuse::refuse("Cook.Effect.OldForm", &[]));
            } else if let Some(table) = lower(&change.path).strip_suffix(".json") {
                let path = change.path[..table.len()].to_string();
                let table_key = place_of(&path);
                let current = match merged.get(&table_key) {
                    Some(current) => current.clone(),
                    None => start(&path)?,
                };
                let mut rows = super::table::read_rows(&bytes)?;
                // A stage's placement rows may give their objects' arguments by name (stages.md section 1).
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
                // Applied all the same: a remove or change matching no row is said, not refused.
                for (why, row) in unmatched {
                    super::refuse::note(why, &[("row", row)]);
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
                    None => start(&change.path)?,
                };
                let cooked = super::cook_file(&change.path, bytes, stats)?.context("not a particle file")?;
                merged.insert(key, super::merge::add_particles(&current, &cooked, first)?);
            } else if RT_ROWS.contains(&lower(&change.path).as_str()) {
                // Ray tracing's rows the host reads (RtStages.tsv, RtEffects.tsv): after the game's and earlier mods'.
                let key = place_of(&change.path);
                let current = match merged.get(&key) {
                    Some(current) => current.clone(),
                    None => start(&change.path)?,
                };
                merged.insert(key, super::merge::add_rows(&current, &bytes)?);
            } else {
                bail!("only tables (.json; ray tracing's NebulaData/Rt*.tsv), messages (message.bmg.json, messages.json) and particles (.jpc, .jpc.json) can be added to");
            }
            Ok(())
        })();
        let notes = super::refuse::take_notes();
        outcomes.push((change, match result {
            Ok(()) => MergeOutcome::Applied(notes),
            Err(error) => MergeOutcome::Refused(finding_of(&error)),
        }));
    }
    outcomes
}

/// Each merge as the game's start applies it (run_merges): what each came to goes in the cook's facts and, for the
/// log, in `failed`.
#[allow(clippy::too_many_arguments)]
fn apply_merges(changes: &[Change], base: Option<&Path>, pak: &mut File, temporary: &Path, at: &mut u64, placed: &mut Vec<store::Placed>,
                failed: &mut Vec<(String, String)>, stats: &mut Stats) -> Result<()> {
    let mut merged: BTreeMap<String, Vec<u8>> = BTreeMap::new();
    let mut cased: BTreeMap<String, String> = BTreeMap::new();
    {
        let shown: &[store::Placed] = placed;
        let has = |file: &str| shown.iter().any(|p| store::same_path(&p.path, file)) || base.is_some_and(|base| store::read_file(base, file).is_ok());
        let mut start = |path: &str| merge_start(path, base, temporary, pak, shown);
        for (change, outcome) in run_merges(changes, &mut start, &has, &mut merged, &mut cased, stats) {
            let (path, source) = (format!("+{}", change.path), change.source.display().to_string());
            match outcome {
                MergeOutcome::Refused(finding) => {
                    failed.push((format!("merge/{}", change.path), luma_validate::reason(&finding, "UsEnglish")));
                    super::refuse::record(super::refuse::CookFact::Refused { path, source, finding });
                }
                MergeOutcome::Applied(notes) => {
                    for finding in notes {
                        failed.push((format!("merge/{}", change.path), luma_validate::reason(&finding, "UsEnglish")));
                        super::refuse::record(super::refuse::CookFact::Noted { path: path.clone(), source: source.clone(), finding });
                    }
                }
            }
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

/// The patches with every patch of the effect table left out that names a particle nobody has (effects.md section 7):
/// the game's names and the mods' (after their effects) are known by now. A left-out patch is refused by its
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
            // A mod that only replaces has no list (the run time's reader: no list, no effects of its own).
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
    // Each file's notes and the facts are taken on this thread, the last when the facts file is written.
    let _reading = super::refuse::reading();
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

/// The plan's texture pack pictures (`%texture<TAB>mod<TAB>entry<TAB>file`), each read as the pack reader reads it
/// (pack_texture::check, the one `lumalink check` asks): one the reader will not take is a failed fact on its mod and
/// entry. Nothing is cooked; the pack reader reads the file itself.
fn texture_facts(text: &str, facts: &mut Vec<Fact>) -> Result<()> {
    for line in text.lines().filter_map(|line| line.strip_prefix("%texture\t")) {
        let mut parts = line.splitn(3, '\t');
        let (Some(mod_id), Some(entry), Some(file)) = (parts.next(), parts.next(), parts.next()) else { bail!("the plan's line {line:?}") };
        let checked = fs::read(file).map_err(|_| super::refuse::refuse("Cook.Image.Unreadable", &[("image", entry.to_string())]));
        let Err(error) = checked.and_then(|bytes| super::pack_texture::check(entry, &bytes)) else { continue };
        let Some(finding) = super::refuse::refusal_of(&error) else { return Err(error) };
        let mut refusal = finding.clone();
        refusal.mod_id = Some(mod_id.to_string());
        refusal.file = entry.to_string();
        let why = luma_validate::reason(&refusal, "UsEnglish");
        facts.push(Fact::Failed { table: entry.to_string(), mod_id: mod_id.to_string(), file: entry.to_string(), refusal, why });
    }
    Ok(())
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
    texture_facts(&text, facts)?;
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
    // flags.tsv: each text's program_table flags and coordinates (programs::text_flags), which the game's table has no row for.
    let mut flags = String::new();
    for (name, text) in &texts {
        fs::write(sources.join(name), text)?;
        let pixel = name.starts_with('p');
        let coords = if pixel { 0 } else { super::programs::text_coords(text) };
        flags.push_str(&format!("{}\t{}\t{coords}\n", &name[..name.len() - ".hlsl".len()], super::programs::text_flags(pixel, text)));
    }
    fs::write(sources.join("flags.tsv"), flags)?;
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
    fn a_collision_goes_into_the_archive_its_model_cooks_into() {
        // In an archive folder: beside the source, as before.
        assert_eq!(collision_stem("ObjectData/Kuribo.arc/Ground.kcl.glb"), "ObjectData/Kuribo.arc/Ground");
        assert_eq!(collision_stem("ObjectData/Kuribo.ARC/Ground.KCL.gltf"), "ObjectData/Kuribo.ARC/Ground");
        // A mod's assets/ (Luma/<id>/): the archive of the model of the same name, where the actor reads it. A loose
        // Luma/<id>/Planet.kcl was in no archive, so an actor of Planet.arc found no collision.
        let collision = collision_stem("Luma/my.mod/NebulaPlanet.kcl.glb");
        assert_eq!(collision, "Luma/my.mod/NebulaPlanet.arc/NebulaPlanet");
        let (model, _) = model_place(&Change { path: "Luma/my.mod/NebulaPlanet.glb".into(), source: PathBuf::new(), merge: false }, None).unwrap();
        assert_eq!(model.rsplit_once('/').unwrap().0, collision.rsplit_once('/').unwrap().0);
        assert_eq!(collision_stem("Luma/my.mod/stages/Rock.kcl.glb"), "Luma/my.mod/stages/Rock.arc/Rock");
        assert_eq!(collision_stem("Ground.kcl.glb"), "Ground.arc/Ground");
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
            "ObjectData/MarioHandL.arc/MarioHandL.bdl.model.json",
        ];
        let dir = tempfile::tempdir().unwrap();
        for path in cooked {
            assert!(luma_validate::cooked_source(&path.to_lowercase()), "{path}");
            match cook_source(path, &dir.path().join("x"), b"not this", None, None) {
                Ok(made) => assert!(made > 0, "{path}: made nothing"),
                Err(error) => assert!(super::super::refuse::refusal_of(&error).is_some() || !format!("{error:#}").is_empty(), "{path}"),
            }
        }
        // Source kinds the cook does not make are the validator's (L304, each named with where it goes); the cook places
        // such a file as it is, as the overlay does.
        for path in ["Fonts/Title.ttf", "MovieData/Intro.webm", "ObjectData/Kuribo.arc/Notes.json"] {
            assert!(!luma_validate::cooked_source(&path.to_lowercase()), "{path}");
            assert_eq!(cook_source(path, &dir.path().join("x"), b"not this", None, None).unwrap(), 1, "{path}");
        }
    }

    #[test]
    fn only_a_wave_archive_reads_the_game_s_wave_layout() {
        // A mod's check cooks each file alone (cook_source): the layout is read and parsed for a wave archive only,
        // so a mod of thousands of other files (found by fuzzing the container: 20000 empty files) is not that many
        // reads of the store.
        for path in ["AudioRes/Waves/Wave_0.aw", "AudioRes/Waves/WAVE_1.AW"] {
            assert!(reads_wave_layout(path), "{path}");
        }
        for path in ["layers/base/files/f0.bin", "AudioRes/Waves/Wave_0.aw.json", "ObjectData/Kuribo.arc", "AudioRes/aw"] {
            assert!(!reads_wave_layout(path), "{path}");
        }
        // A wave archive still needs the layout: without a store whose layout reads, it is refused for that.
        let dir = tempfile::tempdir().unwrap();
        let store = dir.path().join("game.pak");
        std::fs::write(&store, b"not a store").unwrap();
        let error = cook_source("AudioRes/Waves/Wave_0.aw", &dir.path().join("x"), b"wave bytes", Some(&store), None).unwrap_err();
        let (phrase, args) = super::super::refuse::said(&error);
        assert_eq!(phrase, "Cook.Failed");
        assert!(args.iter().any(|(name, value)| *name == "detail" && value.contains("wave layout is not known")), "{args:?}");
        // Any other file is cooked the same with or without the store's layout.
        let said = |base: Option<&Path>| super::super::refuse::said(&cook_source("Luma/m/Notes.bin", &dir.path().join("x"), b"", base, None).unwrap_err()).0;
        assert_eq!(said(Some(&store)), said(None));
    }

    #[test]
    fn a_mod_s_own_sound_archive_lays_out_its_wave_archives() {
        // A mod that brings AudioRes/SMR.szs lays its wave archives out by that archive's wave systems, not the
        // game's (the corpus's character mods carry their own sound archive and wave archives shorter than the disc's).
        let dir = tempfile::tempdir().unwrap();
        let place = audio_data::WavePlace { offset: 2, length: 2, format: 3, samples: 1, new_offset: 0, new_length: 2 };
        let waves: BTreeMap<String, Vec<audio_data::WavePlace>> =
            [(audio_data::wave_archive_key("AudioRes/Waves/Wave_0.aw"), vec![place])].into_iter().collect();
        assert_eq!(cook_source("AudioRes/Waves/Wave_0.aw", &dir.path().join("x"), &[1, 2, 3, 4], None, Some(&waves)).unwrap(), 1);
        // A wave past the end of the mod's own archive is still refused, by its place.
        let error = cook_source("AudioRes/Waves/Wave_0.aw", &dir.path().join("x"), &[1, 2], None, Some(&waves)).unwrap_err();
        assert!(format!("{error:#}").contains("past the file's end"), "{error:#}");
        // A sound archive whose wave systems do not read is refused as the mod's.
        let error = mod_wave_layout(b"not a sound archive").unwrap_err();
        let (phrase, args) = super::super::refuse::said(&error);
        assert_eq!(phrase, "Cook.Failed");
        assert!(args.iter().any(|(_, value)| value.contains("the mod's sound archive")), "{args:?}");
        assert!(is_sound_archive("/AudioRes/SMR.szs") && is_sound_archive("audiores/smr.SZS") && !is_sound_archive("AudioRes/SMR.szs.json"));
    }

    #[test]
    fn the_check_cooks_each_file_as_the_overlay_does_and_names_every_fault() {
        use super::super::refuse::said;
        let dir = tempfile::tempdir().unwrap();
        let at = dir.path().join("x");
        // A model in a mod's assets is cooked like one at a game path: an empty one is refused by the model cook.
        let error = cook_source("Luma/m/Ball.glb", &at, b"", None, None).unwrap_err();
        assert!(said(&error).0.split('.').take(2).eq(["Cook", "Model"]), "{:?}", said(&error));
        // A file another file reads (a sound, font or movie in assets, a screen pass's HLSL) makes nothing by itself.
        for path in ["Luma/m/Song.wav", "Luma/m/Title.ttf", "Luma/m/Intro.webm"] {
            assert_eq!(cook_source(path, &at, b"", None, None).unwrap(), 0, "{path}");
        }
        // The overlay's own refusals of a file by what it is, each a finding.
        assert_eq!(said(&cook_source("Luma/m/Notes.bin", &at, b"", None, None).unwrap_err()).0, "Cook.EmptyFile");
        assert_eq!(said(&cook_source("ObjectData/Fake.arc", &at, b"not an archive", None, None).unwrap_err()).0, "Cook.NotArchive");
        assert_eq!(said(&cook_source("opening.bnr", &at, b"banner", None, None).unwrap_err()).0, "Cook.ConsoleOnly");
        // A member of a mod's archive the store leaves out is said by name, not only counted.
        super::super::refuse::take_notes();
        let mut arc = super::super::archive::tests::small_archive();
        let name = arc.windows(6).position(|w| w == b"a.bin\0").unwrap();
        arc[name..name + 5].copy_from_slice(b"a.ini");
        assert_eq!(cook_source("ObjectData/Thing.arc", &at, &arc, None, None).unwrap(), 1);
        let notes = super::super::refuse::said_notes();
        assert_eq!(notes, vec![("Cook.ToolFile", vec![("file", "ObjectData/Thing.arc/a.ini".to_string())])]);
    }

    #[test]
    fn a_layout_is_the_archives_layout_file_and_its_textures_are_the_mods() {
        // The test takes what the cook notes (refuse::reading).
        let _reading = crate::cook::refuse::reading();
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
        assert_eq!(cook_source("LayoutData/Hud.arc/Hud.layout.json", Path::new("layers/base/files/x"), source, None, None).unwrap(), 1);
        let error = cook_source("LayoutData/Hud.layout.json", &archive, source, None, None).unwrap_err();
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
        let error = cook_source(path, &source, text("Luma/my.mod/fonts/Title").as_bytes(), None, None).unwrap_err();
        let (why, args) = said(&error);
        assert_eq!(why, "Cook.Layout.NoFont");
        assert!(args.contains(&("uri", "assets/fonts/Title.font.json".into())), "{args:?}");
        // In any layer's assets; another mod's font is not looked for.
        std::fs::create_dir_all(root.join("layers/extra/assets/fonts")).unwrap();
        std::fs::write(root.join("layers/extra/assets/fonts/Title.font.json"), b"{}").unwrap();
        assert_eq!(cook_source(path, &source, text("Luma/my.mod/fonts/Title").as_bytes(), None, None).unwrap(), 1);
        assert_eq!(cook_source(path, &source, text("Luma/other.mod/fonts/Title").as_bytes(), None, None).unwrap(), 1);
    }

    #[test]
    fn a_mods_font_is_cooked_as_the_games_fonts_with_its_kerning_and_fallback_beside_it() {
        // The test takes what the cook notes (refuse::reading).
        let _reading = crate::cook::refuse::reading();
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
        // The test takes what the cook notes (refuse::reading).
        let _reading = crate::cook::refuse::reading();
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
        // The test takes what the cook notes (refuse::reading).
        let _reading = crate::cook::refuse::reading();
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
        assert_eq!(cook_source("ObjectData/Kuribo.arc/Kuribo.anim.gltf", &source, anim.as_bytes(), Some(&one), None).unwrap(), 1);
        // Two models, neither named as the archive: which one is not guessed.
        let two_dir = tempfile::tempdir().unwrap();
        let two = game_store(two_dir.path(), &[("ObjectData/Kuribo.arc/a.bdl", &block), ("ObjectData/Kuribo.arc/b.bdl", &block)]);
        let error = cook_source("ObjectData/Kuribo.arc/Kuribo.anim.gltf", &source, anim.as_bytes(), Some(&two), None).unwrap_err();
        assert_eq!(refusal_of(&error).map(|f| f.args.iter().any(|(n, v)| *n == "why" && *v == luma_validate::Arg::Phrase("Cook.Anim.WhichModel"))), Some(true), "{error:#}");
        // Not in a game archive, and no game to read the model from.
        for (path, base, why) in [
            ("ObjectData/Walk.anim.gltf", Some(one.as_path()), "Cook.Anim.NotInArchive"),
            ("ObjectData/Kuribo.arc/Kuribo.anim.gltf", None, "Cook.Anim.NoGame"),
        ] {
            let error = cook_source(path, &source, anim.as_bytes(), base, None).unwrap_err();
            assert_eq!(refusal_of(&error).map(|f| f.args.iter().any(|(n, v)| *n == "why" && *v == luma_validate::Arg::Phrase(why))), Some(true), "{why}: {error:#}");
        }
    }

    #[test]
    fn a_model_takes_the_place_and_joints_of_the_games_model() {
        // The test takes what the cook notes (refuse::reading).
        let _reading = crate::cook::refuse::reading();
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
        // A level needs its base model among the mod's files (models.md section 6); the endings keep their capitals.
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
        // The test takes what the cook notes (refuse::reading).
        let _reading = crate::cook::refuse::reading();
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
        // The test takes what the cook notes (refuse::reading).
        let _reading = crate::cook::refuse::reading();
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
        // The test takes what the cook notes (refuse::reading).
        let _reading = crate::cook::refuse::reading();
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

    /// A store holding one table `ObjectData/Things/Things.bcsv` (columns name: text, speed: whole number).
    fn things_store(dir: &Path) -> PathBuf {
        let base = dir.join("game.pak");
        let table = super::super::table::TableIr::from_json(br#"{"rows":[{"name":"Game","speed":1}]}"#).unwrap().to_ntbl();
        let mut pak = File::create(&base).unwrap();
        pak.write_all(&vec![0u8; 0x100]).unwrap();
        let position = pak.stream_position().unwrap();
        pak.write_all(&table).unwrap();
        let placed = vec![store::Placed { path: "ObjectData/Things/Things.bcsv".into(), position, size: table.len() as u64, folder: false }];
        let end = pak.stream_position().unwrap();
        store::write_index(&mut pak, end, &placed, b"RMGE01\0\0").unwrap();
        base
    }

    /// The check before a pack and the game's start say the same of each merge/ file, through run_merges: a file the
    /// game does not have, a column the table lacks, a value its column cannot hold, a remove or change of another form
    /// (refused, nothing of the file applied), and a remove or change matching no row (said, the rest applied).
    #[test]
    fn the_check_says_of_each_merge_what_the_start_says() {
        // The test takes what the cook notes (refuse::reading).
        let _reading = crate::cook::refuse::reading();
        let dir = tempfile::tempdir().unwrap();
        let base = things_store(dir.path());
        let file = |name: &str, text: &str| {
            let path = dir.path().join(name);
            fs::write(&path, text).unwrap();
            path
        };
        let at = "ObjectData/Things.arc/Things.bcsv.json";
        let merge = |path: &str, name: &str, text: &str| Change { path: path.into(), source: file(name, text), merge: true };
        let changes = vec![
            merge(at, "good.json", r#"{"rows":[{"name":"A","speed":2}]}"#),
            merge("ObjectData/Nothing.arc/Nothing.bcsv.json", "nobase.json", r#"{"rows":[{"name":"A"}]}"#),
            merge(at, "column.json", r#"{"change":[{"where":{"name":"Game"},"set":{"nope":1}}]}"#),
            merge(at, "text.json", r#"{"change":[{"where":{"name":"Game"},"set":{"name":3}}]}"#),
            merge(at, "whole.json", r#"{"change":[{"where":{"name":"Game"},"set":{"speed":"fast"}}]}"#),
            merge(at, "remove.json", r#"{"remove":[3]}"#),
            merge(at, "change.json", r#"{"change":[{"set":{"speed":3}}]}"#),
            merge(at, "norow.json", r#"{"remove":[{"name":"Nobody"}],"change":[{"where":{"name":"Nobody"},"set":{"speed":4}}],"rows":[{"name":"B"}]}"#),
            merge(at, "json.json", r#"{"rows":"#),
            merge(at, "rows.json", r#"{"columns":{}}"#),
            merge(at, "row.json", r#"{"rows":[3]}"#),
        ];
        // A finding's phrase and arguments; every placeholder of its words is given, in four languages.
        let said = |finding: &Finding| -> (&'static str, Vec<(&'static str, String)>) {
            use luma_validate::Arg;
            let Arg::Phrase(why) = finding.args[0].1 else { panic!("{finding:?}") };
            for language in ["UsEnglish", "UsFrench", "JpJapanese", "KrKorean"] {
                let words = luma_validate::reason(finding, language);
                let unfilled = ["{row}", "{name}", "{file}", "{line}", "{column}", "{detail}"].iter().any(|p| words.contains(p));
                assert!(!unfilled && !words.contains("Cook."), "{why} in {language}: {words}");
            }
            let rest = finding.args[1..].iter().map(|(n, v)| (*n, match v { Arg::Text(t) => t.clone(), Arg::Phrase(p) => p.to_string() })).collect();
            (why, rest)
        };
        // The check.
        let mut checked: Vec<(String, &str, bool, Vec<(&str, String)>)> = Vec::new();
        for (change, outcome) in check_merges(&changes, &base) {
            let name = change.source.file_name().unwrap().to_string_lossy().into_owned();
            match outcome {
                MergeOutcome::Refused(f) => checked.push((name, said(&f).0, true, said(&f).1)),
                MergeOutcome::Applied(notes) => notes.iter().for_each(|f| checked.push((name.clone(), said(f).0, false, said(f).1))),
            }
        }
        let whys: Vec<(&str, &str, bool)> = checked.iter().map(|(n, w, r, _)| (n.as_str(), *w, *r)).collect();
        assert_eq!(
            whys,
            [
                ("nobase.json", "Cook.Merge.NoBase", true),
                ("column.json", "Cook.Merge.NoColumn", true),
                ("text.json", "Cook.Cell.Text", true),
                ("whole.json", "Cook.Cell.Whole", true),
                ("remove.json", "Cook.Merge.RemoveForm", true),
                ("change.json", "Cook.Merge.ChangeForm", true),
                ("norow.json", "Cook.Merge.RemoveNoRow", false),
                ("norow.json", "Cook.Merge.ChangeNoRow", false),
                ("json.json", "Cook.Table.Json", true),
                ("rows.json", "Cook.Table.NoRows", true),
                ("row.json", "Cook.Table.RowNotObject", true),
            ],
            "{checked:?}"
        );
        // The game's start: the same findings in its cook facts, on the same files.
        super::super::refuse::take_facts();
        let output = dir.path().join("mods.pak");
        write_overlay("RMGE01", &changes, &PlanPatches::default(), Some(&base), &output, &mut Vec::new(), &mut Stats::default()).unwrap();
        let mut started: Vec<(String, &str, bool, Vec<(&str, String)>)> = Vec::new();
        for fact in super::super::refuse::take_facts() {
            let (source, finding, refused) = match &fact {
                super::super::refuse::CookFact::Refused { source, finding, .. } => (source, finding, true),
                super::super::refuse::CookFact::Noted { source, finding, .. } => (source, finding, false),
                _ => continue,
            };
            let name = Path::new(source).file_name().unwrap().to_string_lossy().into_owned();
            started.push((name, said(finding).0, refused, said(finding).1));
        }
        assert_eq!(started, checked);
        // The good merge and the rows of the one matching no row are in the table: the game's row, A, B.
        let merged = store::read_file(&output, "ObjectData/Things.arc/Things.bcsv").unwrap();
        assert_eq!(super::super::table::row_count(&merged).unwrap(), 3);
    }

    /// merge/ holds what the validator's L309 allows and nothing else: each kind it allows reaches a branch of
    /// run_merges, each it refuses is refused there too.
    /// A plan's texture pack pictures are each read as the pack reader reads them: one it will not take is a failed
    /// fact on its mod and entry, said as `lumalink check` says it; one it takes, nothing.
    #[test]
    fn a_plans_pack_pictures_the_reader_refuses_are_failed_facts() {
        let dir = tempfile::tempdir().unwrap();
        let pictures = Path::new(env!("CARGO_MANIFEST_DIR")).join("data/pack-pictures");
        let entry = |name: &str| format!("layers/base/textures/{name}");
        let mut plan = String::from("RMGE01\n");
        for name in ["good.png", "cut.png", "bc6h.dds", "dxt1.dds"] {
            plan.push_str(&format!("%texture\tpack.mod\t{}\t{}\n", entry(name), pictures.join(name).display()));
        }
        plan.push_str(&format!("%texture\tpack.mod\t{}\t{}\n", entry("gone.png"), dir.path().join("gone.png").display()));
        assert!(super::super::overlay_patches::read_patches(&plan).is_ok());
        let mut facts = Vec::new();
        texture_facts(&plan, &mut facts).unwrap();
        let said: Vec<(String, String, &str)> = facts
            .iter()
            .map(|f| match f {
                Fact::Failed { mod_id, file, refusal, .. } => (mod_id.clone(), file.clone(), refusal.args.first().map_or("", |(_, a)| match a {
                    luma_validate::Arg::Phrase(p) => p,
                    _ => "",
                })),
                other => panic!("{other:?}"),
            })
            .collect();
        let pack = |name: &str, why: &'static str| ("pack.mod".to_string(), entry(name), why);
        assert_eq!(said, [pack("cut.png", "Cook.Image.Unreadable"), pack("bc6h.dds", "Cook.Texture.DdsForm"), pack("gone.png", "Cook.Image.Unreadable")]);
        // What the start says of each is what the check says of the same file.
        for (fact, name) in facts.iter().zip(["cut.png", "bc6h.dds"]) {
            let Fact::Failed { refusal, .. } = fact else { unreachable!() };
            let check = super::super::pack_texture::check(&entry(name), &fs::read(pictures.join(name)).unwrap()).unwrap_err();
            assert_eq!(refusal.args, super::super::refuse::refusal_of(&check).unwrap().args);
        }
    }

    #[test]
    fn merge_takes_exactly_what_l309_allows() {
        let dir = tempfile::tempdir().unwrap();
        let base = things_store(dir.path());
        let source = dir.path().join("x");
        fs::write(&source, b"x").unwrap();
        let mut paths: Vec<String> = luma_validate::MERGE_TABLES.iter().map(|p| p.to_string()).collect();
        paths.extend(["a/b.bcsv.json", "a/b.jpc", "a/b.jpc.json", "a/b.png", "a/b.jpg", "a/b.tsv", "NebulaData/RtOther.tsv", "a/b.txt", "a/b.bcsv"].map(String::from));
        for path in paths {
            let changes = [Change { path: path.clone(), source: source.clone(), merge: true }];
            let refused_by_kind = check_merges(&changes, &base).iter().any(|(_, outcome)| match outcome {
                MergeOutcome::Refused(f) => luma_validate::reason(f, "UsEnglish").contains("can be added to"),
                MergeOutcome::Applied(_) => false,
            });
            assert_eq!(refused_by_kind, !luma_validate::mergeable(&path), "{path}");
        }
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
        // The test takes what the cook notes (refuse::reading).
        let _reading = crate::cook::refuse::reading();
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
        // The test takes what the cook notes (refuse::reading).
        let _reading = crate::cook::refuse::reading();
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
        // The test takes what the cook notes (refuse::reading).
        let _reading = crate::cook::refuse::reading();
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
