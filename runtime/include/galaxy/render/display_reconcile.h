// SPDX-License-Identifier: GPL-3.0-only
// When the window is placed again for the display setting (P-110, P-111, P-103): one rule for every renderer and host
// (host_window_win32.cpp reconcile_display, host_window_sdl.cpp reconcile_display). What was last applied to the window is kept beside the setting; the window
// is placed when they differ. A size or monitor the player gives the window by hand (dragging its edge, moving it to
// another monitor, a new scale) is written to both at once (placed_by_player), so it is kept, not undone.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

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

// The setting's key for each display, in the order the system lists them, where only names are given (SDL): the name,
// cut at a whole character to leave room in RenderConfig::monitor, and " (2)", " (3)" after a second and third of one
// name, so two screens of one model stay apart.
[[nodiscard]] inline std::vector<std::string> display_keys(const std::vector<std::string>& names) {
    constexpr std::size_t kMost = std::tuple_size<decltype(DisplayState::monitor)>::value - 1u - 6u;  // " (999)"
    std::vector<std::string> bases;
    bases.reserve(names.size());
    for (const std::string& name : names) {
        std::size_t cut = std::min(name.size(), kMost);
        while (cut > 0 && cut < name.size() && (static_cast<unsigned char>(name[cut]) & 0xC0u) == 0x80u) {
            --cut;
        }
        bases.push_back(name.substr(0, cut));
    }
    std::vector<std::string> keys;
    keys.reserve(names.size());
    for (std::size_t i = 0; i < bases.size(); ++i) {
        std::size_t before = 0;
        for (std::size_t j = 0; j < i; ++j) {
            before += bases[j] == bases[i] ? 1u : 0u;
        }
        keys.push_back(before == 0 ? bases[i] : bases[i] + " (" + std::to_string(before + 1u) + ")");
    }
    return keys;
}

}  // namespace galaxy::render::host_window
