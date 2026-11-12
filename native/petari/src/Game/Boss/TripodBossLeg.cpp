#include "Game/Boss/TripodBossLeg.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/AudioLib/AudAnmSoundObject.hpp"
#include "Game/Boss/TripodBoss.hpp"
#include "Game/Boss/TripodBossMovableArea.hpp"
#include "Game/Boss/TripodBossStepPoint.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/IKJointCtrl.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/IKJoint.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sLiftHeight = 1000.0f;  // above the step point: lift ends, ankle fully turned, landing target
    const s32 sLeaveOutWaitTime = NebulaFrameClock::stepsOfSeconds(1);
    const f32 sTargetBrakeDistance = 300.0f;
    const f32 sStampSignDistance = 300.0f;
    const f32 sMoveDamping = 0.9f;
    const f32 sApproachDamping = 0.95f;
    const f32 sFallDamping = 0.98f;
    const f32 sFallAccel = 1.0f;
    const f32 sUpAccel = 1.5f;
    const f32 sLeaveOutUpAccel = 0.8f;
    const f32 sAnkleSlerpRate = 0.2f;
    const f32 sAnkleLandingStartRate = 0.15f;
    const f32 sAnkleShadowOffset = 630.0f;
    const s32 sStampSignTime = NebulaFrameClock::stepsOfSeconds(1);
    const s32 sStampSignVibrationTime = 20;
    const f32 sStampSignVibrationCycle = 10.0f;
    const f32 sStampSignVibrationAmplitude = 20.0f;
    const f32 sLandShakeStrongDistance = 2000.0f;
    const f32 sLandShakeMiddleDistance = 3500.0f;
    const s32 sDamageVibrationTime = 27;
    const f32 sDamageVibrationCycle = 0.25f;
    const f32 sDamageVibrationAmplitude = 40.0f;
    const f32 sShadowDropLength = 2000.0f;

    inline Vec3f calcAnkleShadowPosition(const Vec3f& rPosition, const Vec3f& rNormal) {
        Vec3f offset(rNormal);
        offset *= sAnkleShadowOffset;
        Vec3f position(rPosition);
        position -= offset;
        return position;
    }

    // The ankle standing on a surface: sole against rNormal, toe along rFront.
    inline void calcStandQuat(Quat4f* pQuat, const Vec3f& rNormal, const Vec3f& rFront) {
        Pos3f mtx;
        mtx.identity();
        MR::makeMtxSideUp(&mtx, -rNormal, rFront);
        mtx.getQuat(*pQuat);
    }
};  // namespace

namespace NrvTripodBossLeg {
    NEW_NERVE(TripodBossLegNrvHold, TripodBossLeg, Hold);
    NEW_NERVE(TripodBossLegNrvDemo, TripodBossLeg, Demo);
    NEW_NERVE(TripodBossLegNrvMove, TripodBossLeg, Move);
    NEW_NERVE(TripodBossLegNrvUp, TripodBossLeg, Up);
    NEW_NERVE(TripodBossLegNrvLeaveOut, TripodBossLeg, LeaveOut);
    NEW_NERVE(TripodBossLegNrvMoveToLandingPos, TripodBossLeg, MoveToLandingPos);
    NEW_NERVE(TripodBossLegNrvStampSign, TripodBossLeg, StampSign);
    NEW_NERVE(TripodBossLegNrvLanding, TripodBossLeg, Landing);
    NEW_NERVE(TripodBossLegNrvDamageVibration, TripodBossLeg, DamageVibration);
    NEW_NERVE(TripodBossLegNrvDamage, TripodBossLeg, Damage);
    NEW_NERVE(TripodBossLegNrvBreak, TripodBossLeg, Break);
};  // namespace NrvTripodBossLeg

