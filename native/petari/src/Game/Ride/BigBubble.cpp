#include "Game/Util/CameraUtil.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/BezierSurface.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/MapObj/BigBubbleDrawer.hpp"
#include "Game/Ride/BigBubble.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/AreaObj/BigBubbleGoalArea.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/BigBubbleMoveLimitter.hpp"
#include "Game/MapObj/BigFanHolder.hpp"
#include "Game/MapObj/ElectricRailHolder.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util.hpp"
#include "Game/Util/OctahedronBezierSurface.hpp"
#include <nebula_geometry.hpp>
#include <nebula_math.h>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 1.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_setShapeType_f1, "setShapeType.f1", 0.6f);
NEBULA_TUNABLE_NAMED(f32, cNum_setShapeType_f2, "setShapeType.f2", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_setShapeType_f3, "setShapeType.f3", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_setScale_f1, "setScale.f1", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_setScale_f2, "setScale.f2", 0.95f);
NEBULA_TUNABLE_NAMED(f32, cNum_requestAssimilate_f1, "requestAssimilate.f1", 8.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAppear_f1, "exeAppear.f1", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_addDeformVelocityInternalPressure_f1, "addDeformVelocityInternalPressure.f1", 0.125f);
NEBULA_TUNABLE_NAMED(f32, cNum_addDeformVelocityInternalPressure_f2, "addDeformVelocityInternalPressure.f2", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_addAccelPointing_f1, "addAccelPointing.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateCaptureReduceVolume_f1, "updateCaptureReduceVolume.f1", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateCaptureReduceVolume_f2, "updateCaptureReduceVolume.f2", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateCaptureWarningColor_f1, "updateCaptureWarningColor.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateCaptureWarningColor_f2, "updateCaptureWarningColor.f2", 128.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_control_setBinderRadius_1, "control.setBinderRadius.1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_setBinderRadius_2, "control.setBinderRadius.2", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_generate_setBinderRadius_1, "generate.setBinderRadius.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setScale_setBinderRadius_1, "setScale.setBinderRadius.1", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateNormalVelocity_reboundVelocityFromEachCollision_1, "updateNormalVelocity.reboundVelocityFromEachCollision.1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateNormalVelocity_reboundVelocityFromEachCollision_2, "updateNormalVelocity.reboundVelocityFromEachCollision.2", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateNormalVelocity_reboundVelocityFromEachCollision_3, "updateNormalVelocity.reboundVelocityFromEachCollision.3", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateNormalVelocity_reboundVelocityFromEachCollision_4, "updateNormalVelocity.reboundVelocityFromEachCollision.4", 0.0f);

namespace {
    NEBULA_TUNABLE(f32, sNormalFric, 0.992f);
    NEBULA_TUNABLE(f32, sBuoyancyAccel, 0.03f);
    NEBULA_TUNABLE(f32, sPushedAccel, 0.18f);
    NEBULA_TUNABLE(f32, sPushDeformAccel, 1.1f);
    NEBULA_TUNABLE(f32, sNeedPushMoveMinRate, 0.9f);
    NEBULA_TUNABLE(f32, sNeedPushDeformMinRate, 0.9f);
    NEBULA_TUNABLE(f32, sBaseRadiusSphere, 150.0f);
    NEBULA_TUNABLE(f32, sBaseRadiusCube, 180.0f);
    NEBULA_TUNABLE(f32, sBaseRadiusOctahedron, 120.0f);
    NEBULA_TUNABLE(f32, sPointAccelMin, 0.0f);
    NEBULA_TUNABLE(f32, sPointAccelMax, 1.0f);
    NEBULA_TUNABLE(f32, sPointAccelDistanceMin, 50.0f);
    NEBULA_TUNABLE(f32, sPointAccelDistanceMax, 1000.0f);
    NEBULA_TUNABLE(f32, sPointAccelDeformPowerRate, 1.5f);
    NEBULA_TUNABLE(f32, sStarPieceDeformPower, 15.0f);
    NEBULA_TUNABLE(f32, sMeshScaleFreq, 0.93f);
    NEBULA_TUNABLE(f32, sPushMeshPower, 0.02f);
    NEBULA_TUNABLE(f32, sPressPower, 10.0f);
    NEBULA_TUNABLE(f32, sAccelDeformPower, 0.8f);
    NEBULA_TUNABLE(f32, sMaxDeformSpeed, 10.0f);
    NEBULA_TUNABLE(f32, sSpinStartScreenSpeed, 0.03f);
    NEBULA_TUNABLE(f32, sSpinMaxScreenSpeed, 0.2f);
    NEBULA_TUNABLE(f32, sSpinPower, 0.03f);
    NEBULA_TUNABLE(f32, sSpinFreq, 0.99f);
    NEBULA_TUNABLE(f32, sMaxSpinSpeed, 0.2f);
    NEBULA_TUNABLE(f32, sCaptureStartVolume, 0.75f);
    NEBULA_TUNABLE(f32, sCaptureEndVolume, 0.25f);
    NEBULA_TUNABLE(f32, sWindMinDistance, 0.0f);
    NEBULA_TUNABLE(f32, sWindMaxDistance, 0.5f);
    NEBULA_TUNABLE(f32, sWindAccel, 0.25f);
    NEBULA_TUNABLE(f32, sCorioriRotSpeed, 3.0f);
    NEBULA_TUNABLE(f32, sCoriolisAccelPower, 0.1f);
    static const s32 sReduceSizeInterval = NebulaFrameClock::stepsOfSeconds(2);
    NEBULA_TUNABLE(f32, sReduceVolume, 0.5f);
    NEBULA_TUNABLE(f32, sMaxVolume, 5.0f);
    static const s32 sAppearTime = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, sAppearAddMoment, 0.03f);
    NEBULA_TUNABLE(s32, sWarningCycle, 40);
    static const s32 sAutoBreakTime = NebulaFrameClock::stepsOfSeconds(20);
    NEBULA_TUNABLE(s32, sBreakTime, 20);
    static const s32 sEscapeTime = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, sEscapeVelocity, 30.0f);
    NEBULA_TUNABLE(f32, sMargeableRate, 0.9f);
    static const f32 sMergeDeformAccel = 0.75f;
    NEBULA_TUNABLE(s32, sMergedTime, 45);
    NEBULA_TUNABLE(f32, sMergeRotateRate, 0.2f);
    NEBULA_TUNABLE(f32, sMergeDirCorrection, 0.95f);
    NEBULA_TUNABLE(s32, sTurchDeformTime, 25);
    static const f32 sTurchDeformAccel = sMergeDeformAccel / 100.0f;
    NEBULA_TUNABLE(f32, sRadiusSubLevel2, 3.0f);
    NEBULA_TUNABLE(f32, sRadiusSubLevel3, 7.0f);
    NEBULA_TUNABLE(f32, sRadiusSubLevel4, 15.0f);
    NEBULA_TUNABLE(f32, sRadiusSubLevel5, 30.0f);
    NEBULA_TUNABLE(f32, sRadiusSubLevel6, 50.0f);
    NEBULA_TUNABLE(f32, sRadiusSubLevel1, 0.0f);
};  // namespace

