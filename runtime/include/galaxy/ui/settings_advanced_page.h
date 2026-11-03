// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "galaxy/ui/settings_model.h"

namespace galaxy::ui {

// The settings lock serializes row building and actions; this model outlives them.
class AdvancedPageModel {
public:
    using Close = void (*)();
    std::vector<SettingsRow> rows(Close close);
    std::size_t selected_stage() const { return stage_; }
    std::uint32_t selected_star() const { return star_; }

private:
    std::size_t stage_ = 0;
    std::uint32_t star_ = 1;
};

}  // namespace galaxy::ui
