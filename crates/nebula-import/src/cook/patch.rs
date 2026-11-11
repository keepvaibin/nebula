//! Patches by key (luma-format 5.1, contract table-keys; plan 3.11): a game table and the patches of every mod that is
//! on, in load order, make the table the game reads. Each mod's patch applies in this order: `rows` (a whole `#`
//! table), `remove`, `set`, `add`, `select`. Patches reach here checked by the validator; what only the mod list
//! decides (two mods changing one cell, a row another mod removed) is a [`Fact`] for the Mods screen, never silent.
//! The later mod in load order wins. A table of a kind (cook::table_columns) that has no rows takes its kind's columns
//! first, and a row a mod adds starts from the value most of the game's rows have in each column (table-keys 10).

use super::table::{Cell, Column, Kind, TableIr};
use super::table_columns::{columns_of, widen_empty, KindColumn};
use super::table_keys::{rule_for, Key};
use anyhow::{bail, Context, Result};
use serde_json::{Map, Value};
use std::collections::HashMap;

/// One mod's patch of the table.
pub struct ModPatch<'a> {
    pub mod_id: &'a str,
    /// The patch file in the mod, to name it.
    pub file: &'a str,
    pub patch: &'a Value,
}

/// What the Mods screen says about a patched table.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Fact {
    /// Two mods change one row (`column` None: the whole row, by add, remove or rows); `later` is used.
    Conflict { table: String, row: String, column: Option<String>, earlier: String, later: String },
    /// A row a patch names is not there: `removed_by` removed it, or (None) an earlier mod's `rows` left it out.
    Missing { table: String, row: String, mod_id: String, file: String, removed_by: Option<String> },
    /// How many rows a mod's selector matched (0 is a Note: L407).
    Selected { table: String, mod_id: String, file: String, selector: usize, rows: usize },
    /// A mod's patch could not apply (the validator passed it against another game, or the mod changed since): the
    /// whole patch is left out, the other mods' still apply. `why` is English, for the log.
    Failed { table: String, mod_id: String, file: String, why: String },
}

/// Who changed what, so the next mod's change to it is named.
#[derive(Clone, Default)]
struct Owners {
    rows: HashMap<String, String>,
    cells: HashMap<(String, u32), String>,
    removed: HashMap<String, String>,
    /// A mod gave the whole table.
    whole: Option<String>,
}

struct Applier<'t> {
    path: String,
    table: &'t mut TableIr,
    key: Vec<usize>,
    key_names: Vec<String>,
    positional: bool,
    case_insensitive: bool,
    /// The kind's columns, for the usual value of a column a new row leaves out.
    kind: &'static [KindColumn],
    owners: Owners,
    facts: Vec<Fact>,
}

/// The table with every patch applied, and the facts. A patch that cannot apply is left out whole (Fact::Failed, naming
/// the mod and the file); an error is a game table its rule cannot read (a key column missing from a table with rows).
pub fn apply(path: &str, table: &mut TableIr, patches: &[ModPatch]) -> Result<Vec<Fact>> {
    // A disc path names the same table as its store path (an archive is a folder of the store).
    let store_path = super::store::store_path(path.trim_start_matches('/'));
    let path = store_path.as_str();
    let rule = rule_for(path);
    let kind = columns_of(path).unwrap_or(&[]);
    if !patches.is_empty() {
        // An empty layer's table often has only a few of its kind's columns (21 generalpos files lack even their key):
        // it takes the rest, so a row added to it keeps every value (no row is there to read one it never had).
        widen_empty(path, table);
    }
    let (key, key_names, positional) = match &rule.key {
        Key::Position => (Vec::new(), Vec::new(), true),
        Key::Columns(names) => {
            let mut at = Vec::with_capacity(names.len());
            for name in names {
                at.push(table.column_of(name)?.with_context(|| format!("{path}: its key column {name} is not in the table"))?);
            }
            (at, names.clone(), false)
        }
    };
    let mut a = Applier {
        path: path.to_string(),
        table,
        key,
        key_names,
        positional,
        case_insensitive: rule.case_insensitive,
        kind,
        owners: Owners::default(),
        facts: Vec::new(),
    };
    for p in patches {
        let (table, owners, facts) = (a.table.clone(), a.owners.clone(), a.facts.len());
        if let Err(e) = a.one(p) {
            *a.table = table;
            a.owners = owners;
            a.facts.truncate(facts);
            a.facts.push(Fact::Failed { table: a.path.clone(), mod_id: p.mod_id.into(), file: p.file.into(), why: format!("{e:#}") });
        }
    }
    Ok(a.facts)
}