enum Side { Side_Top = 0, Side_Right = 1, Side_Back = 2, Side_Left = 3, Side_Front = 4, Side_Bottom = 5 };

namespace NrvBigBubble {
    NEW_NERVE(BigBubbleNrvAppear, BigBubble, Appear);
    NEW_NERVE(BigBubbleNrvWait, BigBubble, Wait);
    NEW_NERVE_ONEND(BigBubbleNrvCapture, BigBubble, Capture, Capture);
    NEW_NERVE(BigBubbleNrvBreak, BigBubble, Break);
    NEW_NERVE(BigBubbleNrvEscape, BigBubble, Escape);
    NEW_NERVE(BigBubbleNrvGoal, BigBubble, Goal);
    NEW_NERVE(BigBubbleNrvMerged, BigBubble, Merged);
};  // namespace NrvBigBubble

BigBubble::BigBubble(const char* pName)
    : LiveActor(pName), mSurface(), mMoveLimitter(), mHost(), mRider(), mMergeBubble(), mBubbleQuat(0, 0, 0, 1), mRiderQuat(0, 0, 0, 1),
      mRiderPos(0.0f, 0.0f, 0.0f), mSpawnPosition(0.0f, 0.0f, 0.0f), mPointerPos(0.0f, 0.0f, 0.0f),
      mPushDirection(0.0f, 0.0f, 0.0f), mMoment(0.0f, 0.0f, 0.0f), mCoriolisAccel(1.0f, 0.0f, 0.0f), mInterpolateRate(), mPointAccel(),
      mPushInvalidTime(), mReduceVolumeTimer(), mMergeIndex(-1), mShapeType(-1), mAppearRadius(1.0f), mVolume(1.0f), mDrawZ(),
      mBinderRadius(), mWarningColor(255, 0, 0, 0), mIsObstruct(), mIsHidden(), mIsExitLimitter(), mIsBroken(), mIsShrinkable(true),
      mIsAttached(true) {
    mSurface = new OctahedronBezierSurface(BezierSurface::FLAG_NORMAL | BezierSurface::FLAG_TEX);
    mEffectMtx.identity();
    mBaseMtx.identity();

    for (s32 idx = 0; idx < 6; idx++) {
        mDeformCoeff[idx] = 1.0f;
        mDeformSpeed[idx] = 0.0f;
        mMergeBubbles[idx] = nullptr;
    }
}

void BigBubble::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    mBaseMtx.setTrans(mPosition);
    MR::connectToScene(this, MR::MovementType_Ride, MR::CalcAnimType_MapObj, MR::DrawBufferType_None, MR::DrawType_None);
    initSensor();
    initNerve(GET_NERVE(BigBubble, BigBubbleNrvWait));
    initBinder(getBaseRadius(), 0, 16);
    initEffectKeeper(0, "BigBubble");
    MR::setEffectHostMtx(this, "PushWind", mEffectMtx);
    initSound(6, false);
    MR::onCalcGravity(this);
    MR::initShadowVolumeSphere(this, getBaseRadius() * cNum_init_f1);
    MR::initStarPointerTarget(this, getBaseRadius(), Vec3f(0.0f, 0.0f, 0.0f));
    if (MR::isValidInfo(rIter))
        makeActorAppeared();
    else
        makeActorDead();
    MR::invalidateClipping(this);
}

