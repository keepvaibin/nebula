//! What the game has, by name: the self-description (plan 1.8) as far as it exists. Until 1.8's file is signed, the
//! function rows come from the kernel's `.rows` files (one line per function: name, signature, `hook` or `tell`,
//! source line; `tell` rows are constructors and destructors, which take `before` and `after` only), and the records,
//! fields and globals from the types file `luma_types.json` (plan 1.2, contract types draft 2).

use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};

/// Why a folder of `.rows` files does not read: what a tool says in its own language.
#[derive(Debug)]
pub enum RowsError {
    /// A folder or file that cannot be read.
    Read { path: PathBuf, error: std::io::Error },
    /// A line that is not four tab-separated fields (name, signature, `hook` or `tell`, source line); 1-based.
    Line { path: PathBuf, line: usize },
    /// A types file that does not read.
    Types { path: PathBuf, error: TypesError },
}

impl std::fmt::Display for RowsError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            RowsError::Read { path, error } => write!(f, "{}: {error}", path.display()),
            RowsError::Line { path, line } => write!(f, "{} line {line}: not name, signature, hook or tell, source line", path.display()),
            RowsError::Types { path, error } => write!(f, "{}: {error}", path.display()),
        }
    }
}

impl std::error::Error for RowsError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            RowsError::Read { error, .. } => Some(error),
            RowsError::Line { .. } => None,
            RowsError::Types { error, .. } => Some(error),
        }
    }
}

/// The types file's name in a folder of names.
pub const TYPES_FILE: &str = "luma_types.json";

#[derive(Clone, Debug, Default)]
pub struct Names {
    /// Each function's signatures, with whether the row is a constructor or destructor (`tell`).
    pub functions: HashMap<String, Vec<(String, bool)>>,
    /// The game's records, fields and globals (kinds 1, 2 and 7); None when the types file was not given.
    pub types: Option<Types>,
    /// The NameObj class names (kind 3); None until the self-description lists classes (1.3, 1.8).
    pub classes: Option<HashSet<String>>,
    /// The services' permission names; None until the service registry (2.1) lists them.
    pub permissions: Option<Vec<String>>,
    /// The LumaCore version mods are checked against (`LUMA_API`).
    pub api: u32,
}

impl Names {
    /// Reads every `*.rows` file under `folder`, and the types file `luma_types.json` if one is there (one only). A line
    /// that is not four tab-separated fields, or a types file that does not read, is an error naming it.
    pub fn from_rows_folder(folder: &Path, api: u32) -> Result<Names, RowsError> {
        let mut names = Names { api, ..Names::default() };
        let mut stack = vec![folder.to_path_buf()];
        while let Some(dir) = stack.pop() {
            let read = |error| RowsError::Read { path: dir.clone(), error };
            for entry in std::fs::read_dir(&dir).map_err(read)? {
                let path = entry.map_err(read)?.path();
                if path.is_dir() {
                    stack.push(path);
                } else if path.extension().is_some_and(|e| e == "rows") {
                    let text = std::fs::read_to_string(&path).map_err(|error| RowsError::Read { path: path.clone(), error })?;
                    names.add_rows(&text).map_err(|line| RowsError::Line { path: path.clone(), line })?;
                } else if path.file_name().is_some_and(|n| n == TYPES_FILE) {
                    if names.types.is_some() {
                        let why = format!("a second {TYPES_FILE} under {}", folder.display());
                        return Err(RowsError::Types { path, error: TypesError::Row { at: String::new(), why } });
                    }
                    let bytes = std::fs::read(&path).map_err(|error| RowsError::Read { path: path.clone(), error })?;
                    names.types = Some(Types::from_json(&bytes).map_err(|error| RowsError::Types { path: path.clone(), error })?);
                }
            }
        }
        Ok(names)
    }

    /// Adds the rows of one `.rows` text; Err gives the line number of a malformed line.
    pub fn add_rows(&mut self, text: &str) -> Result<(), usize> {
        for (n, line) in text.lines().enumerate() {
            if line.is_empty() {
                continue;
            }
            let fields: Vec<&str> = line.split('\t').collect();
            if fields.len() != 4 || !matches!(fields[2], "hook" | "tell") {
                return Err(n + 1);
            }
            let list = self.functions.entry(fields[0].to_string()).or_default();
            if !list.iter().any(|(s, _)| s == fields[1]) {
                list.push((fields[1].to_string(), fields[2] == "tell"));
            }
        }
        Ok(())
    }
}

