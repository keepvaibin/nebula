#include "Game/Enemy/WalkerStateChase.hpp"
#include "Game/Enemy/WalkerStateFunction.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_exeStart_f1, "exeStart.f1", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeStart_f2, "exeStart.f2", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeStart_f3, "exeStart.f3", 150.0f);

namespace {
    WalkerStateChaseParam sDefaultParam;
};  // namespace

namespace NrvWalkerStateChase {
    NEW_NERVE(WalkerStateChaseNrvStart, WalkerStateChase, Start);
    NEW_NERVE(WalkerStateChaseNrvEnd, WalkerStateChase, End);
};  // namespace NrvWalkerStateChase

// The speed is each walker's own: Kuribo, KuriboChief and KuriboMini set it.
WalkerStateChaseParam::WalkerStateChaseParam() : mChaseSpeed(0.0f), mChaseTime(130), mForceChaseEndTime(300), mTurnMaxRateDegree(2.0f), mChaseEndWaitTime(30) {
}

WalkerStateChase::WalkerStateChase(LiveActor* pHost, Vec3f* pDirection, WalkerStateParam* pStateParam, WalkerStateChaseParam* pChaseParam)
    : ActorStateBase< LiveActor >("クリボー追いかけ状態", pHost), mStateParam(pStateParam), mChaseParam(pChaseParam), mDirection(pDirection) {
    if (mChaseParam == nullptr) {
        mChaseParam = &::sDefaultParam;
    }

    initNerve(GET_NERVE(WalkerStateChase, WalkerStateChaseNrvStart));
}

void WalkerStateChase::appear() {
    mIsDead = false;
    setNerve(GET_NERVE(WalkerStateChase, WalkerStateChaseNrvStart));
}

void WalkerStateChase::exeStart() {
    if (MR::isFirstStep(this)) {
        MR::startAction(getHost(), "Run");
    }

    const bool isInSight = WalkerStateFunction::isInSightPlayer(getHost(), *mDirection, mStateParam);
    if (isInSight) {
        MR::turnDirectionToTargetUseGroundNormalDegree(getHost(), mDirection, *MR::getPlayerPos(), mChaseParam->mTurnMaxRateDegree);
    }

    MR::addVelocityMoveToDirection(getHost(), *mDirection, mChaseParam->mChaseSpeed);
    WalkerStateFunction::calcPassiveMovement(getHost(), mStateParam);

    if (MR::isFallNextMove(getHost(), cNum_exeStart_f1, cNum_exeStart_f2, cNum_exeStart_f3, nullptr)) {
        MR::zeroVelocity(getHost());
        setNerve(GET_NERVE(WalkerStateChase, WalkerStateChaseNrvEnd));
    } else if (MR::isGreaterStep(this, mChaseParam->mForceChaseEndTime) || (MR::isGreaterStep(this, mChaseParam->mChaseTime) && !isInSight)) {
        setNerve(GET_NERVE(WalkerStateChase, WalkerStateChaseNrvEnd));
    }
}

void WalkerStateChase::exeEnd() {
    if (MR::isFirstStep(this)) {
        MR::startAction(getHost(), "Wait");
    }

    WalkerStateFunction::calcPassiveMovement(getHost(), mStateParam);
    if (MR::isGreaterStep(this, mChaseParam->mChaseEndWaitTime)) {
        kill();
    }
}

bool WalkerStateChase::isRunning() const {
    return isNerve(GET_NERVE(WalkerStateChase, WalkerStateChaseNrvStart)) && MR::isBindedGround(getHost());
}
