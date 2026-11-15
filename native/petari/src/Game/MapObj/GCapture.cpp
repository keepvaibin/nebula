#include "Game/MapObj/GCapture.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Camera/CameraTargetMtx.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/BlueStarCupsulePlanet.hpp"
#include "Game/MapObj/GCaptureRibbon.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "math_types.hpp"
#include <nebula_geometry.hpp>

#include "Game/System/NebulaInput.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const s32 sTargetKeepSteps = NebulaFrameClock::stepsOfMilliseconds(500);
    const s32 sGuidanceSteps = 20;
    const s32 sCaptureSteps = NebulaFrameClock::stepsOfMilliseconds(500);
    const s32 sCaptureCancelSteps = 40;
    const s32 sRecaptureSteps = 15;
    const s32 sTractionSpinSteps = 35;
    const s32 sReleaseHoldSteps = NebulaFrameClock::stepsOfMilliseconds(500);
    const s32 sHoldReleaseSteps = NebulaFrameClock::stepsOfSeconds(3);
    const s32 sCoolDownSteps = 10;
    const s32 sBreakSteps = 5;
    const f32 sRibbonLength = 180.0f;
    const f32 sRibbonGrowSpeed = 0.033333335f;
    const f32 sMinBindRadius = 75.0f;
    const f32 sMaxBindRadius = 130.0f;
    const f32 sTractSpinAccel = 0.002f;
    const f32 sTractionAccel = 0.7f;
    const f32 sNearTargetRange = 500.0f;
    const f32 sFriction = 0.99f;
    const f32 sNearTargetFriction = 0.75f;
    const f32 sRotateFriction = 0.992f;
    const f32 sPointerSpinPower = 0.055104f;
    const f32 sMaxSpinSpeed = 0.2f;
    const f32 sReflectSpeed = 25.0f;
    const f32 sPointRadius = 200.0f;
};  // namespace

namespace NrvGCapture {
    NEW_NERVE(GCaptureNrvWait, GCapture, Wait);
    NEW_NERVE_ONEND(GCaptureNrvCapture, GCapture, Capture, Capture);
    NEW_NERVE_ONEND(GCaptureNrvRecapture, GCapture, Recapture, Recapture);
    NEW_NERVE(GCaptureNrvTraction, GCapture, Traction);
    NEW_NERVE(GCaptureNrvHold, GCapture, Hold);
    NEW_NERVE(GCaptureNrvBreak, GCapture, Break);
    NEW_NERVE(GCaptureNrvCoolDown, GCapture, CoolDown);
};  // namespace NrvGCapture

GCapture::GCapture(const char* pName)
    : LiveActor(pName), mBindQuat(0.0f, 0.0f, 0.0f, 1.0f), mEffectPos(0, 0, 0), mEffectOffset(0, 0, 0), mRotateVelocity(0, 0, 0), mRotateAxis(0, 0, 1), mBindActor(nullptr),
      mTarget(nullptr), mTractTarget(nullptr), mCaptureRibbon(nullptr), mTargetMtx(nullptr), mTractPower(0.0f), mEffectBlend(0.0f), mRibbonRate(0.1f), mTargetKeepTime(0),
      mTargetDist(FLOAT_MAX), mGuidanceTime(0) {
    lumacore::setClass(this, lumacore::kClassId< GCapture >);
    mCaptureRibbon = new GCaptureRibbon("Gキャプチャーリボン");
    mTargetMtx = new CameraTargetMtx("カメラターゲットダミー");
    mRibbonPointMtx.identity();
}