void BigBubble::initSensor() {
    initHitSensor(3);
    MR::addHitSensorRide(this, "ride", 16, getBaseRadius(), Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensor(this, "body", ATYPE_BIG_BUBBLE, 16, getBaseRadius(), Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorBinder(this, "bind", 16, getBaseRadius(), Vec3f(0.0f, 0.0f, 0.0f));
}

void BigBubble::makeActorAppeared() {
    updateMeshPoint();
    mSurface->calcAnim();
    LiveActor::makeActorAppeared();
    setNerve(GET_NERVE(BigBubble, BigBubbleNrvAppear));

    for (s32 idx = 0; idx < 6; idx++) {
        mDeformSpeed[idx] = 0.0f;
        mDeformCoeff[idx] = 0.0f;
        mMergeBubbles[idx] = nullptr;
    }

    mMergeIndex = -1;
    mMergeBubble = nullptr;
    mIsExitLimitter = false;
    mIsBroken = false;
}

void BigBubble::makeActorDead() {
    mRider = nullptr;
    mHost = nullptr;
    LiveActor::makeActorDead();

    for (s32 idx = 0; idx < 6; idx++) {
        mDeformCoeff[idx] = 0.0f;
        mDeformSpeed[idx] = 0.0f;
        mMergeBubbles[idx] = nullptr;
    }

    mMergeBubble = nullptr;
}

void BigBubble::control() {
    mIsHidden = MR::isJudgedToClipFrustum(mPosition, getSize() * cNum_control_f1);

    if (!isNerve(GET_NERVE(BigBubble, BigBubbleNrvMerged))) {
        doMoveLimit();
        updatePose();
        if (!mIsHidden) {
            updateMeshPoint();
        }
    }

    if (mPushInvalidTime > 0) {
        mPushInvalidTime--;
    }

    f32 radius = MR::getBinderRadius(this);
    MR::setBinderRadius(this, cNum_control_setBinderRadius_1 * mBinderRadius + cNum_control_setBinderRadius_2 * radius);
}

void BigBubble::calcAnim() {
    if (isDraw()) {
        f32 radius = MR::calcPointRadius2D(mPosition, getSize());

        s32 divideLevel = 0;
        if (radius >= ::sRadiusSubLevel6) {
            divideLevel = 6;
        } else if (radius >= ::sRadiusSubLevel5) {
            divideLevel = 5;
        } else if (radius >= ::sRadiusSubLevel4) {
            divideLevel = 4;
        } else if (radius >= ::sRadiusSubLevel3) {
            divideLevel = 3;
        } else if (radius >= ::sRadiusSubLevel2) {
            divideLevel = 2;
        } else if (radius >= ::sRadiusSubLevel1) {
            divideLevel = 1;
        }

        mSurface->setDivideLevel(divideLevel);
        mSurface->calcAnim();
    }

    f32 dot = MR::getCamZdir().dot(mPosition);
    mDrawZ = dot + mScale.x;
}

void BigBubble::draw() const {
    if (isDraw()) {
        BigBubbleDrawer::setWarningColor(mWarningColor);
        TypedDraw site(BigBubbleDrawer::material(), nullptr, MR::getCameraViewMtx());
        mSurface->draw(site);
    }
}

void BigBubble::generate(const Vec3f& rPos, const Vec3f& rUp, f32 volume, bool isObstruct, s32 actionType, s32 shapeType,
                         const BigBubbleMoveLimitter* pLimitter) {
    setShapeType(shapeType);
    setActionType(actionType);

    mPosition.set(rPos);
    mBaseMtx.setQT(mBubbleQuat, mPosition);
    mMoveLimitter = pLimitter;
    if (mMoveLimitter != nullptr) {
        mMoveLimitter->limitPosition(&mPosition, getSize());
    }

    mIsObstruct = isObstruct;
    mAppearRadius = getRadius(volume);

    resetDeformVelocity();
    setScale(0.0f);
    MR::setBinderRadius(this, cNum_generate_setBinderRadius_1);
    mVolume = volume;

    MR::invalidateHitSensors(this);
    updateMeshPoint();
    makeActorAppeared();

    MR::getRandomVector(&mCoriolisAccel, 1.0f);
    mCoriolisAccel.orthogonalize(mGravity);
    MR::onBind(this);
    MR::offCalcGravity(this);
    mGravity.set(-rUp);
    mWarningColor.a = 0;
}

// The generator's action type: 0 the captured player steers it with the pointer, 1 it drifts; neither shrinks.
// Others keep the defaults.
void BigBubble::setActionType(s32 actionType) {
    switch (actionType) {
    case 0:
        mIsShrinkable = false;
        mIsAttached = true;
        break;
    case 1:
        mIsShrinkable = false;
        mIsAttached = false;
        break;
    }
}

void BigBubble::setShapeType(s32 shapeType) {
    mShapeType = shapeType;
    switch (mShapeType) {
    case ShapeType_Sphere:
        mSurface->setDiagonalRate(cNum_setShapeType_f1);
        break;
    case ShapeType_Cube:
        mSurface->setDiagonalRate(cNum_setShapeType_f2);
        break;
    case ShapeType_Octahedron:
        mSurface->setDiagonalRate(cNum_setShapeType_f3);
        break;
    default:
        break;
    }
}

f32 BigBubble::getBaseRadius() const {
    switch (mShapeType) {
    case ShapeType_Sphere:
        return ::sBaseRadiusSphere;
    case ShapeType_Cube:
        return ::sBaseRadiusCube;
    case ShapeType_Octahedron:
        return ::sBaseRadiusOctahedron;
    default:
        return ::sBaseRadiusSphere;
    }
}

void BigBubble::setHost(LiveActor* pHost) {
    mHost = pHost;
}

void BigBubble::setScale(f32 scale) {
    f32 radius = getBaseRadius();
    mScale.set(Vec3f(scale, scale, scale));
    MR::setStarPointerTargetRadius3d(this, radius * scale);
    MR::setShadowVolumeSphereRadius(this, nullptr, cNum_setScale_f1 * radius * scale);
    mBinderRadius = radius * scale * cNum_setScale_f2;
    MR::setBinderRadius(this, radius * scale * cNum_setScale_setBinderRadius_1);
    MR::setSensorRadius(this, "body", radius * scale);
    MR::setSensorRadius(this, "bind", radius * scale);
    MR::setSensorRadius(this, "ride", radius * scale);
}

void BigBubble::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if ((mRider == nullptr || !MR::tryGetItem(pSender, pReceiver)) && isPushable() && pSender->isType(ATYPE_BIG_BUBBLE) &&
        pReceiver->isType(ATYPE_BIG_BUBBLE)) {
        pReceiver->receiveMessage(ACTMES_PUSH, pSender);
        f32 otherScale = pReceiver->mHost->mScale.x;
        f32 thisScale = mScale.x;
        if (isNerve(GET_NERVE(BigBubble, BigBubbleNrvCapture)) || thisScale >= otherScale) {
            s32 mergeIndex = getMergeIndex(MR::getSensorPos(pReceiver));
            if (mergeIndex != -1 && pReceiver->receiveMessage(ACTMES_BIG_BUBBLE_MERGE, pSender)) {
                mReduceVolumeTimer = 0;
                mMergeBubbles[mergeIndex] = static_cast< BigBubble* >(pReceiver->mHost);
            }
        }
    }
}

bool BigBubble::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgStarPieceReflect(msg)) {
        Vec3f sensorDir;
        MR::calcSensorDirectionNormalize(&sensorDir, pSender, pReceiver);
        addDeformVelocityOuter(sensorDir * ::sStarPieceDeformPower, false);
        return true;
    }

    if (MR::isMsgPlayerHipDrop(msg) && mRider == nullptr) {
        setNerve(GET_NERVE(BigBubble, BigBubbleNrvBreak));
        return true;
    }

    return false;
}

