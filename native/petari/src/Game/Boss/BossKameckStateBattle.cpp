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

namespace {
    const f32 sTurnPlayerDegree = 6.0f;
    static const s32 sWaitTime = NebulaFrameClock::stepsOfSeconds(1);
    static const s32 sMoveTime = NebulaFrameClock::stepsOfMilliseconds(1500);
    const s32 sFinalMoveTime = 40;
    static const s32 sAttackWaitTime = NebulaFrameClock::stepsOfSeconds(1);
    static const s32 sAttackTime = NebulaFrameClock::stepsOfMilliseconds(500);
    const s32 sShootTiming = 23;
    const f32 sBeamSpeed = 20.0f;
    static const s32 sRecoverMoveStartTime = NebulaFrameClock::stepsOfSeconds(1);
    const s32 sSwingSeTiming = 12;
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
    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(2)) && MR::isNear(mHost, mMoveTarget, 100.0f)) {
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

    MR::attenuateVelocity(mHost, 0.96f);

    if (MR::isGreaterStep(this, ::sWaitTime)) {
        startMove();
    }
}

void BossKameckStateBattle::exeMove() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "Wait");
        selectPosition();
    }

    MR::startLevelSound(mHost, "SE_BM_LV_KAMECK_FLOAT", mHost->mVelocity.length() * 100.0f);

    s32 step = mIsFinal ? ::sFinalMoveTime : ::sMoveTime;

    if (MR::isLessEqualStep(this, step)) {
        if (MR::isNear(mHost, mMoveTarget, 100.0f) || MR::isNearPlayer(mMoveTarget, 400.0f)) {
            selectPosition();
        }

        BossKameck* boss = mHost;
        MR::addVelocityAwayFromTarget(boss, *MR::getPlayerPos(), 1.5f, 0.0f, 0.0f, 500.0f);
    }

    MR::turnDirectionToPlayerDegree(mHost, &mHost->mFront, ::sTurnPlayerDegree);
    MR::addVelocityMoveToTarget(mHost, mMoveTarget, 0.1f * 0.9f, 0.9f, 0.0f, 400.0f);
    MR::addVelocityKeepHeight(mHost, mMoveTarget, 0.0f, 0.5f, 50.0f);
    MR::attenuateVelocity(mHost, 0.96f);

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
        maxSpeed = 20.0f;
    } else {
        maxSpeed = 15.0f;
    }

    MR::addVelocityMoveToTarget(mHost, mMoveTarget, maxSpeed * 0.1f, maxSpeed, 0.0f, 400.0f);
    MR::addVelocityKeepHeight(mHost, mMoveTarget, 0.0f, 0.5f, 50.0f);
    MR::attenuateVelocity(mHost, 0.9f);
    MR::turnDirectionToPlayerDegree(mHost, &mHost->mFront, ::sTurnPlayerDegree);

    if (MR::isNear(mHost, mMoveTarget, 100.0f)) {
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
    MR::addVelocityMoveToTarget(mHost, mMoveTarget, 0.1f * 0.9f, 0.9f, 0.0f, 400.0f);
    MR::addVelocityKeepHeight(mHost, mMoveTarget, 0.0f, 0.2f, 50.0f);
    MR::attenuateVelocity(mHost, 0.9f);

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

    MR::attenuateVelocity(mHost, 0.96f);

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
        MR::addVelocityMoveToTarget(mHost, mMoveTarget, 0.1f * 0.9f, 0.9f, 0.0f, 400.0f);
        MR::addVelocityKeepHeight(mHost, mMoveTarget, 0.0f, 0.5f, 50.0f);
        MR::attenuateVelocity(mHost, 0.96f);
    }

    if (MR::isGreaterStep(this, ::sRecoverMoveStartTime)) {
        if (MR::isNear(mHost, mMoveTarget, 100.0f)) {
            MR::validateHitSensors(mHost);
            startMove();
        }
    }
}

void BossKameckStateBattle::exeGuard() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "Guard");
        MR::turnDirectionToTarget(mHost, &mHost->mFront, *MR::getPlayerPos(), 0.3f);
    }

    MR::attenuateVelocity(mHost, 0.96f);

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
