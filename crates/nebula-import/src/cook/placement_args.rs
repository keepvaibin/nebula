//! A placement row's arguments by name (docs/modding/content/stages.md section 1): a mod's row of a
//! stage's placement table (`<Stage>.arc/jmp/placement/<layer>/objinfo.json`, whole or as a patch by key) may give
//! `"args": {"ShotInterval": 120, "Rotate": true}` instead of `Obj_arg0` to `Obj_arg7`. The names are the game's own:
//! for each object of the factory, the field each `MR::getPlacementArgN` call of its class fills, from the game's
//! `smg_game.luma-placement.json`, which the build writes beside the game module from its source: the
//! factory's objects and their classes, and per class every placement read of it and its bases (`from`), with the
//! field it fills: `mShotInterval` is `ShotInterval`. One name per argument: a read with a field in the class itself,
//! else in a base, else `Arg<n>`. A column read by its own name (`getPlacementArgNoInit(iter, "RotateSpeed", &v)`,
//! `arg` null) is named as it is, and takes the value as given. `Arg0` to `Arg7` name any object's arguments.
//!
//! The game reads every argument as a whole number, -1 meaning "not given" (PlacementUtil's getPlacementArgNoInit):
//! a float field takes it converted, a bool field is true for any value but -1. So a whole number goes in as it is,
//! `true` as 1, `false` and `null` as -1; a fraction is refused (the game would never see it). A row that gives
//! `args` has -1 in each argument it names neither way, as the game's own rows have.

use super::particle_json::{as_int, not_kind};
use super::refuse::refuse;
use anyhow::{Context, Result};
use serde_json::{Map, Value};
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};

/// What a field an argument fills is, by the overload the game calls.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ArgKind {
    Int,
    Float,
    Bool,
}

/// One named argument of an object: `Obj_arg<n>` (`arg` Some) or a column read by its own name.
#[derive(Clone, Debug, PartialEq)]
pub struct ArgName {
    pub arg: Option<u8>,
    /// The table's column: `Obj_arg<n>`, or the name read.
    pub column: String,
    pub name: String,
    pub kind: ArgKind,
}

/// The facts file's name, beside the game module.
pub const FILE_NAME: &str = "smg_game.luma-placement.json";
/// The version of the facts file this cook reads (its `placement`).
const VERSION: u64 = 1;

/// The objects of the game's factory and their named arguments.
#[derive(Default)]
pub struct ObjectArgs {
    /// By the object's name without case: its name as the factory writes it, and its arguments by arg then name.
    objects: HashMap<String, (String, Vec<ArgName>)>,
}

/// `mShotInterval` -> `ShotInterval`, `m_speed` -> `speed`, `mArg0` -> `Arg0`; a local or no target: `Arg<n>`.
pub fn modder_name(target: Option<&str>, arg: u8) -> String {
    let field = target.and_then(|t| t.rsplit(['.', '>']).next()).unwrap_or("");
    let stripped = field.strip_prefix("m_").or_else(|| field.strip_prefix('m').filter(|r| r.starts_with(|c: char| c.is_ascii_uppercase())));
    match stripped {
        Some(name) if !name.is_empty() && name.chars().all(|c| c.is_ascii_alphanumeric() || c == '_') => name.to_string(),
        _ => format!("Arg{arg}"),
    }
}

