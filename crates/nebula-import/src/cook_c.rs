//! The cook as C calls for the game's run time (plan 2.8 layer 3; contracts/services-assets.md 4): the `assets` service
//! gives one file as the disc has it and takes back what the store would hold for it, as `cook::cook_entry` makes it at
//! install. Resolved by name from nebula_import (Windows, Linux, Android) or linked weakly (Apple).
//!
//! One file at a time under the importer's cook lock (cook::COOK_LOCK, which the store's and the mods' cooks hold too),
//! so the cook's shared lists (the pipeline list, the texture names) are never met half-written; the validator and the
//! table patch calls share nothing with it. A file whose models or particles draw with a program the game was not
//! built with is refused with words: that program comes only with an install (pipeline_list::unshipped).

use crate::cook::{self, pipeline_list, refuse, Stats};
use std::ffi::{c_char, CStr, CString};
use std::ptr;

/// One file cooked, or the words why not.
pub struct LumaCooked {
    error: Option<CString>,
    /// In the store's order: path inside ("" the file itself), bytes (None: a folder).
    items: Vec<(CString, Option<Vec<u8>>)>,
}

impl LumaCooked {
    fn failed(words: &str) -> LumaCooked {
        LumaCooked { error: Some(cstring(words)), items: Vec::new() }
    }
}

fn cstring(text: &str) -> CString {
    CString::new(text.replace('\0', " ")).unwrap_or_default()
}

/// The words an error says: a refusal in the player's first language's words (English), else the cooker's chain.
fn words_of(error: &anyhow::Error) -> String {
    match refuse::refusal_of(error) {
        Some(finding) => std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| luma_validate::reason(finding, "UsEnglish")))
            .unwrap_or_else(|_| format!("{error:#}")),
        None => format!("{error:#}"),
    }
}

unsafe fn cook_one(name: *const c_char, bytes: *const u8, size: u64) -> LumaCooked {
    if name.is_null() {
        return LumaCooked::failed("the file has no name");
    }
    let Ok(name) = CStr::from_ptr(name).to_str() else {
        return LumaCooked::failed("the file's name is not UTF-8");
    };
    if name.is_empty() {
        return LumaCooked::failed("the file has no name");
    }
    let Ok(length) = usize::try_from(size) else {
        return LumaCooked::failed(&format!("{name}: {size} bytes is more than this machine can hold"));
    };
    if bytes.is_null() && length > 0 {
        return LumaCooked::failed(&format!("{name}: no bytes given for {size}"));
    }
    let data = if length == 0 { Vec::new() } else { std::slice::from_raw_parts(bytes, length).to_vec() };

    let _cook = cook::COOK_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let mut stats = Stats::default();
    let (result, added) = pipeline_list::added_by(|| cook::cook_entry(name, data, &mut stats));
    // What the cook noted for an install's report is not this call's.
    refuse::take_facts();
    refuse::take_notes();
    let mut items = match result {
        Ok(items) => items,
        Err(error) => return LumaCooked::failed(&words_of(&error)),
    };
    if let Some((vertex, pixel)) = pipeline_list::unshipped(&added).first() {
        return LumaCooked::failed(&format!(
            "{name} draws with a program the game was not built with ({vertex:016x}, {pixel:016x}); it comes only with an install of the mod"
        ));
    }
    items.sort_by_key(|i| i.order);
    LumaCooked { error: None, items: items.into_iter().map(|i| (cstring(&i.path), i.bytes)).collect() }
}

/// One file as the disc has it (compressed or not, an archive or one file), cooked as the store holds it. `name` is
/// its path as the mod gave it ("ObjectData/Kuribo.arc"). Never null but for want of memory.
///
/// # Safety
/// `name` is null or a NUL-ended string; `bytes` is null or `size` readable bytes. Both are read before the call
/// returns and not kept.
#[no_mangle]
pub unsafe extern "C" fn luma_cook_entry(name: *const c_char, bytes: *const u8, size: u64) -> *mut LumaCooked {
    let cooked = std::panic::catch_unwind(|| cook_one(name, bytes, size)).unwrap_or_else(|_| LumaCooked::failed("the cooker stopped on a fault of its own"));
    Box::into_raw(Box::new(cooked))
}

