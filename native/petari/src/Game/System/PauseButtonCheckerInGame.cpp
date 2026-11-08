#include "Game/System/PauseButtonCheckerInGame.hpp"
#include "Game/Util/PlayerInputUtil.hpp"

PauseButtonCheckerInGame::PauseButtonCheckerInGame()
    : mPlusHoldFrame(0), mMinusHoldFrame(0)
      ,
      mPlusUsed(false), mMinusUsed(false)
{
}

void PauseButtonCheckerInGame::update() {
    if (MR::isHeldPause(PLAYER_1)) {
        mPlusHoldFrame++;
    } else {
        mPlusHoldFrame = 0;
        mPlusUsed = false;
    }

    if (MR::isHeldMap(PLAYER_1)) {
        mMinusHoldFrame++;
    } else {
        mMinusHoldFrame = 0;
        mMinusUsed = false;
    }
}

// A press pauses the first frame it is permitted: a tap is enough, and a press made while A or B is held pauses once they are let go.
bool PauseButtonCheckerInGame::isPermitToPlusPause() const {
    if (mPlusHoldFrame == 0 || mPlusUsed || !isPermitToPause()) {
        return false;
    }
    // Either button held when the game resumes (the pause closed with it) waits to be let go.
    mPlusUsed = true;
    mMinusUsed = true;
    return true;
}

bool PauseButtonCheckerInGame::isPermitToMinusPause() const {
    if (mMinusHoldFrame == 0 || mMinusUsed || !isPermitToPause()) {
        return false;
    }
    mPlusUsed = true;
    mMinusUsed = true;
    return true;
}

// Not while A or B is held.
bool PauseButtonCheckerInGame::isPermitToPause() const {
    return !MR::isHeldJump(PLAYER_1) && !MR::isHeldFire(PLAYER_1);
}
