//! L400-L407: patches by key and by selector (luma-format 5.1).

use super::{p, seg, t, Ctx};
use crate::json::{self, Kind, Value};
use crate::{same_column, Arg, ColumnType, Table};

pub const MAX_PATCH: u64 = 64 << 20;
const KEYS: [&str; 7] = ["patch", "key", "set", "add", "remove", "select", "rows"];
/// The key of a table whose rows are named by place (contract table-keys 3).
const PLACE: &str = "#";
const MATCH_KINDS: [&str; 3] = ["equals", "prefix", "glob"];
/// The separator of key values in a row id (`Table::rows`, `Touches`).
pub const SEP: char = '\u{1f}';

struct P<'c, 'i, 'a> {
    ctx: &'c mut Ctx<'i, 'a>,
    file: String,
}

impl P<'_, '_, '_> {
    fn at(&mut self, code: u16, v: &Value, ptr: &str, args: Vec<(&'static str, Arg)>) {
        let file = self.file.clone();
        self.ctx.add(code, &file, Some(v.pos), Some(ptr), args);
    }
    fn bad(&mut self, v: &Value, ptr: &str, why: &'static str, detail: impl Into<String>) {
        self.at(400, v, ptr, vec![("found", p(why)), ("detail", t(detail))]);
    }
}

/// A cell's value as text for row ids: strings as they are, numbers as written, a word given as
/// `{ "bytes": "<hex>" }` as its text when that is UTF-8 and `{bytes:<hex>}` when not (table-keys 5).
fn cell_text(v: &Value) -> Option<String> {
    match &v.kind {
        Kind::String(s) => Some(s.clone()),
        Kind::Number(_, text) => Some(text.clone()),
        Kind::Bool(b) => Some(if *b { "1".into() } else { "0".into() }),
        Kind::Object(_) => {
            let bytes = word_bytes(v)?;
            Some(match String::from_utf8(bytes) {
                Ok(text) => text,
                Err(e) => format!("{{bytes:{}}}", e.as_bytes().iter().map(|b| format!("{b:02x}")).collect::<String>()),
            })
        }
        _ => None,
    }
}

/// The bytes of `{ "bytes": "<hex>" }`: an even number of hex digits, nothing else in the object.
fn word_bytes(v: &Value) -> Option<Vec<u8>> {
    let members = v.members();
    if members.len() != 1 || members[0].key != "bytes" {
        return None;
    }
    let hex = members[0].value.as_str()?;
    if hex.len() % 2 != 0 || !hex.bytes().all(|b| b.is_ascii_hexdigit()) {
        return None;
    }
    (0..hex.len()).step_by(2).map(|i| u8::from_str_radix(&hex[i..i + 2], 16).ok()).collect()
}

/// Whether a value is the byte form (well made or not), to say so rather than call it the wrong kind.
fn is_byte_form(v: &Value) -> bool {
    v.is_object() && v.members().iter().any(|m| m.key == "bytes")
}

fn fits(v: &Value, column: ColumnType) -> bool {
    match column {
        ColumnType::Int { min, max } => v.as_i64().is_some_and(|n| n >= min && n <= max),
        ColumnType::Float => v.as_f64().is_some(),
        ColumnType::Text => v.as_str().is_some() || word_bytes(v).is_some(),
    }
}

/// `*` any run of characters, `?` one, as the applier's select does (it compares bytes; a selector is UTF-8).
fn glob(pattern: &[u8], word: &[u8]) -> bool {
    match pattern.split_first() {
        None => word.is_empty(),
        Some((b'*', rest)) => (0..=word.len()).any(|skip| glob(rest, &word[skip..])),
        Some((b'?', rest)) => !word.is_empty() && glob(rest, &word[1..]),
        Some((c, rest)) => word.first() == Some(c) && glob(rest, &word[1..]),
    }
}

