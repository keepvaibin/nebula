#include "Game/MapObj/CocoNut.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/Spine.hpp"
#include "Game/Util.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "math_types.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcSoundLevel_f1, "calcSoundLevel.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 1500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f1, "initSensor.f1", 65.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f2, "initSensor.f2", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotate_f1, "updateRotate.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateGravity_f1, "updateGravity.f1", 0.4f);
NEBULA_TUNABLE_NAMED(f32, cNum_processMove_f1, "processMove.f1", 2.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_processMove_f2, "processMove.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_processMove_f3, "processMove.f3", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_processMove_f4, "processMove.f4", 0.925f);
NEBULA_TUNABLE_NAMED(f32, cNum_processMove_f5, "processMove.f5", 0.75f);
NEBULA_TUNABLE_NAMED(f32, cNum_processMove_f6, "processMove.f6", 0.75f);
NEBULA_TUNABLE_NAMED(f32, cNum_processMove_f7, "processMove.f7", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_processMove_f8, "processMove.f8", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_processMove_f9, "processMove.f9", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_processMove_f10, "processMove.f10", 0.925f);
NEBULA_TUNABLE_NAMED(f32, cNum_reviseFrontVec_f1, "reviseFrontVec.f1", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_reviseFrontVec_f2, "reviseFrontVec.f2", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryDisappear_f1, "tryDisappear.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isValidPushedFromPlayer_f1, "isValidPushedFromPlayer.f1", 45.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isOnGround_f1, "isOnGround.f1", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPlayerAttack_f1, "receiveMsgPlayerAttack.f1", 35.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPlayerAttack_f2, "receiveMsgPlayerAttack.f2", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPlayerAttack_f3, "receiveMsgPlayerAttack.f3", 25.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPlayerAttack_f4, "receiveMsgPlayerAttack.f4", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPlayerAttack_f5, "receiveMsgPlayerAttack.f5", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPlayerAttack_f6, "receiveMsgPlayerAttack.f6", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPlayerAttack_f7, "receiveMsgPlayerAttack.f7", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPlayerAttack_f8, "receiveMsgPlayerAttack.f8", 65.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f1, "receiveOtherMsg.f1", 1.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f2, "receiveOtherMsg.f2", 18.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f3, "receiveOtherMsg.f3", 18.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f4, "receiveOtherMsg.f4", 0.7f);
NEBULA_TUNABLE_NAMED(f32, cNum_initMapToolInfo_f1, "initMapToolInfo.f1", 55.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_emitEffectSpinHit_f1, "emitEffectSpinHit.f1", 0.7f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_isValidPushedFromPlayer_1, "isValidPushedFromPlayer.1", 15);
NEBULA_TUNABLE_NAMED(s32, cStep_exeMove_1, "exeMove.1", 1);

namespace NrvCocoNut {
    NEW_NERVE(CocoNutNrvWait, CocoNut, Wait);
    NEW_NERVE(CocoNutNrvWaitOnBind, CocoNut, WaitOnBind);
    NEW_NERVE(CocoNutNrvMove, CocoNut, Move);
    NEW_NERVE(CocoNutNrvInWater, CocoNut, InWater);
    NEW_NERVE(CocoNutNrvBreak, CocoNut, Break);
    NEW_NERVE(CocoNutNrvReplaceReady, CocoNut, ReplaceReady);
};  // namespace NrvCocoNut

namespace {
    NEBULA_TUNABLE(f32, cMinSpeed, 1.5f);
    NEBULA_TUNABLE(f32, cMaxSpeed, 35.0f);
    NEBULA_TUNABLE(f32, cMaxFallSpeed, 25.0f);

    // The sound level of a speed: 100 at cMaxSpeed.
    s32 calcSoundLevel(f32 speed) {
        s32 level = cNum_calcSoundLevel_f1 * (speed / cMaxSpeed);
        return level > 100 ? 100 : level;
    }
};  // namespace

CocoNut::CocoNut(const char* pName)
    : LiveActor(pName), mMoveSpeed(), mFallSpeed(), mFront(0.0f, 0.0f, 1.0f), mRadius(55.0f), mIsBindOff(), mOffGroundStep(), mIsKicked(),
      mSpawnPosition(gZeroVec), mIsOnMoveArea(), mAreaVelocity(gZeroVec), mSphericalShadow(), mRespawnWhenOutOfView(), mIsGroupClipping(),
      mContinueRolling() {
    mRollMtx.identity();
    mSmokeMtx.identity();
    mSpinHitMtx.identity();
}

void CocoNut::init(const TableIter& rIter) {
    initMapToolInfo(rIter);
    initModel();
    MR::connectToSceneNoSilhouettedMapObjStrongLight(this);
    MR::initLightCtrl(this);
    initSensor();
    initBinder(mRadius, 0.0f, 0);
    MR::onCalcGravity(this);
    initEffect();
    initSound(6, 0);

    if (!mSphericalShadow) {
        MR::initShadowVolumeCylinder(this, mRadius);
    } else {
        MR::initShadowVolumeSphere(this, mRadius);
    }

    MR::setShadowDropLength(this, nullptr, cNum_init_f1);

    s32 groupId;
    if (MR::getPlacementClippingGroupID(rIter, &groupId)) {
        MR::setGroupClipping(this, rIter, 32);
        mIsGroupClipping = true;
    } else {
        MR::setClippingFar200m(this);
    }

    MR::tryRegisterDemoCast(this, rIter);

    if (!mSphericalShadow) {
        initNerve(GET_NERVE(CocoNut, CocoNutNrvWaitOnBind));
    } else {
        initNerve(GET_NERVE(CocoNut, CocoNutNrvWait));
    }

    makeActorAppeared();
}

void CocoNut::initAfterPlacement() {
    Pos3f placed;

    Vec3f gravity(mGravity);
    MR::makeMtxTR(&placed.mMtx[0], this);

    mFront.set< f32 >(placed.mMtx[0][2], placed.mMtx[1][2], placed.mMtx[2][2]);

    if (MR::isSameDirection(mFront, gravity)) {
        Pos3f upright;
        MR::makeMtxUpNoSupport(&upright, -gravity);
        mFront.set< f32 >(upright.mMtx[0][2], upright.mMtx[1][2], upright.mMtx[2][2]);
    }
}

void CocoNut::startClipped() {
    if (isNerve(GET_NERVE(CocoNut, CocoNutNrvReplaceReady))) {
        if (!MR::isDemoActive()) {
            mFallSpeed = 0.0f;
            mMoveSpeed = 0.0f;
            mAreaVelocity.zero();
            mVelocity.zero();
            MR::onBind(this);
            MR::onCalcGravity(this);
            MR::showModel(this);
            MR::validateHitSensors(this);

            if (!mSphericalShadow) {
                mIsBindOff = false;
                setNerve(GET_NERVE(CocoNut, CocoNutNrvWait));
            } else {
                setNerve(GET_NERVE(CocoNut, CocoNutNrvWaitOnBind));
            }
        }
    } else if (mRespawnWhenOutOfView) {
        statusToHide();
        mPosition.set(mSpawnPosition);
        setNerve(GET_NERVE(CocoNut, CocoNutNrvReplaceReady));
    }

    LiveActor::startClipped();
}

void CocoNut::hit(const Vec3f& rFront, f32 speed) {
    setFrontVec(rFront);

    if (speed < cMinSpeed) {
        mMoveSpeed = cMinSpeed;
    } else if (speed > cMaxSpeed) {
        mMoveSpeed = cMaxSpeed;
    } else {
        mMoveSpeed = speed;
    }

    if (!isNerve(GET_NERVE(CocoNut, CocoNutNrvMove))) {
        setNerve(GET_NERVE(CocoNut, CocoNutNrvMove));
    }
}

// Whether a nut at rOtherPos moving along rOtherVelocity closes on this one at rPos.
bool CocoNut::isPossibleToHit(const Vec3f& rPos, const Vec3f& rOtherPos, const Vec3f& rOtherVelocity) const {
    Vec3f toOther;
    Vec3f closing;
    Vec3f otherDir;
    Vec3f dir;

    toOther.sub(rOtherPos, rPos);

    if (MR::normalizeOrZero(&toOther)) {
        return false;
    }

    if (MR::normalizeOrZero(rOtherVelocity, &otherDir)) {
        return false;
    }

    if (isNerve(GET_NERVE(CocoNut, CocoNutNrvMove))) {
        if (MR::normalizeOrZero(mVelocity, &dir)) {
            return false;
        }

        closing.sub(otherDir, dir);

        if (MR::normalizeOrZero(&closing)) {
            return false;
        }
    } else {
        closing.set(otherDir);
    }

    return toOther.dot(closing) < 0.0f;
}

f32 CocoNut::calcMoveSpeed() const {
    return !isNerve(GET_NERVE(CocoNut, CocoNutNrvMove)) ? 0.0f : MR::max(mMoveSpeed, mAreaVelocity.length());
}

void CocoNut::initSensor() {
    initHitSensor(2);
    MR::addHitSensor(this, "body", ATYPE_COCO_NUT, 16, cNum_initSensor_f1 * mScale.x, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensor(this, "eye", ATYPE_EYE, 16, cNum_initSensor_f2 * mScale.x, Vec3f(0.0f, 0.0f, 0.0f));
}

void CocoNut::initModel() {
    initModelManagerWithAnm(getModelName(), nullptr, false);
}

void CocoNut::initEffect() {
    initEffectKeeper(0, "CocoNut");
    MR::setEffectHostMtx(this, "RollingSmoke", mSmokeMtx.toMtxPtr());
    MR::setEffectHostMtx(this, "RollingSmokeAttrWater", mSmokeMtx.toMtxPtr());
    MR::setEffectHostMtx(this, "RollingSmokeAttrSand", mSmokeMtx.toMtxPtr());
    MR::setEffectHostMtx(this, "Land", mSmokeMtx.toMtxPtr());
    MR::setEffectHostMtx(this, "LandAttrWater", mSmokeMtx.toMtxPtr());
    MR::setEffectHostMtx(this, "WaterColumn", mSmokeMtx.toMtxPtr());
    MR::setEffectHostMtx(this, getBreakEffectName(), mSmokeMtx.toMtxPtr());
    MR::setEffectHostMtx(this, "SpinHitMark", mSpinHitMtx.toMtxPtr());
}

// Rolls the model by the distance moved this frame, scaled by rate.
void CocoNut::updateRotate(f32 rate) {
    Pos3f roll;
    Vec3f dir;
    Vec3f up = -mGravity;

    if (mRadius > 0.0f && !MR::normalizeOrZero(mVelocity, &dir) && !MR::isSameDirection(dir, up)) {
        Vec3f axis = dir.cross(up);

        f32 angle = (mVelocity.length() * -cNum_updateRotate_f1 * rate) / (mRadius * MR::pi());
        roll.makeRotate(axis, MR::toRadian(angle));

        mRollMtx.concat(roll, mRollMtx);
    }
}

void CocoNut::updateGravity() {
    Vec3f fall;

    f32 accel = mIsKicked ? cNum_updateGravity_f1 : 1.0f;
    fall.set(mGravity);
    f32 fallSpeed = mFallSpeed + accel;

    mFallSpeed = fallSpeed >= cMaxFallSpeed ? cMaxFallSpeed : fallSpeed;
    fall.scale(mFallSpeed);
    mVelocity.add(fall);
}

void CocoNut::processMove() {
    Vec3f areaVelocity;
    Vec3f wallNormal;
    Vec3f front;
    Vec3f normal;

    if (isOnGround()) {
        f32 speed = calcMoveSpeed();
        bool inWater = MR::isBindedGroundWater(this);
        const Vec3f* pGroundNormal = MR::getGroundNormal(this);

        if (cNum_processMove_f1 < mFallSpeed) {
            if (mGravity.dot(*pGroundNormal) < 0.0f) {
                mFallSpeed *= -cNum_processMove_f2;
                s32 level = calcSoundLevel(speed);

                if (level > 0) {
                    if (inWater) {
                        MR::startSound(this, "SE_OJ_COCONUT_BOUND_WATER", level);
                    } else {
                        MR::startSound(this, "SE_OJ_COCONUT_BOUND", level);
                    }
                }
            }

            if (mOffGroundStep >= 10 && cNum_processMove_f3 < mFallSpeed) {
                MR::emitEffect(this, "Land");
            }
        } else {
            mFallSpeed = 0.0f;
        }

        MR::emitEffect(this, "RollingSmoke");
        s32 rollLevel = calcSoundLevel(speed);

        if (rollLevel >= 10) {
            if (inWater) {
                MR::startLevelSound(this, "SE_OJ_LV_COCONUT_ROLL_WATER", rollLevel);
            } else {
                MR::startLevelSound(this, "SE_OJ_LV_COCONUT_ROLL", rollLevel);
            }
        }

        mMoveSpeed *= cNum_processMove_f4;
        mIsOnMoveArea = MR::calcVelocityAreaMoveOnGround(&areaVelocity, this);

        if (mIsOnMoveArea) {
            mAreaVelocity.set(areaVelocity);
            mAreaVelocity.mult(cNum_processMove_f5);
        }

        mOffGroundStep = 0;
        mIsKicked = false;
        updateRotate(1.0f);
    } else {
        if (isInGroundGracePeriod()) {
            mOffGroundStep++;
        } else {
            MR::deleteEffect(this, "RollingSmoke");
        }

        updateRotate(cNum_processMove_f6);
    }

    // Off a wall: the front reflects and a fifth of the speed is lost.
    if (getWallNormal(&wallNormal) && mFront.dot(wallNormal) < 0.0f) {
        front.normalize(mFront);
        normal.normalize(wallNormal);

        mFront.scaleAdd(-cNum_processMove_f7 * front.dot(normal), normal, mFront);

        mFront.normalize();
        MR::normalize(&mFront);

        mMoveSpeed *= cNum_processMove_f8;

        MR::startSound(this, "SE_OJ_COCONUT_BOUND", static_cast< s32 >(cNum_processMove_f9 * (calcMoveSpeed() / cMaxSpeed)));
    }

    setFrontVec(mFront);
    mVelocity.set(mFront);
    mVelocity.scale(mMoveSpeed);

    if (!mIsOnMoveArea || mOffGroundStep >= 10) {
        mAreaVelocity.mult(cNum_processMove_f10);
    }

    mVelocity.add(mAreaVelocity);
    updateGravity();
}

// The front along rDir with gravity's part taken out; rDir along gravity is kept whole.
void CocoNut::setFrontVec(const Vec3f& rDir) {
    Vec3f dir;
    Vec3f gravity(mGravity);

    if (!MR::normalizeOrZero(rDir, &dir)) {
        if (MR::isSameDirection(rDir, gravity)) {
            mFront.set< f32 >(dir);
        } else {
            MR::vecKillElement(dir, gravity, &mFront);
            MR::normalize(&mFront);
        }
    }
}

bool CocoNut::tryHit(HitSensor* pOtherSensor, HitSensor* pMySensor) {
    CocoNut* pOther = static_cast< CocoNut* >(pMySensor->mHost);

    if (!isNerve(GET_NERVE(CocoNut, CocoNutNrvMove))) {
        return false;
    }

    if (calcMoveSpeed() < pOther->calcMoveSpeed()) {
        return false;
    }

    const Vec3f& rOtherSensorPos = pOtherSensor->mPosition;
    const Vec3f& rMySensorPos = pMySensor->mPosition;

    if (!pOther->isPossibleToHit(rMySensorPos, rOtherSensorPos, mVelocity)) {
        return false;
    }

    Vec3f myFront;
    Vec3f otherFront;
    f32 mySpeed;
    f32 otherSpeed;
    calcHitSpeedAndFrontVec(&mySpeed, &otherSpeed, &myFront, &otherFront, rOtherSensorPos, rMySensorPos);
    hit(myFront, mySpeed);
    pOther->hit(otherFront, otherSpeed);
    return true;
}

bool CocoNut::tryPushedFromActor(HitSensor* pOtherSensor, HitSensor* pMySensor) {
    const Vec3f& rOtherSensorPos = pOtherSensor->mPosition;
    const Vec3f& rMySensorPos = pMySensor->mPosition;

    if (mIsKicked) {
        return false;
    }

    if (isNerve(GET_NERVE(CocoNut, CocoNutNrvMove))) {
        Vec3f toOther;
        toOther.sub(rOtherSensorPos, rMySensorPos);
        MR::normalize(&toOther);

        if (0.0f < toOther.dot(mFront)) {
            return false;
        }

        Vec3f myFront;
        Vec3f otherFront;
        f32 mySpeed;
        f32 otherSpeed;
        calcHitSpeedAndFrontVec(&mySpeed, &otherSpeed, &myFront, &otherFront, rOtherSensorPos, rMySensorPos);
        hit(myFront, mySpeed);
    } else {
        if (((pOtherSensor->mRadius + pMySensor->mRadius) - rOtherSensorPos.distance(rMySensorPos)) < 0.0f) {
            return false;
        }

        Vec3f push;
        push.sub(rOtherSensorPos, rMySensorPos);

        if (MR::normalizeOrZero(&push)) {
            return false;
        }

        hit(push, cMinSpeed);
    }

    return true;
}

// A spin aims the nut at a Sambo or water-bazooka capsule within 15 degrees of its front.
void CocoNut::reviseFrontVec() {
    HitSensor* pEye = getSensor("eye");
    LiveActor* pTarget = nullptr;

    for (int i = 0; i < pEye->mSensorCount; i++) {
        HitSensor* pSensor = pEye->mSensors[i];

        if ((pSensor->isType(ATYPE_SAMBO_BODY) || pSensor->isType(ATYPE_WATER_BAZOOKA_CAPSULE)) && !MR::isDead(pSensor->mHost)) {
            pTarget = pSensor->mHost;
            break;
        }
    }

    if (pTarget == nullptr) {
        return;
    }

    Vec3f gravity(mGravity);
    Vec3f toTarget;
    Vec3f toTargetH;

    toTarget.sub(pTarget->mPosition, mPosition);

    if (!MR::isSameDirection(toTarget, gravity)) {
        MR::vecKillElement(toTarget, gravity, &toTargetH);
        MR::normalize(&toTargetH);

        if (MR::cosDegree(cNum_reviseFrontVec_f1) < toTargetH.dot(mFront)) {
            mFront.lerp(mFront, toTargetH, cNum_reviseFrontVec_f2);
        }
    }
}

void CocoNut::statusToWait() {
    mVelocity.zero();
    mFallSpeed = 0.0f;
    mMoveSpeed = 0.0f;
    mAreaVelocity.zero();
    MR::validateClipping(this);

    if (!mSphericalShadow && !MR::isBindedGroundIce(this)) {
        mIsBindOff = true;
        MR::offBind(this);
        MR::offCalcGravity(this);

        if (!isNerve(GET_NERVE(CocoNut, CocoNutNrvWait))) {
            setNerve(GET_NERVE(CocoNut, CocoNutNrvWait));
        }
    } else {
        setNerve(GET_NERVE(CocoNut, CocoNutNrvWaitOnBind));
    }
}

void CocoNut::tryMoveEnd() {
    if (isOnGround()) {
        const bool carried = mIsOnMoveArea && mOffGroundStep < 10;

        if (!carried && calcMoveSpeed() < cMinSpeed && (!mContinueRolling || !isContactWithOtherCocoNut())) {
            statusToWait();
            return;
        }
    }

    if (sendMsgToBindedSensor()) {
        setNerve(GET_NERVE(CocoNut, CocoNutNrvBreak));
        return;
    }

    if (!tryDisappear()) {
        mMoveSpeed = MR::max(mMoveSpeed, cMinSpeed);
    }
}

bool CocoNut::tryDisappear() {
    Vec3f below;
    below.scale(-cNum_tryDisappear_f1, mGravity);

    if (MR::isInWater(this, below)) {
        setNerve(GET_NERVE(CocoNut, CocoNutNrvInWater));
        return true;
    }

    if (MR::isInDeath(this, Vec3f(0.0f, 0.0f, 0.0f))) {
        setNerve(GET_NERVE(CocoNut, CocoNutNrvBreak));
        return true;
    }

    return false;
}

// A push counts when the player outruns the nut and runs within 45 degrees of the line from them to it.
bool CocoNut::isValidPushedFromPlayer(const HitSensor* pMySensor, const HitSensor* pPlayerSensor) const {
    if (mFallSpeed < 0.0f) {
        return false;
    }

    if (isNerve(GET_NERVE(CocoNut, CocoNutNrvMove)) && MR::isLessStep(this, cStep_isValidPushedFromPlayer_1)) {
        return false;
    }

    Vec3f* pPlayerVelocity = MR::getPlayerVelocity();

    if (pPlayerVelocity->squared() < mVelocity.squared()) {
        return false;
    }

    Vec3f fromPlayer;
    fromPlayer.sub(pMySensor->mPosition, pPlayerSensor->mPosition);

    if (MR::normalizeOrZero(&fromPlayer)) {
        return false;
    }

    Vec3f playerDir;

    if (MR::normalizeOrZero(*pPlayerVelocity, &playerDir)) {
        return false;
    }

    return !(fromPlayer.dot(playerDir) < MR::cosDegree(cNum_isValidPushedFromPlayer_f1));
}

// Two nuts meeting: the other leaves along the line between them, this one across it; the speed splits by the angle.
void CocoNut::calcHitSpeedAndFrontVec(f32* pMySpeed, f32* pOtherSpeed, Vec3f* pMyFront, Vec3f* pOtherFront, const Vec3f& rOtherPos,
                                      const Vec3f& rMyPos) const {
    Vec3f axis;
    Vec3f dir;

    pOtherFront->sub(rMyPos, rOtherPos);
    MR::normalize(pOtherFront);
    axis.set(mGravity);
    pMyFront->cross(*pOtherFront, axis);
    MR::normalize(pMyFront);

    if (MR::normalizeOrZero(mVelocity, &dir)) {
        dir.set(mFront);
    }

    f32 cosine = dir.dot(*pMyFront);

    if (cosine < 0.0f) {
        axis.negate();
        pMyFront->cross(*pOtherFront, axis);
        MR::normalize(pMyFront);
        cosine = dir.dot(*pMyFront);
    }

    f32 sine = MR::sqrt(1.0f - cosine * cosine);

    f32 speed = calcMoveSpeed();
    *pOtherSpeed = speed * sine;
    *pMySpeed = speed * cosine;
}

// On a floor (less than 120 degrees from gravity's opposite) while falling.
bool CocoNut::isOnGround() const {
    if (0.0f < mFallSpeed && MR::isOnGround(this)) {
        if (MR::getGroundNormal(this)->dot(mGravity) < MR::cosDegree(cNum_isOnGround_f1)) {
            return true;
        }
    }

    return false;
}

// A wall, or ground too steep to roll on.
bool CocoNut::getWallNormal(Vec3f* pNormal) const {
    if (MR::isBindedWall(this)) {
        pNormal->set(*MR::getWallNormal(this));
        return true;
    }

    if (0.0f < mFallSpeed && MR::isOnGround(this) && !isOnGround()) {
        pNormal->set(*MR::getGroundNormal(this));
        return true;
    }

    return false;
}

bool CocoNut::sendMsgToBindedSensor() {
    if (MR::isBindedGround(this)) {
        return sendMsgEnemyAttackToBindedSensor(MR::getGroundSensor(this));
    }

    if (MR::isBindedWall(this)) {
        return sendMsgEnemyAttackToBindedSensor(MR::getWallSensor(this));
    }

    if (MR::isBindedRoof(this)) {
        return sendMsgEnemyAttackToBindedSensor(MR::getRoofSensor(this));
    }

    return false;
}

bool CocoNut::sendMsgEnemyAttackToBindedSensor(HitSensor* pSensor) {
    if (mIsKicked) {
        return MR::sendMsgEnemyAttack(pSensor, getSensor("body"));
    }

    return false;
}

bool CocoNut::isValidReceiveMsg(const HitSensor* pSensor) const {
    return (isNerve(GET_NERVE(CocoNut, CocoNutNrvWait)) || isNerve(GET_NERVE(CocoNut, CocoNutNrvWaitOnBind)) ||
            isNerve(GET_NERVE(CocoNut, CocoNutNrvMove))) &&
           pSensor == getSensor("body");
}

// With the star-piece counter stopped the nuts are watermelons.
const char* CocoNut::getModelName() {
    return MR::isStarPieceCounterStop() ? "Watermelon" : "CocoNut";
}

const char* CocoNut::getBreakEffectName() {
    return MR::isStarPieceCounterStop() ? "BreakWatermelon" : "CocoNutBreak";
}

void CocoNut::calcAndSetBaseMtx() {
    mRollMtx.mMtx[0][3] = mPosition.x;
    mRollMtx.mMtx[1][3] = mPosition.y;
    mRollMtx.mMtx[2][3] = mPosition.z;

    MR::setBaseTRMtx(this, mRollMtx);

    if (isNerve(GET_NERVE(CocoNut, CocoNutNrvMove)) && MR::isOnGround(this)) {
        const Vec3f* pGroundNormal = MR::getGroundNormal(this);

        Vec3f contact;
        Vec3f down(*pGroundNormal);
        down.scale(mRadius);

        contact.sub(mPosition, down);

        if (MR::isSameDirection(*pGroundNormal, mFront)) {
            MR::makeMtxUpNoSupportPos(&mSmokeMtx, *pGroundNormal, contact);
        } else {
            MR::makeMtxUpFrontPos(&mSmokeMtx, *pGroundNormal, mFront, contact);
        }

        const f32 scale = mScale.x;
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 3; col++) {
                mSmokeMtx.mMtx[row][col] *= scale;
            }
        }
    }
}

void CocoNut::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!isValidReceiveMsg(pSender)) {
        return;
    }

    if (MR::isSensorPlayer(pReceiver) && !isValidPushedFromPlayer(pSender, pReceiver)) {
        if (!MR::isPlayerHipDropFalling()) {
            MR::sendMsgPush(pReceiver, pSender);
        }
    } else if (pReceiver->isType(ATYPE_COCO_NUT)) {
        if (MR::sendMsgPush(pReceiver, pSender)) {
            MR::startSound(this, "SE_OJ_COCONUT_HIT");
        }
    } else if (mIsKicked && isNerve(GET_NERVE(CocoNut, CocoNutNrvMove)) && MR::sendMsgToEnemyAttackBlow(pReceiver, pSender)) {
        MR::startSound(this, "SE_OJ_COCONUT_HIT");
        setNerve(GET_NERVE(CocoNut, CocoNutNrvBreak));
    } else {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool CocoNut::receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) {
    if (!isValidReceiveMsg(pReceiver)) {
        return false;
    }

    if (pSender->isType(ATYPE_COCO_NUT)) {
        return tryHit(pReceiver, pSender);
    }

    return tryPushedFromActor(pReceiver, pSender);
}

