//! Patches by key (luma.md 5.1, docs/modding/format/table-keys.md): a game table and the patches of every mod that is
//! on, from the bottom of the mod list up, make the table the game reads. Each mod's patch applies in this order: `rows` (a whole `#`
//! table), `remove`, `set`, `add`, `select`. Patches reach here checked by the validator; what only the mod list
//! decides (two mods changing one cell, a row another mod removed) is a [`Fact`] for the Mods screen, never silent.
//! The last given wins: the mod higher in the list, as for whole files. A table of a kind (cook::table_columns) that
//! has no rows takes its kind's columns first, and a row a mod adds starts from the value most of the game's rows have
//! in each column (table-keys 10).

use super::table::{Cell, Column, Kind, TableIr};
use super::table_columns::{columns_of, widen_empty, KindColumn};
use super::table_keys::{rule_for, Key};
use anyhow::{Context, Result};
use luma_validate::{Arg, ColumnType, Finding, Level};
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
    /// Two mods change one row (`column` None: the whole row, by add, remove or rows); `higher` is used.
    Conflict { table: String, row: String, column: Option<String>, lower: String, higher: String },
    /// A row a patch names is not there: `removed_by` removed it, or (None) the `rows` of a mod given before it (lower in the list) left it out.
    Missing { table: String, row: String, mod_id: String, file: String, removed_by: Option<String> },
    /// How many rows a mod's selector matched (0 is a Note: L407).
    Selected { table: String, mod_id: String, file: String, selector: usize, rows: usize },
    /// A mod's patch could not apply (the validator passed it against another game, or the mod changed since): the
    /// whole patch is left out, the other mods' still apply. `refusal` is the validator's finding for it (its code and
    /// parameters, said in any language by `luma_validate::reason`); `why` is that in English, for the log.
    Failed { table: String, mod_id: String, file: String, refusal: Finding, why: String },
}

/// Why a patch cannot apply, as the validator names the same fault (its L400, L402, L403 and L406 with the same
/// phrases and parameters), so the Mods screen says it in the player's language. The applier's only
/// error kind for a patch; any other error is about the game's table and goes to the caller.
#[derive(Debug)]
pub struct Refusal(pub Finding);

impl std::fmt::Display for Refusal {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "L{} {}", self.0.code, luma_validate::reason(&self.0, "UsEnglish"))
    }
}

impl std::error::Error for Refusal {}

pub(crate) fn refuse(code: u16, args: Vec<(&'static str, Arg)>) -> anyhow::Error {
    anyhow::Error::new(Refusal(Finding { code, level: Level::Error, mod_id: None, file: String::new(), line: None, column: None, pointer: None, args }))
}

/// A patch file that is not JSON: left out whole, named as the validator's L400 names it (Json.Truncated,
/// Json.Comment...).
pub fn unreadable(table: &str, mod_id: &str, file: &str, json: &[u8]) -> Fact {
    let (what, detail) = match luma_validate::json::parse(json) {
        Err(e) => (e.what, e.detail),
        Ok(_) => ("Json.Unreadable", String::new()),
    };
    let refusal = Finding {
        code: 400,
        level: Level::Error,
        mod_id: Some(mod_id.to_string()),
        file: file.to_string(),
        line: None,
        column: None,
        pointer: None,
        args: vec![("found", Arg::Phrase(what)), ("detail", Arg::Text(detail))],
    };
    let why = luma_validate::reason(&refusal, "UsEnglish");
    Fact::Failed { table: table.to_string(), mod_id: mod_id.to_string(), file: file.to_string(), refusal, why }
}

/// L400 with a shape phrase, as the validator's `bad`.
pub(crate) fn shape(why: &'static str, detail: impl Into<String>) -> anyhow::Error {
    refuse(400, vec![("found", Arg::Phrase(why)), ("detail", Arg::Text(detail.into()))])
}

fn text(s: impl Into<String>) -> Arg {
    Arg::Text(s.into())
}

/// A value as the validator quotes it in Patch.ValueType: text in quotation marks, a number as written, else its kind.
fn found(v: &Value) -> Arg {
    match v {
        Value::String(s) => text(format!("\"{s}\"")),
        Value::Number(n) => text(n.to_string()),
        Value::Null => Arg::Phrase("Type.Null"),
        Value::Bool(_) => Arg::Phrase("Type.Bool"),
        Value::Array(_) => Arg::Phrase("Type.Array"),
        Value::Object(_) => Arg::Phrase("Type.Object"),
    }
}

/// Another table of a table's link group (table-keys 13): one zone's tables of one kind in its other layers, with the
/// patches of the mods that are on for it, in the order a table's are given.
pub struct Link<'a> {
    pub path: &'a str,
    pub table: &'a TableIr,
    pub patches: &'a [ModPatch<'a>],
}

