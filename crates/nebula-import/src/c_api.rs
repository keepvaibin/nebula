//! The importer as C functions, for an app that links it (Apple's: docs/APPLE.md): what the command line's `identify`
//! and `import` do, by path. A document picker's file is read where it lies; nothing is copied.

use crate::{identify_input, json_string, Partition};
use std::ffi::{c_char, c_void, CStr, CString};
use std::path::PathBuf;

/// Called with the bytes read so far and the whole; returns false to stop the import.
pub type NebulaImportProgress = unsafe extern "C" fn(done: u64, total: u64, context: *mut c_void) -> bool;

fn c_string(text: &str) -> *mut c_char {
    CString::new(text.replace('\0', " ")).unwrap_or_default().into_raw()
}

unsafe fn path_of(text: *const c_char) -> Option<PathBuf> {
    if text.is_null() {
        return None;
    }
    Some(PathBuf::from(CStr::from_ptr(text).to_string_lossy().into_owned()))
}

/// The store version this importer writes and the game reads (cook/store.rs VERSION): a game.pak of another needs
/// setting up again.
#[no_mangle]
pub extern "C" fn nebula_import_store_version() -> u32 {
    crate::cook::store::VERSION
}

/// The game of a disc image or an extracted folder, as the command line's `identify` prints it (JSON: format, gameId,
/// title, revision, mainDolSha1, languages, supported, message); on a failure `{"supported":false,"message":…}`.
/// The text is the caller's to give back to nebula_import_free.
///
/// # Safety
/// `input` is null or a NUL-terminated path.
#[no_mangle]
pub unsafe extern "C" fn nebula_import_identify(input: *const c_char) -> *mut c_char {
    let Some(input) = path_of(input) else {
        return c_string("{\"supported\":false,\"message\":\"no file was given\"}");
    };
    let json = match std::panic::catch_unwind(|| identify_input(&input)) {
        Ok(Ok(identity)) => identity.to_json(),
        Ok(Err(error)) => format!("{{\"supported\":false,\"message\":{}}}", json_string(&format!("{error:#}"))),
        Err(_) => "{\"supported\":false,\"message\":\"the file could not be read\"}".to_owned(),
    };
    c_string(&json)
}

/// Builds game.pak at `output` from the disc image or extracted folder `input`. Null when it succeeded, else the
/// reason, the caller's to give back to nebula_import_free. `progress` may be null.
///
/// # Safety
/// `input` and `output` are null or NUL-terminated paths; `progress`, when given, may be called with `context` on
/// this thread until the function returns.
#[no_mangle]
pub unsafe extern "C" fn nebula_import_build(
    input: *const c_char,
    output: *const c_char,
    progress: Option<NebulaImportProgress>,
    context: *mut c_void,
) -> *mut c_char {
    let (Some(input), Some(output)) = (path_of(input), path_of(output)) else {
        return c_string("no file or no destination was given");
    };
    let context = context as usize;
    let result = std::panic::catch_unwind(move || {
        let mut report = |done: u64, total: u64| -> bool {
            match progress {
                Some(progress) => unsafe { progress(done, total, context as *mut c_void) },
                None => true,
            }
        };
        Partition::open(&input).and_then(|mut partition| partition.write_cooked_pak(&output, &mut report))
    });
    match result {
        Ok(Ok(_)) => std::ptr::null_mut(),
        Ok(Err(error)) => c_string(&format!("{error:#}")),
        Err(_) => c_string("the import stopped unexpectedly"),
    }
}

/// Gives back a text one of these functions returned.
///
/// # Safety
/// `text` is null or a pointer from nebula_import_identify or nebula_import_build, given back once.
#[no_mangle]
pub unsafe extern "C" fn nebula_import_free(text: *mut c_char) {
    if !text.is_null() {
        drop(CString::from_raw(text));
    }
}
