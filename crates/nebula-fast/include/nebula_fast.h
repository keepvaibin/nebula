#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* FNV-1a 64 of `len` bytes at `ptr`, ASCII letters folded to lower case.
   Matches the asset store's path hash. `ptr` may be NULL only when `len` is 0. */
uint64_t nebula_fast_path_hash(const uint8_t* ptr, size_t len);

/* FNV-1a 64 of a null-terminated path, ASCII-lowercased. NULL hashes as the empty path. */
uint64_t nebula_fast_path_hash_cstr(const char* ptr);

#ifdef __cplusplus
}
#endif
