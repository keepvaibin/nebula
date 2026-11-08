#include "Game/Boss/TripodBossStepSequence.hpp"
#include "Game/Boss/TripodBossStepPoint.hpp"

TripodBossStepSequence::TripodBossStepSequence() : mCurrentPoint(), mPointNum() {
}

void TripodBossStepSequence::addStepPoint(TripodBossStepPoint* pPoint) {
    s32 sortPriority = pPoint->mSequencePriority;

    if (mPointNum == mStepPoints.size()) {
        mStepPoints.grow();
    }
    mPointNum++;

    for (s32 i = mPointNum - 1; i > 0; i--) {
        if (mStepPoints[i - 1]->mSequencePriority < sortPriority) {
            mStepPoints[i] = pPoint;
            return;
        }

        mStepPoints[i] = mStepPoints[i - 1];
    }

    mStepPoints[0] = pPoint;
}

s32 TripodBossStepSequence::getCurrentLeg() const {
    return mStepPoints[mCurrentPoint]->mLeg;
}

s32 TripodBossStepSequence::getCurrentWaitTime() const {
    return mStepPoints[mCurrentPoint]->mWaitTime;
}

TripodBossStepPoint* TripodBossStepSequence::getCurrentStepPoint() {
    return mPointNum == 0 ? nullptr : mStepPoints[mCurrentPoint];
}

void TripodBossStepSequence::nextStep() {
    mCurrentPoint++;

    if (mCurrentPoint >= mPointNum) {
        mCurrentPoint = 0;
    }
}

bool TripodBossStepSequence::isEmpty() const {
    return mPointNum == 0;
}

void TripodBossStepSequence::reset() {
    mCurrentPoint = 0;
}
