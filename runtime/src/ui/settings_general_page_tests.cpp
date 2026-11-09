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
    mutable int action = -1;
    galaxy::ui::GeneralPageState query(const galaxy::RuntimeSettings& settings) const override {
        galaxy::ui::GeneralPageState state;
        state.auto_icons = galaxy::ButtonIcons::Keyboard;
        state.display_language = settings.language == galaxy::GameLanguage::Auto ? galaxy::GameLanguage::English : settings.language;
        if (!empty_languages) state.languages = {galaxy::GameLanguage::English, galaxy::GameLanguage::French};
        state.mii_count = 3;
        state.focus_loss_offered = focus_offered;
        return state;
    }
    void act(galaxy::ui::GeneralPageAction value) const override { action = static_cast<int>(value); }
};
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
        std::cout << "General page: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
