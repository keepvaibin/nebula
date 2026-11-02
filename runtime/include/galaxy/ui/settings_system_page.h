// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "galaxy/ui/settings_model.h"

namespace galaxy::ui {
std::vector<SettingsRow> system_page(const render::RenderFeatureCaps& caps, std::string post_process,
                                     void (*record_diagnostics)());
}  // namespace galaxy::ui
