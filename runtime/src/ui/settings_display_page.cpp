// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/ui/settings_display_page.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <utility>

namespace galaxy::ui {
using namespace galaxy::render;
std::vector<SettingsRow> display_page(const RenderConfig& render, const DisplayPageHost& host) {
    std::vector<SettingsRow> rows;
    char text[96]{};
    const DisplayPageState state = host.query(render);
    rows.push_back({"SCREEN", {}, nullptr, {}, true});
    if (!state.fullscreen_only) {
        std::string value = "PRIMARY";
        if (state.current_monitor && *state.current_monitor < state.monitors.size()) {
            const std::size_t index = *state.current_monitor;
            const auto& monitor = state.monitors[index];
            const std::string name = monitor.name.empty() ? std::string("Display") : mods_ui_text(monitor.name, 22u);
            std::snprintf(text, sizeof(text), "%u %s", static_cast<unsigned>(index + 1u), name.c_str());
            value = text;
        }
        SettingsRow row{"MONITOR", value, nullptr, "THE DISPLAY THE GAME RUNS ON"};
        if (state.monitors.size() >= 2u) {
            row.change = [&host](RenderConfig& draft, int delta) {
                const auto live = host.query(draft);
                if (live.monitors.empty()) return;
                const std::size_t index = std::min(live.current_monitor.value_or(0u), live.monitors.size() - 1u);
                const std::size_t next = settings_step(static_cast<unsigned>(index), static_cast<unsigned>(live.monitors.size()), delta);
                if (next == index) return;
                const std::string& device = live.monitors[next].id;
                draft.monitor.fill('\0');
                std::memcpy(draft.monitor.data(), device.data(), std::min(device.size(), draft.monitor.size() - 1u));
                host.changed(DisplayChange::Monitor, draft);
            };
        }
        rows.push_back(std::move(row));
    }
    if (!state.fullscreen_only) {
        static constexpr const char* kModes[] = {"WINDOWED", "BORDERLESS FULLSCREEN"};
        rows.push_back({"DISPLAY MODE", kModes[std::min(render.display_mode, kDisplayBorderless)],
                        [&host](RenderConfig& draft, int delta) {
                            draft.display_mode = settings_step(std::min(draft.display_mode, kDisplayBorderless), 2u, delta);
                            host.changed(DisplayChange::Mode, draft);
                        },
                        "BORDERLESS FILLS THE SCREEN AND SWITCHES INSTANTLY"});
    }
    if (!state.fullscreen_only) {
        const auto [w, h] = state.output_size;
        std::snprintf(text, sizeof(text), "%uX%u", w, h);
        SettingsRow row{"RESOLUTION", text, nullptr, "BORDERLESS USES THE DESKTOP RESOLUTION"};
        if (render.display_mode == kDisplayWindowed) {
            row.help = "THE WINDOW SIZE";
            row.change = [&host](RenderConfig& draft, int delta) {
                const auto sizes = host.query(draft).window_sizes;  // smallest first
                if (sizes.empty()) return;
                std::size_t index = 0u;
                for (std::size_t i = 0; i < sizes.size(); ++i) {
                    if (std::uint64_t{sizes[i].first} * sizes[i].second <=
                        std::uint64_t{draft.window_width} * draft.window_height) {
                        index = i;
                    }
                }
                const int moved = static_cast<int>(index) + delta;
                if (moved < 0 || moved >= static_cast<int>(sizes.size())) return;
                draft.window_width = sizes[static_cast<std::size_t>(moved)].first;
                draft.window_height = sizes[static_cast<std::size_t>(moved)].second;
                host.changed(DisplayChange::Resolution, draft);
            };
        }
        rows.push_back(std::move(row));
    }
    {
        const unsigned hz = state.frame_rates.refresh_hz;
        // A rate the system did not report is said so, not guessed.
        if (hz != 0u) {
            std::snprintf(text, sizeof(text), "%u HZ", hz);
        } else {
            std::snprintf(text, sizeof(text), "UNKNOWN");
        }
        rows.push_back({"REFRESH RATE", text, nullptr, state.fullscreen_only ? "FOLLOWS THE PHONE DISPLAY" : "SET IN THE SYSTEM DISPLAY SETTINGS"});
    }
    {
        static constexpr const char* kAspects[] = {"AUTO", "4:3", "3:2", "16:10", "16:9", "21:9", "32:9"};
        static_assert(std::size(kAspects) == kAspectModeCount);
        rows.push_back({"ASPECT RATIO", kAspects[std::min(render.aspect_mode, kAspectModeCount - 1u)],
                        [&host](RenderConfig& draft, int delta) {
                            draft.aspect_mode = settings_step(draft.aspect_mode, kAspectModeCount, delta);
                            host.changed(DisplayChange::Aspect, draft);
                        },
                        "AUTO MATCHES THE WINDOW. WIDER SHOWS MORE OF THE WORLD"});
    }
    rows.push_back({"VSYNC", render.vsync_interval == 0u ? "OFF" : "ON",
                    [&host](RenderConfig& draft, int) {
                        draft.vsync_interval = draft.vsync_interval == 0u ? 1u : 0u;
                        normalize_interpolation(draft, host.query(draft).frame_rates);
                    },
                    "WAITS FOR THE DISPLAY: NO TEARING, A LITTLE MORE LATENCY"});
    // Only while the display reports HDR mode: the picture is sent as HDR10, white at the paper white, highlights up to the
    // display's peak.
    if (state.hdr_offered) {
        rows.push_back({"HDR", render.hdr ? "ON" : "OFF",
                        [](RenderConfig& draft, int) { draft.hdr = !draft.hdr; },
                        render.hdr && !state.hdr_active ? "WAITING: FRAME GENERATION KEEPS THE PICTURE SDR"
                                                        : "WIDE BRIGHTNESS RANGE ON AN HDR DISPLAY"});
        if (render.hdr) {
            std::snprintf(text, sizeof(text), "%u NITS", std::clamp(render.paper_white, 80u, 500u));
            rows.push_back({"PAPER WHITE", text,
                            [](RenderConfig& draft, int delta) {
                                draft.paper_white = static_cast<unsigned>(std::clamp(static_cast<int>(draft.paper_white) + delta * 10, 80, 500));
                            },
                            "BRIGHTNESS OF THE PICTURE'S WHITE. RAISE IT IN A BRIGHT ROOM"});
        }
    }
    rows.push_back({"INTERFACE", {}, nullptr, {}, true});
    // A temporal upscaler always draws the HUD at the display's. With an exact internal resolution INTERNAL is
    // hud_internal_resolution (the scene not enlarged before the HUD: one enlarge at present) and DISPLAY its default.
    const bool fixed = state.temporal_upscaling;
    const bool exact = render_internal_height(render) != 0u;
    const bool display = render.hud_output_resolution || fixed || (exact && !render.hud_internal_resolution);
    std::function<void(RenderConfig&, int)> choose;
    if (exact) {
        choose = [](RenderConfig& draft, int) {
            const bool internal = !draft.hud_output_resolution && draft.hud_internal_resolution;
            draft.hud_internal_resolution = !internal;
            draft.hud_output_resolution = false;
        };
    } else if (!fixed) {
        choose = [](RenderConfig& draft, int) { draft.hud_output_resolution = !draft.hud_output_resolution; };
    }
    rows.push_back({"HUD RESOLUTION", display ? "DISPLAY" : "INTERNAL", choose,
                    exact ? "INTERNAL IS FASTER WITH SOFTER TEXT. DISPLAY DRAWS MENUS AND TEXT SHARP AT THE SCREEN RESOLUTION"
                          : "DISPLAY DRAWS MENUS AND TEXT SHARP AT THE SCREEN RESOLUTION"});
    return rows;
}


}  // namespace galaxy::ui
