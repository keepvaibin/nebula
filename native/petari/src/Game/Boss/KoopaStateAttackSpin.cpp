#include "Game/Boss/KoopaStateAttackSpin.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/Koopa.hpp"
#include "Game/Boss/KoopaFunction.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

namespace {
    const MR::ActorMoveParam sRunParam = {3.0f, 1.0f, 0.9f, 3.0f};
    const MR::ActorMoveParam sParamAttackSpin1st = {2.0f, 1.0f, 0.9f, 0.0f};
    const MR::ActorMoveParam sParamAttackSpin2nd = {3.0f, 1.0f, 0.9f, 0.5f};
    const MR::ActorMoveParam sParamAttackSpinBrake = {1.0f, 1.0f, 0.9f, 0.0f};
    const MR::ActorMoveParam sEndParam = {0.0f, 1.0f, 0.9f, 0.0f};
    const MR::ActorMoveParam sEvenParam = {0.0f, 1.0f, 0.95f, 0.5f};
    const s32 sWaitToStartStep = NebulaFrameClock::stepsOfMilliseconds(500);
    const f32 sDistanceToJump = 1000.0f;
    const s32 sRunStepMax = NebulaFrameClock::stepsOfSeconds(2);
    const f32 sRunWallJumpSpeed = 10.0f;
    const f32 sStartTurnSpeed = 5.0f;
    const f32 sWallJumpSpeed = 15.0f;
    const s32 sSpinBrakeStep = NebulaFrameClock::stepsOfSeconds(1);
    const s32 sSpinAttackStepMin = 5;
    const s32 sSpinAttackStepMax = 65;
    const s32 sEvenStopSceneStep = 15;
};  // namespace

namespace NrvKoopaStateAttackSpin {
    NEW_NERVE(KoopaStateAttackSpinNrvWaitToStart, KoopaStateAttackSpin, WaitToStart);
    NEW_NERVE(KoopaStateAttackSpinNrvRun, KoopaStateAttackSpin, Run);
    NEW_NERVE(KoopaStateAttackSpinNrvStart, KoopaStateAttackSpin, Start);
    NEW_NERVE(KoopaStateAttackSpinNrvSpin, KoopaStateAttackSpin, Spin);
    NEW_NERVE(KoopaStateAttackSpinNrvEnd, KoopaStateAttackSpin, End);
    NEW_NERVE(KoopaStateAttackSpinNrvEven, KoopaStateAttackSpin, Even);
};  // namespace NrvKoopaStateAttackSpin

KoopaStateAttackSpin::KoopaStateAttackSpin(Koopa* pKoopa)
    : ActorStateBase< Koopa >("State[スピン攻撃]", pKoopa), mMaxAttacks(1), mAttacks(), mMoveParam(&::sParamAttackSpin1st),
      mSpinDelay(::sWaitToStartStep) {
}

void KoopaStateAttackSpin::init() {
    MR::addHitSensor(mHost, "AttackSpin", ATYPE_KOOPA_ATTACK, 8, 250.0f, Vec3f(0.0f, 250.0f, 0.0f));

    initNerve(GET_NERVE(KoopaStateAttackSpin, KoopaStateAttackSpinNrvStart));

    kill();
}

void KoopaStateAttackSpin::appear() {
    mIsDead = false;

    MR::validateHitSensor(mHost, "AttackSpin");

    mAttacks = 0;

    if (KoopaFunction::isKoopaLv1(mHost)) {
        mMaxAttacks = 1;
    } else if (KoopaFunction::isKoopaLv2(mHost)) {
        mMaxAttacks = 2;
    } else {
        mMaxAttacks = 3;
    }

    mMoveParam = &::sParamAttackSpin1st;

    setNerve(GET_NERVE(KoopaStateAttackSpin, KoopaStateAttackSpinNrvWaitToStart));
}

void KoopaStateAttackSpin::kill() {
    mIsDead = true;

    MR::invalidateHitSensor(mHost, "AttackSpin");
}

bool KoopaStateAttackSpin::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    return KoopaFunction::tryKoopaPushPlayer(pSender, pReceiver) ||
           (isNerve(GET_NERVE(KoopaStateAttackSpin, KoopaStateAttackSpinNrvSpin)) && MR::isGreaterStep(this, ::sSpinAttackStepMin) &&
            MR::isLessStep(this, ::sSpinAttackStepMax) && KoopaFunction::tryKoopaAttackPlayerMaximum(pSender, pReceiver));
}

