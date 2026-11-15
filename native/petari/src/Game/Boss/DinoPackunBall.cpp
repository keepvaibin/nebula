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
#include "Game/System/LumaCore.hpp"

namespace NrvDinoPackunBall {
    NEW_NERVE(DinoPackunBallNrvWait, DinoPackunBall, Wait);
    NEW_NERVE(DinoPackunBallNrvShoot, DinoPackunBall, Shoot);
    NEW_NERVE(DinoPackunBallNrvCharge, DinoPackunBall, Charge);
    NEW_NERVE(DinoPackunBallNrvReverse, DinoPackunBall, Reverse);
    NEW_NERVE(DinoPackunBallNrvRebound, DinoPackunBall, Rebound);
    NEW_NERVE(DinoPackunBallNrvLock, DinoPackunBall, Lock);
};  // namespace NrvDinoPackunBall

namespace {
    const f32 sRadius = 135.0f;
    // Steps the drag sound keeps going after the ball stops dragging.
    const s32 sDragSoundSteps = 10;
    // Shot away from the head it stays clear of it out to sDodgeFarDistance and is pushed off inside sDodgeNearDistance.
    const f32 sDodgeNearDistance = 500.0f;
    const f32 sDodgeFarDistance = 1500.0f;
    const s32 sReboundSteps = NebulaFrameClock::stepsOfSeconds(3);
};  // namespace

DinoPackunBall::DinoPackunBall(const char* pName, DinoPackun* pParent)
    : DinoPackunTailNode(pName, pParent), mWeakSensor(nullptr), mReboundVelocity(0.0f, 0.0f, 0.0f), mIsBlackDrag(false), mIsOnFire(false),
      mDragSoundStep(-1) {
    lumacore::setClass(this, lumacore::kClassId< DinoPackunBall >);
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
        MR::addRandomVector(&mVelocity, mVelocity, 2.0f);
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
            if (awaySpeed < 5.0f) {
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
        mVelocity.set((bounceDir * speed) * 0.3f);
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
        MR::addVelocitySeparateHV(this, pSender, pReceiver, 120.0f, 40.0f);
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
    mReboundVelocity.set(reaction * 40.0f);
    MR::zeroVelocity(this);
    setNerve(GET_NERVE(DinoPackunBall, DinoPackunBallNrvRebound));
    return true;
}

void DinoPackunBall::exeWait() {
    MR::addVelocityToGravity(this, 1.0f);
    MR::attenuateVelocity(this, 0.9f);
    MR::reboundVelocityFromCollision(this, 0.0f, 0.0f, 1.0f);

    if (MR::isBindedGround(this) && mVelocity.length() > 5.0f) {
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
    MR::reboundVelocityFromCollision(this, 0.0f, 0.0f, 1.0f);

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
    MR::attenuateVelocity(this, 0.88f);
    MR::reboundVelocityFromCollision(this, 0.0f, 0.0f, 1.0f);
    MR::startLevelSound(this, "SE_BM_LV_D_PAKKUN_TAIL_CRG");

    if (MR::isGreaterStep(this, 35)) {
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
    mVelocity.add(toHead * 8.0f);
    MR::attenuateVelocity(this, 0.98f);
    MR::reboundVelocityFromCollision(this, 0.0f, 0.0f, 1.0f);
    tryRebound();
}

void DinoPackunBall::exeRebound() {
    MR::addVelocityToGravity(this, 1.0f);
    MR::attenuateVelocity(this, 0.9f);
    MR::reboundVelocityFromCollision(this, 0.0f, 0.0f, 1.0f);

    if (MR::isGreaterStep(this, sReboundSteps)) {
        setNerve(GET_NERVE(DinoPackunBall, DinoPackunBallNrvWait));
    }
}

void DinoPackunBall::exeLock() {}
