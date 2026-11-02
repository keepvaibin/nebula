// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/input/keyboard_state.h"
#include <iostream>
#include <stdexcept>

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error Keyboard event policy must not include Windows.h
#endif

namespace {
using galaxy::input::KeyboardEventState;
void check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
void changed_layout_and_overlap() {
    KeyboardEventState state;
    state.event(0x11, 'W', 'W', true, false, false);
    auto value = state.sample();
    check(value.positions_available && value.all.key('W') && value.all.position('W'), "physical key starts");
    // The layout changed while W stayed down; another physical key now also publishes logical W.
    state.event(0x2c, 'W', 'Z', true, false, false);
    value = state.sample();
    check(value.all.position('W') && value.all.position('Z'), "layout change retains each physical position");
    state.event(0x11, 'Z', 'W', false, false, false);
    value = state.sample();
    check(value.all.key('W') && !value.all.position('W') && value.all.position('Z'), "release uses original mapping and preserves overlapping key");
    state.event(0x2c, 'W', 'Z', false, false, false);
    value = state.sample();
    check(!value.all.key('W') && !value.all.position('Z'), "last source releases logical key");
    std::cout << "layout-change hold/release: positions retained; overlapping logical keys release independently\n";
}
void pulses_focus_and_injection() {
    KeyboardEventState state;
    state.event(0x11, 'W', 'W', true, false, false);
    state.event(0x11, 'W', 'W', false, false, false);
    check(state.sample().all.position('W'), "short pulse retained until sample");
    check(!state.sample().all.position('W'), "pulse consumed once");
    state.event(0x11, 'W', 'W', true, false, true);
    state.event(0x11, 'W', 'W', true, false, false);
    auto value = state.sample();
    check(value.physical.position('W') && value.all.position('W'), "physical and injected sources coexist");
    state.event(0x11, 'W', 'W', false, false, false);
    value = state.sample();
    check(!value.physical.position('W') && value.all.position('W'), "injected hold never becomes physical input");
    state.event(0x11, 'W', 'W', true, true, false);
    state.event(0x11, 'W', 'W', false, false, false);
    check(state.sample().physical.position('W'), "another source's previous-level bit cannot erase a physical tap");
    check(!state.sample().physical.position('W'), "overlapping physical tap consumed once");
    state.clear(); value = state.sample();
    check(!value.all.position('W') && !value.all.key('W') && value.positions_available, "focus loss clears data but keeps capability");
    state.event(0x11, 'W', 'W', true, true, false);
    check(state.sample().all.position('W'), "repeat after focus can restore held level");
    state.event(0x11, 'W', 'W', false, false, false);
    check(!state.sample().all.position('W'), "repeat never leaves an extra pulse");
    std::cout << "focus/pulse/injection: released cleanly; physical filter preserves real keys\n";
}
}
int main() {
    try { changed_layout_and_overlap(); pulses_focus_and_injection(); return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