TripodBossLeg::TripodBossLeg(const char* pName)
    : LiveActor(pName), mBoss(), mMoveArea(), mPrevStepPoint(), mStepPoint(), mJoint(new IKJoint()), mAnkleStartQuat(0, 0, 0, 1), mAnkleLandingQuat(0, 0, 0, 1), mRootLocalPos(0, 0, 0),
      mForceEndPoint(0, 0, 0), mEndVelocity(0, 0, 0), mEndTarget(0, 0, 0), mPoseMode(1), mLandingHeight(), mIsAttackHit(), mIsPressPlayer(), mDemoEffectTiming() {
    lumacore::setClass(this, lumacore::kClassId< TripodBossLeg >);
    mRootLocalMtx.identity();
    mEndJointMtx.identity();
    mAnkleShadowMtx.identity();
    mRootLocalYMtx.identity();
    mRootLocalYZMtx.identity();
    mAnkleLocalXMtx.identity();
    mAnkleLocalXZMtx.identity();
}

void TripodBossLeg::init(const TableIter& /*rIter*/) {
    initNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvHold));
    MR::invalidateClipping(this);
    initHitSensor(1);
    MR::addHitSensorMtxMapObj(this, "body", 16, 1000.0f, mEndJointMtx, Vec3f(500.0f, 200.0f, 0.0f));
    initSound(6, false);
    mSoundObj = new AudAnmSoundObject(&mForceEndPoint, 6, NebulaArena::current());
    initEffectKeeper(0, "TripodBoss");
    MR::setEffectHostMtx(this, "LegSmoke", mEndJointMtx);
    makeActorAppeared();
}

void TripodBossLeg::initShadow(const char* pName) {
    MR::initShadowVolumeFlatModel(this, pName, mAnkleShadowMtx);
    MR::setShadowDropLength(this, nullptr, sShadowDropLength);
}

void TripodBossLeg::makeActorAppeared() {
    LiveActor::makeActorAppeared();
}

void TripodBossLeg::makeActorDead() {
    LiveActor::makeActorDead();
}

void TripodBossLeg::control() {
    mIsAttackHit = false;
    mIsPressPlayer = false;
    mEndJointMtx.setTrans(mForceEndPoint);
    separateAnkleJointLocalAxis();
    updateAnkleShadowMatrix();
    mSoundObj->process();
}

void TripodBossLeg::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (pReceiver->receiveMessage(ACTMES_TRIPODBOSS_LEG_IS_DAMAGE, pSender)) {
        mIsAttackHit = true;
    }

    if (MR::isSensorPlayer(pReceiver)) {
        mIsPressPlayer = true;
    }
}

void TripodBossLeg::setBody(TripodBoss* pBoss) {
    mBoss = pBoss;
}

void TripodBossLeg::setMovableArea(TripodBossMovableArea* pArea) {
    mMoveArea = pArea;
}

void TripodBossLeg::setIKParam(f32 rootLength, f32 middleLength, const Vec3f& rRootLocalPos, const Vec3f& rSide, const Vec3f& rUp) {
    mJoint->setRootBoneLength(rootLength);
    mJoint->setMiddleBoneLength(middleLength);
    mJoint->setFirstPose(rSide, rUp);
    mRootLocalPos = rRootLocalPos;
    Vec3f rootDirXZ(mRootLocalPos);
    rootDirXZ.y = 0.0f;
    mRootLocalMtx.identity();

    if (!MR::isNearZero(rootDirXZ)) {
        MR::normalize(&rootDirXZ);
        mRootLocalMtx.setXDir(rootDirXZ);
        mRootLocalMtx.setZDir(Vec3f(-rootDirXZ.z, 0.0f, rootDirXZ.x));
    }

    mRootLocalMtx.setTrans(mRootLocalPos);
}

void TripodBossLeg::setStepTarget(TripodBossStepPoint* pPoint) {
    mPrevStepPoint = pPoint;
    mStepPoint = pPoint;
    mForceEndPoint = pPoint->mStepPosition;
    MR::makeMtxSideUpPos(&mEndJointMtx, -mStepPoint->mStepNormal, mStepPoint->mStepFront, mForceEndPoint);
}

void TripodBossLeg::setWait() {
    setNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvHold));
}

void TripodBossLeg::setForceEndPoint(const Vec3f& rPoint) {
    mForceEndPoint.set(rPoint);
}