bool CocoNut::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (!isValidReceiveMsg(pReceiver)) {
        return false;
    }

    if (!MR::isMsgPlayerHitAll(msg) && !MR::isMsgPlayerTrample(msg) && !MR::isMsgPlayerHipDrop(msg) && !MR::isMsgStarPieceReflect(msg)) {
        return false;
    }

    // A spin launches the nut, a hip drop flips it high, a trample or star piece low.
    f32 moveSpeed;
    f32 fallSpeed;

    if (MR::isMsgPlayerSpinAttack(msg)) {
        moveSpeed = cNum_receiveMsgPlayerAttack_f1;
        fallSpeed = -cNum_receiveMsgPlayerAttack_f2;
        MR::startSound(this, "SE_PM_SPIN_HIT");
        MR::startSound(this, "SE_OJ_COCONUT_LAUNCH");
        emitEffectSpinHit(pSender, pReceiver);
        mIsKicked = true;
    } else if (MR::isMsgPlayerHipDrop(msg) || MR::isMsgInvincibleAttack(msg)) {
        moveSpeed = cNum_receiveMsgPlayerAttack_f3;
        fallSpeed = -cNum_receiveMsgPlayerAttack_f4;
        MR::startSound(this, "SE_OJ_COCONUT_FLIP_M");

        if (MR::isMsgPlayerHipDrop(msg)) {
            MR::sendMsgAwayJump(pSender, pReceiver);
        }
    } else {
        moveSpeed = cNum_receiveMsgPlayerAttack_f5;
        fallSpeed = -cNum_receiveMsgPlayerAttack_f6;
        MR::startSound(this, "SE_OJ_COCONUT_FLIP_S");
    }

    mMoveSpeed = moveSpeed;
    mFallSpeed = fallSpeed;
    Vec3f away;
    Vec3f awayDir;
    Vec3f shotDir;
    away.sub(pReceiver->mPosition, pSender->mPosition);

    // A star piece more than 60 degrees off the line also pushes the nut along its own path.
    if (MR::isMsgStarPieceReflect(msg) && !MR::normalizeOrZero(away, &awayDir) && !MR::normalizeOrZero(pSender->mHost->mVelocity, &shotDir)) {
        if (awayDir.dot(shotDir) < MR::cosDegree(cNum_receiveMsgPlayerAttack_f7)) {
            Vec3f shot(shotDir);
            shot.scale(cNum_receiveMsgPlayerAttack_f8);
            away.add(shot);
        }
    }

    setFrontVec(away);

    if (MR::isMsgPlayerSpinAttack(msg)) {
        reviseFrontVec();
    }

    setNerve(GET_NERVE(CocoNut, CocoNutNrvMove));
    return true;
}

