// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The settings menu's HDR calibration (the DISPLAY page's HDR CALIBRATION row): three steps over the game, as the HDR
// Gaming Interest Group's guidelines set them, each a slider whose level the last write shows at once
// (post_process_settings.h HdrCalibration). Peak brightness: a star at the level in a box whose surround is at the top
// of the PQ range, raised until the star vanishes (the display clips both alike). Black level: a star at the level on
// true black, lowered until it vanishes. Each starts where its star is plainly seen. Paper white: the game's own
// picture, its white set to taste; opened at the title or the save select, a reference stage with Mario on it
// (calibration_scene.h), made ready under the first two steps.
// Save keeps the three in the settings (applied and written at once); Cancel, closing the menu or the picture leaving
// HDR keeps the settings as they were. native_settings.cpp calls in under its own lock.

#include "galaxy/render/post_process_settings.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render_api.h"

#include <cstdint>
#include <functional>
#include <string>

namespace galaxy::ui::hdr_calibration {

enum class Step : unsigned { Peak = 1u, Black = 2u, PaperWhite = 3u };
inline constexpr unsigned kStepCount = 3u;
// The view's status_number (nebula_game_api.h NEBULA_HDR_*): the step in bits 0-3, the slider's place in bits 4-15, its
// last place in bits 16-27, and bit 28 while the reference stage is not yet ready (the screen stays black).
inline constexpr unsigned kStepMask = 0xFu;
inline constexpr unsigned kPlaceShift = 4u;
inline constexpr unsigned kLastShift = 16u;
inline constexpr unsigned kFieldMask = 0xFFFu;
inline constexpr unsigned kSceneWaitBit = 1u << 28u;
[[nodiscard]] constexpr int status_number(Step step, unsigned place, unsigned last, bool scene_wait = false) noexcept {
    return static_cast<int>(static_cast<unsigned>(step) | (place & kFieldMask) << kPlaceShift | (last & kFieldMask) << kLastShift |
                            (scene_wait ? kSceneWaitBit : 0u));
}

// The levels found: the peak in nits, the black as a step of hdr_black_nits, the paper white in nits.
struct Levels {
    unsigned peak_nits = 1000u;
    unsigned black = 0u;
    unsigned paper_white_nits = render::kPaperWhiteDefault;
    friend bool operator==(const Levels&, const Levels&) = default;
};
// The levels the calibration starts from, each on its slider's steps: the peak at half the display's report (200 nits
// without one) and the black at about 0.5 nits in its slider's middle, both stars plainly seen; the settings' paper white.
[[nodiscard]] Levels starting_levels(const render::RenderConfig& settings, const render::DisplayHdr& display) noexcept;
// A level as the slider shows it ("1000 NITS", "0.0031 NITS").
[[nodiscard]] std::string level_text(Step step, const Levels& levels);

// Opens at the peak step from the settings' levels, the reference stage's visit started where it may
// (calibration_scene::start). `save` takes the levels found at Save.
void open(const render::RenderConfig& settings, const render::DisplayHdr& display, std::function<void(const Levels&)> save);
// It is up: the page under it waits.
[[nodiscard]] bool up() noexcept;
// A key while up(): Left and Right (the D-pad, a stick) move the slider a step, again while held; Enter, Space or A goes to
// the next step, or saves at the last; Backspace (X) goes back a step; Escape or F1 (B) cancels. True when taken.
bool key(std::uintptr_t key, bool was_down);
// The game's slider and answers: Change moves the slider `arg` steps, SetValue puts it at place `arg`; Apply, Revert
// and Close answer as Enter, Backspace and Escape. True when taken.
bool input(render::NativeSettingsInput input, int arg);
// The view's status while up(): false when nothing is up (or the picture left HDR, which cancels it).
bool fill(render::NativeSettingsView& view);
// The menu closed: cancelled.
void close() noexcept;

}  // namespace galaxy::ui::hdr_calibration
