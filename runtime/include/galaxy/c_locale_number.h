// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Numbers in settings files in the "C" locale on every platform: a decimal point whatever the player's locale.

#include <cstddef>
#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
#include <locale.h>
#else
#include <locale.h>
#if defined(__APPLE__)
#include <xlocale.h>
#endif
#endif

namespace galaxy {

#if defined(_WIN32)
[[nodiscard]] inline _locale_t c_numeric_locale() noexcept {
    static const _locale_t locale = _create_locale(LC_NUMERIC, "C");
    return locale;
}
#else
[[nodiscard]] inline locale_t c_numeric_locale() noexcept {
    static const locale_t locale = newlocale(LC_NUMERIC_MASK, "C", static_cast<locale_t>(0));
    return locale;
}
#endif

// strtof in the C locale (errno as strtof sets it); `*end == text` when nothing was read.
inline float strtof_c(const char* text, char** end) noexcept {
    const auto locale = c_numeric_locale();
    if (locale == nullptr) {
        *end = const_cast<char*>(text);
        return 0.0f;
    }
#if defined(_WIN32)
    return _strtof_l(text, end, locale);
#else
    return strtof_l(text, end, locale);
#endif
}

// "%.9g" in the C locale, truncated to `size`.
inline void format_g9_c(char* buffer, std::size_t size, double value) noexcept {
    if (size == 0) return;
    buffer[0] = '\0';
    const auto locale = c_numeric_locale();
    if (locale == nullptr) return;
#if defined(_WIN32)
    _snprintf_s_l(buffer, size, _TRUNCATE, "%.9g", locale, value);
#else
    const locale_t previous = uselocale(locale);
    std::snprintf(buffer, size, "%.9g", value);
    uselocale(previous);
#endif
}

}  // namespace galaxy
