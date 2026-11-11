#include "Game/Boss/BossKameckStateBattle.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/BossKameck.hpp"
#include "Game/Boss/BossKameckBattlePattern.hpp"
#include "Game/Boss/BossKameckMoveRail.hpp"
#include "Game/Enemy/KameckBeam.hpp"
#include "Game/Enemy/KameckBeamHolder.hpp"
#include "Game/LiveActor/ActiveActorList.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_tryAttackWait_f1, "tryAttackWait.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMove_f1, "exeMove.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMove_f2, "exeMove.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMove_f3, "exeMove.f3", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMove_f4, "exeMove.f4", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMove_f5, "exeMove.f5", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMove_f6, "exeMove.f6", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHideMove_f1, "exeHideMove.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHideMove_f2, "exeHideMove.f2", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHideMove_f3, "exeHideMove.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHideMove_f4, "exeHideMove.f4", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHideMove_f5, "exeHideMove.f5", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttackWait_f1, "exeAttackWait.f1", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttackWait_f2, "exeAttackWait.f2", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRecover_f1, "exeRecover.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRecover_f2, "exeRecover.f2", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRecover_f3, "exeRecover.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeGuard_f1, "exeGuard.f1", 0.3f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_attenuateVelocity_1, "exeWait.attenuateVelocity.1", 0.96f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMove_isNearPlayer_1, "exeMove.isNearPlayer.1", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMove_addVelocityMoveToTarget_1, "exeMove.addVelocityMoveToTarget.1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMove_addVelocityMoveToTarget_2, "exeMove.addVelocityMoveToTarget.2", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMove_addVelocityMoveToTarget_3, "exeMove.addVelocityMoveToTarget.3", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMove_addVelocityMoveToTarget_4, "exeMove.addVelocityMoveToTarget.4", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMove_addVelocityMoveToTarget_5, "exeMove.addVelocityMoveToTarget.5", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMove_attenuateVelocity_1, "exeMove.attenuateVelocity.1", 0.96f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHideMove_addVelocityMoveToTarget_1, "exeHideMove.addVelocityMoveToTarget.1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHideMove_addVelocityMoveToTarget_2, "exeHideMove.addVelocityMoveToTarget.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHideMove_addVelocityMoveToTarget_3, "exeHideMove.addVelocityMoveToTarget.3", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHideMove_attenuateVelocity_1, "exeHideMove.attenuateVelocity.1", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttackWait_addVelocityMoveToTarget_1, "exeAttackWait.addVelocityMoveToTarget.1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttackWait_addVelocityMoveToTarget_2, "exeAttackWait.addVelocityMoveToTarget.2", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttackWait_addVelocityMoveToTarget_3, "exeAttackWait.addVelocityMoveToTarget.3", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttackWait_addVelocityMoveToTarget_4, "exeAttackWait.addVelocityMoveToTarget.4", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttackWait_addVelocityMoveToTarget_5, "exeAttackWait.addVelocityMoveToTarget.5", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttackWait_attenuateVelocity_1, "exeAttackWait.attenuateVelocity.1", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttack_attenuateVelocity_1, "exeAttack.attenuateVelocity.1", 0.96f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRecover_addVelocityMoveToTarget_1, "exeRecover.addVelocityMoveToTarget.1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRecover_addVelocityMoveToTarget_2, "exeRecover.addVelocityMoveToTarget.2", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRecover_addVelocityMoveToTarget_3, "exeRecover.addVelocityMoveToTarget.3", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRecover_addVelocityMoveToTarget_4, "exeRecover.addVelocityMoveToTarget.4", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRecover_addVelocityMoveToTarget_5, "exeRecover.addVelocityMoveToTarget.5", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRecover_attenuateVelocity_1, "exeRecover.attenuateVelocity.1", 0.96f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeGuard_attenuateVelocity_1, "exeGuard.attenuateVelocity.1", 0.96f);

namespace {
    NEBULA_TUNABLE(f32, sTurnPlayerDegree, 6.0f);
    static const s32 sWaitTime = NebulaFrameClock::stepsOfSeconds(1);
    static const s32 sMoveTime = NebulaFrameClock::stepsOfMilliseconds(1500);
    NEBULA_TUNABLE(s32, sFinalMoveTime, 40);
    static const s32 sAttackWaitTime = NebulaFrameClock::stepsOfSeconds(1);
    static const s32 sAttackTime = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(s32, sShootTiming, 23);
    NEBULA_TUNABLE(f32, sBeamSpeed, 20.0f);
    static const s32 sRecoverMoveStartTime = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(s32, sSwingSeTiming, 12);
};  // namespace