/// Null: it cooked. Else the cooker's words (English UTF-8, NUL-ended), valid until luma_cooked_free.
///
/// # Safety
/// `cooked` is null or from luma_cook_entry and not freed.
#[no_mangle]
pub unsafe extern "C" fn luma_cooked_error(cooked: *const LumaCooked) -> *const c_char {
    match cooked.as_ref() {
        None => ptr::null(),
        Some(c) => c.error.as_ref().map_or(ptr::null(), |e| e.as_ptr()),
    }
}

/// Its items in the store's order; 0 when it did not cook.
///
/// # Safety
/// As luma_cooked_error.
#[no_mangle]
pub unsafe extern "C" fn luma_cooked_count(cooked: *const LumaCooked) -> u32 {
    cooked.as_ref().map_or(0, |c| u32::try_from(c.items.len()).unwrap_or(u32::MAX))
}

/// Item `index`: `*path` its path inside ("" the file itself, or an empty archive), `*folder` 1 for a folder (bytes
/// null, `*size` 0), else its bytes and `*size`. Null with `*path` "" and `*size` 0 for an index past the count. Each
/// out pointer may be null. Valid until luma_cooked_free.
///
/// # Safety
/// As luma_cooked_error; each non-null out pointer is writable.
#[no_mangle]
pub unsafe extern "C" fn luma_cooked_item(cooked: *const LumaCooked, index: u32, path: *mut *const c_char, size: *mut u64, folder: *mut u32) -> *const u8 {
    static EMPTY: &CStr = c"";
    let item = cooked.as_ref().and_then(|c| c.items.get(index as usize));
    let (name, bytes) = match item {
        Some((name, bytes)) => (name.as_c_str(), bytes.as_deref()),
        None => (EMPTY, None),
    };
    if !path.is_null() {
        *path = name.as_ptr();
    }
    if !size.is_null() {
        *size = bytes.map_or(0, |b| b.len() as u64);
    }
    if !folder.is_null() {
        *folder = u32::from(item.is_some() && bytes.is_none());
    }
    bytes.map_or(ptr::null(), |b| if b.is_empty() { ptr::NonNull::<u8>::dangling().as_ptr().cast_const() } else { b.as_ptr() })
}

/// # Safety
/// `cooked` is null or from luma_cook_entry, freed once.
#[no_mangle]
pub unsafe extern "C" fn luma_cooked_free(cooked: *mut LumaCooked) {
    if !cooked.is_null() {
        drop(Box::from_raw(cooked));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    unsafe fn items(c: *const LumaCooked) -> Vec<(String, Option<usize>)> {
        (0..luma_cooked_count(c))
            .map(|i| {
                let (mut path, mut size, mut folder) = (ptr::null(), 0u64, 0u32);
                let bytes = luma_cooked_item(c, i, &mut path, &mut size, &mut folder);
                assert_eq!(bytes.is_null(), folder == 1);
                (CStr::from_ptr(path).to_string_lossy().into_owned(), (folder == 0).then_some(size as usize))
            })
            .collect()
    }

    #[test]
    fn a_file_cooks_as_the_store_holds_it_and_a_bad_one_says_why() {
        unsafe {
            // A file no reader takes is held as it is.
            let c = luma_cook_entry(c"Data/notes.bin".as_ptr(), b"abc".as_ptr(), 3);
            assert!(luma_cooked_error(c).is_null());
            assert_eq!(items(c), vec![(String::new(), Some(3))]);
            luma_cooked_free(c);

            // A compressed file whose data ends early: refused with words.
            let yaz0 = b"Yaz0\0\0\0\x10\0\0\0\0\0\0\0\0";
            let c = luma_cook_entry(c"ObjectData/Broken.arc".as_ptr(), yaz0.as_ptr(), yaz0.len() as u64);
            assert!(!luma_cooked_error(c).is_null());
            assert_eq!(luma_cooked_count(c), 0);
            luma_cooked_free(c);

            let c = luma_cook_entry(ptr::null(), ptr::null(), 0);
            assert_eq!(CStr::from_ptr(luma_cooked_error(c)).to_str().unwrap(), "the file has no name");
            let (mut path, mut size, mut folder) = (ptr::null(), 7u64, 7u32);
            assert!(luma_cooked_item(c, 0, &mut path, &mut size, &mut folder).is_null());
            assert_eq!((CStr::from_ptr(path).to_bytes(), size, folder), (&b""[..], 0, 0));
            luma_cooked_free(c);
            luma_cooked_free(ptr::null_mut());
        }
    }
}