bool BigBubble::receiveMsgEnemyAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (!MR::isSensorRide(pReceiver) || !isEnemyAttackBreakable()) {
        return false;
    }

    if (mRider != nullptr) {
        MR::endBindAndPlayerDamageMsg(this, msg);
        mRider = nullptr;
    }

    setNerve(GET_NERVE(BigBubble, BigBubbleNrvBreak));
    return true;
}

bool BigBubble::receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) {
    if (pSender->isType(ATYPE_BIG_BUBBLE)) {
        if (isNerve(GET_NERVE(BigBubble, BigBubbleNrvWait)) || isNerve(GET_NERVE(BigBubble, BigBubbleNrvCapture))) {
            Vec3f dir = pReceiver->mPosition - pSender->mPosition;
            f32 dist = dir.length();
            f32 sumRadii = pReceiver->getRadius() + pSender->getRadius();
            f32 pushMoveRate = sumRadii * ::sNeedPushMoveMinRate - dist;
            MR::normalizeOrZero(&dir);
            if (MR::isNearZero(dir)) {
                MR::addRandomVector(&dir, dir, 1.0f);
                MR::normalizeOrZero(&dir);
            }

            // A bubble shrunk to nothing has no size to divide by.
            const f32 size = getSize();
            if (pushMoveRate > 0.0f && size > 0.0f) {
                Vec3f moveRate = dir * (pushMoveRate / size);
                mVelocity.add(moveRate * ::sPushedAccel);
            }

            f32 pushDeformRate = sumRadii * ::sNeedPushDeformMinRate - dist;
            if (pushDeformRate > 0.0f) {
                addDeformVelocityOuter(dir * (pushDeformRate * (::sPushDeformAccel / getBaseRadius())), false);
            }
        }

        return true;
    }

    return false;
}

bool BigBubble::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (msg == ACTMES_AUTORUSH_BEGIN) {
        return requestBind(pSender);
    }

    if (msg == ACTMES_RUSH_CANCEL) {
        return requestCancelBind();
    }

    if (msg == ACTMES_BIG_BUBBLE_MERGE) {
        return requestMerged(pReceiver, pSender);
    }

    if (msg == ACTMES_BIG_BUBBLE_ASSIMILATE) {
        return requestAssimilate(pReceiver, pSender);
    }

    if (msg == ACTMES_UPDATE_BASEMTX) {
        if (mRider != nullptr) {
            updateBindActorMatrix();
            return true;
        }

        return false;
    }

    return false;
}

bool BigBubble::requestBind(HitSensor* pSensor) {
    if (!isNerve(GET_NERVE(BigBubble, BigBubbleNrvWait))) {
        return false;
    }

    f32 scale = pSensor->mHost->mScale.x;
    if (scale * scale * scale < 1.0f) {
        return false;
    }

    mRider = pSensor->mHost;
    mRiderPos = mRider->mPosition;
    mRiderBasePos = mRiderPos;
    mInterpolateRate = 0.0f;

    Pos3f mtx;
    mtx.set(mRider->getBaseMtx());
    mtx.getQuat(mRiderQuat);

    setNerve(GET_NERVE(BigBubble, BigBubbleNrvCapture));
    return true;
}

bool BigBubble::requestMerged(HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(BigBubble, BigBubbleNrvWait))) {
        f32 dist = pReceiver->mPosition.distance(pSender->mPosition);
        f32 radius1 = pSender->mRadius;
        f32 radius2 = pReceiver->mRadius;
        mReduceVolumeTimer = 0;
        if (dist < (radius2 + radius1) * ::sMargeableRate) {
            setNerve(GET_NERVE(BigBubble, BigBubbleNrvMerged));
            mMergeBubble = static_cast< BigBubble* >(pReceiver->mHost);
            mMergeIndex = getNearAxisIndex(MR::getSensorPos(pReceiver));
            return true;
        }
    }

    return false;
}

bool BigBubble::requestAssimilate(HitSensor* pSender, HitSensor* pReceiver) {
    f32 scale = pReceiver->mHost->mScale.x;
    Vec3f dir;
    MR::calcSensorDirectionNormalize(&dir, pSender, pReceiver);
    if (MR::isNearZero(dir)) {
        dir.set(mGravity);
    }

    Vec3f pos = mPosition + dir * getSize();
    Pos3f mtx;
    MR::makeMtxUpNoSupportPos(&mtx, dir, pos);
    MR::emitEffectHit(this, mtx, "Merge");

    mReduceVolumeTimer = 0;
    mVolume += scale * scale * scale;
    if (mVolume > ::sMaxVolume) {
        mVolume = ::sMaxVolume;
    }

    setScale(getRadius(mVolume));
    addDeformVelocityOuter(dir * scale * cNum_requestAssimilate_f1, true);

    for (s32 idx = 0; idx < 6; idx++) {
        if (mMergeBubbles[idx] == pReceiver->mHost) {
            mMergeBubbles[idx] = nullptr;
            break;
        }
    }

    MR::startSound(this, "SE_OJ_BIG_BUBBLE_MERGE");
    return true;
}