void TripodBossLeg::setDemoEffectTiming(bool isOn) {
    if (isOn) {
        if (mDemoEffectTiming == 0) {
            mDemoEffectTiming = 1;
        } else {
            mDemoEffectTiming = 2;
        }
    } else {
        mDemoEffectTiming = 0;
    }
}

const Pos3f& TripodBossLeg::getRootJointMatrix() const {
    return mJoint->mRootMtx;
}

const Pos3f& TripodBossLeg::getMiddleJointMatrix() const {
    return mJoint->mMiddleMtx;
}

const Pos3f& TripodBossLeg::getEndJointMatrix() const {
    return mEndJointMtx;
}

const Pos3f& TripodBossLeg::getRootLocalYMatrix() const {
    return mRootLocalYMtx;
}

const Pos3f& TripodBossLeg::getRootLocalYZMatrix() const {
    return mRootLocalYZMtx;
}

const Pos3f& TripodBossLeg::getAnkleLocalXMatrix() const {
    return mAnkleLocalXMtx;
}

const Pos3f& TripodBossLeg::getAnkleLocalXZMatrix() const {
    return mAnkleLocalXZMtx;
}

void TripodBossLeg::requestStepTarget(TripodBossStepPoint* pPoint) {
    if (pPoint == nullptr || !canStep()) {
        return;
    }

    // A leg that never had a standing point lifts from the new one.
    mPrevStepPoint = mStepPoint != nullptr ? mStepPoint : pPoint;
    mStepPoint = pPoint;

    if (isNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvHold))) {
        setNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvUp));
    } else {
        setNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvMoveToLandingPos));
    }
}

void TripodBossLeg::requestLeaveOut() {
    setNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvLeaveOut));
}

void TripodBossLeg::requestBreak() {
    if (!isNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvBreak))) {
        setNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvBreak));
    }
}

void TripodBossLeg::requestStartDemo() {
    mPoseMode = 1;

    setNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvDemo));
}

void TripodBossLeg::requestEndDemo() {
    mPoseMode = 0;

    setNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvHold));
}

bool TripodBossLeg::isLanding() const {
    return isNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvHold));
}

bool TripodBossLeg::isDamage() const {
    return isNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvDamageVibration)) || isNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvDamage));
}

bool TripodBossLeg::isStop() const {
    return isNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvHold)) || isNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvDemo)) ||
           isNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvDamage)) || isNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvBreak));
}

bool TripodBossLeg::isBroken() const {
    return isNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvBreak));
}

bool TripodBossLeg::isPressPlayer() const {
    return mIsPressPlayer;
}

bool TripodBossLeg::canStep() const {
    return !isNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvDamage)) && !isNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvBreak));
}

bool TripodBossLeg::canCancelStep() const {
    return !isNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvLanding)) && !isNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvStampSign));
}

bool TripodBossLeg::canWeighting() const {
    return isNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvHold)) || isNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvLanding)) ||
           (isNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvLeaveOut)) && MR::isLessStep(this, sLeaveOutWaitTime));
}

void TripodBossLeg::exeDemo() {
    if (MR::isFirstStep(this)) {
        mEndVelocity.zero();
    }

    if (mDemoEffectTiming == 1) {
        MR::startSoundObject(mSoundObj, "SE_BM_TRIPOD_LAND");
        MR::emitEffect(this, "LegSmoke");
    }

    updateIKPose();
    updateAnkleUp(sLiftHeight);
}

void TripodBossLeg::exeUp() {
    if (MR::isFirstStep(this)) {
        MR::startSoundObject(mSoundObj, "SE_BM_TRIPOD_LEG_UP");
        calcStandQuat(&mAnkleStartQuat, mPrevStepPoint->mStepNormal, mPrevStepPoint->mStepFront);
    }

    addAccelUpLeg(mPrevStepPoint, sUpAccel);
    f32 legHeight = calcLegHeight(mPrevStepPoint);
    updateAnkleUp(legHeight);

    if (legHeight > sLiftHeight) {
        setNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvMoveToLandingPos));
    }
}

