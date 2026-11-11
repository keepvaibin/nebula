//! The validator as C functions, for the game (validator contract 3): `include/luma_validate.h` declares them. The
//! game calls `luma_validate` with no kept report at import (every rule) and with the mod's kept report at each start
//! (the rules that read the whole `.luma` are taken from it when it is for the same `.luma` and the same game). The
//! findings and their text are those of `luma_validate::validate`, field for field.

use luma_validate::{message, phrase, recheck, ColumnType, Game, Input, Installed, Level, Names, Report, Saved, Source, Store, Table};
use std::cell::RefCell;
use std::collections::{BTreeMap, HashSet};
use std::ffi::{c_char, c_void, CStr, CString};
use std::path::PathBuf;

#[repr(C)]
pub struct LumaTableColumn {
    pub name: *const c_char,
    /// 0 an integer from `min` to `max`, 1 a float, 2 text.
    pub kind: u32,
    pub min: i64,
    pub max: i64,
}

#[repr(C)]
pub struct LumaTable {
    pub key: *const *const c_char,
    pub key_count: u32,
    pub columns: *const LumaTableColumn,
    pub column_count: u32,
    /// Each row's key values joined by U+001F; null when the rows are not known.
    pub rows: *const *const c_char,
    pub row_count: u32,
    pub case_insensitive: u32,
}

#[repr(C)]
pub struct LumaGameStore {
    pub context: *mut c_void,
    /// The game's own case of a lowercase path, NUL-terminated in `out` when it fits; returns its length in bytes (0:
    /// the game has no such path).
    pub path: Option<unsafe extern "C" fn(context: *mut c_void, lower: *const c_char, out: *mut c_char, out_size: u32) -> u32>,
    /// A path's rows; null when the file has none. Valid until the next call on this store.
    pub table: Option<unsafe extern "C" fn(context: *mut c_void, path: *const c_char) -> *const LumaTable>,
    /// Non-zero when a file at a path the game does not have may be new (a kind the game finds by name).
    pub may_be_new: Option<unsafe extern "C" fn(context: *mut c_void, path: *const c_char) -> u32>,
}

#[repr(C)]
pub struct LumaInstalledMod {
    /// Its file name, to name it in messages.
    pub file: *const c_char,
    pub on: u32,
    /// Its kept report (luma_report_kept).
    pub kept: *const u8,
    pub kept_size: u64,
}

#[repr(C)]
pub struct LumaOption {
    pub key: *const c_char,
    pub value: *const c_char,
}

#[repr(C)]
pub struct LumaValidateInput {
    pub size: u32,
    pub path: *const c_char,
    pub platform: *const c_char,
    pub nebula: *const c_char,
    pub region: *const c_char,
    pub languages: *const *const c_char,
    pub language_count: u32,
    pub names: *const c_char,
    pub names_size: u64,
    pub luma_api: u32,
    pub permissions: *const *const c_char,
    pub permission_count: u32,
    pub store: *const LumaGameStore,
    pub store_key: *const c_char,
    pub installed: *const LumaInstalledMod,
    pub installed_count: u32,
    pub position: u32,
    pub options: *const LumaOption,
    pub option_count: u32,
    pub kept: *const u8,
    pub kept_size: u64,
}

#[repr(C)]
pub struct LumaFinding {
    pub code: u32,
    /// 0 an error (the mod is refused or off), 1 a note.
    pub level: u32,
    pub mod_id: *const c_char,
    pub file: *const c_char,
    /// 0 when the finding has no line or column.
    pub line: u32,
    pub column: u32,
    pub pointer: *const c_char,
    pub text: *const c_char,
}

/// What `luma_validate` returns.
pub struct LumaReport {
    /// None when the input was not valid.
    report: Option<Report>,
    kept: Vec<u8>,
    strings: Vec<[Option<CString>; 3]>,
    id: Option<CString>,
    version: Option<CString>,
    shown: RefCell<Option<(CString, LumaFinding)>>,
    shown_not_checked: RefCell<Option<CString>>,
    /// Why the input was not valid (for the developer's log, in English).
    invalid: Option<CString>,
}