bool BigBubble::requestCancelBind() {
    mRider = nullptr;
    mWarningColor.a = 0;
    MR::emitEffect(this, "Break");
    MR::startSound(this, "SE_OJ_BIG_BUBBLE_BREAK");
    mIsBroken = true;
    mRiderBasePos = mRiderPos;
    mInterpolateRate = 0.0f;
    kill();
    return true;
}

bool BigBubble::tryAppearEnd() {
    if (MR::isGreaterEqualStep(this, ::sAppearTime)) {
        setNerve(GET_NERVE(BigBubble, BigBubbleNrvWait));
        return true;
    }

    return false;
}

bool BigBubble::tryBreak() {
    bool breakBubble = false;

    if (MR::isPressedMovingWall(this)) {
        breakBubble = true;
    }

    if (MR::isBindedGroundWater(this)) {
        breakBubble = true;
    }

    if (MR::isBindedDamageFire(this)) {
        if (mRider != nullptr) {
            MR::endBindAndPlayerFireDamage(this);
            mRider = nullptr;
        }

        breakBubble = true;
    }

    if (ElectricRailFunction::isTouchRail(getSensor("body"), nullptr, nullptr)) {
        if (mRider != nullptr) {
            MR::endBindAndPlayerElectricDamage(this);
            mRider = nullptr;
        }

        breakBubble = true;
    }

    if (breakBubble || mIsExitLimitter) {
        setNerve(GET_NERVE(BigBubble, BigBubbleNrvBreak));
        return true;
    }

    return false;
}

bool BigBubble::tryAutoBreak() {
    if ((!mIsObstruct && MR::isGreaterStep(this, ::sAutoBreakTime)) || mIsExitLimitter) {
        if (mRider != nullptr) {
            MR::endBindAndPlayerWeakGravityLimitJump(this, mVelocity);
            mRider = nullptr;
        }

        setNerve(GET_NERVE(BigBubble, BigBubbleNrvBreak));
        return true;
    }

    return false;
}

bool BigBubble::tryBreakEnd() {
    if (MR::isGreaterStep(this, ::sBreakTime)) {
        kill();
        return true;
    }

    return false;
}

bool BigBubble::tryEscape() {
    if (MR::isPressedCrouch(PLAYER_1)) {
        setNerve(GET_NERVE(BigBubble, BigBubbleNrvEscape));
        return true;
    }

    return false;
}

bool BigBubble::tryEscapeEnd() {
    if (MR::isGreaterStep(this, ::sEscapeTime)) {
        if (mRider != nullptr) {
            MR::calcGravity(this);
            MR::endBindAndPlayerJump(this, mGravity * ::sEscapeVelocity, ::sEscapeTime);
            mRider = nullptr;
        }

        MR::emitEffect(this, "Break");
        MR::startSound(this, "SE_OJ_BIG_BUBBLE_BREAK");
        kill();
        return true;
    }

    return false;
}

bool BigBubble::tryMergedCancel() {
    if (MR::isDead(mMergeBubble)) {
        kill();
        return true;
    }

    return false;
}

bool BigBubble::tryMergedEnd() {
    if (MR::isGreaterStep(this, ::sMergedTime)) {
        if (mMergeBubble->receiveMessage(ACTMES_BIG_BUBBLE_ASSIMILATE, getSensor("body"), mMergeBubble->getSensor("body"))) {
            mMergeBubble = nullptr;
            kill();
            return true;
        }
    }

    return false;
}

bool BigBubble::tryGoal() {
    if (MR::checkBigBubbleGoal(this)) {
        if (mRider != nullptr) {
            MR::endBindAndPlayerWeakGravityLimitJump(this, mVelocity);
            mRider = nullptr;
        }

        setNerve(GET_NERVE(BigBubble, BigBubbleNrvGoal));
        return true;
    }

    return false;
}

void BigBubble::exeAppear() {
    if (MR::isFirstStep(this)) {
        mSpawnPosition = mPosition;
        MR::invalidateHitSensors(this);
        MR::getRandomVector(&mMoment, ::sAppearAddMoment);
        MR::startSound(this, "SE_OJ_BIG_BUBBLE_APPEAR");
    }

    f32 scale = MR::calcNerveValue(this, ::sAppearTime, cNum_exeAppear_f1, mAppearRadius);
    setScale(scale);

    mPosition.set(mSpawnPosition + mGravity * (-scale * getBaseRadius()));
    if (tryAppearEnd()) {
        MR::onCalcGravity(this);
        MR::validateHitSensors(this);
        MR::invalidateHitSensor(this, "ride");
    }
}

void BigBubble::exeWait() {
    if (MR::isFirstStep(this)) {
        mPushDirection.zero();
    }

    addCoriolisAccel();

    if (mIsObstruct) {
        MR::zeroVelocity(this);
    } else {
        updateNormalVelocity();
    }

    tryAutoBreak();
}

void BigBubble::exeCapture() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("SpaceWait", "BigBubbleCapture");
        mPushDirection.zero();
        mReduceVolumeTimer = 0;
        MR::startSound(this, "SE_OJ_BIG_BUBBLE_MARIO_IN");
        mRiderBasePos = mRiderPos;
        MR::validateHitSensor(this, "ride");
    }

    if (mIsAttached) {
        if (addAccelPointing(PLAYER_1)) {
            MR::makeMtxUpNoSupportPos(&mEffectMtx, mPosition - mPointerPos, mPointerPos);
            MR::emitEffect(this, "PushWind");
        } else {
            MR::deleteEffect(this, "PushWind");
        }

        addAccelMoment(PLAYER_1);
    } else {
        addCoriolisAccel();
    }

    MR::requestStarPointerModeBigBubble(this, mPosition);
    MR::requestBigBubbleGuidance();
    updateNormalVelocity();

    mInterpolateRate = MR::calcNerveEaseInOutRate(this, 30);
    mRiderQuat.slerp(mBubbleQuat, ::sSpinPower);

    if (mIsShrinkable) {
        updateCaptureReduceVolume();
        updateCaptureWarningColor();
    }

    if (!tryEscape() && !tryBreak()) {
        tryGoal();
    }
}

