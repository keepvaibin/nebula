#include "Game/Enemy/MoguStone.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ParabolicPath.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>
#include <nebula_math.h>
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sIceArcAddHeight = 200.0f;
    const s32 sThrowSteps = 100;
    const s32 sFallSteps = NebulaFrameClock::stepsOfSeconds(1);
    const s32 sIcePathSteps = 101;
};  // namespace

namespace NrvMoguStone {
    NEW_NERVE(MoguStoneNrvTaken, MoguStone, Taken);
    NEW_NERVE(MoguStoneNrvThrow, MoguStone, Throw);
    NEW_NERVE(MoguStoneNrvFall, MoguStone, Fall);
};  // namespace NrvMoguStone

MoguStone::MoguStone(const char* pName, const char* pModelName)
    : ModelObj(pName, pModelName, nullptr, -2, -2, -2, false), mPoseQuat(0.0f, 0.0f, 0.0f, 1.0f), mRotateQuat(0.0f, 0.0f, 0.0f, 1.0f), mThrowDir(0.0f, 0.0f, 1.0f), mThrowSpeed(0.0f),
      mIsThrowHorizontal(true) {
    lumacore::setClass(this, lumacore::kClassId< MoguStone >);
}

void MoguStone::init(const TableIter& rIter) {
    initBinder(40.0f * mScale.y, 0.0f, 0);
    ModelObj::init(rIter);
    initHitSensor(1);
    MR::addHitSensorEnemy(this, "body", 32, 80.0f * mScale.y, Vec3f(0.0f, 0.0f, 0.0f));
    initNerve(GET_NERVE(MoguStone, MoguStoneNrvTaken));
    MR::invalidateClipping(this);
    MR::onCalcGravity(this);
    MR::initShadowVolumeSphere(this, 50.0f * mScale.y);
    makeActorDead();
}

void MoguStone::appear() {
    LiveActor::appear();
    setNerve(GET_NERVE(MoguStone, MoguStoneNrvTaken));
}

// Thrown from `rFrom` toward `rTo`; `isOnGravity` keeps it level under gravity, otherwise it flies straight.
// `rFrom` may be this stone's own position.
void MoguStone::emit(bool isOnGravity, const Vec3f& rFrom, const Vec3f& rTo, f32 speed) {
    mThrowSpeed = speed;
    mPosition.set(rFrom);
    mVelocity.zero();
    MR::calcGravity(this);

    Vec3f toTarget(rTo);
    toTarget -= rFrom;

    if (isOnGravity) {
        MR::onCalcGravity(this);
        MR::vecKillElement(toTarget, mGravity, &mThrowDir);
        mIsThrowHorizontal = true;
    } else {
        MR::offCalcGravity(this);
        mThrowDir.set(toTarget);
        mIsThrowHorizontal = false;
    }

    MR::normalizeOrZero(&mThrowDir);
    setNerve(GET_NERVE(MoguStone, MoguStoneNrvThrow));
}

void MoguStone::breakAndKill() {
    MR::emitEffect(this, "Break");
    startBreakSound();
    kill();
}

// Flies at the throw speed, facing the way it flies; a stone breaks after the throw time.
void MoguStone::doBehavior() {
    if (MR::isFirstStep(this)) {
        mRotateQuat.set(0, 0, 0, 1);
    }

    if (mIsThrowHorizontal) {
        MR::vecKillElement(mThrowDir, mGravity, &mThrowDir);
        MR::normalizeOrZero(&mThrowDir);
    }

    Vec3f velocity(mThrowDir);
    velocity.scale(mThrowSpeed);
    mVelocity.set(velocity);

    Vec3f up(-mGravity);
    MR::makeQuatUpFront(&mPoseQuat, up, mThrowDir);

    if (MR::isGreaterStep(this, sThrowSteps)) {
        breakAndKill();
    }
}

void MoguStone::startBreakSound() {
    MR::startSound(this, "SE_EM_MOGU_SPANA_BREAK");
}

void MoguStone::startThrowLevelSound() {
    MR::startLevelSound(this, "SE_EM_LV_MOGU_SPANA_FLY");
}

// A stone dies in doBehavior at the throw time; only the ice (which does not) falls after it.
void MoguStone::exeThrow() {
    startThrowLevelSound();
    doBehavior();
    NebulaQuatMultiply(&mRotateQuat, &mPoseQuat, &mPoseQuat);
    mPoseQuat.normalize();

    if (MR::isBinded(this)) {
        breakAndKill();
        return;
    }

    if (MR::isGreaterStep(this, sThrowSteps)) {
        setNerve(GET_NERVE(MoguStone, MoguStoneNrvFall));
    }
}

