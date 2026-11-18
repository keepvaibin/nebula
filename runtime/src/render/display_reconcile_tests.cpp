// SPDX-License-Identifier: GPL-3.0-only
// The display rule (display_reconcile.h, P-110 and P-111): the window is placed when the setting differs from what was
// applied, and a placement the player made by hand is kept.
#include "galaxy/render/display_reconcile.h"

#include <cstdio>

using namespace galaxy::render::host_window;

namespace {

bool g_ok = true;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        g_ok = false;
    }
}

constexpr unsigned kWindowed = 0;
constexpr unsigned kBorderless = 1;

DisplayState state(unsigned mode, const char* monitor, unsigned width, unsigned height) {
    DisplayState s;
    s.mode = mode;
    std::snprintf(s.monitor.data(), s.monitor.size(), "%s", monitor);
    s.window_width = width;
    s.window_height = height;
    return s;
}

}  // namespace

int main() {
    const DisplayState one = state(kWindowed, "\\\\.\\DISPLAY1", 1280, 720);
    AppliedDisplay applied;
    expect(display_needs_placing(applied, one, false, true, false), "nothing applied yet: placed");
    applied = {true, one};
    expect(!display_needs_placing(applied, one, false, true, false), "as applied: left alone");
    expect(display_needs_placing(applied, one, true, true, false), "the monitors changed: placed again");
    expect(display_needs_placing(applied, state(kBorderless, "\\\\.\\DISPLAY1", 1280, 720), false, false, false),
           "another mode: placed");
    expect(display_needs_placing(applied, state(kWindowed, "\\\\.\\DISPLAY2", 1280, 720), false, true, false),
           "another monitor: placed");
    const DisplayState bigger = state(kWindowed, "\\\\.\\DISPLAY1", 1600, 900);
    expect(display_needs_placing(applied, bigger, false, true, false), "a size chosen in the settings: placed");
    expect(!display_needs_placing(applied, bigger, false, true, true), "while the player drags the window: left alone");
    AppliedDisplay borderless{true, state(kBorderless, "\\\\.\\DISPLAY1", 1280, 720)};
    expect(!display_needs_placing(borderless, state(kBorderless, "\\\\.\\DISPLAY1", 1600, 900), false, false, false),
           "borderless: the windowed size does not move it");

    // The player drags the window to 1500 x 800 on the second monitor. Before, only the setting took the size (and not
    // the monitor), the applied state kept the old one, and the next frame put the window back in the middle of the
    // first monitor.
    const DisplayState setting_before = one;
    const DisplayState old_setting = [&] {
        DisplayState s = setting_before;
        s.window_width = 1500;
        s.window_height = 800;
        return s;
    }();
    expect(display_needs_placing(applied, old_setting, false, true, false), "the old way: the window snaps back");
    const DisplayState now = placed_by_player(setting_before, 1500, 800, "\\\\.\\DISPLAY2");
    expect(now.window_width == 1500 && now.window_height == 800 && same_monitor(now, state(0, "\\\\.\\DISPLAY2", 0, 0)),
           "the setting takes the size and the monitor");
    applied = {true, now};
    expect(!display_needs_placing(applied, now, false, true, false), "placed by hand: kept");
    expect(placed_by_player(setting_before, 800, 600, "").monitor == setting_before.monitor,
           "no monitor known: the setting's stays");

    std::printf("%s\n", g_ok ? "display reconcile: every case holds" : "display reconcile: FAILED");
    return g_ok ? 0 : 1;
}
