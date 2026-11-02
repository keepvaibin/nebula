// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/ui/settings_advanced_page.h"
#include "galaxy/render/galaxy_select.h"
#include <algorithm>
#include <cstdio>
#include <iostream>

namespace galaxy::ui {
using namespace galaxy::render;
std::vector<SettingsRow> AdvancedPageModel::rows(Close close) {
    std::vector<SettingsRow> rows;
    want_galaxy_select_names();
    const std::size_t stage = std::min(stage_, kGalaxyStageCount - 1u);
    const std::uint32_t star = std::clamp(star_, 1u, kGalaxyStages[stage].stars);
    std::string name = galaxy_select_name(stage);
    if (name.empty()) {
        name = std::string_view(kGalaxyStages[stage].id) == kObservatoryId ? std::string(kObservatoryName)
                                                                            : upper_ascii(kGalaxyStages[stage].id);
    }
    rows.push_back({"GALAXY SELECT", {}, nullptr, {}, true});
    rows.push_back({"GALAXY", name,
                    [this](RenderConfig&, int delta) {
                        stage_ = settings_step(
                            static_cast<unsigned>(std::min(stage_, kGalaxyStageCount - 1u)),
                            static_cast<unsigned>(kGalaxyStageCount), delta);
                        star_ = 1u;
                    },
                    "ANY GALAXY, FOR TESTING. STARS COLLECTED THERE ARE SAVED"});
    rows.back().names = 2u;  // the game's own name in the played language
    std::string star_name = galaxy_select_star_name(stage, star);
    char star_text[96]{};
    std::snprintf(star_text, sizeof(star_text), star_name.empty() ? "%u" : "%u  %s", star, star_name.c_str());
    rows.push_back({"STAR", star_text,
                    [this](RenderConfig&, int delta) {
                        const std::size_t at = std::min(stage_, kGalaxyStageCount - 1u);
                        star_ = settings_step(star_ - 1u, kGalaxyStages[at].stars,
                                                             delta) + 1u;
                    },
                    "THE MISSION TO PLAY"});
    rows.back().names = 2u;
    rows.push_back(button_row("TRAVEL", {"GO"},
                    [this, close](RenderConfig&, unsigned) {
                        const std::size_t at = std::min(stage_, kGalaxyStageCount - 1u);
                        request_galaxy_travel(at, std::clamp(star_, 1u, kGalaxyStages[at].stars));
                        std::cerr << "[settings] travel to " << kGalaxyStages[at].id << ' '
                                  << star_ << '\n';
                        if (close) close();
                    },
                    "LOADS IT WITH THE GAME OWN STAGE CHANGE, FROM ANY GALAXY"));
    rows.push_back(button_row("GRAND STARS", {"GET"},
                    [this, close](RenderConfig&, unsigned) {
                        request_grand_stars();
                        std::cerr << "[settings] grand stars\n";
                        if (close) close();
                    },
                    "GIVES THIS SAVE FILE GRAND STARS 1-6 (EVERY DOME POWERED) AND LOADS THE OBSERVATORY. "
                    "USE A SPARE FILE"));
    return rows;
}


}  // namespace galaxy::ui
