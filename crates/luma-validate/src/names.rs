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
    /// Each named enum by its row's name (the LumaScript compiler's game enums; the validator binds none).
    pub enums: HashMap<String, Enum>,
    /// The enums with no name, whose values are names of their scope (`NEBULA_CLIP_JOINTS`), in the file's order.
    pub anonymous_enums: Vec<Enum>,
    /// Every alias and spelling row, a record's or not (`s8` to `signed char`, `Vec3f` to `Vec3<f32>`), to its `of`.
    pub typedefs: HashMap<String, String>,
    /// Every global row, defined or not, with what the compiler needs beside the type text (types draft 2).
    pub global_facts: HashMap<String, Global>,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Record {
    pub size: u64,
    /// Every field name a mod may write for it, its own and those it reaches in its bases, to its type text (a
    /// bit-field's reads "u32 : 3", TY-3).
    pub fields: HashMap<String, String>,
    pub kind: RecordKind,
    pub align: u64,
    /// It has a vtable.
    pub polymorphic: bool,
    pub is_abstract: bool,
    /// Trivially copyable (types TY-10): a bit copy is the C++ copy.
    pub trivial_copy: bool,
    /// Trivially destructible (TY-10): dropping one frees nothing.
    pub trivial_destroy: bool,
    /// No copy or move constructor and no copy or move assignment can be called (TY-10b). clang calls such a record
    /// trivially copyable, so a reader that takes a record by value needs `trivial_copy && !no_copy`.
    pub no_copy: bool,
    /// In declaration order.
    pub bases: Vec<Base>,
    /// Its own fields in declaration order.
    pub own: Vec<Field>,
    /// The fields it reaches in its bases (the generator's `reachable`), in the file's order.
    pub reached: Vec<Reached>,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum RecordKind {
    #[default]
    Struct,
    Class,
    Union,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Base {
    pub name: String,
    pub offset: u64,
    pub is_virtual: bool,
}

/// A record's own field (types draft 2, section 2). The offsets serve messages and the build report only: a mod
/// reaches a field through the import row the loader binds (abi section 3, checker audit C42-3).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Field {
    pub name: String,
    /// The element's type text for an array, the bit-field's text ("u32 : 3") for a bit-field (TY-3).
    pub ty: String,
    pub offset: u64,
    /// Each extent of an array field, outermost first; empty for no array. One extent when the file gives only
    /// `count` (the generator writes `extents` for two or more).
    pub extents: Vec<u64>,
    pub bits: Option<Bits>,
    pub access: Access,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Bits {
    pub width: u32,
    /// From the field's byte offset.
    pub offset: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Access {
    Public,
    Protected,
    Private,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Reached {
    pub name: String,
    /// The base that declares it.
    pub from: String,
    pub offset: u64,
    pub ty: String,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Enum {
    /// The underlying type's text.
    pub underlying: String,
    pub scoped: bool,
    /// Names and values in declaration order.
    pub values: Vec<(String, i64)>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Global {
    pub ty: String,
    pub konst: bool,
    /// Defined by the module: it has an address a kind 7 import binds.
    pub defined: bool,
    /// The value a `const` or `constexpr` global has at compile time, as the file writes the number.
    pub value: Option<String>,
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
        let number = |v: &Value, key: &str, at: &str| v.get(key).and_then(Value::as_i64).filter(|n| *n >= 0).map(|n| n as u64).ok_or_else(|| bad(format!("{at}.{key}"), "missing, or not a whole number from 0"));
        let flag = |v: &Value, key: &str, at: &str| v.get(key).and_then(Value::as_bool).ok_or_else(|| bad(format!("{at}.{key}"), "missing, or not true or false"));
        fn items<'v>(v: &'v Value, key: &str, at: &str) -> Result<&'v [Value], TypesError> {
            v.get(key).filter(|v| v.is_array()).map(Value::items).ok_or_else(|| TypesError::Row { at: format!("{at}.{key}"), why: "missing, or not a list".to_string() })
        }
        let is_null = |v: &Value, key: &str| matches!(v.get(key).map(|v| &v.kind), Some(crate::json::Kind::Null));
        let mut types = Types::default();
        let mut aliases = Vec::new();
        for (i, row) in list("types")?.iter().enumerate() {
            let at = format!("types[{i}]");
            match text(row, "kind", &at)?.as_str() {
                kind @ ("struct" | "class" | "union") => {
                    let name = text(row, "name", &at)?;
                    let mut record = Record {
                        size: number(row, "size", &at)?,
                        kind: match kind {
                            "struct" => RecordKind::Struct,
                            "class" => RecordKind::Class,
                            _ => RecordKind::Union,
                        },
                        align: number(row, "align", &at)?,
                        polymorphic: flag(row, "polymorphic", &at)?,
                        is_abstract: flag(row, "abstract", &at)?,
                        trivial_copy: flag(row, "trivial_copy", &at)?,
                        trivial_destroy: flag(row, "trivial_destroy", &at)?,
                        no_copy: flag(row, "no_copy", &at)?,
                        ..Record::default()
                    };
                    for (k, b) in items(row, "bases", &at)?.iter().enumerate() {
                        let at = format!("{at}.bases[{k}]");
                        record.bases.push(Base { name: text(b, "name", &at)?, offset: number(b, "offset", &at)?, is_virtual: flag(b, "virtual", &at)? });
                    }
                    // The inherited rows first: a record's own field of a name hides a base's (the generator writes
                    // none such, and own last keeps C++'s answer if one ever came).
                    for (k, f) in items(row, "reachable", &at)?.iter().enumerate() {
                        let at = format!("{at}.reachable[{k}]");
                        let reached = Reached { name: text(f, "name", &at)?, from: text(f, "from", &at)?, offset: number(f, "offset", &at)?, ty: text(f, "type", &at)? };
                        record.fields.insert(reached.name.clone(), reached.ty.clone());
                        record.reached.push(reached);
                    }
                    for (k, f) in items(row, "fields", &at)?.iter().enumerate() {
                        let at = format!("{at}.fields[{k}]");
                        let count = number(f, "count", &at)?;
                        let extents = if is_null(f, "extents") {
                            if count == 1 { Vec::new() } else { vec![count] }
                        } else {
                            let list: Vec<u64> = items(f, "extents", &at)?.iter().map(|e| e.as_i64().filter(|n| *n >= 0).map(|n| n as u64)).collect::<Option<_>>()
                                .ok_or_else(|| bad(format!("{at}.extents"), "not a list of whole numbers from 0"))?;
                            if list.len() < 2 || list.iter().try_fold(1u64, |a, e| a.checked_mul(*e)) != Some(count) {
                                return Err(bad(format!("{at}.extents"), &format!("{list:?} is not two or more extents whose product is the count {count}")));
                            }
                            list
                        };
                        let bits = if is_null(f, "bits") {
                            None
                        } else {
                            let b = f.get("bits").filter(|b| b.is_object()).ok_or_else(|| bad(format!("{at}.bits"), "missing, or not null or an object"))?;
                            let at = format!("{at}.bits");
                            let (width, offset) = (number(b, "width", &at)?, number(b, "offset", &at)?);
                            if !(1..=64).contains(&width) || offset > u64::from(u32::MAX) {
                                return Err(bad(at, &format!("width {width} at bit {offset} is no bit-field")));
                            }
                            Some(Bits { width: width as u32, offset: offset as u32 })
                        };
                        let access = match text(f, "access", &at)?.as_str() {
                            "public" => Access::Public,
                            "protected" => Access::Protected,
                            "private" => Access::Private,
                            other => return Err(bad(format!("{at}.access"), &format!("\"{other}\" is no access"))),
                        };
                        let field = Field { name: text(f, "name", &at)?, ty: text(f, "type", &at)?, offset: number(f, "offset", &at)?, extents, bits, access };
                        record.fields.insert(field.name.clone(), field.ty.clone());
                        record.own.push(field);
                    }
                    if types.records.insert(name.clone(), record).is_some() {
                        return Err(bad(at, &format!("a second record named \"{name}\"")));
                    }
                }
                "alias" | "spelling" => {
                    let (name, of) = (text(row, "name", &at)?, text(row, "of", &at)?);
                    if let Some(other) = types.typedefs.insert(name.clone(), of.clone()).filter(|o| *o != of) {
                        return Err(bad(at, &format!("\"{name}\" is both \"{other}\" and \"{of}\"")));
                    }
                    // One with no record (an alias of `signed char`) names no record: not a kind 1 or 2 name.
                    if let Some(record) = row.get("record").and_then(Value::as_str) {
                        aliases.push((name, record.to_string(), at));
                    } else if !is_null(row, "record") {
                        return Err(bad(format!("{at}.record"), "missing, or not text or null"));
                    }
                }
                "enum" => {
                    let mut e = Enum { underlying: text(row, "underlying", &at)?, scoped: flag(row, "scoped", &at)?, values: Vec::new() };
                    for (k, v) in items(row, "values", &at)?.iter().enumerate() {
                        let at = format!("{at}.values[{k}]");
                        let value = v.get("value").and_then(Value::as_i64).ok_or_else(|| bad(format!("{at}.value"), "missing, or not a whole number"))?;
                        e.values.push((text(v, "name", &at)?, value));
                    }
                    if is_null(row, "name") {
                        types.anonymous_enums.push(e);
                    } else {
                        let name = text(row, "name", &at)?;
                        if types.enums.insert(name.clone(), e).is_some() {
                            return Err(bad(at, &format!("a second enum named \"{name}\"")));
                        }
                    }
                }
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
            let value = match row.get("value").map(|v| &v.kind) {
                Some(crate::json::Kind::Null) => None,
                Some(crate::json::Kind::Number(_, written)) => Some(written.clone()),
                _ => return Err(bad(format!("{at}.value"), "missing, or not null or a number")),
            };
            let global = Global { ty: ty.clone(), konst: flag(row, "const", &at)?, defined, value };
            if types.global_facts.insert(name.clone(), global).is_some() {
                return Err(bad(at, &format!("a second global named \"{name}\"")));
            }
            // Declared but defined by no object of the module: no address to bind (and left_out says so).
            if defined {
                types.globals.insert(name, ty);
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