void GCapture::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_MapObj, MR::DrawBufferType_None, MR::DrawBufferType_EnemyDecoration);
    initSound(8, false);
    initHitSensor(2);
    MR::addHitSensorRide(this, "body", 16, sPointRadius, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensor(this, "bind", 104, 16, 80.0f, Vec3f(0.0f, 0.0f, 0.0f));
    getSensor("bind")->invalidate();
    initBinder(sMinBindRadius, 0.0f, 8);
    initEffectKeeper(0, "GCapture");
    MR::setEffectHostSRT(this, "LightGrow", &mEffectPos, nullptr, nullptr);
    MR::setEffectHostSRT(this, "Light", &mEffectPos, nullptr, nullptr);
    MR::setEffectHostSRT(this, "LightBreak", &mEffectPos, nullptr, nullptr);
    MR::setEffectHostMtx(this, "LightSplash", mRibbonPointMtx.mMtx);
    MR::setEffectHostMtx(this, "RibbonPoint", mRibbonPointMtx.mMtx);
    MR::setEffectHostMtx(this, "RibbonBreak", mRibbonPointMtx.mMtx);
    mCaptureRibbon->initWithoutIter();
    initNerve(GET_NERVE(GCapture, GCaptureNrvWait));
    MR::initStarPointerTarget(this, sPointRadius, Vec3f(0.0f, 0.0f, 0.0f));
    makeActorAppeared();
    MR::invalidateClipping(this);
}

void GCapture::draw() const {
    mCaptureRibbon->draw();
}

void GCapture::control() {
    if (mGuidanceTime > 0) {
        MR::requestBlueStarGuidance();
        mGuidanceTime--;
    }

    // A target not requested again within the keep time is dropped.
    if (mTargetKeepTime > sTargetKeepSteps) {
        mTarget = nullptr;
        mTargetKeepTime = 0;
    }

    mTargetDist = FLOAT_MAX;

    if (mTarget != nullptr) {
        mTargetKeepTime++;
    }

    if (mBindActor != nullptr) {
        // The bound actor slides from where it was caught to the capture point.
        if (0.0f == mEffectBlend) {
            mEffectPos = mPosition;
        } else {
            mEffectPos = mPosition + mEffectOffset * MR::getEaseInOutValue(mEffectBlend, 0.0f, 1.0f, 1.0f);
        }

        calcBindActorPose();
        mRotateVelocity.scale(sRotateFriction);
        updateCameraTargetMatrix();
    }
}

void GCapture::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (mBindActor != nullptr && !MR::isSensorAutoRush(pSender)) {
        MR::tryGetItem(pSender, pReceiver);
    }
}

bool GCapture::receiveMsgEnemyAttack(u32 msg, HitSensor*, HitSensor*) {
    if (MR::isPlayerElementModeInvincible()) {
        return true;
    }

    return requestDamageCancel(msg);
}

bool GCapture::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (msg == ACTMES_IS_RUSH_TAKEOVER) {
        return isNerve(GET_NERVE(GCapture, GCaptureNrvCapture));
    }

    if (msg == ACTMES_AUTORUSH_BEGIN) {
        return requestBind(pSender);
    }

    if (msg == ACTMES_RUSH_CANCEL) {
        return requestCancelBind();
    }

    if (msg == ACTMES_UPDATE_BASEMTX && mBindActor != nullptr) {
        updateBindActorMatrix();
        return true;
    }

    return false;
}

bool GCapture::requestBind(HitSensor* pSensor) {
    if (!isNerve(GET_NERVE(GCapture, GCaptureNrvCapture))) {
        return false;
    }

    mBindActor = pSensor->mHost;
    mEffectPos = mBindActor->mPosition;
    mEffectOffset = mEffectPos - mPosition;
    mEffectBlend = 1.0f;
    MR::zeroVelocity(this);

    Pos3f pos;
    pos.set(mBindActor->getBaseMtx());
    pos.getQuat(mBindQuat);
    mRotateVelocity.zero();
    MR::startStarPointerModeBlueStar(this);
    MR::invalidateClipping(this);
    setNerve(GET_NERVE(GCapture, GCaptureNrvTraction));
    MR::emitEffect(this, "LightGrow");
    return true;
}

