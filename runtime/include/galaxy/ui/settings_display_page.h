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
    bool fullscreen_only = false;
    bool temporal_upscaling = false;  // a temporal upscaler runs: the HUD draws at the display's resolution
    bool hdr_offered = false;         // the display is in HDR mode, or Windows turns it on for the game (get_display_hdr)
    bool hdr_display_on = false;      // the display is in HDR mode now
    bool hdr_active = false;          // the swap chain is HDR10 now
    bool hdr_in_use = false;          // the settings in use (not the draft) have HDR on
    bool hdr_held_sdr = false;        // frame generation keeps the picture SDR
    bool hdr_refused = false;         // Windows keeps HDR off for the display (it refused, or it was turned off outside the game)
};
enum class DisplayChange { Monitor, Mode, Resolution, Aspect };
class DisplayPageHost {
public:
    virtual ~DisplayPageHost() = default;
    virtual DisplayPageState query(const RenderConfig& render) const = 0;
    virtual void changed(DisplayChange change, RenderConfig& render) const = 0;
    // HDR CALIBRATION's button: the calibration opens from `render`'s levels (ui/hdr_calibration.h).
    virtual void calibrate_hdr(const RenderConfig& render) const { (void)render; }
};

// The host outlives the returned row actions.
std::vector<SettingsRow> display_page(const RenderConfig& render, const DisplayPageHost& host);

}  // namespace galaxy::ui