/// What the other tables of a group hold: their game keys, and the keys mods add to them.
#[derive(Default)]
struct Group {
    /// A key the game has in another layer, and that table's path.
    game: HashMap<String, String>,
    /// A key mods add to another layer: each mod and the table.
    adds: HashMap<String, Vec<(String, String)>>,
    /// Each mod's place in the list, from the bottom up (higher is higher in the list).
    rank: HashMap<String, usize>,
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
    group: Group,
}

/// The table with every patch applied, and the facts. A patch that cannot apply is left out whole (Fact::Failed, naming
/// the mod and the file); an error is a game table its rule cannot read (a key column missing from a table with rows).
pub fn apply(path: &str, table: &mut TableIr, patches: &[ModPatch]) -> Result<Vec<Fact>> {
    apply_linked(path, table, patches, &[], &[])
}

/// `apply` for a table of a link group (table-keys 13), given the group's other tables with their patches and every
/// mod that is on from the bottom of the list up. A key a patch adds that the game has in another layer leaves that
/// patch out (L402 Patch.KeyInGroup, as the validator); a key a higher mod adds to another layer leaves this row out
/// (a conflict fact, the higher's row stays); a key the same mod adds to another layer leaves the patch out too.
pub fn apply_linked(path: &str, table: &mut TableIr, patches: &[ModPatch], links: &[Link], mods: &[&str]) -> Result<Vec<Fact>> {
    let mut group = Group { rank: mods.iter().enumerate().map(|(i, m)| (m.to_string(), i)).collect(), ..Group::default() };
    if !patches.is_empty() {
        // Which of two mods adding one key in the group is higher comes from `mods` alone: a patch of a mod
        // it does not name cannot be ordered, so the input is not valid, never guessed.
        if links.iter().any(|l| !l.patches.is_empty()) {
            let unnamed = patches.iter().map(|p| (p.mod_id, path)).chain(links.iter().flat_map(|l| l.patches.iter().map(move |p| (p.mod_id, l.path))));
            for (mod_id, at) in unnamed {
                if !group.rank.contains_key(mod_id) {
                    anyhow::bail!("{mod_id}: its patch of {at} is from a mod the mod list (`mods`) does not name, so which mod is higher cannot be known");
                }
            }
        }
        for link in links {
            let mut other = link.table.clone();
            let la = Applier::open(link.path, &mut other, true)?;
            if la.positional {
                continue;
            }
            for r in 0..la.table.rows.len() {
                group.game.entry(la.row_id(r)).or_insert_with(|| la.path.clone());
            }
            for p in link.patches {
                let rows = p.patch.get("add").and_then(Value::as_array).map_or(&[][..], Vec::as_slice);
                for row in rows {
                    // A row that cannot be read is that patch's fault, named in its own table's call.
                    let Some(Ok(cells)) = row.as_object().map(|fields| la.cells(fields)) else { continue };
                    let mut full = la.new_row();
                    for (c, cell) in cells {
                        full[c] = cell;
                    }
                    group.adds.entry(la.key_text(&full)).or_default().push((p.mod_id.to_string(), la.path.clone()));
                }
            }
        }
    }
    let mut a = Applier::open(path, table, !patches.is_empty())?;
    a.group = group;
    for p in patches {
        let (table, owners, facts) = (a.table.clone(), a.owners.clone(), a.facts.len());
        if let Err(e) = a.one(p) {
            *a.table = table;
            a.owners = owners;
            a.facts.truncate(facts);
            let mut refusal = match e.downcast::<Refusal>() {
                Ok(Refusal(f)) => f,
                Err(other) => return Err(other),
            };
            refusal.mod_id = Some(p.mod_id.into());
            refusal.file = p.file.into();
            let why = luma_validate::reason(&refusal, "UsEnglish");
            a.facts.push(Fact::Failed { table: a.path.clone(), mod_id: p.mod_id.into(), file: p.file.into(), refusal, why });
        }
    }
    Ok(a.facts)
}

