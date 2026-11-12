#include "Game/Boss/KoopaBattleBase.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/Koopa.hpp"
#include "Game/Boss/KoopaFunction.hpp"
#include "Game/Boss/KoopaStateDamageEscape.hpp"
#include "Game/Boss/KoopaStateGuard.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/NerveUtil.hpp"

namespace {
    static MR::ActorMoveParam sFindParam = {0.0f, 1.0f, 0.98f, 3.0f};
    const f32 sChasePlayerJumpSpeed = 15.0f;
    const f32 sWalkAnimRate = 0.18f;
    static const s32 sWanderStepToAttackMin = NebulaFrameClock::stepsOfSeconds(1);
    static const s32 sWanderStepToAttackMax = NebulaFrameClock::stepsOfSeconds(2);
    static const s32 sSearchStep = NebulaFrameClock::stepsOfSeconds(2);
    const f32 sSearchTurnSpeed = 2.4f;
    const f32 sFindJumpSpeed = 10.0f;
};  // namespace

KoopaBattleBase::KoopaBattleBase(const char* pName, Koopa* pKoopa)
    : ActorStateBase< Koopa >(pName, pKoopa), mStateDamageEscape(), mStateGuard(), mWanderTime(-1) {
}

void KoopaBattleBase::init() {
    mStateDamageEscape = new KoopaStateDamageEscape(mHost);
    mStateGuard = new KoopaStateGuard(mHost);
}

void KoopaBattleBase::kill() {
    mIsDead = true;

    MR::zeroVelocity(mHost);

    KoopaFunction::startRecoverKoopaArmor(mHost);
    KoopaFunction::startRecoverKoopaTailThorn(mHost);

    MR::overlayWithPreviousScreen(2);
}

void KoopaBattleBase::updateChasePlayer(const MR::ActorMoveParam& rMoveParam) {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "Walk");
        MR::onBind(mHost);
        KoopaFunction::startFaceCtrl(mHost);
    }

    if (MR::isBindedWall(mHost)) {
        Koopa* pKoopa = mHost;
        if (!MR::sendMsgEnemyAttackToBindedSensor(pKoopa, pKoopa->getSensor("Body"))) {
            MR::addVelocityJump(mHost, ::sChasePlayerJumpSpeed);
        }
    }

    MR::moveAndTurnToPlayer(mHost, &mHost->mFront, rMoveParam);

    Koopa* pKoopa = mHost;
    MR::setBckRate(pKoopa, MR::calcVelocityLength(pKoopa) * ::sWalkAnimRate);
}

bool KoopaBattleBase::updateWander(const MR::ActorMoveParam& rMoveParam) {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "Walk");

        MR::onBind(mHost);

        KoopaFunction::endFaceCtrl(mHost, -1);

        mWanderTime = MR::getRandom(::sWanderStepToAttackMin, ::sWanderStepToAttackMax);
    }

    if (MR::isBindedWall(mHost)) {
        Koopa* pKoopa = mHost;
        if (!MR::sendMsgEnemyAttackToBindedSensor(pKoopa, pKoopa->getSensor("Body"))) {
            MR::addVelocityJump(mHost, ::sChasePlayerJumpSpeed);
        }
    }

    MR::moveAndTurnToDirection(mHost, KoopaFunction::getKoopaFrontPtr(mHost), KoopaFunction::getKoopaFront(mHost), rMoveParam);

    Koopa* pKoopa = mHost;
    MR::setBckRate(pKoopa, MR::calcVelocityLength(pKoopa) * ::sWalkAnimRate);

    return MR::isStep(this, mWanderTime);
}

bool KoopaBattleBase::updateSearch() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "Search");
        MR::zeroVelocity(mHost);
    }

    MR::rotateVecDegree(KoopaFunction::getKoopaFrontPtr(mHost), mHost->mGravity, ::sSearchTurnSpeed);

    if (MR::isStep(this, ::sSearchStep)) {
        return true;
    }

    return false;
}

bool KoopaBattleBase::updateFind() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "Find");
        MR::setVelocityJump(mHost, ::sFindJumpSpeed);
    }

    Koopa* pKoopa = mHost;
    MR::moveAndTurnToPlayer(pKoopa, KoopaFunction::getKoopaFrontPtr(pKoopa), ::sFindParam);

    if (!MR::isFirstStep(this) && MR::isBindedGround(mHost)) {
        MR::tryRumblePadAndCameraDistanceMiddle(mHost, 800.0f, 1200.0f, 2000.0f);
        return true;
    }

    return false;
}

// True once Bowser, already angry, is knocked down again: the level is over.
bool KoopaBattleBase::updateDamageReverse(const Nerve* pEndNerve, const Nerve* pRecoverNerve) {
    if (MR::isFirstStep(this)) {
        mStateDamageEscape->appear();
        mStateDamageEscape->startDamageReverse();
    }

    if (mStateDamageEscape->update()) {
        setNerve(pEndNerve);
        return false;
    }

    if (!mStateDamageEscape->isDownEnd()) {
        return false;
    }

    if (KoopaFunction::isKoopaAngry(mHost)) {
        return true;
    }

    KoopaFunction::startKoopaAngry(mHost);
    setNerve(pRecoverNerve);
    return false;
}

bool KoopaBattleBase::updateRecover(const Nerve* pNerve) {
    if (MR::isFirstStep(this)) {
        MR::zeroVelocity(mHost);
        MR::startAction(mHost, "Recover");
        KoopaFunction::startRecoverKoopaArmor(mHost);
    }

    if (MR::isActionEnd(mHost)) {
        setNerve(pNerve);

        return true;
    }

    return false;
}
