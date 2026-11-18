// SPDX-License-Identifier: GPL-3.0-only
// When the window is placed again for the display setting (P-110, P-111): one rule for every Windows renderer
// (host_window_win32.cpp reconcile_display). What was last applied to the window is kept beside the setting; the window
// is placed when they differ. A size or monitor the player gives the window by hand (dragging its edge, moving it to
// another monitor, a new scale) is written to both at once (placed_by_player), so it is kept, not undone.
#pragma once

#include <array>
#include <cstring>
#include <string_view>

namespace galaxy::render::host_window {

struct DisplayState {
    unsigned mode = 0;  // display_modes.h: kDisplayWindowed or kDisplayBorderless
    std::array<char, 64> monitor{};  // RenderConfig::monitor: the monitor's device name
    unsigned window_width = 0;       // the windowed client size, in pixels
    unsigned window_height = 0;
};

struct AppliedDisplay {
    bool valid = false;
    DisplayState state{};
};

[[nodiscard]] inline bool same_monitor(const DisplayState& a, const DisplayState& b) noexcept {
    return std::strncmp(a.monitor.data(), b.monitor.data(), a.monitor.size()) == 0;
}

// Place the window for `wanted`: nothing applied yet, the monitors changed, another mode or monitor, or (`windowed`)
// another size while the player is not dragging the window.
[[nodiscard]] inline bool display_needs_placing(const AppliedDisplay& applied, const DisplayState& wanted,
                                                bool monitors_changed, bool windowed, bool dragging) noexcept {
    if (!applied.valid || monitors_changed || applied.state.mode != wanted.mode || !same_monitor(applied.state, wanted)) {
        return true;
    }
    if (!windowed || dragging) {
        return false;
    }
    return applied.state.window_width != wanted.window_width || applied.state.window_height != wanted.window_height;
}

// The setting after the player placed the window by hand: windowed at `width` x `height` on `monitor`.
[[nodiscard]] inline DisplayState placed_by_player(DisplayState setting, unsigned width, unsigned height,
                                                   std::string_view monitor) noexcept {
    setting.window_width = width;
    setting.window_height = height;
    if (!monitor.empty() && monitor.size() < setting.monitor.size()) {
        setting.monitor.fill('\0');
        std::memcpy(setting.monitor.data(), monitor.data(), monitor.size());
    }
    return setting;
}

}  // namespace galaxy::render::host_window