unsafe fn text(p: *const c_char) -> Option<String> {
    if p.is_null() {
        return None;
    }
    Some(CStr::from_ptr(p).to_string_lossy().into_owned())
}

unsafe fn list<'a, T>(p: *const T, n: u32) -> &'a [T] {
    if p.is_null() || n == 0 {
        &[]
    } else {
        std::slice::from_raw_parts(p, n as usize)
    }
}

fn c(s: &str) -> CString {
    CString::new(s.replace('\0', " ")).unwrap_or_default()
}

struct CStore(*const LumaGameStore);

impl Store for CStore {
    fn path(&self, lower: &str) -> Option<String> {
        let store = unsafe { &*self.0 };
        let f = store.path?;
        let lower = c(lower);
        let mut out = vec![0u8; 1024];
        loop {
            let n = unsafe { f(store.context, lower.as_ptr(), out.as_mut_ptr().cast(), out.len() as u32) } as usize;
            if n == 0 {
                return None;
            }
            if n < out.len() {
                return Some(String::from_utf8_lossy(&out[..n]).into_owned());
            }
            if n >= 1 << 20 {
                return None;
            }
            out = vec![0u8; n + 1];
        }
    }
    fn table(&self, path: &str) -> Option<Table> {
        let store = unsafe { &*self.0 };
        let f = store.table?;
        let path = c(path);
        let t = unsafe { f(store.context, path.as_ptr()) };
        if t.is_null() {
            return None;
        }
        unsafe {
            let t = &*t;
            let key = list(t.key, t.key_count).iter().filter_map(|&k| text(k)).collect();
            let columns = list(t.columns, t.column_count)
                .iter()
                .filter_map(|col| {
                    let kind = match col.kind {
                        0 => ColumnType::Int { min: col.min, max: col.max },
                        1 => ColumnType::Float,
                        _ => ColumnType::Text,
                    };
                    Some((text(col.name)?, kind))
                })
                .collect();
            let rows = (!t.rows.is_null()).then(|| list(t.rows, t.row_count).iter().filter_map(|&r| text(r)).collect::<HashSet<_>>());
            Some(Table { key, columns, rows, case_insensitive: t.case_insensitive != 0 })
        }
    }
    fn may_be_new(&self, path: &str) -> bool {
        let store = unsafe { &*self.0 };
        let Some(f) = store.may_be_new else { return false };
        let path = c(path);
        unsafe { f(store.context, path.as_ptr()) != 0 }
    }
}

fn hex_sha1(bytes: &[u8]) -> String {
    use sha1::{Digest, Sha1};
    Sha1::digest(bytes).iter().map(|b| format!("{b:02x}")).collect()
}

fn invalid(why: &str) -> *mut LumaReport {
    Box::into_raw(Box::new(LumaReport {
        report: None,
        kept: Vec::new(),
        strings: Vec::new(),
        id: None,
        version: None,
        shown: RefCell::new(None),
        shown_not_checked: RefCell::new(None),
        invalid: Some(c(why)),
    }))
}