fn object<'v>(v: &'v Value, what: &str) -> Result<&'v Map<String, Value>> {
    v.as_object().with_context(|| format!("{what} is not a set of columns"))
}

fn list<'v>(root: &'v Value, name: &str) -> Result<&'v [Value]> {
    match root.get(name) {
        None => Ok(&[]),
        Some(v) => Ok(v.as_array().with_context(|| format!("{name} is not a list"))?.as_slice()),
    }
}

impl Applier<'_> {
    fn one(&mut self, p: &ModPatch) -> Result<()> {
        let root = p.patch;
        if root.get("patch").and_then(Value::as_u64) != Some(1) {
            bail!("not a patch of form 1");
        }
        if let Some(rows) = root.get("rows") {
            self.whole(p, rows.as_array().context("rows is not a list")?)?;
        }
        for (i, w) in list(root, "remove")?.iter().enumerate() {
            if self.positional {
                bail!("remove {}: rows of this table are named by place, which a remove would move (table-keys 3)", i + 1);
            }
            let Some(at) = self.find(p, w, &format!("remove {}", i + 1))? else { continue };
            let id = self.row_id(at);
            self.conflict_row(&id, p.mod_id);
            let mine = self.owners.cells.keys().filter(|(r, _)| *r == id).cloned().collect::<Vec<_>>();
            for k in mine {
                let earlier = self.owners.cells.remove(&k).unwrap_or_default();
                self.push_conflict(&id, None, earlier, p.mod_id);
            }
            self.table.rows.remove(at);
            self.owners.removed.insert(id, p.mod_id.to_string());
        }
        for (i, s) in list(root, "set")?.iter().enumerate() {
            let what = format!("set {}", i + 1);
            let w = s.get("where").with_context(|| format!("{what} has no where"))?;
            let values = object(s.get("values").with_context(|| format!("{what} has no values"))?, &what)?;
            let cells = self.cells(values, &what)?;
            if let Some(at) = self.find(p, w, &what)? {
                self.write(at, &cells, p.mod_id);
            }
        }
        for (i, row) in list(root, "add")?.iter().enumerate() {
            let what = format!("add {}", i + 1);
            let fields = object(row, &what)?;
            let mut cells = self.new_row();
            for (c, cell) in self.cells(fields, &what)? {
                cells[c] = cell;
            }
            if self.positional {
                self.table.rows.push(cells);
                let id = format!("#{}", self.table.rows.len() - 1);
                self.owners.rows.insert(id, p.mod_id.to_string());
                continue;
            }
            let id = self.key_text(&cells);
            match (0..self.table.rows.len()).find(|&r| self.row_id(r) == id) {
                // A row of that key is there: another mod added it (the validator refuses one the game has).
                Some(at) => {
                    let earlier = self.owners.rows.get(&id).cloned().unwrap_or_else(|| "the game".to_string());
                    self.push_conflict(&id, None, earlier, p.mod_id);
                    self.table.rows[at] = cells;
                }
                None => self.table.rows.push(cells),
            }
            self.owners.removed.remove(&id);
            self.owners.rows.insert(id, p.mod_id.to_string());
        }
        for (i, s) in list(root, "select")?.iter().enumerate() {
            let what = format!("select {}", i + 1);
            let wanted = object(s.get("match").with_context(|| format!("{what} has no match"))?, &what)?;
            let values = object(s.get("values").with_context(|| format!("{what} has no values"))?, &what)?;
            let cells = self.cells(values, &what)?;
            let mut tests = Vec::with_capacity(wanted.len());
            for (name, test) in wanted {
                let c = self.table.column_of(name)?.with_context(|| format!("{what}: no column {name}"))?;
                let one = object(test, &what)?;
                let (kind, value) = one.iter().next().filter(|_| one.len() == 1).with_context(|| format!("{what}: {name} takes one of equals, prefix, glob"))?;
                tests.push((c, kind.clone(), value.clone()));
            }
            let mut count = 0;
            for r in 0..self.table.rows.len() {
                if tests.iter().all(|(c, kind, value)| self.test(r, *c, kind, value)) {
                    count += 1;
                    self.write(r, &cells, p.mod_id);
                }
            }
            self.facts.push(Fact::Selected { table: self.path.clone(), mod_id: p.mod_id.into(), file: p.file.into(), selector: i, rows: count });
        }
        Ok(())
    }

    /// `rows`: the whole of a `#` table.
    fn whole(&mut self, p: &ModPatch, rows: &[Value]) -> Result<()> {
        if !self.positional {
            bail!("rows gives a whole table, which only a table named by place takes (table-keys 3)");
        }
        let earlier = self.owners.whole.clone().or_else(|| self.owners.rows.values().chain(self.owners.cells.values()).next().cloned());
        if let Some(earlier) = earlier {
            self.push_conflict("*", None, earlier, p.mod_id);
        }
        let mut whole = Vec::with_capacity(rows.len());
        for (i, row) in rows.iter().enumerate() {
            let what = format!("rows {}", i + 1);
            let mut cells = self.new_row();
            for (c, cell) in self.cells(object(row, &what)?, &what)? {
                cells[c] = cell;
            }
            whole.push(cells);
        }
        self.table.rows = whole;
        self.owners = Owners { whole: Some(p.mod_id.to_string()), ..Owners::default() };
        Ok(())
    }

    /// A new row before its values: each column the value most of the game's rows of this kind have (Obj_arg0 -1,
    /// scale_x 1), a word empty, 0 for a column of no kind.
    fn new_row(&self) -> Vec<Cell> {
        self.table.columns.iter().map(|c| self.kind.iter().find(|k| k.column.hash == c.hash).map_or_else(|| blank(c), |k| usual(c, &k.usual))).collect()
    }

    fn cells(&self, values: &Map<String, Value>, what: &str) -> Result<Vec<(usize, Cell)>> {
        let mut out = Vec::with_capacity(values.len());
        for (name, value) in values {
            let c = self.table.column_of(name)?.with_context(|| format!("{what}: no column {name}"))?;
            out.push((c, TableIr::cell(&self.table.columns[c], name, value)?));
        }
        Ok(out)
    }

    fn write(&mut self, at: usize, cells: &[(usize, Cell)], mod_id: &str) {
        let id = self.row_id(at);
        for (c, cell) in cells {
            let hash = self.table.columns[*c].hash;
            if let Some(earlier) = self.owners.cells.insert((id.clone(), hash), mod_id.to_string()) {
                if earlier != mod_id {
                    self.push_conflict(&id, Some(format!("0x{hash:08x}")), earlier, mod_id);
                }
            }
            self.table.rows[at][*c] = cell.clone();
        }
    }

    /// The row a `where` names, or None with a fact when it is not there.
    fn find(&mut self, p: &ModPatch, w: &Value, what: &str) -> Result<Option<usize>> {
        let w = object(w, what)?;
        let id = if self.positional {
            let n = w.get("#").and_then(Value::as_u64).filter(|_| w.len() == 1).with_context(|| format!("{what}: a row of this table is named by \"#\""))?;
            if (n as usize) < self.table.rows.len() {
                return Ok(Some(n as usize));
            }
            format!("#{n}")
        } else {
            let mut parts = Vec::with_capacity(self.key.len());
            if w.len() != self.key.len() {
                bail!("{what}: where names the key {}", self.key_names.join(", "));
            }
            for (name, &c) in self.key_names.iter().zip(&self.key) {
                let v = w.get(name).with_context(|| format!("{what}: where names the key {}", self.key_names.join(", ")))?;
                let cell = TableIr::cell(&self.table.columns[c], name, v)?;
                parts.push(self.cell_text(c, &cell));
            }
            let id = self.fold(parts.join("\u{1f}"));
            let found: Vec<usize> = (0..self.table.rows.len()).filter(|&r| self.row_id(r) == id).collect();
            // The game repeats a few keys (table-keys 4); a where then means the first, as the game reads it.
            if let Some(&at) = found.first() {
                return Ok(Some(at));
            }
            id
        };
        let removed_by = self.owners.removed.get(&id).cloned();
        self.facts.push(Fact::Missing { table: self.path.clone(), row: id, mod_id: p.mod_id.into(), file: p.file.into(), removed_by });
        Ok(None)
    }

    fn test(&self, r: usize, c: usize, kind: &str, value: &Value) -> bool {
        let column = &self.table.columns[c];
        let cell = &self.table.rows[r][c];
        match kind {
            "equals" => TableIr::cell_is(column, cell, value),
            "prefix" | "glob" => {
                let (Cell::Text(word), Some(pattern)) = (cell, value.as_str()) else { return false };
                if kind == "prefix" {
                    word.starts_with(pattern.as_bytes())
                } else {
                    glob(pattern.as_bytes(), word)
                }
            }
            _ => false,
        }
    }

    fn cell_text(&self, c: usize, cell: &Cell) -> String {
        cell_text(&self.table.columns[c], cell)
    }
}