bool GCapture::requestCancelBind() {
    if (mBindActor == nullptr || !canCancelBind()) {
        return false;
    }

    mBindActor = nullptr;
    setNerve(GET_NERVE(GCapture, GCaptureNrvCoolDown));
    releaseTractTarget();
    MR::deleteEffectAll(this);
    MR::emitEffect(this, "LightBreak");
    MR::startSound(this, "SE_OJ_GCAPTURE_RELEASE");
    MR::endStarPointerMode(this);
    return true;
}

bool GCapture::requestDamageCancel(u32 msg) {
    if (mBindActor != nullptr && canCancelBind() && !MR::isPlayerElementModeInvincible()) {
        endBindByDamage(msg);
        setNerve(GET_NERVE(GCapture, GCaptureNrvCoolDown));
        return true;
    }

    return false;
}

void GCapture::reset() {
    if (mBindActor != nullptr) {
        MR::endBindAndPlayerWeakGravityLimitJump(this, mVelocity);
        mBindActor = nullptr;
        MR::endStarPointerMode(this);
    }

    releaseTractTarget();
    setNerve(GET_NERVE(GCapture, GCaptureNrvWait));
}

bool GCapture::tryCapture() {
    if (mTarget != nullptr && MR::isPressedJump(PLAYER_1)) {
        decideTractTarget();
        MR::setBinderRadius(this, sMinBindRadius);
        mTractTarget->getTargetPosition(&mPosition);
        setNerve(GET_NERVE(GCapture, GCaptureNrvCapture));
        return true;
    }

    return false;
}

bool GCapture::tryCancelCapture() {
    bool isOutOfRange = false;

    if (mTractTarget != nullptr) {
        Vec3f targetPos;
        mTractTarget->getTargetPosition(&targetPos);
        isOutOfRange = MR::calcDistance(this, targetPos) >= mTractTarget->getPointableRange();
    }

    if (MR::isGreaterStep(this, sCaptureCancelSteps) || (!MR::isHeldJump(PLAYER_1) && MR::isGreaterStep(this, sReleaseHoldSteps)) || isOutOfRange) {
        releaseTractTarget();
        setNerve(GET_NERVE(GCapture, GCaptureNrvWait));
        return true;
    }

    return false;
}

bool GCapture::tryEndTraction() {
    if (!MR::isHeldJump(PLAYER_1) && MR::isGreaterStep(this, sReleaseHoldSteps)) {
        releaseTractTarget();
        setNerve(GET_NERVE(GCapture, GCaptureNrvHold));
        return true;
    }

    return false;
}

bool GCapture::tryRecapture() {
    if (mTarget != nullptr && MR::isPressedJump(PLAYER_1)) {
        decideTractTarget();
        setNerve(GET_NERVE(GCapture, GCaptureNrvRecapture));
        return true;
    }

    return false;
}

bool GCapture::tryRetraction() {
    if (MR::isGreaterStep(this, sRecaptureSteps)) {
        setNerve(GET_NERVE(GCapture, GCaptureNrvTraction));
        return true;
    }

    return false;
}

// Spin and crouch break free with their own motion; a target can force the release, or release within its distance.
bool GCapture::tryBreak() {
    bool isBreak = false;

    if (MR::isSpin(PLAYER_1)) {
        MR::startBckPlayer("Spin2nd", "GCaptureCancel");
        isBreak = true;
    } else if (MR::isPressedCrouch(PLAYER_1)) {
        MR::startBckPlayer("GCaptureBreak", "GCaptureBreak");
        isBreak = true;
    } else if (mTractTarget != nullptr) {
        if (mTractTarget->isReleaseForce()) {
            isBreak = true;
        } else if (mTractTarget->releaseDistance() >= 0.0f) {
            Vec3f targetPos;
            mTractTarget->getTargetPosition(&targetPos);
            isBreak = mPosition.distance(targetPos) <= mTractTarget->releaseDistance();
        }
    }

    if (isBreak) {
        setNerve(GET_NERVE(GCapture, GCaptureNrvBreak));
    }

    return isBreak;
}

bool GCapture::tryFireDamage() {
    if (MR::isBindedDamageFire(this) && !MR::isPlayerElementModeInvincible()) {
        endBindByFireDamage();
        setNerve(GET_NERVE(GCapture, GCaptureNrvCoolDown));
        return true;
    }

    return false;
}

