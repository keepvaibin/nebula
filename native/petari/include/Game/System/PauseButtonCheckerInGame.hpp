#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

class PauseButtonCheckerInGame {
public:
    PauseButtonCheckerInGame();

    void update();
    bool isPermitToPlusPause() const;
    bool isPermitToMinusPause() const;
    bool isPermitToPause() const;

private:
    /* 0x0 */ s32 mPlusHoldFrame;
    /* 0x4 */ s32 mMinusHoldFrame;
    // A press of + or - has paused: held on when the game resumes, neither pauses again until let go.
    mutable bool mPlusUsed;
    mutable bool mMinusUsed;
};