/// A key cell as text, the way the validator writes a row's key (`Table::rows`): a whole number signed at its width
/// (a field unsigned), a word as itself or `{bytes:<hex>}` when it is not UTF-8.
pub fn cell_text(column: &super::table::Column, cell: &Cell) -> String {
    match cell {
        Cell::Bits(b) => match (column.kind, column.width) {
            (Kind::Int, 8) => (*b as u8 as i8).to_string(),
            (Kind::Int, 16) => (*b as u16 as i16).to_string(),
            (Kind::Int, _) => (*b as i32).to_string(),
            (Kind::Float, _) => f32::from_bits(*b).to_string(),
            _ => b.to_string(),
        },
        Cell::Text(t) => match std::str::from_utf8(t) {
            Ok(s) => s.to_string(),
            Err(_) => format!("{{bytes:{}}}", t.iter().map(|b| format!("{b:02x}")).collect::<String>()),
        },
    }
}

impl Applier<'_> {
    fn fold(&self, id: String) -> String {
        if self.case_insensitive {
            id.to_lowercase()
        } else {
            id
        }
    }

    fn key_text(&self, cells: &[Cell]) -> String {
        let parts: Vec<String> = self.key.iter().map(|&c| self.cell_text(c, &cells[c])).collect();
        self.fold(parts.join("\u{1f}"))
    }

    fn row_id(&self, at: usize) -> String {
        if self.positional {
            format!("#{at}")
        } else {
            self.key_text(&self.table.rows[at])
        }
    }

    fn conflict_row(&mut self, id: &str, later: &str) {
        if let Some(earlier) = self.owners.rows.get(id).cloned() {
            if earlier != later {
                self.push_conflict(id, None, earlier, later);
            }
        }
    }

    fn push_conflict(&mut self, row: &str, column: Option<String>, earlier: String, later: &str) {
        if earlier == later {
            return;
        }
        self.facts.push(Fact::Conflict { table: self.path.clone(), row: row.replace('\u{1f}', ", "), column, earlier, later: later.to_string() });
    }
}

