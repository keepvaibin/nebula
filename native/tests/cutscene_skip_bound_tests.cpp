// SPDX-License-Identifier: GPL-3.0-only
// The cutscene skip's bound (Game/System/NebulaCutsceneSkipBound.hpp): a skip on a slow device, which fits few steps
// of play in each frame, runs as many steps as one on a fast device before the bound ends it; steps in which the
// cutscene's time sheet moves do not count toward it.
#include "Game/System/NebulaCutsceneSkipBound.hpp"

#include <stdio.h>

namespace {
    int sFailures;

    void expect(bool ok, const char* pWhat) {
        if (!ok) {
            printf("FAIL %s\n", pWhat);
            sFailures++;
        }
    }

    // Steps run until the bound ends the skip (or -1 after `most`), each `stepNs` of wall time after the last; the
    // sheet moves in the first `sheetSteps` of them.
    s32 stepsUntilEnd(s64 stepNs, s32 most, s32 sheetSteps = 0) {
        NebulaCutsceneSkipBound bound;
        s64 now = 1000000000;
        bound.begin(now);
        for (s32 i = 1; i <= most; i++) {
            now += stepNs;
            if (bound.step(now, i <= sheetSteps)) {
                return i;
            }
        }
        return -1;
    }
}  // namespace

int main() {
    const s32 bound = NebulaCutsceneSkipBound::cMaxSteps;
    // A fast device: 40 steps in a 9 ms share of a frame.
    expect(stepsUntilEnd(225000, bound * 2) == bound, "a fast device ends at the bound");
    // A slow device: 2 steps a frame at 30 frames a second, so 60 s of wall time pass long before the bound.
    expect(stepsUntilEnd(16666667, bound * 2) == bound, "a slow device ends at the same step");
    // A scene of 40 s of play skipped at 3 steps a frame (over 7 minutes of wall time) is not cut short.
    expect(stepsUntilEnd(11111111, NebulaFrameClock::stepsOfSeconds(40)) == -1, "a long scene on a slow device runs to its end");
    // A ten-minute sheet that keeps moving runs to its end; the bound counts from where it stops.
    const s32 sheet = NebulaFrameClock::stepsOfSeconds(600);
    expect(stepsUntilEnd(225000, sheet + bound * 2, sheet) == sheet + bound, "a moving sheet does not count");
    expect(bound == NebulaFrameClock::stepsOfSeconds(180), "three minutes of play");
    if (sFailures == 0) {
        printf("cutscene skip bound: pass\n");
    }
    return sFailures == 0 ? 0 : 1;
}
