#include "Game/Boss/DinoPackunBattleVs1Lv1.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/DinoPackun.hpp"
#include "Game/Boss/DinoPackunStateDamage.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

namespace {
    const f32 cSightDistance = 1000.0f;
    const f32 cSightDegree = 80.0f;
    const s32 cTurnSteps = NebulaFrameClock::stepsOfSeconds(1);
    const f32 cTurnDegree = 1.0f;
    const s32 cWalkSteps = NebulaFrameClock::stepsOfSeconds(3);
    const f32 cWalkSpeed = 0.5f;
    const s32 cWalkFootPrintPeriod = 91;
    const s32 cChaseSteps = NebulaFrameClock::stepsOfSeconds(4);
    const f32 cChaseFaceDegree = 180.0f;
    const f32 cChaseTurnDegree = 1.0f;
    const f32 cChaseSpeed = 1.5f;
    const s32 cChaseAccelSteps = NebulaFrameClock::stepsOfSeconds(1);
    const s32 cChaseFootPrintPeriod = 46;
    // A star piece hit stops the chase this long.
    const s32 cHitReactionSteps = 15;
    const s32 cCoolDownSteps = NebulaFrameClock::stepsOfMilliseconds(1500);
    const s32 cFindLandStep = 14;
    const f32 cFindTurnDegree = 1.0f;
};  // namespace

namespace NrvDinoPackunBattleVs1Lv1 {
    NEW_NERVE(DinoPackunBattleVs1Lv1NrvStart, DinoPackunBattleVs1Lv1, Start);
    NEW_NERVE(DinoPackunBattleVs1Lv1NrvTurn, DinoPackunBattleVs1Lv1, Turn);
    NEW_NERVE(DinoPackunBattleVs1Lv1NrvWalk, DinoPackunBattleVs1Lv1, Walk);
    NEW_NERVE(DinoPackunBattleVs1Lv1NrvFind, DinoPackunBattleVs1Lv1, Find);
    NEW_NERVE(DinoPackunBattleVs1Lv1NrvChase, DinoPackunBattleVs1Lv1, Chase);
    NEW_NERVE(DinoPackunBattleVs1Lv1NrvCoolDown, DinoPackunBattleVs1Lv1, CoolDown);
    NEW_NERVE(DinoPackunBattleVs1Lv1NrvAttackHit, DinoPackunBattleVs1Lv1, AttackHit);
    NEW_NERVE(DinoPackunBattleVs1Lv1NrvDamage, DinoPackunBattleVs1Lv1, Damage);
};  // namespace NrvDinoPackunBattleVs1Lv1

// The first fight's second phase, after the egg breaks: wanders until it sees the player, then chases.
DinoPackunBattleVs1Lv1::DinoPackunBattleVs1Lv1(DinoPackun* pHost) : DinoPackunAction("２戦目", pHost), mStateDamage(nullptr) {
    initNerve(GET_NERVE(DinoPackunBattleVs1Lv1, DinoPackunBattleVs1Lv1NrvStart));
    mStateDamage = new DinoPackunStateDamage(pHost);
    mStateDamage->setDamageNormal();
}

void DinoPackunBattleVs1Lv1::appear() {
    if (MR::isDead(getHost())) {
        getHost()->makeActorAppeared();
    }

    mIsDead = false;
    setNerve(GET_NERVE(DinoPackunBattleVs1Lv1, DinoPackunBattleVs1Lv1NrvStart));
}

// The player is blown away by the chase and hit while it wanders, else pushed; other actors are hit while it moves.
void DinoPackunBattleVs1Lv1::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorPlayer(pReceiver)) {
        if (isNerve(GET_NERVE(DinoPackunBattleVs1Lv1, DinoPackunBattleVs1Lv1NrvWalk)) ||
            isNerve(GET_NERVE(DinoPackunBattleVs1Lv1, DinoPackunBattleVs1Lv1NrvChase))) {
            MR::sendMsgEnemyAttack(pReceiver, pSender);
        }

        return;
    }

    if (isNerve(GET_NERVE(DinoPackunBattleVs1Lv1, DinoPackunBattleVs1Lv1NrvChase)) && sendBlowAttackMessage(pSender, pReceiver, false)) {
        setNerve(GET_NERVE(DinoPackunBattleVs1Lv1, DinoPackunBattleVs1Lv1NrvAttackHit));
        return;
    }

    bool isWandering = isNerve(GET_NERVE(DinoPackunBattleVs1Lv1, DinoPackunBattleVs1Lv1NrvWalk)) ||
                       isNerve(GET_NERVE(DinoPackunBattleVs1Lv1, DinoPackunBattleVs1Lv1NrvTurn)) ||
                       isNerve(GET_NERVE(DinoPackunBattleVs1Lv1, DinoPackunBattleVs1Lv1NrvFind)) ||
                       isNerve(GET_NERVE(DinoPackunBattleVs1Lv1, DinoPackunBattleVs1Lv1NrvCoolDown));

    if (isWandering && sendHitAttackMessage(pSender, pReceiver, false)) {
        setNerve(GET_NERVE(DinoPackunBattleVs1Lv1, DinoPackunBattleVs1Lv1NrvAttackHit));
        return;
    }

    MR::sendMsgPush(pReceiver, pSender);
}

