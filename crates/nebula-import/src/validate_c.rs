//! The validator as C functions, for the game (validator contract 3): `include/luma_validate.h` declares them. The
//! game calls `luma_validate` with no kept report at import (every rule) and with the mod's kept report at each start
//! (the rules that read the whole `.luma` are taken from it when it is for the same `.luma` and the same game). The
//! findings and their text are those of `luma_validate::validate`, field for field.

use luma_validate::{message, message_of, phrase, reason_of, recheck, Game, Input, Installed, Level, Names, Report, Saved, Source, Store, Table};
use std::cell::RefCell;
use std::collections::BTreeMap;
use std::ffi::{c_char, c_void, CStr, CString};
use std::path::PathBuf;

/// Called by a store's `folder` once for each name directly in the folder (the game's case; non-zero: a folder).
pub type LumaFolderSink = unsafe extern "C" fn(sink_context: *mut c_void, name: *const c_char, is_folder: u32);

#[repr(C)]
pub struct LumaGameStore {
    /// `size_of::<LumaGameStore>()`.
    pub size: u32,
    pub context: *mut c_void,
    /// The game's own case of a lowercase path, NUL-terminated in `out` when it fits; returns its length in bytes (0:
    /// the game has no such path).
    pub path: Option<unsafe extern "C" fn(context: *mut c_void, lower: *const c_char, out: *mut c_char, out_size: u32) -> u32>,
    /// The file at `path` when it is a table: its cooked bytes (NTBL) and its path in the store (null: `path`);
    /// 0 when it is no table. Valid until the next call on this store.
    pub table: Option<
        unsafe extern "C" fn(context: *mut c_void, path: *const c_char, bytes: *mut *const u8, bytes_size: *mut u64, store_path: *mut *const c_char) -> u32,
    >,
    /// The names directly in the folder at `lower`, each through `sink`; how many, or -1 when the path is no folder.
    pub folder: Option<unsafe extern "C" fn(context: *mut c_void, lower: *const c_char, sink: LumaFolderSink, sink_context: *mut c_void) -> i32>,
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

thread_local! {
    /// The text luma_validate_message and luma_validate_reason gave last on this thread.
    static SAID: RefCell<CString> = RefCell::new(CString::default());
}

/// The parameters a caller gives by name: an entry whose name or value is null is left out (the template then shows
/// `{name}`, so it is seen).
unsafe fn given(names: *const *const c_char, values: *const *const c_char, count: u32) -> Vec<(String, String)> {
    let names = list(names, count);
    let values = list(values, count);
    names.iter().zip(values).filter_map(|(n, v)| Some((text(*n)?, text(*v)?))).collect()
}

unsafe fn say(code: u16, names: *const *const c_char, values: *const *const c_char, count: u32, language: *const c_char,
              how: fn(u16, &[(&str, &str)], &str) -> String) -> *const c_char {
    let args = given(names, values, count);
    let args: Vec<(&str, &str)> = args.iter().map(|(n, v)| (n.as_str(), v.as_str())).collect();
    let language = text(language).unwrap_or_default();
    // As luma_validate: no panic crosses into C (audits/3.11.md, 09:12 note). None is known; the id stays visible.
    let said = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| how(code, &args, &language))).unwrap_or_else(|_| format!("{{L{code}}}"));
    let said = c(&said);
    SAID.with(|s| {
        *s.borrow_mut() = said;
        s.borrow().as_ptr()
    })
}

/// Rule `code`'s message with its parameters (`names[i]` = `values[i]`; a value `T:<text>` or plain is a literal,
/// `P:<id>` a phrase; the place as `mod`, `file`, `line`, `column`) in `language`. Valid until the next call of this
/// or luma_validate_reason on the same thread.
///
/// # Safety
/// `names` and `values` are null or hold `count` pointers, each null or NUL-terminated; `language` null or NUL-terminated.
#[no_mangle]
pub unsafe extern "C" fn luma_validate_message(code: u16, names: *const *const c_char, values: *const *const c_char, count: u32,
                                               language: *const c_char) -> *const c_char {
    say(code, names, values, count, language, message_of)
}

/// Why, without where: the `why` or `found` phrase filled with the parameters (else the whole message), as a screen
/// that names the mod and the file itself says it (the Mods screen). As luma_validate_message otherwise.
///
/// # Safety
/// As luma_validate_message.
#[no_mangle]
pub unsafe extern "C" fn luma_validate_reason(code: u16, names: *const *const c_char, values: *const *const c_char, count: u32,
                                              language: *const c_char) -> *const c_char {
    say(code, names, values, count, language, reason_of)
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
        // As the pak store does (store_view): the cooked table read by the cooker's own reader, its view built by the
        // same code; bytes that do not read as a table are no table (the patch rules then name the file as one
        // without rows).
        let store = unsafe { &*self.0 };
        let f = store.table?;
        let asked = c(path);
        let (mut bytes, mut size, mut store_path) = (std::ptr::null::<u8>(), 0u64, std::ptr::null::<c_char>());
        if unsafe { f(store.context, asked.as_ptr(), &mut bytes, &mut size, &mut store_path) } == 0 || bytes.is_null() {
            return None;
        }
        let bytes = unsafe { std::slice::from_raw_parts(bytes, usize::try_from(size).ok()?) };
        let ir = crate::cook::table::TableIr::from_ntbl(bytes).ok()?;
        let at = unsafe { text(store_path) }.unwrap_or_else(|| path.to_string());
        Some(crate::store_view::table_view(&crate::cook::store::store_path(at.trim_start_matches('/')), &ir))
    }
    fn read_table(&self, path: &str, bytes: &[u8]) -> Option<Table> {
        // Read here, as LumaLink's pak store reads it: the game's store gives only its own tables.
        crate::store_view::view_of_bytes(path, bytes)
    }
    // The kinds the game finds by name are this library's own list, as LumaLink's PakStore reads it (F INBOX-G 46).
    fn may_be_new(&self, path: &str) -> bool {
        crate::store_view::may_be_new(path)
    }
    fn folder(&self, lower: &str) -> Option<Vec<(String, bool)>> {
        unsafe extern "C" fn sink(context: *mut c_void, name: *const c_char, is_folder: u32) {
            let names = &mut *context.cast::<Vec<(String, bool)>>();
            if let Some(name) = text(name).filter(|n| !n.is_empty()) {
                names.push((name, is_folder != 0));
            }
        }
        let store = unsafe { &*self.0 };
        let f = store.folder?;
        let lower = c(lower);
        let mut names: Vec<(String, bool)> = Vec::new();
        let n = unsafe { f(store.context, lower.as_ptr(), sink, (&mut names as *mut Vec<(String, bool)>).cast()) };
        if n < 0 {
            return None;
        }
        // One entry per name, whatever order or repeats the store gave.
        names.sort();
        names.dedup_by(|a, b| a.0 == b.0 && {
            b.1 |= a.1;
            true
        });
        Some(names)
    }
}

/// The validator's view of a C store, for tests of the C interface.
#[doc(hidden)]
pub fn store_for_tests(store: &LumaGameStore) -> impl Store + '_ {
    CStore(store)
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
        if s.size as usize != std::mem::size_of::<LumaGameStore>() {
            return Err(format!("store: size is {}, sizeof(LumaGameStore) is {} (luma_validate.h of another revision)", s.size, std::mem::size_of::<LumaGameStore>()));
        }
        if s.path.is_none() || s.table.is_none() || s.folder.is_none() {
            return Err("store: path, table and folder are each needed".to_string());
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
