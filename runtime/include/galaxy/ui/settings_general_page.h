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
    bool folders_open = true;  // the platform shows a folder's files (host_window::folders_open)
    bool focus_loss_offered = false;  // the host acts on a window that loses focus (the desktop host)
};

enum class GeneralPageAction { ImportMiis, OpenMiiFolder };
class GeneralPageHost {
public:
    virtual ~GeneralPageHost() = default;
    virtual GeneralPageState query(const RuntimeSettings& settings) const = 0;
    virtual void act(GeneralPageAction action) const = 0;
};

// The host and draft outlive the rows that refer to them.
std::vector<SettingsRow> general_page(const RuntimeSettings& runtime, RuntimeSettings& draft,
                                      const GeneralPageHost& host);

}  // namespace galaxy::ui