void MoguStone::exeFall() {
    NebulaQuatMultiply(&mRotateQuat, &mPoseQuat, &mPoseQuat);
    mPoseQuat.normalize();
    MR::applyVelocityDampAndGravity(this, 2.0f, 0.8f, 0.98f, 0.98f, 1.0f);

    if (MR::isBinded(this) || MR::isGreaterStep(this, sFallSteps)) {
        breakAndKill();
    }
}

bool MoguStone::isTaken() const {
    return isNerve(GET_NERVE(MoguStone, MoguStoneNrvTaken));
}

void MoguStone::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (pSender == getSensor("body") && MR::isSensorPlayer(pReceiver) && MR::sendMsgEnemyAttack(pReceiver, pSender)) {
        MR::emitEffect(this, "Break");
        MR::startSound(this, "SE_BM_ICEMERAKING_STONE_BREAK");
        kill();
    }
}

void MoguStone::exeTaken() {
}

bool MoguStone::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (!isNerve(GET_NERVE(MoguStone, MoguStoneNrvThrow))) {
        return false;
    }

    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (MR::isMsgPlayerHitAll(msg) || MR::isMsgStarPieceAttack(msg)) {
        MR::emitEffect(this, "Break");
        MR::startSound(this, "SE_BM_ICEMERAKING_STONE_BREAK");
        kill();
        return true;
    }

    return false;
}

void MoguStone::calcAndSetBaseMtx() {
    Pos3f mtx;
    mtx.setQT(mPoseQuat, mPosition);
    MR::setBaseTRMtx(this, mtx);
}

ThrowingIce::ThrowingIce(const char* pName) : MoguStone(pName, "IceManIce"), mPath(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< ThrowingIce >);
    mPath = new ParabolicPath();
    mScale.set(1.3f, 1.3f, 1.3f);
}

void ThrowingIce::init(const TableIter& rIter) {
    initBinder(60.0f * mScale.y, 0.0f, 0);
    ModelObj::init(rIter);
    initHitSensor(1);
    MR::addHitSensorEnemy(this, "body", 32, 80.0f * mScale.y, Vec3f(0.0f, 0.0f, 0.0f));
    initNerve(GET_NERVE(MoguStone, MoguStoneNrvTaken));
    MR::invalidateClipping(this);
    MR::onCalcGravity(this);
    MR::initShadowVolumeSphere(this, 90.0f * mScale.y);
    makeActorDead();
}

// An arc from `rFrom` to `rTo`, up against `rGravity`, with the added height.
void ThrowingIce::emitIce(const Vec3f& rFrom, const Vec3f& rTo, f32 speed, const Vec3f& rGravity) {
    mPath->initFromUpVectorAddHeight(rFrom, rTo, -rGravity, sIceArcAddHeight);
    mThrowSpeed = speed;

    Vec3f toTarget(rTo);
    toTarget -= rFrom;
    MR::vecKillElement(toTarget, mGravity, &mThrowDir);
    MR::normalizeOrZero(&mThrowDir);
    setNerve(GET_NERVE(MoguStone, MoguStoneNrvThrow));
    MR::emitEffect(this, "Smoke");
}

// Follows the arc over the path time, tumbling about the axis across the throw.
void ThrowingIce::doBehavior() {
    if (MR::isFirstStep(this)) {
        Vec3f spinAxis = mThrowDir.cross(mGravity);
        mRotateQuat.setRotate(spinAxis, 0.25f);
    }

    f32 rate = MR::calcNerveRate(this, sIcePathSteps);
    Vec3f pathPos;
    mPath->calcPosition(&pathPos, rate);
    Vec3f step(pathPos);
    step -= mPosition;
    mVelocity.set(step);
}

void ThrowingIce::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (pSender == getSensor("body") && MR::isSensorPlayer(pReceiver) &&
        (MR::isPlayerElementModeIce() ? MR::sendMsgEnemyAttackStrong(pReceiver, pSender) : MR::sendMsgEnemyAttackFreeze(pReceiver, pSender))) {
        MR::emitEffect(this, "Break");
        MR::startSound(this, "SE_BM_ICEMERAKING_STONE_BREAK");
        MR::deleteEffect(this, "Smoke");
        kill();
    }
}

// Breaks as the stone does but never reports the hit; a spin passes through.
bool ThrowingIce::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (!isNerve(GET_NERVE(MoguStone, MoguStoneNrvThrow)) || MR::isMsgPlayerSpinAttack(msg)) {
        return false;
    }

    MoguStone::receiveMsgPlayerAttack(msg, pSender, pReceiver);
    return false;
}

void ThrowingIce::startBreakSound() {
    MR::startSound(this, "SE_BM_ICEMERAKING_STONE_BREAK");
}

void ThrowingIce::startThrowLevelSound() {
}
