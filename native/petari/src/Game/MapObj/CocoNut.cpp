#include "Game/MapObj/CocoNut.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/Spine.hpp"
#include "Game/Util.hpp"
#include "Game/Util/MathUtil.hpp"
#include "math_types.hpp"
#include <nebula_geometry.hpp>
#include <cmath>

#include "Game/Util/StringUtil.hpp"

namespace NrvCocoNut {
    NEW_NERVE(CocoNutNrvWait, CocoNut, Wait);
    NEW_NERVE(CocoNutNrvWaitOnBind, CocoNut, WaitOnBind);
    NEW_NERVE(CocoNutNrvMove, CocoNut, Move);
    NEW_NERVE(CocoNutNrvInWater, CocoNut, InWater);
    NEW_NERVE(CocoNutNrvBreak, CocoNut, Break);
    NEW_NERVE(CocoNutNrvReplaceReady, CocoNut, ReplaceReady);
};  // namespace NrvCocoNut

CocoNut::CocoNut(const char* pName)
    : LiveActor(pName), mMoveSpeed(), mFallSpeed(), mFront(0.0f, 0.0f, 1.0f), mRadius(55.0f), mIsBindOff(), mOffGroundStep(), mIsKicked(), mSpawnPosition(gZeroVec), mIsOnMoveArea(), mAreaVelocity(gZeroVec),
      mSphericalShadow(), mRespawnWhenOutOfView(), mIsGroupClipping(), mContinueRolling() {
    mRollMtx.identity();
    mSmokeMtx.identity();
    mSpinHitMtx.identity();
}

