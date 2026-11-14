//! What the game has, by name, from its self-description folder: the function rows from the kernel's `.rows` files
//! (one line per function: name, signature, `hook`, `tell` or `call`, source line; `tell` rows are constructors and
//! destructors, which take `before` and `after` only, and variadic functions, which take `before` only; a `call` row
//! is a constexpr function, called but never hooked: docs/modding/language/varargs.md), and the records, fields and
//! globals from the types file `luma_types.json` (docs/modding/language/types.md).

use std::collections::HashMap;
use std::path::{Path, PathBuf};

/// Why a folder of `.rows` files does not read: what a tool says in its own language.
#[derive(Debug)]
pub enum RowsError {
    /// A folder or file that cannot be read.
    Read { path: PathBuf, error: std::io::Error },
    /// A line that is not four tab-separated fields (name, signature, `hook`, `tell` or `call`, source line); 1-based.
    Line { path: PathBuf, line: usize },
    /// A types file that does not read.
    Types { path: PathBuf, error: TypesError },
}

impl std::fmt::Display for RowsError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            RowsError::Read { path, error } => write!(f, "{}: {error}", path.display()),
            RowsError::Line { path, line } => write!(f, "{} line {line}: not name, signature, hook, tell or call, source line", path.display()),
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

/// The function rows' facts file's name in a folder of names (types TY-11).
pub const FUNCTIONS_FILE: &str = "luma_functions.json";

/// The services catalog's file name in a folder of names: an object whose `services` key is the self-description's
/// (services-registry 9), so the same reader takes either.
pub const SERVICES_FILE: &str = "luma_services.json";

/// The renderer's feature list's file name in a folder of names: an object whose `renderer` key is the
/// self-description's (luma-format revision 4), so the same reader takes either.
pub const RENDERER_FILE: &str = "luma_renderer.json";

/// At most this many names in the renderer's list and in a mod's `renderer.features`.
pub const MAX_FEATURES: usize = 64;

/// A renderer feature name (luma-format revision 4): `[a-z][a-z0-9_]*`, at most 32 bytes, no `__`, not ending in `_`.
pub fn feature_name_ok(name: &str) -> bool {
    let b = name.as_bytes();
    b.len() <= 32
        && b.first().is_some_and(u8::is_ascii_lowercase)
        && b.iter().all(|&c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == b'_')
        && !name.contains("__")
        && !name.ends_with('_')
}

/// Reads an object with a `renderer` key holding `features` (the self-description, or luma_renderer.json): the names
/// every device of this build may have, in byte order, each once, at most MAX_FEATURES. Anything else is an error
/// naming the place.
pub fn renderer_features_from_json(bytes: &[u8]) -> Result<std::collections::BTreeSet<String>, TypesError> {
    use crate::json::Value;
    let root = crate::json::parse(bytes)
        .map_err(|e| TypesError::Json { line: e.pos.line, column: e.pos.column, what: e.what, detail: e.detail })?;
    let bad = |at: String, why: &str| TypesError::Row { at, why: why.to_string() };
    let renderer = root.get("renderer").filter(|v| v.is_object()).ok_or_else(|| bad("renderer".into(), "missing, or not an object"))?;
    if let Some(m) = renderer.members().iter().find(|m| m.key != "features") {
        return Err(bad(format!("renderer.{}", m.key), "not a key of renderer (only features)"));
    }
    let list = renderer.get("features").filter(|v| v.is_array()).map(Value::items).ok_or_else(|| bad("renderer.features".into(), "missing, or not a list"))?;
    if list.len() > MAX_FEATURES {
        return Err(bad("renderer.features".into(), "more than 64 names"));
    }
    let mut out = std::collections::BTreeSet::new();
    let mut last: Option<&str> = None;
    for (i, v) in list.iter().enumerate() {
        let at = format!("renderer.features[{i}]");
        let name = v.as_str().filter(|n| feature_name_ok(n)).ok_or_else(|| bad(at.clone(), "not a feature name"))?;
        if last.is_some_and(|l| l.as_bytes() >= name.as_bytes()) {
            return Err(bad(at, "not after the name before it (in byte order), or given twice"));
        }
        last = Some(name);
        out.insert(name.to_string());
    }
    Ok(out)
}

