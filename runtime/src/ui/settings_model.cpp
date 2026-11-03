// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/ui/settings_model.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace galaxy::ui {
// A row of buttons; its value is their words with '|' between them (NEBULA_SETTINGS_ROW_BUTTONS).
SettingsRow button_row(std::string label, std::vector<std::string> buttons, std::function<void(RenderConfig&, unsigned)> press,
                       std::string help) {
    SettingsRow row{std::move(label), {}, nullptr, std::move(help)};
    for (const std::string& word : buttons) {
        row.value += (row.value.empty() ? "" : "|") + word;
    }
    row.buttons = std::move(buttons);
    row.press = std::move(press);
    return row;
}
float quality_render_fraction(unsigned quality) {
    static constexpr float kRatio[] = {1.0f, 1.5f, 1.7f, 2.0f, 3.0f};
    return 1.0f / kRatio[std::min(quality, 4u)];
}

const char* game_language_name(GameLanguage language) {
    switch (language) {
    case GameLanguage::Japanese: return "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";
    case GameLanguage::German: return "DEUTSCH";
    case GameLanguage::French: return "FRAN\xC3\x87" "AIS";
    case GameLanguage::Spanish: return "ESPA\xC3\x91" "OL";
    case GameLanguage::Italian: return "ITALIANO";
    case GameLanguage::Dutch: return "NEDERLANDS";
    case GameLanguage::English:
    case GameLanguage::Auto: break;
    }
    return "ENGLISH";
}

// Rows the panel shows at once; longer pages scroll.
unsigned native_settings_first_row(unsigned first, unsigned selected, unsigned count) {
    if (count <= kNativeSettingsVisibleRows) return 0u;
    if (selected < first) first = selected;
    if (selected >= first + kNativeSettingsVisibleRows) first = selected + 1u - kNativeSettingsVisibleRows;
    return std::min(first, count - kNativeSettingsVisibleRows);
}

float step_gain(float current, int delta) {
    const float snapped = std::round(current * 20.0f) / 20.0f;
    return std::clamp(snapped + static_cast<float>(delta) * 0.05f, 0.0f, 2.0f);
}

bool display_settings_differ(const RenderConfig& a, const RenderConfig& b) {
    return a.display_mode != b.display_mode || a.monitor != b.monitor ||
        (a.display_mode == render::kDisplayWindowed &&
         (a.window_width != b.window_width || a.window_height != b.window_height));
}

Interpolation interpolation_of(const RenderConfig& render) {
    if (render.frame_generation != 0u) {
        return Interpolation::kFrameGeneration;
    }
    const auto rate = static_cast<unsigned>(std::lround(std::max(0.0f, render.max_fps)));
    return rate != 0u && rate <= 61u ? Interpolation::kOff : Interpolation::kNative;
}

// The targets a mode offers, lowest first (0: the display's refresh rate): the standard rates
// below the display's, then its own. With a temporal upscaler native interpolation runs at
// multiples of 60, as does frame generation without dynamic multi frame generation (and at most
// the multiplier the GPU runs).
std::vector<unsigned> interpolation_targets(Interpolation mode, const FrameRateCaps& caps) {
    std::vector<unsigned> targets;
    if (mode == Interpolation::kOff) {
        return targets;
    }
    const unsigned refresh = caps.refresh_hz;
    const bool generated = mode == Interpolation::kFrameGeneration;
    // NVIDIA's fixed frame generation makes whole multiples of the game's 60.
    const bool sixties = generated && caps.generated_supported && !caps.generated_dynamic;
    const unsigned most = generated && caps.generated_supported
        ? 60u * (caps.generated_max + 1u) : 1000u;
    for (const unsigned rate : {120u, 144u, 165u, 180u, 240u, 360u}) {
        if (refresh != 0u && rate + 1u >= refresh) {
            break;
        }
        if ((rate == 180u && !sixties) || (sixties && rate % 60u != 0u) || rate > most) {
            continue;
        }
        targets.push_back(rate);
    }
    // Its own rate: above 60, and with the multiples of 60 only where it is one (frame generation
    // takes the largest multiple at or below it).
    const bool own = refresh == 0u || (refresh > 61u && (!sixties || generated || refresh % 60u <= 1u ||
                                                          refresh % 60u >= 59u));
    if (own) {
        targets.push_back(0u);
    }
    return targets;
}

std::vector<Interpolation> interpolation_modes(const FrameRateCaps& caps) {
    std::vector<Interpolation> modes{Interpolation::kOff};
    if (!interpolation_targets(Interpolation::kNative, caps).empty()) {
        modes.push_back(Interpolation::kNative);
    }
    if (caps.generated_offered && !interpolation_targets(Interpolation::kFrameGeneration, caps).empty()) {
        modes.push_back(Interpolation::kFrameGeneration);
    }
    return modes;
}

// Keeps the draft's target one its mode offers: the same, else the highest at or below it (the
// display's rate counting as its refresh), else the lowest offered; a mode with none turns off.
void normalize_interpolation(RenderConfig& render, const FrameRateCaps& caps) {
    const Interpolation mode = interpolation_of(render);
    const auto rate = static_cast<unsigned>(std::lround(std::max(0.0f, render.max_fps)));
    if (mode == Interpolation::kOff) {
        render.max_fps = rate != 0u && rate <= 30u ? 30.0f : 60.0f;
        return;
    }
    const auto targets = interpolation_targets(mode, caps);
    if (targets.empty()) {
        render.frame_generation = 0u;
        render.max_fps = 60.0f;
        return;
    }
    if (std::find(targets.begin(), targets.end(), rate) != targets.end()) {
        return;
    }
    const unsigned refresh = caps.refresh_hz;
    const unsigned wanted = rate == 0u ? (refresh != 0u ? refresh : 1000u) : rate;
    unsigned chosen = targets.front();
    for (const unsigned target : targets) {
        const unsigned value = target == 0u ? (refresh != 0u ? refresh : 1000u) : target;
        if (value <= wanted) {
            chosen = target;
        }
    }
    render.max_fps = static_cast<float>(chosen);
}

std::string percent_text(float value) {
    char text[32]{};
    std::snprintf(
        text,
        sizeof(text),
        "%u%%",
        static_cast<unsigned>(std::lround(value * 100.0f)));
    return text;
}

// A name as the menu shows it, shortened to `width` characters (the overlay's font draws it in capitals).
std::string mods_ui_text(std::string_view text, std::size_t width) {
    std::size_t characters = 0u;
    std::size_t cut = text.size();
    for (std::size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0u) == 0x80u) continue;
        if (characters == width - 3u) cut = i;
        ++characters;
    }
    return characters <= width ? std::string(text) : std::string(text.substr(0, cut)) + "...";
}

std::string upper_ascii(std::string text) {
    for (char& ch : text) {
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    }
    return text;
}

// One step of a row's choice, clamped to its ends.
unsigned settings_step(unsigned value, unsigned count, int delta) {
    return static_cast<unsigned>(std::clamp(static_cast<int>(value) + delta, 0, static_cast<int>(count) - 1));
}


}  // namespace galaxy::ui
