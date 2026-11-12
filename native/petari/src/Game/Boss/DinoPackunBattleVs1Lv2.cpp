#include "Game/Boss/DinoPackunBattleVs1Lv2.hpp"
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
    const f32 cSightDistance = 1300.0f;
    const f32 cSightDegree = 90.0f;
    const s32 cTurnSteps = NebulaFrameClock::stepsOfMilliseconds(500);
    const f32 cTurnDegree = 1.0f;
    // Turns walk, as fast as the walk.
    const f32 cWalkSpeed = 1.3f;
    const s32 cWalkSteps = NebulaFrameClock::stepsOfSeconds(3);
    const s32 cWalkFootPrintPeriod = 50;
    const s32 cChaseSteps = NebulaFrameClock::stepsOfSeconds(5);
    const f32 cChaseFaceDegree = 180.0f;
    const f32 cChaseTurnDegree = 1.3f;
    const f32 cChaseSpeed = 1.8f;
    const s32 cChaseAccelSteps = NebulaFrameClock::stepsOfMilliseconds(1500);
    const s32 cChaseFootPrintPeriod = 46;
    // A star piece hit stops the chase this long.
    const s32 cHitReactionSteps = 15;
    const s32 cCoolDownSteps = NebulaFrameClock::stepsOfMilliseconds(500);
    const s32 cFindLandStep = 14;
    const f32 cFindTurnDegree = 4.0f;
};  // namespace

namespace NrvDinoPackunBattleVs1Lv2 {
    NEW_NERVE(DinoPackunBattleVs1Lv2NrvStart, DinoPackunBattleVs1Lv2, Start);
    NEW_NERVE(DinoPackunBattleVs1Lv2NrvTurn, DinoPackunBattleVs1Lv2, Turn);
    NEW_NERVE(DinoPackunBattleVs1Lv2NrvWalk, DinoPackunBattleVs1Lv2, Walk);
    NEW_NERVE(DinoPackunBattleVs1Lv2NrvFind, DinoPackunBattleVs1Lv2, Find);
    NEW_NERVE(DinoPackunBattleVs1Lv2NrvChase, DinoPackunBattleVs1Lv2, Chase);
    NEW_NERVE(DinoPackunBattleVs1Lv2NrvCoolDown, DinoPackunBattleVs1Lv2, CoolDown);
    NEW_NERVE(DinoPackunBattleVs1Lv2NrvAttackHit, DinoPackunBattleVs1Lv2, AttackHit);
    NEW_NERVE(DinoPackunBattleVs1Lv2NrvDamage, DinoPackunBattleVs1Lv2, Damage);
};  // namespace NrvDinoPackunBattleVs1Lv2

// The first fight's angry last phases: faster, sees farther, and its walk blows the player away too.
DinoPackunBattleVs1Lv2::DinoPackunBattleVs1Lv2(DinoPackun* pHost)
    : DinoPackunAction("ラスト", pHost), mStateDamage(nullptr), mIsStartFromDemo(true) {
    initNerve(GET_NERVE(DinoPackunBattleVs1Lv2, DinoPackunBattleVs1Lv2NrvStart));
    mStateDamage = new DinoPackunStateDamage(pHost);
    mStateDamage->setDamageNormal();
}

void DinoPackunBattleVs1Lv2::appear() {
    if (MR::isDead(getHost())) {
        getHost()->makeActorAppeared();
    }

    mIsDead = false;

    if (mIsStartFromDemo) {
        setNerve(GET_NERVE(DinoPackunBattleVs1Lv2, DinoPackunBattleVs1Lv2NrvStart));
    } else {
        setNerve(GET_NERVE(DinoPackunBattleVs1Lv2, DinoPackunBattleVs1Lv2NrvFind));
    }
}

bool DinoPackunBattleVs1Lv2::isRunNerve() const {
    return isNerve(GET_NERVE(DinoPackunBattleVs1Lv2, DinoPackunBattleVs1Lv2NrvChase)) ||
           isNerve(GET_NERVE(DinoPackunBattleVs1Lv2, DinoPackunBattleVs1Lv2NrvWalk));
}

// The player is blown away by the chase and walk and hit while it turns or stands, else pushed; other actors are hit
// while it runs.
void DinoPackunBattleVs1Lv2::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorPlayer(pReceiver)) {
        if (isRunNerve()) {
            MR::sendMsgEnemyAttack(pReceiver, pSender);
        }

        return;
    }

    if (isRunNerve() && sendBlowAttackMessage(pSender, pReceiver, false)) {
        setNerve(GET_NERVE(DinoPackunBattleVs1Lv2, DinoPackunBattleVs1Lv2NrvAttackHit));
        return;
    }

    bool isHitNerve = isNerve(GET_NERVE(DinoPackunBattleVs1Lv2, DinoPackunBattleVs1Lv2NrvTurn)) ||
                      isNerve(GET_NERVE(DinoPackunBattleVs1Lv2, DinoPackunBattleVs1Lv2NrvFind)) ||
                      isNerve(GET_NERVE(DinoPackunBattleVs1Lv2, DinoPackunBattleVs1Lv2NrvCoolDown));

    if (isHitNerve && sendHitAttackMessage(pSender, pReceiver, false)) {
        setNerve(GET_NERVE(DinoPackunBattleVs1Lv2, DinoPackunBattleVs1Lv2NrvAttackHit));
        return;
    }

    MR::sendMsgPush(pReceiver, pSender);
}

