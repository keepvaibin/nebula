#pragma once

#include <revolution.h>

class PauseButtonCheckerInGame {
public:
    PauseButtonCheckerInGame();

    void update();
    bool isPermitToPlusPause() const;
    bool isPermitToMinusPause() const;
    bool isPermitToPause() const NO_INLINE;

private:
    /* 0x0 */ s32 mPlusHoldFrame;
    /* 0x4 */ s32 mMinusHoldFrame;
#ifdef TARGET_PC
    // A press of + or - has paused: held on when the game resumes, neither pauses again until let go.
    mutable bool mPlusUsed;
    mutable bool mMinusUsed;
#endif
};
