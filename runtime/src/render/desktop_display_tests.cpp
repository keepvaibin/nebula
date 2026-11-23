// SPDX-License-Identifier: GPL-3.0-only
// The desktop SDL host's displays, and the choice and placement of the game's window (galaxy/render/desktop_display.h), with
// made-up displays: a 2560 x 1440 display at 240 Hz at a scale of 150 % (the owner's Linux PC), two displays of one model, a
// display that went away.
#include "galaxy/render/desktop_display.h"

#include <iostream>
#include <stdexcept>

namespace {
using namespace galaxy::render;
using namespace galaxy::render::desktop_display;

void check(bool value, const char* text) {
    if (!value) throw std::runtime_error(text);
}

// What SDL reports on Wayland for a 2560 x 1440 display at 150 %: the desktop mode in points (1707 x 960) and the density
// that turns them into pixels.
Display owners_display() {
    Display display;
    display.name = "DP-1";
    display.primary = true;
    const std::vector<RawMode> raw{{1707, 960, 1.49971f, 239.97f}, {2560, 1440, 1.0f, 239.97f}, {2560, 1440, 1.0f, 60.0f},
                                   {1920, 1080, 1.0f, 239.97f}, {1280, 720, 1.0f, 60.0f}, {320, 240, 1.0f, 60.0f}};
    display.width = pixels_of(1707, 1.49971f);
    display.height = pixels_of(960, 1.49971f);
    display.refresh_hz = 239.97;
    display.usable_width = display.width;
    display.usable_height = display.height;
    display.modes = modes_of(raw, {display.width, display.height, 240u});
    return display;
}

Display plain(const char* name, int x, bool primary, unsigned width, unsigned height) {
    Display display;
    display.name = name;
    display.x = x;
    display.primary = primary;
    display.width = width;
    display.height = height;
    display.refresh_hz = 60.0;
    display.usable_width = width;
    display.usable_height = height - 40;
    display.modes = {{width, height, 60u}};
    return display;
}

void the_owners_display() {
    const Display display = owners_display();
    check(display.width == 2560u && display.height == 1440u, "1707 x 960 points at 1.4997 are 2560 x 1440 pixels");
    check(whole_hz(display.refresh_hz) == 240u, "239.97 Hz is 240 for MATCH REFRESH");
    check(display.modes.size() == 4u, "the 1707 x 960 mode is the 2560 x 1440 one, the 320 x 240 mode is left out");
    check(display.modes.front() == DisplayModeInfo{2560u, 1440u, 240u} && display.modes[1] == DisplayModeInfo{2560u, 1440u, 60u},
          "the largest first, the faster of one size first");
    const auto sizes = distinct_resolutions(display.modes);
    check(sizes.size() == 3u && sizes[0] == std::pair<unsigned, unsigned>{2560u, 1440u} &&
              sizes[1] == std::pair<unsigned, unsigned>{1920u, 1080u} && sizes[2] == std::pair<unsigned, unsigned>{1280u, 720u},
          "the resolutions: each size once, largest first");
    // The first start: the default configuration is borderless on the primary display.
    const std::vector<Display> displays{display};
    const auto ids = display_ids(displays);
    const Choice start = choice_of(RenderConfig{}, false);
    check(start.mode == kDisplayBorderless, "the default setting is borderless full screen");
    const auto placed = place_window(start, ids, displays, 0, 0);
    check(placed && placed->fullscreen && placed->display == 0u && placed->client_width == 2560u && placed->client_height == 1440u,
          "borderless on his display: 2560 x 1440 pixels, not the 854 x 480 window at 150 % (1281 x 720)");
}

void sizes_and_rates() {
    check(pixels_of(854, 1.5f) == 1281u, "854 points at 1.5 are 1281 pixels: the size the owner saw");
    check(window_points(854u, 1.5) == 569u && pixels_of(static_cast<int>(window_points(854u, 1.5)), 1.5f) == 854u,
          "a window of 854 pixels at 1.5 is 569 points, which is 854 pixels again");
    check(window_points(854u, 0.0) == 854u && window_points(1u, 4.0) == 1u,
          "no density is a density of 1; a window is never 0 points");
    check(whole_hz(59.94) == 60u && whole_hz(0.0) == 0u && whole_hz(0.4) == 0u, "whole hertz; none when not known");
    check(modes_of({}, {1024u, 768u, 60u}) == std::vector<DisplayModeInfo>{{1024u, 768u, 60u}}, "no mode left: the current one");
    check(modes_of({{800, 600, 1.0f, 0.0f}}, {1u, 1u, 1u}).front().refresh_hz == 1u,
          "a mode with no rate has the rate 1, as Windows lists it");
}

void the_list() {
    std::vector<Display> displays{plain("Right", 2560, false, 1920, 1080), plain("Main", 0, true, 2560, 1440),
                                  plain("Left", -1920, false, 1920, 1080)};
    order_displays(displays);
    check(displays[0].name == "Main" && displays[1].name == "Left" && displays[2].name == "Right",
          "the primary display first, then left to right");
    std::vector<Display> twins{plain("DELL U2723QE", 0, true, 2560, 1440), plain("DELL U2723QE", 2560, false, 2560, 1440),
                               plain("DELL U2723QE", 5120, false, 2560, 1440), plain("", 7680, false, 1920, 1080)};
    const auto ids = display_ids(twins);
    check(ids[0] == "DELL U2723QE" && ids[1] == "DELL U2723QE 2" && ids[2] == "DELL U2723QE 3" && ids[3] == "Display",
          "two of one model stay two choices; a display with no name is Display");
    std::vector<Display> long_names{plain("x", 0, true, 800, 600)};
    long_names[0].name = std::string(70, 'x');
    check(display_ids(long_names)[0].size() <= kIdBytes - 3u, "an id fits RenderConfig::monitor with room for a number");
    long_names[0].name.clear();
    for (int i = 0; i < 40; ++i) long_names[0].name += "\xC3\xBC";  // 80 bytes: u with a diaeresis, two bytes each
    const std::string cut = display_ids(long_names)[0];
    check(cut.size() <= kIdBytes && cut.size() % 2u == 0u, "a long name is cut between characters, never inside one");
}

void choosing() {
    std::vector<Display> displays{plain("Main", 0, true, 2560, 1440), plain("Side", 2560, false, 1920, 1080)};
    const auto ids = display_ids(displays);
    check(find_display(ids, displays, "Side") == std::optional<std::size_t>{1u}, "a saved id names its display");
    check(find_display(ids, displays, "sIDE") == std::optional<std::size_t>{1u}, "the id matches without regard to case");
    check(find_display(ids, displays, "Gone") == std::optional<std::size_t>{0u}, "a display that went away: the primary");
    check(find_display(ids, displays, "") == std::optional<std::size_t>{0u}, "no saved id: the primary");
    displays[0].primary = false;
    check(find_display(ids, displays, "Gone") == std::optional<std::size_t>{0u}, "no primary: the first");
    check(!find_display({}, {}, "Main"), "no displays: none");
}

void the_choice() {
    RenderConfig config;
    config.display_mode = 2u;
    config.window_width = 1600u;
    config.window_height = 900u;
    config.monitor.fill('\0');
    config.monitor[0] = 'S';
    config.monitor[1] = 'i';
    check(choice_of(config, false).mode == kDisplayBorderless && choice_of(config, false).monitor == "Si",
          "an exclusive mode saved by another build opens borderless; the id is the text up to its end mark");
    check(choice_of(config, true).mode == kDisplayWindowed, "a hidden test window stays windowed");
    config.monitor.fill('x');
    check(choice_of(config, false).monitor.size() == config.monitor.size(), "an id with no end mark is all of the array");
}

void placing_again() {
    const Choice windowed{kDisplayWindowed, "Main", 1280u, 720u};
    Choice full = windowed;
    full.mode = kDisplayBorderless;
    check(must_place(std::nullopt, windowed, false), "not before it has been placed");
    check(!must_place(windowed, windowed, false), "no change, nothing to do");
    check(must_place(windowed, windowed, true), "a display added, removed or changed");
    check(must_place(windowed, full, false) && must_place(full, windowed, false), "the mode changed");
    Choice other = windowed;
    other.monitor = "Side";
    check(must_place(windowed, other, false), "the display changed");
    Choice larger = windowed;
    larger.window_width = 1920u;
    check(must_place(windowed, larger, false), "windowed: the size changed");
    Choice full_larger = full;
    full_larger.window_width = 1920u;
    check(!must_place(full, full_larger, false), "full screen: a windowed size that changed is not a reason");
}

void placing() {
    std::vector<Display> displays{plain("Main", 0, true, 2560, 1440), plain("Side", 2560, false, 1920, 1080)};
    const auto ids = display_ids(displays);
    auto placed = place_window({kDisplayBorderless, "Side", 854u, 480u}, ids, displays, 0, 0);
    check(placed && placed->fullscreen && placed->display == 1u && placed->client_width == 1920u && placed->client_height == 1080u,
          "borderless on the named display, at its own pixels");
    placed = place_window({kDisplayWindowed, "Side", 854u, 480u}, ids, displays, 0, 0);
    check(placed && !placed->fullscreen && placed->display == 1u && placed->client_width == 854u && placed->client_height == 480u,
          "windowed: the configured size on the named display");
    placed = place_window({kDisplayWindowed, "Side", 3840u, 2160u}, ids, displays, 16, 31);
    check(placed && placed->client_width == 1904u && placed->client_height == 1009u,
          "windowed: limited to the usable area less the frame (1920 - 16, 1080 - 40 - 31)");
    placed = place_window({kDisplayWindowed, "Main", 100u, 100u}, ids, displays, 0, 0);
    check(placed && placed->client_width == 320u && placed->client_height == 240u, "windowed: never below 320 x 240");
    check(!place_window({kDisplayBorderless, "Main", 1u, 1u}, {}, {}, 0, 0), "no displays: nowhere to place");
    const auto limit = max_client_size(2560, 1400, 16, 39);
    check(limit.first == 2544 && limit.second == 1361 && max_client_size(100, 100, 300, 300).first == 0,
          "the largest client size is the usable area less the frame, never below 0");
}

void full_screen_pixels() {
    const Pixels display{2560u, 1440u};
    check(fullscreen_pixels(display, {2561u, 1440u}, 1.5) == display,
          "150 %: 1707 points make 2561 pixels, the picture is the display's 2560");
    check(fullscreen_pixels(display, {2559u, 1439u}, 1.5) == display, "a pixel short is the display's too");
    check(fullscreen_pixels(display, {2560u, 1440u}, 1.0) == display, "an exact size stays");
    check(fullscreen_pixels(display, {1281u, 720u}, 1.5) == Pixels{1281u, 720u},
          "a window that is not full screen yet (the 854 x 480 window at 150 %) keeps the size the system reports");
    check(fullscreen_pixels(display, {1920u, 1080u}, 1.0) == Pixels{1920u, 1080u}, "another mode keeps its size");
    check(fullscreen_pixels(display, {2564u, 1440u}, 1.5) == Pixels{2564u, 1440u}, "four pixels off is not rounding");
    check(fullscreen_pixels(display, {2563u, 1440u}, 3.0) == display, "at a scale of 3 rounding may be 3 pixels");
    check(fullscreen_pixels({0u, 0u}, {1281u, 720u}, 1.5) == Pixels{1281u, 720u}, "a display of unknown size changes nothing");
}

void the_pointer() {
    check(os_cursor_hidden(true, false), "hidden while the game's window has the focus");
    check(!os_cursor_hidden(false, false), "shown when the window loses the focus (another program, a system dialog)");
    check(!os_cursor_hidden(true, true), "shown while the window is minimised");
    check(!os_cursor_hidden(false, true), "shown");
}

}  // namespace

int main() {
    try {
        the_owners_display();
        sizes_and_rates();
        the_list();
        choosing();
        the_choice();
        placing_again();
        placing();
        full_screen_pixels();
        the_pointer();
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    std::cout << "desktop display tests passed\n";
    return 0;
}
