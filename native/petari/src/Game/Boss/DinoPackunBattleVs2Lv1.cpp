#include "Game/Boss/DinoPackunBattleVs2Lv1.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/DinoPackun.hpp"
#include "Game/Boss/DinoPackunBall.hpp"
#include "Game/Boss/DinoPackunFire.hpp"
#include "Game/Boss/DinoPackunStateAwake.hpp"
#include "Game/Boss/DinoPackunStateDamage.hpp"
#include "Game/Boss/DinoPackunStateFire.hpp"
#include "Game/Boss/DinoPackunTail.hpp"
#include "Game/Boss/DinoPackunTrackFire.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

namespace {
    const Vec3f sShotMouthFireOffset = Vec3f(150.0f, 150.0f, 0.0f);

    // While moving the tail bends up to sMovingBendRate over sBendEaseSteps.
    const s32 sBendEaseSteps = NebulaFrameClock::stepsOfMilliseconds(500);
    const f32 sMovingBendRate = 1.5f;
    // The mouth fires on these frames of the chase animation.
    const f32 sMouthFireFrame0 = 32.0f;
    const f32 sMouthFireFrame1 = 132.0f;
    // Tail fire leaves a trail point every this far.
    const f32 sTailFireSpacing = 120.0f;
    const f32 sSightDistance = 1050.0f;
    const f32 sChaseSightDistance = 2000.0f;
};  // namespace

namespace NrvDinoPackunBattleVs2Lv1 {
    NEW_NERVE(DinoPackunBattleVs2Lv1NrvTurn, DinoPackunBattleVs2Lv1, Turn);
    NEW_NERVE_ONEND(DinoPackunBattleVs2Lv1NrvWalk, DinoPackunBattleVs2Lv1, Walk, Walk);
    NEW_NERVE(DinoPackunBattleVs2Lv1NrvFindCrazy, DinoPackunBattleVs2Lv1, FindCrazy);
    NEW_NERVE_ONEND(DinoPackunBattleVs2Lv1NrvCrazy, DinoPackunBattleVs2Lv1, Crazy, Crazy);
    NEW_NERVE_ONEND(DinoPackunBattleVs2Lv1NrvChase, DinoPackunBattleVs2Lv1, Chase, Chase);
    NEW_NERVE_ONEND(DinoPackunBattleVs2Lv1NrvAwake, DinoPackunBattleVs2Lv1, Awake, Awake);
    NEW_NERVE(DinoPackunBattleVs2Lv1NrvAttackHit, DinoPackunBattleVs2Lv1, AttackHit);
    NEW_NERVE(DinoPackunBattleVs2Lv1NrvDamage, DinoPackunBattleVs2Lv1, Damage);
};  // namespace NrvDinoPackunBattleVs2Lv1

DinoPackunBattleVs2Lv1::DinoPackunBattleVs2Lv1(DinoPackun* pPackun)
    : DinoPackunAction("ラスト", pPackun), mStateDamage(nullptr), mStateAwake(nullptr), mTrackFireHolder(nullptr), mFireHolder(nullptr),
      mStateFire(nullptr), mLastFireTailPos(0.0f, 0.0f, 0.0f), mWalkTurnSide(0), mHasFireTail(false), mHasFireMouth(false),
      mIsShortWalk(false), mIsAngry(false), mIsFireOnAwake(false), mIsCrazyOnAwake(false) {}

void DinoPackunBattleVs2Lv1::setTrackFire(DinoPackunTrackFireHolder* pHolder) {
    mTrackFireHolder = pHolder;
}

void DinoPackunBattleVs2Lv1::setFireBall(DinoPackunFireHolder* pHolder) {
    mFireHolder = pHolder;
}

void DinoPackunBattleVs2Lv1::setMiddleBattle() {
    mStateDamage->setDamageNormal();
}

void DinoPackunBattleVs2Lv1::setLastBattle() {
    mStateDamage->setDamageLast();
}

// Level 1 walks calm; level 2 walks angry with a burning tail and fires on waking; level 3 also spits fire and
// runs wild, and its damage ends the fight.
void DinoPackunBattleVs2Lv1::setBattleLevel(s32 level) {
    mHasFireTail = level >= 2;
    mHasFireMouth = level >= 3;
    mIsCrazyOnAwake = level >= 3;
    mIsAngry = level >= 2;
    mIsShortWalk = level >= 2;
    mIsFireOnAwake = level == 2;

    if (level >= 3) {
        setLastBattle();
    } else {
        setMiddleBattle();
    }
}

void DinoPackunBattleVs2Lv1::init() {
    initNerve(GET_NERVE(DinoPackunBattleVs2Lv1, DinoPackunBattleVs2Lv1NrvAwake));
    mStateDamage = new DinoPackunStateDamage(getHost());
    mStateDamage->setDamageLast();
    mStateDamage->init();
    mStateAwake = new DinoPackunStateAwake(getHost());
    mStateAwake->init();
    mStateFire = new DinoPackunStateFire(getHost());
    mStateFire->init();
}