unsafe fn run(input: &LumaValidateInput) -> Result<LumaReport, String> {
    let path = PathBuf::from(text(input.path).ok_or("path is null")?);
    let platform = text(input.platform).ok_or("platform is null")?;
    if !luma_validate::PLATFORMS.contains(&platform.as_str()) {
        return Err(format!("platform \"{platform}\" is not one of luma-format's"));
    }
    let nebula_text = text(input.nebula).ok_or("nebula is null")?;
    let nebula = luma_validate::semver::Version::parse(&nebula_text).ok_or_else(|| format!("nebula \"{nebula_text}\" is not a version"))?;
    let region = text(input.region);
    let languages: Vec<String> = list(input.languages, input.language_count).iter().filter_map(|&l| text(l)).collect();
    let names_text = (!input.names.is_null()).then(|| {
        let bytes = std::slice::from_raw_parts(input.names.cast::<u8>(), input.names_size as usize);
        String::from_utf8_lossy(bytes).into_owned()
    });
    let names = match &names_text {
        Some(t) => {
            let mut n = Names { api: input.luma_api, ..Names::default() };
            n.add_rows(t).map_err(|line| format!("names line {line}: not name, signature, kind, line"))?;
            n.permissions = (!input.permissions.is_null()).then(|| list(input.permissions, input.permission_count).iter().filter_map(|&p| text(p)).collect());
            Some(n)
        }
        None => None,
    };
    if let Some(s) = input.store.as_ref() {
        if s.path.is_none() || s.table.is_none() || s.may_be_new.is_none() {
            return Err("store: path, table and may_be_new are each needed".to_string());
        }
    }
    let store = (!input.store.is_null()).then(|| CStore(input.store));
    let store_key = text(input.store_key).unwrap_or_default();
    // What identifies the game for a kept report: a change in any of it checks every rule again.
    let mut perms: Vec<String> = names.as_ref().and_then(|n| n.permissions.clone()).unwrap_or_default();
    perms.sort();
    let game_key = format!(
        "nebula {nebula}; region {}; platform {platform}; languages {}; api {}; names {}; permissions {}; store {}",
        region.as_deref().unwrap_or("-"),
        languages.join(","),
        input.luma_api,
        names_text.as_deref().map_or_else(|| "-".to_string(), |t| hex_sha1(t.as_bytes())),
        perms.join(","),
        if store.is_some() { store_key.as_str() } else { "-" },
    );
    // Every other mod is given by its kept report; one that does not read is the caller's fault, never skipped.
    let mut installed: Vec<Installed> = Vec::new();
    for (i, m) in list(input.installed, input.installed_count).iter().enumerate() {
        let file = text(m.file).ok_or_else(|| format!("installed[{i}]: file is null"))?;
        let saved = (!m.kept.is_null())
            .then(|| std::slice::from_raw_parts(m.kept, m.kept_size as usize))
            .and_then(Saved::parse)
            .ok_or_else(|| format!("installed[{i}] {file}: its kept report does not read; check it first"))?;
        match saved.installed(&file, m.on != 0) {
            Some(mod_) => installed.push(mod_),
            // A mod whose luma.json gives no id was refused; it can only be off.
            None if m.on == 0 => {}
            None => return Err(format!("installed[{i}] {file}: on, but its kept report has no id (it was refused)")),
        }
    }
    let options: Option<BTreeMap<String, String>> =
        (!input.options.is_null()).then(|| list(input.options, input.option_count).iter().filter_map(|o| Some((text(o.key)?, text(o.value)?))).collect());
    let kept = (!input.kept.is_null()).then(|| std::slice::from_raw_parts(input.kept, input.kept_size as usize)).and_then(Saved::parse);

    let game = Game {
        nebula,
        region: region.as_deref(),
        extra_languages: &languages,
        names: names.as_ref(),
        store: store.as_ref().map(|s| s as &dyn Store),
    };
    let platforms = [platform.as_str()];
    let position = (input.position != u32::MAX).then_some(input.position as usize);
    let report = recheck(
        &Input { source: Source::File(&path), game: Some(&game), installed: &installed, platforms: &platforms, options: options.as_ref(), position },
        kept.as_ref(),
        &game_key,
    );
    let kept = Saved::from_report(&report, &game_key).to_bytes();
    let strings = report
        .findings
        .iter()
        .map(|f| [f.mod_id.as_deref().map(c), Some(c(&f.file)), f.pointer.as_deref().map(c)])
        .collect();
    let id = report.about.as_ref().map(|a| c(&a.id));
    let version = report.about.as_ref().map(|a| c(&a.version.to_string()));
    Ok(LumaReport { report: Some(report), kept, strings, id, version, shown: RefCell::new(None), shown_not_checked: RefCell::new(None), invalid: None })
}

