#include "galaxy/ui/settings_general_page.h"

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error The shared General page must not include Windows.h
#endif

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
class Host final : public galaxy::ui::GeneralPageHost {
public:
    bool empty_languages = false;
    bool focus_offered = false;
    bool discord_offered = true;
    bool saves = false;
    galaxy::ui::SavesStep step = galaxy::ui::SavesStep::Idle;
    bool replace = false;
    std::string status;
    bool by_file = false;
    bool whole_offered = false;
    galaxy::ui::SaveImportFiles imported{};
    galaxy::ui::SaveImportFiles current{};
    mutable galaxy::ui::SaveImportTargets targets{};
    mutable unsigned stepped = 0;
    mutable int action = -1;
    galaxy::ui::GeneralPageState query(const galaxy::RuntimeSettings& settings) const override {
        galaxy::ui::GeneralPageState state;
        state.auto_icons = galaxy::ButtonIcons::Keyboard;
        state.display_language = settings.language == galaxy::GameLanguage::Auto ? galaxy::GameLanguage::English : settings.language;
        if (!empty_languages) state.languages = {galaxy::GameLanguage::English, galaxy::GameLanguage::French};
        state.mii_count = 3;
        state.focus_loss_offered = focus_offered;
        state.discord_offered = discord_offered;
        state.saves_offered = saves;
        state.saves_step = step;
        state.saves_replace = replace;
        state.saves_import_status = status;
        state.saves_by_file = by_file;
        state.saves_whole_offered = whole_offered;
        state.import_files = imported;
        state.current_files = current;
        state.import_targets = targets;
        return state;
    }
    void act(galaxy::ui::GeneralPageAction value) const override { action = static_cast<int>(value); }
    void step_import_target(unsigned file, int delta) const override {
        stepped = file;
        galaxy::ui::save_import_step(targets, file, delta);
    }
};
galaxy::ui::SaveImportFile made(bool luigi, unsigned stars) { return {true, false, luigi, stars}; }
galaxy::ui::SaveImportFile damaged() { return {false, true, false, 0u}; }
bool same(const galaxy::ui::SaveImportTargets& a, std::initializer_list<int> b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](std::uint8_t x, int y) { return x == y; });
}
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
galaxy::ui::SettingsRow& row(std::vector<galaxy::ui::SettingsRow>& rows, const char* label) {
    const auto found = std::find_if(rows.begin(), rows.end(), [label](const auto& r) { return !r.heading && r.label == label; });
    if (found == rows.end()) throw std::runtime_error(label);
    return *found;
}
}

