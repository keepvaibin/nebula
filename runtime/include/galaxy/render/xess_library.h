// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Intel XeSS's library, loaded at run time from a vendor folder (Nebula does not link it).

#include "galaxy/windows_api.h"

#include <string>

namespace galaxy::render::xess_library {

// vendor\xess-3.0.2 in Nebula's data folder (platform_paths.h).
[[nodiscard]] std::wstring directory();
// libxess.dll, once its signature is checked; null when it is absent or unsigned.
[[nodiscard]] HMODULE module() noexcept;
[[nodiscard]] inline bool available() noexcept { return module() != nullptr; }

template <typename Function>
[[nodiscard]] Function* get(const char* name) noexcept {
    const HMODULE library = module();
    return library != nullptr ? reinterpret_cast<Function*>(GetProcAddress(library, name)) : nullptr;
}

// The render ratios of the quality rows (native, 1.5, 1.7, 2, 3, then 1.3) as XeSS 1.3's modes.
[[nodiscard]] inline int quality_setting(unsigned quality) noexcept {
    // AA, ultra quality, quality, balanced, ultra performance, ultra quality plus
    constexpr int kSettings[] = {106, 104, 103, 102, 100, 105};
    return kSettings[quality < 5u ? quality : 5u];
}

}  // namespace galaxy::render::xess_library