namespace NrvBossKameckStateBattle {
    NEW_NERVE(BossKameckStateBattleNrvWait, BossKameckStateBattle, Wait);
    NEW_NERVE(BossKameckStateBattleNrvMove, BossKameckStateBattle, Move);
    NEW_NERVE(BossKameckStateBattleNrvHideMoveStart, BossKameckStateBattle, HideMoveStart);
    NEW_NERVE(BossKameckStateBattleNrvHideMove, BossKameckStateBattle, HideMove);
    NEW_NERVE(BossKameckStateBattleNrvHideMoveEnd, BossKameckStateBattle, HideMoveEnd);
    NEW_NERVE(BossKameckStateBattleNrvSummonKameckWait, BossKameckStateBattle, SummonKameckWait);
    NEW_NERVE(BossKameckStateBattleNrvSummonKameck, BossKameckStateBattle, SummonKameck);
    NEW_NERVE(BossKameckStateBattleNrvAttackWait, BossKameckStateBattle, AttackWait);
    NEW_NERVE(BossKameckStateBattleNrvAttack, BossKameckStateBattle, Attack);
    NEW_NERVE(BossKameckStateBattleNrvDamage, BossKameckStateBattle, Damage);
    NEW_NERVE(BossKameckStateBattleNrvRecover, BossKameckStateBattle, Recover);
    NEW_NERVE(BossKameckStateBattleNrvGuard, BossKameckStateBattle, Guard);
};  // namespace NrvBossKameckStateBattle

BossKameckStateBattle::BossKameckStateBattle(BossKameck* pBoss)
    : ActorStateBase< BossKameck >("ボスカメック戦闘状態", pBoss), mMoveRail(), mBattlePattern(), mBeam(), mBeamEventListener(),
      mMoveTarget(0.0f, 0.0f, 0.0f), mMoveRailIndex(-1), mMoveNumMax(1), mMoveNum(), mBeamNumAlone(), mIsFinal(), mIsVs2() {
}

void BossKameckStateBattle::init() {
    initNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvWait));
}

void BossKameckStateBattle::appear() {
    mIsDead = false;

    startMove();
    MR::onCalcGravity(mHost);
}

void BossKameckStateBattle::setMoveRail(BossKameckMoveRail* pMoveRail) {
    mMoveRail = pMoveRail;
}

void BossKameckStateBattle::setBattlePattern(BossKameckBattlePattern* pBattlePattern) {
    mBattlePattern = pBattlePattern;
}

void BossKameckStateBattle::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isEnableGuard() && MR::isSensorPlayer(pReceiver)) {
        if (isNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvGuard))) {
            if (MR::sendMsgEnemyAttackFlipRot(pReceiver, pSender)) {
                return;
            }
        } else if (MR::isSensorEnemyAttack(pSender)) {
            if (!MR::sendMsgEnemyAttack(pReceiver, pSender)) {
                MR::sendMsgPush(pReceiver, pSender);
            }
        }
    }
}

bool BossKameckStateBattle::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgStarPieceReflect(msg)) {
        return true;
    }

    if (MR::isMsgJetTurtleAttack(msg)) {
        return requestDamage(pSender, pReceiver);
    }

    if (MR::isMsgPlayerSpinAttack(msg)) {
        return requestGuard(pSender, pReceiver);
    }

    return false;
}

bool BossKameckStateBattle::requestDamage(HitSensor* pSender, HitSensor* pReceiver) {
    if (isEnableDamage()) {
        if (mBeam != nullptr) {
            mBeam->kill();
            mBeam = nullptr;
        }

        mHost->killAllBeam();
        MR::emitEffectHitBetweenSensors(mHost, pSender, pReceiver, 0.0f, "HitMarkNormal");
        setNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvDamage));

        return true;
    }

    return false;
}

bool BossKameckStateBattle::requestGuard(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (isEnableGuard()) {
        if (mBeam != nullptr) {
            mBeam->kill();
            mBeam = nullptr;
        }

        setNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvGuard));

        return true;
    }

    return false;
}

