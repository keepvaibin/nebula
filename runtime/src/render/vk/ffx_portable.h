// SPDX-License-Identifier: GPL-3.0-only
// Forced into FidelityFX's sources where the compiler is not MSVC: the few Microsoft library calls they use.
#pragma once
#include <codecvt>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <locale>
#include <string>

#include <FidelityFX/host/ffx_util.h>

// Names are wide strings in FidelityFX's contexts: four bytes a character here, and its context grows past the default (837,848 bytes).
#undef FFX_SDK_DEFAULT_CONTEXT_SIZE
#define FFX_SDK_DEFAULT_CONTEXT_SIZE (1024 * 256)

template <typename T, std::size_t N>
constexpr std::size_t nebula_ffx_countof(const T (&)[N]) noexcept {
    return N;
}
#define _countof(array) nebula_ffx_countof(array)

template <std::size_t N>
inline int wcscpy_s(wchar_t (&destination)[N], const wchar_t* source) noexcept {
    std::wcsncpy(destination, source, N - 1u);
    destination[N - 1u] = L'\0';
    return 0;
}

inline int wcscpy_s(wchar_t* destination, std::size_t size, const wchar_t* source) noexcept {
    std::wcsncpy(destination, source, size - 1u);
    destination[size - 1u] = L'\0';
    return 0;
}

inline int strcpy_s(char* destination, std::size_t size, const char* source) noexcept {
    std::snprintf(destination, size, "%s", source);
    return 0;
}

template <std::size_t N, typename... Args>
inline int sprintf_s(char (&destination)[N], const char* format, Args... args) noexcept {
    return std::snprintf(destination, N, format, args...);
}

template <typename... Args>
inline int sprintf_s(char* destination, std::size_t size, const char* format, Args... args) noexcept {
    return std::snprintf(destination, size, format, args...);
}