bool KoopaStateAttackSpin::tryDamage(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isMsgPlayerSpinAttack(msg) || !MR::isSensorPlayer(pSender)) {
        return false;
    }

    if (isNerve(GET_NERVE(KoopaStateAttackSpin, KoopaStateAttackSpinNrvEven))) {
        return false;
    }

    MR::sendMsgEnemyAttackFlipJump(pReceiver, pSender);

    MR::tryRumblePadStrong(this, PLAYER_1);

    MR::startSound(mHost, "SE_BM_KOOPA_SPIN_EVEN");
    MR::startSound(mHost, "SE_BV_KOOPA_GUARD");

    MR::stopSceneForDefaultHit(::sEvenStopSceneStep);

    mAttacks++;
    if (mMaxAttacks >= 2 && mAttacks >= mMaxAttacks) {
        kill();
        return true;
    }

    setNerve(GET_NERVE(KoopaStateAttackSpin, KoopaStateAttackSpinNrvEven));
    return true;
}

void KoopaStateAttackSpin::exeWaitToStart() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "Wait");
        MR::zeroVelocity(mHost);
    }

    if (MR::isStep(this, mSpinDelay)) {
        setNerve(GET_NERVE(KoopaStateAttackSpin, KoopaStateAttackSpinNrvStart));
    }
}

void KoopaStateAttackSpin::exeRun() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "Run");
    }

    if (KoopaFunction::moveAndTurnKoopaToPlayer(mHost, ::sRunParam)) {
        return;
    }

    if (MR::isBindedWall(mHost)) {
        MR::addVelocityJump(mHost, ::sRunWallJumpSpeed);
    }

    if ((MR::isNearPlayer(mHost, ::sDistanceToJump) || MR::isGreaterStep(this, ::sRunStepMax)) && MR::calcGravitySpeed(mHost) >= 0.0f) {
        setNerve(GET_NERVE(KoopaStateAttackSpin, KoopaStateAttackSpinNrvStart));
    }
}

void KoopaStateAttackSpin::exeStart() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "SpinStart");
        MR::zeroVelocity(mHost);
    }

    MR::turnDirectionToPlayerDegree(mHost, &mHost->mFront, ::sStartTurnSpeed);

    if (MR::isActionEnd(mHost)) {
        setNerve(GET_NERVE(KoopaStateAttackSpin, KoopaStateAttackSpinNrvSpin));
    }
}

void KoopaStateAttackSpin::exeSpin() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "Spin");
        MR::zeroVelocity(mHost);
    }

    KoopaFunction::moveAndTurnKoopaToPlayer(mHost, MR::isLessStep(this, ::sSpinBrakeStep) ? *mMoveParam : ::sParamAttackSpinBrake);

    if (MR::isBindedWall(mHost)) {
        MR::addVelocityJump(mHost, ::sWallJumpSpeed);
    }

    if (MR::isActionEnd(mHost)) {
        mAttacks++;

        if (!MR::isPlayerDamaging() && mAttacks < mMaxAttacks) {
            mMoveParam = &::sParamAttackSpin2nd;
            setNerve(GET_NERVE(KoopaStateAttackSpin, KoopaStateAttackSpinNrvStart));
        } else {
            setNerve(GET_NERVE(KoopaStateAttackSpin, KoopaStateAttackSpinNrvEnd));
        }
    }
}

void KoopaStateAttackSpin::exeEnd() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "SpinEnd");
    }

    KoopaFunction::moveAndTurnKoopaToPlayer(mHost, ::sEndParam);

    if (MR::isActionEnd(mHost)) {
        MR::zeroVelocity(mHost);
        kill();
    }
}

void KoopaStateAttackSpin::exeEven() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "SpinEven");
        MR::calcVecFromPlayerH(&mHost->mVelocity, mHost);
        MR::scaleVelocity(mHost, 30.0f);
    }

    KoopaFunction::moveAndTurnKoopaToPlayer(mHost, ::sEvenParam);

    if (MR::isActionEnd(mHost)) {
        if (!MR::isPlayerDamaging() && mAttacks < mMaxAttacks) {
            mMoveParam = &::sParamAttackSpin2nd;
            setNerve(GET_NERVE(KoopaStateAttackSpin, KoopaStateAttackSpinNrvSpin));
        } else {
            kill();
        }
    }
}
