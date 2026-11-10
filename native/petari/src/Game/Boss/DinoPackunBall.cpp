#include "Game/Boss/DinoPackunBall.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/JointController.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_addDodgeTargetVelocity_f1, "addDodgeTargetVelocity.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f1, "attackSensor.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f2, "attackSensor.f2", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryRebound_f1, "tryRebound.f1", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f1, "exeWait.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeReverse_f1, "exeReverse.f1", 8.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_requestPunch_addVelocitySeparateHV_1, "requestPunch.addVelocitySeparateHV.1", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_requestPunch_addVelocitySeparateHV_2, "requestPunch.addVelocitySeparateHV.2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_addVelocityToGravity_1, "exeWait.addVelocityToGravity.1", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_attenuateVelocity_1, "exeWait.attenuateVelocity.1", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_reboundVelocityFromCollision_1, "exeWait.reboundVelocityFromCollision.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_reboundVelocityFromCollision_2, "exeWait.reboundVelocityFromCollision.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_reboundVelocityFromCollision_3, "exeWait.reboundVelocityFromCollision.3", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeShoot_reboundVelocityFromCollision_1, "exeShoot.reboundVelocityFromCollision.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeShoot_reboundVelocityFromCollision_2, "exeShoot.reboundVelocityFromCollision.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeShoot_reboundVelocityFromCollision_3, "exeShoot.reboundVelocityFromCollision.3", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeCharge_attenuateVelocity_1, "exeCharge.attenuateVelocity.1", 0.88f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeCharge_reboundVelocityFromCollision_1, "exeCharge.reboundVelocityFromCollision.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeCharge_reboundVelocityFromCollision_2, "exeCharge.reboundVelocityFromCollision.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeCharge_reboundVelocityFromCollision_3, "exeCharge.reboundVelocityFromCollision.3", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeReverse_attenuateVelocity_1, "exeReverse.attenuateVelocity.1", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeReverse_reboundVelocityFromCollision_1, "exeReverse.reboundVelocityFromCollision.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeReverse_reboundVelocityFromCollision_2, "exeReverse.reboundVelocityFromCollision.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeReverse_reboundVelocityFromCollision_3, "exeReverse.reboundVelocityFromCollision.3", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRebound_addVelocityToGravity_1, "exeRebound.addVelocityToGravity.1", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRebound_attenuateVelocity_1, "exeRebound.attenuateVelocity.1", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRebound_reboundVelocityFromCollision_1, "exeRebound.reboundVelocityFromCollision.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRebound_reboundVelocityFromCollision_2, "exeRebound.reboundVelocityFromCollision.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRebound_reboundVelocityFromCollision_3, "exeRebound.reboundVelocityFromCollision.3", 1.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeCharge_1, "exeCharge.1", 35);

namespace NrvDinoPackunBall {
    NEW_NERVE(DinoPackunBallNrvWait, DinoPackunBall, Wait);
    NEW_NERVE(DinoPackunBallNrvShoot, DinoPackunBall, Shoot);
    NEW_NERVE(DinoPackunBallNrvCharge, DinoPackunBall, Charge);
    NEW_NERVE(DinoPackunBallNrvReverse, DinoPackunBall, Reverse);
    NEW_NERVE(DinoPackunBallNrvRebound, DinoPackunBall, Rebound);
    NEW_NERVE(DinoPackunBallNrvLock, DinoPackunBall, Lock);
};  // namespace NrvDinoPackunBall

namespace {
    NEBULA_TUNABLE(f32, sRadius, 135.0f);
    // Steps the drag sound keeps going after the ball stops dragging.
    NEBULA_TUNABLE(s32, sDragSoundSteps, 10);
    // Shot away from the head it stays clear of it out to sDodgeFarDistance and is pushed off inside sDodgeNearDistance.
    NEBULA_TUNABLE(f32, sDodgeNearDistance, 500.0f);
    NEBULA_TUNABLE(f32, sDodgeFarDistance, 1500.0f);
    const s32 sReboundSteps = NebulaFrameClock::stepsOfSeconds(3);
};  // namespace

