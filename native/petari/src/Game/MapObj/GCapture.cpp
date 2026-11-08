#include "Game/MapObj/GCapture.hpp"
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
#include "Game/Util/SpringValue.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "math_types.hpp"
#include <nebula_geometry.hpp>

#include "Game/System/NebulaInput.hpp"

namespace {
    // sMinActiveTime
    // sMaxHoldTime
    // sHitEnableTime
    // sPointRadius
    // sBreakTime
    // sWandSpinStartScreenSpeed
    // sWandSpinMaxScreenSpeed
    // sWandSpinPower
    // sWandTouchRefrectPower
    // sWandMaxSpinSpeed
    // sRollMotionStartSpinSpeed
    // sFreeSpinXPower
    // sCaptureAccel
    // sCaptureFric
    // sCaptureTime
    // sCaptureCancelTime
    // sRecaptureTime
    // sGapFillSpeed
    // sInTouchableRangeMarginTime
    // sNormalFreq
    // sNearTargetFreq
    // sTractionPower
    // sNearParamStartRange
    // sTractionPowerUpSpeed
    // sTractionPowerDownSpeed
    // sTractLineDispTime
    // sTractSpinTime
    // sTractSpinAccel
    // sTractSpinFreq
    // sGroundReboundRate
    // sReflectAccel
    // sMinBindRadius
    // sMaxBindRadius
    // sMaxBubbleRadius
    // sBubbleGrowSpeed
    // sStartActiveEffectTime
    // sFreeSpinYPower
    // sNearTargetTractionPower
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
    : LiveActor(pName), mBindQuat(0.0f, 0.0f, 0.0f, 1.0f), mEffectPos(0, 0, 0), mEffectOffset(0, 0, 0), mRotateVelocity(0, 0, 0), mRotateAxis(0, 0, 1), mBindActor(), mTarget(),
      mTractTarget(), mCaptureRibbon(), mTractPower(), mEffectBlend(), mRibbonRate(0.1f), mTargetKeepTime(), mTargetDist(FLOAT_MAX), mGuidanceTime(), mIsStarPointerPointing() {
    mCaptureRibbon = new GCaptureRibbon("Gキャプチャーリボン");
    mTargetMtx = new CameraTargetMtx("カメラターゲットダミー");
    mSpringValue = new SpringValue(1.0f, 1.0f, 0.05f, 0.91f, 0.0f);
    mRibbonPointMtx.identity();
}

void GCapture::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_MapObj, MR::DrawBufferType_None, MR::DrawBufferType_EnemyDecoration);
    initSound(8, false);
    initHitSensor(2);
    MR::addHitSensorRide(this, "body", 16, 200.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensor(this, "bind", 104, 16, 80.0f, Vec3f(0.0f, 0.0f, 0.0f));
    getSensor("bind")->invalidate();
    initBinder(75.0f, 0.0f, 8);
    initEffectKeeper(0, "GCapture");
    MR::setEffectHostSRT(this, "LightGrow", &mEffectPos, nullptr, nullptr);
    MR::setEffectHostSRT(this, "Light", &mEffectPos, nullptr, nullptr);
    MR::setEffectHostSRT(this, "LightBreak", &mEffectPos, nullptr, nullptr);
    MR::setEffectHostMtx(this, "LightSplash", mRibbonPointMtx.mMtx);
    MR::setEffectHostMtx(this, "RibbonPoint", mRibbonPointMtx.mMtx);
    MR::setEffectHostMtx(this, "RibbonBreak", mRibbonPointMtx.mMtx);
    mCaptureRibbon->initWithoutIter();
    initNerve(GET_NERVE(GCapture, GCaptureNrvWait));
    MR::initStarPointerTarget(this, 200.0f, Vec3f(0.0f, 0.0f, 0.0f));
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

    if (mTargetKeepTime > 30) {
        mTarget = nullptr;
        mTargetKeepTime = 0;
    }

    mTargetDist = FLOAT_MAX;

    if (mTarget != nullptr) {
        mTargetKeepTime++;
    }

    if (mBindActor != nullptr) {
        if (0.0f == mEffectBlend) {
            mEffectPos = mPosition;
        } else {
            mEffectPos = mPosition + mEffectOffset * MR::getEaseInOutValue(mEffectBlend, 0.0f, 1.0f, 1.0f);
        }

        calcBindActorPose();
        mRotateVelocity.x *= 0.992f;
        mRotateVelocity.y *= 0.992f;
        mRotateVelocity.z *= 0.992f;
        updateCameraTargetMatrix();
    }
}