impl<'t> Applier<'t> {
    /// The applier of one table: its store path, its rule's key, its kind's columns. `widen`: an empty table takes its
    /// kind's columns first (when a patch will add to it, or to read a group's keys).
    fn open(path: &str, table: &'t mut TableIr, widen: bool) -> Result<Self> {
        // A disc path names the same table as its store path (an archive is a folder of the store).
        let store_path = super::store::store_path(path.trim_start_matches('/'));
        let path = store_path.as_str();
        let rule = rule_for(path);
        let kind = columns_of(path).unwrap_or(&[]);
        if widen {
            // An empty layer's table often has only a few of its kind's columns (21 generalpos files lack even their
            // key): it takes the rest, so a row added to it keeps every value (no row is there to read one it never had).
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
        Ok(Applier {
            path: path.to_string(),
            table,
            key,
            key_names,
            positional,
            case_insensitive: rule.case_insensitive,
            kind,
            owners: Owners::default(),
            facts: Vec::new(),
            group: Group::default(),
        })
    }
}

fn object(v: &Value, refused: impl FnOnce() -> anyhow::Error) -> Result<&Map<String, Value>> {
    v.as_object().ok_or_else(refused)
}

fn list<'v>(root: &'v Value, name: &str) -> Result<&'v [Value]> {
    match root.get(name) {
        None => Ok(&[]),
        Some(v) => Ok(v.as_array().ok_or_else(|| shape("Patch.NotArray", name))?.as_slice()),
    }
}

