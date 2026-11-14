//! Placements of classes only code can make (references 3): a stage's placement row names its object's class, which
//! the game's factory makes (PlacementInfoOrdered, NameObjFactory::getCreator) or a mod's code makes at its start. A
//! class neither the game's factory list (the self-description's placement facts) nor the game's planet table names
//! cannot be checked before the mods' code runs: it is kept as an `after_start` touch (class, file, line), which the
//! host gives the game module after the last mod's start (luma_refs.h), and said as not checked here (L403,
//! Missing.AfterStart; Missing.Classes when the factory list was not given, and every placement waits). A class no
//! mod made is then Content.MissingClass (620) and the placeholder object stands there.

use super::patches::{MAX_PATCH, SEP};
use super::Ctx;
use crate::json::Value;
use crate::same_column;
use std::collections::HashSet;

/// The game's planet table: a planet's name is a class too (PlanetMapCreatorFunction, before the factory).
const PLANET_TABLE: &str = "objectdata/planetmapdatatable.arc/planetmapdatatable.bcsv";

/// Whether `path` (a game path, without a source ending) is a stage's placement table: a table of
/// `jmp/Placement/<layer>/` or `jmp/MapParts/<layer>/`, whose rows' `name` is a class. A zone list (`StageObjInfo`)
/// names zones, not classes.
pub(crate) fn is_placement_table(path: &str) -> bool {
    let lower = path.to_ascii_lowercase();
    let parts: Vec<&str> = lower.split('/').collect();
    let n = parts.len();
    n >= 4 && parts[n - 4] == "jmp" && matches!(parts[n - 3], "placement" | "mapparts") && parts[n - 1] != "stageobjinfo"
}

/// The classes known before any mod's code runs, in lowercase: the factory's (None when the self-description gives
/// none: then no class is known and every placement is left to the check after start) and the game's planets.
struct Known {
    factory: Option<HashSet<String>>,
    planets: HashSet<String>,
}

impl Known {
    fn of(ctx: &Ctx) -> Known {
        let game = ctx.game();
        let factory = game.and_then(|g| g.names).and_then(|n| n.factory.clone());
        let mut planets = HashSet::new();
        if factory.is_some() {
            if let Some(store) = game.and_then(|g| g.store) {
                if let Some(table) = store.path(PLANET_TABLE).and_then(|p| store.table(&p)) {
                    if let (Some(rows), Some(at)) = (table.cells.as_ref(), table.columns.iter().position(|(c, _)| same_column(c, "PlanetName"))) {
                        planets.extend(rows.iter().filter_map(|r| r.get(at)).map(|n| n.to_lowercase()));
                    }
                }
            }
        }
        Known { factory, planets }
    }

    fn has(&self, class: &str) -> bool {
        let lower = class.to_lowercase();
        self.factory.as_ref().is_some_and(|f| f.contains(&lower)) || self.planets.contains(&lower)
    }
}

/// One placement of `class` at `file` and `line`; `shape` its `ShapeModelNo` when given: a row with a shape (not -1)
/// is made by the model-changeable factory by its shape, not by class.
fn placed(ctx: &mut Ctx, known: &Known, class: &str, shape: Option<i64>, file: &str, line: u32) {
    if class.is_empty() || shape.is_some_and(|s| s != -1) || known.has(class) {
        return;
    }
    ctx.facts.touches.after_start.push(format!("{class}{SEP}{file}{SEP}{line}"));
    ctx.not_checked(403, if known.factory.is_some() { "Missing.AfterStart" } else { "Missing.Classes" });
}

/// A row's (or a patch's values') class and shape: its `name` text and its `ShapeModelNo` number.
fn class_of(values: &Value) -> Option<(&str, Option<i64>, u32)> {
    let member = |name: &str| values.members().iter().find(|m| same_column(&m.key, name));
    let class = member("name")?;
    Some((class.value.as_str()?, member("ShapeModelNo").and_then(|m| m.value.as_i64()), class.value.pos.line))
}

/// The placements a patch of a placement table gives (`add` and `rows` rows, `set` and `select` values that write
/// `name`), each at its `name`'s line in the patch. A patch in a form the patch rules refuse gives what it can.
pub(crate) fn patch(ctx: &mut Ctx, file: &str, target: &str, root: &Value) {
    if !is_placement_table(target) {
        return;
    }
    let known = Known::of(ctx);
    let mut found = Vec::new();
    for list in ["add", "rows"] {
        for row in root.get(list).map(|l| l.items()).unwrap_or_default() {
            found.extend(class_of(row));
        }
    }
    for list in ["set", "select"] {
        for entry in root.get(list).map(|l| l.items()).unwrap_or_default() {
            found.extend(entry.get("values").and_then(class_of));
        }
    }
    for (class, shape, line) in found {
        placed(ctx, &known, class, shape, file, line);
    }
}

/// The placements of a stage's placement table a mod brings whole (`files/<path>`, the game's form or its source
/// form `.json`), each at its row's number (from 1), as the store reads the table.
pub(crate) fn whole_tables(ctx: &mut Ctx) {
    let names: Vec<String> = ctx.m.items.iter().filter(|i| i.kind == crate::zip::EntryKind::File).map(|i| i.name.clone()).collect();
    let mut known: Option<Known> = None;
    for name in names {
        let parts: Vec<&str> = name.splitn(4, '/').collect();
        if parts.len() != 4 || parts[0] != "layers" || parts[2] != "files" {
            continue;
        }
        let path = parts[3].strip_suffix(".json").unwrap_or(parts[3]).to_string();
        if !is_placement_table(&path) {
            continue;
        }
        let Some(store) = ctx.game().and_then(|g| g.store) else {
            // Without the game's store the table's form is not read: every placement waits for the store.
            ctx.not_checked(403, "Missing.Store");
            continue;
        };
        let bytes = match ctx.m.kept.get(&name) {
            Some(b) => b.clone(),
            None => {
                let Some(index) = ctx.m.index_of(&name) else { continue };
                match ctx.m.keep(index, MAX_PATCH) {
                    Ok(b) => b.to_vec(),
                    Err(_) => continue, // L105 or L106 already
                }
            }
        };
        let Some(table) = store.read_table(&path, &bytes) else {
            ctx.not_checked(403, "Missing.Store");
            continue;
        };
        let (Some(rows), Some(at)) = (table.cells.as_ref(), table.columns.iter().position(|(c, _)| same_column(c, "name"))) else { continue };
        let shape_at = table.columns.iter().position(|(c, _)| same_column(c, "ShapeModelNo"));
        let known = known.get_or_insert_with(|| Known::of(ctx));
        let placements: Vec<(String, Option<i64>, u32)> = rows
            .iter()
            .enumerate()
            .filter_map(|(i, row)| {
                let class = row.get(at)?.clone();
                let shape = shape_at.and_then(|s| row.get(s)).and_then(|v| v.parse::<i64>().ok());
                Some((class, shape, u32::try_from(i + 1).unwrap_or(u32::MAX)))
            })
            .collect();
        for (class, shape, line) in placements {
            placed(ctx, known, &class, shape, &name, line);
        }
    }
}
