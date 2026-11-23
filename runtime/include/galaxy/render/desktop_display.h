// SPDX-License-Identifier: GPL-3.0-only
// The desktop SDL host's displays and the rules that choose and place the game's window, as plain numbers: the list of
// displays as the settings show it, which one a saved setting names, whether and how the window is placed again when a
// setting changes, the picture's size in full screen, the size of a windowed window in the window system's own units, and
// when the system's pointer is hidden. The rules are the Windows host's (host_window_win32.cpp,
// RendererVk::reconcile_display_state); the SDL host (platform/sdl/host_window_sdl.cpp) reads SDL's displays into these
// types and carries the answers out, and the unit test runs them with made-up displays.
#pragma once

#include "galaxy/render/display_modes.h"
#include "galaxy/render/render_config.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace galaxy::render::desktop_display {

// One display, in pixels (the system's own units differ from pixels where it scales: Wayland and macOS).
struct Display {
    std::uint32_t handle = 0;         // the system's own number for it (SDL's display ID), good for this run only
    std::string name;                 // the system's name for it; empty when it gave none
    int x = 0;                        // its top left on the desktop, in the system's units: orders the list
    int y = 0;
    unsigned width = 0;               // the desktop mode, in pixels
    unsigned height = 0;
    double refresh_hz = 0.0;          // of the current mode; 0 when the system did not say
    long usable_width = 0;            // the area a window may take (less panels and bars), in pixels
    long usable_height = 0;
    bool primary = false;
    std::vector<DisplayModeInfo> modes;  // 32-bit progressive modes the display offers, in pixels, sorted (sort_display_modes)

    [[nodiscard]] bool operator==(const Display&) const = default;
};

// A display mode as the system reports it: a size in its own units and the density that turns them into pixels.
struct RawMode {
    int width = 0;
    int height = 0;
    float pixel_density = 1.0f;       // 0 or less: 1
    float refresh_hz = 0.0f;          // 0: not given
};

[[nodiscard]] inline unsigned pixels_of(int size, float density) noexcept {
    if (size <= 0) return 0u;
    const double scale = density > 0.0f ? static_cast<double>(density) : 1.0;
    return static_cast<unsigned>(std::max(1L, std::lround(static_cast<double>(size) * scale)));
}

// A refresh rate in whole hertz as the settings and the Windows host show it (239.97 is 240); 0 when it is not known.
[[nodiscard]] inline unsigned whole_hz(double hz) noexcept {
    return hz >= 1.0 ? static_cast<unsigned>(std::lround(hz)) : 0u;
}

// The modes a display offers, as Windows lists them (enumerate_monitors): 32-bit progressive modes of at least 640 x 480,
// each size and rate once, the largest first and then the faster; the current mode alone when none is left.
[[nodiscard]] inline std::vector<DisplayModeInfo> modes_of(const std::vector<RawMode>& raw, DisplayModeInfo current) {
    std::vector<DisplayModeInfo> modes;
    for (const RawMode& mode : raw) {
        DisplayModeInfo found{pixels_of(mode.width, mode.pixel_density), pixels_of(mode.height, mode.pixel_density),
                              std::max(1u, whole_hz(mode.refresh_hz))};
        if (found.width < 640u || found.height < 480u) continue;
        if (std::find(modes.begin(), modes.end(), found) == modes.end()) modes.push_back(found);
    }
    if (modes.empty()) modes.push_back(current);
    sort_display_modes(modes);
    return modes;
}

// The list the settings show: the primary display first, then the others left to right and top to bottom (Windows:
// enumerate_monitors).
inline void order_displays(std::vector<Display>& displays) {
    std::stable_sort(displays.begin(), displays.end(), [](const Display& a, const Display& b) {
        if (a.primary != b.primary) return a.primary;
        if (a.x != b.x) return a.x < b.x;
        return a.y < b.y;
    });
}

// RenderConfig::monitor holds the display's id as text with its end mark.
inline constexpr std::size_t kIdBytes = sizeof(RenderConfig::monitor) - 1u;

// The saved id of each display, in the list's order: its name (its place in the list where it gave none), and the name
// again with " 2", " 3" for each further display of the same name, so two of one model stay two choices. At most kIdBytes
// bytes, cut at a character's edge. The id is what config.ini keeps, so it is the same at every start while the
// displays are.
[[nodiscard]] inline std::vector<std::string> display_ids(const std::vector<Display>& displays) {
    std::vector<std::string> ids;
    ids.reserve(displays.size());
    for (std::size_t i = 0; i < displays.size(); ++i) {
        std::string base = displays[i].name.empty() ? std::string("Display") : displays[i].name;
        // Room for the number after it.
        const std::size_t room = kIdBytes - 4u;
        if (base.size() > room) {
            std::size_t cut = room;
            while (cut > 0u && (static_cast<unsigned char>(base[cut]) & 0xC0u) == 0x80u) --cut;  // not inside a character
            base.resize(cut);
        }
        std::string id = base;
        for (unsigned number = 2u; std::find(ids.begin(), ids.end(), id) != ids.end(); ++number) {
            id = base + ' ' + std::to_string(number);
        }
        ids.push_back(std::move(id));
    }
    return ids;
}

[[nodiscard]] inline bool same_id(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto fold = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
        if (fold(a[i]) != fold(b[i])) return false;
    }
    return true;
}