void BigBubble::exeMerged() {
    if (tryMergedCancel()) {
        MR::onBind(this);
        return;
    }

    if (MR::isFirstStep(this)) {
        MR::offBind(this);
        MR::zeroVelocity(this);
        mMoment.zero();
    }

    Vec3f mergePos, mergeDir;
    mMergeBubble->calcMergePosition(&mergePos, &mergeDir, this);

    // The smaller of the two sizes.
    f32 mergeSize = getSize();
    const f32 otherSize = mMergeBubble->getBaseRadius() * mMergeBubble->mScale.x;
    if (otherSize < mergeSize) {
        mergeSize = otherSize;
    }

    f32 deformAccel = mergeSize * MR::calcNerveRate(this, ::sTurchDeformTime) * ::sTurchDeformAccel;
    addDeformVelocityOuter(mergeDir * deformAccel, false);
    mMergeBubble->addDeformVelocityOuter(mergeDir * -deformAccel * ::sMergeDeformAccel, false);
    addDeformVelocityInternalPressure();
    addDeformVelocityRebound();
    updateDeformVelocity();

    Vec3f localDir;
    calcLocalDirection(&localDir, mMergeIndex);
    Quat4f rot;
    rot.setRotate(localDir, -mergeDir, ::sMergeRotateRate);
    mBubbleQuat.mult(rot);
    calcLocalDirection(&localDir, mMergeIndex);
    mPosition.set(mergePos - localDir * mDeformCoeff[mMergeIndex] * ::sMergeDirCorrection);
    MR::rotateQuatMoment(&mBubbleQuat, mMoment);

    mBaseMtx.setQT(mBubbleQuat, mPosition);
    updateMeshPoint();
    if (tryMergedCancel() || tryMergedEnd()) {
        MR::onBind(this);
    }
}

void BigBubble::exeBreak() {
    if (MR::isFirstStep(this)) {
        if (mRider != nullptr) {
            MR::endBindAndPlayerWeakGravityLimitJump(this, mVelocity);
            mRider = nullptr;
        }

        mWarningColor.a = 0;
        MR::emitEffect(this, "Break");
        MR::startSound(this, "SE_OJ_BIG_BUBBLE_BREAK");
        mIsBroken = true;
        mRiderBasePos = mRiderPos;
        mInterpolateRate = 0.0f;
    }

    mInterpolateRate = MR::calcNerveRate(this, ::sBreakTime);
    updateNormalVelocity();
    tryBreakEnd();
}

void BigBubble::exeEscape() {
    if (MR::isFirstStep(this)) {
        mWarningColor.a = 0;
        MR::startBckPlayer("SwimDive", "BigBubbleEscape");
        MR::startSoundPlayer("SE_PV_HIP_DROP");
        MR::startSoundPlayer("SE_PM_PRE_HIPDROP");
        mRiderBasePos = mRiderPos;
        mInterpolateRate = 0.0f;
    }

    mInterpolateRate = MR::calcNerveRate(this, ::sEscapeTime);
    MR::calcGravity(this);
    updateNormalVelocity();
    Quat4f rot;
    Vec3f up;
    mRiderQuat.getYDir(up);
    rot.setRotate(up, mGravity, ::sMaxSpinSpeed);
    mRiderQuat.mult(rot);
    tryEscapeEnd();
}

void BigBubble::exeGoal() {
    if (MR::isFirstStep(this)) {
        MR::startSystemSE("SE_SY_READ_RIDDLE_S");
    }

    updateNormalVelocity();
    tryBreakEnd();
}

void BigBubble::endCapture() {
    MR::invalidateHitSensor(this, "ride");
    mPointAccel = 0.0f;
}

void BigBubble::addDeformVelocityOuter(const Vec3f& rDir, bool reverse) {
    Vec3f side, up, front;
    mBaseMtx.getXDir(side);
    mBaseMtx.getYDir(up);
    mBaseMtx.getZDir(front);

    f32 projSide = rDir.dot(side);
    f32 projUp = rDir.dot(up);
    f32 projFront = rDir.dot(front);

    if (!reverse) {
        if (projSide < 0.0f) {
            mDeformSpeed[Side_Right] += projSide * ::sAccelDeformPower;
        } else {
            mDeformSpeed[Side_Left] -= projSide * ::sAccelDeformPower;
        }

        if (projUp < 0.0f) {
            mDeformSpeed[Side_Top] += projUp * ::sAccelDeformPower;
        } else {
            mDeformSpeed[Side_Bottom] -= projUp * ::sAccelDeformPower;
        }

        if (projFront < 0.0f) {
            mDeformSpeed[Side_Front] += projFront * ::sAccelDeformPower;
        } else {
            mDeformSpeed[Side_Back] -= projFront * ::sAccelDeformPower;
        }
    } else {
        if (projSide > 0.0f) {
            mDeformSpeed[Side_Right] += projSide * ::sAccelDeformPower;
        } else {
            mDeformSpeed[Side_Left] -= projSide * ::sAccelDeformPower;
        }

        if (projUp > 0.0f) {
            mDeformSpeed[Side_Top] += projUp * ::sAccelDeformPower;
        } else {
            mDeformSpeed[Side_Bottom] -= projUp * ::sAccelDeformPower;
        }

        if (projFront > 0.0f) {
            mDeformSpeed[Side_Front] += projFront * ::sAccelDeformPower;
        } else {
            mDeformSpeed[Side_Back] -= projFront * ::sAccelDeformPower;
        }
    }
}