bool GCapture::tryRelease() {
    if (!MR::isGreaterStep(this, sHoldReleaseSteps) && !MR::isBinded(this)) {
        return false;
    }

    if (MR::sendMsgToBindedSensor(197, this, getSensor("body"))) {
        return false;
    }

    MR::startBckPlayer("Fall", "GCaptureFall");
    releaseTractTarget();
    setNerve(GET_NERVE(GCapture, GCaptureNrvBreak));
    return true;
}

void GCapture::exeCoolDown() {
    if (MR::isFirstStep(this)) {
        MR::offCalcGravity(this);
        mTractPower = 0.0f;
        MR::forceDeleteEffect(this, "Light");
        MR::zeroVelocity(this);
    }

    if (MR::isGreaterStep(this, sCoolDownSteps)) {
        setNerve(GET_NERVE(GCapture, GCaptureNrvWait));
    }
}

void GCapture::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::offCalcGravity(this);
        MR::offBind(this);
        mTractPower = 0.0f;
        MR::forceDeleteEffect(this, "Light");
    }

    tryCapture();
}

void GCapture::exeCapture() {
    Vec3f targetPos;
    mTractTarget->getTargetPosition(&targetPos);
    mPosition.set(*MR::getPlayerCenterPos());

    if (MR::isFirstStep(this)) {
        mRibbonRate = 0.1f;
        mRibbonPointMtx.identity();
        mRibbonPointMtx.setTrans(targetPos);
        MR::emitEffect(this, "RibbonPoint");
        MR::zeroVelocity(this);
        MR::onBind(this);
    }

    if (MR::isStep(this, sCaptureSteps)) {
        getSensor("bind")->validate();
    }

    updateRibbon(targetPos, sCaptureSteps);
    MR::reboundVelocityFromCollision(this, 0.5f, 0.0f, 1.0f);
    tryCancelCapture();
}

void GCapture::endCapture() {
    MR::deleteEffect(this, "RibbonPoint");
    MR::emitEffect(this, "RibbonBreak");
    getSensor("bind")->invalidate();
}

void GCapture::exeRecapture() {
    Vec3f targetPos;
    mTractTarget->getTargetPosition(&targetPos);

    if (MR::isFirstStep(this)) {
        mCaptureRibbon->setLine(targetPos, targetPos);
        mRibbonRate = 1.0f;
        mRibbonPointMtx.identity();
        mRibbonPointMtx.setTrans(targetPos);
        MR::emitEffect(this, "RibbonPoint");
        MR::onCalcGravity(this);
    } else {
        updateRibbon(targetPos, sRecaptureSteps);
    }

    downTractPower();
    MR::reboundVelocityFromCollision(this, 0.5f, 0.0f, 1.0f);
    fillGapBindTrans();
    MR::attenuateVelocity(this, sFriction);

    if (tryFireDamage() || tryRecapture() || tryRetraction() || tryBreak()) {
        return;
    }
}

void GCapture::endRecapture() {
    endCapture();
}