void BossKameckStateBattle::control() {
    mHost->updatePose();
}

bool BossKameckStateBattle::tryAttackWait() {
    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(2)) && MR::isNear(mHost, mMoveTarget, cNum_tryAttackWait_f1)) {
        if (tryChargeBeam()) {
            setNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvAttackWait));
        } else {
            startMove();
        }

        return true;
    }

    return false;
}

bool BossKameckStateBattle::trySummonKameck() {
    if (!mBattlePattern->mCanSummon) {
        return false;
    }

    if (mBeamNumAlone <= 3) {
        return false;
    }

    if (mHost->getLivingKameckNum() == 0) {
        mBeamNumAlone = 0;
        return true;
    }

    return false;
}

bool BossKameckStateBattle::tryChargeBeam() {
    if (mHost->mActorList->isFull()) {
        return false;
    }

    mBeam = MR::startFollowKameckBeam(mBattlePattern->goNextPattern(), MR::getJointMtx(mHost, "PowerStarC"), 1.0f, Vec3f(0.0f, 0.0f, 0.0f),
                                      mBeamEventListener);

    if (mBeam != nullptr) {
        mBeam->setEventListener(mHost->mBeamEventListener);

        if (mHost->getLivingKameckNum() == 0) {
            mBeamNumAlone++;
        } else {
            mBeamNumAlone = 0;
        }

        return true;
    }

    return false;
}

void BossKameckStateBattle::startMove() {
    if (mIsVs2) {
        setNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvHideMoveStart));
    } else {
        setNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvMove));
    }
}

void BossKameckStateBattle::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "Wait");
    }

    MR::attenuateVelocity(mHost, cNum_exeWait_attenuateVelocity_1);

    if (MR::isGreaterStep(this, ::sWaitTime)) {
        startMove();
    }
}

void BossKameckStateBattle::exeMove() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "Wait");
        selectPosition();
    }

    MR::startLevelSound(mHost, "SE_BM_LV_KAMECK_FLOAT", mHost->mVelocity.length() * cNum_exeMove_f1);

    s32 step = mIsFinal ? ::sFinalMoveTime : ::sMoveTime;

    if (MR::isLessEqualStep(this, step)) {
        if (MR::isNear(mHost, mMoveTarget, cNum_exeMove_f2) || MR::isNearPlayer(mMoveTarget, cNum_exeMove_isNearPlayer_1)) {
            selectPosition();
        }

        BossKameck* boss = mHost;
        MR::addVelocityAwayFromTarget(boss, *MR::getPlayerPos(), cNum_exeMove_f3, 0.0f, 0.0f, cNum_exeMove_f4);
    }

    MR::turnDirectionToPlayerDegree(mHost, &mHost->mFront, ::sTurnPlayerDegree);
    MR::addVelocityMoveToTarget(mHost, mMoveTarget, cNum_exeMove_addVelocityMoveToTarget_1 * cNum_exeMove_addVelocityMoveToTarget_2, cNum_exeMove_addVelocityMoveToTarget_3, cNum_exeMove_addVelocityMoveToTarget_4, cNum_exeMove_addVelocityMoveToTarget_5);
    MR::addVelocityKeepHeight(mHost, mMoveTarget, 0.0f, cNum_exeMove_f5, cNum_exeMove_f6);
    MR::attenuateVelocity(mHost, cNum_exeMove_attenuateVelocity_1);

    if (tryAttackWait()) {
        return;
    }
}

void BossKameckStateBattle::exeHideMoveStart() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "Hide");
        MR::startSound(mHost, "SE_BM_KAMECK_HIDE_SPIN");
    }

    MR::zeroVelocity(mHost);
    MR::turnDirectionToPlayerDegree(mHost, &mHost->mFront, ::sTurnPlayerDegree);

    if (MR::isActionEnd(mHost)) {
        MR::startSound(mHost, "SE_BM_KAMECK_HIDE_SMOKE");
        setNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvHideMove));
    }
}