bool CocoNut::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (!isValidReceiveMsg(pReceiver)) {
        return false;
    }

    if (MR::isMsgPlayerKick(msg)) {
        if (isValidPushedFromPlayer(pReceiver, pSender)) {
            Vec3f* pPlayerVelocity = MR::getPlayerVelocity();
            setFrontVec(*pPlayerVelocity);
            f32 along = mFront.dot(*pPlayerVelocity);
            f32 kickSpeed = cNum_receiveOtherMsg_f1 * along;

            mMoveSpeed = cNum_receiveOtherMsg_f2 >= kickSpeed ? cNum_receiveOtherMsg_f3 : kickSpeed;
            mFallSpeed = -(cNum_receiveOtherMsg_f4 * along);
            MR::startSound(this, "SE_OJ_COCONUT_FLIP_S");
            setNerve(GET_NERVE(CocoNut, CocoNutNrvMove));
            return true;
        }
    } else if (MR::isMsgHitmarkEmit(msg)) {
        return true;
    }

    return false;
}

void CocoNut::initMapToolInfo(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::getPlacementArg0NoInit(rIter, &mSphericalShadow);
    MR::getPlacementArg1NoInit(rIter, &mRespawnWhenOutOfView);
    MR::getPlacementArg2NoInit(rIter, &mContinueRolling);
    mRadius = cNum_initMapToolInfo_f1 * mScale.x;
    mSpawnPosition.set(mPosition);
}

