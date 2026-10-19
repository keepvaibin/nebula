//! Android's interface (dev.nebula.galaxy.Importer): the disc image arrives as a file descriptor from the system's
//! file picker, so the app never needs storage permissions.

#![allow(non_snake_case)]

use crate::{build_pak, identify, DiscFile};
use jni_sys::{jboolean, jclass, jint, jlong, jobject, jstring, jvalue, JNIEnv, JNI_FALSE};
use std::ffi::{CStr, CString};
use std::path::PathBuf;

/// Our own copy of the picker's descriptor: Java closes its own.
fn disc_from_fd(fd: jint) -> Result<DiscFile, String> {
    #[cfg(unix)]
    {
        use std::fs::File;
        use std::os::fd::BorrowedFd;
        if fd < 0 {
            return Err("the disc image could not be opened".into());
        }
        let owned = unsafe { BorrowedFd::borrow_raw(fd) }
            .try_clone_to_owned()
            .map_err(|error| format!("the disc image could not be opened: {error}"))?;
        Ok(DiscFile::new(File::from(owned)))
    }
    #[cfg(not(unix))]
    {
        let _ = fd;
        Err("file descriptors are an Android interface".into())
    }
}

unsafe fn new_string(env: *mut JNIEnv, text: &str) -> jstring {
    let text = CString::new(text.replace('\0', " ")).unwrap_or_default();
    ((**env).v1_1.NewStringUTF)(env, text.as_ptr())
}

unsafe fn rust_string(env: *mut JNIEnv, text: jstring) -> Option<String> {
    if text.is_null() {
        return None;
    }
    let chars = ((**env).v1_1.GetStringUTFChars)(env, text, std::ptr::null_mut());
    if chars.is_null() {
        return None;
    }
    let value = CStr::from_ptr(chars).to_string_lossy().into_owned();
    ((**env).v1_1.ReleaseStringUTFChars)(env, text, chars);
    Some(value)
}

fn json_escape(text: &str) -> String {
    let mut out = String::with_capacity(text.len() + 2);
    for c in text.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            c if (c as u32) < 0x20 => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out
}

/// `{"supported":…,"gameId":…,"title":…,"format":…,"message":…}`; on a failure, supported false and the reason.
#[no_mangle]
pub unsafe extern "system" fn Java_dev_nebula_galaxy_Importer_identify(env: *mut JNIEnv, _: jclass, fd: jint) -> jstring {
    let json = match disc_from_fd(fd).and_then(|disc| identify(disc).map_err(|error| format!("{error:#}"))) {
        Ok(identity) => format!(
            "{{\"supported\":{},\"gameId\":\"{}\",\"title\":\"{}\",\"format\":\"{}\",\"message\":\"{}\"}}",
            identity.supported,
            json_escape(&identity.game_id),
            json_escape(&identity.title),
            json_escape(&identity.format),
            json_escape(&identity.message)
        ),
        Err(message) => format!("{{\"supported\":false,\"message\":\"{}\"}}", json_escape(&message)),
    };
    new_string(env, &json)
}

/// Builds game.pak at `output`; null when it succeeded, else the reason. `progress.update(done, total)` returns false
/// to cancel.
#[no_mangle]
pub unsafe extern "system" fn Java_dev_nebula_galaxy_Importer_build(
    env: *mut JNIEnv,
    _: jclass,
    fd: jint,
    output: jstring,
    progress: jobject,
) -> jstring {
    let Some(output) = rust_string(env, output) else {
        return new_string(env, "no destination was given");
    };
    let callback = if progress.is_null() {
        std::ptr::null_mut()
    } else {
        let class = ((**env).v1_1.GetObjectClass)(env, progress);
        let method = ((**env).v1_1.GetMethodID)(env, class, c"update".as_ptr(), c"(JJ)Z".as_ptr());
        ((**env).v1_1.DeleteLocalRef)(env, class);
        method
    };
    let mut report = |done: u64, total: u64| -> bool {
        if callback.is_null() {
            return true;
        }
        let arguments = [jvalue { j: done as jlong }, jvalue { j: total as jlong }];
        let keep: jboolean = ((**env).v1_1.CallBooleanMethodA)(env, progress, callback, arguments.as_ptr());
        if ((**env).v1_2.ExceptionCheck)(env) != JNI_FALSE {
            ((**env).v1_1.ExceptionClear)(env);
            return false;
        }
        keep != JNI_FALSE
    };
    let result = disc_from_fd(fd)
        .and_then(|disc| build_pak(disc, &PathBuf::from(output), &mut report).map_err(|error| format!("{error:#}")));
    match result {
        Ok(_) => std::ptr::null_mut(),
        Err(message) => new_string(env, &message),
    }
}