void TripodBossLeg::exeLeaveOut() {
    if (MR::isFirstStep(this)) {
        MR::startSoundObject(mSoundObj, "SE_BM_TRIPOD_LEG_UP");
        calcStandQuat(&mAnkleStartQuat, mStepPoint->mStepNormal, mStepPoint->mStepFront);
        mEndTarget = mStepPoint->mStepPosition + mStepPoint->mStepNormal * sLiftHeight;
    }

    if (MR::isLessStep(this, sLeaveOutWaitTime)) {
        mEndVelocity.zero();
        updatePose();
    } else if (calcLegHeight(mStepPoint) < sLiftHeight) {
        addAccelUpLeg(mStepPoint, sLeaveOutUpAccel);
        updateAnkleUp(calcLegHeight(mStepPoint));
    } else {
        addToTargetPower(mEndTarget, 1.0f);
        moveEndPoint(sMoveDamping);
        updatePose();
        updateAnkleSlerpToBasePose();
    }
}

void TripodBossLeg::exeMove() {
    MR::startSoundObjectLevel(mSoundObj, "SE_BM_LV_TRIPOD_LEG_MOVE");
    addToTargetPower(mEndTarget, 1.0f);
    moveEndPoint(sMoveDamping);
    updatePose();
}

void TripodBossLeg::exeMoveToLandingPos() {
    if (MR::isFirstStep(this)) {
        mEndTarget = mStepPoint->mStepPosition + mStepPoint->mStepNormal * sLiftHeight;
    }

    MR::startSoundObjectLevel(mSoundObj, "SE_BM_LV_TRIPOD_LEG_MOVE");
    addToTargetPower(mEndTarget, 1.0f);
    moveEndPoint(sApproachDamping);
    updatePose();
    updateAnkleSlerpToBasePose();

    if (mForceEndPoint.squared(mEndTarget) < sStampSignDistance * sStampSignDistance) {
        setNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvStampSign));
    }
}

void TripodBossLeg::exeStampSign() {
    if (MR::isFirstStep(this)) {
        mEndTarget = mStepPoint->mStepPosition + mStepPoint->mStepNormal * sLiftHeight;
    }

    MR::startSoundObjectLevel(mSoundObj, "SE_BM_LV_TRIPOD_LEG_PREFALL");
    addToTargetPower(mEndTarget, 1.0f);
    moveEndPoint(sMoveDamping);

    if (MR::isGreaterStep(this, sStampSignVibrationTime)) {
        mForceEndPoint += mStepPoint->mStepNormal * (MR::sin(getNerveStep() * TWO_PI / sStampSignVibrationCycle) * sStampSignVibrationAmplitude);
    }

    updatePose();
    updateAnkleSlerpToBasePose();

    if (MR::isGreaterStep(this, sStampSignTime)) {
        mEndTarget = mStepPoint->mStepPosition;

        setNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvLanding));
    }
}

void TripodBossLeg::exeLanding() {
    if (MR::isFirstStep(this)) {
        MR::startSoundObject(mSoundObj, "SE_BM_TRIPOD_FALL");
        mEndVelocity.zero();
        mEndJointMtx.getQuat(mAnkleStartQuat);
        calcStandQuat(&mAnkleLandingQuat, mStepPoint->mStepNormal, mStepPoint->mStepFront);
        Vec3f offset(mForceEndPoint - mStepPoint->mStepPosition);
        mLandingHeight = offset.dot(mStepPoint->mStepNormal);
    }

    Vec3f toTarget(mEndTarget - mForceEndPoint);
    MR::normalizeOrZero(&toTarget);
    mEndVelocity += toTarget * sFallAccel;
    moveEndPoint(sFallDamping);
    updateIKPose();
    Vec3f prevEndPos;
    mEndJointMtx.getTrans(prevEndPos);
    Vec3f endJointPos;
    mJoint->getEndJointPosition(&endJointPos);
    HitResult hitResult;

    if (mMoveArea->collideSphere(&hitResult, prevEndPos, 0.0f, endJointPos - prevEndPos)) {
        MR::startSoundObject(mSoundObj, "SE_BM_TRIPOD_LAND");
        f32 dist = mForceEndPoint.distance(*MR::getPlayerPos());

        if (dist < sLandShakeStrongDistance) {
            MR::shakeCameraStrong();
        } else if (dist < sLandShakeMiddleDistance) {
            MR::shakeCameraNormal();
        } else {
            MR::shakeCameraWeak();
        }

        MR::emitEffect(this, "LegSmoke");
        mForceEndPoint = hitResult.mHitPos;
        mEndVelocity.zero();

        if (mIsAttackHit) {
            setNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvDamageVibration));
        } else {
            setNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvHold));
        }

        updateIKPose();
        updateAnkleLanding();
    } else {
        mJoint->getEndJointPosition(&mForceEndPoint);
        updateAnkleLanding();
    }
}