DinoPackunBall::DinoPackunBall(const char* pName, DinoPackun* pParent)
    : DinoPackunTailNode(pName, pParent), mWeakSensor(nullptr), mReboundVelocity(0.0f, 0.0f, 0.0f), mIsBlackDrag(false), mIsOnFire(false),
      mDragSoundStep(-1) {
    mDragEffectMtx.identity();
}

void DinoPackunBall::init(const TableIter& /*rIter*/) {
    MR::connectToSceneEnemyDecorationMovement(this);
    initBinder(sRadius, 0.0f, 0);
    initSound(6, false);
    initEffectKeeper(0, "DinoPackunBall");
    MR::setEffectHostMtx(this, "TailDrag", mDragEffectMtx);
    MR::setEffectHostMtx(this, "TailDragBlack", mDragEffectMtx);
    initNerve(GET_NERVE(DinoPackunBall, DinoPackunBallNrvWait));
    MR::onCalcGravity(this);
    MR::invalidateClipping(this);
    initHitSensor(1);
    MR::addHitSensorEnemy(this, "body", 8, sRadius, Vec3f(0.0f, 0.0f, 0.0f));
    makeActorAppeared();
}

void DinoPackunBall::control() {
    // The drag effect sits under the ball, up against gravity.
    MR::makeMtxUpNoSupport(&mDragEffectMtx, -mGravity);
    mDragEffectMtx.setTrans(mPosition + mGravity * sRadius);

    if (mDragSoundStep > 0) {
        MR::startLevelSound(this, "SE_BM_LV_D_PAKKUN_TAIL_DRAG");
        mDragSoundStep--;
    } else {
        mDragSoundStep = -1;
    }

    if (mIsOnFire) {
        MR::startLevelSound(this, "SE_BM_LV_D_PAKKUN_TAIL_FIRE");
    }
}

// The ball's turn stays the identity: only its translation moves.
void DinoPackunBall::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, Quat4f(0.0f, 0.0f, 0.0f, 1.0f));
    MR::setBaseScale(this, mScale);
}

// The tail pulls the ball while it waits, and more and more over a rebound.
void DinoPackunBall::addNodeVelocity(const Vec3f& rVel) {
    f32 rate;

    if (isNerve(GET_NERVE(DinoPackunBall, DinoPackunBallNrvWait))) {
        rate = 1.0f;
    } else if (isNerve(GET_NERVE(DinoPackunBall, DinoPackunBallNrvRebound))) {
        rate = MR::calcNerveRate(this, sReboundSteps);
    } else {
        return;
    }

    mVelocity.add(rVel * rate);
}

void DinoPackunBall::setWeakSensor(HitSensor* pSensor) {
    mWeakSensor = pSensor;
}

typedef bool (DinoPackunTailNode::*calcFunc)(Pos3f*, const JointControllerInfo&);

JointController* DinoPackunBall::createJointControllerOwn(LiveActor* pActor, const char* pJointName) {
    return MR::createJointDelegator(static_cast< DinoPackunTailNode* >(this), pActor, &DinoPackunTailNode::turnJointLocalXDir,
                                    static_cast< calcFunc >(nullptr), pJointName);
}

void DinoPackunBall::requestLockPosition() {
    MR::zeroVelocity(this);
    setNerve(GET_NERVE(DinoPackunBall, DinoPackunBallNrvLock));
}

void DinoPackunBall::requestUnLockPosition() {
    mVelocity.set(mReboundVelocity);
    setNerve(GET_NERVE(DinoPackunBall, DinoPackunBallNrvRebound));
}

const char* DinoPackunBall::getDragEffectName() const {
    return mIsBlackDrag ? "TailDragBlack" : "TailDrag";
}

// Keeps the flying ball off the weak spot (the head): closing speed is taken away, fully when near, and inside
// sDodgeNearDistance it is pushed out. Right on top of it the ball takes a random kick.
void DinoPackunBall::addDodgeTargetVelocity() {
    Vec3f away;
    away.set(mPosition - MR::getSensorPos(mWeakSensor));
    away.orthogonalize(MR::getSensorHost(mWeakSensor)->mGravity);

    f32 distance;
    MR::separateScalarAndDirection(&distance, &away, away);
    f32 awaySpeed = mVelocity.dot(away);

    if (MR::isNearZero(distance)) {
        MR::addRandomVector(&mVelocity, mVelocity, cNum_addDodgeTargetVelocity_f1);
        return;
    }

    if (awaySpeed < 0.0f) {
        f32 nearness = 1.0f - MR::normalize(distance, sDodgeNearDistance, sDodgeFarDistance);
        mVelocity.add((-away * nearness) * awaySpeed);
    }

    if (distance < sDodgeNearDistance) {
        mVelocity.add(away);
    }
}