impl ObjectArgs {
    /// From the facts file: `{"placement": 1, "factory": [{name, class|null, creator}], "placement_args": {Class: [{arg
    /// (0-7, or null for a column read by name), field, type, target|null, from, ...}]}}`, each class's rows its bases'
    /// included, each naming in `from` the class whose method reads it.
    pub fn from_description(json: &[u8]) -> Result<ObjectArgs> {
        let root: Value = serde_json::from_slice(json).with_context(|| format!("{FILE_NAME} is not JSON"))?;
        let version = root.get("placement").and_then(Value::as_u64).unwrap_or(VERSION);
        anyhow::ensure!(version <= VERSION, "{FILE_NAME} is placement version {version}; this cook reads {VERSION}");
        let factory = root.get("factory").and_then(Value::as_array).with_context(|| format!("{FILE_NAME} has no factory"))?;
        let classes = root.get("placement_args").and_then(Value::as_object).with_context(|| format!("{FILE_NAME} has no placement_args"))?;
        let mut by_class: HashMap<&str, Vec<ArgName>> = HashMap::new();
        for (class, rows) in classes {
            // Each column's reads, in the file's order.
            let mut columns: Vec<(Option<u8>, String, Vec<&Value>)> = Vec::new();
            for row in rows.as_array().into_iter().flatten() {
                let arg = match row.get("arg") {
                    None | Some(Value::Null) => None,
                    Some(v) => Some(v.as_u64().filter(|a| *a < 8).with_context(|| format!("{class}: placement argument {v}"))? as u8),
                };
                let column = match (arg, row.get("field").and_then(Value::as_str)) {
                    (Some(a), _) => format!("Obj_arg{a}"),
                    (None, Some(field)) if !field.is_empty() => field.to_string(),
                    // A column named by a variable: nothing a modder can name.
                    (None, _) => continue,
                };
                match columns.iter_mut().find(|(_, c, _)| c.eq_ignore_ascii_case(&column)) {
                    Some((_, _, reads)) => reads.push(row),
                    None => columns.push((arg, column, vec![row])),
                }
            }
            let list = by_class.entry(class.as_str()).or_default();
            for (arg, column, reads) in columns {
                let kind_of = |r: &Value| match r.get("type").and_then(Value::as_str) {
                    Some("bool") => ArgKind::Bool,
                    Some("f32") => ArgKind::Float,
                    _ => ArgKind::Int,
                };
                // Read two ways (rare): a whole number.
                let kind = if reads.iter().all(|r| kind_of(r) == kind_of(reads[0])) { kind_of(reads[0]) } else { ArgKind::Int };
                let name = match arg {
                    None => column.clone(),
                    // The class's own read with a field first, then a base's, then Arg<n>.
                    Some(a) => {
                        let own = |r: &&&Value| r.get("from").and_then(Value::as_str).is_none_or(|f| f == class);
                        let generic = format!("Arg{a}");
                        reads.iter().filter(own).chain(reads.iter().filter(|r| !own(r)))
                            .map(|r| modder_name(r.get("target").and_then(Value::as_str), a))
                            .find(|n| *n != generic)
                            .unwrap_or(generic)
                    }
                };
                list.push(ArgName { arg, column, name, kind });
            }
        }
        // A name an argument shares with another column names neither argument: each is its Arg<n> (a column read by
        // its own name keeps it).
        for list in by_class.values_mut() {
            let names: Vec<(String, String)> = list.iter().map(|a| (a.name.to_ascii_lowercase(), a.column.to_ascii_lowercase())).collect();
            for a in list.iter_mut() {
                let Some(n) = a.arg else { continue };
                let (name, column) = (a.name.to_ascii_lowercase(), a.column.to_ascii_lowercase());
                if names.iter().any(|(other, at)| *other == name && *at != column) {
                    a.name = format!("Arg{n}");
                }
            }
            list.sort_by(|a, b| (a.arg.unwrap_or(8), &a.column).cmp(&(b.arg.unwrap_or(8), &b.column)));
        }
        let mut objects = HashMap::new();
        for row in factory {
            let Some(name) = row.get("name").and_then(Value::as_str) else { continue };
            let list = row.get("class").and_then(Value::as_str).and_then(|c| by_class.get(c)).cloned().unwrap_or_default();
            objects.insert(name.to_ascii_lowercase(), (name.to_string(), list));
        }
        Ok(ObjectArgs { objects })
    }

    /// An object's named arguments; None when the factory has no such object.
    pub fn of(&self, object: &str) -> Option<&[ArgName]> {
        self.objects.get(&object.to_ascii_lowercase()).map(|(_, list)| list.as_slice())
    }
}

