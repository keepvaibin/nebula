// SPDX-License-Identifier: GPL-3.0-only
// Windows text of any length: an environment variable's value and the running program's path.
#pragma once

#if defined(_WIN32)
#include "galaxy/windows_api.h"

#include <string>

namespace galaxy::windows {

// The value of an environment variable; empty when it is not set or is empty.
[[nodiscard]] inline std::wstring environment_text(const wchar_t* name) {
    std::wstring value(64u, L'\0');
    for (;;) {
        const DWORD length = GetEnvironmentVariableW(name, value.data(), static_cast<DWORD>(value.size()));
        if (length == 0u) {
            return {};
        }
        const bool fits = length < value.size();
        value.resize(length);
        if (fits) {
            return value;
        }
    }
}

// The full path of the running program; empty when it cannot be read.
[[nodiscard]] inline std::wstring executable_path() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0u) {
            return {};
        }
        if (length < path.size()) {
            path.resize(length);
            return path;
        }
        if (path.size() >= 65536u) {
            return {};
        }
        path.resize(path.size() * 2u);
    }
}

}  // namespace galaxy::windows
#endif
