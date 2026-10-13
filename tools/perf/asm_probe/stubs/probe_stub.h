// SPDX-License-Identifier: GPL-3.0-only
// Local stubs for compiling generated probe shards outside MSVC/clang-cl, for
// tools/perf/asm_probe.py only. Never shipped and never included by the
// runtime or the module build.
//
// - The runtime headers include <intrin.h> only under _MSC_VER; the SSE/AVX
//   intrinsics the fast paths use come from <immintrin.h> elsewhere.
// - getenv_s is an MSVC CRT function (mingw-w64 provides it; glibc does not).
#pragma once
#include <immintrin.h>
#if !defined(_WIN32)
#include <cstddef>
#include <cstdlib>
#include <cstring>
inline int getenv_s(std::size_t* length, char* buffer, std::size_t size, const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) {
        if (length != nullptr) *length = 0;
        return 0;
    }
    const std::size_t needed = std::strlen(value) + 1;
    if (length != nullptr) *length = needed;
    if (buffer != nullptr && size != 0) {
        std::strncpy(buffer, value, size - 1);
        buffer[size - 1] = '\0';
    }
    return 0;
}
#endif
