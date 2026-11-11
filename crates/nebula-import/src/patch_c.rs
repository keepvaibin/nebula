//! The applier's C interface (`include/luma_patch.h`, contract table-keys 8): the game gives one cooked table and the
//! patches of the mods that are on, and takes back the patched table and the facts for the Mods screen.

use crate::cook::patch::{apply_linked, Fact, Link, ModPatch};
use crate::cook::table::TableIr;
use luma_validate::Finding;
use std::cell::RefCell;
use std::ffi::{c_char, CStr, CString};
use std::ptr;

#[repr(C)]
pub struct LumaTablePatch {
    pub mod_id: *const c_char,
    pub file: *const c_char,
    pub json: *const u8,
    pub json_size: u64,
}

#[repr(C)]
pub struct LumaLinkTable {
    pub path: *const c_char,
    pub table: *const u8,
    pub table_size: u64,
    pub patches: *const LumaTablePatch,
    pub patch_count: u32,
}

#[repr(C)]
pub struct LumaPatchInput {
    pub size: u32,
    pub path: *const c_char,
    pub table: *const u8,
    pub table_size: u64,
    pub patches: *const LumaTablePatch,
    pub patch_count: u32,
    pub links: *const LumaLinkTable,
    pub link_count: u32,
    pub mods: *const *const c_char,
    pub mod_count: u32,
}

/// A patch read from the C input: its mod, its file, its JSON.
type OwnedPatch = (String, String, serde_json::Value);

unsafe fn slice<'a, T>(p: *const T, count: u32, what: &str) -> Result<&'a [T], String> {
    if count == 0 {
        Ok(&[])
    } else if p.is_null() {
        Err(format!("the {what} are null"))
    } else {
        Ok(std::slice::from_raw_parts(p, count as usize))
    }
}

pub const LUMA_PATCH_CONFLICT: u32 = 0;
pub const LUMA_PATCH_MISSING: u32 = 1;
pub const LUMA_PATCH_SELECTED: u32 = 2;
pub const LUMA_PATCH_FAILED: u32 = 3;

#[repr(C)]
pub struct LumaPatchFact {
    pub kind: u32,
    pub mod_id: *const c_char,
    pub file: *const c_char,
    pub other: *const c_char,
    pub row: *const c_char,
    pub column: *const c_char,
    pub why: *const c_char,
    pub selector: u32,
    pub rows: u32,
    pub code: u32,
}

pub struct LumaPatched {
    error: Option<CString>,
    table: Vec<u8>,
    /// The texts the facts point into; a CString's bytes stay put when this list grows.
    texts: Vec<CString>,
    facts: Vec<LumaPatchFact>,
    /// Each fact's validator finding (LUMA_PATCH_FAILED), for its reason in a language.
    refusals: Vec<Option<Finding>>,
    /// Reasons said so far, kept until the result is freed.
    said: RefCell<Vec<CString>>,
}

fn cstring(text: &str) -> CString {
    CString::new(text.replace('\0', "\u{fffd}")).expect("no NUL is left")
}

unsafe fn text<'a>(p: *const c_char) -> Option<&'a str> {
    if p.is_null() {
        None
    } else {
        CStr::from_ptr(p).to_str().ok()
    }
}

/// A text kept in `texts`, as a pointer the facts hold (null for none).
fn put(texts: &mut Vec<CString>, s: Option<&str>) -> *const c_char {
    match s {
        None => ptr::null(),
        Some(s) => {
            texts.push(cstring(s));
            texts.last().expect("just pushed").as_ptr()
        }
    }
}

unsafe fn bytes<'a>(p: *const u8, size: u64) -> Option<&'a [u8]> {
    if p.is_null() {
        return (size == 0).then_some(&[]);
    }
    Some(std::slice::from_raw_parts(p, usize::try_from(size).ok()?))
}

fn failed(why: &str) -> *mut LumaPatched {
    Box::into_raw(Box::new(LumaPatched {
        error: Some(cstring(why)),
        table: Vec::new(),
        texts: Vec::new(),
        facts: Vec::new(),
        refusals: Vec::new(),
        said: RefCell::new(Vec::new()),
    }))
}