/// A service name (services-registry 2): parts `[a-z][a-z0-9]*` joined by `.`, at most 31 bytes.
pub fn service_name_ok(name: &str) -> bool {
    name.len() <= 31 && name.split('.').all(|part| part.as_bytes().first().is_some_and(u8::is_ascii_lowercase) && part.bytes().all(|b| b.is_ascii_lowercase() || b.is_ascii_digit()))
}

/// A kind 5 row's version text (services-registry 2, G R-4): `<major>.<minor>`, each a decimal number from 0 to
/// 65535 with no leading zero, nothing else.
pub fn service_version(text: &str) -> Option<(u16, u16)> {
    let number = |s: &str| -> Option<u16> {
        if s.is_empty() || !s.bytes().all(|b| b.is_ascii_digit()) || (s.len() > 1 && s.starts_with('0')) {
            return None;
        }
        s.parse().ok()
    };
    let (a, b) = text.split_once('.')?;
    Some((number(a)?, number(b)?))
}

/// The permissions a mod may ask for (luma-format revision 3, services-registry 5.1): a closed list, the kernel's
/// cPermissions (LumaServices.cpp). Most are the name of the one service that needs them; `native` is asked by extra
/// libraries (F-1) and by draw.native (contract draw 16), `input.devices` by input.dev (services-input).
pub const PERMISSIONS: [&str; 11] = [
    "native",
    "fs.exchange",
    "net.local",
    "net.internet",
    "input.inject",
    "input.devices",
    "audio.microphone",
    "capture.camera",
    "system.clipboard",
    "system.links",
    "save.game",
];

/// The permission an import of the service `name` shows in use (for L218, with or without a catalog): the two
/// services whose permission is not their name, else the name itself.
pub fn service_permission(name: &str) -> &str {
    match name {
        "draw.native" => "native",
        "input.dev" => "input.devices",
        other => other,
    }
}

/// One catalogued service (services-registry 9).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Service {
    pub name: String,
    pub major: u16,
    pub minor: u16,
    /// The permission it needs (one of PERMISSIONS), or None.
    pub permission: Option<String>,
}

/// The game's services catalog, in its order: by name in byte order, then major.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Services {
    pub rows: Vec<Service>,
}

impl Services {
    /// Reads an object with a `services` key (the self-description, or luma_services.json). A row not in registry 9's
    /// form is an error naming it: a name outside section 2's grammar, a major or minor that is no whole number from 0
    /// to 65535, a permission that is not null, the service's own name or one of PERMISSIONS, a row out of order or given twice.
    pub fn from_json(bytes: &[u8]) -> Result<Services, TypesError> {
        use crate::json::{Kind, Value};
        let root = crate::json::parse(bytes)
            .map_err(|e| TypesError::Json { line: e.pos.line, column: e.pos.column, what: e.what, detail: e.detail })?;
        let bad = |at: String, why: &str| TypesError::Row { at, why: why.to_string() };
        let list = root.get("services").filter(|v| v.is_array()).map(Value::items).ok_or_else(|| bad("services".into(), "missing, or not a list"))?;
        let mut rows: Vec<Service> = Vec::new();
        for (i, row) in list.iter().enumerate() {
            let at = format!("services[{i}]");
            if let Some(m) = row.members().iter().find(|m| !matches!(m.key.as_str(), "name" | "major" | "minor" | "permission")) {
                return Err(bad(format!("{at}.{}", m.key), "not a key of a service row (name, major, minor, permission)"));
            }
            let name = row.get("name").and_then(Value::as_str).filter(|n| service_name_ok(n)).ok_or_else(|| bad(format!("{at}.name"), "missing, or not a service name"))?;
            let version = |key: &str| {
                row.get(key).and_then(Value::as_i64).filter(|n| (0..=i64::from(u16::MAX)).contains(n)).map(|n| n as u16).ok_or_else(|| bad(format!("{at}.{key}"), "missing, or not a whole number from 0 to 65535"))
            };
            let (major, minor) = (version("major")?, version("minor")?);
            let permission = match row.get("permission").map(|v| &v.kind) {
                Some(Kind::Null) => None,
                Some(Kind::String(p)) if p == name || PERMISSIONS.contains(&p.as_str()) => Some(p.clone()),
                _ => return Err(bad(format!("{at}.permission"), "missing, or not null, the service's own name or a permission a mod may ask for")),
            };
            if let Some(last) = rows.last() {
                if (last.name.as_bytes(), last.major) >= (name.as_bytes(), major) {
                    return Err(bad(at, "not after the row before it (by name in byte order, then major), or given twice"));
                }
            }
            rows.push(Service { name: name.to_string(), major, minor, permission });
        }
        Ok(Services { rows })
    }

