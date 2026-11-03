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
    {
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
    {
        static constexpr const char* kModes[] = {"WINDOWED", "BORDERLESS FULLSCREEN"};
        rows.push_back({"DISPLAY MODE", kModes[std::min(render.display_mode, kDisplayBorderless)],
                        [&host](RenderConfig& draft, int delta) {
                            draft.display_mode = settings_step(std::min(draft.display_mode, kDisplayBorderless), 2u, delta);
                            host.changed(DisplayChange::Mode, draft);
                        },
                        "BORDERLESS FILLS THE SCREEN AND SWITCHES INSTANTLY"});
    }
    {
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
        std::snprintf(text, sizeof(text), "%u HZ", hz != 0u ? hz : 60u);
        rows.push_back({"REFRESH RATE", text, nullptr, "SET IN THE WINDOWS DISPLAY SETTINGS"});
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
    rows.push_back({"INTERFACE", {}, nullptr, {}, true});
    rows.push_back({"HUD RESOLUTION", render.hud_output_resolution ? "DISPLAY" : "INTERNAL",
                    [&host](RenderConfig& draft, int) { draft.hud_output_resolution = !draft.hud_output_resolution; },
                    "DISPLAY DRAWS MENUS AND TEXT SHARP AT THE SCREEN RESOLUTION"});
    return rows;
}


}  // namespace galaxy::ui
