//! L800-L803, L805, L305, L404: the mod against the game's release and the other installed mods.

use super::patches::SEP;
use super::{p, t, Ctx};
use crate::semver::Range;

pub(crate) fn check(ctx: &mut Ctx) {
    let own = ctx.mod_id.clone().unwrap_or_default();
    let root = ctx.manifest.clone();
    let root = root.as_ref();
    let shown_file = ctx.m.shown.clone();

    // L803: the Nebula release this mod needs.
    if let Some((range, v)) = root.and_then(|r| r.get("nebula")).and_then(|v| v.as_str().and_then(Range::parse).map(|r| (r, v))) {
        match ctx.game() {
            Some(game) if !range.holds(&game.nebula) => {
                let (pos, found) = (v.pos, game.nebula.to_string());
                ctx.add(803, "luma.json", Some(pos), Some("/nebula"), vec![("expected", t(v.as_str().unwrap_or(""))), ("found", t(found))]);
            }
            Some(_) => {}
            None => ctx.not_checked(803, "Missing.Game"),
        }
    }

    let installed = ctx.input.installed;
    // L802: another installed mod with this id.
    // An update of an installed mod is not this: the game leaves the mod it replaces out of `installed`.
    for other in installed.iter().filter(|o| o.id == own && o.file != shown_file) {
        ctx.add(802, "luma.json", None, None, vec![("found", t(own.clone())), ("other", t(other.file.clone()))]);
    }
    // L800: each need installed, on, and of a version in range.
    for (id, range) in ctx.facts.needs.clone() {
        let found = match installed.iter().find(|o| o.id == id) {
            None => Some(p("Need.Missing")),
            Some(o) if !o.on => Some(p("Need.Off")),
            Some(o) if range.as_ref().is_some_and(|r| !r.holds(&o.version)) => {
                let need = root.and_then(|r| r.get("needs")).map(|n| n.items().to_vec()).unwrap_or_default();
                let wanted = need.iter().find(|x| x.get("id").and_then(|i| i.as_str()) == Some(id.as_str())).and_then(|x| x.get("version")).and_then(|v| v.as_str()).unwrap_or("").to_string();
                ctx.add(800, "luma.json", None, None, vec![("other", t(id.clone())), ("expected", t(wanted)), ("found", p("Need.Version")), ("version", t(o.version.to_string()))]);
                None
            }
            Some(_) => None,
        };
        if let Some(found) = found {
            let need = root.and_then(|r| r.get("needs")).map(|n| n.items().to_vec()).unwrap_or_default();
            let wanted = need.iter().find(|x| x.get("id").and_then(|i| i.as_str()) == Some(id.as_str())).and_then(|x| x.get("version")).and_then(|v| v.as_str()).unwrap_or("").to_string();
            ctx.add(800, "luma.json", None, None, vec![("other", t(id.clone())), ("expected", t(wanted)), ("found", found)]);
        }
    }
    // L801: a conflict, declared by this mod or by the other.
    let conflicts: Vec<(String, String)> = root
        .and_then(|r| r.get("conflicts"))
        .map(|c| c.items().iter().filter_map(|x| Some((x.get("id")?.as_str()?.to_string(), x.get("why")?.as_str()?.to_string()))).collect())
        .unwrap_or_default();
    for other in installed.iter().filter(|o| o.on && o.id != own) {
        if let Some((_, why)) = conflicts.iter().find(|(id, _)| *id == other.id) {
            ctx.add(801, "luma.json", None, None, vec![("other", t(other.id.clone())), ("why", t(why.clone()))]);
        } else if other.conflicts.contains(&own) {
            ctx.add(801, "luma.json", None, None, vec![("other", t(other.id.clone())), ("why", p("Conflict.Theirs"))]);
        }
    }
    // L305 and L404 (Notes): the same file replaced, or the same cell changed, by another mod that is on. The mod higher
    // in the list wins, for everything (F, INBOX-G 13): installed mods are given in list order, the top first, and
    // `position` says how many are above this one.
    let position = ctx.input.position.unwrap_or(installed.len());
    let touches = ctx.facts.touches.clone();
    for (i, other) in installed.iter().enumerate().filter(|(_, o)| o.on && o.id != own) {
        let winner = if i < position { other.id.clone() } else { own.clone() };
        for path in touches.replaces.iter().filter(|p| other.touches.replaces.contains(p)) {
            ctx.add(305, "luma.json", None, None, vec![("other", t(other.id.clone())), ("found", t(path.clone())), ("winner", t(winner.clone()))]);
        }
        for cell in touches.cells.iter().filter(|c| other.touches.cells.contains(c)) {
            let parts: Vec<&str> = cell.split(SEP).collect();
            if parts.len() >= 3 {
                let column = parts[parts.len() - 1];
                let row = parts[1..parts.len() - 1].join(", ");
                ctx.add(404, "luma.json", None, None, vec![
                    ("other", t(other.id.clone())), ("target", t(parts[0])), ("row", t(row)), ("what", p("Clash.Column")), ("column", t(column)), ("winner", t(winner.clone())),
                ]);
            }
        }
        // L805: a function this mod replaces that a mod above it in the list replaces already; the game binds from the
        // top down, holds the higher mod's replacement and refuses this one, so this mod is off.
        if i < position {
            for f in touches.functions.iter().filter(|f| other.touches.functions.contains(f)) {
                let (name, sig) = f.split_once(SEP).unwrap_or((f.as_str(), ""));
                let file = ctx.input.platforms.first().and_then(|p| super::code::entry_path(p)).unwrap_or_else(|| "luma.json".to_string());
                ctx.add(805, &file, None, None, vec![("other", t(other.id.clone())), ("found", t(name)), ("sig", t(sig))]);
            }
        }
        // Two mods adding one key: to one table, or (table-keys 13) to one zone's link group in any layers. The higher
        // mod's row stays (the applier's conflict fact). Rows added by place (`+n`) are not keys.
        let same_table = touches.adds.iter().filter(|r| other.touches.adds.contains(r)).filter(|r| {
            let mut parts = r.split(SEP);
            let (table, first) = (parts.next().unwrap_or(""), parts.next().unwrap_or(""));
            !first.starts_with('+') && crate::link_group(table).is_none()
        });
        for row in same_table.chain(touches.links.iter().filter(|k| other.touches.links.contains(k))) {
            let parts: Vec<&str> = row.split(SEP).collect();
            ctx.add(404, "luma.json", None, None, vec![
                ("other", t(other.id.clone())), ("target", t(parts[0])), ("row", t(parts[1..].join(", "))), ("what", p("Clash.Row")), ("winner", t(winner.clone())),
            ]);
        }
        for row in touches.adds.iter().filter(|r| other.touches.removes.contains(r)).chain(touches.removes.iter().filter(|r| other.touches.adds.contains(r))) {
            let parts: Vec<&str> = row.split(SEP).collect();
            ctx.add(404, "luma.json", None, None, vec![
                ("other", t(other.id.clone())), ("target", t(parts[0])), ("row", t(parts[1..].join(", "))), ("what", p("Clash.Row")), ("winner", t(winner.clone())),
            ]);
        }
    }
}