// Star pieces only make it flinch; a spin bounces off.
bool DinoPackunBattleVs1Lv2::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
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

bool DinoPackunBattleVs1Lv2::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(DinoPackunBattleVs1Lv2, DinoPackunBattleVs1Lv2NrvDamage))) {
        return mStateDamage->receiveOtherMsg(msg, pSender, pReceiver);
    }

    if (mStateDamage->isDamageMessage(msg)) {
        setNerve(GET_NERVE(DinoPackunBattleVs1Lv2, DinoPackunBattleVs1Lv2NrvDamage));
        return true;
    }

    return false;
}

void DinoPackunBattleVs1Lv2::setMiddleBattle() {
    mStateDamage->setDamageNormal();
}

void DinoPackunBattleVs1Lv2::setLastBattle() {
    mStateDamage->setDamageLast();
}

void DinoPackunBattleVs1Lv2::setStartFromDemo() {
    mIsStartFromDemo = true;
}

void DinoPackunBattleVs1Lv2::setStartContinueBattle() {
    mIsStartFromDemo = false;
}

bool DinoPackunBattleVs1Lv2::tryFind() {
    if (MR::isInSightConePlayer(getHost(), getHost()->mFront, cSightDistance, cSightDegree)) {
        setNerve(GET_NERVE(DinoPackunBattleVs1Lv2, DinoPackunBattleVs1Lv2NrvFind));
        return true;
    }

    return false;
}

void DinoPackunBattleVs1Lv2::exeStart() {
    if (MR::isFirstStep(this)) {
        MR::startBck(getHost(), "Find");
    }

    if (updateStart()) {
        setNerve(GET_NERVE(DinoPackunBattleVs1Lv2, DinoPackunBattleVs1Lv2NrvChase));
    }
}

// Turns while walking on, unlike updateTurn's turn on the spot.
void DinoPackunBattleVs1Lv2::exeTurn() {
    if (MR::isFirstStep(this)) {
        MR::startAction(getHost(), "AngryWalk");
        MR::startSound(getHost(), "SE_BV_D_PAKKUN_EGG_WALK");
        MR::startSound(getHost(), "SE_BM_D_PAKKUN_SLAVER");
    }

    Vec3f side;
    MR::calcSideVec(&side, getHost());
    MR::turnDirectionDegree(getHost(), &getHost()->mFront, side * mSide, cTurnDegree);
    MR::addVelocityMoveToDirection(getHost(), getHost()->mFront, cWalkSpeed);
    getHost()->updateRunVelocity();
    getHost()->updateFootPrintNerve(getNerveStep(), cWalkFootPrintPeriod);

    if (MR::isGreaterStep(this, cTurnSteps)) {
        setNerve(GET_NERVE(DinoPackunBattleVs1Lv2, DinoPackunBattleVs1Lv2NrvWalk));
    } else {
        tryFind();
    }
}

void DinoPackunBattleVs1Lv2::exeWalk() {
    if (MR::isFirstStep(this)) {
        MR::startAction(getHost(), "AngryWalk");
    }

    if (updateWalk(cWalkSteps, cWalkSpeed, cWalkFootPrintPeriod)) {
        setNerve(GET_NERVE(DinoPackunBattleVs1Lv2, DinoPackunBattleVs1Lv2NrvTurn));
    } else {
        tryFind();
    }
}

void DinoPackunBattleVs1Lv2::exeChase() {
    if (MR::isFirstStep(this)) {
        MR::startBck(getHost(), "AngryChase");
        MR::startSound(getHost(), "SE_BV_D_PAKKUN_CHASE");
        MR::startSound(getHost(), "SE_BM_D_PAKKUN_SLAVER");
    }

    bool isHit = getHost()->isHitReaction(cHitReactionSteps);
    f32 speed = isHit ? 0.0f : cChaseSpeed;
    f32 turnDegree = isHit ? 0.0f : cChaseTurnDegree;

    if (updateChase(cChaseSteps, cChaseFaceDegree, turnDegree, speed, cChaseAccelSteps, cChaseFootPrintPeriod)) {
        setNerve(GET_NERVE(DinoPackunBattleVs1Lv2, DinoPackunBattleVs1Lv2NrvCoolDown));
    }
}

void DinoPackunBattleVs1Lv2::exeDamage() {
    if (MR::updateActorState(this, mStateDamage)) {
        kill();
    }
}

void DinoPackunBattleVs1Lv2::exeAttackHit() {
    if (updateAttackHit()) {
        setNerve(GET_NERVE(DinoPackunBattleVs1Lv2, DinoPackunBattleVs1Lv2NrvTurn));
    }
}

void DinoPackunBattleVs1Lv2::exeCoolDown() {
    if (updateCoolDown(cCoolDownSteps)) {
        setNerve(GET_NERVE(DinoPackunBattleVs1Lv2, DinoPackunBattleVs1Lv2NrvTurn));
    }
}

void DinoPackunBattleVs1Lv2::exeFind() {
    if (updateFind(cFindLandStep, cFindTurnDegree)) {
        setNerve(GET_NERVE(DinoPackunBattleVs1Lv2, DinoPackunBattleVs1Lv2NrvChase));
    }
}
