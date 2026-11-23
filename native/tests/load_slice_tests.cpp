// SPDX-License-Identifier: GPL-3.0-only
// The load's slices (Game/NameObj/LoadSlice.hpp, P-260): a slice ends after its fixed count of items, never by the
// clock, so a load spread over frames takes the same frames on every machine; the whole slice never ends.
#include "Game/NameObj/LoadSlice.hpp"

#include <stdio.h>

namespace {
    int sFailures;

    void expect(bool ok, const char* pWhat) {
        if (!ok) {
            printf("FAIL %s\n", pWhat);
            sFailures++;
        }
    }

    // The frames a load of `items` items takes in slices of `perFrame`, each frame resuming where the last stopped
    // (as GameScene::initAfterScenarioChosen is resumed once a frame).
    unsigned framesFor(unsigned items, uint32_t perFrame) {
        unsigned done = 0;
        unsigned frames = 0;
        while (done < items) {
            LoadSlice slice(perFrame);
            frames++;
            while (done < items) {
                done++;
                if (slice.spend()) {
                    break;
                }
            }
        }
        return frames;
    }
}  // namespace

int main() {
    // A slice of three: two items leave it open, the third uses it up, and it stays used up.
    LoadSlice three(3);
    expect(!three.isUsedUp(), "a new slice is not used up");
    expect(!three.spend(), "the first item of three");
    expect(!three.spend(), "the second item of three");
    expect(three.spend(), "the third item uses up a slice of three");
    expect(three.isUsedUp(), "used up after its count");
    expect(three.spend(), "a used-up slice stays used up");

    // The whole slice never runs out.
    LoadSlice whole = LoadSlice::whole();
    bool ended = false;
    for (unsigned i = 0; i < 1000000; i++) {
        ended = ended || whole.spend();
    }
    expect(!ended && !whole.isUsedUp(), "the whole slice never ends");

    // The frames follow the count of work alone.
    expect(framesFor(1, LoadSlice::cItemsPerFrame) == 1, "one item, one frame");
    expect(framesFor(LoadSlice::cItemsPerFrame, LoadSlice::cItemsPerFrame) == 1, "a full slice, one frame");
    expect(framesFor(LoadSlice::cItemsPerFrame + 1, LoadSlice::cItemsPerFrame) == 2, "one item more, a second frame");
    expect(framesFor(1000, 32) == 32, "1000 items in slices of 32: 32 frames");

    if (sFailures != 0) {
        printf("%d failure(s)\n", sFailures);
        return 1;
    }
    printf("load slice tests passed\n");
    return 0;
}
