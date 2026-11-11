//! L400-L407: patches by key and by selector (luma-format 5.1).

use super::{p, seg, t, Ctx};
use crate::json::{self, Kind, Value};
use crate::{Arg, ColumnType, Table};

pub const MAX_PATCH: u64 = 64 << 20;
const KEYS: [&str; 6] = ["patch", "key", "set", "add", "remove", "select"];
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

/// A cell's value as text for row ids: strings as they are, numbers as written.
fn cell_text(v: &Value) -> Option<String> {
    match &v.kind {
        Kind::String(s) => Some(s.clone()),
        Kind::Number(_, text) => Some(text.clone()),
        Kind::Bool(b) => Some(if *b { "1".into() } else { "0".into() }),
        _ => None,
    }
}

fn fits(v: &Value, column: ColumnType) -> bool {
    match column {
        ColumnType::Int { min, max } => v.as_i64().is_some_and(|n| n >= min && n <= max),
        ColumnType::Float => v.as_f64().is_some(),
        ColumnType::Text => v.as_str().is_some(),
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
        let mut pc = P { ctx, file: name.clone() };
        one(&mut pc, &root, &target);
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
    let column = |name: &str| table.as_ref().and_then(|tb| tb.columns.iter().find(|(c, _)| c == name).map(|(_, ty)| *ty));
    let row_id = |obj: &Value| -> Option<String> {
        let mut parts = Vec::new();
        for k in &key {
            parts.push(cell_text(obj.get(k)?)?);
        }
        let id = parts.join(&SEP.to_string());
        Some(if case_insensitive { id.to_lowercase() } else { id })
    };

    let target_shown = target.to_lowercase();
    // Values against the column types (L403), every column named existing.
    let check_values = |pc: &mut P, values: &Value, ptr: &str| {
        for m in values.members() {
            let vp = format!("{ptr}/{}", seg(&m.key));
            if table.is_none() {
                continue;
            }
            match column(&m.key) {
                None => pc.at(403, &m.value, &vp, vec![("why", p("Patch.NoSuchColumn")), ("column", t(m.key.clone())), ("target", t(target_shown.clone()))]),
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
            let id = row_id(w);
            if let (Some(rows), Some(id)) = (rows, &id) {
                if !rows.contains(id) {
                    pc.at(402, w, &format!("{ep}/where"), vec![("why", p("Patch.RowNotFound")), ("row", t(id.replace(SEP, ", ")))]);
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
            let Some(id) = row_id(row) else {
                pc.bad(row, &rp, "Patch.AddNoKey", key.join(", "));
                continue;
            };
            if rows.is_some_and(|rows| rows.contains(&id)) {
                pc.at(402, row, &rp, vec![("why", p("Patch.RowExists")), ("row", t(id.replace(SEP, ", ")))]);
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
            let names: Vec<&str> = row.members().iter().map(|m| m.key.as_str()).collect();
            if names != key.iter().map(String::as_str).collect::<Vec<_>>() {
                pc.bad(row, &rp, "Patch.WhereNotKey", key.join(", "));
                continue;
            }
            let Some(id) = row_id(row) else { continue };
            if rows.is_some_and(|rows| !rows.contains(&id)) {
                pc.at(402, row, &rp, vec![("why", p("Patch.RowNotFound")), ("row", t(id.replace(SEP, ", ")))]);
            }
            pc.ctx.facts.touches.removes.push(format!("{target_shown}{SEP}{id}"));
        }
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
        // How many rows a selector matches is known at load (3.11): L407 is the content system's.
        if !select.items().is_empty() {
            pc.ctx.not_checked(407, "Missing.Load");
        }
    }
}
