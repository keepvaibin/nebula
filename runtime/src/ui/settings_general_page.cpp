// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/ui/settings_general_page.h"
#include <algorithm>
#include <cstdio>

namespace galaxy::ui {
std::vector<SettingsRow> general_page(const RuntimeSettings& runtime, RuntimeSettings& draft_runtime, const GeneralPageHost& host) {
    const GeneralPageState state = host.query(runtime);
    std::vector<SettingsRow> rows;
    char text[32]{};
    std::snprintf(text, sizeof(text), "%uX", std::clamp(runtime.text_speed, 1u, 4u));
    rows.push_back({"TEXT SPEED", runtime.text_speed >= 4u ? std::string("INSTANT") : std::string(text),
                    [&draft_runtime, &host](RenderConfig&, int delta) {
                        RuntimeSettings& settings = draft_runtime;
                        settings.text_speed = settings_step(std::clamp(settings.text_speed, 1u, 4u) - 1u, 4u, delta) + 1u;
                    },
                    "HOW FAST DIALOGUE TEXT APPEARS. INSTANT: EACH PAGE AT ONCE"});
    {
        static constexpr const char* kIcons[] = {"AUTO", "WII", "XBOX", "NINTENDO SWITCH", "PLAYSTATION",
                                                 "KEYBOARD AND MOUSE"};
        // Auto names the icons it shows now.
        std::string icons = kIcons[std::min<std::size_t>(static_cast<std::size_t>(runtime.button_icons), 5u)];
        if (runtime.button_icons == ButtonIcons::Auto) {
            const ButtonIcons shown = state.auto_icons;
            icons += shown == ButtonIcons::Xbox          ? " (XBOX)"
                     : shown == ButtonIcons::Nintendo    ? " (NINTENDO SWITCH)"
                     : shown == ButtonIcons::PlayStation ? " (PLAYSTATION)"
                     : shown == ButtonIcons::Keyboard    ? " (KEYBOARD AND MOUSE)"
                                                             : " (WII)";
        }
        rows.push_back({"BUTTON ICONS", icons,
                        [&draft_runtime, &host](RenderConfig&, int delta) {
                            RuntimeSettings& settings = draft_runtime;
                            settings.button_icons = static_cast<ButtonIcons>(
                                settings_step(static_cast<unsigned>(settings.button_icons), 6u, delta));
                        },
                        "THE BUTTONS THE GAME'S ICONS SHOW. AUTO: THE CONTROLLER IN USE"});
    }
    rows.push_back({"LANGUAGE", game_language_name(state.display_language),
                    [&draft_runtime, &host](RenderConfig&, int delta) {
                        // Saved when applied; the game switches at the next start (game_language_for_boot).
                        // The languages on the disc as it is read (galaxy::set_disc_languages); Auto shows the one
                        // it plays in, and a step chooses from there.
                        if (delta == 0) return;
                        RuntimeSettings& settings = draft_runtime;
                        const GeneralPageState live = host.query(settings);
                        const auto& offered = live.languages;
                        if (offered.empty()) return;
                        const GameLanguage current = live.display_language;
                        std::size_t index = 0u;
                        for (std::size_t i = 0; i < offered.size(); ++i) {
                            if (offered[i] == current) index = i;
                        }
                        const GameLanguage chosen = offered[settings_step(static_cast<unsigned>(index),
                                                                         static_cast<unsigned>(offered.size()), delta)];
                        // Back on the language Auto plays in: Auto again, so nothing is left to apply.
                        const GameLanguage saved = live.saved_language;
                        settings.language =
                            saved == GameLanguage::Auto && chosen == live.resolved_saved_language ? saved
                                                                                                         : chosen;
                    },
                    "THE GAME'S TEXT AND MENUS"});
    rows.push_back({"DISCORD STATUS", runtime.discord_status ? std::string("ON") : std::string("OFF"),
                    [&draft_runtime, &host](RenderConfig&, int delta) {
                        if (delta == 0) return;
                        draft_runtime.discord_status = !draft_runtime.discord_status;
                    },
                    "SHOWS YOUR CURRENT GALAXY AND MISSION IN DISCORD"});
    {
        rows.push_back({"MIIS", {}, nullptr, {}, true});
        std::string status = state.choosing_miis ? "CHOOSING FILES" : state.mii_status;
        rows.push_back(button_row("IMPORT MIIS", {"IMPORT"}, [&draft_runtime, &host](RenderConfig&, unsigned) { host.act(GeneralPageAction::ImportMiis); },
                                  status.empty() ? std::string("MIIS FROM A WII, DS, 3DS, WII U, SWITCH, MII STUDIO OR MII "
                                                               "CREATOR, FOR THE MII ICONS")
                                                 : status));
        const int count = state.mii_count;
        char mii_text[32]{};
        std::snprintf(mii_text, sizeof(mii_text), "%d OF 100", count);
        rows.push_back({"YOUR MIIS", mii_text, nullptr, "THE MII ICON PICKER ON THE FILE SELECT SHOWS THEM"});
        rows.push_back(button_row("MIIS FOLDER", {"OPEN"}, [&draft_runtime, &host](RenderConfig&, unsigned) { host.act(GeneralPageAction::OpenMiiFolder); },
                                  "FILES PUT HERE ARE IMPORTED AT EVERY START"));
    }
    return rows;
}


}  // namespace galaxy::ui