void GCapture::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (mBindActor != nullptr && !MR::isSensorAutoRush(pSender) && MR::tryGetItem(pSender, pReceiver)) {
        return;
    }
}

bool GCapture::receiveMsgEnemyAttack(u32 msg, HitSensor*, HitSensor*) {
    if (MR::isPlayerElementModeInvincible()) {
        return true;
    }

    return requestDamageCancel(msg);
}

bool GCapture::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (msg == ACTMES_IS_RUSH_TAKEOVER) {
        return isNerve(GET_NERVE(GCapture, GCaptureNrvCapture));
    } else if (msg == ACTMES_AUTORUSH_BEGIN) {
        return requestBind(pSender);
    } else if (msg == ACTMES_RUSH_CANCEL) {
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
    if (mBindActor != nullptr && canCancelBind()) {
        mBindActor = nullptr;
        setNerve(GET_NERVE(GCapture, GCaptureNrvCoolDown));
        releaseTractTarget();
        MR::deleteEffectAll(this);
        MR::emitEffect(this, "LightBreak");
        MR::startSound(this, "SE_OJ_GCAPTURE_RELEASE");
        MR::endStarPointerMode(this);
        return true;
    }

    return false;
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
        MR::setBinderRadius(this, 75.0f);
        mTractTarget->getTargetPosition(&mPosition);
        setNerve(GET_NERVE(GCapture, GCaptureNrvCapture));
        return true;
    }

    return false;
}

bool GCapture::tryCancelCapture() {
    bool ret = false;

    if (mTractTarget != nullptr) {
        Vec3f position;
        mTractTarget->getTargetPosition(&position);
        if (MR::calcDistance(this, position) >= mTractTarget->getPointableRange()) {
            ret = true;
        }
    }

    if (MR::isGreaterStep(this, 40) || (!MR::isHeldJump(PLAYER_1) && MR::isGreaterStep(this, 30)) || ret) {
        releaseTractTarget();
        setNerve(GET_NERVE(GCapture, GCaptureNrvWait));
        return true;
    }

    return false;
}