fn blank(c: &Column) -> Cell {
    if c.kind == Kind::Text {
        Cell::Text(Vec::new())
    } else {
        Cell::Bits(0)
    }
}

/// A kind's usual value (as cell_text writes it) in this table's column; a value that does not read as the column's
/// form is no value (blank).
fn usual(c: &Column, text: &str) -> Cell {
    match c.kind {
        Kind::Text => Cell::Text(text.as_bytes().to_vec()),
        Kind::Float => text.parse::<f32>().map_or_else(|_| blank(c), |f| Cell::Bits(f.to_bits())),
        Kind::Int | Kind::Field => text.parse::<i64>().map_or_else(|_| blank(c), |n| Cell::Bits(n as u32 & c.mask)),
    }
}

/// `*` any run of bytes, `?` one byte.
fn glob(pattern: &[u8], word: &[u8]) -> bool {
    match pattern.split_first() {
        None => word.is_empty(),
        Some((b'*', rest)) => (0..=word.len()).any(|skip| glob(rest, &word[skip..])),
        Some((b'?', rest)) => !word.is_empty() && glob(rest, &word[1..]),
        Some((c, rest)) => word.first() == Some(c) && glob(rest, &word[1..]),
    }
}

#[cfg(test)]
mod tests {
    use super::super::table::{column_hash, Column};
    use super::*;
    use serde_json::json;