void GCapture::exeTraction() {
    Vec3f targetPos;
    mTractTarget->getTargetPosition(&targetPos);

    if (MR::isFirstStep(this)) {
        // Spins about a random axis square to the pull.
        MR::startBckPlayer("SpaceStruggle", "GCaptureTraction");
        Vec3f rand;
        MR::getRandomVector(&rand, 1.0f);
        MR::normalizeOrZero(&rand);
        Vec3f toTarget(targetPos - mPosition);
        MR::normalizeOrZero(&toTarget);
        mRotateAxis.cross(toTarget, rand);
        MR::normalizeOrZero(&mRotateAxis);
        MR::onCalcGravity(this);
        MR::emitEffect(this, "LightSplash");
    }

    if (MR::isStep(this, sCaptureSteps)) {
        MR::emitEffect(this, "Light");
    }

    if (MR::isLessStep(this, sTractionSpinSteps)) {
        mRotateVelocity += mRotateAxis * sTractSpinAccel;
    }

    f32 targetDist;
    Vec3f dir;
    MR::separateScalarAndDirection(&targetDist, &dir, targetPos - mPosition);

    if (mRibbonRate < 1.0f) {
        mRibbonRate += sRibbonGrowSpeed;
        if (mRibbonRate > 1.0f) {
            mRibbonRate = 1.0f;
        }
    }

    // The ribbon reaches out from the capture point up to its length, then spans the whole way.
    if (targetDist > sRibbonLength * mRibbonRate) {
        Vec3f ribbonEnd(mPosition + dir * mRibbonRate * sRibbonLength);
        mCaptureRibbon->setLine(targetPos, ribbonEnd);
        if (MR::isNearZero(dir)) {
            mRibbonPointMtx.setTrans(ribbonEnd);
        } else {
            MR::makeMtxUpNoSupportPos(&mRibbonPointMtx, dir, ribbonEnd);
        }
    } else {
        mCaptureRibbon->setLine(targetPos, targetPos);
        mRibbonPointMtx.identity();
        mRibbonPointMtx.setTrans(targetPos);
    }

    MR::setBinderRadius(this, (sMinBindRadius * (1.0f - mRibbonRate)) + (sMaxBindRadius * mRibbonRate));

    // Less friction near the target.
    f32 friction;
    if (targetDist < sNearTargetRange) {
        f32 farRate = targetDist / sNearTargetRange;
        f32 nearRate = 1.0f - farRate;
        friction = sFriction * farRate + (sNearTargetFriction * nearRate);
    } else {
        friction = sFriction;
    }

    mVelocity += dir * sTractionAccel * mTractPower;
    MR::attenuateVelocity(this, friction);
    upTractPower();
    MR::reboundVelocityFromCollision(this, 0.5f, 0.0f, 1.0f);
    tryAddVelocityReflectJumpCollision();
    fillGapBindTrans();

    if (tryFireDamage() || tryEndTraction() || tryBreak() || tryRecapture()) {
        MR::deleteEffect(this, "LightSplash");
        MR::emitEffect(this, "Light");
    }
}

void GCapture::exeHold() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("SpaceWait", "GCaptureHold");
        MR::onCalcGravity(this);
    }

    MR::startLevelSound(this, "SE_OJ_LV_GCAPTURE_CAPTURE");
    downTractPower();
    fillGapBindTrans();
    MR::reboundVelocityFromCollision(this, 0.5f, 0.0f, 1.0f);
    bool isReflected = tryAddVelocityReflectJumpCollision();
    // Rumbles while pointed at.
    MR::isStarPointerPointing(this, 0, true, "弱");
    addRotateAccelPointing();
    MR::attenuateVelocity(this, sFriction);
    MR::addTransMtx(mRibbonPointMtx, mVelocity);

    if (tryFireDamage() || tryRecapture() || tryBreak()) {
        return;
    }

    if (!isReflected) {
        tryRelease();
    }
}

void GCapture::exeBreak() {
    if (MR::isFirstStep(this)) {
        // The bound actor turns upright against gravity.
        Vec3f gravity;
        MR::calcGravityVector(this, mEffectPos, &gravity, nullptr, 0);
        Vec3f yDir;
        mBindQuat.getYDir(yDir);
        Quat4f rot;
        rot.setRotate(yDir, -gravity);
        mBindQuat.mult(rot, mBindQuat);
        mRotateVelocity.zero();
        MR::emitEffect(this, "LightBreak");
        MR::startSound(this, "SE_OJ_GCAPTURE_RELEASE");
    }

    if (mEffectBlend > 0.0f) {
        mEffectBlend -= 0.2f;
        if (mEffectBlend < 0.0f) {
            mEffectBlend = 0.0f;
        }
    }

    MR::attenuateVelocity(this, sFriction);

    if (MR::isGreaterStep(this, sBreakSteps)) {
        setNerve(GET_NERVE(GCapture, GCaptureNrvWait));
        releaseTractTarget();
        MR::endBindAndPlayerWeakGravityLimitJump(this, mVelocity);
        mBindActor = nullptr;
        MR::endStarPointerMode(this);
    }
}

