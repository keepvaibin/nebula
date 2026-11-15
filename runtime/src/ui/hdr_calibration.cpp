// SPDX-License-Identifier: GPL-3.0-only
// hdr_calibration.h: the settings menu's HDR calibration.
#include "galaxy/ui/hdr_calibration.h"

#include "galaxy/calibration_scene.h"
#include "galaxy/native_settings_pad.h"
#include "galaxy/virtual_keys.h"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <utility>

namespace galaxy::ui::hdr_calibration {
namespace {

using render::HdrPattern;
using render::kHdrBlackSteps;
using render::kHdrPeakSteps;
using render::kPaperWhiteMax;
using render::kPaperWhiteMin;

// The paper white's slider: kPaperWhiteMin to kPaperWhiteMax, 10 nits apart (the DISPLAY page's PAPER WHITE row's steps).
inline constexpr unsigned kPaperWhiteSpacing = 10u;
inline constexpr unsigned kPaperWhiteSteps = (kPaperWhiteMax - kPaperWhiteMin) / kPaperWhiteSpacing;

struct State {
    bool up = false;
    Step step = Step::Peak;
    Levels levels;
    std::function<void(const Levels&)> save;
};
State s_state;

unsigned last_place(Step step) noexcept {
    switch (step) {
    case Step::Peak: return kHdrPeakSteps;
    case Step::Black: return kHdrBlackSteps;
    case Step::PaperWhite: return kPaperWhiteSteps;
    }
    return 0u;
}

unsigned place_of(Step step, const Levels& levels) noexcept {
    switch (step) {
    case Step::Peak: return render::hdr_peak_step(levels.peak_nits);
    case Step::Black: return std::min(levels.black, kHdrBlackSteps);
    case Step::PaperWhite:
        return (std::clamp(levels.paper_white_nits, kPaperWhiteMin, kPaperWhiteMax) - kPaperWhiteMin + kPaperWhiteSpacing / 2u) /
               kPaperWhiteSpacing;
    }
    return 0u;
}

void set_place(Step step, Levels& levels, unsigned place) noexcept {
    place = std::min(place, last_place(step));
    switch (step) {
    case Step::Peak: levels.peak_nits = render::hdr_peak_at(place); break;
    case Step::Black: levels.black = place; break;
    case Step::PaperWhite: levels.paper_white_nits = kPaperWhiteMin + place * kPaperWhiteSpacing; break;
    }
}

// The last write's levels and pattern for the step on screen.
void show() noexcept {
    render::HdrCalibration live;
    live.active = s_state.up;
    live.pattern = !s_state.up                      ? HdrPattern::None
                   : s_state.step == Step::Peak  ? HdrPattern::Peak
                   : s_state.step == Step::Black ? HdrPattern::Black
                                                 : HdrPattern::None;
    live.peak_nits = s_state.levels.peak_nits;
    live.black = s_state.levels.black;
    live.paper_white_nits = s_state.levels.paper_white_nits;
    render::set_hdr_calibration(s_state.up ? live : render::HdrCalibration{});
}

void move(int steps) {
    const unsigned place = place_of(s_state.step, s_state.levels);
    const int moved = std::clamp(static_cast<int>(place) + steps, 0, static_cast<int>(last_place(s_state.step)));
    set_place(s_state.step, s_state.levels, static_cast<unsigned>(moved));
    show();
}

void end(bool saved) {
    if (!s_state.up) {
        return;
    }
    const Levels found = s_state.levels;
    std::function<void(const Levels&)> save = std::move(s_state.save);
    s_state = State{};
    show();
    calibration_scene::stop();
    if (saved && save) {
        save(found);
    }
    std::cerr << "[settings] HDR CALIBRATION: " << (saved ? "SAVED" : "CANCELLED");
    if (saved) {
        std::cerr << " peak " << found.peak_nits << " nits, black " << render::hdr_black_nits(found.black) << " nits, paper white "
                  << found.paper_white_nits << " nits";
    }
    std::cerr << '\n';
}

void next() {
    if (s_state.step == Step::PaperWhite) {
        end(true);
        return;
    }
    s_state.step = static_cast<Step>(static_cast<unsigned>(s_state.step) + 1u);
    show();
}

void back() {
    if (s_state.step != Step::Peak) {
        s_state.step = static_cast<Step>(static_cast<unsigned>(s_state.step) - 1u);
        show();
    }
}

}  // namespace

Levels starting_levels(const render::RenderConfig& settings, const render::DisplayHdr& display) noexcept {
    Levels levels;
    // A test that starts with its star already gone leaves the player nothing to look for (HGIG: start where the symbol
    // shows, move until it just vanishes).
    const unsigned peak = display.peak_nits != 0u ? display.peak_nits / 2u : 200u;
    levels.peak_nits = render::hdr_peak_at(render::hdr_peak_step(peak));
    levels.black = render::kHdrBlackStart;
    set_place(Step::PaperWhite, levels, place_of(Step::PaperWhite, Levels{0u, 0u, settings.paper_white}));
    return levels;
}

std::string level_text(Step step, const Levels& levels) {
    char text[32];
    switch (step) {
    case Step::Peak: std::snprintf(text, sizeof(text), "%u NITS", levels.peak_nits); break;
    case Step::Black:
        if (levels.black == 0u) {
            std::snprintf(text, sizeof(text), "0 NITS");
        } else {
            std::snprintf(text, sizeof(text), "%.4f NITS", static_cast<double>(render::hdr_black_nits(levels.black)));
        }
        break;
    case Step::PaperWhite: std::snprintf(text, sizeof(text), "%u NITS", levels.paper_white_nits); break;
    }
    return text;
}

void open(const render::RenderConfig& settings, const render::DisplayHdr& display, std::function<void(const Levels&)> save) {
    s_state = State{};
    s_state.up = true;
    s_state.levels = starting_levels(settings, display);
    s_state.save = std::move(save);
    show();
    std::cerr << "[settings] HDR CALIBRATION: OPEN peak " << s_state.levels.peak_nits << " nits\n";
    (void)calibration_scene::start();
}

bool up() noexcept {
    return s_state.up;
}

bool key(std::uintptr_t key, bool was_down) {
    if (!s_state.up) {
        return false;
    }
    namespace pad = galaxy::input::settings_pad;
    switch (key) {
    case VK_LEFT: move(-1); break;
    case VK_RIGHT: move(1); break;
    case VK_RETURN:
    case VK_SPACE:
    case pad::kKeyAccept:
        if (!was_down) next();
        break;
    case VK_BACK:
        if (!was_down) back();
        break;
    case VK_ESCAPE:
    case VK_F1:
        if (!was_down) end(false);
        break;
    default:
        break;
    }
    // Every key waits while it is up (the page's keys must not move its rows meanwhile).
    return true;
}

bool input(render::NativeSettingsInput input, int arg) {
    if (!s_state.up) {
        return false;
    }
    switch (input) {
    case render::NativeSettingsInput::Change: move(arg); return true;
    case render::NativeSettingsInput::SetValue:
        set_place(s_state.step, s_state.levels, static_cast<unsigned>(std::max(arg, 0)));
        show();
        return true;
    case render::NativeSettingsInput::Apply: next(); return true;
    case render::NativeSettingsInput::Revert: back(); return true;
    case render::NativeSettingsInput::Close: end(false); return true;
    default: return false;
    }
}

bool fill(render::NativeSettingsView& view) {
    if (!s_state.up) {
        return false;
    }
    // The picture left HDR (frame generation turned on, the display left HDR mode): nothing to calibrate.
    if (!render::get_display_hdr().active) {
        end(false);
        return false;
    }
    view.status = render::NativeSettingsStatus::HdrCalibration;
    view.status_number = status_number(s_state.step, place_of(s_state.step, s_state.levels), last_place(s_state.step),
                                       calibration_scene::waiting());
    view.status_text = level_text(s_state.step, s_state.levels);
    return true;
}

void close() noexcept {
    end(false);
}

}  // namespace galaxy::ui::hdr_calibration
