#include "Game/Boss/TripodBossLeg.hpp"
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
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

namespace {
    inline Vec3f calcAnkleShadowPosition(const Vec3f& rPosition, const Vec3f& rNormal) {
        Vec3f offset(rNormal);
        offset *= 630.0f;
        Vec3f position(rPosition);
        position -= offset;
        return position;
    }

    static const s32 sStampSignTime = 60;
    static const s32 sStampSignVibrationTime = 20;
    static const f32 sStampSignVibrationCycle = 10.0f;
    static const f32 sStampSignVibrationAmplitude = 20.0f;
    static const f32 sLandShakeStrongDistance = 2000.0f;
    static const f32 sLandShakeMiddleDistance = 3500.0f;
    static const s32 sDamageVibrationTime = 27;
    static const f32 sDamageVibrationCycle = 0.25f;
    static const f32 sDamageVibrationAmplitude = 40.0f;
    static const f32 sShadowDropLength = 2000.0f;
};  // namespace

namespace {
    inline f32 getStampVibrationAmplitude() {
        return ::sStampSignVibrationAmplitude;
    }

    inline f32 getDamageVibrationAmplitude() {
        return ::sDamageVibrationAmplitude;
    }

    inline f32 getDamageVibrationCycle() {
        return ::sDamageVibrationCycle;
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

TripodBossLeg::~TripodBossLeg() {
}

TripodBossLeg::TripodBossLeg(const char* pName)
    : LiveActor(pName), mBoss(), mMoveArea(), mPrevStepPoint(), mStepPoint(), mJoint(new IKJoint()), mAnkleStartQuat(0, 0, 0, 1), mAnkleLandingQuat(0, 0, 0, 1), mRootLocalPos(0, 0, 0),
      mForceEndPoint(0, 0, 0), mEndVelocity(0, 0, 0), mEndTarget(0, 0, 0), mPoseMode(1), mLandingHeight(), mIsAttackHit(), mIsPressPlayer(), mDemoEffectTiming() {
    mRootLocalMtx.identity();
    mEndJointMtx.identity();
    mAnkleShadowMtx.identity();
    mRootLocalYMtx.identity();
    mRootLocalYZMtx.identity();
    mAnkleLocalXMtx.identity();
    mAnkleLocalXZMtx.identity();
}

void TripodBossLeg::init(const TableIter& rIter) {
    initNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvHold));
    MR::invalidateClipping(this);
    initHitSensor(1);
    MR::addHitSensorMtxMapObj(this, "body", 16, 1000.0f, mEndJointMtx, Vec3f(500.0f, 200.0f, 0.0f));
    initSound(6, false);
    mSoundObj = new AudAnmSoundObject(&mForceEndPoint, 6, MR::getCurrentHeap());
    initEffectKeeper(0, "TripodBoss");
    MR::setEffectHostMtx(this, "LegSmoke", mEndJointMtx);
    makeActorAppeared();
}

void TripodBossLeg::initShadow(const char* pName) {
    MR::initShadowVolumeFlatModel(this, pName, mAnkleShadowMtx);
    MR::setShadowDropLength(this, nullptr, ::sShadowDropLength);
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

void TripodBossLeg::setIKParam(f32 rootLength, f32 middleLength, const Vec3f& rA3, const Vec3f& rA4, const Vec3f& rA5) {
    mJoint->setRootBoneLength(rootLength);
    mJoint->setMiddleBoneLength(middleLength);
    mJoint->setFirstPose(rA4, rA5);
    mRootLocalPos = rA3;
    Vec3f v12(mRootLocalPos);
    v12.y = 0.0f;
    mRootLocalMtx.identity();

    if (!MR::isNearZero(v12)) {
        MR::normalize(&v12);
        mRootLocalMtx.setXDir(v12);
        mRootLocalMtx.setZDir(Vec3f(-v12.z, 0.0f, v12.x));
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

void TripodBossLeg::setDemoEffectTiming(bool timg) {
    if (timg) {
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
    bool v4 = isNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvDamage)) || isNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvBreak));

    if (v4) {
        return;
    }

    mPrevStepPoint = mStepPoint;
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
           (isNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvLeaveOut)) && MR::isLessStep(this, 60));
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
    updateAnkleUp(1000.0f);
}

void TripodBossLeg::exeUp() {
    if (MR::isFirstStep(this)) {
        MR::startSoundObject(mSoundObj, "SE_BM_TRIPOD_LEG_UP");
        Pos3f v5;
        v5.identity();
        MR::makeMtxSideUp(&v5, -mPrevStepPoint->mStepNormal, mPrevStepPoint->mStepFront);
        v5.getQuat(mAnkleStartQuat);
    }

    addAccelUpLeg(mPrevStepPoint, 1.5f);
    f32 legHeight = calcLegHeight(mPrevStepPoint);
    updateAnkleUp(legHeight);

    if (legHeight > 1000.0f) {
        setNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvMoveToLandingPos));
    }
}