void BigBubble::addDeformVelocityInternalPressure() {
    f32 size = getSize();
    // BUG: pressure should be from opposing faces.
    f32 pressure = (mDeformCoeff[Side_Top] + mDeformCoeff[Side_Left]) * (mDeformCoeff[Side_Back] + mDeformCoeff[Side_Front]) *
                   (mDeformCoeff[Side_Right] + mDeformCoeff[Side_Bottom]) * cNum_addDeformVelocityInternalPressure_f1;
    if (pressure < 0.0f) {
        pressure = 0.0f;
    }

    if (size > cNum_addDeformVelocityInternalPressure_f2) {
        for (s32 idx = 0; idx < 6; idx++) {
            mDeformSpeed[idx] += (1.0f - pressure / (size * size * size)) * ::sPressPower;
        }
    }
}

void BigBubble::addDeformVelocityRebound() {
    f32 size = getSize();
    for (s32 idx = 0; idx < 6; idx++) {
        mDeformSpeed[idx] += (size - mDeformCoeff[idx]) * ::sPushMeshPower;
    }
}

void BigBubble::addCoriolisAccel() {
    MR::rotateVecDegree(&mCoriolisAccel, mGravity, ::sCorioriRotSpeed);
    mCoriolisAccel.orthogonalize(mGravity);
    if (MR::isNearZero(mCoriolisAccel)) {
        MR::getRandomVector(&mCoriolisAccel, 1.0f);
    }

    MR::normalizeOrZero(&mCoriolisAccel);
    mVelocity.add(mCoriolisAccel * mScale.x * ::sCoriolisAccelPower);
}

void BigBubble::addAccelMoment(s32 padChannel) {
    Vec3f rotateMoment;
    if (MR::calcStarPointerStrokeRotateMoment(&rotateMoment, mPosition, getSize(), padChannel)) {
        mMoment.add(rotateMoment * ::sSpinStartScreenSpeed);
        f32 spinSpeed = mMoment.length();
        if (spinSpeed > ::sSpinMaxScreenSpeed) {
            mMoment *= ::sSpinMaxScreenSpeed / spinSpeed;
        }
    }
}

bool BigBubble::addAccelPointing(s32 padChannel) {
    mPointAccel = 0.0f;
    if (!MR::isHeldJump(padChannel)) {
        return false;
    }

    if (!MR::isStarPointerInScreen(padChannel)) {
        return false;
    }

    if (MR::isStarPointerPointing(this, padChannel, true, "弱")) {
        return false;
    }

    MR::calcStarPointerWorldPointingPos(&mPointerPos, mPosition, padChannel);
    f32 pointerDist = mPointerPos.distance(mPosition);
    if (pointerDist <= ::sPointAccelDistanceMin) {
        return false;
    }

    if (pointerDist >= ::sPointAccelDistanceMax) {
        return false;
    }

    mPointAccel = 1.0f - (pointerDist - ::sPointAccelDistanceMin) / (::sPointAccelDistanceMax - ::sPointAccelDistanceMin);
    mPointAccel = mPointAccel * (::sPointAccelMax - ::sPointAccelMin) + ::sPointAccelMin;
    MR::startSystemLevelSE("SE_SY_LV_BIG_BUBBLE_WIND", mPointAccel * cNum_addAccelPointing_f1);

    Vec3f accelDir;
    MR::normalizeOrZero(mPosition - mPointerPos, &accelDir);
    MR::addVelocity(this, accelDir * mPointAccel);
    addDeformVelocityOuter(accelDir * ::sPointAccelDeformPowerRate, false);
    return true;
}

void BigBubble::doMoveLimit() {
    if (mMoveLimitter != nullptr) {
        if (mMoveLimitter->limitPosition(&mPosition, getSize())) {
            mIsExitLimitter = true;
        }

        if (mMoveLimitter->limitVelocity(&mVelocity, mPosition, getSize())) {
            mIsExitLimitter = true;
        }
    }
}

void BigBubble::updatePose() {
    MR::rotateQuatMoment(&mBubbleQuat, mMoment);
    mBaseMtx.setQT(mBubbleQuat, mPosition);
    if (mPushInvalidTime > 0) {
        addDeformVelocityOuter(mPushDirection, false);
    }

    addDeformVelocityOuter(-mGravity * ::sBuoyancyAccel * mScale.x, false);
    Vec3f deformVel;
    MR::clampLength(&deformVel, -mVelocity, ::sMaxDeformSpeed);
    addDeformVelocityOuter(deformVel * (1.0f / ::sMaxDeformSpeed), false);
    addDeformVelocityInternalPressure();
    addDeformVelocityRebound();
    updateDeformVelocity();
}

void BigBubble::updateBindActorMatrix() {
    if (mInterpolateRate < 1.0f) {
        mRiderPos.set(mRiderBasePos * (1.0f - mInterpolateRate) + mPosition * mInterpolateRate);
    } else {
        mRiderPos.set(mPosition);
    }

    Pos3f mtx;
    mtx.setQT(mRiderQuat, mRiderPos);
    MR::setBaseTRMtx(mRider, mtx);
}

void BigBubble::updateNormalVelocity() {
    MR::addVelocityToGravity(this, -::sBuoyancyAccel * mScale.x);
    MR::attenuateVelocity(this, ::sNormalFric);
    Vec3f windDir;
    f32 windDistance;
    BigFanFunction::calcWindInfo(&windDir, mPosition, &windDistance);
    windDistance = MR::normalize(windDistance, ::sWindMinDistance, ::sWindMaxDistance);
    mVelocity.add(windDir * ::sWindAccel * windDistance);
    mMoment.mult(::sSpinFreq);
    MR::reboundVelocityFromEachCollision(this, cNum_updateNormalVelocity_reboundVelocityFromEachCollision_1, cNum_updateNormalVelocity_reboundVelocityFromEachCollision_2, cNum_updateNormalVelocity_reboundVelocityFromEachCollision_3, cNum_updateNormalVelocity_reboundVelocityFromEachCollision_4);
}

void BigBubble::resetDeformVelocity() {
    for (s32 idx = 0; idx < 6; idx++) {
        mDeformCoeff[idx] = 0.0f;
        mDeformSpeed[idx] = 0.0f;
    }
}

