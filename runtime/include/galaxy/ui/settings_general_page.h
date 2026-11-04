// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "galaxy/ui/settings_model.h"

namespace galaxy::ui {

struct GeneralPageState {
    ButtonIcons auto_icons = ButtonIcons::Game;
    GameLanguage display_language = GameLanguage::English;
    std::vector<GameLanguage> languages;
    GameLanguage saved_language = GameLanguage::Auto;
    GameLanguage resolved_saved_language = GameLanguage::English;
    std::string mii_status;
    bool choosing_miis = false;
    int mii_count = 0;
    // A phone's on-screen controls; size in percent, 0: what the game gives this screen.
    bool touch_controls = false;
    unsigned touch_size = 0;
    unsigned touch_opacity = 60;
    bool touch_vibration = true;
};

enum class GeneralPageAction { ImportMiis, OpenMiiFolder, ResetTouchPlaces };
class GeneralPageHost {
public:
    virtual ~GeneralPageHost() = default;
    virtual GeneralPageState query(const RuntimeSettings& settings) const = 0;
    virtual void act(GeneralPageAction action) const = 0;
    virtual void set_touch_controls(unsigned, unsigned, bool) const {}
};

// The host and draft outlive the rows that refer to them.
std::vector<SettingsRow> general_page(const RuntimeSettings& runtime, RuntimeSettings& draft,
                                      const GeneralPageHost& host);

}  // namespace galaxy::ui