    /// The row a kind 5 import of `name` at `major.minor` binds: that name and major, and a minor at least the one asked.
    pub fn find(&self, name: &str, major: u16, minor: u16) -> Option<&Service> {
        self.rows.iter().find(|s| s.name == name && s.major == major && s.minor >= minor)
    }
}

#[derive(Clone, Debug, Default)]
pub struct Names {
    /// Each function's signatures, with whether the row is a constructor or destructor (`tell`).
    pub functions: HashMap<String, Vec<(String, bool)>>,
    /// The `call` rows (name, signature): constexpr functions, which a mod calls and no hook may take.
    pub call_only: std::collections::HashSet<(String, String)>,
    /// The game's records, fields and globals (kinds 1, 2 and 7); None when the types file was not given.
    pub types: Option<Types>,
    /// Each function row's facts (types TY-11): what it is, its record, virtual and const; None when the folder has
    /// no facts file.
    pub function_facts: Option<FunctionFacts>,
    /// The game's services catalog (services-registry 9, the self-description's `services`); None when not given:
    /// kind 5 imports are not checked.
    pub services: Option<Services>,
    /// The renderer's feature names (the self-description's `renderer.features`, luma-format revision 4); None when
    /// not given: a mod's `renderer.features` is read for its form only, and L806 is not checked.
    pub renderer_features: Option<std::collections::BTreeSet<String>>,
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
                } else if path.file_name().is_some_and(|n| n == FUNCTIONS_FILE) {
                    if names.function_facts.is_some() {
                        let why = format!("a second {FUNCTIONS_FILE} under {}", folder.display());
                        return Err(RowsError::Types { path, error: TypesError::Row { at: String::new(), why } });
                    }
                    let bytes = std::fs::read(&path).map_err(|error| RowsError::Read { path: path.clone(), error })?;
                    names.function_facts = Some(FunctionFacts::from_json(&bytes).map_err(|error| RowsError::Types { path: path.clone(), error })?);
                } else if path.file_name().is_some_and(|n| n == SERVICES_FILE) {
                    if names.services.is_some() {
                        let why = format!("a second {SERVICES_FILE} under {}", folder.display());
                        return Err(RowsError::Types { path, error: TypesError::Row { at: String::new(), why } });
                    }
                    let bytes = std::fs::read(&path).map_err(|error| RowsError::Read { path: path.clone(), error })?;
                    names.services = Some(Services::from_json(&bytes).map_err(|error| RowsError::Types { path: path.clone(), error })?);
                } else if path.file_name().is_some_and(|n| n == RENDERER_FILE) {
                    if names.renderer_features.is_some() {
                        let why = format!("a second {RENDERER_FILE} under {}", folder.display());
                        return Err(RowsError::Types { path, error: TypesError::Row { at: String::new(), why } });
                    }
                    let bytes = std::fs::read(&path).map_err(|error| RowsError::Read { path: path.clone(), error })?;
                    names.renderer_features = Some(renderer_features_from_json(&bytes).map_err(|error| RowsError::Types { path: path.clone(), error })?);
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
            if fields.len() != 4 || !matches!(fields[2], "hook" | "tell" | "call") {
                return Err(n + 1);
            }
            if fields[2] == "call" {
                self.call_only.insert((fields[0].to_string(), fields[1].to_string()));
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
    /// Each virtual function a record declares (its `virtuals`, objects 7a), by name and signature, to whether it has a
    /// row the kernel binds (import kind 8, LumaCore.method). None when no record carries `virtuals` (a file of before
    /// objects 7a): kind 8 is not checked then.
    pub methods: Option<HashMap<(String, String), bool>>,
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
    /// In bits from the start of the record (StarPiece's `mFlags.isGettable`: byte offset 372, bit 2977).
    pub offset: u32,
}

/// What a function row is (types TY-11).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FunctionKind {
    Free,
    Method,
    Static,
    Constructor,
    Destructor,
}

