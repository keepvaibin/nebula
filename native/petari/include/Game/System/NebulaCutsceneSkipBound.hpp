#pragma once

#include "Game/System/NebulaFrameClock.hpp"

// The bound on one cutscene skip: a skip ends when its cutscene does; this ends one that never does without the
// player. Begun when the skip starts, told of each step of play the skip runs, the wall clock then, and whether the
// cutscene's own clock (its time sheet) moved in that step; true once the skip must end.
class NebulaCutsceneSkipBound {
public:
    // Three minutes of play with no time sheet moving: a scene the stall check cannot see (a boss's own starter, an
    // opening camera) runs that long only when it waits on the player. A sheet that moves is the scene going on, and a
    // sheet that stops is the stall check's, so steps with a moving sheet do not count: the ending runs to its end.
    static constexpr s32 cMaxSteps = NebulaFrameClock::stepsOfSeconds(180);

    void begin(s64 nowNs) {
        mStartNs = nowNs;
        mLastNs = nowNs;
        mSteps = 0;
        mUnpacedSteps = 0;
    }

    // Counted in steps of play, not wall time (P-158): a device that fits fewer extra steps in a frame takes longer to
    // skip the same scene and still reaches its end.
    bool step(s64 nowNs, bool sheetMoved) {
        mLastNs = nowNs;
        mSteps++;
        if (!sheetMoved) {
            mUnpacedSteps++;
        }
        return mUnpacedSteps >= cMaxSteps;
    }

    s32 steps() const { return mSteps; }

    // Wall time from the start to the last step, for the log.
    f32 wallSeconds() const { return static_cast< f32 >(mLastNs - mStartNs) / 1e9f; }

private:
    s64 mStartNs = 0;
    s64 mLastNs = 0;
    s32 mSteps = 0;
    s32 mUnpacedSteps = 0;
};
