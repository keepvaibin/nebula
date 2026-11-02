// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "galaxy/ui/settings_model.h"
#include <optional>
#include <utility>

namespace galaxy::ui {

struct DisplayMonitorChoice {
    std::string id;
    std::string name;
};
struct DisplayPageState {
    std::vector<DisplayMonitorChoice> monitors;
    std::optional<std::size_t> current_monitor;
    std::pair<unsigned, unsigned> output_size;
    std::vector<std::pair<unsigned, unsigned>> window_sizes;
    FrameRateCaps frame_rates;
};
enum class DisplayChange { Monitor, Mode, Resolution, Aspect };
class DisplayPageHost {
public:
    virtual ~DisplayPageHost() = default;
    virtual DisplayPageState query(const RenderConfig& render) const = 0;
    virtual void changed(DisplayChange change, RenderConfig& render) const = 0;
};

// The host outlives the returned row actions.
std::vector<SettingsRow> display_page(const RenderConfig& render, const DisplayPageHost& host);

}  // namespace galaxy::ui