// Star pieces only make it flinch; a spin bounces off.
bool DinoPackunBattleVs1Lv1::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        getHost()->startHitReaction();
        return true;
    }

    if (MR::isMsgPlayerSpinAttack(msg) && MR::sendMsgEnemyAttackFlipWeakJump(pSender, pReceiver)) {
        MR::emitEffectHitBetweenSensors(getHost(), pSender, pReceiver, 0.0f, "InvalidHitMark");
    }

    return false;
}

bool DinoPackunBattleVs1Lv1::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(DinoPackunBattleVs1Lv1, DinoPackunBattleVs1Lv1NrvDamage))) {
        return mStateDamage->receiveOtherMsg(msg, pSender, pReceiver);
    }

    if (mStateDamage->isDamageMessage(msg)) {
        setNerve(GET_NERVE(DinoPackunBattleVs1Lv1, DinoPackunBattleVs1Lv1NrvDamage));
        return true;
    }

    return false;
}

bool DinoPackunBattleVs1Lv1::tryFind() {
    if (MR::isInSightConePlayer(getHost(), getHost()->mFront, cSightDistance, cSightDegree)) {
        setNerve(GET_NERVE(DinoPackunBattleVs1Lv1, DinoPackunBattleVs1Lv1NrvFind));
        return true;
    }

    return false;
}

void DinoPackunBattleVs1Lv1::exeStart() {
    if (MR::isFirstStep(this)) {
        MR::startBck(getHost(), "Find");
    }

    if (updateStart()) {
        setNerve(GET_NERVE(DinoPackunBattleVs1Lv1, DinoPackunBattleVs1Lv1NrvChase));
    }
}

void DinoPackunBattleVs1Lv1::exeTurn() {
    if (MR::isFirstStep(this)) {
        MR::startBck(getHost(), mSide > 0.0f ? "TurnRight" : "TurnLeft");
        MR::startSound(getHost(), "SE_BV_D_PAKKUN_EGG_WALK");
        MR::startSound(getHost(), "SE_BM_D_PAKKUN_SLAVER");
    }

    if (updateTurn(cTurnSteps, cTurnDegree)) {
        setNerve(GET_NERVE(DinoPackunBattleVs1Lv1, DinoPackunBattleVs1Lv1NrvWalk));
    } else {
        tryFind();
    }
}

void DinoPackunBattleVs1Lv1::exeWalk() {
    if (MR::isFirstStep(this)) {
        MR::startBck(getHost(), "Walk");
    }

    if (updateWalk(cWalkSteps, cWalkSpeed, cWalkFootPrintPeriod)) {
        setNerve(GET_NERVE(DinoPackunBattleVs1Lv1, DinoPackunBattleVs1Lv1NrvTurn));
    } else {
        tryFind();
    }
}

void DinoPackunBattleVs1Lv1::exeChase() {
    if (MR::isFirstStep(this)) {
        MR::startBck(getHost(), "Chase");
        MR::startSound(getHost(), "SE_BV_D_PAKKUN_CHASE");
        MR::startSound(getHost(), "SE_BM_D_PAKKUN_SLAVER");
    }

    bool isHit = getHost()->isHitReaction(cHitReactionSteps);
    f32 speed = isHit ? 0.0f : cChaseSpeed;
    f32 turnDegree = isHit ? 0.0f : cChaseTurnDegree;

    if (updateChase(cChaseSteps, cChaseFaceDegree, turnDegree, speed, cChaseAccelSteps, cChaseFootPrintPeriod)) {
        setNerve(GET_NERVE(DinoPackunBattleVs1Lv1, DinoPackunBattleVs1Lv1NrvCoolDown));
    }
}

void DinoPackunBattleVs1Lv1::exeDamage() {
    if (MR::updateActorState(this, mStateDamage)) {
        kill();
    }
}

void DinoPackunBattleVs1Lv1::exeAttackHit() {
    if (updateAttackHit()) {
        setNerve(GET_NERVE(DinoPackunBattleVs1Lv1, DinoPackunBattleVs1Lv1NrvTurn));
    }
}

void DinoPackunBattleVs1Lv1::exeCoolDown() {
    if (updateCoolDown(cCoolDownSteps)) {
        setNerve(GET_NERVE(DinoPackunBattleVs1Lv1, DinoPackunBattleVs1Lv1NrvTurn));
    }
}

void DinoPackunBattleVs1Lv1::exeFind() {
    if (updateFind(cFindLandStep, cFindTurnDegree)) {
        setNerve(GET_NERVE(DinoPackunBattleVs1Lv1, DinoPackunBattleVs1Lv1NrvChase));
    }
}