void DinoPackunBattleVs2Lv1::appear() {
    if (MR::isDead(getHost())) {
        getHost()->makeActorAppeared();
    }

    mIsDead = false;
    if (mIsFireOnAwake) {
        mStateFire->requestFireDirect();
    }

    if (mIsCrazyOnAwake) {
        setNerve(GET_NERVE(DinoPackunBattleVs2Lv1, DinoPackunBattleVs2Lv1NrvFindCrazy));
    } else {
        setNerve(GET_NERVE(DinoPackunBattleVs2Lv1, DinoPackunBattleVs2Lv1NrvAwake));
    }
}

void DinoPackunBattleVs2Lv1::control() {
    mStateFire->update();
}

bool DinoPackunBattleVs2Lv1::isMovingNerve() const {
    return isNerve(GET_NERVE(DinoPackunBattleVs2Lv1, DinoPackunBattleVs2Lv1NrvChase)) ||
           isNerve(GET_NERVE(DinoPackunBattleVs2Lv1, DinoPackunBattleVs2Lv1NrvWalk)) ||
           isNerve(GET_NERVE(DinoPackunBattleVs2Lv1, DinoPackunBattleVs2Lv1NrvCrazy));
}

// Moving it blows the player away, turning or waking it hits; otherwise it pushes. A level 3 hit keeps running.
void DinoPackunBattleVs2Lv1::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorPlayer(pReceiver)) {
        if (isMovingNerve()) {
            MR::sendMsgEnemyAttack(pReceiver, pSender);
        }
        return;
    }

    if (isMovingNerve() && sendBlowAttackMessage(pSender, pReceiver, mStateFire->isFire())) {
        if (!mIsCrazyOnAwake) {
            setNerve(GET_NERVE(DinoPackunBattleVs2Lv1, DinoPackunBattleVs2Lv1NrvAttackHit));
        }
        return;
    }

    bool isStanding = isNerve(GET_NERVE(DinoPackunBattleVs2Lv1, DinoPackunBattleVs2Lv1NrvTurn)) ||
                      isNerve(GET_NERVE(DinoPackunBattleVs2Lv1, DinoPackunBattleVs2Lv1NrvAwake));

    if (isStanding && sendHitAttackMessage(pSender, pReceiver, mStateFire->isFire())) {
        if (!mIsCrazyOnAwake) {
            setNerve(GET_NERVE(DinoPackunBattleVs2Lv1, DinoPackunBattleVs2Lv1NrvAttackHit));
        }
    } else {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool DinoPackunBattleVs2Lv1::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgStarPieceReflect(msg)) {
        getHost()->startHitReaction();
        return true;
    }

    // A spin on the head only bounces the player off.
    if (MR::isMsgPlayerSpinAttack(msg) && MR::sendMsgEnemyAttackFlipWeakJump(pSender, pReceiver)) {
        MR::emitEffectHitBetweenSensors(getHost(), pSender, pReceiver, 0.0f, "InvalidHitMark");
    }

    return false;
}

bool DinoPackunBattleVs2Lv1::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(DinoPackunBattleVs2Lv1, DinoPackunBattleVs2Lv1NrvDamage))) {
        if (mStateDamage->receiveOtherMsg(msg, pSender, pReceiver)) {
            mStateFire->requestCool();
            return true;
        }
    } else if (mStateDamage->isDamageMessage(msg)) {
        mTrackFireHolder->killAll();
        mFireHolder->killAll();
        setNerve(GET_NERVE(DinoPackunBattleVs2Lv1, DinoPackunBattleVs2Lv1NrvDamage));
        return true;
    }

    return false;
}

bool DinoPackunBattleVs2Lv1::tryAwake() {
    if (MR::isInSightConePlayer(getHost(), getHost()->mFront, sSightDistance, 60.0f)) {
        setNerve(GET_NERVE(DinoPackunBattleVs2Lv1, DinoPackunBattleVs2Lv1NrvAwake));
        return true;
    }

    return false;
}

// -1 in odd periods of period steps, 1 in even ones.
f32 DinoPackunBattleVs2Lv1::calcWiggleSign(s32 period) const {
    return (getNerveStep() / period) % 2 != 0 ? -1.0f : 1.0f;
}

