// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "galaxy/game_step.h"
#include "galaxy/render/render_config.h"
#include "galaxy/runtime_settings.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::ui {
using render::RenderConfig;
// The game's steps a second, and the half rate a frame-rate limit also offers. A display rate of 0 is unknown.
inline constexpr unsigned kStepRate = static_cast<unsigned>(kGameStepsPerSecond);
inline constexpr unsigned kHalfStepRate = kStepRate / 2u;
struct SettingsRow {
    std::string label;
    std::string value;
    std::function<void(RenderConfig&, int)> change;
    std::string help;
    bool heading = false;
    std::string key;  // names the row across rebuilds (a mod's buttons)
    std::uint8_t names = 0u;  // words shown as they are: 1 its label, 2 its value (a mod's own), 4 its help (already in the played language)
    // A row of buttons (nothing to change): their words, and what pressing the one at an index does. Left and right
    // move between them; A, Space or a click presses one (Right too when there is only one).
    std::vector<std::string> buttons;
    std::function<void(RenderConfig&, unsigned)> press;
    std::uint8_t list = 0u;  // the MODS page's lists: 1 off, 2 on (a mod, or the list's title)
    bool held = false;       // a mod picked up to move
    // A setting along a range (a slider): its steps from the first value and where it is.
    std::uint16_t slider_steps = 0u;
    std::uint16_t slider_at = 0u;
};

SettingsRow button_row(std::string label, std::vector<std::string> buttons,
                       std::function<void(RenderConfig&, unsigned)> press, std::string help);
inline constexpr unsigned kNativeSettingsVisibleRows = 9u;
const char* game_language_name(GameLanguage language);
unsigned native_settings_first_row(unsigned first, unsigned selected, unsigned count);
float step_gain(float current, int delta);
bool display_settings_differ(const RenderConfig& a, const RenderConfig& b);
float quality_render_fraction(unsigned quality);
std::string percent_text(float value);
std::string mods_ui_text(std::string_view text, std::size_t width);
std::string upper_ascii(std::string text);
unsigned settings_step(unsigned value, unsigned count, int delta);
enum class Interpolation : unsigned { kOff, kNative, kFrameGeneration, kFsrGeneration };
struct FrameRateCaps {
    unsigned refresh_hz = 0;
    bool generated_offered = false;
    bool generated_supported = false;
    bool generated_dynamic = false;
    unsigned generated_max = 0;
    bool fsr_generation_offered = false;
};
Interpolation interpolation_of(const RenderConfig& render);
std::vector<unsigned> interpolation_targets(Interpolation mode, const FrameRateCaps& caps);
std::vector<Interpolation> interpolation_modes(const FrameRateCaps& caps);
void normalize_interpolation(RenderConfig& render, const FrameRateCaps& caps);
}  // namespace galaxy::ui