void CocoNut::statusToHide() {
    mVelocity.zero();
    MR::offBind(this);
    MR::offCalcGravity(this);
    MR::hideModel(this);
    MR::invalidateHitSensors(this);
    MR::clearHitSensors(this);
    MR::deleteEffectAll(this);
}

// The mark sits 70% of the way from the other sensor to this one.
void CocoNut::emitEffectSpinHit(const HitSensor* pOtherSensor, const HitSensor* pMySensor) {
    Vec3f point;
    point.lerp(pOtherSensor->mPosition, pMySensor->mPosition, cNum_emitEffectSpinHit_f1);
    mSpinHitMtx.identity();
    mSpinHitMtx.mMtx[0][3] = point.x;
    mSpinHitMtx.mMtx[1][3] = point.y;
    mSpinHitMtx.mMtx[2][3] = point.z;
    MR::emitEffect(this, "SpinHitMark");
}

bool CocoNut::isContactWithOtherCocoNut() const {
    HitSensor* pBody = getSensor("body");

    for (int i = 0; i < pBody->mSensorCount; i++) {
        if (pBody->mSensors[i]->isType(ATYPE_COCO_NUT)) {
            return true;
        }
    }

    return false;
}

void CocoNut::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::deleteEffect(this, "RollingSmoke");
    }

    if (!mIsBindOff) {
        if (tryDisappear()) {
            return;
        }

        updateGravity();
    }

    if (!mIsBindOff && MR::isOnGround(this)) {
        statusToWait();
    }
}

