#include "Game/System/InputPointer.hpp"
#include "Game/System/PlayerInputs.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/System/NebulaFrameClock.hpp"

namespace {
    static const s32 sIsValidInScreenTime = 5;
    static const s32 sIsInValidOutScreenTime = 10;
    static const s32 sLostSensorTime = 20;
};  // namespace

InputPointer::InputPointer(const PlayerInput* pPad)
    : mPad(pPad), mPosPlayRadius(0.03f), mPosSensitivity(0.5f), mHoriPlayRadius(), mHoriSensitivity(1.0f), mDistPlayRadius(), mDistSensitivity(1.0f),
      mLostSensorTime(), mDistDisplay(0.0f), mDpdValidFg(), mValidInScreenTime(), mInValidOutScreenTime(), mEnablePastCount(), mIsPointInScreen() {
    mPointingPosArray = new TVec2f[120];
    mHorizonArray = new TVec2f[120];
    mMaxPastCount = 120;
    reset();
}

void InputPointer::reset() {
    for (s32 i = 0; i < mMaxPastCount; i++) {
        mPointingPosArray[i].set(0.0f);
        mHorizonArray[i].set(0.0f);
    }

    mDpdValidFg = 0;
    mDistDisplay = 0.0f;
    mValidInScreenTime = 0;
    mEnablePastCount = 0;
    mLostSensorTime = 0;
    mIsPointInScreen = false;
}

void InputPointer::update() {
    PlayerInputSample* pSample = mPad->getSample(0);

    if (pSample == nullptr) {
        reset();
        return;
    }

    mDistDisplay = pSample->mDistance;
    mDpdValidFg = pSample->mIsPointerOn ? 2 : 0;

    s32 validCount = mPad->getValidStatusCount();
    mEnablePastCount = 0;

    if (validCount > mMaxPastCount) {
        validCount = mMaxPastCount;
    }

    bool isAnyDpdValid = false;

    for (s32 i = validCount - 1; i >= 0; i--) {
        PlayerInputSample* curStatus = mPad->getSample(i);
        s8 dpdValidFg = curStatus->mIsPointerOn ? 2 : 0;

        if (dpdValidFg > 0) {
            isAnyDpdValid = true;
        }

        if (dpdValidFg < 2) {
            mValidInScreenTime = 0;
            mInValidOutScreenTime++;
        } else {
            if ((!mIsPointInScreen && mValidInScreenTime >= ::sIsValidInScreenTime) ||
                (mIsPointInScreen && mInValidOutScreenTime <= ::sIsInValidOutScreenTime)) {
                mPointingPosArray[mEnablePastCount].x = curStatus->mPointerPos.x;
                mPointingPosArray[mEnablePastCount].y = curStatus->mPointerPos.y;
                mHorizonArray[mEnablePastCount].x = curStatus->mHorizon.x;
                mHorizonArray[mEnablePastCount].y = curStatus->mHorizon.y;


                mEnablePastCount++;
            }

            mInValidOutScreenTime = 0;
            mValidInScreenTime++;
        }
    }

    mIsPointInScreen = mEnablePastCount > 0;

    if (isAnyDpdValid) {
        mLostSensorTime = 0;
    } else if (mLostSensorTime < ::sLostSensorTime) {
        mLostSensorTime++;
    }
}

void InputPointer::getPointingPos(TVec2f* pOut) const {
    if (mIsPointInScreen) {
        pOut->set(mPointingPosArray[mEnablePastCount - 1]);
    } else {
        pOut->zero();
    }
}

void InputPointer::getHorizonVec(TVec2f* pOut) const {
    if (mIsPointInScreen) {
        pOut->set(mHorizonArray[mEnablePastCount - 1]);
    } else {
        pOut->zero();
    }
}

void InputPointer::getPastPointingPos(TVec2f* pOut, s32 idx) const {
    pOut->set(mPointingPosArray[mEnablePastCount - 1 - idx]);
}

s32 InputPointer::getEnablePastCount() const {
    return mEnablePastCount;
}

void InputPointer::getPointingPosBasedOnScreen(TVec2f* pOut) const {
    pOut->x = (0.5f + (0.5f * mPointingPosArray->x)) * static_cast< s32 >(MR::getScreenWidth());
    pOut->y = (0.5f + (0.5f * mPointingPosArray->y)) * static_cast< s32 >(NebulaFrameClock::cScreenUnitsDown);
}
