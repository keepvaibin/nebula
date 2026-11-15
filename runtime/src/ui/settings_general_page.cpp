// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/ui/settings_general_page.h"
#include <algorithm>
#include <cstdio>

namespace galaxy::ui {
namespace {

bool holds_file(const SaveImportFile& file) { return file.exists || file.damaged; }

// "MARIO, 12 STARS": the character last played and that character's power stars.
std::string save_file_words(const SaveImportFile& file) {
    char stars[32]{};
    std::snprintf(stars, sizeof(stars), file.stars == 1u ? "%u STAR" : "%u STARS", file.stars);
    return std::string(file.luigi ? "LUIGI" : "MARIO") + ", " + stars;
}

// The per-file import's rows: each file of the picked save that holds one, where it goes, then the buttons.
void add_import_rows(std::vector<SettingsRow>& rows, const GeneralPageState& state, const GeneralPageHost& host) {
    bool any = false;
    bool replaces = false;
    for (unsigned file = 1u; file <= kSaveImportFiles; ++file) {
        const SaveImportFile& imported = state.import_files[file - 1u];
        if (!holds_file(imported)) continue;
        char label[32]{};
        std::snprintf(label, sizeof(label), "IMPORTED FILE %u", file);
        if (imported.damaged) {
            rows.push_back({label, "DAMAGED", nullptr, "THIS FILE DOES NOT LOAD. IT IS LEFT OUT"});
            continue;
        }
        const unsigned target = state.import_targets[file - 1u];
        std::string value = "LEAVE OUT";
        std::string help = save_file_words(imported);
        if (target >= 1u && target <= kSaveImportFiles) {
            any = true;
            const SaveImportFile& there = state.current_files[target - 1u];
            char text[32]{};
            if (holds_file(there)) {
                replaces = true;
                std::snprintf(text, sizeof(text), "REPLACE FILE %u", target);
                char now[32]{};
                std::snprintf(now, sizeof(now), "IN FILE %u NOW", target);
                help += std::string(". ") + now + ": " + (there.damaged ? std::string("A DAMAGED FILE") : save_file_words(there));
            } else {
                std::snprintf(text, sizeof(text), "ADD AS FILE %u", target);
            }
            value = text;
        }
        rows.push_back({label, value, [&host, file](RenderConfig&, int delta) {
                            if (delta != 0) host.step_import_target(file, delta);
                        },
                        help});
    }
    if (any) {
        rows.push_back(button_row("IMPORT SAVES", {"IMPORT", "CANCEL"},
                                  [&host](RenderConfig&, unsigned index) {
                                      host.act(index == 0u ? GeneralPageAction::ConfirmImport : GeneralPageAction::CancelImport);
                                  },
                                  replaces ? "ADDS THE CHOSEN FILES AND RESTARTS THE GAME. REPLACED FILES ARE BACKED UP"
                                           : "ADDS THE CHOSEN FILES AND RESTARTS THE GAME"));
    } else {
        rows.push_back(button_row("IMPORT SAVES", {"CANCEL"}, [&host](RenderConfig&, unsigned) { host.act(GeneralPageAction::CancelImport); },
                                  "CHOOSE WHERE A FILE GOES"));
    }
    if (state.saves_whole_offered) {
        rows.push_back(button_row("WHOLE SAVE", {"REPLACE"}, [&host](RenderConfig&, unsigned) { host.act(GeneralPageAction::ReplaceAllSaves); },
                                  "REPLACES YOUR SAVE AND RESTARTS THE GAME. THE CURRENT SAVE IS BACKED UP"));
    }
}

}  // namespace

SaveImportTargets save_import_first_targets(const SaveImportFiles& imported, const SaveImportFiles& current) {
    SaveImportTargets targets{};
    std::array<bool, kSaveImportFiles> taken{};
    for (std::size_t i = 0; i < kSaveImportFiles; ++i) taken[i] = holds_file(current[i]);
    for (std::size_t i = 0; i < kSaveImportFiles; ++i) {
        if (!imported[i].exists || imported[i].damaged) continue;
        std::size_t place = kSaveImportFiles;
        if (!taken[i]) {
            place = i;
        } else {
            for (std::size_t j = 0; j < kSaveImportFiles && place == kSaveImportFiles; ++j) {
                if (!taken[j]) place = j;
            }
        }
        if (place == kSaveImportFiles) continue;  // no free file left
        taken[place] = true;
        targets[i] = static_cast<std::uint8_t>(place + 1u);
    }
    return targets;
}

void save_import_step(SaveImportTargets& targets, unsigned file, int delta) {
    if (file < 1u || file > kSaveImportFiles || delta == 0) return;
    std::array<bool, kSaveImportFiles + 1u> other{};  // the user files the other imported files go to
    for (std::size_t i = 0; i < kSaveImportFiles; ++i) {
        if (i + 1u != file && targets[i] >= 1u && targets[i] <= kSaveImportFiles) other[targets[i]] = true;
    }
    // 0 (left out), then 1 to 6: a step at a time, past the taken ones.
    constexpr int kChoices = static_cast<int>(kSaveImportFiles) + 1;
    int at = targets[file - 1u] <= kSaveImportFiles ? targets[file - 1u] : 0;
    const int direction = delta > 0 ? 1 : -1;
    for (int steps = delta > 0 ? delta : -delta; steps > 0; --steps) {
        do {
            at = (at + direction + kChoices) % kChoices;
        } while (at != 0 && other[static_cast<std::size_t>(at)]);
    }
    targets[file - 1u] = static_cast<std::uint8_t>(at);
}
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
    {
        // In steps the eye can tell apart; Auto: as large as this screen calls for.
        static constexpr unsigned kSizes[] = {0u, 80u, 90u, 100u, 110u, 120u, 130u, 140u, 150u, 160u, 180u, 200u};
        std::string size = "AUTO";
        if (runtime.menu_text_size != 0u) {
            size = std::to_string(runtime.menu_text_size) + "%";
        }
        rows.push_back({"MENU TEXT SIZE", size,
                        [&draft_runtime](RenderConfig&, int delta) {
                            unsigned index = 0u;
                            for (unsigned i = 0u; i < std::size(kSizes); ++i) {
                                if (kSizes[i] == draft_runtime.menu_text_size) index = i;
                            }
                            draft_runtime.menu_text_size = kSizes[settings_step(index, static_cast<unsigned>(std::size(kSizes)), delta)];
                        },
                        "HOW LARGE THE MENUS ARE DRAWN. AUTO: FOR THIS SCREEN'S SIZE"});
    }
    rows.push_back({"LANGUAGE", game_language_name(state.display_language),
                    [&draft_runtime, &host](RenderConfig&, int delta) {
                        // Saved when applied; the game switches at the next start (game_language_for_boot).
                        // The languages the game's data holds (galaxy::set_game_languages); Auto shows the one
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
    if (state.focus_loss_offered) {
        static constexpr const char* kFocus[] = {"KEEP RUNNING", "PAUSE", "MUTE"};
        rows.push_back({"ON LOSING FOCUS", kFocus[std::min<std::size_t>(static_cast<std::size_t>(runtime.focus_loss), 2u)],
                        [&draft_runtime](RenderConfig&, int delta) {
                            draft_runtime.focus_loss = static_cast<FocusLoss>(
                                settings_step(static_cast<unsigned>(draft_runtime.focus_loss), 3u, delta));
                        },
                        "WHEN ANOTHER PROGRAM COMES IN FRONT. PAUSE USES THE GAME'S OWN PAUSE MENU. MINIMISING ALWAYS HOLDS THE GAME"});
    }
    if (state.discord_offered) {
        rows.push_back({"DISCORD STATUS", runtime.discord_status ? std::string("ON") : std::string("OFF"),
                        [&draft_runtime, &host](RenderConfig&, int delta) {
                            if (delta == 0) return;
                            draft_runtime.discord_status = !draft_runtime.discord_status;
                        },
                        "SHOWS YOUR CURRENT GALAXY AND MISSION IN DISCORD"});
    }
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
        if (state.folders_open) {
            rows.push_back(button_row("MIIS FOLDER", {"OPEN"}, [&host](RenderConfig&, unsigned) { host.act(GeneralPageAction::OpenMiiFolder); },
                                      "FILES PUT HERE ARE IMPORTED AT EVERY START"));
        }
    }
    if (state.saves_offered) {
        rows.push_back({"SAVES", {}, nullptr, {}, true});
        switch (state.saves_step) {
        case SavesStep::Confirm:
            if (state.saves_by_file) {
                add_import_rows(rows, state, host);
                break;
            }
            rows.push_back(button_row("IMPORT SAVES", {state.saves_replace ? "REPLACE" : "IMPORT", "CANCEL"},
                                      [&host](RenderConfig&, unsigned index) {
                                          host.act(index == 0u ? GeneralPageAction::ConfirmImport : GeneralPageAction::CancelImport);
                                      },
                                      state.saves_replace ? "REPLACES YOUR SAVE AND RESTARTS THE GAME. THE CURRENT SAVE IS BACKED UP"
                                                          : "ADDS THE SAVE AND RESTARTS THE GAME"));
            break;
        case SavesStep::Reading:
            rows.push_back(button_row("IMPORT SAVES", {"CANCEL"}, [&host](RenderConfig&, unsigned) { host.act(GeneralPageAction::CancelImport); },
                                      "READING THE SAVE"));
            break;
        case SavesStep::Importing: rows.push_back({"IMPORT SAVES", "IMPORTING", nullptr, {}}); break;
        default:
            rows.push_back(button_row("IMPORT SAVES", {"IMPORT"}, [&host](RenderConfig&, unsigned) { host.act(GeneralPageAction::ImportSaves); },
                                      state.saves_step == SavesStep::Choosing ? std::string("CHOOSING A FILE")
                                      : state.saves_import_status.empty()
                                          ? std::string("A WII, DOLPHIN OR SWITCH SAVE, OR A NEBULA BACKUP")
                                          : state.saves_import_status));
            break;
        }
        if (state.saves_step == SavesStep::Exporting) {
            rows.push_back({"EXPORT SAVES", "EXPORTING", nullptr, {}});
        } else {
            rows.push_back(button_row("EXPORT SAVES", {"EXPORT"}, [&host](RenderConfig&, unsigned) { host.act(GeneralPageAction::ExportSaves); },
                                      state.saves_export_status.empty() ? std::string("ALL YOUR SAVES IN ONE ZIP FILE")
                                                                        : state.saves_export_status));
        }
        if (state.folders_open) {
            rows.push_back(button_row("SAVES FOLDER", {"OPEN"}, [&host](RenderConfig&, unsigned) { host.act(GeneralPageAction::OpenSaveFolder); },
                                      "REPLACED SAVES ARE KEPT IN A BACKUP FOLDER BESIDE IT"));
        }
    }
    return rows;
}


}  // namespace galaxy::ui