int main() {
    try {
        Host host;
        galaxy::RuntimeSettings runtime;
        galaxy::ui::RenderConfig render;
        auto rows = galaxy::ui::general_page(runtime, runtime, host);
        check(row(rows, "BUTTON ICONS").value == "AUTO (KEYBOARD AND MOUSE)", "auto icon label");
        check(row(rows, "YOUR MIIS").value == "3 OF 100", "Mii count label");
        row(rows, "TEXT SPEED").change(render, 1);
        check(runtime.text_speed == 2, "text speed action");
        row(rows, "LANGUAGE").change(render, 1);
        check(runtime.language == galaxy::GameLanguage::French, "language advances among offered languages");
        row(rows, "LANGUAGE").change(render, -1);
        check(runtime.language == galaxy::GameLanguage::Auto, "language restores saved auto");
        host.empty_languages = true;
        row(rows, "LANGUAGE").change(render, 1);
        check(runtime.language == galaxy::GameLanguage::Auto, "empty language snapshot is ignored");
        row(rows, "DISCORD STATUS").change(render, 1);
        check(!runtime.discord_status, "status action");
        host.discord_offered = false;
        rows = galaxy::ui::general_page(runtime, runtime, host);
        check(std::none_of(rows.begin(), rows.end(), [](const auto& r) { return r.label == "DISCORD STATUS"; }),
              "no Discord row where Discord's desktop app cannot be reached");
        host.discord_offered = true;
        check(std::none_of(rows.begin(), rows.end(), [](const auto& r) { return r.label == "ON LOSING FOCUS"; }), "no focus row where the host does not act on it");
        host.focus_offered = true;
        rows = galaxy::ui::general_page(runtime, runtime, host);
        check(row(rows, "ON LOSING FOCUS").value == "KEEP RUNNING", "focus row default");
        row(rows, "ON LOSING FOCUS").change(render, 1);
        check(runtime.focus_loss == galaxy::FocusLoss::Pause, "focus row steps to pause");
        row(rows, "ON LOSING FOCUS").change(render, 5);
        check(runtime.focus_loss == galaxy::FocusLoss::Mute, "focus row ends at mute");
        row(rows, "ON LOSING FOCUS").change(render, -9);
        check(runtime.focus_loss == galaxy::FocusLoss::KeepRunning, "focus row ends at keep running");
        row(rows, "IMPORT MIIS").press(render, 0);
        check(host.action == static_cast<int>(galaxy::ui::GeneralPageAction::ImportMiis), "import action");
        row(rows, "MIIS FOLDER").press(render, 0);
        check(host.action == static_cast<int>(galaxy::ui::GeneralPageAction::OpenMiiFolder), "folder action");
        check(std::none_of(rows.begin(), rows.end(), [](const auto& r) { return r.label == "IMPORT SAVES"; }),
              "no SAVES rows without a save folder");
        host.saves = true;
        rows = galaxy::ui::general_page(runtime, runtime, host);
        check(row(rows, "IMPORT SAVES").buttons == std::vector<std::string>{"IMPORT"}, "import button");
        row(rows, "IMPORT SAVES").press(render, 0);
        check(host.action == static_cast<int>(galaxy::ui::GeneralPageAction::ImportSaves), "import saves action");
        row(rows, "EXPORT SAVES").press(render, 0);
        check(host.action == static_cast<int>(galaxy::ui::GeneralPageAction::ExportSaves), "export saves action");
        row(rows, "SAVES FOLDER").press(render, 0);
        check(host.action == static_cast<int>(galaxy::ui::GeneralPageAction::OpenSaveFolder), "saves folder action");
        host.status = "THIS SAVE IS DAMAGED";
        rows = galaxy::ui::general_page(runtime, runtime, host);
        check(row(rows, "IMPORT SAVES").help == "THIS SAVE IS DAMAGED", "an import's result is the row's help");
        host.status.clear();
        host.step = galaxy::ui::SavesStep::Confirm;
        host.replace = true;
        rows = galaxy::ui::general_page(runtime, runtime, host);
        check((row(rows, "IMPORT SAVES").buttons == std::vector<std::string>{"REPLACE", "CANCEL"}), "confirm asks Replace");
        row(rows, "IMPORT SAVES").press(render, 0);
        check(host.action == static_cast<int>(galaxy::ui::GeneralPageAction::ConfirmImport), "replace confirms");
        row(rows, "IMPORT SAVES").press(render, 1);
        check(host.action == static_cast<int>(galaxy::ui::GeneralPageAction::CancelImport), "cancel cancels");
        host.replace = false;
        rows = galaxy::ui::general_page(runtime, runtime, host);
        check(row(rows, "IMPORT SAVES").buttons.front() == "IMPORT", "nothing to replace: the confirm says Import");
        host.step = galaxy::ui::SavesStep::Importing;
        rows = galaxy::ui::general_page(runtime, runtime, host);
        check(row(rows, "IMPORT SAVES").value == "IMPORTING" && row(rows, "IMPORT SAVES").buttons.empty(), "importing shows its state");
        host.step = galaxy::ui::SavesStep::Exporting;
        rows = galaxy::ui::general_page(runtime, runtime, host);
        check(row(rows, "EXPORT SAVES").value == "EXPORTING", "exporting shows its state");
        host.step = galaxy::ui::SavesStep::Reading;
        rows = galaxy::ui::general_page(runtime, runtime, host);
        check((row(rows, "IMPORT SAVES").buttons == std::vector<std::string>{"CANCEL"}) &&
                  row(rows, "IMPORT SAVES").help == "READING THE SAVE",
              "the game's read can be cancelled");
        row(rows, "IMPORT SAVES").press(render, 0);
        check(host.action == static_cast<int>(galaxy::ui::GeneralPageAction::CancelImport), "reading cancels");

        // The first choice: its own number when free, else the first free file; a damaged file left out; none past
        // the free files.
        {
            galaxy::ui::SaveImportFiles imported{made(false, 42u), made(true, 7u), damaged(), {}, made(false, 1u), {}};
            galaxy::ui::SaveImportFiles current{made(true, 1u), {}, {}, made(false, 3u), {}, {}};
            check(same(galaxy::ui::save_import_first_targets(imported, current), {2, 3, 0, 0, 5, 0}), "first choice");
            galaxy::ui::SaveImportFiles full{made(false, 1u), made(false, 2u), damaged(), made(false, 4u), made(false, 5u), made(false, 6u)};
            check(same(galaxy::ui::save_import_first_targets(imported, full), {0, 0, 0, 0, 0, 0}), "full: all left out");
            galaxy::ui::SaveImportFiles one_free{made(false, 1u), made(false, 2u), {}, made(false, 4u), made(false, 5u), made(false, 6u)};
            check(same(galaxy::ui::save_import_first_targets(imported, one_free), {3, 0, 0, 0, 0, 0}), "one free file: the first imported");
            check(same(galaxy::ui::save_import_first_targets(galaxy::ui::SaveImportFiles{}, current), {0, 0, 0, 0, 0, 0}),
                  "an empty save places nothing");
        }
        // A step passes the files the other imported files go to, through left out, and wraps around.
        {
            galaxy::ui::SaveImportTargets targets{2, 3, 0, 0, 5, 0};
            galaxy::ui::save_import_step(targets, 1u, 1);
            check(same(targets, {4, 3, 0, 0, 5, 0}), "a step passes a taken file");
            galaxy::ui::save_import_step(targets, 1u, -1);
            check(same(targets, {2, 3, 0, 0, 5, 0}), "back past the taken file");
            galaxy::ui::save_import_step(targets, 1u, -2);
            check(same(targets, {0, 3, 0, 0, 5, 0}), "down to left out");
            galaxy::ui::save_import_step(targets, 1u, -1);
            check(same(targets, {6, 3, 0, 0, 5, 0}), "left out wraps to the last file");
            galaxy::ui::save_import_step(targets, 5u, 1);
            check(same(targets, {6, 3, 0, 0, 0, 0}), "past the last free file: left out");
            galaxy::ui::save_import_step(targets, 9u, 1);
            galaxy::ui::save_import_step(targets, 2u, 0);
            check(same(targets, {6, 3, 0, 0, 0, 0}), "no file past 6, no step of 0");
            galaxy::ui::SaveImportTargets crowded{1, 2, 3, 4, 5, 0};
            galaxy::ui::save_import_step(crowded, 6u, 1);
            check(same(crowded, {1, 2, 3, 4, 5, 6}), "the one file left");
            galaxy::ui::save_import_step(crowded, 6u, 1);
            check(same(crowded, {1, 2, 3, 4, 5, 0}), "then left out again");
            for (unsigned file = 1u; file <= 6u; ++file) {
                for (int delta : {1, -1, 3, -5, 7}) {
                    galaxy::ui::save_import_step(crowded, file, delta);
                    std::array<int, 7> uses{};
                    for (const std::uint8_t target : crowded) ++uses[target];
                    for (std::size_t n = 1; n < uses.size(); ++n) check(uses[n] <= 1, "never two files to one");
                }
            }
        }
        // The choice's rows: where each file goes, a replaced file named, a damaged one left out, the buttons.
        host.step = galaxy::ui::SavesStep::Confirm;
        host.by_file = true;
        host.imported = {made(false, 42u), {}, damaged(), made(true, 1u), {}, {}};
        host.current = {made(true, 1u), {}, damaged(), {}, {}, {}};
        host.targets = {1, 0, 0, 0, 0, 0};
        rows = galaxy::ui::general_page(runtime, runtime, host);
        check(row(rows, "IMPORTED FILE 1").value == "REPLACE FILE 1", "a file that is there is replaced");
        check(row(rows, "IMPORTED FILE 1").help == "MARIO, 42 STARS. IN FILE 1 NOW: LUIGI, 1 STAR", "the replaced file named");
        check(row(rows, "IMPORTED FILE 3").value == "DAMAGED" && !row(rows, "IMPORTED FILE 3").change, "a damaged file is left out");
        check(row(rows, "IMPORTED FILE 4").value == "LEAVE OUT" && row(rows, "IMPORTED FILE 4").help == "LUIGI, 1 STAR",
              "a file left out");
        check(std::none_of(rows.begin(), rows.end(), [](const auto& r) { return r.label == "IMPORTED FILE 2"; }),
              "no row for an empty file");
        check((row(rows, "IMPORT SAVES").buttons == std::vector<std::string>{"IMPORT", "CANCEL"}), "import and cancel");
        check(row(rows, "IMPORT SAVES").help == "ADDS THE CHOSEN FILES AND RESTARTS THE GAME. REPLACED FILES ARE BACKED UP",
              "a replace says the backup");
        check(std::none_of(rows.begin(), rows.end(), [](const auto& r) { return r.label == "WHOLE SAVE"; }),
              "no whole import when the file holds only this save");
        row(rows, "IMPORTED FILE 4").change(render, 1);
        check(host.stepped == 4u && host.targets[3] == 2u, "a step goes to the host");
        rows = galaxy::ui::general_page(runtime, runtime, host);
        check(row(rows, "IMPORTED FILE 4").value == "ADD AS FILE 2" && row(rows, "IMPORTED FILE 4").help == "LUIGI, 1 STAR",
              "a free file is added");
        host.targets = {3, 0, 0, 0, 0, 0};
        rows = galaxy::ui::general_page(runtime, runtime, host);
        check(row(rows, "IMPORTED FILE 1").help == "MARIO, 42 STARS. IN FILE 3 NOW: A DAMAGED FILE", "a damaged file replaced");
        host.targets = {2, 0, 0, 0, 0, 0};
        rows = galaxy::ui::general_page(runtime, runtime, host);
        check(row(rows, "IMPORT SAVES").help == "ADDS THE CHOSEN FILES AND RESTARTS THE GAME", "adding only: no backup said");
        row(rows, "IMPORT SAVES").press(render, 0);
        check(host.action == static_cast<int>(galaxy::ui::GeneralPageAction::ConfirmImport), "import confirms");
        row(rows, "IMPORT SAVES").press(render, 1);
        check(host.action == static_cast<int>(galaxy::ui::GeneralPageAction::CancelImport), "cancel cancels the choice");
        host.targets = {};
        rows = galaxy::ui::general_page(runtime, runtime, host);
        check((row(rows, "IMPORT SAVES").buttons == std::vector<std::string>{"CANCEL"}) &&
                  row(rows, "IMPORT SAVES").help == "CHOOSE WHERE A FILE GOES",
              "nothing chosen: nothing to import");
        row(rows, "IMPORT SAVES").press(render, 0);
        check(host.action == static_cast<int>(galaxy::ui::GeneralPageAction::CancelImport), "its one button cancels");
        host.whole_offered = true;
        rows = galaxy::ui::general_page(runtime, runtime, host);
        row(rows, "WHOLE SAVE").press(render, 0);
        check(host.action == static_cast<int>(galaxy::ui::GeneralPageAction::ReplaceAllSaves), "the whole save beside the choice");
        std::cout << "General page: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
