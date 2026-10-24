#include "Game/System/PauseButtonCheckerInGame.hpp"
#include "Game/Util/GamePadUtil.hpp"

namespace {
    static const s32 sHoldCounterMax = 12;
};  // namespace

PauseButtonCheckerInGame::PauseButtonCheckerInGame()
    : mPlusHoldFrame(0), mMinusHoldFrame(0)
#ifdef TARGET_PC
      ,
      mPlusUsed(false), mMinusUsed(false)
#endif
{
}

void PauseButtonCheckerInGame::update() {
    if (MR::testCorePadButtonPlus(WPAD_CHAN0)) {
        mPlusHoldFrame++;
    } else {
        mPlusHoldFrame = 0;
#ifdef TARGET_PC
        mPlusUsed = false;
#endif
    }

    if (MR::testCorePadButtonMinus(WPAD_CHAN0)) {
        mMinusHoldFrame++;
    } else {
        mMinusHoldFrame = 0;
#ifdef TARGET_PC
        mMinusUsed = false;
#endif
    }
}

#ifdef TARGET_PC
// A press pauses the first frame it is permitted: a tap is enough (the console waited for 12 frames of holding, longer
// than a quick press), and a press made while A or B is held pauses once they are let go.
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

// Not while A or B is held. The console also waited for the remote to be still, which drops a press made while the
// controller moves (a gyro pointer, a hand in motion).
bool PauseButtonCheckerInGame::isPermitToPause() const {
    return !MR::testCorePadButtonA(WPAD_CHAN0) && !MR::testCorePadButtonB(WPAD_CHAN0);
}
#else
bool PauseButtonCheckerInGame::isPermitToPlusPause() const {
    return isPermitToPause() && mPlusHoldFrame == ::sHoldCounterMax;
}

bool PauseButtonCheckerInGame::isPermitToMinusPause() const {
    return isPermitToPause() && mMinusHoldFrame == ::sHoldCounterMax;
}

bool PauseButtonCheckerInGame::isPermitToPause() const {
    return !MR::isOperatingWPad(WPAD_CHAN0);
}
#endif