void TripodBossLeg::exeLeaveOut() {
    if (MR::isFirstStep(this)) {
        MR::startSoundObject(mSoundObj, "SE_BM_TRIPOD_LEG_UP");
        Pos3f v9;
        v9.identity();
        MR::makeMtxSideUp(&v9, -mStepPoint->mStepNormal, mStepPoint->mStepFront);
        v9.getQuat(mAnkleStartQuat);
        mEndTarget = mStepPoint->mStepPosition + mStepPoint->mStepNormal * 1000.0f;
    }

    if (MR::isLessStep(this, 60)) {
        mEndVelocity.zero();
        updatePose();
    } else if (calcLegHeight(mStepPoint) < 1000.0f) {
        addAccelUpLeg(mStepPoint, 0.8f);
        updateAnkleUp(calcLegHeight(mStepPoint));
    } else {
        addToTargetPower(mEndTarget, 1.0f);
        addIKLimitPower();
        mForceEndPoint += mEndVelocity;
        mEndVelocity.x *= 0.9f;
        mEndVelocity.y *= 0.9f;
        mEndVelocity.z *= 0.9f;
        updatePose();
        updateAnkleSlerpToBasePose();
    }
}

void TripodBossLeg::exeMove() {
    MR::startSoundObjectLevel(mSoundObj, "SE_BM_LV_TRIPOD_LEG_MOVE");
    addToTargetPower(mEndTarget, 1.0f);
    addIKLimitPower();
    mForceEndPoint += mEndVelocity;
    mEndVelocity.x *= 0.9f;
    mEndVelocity.y *= 0.9f;
    mEndVelocity.z *= 0.9f;
    updatePose();
}

void TripodBossLeg::exeMoveToLandingPos() {
    if (MR::isFirstStep(this)) {
        Vec3f v14 = mStepPoint->mStepPosition + mStepPoint->mStepNormal * 1000.0f;
        mEndTarget = v14;
    }

    MR::startSoundObjectLevel(mSoundObj, "SE_BM_LV_TRIPOD_LEG_MOVE");
    addToTargetPower(mEndTarget, 1.0f);
    addIKLimitPower();
    mForceEndPoint += mEndVelocity;
    mEndVelocity.x *= 0.95f;
    mEndVelocity.y *= 0.95f;
    mEndVelocity.z *= 0.95f;
    updatePose();
    updateAnkleSlerpToBasePose();

    if (mForceEndPoint.squared(mEndTarget) < 90000.0f) {
        setNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvStampSign));
    }
}

void TripodBossLeg::exeStampSign() {
    if (MR::isFirstStep(this)) {
        mEndTarget = mStepPoint->mStepPosition + mStepPoint->mStepNormal * 1000.0f;
    }

    MR::startSoundObjectLevel(mSoundObj, "SE_BM_LV_TRIPOD_LEG_PREFALL");
    addToTargetPower(mEndTarget, 1.0f);
    addIKLimitPower();
    mForceEndPoint += mEndVelocity;
    mEndVelocity.x *= 0.9f;
    mEndVelocity.y *= 0.9f;
    mEndVelocity.z *= 0.9f;

    if (MR::isGreaterStep(this, ::sStampSignVibrationTime)) {
        Vec3f v8(mStepPoint->mStepNormal);
        mForceEndPoint += v8 * (MR::sin(getNerveStep() * TWO_PI / ::sStampSignVibrationCycle) * getStampVibrationAmplitude());
    }

    updatePose();
    updateAnkleSlerpToBasePose();

    if (MR::isGreaterStep(this, ::sStampSignTime)) {
        mEndTarget = mStepPoint->mStepPosition;

        setNerve(GET_NERVE(TripodBossLeg, TripodBossLegNrvLanding));
    }
}