// The display a saved id names; else the primary one, else the first; none when there are no displays (Windows:
// find_monitor).
[[nodiscard]] inline std::optional<std::size_t> find_display(const std::vector<std::string>& ids,
                                                             const std::vector<Display>& displays,
                                                             std::string_view wanted) {
    if (!wanted.empty()) {
        for (std::size_t i = 0; i < ids.size() && i < displays.size(); ++i) {
            if (same_id(ids[i], wanted)) return i;
        }
    }
    for (std::size_t i = 0; i < displays.size(); ++i) {
        if (displays[i].primary) return i;
    }
    if (displays.empty()) return std::nullopt;
    return std::size_t{0};
}

// What the settings ask of the window: the display mode (RenderConfig::display_mode), the display's id and the windowed
// client size.
struct Choice {
    unsigned mode = kDisplayWindowed;
    std::string monitor;
    unsigned window_width = 0;
    unsigned window_height = 0;

    [[nodiscard]] bool operator==(const Choice&) const = default;
};

// The choice a configuration holds. A hidden test window stays windowed whatever the setting is.
[[nodiscard]] inline Choice choice_of(const RenderConfig& config, bool hidden_window) {
    Choice choice;
    choice.mode = hidden_window ? kDisplayWindowed : std::min(config.display_mode, kDisplayBorderless);
    const auto end = std::find(config.monitor.begin(), config.monitor.end(), '\0');
    choice.monitor.assign(config.monitor.begin(), end);
    choice.window_width = config.window_width;
    choice.window_height = config.window_height;
    return choice;
}

// Whether the window is placed again: not before it has been placed, when a display was added, removed or changed, or when
// the mode or the display changed; in windowed mode also when the size did (RendererVk::reconcile_display_state).
[[nodiscard]] inline bool must_place(const std::optional<Choice>& placed, const Choice& wanted, bool displays_changed) {
    if (!placed || displays_changed || placed->mode != wanted.mode || placed->monitor != wanted.monitor) return true;
    if (wanted.mode != kDisplayWindowed) return false;
    return placed->window_width != wanted.window_width || placed->window_height != wanted.window_height;
}

// The area a window may take less its frame: the client size a window can have at most.
[[nodiscard]] constexpr std::pair<long, long> max_client_size(long usable_width, long usable_height, long frame_width,
                                                              long frame_height) noexcept {
    return {std::max(0L, usable_width - frame_width), std::max(0L, usable_height - frame_height)};
}

struct Placement {
    bool fullscreen = false;          // borderless full screen on the display
    std::size_t display = 0;          // its place in the list
    unsigned client_width = 0;        // windowed: the client size in pixels, as clamped (full screen: the display's pixels)
    unsigned client_height = 0;
};

// Where the window goes for a choice: borderless full screen at the desktop mode of the display, or a window of the
// configured client size on it, limited to the area it may take and never below 320 x 240 (Windows:
// windowed_rect_on_monitor). None when there are no displays.
[[nodiscard]] inline std::optional<Placement> place_window(const Choice& choice, const std::vector<std::string>& ids,
                                                           const std::vector<Display>& displays, long frame_width,
                                                           long frame_height) {
    const std::optional<std::size_t> found = find_display(ids, displays, choice.monitor);
    if (!found) return std::nullopt;
    const Display& display = displays[*found];
    Placement placement;
    placement.display = *found;
    if (choice.mode != kDisplayWindowed) {
        placement.fullscreen = true;
        placement.client_width = display.width;
        placement.client_height = display.height;
        return placement;
    }
    const auto [width, height] = windowed_client_size(display.usable_width, display.usable_height, frame_width, frame_height,
                                                      choice.window_width, choice.window_height);
    placement.client_width = static_cast<unsigned>(width);
    placement.client_height = static_cast<unsigned>(height);
    return placement;
}

struct Pixels {
    unsigned width = 0;
    unsigned height = 0;

    [[nodiscard]] bool operator==(const Pixels&) const noexcept = default;
};

// The picture's size in full screen. A system that scales makes a window of whole points and then the picture of whole
// pixels from them, which can be a pixel or two off the display's own (a 2560-pixel display at a scale of 150 % reports a
// window of 1707 points, so 2561 pixels). The picture is the display's own pixels when the reported size is that size
// rounded; any other size the system reports (a window not yet full screen, another mode) is kept. `density`: pixels to a
// point.
[[nodiscard]] inline Pixels fullscreen_pixels(Pixels display, Pixels reported, double density) noexcept {
    if (display.width == 0u || display.height == 0u) return reported;
    const double rounding = density > 1.0 ? std::ceil(density) : 1.0;
    const auto close = [rounding](unsigned a, unsigned b) {
        return std::abs(static_cast<double>(a) - static_cast<double>(b)) <= rounding;
    };
    return close(display.width, reported.width) && close(display.height, reported.height) ? display : reported;
}

// A client size in pixels as the window system's own units at a density of pixels to a point (a window of 854 pixels at
// 1.5 is 569 points, which is 854 pixels again).
[[nodiscard]] inline unsigned window_points(unsigned pixels, double density) noexcept {
    const double scale = density > 0.0 ? density : 1.0;
    return static_cast<unsigned>(std::max(1L, std::lround(static_cast<double>(pixels) / scale)));
}

// The system's pointer is hidden while the game's window has the keyboard focus and is not minimised (the game draws its
// own); over the window's frame and outside it the system shows its own, as it does when the window loses the focus
// (Windows: WM_SETCURSOR, WM_KILLFOCUS; the Mac host's update_cursor).
[[nodiscard]] constexpr bool os_cursor_hidden(bool window_focused, bool window_minimized) noexcept {
    return window_focused && !window_minimized;
}

}  // namespace galaxy::render::desktop_display
