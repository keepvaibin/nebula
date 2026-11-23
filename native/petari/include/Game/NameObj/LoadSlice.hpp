#pragma once

#include <cstdint>

// A slice of a load that runs over several frames (P-260): a fixed count of work items, never the wall clock, so how
// many frames lie between a star's choice and play is the same on every machine. An item is one indivisible piece of
// the load: a phase of the scene's start (GameScene::initAfterScenarioChosen), the player, one placement made whole, one
// piece of an object that initialises in pieces (FileSelector::initStep), one Star Bit of the reserve. The load worker
// that takes the load off the frames is G3.7's.
class LoadSlice {
public:
    // The items of one frame's slice (decided in step S0.2.1: docs/plan/native/decisions.tsv).
    static constexpr uint32_t cItemsPerFrame = 32;

    explicit LoadSlice(uint32_t items) : mLeft(items) {
    }

    // A slice that never runs out: the whole load at once (a load that is not spread over frames).
    static LoadSlice whole() {
        return LoadSlice(UINT32_MAX);
    }

    // One item done. True when the slice is used up.
    bool spend() {
        if (mLeft != UINT32_MAX && mLeft != 0) {
            --mLeft;
        }
        return mLeft == 0;
    }

    bool isUsedUp() const {
        return mLeft == 0;
    }

private:
    uint32_t mLeft;
};
