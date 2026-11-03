// SPDX-License-Identifier: GPL-3.0-only
// Small things every platform's C library spells differently.
#pragma once

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace galaxy {

// An environment value into `buffer`; `*length` its size with the terminator, 0 when unset; non-zero when it does not fit.
[[nodiscard]] inline int env_read(std::size_t* length, char* buffer, std::size_t size, const char* name) noexcept {
    *length = 0u;
    if (size != 0u) {
        buffer[0] = '\0';
    }
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const char* const value = std::getenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (value == nullptr || value[0] == '\0') {
        return 0;
    }
    const std::size_t needed = std::strlen(value) + 1u;
    *length = needed;
    if (needed > size) {
        return 34;
    }
    std::memcpy(buffer, value, needed);
    return 0;
}

[[nodiscard]] inline std::FILE* open_file(const char* path, const char* mode) noexcept {
#if defined(_MSC_VER)
    std::FILE* file = nullptr;
    return fopen_s(&file, path, mode) == 0 ? file : nullptr;
#else
    return std::fopen(path, mode);
#endif
}

// ASCII letters in either case.
[[nodiscard]] inline bool text_iequal(const char* a, const char* b) noexcept {
    for (; *a != '\0' && *b != '\0'; ++a, ++b) {
        if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) {
            return false;
        }
    }
    return *a == *b;
}

}  // namespace galaxy