void BossKameckStateBattle::exeHideMove() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "Move");
        MR::hideModelAndOnCalcAnim(mHost);
        selectPosition();
        mMoveNum = 0;
    }

    MR::startLevelSound(mHost, "SE_BM_LV_KAMECK_HIDE_MOVE");

    f32 maxSpeed;

    if (mIsFinal) {
        maxSpeed = cNum_exeHideMove_f1;
    } else {
        maxSpeed = cNum_exeHideMove_f2;
    }

    MR::addVelocityMoveToTarget(mHost, mMoveTarget, maxSpeed * cNum_exeHideMove_addVelocityMoveToTarget_1, maxSpeed, cNum_exeHideMove_addVelocityMoveToTarget_2, cNum_exeHideMove_addVelocityMoveToTarget_3);
    MR::addVelocityKeepHeight(mHost, mMoveTarget, 0.0f, cNum_exeHideMove_f3, cNum_exeHideMove_f4);
    MR::attenuateVelocity(mHost, cNum_exeHideMove_attenuateVelocity_1);
    MR::turnDirectionToPlayerDegree(mHost, &mHost->mFront, ::sTurnPlayerDegree);

    if (MR::isNear(mHost, mMoveTarget, cNum_exeHideMove_f5)) {
        if (mMoveNumMax <= mMoveNum) {
            setNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvHideMoveEnd));
        } else {
            mMoveNum++;

            if (mMoveNumMax == mMoveNum) {
                selectStoppablePosition();
            } else {
                selectPosition();
            }
        }
    }
}

void BossKameckStateBattle::exeHideMoveEnd() {
    if (MR::isFirstStep(this)) {
        MR::showModel(mHost);
        MR::startAction(mHost, "Appear");
        MR::startSound(mHost, "SE_BM_KAMECK_APPEAR_SMOKE");
        MR::startSound(mHost, "SE_BM_KAMECK_APPEAR_SPIN");
    }

    MR::zeroVelocity(mHost);
    MR::turnDirectionToPlayerDegree(mHost, &mHost->mFront, ::sTurnPlayerDegree);

    if (MR::isActionEnd(mHost)) {
        if (trySummonKameck()) {
            setNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvSummonKameckWait));
        } else if (tryChargeBeam()) {
            setNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvAttackWait));
        } else {
            startMove();
        }
    }
}

void BossKameckStateBattle::exeSummonKameckWait() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "SummonWait");
    }

    MR::zeroVelocity(mHost);
    MR::turnDirectionToPlayerDegree(mHost, &mHost->mFront, ::sTurnPlayerDegree);

    if (MR::isActionEnd(mHost)) {
        setNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvSummonKameck));
    }
}

void BossKameckStateBattle::exeSummonKameck() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "Summon");
        mHost->appearKameck();
    }

    if (MR::isActionEnd(mHost)) {
        setNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvWait));
    }
}

void BossKameckStateBattle::exeAttackWait() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "AttackWait");
    }

    MR::startLevelSound(mHost, "SE_BM_LV_KAMECK_STAFF_TURN");
    MR::turnDirectionToPlayerDegree(mHost, &mHost->mFront, ::sTurnPlayerDegree);
    MR::addVelocityMoveToTarget(mHost, mMoveTarget, cNum_exeAttackWait_addVelocityMoveToTarget_1 * cNum_exeAttackWait_addVelocityMoveToTarget_2, cNum_exeAttackWait_addVelocityMoveToTarget_3, cNum_exeAttackWait_addVelocityMoveToTarget_4, cNum_exeAttackWait_addVelocityMoveToTarget_5);
    MR::addVelocityKeepHeight(mHost, mMoveTarget, 0.0f, cNum_exeAttackWait_f1, cNum_exeAttackWait_f2);
    MR::attenuateVelocity(mHost, cNum_exeAttackWait_attenuateVelocity_1);

    if (MR::isGreaterStep(this, ::sAttackWaitTime)) {
        setNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvAttack));
    }
}

void BossKameckStateBattle::exeAttack() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "Attack");
    }

    if (MR::isStep(this, ::sSwingSeTiming)) {
        MR::startSound(mHost, "SE_BM_KAMECK_STAFF_SWING");
        MR::startSound(mHost, "SE_BV_KAMECK_STAFF_SWING");
    }

    if (MR::isStep(this, ::sShootTiming)) {
        if (mBeam != nullptr) {
            mBeam->requestShootToPlayerGround(::sBeamSpeed);
            mHost->mActorList->addActor(mBeam);
            mBeam = nullptr;
        }

        MR::startSound(mHost, "SE_BM_KAMECK_STAFF_EFFECT");
    }

    MR::attenuateVelocity(mHost, cNum_exeAttack_attenuateVelocity_1);

    if (MR::isGreaterStep(this, ::sAttackTime)) {
        setNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvWait));
    }
}