bool GCapture::tryEndTraction() {
    if (!MR::isHeldJump(PLAYER_1) && MR::isGreaterStep(this, 30)) {
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
    if (MR::isGreaterStep(this, 15)) {
        setNerve(GET_NERVE(GCapture, GCaptureNrvTraction));
        return true;
    }

    return false;
}

bool GCapture::tryBreak() {
    s32 channel = PLAYER_1;

    if (MR::isSpin(channel)) {
        MR::startBckPlayer("Spin2nd", "GCaptureCancel");
        channel = PLAYER_2;
    } else if (MR::isPressedCrouch(channel)) {
        MR::startBckPlayer("GCaptureBreak", "GCaptureBreak");
        channel = PLAYER_2;
    } else if (mTractTarget != nullptr) {
        if (mTractTarget->isReleaseForce()) {
            channel = PLAYER_2;
        } else if ((mTractTarget->releaseDistance() >= 0.0f) != false) {
            Vec3f targetPos;
            mTractTarget->getTargetPosition(&targetPos);
            if (mPosition.distance(targetPos) <= mTractTarget->releaseDistance()) {
                channel = PLAYER_2;
            }
        }
    }

    if (channel != PLAYER_1) {
        setNerve(GET_NERVE(GCapture, GCaptureNrvBreak));
        return true;
    }

    return false;
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
    if (MR::isGreaterStep(this, 180) || MR::isBinded(this)) {
        if (MR::sendMsgToBindedSensor(197, this, getSensor("body"))) {
            return false;
        }

        MR::startBckPlayer("Fall", "GCaptureFall");
        releaseTractTarget();
        setNerve(GET_NERVE(GCapture, GCaptureNrvBreak));
        return true;
    }

    return false;
}

void GCapture::exeCoolDown() {
    if (MR::isFirstStep(this)) {
        MR::offCalcGravity(this);
        mTractPower = 0.0f;
        MR::forceDeleteEffect(this, "Light");
        MR::zeroVelocity(this);
    }

    if (MR::isGreaterStep(this, 10)) {
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

    if (tryCapture()) {
        return;
    }
}

void GCapture::exeCapture() {
    Vec3f pos;
    mTractTarget->getTargetPosition(&pos);
    mPosition.set(*MR::getPlayerCenterPos());

    if (MR::isFirstStep(this)) {
        mRibbonRate = 0.1f;
        mRibbonPointMtx.identity();
        mRibbonPointMtx.setTrans(pos);
        MR::emitEffect(this, "RibbonPoint");
        MR::zeroVelocity(this);
        MR::onBind(this);
    }

    if (MR::isStep(this, 30)) {
        getSensor("bind")->validate();
    }

    updateRibbon(pos, 30);
    MR::reboundVelocityFromCollision(this, 0.5f, 0.0f, 1.0f);

    if (tryCancelCapture()) {
        return;
    }
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
        mCaptureRibbon->lengthen(targetPos, targetPos);
        mRibbonRate = 1.0f;
        mRibbonPointMtx.identity();
        mRibbonPointMtx.setTrans(targetPos);
        MR::emitEffect(this, "RibbonPoint");
        MR::onCalcGravity(this);
    } else {
        updateRibbon(targetPos, 15);
    }

    downTractPower();
    MR::reboundVelocityFromCollision(this, 0.5f, 0.0f, 1.0f);
    fillGapBindTrans();
    MR::attenuateVelocity(this, 0.99f);

    if (tryFireDamage() || tryRecapture() || tryRetraction() || tryBreak()) {
        return;
    }
}

void GCapture::endRecapture() {
    MR::deleteEffect(this, "RibbonPoint");
    MR::emitEffect(this, "RibbonBreak");
    getSensor("bind")->invalidate();
}

void GCapture::exeTraction() {
    Vec3f targetPos, rand, v22, dir, v20;
    mTractTarget->getTargetPosition(&targetPos);

    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("SpaceStruggle", "GCaptureTraction");
        MR::getRandomVector(&rand, 1.0f);
        MR::normalizeOrZero(&rand);
        v22.set(targetPos - mPosition);
        MR::normalizeOrZero(&v22);
        mRotateAxis.cross(v22, rand);
        MR::normalizeOrZero(&mRotateAxis);
        MR::onCalcGravity(this);
        MR::emitEffect(this, "LightSplash");
    }

    if (MR::isStep(this, 30)) {
        MR::emitEffect(this, "Light");
    }

    if (MR::isLessStep(this, 35)) {
        mRotateVelocity += mRotateAxis * 0.002f;
    }

    f32 scalar;
    MR::separateScalarAndDirection(&scalar, &dir, targetPos - mPosition);

    if (mRibbonRate < 1.0f) {
        mRibbonRate += 0.033333335f;
        if (mRibbonRate > 1.0f) {
            mRibbonRate = 1.0f;
        }
    }

    if (scalar > 180.0f * mRibbonRate) {
        v20.set(mPosition + dir * mRibbonRate * 180.0f);
        mCaptureRibbon->shorten(targetPos, v20);
        if (MR::isNearZero(dir)) {
            mRibbonPointMtx.setTrans(v20);
        } else {
            MR::makeMtxUpNoSupportPos(&mRibbonPointMtx, dir, v20);
        }
    } else {
        mCaptureRibbon->shorten(targetPos, targetPos);
        mRibbonPointMtx.identity();
        mRibbonPointMtx.setTrans(targetPos);
    }

    MR::setBinderRadius(this, (75.0f * (1.0f - mRibbonRate)) + (130.0f * mRibbonRate));
    f32 velocity;

    if (scalar < 500.0f) {
        f32 tmp = scalar / 500.0f;
        f32 tmp2 = 1.0f - tmp;
        velocity = 0.99f * tmp + (0.75f * tmp2);  // lerp inline, cleanup needed
    } else {
        velocity = 0.99f;
    }

    mVelocity += dir * 0.7f * mTractPower;
    MR::attenuateVelocity(this, velocity);
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
    bool tryAdd = tryAddVelocityReflectJumpCollision();
    mIsStarPointerPointing = MR::isStarPointerPointing(this, 0, true, "弱");
    addRotateAccelPointing();
    MR::attenuateVelocity(this, 0.99f);
    MR::addTransMtx(mRibbonPointMtx, mVelocity);

    if (tryFireDamage() || tryRecapture() || tryBreak()) {
        return;
    }

    if (tryAdd || tryRelease()) {
        return;
    }
}

