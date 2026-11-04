// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Intel XeSS's library, loaded at run time from a vendor folder (Nebula does not link it).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <string>

namespace galaxy::render::xess_library {

// GALAXY_XESS_DIR, else %LOCALAPPDATA%\Nebula\vendor\xess-3.0.2.
[[nodiscard]] std::wstring directory();
// libxess.dll, once its signature is checked; null when it is absent or unsigned.
[[nodiscard]] HMODULE module() noexcept;
[[nodiscard]] inline bool available() noexcept { return module() != nullptr; }

template <typename Function>
[[nodiscard]] Function* get(const char* name) noexcept {
    const HMODULE library = module();
    return library != nullptr ? reinterpret_cast<Function*>(GetProcAddress(library, name)) : nullptr;
}

// The render ratios of the quality rows (native, 1.5, 1.7, 2, 3) as XeSS's modes.
[[nodiscard]] inline int quality_setting(unsigned quality) noexcept {
    constexpr int kSettings[] = {106, 104, 103, 102, 100};  // AA, ultra quality, quality, balanced, ultra performance
    return kSettings[quality < 4u ? quality : 4u];
}

}  // namespace galaxy::render::xess_library