/// Validates the mod at `input->path` against the running game. Never null; give it back to luma_report_free.
///
/// # Safety
/// `input` is null or points at a LumaValidateInput whose pointers are null or valid for the counts and sizes given,
/// and whose store's functions may be called on this thread until this returns.
#[no_mangle]
pub unsafe extern "C" fn luma_validate(input: *const LumaValidateInput) -> *mut LumaReport {
    if input.is_null() {
        return invalid("input is null");
    }
    if (*input).size as usize != std::mem::size_of::<LumaValidateInput>() {
        return invalid(&format!("input size is {}, not sizeof(LumaValidateInput) = {}", (*input).size, std::mem::size_of::<LumaValidateInput>()));
    }
    let input = &*input;
    match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| run(input))) {
        Ok(Ok(r)) => Box::into_raw(Box::new(r)),
        Ok(Err(why)) => invalid(&why),
        Err(payload) => invalid(&format!("the validator failed (a bug): {}", panic_text(payload.as_ref()))),
    }
}

/// A panic's message, for the log: the store's functions are C and cannot unwind into Rust, so a panic caught at
/// this boundary is the Rust side's own bug (3.1 V-2).
pub(crate) fn panic_text(payload: &(dyn std::any::Any + Send)) -> String {
    if let Some(text) = payload.downcast_ref::<&str>() {
        (*text).to_string()
    } else if let Some(text) = payload.downcast_ref::<String>() {
        text.clone()
    } else {
        "no message".to_string()
    }
}

#[cfg(test)]
mod panic_tests {
    #[test]
    fn a_panic_names_its_message() {
        let a = std::panic::catch_unwind(|| panic!("index 7 past 3")).unwrap_err();
        assert_eq!(super::panic_text(a.as_ref()), "index 7 past 3");
        let n = 7;
        let b = std::panic::catch_unwind(|| panic!("index {n} past 3")).unwrap_err();
        assert_eq!(super::panic_text(b.as_ref()), "index 7 past 3");
        let c = std::panic::catch_unwind(|| std::panic::panic_any(5u8)).unwrap_err();
        assert_eq!(super::panic_text(c.as_ref()), "no message");
    }
}

/// Why the input was not valid (luma_report_state -1), in English for the developer's log; null otherwise.
///
/// # Safety
/// As luma_report_state.
#[no_mangle]
pub unsafe extern "C" fn luma_report_invalid(report: *const LumaReport) -> *const c_char {
    report.as_ref().and_then(|r| r.invalid.as_ref()).map_or(std::ptr::null(), |c| c.as_ptr())
}

/// 0: every rule ran; 1: a start's check (the whole-file rules from the kept report); -1: the input was not valid
/// (luma_report_invalid says why).
///
/// # Safety
/// `report` is null or from luma_validate, not yet freed.
#[no_mangle]
pub unsafe extern "C" fn luma_report_state(report: *const LumaReport) -> i32 {
    match report.as_ref().and_then(|r| r.report.as_ref()) {
        Some(r) if r.full => 0,
        Some(_) => 1,
        None => -1,
    }
}

/// How many findings; luma_report_errors counts those that refuse the mod or leave it off.
///
/// # Safety
/// As luma_report_state.
#[no_mangle]
pub unsafe extern "C" fn luma_report_count(report: *const LumaReport) -> u32 {
    report.as_ref().and_then(|r| r.report.as_ref()).map_or(0, |r| r.findings.len() as u32)
}

/// # Safety
/// As luma_report_state.
#[no_mangle]
pub unsafe extern "C" fn luma_report_errors(report: *const LumaReport) -> u32 {
    report.as_ref().and_then(|r| r.report.as_ref()).map_or(0, |r| r.errors() as u32)
}