void GCapture::exeBreak() {
    Vec3f dest, yDir;
    Quat4f rot;
    if (MR::isFirstStep(this)) {
        MR::calcGravityVector(this, mEffectPos, &dest, nullptr, 0);
        mBindQuat.getYDir(yDir);
        rot.setRotate(yDir, -dest);
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

    MR::attenuateVelocity(this, 0.99f);

    if (MR::isGreaterStep(this, 5)) {
        setNerve(GET_NERVE(GCapture, GCaptureNrvWait));
        releaseTractTarget();
        MR::endBindAndPlayerWeakGravityLimitJump(this, mVelocity);
        mBindActor = nullptr;
        MR::endStarPointerMode(this);
    }
}

void GCapture::endBindByDamage(s32 a1) {
    MR::endBindAndPlayerDamageMsg(this, a1);
    mBindActor = nullptr;
    MR::deleteEffectAll(this);
    MR::emitEffect(this, "LightBreak");
    MR::startSound(this, "SE_OJ_GCAPTURE_RELEASE");
    releaseTractTarget();
    MR::endStarPointerMode(this);
}

void GCapture::endBindByFireDamage() {
    MR::endBindAndPlayerFireDamage(this);
    mBindActor = nullptr;
    MR::deleteEffectAll(this);
    MR::emitEffect(this, "LightBreak");
    MR::startSound(this, "SE_OJ_GCAPTURE_RELEASE");
    releaseTractTarget();
    MR::endStarPointerMode(this);
}

void GCapture::calcBindActorPose() {
    f32 scalar;
    Vec3f dir;
    Quat4f rot;
    MR::separateScalarAndDirection(&scalar, &dir, mRotateVelocity);
    rot.setRotate(dir, scalar);
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
    if (isNerve(GET_NERVE(GCapture, GCaptureNrvTraction)) || isNerve(GET_NERVE(GCapture, GCaptureNrvHold)) ||
        isNerve(GET_NERVE(GCapture, GCaptureNrvRecapture))) {
        return true;
    }

    return false;
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

void GCapture::updateRibbon(const Vec3f& rVec, s32 step) {
    Vec3f v13(mPosition - rVec);
    Vec3f v12;
    MR::normalizeOrZero(v13, &v12);
    v13 -= v12 * mRibbonRate * 180.0f;
    f32 rate = MR::calcNerveRate(this, step);
    Vec3f v11(rVec + v13 * (rate * rate));
    mCaptureRibbon->lengthen(rVec, v11);
    updateRibbonPointEffectMatrix(v11);
}

void GCapture::updateRibbonPointEffectMatrix(const Vec3f& rVec) {
    Vec3f v5;
    mRibbonPointMtx.getTrans(v5);
    Vec3f v4 = v5 - rVec;

    if (MR::isNearZero(v4)) {
        mRibbonPointMtx.setTrans(rVec);
    } else {
        MR::makeMtxUpNoSupportPos(&mRibbonPointMtx, v4, rVec);
    }
}

void GCapture::addRotateAccelPointing() {
    Vec3f rotate;
    if (MR::calcStarPointerStrokeRotateMoment(&rotate, mPosition, 200.0f, 0)) {
        mRotateVelocity += rotate * 0.055104f;
        f32 mag = mRotateVelocity.length();
        if (mag > 0.2f) {
            mRotateVelocity.scale(0.2f / mag);
        }
    }
}

bool GCapture::tryAddVelocityReflectJumpCollision() {
    bool ret = MR::sendMsgToBindedSensor(196, this, getSensor("body"));
    if (ret) {
        MR::addVelocityToCollisionNormal(this, 25.0f);

        f32 len = mVelocity.length();

        if (len > 25.0f) {
            mVelocity.scale(25.0f / len);
        }
    }

    return ret;
}

bool GCapture::requestTarget(GCaptureTargetable* gCaptureTarget) {
    Vec3f targetPos;
    if (canRequestTarget()) {
        Vec2f pointerPos = *MR::getStarPointerScreenPosition(PLAYER_1);
        gCaptureTarget->getTargetPosition(&targetPos);
        Vec2f screenPos;
        MR::calcScreenPosition(&screenPos, targetPos);
        f32 dist = screenPos.distance(pointerPos);
        if (dist < mTargetDist) {
            mTargetDist = dist;
            mTarget = gCaptureTarget;
            mTargetKeepTime = false;
            return true;
        }
    }

    return false;
}

bool GCapture::isRequestedTarget(GCaptureTargetable* gCaptureTarget) {
    if (gCaptureTarget == nullptr) {
        return false;
    }

    return mTarget == gCaptureTarget;
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

    bool requestGCaptureTarget(GCaptureTargetable* gCaptureTarget) {
        GCapture* gCapture = static_cast< GCapture* >(MR::getSceneObjHolder()->getObj(SceneObj_GCapture));
        return gCapture->requestTarget(gCaptureTarget);
    }

    void unrequestGCaptureTarget(GCaptureTargetable* gCaptureTarget) {
        GCapture* gCapture = static_cast< GCapture* >(MR::getSceneObjHolder()->getObj(SceneObj_GCapture));
        if (gCapture->isRequestedTarget(gCaptureTarget)) {
            gCapture->mTarget = nullptr;
        }
    }

    bool isRequestedGCaptureTarget(GCaptureTargetable* gCaptureTarget) {
        GCapture* gCapture = static_cast< GCapture* >(MR::getSceneObjHolder()->getObj(SceneObj_GCapture));
        return gCapture->isRequestedTarget(gCaptureTarget);
    }

    void noticeInTouchableRange() {
        GCapture* gCapture = static_cast< GCapture* >(MR::getSceneObjHolder()->getObj(SceneObj_GCapture));
        gCapture->mGuidanceTime = 20;
    }

    bool isOnTractTrigger() {
        return MR::isPressedJump(PLAYER_1) != false;
    }

    bool isPlayerGCaptured() {
        if (!MR::isExistSceneObj(SceneObj_GCapture)) {
            return false;
        }
        GCapture* gCapture = static_cast< GCapture* >(MR::getSceneObjHolder()->getObj(SceneObj_GCapture));
        if (gCapture == nullptr) {
            return false;
        }

        return gCapture->mBindActor;
    }
};  // namespace MR