void TripodBossLeg::exeDamageVibration() {
    if (MR::isFirstStep(this)) {
        mEndVelocity.zero();
        mEndTarget = mForceEndPoint;
    }

    mForceEndPoint = mEndTarget + mStepPoint->mStepNormal * (MR::sin(getNerveStep() * TWO_PI * sDamageVibrationCycle) * sDamageVibrationAmplitude);
    updateIKPose();

    if (MR::isGreaterStep(this, sDamageVibrationTime)) {
        mForceEndPoint = mEndTarget;
        setNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvDamage));
    }
}

void TripodBossLeg::exeBreak() {
    if (MR::isFirstStep(this)) {
        mEndVelocity.zero();
    }

    updateIKPose();
    kill();
}

void TripodBossLeg::exeDamage() {
    updateIKPose();
}

void TripodBossLeg::exeHold() {
    if (MR::isFirstStep(this)) {
        mEndVelocity.zero();
    }

    updateIKPose();
}

void TripodBossLeg::addToTargetPower(const Vec3f& rTarget, f32 power) {
    Vec3f dir(rTarget - mForceEndPoint);
    f32 dist;
    MR::separateScalarAndDirection(&dist, &dir, dir);
    mEndVelocity += dir * (power * MR::clamp(dist / sTargetBrakeDistance, -1.0f, 1.0f));
    f32 brakeRate = (1.0f - (dist / sTargetBrakeDistance));
    if (brakeRate > 0.0f) {
        mEndVelocity -= mEndVelocity * brakeRate * 0.1f;
    }
}

// Pulls the end toward the root past 90% of the reach; the folded branch (below 50%) has power 0.
void TripodBossLeg::addIKLimitPower() {
    Vec3f rootJoint;
    mJoint->getRootJointPosition(&rootJoint);
    Vec3f toRoot(rootJoint - mForceEndPoint);
    f32 dist;
    MR::separateScalarAndDirection(&dist, &toRoot, toRoot);
    f32 maxDist = mJoint->getMaxLimitDistance();
    f32 minDist = mJoint->getMinLimitDistance();
    f32 rate = MR::normalize(dist, minDist, maxDist);
    f32 startRate, power;

    if (rate < 0.5f) {
        rate = 1.0f - rate;
        toRoot = -toRoot;
        startRate = 0.5f;
        power = 0.0f;
    } else {
        startRate = 0.9f;
        power = 5.0f;
    }

    if (startRate < rate) {
        f32 ease = MR::getEaseInOutValue(((1.0f / (1.0f - startRate))) * (rate - startRate), 0.0f, 1.0f, 1.0f);
        mEndVelocity += toRoot * ease * power;
    }
}

void TripodBossLeg::moveEndPoint(f32 damping) {
    addIKLimitPower();
    mForceEndPoint += mEndVelocity;
    mEndVelocity.x *= damping;
    mEndVelocity.y *= damping;
    mEndVelocity.z *= damping;
}

void TripodBossLeg::addAccelUpLeg(TripodBossStepPoint* pPoint, f32 accel) {
    mEndVelocity += pPoint->mStepNormal * accel;
    moveEndPoint(sMoveDamping);
    updatePose();
}

