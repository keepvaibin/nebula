//! L300-L307: layers and the paths in them (luma-format 4, 5).

use super::{p, t, Ctx};
use crate::zip::EntryKind;
use std::collections::BTreeSet;

/// Source endings a whole file may have instead of the game format's (luma-format 5).
pub const SOURCE_ENDINGS: [&str; 16] =
    [".gltf", ".glb", ".bin", ".png", ".jpg", ".jpeg", ".ktx2", ".dds", ".wav", ".ogg", ".mid", ".json", ".ttf", ".otf", ".webm", ".mp4"];

/// A source file (lowercase path under `files/`) of a kind the cook makes: a model, an animation file, collision, a
/// cutscene camera (`.glb`, `.gltf` and their `.anim.`, `.kcl.`, `.canm.` forms), a picture (`.png`), a stream (`.wav`,
/// `.ogg`), a table (`.bcsv.json`, `.bcam.json`, a stage's placement `.arc/jmp/**.json`), a screen layout
/// (`.layout.json`) or a sound effect (`.sound.json`, which belongs in `assets/`: the cook refuses it here by name).
/// What it stands in for is the cook's to say (nebula-import cook::overlay::cook_source, which a test holds to this
/// list): it refuses by name (L804) at the game's start and in LumaLink's check before a pack.
pub fn cooked_source(lower: &str) -> bool {
    [".glb", ".gltf", ".png", ".wav", ".ogg", ".bcsv.json", ".bcam.json", ".layout.json", ".sound.json"].iter().any(|e| lower.ends_with(e))
        || (lower.ends_with(".json") && lower.contains(".arc/jmp/"))
}

/// The game path a file at `layers/<l>/files/<path>` stands at, as kept for L305: lowercase, and a source form with
/// `.json` added to a file name that has its own ending (`scenariodata.bcsv.json`) is that file, so two mods bringing
/// one file in either form are named (V4-2).
fn replaced(path: &str) -> String {
    let lower = path.to_lowercase();
    match lower.strip_suffix(".json") {
        Some(stem) if stem.rsplit('/').next().is_some_and(|name| name.contains('.')) => stem.to_string(),
        _ => lower,
    }
}