CocoNut::~CocoNut() {
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

    MR::setShadowDropLength(this, nullptr, 1500.0f);

    s32 stack_8;

    if (MR::getPlacementClippingGroupID(rIter, &stack_8)) {
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
    Pos3f stack_50;

    Vec3f gravity(mGravity);
    MR::makeMtxTR(&stack_50.mMtx[0], this);

    mFront.set< f32 >(stack_50.mMtx[0][2], stack_50.mMtx[1][2], stack_50.mMtx[2][2]);

    if (MR::isSameDirection(mFront, gravity)) {
        Pos3f stack_20;
        MR::makeMtxUpNoSupport(&stack_20, -gravity);
        mFront.set< f32 >(stack_20.mMtx[0][2], stack_20.mMtx[1][2], stack_20.mMtx[2][2]);
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

void CocoNut::hit(const Vec3f& rA1, f32 a2) {
    setFrontVec(rA1);

    f32 var_f0;

    if (a2 < 1.5f) {
        var_f0 = 1.5f;
    } else if (a2 > 35.0f) {
        var_f0 = 35.0f;
    } else {
        var_f0 = a2;
    }

    mMoveSpeed = var_f0;

    if (!isNerve(GET_NERVE(CocoNut, CocoNutNrvMove))) {
        setNerve(GET_NERVE(CocoNut, CocoNutNrvMove));
    }
}

bool CocoNut::isPossibleToHit(const Vec3f& rA1, const Vec3f& rA2, const Vec3f& rA3) const {
    Vec3f stack_2C;
    Vec3f stack_20;
    Vec3f stack_14;
    Vec3f stack_8;

    stack_2C.sub(rA2, rA1);

    if (MR::normalizeOrZero(&stack_2C)) {
        return false;
    }

    if (MR::normalizeOrZero(rA3, &stack_14)) {
        return false;
    }

    if (isNerve(GET_NERVE(CocoNut, CocoNutNrvMove))) {
        if (MR::normalizeOrZero(mVelocity, &stack_8)) {
            return false;
        }

        stack_20.sub(stack_14, stack_8);

        if (MR::normalizeOrZero(&stack_20)) {
            return false;
        }
    } else {
        stack_20.set(stack_14);
    }

    return stack_2C.dot(stack_20) < 0.0f;
}

f32 CocoNut::calcMoveSpeed() const {
    return !isNerve(GET_NERVE(CocoNut, CocoNutNrvMove)) ? 0.0f : MR::max(mMoveSpeed, mAreaVelocity.length());
}

void CocoNut::initSensor() {
    initHitSensor(2);
    MR::addHitSensor(this, "body", ATYPE_COCO_NUT, 16, 65.0f * mScale.x, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensor(this, "eye", ATYPE_EYE, 16, 2000.0f * mScale.x, Vec3f(0.0f, 0.0f, 0.0f));
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

void CocoNut::updateRotate(f32 a1) {
    Pos3f stack_38;
    Vec3f stack_2C;
    Vec3f stack_20 = -mGravity;

    if (!MR::normalizeOrZero(mVelocity, &stack_2C) && !MR::isSameDirection(stack_2C, stack_20)) {
        Vec3f stack_14 = stack_2C.cross(stack_20);

        f32 angle = (mVelocity.length() * -180.0f * a1) / (mRadius * MR::pi());
        stack_38.makeRotate(stack_14, MR::toRadian(angle));

        mRollMtx.concat(stack_38, mRollMtx);
    }
}

void CocoNut::updateGravity() {
    Vec3f stack_8;

    f32 f31 = mIsKicked ? 0.4f : 1.0f;
    stack_8.set(mGravity);
    f32 f0 = mFallSpeed + f31;

    f32 f2 = f0 >= 25.0f ? 25.0f : f0;
    mFallSpeed = f2;
    stack_8.scale((f32)(f64)f2);  // real
    mVelocity.add(stack_8);
}

// issues around MR::deleteEffect and Normalize calls
void CocoNut::processMove() {
    Vec3f stack_2C;
    Vec3f stack_20;
    Vec3f stack_14;
    Vec3f stack_8;

    if (isOnGround()) {
        f32 temp_f31 = calcMoveSpeed();
        bool temp_r31 = MR::isBindedGroundWater(this);
        const Vec3f* temp_r3_1 = MR::getGroundNormal(this);

        if (2.5f < mFallSpeed) {
            if (mGravity.dot(*temp_r3_1) < 0.0f) {
                mFallSpeed *= -0.5f;
                s32 var_r5 = 100.0f * (temp_f31 / 35.0f);

                if (var_r5 > 100) {
                    var_r5 = 100;
                }

                if (var_r5 > 0) {
                    if (temp_r31) {
                        MR::startSound(this, "SE_OJ_COCONUT_BOUND_WATER", var_r5);
                    } else {
                        MR::startSound(this, "SE_OJ_COCONUT_BOUND", var_r5);
                    }
                }
            }

            if (mOffGroundStep >= 10 && 3.0f < mFallSpeed) {
                MR::emitEffect(this, "Land");
            }
        } else {
            mFallSpeed = 0.0f;
        }

        MR::emitEffect(this, "RollingSmoke");
        s32 var_r5_2 = 100.0f * (temp_f31 / 35.0f);

        if (var_r5_2 > 100) {
            var_r5_2 = 100;
        }

        if (var_r5_2 >= 10) {
            if (temp_r31) {
                MR::startLevelSound(this, "SE_OJ_LV_COCONUT_ROLL_WATER", var_r5_2);
            } else {
                MR::startLevelSound(this, "SE_OJ_LV_COCONUT_ROLL", var_r5_2);
            }
        }

        mMoveSpeed *= 0.925f;
        mIsOnMoveArea = MR::calcVelocityAreaMoveOnGround(&stack_2C, this);

        if (mIsOnMoveArea) {
            mAreaVelocity.set(stack_2C);
            mAreaVelocity.mult(0.75f);
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

        updateRotate(0.75f);
    }

    if (getWallNormal(&stack_20) && mFront.dot(stack_20) < 0.0f) {
        stack_14.normalize(mFront);
        stack_8.normalize(stack_20);

        f32 ok2 = -2.0f * stack_14.dot(stack_8);
        mFront.scaleAdd(ok2, stack_8, mFront);

        mFront.normalize();
        MR::normalize(&mFront);

        mMoveSpeed *= 0.8f;

        MR::startSound(this, "SE_OJ_COCONUT_BOUND", 100.0f * (calcMoveSpeed() / 35.0f));
    }

    setFrontVec(mFront);
    mVelocity.set(mFront);
    mVelocity.scale(mMoveSpeed);

    bool ok = mIsOnMoveArea && mOffGroundStep < 10;

    if (!ok) {
        mAreaVelocity.mult(0.925f);
    }

    mVelocity.add(mAreaVelocity);
    updateGravity();
}

void CocoNut::setFrontVec(const Vec3f& rA1) {
    Vec3f stack_14;
    Vec3f stack_8(mGravity);

    if (!MR::normalizeOrZero(rA1, &stack_14)) {
        if (MR::isSameDirection(rA1, stack_8)) {
            mFront.set< f32 >(stack_14);
        } else {
            MR::vecKillElement(stack_14, stack_8, &mFront);
            MR::normalize(&mFront);
        }
    }
}

bool CocoNut::tryHit(HitSensor* pOtherSensor, HitSensor* pMySensor) {
    CocoNut* nut = static_cast< CocoNut* >(pMySensor->mHost);

    if (!isNerve(GET_NERVE(CocoNut, CocoNutNrvMove))) {
        return false;
    }

    f32 moveSpeed = nut->calcMoveSpeed();

    if (calcMoveSpeed() < moveSpeed) {
        return false;
    }

    Vec3f* otherSensorPos = &pOtherSensor->mPosition;
    Vec3f* mySensorPos = &pMySensor->mPosition;

    if (!nut->isPossibleToHit(*mySensorPos, *otherSensorPos, mVelocity)) {
        return false;
    }

    Vec3f stack_1C;
    Vec3f stack_10;
    f32 stack_C;
    f32 stack_8;
    calcHitSpeedAndFrontVec(&stack_C, &stack_8, &stack_1C, &stack_10, *otherSensorPos, *mySensorPos);
    hit(stack_1C, stack_C);
    nut->hit(stack_10, stack_8);
    return true;
}

bool CocoNut::tryPushedFromActor(HitSensor* pOtherSensor, HitSensor* pMySensor) {
    Vec3f stack_34;
    Vec3f stack_28;
    Vec3f stack_1C;
    Vec3f stack_10;
    f32 stack_C;
    f32 stack_8;

    Vec3f* otherSensorPos = &pOtherSensor->mPosition;
    Vec3f* mySensorPos = &pMySensor->mPosition;

    if (mIsKicked) {
        return false;
    }

    if (isNerve(GET_NERVE(CocoNut, CocoNutNrvMove))) {
        stack_34.sub(*otherSensorPos, *mySensorPos);
        MR::normalize(&stack_34);

        if (0.0f < stack_34.dot(mFront)) {
            return false;
        }

        calcHitSpeedAndFrontVec(&stack_C, &stack_8, &stack_28, &stack_1C, *otherSensorPos, *mySensorPos);
        hit(stack_28, stack_C);
    } else {
        f32 mySensorRadius = pMySensor->mRadius;
        f32 otherSensorRadius = pOtherSensor->mRadius;

        if (((otherSensorRadius + mySensorRadius) - otherSensorPos->distance(*mySensorPos)) < 0.0f) {
            return false;
        }

        stack_10.sub(*otherSensorPos, *mySensorPos);

        if (MR::normalizeOrZero(&stack_10)) {
            return false;
        }

        hit(stack_10, 1.5f);
    }

    return true;
}

void CocoNut::reviseFrontVec() {
    HitSensor* sensor;

    HitSensor* eye = getSensor("eye");
    LiveActor* found_actor = nullptr;

    for (int i = 0; i < eye->mSensorCount; i++) {
        sensor = eye->mSensors[i];

        if ((sensor->isType(ATYPE_SAMBO_BODY) || sensor->isType(ATYPE_WATER_BAZOOKA_CAPSULE)) && !MR::isDead(sensor->mHost)) {
            found_actor = sensor->mHost;
            break;
        }
    }

    if (found_actor == nullptr) {
        return;
    }

    Vec3f stack_20(mGravity);
    Vec3f stack_14;
    Vec3f stack_8;

    stack_14.sub(found_actor->mPosition, this->mPosition);

    if (!MR::isSameDirection(stack_14, stack_20)) {
        MR::vecKillElement(stack_14, stack_20, &stack_8);
        MR::normalize(&stack_8);
        f32 temp_f31 = stack_8.dot(mFront);

        if (MR::cosDegree(15.0f) < temp_f31) {
            mFront.lerp(mFront, stack_8, 0.8f);
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
        bool var_r3 = mIsOnMoveArea && mOffGroundStep < 10;

        if (!var_r3 && calcMoveSpeed() < 1.5f && (!mContinueRolling || !isContactWithOtherCocoNut())) {
            statusToWait();
            return;
        }
    }

    if (sendMsgToBindedSensor()) {
        setNerve(GET_NERVE(CocoNut, CocoNutNrvBreak));
        return;
    }

    if (!tryDisappear()) {
        mMoveSpeed = MR::max(mMoveSpeed, 1.5f);
    }
}

bool CocoNut::tryDisappear() {
    Vec3f stack_14;
    stack_14.scale(-100.0f, mGravity);

    if (MR::isInWater(this, stack_14)) {
        setNerve(GET_NERVE(CocoNut, CocoNutNrvInWater));
        return true;
    }

    if (MR::isInDeath(this, Vec3f(0.0f, 0.0f, 0.0f))) {
        setNerve(GET_NERVE(CocoNut, CocoNutNrvBreak));
        return true;
    }

    return false;
}

bool CocoNut::isValidPushedFromPlayer(const HitSensor* pArg0, const HitSensor* pArg1) const {
    if (mFallSpeed < 0.0f) {
        return false;
    }

    if (isNerve(GET_NERVE(CocoNut, CocoNutNrvMove)) && MR::isLessStep(this, 15)) {
        return false;
    }

    Vec3f* playerVelocity = MR::getPlayerVelocity();
    f32 nutVelocitySquared = mVelocity.squared();

    if (playerVelocity->squared() < nutVelocitySquared) {
        return false;
    }

    Vec3f stack_14;
    stack_14.sub(pArg0->mPosition, pArg1->mPosition);

    if (MR::normalizeOrZero(&stack_14)) {
        return false;
    }

    Vec3f stack_8;

    if (MR::normalizeOrZero(*playerVelocity, &stack_8)) {
        return false;
    }

    if (stack_14.dot(stack_8) < MR::cosDegree(45.0f)) {
        return false;
    }

    return true;
}

// the frsqrte is most likely an inlined function. possibly FloatUtil::sqrt
void CocoNut::calcHitSpeedAndFrontVec(f32* pArg0, f32* pArg1, Vec3f* pArg2, Vec3f* pArg3, const Vec3f& rArg4, const Vec3f& rArg5) const {
    Vec3f stack_14;
    Vec3f stack_8;

    pArg3->sub(rArg5, rArg4);
    MR::normalize(pArg3);
    stack_14.set(mGravity);
    pArg2->cross(*pArg3, stack_14);
    MR::normalize(pArg2);

    if (MR::normalizeOrZero(mVelocity, &stack_8)) {
        stack_8.set(mFront);
    }

    f32 var_f30 = stack_8.dot(*pArg2);

    if (var_f30 < 0.0f) {
        stack_14.negate();
        pArg2->cross(*pArg3, stack_14);
        MR::normalize(pArg2);
        var_f30 = stack_8.dot(*pArg2);
    }

    f32 var_f31 = MR::sqrt(1.0f - var_f30 * var_f30);

    f32 temp_f1_2 = calcMoveSpeed();
    *pArg1 = temp_f1_2 * var_f31;
    *pArg0 = temp_f1_2 * var_f30;
}

bool CocoNut::isOnGround() const {
    if (0.0f < mFallSpeed && MR::isOnGround(this)) {
        const Vec3f* groundNormal = MR::getGroundNormal(this);

        if (groundNormal->dot(mGravity) < MR::cosDegree(120.0f)) {
            return true;
        }
    }

    return false;
}

bool CocoNut::getWallNormal(Vec3f* pArg0) const {
    if (MR::isBindedWall(this)) {
        pArg0->set(*MR::getWallNormal(this));
        return true;
    }

    if (0.0f < mFallSpeed && (MR::isOnGround(this)) && !isOnGround()) {
        pArg0->set(*MR::getGroundNormal(this));
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

const char* CocoNut::getModelName() {
    bool watermelonMode = MR::isStarPieceCounterStop();
    return watermelonMode ? "Watermelon" : "CocoNut";
}

const char* CocoNut::getBreakEffectName() {
    bool watermelonMode = MR::isStarPieceCounterStop();
    return watermelonMode ? "BreakWatermelon" : "CocoNutBreak";
}

void CocoNut::calcAndSetBaseMtx() {
    mRollMtx.mMtx[0][3] = this->mPosition.x;
    mRollMtx.mMtx[1][3] = this->mPosition.y;
    mRollMtx.mMtx[2][3] = this->mPosition.z;

    MR::setBaseTRMtx(this, mRollMtx);

    if (isNerve(GET_NERVE(CocoNut, CocoNutNrvMove)) && MR::isOnGround(this)) {
        const Vec3f* groundNormal = MR::getGroundNormal(this);
        f32 temp_f31 = mRadius;

        Vec3f stack_14;
        Vec3f stack_8(*groundNormal);
        stack_8.scale(temp_f31);

        stack_14.sub(mPosition, stack_8);

        if (MR::isSameDirection(*groundNormal, mFront)) {
            MR::makeMtxUpNoSupportPos(&mSmokeMtx, *groundNormal, stack_14);
        } else {
            MR::makeMtxUpFrontPos(&mSmokeMtx, *groundNormal, mFront, stack_14);
        }

        f32 temp_f9 = mScale.x;
        mSmokeMtx.mMtx[0][0] *= temp_f9;
        mSmokeMtx.mMtx[0][1] *= temp_f9;
        mSmokeMtx.mMtx[0][2] *= temp_f9;
        mSmokeMtx.mMtx[1][0] *= temp_f9;
        mSmokeMtx.mMtx[1][1] *= temp_f9;
        mSmokeMtx.mMtx[1][2] *= temp_f9;
        mSmokeMtx.mMtx[2][0] *= temp_f9;
        mSmokeMtx.mMtx[2][1] *= temp_f9;
        mSmokeMtx.mMtx[2][2] *= temp_f9;
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

    f32 var_f31;
    f32 var_f30;

    if (MR::isMsgPlayerSpinAttack(msg)) {
        var_f31 = 35.0f;
        var_f30 = -20.0f;
        MR::startSound(this, "SE_PM_SPIN_HIT");
        MR::startSound(this, "SE_OJ_COCONUT_LAUNCH");
        emitEffectSpinHit(pSender, pReceiver);
        mIsKicked = true;
    } else if (MR::isMsgPlayerHipDrop(msg) || MR::isMsgInvincibleAttack(msg)) {
        var_f31 = 25.0f;
        var_f30 = -15.0f;
        MR::startSound(this, "SE_OJ_COCONUT_FLIP_M");

        if (MR::isMsgPlayerHipDrop(msg)) {
            MR::sendMsgAwayJump(pSender, pReceiver);
        }
    } else {
        var_f31 = 10.0f;
        var_f30 = -5.0f;
        MR::startSound(this, "SE_OJ_COCONUT_FLIP_S");
    }

    mMoveSpeed = var_f31;
    mFallSpeed = var_f30;
    Vec3f stack_2C;
    Vec3f stack_20;
    Vec3f stack_14;
    stack_2C.sub(pReceiver->mPosition, pSender->mPosition);

    if (MR::isMsgStarPieceReflect(msg) && !MR::normalizeOrZero(stack_2C, &stack_20) && !MR::normalizeOrZero(pSender->mHost->mVelocity, &stack_14)) {
        if (stack_20.dot(stack_14) < MR::cosDegree(60.0f)) {
            Vec3f stack_8(stack_14);
            stack_8.scale(65.0f);
            stack_2C.add(stack_8);
        }
    }

    setFrontVec(stack_2C);

    if (MR::isMsgPlayerSpinAttack(msg)) {
        reviseFrontVec();
    }

    setNerve(GET_NERVE(CocoNut, CocoNutNrvMove));
    return true;
}

bool CocoNut::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    f32 temp_f0;
    f32 temp_f1;
    f32 var_f2;

    if (!isValidReceiveMsg(pReceiver)) {
        return false;
    }

    if (MR::isMsgPlayerKick(msg)) {
        if (isValidPushedFromPlayer(pReceiver, pSender)) {
            Vec3f* playerVelocity = MR::getPlayerVelocity();
            setFrontVec(*playerVelocity);
            temp_f1 = mFront.dot(*playerVelocity);
            var_f2 = 18.0f;
            temp_f0 = 1.1f * temp_f1;

            mMoveSpeed = var_f2 >= temp_f0 ? var_f2 : temp_f0;
            mFallSpeed = -(0.7f * temp_f1);
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
    mRadius = 55.0f * mScale.x;
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

void CocoNut::emitEffectSpinHit(const HitSensor* pOtherSensor, const HitSensor* pMySensor) {
    Vec3f point;  // point 70% of the way between pOtherSensor and pMySensor
    point.lerp(pOtherSensor->mPosition, pMySensor->mPosition, 0.7f);
    mSpinHitMtx.mMtx[0][0] = 1.0f;
    mSpinHitMtx.mMtx[1][0] = 0.0f;
    mSpinHitMtx.mMtx[2][0] = 0.0f;
    mSpinHitMtx.mMtx[0][1] = 0.0f;
    mSpinHitMtx.mMtx[1][1] = 1.0f;
    mSpinHitMtx.mMtx[2][1] = 0.0f;
    mSpinHitMtx.mMtx[0][2] = 0.0f;
    mSpinHitMtx.mMtx[1][2] = 0.0f;
    mSpinHitMtx.mMtx[2][2] = 1.0f;
    mSpinHitMtx.mMtx[0][3] = point.x;
    mSpinHitMtx.mMtx[1][3] = point.y;
    mSpinHitMtx.mMtx[2][3] = point.z;
    MR::emitEffect(this, "SpinHitMark");
}

bool CocoNut::isContactWithOtherCocoNut() const {
    HitSensor* body = getSensor("body");

    for (int i = 0; i < body->mSensorCount; i++) {
        if (body->mSensors[i]->isType(ATYPE_COCO_NUT)) {
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

    if (mIsKicked && MR::isStep(this, 1)) {
        MR::stopScene(4);
    }

    processMove();
    tryMoveEnd();
}

void CocoNut::exeInWater() {
    if (MR::isFirstStep(this)) {
        mVelocity.zero();
        MR::offBind(this);
        MR::offCalcGravity(this);
        MR::hideModel(this);
        MR::invalidateHitSensors(this);
        MR::clearHitSensors(this);
        MR::deleteEffectAll(this);
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
