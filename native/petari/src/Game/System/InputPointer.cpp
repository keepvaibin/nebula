#include "Game/System/InputPointer.hpp"
#include "Game/System/PlayerInputs.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_getPointingPosBasedOnScreen_f1, "getPointingPosBasedOnScreen.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_getPointingPosBasedOnScreen_f2, "getPointingPosBasedOnScreen.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_getPointingPosBasedOnScreen_f3, "getPointingPosBasedOnScreen.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_getPointingPosBasedOnScreen_f4, "getPointingPosBasedOnScreen.f4", 0.5f);

InputPointer::InputPointer(const PlayerInput* pPad)
    : mPad(pPad), mPos(0.0f, 0.0f), mHorizon(0.0f, 0.0f), mDistDisplay(0.0f), mIsPointInScreen(false) {
}

void InputPointer::update() {
    const PlayerInputSample* const pSample = mPad->getSample();
    if (pSample == nullptr) {
        mIsPointInScreen = false;
        mDistDisplay = 0.0f;
        return;
    }

    mDistDisplay = pSample->mDistance;
    mIsPointInScreen = pSample->mIsPointerOn;
    if (mIsPointInScreen) {
        mPos.set(pSample->mPointerPos.x, pSample->mPointerPos.y);
        mHorizon.set(pSample->mHorizon.x, pSample->mHorizon.y);
    }
}

void InputPointer::getPointingPos(Vec2f* pOut) const {
    if (mIsPointInScreen) {
        pOut->set(mPos);
    } else {
        pOut->zero();
    }
}

void InputPointer::getHorizonVec(Vec2f* pOut) const {
    if (mIsPointInScreen) {
        pOut->set(mHorizon);
    } else {
        pOut->zero();
    }
}

void InputPointer::getPastPointingPos(Vec2f* pOut, s32) const {
    pOut->set(mPos);
}

s32 InputPointer::getEnablePastCount() const {
    return mIsPointInScreen ? 1 : 0;
}

void InputPointer::getPointingPosBasedOnScreen(Vec2f* pOut) const {
    pOut->x = (cNum_getPointingPosBasedOnScreen_f1 + (cNum_getPointingPosBasedOnScreen_f2 * mPos.x)) * static_cast< s32 >(MR::getScreenWidth());
    pOut->y = (cNum_getPointingPosBasedOnScreen_f3 + (cNum_getPointingPosBasedOnScreen_f4 * mPos.y)) * static_cast< s32 >(NebulaFrameClock::cScreenLines);
}