void DinoPackunBall::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (mIsOnFire) {
        MR::sendMsgEnemyAttackFire(pReceiver, pSender);
        return;
    }

    if (isNerve(GET_NERVE(DinoPackunBall, DinoPackunBallNrvWait)) || isNerve(GET_NERVE(DinoPackunBall, DinoPackunBallNrvRebound))) {
        if (!MR::sendMsgPush(pReceiver, pSender)) {
            return;
        }

        // Gives way to the player up to 5 a step; stops moving into anything else.
        Vec3f away;
        MR::calcSensorDirectionNormalize(&away, pReceiver, pSender);
        f32 awaySpeed = mVelocity.dot(away);

        if (MR::isSensorPlayer(pReceiver)) {
            if (awaySpeed < cNum_attackSensor_f1) {
                mVelocity.add(away);
            }
        } else if (awaySpeed < 0.0f) {
            mVelocity.sub(away * awaySpeed);
        }
        return;
    }

    // Swung back into the head: hits it and bounces off at 0.3 of its speed.
    if (pReceiver == mWeakSensor && isNerve(GET_NERVE(DinoPackunBall, DinoPackunBallNrvReverse)) &&
        MR::sendArbitraryMsg(ACTMES_DINO_PACKUN_BALL_ATTACK, pReceiver, pSender)) {
        Vec3f bounceDir;
        MR::calcSensorDirection(&bounceDir, pReceiver, pSender);
        MR::normalizeOrZero(&bounceDir);
        f32 speed = mVelocity.length();
        mVelocity.set((bounceDir * speed) * cNum_attackSensor_f2);
        setNerve(GET_NERVE(DinoPackunBall, DinoPackunBallNrvRebound));
    }
}

bool DinoPackunBall::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgStarPieceReflect(msg)) {
        return true;
    }

    if (mIsOnFire) {
        return false;
    }

    if (MR::isMsgPlayerTrample(msg)) {
        return true;
    }

    if (MR::isMsgPlayerSpinAttack(msg)) {
        return requestPunch(pReceiver, pSender);
    }

    return false;
}

bool DinoPackunBall::receiveMsgEnemyAttack(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return false;
}

// A spin while it waits shoots the ball away from the player.
bool DinoPackunBall::requestPunch(HitSensor* pReceiver, HitSensor* pSender) {
    if (isNerve(GET_NERVE(DinoPackunBall, DinoPackunBallNrvWait)) && MR::sendArbitraryMsg(ACTMES_DINO_PACKUN_PUNCHED_BALL, mWeakSensor, pReceiver)) {
        MR::addVelocitySeparateHV(this, pSender, pReceiver, cNum_requestPunch_addVelocitySeparateHV_1, cNum_requestPunch_addVelocitySeparateHV_2);
        setNerve(GET_NERVE(DinoPackunBall, DinoPackunBallNrvShoot));
        return true;
    }

    return false;
}

void DinoPackunBall::setDamageFire() {
    mIsOnFire = true;
}

void DinoPackunBall::setDamageNone() {
    mIsOnFire = false;
}

// Hitting what it is bound to (the head) bounces it back off the surface at 40.
bool DinoPackunBall::tryRebound() {
    if (!MR::sendMsgToBindedSensor(ACTMES_DINO_PACKUN_BALL_ATTACK, getSensor(nullptr))) {
        return false;
    }

    Vec3f reaction(*MR::getBindedFixReactionVector(this));
    MR::normalizeOrZero(&reaction);
    mReboundVelocity.set(reaction * cNum_tryRebound_f1);
    MR::zeroVelocity(this);
    setNerve(GET_NERVE(DinoPackunBall, DinoPackunBallNrvRebound));
    return true;
}