/// The game's types by name, as the types file (`luma_types.json`, contract types draft 2) gives them: what a mod's
/// kind 1, 2 and 7 imports bind. The validator walks nothing itself (TY-1): a record's fields are its own and the
/// generator's `reachable`, and what the generator left out keeps its reason, so the kernel and the validator give
/// one answer.
#[derive(Clone, Debug, Default)]
pub struct Types {
    /// Each record (struct, class, union) by its row's name.
    pub records: HashMap<String, Record>,
    /// Every other spelling of a record (an alias or spelling row that names one) to the record's row name (TY-2).
    pub aliases: HashMap<String, String>,
    /// Each global a mod may bind (defined by the module) to its type text.
    pub globals: HashMap<String, String>,
    /// What the generator left out, by kind and name (a field as "Record.field"), to its reason as written.
    pub left_out: HashMap<(LeftOutKind, String), String>,
    /// Names left out a second time with another reason, which `left_out` does not keep (the first one, in the file's
    /// order, is the kernel's too: A-TY7-3). Shown by the real-file test so a second reason is never unseen.
    pub left_out_again: Vec<(LeftOutKind, String, String)>,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Record {
    pub size: u64,
    /// Every field name a mod may write for it, its own and those it reaches in its bases, to its type text (a
    /// bit-field's reads "u32 : 3", TY-3).
    pub fields: HashMap<String, String>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum LeftOutKind {
    Record,
    Field,
    Global,
    Enum,
    /// A typedef or using of the kernel's own headers.
    Alias,
    /// A class template by its name without arguments: no instance the game uses has these arguments.
    Template,
}

/// Why a types file does not read (English, as a tool says it): the JSON's own error, or the row that is not in the
/// form draft 2 gives.
#[derive(Debug, PartialEq, Eq)]
pub enum TypesError {
    /// Not JSON: at line and column, the reader's phrase id and its detail.
    Json { line: u32, column: u32, what: &'static str, detail: String },
    /// A row not in the form: where (`types[12]`), and what is wrong with it.
    Row { at: String, why: String },
}

impl std::fmt::Display for TypesError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            TypesError::Json { line, column, what, detail } => write!(f, "line {line} column {column}: not JSON ({what} {detail})"),
            TypesError::Row { at, why } if at.is_empty() => write!(f, "{why}"),
            TypesError::Row { at, why } => write!(f, "{at}: {why}"),
        }
    }
}

impl std::error::Error for TypesError {}

