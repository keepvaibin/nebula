//! Small, fast primitives the game can call into: path hashing uses the same
//! FNV-1a 64 the asset store indexes by, so a native lookup and a cook agree on
//! every byte.
//!
//! The C ABI is the stable surface: `nebula_fast_path_hash` hashes a path's
//! bytes with ASCII letters folded to lower case (the store's key). Pointers
//! must be valid for `len` bytes; a null pointer with a zero length hashes the
//! empty path.

/// FNV-1a 64 offset basis.
pub const FNV_OFFSET: u64 = 0xcbf29ce484222325;
/// FNV-1a 64 prime.
pub const FNV_PRIME: u64 = 0x100000001b3;

/// FNV-1a 64 of `bytes`, folding ASCII A-Z to a-z as the store does.
#[inline]
pub fn path_hash_bytes(bytes: &[u8]) -> u64 {
    let mut h = FNV_OFFSET;
    for &b in bytes {
        let c = if b.is_ascii_uppercase() { b + 32 } else { b };
        h ^= c as u64;
        h = h.wrapping_mul(FNV_PRIME);
    }
    h
}

/// FNV-1a 64 of a UTF-8 path string, ASCII-lowercased like the asset store.
#[inline]
pub fn path_hash(path: &str) -> u64 {
    path_hash_bytes(path.as_bytes())
}

/// C ABI: hash `len` bytes at `ptr`, ASCII-lowercased. Returns the FNV-1a 64
/// the store's index is sorted by.
#[no_mangle]
pub unsafe extern "C" fn nebula_fast_path_hash(ptr: *const u8, len: usize) -> u64 {
    if len == 0 {
        return FNV_OFFSET;
    }
    let bytes = std::slice::from_raw_parts(ptr, len);
    path_hash_bytes(bytes)
}

/// C ABI: hash a null-terminated C string, ASCII-lowercased.
#[no_mangle]
pub unsafe extern "C" fn nebula_fast_path_hash_cstr(ptr: *const std::ffi::c_char) -> u64 {
    if ptr.is_null() {
        return FNV_OFFSET;
    }
    let bytes = std::ffi::CStr::from_ptr(ptr).to_bytes();
    path_hash_bytes(bytes)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_is_offset_basis() {
        assert_eq!(path_hash(""), FNV_OFFSET);
        assert_eq!(path_hash_bytes(&[]), FNV_OFFSET);
    }

    #[test]
    fn lowercasing_matches_store() {
        assert_eq!(path_hash("ObjectData/Foo.arc"), path_hash("objectdata/foo.arc"));
        assert_eq!(path_hash("StageData/AstroGalaxy"), path_hash("stagedata/astrogalaxy"));
    }

    #[test]
    fn known_vector() {
        // FNV-1a 64 of "foobar" (lowercase) is a stable reference value.
        assert_eq!(path_hash("foobar"), 0x85944171f73967e8);
    }

    #[test]
    fn c_abi_matches_rust() {
        let s = b"LayoutData/Home.arc";
        let h = unsafe { nebula_fast_path_hash(s.as_ptr(), s.len()) };
        assert_eq!(h, path_hash("LayoutData/Home.arc"));
    }
}
