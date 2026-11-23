// SPDX-License-Identifier: GPL-3.0-only
// The settings menu's pages by their numbers (runtime/src/ui/native_settings.cpp keeps their rows): which of them a player
// is shown. MODS is shown only where the program has mod support (galaxy/mod_support.h), ADVANCED only in a test build or
// in dev mode.
#pragma once

#include <cstddef>

namespace galaxy::ui {

inline constexpr unsigned kSettingsTabMods = 5u;
inline constexpr unsigned kSettingsTabAdvanced = 7u;
inline constexpr unsigned kSettingsTabCount = 8u;

struct SettingsTabs {
    unsigned tabs[kSettingsTabCount]{};
    unsigned count = 0u;
};

[[nodiscard]] constexpr SettingsTabs settings_tabs(bool advanced, bool mods) noexcept {
    SettingsTabs shown;
    for (unsigned tab = 0u; tab < kSettingsTabCount; ++tab) {
        if (tab == kSettingsTabAdvanced && !advanced) continue;
        if (tab == kSettingsTabMods && !mods) continue;
        shown.tabs[shown.count++] = tab;
    }
    return shown;
}

[[nodiscard]] constexpr bool settings_tab_shown(unsigned tab, bool advanced, bool mods) noexcept {
    const SettingsTabs shown = settings_tabs(advanced, mods);
    for (unsigned i = 0u; i < shown.count; ++i) {
        if (shown.tabs[i] == tab) return true;
    }
    return false;
}

}  // namespace galaxy::ui