    fn objinfo() -> TableIr {
        let col = |name: &str, kind| Column { hash: column_hash(name), mask: u32::MAX, kind, width: 32 };
        let mut t = TableIr { columns: vec![col("l_id", Kind::Int), col("name", Kind::Text), col("speed", Kind::Float)], rows: vec![] };
        t.add_rows(&[json!({"l_id": 1, "name": "Kuribo", "speed": 1.0}), json!({"l_id": 2, "name": "Kuribo", "speed": 1.0}), json!({"l_id": 3, "name": "Takobo", "speed": 2.0})]).unwrap();
        t
    }

    const OBJINFO: &str = "StageData/AstroGalaxy/jmp/Placement/Common/ObjInfo";

    fn speed(t: &TableIr, r: usize) -> f32 {
        match t.rows[r][2] {
            Cell::Bits(b) => f32::from_bits(b),
            _ => panic!(),
        }
    }

    #[test]
    fn one_mod_sets_adds_removes_and_selects() {
        let mut t = objinfo();
        let p = json!({"patch": 1, "key": ["l_id"],
            "remove": [{"l_id": 3}],
            "set": [{"where": {"l_id": 1}, "values": {"speed": 2.5}}],
            "add": [{"l_id": 9, "name": "GoldKuribo", "speed": 3.0}],
            "select": [{"match": {"name": {"glob": "Kuri*"}}, "values": {"speed": 4.0}}, {"match": {"name": {"equals": "Nobody"}}, "values": {"speed": 1.0}}]});
        let facts = apply(OBJINFO, &mut t, &[ModPatch { mod_id: "a", file: "patches/x.json", patch: &p }]).unwrap();
        assert_eq!(t.rows.len(), 3);
        assert_eq!((speed(&t, 0), speed(&t, 1), speed(&t, 2)), (4.0, 4.0, 3.0));
        let selected: Vec<usize> = facts.iter().filter_map(|f| if let Fact::Selected { rows, .. } = f { Some(*rows) } else { None }).collect();
        assert_eq!(selected, vec![2, 0]);
        assert!(!facts.iter().any(|f| matches!(f, Fact::Conflict { .. } | Fact::Missing { .. })), "{facts:?}");
    }

    #[test]
    fn two_mods_on_one_cell_name_both_and_the_later_wins() {
        let mut t = objinfo();
        let a = json!({"patch": 1, "set": [{"where": {"l_id": 1}, "values": {"speed": 5.0}}]});
        let b = json!({"patch": 1, "set": [{"where": {"l_id": 1}, "values": {"speed": 6.0}}], "remove": [{"l_id": 2}]});
        let c = json!({"patch": 1, "set": [{"where": {"l_id": 2}, "values": {"speed": 7.0}}]});
        let facts = apply(OBJINFO, &mut t, &[
            ModPatch { mod_id: "a", file: "pa", patch: &a },
            ModPatch { mod_id: "b", file: "pb", patch: &b },
            ModPatch { mod_id: "c", file: "pc", patch: &c },
        ]).unwrap();
        assert_eq!(speed(&t, 0), 6.0);
        assert!(facts.contains(&Fact::Conflict { table: OBJINFO.into(), row: "1".into(), column: Some(format!("0x{:08x}", column_hash("speed"))), earlier: "a".into(), later: "b".into() }));
        assert!(facts.contains(&Fact::Missing { table: OBJINFO.into(), row: "2".into(), mod_id: "c".into(), file: "pc".into(), removed_by: Some("b".into()) }));
    }

