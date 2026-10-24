// SPDX-License-Identifier: GPL-3.0-only
// The C11 Annex K functions the runtime uses (getenv_s, fopen_s), Microsoft's _dupenv_s, _stricmp and _putenv_s, and
// MAX_PATH, for C libraries without them (Android, Linux, macOS): platform/sdl/CMakeLists.txt includes this before
// every runtime source there. Results match Microsoft's: a variable that is not set gives 0 and a length of 0.
#pragma once

#if !defined(_WIN32)

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#ifndef MAX_PATH
#define MAX_PATH 4096
#endif

inline int getenv_s(std::size_t* length, char* buffer, std::size_t size, const char* name) {
    const char* const value = name != nullptr ? std::getenv(name) : nullptr;
    if (value == nullptr) {
        if (length != nullptr) *length = 0;
        if (buffer != nullptr && size != 0) buffer[0] = '\0';
        return 0;
    }
    const std::size_t needed = std::strlen(value) + 1;
    if (length != nullptr) *length = needed;
    if (buffer == nullptr || size == 0) return 0;
    if (needed > size) {
        buffer[0] = '\0';
        return ERANGE;
    }
    std::memcpy(buffer, value, needed);
    return 0;
}

// A malloc'ed copy of the variable (free it), or null when it is not set.
inline int _dupenv_s(char** buffer, std::size_t* length, const char* name) {
    if (buffer == nullptr) return EINVAL;
    *buffer = nullptr;
    if (length != nullptr) *length = 0;
    const char* const value = name != nullptr ? std::getenv(name) : nullptr;
    if (value == nullptr) return 0;
    const std::size_t needed = std::strlen(value) + 1;
    *buffer = static_cast<char*>(std::malloc(needed));
    if (*buffer == nullptr) return ENOMEM;
    std::memcpy(*buffer, value, needed);
    if (length != nullptr) *length = needed;
    return 0;
}

inline int _stricmp(const char* a, const char* b) {
    return strcasecmp(a, b);
}

inline int _putenv_s(const char* name, const char* value) {
    return setenv(name, value, 1) == 0 ? 0 : errno;
}

inline int fopen_s(std::FILE** file, const char* path, const char* mode) {
    if (file == nullptr) return EINVAL;
    *file = std::fopen(path, mode);
    return *file != nullptr ? 0 : errno;
}

#endif