unsafe fn run(input: *const LumaPatchInput) -> Result<LumaPatched, String> {
    if input.is_null() {
        return Err("the input is null".into());
    }
    if (*input).size as usize != std::mem::size_of::<LumaPatchInput>() {
        return Err(format!("the input's size is {}, not sizeof(LumaPatchInput) = {}", (*input).size, std::mem::size_of::<LumaPatchInput>()));
    }
    let input = &*input;
    let path = text(input.path).ok_or("the path is null or not UTF-8")?;
    let table_bytes = bytes(input.table, input.table_size).ok_or("the table is null")?;
    let mut table = TableIr::from_ntbl(table_bytes).map_err(|e| format!("{path}: {e:#}"))?;
    let given = slice(input.patches, input.patch_count, "patches")?;

    // A patch that is not JSON is a mod that cannot apply, named like any other.
    let mut owned = Vec::with_capacity(given.len());
    let mut unread = Vec::new();
    for (i, p) in given.iter().enumerate() {
        let mod_id = text(p.mod_id).ok_or_else(|| format!("patch {i}: its mod id is null or not UTF-8"))?.to_string();
        let file = text(p.file).ok_or_else(|| format!("patch {i}: its file is null or not UTF-8"))?.to_string();
        let json = bytes(p.json, p.json_size).ok_or_else(|| format!("{mod_id}: {file}: its bytes are null"))?;
        match serde_json::from_slice::<serde_json::Value>(json) {
            Ok(value) => owned.push((mod_id, file, value)),
            // The validator's own reading names the fault as its L400 does (Json.Truncated, Json.Comment...).
            Err(_) => unread.push(crate::cook::patch::unreadable(path, &mod_id, &file, json)),
        }
    }
    let patches: Vec<ModPatch> = owned.iter().map(|(mod_id, file, patch)| ModPatch { mod_id, file, patch }).collect();

    // The group's other tables (table-keys 13). A link patch that is not JSON adds nothing here: its own table's call
    // names it.
    let mut link_owned: Vec<(String, TableIr, Vec<OwnedPatch>)> = Vec::new();
    for (i, l) in slice(input.links, input.link_count, "links")?.iter().enumerate() {
        let link_path = text(l.path).ok_or_else(|| format!("link {i}: its path is null or not UTF-8"))?.to_string();
        let link_bytes = bytes(l.table, l.table_size).ok_or_else(|| format!("{link_path}: the table is null"))?;
        let link_table = TableIr::from_ntbl(link_bytes).map_err(|e| format!("{link_path}: {e:#}"))?;
        let mut link_patches = Vec::new();
        for (j, p) in slice(l.patches, l.patch_count, "link's patches")?.iter().enumerate() {
            let mod_id = text(p.mod_id).ok_or_else(|| format!("{link_path}: patch {j}: its mod id is null or not UTF-8"))?.to_string();
            let file = text(p.file).ok_or_else(|| format!("{link_path}: patch {j}: its file is null or not UTF-8"))?.to_string();
            let json = bytes(p.json, p.json_size).ok_or_else(|| format!("{mod_id}: {file}: its bytes are null"))?;
            if let Ok(value) = serde_json::from_slice::<serde_json::Value>(json) {
                link_patches.push((mod_id, file, value));
            }
        }
        link_owned.push((link_path, link_table, link_patches));
    }
    let link_patches: Vec<Vec<ModPatch>> =
        link_owned.iter().map(|(_, _, ps)| ps.iter().map(|(mod_id, file, patch)| ModPatch { mod_id, file, patch }).collect()).collect();
    let links: Vec<Link> = link_owned.iter().zip(&link_patches).map(|((p, t, _), ps)| Link { path: p, table: t, patches: ps }).collect();
    let mut mods = Vec::new();
    for (i, m) in slice(input.mods, input.mod_count, "mods")?.iter().enumerate() {
        mods.push(text(*m).ok_or_else(|| format!("mod {i}: its id is null or not UTF-8"))?);
    }

    let mut facts = unread;
    facts.extend(apply_linked(path, &mut table, &patches, &links, &mods).map_err(|e| format!("{e:#}"))?);

    let mut out = LumaPatched {
        error: None,
        table: table.to_ntbl(),
        texts: Vec::new(),
        facts: Vec::new(),
        refusals: Vec::new(),
        said: RefCell::new(Vec::new()),
    };
    for fact in &facts {
        out.refusals.push(match fact {
            Fact::Failed { refusal, .. } => Some(refusal.clone()),
            _ => None,
        });
        let t = &mut out.texts;
        let c = match fact {
            Fact::Conflict { row, column, lower, higher, .. } => LumaPatchFact {
                kind: LUMA_PATCH_CONFLICT,
                mod_id: put(t, Some(higher)),
                file: ptr::null(),
                other: put(t, Some(lower)),
                row: put(t, Some(row)),
                column: put(t, column.as_deref()),
                why: ptr::null(),
                selector: 0,
                rows: 0,
                code: 0,
            },
            Fact::Missing { row, mod_id, file, removed_by, .. } => LumaPatchFact {
                kind: LUMA_PATCH_MISSING,
                mod_id: put(t, Some(mod_id)),
                file: put(t, Some(file)),
                other: put(t, removed_by.as_deref()),
                row: put(t, Some(row)),
                column: ptr::null(),
                why: ptr::null(),
                selector: 0,
                rows: 0,
                code: 0,
            },
            Fact::Selected { mod_id, file, selector, rows, .. } => LumaPatchFact {
                kind: LUMA_PATCH_SELECTED,
                mod_id: put(t, Some(mod_id)),
                file: put(t, Some(file)),
                other: ptr::null(),
                row: ptr::null(),
                column: ptr::null(),
                why: ptr::null(),
                selector: u32::try_from(*selector).unwrap_or(u32::MAX),
                rows: u32::try_from(*rows).unwrap_or(u32::MAX),
                code: 0,
            },
            Fact::Failed { mod_id, file, why, refusal, .. } => LumaPatchFact {
                kind: LUMA_PATCH_FAILED,
                mod_id: put(t, Some(mod_id)),
                file: put(t, Some(file)),
                other: ptr::null(),
                row: ptr::null(),
                column: ptr::null(),
                why: put(t, Some(why)),
                selector: 0,
                rows: 0,
                code: u32::from(refusal.code),
            },
        };
        out.facts.push(c);
    }
    Ok(out)
}