f32 TripodBossLeg::calcLegHeight(TripodBossStepPoint* pPoint) const {
    return (mForceEndPoint - pPoint->mStepPosition).dot(pPoint->mStepNormal);
}

void TripodBossLeg::updatePose() {
    updateIKPose();

    if (bindEndPosition()) {
        updateIKPose();
    }

    mJoint->getEndJointPosition(&mForceEndPoint);
}

void TripodBossLeg::updateIKPose() {
    Pos3f bodyMtx;
    mBoss->getBodyMatrix(&bodyMtx);

    switch (mPoseMode) {
    case 0: {
        Vec3f rootPos;
        bodyMtx.mult(mRootLocalPos, rootPos);
        Vec3f legUp;
        mBoss->calcLegUpVector(&legUp, mForceEndPoint);
        mJoint->updateByUpVector(rootPos, mForceEndPoint, legUp);
        break;
    }

    case 1:
        mJoint->updateByLocalRootAndWorldTarget(bodyMtx, mRootLocalPos, mForceEndPoint);
        break;
    }

    separateLocalAxisRootJoint(bodyMtx);
}

bool TripodBossLeg::bindEndPosition() {
    Vec3f prevEndPos;
    mEndJointMtx.getTrans(prevEndPos);
    Vec3f endJointPos;
    mJoint->getEndJointPosition(&endJointPos);
    HitResult hitResult;
    if (mMoveArea->collideSphere(&hitResult, prevEndPos, 0.0f, endJointPos - prevEndPos)) {
        mForceEndPoint = hitResult.mPushedPos;
        if (mEndVelocity.dot(hitResult.mNormal) < 0.0f) {
            mEndVelocity.orthogonalize2(hitResult.mNormal);
        }

        return true;
    }

    return false;
}

void TripodBossLeg::separateLocalAxisRootJoint(const Pos3f& rBodyMtx) {
    Pos3f rootBaseMtx;
    rootBaseMtx.set(rBodyMtx);
    rootBaseMtx.setTrans(0.0f, 0.0f, 0.0f);
    rootBaseMtx.concat(rBodyMtx, mRootLocalMtx);
    MR::separateMatrixRotateYZX(&mRootLocalYMtx, &mRootLocalYZMtx, rootBaseMtx, mJoint->mRootMtx);
    Vec3f jointRootPos;
    mJoint->getRootJointPosition(&jointRootPos);
    mRootLocalYMtx.setTrans(jointRootPos);
    mRootLocalYZMtx.setTrans(jointRootPos);
}

void TripodBossLeg::separateAnkleJointLocalAxis() {
    MR::separateMatrixRotateYZX(&mAnkleLocalXZMtx, &mAnkleLocalXMtx, mEndJointMtx, mJoint->mMiddleMtx);
    mAnkleLocalXMtx.setTrans(mForceEndPoint);
    mAnkleLocalXZMtx.setTrans(mForceEndPoint);
}

void TripodBossLeg::updateAnkleUp(f32 legHeight) {
    Quat4f quat;
    Vec3f landingNormal;
    Vec3f landingFront;

    mMoveArea->calcLandingNormal(&landingNormal, mForceEndPoint);
    mMoveArea->calcLandingFront(&landingFront, mForceEndPoint);
    calcStandQuat(&quat, landingNormal, landingFront);

    f32 rate = MR::clamp((legHeight / sLiftHeight), 0.0f, 1.0f);
    quat.slerp(mAnkleStartQuat, (1.0f - (rate * (rate * rate))));
    quat.normalize();
    mEndJointMtx.makeQuat(quat);
}

void TripodBossLeg::updateAnkleSlerpToBasePose() {
    Quat4f quat;
    Quat4f standQuat;
    Vec3f landingNormal;
    Vec3f landingFront;
    mEndJointMtx.getQuat(quat);
    mMoveArea->calcLandingNormal(&landingNormal, mForceEndPoint);
    mMoveArea->calcLandingFront(&landingFront, mForceEndPoint);
    calcStandQuat(&standQuat, landingNormal, landingFront);
    quat.slerp(quat, standQuat, sAnkleSlerpRate);
    quat.normalize();
    mEndJointMtx.makeQuat(quat);
}