/// The file the facts are read from, when set (LumaLink's `--names` folder, a test); else FILE_NAME beside the running
/// program (the game module's folder).
static FILE: Mutex<Option<PathBuf>> = Mutex::new(None);
/// The facts read, by the file they came from (None: not readable).
static LOADED: Mutex<Option<(PathBuf, Option<Arc<ObjectArgs>>)>> = Mutex::new(None);

/// Held by each test that sets the facts file, so two tests never read each other's.
#[cfg(test)]
pub(crate) static TEST_FILE: Mutex<()> = Mutex::new(());

/// Read the facts from `path` from now on (a folder: its FILE_NAME).
pub fn use_file(path: &Path) {
    let file = if path.is_dir() { path.join(FILE_NAME) } else { path.to_path_buf() };
    *FILE.lock().unwrap_or_else(|e| e.into_inner()) = Some(file);
}

fn facts_path() -> PathBuf {
    if let Some(file) = FILE.lock().unwrap_or_else(|e| e.into_inner()).clone() {
        return file;
    }
    std::env::current_exe().ok().and_then(|exe| exe.parent().map(|p| p.join(FILE_NAME))).unwrap_or_else(|| PathBuf::from(FILE_NAME))
}

/// The game's placement facts, read once per file; a refusal naming the file when it cannot be read.
pub fn objects() -> Result<Arc<ObjectArgs>> {
    let path = facts_path();
    let mut loaded = LOADED.lock().unwrap_or_else(|e| e.into_inner());
    let known = match loaded.as_ref() {
        Some((from, objects)) if *from == path => objects.clone(),
        _ => {
            let objects = std::fs::read(&path).ok().and_then(|b| ObjectArgs::from_description(&b).ok()).map(Arc::new);
            *loaded = Some((path.clone(), objects.clone()));
            objects
        }
    };
    known.ok_or_else(|| refuse("Cook.Stage.NoFacts", &[("uri", path.display().to_string())]))
}

/// Whether any row gives `args` (only then are the facts read).
pub fn any_args(rows: &[Value]) -> bool {
    rows.iter().any(|r| r.get("args").is_some())
}

/// A whole table's JSON with its rows' `args` as `Obj_arg<n>`; None when no row gives `args` (the bytes stand).
pub fn expand_table(bytes: &[u8]) -> Result<Option<Vec<u8>>> {
    let Ok(mut root) = serde_json::from_slice::<Value>(bytes) else { return Ok(None) };
    let Some(rows) = root.get_mut("rows").and_then(Value::as_array_mut) else { return Ok(None) };
    if !any_args(rows) {
        return Ok(None);
    }
    let facts = objects()?;
    expand_rows(rows, "/rows", Some(&facts), true)?;
    Ok(Some(serde_json::to_vec(&root)?))
}

/// Every row's `args` in a placement table's rows (`at`: where the rows are, `/rows`) turned into `Obj_arg<n>`
/// columns: how many rows gave them. `fill`: each argument given neither way is -1 (a whole table, a merge); a patch's
/// new row starts from its table's usual values instead, in the columns the table has. Without the facts (`objects`
/// None) a row with `args` is refused.
pub fn expand_rows(rows: &mut [Value], at: &str, objects: Option<&ObjectArgs>, fill: bool) -> Result<usize> {
    let mut expanded = 0;
    for (i, row) in rows.iter_mut().enumerate() {
        let Some(fields) = row.as_object_mut() else { continue };
        let object = fields.get("name").and_then(Value::as_str).unwrap_or("").to_string();
        if expand_fields(fields, &format!("{at}/{i}"), &object, objects, fill)? {
            expanded += 1;
        }
    }
    Ok(expanded)
}

/// A column named as the game's tables name it, in any case (`obj_arg0` is Obj_arg0).
fn has_column(fields: &Map<String, Value>, column: &str) -> bool {
    fields.keys().any(|k| k.eq_ignore_ascii_case(column))
}

