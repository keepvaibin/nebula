// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/input/wiimote_motion.h"

#include <iostream>

namespace {
int failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::cerr << "FAILED: " << what << '\n';
        ++failures;
    }
}

galaxy::input::PlayerInput at_rest() {
    galaxy::input::PlayerInput input{};
    input.gravity = {0.0f, 0.0f, 1.0f};
    input.nunchuk_gravity = {0.0f, 0.0f, 1.0f};
    return input;
}
}  // namespace

int main() {
    using namespace galaxy::input::wiimote;
    // A remote lying still: no spin, the tilt at rest.
    Motion motion;
    MotionFrame frame;
    int spins = 0;
    for (int i = 0; i < 120; ++i) {
        motion.add(at_rest());
        (void)motion.read(frame);
        spins += frame.spin_presses + frame.sub_spin_presses;
    }
    expect(spins == 0, "a still remote does not spin");
    expect(frame.tilt[2] > 0.99f, "at rest, 1 g on z");

    // A flick: one spin on the main hand; the Nunchuk, which did not move, none.
    spins = 0;
    int sub_spins = 0;
    for (int i = 0; i < 25; ++i) {
        auto input = at_rest();
        input.gravity = {3.0f, 0.0f, 1.0f};
        motion.add(input);
        (void)motion.read(frame);
        spins += frame.spin_presses;
        sub_spins += frame.sub_spin_presses;
    }
    expect(spins == 1, "a flick is one spin");
    expect(sub_spins == 0, "only the hand that moved spins");

    // The sensors' reach holds the tilt.
    Motion limits;
    for (int i = 0; i < 200; ++i) {
        auto input = at_rest();
        input.gravity = {9.0f, 0.0f, 1.0f};
        limits.add(input);
    }
    (void)limits.read(frame);
    expect(frame.tilt[0] <= 3.4f, "the tilt stays inside what the sensor can read");

    // The pointer comes on after five good reports, and rides over a short loss.
    Motion pointer;
    bool on = false;
    int reports = 0;
    for (; reports < 20 && !on; ++reports) {
        auto input = at_rest();
        input.pointer_on = true;
        input.pointer_x = 0.5f;
        pointer.add(input);
        on = pointer.read(frame).pointer_on;
    }
    expect(reports == 6, "the pointer comes on after five reports");
    for (int i = 0; i < 3; ++i) {
        auto lost = at_rest();
        lost.pointer_on = false;
        pointer.add(lost);
    }
    auto back = at_rest();
    back.pointer_on = true;
    back.pointer_x = 0.25f;
    pointer.add(back);
    const auto kept = pointer.read(frame);
    expect(kept.pointer_on && kept.pointer_x == 0.25f, "a short loss keeps the pointer");

    if (failures != 0) {
        return 1;
    }
    std::cout << "wiimote motion checks passed\n";
    return 0;
}