pub(crate) fn check(ctx: &mut Ctx) {
    let listed: Vec<(String, crate::json::Pos, String)> = ctx.facts.layers.clone();
    let mut folders: BTreeSet<String> = BTreeSet::new();
    for item in ctx.m.items.clone() {
        let mut parts = item.name.splitn(4, '/');
        if parts.next() != Some("layers") {
            continue;
        }
        let Some(layer) = parts.next() else { continue };
        let is_folder_entry = item.kind == EntryKind::Folder;
        let part = parts.next();
        if part.is_none() && !is_folder_entry {
            // A file directly under layers/.
            ctx.add(306, &item.name, None, None, vec![("entry", t(item.name.clone()))]);
            continue;
        }
        folders.insert(layer.to_string());
        let Some(part) = part else { continue };
        let rest = parts.next();
        // textures/: the game's textures replaced by their names (`<model>/<texture>.png` or `.dds`, or Dolphin's
        // `tex1_` names, which the game names at install), as a texture pack (luma-format 5, decision 3 of the one form).
        let texture_ok = rest.is_some_and(|r| {
            let lower = r.to_ascii_lowercase();
            is_folder_entry || lower.ends_with(".png") || lower.ends_with(".dds")
        });
        if !matches!(part, "files" | "patches" | "assets" | "textures") || (rest.is_none() && !is_folder_entry) || (part == "textures" && rest.is_some() && !texture_ok) {
            ctx.add(306, &item.name, None, None, vec![("entry", t(item.name.clone()))]);
        }
    }
    // A layer whose whole content is the previous engine's rows (`pre_standard.layers.<name>`, OLD-ROWS) has no folder:
    // a .luma holds no empty folders.
    let rows_only: BTreeSet<String> = ctx
        .manifest
        .as_ref()
        .and_then(|m| m.get("pre_standard"))
        .and_then(|p| p.get("layers"))
        .map(|l| l.members().iter().map(|m| m.key.clone()).collect())
        .unwrap_or_default();
    for (name, pos, ptr) in &listed {
        if !folders.contains(name) && !rows_only.contains(name) {
            let ptr = if ptr.is_empty() { None } else { Some(ptr.as_str()) };
            ctx.add(300, "luma.json", Some(*pos), ptr, vec![("layer", t(name.clone())), ("why", p("Layer.NoFolder"))]);
        }
    }
    for folder in &folders {
        if !listed.iter().any(|(n, _, _)| n == folder) {
            ctx.add(300, &format!("layers/{folder}/"), None, None, vec![("layer", t(folder.clone())), ("why", p("Layer.NotListed"))]);
        }
    }

    // L307: a layer the mod's code chooses, on a platform it may apply to where the mod has no code.
    if let Some(root) = ctx.manifest.clone() {
        for (i, layer) in root.get("layers").map(|l| l.items().to_vec()).unwrap_or_default().iter().enumerate() {
            let Some(when) = layer.get("when") else { continue };
            if when.get("code").and_then(|c| c.as_bool()) != Some(true) {
                continue;
            }
            let name = layer.get("name").and_then(|n| n.as_str()).unwrap_or("").to_string();
            let platforms: Vec<String> = match when.get("platform") {
                Some(list) => list.items().iter().filter_map(|x| x.as_str().map(str::to_string)).collect(),
                None => ctx.input.platforms.iter().map(|s| s.to_string()).collect(),
            };
            for platform in platforms {
                if !ctx.facts.code_platforms.contains(&platform) {
                    ctx.add(307, "luma.json", Some(when.pos), Some(&format!("/layers/{i}/when")), vec![("layer", t(name.clone())), ("platform", t(platform))]);
                }
            }
        }
    }

    // L302-L304: whole files at game paths, against the player's game.
    let store = ctx.game().and_then(|g| g.store);
    for item in ctx.m.items.clone() {
        if item.kind != EntryKind::File {
            continue;
        }
        let parts: Vec<&str> = item.name.splitn(4, '/').collect();
        if parts.len() != 4 || parts[0] != "layers" {
            continue;
        }
        match parts[2] {
            "files" => {
                let path = parts[3];
                ctx.facts.touches.replaces.push(replaced(path));
                let Some(store) = store else {
                    ctx.not_checked(302, "Missing.Store");
                    ctx.not_checked(303, "Missing.Store");
                    continue;
                };
                let lower = path.to_lowercase();
                let source = SOURCE_ENDINGS.iter().find(|e| lower.ends_with(**e));
                match store.path(&lower) {
                    Some(game_path) => {
                        if store.table(&game_path).is_some() {
                            ctx.add(303, &item.name, None, None, vec![("found", t(game_path))]);
                        }
                    }
                    None => {
                        // A source form of a game table at the table's own path (`objinfo.json` beside `objinfo`,
                        // as `lumalink extract --game` writes it) still replaces rows: L303, in any form.
                        let table = source.and_then(|e| store.path(&lower[..lower.len() - e.len()])).filter(|g| store.table(g).is_some());
                        if let Some(game_path) = table {
                            ctx.add(303, &item.name, None, None, vec![("found", t(game_path))]);
                        } else if source.is_some() && !cooked_source(&lower) {
                            // A source standing in for a game file of another ending: what it may stand in for is
                            // each content kind's contract (3.2 to 3.10), checked by that kind's rules; the kinds the
                            // cook makes are the cook's (L804), the others not yet defined.
                            ctx.not_checked(304, "Missing.KindContract");
                        } else if !store.may_be_new(&lower) {
                            ctx.add(302, &item.name, None, None, vec![("found", t(path))]);
                        }
                    }
                }
            }
            "assets" => {
                ctx.facts.touches.assets.push(parts[3].to_string());
            }
            _ => {}
        }
    }
}
