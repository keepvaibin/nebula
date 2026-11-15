// SPDX-License-Identifier: GPL-3.0-only
// hdr_calibration.h: the steps, the slider, the patterns the last write takes, Save and Cancel.
#include "galaxy/ui/hdr_calibration.h"

#include "galaxy/native_settings_pad.h"
#include "galaxy/virtual_keys.h"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {
namespace cal = galaxy::ui::hdr_calibration;
using galaxy::render::HdrPattern;

void check(bool value, const char* text) {
    if (!value) throw std::runtime_error(text);
}

unsigned step_of(const galaxy::render::NativeSettingsView& view) {
    return static_cast<unsigned>(view.status_number) & cal::kStepMask;
}
unsigned place_of(const galaxy::render::NativeSettingsView& view) {
    return static_cast<unsigned>(view.status_number) >> cal::kPlaceShift & cal::kFieldMask;
}
unsigned last_of(const galaxy::render::NativeSettingsView& view) {
    return static_cast<unsigned>(view.status_number) >> cal::kLastShift & cal::kFieldMask;
}
}  // namespace

int main() {
    try {
        galaxy::render::RenderConfig settings;
        galaxy::render::DisplayHdr display;
        display.available = true;
        display.active = true;
        display.peak_nits = 640u;
        galaxy::render::set_display_hdr(display);

        // Where it starts: both stars plainly seen (half the display's report, 200 nits without one; the black at its
        // slider's top), whatever the settings hold; the settings' paper white.
        check(cal::starting_levels(settings, display).peak_nits == 325u, "half the display's peak, on the slider");
        check(cal::starting_levels(settings, galaxy::render::DisplayHdr{}).peak_nits == 200u, "200 nits without a report");
        settings.hdr_peak = 1500u;
        settings.hdr_black = 12u;
        settings.paper_white = 237u;
        const cal::Levels start = cal::starting_levels(settings, display);
        check(start.peak_nits == 325u && start.black == galaxy::render::kHdrBlackStart && start.paper_white_nits == 240u,
              "the stars seen, the settings' paper white");
        check(cal::level_text(cal::Step::Peak, start) == "325 NITS", "the peak's text");
        check(cal::level_text(cal::Step::Black, cal::Levels{1000u, 0u, 200u}) == "0 NITS", "true black's text");
        check(cal::level_text(cal::Step::Black, cal::Levels{1000u, 127u, 200u}) == "2.0000 NITS", "the black's text at its slider's right end");
        check(cal::status_number(cal::Step::Black, 5u, 127u) == (2 | 5 << 4 | 127 << 16), "the status number's fields");
        check(cal::status_number(cal::Step::PaperWhite, 5u, 127u, true) == (3 | 5 << 4 | 127 << 16 | 1 << 28),
              "the reference stage's wait bit");

        // Nothing up: nothing taken, nothing drawn.
        galaxy::render::NativeSettingsView view;
        check(!cal::up() && !cal::fill(view) && !cal::key(VK_RIGHT, false), "closed");
        check(!galaxy::render::hdr_calibration().active, "no pattern while closed");

        std::optional<cal::Levels> saved;
        cal::open(settings, display, [&saved](const cal::Levels& levels) { saved = levels; });
        check(cal::up() && cal::fill(view) && view.status == galaxy::render::NativeSettingsStatus::HdrCalibration, "open");
        check(step_of(view) == 1u && last_of(view) == galaxy::render::kHdrPeakSteps &&
                  place_of(view) == galaxy::render::hdr_peak_step(325u) && view.status_text == "325 NITS",
              "the peak step at half the display's peak");
        check(galaxy::render::hdr_calibration().active && galaxy::render::hdr_calibration().pattern == HdrPattern::Peak &&
                  galaxy::render::hdr_calibration().peak_nits == 325u,
              "the peak pattern at the level");
        // Right, Left and a held key step; the slider stops at its ends.
        check(cal::key(VK_RIGHT, false) && cal::key(VK_RIGHT, true), "right taken, held too");
        cal::fill(view);
        check(place_of(view) == galaxy::render::hdr_peak_step(325u) + 2u, "two steps up");
        check(galaxy::render::hdr_calibration().peak_nits == galaxy::render::hdr_peak_at(place_of(view)), "the pattern follows");
        check(cal::input(galaxy::render::NativeSettingsInput::SetValue, 100000), "set value taken");
        cal::fill(view);
        check(place_of(view) == galaxy::render::kHdrPeakSteps && galaxy::render::hdr_calibration().peak_nits == 10000u,
              "the top of the slider");
        check(cal::input(galaxy::render::NativeSettingsInput::Change, -1000), "change taken");
        cal::fill(view);
        check(place_of(view) == 0u && galaxy::render::hdr_calibration().peak_nits == galaxy::render::kHdrPeakMin, "its foot");
        cal::input(galaxy::render::NativeSettingsInput::SetValue, static_cast<int>(galaxy::render::hdr_peak_step(800u)));
        // Every other key waits while it is up.
        check(cal::key(VK_UP, false) && cal::key(VK_TAB, false), "the page's keys wait");
        // A held Enter does not go on; a press does (A as well).
        check(cal::key(VK_RETURN, true), "held enter taken");
        cal::fill(view);
        check(step_of(view) == 1u, "a held key does not go on");
        cal::key(galaxy::input::settings_pad::kKeyAccept, false);
        cal::fill(view);
        check(step_of(view) == 2u && last_of(view) == galaxy::render::kHdrBlackSteps &&
                  place_of(view) == galaxy::render::kHdrBlackStart && view.status_text == "0.5079 NITS",
              "the black step in its slider's middle, 0 nits at its left end");
        check(galaxy::render::hdr_calibration().pattern == HdrPattern::Black && galaxy::render::hdr_calibration().peak_nits == 800u,
              "the black pattern, the peak found kept");
        cal::input(galaxy::render::NativeSettingsInput::SetValue, 12);
        cal::key(VK_LEFT, false);
        cal::fill(view);
        check(place_of(view) == 11u && galaxy::render::hdr_calibration().black == 11u, "the black steps");
        // Back and on again keeps what was found.
        cal::key(VK_BACK, false);
        cal::fill(view);
        check(step_of(view) == 1u && view.status_text == "800 NITS", "back to the peak");
        cal::input(galaxy::render::NativeSettingsInput::Apply, 0);
        cal::input(galaxy::render::NativeSettingsInput::Apply, 0);
        cal::fill(view);
        check(step_of(view) == 3u && last_of(view) == 42u && view.status_text == "240 NITS", "the paper white step");
        check(galaxy::render::hdr_calibration().pattern == HdrPattern::None && galaxy::render::hdr_calibration().active,
              "the game itself at the paper white step");
        cal::key(VK_RIGHT, false);
        check(!saved, "nothing saved before Save");
        cal::key(VK_SPACE, false);
        check(!cal::up() && saved && saved->peak_nits == 800u && saved->black == 11u && saved->paper_white_nits == 250u,
              "save hands over the levels found");
        check(!galaxy::render::hdr_calibration().active, "the settings' levels again after save");

        // Cancel keeps nothing: Escape, closing the menu, the picture leaving HDR.
        saved.reset();
        cal::open(settings, display, [&saved](const cal::Levels& levels) { saved = levels; });
        cal::key(VK_RIGHT, false);
        cal::key(VK_ESCAPE, false);
        check(!cal::up() && !saved && !galaxy::render::hdr_calibration().active, "escape cancels");
        cal::open(settings, display, [&saved](const cal::Levels& levels) { saved = levels; });
        cal::close();
        check(!cal::up() && !saved && !galaxy::render::hdr_calibration().active, "the menu closed cancels");
        cal::open(settings, display, [&saved](const cal::Levels& levels) { saved = levels; });
        display.active = false;
        galaxy::render::set_display_hdr(display);
        check(!cal::fill(view) && !cal::up() && !saved && !galaxy::render::hdr_calibration().active,
              "the picture leaving HDR cancels");
        std::cout << "HDR calibration: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "HDR calibration: " << error.what() << '\n';
        return 1;
    }
}