/// A function a virtual overrides directly, by the name and signature its row has or would have.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Overridden {
    pub name: String,
    pub signature: String,
    /// It is a function row.
    pub row: bool,
}

/// One row's facts.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Facts {
    pub kind: FunctionKind,
    /// The record as the types file names it; None for a free function or a record no mod can name.
    pub owner: Option<String>,
    pub is_virtual: bool,
    pub pure: bool,
    /// A const method: called on a const view.
    pub konst: bool,
    pub overrides: Vec<Overridden>,
    /// C-6: one entry per declared parameter (a method's `this` not counted): what the function does through a
    /// pointer parameter its header marks; None for no mark.
    pub marks: Vec<Option<Mark>>,
}

/// What a function does through a pointer parameter (self-description C-6, Game/LumaMarks.hpp).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Way {
    /// Reads one value, never writes it, does not keep the address.
    In,
    /// May write one value, never reads it first, does not keep the address.
    Out,
    /// Reads one value, may write it, does not keep the address.
    InOut,
    /// Keeps the address: never callable from LumaScript.
    Kept,
}

/// One parameter's mark (C-6).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Mark {
    pub way: Way,
    /// An array whose element count is the parameter at this 0-based index (an integer, itself unmarked).
    pub count: Option<usize>,
    /// An array of exactly this many elements (1 to 4096).
    pub fixed: Option<u32>,
    /// The function accepts null there.
    pub nullable: bool,
}

/// The number of parameters a row's signature declares (`bool(const Vec3f &, s32) const` -> 2): top-level commas
/// inside its last top-level parentheses, `()` and `(void)` none. None when the text has no balanced parameter list.
pub fn signature_params(signature: &str) -> Option<usize> {
    // The function's own list is the last parenthesised group at the top level: a group before it is a declarator
    // (`const f32 (*)[4]()`, a function returning a pointer to an array), and only qualifiers come after it.
    let (mut depth, mut open, mut last) = (0i32, 0usize, None);
    let mut commas = 0usize;
    let mut last_commas = 0usize;
    for (i, c) in signature.char_indices() {
        match c {
            '(' | '<' | '[' => {
                if depth == 0 && c == '(' {
                    open = i;
                    commas = 0;
                }
                depth += 1;
            }
            ')' | '>' | ']' => {
                depth -= 1;
                if depth < 0 {
                    return None;
                }
                if depth == 0 && c == ')' {
                    last = Some((open, i));
                    last_commas = commas;
                }
            }
            ',' if depth == 1 => commas += 1,
            _ => {}
        }
    }
    let (open, close) = last?;
    let inside = signature[open + 1..close].trim();
    // A variadic's `...` is not a parameter (`void(const char *, ...)` has one; its marks list has one entry).
    let named = match inside.strip_suffix("...") {
        Some(before) => {
            let before = before.trim_end();
            if before.is_empty() {
                return Some(0);
            }
            match before.strip_suffix(',') {
                Some(_) => return Some(last_commas),
                None => inside,
            }
        }
        None => inside,
    };
    Some(if named.is_empty() || named == "void" { 0 } else { last_commas + 1 })
}

/// Whether a row's signature takes a variable number of arguments: its own parameter list ends with `...`.
pub fn signature_variadic(signature: &str) -> bool {
    let Some(close) = signature.rfind(')') else { return false };
    signature[..close].trim_end().ends_with("...")
}

/// luma_functions.json (types TY-11): each row's facts by its name and signature.
#[derive(Clone, Debug, Default)]
pub struct FunctionFacts {
    pub rows: HashMap<(String, String), Facts>,
}