void BossKameckStateBattle::exeDamage() {
    if (MR::isFirstStep(this)) {
        MR::zeroVelocity(mHost);

        if (mIsFinal) {
            MR::stopStageBGM(30);
            MR::startAction(mHost, "Down");
            MR::startSound(mHost, "SE_BV_KAMECK_LAST_DAMAGE");
            MR::startSound(mHost, "SE_BM_KAMECK_LAST_DAMAGE");
        } else {
            MR::startAction(mHost, "Damage");
            MR::startSound(mHost, "SE_BV_KAMECK_DAMAGE");
            MR::startSound(mHost, "SE_BM_KAMECK_DAMAGE");
        }
    }

    if (MR::isActionEnd(mHost)) {
        kill();
    }
}

void BossKameckStateBattle::exeRecover() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "Recover");
        MR::startSound(mHost, "SE_BM_KAMECK_RECOVER");
    }

    if (MR::isGreaterStep(this, ::sRecoverMoveStartTime)) {
        MR::turnDirectionToPlayerDegree(mHost, &mHost->mFront, ::sTurnPlayerDegree);
        MR::addVelocityMoveToTarget(mHost, mMoveTarget, cNum_exeRecover_addVelocityMoveToTarget_1 * cNum_exeRecover_addVelocityMoveToTarget_2, cNum_exeRecover_addVelocityMoveToTarget_3, cNum_exeRecover_addVelocityMoveToTarget_4, cNum_exeRecover_addVelocityMoveToTarget_5);
        MR::addVelocityKeepHeight(mHost, mMoveTarget, 0.0f, cNum_exeRecover_f1, cNum_exeRecover_f2);
        MR::attenuateVelocity(mHost, cNum_exeRecover_attenuateVelocity_1);
    }

    if (MR::isGreaterStep(this, ::sRecoverMoveStartTime)) {
        if (MR::isNear(mHost, mMoveTarget, cNum_exeRecover_f3)) {
            MR::validateHitSensors(mHost);
            startMove();
        }
    }
}

void BossKameckStateBattle::exeGuard() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "Guard");
        MR::turnDirectionToTarget(mHost, &mHost->mFront, *MR::getPlayerPos(), cNum_exeGuard_f1);
    }

    MR::attenuateVelocity(mHost, cNum_exeGuard_attenuateVelocity_1);

    if (MR::isActionEnd(mHost)) {
        startMove();
    }
}

void BossKameckStateBattle::selectStoppablePosition() {
    // A level with no move rail of this number keeps the boss where it is.
    if (mMoveRail == nullptr) {
        mMoveTarget.set(mHost->mPosition);
        return;
    }

    mMoveRailIndex = mMoveRail->calcRandomStoppableIndex(mMoveRailIndex);
    mMoveRail->calcMovePosition(&mMoveTarget, mMoveRailIndex);
}

void BossKameckStateBattle::selectPosition() {
    if (mMoveRail == nullptr) {
        mMoveTarget.set(mHost->mPosition);
        return;
    }

    mMoveRailIndex = mMoveRail->calcRandomIndex(mMoveRailIndex);
    mMoveRail->calcMovePosition(&mMoveTarget, mMoveRailIndex);
}

bool BossKameckStateBattle::isEnableDamage() const {
    if (isNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvWait)) ||
        isNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvMove)) ||
        isNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvHideMoveStart)) ||
        isNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvHideMoveEnd)) ||
        isNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvSummonKameckWait)) ||
        isNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvSummonKameck)) ||
        isNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvAttackWait)) ||
        isNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvAttack))) {
        return true;
    }

    return false;
}

bool BossKameckStateBattle::isEnableGuard() const {
    if (isNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvWait)) ||
        isNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvMove)) ||
        isNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvHideMoveStart)) ||
        isNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvHideMoveEnd)) ||
        isNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvSummonKameckWait)) ||
        isNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvSummonKameck)) ||
        isNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvAttackWait)) ||
        isNerve(GET_NERVE(BossKameckStateBattle, BossKameckStateBattleNrvAttack))) {
        return true;
    }

    return false;
}