impl Applier<'_> {
    fn one(&mut self, p: &ModPatch) -> Result<()> {
        let root = p.patch;
        let root_object = object(root, || shape("Patch.NotObject", ""))?;
        match root_object.get("patch") {
            None => return Err(shape("Patch.NoVersion", "")),
            Some(v) if v.as_u64() != Some(1) => return Err(shape("Patch.Version", v.to_string())),
            Some(_) => {}
        }
        // A stage's placement rows may give their objects' arguments by name (stages.md section 1).
        let expanded = self.placement_rows(root)?;
        let root = expanded.as_ref().unwrap_or(root);
        if let Some(rows) = root.get("rows") {
            self.whole(p, rows.as_array().ok_or_else(|| shape("Patch.NotArray", "rows"))?)?;
        }
        for w in list(root, "remove")? {
            if self.positional {
                return Err(refuse(402, vec![("why", Arg::Phrase("Patch.RemoveByPlace")), ("row", text("")), ("count", text(""))]));
            }
            let Some(at) = self.find(p, w)? else { continue };
            let id = self.row_id(at);
            self.conflict_row(&id, p.mod_id);
            let mine = self.owners.cells.keys().filter(|(r, _)| *r == id).cloned().collect::<Vec<_>>();
            for k in mine {
                let lower = self.owners.cells.remove(&k).unwrap_or_default();
                self.push_conflict(&id, None, lower, p.mod_id);
            }
            self.table.rows.remove(at);
            self.owners.removed.insert(id, p.mod_id.to_string());
        }
        for (i, s) in list(root, "set")?.iter().enumerate() {
            let set_shape = || shape("Patch.SetShape", "");
            let w = s.get("where").ok_or_else(set_shape)?;
            let values = object(s.get("values").ok_or_else(set_shape)?, set_shape)?;
            if values.contains_key("args") && self.is_placement() {
                // Arguments by name for the row's own object; only the arguments named change.
                let Some(at) = self.find(p, w)? else { continue };
                let mut named = values.clone();
                let object = self.text_of(at, "name");
                let facts = super::placement_args::objects()?;
                super::placement_args::expand_fields(&mut named, &format!("/set/{i}/values"), &object, Some(&facts), false)?;
                let cells = self.cells(&named)?;
                self.write(at, &cells, &named, p.mod_id);
                continue;
            }
            let cells = self.cells(values)?;
            if let Some(at) = self.find(p, w)? {
                self.write(at, &cells, values, p.mod_id);
            }
        }
        for row in list(root, "add")? {
            let fields = object(row, || shape("Patch.RowShape", ""))?;
            let mut cells = self.new_row();
            for (c, cell) in self.cells(fields)? {
                cells[c] = cell;
            }
            if self.positional {
                self.table.rows.push(cells);
                let id = format!("#{}", self.table.rows.len() - 1);
                self.owners.rows.insert(id, p.mod_id.to_string());
                continue;
            }
            let id = self.key_text(&cells);
            // Table-keys 13: the key is new to the zone's other layers, or a higher mod's row there stays.
            let in_group = |there: &String| refuse(402, vec![("why", Arg::Phrase("Patch.KeyInGroup")), ("row", text(id.replace('\u{1f}', ", "))), ("where", text(there.clone()))]);
            if let Some(there) = self.group.game.get(&id) {
                return Err(in_group(there));
            }
            if let Some(adders) = self.group.adds.get(&id) {
                if let Some((_, there)) = adders.iter().find(|(m, _)| m == p.mod_id) {
                    return Err(in_group(there));
                }
                let rank = |m: &str| self.group.rank.get(m).copied();
                let mine = rank(p.mod_id);
                let higher = adders.iter().map(|(m, _)| m).filter(|m| rank(m) > mine).max_by_key(|m| rank(m)).cloned();
                if let Some(higher) = higher {
                    self.push_conflict(&id, None, p.mod_id.to_string(), &higher);
                    continue;
                }
            }
            match (0..self.table.rows.len()).find(|&r| self.row_id(r) == id) {
                // A row of that key is there: another mod added it (the validator refuses one the game has).
                Some(at) => {
                    let lower = self.owners.rows.get(&id).cloned().unwrap_or_else(|| "the game".to_string());
                    self.push_conflict(&id, None, lower, p.mod_id);
                    self.table.rows[at] = cells;
                }
                None => self.table.rows.push(cells),
            }
            self.owners.removed.remove(&id);
            self.owners.rows.insert(id, p.mod_id.to_string());
        }
        for (i, s) in list(root, "select")?.iter().enumerate() {
            let select_shape = || shape("Patch.SelectShape", "");
            let wanted = object(s.get("match").ok_or_else(select_shape)?, select_shape)?;
            let values = object(s.get("values").ok_or_else(select_shape)?, select_shape)?;
            let cells = self.cells(values)?;
            let mut tests = Vec::with_capacity(wanted.len());
            for (name, test) in wanted {
                let match_kind = || refuse(406, vec![("why", Arg::Phrase("Select.MatchKind")), ("column", text(name.clone()))]);
                let c = self.column(name).ok_or_else(|| refuse(406, vec![("why", Arg::Phrase("Select.NoSuchColumn")), ("column", text(name.clone()))]))?;
                let one = object(test, match_kind)?;
                let (kind, value) = one.iter().next().filter(|_| one.len() == 1).ok_or_else(match_kind)?;
                tests.push((c, kind.clone(), value.clone()));
            }
            let mut count = 0;
            for r in 0..self.table.rows.len() {
                if tests.iter().all(|(c, kind, value)| self.test(r, *c, kind, value)) {
                    count += 1;
                    self.write(r, &cells, values, p.mod_id);
                }
            }
            self.facts.push(Fact::Selected { table: self.path.clone(), mod_id: p.mod_id.into(), file: p.file.into(), selector: i, rows: count });
        }
        Ok(())
    }

    /// `rows`: the whole of a `#` table.
    /// A stage's placement table (`StageData/<Stage>/jmp/...`), whose rows may give arguments by name.
    fn is_placement(&self) -> bool {
        self.path.to_ascii_lowercase().contains("/jmp/")
    }

    /// The patch with its `rows` and `add` rows' `args` as `Obj_arg<n>` (placement_args), None when it has none.
    fn placement_rows(&self, root: &Value) -> Result<Option<Value>> {
        let has = |k: &str| root.get(k).and_then(Value::as_array).is_some_and(|rows| super::placement_args::any_args(rows));
        if !self.is_placement() || !(has("rows") || has("add")) {
            return Ok(None);
        }
        let facts = super::placement_args::objects()?;
        let mut copy = root.clone();
        for k in ["rows", "add"] {
            if let Some(rows) = copy.get_mut(k).and_then(Value::as_array_mut) {
                super::placement_args::expand_rows(rows, &format!("/{k}"), Some(&facts), false)?;
            }
        }
        Ok(Some(copy))
    }

    /// A row's word in the column `name` as text ("" when the table has no such column).
    fn text_of(&self, row: usize, name: &str) -> String {
        match self.table.column_of(name).ok().flatten().and_then(|c| self.table.rows.get(row)?.get(c)) {
            Some(Cell::Text(bytes)) => String::from_utf8_lossy(bytes).into_owned(),
            _ => String::new(),
        }
    }

    fn whole(&mut self, p: &ModPatch, rows: &[Value]) -> Result<()> {
        if !self.positional {
            return Err(refuse(402, vec![("why", Arg::Phrase("Patch.RowsNotByPlace")), ("row", text("")), ("count", text(""))]));
        }
        let lower = self.owners.whole.clone().or_else(|| self.owners.rows.values().chain(self.owners.cells.values()).next().cloned());
        if let Some(lower) = lower {
            self.push_conflict("*", None, lower, p.mod_id);
        }
        let mut whole = Vec::with_capacity(rows.len());
        for row in rows {
            let mut cells = self.new_row();
            for (c, cell) in self.cells(object(row, || shape("Patch.RowShape", ""))?)? {
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

    /// A column by name; None for one the table lacks (or a name no column could have).
    fn column(&self, name: &str) -> Option<usize> {
        self.table.column_of(name).ok().flatten()
    }

    fn cells(&self, values: &Map<String, Value>) -> Result<Vec<(usize, Cell)>> {
        let mut out = Vec::with_capacity(values.len());
        for (name, value) in values {
            let c = self.column(name).ok_or_else(|| {
                refuse(403, vec![("why", Arg::Phrase("Patch.NoSuchColumn")), ("column", text(name.clone())), ("target", text(self.path.clone()))])
            })?;
            out.push((c, self.cell(c, name, value)?));
        }
        Ok(out)
    }

    /// A value as a cell of column `c`, or the validator's L403 for a value the column cannot hold.
    fn cell(&self, c: usize, name: &str, value: &Value) -> Result<Cell> {
        let column = &self.table.columns[c];
        TableIr::cell(column, name, value).map_err(|_| {
            if value.get("bytes").is_some() {
                return refuse(403, vec![("why", Arg::Phrase("Patch.BytesShape")), ("column", text(name))]);
            }
            let mut args = vec![("why", Arg::Phrase("Patch.ValueType")), ("column", text(name)), ("found", found(value))];
            match crate::store_view::column_type(column) {
                ColumnType::Int { min, max } => {
                    args.push(("expected", Arg::Phrase("Column.Int")));
                    args.push(("min", text(min.to_string())));
                    args.push(("max", text(max.to_string())));
                }
                ColumnType::Float => args.push(("expected", Arg::Phrase("Column.Float"))),
                ColumnType::Text => args.push(("expected", Arg::Phrase("Column.Text"))),
            }
            refuse(403, args)
        })
    }

    /// `values` are the patch's, which `cells` came from: a conflict names the column as the patch wrote it.
    fn write(&mut self, at: usize, cells: &[(usize, Cell)], values: &Map<String, Value>, mod_id: &str) {
        let id = self.row_id(at);
        for (c, cell) in cells {
            let hash = self.table.columns[*c].hash;
            if let Some(lower) = self.owners.cells.insert((id.clone(), hash), mod_id.to_string()) {
                if lower != mod_id {
                    // `cells` holds only columns `values` names (`Applier::cells`).
                    let name = values.keys().find(|n| self.column(n) == Some(*c)).cloned().unwrap_or_else(|| format!("0x{hash:08x}"));
                    self.push_conflict(&id, Some(name), lower, mod_id);
                }
            }
            self.table.rows[at][*c] = cell.clone();
        }
    }

    /// The row a `where` names, or None with a fact when it is not there.
    fn find(&mut self, p: &ModPatch, w: &Value) -> Result<Option<usize>> {
        let not_key = || shape("Patch.WhereNotKey", if self.positional { "#".to_string() } else { self.key_names.join(", ") });
        let w = object(w, not_key)?;
        let id = if self.positional {
            let n = w.get("#").and_then(Value::as_u64).filter(|_| w.len() == 1).ok_or_else(not_key)?;
            if (n as usize) < self.table.rows.len() {
                return Ok(Some(n as usize));
            }
            format!("#{n}")
        } else {
            let mut parts = Vec::with_capacity(self.key.len());
            if w.len() != self.key.len() {
                return Err(not_key());
            }
            for (name, &c) in self.key_names.iter().zip(&self.key) {
                let v = w.get(name).ok_or_else(not_key)?;
                let cell = TableIr::cell(&self.table.columns[c], name, v)
                    .map_err(|_| refuse(403, vec![("why", Arg::Phrase("Patch.KeyValue")), ("column", text(name.clone()))]))?;
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

    fn conflict_row(&mut self, id: &str, higher: &str) {
        if let Some(lower) = self.owners.rows.get(id).cloned() {
            if lower != higher {
                self.push_conflict(id, None, lower, higher);
            }
        }
    }

    fn push_conflict(&mut self, row: &str, column: Option<String>, lower: String, higher: &str) {
        if lower == higher {
            return;
        }
        // The row as every fact gives it, key values joined by U+001F (luma_patch.h); the screen shows them.
        self.facts.push(Fact::Conflict { table: self.path.clone(), row: row.to_string(), column, lower, higher: higher.to_string() });
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

    /// stages.md section 1: `args` by name in a placement table's `add` and `set`; a wrong name fails that patch alone.
    #[test]
    fn placement_rows_take_arguments_by_name() {
        let _file = super::super::placement_args::TEST_FILE.lock().unwrap_or_else(|e| e.into_inner());
        let dir = tempfile::tempdir().unwrap();
        let description = json!({"factory": [{"name": "Kuribo", "class": "Kuribo"}],
            "placement_args": {"Kuribo": [{"arg": 0, "target": "mSpeed", "type": "f32"}, {"arg": 2, "target": "mIsSleep", "type": "bool"}]}});
        std::fs::write(dir.path().join(super::super::placement_args::FILE_NAME), description.to_string()).unwrap();
        super::super::placement_args::use_file(dir.path());
        let col = |name: &str| Column { hash: column_hash(name), mask: u32::MAX, kind: if name == "name" { Kind::Text } else { Kind::Int }, width: 32 };
        let mut t = TableIr { columns: ["l_id", "name", "Obj_arg0", "Obj_arg1", "Obj_arg2"].iter().map(|n| col(n)).collect(), rows: vec![] };
        t.add_rows(&[json!({"l_id": 1, "name": "Kuribo", "Obj_arg0": 5, "Obj_arg1": 7, "Obj_arg2": -1})]).unwrap();
        let args = |t: &TableIr, r: usize| -> Vec<i32> {
            (2..5).map(|c| match t.rows[r][c] { Cell::Bits(b) => b as i32, _ => panic!() }).collect()
        };
        let good = json!({"patch": 1,
            "set": [{"where": {"l_id": 1}, "values": {"args": {"IsSleep": true}}}],
            "add": [{"l_id": 9, "name": "Kuribo", "args": {"Speed": 3}}]});
        let facts = apply(OBJINFO, &mut t, &[ModPatch { mod_id: "a", file: "pa", patch: &good }]).unwrap();
        assert!(facts.is_empty(), "{facts:?}");
        assert_eq!(args(&t, 0), vec![5, 7, 1], "set: only IsSleep changes");
        assert_eq!(args(&t, 1), vec![3, -1, -1], "add: the rest not given");
        let before = t.clone();
        let bad = json!({"patch": 1, "add": [{"l_id": 10, "name": "Kuribo", "args": {"Sleepy": 1}}]});
        let facts = apply(OBJINFO, &mut t, &[ModPatch { mod_id: "b", file: "pb", patch: &bad }]).unwrap();
        assert_eq!(t, before);
        let [Fact::Failed { mod_id, refusal, why, .. }] = facts.as_slice() else { panic!("{facts:?}") };
        assert_eq!(mod_id, "b");
        assert_eq!(refusal.code, 804);
        assert!(why.contains("Kuribo has no argument \"Sleepy\"; its arguments are Speed, IsSleep, Arg0-Arg7"), "{why}");
    }

    #[test]
    fn two_mods_on_one_cell_name_both_and_the_higher_wins() {
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
        assert!(facts.contains(&Fact::Conflict { table: OBJINFO.into(), row: "1".into(), column: Some("speed".into()), lower: "a".into(), higher: "b".into() }));
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
        assert!(facts.contains(&Fact::Conflict { table: OBJINFO.into(), row: "9".into(), column: None, lower: "a".into(), higher: "b".into() }));
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
        assert!(facts.iter().any(|f| matches!(f, Fact::Conflict { lower, higher, .. } if lower == "a" && higher == "w")), "{facts:?}");
    }

    #[test]
    fn a_conflict_names_the_row_by_its_key_values_and_the_column_by_name() {
        // audits/3.11.md P-2 and P-3: key values joined by U+001F, a word that is not UTF-8 as {bytes:<hex>}, a
        // whole number signed at its width; the column as the patch wrote it.
        let floats = ["pos_x", "pos_y", "pos_z", "dir_x", "dir_y", "dir_z"];
        let mut t = TableIr { columns: floats.iter().map(|n| Column { hash: column_hash(n), mask: u32::MAX, kind: Kind::Float, width: 32 }).collect(), rows: vec![] };
        let path = "StageData/PoleUnizoZone/jmp/GeneralPos/Common/GeneralPosInfo";
        let a = json!({"patch": 1, "key": ["PosName", "Obj_ID"], "add": [{"PosName": "Start", "Obj_ID": -5}]});
        let b = json!({"patch": 1, "key": ["PosName", "Obj_ID"], "set": [{"where": {"PosName": "Start", "Obj_ID": -5}, "values": {"pos_x": 3.0}}]});
        let c = json!({"patch": 1, "key": ["PosName", "Obj_ID"], "set": [{"where": {"PosName": "Start", "Obj_ID": -5}, "values": {"pos_x": 4.0}}]});
        let facts = apply(path, &mut t, &[
            ModPatch { mod_id: "a", file: "pa", patch: &a },
            ModPatch { mod_id: "b", file: "pb", patch: &b },
            ModPatch { mod_id: "c", file: "pc", patch: &c },
        ]).unwrap();
        let conflict = facts.iter().find_map(|f| match f {
            Fact::Conflict { row, column, lower, higher, .. } if lower == "b" && higher == "c" => Some((row.clone(), column.clone())),
            _ => None,
        });
        let (row, column) = conflict.unwrap_or_else(|| panic!("{facts:?}"));
        assert_eq!(row.to_lowercase(), "start\u{1f}-5");
        assert_eq!(column.as_deref(), Some("pos_x"));

        let col = |name: &str, kind| Column { hash: column_hash(name), mask: u32::MAX, kind, width: 32 };
        let mut t = TableIr { columns: vec![col("id", Kind::Text), col("dist", Kind::Float)], rows: vec![vec![Cell::Text(b"c:\x92".to_vec()), Cell::Bits(0)]] };
        let set = |v: f64| json!({"patch": 1, "set": [{"where": {"id": {"bytes": "633a92"}}, "values": {"dist": v}}]});
        let (low, high) = (set(1.0), set(2.0));
        let facts = apply("StageData/AstroGalaxy/camera/CameraParam.bcam", &mut t, &[ModPatch { mod_id: "l", file: "pl", patch: &low }, ModPatch { mod_id: "h", file: "ph", patch: &high }]).unwrap();
        assert_eq!(facts, vec![Fact::Conflict { table: "StageData/AstroGalaxy/camera/CameraParam.bcam".into(), row: "{bytes:633a92}".into(), column: Some("dist".into()), lower: "l".into(), higher: "h".into() }]);
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
        // The validator's finding: L403 Patch.NoSuchColumn, said in each language with the column's name.
        let Fact::Failed { refusal, .. } = &facts[0] else { unreachable!() };
        assert_eq!((refusal.code, refusal.mod_id.as_deref(), refusal.file.as_str()), (403, Some("m"), "pm"));
        assert_eq!(luma_validate::reason(refusal, "UsEnglish"), format!("\"{OBJINFO}\" has no column \"Obj_arg7\""));
        for language in ["EuFrench", "JpJapanese", "KrKorean"] {
            let said = luma_validate::reason(refusal, language);
            assert!(said.contains("Obj_arg7") && !said.contains('{') && said != luma_validate::reason(refusal, "UsEnglish"), "{language}: {said}");
        }
    }

    #[test]
    fn every_fault_is_the_validators_finding_in_every_language() {
        let keyed = |patch: Value| {
            let mut t = objinfo();
            let facts = apply(OBJINFO, &mut t, &[ModPatch { mod_id: "m", file: "f.json", patch: &patch }]).unwrap();
            let [Fact::Failed { refusal, .. }] = &facts[..] else { panic!("{patch}: {facts:?}") };
            refusal.clone()
        };
        let col = |name: &str, kind| Column { hash: column_hash(name), mask: u32::MAX, kind, width: 32 };
        let by_place = |patch: Value| {
            let mut t = TableIr { columns: vec![col("frame", Kind::Int)], rows: vec![] };
            t.add_rows(&[json!({"frame": 0})]).unwrap();
            let facts = apply("ObjectData/Kuribo/Move.pa", &mut t, &[ModPatch { mod_id: "m", file: "f.json", patch: &patch }]).unwrap();
            let [Fact::Failed { refusal, .. }] = &facts[..] else { panic!("{patch}: {facts:?}") };
            refusal.clone()
        };
        let set = |w: Value, values: Value| json!({"patch": 1, "key": ["l_id"], "set": [{"where": w, "values": values}]});
        let cases: Vec<(luma_validate::Finding, u16, &str)> = vec![
            (keyed(json!([1])), 400, "Patch.NotObject"),
            (keyed(json!({"key": ["l_id"]})), 400, "Patch.NoVersion"),
            (keyed(json!({"patch": 2})), 400, "Patch.Version"),
            (keyed(json!({"patch": 1, "set": 5})), 400, "Patch.NotArray"),
            (keyed(json!({"patch": 1, "rows": []})), 402, "Patch.RowsNotByPlace"),
            (by_place(json!({"patch": 1, "remove": [{"#": 0}]})), 402, "Patch.RemoveByPlace"),
            (by_place(json!({"patch": 1, "set": [{"where": {"frame": 0}, "values": {"frame": 1}}]})), 400, "Patch.WhereNotKey"),
            (keyed(json!({"patch": 1, "set": [{"where": {"l_id": 1}}]})), 400, "Patch.SetShape"),
            (keyed(json!({"patch": 1, "add": [7]})), 400, "Patch.RowShape"),
            (keyed(json!({"patch": 1, "select": [{"values": {"speed": 1.0}}]})), 400, "Patch.SelectShape"),
            (keyed(json!({"patch": 1, "select": [{"match": {"colour": {"equals": 1}}, "values": {"speed": 1.0}}]})), 406, "Select.NoSuchColumn"),
            (keyed(json!({"patch": 1, "select": [{"match": {"name": {"equals": "a", "glob": "b"}}, "values": {"speed": 1.0}}]})), 406, "Select.MatchKind"),
            (keyed(set(json!({"l_id": 1}), json!({"colour": 1}))), 403, "Patch.NoSuchColumn"),
            (keyed(set(json!({"l_id": 1}), json!({"l_id": "one"}))), 403, "Patch.ValueType"),
            (keyed(set(json!({"l_id": 1}), json!({"speed": [1]}))), 403, "Patch.ValueType"),
            (keyed(set(json!({"l_id": 1}), json!({"name": 3}))), 403, "Patch.ValueType"),
            (keyed(set(json!({"l_id": 1}), json!({"name": {"bytes": "zz"}}))), 403, "Patch.BytesShape"),
            (keyed(set(json!({"name": "Kuribo"}), json!({"speed": 1.0}))), 400, "Patch.WhereNotKey"),
            (keyed(set(json!({"l_id": [1]}), json!({"speed": 1.0}))), 403, "Patch.KeyValue"),
        ];
        for (refusal, code, phrase) in &cases {
            let said = refusal.args.iter().find(|(n, _)| *n == "why" || *n == "found").map(|(_, a)| a.clone());
            assert_eq!((refusal.code, said), (*code, Some(Arg::Phrase(phrase))), "{refusal:?}");
            assert_eq!((refusal.mod_id.as_deref(), refusal.file.as_str()), (Some("m"), "f.json"));
            for language in luma_validate::LANGUAGES {
                let words = luma_validate::reason(refusal, language);
                assert!(!words.is_empty() && !words.contains('{'), "{phrase} in {language}: {words}");
            }
        }
        // The int column's range is the validator's, from the column's width.
        let wrong_int = &cases[13].0;
        assert_eq!(luma_validate::reason(wrong_int, "UsEnglish"), format!("column \"l_id\" holds whole numbers from {} to {}; \"one\" does not fit", i32::MIN, i32::MAX));
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
        let Fact::Failed { refusal, .. } = &facts[0] else { unreachable!() };
        assert_eq!(refusal.code, 400);
        assert_eq!(luma_validate::reason(refusal, "UsEnglish"), "it must give exactly the key columns, in order: l_id");
        assert_eq!(speed(&t, 1), speed(&before, 1));
        assert_eq!(speed(&t, 0), 9.0);
        // A table its rule cannot read is an error for the caller.
        let mut t = TableIr { columns: before.columns[1..].to_vec(), rows: before.rows.iter().map(|r| r[1..].to_vec()).collect() };
        assert!(apply(OBJINFO, &mut t, &[ModPatch { mod_id: "g", file: "pg", patch: &good }]).is_err());
    }
}
