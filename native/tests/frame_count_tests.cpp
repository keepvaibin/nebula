// SPDX-License-Identifier: GPL-3.0-only
// MR::countUp (Game/Util/FrameCount.hpp, N-3011): a count of frames in a small unsigned integer stops at its largest
// value. Mario's 16-bit state timers (on the ground, in the air, hanging, jetting, in the dark) count with it, so after
// 65536 steps in one state they still read "long", not 0 as if the state had just begun.
#include "Game/Util/FrameCount.hpp"

#include <stdio.h>

namespace {
    int sFailures = 0;

    void expect(bool ok, const char* what) {
        if (!ok) {
            printf("FAILED: %s\n", what);
            sFailures++;
        }
    }
}  // namespace

int main() {
    // Mario standing for 18 minutes and more: his land timer keeps reading "landed long ago".
    u16 land = 0;
    for (u32 step = 0; step < 70000; step++) {
        MR::countUp(land);
    }
    expect(land == 0xFFFF, "70000 steps on the ground read the most, not 70000 - 65536");
    MR::countUp(land);
    expect(land == 0xFFFF, "and stay there");

    // A landing on a spring adds 0x10; near the top it stops there too.
    u16 near = 0xFFF8;
    MR::countUp(near, 0x10);
    expect(near == 0xFFFF, "an add past the top stops at the top");
    u16 low = 5;
    MR::countUp(low, 0x10);
    expect(low == 0x15, "an add below the top is a plain add");

    // The Luma's spin timer grows by 150 or 300 a spin while it counts down: mashing does not start it over.
    u16 spin = 0xFF00;
    MR::countUp(spin, 0x12c);
    expect(spin == 0xFFFF, "a spin near the top stops at the top");

    // One below the top, and other widths.
    u16 edge = 0xFFFE;
    MR::countUp(edge);
    expect(edge == 0xFFFF, "one below the top reaches it");
    u8 small = 250;
    MR::countUp(small, 10);
    expect(small == 0xFF, "an 8-bit count stops at 255");
    u32 wide = 0xFFFFFFF0u;
    MR::countUp(wide, 0x20);
    expect(wide == 0xFFFFFFFFu, "a 32-bit count stops at its top");
    u32 plain = 7;
    MR::countUp(plain);
    expect(plain == 8, "counting from 7 gives 8");

    // N-3022: Mario's oxygen recovers by 32 at the surface up to its most; with a most near the u16's top the sum
    // stops at the most and reports it went past, it does not wrap to near 0.
    u16 oxygen = 0xFFF0;
    expect(MR::countUpTo(oxygen, 32, 0xFFFF) && oxygen == 0xFFFF, "oxygen near the top recovers to the most");
    u16 coin = 60000;
    MR::countUpTo(coin, 0xFFFF / 8, 0xFFFF);
    expect(coin == 0xFFFF, "a coin's eighth of a most near the top recovers to the most");
    u16 game = 1700;
    expect(!MR::countUpTo(game, 32, 1800) && game == 1732, "under the most the add is plain and not past");
    u16 exact = 1768;
    expect(!MR::countUpTo(exact, 32, 1800) && exact == 1800, "a sum equal to the most is not past it");
    u16 over = 1790;
    expect(MR::countUpTo(over, 32, 1800) && over == 1800, "a sum past the most stops at it");

    // N-3024: the slide task's countdown: 0 frames slide one step and end, N frames slide N steps.
    u16 none = 0;
    expect(!MR::countDownLeft(none) && none == 0, "a slide of 0 frames ends after its step, it does not start over");
    u16 three = 3;
    u32 steps = 1;
    while (MR::countDownLeft(three) && steps < 100) {
        steps++;
    }
    expect(steps == 3 && three == 0, "a slide of 3 frames takes 3 steps");

    if (sFailures != 0) {
        return 1;
    }
    printf("frame_count_tests: all passed\n");
    return 0;
}