impl Types {
    /// Reads a types file. Anything not in draft 2's form is an error naming the row: a wrong file must never read
    /// as a game with fewer names.
    pub fn from_json(bytes: &[u8]) -> Result<Types, TypesError> {
        use crate::json::Value;
        let root = crate::json::parse(bytes)
            .map_err(|e| TypesError::Json { line: e.pos.line, column: e.pos.column, what: e.what, detail: e.detail })?;
        let bad = |at: String, why: &str| TypesError::Row { at, why: why.to_string() };
        let list = |key: &str| root.get(key).filter(|v| v.is_array()).map(Value::items).ok_or_else(|| bad(key.to_string(), "missing, or not a list"));
        let text = |v: &Value, key: &str, at: &str| v.get(key).and_then(Value::as_str).map(str::to_string).ok_or_else(|| bad(format!("{at}.{key}"), "missing, or not text"));
        let mut types = Types::default();
        let mut aliases = Vec::new();
        for (i, row) in list("types")?.iter().enumerate() {
            let at = format!("types[{i}]");
            match text(row, "kind", &at)?.as_str() {
                "struct" | "class" | "union" => {
                    let name = text(row, "name", &at)?;
                    let size = row.get("size").and_then(Value::as_i64).filter(|s| *s >= 0).ok_or_else(|| bad(format!("{at}.size"), "missing, or not a size"))?;
                    let mut fields = HashMap::new();
                    // The inherited rows first: a record's own field of a name hides a base's (the generator writes
                    // none such, and own last keeps C++'s answer if one ever came).
                    for key in ["reachable", "fields"] {
                        for (k, f) in row.get(key).filter(|v| v.is_array()).ok_or_else(|| bad(format!("{at}.{key}"), "missing, or not a list"))?.items().iter().enumerate() {
                            let at = format!("{at}.{key}[{k}]");
                            fields.insert(text(f, "name", &at)?, text(f, "type", &at)?);
                        }
                    }
                    if types.records.insert(name.clone(), Record { size: size as u64, fields }).is_some() {
                        return Err(bad(at, &format!("a second record named \"{name}\"")));
                    }
                }
                "alias" | "spelling" => {
                    // One with no record (an alias of `signed char`) names no record: not a kind 1 or 2 name.
                    if let Some(record) = row.get("record").and_then(Value::as_str) {
                        aliases.push((text(row, "name", &at)?, record.to_string(), at));
                    }
                }
                "enum" => {}
                other => return Err(bad(format!("{at}.kind"), &format!("\"{other}\" is no kind of draft 2"))),
            }
        }
        for (name, record, at) in aliases {
            // A record's own name wins over an alias of the same spelling (`typedef struct X {...} X;`).
            if types.records.contains_key(&name) {
                continue;
            }
            if let Some(other) = types.aliases.insert(name.clone(), record.clone()).filter(|o| *o != record) {
                return Err(bad(at, &format!("\"{name}\" names both \"{other}\" and \"{record}\"")));
            }
        }
        for (i, row) in list("globals")?.iter().enumerate() {
            let at = format!("globals[{i}]");
            let defined = row.get("defined").and_then(Value::as_bool).ok_or_else(|| bad(format!("{at}.defined"), "missing, or not true or false"))?;
            let (name, ty) = (text(row, "name", &at)?, text(row, "type", &at)?);
            // Declared but defined by no object of the module: no address to bind (and left_out says so).
            if defined && types.globals.insert(name.clone(), ty).is_some() {
                return Err(bad(at, &format!("a second global named \"{name}\"")));
            }
        }
        for (i, row) in list("left_out")?.iter().enumerate() {
            let at = format!("left_out[{i}]");
            let kind = match text(row, "kind", &at)?.as_str() {
                "record" => LeftOutKind::Record,
                "field" => LeftOutKind::Field,
                "global" => LeftOutKind::Global,
                "enum" => LeftOutKind::Enum,
                "alias" => LeftOutKind::Alias,
                "template" => LeftOutKind::Template,
                other => return Err(bad(format!("{at}.kind"), &format!("\"{other}\" is no kind of draft 2"))),
            };
            // An anonymous enum or record has no name to look up; the first reason of a name is kept.
            let name = row.get("name").and_then(Value::as_str).unwrap_or("").to_string();
            let reason = text(row, "reason", &at)?;
            match types.left_out.get(&(kind, name.clone())) {
                Some(first) if *first != reason => types.left_out_again.push((kind, name, reason)),
                Some(_) => {}
                None => {
                    types.left_out.insert((kind, name), reason);
                }
            }
        }
        Ok(types)
    }

    /// A record by its row's name or another spelling of it, with its row's name.
    pub fn record(&self, name: &str) -> Option<(&str, &Record)> {
        let row = if self.records.contains_key(name) { name } else { self.aliases.get(name)?.as_str() };
        self.records.get_key_value(row).map(|(k, r)| (k.as_str(), r))
    }

    /// Why a name is left out, if it is: a field as "Record.field" with the record's row name.
    pub fn left_out(&self, kind: LeftOutKind, name: &str) -> Option<&str> {
        self.left_out.get(&(kind, name.to_string())).map(String::as_str)
    }
}

/// "Type.field" split as the kernel splits it (LumaTypes.cpp findField): the type ends at the first `.` outside a
/// template's arguments; the field is the rest, and neither is empty.
pub fn split_field(name: &str) -> Option<(&str, &str)> {
    let mut depth = 0u32;
    for (i, c) in name.char_indices() {
        match c {
            '<' => depth += 1,
            '>' if depth != 0 => depth -= 1,
            '.' if depth == 0 => {
                let (record, field) = (&name[..i], &name[i + 1..]);
                return (!record.is_empty() && !field.is_empty()).then_some((record, field));
            }
            _ => {}
        }
    }
    None
}