void TripodBossLeg::updateAnkleLanding() {
    Vec3f offset(mForceEndPoint - mStepPoint->mStepPosition);
    f32 height = offset.dot(mStepPoint->mStepNormal);
    // A leg that began landing on the step plane counts as already landed.
    f32 heightRatio = mLandingHeight != 0.0f ? height / mLandingHeight : 0.0f;
    f32 rate = MR::normalize(heightRatio, sAnkleLandingStartRate, 1.0f);
    f32 restRate = 1.0f - rate;
    Quat4f quat;
    quat.slerp(mAnkleStartQuat, mAnkleLandingQuat, (restRate * (restRate * restRate)));
    quat.normalize();
    mEndJointMtx.makeQuat(quat);
}

void TripodBossLeg::updateAnkleShadowMatrix() {
    Vec3f landingPosition;
    Vec3f landingNormal;
    Vec3f xDir, yDir, zDir;

    mMoveArea->calcNearLandingPosition(&landingPosition, mForceEndPoint);
    mMoveArea->calcLandingNormal(&landingNormal, landingPosition);

    MR::setShadowDropDirection(this, nullptr, -landingNormal);
    mEndJointMtx.getXYZDir(xDir, yDir, zDir);
    mAnkleShadowMtx.setXYZDir(-zDir, -xDir, yDir);

    mAnkleShadowMtx.setTrans(calcAnkleShadowPosition(mForceEndPoint, landingNormal));
}

namespace MR {
    // Splits rTarget's X axis, in rBase's frame, into a turn about Y (pYMtx = rBase * yaw) then about Z (pYZMtx = pYMtx * pitch).
    void separateMatrixRotateYZX(Pos3f* pYMtx, Pos3f* pYZMtx, const Pos3f& rBase, const Pos3f& rTarget) {
        Pos3f local;
        local.invert(rBase);
        local.concat(local, rTarget);
        Vec3f xDir;
        local.getXDir(xDir);
        f32 lengthXZ = MR::sqrt((xDir.x * xDir.x) + (xDir.z * xDir.z));

        if (MR::isNearZero(lengthXZ, 0.000001f)) {
            if (xDir.y >= 0.0f) {
                pYMtx->setXDir(0.0f, 1.0f, 0.0f);
                pYMtx->setYDir(-1.0f, 0.0f, 0.0f);

            } else {
                pYMtx->setXDir(0.0f, -1.0f, 0.0f);
                pYMtx->setYDir(1.0f, 0.0f, 0.0f);
            }

            pYMtx->setZDir(0.0f, 0.0f, 1.0f);
            pYMtx->concat(rBase, *pYMtx);
            pYZMtx->set(*pYMtx);
        } else {
            f32 length = xDir.length();
            f32 cosPitch = (lengthXZ / length);
            f32 sinPitch = (xDir.y / length);
            f32 cosYaw = (xDir.x / lengthXZ);
            f32 sinYaw = (xDir.z / lengthXZ);

            pYMtx->setXDir(cosYaw, 0.0f, sinYaw);
            pYMtx->setYDir(0.0f, 1.0f, 0.0f);
            pYMtx->setZDir(-sinYaw, 0.0f, cosYaw);
            pYMtx->setTrans(0.0f, 0.0f, 0.0f);
            pYZMtx->setXDir(cosPitch, sinPitch, 0.0f);
            pYZMtx->setYDir(-sinPitch, cosPitch, 0.0f);
            pYZMtx->setZDir(0.0f, 0.0f, 1.0f);
            pYZMtx->setTrans(0.0f, 0.0f, 0.0f);
            pYMtx->concat(rBase, *pYMtx);
            pYZMtx->concat(*pYMtx, *pYZMtx);
        }
    }
};  // namespace MR
