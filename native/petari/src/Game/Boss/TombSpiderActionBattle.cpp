#include "Game/Boss/TombSpiderActionBattle.hpp"
#include "Game/Boss/TombSpider.hpp"
#include "Game/Boss/TombSpiderActionBase.hpp"
#include "Game/Boss/TombSpiderFunction.hpp"
#include "Game/Boss/TombSpiderStateSwoon.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/NerveUtil.hpp"

namespace {
    const s32 sChanceDamageLastStep = 5;
};  // namespace

namespace NrvTombSpiderActionBattle {
    NEW_NERVE(TombSpiderActionBattleNrvWait, TombSpiderActionBattle, Wait);
    NEW_NERVE(TombSpiderActionBattleNrvAttackStart, TombSpiderActionBattle, AttackStart);
    NEW_NERVE(TombSpiderActionBattleNrvAttackLoop, TombSpiderActionBattle, AttackLoop);
    NEW_NERVE(TombSpiderActionBattleNrvAttackEnd, TombSpiderActionBattle, AttackEnd);
    NEW_NERVE(TombSpiderActionBattleNrvDamageEye, TombSpiderActionBattle, DamageEye);
    NEW_NERVE(TombSpiderActionBattleNrvDamageHip, TombSpiderActionBattle, DamageHip);
    NEW_NERVE(TombSpiderActionBattleNrvDamageGland, TombSpiderActionBattle, DamageGland);
    NEW_NERVE(TombSpiderActionBattleNrvSwoon, TombSpiderActionBattle, Swoon);
    NEW_NERVE(TombSpiderActionBattleNrvChanceStart, TombSpiderActionBattle, ChanceStart);
    NEW_NERVE(TombSpiderActionBattleNrvChanceWait, TombSpiderActionBattle, ChanceWait);
    NEW_NERVE(TombSpiderActionBattleNrvChanceDamage, TombSpiderActionBattle, ChanceDamage);
    NEW_NERVE(TombSpiderActionBattleNrvChanceDamageLast, TombSpiderActionBattle, ChanceDamageLast);
    NEW_NERVE(TombSpiderActionBattleNrvChanceEnd, TombSpiderActionBattle, ChanceEnd);
    NEW_NERVE(TombSpiderActionBattleNrvWaitDemo, TombSpiderActionBattle, WaitDemo);
};  // namespace NrvTombSpiderActionBattle

TombSpiderActionBattle::TombSpiderActionBattle(TombSpider* pParent, const char* pName, const TombSpiderBattleParam& rParam)
    : TombSpiderActionBase(pParent, pName), mParam(rParam) {
}

void TombSpiderActionBattle::init() {
    initNerve(GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvWait));
    MR::initActorStateKeeper(this);
    MR::initActorState(this, new TombSpiderStateSwoon(mParent), GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvSwoon));
    TombSpiderActionBase::init();
}

void TombSpiderActionBattle::appear() {
    TombSpiderActionBase::appear();
    TombSpiderFunction::validateMainPartsSensor(mParent);
    TombSpiderFunction::validateGlandSensor(mParent);
}

void TombSpiderActionBattle::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    const bool isAttacker = mParam.mIsGlandAttack ? TombSpiderFunction::isSpiderAttack(pSender) : TombSpiderFunction::isEnemyAttack(pSender);

    if (isAttacker && isNerve(GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvAttackLoop))) {
        if (MR::isSensorPlayer(pReceiver) || TombSpiderFunction::isSpringAttacker(pReceiver)) {
            if (MR::sendMsgEnemyAttack(pReceiver, pSender)) {
                mHitStep = getNerveStep();
            }
        }
    }
}

bool TombSpiderActionBattle::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (msg == ACTMES_SLING_SHOOT_IS_LAST_DAMAGE) {
        return mIsDead;
    }

    if (msg == ACTMES_IS_BROKEN) {
        return !TombSpiderFunction::isSpiderBody(pReceiver);
    }

    if (msg == ACTMES_SLING_SHOOT_ATTACK) {
        if (!isNerve(GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvSwoon)) && TombSpiderFunction::tryDamageEye(pSender, pReceiver)) {
            setNerve(GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvDamageEye));
            return true;
        }

        if (TombSpiderFunction::tryDamageHip(pSender, pReceiver)) {
            setNerve(GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvDamageHip));
            return true;
        }

        if (TombSpiderFunction::tryDamageGland(mParent, pSender, pReceiver)) {
            setNerve(GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvDamageGland));
            return true;
        }

        if (isNerve(GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvChanceWait)) &&
            TombSpiderFunction::tryDamageVitalSpot(mParent, pSender, pReceiver)) {
            startChanceDamage();
            if (mEnergy <= 0) {
                setNerve(GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvChanceDamageLast));
            } else {
                setNerve(GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvChanceDamage));
            }
            return true;
        }

        if (TombSpiderFunction::isSpiderBody(pReceiver)) {
            return true;
        }
    }

    if (msg == ACTMES_SLING_SHOOT_PASS_THROUGH_ENABLE) {
        return isNerve(GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvChanceDamage));
    }

    return false;
}

void TombSpiderActionBattle::exeWait() {
    if (updateWait(mParam.mStepWaitMin, mParam.mStepWaitMax)) {
        setNerve(GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvAttackStart));
    }
}

void TombSpiderActionBattle::exeAttackStart() {
    if (updateAttackStart()) {
        setNerve(GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvAttackLoop));
    }
}

void TombSpiderActionBattle::exeAttackLoop() {
    if (updateAttackLoop(mParam.mStepAttackLoopMin, mParam.mStepAttackLoopMax, mParam.mAttackRotateAccel, mParam.mAttackRotateSpeedMax)) {
        setNerve(GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvAttackEnd));
    }
}

void TombSpiderActionBattle::exeAttackEnd() {
    if (updateAttackEnd()) {
        setNerve(GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvWait));
    }
}

void TombSpiderActionBattle::exeDamageEye() {
    if (updateDamageEye()) {
        setNerve(GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvSwoon));
    }
}

void TombSpiderActionBattle::exeDamageHip() {
    if (updateDamageHip()) {
        setNerve(GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvWaitDemo));
        if (!tryWaitChanceStartDemo(GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvChanceStart))) {
            setNerve(GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvChanceStart));
        }
    }
}

void TombSpiderActionBattle::exeDamageGland() {
    if (updateDamageGland()) {
        setNerve(GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvSwoon));
    }
}

void TombSpiderActionBattle::exeSwoon() {
    MR::updateActorStateAndNextNerve(this, GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvWait));
}

void TombSpiderActionBattle::exeChanceStart() {
    if (updateChanceStart()) {
        setNerve(GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvChanceWait));
    }
}

void TombSpiderActionBattle::exeChanceWait() {
    if (updateChanceWait(mParam.mStepChanceWait)) {
        setNerve(GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvWaitDemo));
        if (!tryWaitChanceEndDemo(GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvChanceEnd))) {
            setNerve(GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvChanceEnd));
        }
    }
}

void TombSpiderActionBattle::exeChanceDamage() {
    if (updateChanceDamage()) {
        setNerve(GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvChanceWait));
    }
}

void TombSpiderActionBattle::exeChanceDamageLast() {
    if (MR::isStep(this, ::sChanceDamageLastStep)) {
        kill();
    }
}

void TombSpiderActionBattle::exeChanceEnd() {
    if (updateChanceEnd()) {
        setNerve(GET_NERVE(TombSpiderActionBattle, TombSpiderActionBattleNrvWait));
    }
}

void TombSpiderActionBattle::exeWaitDemo() {
}