void DinoPackunBall::exeWait() {
    MR::addVelocityToGravity(this, cNum_exeWait_addVelocityToGravity_1);
    MR::attenuateVelocity(this, cNum_exeWait_attenuateVelocity_1);
    MR::reboundVelocityFromCollision(this, cNum_exeWait_reboundVelocityFromCollision_1, cNum_exeWait_reboundVelocityFromCollision_2, cNum_exeWait_reboundVelocityFromCollision_3);

    if (MR::isBindedGround(this) && mVelocity.length() > cNum_exeWait_f1) {
        if (!mIsOnFire) {
            MR::emitEffect(this, getDragEffectName());
        }

        mDragSoundStep = sDragSoundSteps;
    } else {
        MR::deleteEffect(this, getDragEffectName());
    }
}

// Flies freely (no friction) for 30 steps or until sDodgeFarDistance from the head.
void DinoPackunBall::exeShoot() {
    if (MR::isFirstStep(this)) {
        MR::stopScene(3);
        MR::tryRumblePadStrong(this, PLAYER_1);
        MR::startBlowHitSound(this);
        MR::deleteEffect(this, getDragEffectName());
    }

    addDodgeTargetVelocity();
    MR::reboundVelocityFromCollision(this, cNum_exeShoot_reboundVelocityFromCollision_1, cNum_exeShoot_reboundVelocityFromCollision_2, cNum_exeShoot_reboundVelocityFromCollision_3);

    f32 distanceToWeakSensor = MR::getSensorPos(mWeakSensor).distance(mPosition);

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfMilliseconds(500)) || distanceToWeakSensor >= sDodgeFarDistance) {
        setNerve(GET_NERVE(DinoPackunBall, DinoPackunBallNrvCharge));
    }
}

void DinoPackunBall::exeCharge() {
    if (MR::isFirstStep(this)) {
        MR::sendArbitraryMsg(ACTMES_DINO_PACKUN_PULLED_TAIL, mWeakSensor, getSensor(nullptr));
        MR::startSound(this, "SE_BM_D_PAKKUN_TAIL_CRG_ST");
    }

    addDodgeTargetVelocity();
    MR::attenuateVelocity(this, cNum_exeCharge_attenuateVelocity_1);
    MR::reboundVelocityFromCollision(this, cNum_exeCharge_reboundVelocityFromCollision_1, cNum_exeCharge_reboundVelocityFromCollision_2, cNum_exeCharge_reboundVelocityFromCollision_3);
    MR::startLevelSound(this, "SE_BM_LV_D_PAKKUN_TAIL_CRG");

    if (MR::isGreaterStep(this, cStep_exeCharge_1)) {
        setNerve(GET_NERVE(DinoPackunBall, DinoPackunBallNrvReverse));
    }
}

// Pulled back toward the head at 8 a step.
void DinoPackunBall::exeReverse() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_BM_D_PAKKUN_TAIL_REV");
    }

    Vec3f toHead;
    toHead.set(MR::getSensorPos(mWeakSensor) - mPosition);
    MR::normalizeOrZero(&toHead);
    mVelocity.add(toHead * cNum_exeReverse_f1);
    MR::attenuateVelocity(this, cNum_exeReverse_attenuateVelocity_1);
    MR::reboundVelocityFromCollision(this, cNum_exeReverse_reboundVelocityFromCollision_1, cNum_exeReverse_reboundVelocityFromCollision_2, cNum_exeReverse_reboundVelocityFromCollision_3);
    tryRebound();
}

void DinoPackunBall::exeRebound() {
    MR::addVelocityToGravity(this, cNum_exeRebound_addVelocityToGravity_1);
    MR::attenuateVelocity(this, cNum_exeRebound_attenuateVelocity_1);
    MR::reboundVelocityFromCollision(this, cNum_exeRebound_reboundVelocityFromCollision_1, cNum_exeRebound_reboundVelocityFromCollision_2, cNum_exeRebound_reboundVelocityFromCollision_3);

    if (MR::isGreaterStep(this, sReboundSteps)) {
        setNerve(GET_NERVE(DinoPackunBall, DinoPackunBallNrvWait));
    }
}

void DinoPackunBall::exeLock() {}
