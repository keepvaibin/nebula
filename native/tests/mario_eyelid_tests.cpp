// SPDX-License-Identifier: GPL-3.0-only
// MR::eyeLidFramesAt (Game/Player/MarioEyeLid.hpp): Mario's eyelid frames are four textures from "mario_eyeLid.0" in
// the model being drawn, or none. N-3035: the console took texture 0 for a model without the frame and read the four
// frames past a model's textures; a mod may replace Mario's, the bee's and the hopper's models.
#include "Game/Player/MarioEyeLid.hpp"

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
    expect(MR::eyeLidFramesAt(5, 20) == 5, "the four frames inside the model");
    expect(MR::eyeLidFramesAt(16, 20) == 16, "the four frames that end at the model's last texture");
    expect(MR::eyeLidFramesAt(17, 20) == -1, "a first frame three from the end has no four");
    expect(MR::eyeLidFramesAt(0, 3) == -1, "a model of three textures has no four");
    expect(MR::eyeLidFramesAt(-1, 20) == -1, "a model without the frame has none (not texture 0)");
    expect(MR::eyeLidFramesAt(20, 20) == -1, "an index past the textures is none");
    if (sFailures == 0) {
        printf("native_mario_eyelid_tests: all passed\n");
    }
    return sFailures == 0 ? 0 : 1;
}