void GCapture::endBindByDamage(s32 msg) {
    MR::endBindAndPlayerDamageMsg(this, msg);
    endBindAfterDamage();
}

void GCapture::endBindByFireDamage() {
    MR::endBindAndPlayerFireDamage(this);
    endBindAfterDamage();
}

void GCapture::endBindAfterDamage() {
    mBindActor = nullptr;
    MR::deleteEffectAll(this);
    MR::emitEffect(this, "LightBreak");
    MR::startSound(this, "SE_OJ_GCAPTURE_RELEASE");
    releaseTractTarget();
    MR::endStarPointerMode(this);
}

void GCapture::calcBindActorPose() {
    f32 angle;
    Vec3f axis;
    Quat4f rot;
    MR::separateScalarAndDirection(&angle, &axis, mRotateVelocity);
    rot.setRotate(axis, angle);
    mBindQuat.mult(rot, mBindQuat);
    mBindQuat.normalize();
}

void GCapture::updateBindActorMatrix() {
    Pos3f pos;
    pos.setQT(mBindQuat, mEffectPos);
    MR::setBaseTRMtx(mBindActor, pos);
}

bool GCapture::canRequestTarget() const {
    return !MR::isHeldJump(PLAYER_1);
}

bool GCapture::canCancelBind() const {
    return isNerve(GET_NERVE(GCapture, GCaptureNrvTraction)) || isNerve(GET_NERVE(GCapture, GCaptureNrvHold)) ||
           isNerve(GET_NERVE(GCapture, GCaptureNrvRecapture));
}

void GCapture::upTractPower() {
    mTractPower += 0.97f;
    mTractPower = MR::clamp(mTractPower, 0.0f, 1.0f);
}

void GCapture::downTractPower() {
    mTractPower -= 0.12f;
    mTractPower = MR::clamp(mTractPower, 0.0f, 1.0f);
}

void GCapture::fillGapBindTrans() {
    if (mEffectBlend > 0.0f) {
        mEffectBlend -= 0.05f;
        if (mEffectBlend < 0.0f) {
            mEffectBlend = 0.0f;
        }
    }
}

// The ribbon shoots from the target toward the capture point, stopping its length short, easing in over `step`.
void GCapture::updateRibbon(const Vec3f& rTargetPos, s32 step) {
    Vec3f span(mPosition - rTargetPos);
    Vec3f dir;
    MR::normalizeOrZero(span, &dir);
    span -= dir * mRibbonRate * sRibbonLength;
    f32 rate = MR::calcNerveRate(this, step);
    Vec3f ribbonEnd(rTargetPos + span * (rate * rate));
    mCaptureRibbon->setLine(rTargetPos, ribbonEnd);
    updateRibbonPointEffectMatrix(ribbonEnd);
}

// The point effect moves to `rPos`, its up along the way it moved.
void GCapture::updateRibbonPointEffectMatrix(const Vec3f& rPos) {
    Vec3f prevPos;
    mRibbonPointMtx.getTrans(prevPos);
    Vec3f up = prevPos - rPos;

    if (MR::isNearZero(up)) {
        mRibbonPointMtx.setTrans(rPos);
    } else {
        MR::makeMtxUpNoSupportPos(&mRibbonPointMtx, up, rPos);
    }
}

void GCapture::addRotateAccelPointing() {
    Vec3f rotate;
    if (MR::calcStarPointerStrokeRotateMoment(&rotate, mPosition, sPointRadius, 0)) {
        mRotateVelocity += rotate * sPointerSpinPower;
        f32 speed = mRotateVelocity.length();
        if (speed > sMaxSpinSpeed) {
            mRotateVelocity.scale(sMaxSpinSpeed / speed);
        }
    }
}