// Weaves left and right (starting to the other side each walk), then turns about at the end.
void DinoPackunBattleVs2Lv1::exeWalk() {
    if (MR::isFirstStep(this)) {
        MR::startAction(getHost(), mIsAngry ? "AngryWalk" : "Walk");
        mWalkTurnSide = mWalkTurnSide == 0;
    }

    getHost()->mTail->mBendRate = MR::calcNerveValue(this, sBendEaseSteps, 1.0f, sMovingBendRate);

    if (tryAwake()) {
        return;
    }

    s32 weavePeriod = mIsAngry ? 75 : 100;
    s32 coolSteps = mIsAngry ? 15 : 20;
    f32 turnDegree = mIsAngry ? 1.1f : 1.2f;

    MR::rotateDirectionGravityDegree(getHost(), &getHost()->mFront,
                                     (turnDegree * ((getNerveStep() / weavePeriod) % 2 == mWalkTurnSide ? -1.0f : 1.0f)));
    updateOnOffFireTail(weavePeriod, coolSteps, 20);

    if (updateWalk(mIsShortWalk ? 500 : 610, 1.0f, 50)) {
        mSide = mWalkTurnSide == 0 ? 1.0f : -1.0f;
        setNerve(GET_NERVE(DinoPackunBattleVs2Lv1, DinoPackunBattleVs2Lv1NrvTurn));
    }
}

void DinoPackunBattleVs2Lv1::endWalk() {
    getHost()->mTail->mBendRate = 1.0f;
}

void DinoPackunBattleVs2Lv1::exeTurn() {
    if (MR::isFirstStep(this)) {
        MR::startAction(getHost(), mSide > 0.0f ? "TurnRight" : "TurnLeft");
        MR::startSound(getHost(), "SE_BV_D_PAKKUN_EGG_WALK");
        MR::startSound(getHost(), "SE_BM_D_PAKKUN_SLAVER");
    }

    emitFireTail();

    if (updateTurn(60, 3.0f)) {
        setNerve(GET_NERVE(DinoPackunBattleVs2Lv1, DinoPackunBattleVs2Lv1NrvWalk));
    } else if (MR::isInSightFanPlayer(getHost(), getHost()->mFront, sSightDistance, 40.0f, 80.0f)) {
        setNerve(GET_NERVE(DinoPackunBattleVs2Lv1, DinoPackunBattleVs2Lv1NrvAwake));
    }
}

void DinoPackunBattleVs2Lv1::exeFindCrazy() {
    if (MR::isFirstStep(this)) {
        MR::startAction(getHost(), "Find");
        MR::startSound(getHost(), "SE_BV_D_PAKKUN_CHASE");
        MR::startSound(getHost(), "SE_BM_D_PAKKUN_SLAVER");
        mStateFire->requestFire();
    }

    MR::turnDirectionToPlayerDegree(getHost(), &getHost()->mFront, 3.0f);
    MR::addVelocityMoveToDirection(getHost(), getHost()->mFront, 0.0f);
    getHost()->updateNormalVelocity();
    if (MR::isActionEnd(getHost())) {
        setNerve(GET_NERVE(DinoPackunBattleVs2Lv1, DinoPackunBattleVs2Lv1NrvCrazy));
    }
}

void DinoPackunBattleVs2Lv1::startChaseAction() {
    MR::startAction(getHost(), mHasFireMouth ? "FireChase" : "Chase");
    MR::startSound(getHost(), "SE_BV_D_PAKKUN_CHASE");
    MR::startSound(getHost(), "SE_BM_D_PAKKUN_SLAVER");
    mStateFire->requestFire();
}

void DinoPackunBattleVs2Lv1::updateChaseMouthFire() {
    if (MR::checkPassBckFrame(getHost(), sMouthFireFrame0) || MR::checkPassBckFrame(getHost(), sMouthFireFrame1)) {
        emitFireMouth();
    }

    getHost()->mTail->mBendRate = MR::calcNerveValue(this, sBendEaseSteps, 1.0f, sMovingBendRate);
}

// Level 3: runs at the player for its first 60 steps and while it sees it, weaving otherwise; looks again after 240.
void DinoPackunBattleVs2Lv1::exeCrazy() {
    if (MR::isFirstStep(this)) {
        startChaseAction();
    }

    updateChaseMouthFire();

    if (MR::isLessStep(this, NebulaFrameClock::stepsOfSeconds(1)) || MR::isInSightConePlayer(getHost(), getHost()->mFront, sChaseSightDistance, 45.0f)) {
        MR::turnDirectionToPlayerDegree(getHost(), &getHost()->mFront, 1.2f);
    } else {
        MR::rotateDirectionGravityDegree(getHost(), &getHost()->mFront, (1.4f * calcWiggleSign(60)));
    }

    getHost()->updateFootPrintNerve(getNerveStep(), 46);
    updateOnOffFireTail(60, 10, 15);
    f32 rate = MR::calcNerveRate(this, NebulaFrameClock::stepsOfMilliseconds(500));
    MR::addVelocityMoveToDirection(getHost(), getHost()->mFront, (1.9f * rate));
    getHost()->updateRunVelocity();

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(4)) && MR::isInSightConePlayer(getHost(), getHost()->mFront, 1600.0f, 45.0f)) {
        setNerve(GET_NERVE(DinoPackunBattleVs2Lv1, DinoPackunBattleVs2Lv1NrvFindCrazy));
    }
}