/// # Safety
/// `input` is null or a valid `LumaPatchInput` whose pointers are valid for the call.
#[no_mangle]
pub unsafe extern "C" fn luma_patch_table(input: *const LumaPatchInput) -> *mut LumaPatched {
    match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| run(input))) {
        Ok(Ok(result)) => Box::into_raw(Box::new(result)),
        Ok(Err(why)) => failed(&why),
        Err(payload) => failed(&format!("the applier failed (a bug): {}", crate::validate_c::panic_text(payload.as_ref()))),
    }
}

/// # Safety
/// `result` is null or from `luma_patch_table` and not freed.
#[no_mangle]
pub unsafe extern "C" fn luma_patched_state(result: *const LumaPatched) -> i32 {
    match result.as_ref() {
        Some(r) if r.error.is_none() => 0,
        _ => -1,
    }
}

/// # Safety
/// `result` as `luma_patched_state`; `language` null or a NUL-ended name.
#[no_mangle]
pub unsafe extern "C" fn luma_patched_fact_reason(result: *const LumaPatched, index: u32, language: *const c_char) -> *const c_char {
    let Some(r) = result.as_ref() else { return ptr::null() };
    let Some(Some(refusal)) = r.refusals.get(index as usize) else { return ptr::null() };
    let language = text(language).unwrap_or("UsEnglish");
    let mut said = r.said.borrow_mut();
    let reason = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| luma_validate::reason(refusal, language))).unwrap_or_else(|_| format!("{{L{}}}", refusal.code));
    said.push(cstring(&reason));
    said.last().expect("just pushed").as_ptr()
}

/// # Safety
/// As `luma_patched_state`.
#[no_mangle]
pub unsafe extern "C" fn luma_patched_error(result: *const LumaPatched) -> *const c_char {
    result.as_ref().and_then(|r| r.error.as_ref()).map_or(ptr::null(), |e| e.as_ptr())
}

/// # Safety
/// As `luma_patched_state`; `size` is null or writable.
#[no_mangle]
pub unsafe extern "C" fn luma_patched_table(result: *const LumaPatched, size: *mut u64) -> *const u8 {
    let table = result.as_ref().filter(|r| r.error.is_none()).map(|r| r.table.as_slice());
    if !size.is_null() {
        *size = table.map_or(0, |t| t.len() as u64);
    }
    table.map_or(ptr::null(), <[u8]>::as_ptr)
}

/// # Safety
/// As `luma_patched_state`.
#[no_mangle]
pub unsafe extern "C" fn luma_patched_fact_count(result: *const LumaPatched) -> u32 {
    result.as_ref().map_or(0, |r| r.facts.len() as u32)
}

/// # Safety
/// As `luma_patched_state`.
#[no_mangle]
pub unsafe extern "C" fn luma_patched_fact_at(result: *const LumaPatched, index: u32) -> *const LumaPatchFact {
    result.as_ref().and_then(|r| r.facts.get(index as usize)).map_or(ptr::null(), |f| f as *const LumaPatchFact)
}

/// # Safety
/// `result` is null or from `luma_patch_table`, freed once.
#[no_mangle]
pub unsafe extern "C" fn luma_patched_free(result: *mut LumaPatched) {
    if !result.is_null() {
        drop(Box::from_raw(result));
    }
}
