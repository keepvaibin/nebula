#include "Game/Boss/TripodBossStepPoint.hpp"
#include "Game/Boss/TripodBossAccesser.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/MathUtil.hpp"

TripodBossStepPoint::TripodBossStepPoint(const char* pName)
    : LiveActor(pName), mStepPosition(0, 0, 0), mStepNormal(0, 1, 0), mStepFront(0, 0, 1), mLeg(), mSequencePriority(), mArg3(-1), mWaitTime(120) {
}

void TripodBossStepPoint::init(const TableIter& rIter) {
    if (MR::isValidInfo(rIter)) {
        Pos3f mtx;
        MR::getPlacementMatrixFromRT(rIter, &mtx);

        mtx.getTrans(mStepPosition);
        mtx.getYDir(mStepNormal);
        mtx.getZDir(mStepFront);

        MR::getPlacementArg0NoInit(rIter, &mSequencePriority);
        MR::getPlacementArg1NoInit(rIter, &mLeg);
        MR::getPlacementArg2NoInit(rIter, &mWaitTime);
        MR::getPlacementArg3NoInit(rIter, &mArg3);
    }

    makeActorDead();
}

void TripodBossStepPoint::initAfterPlacement() {
    if (mArg3 != -1) {
        MR::addTripodBossStepPoint(this);
    }
}

void TripodBossStepPoint::setStepPosition(const Vec3f& rPos) {
    mStepPosition = rPos;
}

void TripodBossStepPoint::setStepNormal(const Vec3f& rNormal) {
    mStepNormal = rNormal;

    MR::normalizeOrZero(&mStepNormal);
}

void TripodBossStepPoint::setStepFront(const Vec3f& rFront) {
    mStepFront = rFront;

    MR::normalizeOrZero(&mStepFront);
}