/// One row's (or one patch `set`'s) `args` for the game object `object` turned into `Obj_arg<n>` values, `at` the
/// place of the fields. `fill`: every argument given neither way becomes -1, as in a new row; a `set` changes only the
/// arguments it names. False when the fields have no `args`.
pub fn expand_fields(fields: &mut Map<String, Value>, at: &str, object: &str, objects: Option<&ObjectArgs>, fill: bool) -> Result<bool> {
    let Some(given) = fields.remove("args") else { return Ok(false) };
    let here = format!("{at}/args");
    let given = match given {
        Value::Object(map) => map,
        _ => return Err(not_kind(&here, "Cook.Effect.Is.Object")),
    };
    let object = object.to_string();
    let Some(objects) = objects else { return Err(refuse("Cook.Stage.NoFacts", &[("uri", FILE_NAME.to_string())])) };
    let named: Vec<ArgName> = match objects.of(&object) {
        Some(list) => list.to_vec(),
        None => return Err(refuse("Cook.Stage.NoObject", &[("where", here), ("name", object)])),
    };
    let mut set: [Option<i64>; 8] = [None; 8];
    // Columns read by their own names, written as given.
    let mut by_name: Vec<(String, Value)> = Vec::new();
    for (key, value) in &given {
        let at_key = format!("{here}/{key}");
        let generic = key.strip_prefix("Arg").or_else(|| key.strip_prefix("arg")).and_then(|n| n.parse::<u8>().ok()).filter(|n| *n < 8);
        let (arg, column, kind) = match named.iter().find(|a| a.name.eq_ignore_ascii_case(key)) {
            Some(a) => (a.arg, a.column.clone(), a.kind),
            None => match generic {
                Some(n) => (Some(n), format!("Obj_arg{n}"), named.iter().find(|a| a.arg == Some(n)).map_or(ArgKind::Int, |a| a.kind)),
                None => {
                    let list: Vec<&str> = named.iter().map(|a| a.name.as_str()).collect();
                    let expected = if list.is_empty() { "Arg0-Arg7".to_string() } else { format!("{}, Arg0-Arg7", list.join(", ")) };
                    return Err(refuse("Cook.Stage.ArgName", &[("where", at_key), ("name", key.clone()), ("other", object.clone()), ("expected", expected)]));
                }
            },
        };
        let twice = match arg {
            Some(n) => set[n as usize].is_some(),
            None => by_name.iter().any(|(c, _)| c.eq_ignore_ascii_case(&column)),
        };
        if has_column(fields, &column) || twice {
            return Err(refuse("Cook.Stage.ArgTwice", &[("where", at_key), ("name", key.clone()), ("column", column)]));
        }
        let number = match (value, kind) {
            (Value::Null, _) | (Value::Bool(false), ArgKind::Bool) => Value::from(-1),
            (Value::Bool(true), ArgKind::Bool) => Value::from(1),
            (Value::Bool(_), _) => return Err(not_kind(&at_key, "Cook.Effect.Is.Whole")),
            // A column of its own may hold a fraction (its table's column decides); Obj_arg<n> holds a whole number.
            (Value::Number(_), _) if arg.is_none() => value.clone(),
            (v, _) => Value::from(as_int(v, &at_key, i64::from(i32::MIN), i64::from(i32::MAX))?),
        };
        match arg {
            Some(n) => set[n as usize] = number.as_i64(),
            None => by_name.push((column, number)),
        }
    }
    for (column, value) in by_name {
        fields.insert(column, value);
    }
    for (arg, value) in set.iter().enumerate() {
        let column = format!("Obj_arg{arg}");
        match value {
            Some(number) => {
                fields.insert(column, Value::from(*number));
            }
            None if fill && !has_column(fields, &column) => {
                fields.insert(column, Value::from(-1));
            }
            None => {}
        }
    }
    Ok(true)
}

#[cfg(test)]
#[path = "placement_args_tests.rs"]
mod tests;
