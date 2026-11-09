// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "galaxy/ui/settings_model.h"

namespace galaxy::ui {
// The GPU timing rows: the switch (off at every start) and, while it is on, each pass's time.
struct GpuTimingView {
    bool on = false;
    void (*toggle)() = nullptr;
    std::vector<std::pair<std::string, float>> passes;  // name, milliseconds
};

std::vector<SettingsRow> system_page(const render::RenderFeatureCaps& caps, std::string post_process,
                                     void (*record_diagnostics)(), const GpuTimingView* timing = nullptr);
}  // namespace galaxy::ui