bool GCapture::tryAddVelocityReflectJumpCollision() {
    bool isReflected = MR::sendMsgToBindedSensor(196, this, getSensor("body"));
    if (isReflected) {
        MR::addVelocityToCollisionNormal(this, sReflectSpeed);

        f32 speed = mVelocity.length();
        if (speed > sReflectSpeed) {
            mVelocity.scale(sReflectSpeed / speed);
        }
    }

    return isReflected;
}

// The target nearest the pointer on screen this frame wins.
bool GCapture::requestTarget(GCaptureTargetable* pTarget) {
    if (!canRequestTarget()) {
        return false;
    }

    Vec2f pointerPos = *MR::getStarPointerScreenPosition(PLAYER_1);
    Vec3f targetPos;
    pTarget->getTargetPosition(&targetPos);
    Vec2f screenPos;
    MR::calcScreenPosition(&screenPos, targetPos);
    f32 dist = screenPos.distance(pointerPos);
    if (dist < mTargetDist) {
        mTargetDist = dist;
        mTarget = pTarget;
        mTargetKeepTime = 0;
        return true;
    }

    return false;
}

bool GCapture::isRequestedTarget(GCaptureTargetable* pTarget) {
    return pTarget != nullptr && mTarget == pTarget;
}

void GCapture::decideTractTarget() {
    if (mTractTarget != mTarget) {
        releaseTractTarget();
        mTarget->decidedTarget();
        mTractTarget = mTarget;
        mCaptureRibbon->reset();
        mTarget = nullptr;
    }
}

void GCapture::releaseTractTarget() {
    if (mTractTarget != nullptr) {
        mTractTarget->releasedTarget();
        mCaptureRibbon->reset();
    }

    mTractTarget = nullptr;
}

void GCapture::updateCameraTargetMatrix() {
    Pos3f pos;
    pos.identity();

    if (mBindActor != nullptr) {
        pos.setTrans(*MR::getPlayerPos());
    } else {
        pos.setTrans(mEffectPos);
    }

    mTargetMtx->mMatrix.set(pos.mMtx);
}

namespace MR {
    void createGCapture() {
        MR::createSceneObj(SceneObj_GCapture);
    }

    void resetGCapture() {
        if (MR::isExistSceneObj(SceneObj_GCapture)) {
            GCapture* gCapture = static_cast< GCapture* >(MR::getSceneObjHolder()->getObj(SceneObj_GCapture));
            gCapture->reset();
        }
    }

    bool requestGCaptureTarget(GCaptureTargetable* pTarget) {
        GCapture* gCapture = static_cast< GCapture* >(MR::getSceneObjHolder()->getObj(SceneObj_GCapture));
        return gCapture->requestTarget(pTarget);
    }

    void unrequestGCaptureTarget(GCaptureTargetable* pTarget) {
        GCapture* gCapture = static_cast< GCapture* >(MR::getSceneObjHolder()->getObj(SceneObj_GCapture));
        if (gCapture->isRequestedTarget(pTarget)) {
            gCapture->mTarget = nullptr;
        }
    }

    bool isRequestedGCaptureTarget(GCaptureTargetable* pTarget) {
        GCapture* gCapture = static_cast< GCapture* >(MR::getSceneObjHolder()->getObj(SceneObj_GCapture));
        return gCapture->isRequestedTarget(pTarget);
    }

    void noticeInTouchableRange() {
        GCapture* gCapture = static_cast< GCapture* >(MR::getSceneObjHolder()->getObj(SceneObj_GCapture));
        gCapture->mGuidanceTime = sGuidanceSteps;
    }

    bool isOnTractTrigger() {
        return MR::isPressedJump(PLAYER_1);
    }

    bool isPlayerGCaptured() {
        if (!MR::isExistSceneObj(SceneObj_GCapture)) {
            return false;
        }

        GCapture* gCapture = static_cast< GCapture* >(MR::getSceneObjHolder()->getObj(SceneObj_GCapture));
        return gCapture != nullptr && gCapture->mBindActor != nullptr;
    }
};  // namespace MR
