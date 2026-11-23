// SPDX-License-Identifier: GPL-3.0-only
// Mod support in a build and in a test run (mod_support.h), and the settings pages it takes away (ui/settings_tabs.h).
// The game module's choice under it is tested with the mods (mods_tests.cpp, module_choice).
#include "galaxy/mod_support.h"
#include "galaxy/ui/settings_tabs.h"

#include <iostream>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool shows(const galaxy::ui::SettingsTabs& tabs, unsigned tab) {
    for (unsigned i = 0u; i < tabs.count; ++i) {
        if (tabs.tabs[i] == tab) return true;
    }
    return false;
}

}  // namespace

int main() {
    using namespace galaxy;
    bool ok = true;
    ok &= expect(mod_support_for(true, false) && mod_support_for(true, true), "a build with mods has mod support");
    ok &= expect(!mod_support_for(false, false), "a build with no mod support has none in a player's run");
    ok &= expect(mod_support_for(false, true), "a test run keeps what the harness's game half needs");

    // This program's own build (the default build has mods) and its rule from the launch arguments.
    g_launch_args.extensions.clear();
    ok &= expect(mod_support() == kModsInBuild, "without a host extension the build decides");
    g_launch_args.extensions.emplace_back("test_extension");
    ok &= expect(mod_support(), "a named host extension is a test run");
    g_launch_args.extensions.clear();

    const ui::SettingsTabs all = ui::settings_tabs(false, true);
    ok &= expect(all.count == 7u && shows(all, ui::kSettingsTabMods) && !shows(all, ui::kSettingsTabAdvanced),
                 "a player's pages: seven, MODS among them, no ADVANCED");
    const ui::SettingsTabs none = ui::settings_tabs(false, false);
    ok &= expect(none.count == 6u && !shows(none, ui::kSettingsTabMods), "with no mod support the pages have no MODS");
    ok &= expect(none.tabs[4] == 4u && none.tabs[5] == 6u, "the others keep their numbers and their order");
    const ui::SettingsTabs test = ui::settings_tabs(true, false);
    ok &= expect(test.count == 7u && shows(test, ui::kSettingsTabAdvanced) && !shows(test, ui::kSettingsTabMods),
                 "ADVANCED and MODS are decided apart");
    ok &= expect(ui::settings_tab_shown(ui::kSettingsTabMods, false, true) && !ui::settings_tab_shown(ui::kSettingsTabMods, false, false) &&
                     !ui::settings_tab_shown(ui::kSettingsTabAdvanced, false, true) && ui::settings_tab_shown(0u, false, false),
                 "settings_tab_shown follows the list");
    std::cout << (ok ? "mod support tests passed" : "mod support tests FAILED") << '\n';
    return ok ? 0 : 1;
}