void TripodBossLeg::exeLanding() {
    if (MR::isFirstStep(this)) {
        MR::startSoundObject(mSoundObj, "SE_BM_TRIPOD_FALL");
        mEndVelocity.zero();
        mEndJointMtx.getQuat(mAnkleStartQuat);
        Pos3f v18;
        v18.identity();
        TripodBossStepPoint* point = mStepPoint;
        MR::makeMtxSideUp(&v18, -point->mStepNormal, point->mStepFront);
        v18.getQuat(mAnkleLandingQuat);
        Vec3f offset(mForceEndPoint - mStepPoint->mStepPosition);
        mLandingHeight = offset.dot(mStepPoint->mStepNormal);
    }

    Vec3f v15(mEndTarget - mForceEndPoint);
    MR::normalizeOrZero(&v15);
    mEndVelocity += v15.multInLine(1.0f);
    addIKLimitPower();
    mForceEndPoint += mEndVelocity;
    mEndVelocity.x *= 0.98f;
    mEndVelocity.y *= 0.98f;
    mEndVelocity.z *= 0.98f;
    updateIKPose();
    Vec3f v12;
    mEndJointMtx.getTrans(v12);
    Vec3f endJointPos;
    mJoint->getEndJointPosition(&endJointPos);
    HitResult hitResult;

    if (mMoveArea->collideSphere(&hitResult, v12, 0.0f, endJointPos - v12)) {
        MR::startSoundObject(mSoundObj, "SE_BM_TRIPOD_LAND");
        f32 dist = mForceEndPoint.distance(*MR::getPlayerPos());

        if (dist < ::sLandShakeStrongDistance) {
            MR::shakeCameraStrong();
        } else if (dist < ::sLandShakeMiddleDistance) {
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

    Vec3f v5(mStepPoint->mStepNormal);
    mForceEndPoint = mEndTarget + v5 * (MR::sin(getNerveStep() * TWO_PI * getDamageVibrationCycle()) * getDamageVibrationAmplitude());
    updateIKPose();

    if (MR::isGreaterStep(this, ::sDamageVibrationTime)) {
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

void TripodBossLeg::addToTargetPower(const Vec3f& rA1, f32 a2) {
    Vec3f v11(rA1 - mForceEndPoint);
    f32 v7;
    MR::separateScalarAndDirection(&v7, &v11, v11);
    mEndVelocity += v11 * (a2 * MR::clamp(v7 / 300.0f, -1.0f, 1.0f));
    f32 v6 = (1.0f - (v7 / 300.0f));
    if (v6 > 0.0f) {
        mEndVelocity -= mEndVelocity * v6 * 0.1f;
    }
}

void TripodBossLeg::addIKLimitPower() {
    Vec3f rootJoint;
    mJoint->getRootJointPosition(&rootJoint);
    Vec3f v12(rootJoint - mForceEndPoint);
    f32 v8;
    MR::separateScalarAndDirection(&v8, &v12, v12);
    f32 maxDist = mJoint->getMaxLimitDistance();
    f32 minDist = mJoint->getMinLimitDistance();
    f32 v4 = (f32)MR::normalize(v8, minDist, maxDist);
    v8 = v4;
    f32 v5, v6;

    if (v8 < 0.5f) {
        v8 = 1.0f - v8;
        v12 = -v12;
        v5 = 0.5f;
        v6 = 0.0f;
    } else {
        v5 = 0.9f;
        v6 = 5.0f;
    }

    if (v5 < v8) {
        f32 ease = MR::getEaseInOutValue(((1.0f / (1.0f - v5))) * (v8 - v5), 0.0f, 1.0f, 1.0f);
        mEndVelocity += v12 * ease * v6;
    }
}

void TripodBossLeg::addAccelUpLeg(TripodBossStepPoint* pPoint, f32 a3) {
    Vec3f v8(pPoint->mStepNormal);
    mEndVelocity += v8 * a3;
    addIKLimitPower();
    mForceEndPoint += mEndVelocity;
    mEndVelocity.x *= 0.9f;
    mEndVelocity.y *= 0.9f;
    mEndVelocity.z *= 0.9f;
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
        Vec3f v4;
        bodyMtx.mult(mRootLocalPos, v4);
        Vec3f legUp;
        mBoss->calcLegUpVector(&legUp, mForceEndPoint);
        mJoint->updateByUpVector(v4, mForceEndPoint, legUp);
        break;
    }

    case 1:
        mJoint->updateByLocalRootAndWorldTarget(bodyMtx, mRootLocalPos, mForceEndPoint);
        break;
    }

    separateLocalAxisRootJoint(bodyMtx);
}

bool TripodBossLeg::bindEndPosition() {
    Vec3f v6;
    mEndJointMtx.getTrans(v6);
    Vec3f endJointPos;
    mJoint->getEndJointPosition(&endJointPos);
    HitResult hitResult;
    if (mMoveArea->collideSphere(&hitResult, v6, 0.0f, endJointPos - v6)) {
        mForceEndPoint = hitResult.mPushedPos;
        if (mEndVelocity.dot(hitResult.mNormal) < 0.0f) {
            mEndVelocity.orthogonalize2(hitResult.mNormal);
        }

        return true;
    }

    return false;
}

void TripodBossLeg::separateLocalAxisRootJoint(const Pos3f& rPos) {
    Pos3f v5;
    v5.set(rPos);
    v5.setTrans(0.0f, 0.0f, 0.0f);
    v5.concat(rPos, mRootLocalMtx);
    MR::separateMatrixRotateYZX(&mRootLocalYMtx, &mRootLocalYZMtx, v5, mJoint->mRootMtx);
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

void TripodBossLeg::updateAnkleUp(f32 angle) {
    Pos3f v9;
    Quat4f quat;
    Vec3f landingNormal;
    Vec3f landingFront;

    mMoveArea->calcLandingNormal(&landingNormal, mForceEndPoint);
    mMoveArea->calcLandingFront(&landingFront, mForceEndPoint);

    v9.identity();
    MR::makeMtxSideUp(&v9, -landingNormal, landingFront);

    v9.getQuat(quat);
    f32 v4 = MR::clamp((angle / 1000.0f), 0.0f, 1.0f);
    quat.slerp(mAnkleStartQuat, (1.0f - (v4 * (v4 * v4))));
    quat.normalize();
    mEndJointMtx.makeQuat(quat);
}

void TripodBossLeg::updateAnkleSlerpToBasePose() {
    Quat4f quat;
    Quat4f v5;
    Vec3f landingNormal;
    Vec3f landingFront;
    mEndJointMtx.getQuat(quat);
    mMoveArea->calcLandingNormal(&landingNormal, mForceEndPoint);
    mMoveArea->calcLandingFront(&landingFront, mForceEndPoint);
    Pos3f v7;
    v7.identity();
    MR::makeMtxSideUp(&v7, -landingNormal, landingFront);
    v7.getQuat(v5);
    quat.slerp(quat, v5, 0.2f);
    quat.normalize();
    mEndJointMtx.makeQuat(quat);
}

void TripodBossLeg::updateAnkleLanding() {
    Vec3f v6(mForceEndPoint - mStepPoint->mStepPosition);
    f32 v3 = v6.dot(mStepPoint->mStepNormal);
    f32 v4 = MR::normalize((v3 / mLandingHeight), 0.15f, 1.0f);
    Quat4f quat;
    quat.slerp(mAnkleStartQuat, mAnkleLandingQuat, ((1.0f - v4) * ((1.0f - v4) * (1.0f - v4))));
    quat.normalize();
    mEndJointMtx.makeQuat(quat);
}

void TripodBossLeg::updateAnkleShadowMatrix() {
    Vec3f landingPosition;
    Vec3f landingNormal;
    Vec3f v7, v8, v9;

    mMoveArea->calcNearLandingPosition(&landingPosition, mForceEndPoint);
    mMoveArea->calcLandingNormal(&landingNormal, landingPosition);

    MR::setShadowDropDirection(this, nullptr, -landingNormal);
    mEndJointMtx.getXYZDir(v7, v8, v9);
    mAnkleShadowMtx.setXYZDir(-v9, -v7, v8);

    mAnkleShadowMtx.setTrans(calcAnkleShadowPosition(mForceEndPoint, landingNormal));
}

namespace MR {
    void separateMatrixRotateYZX(Pos3f* pA1, Pos3f* pA2, const Pos3f& rA3, const Pos3f& rA4) {
        Pos3f v17;
        v17.invert(rA3);
        v17.concat(v17, rA4);
        Vec3f v16;
        v17.getXDir(v16);
        f32 v10 = MR::sqrt((v16.x * v16.x) + (v16.z * v16.z));

        if (MR::isNearZero(v10, 0.000001f)) {
            if (v16.y >= 0.0f) {
                pA1->setXDir(0.0f, 1.0f, 0.0f);
                pA1->setYDir(-1.0f, 0.0f, 0.0f);

            } else {
                pA1->setXDir(0.0f, -1.0f, 0.0f);
                pA1->setYDir(1.0f, 0.0f, 0.0f);
            }

            pA1->setZDir(0.0f, 0.0f, 1.0f);
            pA1->concat(rA3, *pA1);
            pA2->set(*pA1);
        } else {
            f32 v11 = v16.length();
            f32 v12 = (v10 / v11);
            f32 v14 = (v16.y / v11);
            f32 v15 = (v16.x / v10);
            f32 v13 = (v16.z / v10);

            pA1->setXDir(v15, 0.0f, v13);
            pA1->setYDir(0.0f, 1.0f, 0.0f);
            pA1->setZDir(-v13, 0.0f, v15);
            pA1->setTrans(0.0f, 0.0f, 0.0f);
            pA2->setXDir(v12, v14, 0.0f);
            pA2->setYDir(-v14, v12, 0.0f);
            pA2->setZDir(0.0f, 0.0f, 1.0f);
            pA2->setTrans(0.0f, 0.0f, 0.0f);
            pA1->concat(rA3, *pA1);
            pA2->concat(*pA1, *pA2);
        }
    }
};  // namespace MR