    #[test]
    fn two_mods_adding_one_key_name_both() {
        let mut t = objinfo();
        let a = json!({"patch": 1, "add": [{"l_id": 9, "name": "A"}]});
        let b = json!({"patch": 1, "add": [{"l_id": 9, "name": "B"}]});
        let facts = apply(OBJINFO, &mut t, &[ModPatch { mod_id: "a", file: "pa", patch: &a }, ModPatch { mod_id: "b", file: "pb", patch: &b }]).unwrap();
        assert_eq!(t.rows.len(), 4);
        assert_eq!(t.rows[3][1], Cell::Text(b"B".to_vec()));
        assert!(facts.contains(&Fact::Conflict { table: OBJINFO.into(), row: "9".into(), column: None, earlier: "a".into(), later: "b".into() }));
    }

    #[test]
    fn rows_by_place_set_append_and_replace_whole() {
        let col = |name: &str, kind| Column { hash: column_hash(name), mask: u32::MAX, kind, width: 32 };
        let mut t = TableIr { columns: vec![col("frame", Kind::Int), col("value", Kind::Float)], rows: vec![] };
        t.add_rows(&[json!({"frame": 0, "value": 0.0}), json!({"frame": 10, "value": 1.0})]).unwrap();
        let path = "ObjectData/Kuribo/Move.pa";
        let a = json!({"patch": 1, "set": [{"where": {"#": 1}, "values": {"value": 2.0}}], "add": [{"frame": 20, "value": 3.0}]});
        let mut first = t.clone();
        assert!(apply(path, &mut first, &[ModPatch { mod_id: "a", file: "pa", patch: &a }]).unwrap().is_empty());
        assert_eq!(first.rows.len(), 3);
        let removing = json!({"patch": 1, "remove": [{"#": 0}]});
        let facts = apply(path, &mut t.clone(), &[ModPatch { mod_id: "r", file: "pr", patch: &removing }]).unwrap();
        assert!(matches!(&facts[..], [Fact::Failed { mod_id, .. }] if mod_id == "r"), "{facts:?}");
        let whole = json!({"patch": 1, "rows": [{"frame": 0, "value": 9.0}]});
        let facts = apply(path, &mut t, &[ModPatch { mod_id: "a", file: "pa", patch: &a }, ModPatch { mod_id: "w", file: "pw", patch: &whole }]).unwrap();
        assert_eq!(t.rows.len(), 1);
        assert!(facts.iter().any(|f| matches!(f, Fact::Conflict { earlier, later, .. } if earlier == "a" && later == "w")), "{facts:?}");
    }

    #[test]
    fn a_word_that_is_not_utf8_is_found_by_its_bytes() {
        let col = |name: &str, kind| Column { hash: column_hash(name), mask: u32::MAX, kind, width: 32 };
        let mut t = TableIr { columns: vec![col("id", Kind::Text), col("dist", Kind::Float)], rows: vec![] };
        t.rows.push(vec![Cell::Text(b"c:\x91".to_vec()), Cell::Bits(0)]);
        t.rows.push(vec![Cell::Text(b"c:\x92".to_vec()), Cell::Bits(0)]);
        let p = json!({"patch": 1, "set": [{"where": {"id": {"bytes": "633a92"}}, "values": {"dist": 1.5}}]});
        let facts = apply("StageData/AstroGalaxy/camera/CameraParam.bcam", &mut t, &[ModPatch { mod_id: "a", file: "pa", patch: &p }]).unwrap();
        assert!(facts.is_empty(), "{facts:?}");
        assert_eq!((t.rows[0][1].clone(), t.rows[1][1].clone()), (Cell::Bits(0), Cell::Bits(1.5f32.to_bits())));
    }