/// Finding `index` with its text in `language` (a name of the game's: "UsEnglish", "EuGerman"...; null or unknown is
/// UsEnglish). Null past the last. Valid until the next luma_report_at on this report, or its free.
///
/// # Safety
/// As luma_report_state; `language` is null or NUL-terminated.
#[no_mangle]
pub unsafe extern "C" fn luma_report_at(report: *const LumaReport, index: u32, language: *const c_char) -> *const LumaFinding {
    let Some(r) = report.as_ref() else { return std::ptr::null() };
    let Some(rep) = r.report.as_ref() else { return std::ptr::null() };
    let Some(f) = rep.findings.get(index as usize) else { return std::ptr::null() };
    let language = text(language).unwrap_or_default();
    let s = &r.strings[index as usize];
    let ptr = |x: &Option<CString>| x.as_ref().map_or(std::ptr::null(), |c| c.as_ptr());
    let body = c(&message(f, &language));
    let finding = LumaFinding {
        code: f.code as u32,
        level: if f.level == Level::Error { 0 } else { 1 },
        mod_id: ptr(&s[0]),
        file: ptr(&s[1]),
        line: f.line.unwrap_or(0),
        column: f.column.unwrap_or(0),
        pointer: ptr(&s[2]),
        text: body.as_ptr(),
    };
    let mut shown = r.shown.borrow_mut();
    *shown = Some((body, finding));
    shown.as_ref().map_or(std::ptr::null(), |(_, f)| f as *const LumaFinding)
}

/// How many rules could not be checked for want of an input; each with luma_report_not_checked_at.
///
/// # Safety
/// As luma_report_state.
#[no_mangle]
pub unsafe extern "C" fn luma_report_not_checked_count(report: *const LumaReport) -> u32 {
    report.as_ref().and_then(|r| r.report.as_ref()).map_or(0, |r| r.not_checked.len() as u32)
}

/// The rule's code in `*code` and what was missing as text in `language`. Null past the last. Valid until the next
/// call of this function on this report, or its free.
///
/// # Safety
/// As luma_report_at; `code` is null or writable.
#[no_mangle]
pub unsafe extern "C" fn luma_report_not_checked_at(report: *const LumaReport, index: u32, language: *const c_char, code: *mut u32) -> *const c_char {
    let Some(r) = report.as_ref() else { return std::ptr::null() };
    let Some(n) = r.report.as_ref().and_then(|rep| rep.not_checked.get(index as usize)) else { return std::ptr::null() };
    if let Some(code) = code.as_mut() {
        *code = n.code as u32;
    }
    let mut shown = r.shown_not_checked.borrow_mut();
    *shown = Some(c(&phrase(n.because, &text(language).unwrap_or_default())));
    shown.as_ref().map_or(std::ptr::null(), |s| s.as_ptr())
}

/// The bytes the game keeps beside the mod and gives back as `kept` (its own check at each start) and in the other
/// mods' `installed`. Valid until luma_report_free.
///
/// # Safety
/// As luma_report_state; `size` is null or writable.
#[no_mangle]
pub unsafe extern "C" fn luma_report_kept(report: *const LumaReport, size: *mut u64) -> *const u8 {
    let r = report.as_ref().filter(|r| r.report.is_some());
    if let Some(size) = size.as_mut() {
        *size = r.map_or(0, |r| r.kept.len() as u64);
    }
    r.map_or(std::ptr::null(), |r| r.kept.as_ptr())
}

/// The mod's id and version from its luma.json; null when it does not give them well formed.
///
/// # Safety
/// As luma_report_state.
#[no_mangle]
pub unsafe extern "C" fn luma_report_id(report: *const LumaReport) -> *const c_char {
    report.as_ref().and_then(|r| r.id.as_ref()).map_or(std::ptr::null(), |c| c.as_ptr())
}

/// # Safety
/// As luma_report_state.
#[no_mangle]
pub unsafe extern "C" fn luma_report_version(report: *const LumaReport) -> *const c_char {
    report.as_ref().and_then(|r| r.version.as_ref()).map_or(std::ptr::null(), |c| c.as_ptr())
}

/// # Safety
/// `report` is null or from luma_validate, given back once.
#[no_mangle]
pub unsafe extern "C" fn luma_report_free(report: *mut LumaReport) {
    if !report.is_null() {
        drop(Box::from_raw(report));
    }
}