void DinoPackunBattleVs2Lv1::endCrazy() {
    getHost()->mTail->mBendRate = 1.0f;
}

// Chases while it sees the player, weaving otherwise; walks again once it has lost it after 120 steps.
void DinoPackunBattleVs2Lv1::exeChase() {
    if (MR::isFirstStep(this)) {
        startChaseAction();
    }

    updateChaseMouthFire();

    if (MR::isInSightConePlayer(getHost(), getHost()->mFront, sChaseSightDistance, 60.0f)) {
        MR::turnDirectionToPlayerDegree(getHost(), &getHost()->mFront, 1.0f);
    } else {
        MR::rotateDirectionGravityDegree(getHost(), &getHost()->mFront, (2.0f * calcWiggleSign(60)));
    }

    getHost()->updateFootPrintNerve(getNerveStep(), 46);
    emitFireTail();
    f32 rate = MR::calcNerveRate(this, 45);
    MR::addVelocityMoveToDirection(getHost(), getHost()->mFront, (1.8f * rate));
    getHost()->updateRunVelocity();

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(2)) && !MR::isInSightConePlayer(getHost(), getHost()->mFront, sChaseSightDistance, 60.0f)) {
        setNerve(GET_NERVE(DinoPackunBattleVs2Lv1, DinoPackunBattleVs2Lv1NrvWalk));
    }
}

void DinoPackunBattleVs2Lv1::endChase() {
    getHost()->mTail->mBendRate = 1.0f;
}

void DinoPackunBattleVs2Lv1::exeAttackHit() {
    if (MR::isFirstStep(this)) {
        mStateFire->requestFire();
    }

    if (updateAttackHit()) {
        setNerve(GET_NERVE(DinoPackunBattleVs2Lv1, DinoPackunBattleVs2Lv1NrvTurn));
    }
}

// Over each 2 * period steps the tail burns for period - coolSteps, cools, and shows the sign in the last signSteps.
void DinoPackunBattleVs2Lv1::updateOnOffFireTail(s32 period, s32 coolSteps, s32 signSteps) {
    s32 cycle = 2 * period;
    s32 cycleStep = getNerveStep() % cycle;

    if (cycleStep < period - coolSteps) {
        mStateFire->requestFire();
    } else if (cycleStep >= cycle - signSteps) {
        mStateFire->requestFireSign();
    } else {
        mStateFire->requestCool();
    }

    if (mStateFire->isFire()) {
        emitFireTail();
    }
}

// Spits a fire ball from the mouth, forward at 50 and up at 30 on top of its own speed.
bool DinoPackunBattleVs2Lv1::emitFireMouth() {
    if (!mHasFireMouth) {
        return false;
    }

    DinoPackunFire* pFire = mFireHolder->getDeadMember();
    if (pFire == nullptr) {
        return false;
    }

    Pos3f mouthMtx;
    mouthMtx.set(MR::getJointMtx(getHost(), "DownMouth1"));
    Vec3f shotPos;
    mouthMtx.mult(::sShotMouthFireOffset, shotPos);
    Vec3f shotDir;
    mouthMtx.getXDir(shotDir);
    MR::normalizeOrZero(&shotDir);
    pFire->appearShot(shotPos, shotDir * 50.0f);
    MR::addVelocityJump(pFire, 30.0f);
    MR::addVelocity(pFire, getHost()->mVelocity);
    return true;
}

// Leaves a fire on the ball's trail every sTailFireSpacing.
bool DinoPackunBattleVs2Lv1::emitFireTail() {
    if (!mHasFireTail) {
        return false;
    }

    DinoPackunTrackFire* pFire = mTrackFireHolder->getDeadMember();
    if (pFire == nullptr) {
        return false;
    }

    Vec3f ballPos(getHost()->mBall->mPosition);
    if (!(mLastFireTailPos.distance(ballPos) >= sTailFireSpacing)) {
        return false;
    }

    mLastFireTailPos = ballPos;
    pFire->appearAndSetPos(ballPos);
    return true;
}

void DinoPackunBattleVs2Lv1::exeDamage() {
    if (MR::updateActorState(this, mStateDamage)) {
        kill();
    }
}

void DinoPackunBattleVs2Lv1::exeAwake() {
    MR::updateActorStateAndNextNerve(this, mStateAwake, GET_NERVE(DinoPackunBattleVs2Lv1, DinoPackunBattleVs2Lv1NrvChase));
}

void DinoPackunBattleVs2Lv1::endAwake() {
    mStateAwake->kill();
}
