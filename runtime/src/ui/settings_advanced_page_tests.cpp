#include "galaxy/ui/settings_advanced_page.h"
#include "galaxy/render/galaxy_select.h"

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error The shared Advanced page must not include Windows.h
#endif

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
int closed = 0;
void close_page() { ++closed; }
void check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
galaxy::ui::SettingsRow& row(std::vector<galaxy::ui::SettingsRow>& rows, const char* label) {
    const auto found = std::find_if(rows.begin(), rows.end(), [label](const auto& r) { return !r.heading && r.label == label; });
    if (found == rows.end()) throw std::runtime_error(label);
    return *found;
}
}

int main() {
    try {
        galaxy::ui::AdvancedPageModel model;
        galaxy::ui::RenderConfig config;
        auto rows = model.rows(close_page);
        check(row(rows, "GALAXY").value == "COMET OBSERVATORY", "observatory fallback label");
        row(rows, "GALAXY").change(config, 1);
        row(rows, "STAR").change(config, 1);
        check(model.selected_stage() == 1 && model.selected_star() == 2, "stage and mission selection");
        galaxy::render::set_galaxy_select_name(1, 0, "GALAXY NAME");
        galaxy::render::set_galaxy_select_name(1, 2, "MISSION NAME");
        rows = model.rows(close_page);
        check(row(rows, "GALAXY").value == "GALAXY NAME" && row(rows, "GALAXY").names == 2, "game-provided galaxy name");
        check(row(rows, "STAR").value == "2  MISSION NAME", "game-provided mission name");
        row(rows, "TRAVEL").press(config, 0);
        std::size_t stage = 0; std::uint32_t star = 0;
        check(galaxy::render::take_galaxy_travel(stage, star) && stage == 1 && star == 2 && closed == 1, "native travel request and close");
        check(!galaxy::render::take_galaxy_travel(stage, star), "travel consumed once");
        row(rows, "GRAND STARS").press(config, 0);
        check(galaxy::render::take_grand_stars() && closed == 2, "Grand Stars request and close");
        std::cout << "Advanced page: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
