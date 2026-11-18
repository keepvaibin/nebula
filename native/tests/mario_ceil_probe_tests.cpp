// SPDX-License-Identifier: GPL-3.0-only
// MR::retryCeilProbeAbove (Game/Player/MarioCeilProbe.hpp, N-3016): Mario::calcDistToCeil probes again from 10 above a
// hanging Mario once, not for ever. The test runs calcDistToCeil's loop with a probe whose every hit lies under Mario's
// feet (the step's gravity 90 degrees or more from the air gravity); past cProbeBound probes it stops itself, so a loop
// that never ends shows as a failure, not a hang.
#include "Game/Player/MarioCeilProbe.hpp"

#include <stdio.h>

namespace {
    int sFailures = 0;
    constexpr u32 cProbeBound = 1000;

    void expect(bool ok, const char* what) {
        if (!ok) {
            printf("FAILED: %s\n", what);
            sFailures++;
        }
    }

    // calcDistToCeil's loop: each probe hits at or under the feet; returns the number of probes made.
    u32 probes(f32 offset) {
        u32 count = 0;
        while (true) {
            count++;
            if (count >= cProbeBound) {
                return count;
            }
            if (MR::retryCeilProbeAbove(offset)) {
                continue;
            }
            return count;
        }
    }
}  // namespace

int main() {
    expect(probes(30.0f) == 1, "a standing Mario probes once from under him");
    expect(probes(0.0f) == 2, "a hanging Mario probes again from above once (the console probed for ever)");
    f32 offset = 0.0f;
    expect(MR::retryCeilProbeAbove(offset) && offset == MR::cCeilProbeAbove, "the second probe starts 10 above");
    expect(!MR::retryCeilProbeAbove(offset), "and is the last");
    if (sFailures == 0) {
        printf("native_mario_ceil_probe_tests: all passed\n");
    }
    return sFailures == 0 ? 0 : 1;
}