pub(crate) fn check(ctx: &mut Ctx) {
    let names: Vec<String> = ctx
        .m
        .items
        .iter()
        .filter(|i| i.kind == crate::zip::EntryKind::File)
        .map(|i| i.name.clone())
        .filter(|n| n.split('/').nth(2) == Some("patches") && n.starts_with("layers/"))
        .collect();
    let mut parsed: Vec<(String, String, Value)> = Vec::new();
    for name in names {
        let target = name.splitn(4, '/').nth(3).unwrap_or("").to_string();
        let Some(target) = target.strip_suffix(".json").map(str::to_string) else {
            ctx.add(400, &name, Some(json::Pos { line: 1, column: 1 }), None, vec![("found", p("Patch.NotJson")), ("detail", t(""))]);
            continue;
        };
        let bytes = match ctx.m.kept.get(&name) {
            Some(b) => b.clone(),
            None => {
                let Some(index) = ctx.m.index_of(&name) else { continue };
                match ctx.m.keep(index, MAX_PATCH) {
                    Ok(b) => b.to_vec(),
                    Err(crate::source::ReadError::OverLimit) => {
                        ctx.add(400, &name, Some(json::Pos { line: 1, column: 1 }), None, vec![("found", p("Patch.TooLarge")), ("detail", t(""))]);
                        continue;
                    }
                    Err(_) => continue, // L106 already
                }
            }
        };
        let root = match json::parse(&bytes) {
            Ok(v) => v,
            Err(e) => {
                ctx.add(400, &name, Some(e.pos), None, vec![("found", p(e.what)), ("detail", t(e.detail))]);
                continue;
            }
        };
        parsed.push((name, target, root));
    }
    // The scenario tables' cells first: they say which layers are on together (table-keys T-4).
    if let Some(store) = ctx.game().and_then(|g| g.store) {
        for (_, target, root) in &parsed {
            let Some(galaxy) = store.path(&target.to_lowercase()).and_then(|p| crate::scenario_table_galaxy(&p)) else { continue };
            scenario_cells(ctx, &galaxy, root);
        }
    }
    for (name, target, root) in parsed {
        let mut pc = P { ctx, file: name };
        one(&mut pc, &root, &target);
    }
}

/// The cells a patch of a galaxy's scenario table gives (`set` and `add`, by `ScenarioNo`), each a whole number.
fn scenario_cells(ctx: &mut Ctx, galaxy: &str, root: &Value) {
    let mut push = |scenario: Option<String>, values: &Value| {
        let Some(scenario) = scenario else { return };
        for m in values.members() {
            if same_column(&m.key, "ScenarioNo") {
                continue;
            }
            if let Some(value) = m.value.as_i64() {
                ctx.facts.scenario_cells.push(crate::ScenarioCell { galaxy: galaxy.to_string(), scenario: scenario.clone(), column: m.key.clone(), value });
            }
        }
    };
    let number = |v: &Value| v.members().iter().find(|m| same_column(&m.key, "ScenarioNo")).and_then(|m| cell_text(&m.value));
    for entry in root.get("set").map(|s| s.items()).unwrap_or_default() {
        if let (Some(w), Some(values)) = (entry.get("where"), entry.get("values")) {
            push(number(w), values);
        }
    }
    for row in root.get("add").map(|a| a.items()).unwrap_or_default() {
        push(number(row), row);
    }
}

/// A zone's layer sets, read once (the store path `own` names the zone in the game's case).
fn layer_sets(pc: &mut P, own: &str) -> Option<Vec<crate::LayerSet>> {
    let zone = own.split('/').nth(1)?.to_string();
    let key = zone.to_lowercase();
    if let Some(known) = pc.ctx.facts.layer_sets.get(&key) {
        return known.clone();
    }
    let sets = pc.ctx.game().and_then(|g| g.store).and_then(|s| crate::zone_layer_sets(s, &zone, &pc.ctx.facts.scenario_cells));
    pc.ctx.facts.layer_sets.insert(key, sets.clone());
    sets
}

