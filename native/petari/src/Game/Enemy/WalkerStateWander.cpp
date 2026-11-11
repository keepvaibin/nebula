#include "Game/Enemy/WalkerStateWander.hpp"
#include "Game/Enemy/TerritoryMover.hpp"
#include "Game/Enemy/WalkerStateFunction.hpp"
#include "Game/Enemy/WalkerStateParam.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_WalkerStateWander_f1, "WalkerStateWander.f1", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWalk_f1, "exeWalk.f1", 8.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWalk_f2, "exeWalk.f2", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWalk_f3, "exeWalk.f3", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWalk_f4, "exeWalk.f4", 150.0f);

namespace {
    WalkerStateWanderParam sDefaultParam;
};  // namespace

namespace NrvWalkerStateWander {
    NEW_NERVE(WalkerStateWanderNrvWait, WalkerStateWander, Wait);
    NEW_NERVE(WalkerStateWanderNrvWalk, WalkerStateWander, Walk);
};  // namespace NrvWalkerStateWander

WalkerStateWanderParam::WalkerStateWanderParam() : mWaitTime(120), mWalkTime(120), mSpeed(0.2f), mTurnMaxRateDegree(3.0f), mTargetDistance(20.0f) {
}

WalkerStateWander::WalkerStateWander(LiveActor* pHost, Vec3f* pDirection, WalkerStateParam* pStateParam, WalkerStateWanderParam* pWanderParam)
    : ActorStateBase< LiveActor >("クリボー型うろつき状態", pHost), mDirection(pDirection), mTerritoryMover(nullptr), mStateParam(pStateParam),
      mWanderParam(pWanderParam) {
    initNerve(GET_NERVE(WalkerStateWander, WalkerStateWanderNrvWait));

    if (mWanderParam == nullptr) {
        mWanderParam = &::sDefaultParam;
    }

    mTerritoryMover = new TerritoryMover(cNum_WalkerStateWander_f1);
    mTerritoryMover->setCenter(getHost()->mPosition);
}

void WalkerStateWander::appear() {
    mIsDead = false;
    setNerve(GET_NERVE(WalkerStateWander, WalkerStateWanderNrvWait));
}

void WalkerStateWander::setWanderCenter(const Vec3f& rCenter) {
    mTerritoryMover->setCenter(rCenter);
}

void WalkerStateWander::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startAction(getHost(), "Wait");
    }

    WalkerStateFunction::calcPassiveMovement(getHost(), mStateParam);

    if (MR::isGreaterStep(this, mWanderParam->mWaitTime)) {
        mTerritoryMover->decideNextTargetPos(getHost());
        setNerve(GET_NERVE(WalkerStateWander, WalkerStateWanderNrvWalk));
    }
}

void WalkerStateWander::exeWalk() {
    if (MR::isFirstStep(this)) {
        MR::startAction(getHost(), "Walk");
    }

    MR::turnDirectionToTargetUseGroundNormalDegree(getHost(), mDirection, mTerritoryMover->mTarget, mWanderParam->mTurnMaxRateDegree);

    if (MR::isFaceToTargetHorizontalDegree(getHost(), mTerritoryMover->mTarget, *mDirection, cNum_exeWalk_f1)) {
        MR::addVelocityMoveToDirection(getHost(), *mDirection, mWanderParam->mSpeed);
    }

    WalkerStateFunction::calcPassiveMovement(getHost(), mStateParam);

    if (MR::isFallNextMove(getHost(), cNum_exeWalk_f2, cNum_exeWalk_f3, cNum_exeWalk_f4, nullptr)) {
        MR::zeroVelocity(getHost());
        setNerve(GET_NERVE(WalkerStateWander, WalkerStateWanderNrvWait));
        return;
    }

    if (MR::isGreaterStep(this, mWanderParam->mWalkTime) || mTerritoryMover->isReachedTarget(getHost(), mWanderParam->mTargetDistance)) {
        setNerve(GET_NERVE(WalkerStateWander, WalkerStateWanderNrvWait));
    }
}