void BigBubble::updateDeformVelocity() {
    for (s32 idx = 0; idx < 6; idx++) {
        mDeformCoeff[idx] += mDeformSpeed[idx];
        mDeformSpeed[idx] *= ::sMeshScaleFreq;
        if (mDeformCoeff[idx] < 0.0f) {
            mDeformCoeff[idx] = 0.0f;
            mDeformSpeed[idx] = 0.0f;
        }
    }
}

void BigBubble::updateMeshPoint() {
    Vec3f side, up, front;
    mBaseMtx.getXDir(side);
    mBaseMtx.getYDir(up);
    mBaseMtx.getZDir(front);

    mSurface->getVertexPtr(Side_Top)->set(mPosition + up * mDeformCoeff[Side_Top]);
    mSurface->getVertexPtr(Side_Right)->set(mPosition + side * mDeformCoeff[Side_Right]);
    mSurface->getVertexPtr(Side_Back)->set(mPosition - front * mDeformCoeff[Side_Back]);
    mSurface->getVertexPtr(Side_Left)->set(mPosition - side * mDeformCoeff[Side_Left]);
    mSurface->getVertexPtr(Side_Front)->set(mPosition + front * mDeformCoeff[Side_Front]);
    mSurface->getVertexPtr(Side_Bottom)->set(mPosition - up * mDeformCoeff[Side_Bottom]);
    mSurface->calcControlPoint();
}

void BigBubble::updateCaptureReduceVolume() {
    if (mReduceVolumeTimer >= ::sReduceSizeInterval * 3) {
        mVolume -= ::sReduceVolume;

        if (::sCaptureEndVolume <= mVolume && mVolume < ::sCaptureStartVolume * cNum_updateCaptureReduceVolume_f1) {
            mVolume = ::sCaptureStartVolume * cNum_updateCaptureReduceVolume_f2;
        }

        if (mVolume < 0.0f) {
            mVolume = 0.0f;
        }

        setScale(getRadius(mVolume));
        mReduceVolumeTimer = 0;
        return;
    }

    mReduceVolumeTimer++;
}

s32 BigBubble::getCycle() const {
    if (mReduceVolumeTimer > ::sReduceSizeInterval * 2) {
        return ::sWarningCycle / 4;
    } else if (mReduceVolumeTimer > ::sReduceSizeInterval * 1) {
        return ::sWarningCycle / 2;
    } else {
        return ::sWarningCycle / 1;
    }
}

void BigBubble::updateCaptureWarningColor() {
    if (mVolume < ::sCaptureStartVolume) {
        s32 warningCycle = getCycle();

        f32 flashPhase = static_cast< f32 >(mReduceVolumeTimer % warningCycle) / static_cast< f32 >(warningCycle - 1);
        f32 color = (MR::cos(TWO_PI * flashPhase) + 1.0f) * cNum_updateCaptureWarningColor_f1;

        mWarningColor.a = (s32)(color * cNum_updateCaptureWarningColor_f2);
    } else {
        mWarningColor.a = 0;
    }
}

void BigBubble::calcMergePosition(Vec3f* pPos, Vec3f* pDir, const BigBubble* pBubble) const {
    s32 mergeIndex = -1;
    for (s32 idx = 0; idx < 6; idx++) {
        if (mMergeBubbles[idx] == pBubble) {
            mergeIndex = idx;
            break;
        }
    }

    if (mergeIndex < 0 || mergeIndex >= 6) {
        // Not a merge partner: leave the position and direction defined.
        pPos->set(mPosition);
        pDir->zero();
        return;
    }

    pPos->set(mSurface->getVertex(mergeIndex));
    calcLocalDirection(pDir, mergeIndex);
}

void BigBubble::calcLocalDirection(Vec3f* pDir, s32 mergeIndex) const {
    Vec3f dir;
    switch (mergeIndex) {
    case Side_Top:
        mBubbleQuat.getYDir(dir);
        break;
    case Side_Right:
        mBubbleQuat.getXDir(dir);
        break;
    case Side_Back:
        mBubbleQuat.getZDir(dir);
        dir.negate();
        break;
    case Side_Left:
        mBubbleQuat.getXDir(dir);
        dir.negate();
        break;
    case Side_Front:
        mBubbleQuat.getZDir(dir);
        break;
    case Side_Bottom:
        mBubbleQuat.getYDir(dir);
        dir.negate();
        break;
    }

    pDir->set(dir);
}

s32 BigBubble::getNearAxisIndex(const Vec3f& rPos) const {
    Pos3f mtx;
    mtx.set(getBaseMtx());
    mtx.invert(mtx);

    Vec3f axis;
    mtx.mult(rPos, axis);
    s32 index = MR::getMaxAbsElementIndex(axis);

    switch (index) {
    case 0:
        return axis.x >= 0.0f ? Side_Right : Side_Left;
    case 1:
        return axis.y >= 0.0f ? Side_Top : Side_Bottom;
    case 2:
        return axis.z >= 0.0f ? Side_Front : Side_Back;
    default:
        return -1;
    }
}

s32 BigBubble::getMergeIndex(const Vec3f& rPos) const {
    s32 index = getNearAxisIndex(rPos);
    if (mMergeBubbles[index] == nullptr) {
        return index;
    }

    return -1;
}

bool BigBubble::isPushable() const {
    return isNerve(GET_NERVE(BigBubble, BigBubbleNrvWait)) || isNerve(GET_NERVE(BigBubble, BigBubbleNrvCapture));
}

bool BigBubble::isBindMario() const {
    return mRider != nullptr;
}

bool BigBubble::isDraw() const {
    return !mIsHidden && !mIsBroken && !MR::isDead(this);
}

bool BigBubble::isMerged() const {
    return isNerve(GET_NERVE(BigBubble, BigBubbleNrvMerged));
}

bool BigBubble::isEnemyAttackBreakable() const {
    return isPushable();
}