/// A key added to a table of a link group (table-keys 13) must be new to the tables the game finds it among: for
/// `l_id` (placement, mapparts, childobj) every layer of the zone; for start and generalpos (T-4) common and the layers
/// some scenario turns on together with this one. Both in the game and in this mod's own files. Recorded as touches,
/// for two mods adding one key (L404): the group with `*` for `l_id`; for start and generalpos the table's layer (all
/// 17 for common) and each scenario that turns it on (`@<galaxy>#<ScenarioNo>`), so two adds overlap exactly when
/// the game would meet both.
fn link_add(pc: &mut P, row: &Value, rp: &str, target: &str, id: &str) {
    let store = pc.ctx.game().and_then(|g| g.store);
    let own = store.and_then(|s| s.path(&target.to_lowercase())).unwrap_or_else(|| target.to_string());
    let Some(group) = crate::link_group(&own) else { return };
    let layer = crate::link_layer(&own).unwrap_or(0);
    // None: the whole group (l_id). Some(None): by scenario, but the store gave no scenario tables.
    let reach: Option<Option<Vec<crate::LayerSet>>> = crate::link_by_scenario(&own).then(|| layer_sets(pc, &own));
    if matches!(reach, Some(None)) {
        pc.ctx.not_checked(402, "Missing.Scenarios");
    }
    let together = |other: usize| crate::layers_meet(reach.is_some(), layer, other, reach.as_ref().and_then(|s| s.as_deref()));
    let mut there: Option<String> = None;
    if let Some(store) = store {
        for other in crate::link_paths(&own) {
            if !together(crate::link_layer(&other).unwrap_or(0)) {
                continue;
            }
            let Some(game_path) = store.path(&other) else { continue };
            if store.table(&game_path).is_some_and(|tb| tb.rows.as_ref().is_some_and(|r| r.contains(id))) {
                there = Some(game_path);
                break;
            }
        }
    }
    let key = format!("{group}{SEP}{id}");
    if there.is_none() {
        there = pc.ctx.facts.link_targets.get(&key).and_then(|ts| ts.iter().find(|t| !t.eq_ignore_ascii_case(&own) && together(crate::link_layer(t).unwrap_or(0))).cloned());
    }
    let targets = pc.ctx.facts.link_targets.entry(key.clone()).or_default();
    if !targets.iter().any(|t| t.eq_ignore_ascii_case(&own)) {
        targets.push(own.clone());
    }
    if let Some(there) = there {
        pc.at(402, row, rp, vec![("why", p("Patch.KeyInGroup")), ("row", t(id.replace(SEP, ", "))), ("where", t(there))]);
    }
    let in_layer = |l: &str| format!("{}{SEP}{id}", group.replacen("/*/", &format!("/{l}/"), 1));
    let links = &mut pc.ctx.facts.touches.links;
    match &reach {
        None => links.push(key),
        Some(sets) => {
            if layer == 0 {
                links.extend(crate::LINK_LAYERS.iter().map(|l| in_layer(l)));
            } else {
                links.push(in_layer(crate::LINK_LAYERS[layer]));
                for s in sets.iter().flatten().filter(|s| s.layers >> layer & 1 == 1) {
                    links.push(in_layer(&format!("@{}#{}", s.galaxy, s.scenario)));
                }
            }
            if sets.is_none() {
                links.push(key);
            }
        }
    }
}

