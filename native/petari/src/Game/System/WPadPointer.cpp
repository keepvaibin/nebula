#include "Game/System/WPadPointer.hpp"
#include "Game/System/WPadHolder.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include <JSystem/JUtility/JUTVideo.hpp>

void WPadPointer_FORCE_MATCH_SDATA2() {
    (void)0.03f;
    (void)0.0001f;
}

namespace {
    static const f32 sMoveJudgeSpeed = 0.0001f;
    static const s32 sIsValidInScreenTime = 5;
    static const s32 sIsInValidOutScreenTime = 10;
    static const s32 sLostSensorTime = 20;
};  // namespace

WPadPointer::WPadPointer(const WPad* pPad)
    : mPad(pPad), mPosPlayRadius(0.03f), mPosSensitivity(0.5f), mHoriPlayRadius(), mHoriSensitivity(1.0f), mDistPlayRadius(), mDistSensitivity(1.0f),
      mLostSensorTime(), mDistDisplay(0.0f), mDpdValidFg(), mValidInScreenTime(), mInValidOutScreenTime(), mEnablePastCount(), mIsPointInScreen(),
      mIsPointerMoved() {
    mPointingPosArray = new TVec2f[120];
    mHorizonArray = new TVec2f[120];
    mMaxPastCount = 120;
    reset();
}

void WPadPointer::reset() {
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
    mIsPointerMoved = false;
}

void WPadPointer::update() {
    WPadSample* pSample = mPad->getSample(0);

    if (pSample == nullptr) {
        reset();
        return;
    }

    mIsPointerMoved = false;
    mDistDisplay = pSample->mDistance;
    mDpdValidFg = pSample->mIsPointerOn ? 2 : 0;

    s32 validCount = mPad->getValidStatusCount();
    mEnablePastCount = 0;

    if (validCount > mMaxPastCount) {
        validCount = mMaxPastCount;
    }

    bool isAnyDpdValid = false;

    for (s32 i = validCount - 1; i >= 0; i--) {
        WPadSample* curStatus = mPad->getSample(i);
        s8 dpdValidFg = curStatus->mIsPointerOn ? 2 : 0;

        if (dpdValidFg > 0) {
            isAnyDpdValid = true;
        }

        if (dpdValidFg < 2) {
            mValidInScreenTime = 0;
            mInValidOutScreenTime++;
        } else {
            if (!mIsPointInScreen && mValidInScreenTime >= ::sIsValidInScreenTime ||
                mIsPointInScreen && mInValidOutScreenTime <= ::sIsInValidOutScreenTime) {
                mPointingPosArray[mEnablePastCount].x = curStatus->mPointerPos.x;
                mPointingPosArray[mEnablePastCount].y = curStatus->mPointerPos.y;
                mHorizonArray[mEnablePastCount].x = curStatus->mHorizon.x;
                mHorizonArray[mEnablePastCount].y = curStatus->mHorizon.y;

                if (curStatus->mPointerSpeed > ::sMoveJudgeSpeed) {
                    mIsPointerMoved = true;
                }

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

void WPadPointer::getPointingPos(TVec2f* pOut) const {
    if (mIsPointInScreen) {
        pOut->set(mPointingPosArray[mEnablePastCount - 1]);
    } else {
        pOut->zero();
    }
}

void WPadPointer::getHorizonVec(TVec2f* pOut) const {
    if (mIsPointInScreen) {
        pOut->set(mHorizonArray[mEnablePastCount - 1]);
    } else {
        pOut->zero();
    }
}

void WPadPointer::getPastPointingPos(TVec2f* pOut, s32 idx) const {
    pOut->set(mPointingPosArray[mEnablePastCount - 1 - idx]);
}

s32 WPadPointer::getEnablePastCount() const {
    return mEnablePastCount;
}

void WPadPointer::getPointingPosBasedOnScreen(TVec2f* pOut) const {
    pOut->x = (0.5f + (0.5f * mPointingPosArray->x)) * static_cast< s32 >(MR::getScreenWidth());
    pOut->y = (0.5f + (0.5f * mPointingPosArray->y)) * static_cast< s32 >(JUTVideo::getManager()->getRenderMode()->efbHeight);
}