    #[test]
    fn an_empty_table_takes_its_kinds_columns_and_new_rows_its_usual_values() {
        // The game's empty generalpos files have only positions and directions: no PosName, no Obj_ID.
        let floats = ["pos_x", "pos_y", "pos_z", "dir_x", "dir_y", "dir_z"];
        let mut t = TableIr { columns: floats.iter().map(|n| Column { hash: column_hash(n), mask: u32::MAX, kind: Kind::Float, width: 32 }).collect(), rows: vec![] };
        let path = "StageData/PoleUnizoZone/jmp/GeneralPos/Common/GeneralPosInfo";
        let p = json!({"patch": 1, "key": ["PosName", "Obj_ID"], "add": [{"PosName": "Start", "Obj_ID": 5, "pos_x": 10.0}]});
        let facts = apply(path, &mut t, &[ModPatch { mod_id: "a", file: "pa", patch: &p }]).unwrap();
        assert!(facts.is_empty(), "{facts:?}");
        assert!(t.columns.len() > floats.len());
        let at = |name: &str| t.columns.iter().position(|c| c.hash == column_hash(name)).unwrap();
        assert_eq!(t.rows[0][at("PosName")], Cell::Text(b"Start".to_vec()));
        assert_eq!(t.rows[0][at("pos_x")], Cell::Bits(10.0f32.to_bits()));

        // A placed object that leaves its arguments out takes the game's -1, and its scale 1.
        let mut objinfo = TableIr { columns: vec![Column { hash: column_hash("l_id"), mask: u32::MAX, kind: Kind::Int, width: 32 }], rows: vec![] };
        let p = json!({"patch": 1, "key": ["l_id"], "add": [{"l_id": 1, "name": "Kuribo"}]});
        apply(OBJINFO, &mut objinfo, &[ModPatch { mod_id: "a", file: "pa", patch: &p }]).unwrap();
        let at = |name: &str| objinfo.columns.iter().position(|c| c.hash == column_hash(name)).unwrap();
        assert_eq!(objinfo.rows[0][at("Obj_arg0")], Cell::Bits(u32::MAX));
        assert_eq!(objinfo.rows[0][at("scale_x")], Cell::Bits(1.0f32.to_bits()));
        assert_eq!(objinfo.rows[0][at("pos_x")], Cell::Bits(0));

        // A table with rows keeps its columns: a column it lacks is refused, naming the mod.
        let mut t = objinfo.clone();
        let gone = at("Obj_arg7");
        t.columns.remove(gone);
        for row in &mut t.rows {
            row.remove(gone);
        }
        let p = json!({"patch": 1, "set": [{"where": {"l_id": 1}, "values": {"Obj_arg7": 3}}]});
        let facts = apply(OBJINFO, &mut t, &[ModPatch { mod_id: "m", file: "pm", patch: &p }]).unwrap();
        assert!(matches!(&facts[..], [Fact::Failed { why, .. }] if why.contains("Obj_arg7")), "{facts:?}");
    }

    #[test]
    fn a_patch_that_cannot_apply_is_left_out_whole_and_named() {
        let mut t = objinfo();
        let before = t.clone();
        let good = json!({"patch": 1, "set": [{"where": {"l_id": 1}, "values": {"speed": 9.0}}]});
        // Its first change applies, its second cannot: neither is kept.
        let bad = json!({"patch": 1, "set": [{"where": {"l_id": 2}, "values": {"speed": 5.0}}, {"where": {"name": "Kuribo"}, "values": {"speed": 1.0}}]});
        let facts = apply(OBJINFO, &mut t, &[ModPatch { mod_id: "m", file: "layers/base/patches/x.json", patch: &bad }, ModPatch { mod_id: "g", file: "pg", patch: &good }]).unwrap();
        assert_eq!(facts.len(), 1, "{facts:?}");
        let Fact::Failed { mod_id, file, why, .. } = &facts[0] else { panic!("{facts:?}") };
        assert_eq!((mod_id.as_str(), file.as_str()), ("m", "layers/base/patches/x.json"));
        assert!(why.contains("l_id"), "{why}");
        assert_eq!(speed(&t, 1), speed(&before, 1));
        assert_eq!(speed(&t, 0), 9.0);
        // A table its rule cannot read is an error for the caller.
        let mut t = TableIr { columns: before.columns[1..].to_vec(), rows: before.rows.iter().map(|r| r[1..].to_vec()).collect() };
        assert!(apply(OBJINFO, &mut t, &[ModPatch { mod_id: "g", file: "pg", patch: &good }]).is_err());
    }
}