impl FunctionFacts {
    /// Reads a facts file. Anything not in TY-11's form, a row given twice, or facts that cannot be (a free function
    /// with a record, a const or virtual function that is no member, a pure one that is not virtual, overrides of one
    /// that is not virtual, marks not in C-6's form) is an error naming the row.
    pub fn from_json(bytes: &[u8]) -> Result<FunctionFacts, TypesError> {
        use crate::json::{Kind, Value};
        let root = crate::json::parse(bytes)
            .map_err(|e| TypesError::Json { line: e.pos.line, column: e.pos.column, what: e.what, detail: e.detail })?;
        let bad = |at: String, why: &str| TypesError::Row { at, why: why.to_string() };
        let text = |v: &Value, key: &str, at: &str| v.get(key).and_then(Value::as_str).map(str::to_string).ok_or_else(|| bad(format!("{at}.{key}"), "missing, or not text"));
        let flag = |v: &Value, key: &str, at: &str| v.get(key).and_then(Value::as_bool).ok_or_else(|| bad(format!("{at}.{key}"), "missing, or not true or false"));
        let list = root.get("functions").filter(|v| v.is_array()).map(Value::items).ok_or_else(|| bad("functions".into(), "missing, or not a list"))?;
        let mut facts = FunctionFacts::default();
        for (i, row) in list.iter().enumerate() {
            let at = format!("functions[{i}]");
            let name = text(row, "name", &at)?;
            let signature = text(row, "signature", &at)?;
            let kind = match text(row, "kind", &at)?.as_str() {
                "free" => FunctionKind::Free,
                "method" => FunctionKind::Method,
                "static" => FunctionKind::Static,
                "constructor" => FunctionKind::Constructor,
                "destructor" => FunctionKind::Destructor,
                _ => return Err(bad(format!("{at}.kind"), "not free, method, static, constructor or destructor")),
            };
            let owner = match row.get("owner").map(|v| &v.kind) {
                Some(Kind::Null) => None,
                _ => Some(text(row, "owner", &at)?),
            };
            let (is_virtual, pure, konst) = (flag(row, "virtual", &at)?, flag(row, "pure", &at)?, flag(row, "const", &at)?);
            let members = matches!(kind, FunctionKind::Method | FunctionKind::Constructor | FunctionKind::Destructor);
            if kind == FunctionKind::Free && owner.is_some() {
                return Err(bad(format!("{at}.owner"), "a free function has no record"));
            }
            if konst && kind != FunctionKind::Method || is_virtual && !members || pure && !is_virtual {
                return Err(bad(at, "const only on a method, virtual only on a member, pure only on a virtual"));
            }
            let mut overrides = Vec::new();
            let over = row.get("overrides").filter(|v| v.is_array()).map(Value::items).ok_or_else(|| bad(format!("{at}.overrides"), "missing, or not a list"))?;
            for (k, o) in over.iter().enumerate() {
                let oat = format!("{at}.overrides[{k}]");
                overrides.push(Overridden { name: text(o, "name", &oat)?, signature: text(o, "signature", &oat)?, row: flag(o, "row", &oat)? });
            }
            if !overrides.is_empty() && !is_virtual {
                return Err(bad(format!("{at}.overrides"), "only a virtual overrides"));
            }
            let marks = Self::marks(row, &at, &signature)?;
            if facts.rows.insert((name, signature), Facts { kind, owner, is_virtual, pure, konst, overrides, marks }).is_some() {
                return Err(bad(at, "a row given twice"));
            }
        }
        Ok(facts)
    }