void CocoNut::exeWaitOnBind() {
    if (MR::isFirstStep(this)) {
        MR::deleteEffect(this, "RollingSmoke");
    }

    if (!tryDisappear()) {
        updateGravity();

        if (MR::isOnGround(this)) {
            statusToWait();
        }
    }
}

void CocoNut::exeMove() {
    if (MR::isFirstStep(this)) {
        mOffGroundStep = 0;
        mIsOnMoveArea = false;
        mAreaVelocity.zero();
        MR::onBind(this);
        MR::onCalcGravity(this);
        MR::calcGravity(this);

        if (!mIsGroupClipping) {
            MR::invalidateClipping(this);
        }
    }

    if (mIsKicked && MR::isStep(this, cStep_exeMove_1)) {
        MR::stopScene(4);
    }

    processMove();
    tryMoveEnd();
}

void CocoNut::exeInWater() {
    if (MR::isFirstStep(this)) {
        statusToHide();
        MR::makeMtxUpNoSupportPos(&mSmokeMtx, -mGravity, mPosition);
        MR::emitEffect(this, "WaterColumn");
        MR::startSound(this, "SE_OJ_FALL_IN_WATER_M");
        MR::releaseSoundHandle(this, "SE_OJ_FALL_IN_WATER_M");
    }

    if (!MR::isEffectValid(this, "WaterColumn")) {
        mPosition.set(mSpawnPosition);
        setNerve(GET_NERVE(CocoNut, CocoNutNrvReplaceReady));
    }
}

void CocoNut::exeBreak() {
    if (MR::isFirstStep(this)) {
        statusToHide();
        MR::makeMtxUpNoSupportPos(&mSmokeMtx, -mGravity, mPosition);
        MR::emitEffect(this, getBreakEffectName());
    }

    if (!MR::isEffectValid(this, getBreakEffectName())) {
        mPosition.set(mSpawnPosition);
        setNerve(GET_NERVE(CocoNut, CocoNutNrvReplaceReady));
    }
}

void CocoNut::exeReplaceReady() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
    }
}