fn one(pc: &mut P, root: &Value, target: &str) {
    if !root.is_object() {
        pc.bad(root, "", "Patch.NotObject", "");
        return;
    }
    for m in root.members() {
        if !KEYS.contains(&m.key.as_str()) {
            let file = pc.file.clone();
            pc.ctx.add(400, &file, Some(m.key_pos), Some(&format!("/{}", seg(&m.key))), vec![("found", p("Patch.UnknownKey")), ("detail", t(m.key.clone()))]);
        }
    }
    match root.get("patch") {
        None => pc.bad(root, "", "Patch.NoVersion", ""),
        Some(v) if v.as_i64() != Some(1) => pc.bad(v, "/patch", "Patch.Version", cell_text(v).unwrap_or_default()),
        _ => {}
    }
    let mut key: Vec<String> = Vec::new();
    match root.get("key") {
        None => pc.bad(root, "", "Patch.NoKey", ""),
        Some(v) => {
            let ok = v.is_array() && (1..=8).contains(&v.items().len()) && v.items().iter().all(|k| k.as_str().is_some_and(|s| !s.is_empty()));
            if ok {
                key = v.items().iter().filter_map(|k| k.as_str().map(str::to_string)).collect();
            } else {
                pc.bad(v, "/key", "Patch.KeyShape", "");
            }
        }
    }

    // Another mod's file (F-3): `patches/@<id>/<mod path>.json`; the mod must be a need.
    if let Some(rest) = target.strip_prefix('@') {
        let other = rest.split('/').next().unwrap_or("");
        if !pc.ctx.facts.needs.iter().any(|(id, _)| id == other) {
            let file = pc.file.clone();
            pc.ctx.add(405, &file, None, None, vec![("target", t(rest.split_once('/').map_or("", |x| x.1))), ("other", t(other))]);
        }
    }
    let table: Option<Table> = if target.starts_with('@') {
        // Another mod's file: its rows are that mod's, known at load (3.11), not at import.
        pc.ctx.not_checked(401, "Missing.OtherModRows");
        None
    } else {
        match pc.ctx.game().and_then(|g| g.store) {
            None => {
                for code in [401, 402, 403, 406] {
                    pc.ctx.not_checked(code, "Missing.Store");
                }
                None
            }
            Some(store) => match store.path(&target.to_lowercase()) {
                None => {
                    let file = pc.file.clone();
                    pc.ctx.add(302, &file, None, None, vec![("found", t(target))]);
                    return;
                }
                Some(game_path) => match store.table(&game_path) {
                    None => {
                        pc.bad(root, "", "Patch.NoRows", game_path);
                        return;
                    }
                    Some(table) => {
                        if !key.is_empty() && key != table.key {
                            if let Some(v) = root.get("key") {
                                pc.at(401, v, "/key", vec![("target", t(game_path)), ("expected", t(table.key.join(", "))), ("found", t(key.join(", ")))]);
                            }
                        }
                        Some(table)
                    }
                },
            },
        }
    };
    // A table matched without case compares keys folded to lowercase, and records them so (two mods setting one row
    // in different case then overlap, as they do in the game).
    let case_insensitive = table.as_ref().is_some_and(|tb| tb.case_insensitive);
    let folded: Option<std::collections::HashSet<String>> = if case_insensitive {
        table.as_ref().and_then(|tb| tb.rows.as_ref()).map(|r| r.iter().map(|s| s.to_lowercase()).collect())
    } else {
        None
    };
    let rows = if case_insensitive { folded.as_ref() } else { table.as_ref().and_then(|tb| tb.rows.as_ref()) };
    if table.is_some() && rows.is_none() {
        pc.ctx.not_checked(402, "Missing.Rows");
    }
    let column = |name: &str| table.as_ref().and_then(|tb| tb.columns.iter().find(|(c, _)| same_column(c, name)).map(|(_, ty)| *ty));
    let by_place = key == [PLACE];
    let repeated = |id: &str| table.as_ref().and_then(|tb| tb.repeated.get(id).copied());
    let row_id = |obj: &Value| -> Option<String> {
        let mut parts = Vec::new();
        for k in &key {
            parts.push(cell_text(obj.get(k)?)?);
        }
        let id = parts.join(&SEP.to_string());
        Some(if case_insensitive { id.to_lowercase() } else { id })
    };

    // Touches compare paths in lower case; messages show the path as the mod writes it.
    let target_shown = target.to_lowercase();
    // Values against the column types (L403), every column named existing.
    let check_value = |pc: &mut P, m: &json::Member, vp: &str| {
        if table.is_none() {
            return;
        }
        {
            let vp = vp.to_string();
            match column(&m.key) {
                None => pc.at(403, &m.value, &vp, vec![("why", p("Patch.NoSuchColumn")), ("column", t(m.key.clone())), ("target", t(target.to_string()))]),
                Some(_) if is_byte_form(&m.value) && word_bytes(&m.value).is_none() => {
                    pc.at(403, &m.value, &vp, vec![("why", p("Patch.BytesShape")), ("column", t(m.key.clone()))]);
                }
                Some(ty) if !fits(&m.value, ty) => {
                    let found = match &m.value.kind {
                        crate::json::Kind::String(s) => t(format!("\"{s}\"")),
                        _ => cell_text(&m.value).map(t).unwrap_or_else(|| p(m.value.type_phrase())),
                    };
                    let mut args = vec![("why", p("Patch.ValueType")), ("column", t(m.key.clone())), ("found", found)];
                    match ty {
                        ColumnType::Int { min, max } => {
                            args.push(("expected", p("Column.Int")));
                            args.push(("min", t(min.to_string())));
                            args.push(("max", t(max.to_string())));
                        }
                        ColumnType::Float => args.push(("expected", p("Column.Float"))),
                        ColumnType::Text => args.push(("expected", p("Column.Text"))),
                    }
                    pc.at(403, &m.value, &vp, args);
                }
                Some(_) => {}
            }
        }
    };
    let check_values = |pc: &mut P, values: &Value, ptr: &str| {
        for m in values.members() {
            check_value(pc, m, &format!("{ptr}/{}", seg(&m.key)));
        }
    };
    // A key's values: `#` a place (0 or more), others as their column holds them; without the game, a value that
    // names no row (an array, null, a malformed "bytes") is still wrong. False when one is wrong (it is named).
    let check_key = |pc: &mut P, obj: &Value, ptr: &str| -> bool {
        let mut ok = true;
        for m in obj.members() {
            let vp = format!("{ptr}/{}", seg(&m.key));
            if m.key == PLACE {
                if !m.value.as_i64().is_some_and(|n| n >= 0) {
                    pc.at(403, &m.value, &vp, vec![("why", p("Patch.KeyValue")), ("column", t(m.key.clone()))]);
                    ok = false;
                }
            } else if is_byte_form(&m.value) && word_bytes(&m.value).is_none() {
                pc.at(403, &m.value, &vp, vec![("why", p("Patch.BytesShape")), ("column", t(m.key.clone()))]);
                ok = false;
            } else if cell_text(&m.value).is_none() {
                pc.at(403, &m.value, &vp, vec![("why", p("Patch.KeyValue")), ("column", t(m.key.clone()))]);
                ok = false;
            } else if column(&m.key).is_some_and(|ty| !fits(&m.value, ty)) || (table.is_some() && column(&m.key).is_none()) {
                check_value(pc, m, &vp);
                ok = false;
            }
        }
        ok
    };

    if let Some(set) = root.get("set") {
        if !set.is_array() {
            pc.bad(set, "/set", "Patch.NotArray", "set");
        }
        for (i, entry) in set.items().iter().enumerate() {
            let ep = format!("/set/{i}");
            let (Some(w), Some(values)) = (entry.get("where"), entry.get("values")) else {
                pc.bad(entry, &ep, "Patch.SetShape", "");
                continue;
            };
            if entry.members().len() != 2 || !w.is_object() || !values.is_object() || values.members().is_empty() {
                pc.bad(entry, &ep, "Patch.SetShape", "");
                continue;
            }
            let names: Vec<&str> = w.members().iter().map(|m| m.key.as_str()).collect();
            if names != key.iter().map(String::as_str).collect::<Vec<_>>() {
                pc.bad(w, &format!("{ep}/where"), "Patch.WhereNotKey", key.join(", "));
                continue;
            }
            if !check_key(pc, w, &format!("{ep}/where")) {
                check_values(pc, values, &format!("{ep}/values"));
                continue;
            }
            let id = row_id(w);
            if let (Some(rows), Some(id)) = (rows, &id) {
                if !rows.contains(id) {
                    let why = if by_place { "Patch.PlaceNotFound" } else { "Patch.RowNotFound" };
                    pc.at(402, w, &format!("{ep}/where"), vec![("why", p(why)), ("row", t(id.replace(SEP, ", "))), ("count", t(rows.len().to_string()))]);
                } else if let Some(n) = repeated(id) {
                    pc.at(402, w, &format!("{ep}/where"), vec![("why", p("Patch.KeyRepeated")), ("row", t(id.replace(SEP, ", "))), ("count", t(n.to_string()))]);
                }
            }
            check_values(pc, values, &format!("{ep}/values"));
            if let Some(id) = id {
                for m in values.members() {
                    pc.ctx.facts.touches.cells.push(format!("{target_shown}{SEP}{id}{SEP}{}", m.key));
                }
            }
        }
    }
    if let Some(add) = root.get("add") {
        if !add.is_array() {
            pc.bad(add, "/add", "Patch.NotArray", "add");
        }
        for (i, row) in add.items().iter().enumerate() {
            let rp = format!("/add/{i}");
            if !row.is_object() {
                pc.bad(row, &rp, "Patch.RowShape", "");
                continue;
            }
            if by_place {
                // A row added to a table named by place goes at the end (table-keys 3): it has no key to give.
                check_values(pc, row, &rp);
                pc.ctx.facts.touches.adds.push(format!("{target_shown}{SEP}+{i}"));
                continue;
            }
            if key.iter().any(|k| row.get(k).is_none()) {
                pc.bad(row, &rp, "Patch.AddNoKey", key.join(", "));
                continue;
            }
            let key_only = Value { pos: row.pos, kind: json::Kind::Object(row.members().iter().filter(|m| key.contains(&m.key)).cloned().collect()) };
            if !check_key(pc, &key_only, &rp) {
                continue;
            }
            let Some(id) = row_id(row) else { continue };
            if rows.is_some_and(|rows| rows.contains(&id)) {
                pc.at(402, row, &rp, vec![("why", p("Patch.RowExists")), ("row", t(id.replace(SEP, ", ")))]);
            } else {
                link_add(pc, row, &rp, target, &id);
            }
            check_values(pc, row, &rp);
            pc.ctx.facts.touches.adds.push(format!("{target_shown}{SEP}{id}"));
        }
    }
    if let Some(remove) = root.get("remove") {
        if !remove.is_array() {
            pc.bad(remove, "/remove", "Patch.NotArray", "remove");
        }
        for (i, row) in remove.items().iter().enumerate() {
            let rp = format!("/remove/{i}");
            if by_place {
                pc.at(402, row, &rp, vec![("why", p("Patch.RemoveByPlace")), ("row", t("")), ("count", t(""))]);
                continue;
            }
            let names: Vec<&str> = row.members().iter().map(|m| m.key.as_str()).collect();
            if names != key.iter().map(String::as_str).collect::<Vec<_>>() {
                pc.bad(row, &rp, "Patch.WhereNotKey", key.join(", "));
                continue;
            }
            if !check_key(pc, row, &rp) {
                continue;
            }
            let Some(id) = row_id(row) else { continue };
            if rows.is_some_and(|rows| !rows.contains(&id)) {
                pc.at(402, row, &rp, vec![("why", p("Patch.RowNotFound")), ("row", t(id.replace(SEP, ", ")))]);
            } else if let Some(n) = repeated(&id) {
                pc.at(402, row, &rp, vec![("why", p("Patch.KeyRepeated")), ("row", t(id.replace(SEP, ", "))), ("count", t(n.to_string()))]);
            }
            pc.ctx.facts.touches.removes.push(format!("{target_shown}{SEP}{id}"));
        }
    }
    if let Some(whole) = root.get("rows") {
        if !whole.is_array() {
            pc.bad(whole, "/rows", "Patch.NotArray", "rows");
        } else if table.is_some() && !by_place {
            pc.at(402, whole, "/rows", vec![("why", p("Patch.RowsNotByPlace")), ("row", t("")), ("count", t(""))]);
        }
        for (i, row) in whole.items().iter().enumerate() {
            let rp = format!("/rows/{i}");
            if !row.is_object() {
                pc.bad(row, &rp, "Patch.RowShape", "");
                continue;
            }
            check_values(pc, row, &rp);
        }
        pc.ctx.facts.touches.adds.push(format!("{target_shown}{SEP}*"));
    }
    if let Some(select) = root.get("select") {
        if !select.is_array() {
            pc.bad(select, "/select", "Patch.NotArray", "select");
        }
        for (i, entry) in select.items().iter().enumerate() {
            let ep = format!("/select/{i}");
            let (Some(m), Some(values)) = (entry.get("match"), entry.get("values")) else {
                pc.bad(entry, &ep, "Patch.SelectShape", "");
                continue;
            };
            if entry.members().len() != 2 || !m.is_object() || m.members().is_empty() || !values.is_object() || values.members().is_empty() {
                pc.bad(entry, &ep, "Patch.SelectShape", "");
                continue;
            }
            for c in m.members() {
                let cp = format!("{ep}/match/{}", seg(&c.key));
                let one = c.value.members();
                let shape_ok = one.len() == 1 && cell_text(&one[0].value).is_some();
                if !shape_ok || !MATCH_KINDS.contains(&one[0].key.as_str()) {
                    pc.at(406, &c.value, &cp, vec![("why", p("Select.MatchKind")), ("column", t(c.key.clone()))]);
                } else if one[0].key == "glob" && one[0].value.as_str().is_none() {
                    pc.at(406, &c.value, &cp, vec![("why", p("Select.GlobNotText")), ("column", t(c.key.clone()))]);
                }
                if table.is_some() && column(&c.key).is_none() {
                    pc.at(406, &c.value, &cp, vec![("why", p("Select.NoSuchColumn")), ("column", t(c.key.clone()))]);
                }
            }
            check_values(pc, values, &format!("{ep}/values"));
        }
        // A selector that matches none of the game's rows (nor a row this patch adds) is L407 now; at load the applier
        // counts again over every mod's changes (3.11, its Selected fact). Without the game's values it waits for the
        // load.
        let cells = table.as_ref().and_then(|tb| tb.cells.as_ref().map(|c| (tb, c)));
        for (i, entry) in select.items().iter().enumerate() {
            let Some(m) = entry.get("match") else { continue };
            let Some((tb, game_rows)) = cells else {
                pc.ctx.not_checked(407, "Missing.Load");
                break;
            };
            let mut tests = Vec::new();
            for c in m.members() {
                let one = c.value.members();
                let (Some(at), [only]) = (tb.columns.iter().position(|(name, _)| same_column(name, &c.key)), one) else { continue };
                let Some(value) = cell_text(&only.value) else { continue };
                tests.push((at, only.key.clone(), value));
            }
            if tests.len() != m.members().len() {
                continue; // L406 named it
            }
            let hit = |row: &[String]| {
                tests.iter().all(|(at, kind, value)| {
                    let cell = row.get(*at).map(String::as_str).unwrap_or("");
                    match kind.as_str() {
                        "equals" => cell == value || cell.parse::<f64>().ok().zip(value.parse::<f64>().ok()).is_some_and(|(a, b)| (a - b).abs() < 1e-4),
                        "prefix" => cell.starts_with(value.as_str()),
                        _ => glob(value.as_bytes(), cell.as_bytes()),
                    }
                })
            };
            let added = root.get("add").map(|a| a.items().to_vec()).unwrap_or_default();
            let added_rows: Vec<Vec<String>> = added
                .iter()
                .map(|row| tb.columns.iter().map(|(name, _)| row.members().iter().find(|mm| same_column(name, &mm.key)).and_then(|mm| cell_text(&mm.value)).unwrap_or_default()).collect())
                .collect();
            if !game_rows.iter().chain(added_rows.iter()).any(|r| hit(r)) {
                pc.at(407, m, &format!("/select/{i}/match"), vec![("target", t(target.to_string()))]);
            }
        }
    }
}