    /// A row's "marks" (C-6, G-C6-1 and 2): a list with one entry per parameter of its signature, each null or an
    /// object of exactly `way`, `count`, `fixed`, `nullable`. Any other form is an error naming the row and the entry.
    fn marks(row: &crate::json::Value, at: &str, signature: &str) -> Result<Vec<Option<Mark>>, TypesError> {
        use crate::json::{Kind, Value};
        let bad = |at: String, why: &str| TypesError::Row { at, why: why.to_string() };
        let at = format!("{at}.marks");
        let list = row.get("marks").filter(|v| v.is_array()).map(Value::items).ok_or_else(|| bad(at.clone(), "missing, or not a list"))?;
        let n = signature_params(signature).ok_or_else(|| bad(at.clone(), "the row's signature has no parameter list"))?;
        if list.len() != n {
            return Err(bad(at, &format!("{} entries for {n} parameters", list.len())));
        }
        let mut marks = Vec::with_capacity(n);
        for (i, v) in list.iter().enumerate() {
            let eat = format!("{at}[{i}]");
            if matches!(v.kind, Kind::Null) {
                marks.push(None);
                continue;
            }
            if !v.is_object() || v.members().len() != 4 || ["way", "count", "fixed", "nullable"].iter().any(|k| v.get(k).is_none()) {
                return Err(bad(eat, "not null, nor an object of exactly way, count, fixed and nullable"));
            }
            let way = match v.get("way").and_then(Value::as_str) {
                Some("in") => Way::In,
                Some("out") => Way::Out,
                Some("inout") => Way::InOut,
                Some("kept") => Way::Kept,
                _ => return Err(bad(format!("{eat}.way"), "not in, out, inout or kept")),
            };
            // A whole number from `low` to `high`, or null.
            let whole = |key: &str, low: i64, high: i64| -> Result<Option<i64>, TypesError> {
                let x = v.get(key).expect("checked above");
                if matches!(x.kind, Kind::Null) {
                    return Ok(None);
                }
                match x.as_i64() {
                    Some(k) if (low..=high).contains(&k) => Ok(Some(k)),
                    _ => Err(bad(format!("{eat}.{key}"), &format!("not null or a whole number from {low} to {high}"))),
                }
            };
            let count = whole("count", 0, n as i64 - 1)?.map(|k| k as usize);
            let fixed = whole("fixed", 1, 4096)?.map(|k| k as u32);
            let nullable = v.get("nullable").and_then(Value::as_bool).ok_or_else(|| bad(format!("{eat}.nullable"), "not true or false"))?;
            if count.is_some() && fixed.is_some() {
                return Err(bad(eat, "count and fixed both set"));
            }
            // A kept pointer is one place, never a list; it may be nullable (C-6 as F changed it 11-12: the game takes
            // null there, which is all a script can give it).
            if way == Way::Kept && (count.is_some() || fixed.is_some()) {
                return Err(bad(eat, "kept with a count or a fixed count"));
            }
            if count == Some(i) {
                return Err(bad(format!("{eat}.count"), "its own parameter"));
            }
            marks.push(Some(Mark { way, count, fixed, nullable }));
        }
        // A count parameter is an integer, so it carries no mark itself.
        for (i, m) in marks.iter().enumerate() {
            if let Some(c) = m.and_then(|m| m.count) {
                if marks[c].is_some() {
                    return Err(bad(format!("{at}[{i}].count"), "names a parameter that is itself marked"));
                }
            }
        }
        Ok(marks)
    }

    /// A row's facts.
    pub fn get(&self, name: &str, signature: &str) -> Option<&Facts> {
        self.rows.get(&(name.to_string(), signature.to_string()))
    }
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
                    // Its virtual declarations (objects 7a): name, signature, pure, and whether a mod can bind it.
                    if let Some(list) = row.get("virtuals") {
                        let list = list.is_array().then(|| list.items()).ok_or_else(|| bad(format!("{at}.virtuals"), "not a list"))?;
                        let methods = types.methods.get_or_insert_with(HashMap::new);
                        for (k, v) in list.iter().enumerate() {
                            let at = format!("{at}.virtuals[{k}]");
                            let (vname, sig) = (text(v, "name", &at)?, text(v, "signature", &at)?);
                            flag(v, "pure", &at)?;
                            let has_row = flag(v, "row", &at)?;
                            if methods.insert((vname.clone(), sig.clone()), has_row).is_some() {
                                return Err(bad(at, &format!("a second virtual \"{vname}\" \"{sig}\"")));
                            }
                        }
                    }
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
