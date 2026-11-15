#include "Game/Ride/SurfRay.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include <nebula_game_api.h>
#include "Game/LiveActor/ActorJointCtrl.hpp"
#include "Game/LiveActor/MaterialCtrl.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvSurfRay {
    NEW_NERVE(SurfRayNrvWaitPlayer, SurfRay, WaitPlayer);
    NEW_NERVE(SurfRayNrvTurnToWait, SurfRay, TurnToWait);
    NEW_NERVE(SurfRayNrvWait, SurfRay, Wait);
    NEW_NERVE(SurfRayNrvRideAccel, SurfRay, RideAccel);
    NEW_NERVE(SurfRayNrvRideAccelTwist, SurfRay, RideAccelTwist);
    NEW_NERVE(SurfRayNrvRideFree, SurfRay, RideFree);
    NEW_NERVE(SurfRayNrvRideFreeTwist, SurfRay, RideFreeTwist);
    NEW_NERVE(SurfRayNrvRideJump, SurfRay, RideJump);
    NEW_NERVE(SurfRayNrvRideJumpHigh, SurfRay, RideJumpHigh);
    NEW_NERVE(SurfRayNrvRideFall, SurfRay, RideFall);
    NEW_NERVE(SurfRayNrvRideLand, SurfRay, RideLand);
    NEW_NERVE(SurfRayNrvTutorial, SurfRay, Tutorial);
    NEW_NERVE(SurfRayNrvWipeOut, SurfRay, WipeOut);
    NEW_NERVE(SurfRayNrvWipeIn, SurfRay, WipeIn);
    NEW_NERVE(SurfRayNrvReady, SurfRay, Ready);
};  // namespace NrvSurfRay

namespace {
    const f32 sDistancePlayerNear = 1300.0f;
    const f32 sSpeedToRumble = 5.0f;
    const f32 sBlendRatioAxisUpAir = 0.02f;
    const f32 sBlendRatioAxisUpGround = 0.1f;
    const f32 sBlendRatioAxisFrontWall = 0.1f;
    const f32 sTwistStartDiffMin = 0.2f;
    const f32 sBtkSpeedMin = 1.0f;
    const f32 sBtkSpeedMax = 4.0f;
    const f32 sBtkSpeedRate = 0.4f;
    const f32 sShadowAlphaMax = 64.0f;
    const f32 sShadowAlphaSpeed = 10.0f;
    const f32 sSpeedToEmitEffect = 6.0f;
    const f32 sSpeedToEmitEffectFast = 20.0f;
    const f32 sMoveSe0StartSpeed = 2.5f;
    const f32 sMoveSe1StartSpeed = 7.5f;
    const f32 sMoveSe2StartSpeed = 14.0f;
    const f32 sMoveSe3StartSpeed = 25.0f;
    const f32 sMoveSe3MaxSpeed = 35.0f;
    const f32 sWindSeStartSpeed = 32.0f;
    const f32 sWindSeMaxSpeed = 50.0f;

    bool calcWaterShadowPos(Vec3f* pShadowPos, const Vec3f& rStartPos, const Vec3f& rShadowDir) {
        pShadowPos->set(rStartPos);
        Vec3f sample(rStartPos);

        for (s32 i = 0; i < 10; i++) {
            WaterInfo waterInfo = WaterInfo();

            if (MR::getWaterAreaObj(&waterInfo, sample)) {
                MR::getWaterAreaInfo(&waterInfo, sample, rShadowDir, false);
                pShadowPos->set(waterInfo.mSurfacePos);

                return true;
            }

            sample.add(rShadowDir * 100.0f);
        }

        return false;
    }
};  // namespace

SurfRay::SurfRay(const char* pName, s32 channel)
    : LiveActor(pName), mSurfSpeed(0.0f), mOrthoVelocity(0.0f, 0.0f, 0.0f), mSteerRate(0.0f), mSteerAccel(0.0f),
      mFront(0.0f, 0.0f, 1.0f), mUp(0.0f, 1.0f, 0.0f), mSide(1.0f, 0.0f, 0.0f), mBaseUp(0.0f, 1.0f, 0.0f), mBaseSide(1.0f, 0.0f, 0.0f),
      mGroundNormal(0.0f, 1.0f, 0.0f), mWarpPos(0.0f, 0.0f, 0.0f), mRider(nullptr), mPadChannel(channel), mRayTilt(0.0f), mInWater(false),
      mAirTime(0), mWaterNormal(0.0f, 1.0f, 0.0f), mWaterShadowPos(0.0f, 0.0f, 0.0f), mShadowAlpha(0.0f), mTwistBufferSize(8), mTwistBuffer(nullptr),
      mInTutorialArea(false), mInTutorial(false), mLectureIdx(0), mActorJointCtrl(nullptr), mProjmapFxMtxSetter(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< SurfRay >);
    mEffectHostMtx.identity();
}

void SurfRay::init(const TableIter& rIter) {
    if (MR::hasRetryGalaxySequence()) {
        MR::resetPosition(this, "スタート位置（サーフィン）");
    } else {
        MR::initDefaultPos(this, rIter);
    }

    resetAllInfo();
    initModelManagerWithAnm("SurfRay", nullptr, true);

    mProjmapFxMtxSetter = MR::initMaterialProjmapEffectMtxSetter(this);
    MR::prepareMaterials(this);
    mProjmapFxMtxSetter->updateMtxUseBaseMtx();

    MR::startBtk(this, "SurfRay");
    MR::startBtp(this, "Blink");
    MR::connectToSceneRide(this);
    MR::initLightCtrl(this);

    initHitSensor(2);
    MR::addHitSensorRide(this, "body", 8, 260.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorBinder(this, "bind", 8, 300.0f, Vec3f(0.0f, 0.0f, 0.0f));

    initBinder(200.0f, 50.0f, 8);
    initEffectKeeper(0, nullptr);
    MR::setEffectHostMtx(this, "Ripple", mEffectHostMtx);
    initSound(15, false);

    MR::initShadowFromCSV(this, "Shadow");
    Color8 shadowColor(0, 40, 128, 64);
    MR::setShadowSurfaceOvalColor(this, 0, shadowColor);
    MR::setShadowProjectionPtr(this, 0, &mWaterShadowPos, &mWaterNormal);
    MR::setShadowSurfaceOvalAlpha(this, 0, (u8)(s32)mShadowAlpha);

    initNerve(GET_NERVE(SurfRay, SurfRayNrvWaitPlayer));

    MR::useStageSwitchWriteA(this, rIter);
    MR::tryFindNamePos("ワープ位置（サーフィン）", &mWarpPos, nullptr);

    mActorJointCtrl = new ActorJointCtrl(this);
    mTwistBuffer = new f32[mTwistBufferSize];

    for (s32 i = 0; i < mTwistBufferSize; i++) {
        mTwistBuffer[i] = 0.0f;
    }

    MR::joinToGroupArray(this, rIter, 0, 32);

    if (!MR::hasRetryGalaxySequence()) {
        mInTutorialArea = true;
    }

    MR::invalidateClipping(this);
    makeActorAppeared();
}

void SurfRay::exeWaitPlayer() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "FirstWait");
    }

    ::calcWaterShadowPos(&mWaterShadowPos, mPosition, mGravity);

    if (MR::isNearPlayer(this, ::sDistancePlayerNear)) {
        setNerve(GET_NERVE(SurfRay, SurfRayNrvTurnToWait));
    }
}

void SurfRay::exeTurnToWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "FirstWaitTurn");
        MR::startSound(this, "SE_SM_SURF_RAY_TURNTO_MR");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(SurfRay, SurfRayNrvWait));
    }
}

void SurfRay::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
    }
}

void SurfRay::exeRideAccel() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayerJ("サーフィン（加速）");
        MR::startBck(this, "RunDash");
    }

    if (!updateRideAccel() && isTwistStart()) {
        setNerve(GET_NERVE(SurfRay, SurfRayNrvRideAccelTwist));
    }
}

void SurfRay::exeRideAccelTwist() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayerJ("サーフィン傾き開始（加速）");
    }

    if (!updateRideAccel() && MR::isBckStoppedPlayer()) {
        setNerve(GET_NERVE(SurfRay, SurfRayNrvRideAccel));
    }
}

void SurfRay::exeRideFree() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Run");
        MR::startBckPlayerJ("サーフィン");
    }

    if (!updateRideFree() && isTwistStart()) {
        setNerve(GET_NERVE(SurfRay, SurfRayNrvRideFreeTwist));
    }
}

void SurfRay::exeRideFreeTwist() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayerJ("サーフィン傾き開始");
    }

    if (!updateRideFree() && MR::isBckStoppedPlayer()) {
        setNerve(GET_NERVE(SurfRay, SurfRayNrvRideFree));
    }
}

void SurfRay::exeRideJump() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayerJ("サーフィンジャンプ");
        MR::startBck(this, "Jump");
        MR::startSound(mRider, "SE_PV_JUMP_S");
    }

    if (!updateRide() && mVelocity.dot(mGravity) > 0.0f) {
        setNerve(GET_NERVE(SurfRay, SurfRayNrvRideFall));
    }
}

void SurfRay::exeRideJumpHigh() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayerJ("サーフィンハイジャンプ");
        MR::startBck(this, "Jump");
        MR::startSound(mRider, "SE_PV_JUMP_JOY");
    }

    if (!updateRide() && mVelocity.dot(mGravity) > 0.0f) {
        setNerve(GET_NERVE(SurfRay, SurfRayNrvRideFall));
    }
}

void SurfRay::exeRideFall() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayerJ("サーフィン落下");
        MR::startBck(this, "Fall");
    }

    if (!updateRide() && mVelocity.dot(mGravity) > 0.0f && mInWater) {
        setNerve(GET_NERVE(SurfRay, SurfRayNrvRideLand));
        MR::startSound(this, "SE_SM_SURF_RAY_LANDW");
    }
}

void SurfRay::exeRideLand() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayerJ("サーフィン着地");
        MR::startBck(this, "Land");
    }

    if (!updateRide() && !tryJumpOrFall() && MR::isBckStopped(this)) {
        setNerve(GET_NERVE(SurfRay, SurfRayNrvRideFree));
    }
}

void SurfRay::exeTutorial() {
    // The ray rides in place during the lesson, holding its heading.
    Vec3f heldFront(mFront);
    updateRide();
    mOrthoVelocity.mult(0.5f);
    mFront.set(heldFront);

    mSurfSpeed = 0.0f;
    mRotation.z *= 0.85f;

    switch (mLectureIdx) {
    case 0:
        if (MR::isFirstStep(this)) {
            MR::startBckPlayerJ("サーフィン着地");
            MR::startBck(this, "Land");
        }

        if (MR::isBckOneTimeAndStoppedPlayer()) {
            MR::startBckPlayer("SurfLectureUnbalance");
        }

        if (MR::isBckOneTimeAndStopped(this)) {
            MR::startBck(this, "LectureUnbalance");
        }
        break;
    case 2:
        if (MR::isFirstStep(this)) {
            MR::startBckPlayer("SurfLectureUnbalance");
            MR::startBck(this, "LectureUnbalance");
        }
        break;
    case 3:
        if (MR::isFirstStep(this)) {
            MR::startBckPlayer("SurfRide");
            MR::startBck(this, "Wait");
        }
        break;
    case 4:
    case 8:
    case 12:
        if (MR::isFirstStep(this)) {
            MR::startBckPlayer("SurfLectureAnswer");
            MR::startBck(this, "LectureAnswer");
            MR::startSound(this, "SE_SM_SURF_RAY_JUMPOUT1");
        }

        if (MR::isBckOneTimeAndStoppedPlayer()) {
            MR::startBckPlayer("SurfRideLoop");
        }

        if (MR::isBckOneTimeAndStopped(this)) {
            MR::startBck(this, "Wait");
        }
        break;
    case 5:
    case 6:
    case 9:
    case 10:
        if (MR::isFirstStep(this)) {
            MR::startBckPlayer("SurfRideLoop");
            MR::startBck(this, "Wait");
        }
        break;
    case 7:
    case 11: {
        if (MR::isFirstStep(this)) {
            MR::startBckPlayerJ("サーフィン傾き開始");
        }

        f32 rot = MR::abs((mRotation.z / 70.0f) * 0.6f);

        if (mRotation.z < 0.0f) {
            MR::setBckBlendWeight(1.0f - rot, rot, 0.0f);
        } else {
            MR::setBckBlendWeight(1.0f - rot, 0.0f, rot);
        }
        break;
    }
    default:
        break;
    }
}

void SurfRay::exeWipeOut() {
    if (MR::isFirstStep(this)) {
        MR::closeWipeCircle();
    }

    if (!updateRide() && !MR::isWipeActive()) {
        MR::resetPosition(this, "スタート位置（サーフィン）");
        resetAllInfo();
        MR::startBckPlayer("SurfRide", 0);
        MR::resetCameraMan();
        setNerve(GET_NERVE(SurfRay, SurfRayNrvWipeIn));
    }
}

void SurfRay::exeWipeIn() {
    if (MR::isFirstStep(this)) {
        MR::openWipeCircle();
    }

    if (!MR::isWipeActive()) {
        setNerve(GET_NERVE(SurfRay, SurfRayNrvRideFree));
    }
}

void SurfRay::exeReady() {
    if (MR::isFirstStep(this)) {
        MR::endStartPosCamera();
        MR::startBck(this, "WaitRaceStart");
        MR::startBckPlayer("SurfRide", 0);
        MR::tryEmitEffect(this, "Ripple");
        MR::tryDeleteEffect(this, "RunDashSplash");
        MR::tryDeleteEffect(this, "SwimSplash");
    }

    MR::calcGravity(this);
    updateToWater();
    updateInfo();
    updateToMap();
    updateSound();
    updateRiderMtx();
    mOrthoVelocity.mult(0.5f);
    mSurfSpeed = 0.0f;
    mRotation.z = 0.0f;
}

void SurfRay::control() {
    mVelocity.set(mFront);
    mVelocity.scale(mSurfSpeed);
    mVelocity.add(mOrthoVelocity);
    f32 rate = MR::min(mSurfSpeed * ::sBtkSpeedRate + ::sBtkSpeedMin, ::sBtkSpeedMax);
    MR::setBtkRate(this, rate);
    mActorJointCtrl->update();
}

void SurfRay::calcAndSetBaseMtx() {
    // A base up along the front keeps the last base axes.
    Vec3f baseSide;
    baseSide.cross(mBaseUp, mFront);
    if (!MR::isNearZero(baseSide)) {
        MR::normalize(&baseSide);
        mBaseSide.set(baseSide);
        mBaseUp.cross(mFront, mBaseSide);
        MR::normalize(&mBaseUp);
    }

    mUp.set(mBaseUp);
    MR::rotateVecDegree(&mUp, mFront, mRotation.z);
    mSide.cross(mUp, mFront);
    MR::normalize(&mSide);
    mUp.cross(mFront, mSide);
    MR::normalize(&mUp);

    Pos3f mtx;
    mtx.setTR(mSide, mUp, mFront, mPosition);
    MR::setBaseTRMtx(this, mtx);
    mActorJointCtrl->setCallBackFunction();
    mProjmapFxMtxSetter->updateMtxUseBaseMtx();
}

void SurfRay::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    MR::tryGetItem(pSender, pReceiver);
}

bool SurfRay::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (MR::isMsgAutoRushBegin(msg)) {
        if (MR::getPlayerVelocity()->y > 0.0f) {
            return false;
        }

        mRider = MR::getSensorHost(pSender);
        NebulaHost::gameContext(NEBULA_CONTEXT_TILT_SECTION, 2);
        MR::startStarPointerMode1PInvalid2PValid(this);
        MR::invalidateClipping(this);
        mRayTilt = 0.0f;

        if (MR::isValidSwitchA(this)) {
            MR::onSwitchA(this);
        }

        MR::startBckNoInterpole(this, "Wait");
        MR::startBckPlayer("SurfRideLoop", 0);
        MR::calcAnimDirect(this);
        mActorJointCtrl->resetDynamicCtrl();

        if (mInTutorial) {
            setNerve(GET_NERVE(SurfRay, SurfRayNrvTutorial));
        } else {
            setNerve(GET_NERVE(SurfRay, SurfRayNrvReady));
        }

        return true;
    }

    if (MR::isMsgUpdateBaseMtx(msg)) {
        updateRiderMtx();
        return true;
    }

    if (MR::isMsgRushCancel(msg)) {
        mRider = nullptr;
        setNerve(GET_NERVE(SurfRay, SurfRayNrvRideFree));
        return true;
    }

    if (MR::isMsgTutorialStart(msg)) {
        MR::setPlayerPos(mPosition);
        mInTutorial = true;
        return true;
    }

    if (MR::isMsgTutorialNext(msg)) {
        mLectureIdx++;
        setNerve(GET_NERVE(SurfRay, SurfRayNrvTutorial));
        return true;
    }

    if (MR::isMsgTutorialPrev(msg)) {
        mLectureIdx--;
        setNerve(GET_NERVE(SurfRay, SurfRayNrvTutorial));
        return true;
    }

    if (MR::isMsgTutorialPass(msg)) {
        mInTutorial = false;
        setNerve(GET_NERVE(SurfRay, SurfRayNrvRideFree));
        return true;
    }

    if (MR::isMsgTutorialOmit(msg)) {
        mInTutorialArea = false;
        MR::resetPosition(this, "スタート位置（サーフィン）");
        resetAllInfo();
        MR::setPlayerPos(mPosition);
        MR::startBckPlayer("SurfRide", 0);
        MR::resetCameraMan();
        return true;
    }

    if (MR::isMsgRaceReady(msg)) {
        setNerve(GET_NERVE(SurfRay, SurfRayNrvReady));
        return true;
    }

    if (MR::isMsgRaceStart(msg)) {
        setNerve(GET_NERVE(SurfRay, SurfRayNrvRideFree));
        return true;
    }

    if (MR::isMsgRaceReset(msg)) {
        MR::endBindAndPlayerWait(this);
        resetAllInfo();
        setNerve(GET_NERVE(SurfRay, SurfRayNrvWait));
        MR::endStarPointerMode(this);
        kill();
        return true;
    }

    return false;
}

void SurfRay::resetAllInfo() {
    Rot3f rotMtx;
    MR::makeMtxRotate(rotMtx, mRotation);

    rotMtx.getXDir(mSide);
    rotMtx.getYDir(mUp);
    rotMtx.getZDir(mFront);

    mBaseUp.set(mUp);
    mBaseSide.set(mSide);

    mRotation.zero();
    mVelocity.zero();
    mOrthoVelocity.zero();

    mInWater = false;
    mSurfSpeed = 0.0f;
    mSteerRate = 0.0f;
    mSteerAccel = 0.0f;
    mAirTime = 0;

    MR::tryDeleteEffect(this, "SwimSplash");
    MR::tryDeleteEffect(this, "RunDashSplash");
    MR::tryDeleteEffect(this, "Ripple");
}

bool SurfRay::updateRideAccel() {
    if (updateRide()) {
        return true;
    }

    if (tryJumpOrFall()) {
        return true;
    }

    if (!MR::isHeldJump(mPadChannel)) {
        setNerve(GET_NERVE(SurfRay, SurfRayNrvRideFree));
        return true;
    }

    return false;
}

bool SurfRay::updateRideFree() {
    if (updateRide()) {
        return true;
    }

    if (tryJumpOrFall()) {
        return true;
    }

    if (MR::isHeldJump(mPadChannel)) {
        setNerve(GET_NERVE(SurfRay, SurfRayNrvRideAccel));
        return true;
    }

    return false;
}

bool SurfRay::updateRide() {
    if (mInTutorialArea && mPosition.z > mWarpPos.z) {
        mInTutorialArea = false;
        setNerve(GET_NERVE(SurfRay, SurfRayNrvWipeOut));
        return true;
    }

    MR::calcGravity(this);
    updateToWater();
    updateInfo();
    updateToMap();
    updateAccel();
    updateSound();
    updateRotate();

    return false;
}

void SurfRay::updateInfo() {
    if (MR::isBindedGround(this)) {
        f32 sfxLvl = MR::getLinerValueFromMinMax(mSurfSpeed, 0.0f, 40.0f, 100.0f, 1000.0f);

        MR::startLevelSound(this, "SE_SM_LV_SURF_RAY_LAND", sfxLvl);

        if (MR::reboundVelocityFromCollision(this, 0.3f, 10.0f, 1.0f) && mVelocity.length() > ::sSpeedToRumble) {
            MR::tryRumblePadMiddle(this, mPadChannel);
        }
    }

    if (MR::isBindedWall(this)) {
        if (mVelocity.dot(*MR::getWallNormal(this)) < -10.0f) {
            MR::startSound(this, "SE_SM_SURF_RAY_HIT_WALL");
        }

        if (MR::reboundVelocityFromCollision(this, 0.8f, 10.0f, 1.0f) && mVelocity.length() > ::sSpeedToRumble) {
            MR::tryRumblePadMiddle(this, mPadChannel);
        }
    }

    mSurfSpeed = mFront.dot(mVelocity);
    Vec3f proj(mFront);
    proj.scale(mSurfSpeed);
    mOrthoVelocity.set(mVelocity);
    mOrthoVelocity.sub(proj);
}

void SurfRay::updateToMap() {
    Vec3f fall(mGravity);

    if (mInWater) {
        fall.scale(0.1f);
    } else {
        fall.scale(0.8f);
    }

    mOrthoVelocity.add(fall);

    if (MR::isBinded(this)) {
        if (MR::isBindedGround(this)) {
            mGroundNormal.set(*MR::getBindedNormal(this));
            MR::vecBlendSphere(mBaseUp, mGroundNormal, &mBaseUp, ::sBlendRatioAxisUpGround);
        }

        if (MR::isBindedWall(this)) {
            // Turns along the wall.
            Vec3f wallDir(mVelocity);
            MR::normalize(&wallDir);
            MR::turnVecToPlane(&wallDir, wallDir, *MR::getWallNormal(this));
            MR::turnVecToPlane(&wallDir, wallDir, mGravity);
            Vec3f blendedFront;
            MR::vecBlend(mFront, wallDir, &blendedFront, ::sBlendRatioAxisFrontWall);
            mBaseSide.cross(mBaseUp, blendedFront);
        }
    } else if (mInWater) {
        MR::vecBlendSphere(mBaseUp, mWaterNormal, &mBaseUp, ::sBlendRatioAxisUpGround);
    } else {
        MR::vecBlendSphere(mBaseUp, mGravity * -1.0f, &mBaseUp, ::sBlendRatioAxisUpAir);
    }

    Vec3f front;
    front.cross(mBaseSide, mBaseUp);
    if (!MR::isNearZero(front)) {
        MR::normalize(&front);
        mFront.set(front);
    }
}

void SurfRay::updateToWater() {
    if (tryInWater()) {
        if (!mInWater) {
            f32 lvl = MR::getLinerValueFromMinMax(mOrthoVelocity.length(), 0.0f, 20.0f, 100.0f, 1000.0f);

            if (mAirTime > NebulaFrameClock::stepsOfMilliseconds(500)) {
                MR::tryRumblePadMiddle(this, mPadChannel);
            }

            if (mAirTime > 80 && mOrthoVelocity.length() > 20.0f) {
                MR::startSound(this, "SE_SM_SURF_RAY_JUMPIN2");
            } else if (mAirTime > 5 && mOrthoVelocity.length() > 10.0f) {
                MR::startSound(this, "SE_SM_SURF_RAY_JUMPIN1", lvl);
            } else {
                MR::startSound(this, "SE_SM_SURF_RAY_JUMPIN0", lvl);
            }
        }

        mInWater = true;
        mAirTime = 0;

        if (mVelocity.length() > ::sSpeedToRumble) {
            MR::tryRumblePadWeak(this, mPadChannel);
        }

        MR::getWaterAreaInfo(&mWaterInfo, mPosition, mGravity, false);
        mWaterNormal.set(mWaterInfo.mSurfaceNormal);
        f32 innerProd = mGravity.dot(mVelocity);

        // Sinking deeper than it falls in one step bounces it back up; otherwise it floats toward the wave top.
        if (innerProd > 0.0f && mWaterInfo.mDepth < innerProd) {
            mVelocity.sub(mGravity * innerProd * 1.5f);
        } else {
            f32 waveDepth = mWaterInfo.mCamWaterDepth + mWaterInfo.mWaveHeight;
            mVelocity.sub(mGravity * waveDepth * 0.01f);
        }
    } else {
        if (mInWater) {
            f32 lvl = MR::getLinerValueFromMinMax(mSurfSpeed, 0.0f, 30.0f, 200.0f, 1000.0f);
            MR::startSound(this, "SE_SM_SURF_RAY_JUMPOUT1", lvl);
            mInWater = false;
        }

        mAirTime++;
    }

    WaterInfo waterInfo;
    Vec3f v(mPosition + mGravity * 20.0f);

    if (MR::getWaterAreaObj(&waterInfo, v)) {
        mShadowAlpha -= ::sShadowAlphaSpeed;
    } else if (::calcWaterShadowPos(&mWaterShadowPos, mPosition, mGravity)) {
        mShadowAlpha += ::sShadowAlphaSpeed;
    } else {
        mShadowAlpha -= ::sShadowAlphaSpeed;
    }

    mShadowAlpha = MR::clamp(mShadowAlpha, 0.0f, ::sShadowAlphaMax);
    MR::setShadowSurfaceOvalAlpha(this, 0, (u8)(s32)mShadowAlpha);
}

void SurfRay::updateAccel() {
    if (MR::isHeldJump(mPadChannel)) {
        mSurfSpeed += 0.5f;
    } else {
        mSurfSpeed *= 0.99f;
    }

    mSurfSpeed = MR::clamp(mSurfSpeed, 0.0f, 40.0f);

    if (!mWaterInfo.isInWater()) {
        MR::tryDeleteEffect(this, "SwimSplash");
        MR::tryDeleteEffect(this, "RunDashSplash");
        MR::tryDeleteEffect(this, "Ripple");
    } else if (mSurfSpeed > ::sSpeedToEmitEffectFast) {
        MR::tryEmitEffect(this, "RunDashSplash");
        MR::tryDeleteEffect(this, "SwimSplash");
        MR::tryDeleteEffect(this, "Ripple");
    } else if (mSurfSpeed > ::sSpeedToEmitEffect) {
        MR::tryEmitEffect(this, "SwimSplash");
        MR::tryDeleteEffect(this, "RunDashSplash");
        MR::tryDeleteEffect(this, "Ripple");
    } else {
        MR::tryEmitEffect(this, "Ripple");
        MR::tryDeleteEffect(this, "RunDashSplash");
        MR::tryDeleteEffect(this, "SwimSplash");
        MR::makeMtxUpFrontPos(&mEffectHostMtx, -mGravity, mFront, mWaterInfo.mSurfacePos);
    }

    if (MR::isBinded(this)) {
        mOrthoVelocity.mult(0.9f);
    } else {
        mOrthoVelocity.mult(0.995f);
    }

    if (MR::isNearZero(mOrthoVelocity, 0.1f)) {
        mOrthoVelocity.zero();
    }

    if (mWaterInfo.isInWater() && !isNerve(GET_NERVE(SurfRay, SurfRayNrvRideJump))) {
        mOrthoVelocity.mult(0.95f);
    }
}

void SurfRay::updateRotate() {
    f32 steerAccel;
    f32 accelX = 0.0f;

    if (isRotateStart()) {
        Vec3f accel(0.0f, 0.0f, 0.0f);
        MR::getMainTilt(&accel, mPadChannel);

        if (accel.z > -0.7f || MR::abs(accel.x) > 0.15f) {
            f32 twistAngle = accel.x;
            f32 accelZ = MR::clamp(-accel.z, 0.0f, 1.0f);

            if (twistAngle > 0.0f) {
                twistAngle += accelZ;
            } else if (twistAngle < 0.0f) {
                twistAngle -= accelZ;
            }

            steerAccel = MR::clamp(twistAngle, -1.5f, 1.5f);
            accelX = accel.x;

            // Not for 0 or NaN.
            if (accelX < 0.0f || accelX > 0.0f) {
                mRayTilt = MR::getInterpolateValue(0.3f, mRayTilt, MR::clamp(accelX, -1.0f, 1.0f));
            }
        } else {
            mRayTilt = MR::getInterpolateValue(0.3f, mRayTilt, 0.0f);
            steerAccel = mSteerAccel * 0.95f;
        }

        mSteerAccel = steerAccel;
        mSteerRate += steerAccel;
        mSteerRate = MR::clamp(mSteerRate, -30.0f, 30.0f);
    } else {
        mRayTilt = MR::getInterpolateValue(0.3f, mRayTilt, 0.0f);
        mSteerAccel = 0.0f;
    }

    mSteerRate *= 0.95f;
    if (MR::abs(mSteerRate) < 0.1f) {
        mSteerRate = 0.0f;
    }

    mRotation.z *= 0.7f;
    mRotation.z += mSteerRate;
    mRotation.z = MR::clamp(mRotation.z, -70.0f, 70.0f);

    f32 rayTilt = MR::clamp(mRayTilt * 1.2f, -1.0f, 1.0f);

    if (rayTilt > 0.0f) {
        MR::setBckBlendWeight(1.0f - rayTilt, 0.0f, rayTilt);
    } else {
        MR::setBckBlendWeight(rayTilt + 1.0f, -rayTilt, 0.0f);
    }

    Rot3f mtx;
    mtx.identity();
    mtx.setRotate(mBaseUp, -mRotation.z * 0.00028f);
    mtx.mult(mFront, mFront);

    for (s32 i = mTwistBufferSize - 1; i >= 1; i--) {
        mTwistBuffer[i] = mTwistBuffer[i - 1];
    }

    mTwistBuffer[0] = accelX;
}

void SurfRay::updateSound() {
    f32 lvl;

    if (mSurfSpeed > ::sMoveSe0StartSpeed && mSurfSpeed < ::sMoveSe3StartSpeed) {
        if (mSurfSpeed < ::sMoveSe2StartSpeed) {
            lvl = MR::getLinerValueFromMinMax(mSurfSpeed, ::sMoveSe0StartSpeed, ::sMoveSe1StartSpeed, 0.1f, 1.0f);
        } else {
            lvl = MR::getLinerValueFromMinMax(mSurfSpeed, ::sMoveSe2StartSpeed, ::sMoveSe3StartSpeed, 1.0f, 0.0f);
        }

        MR::startLevelSound(this, "SE_SM_LV_SURF_RAY_MOVE0", lvl * 1000.0f);
    }

    if (mInWater) {
        if (mSurfSpeed > ::sMoveSe1StartSpeed) {
            lvl = MR::getLinerValueFromMinMax(mSurfSpeed, ::sMoveSe1StartSpeed, ::sMoveSe2StartSpeed, 0.1f, 1.0f);
            MR::startLevelSound(this, "SE_SM_LV_SURF_RAY_MOVE1", lvl * 1000.0f);
        }

        if (mSurfSpeed > ::sMoveSe2StartSpeed) {
            lvl = MR::getLinerValueFromMinMax(mSurfSpeed, ::sMoveSe2StartSpeed, ::sMoveSe3StartSpeed, 0.1f, 1.0f);
            MR::startLevelSound(this, "SE_SM_LV_SURF_RAY_MOVE2", lvl * 1000.0f);
        }

        if (mSurfSpeed > ::sMoveSe3StartSpeed) {
            lvl = MR::getLinerValueFromMinMax(mSurfSpeed, ::sMoveSe3StartSpeed, ::sMoveSe3MaxSpeed, 0.1f, 1.0f);
            MR::startLevelSound(this, "SE_SM_LV_SURF_RAY_MOVE3", lvl * 1000.0f);
        }
    }

    if (mVelocity.length() > ::sWindSeStartSpeed) {
        lvl = MR::getLinerValueFromMinMax(mVelocity.length(), ::sWindSeStartSpeed, ::sWindSeMaxSpeed, 0.1f, 1.0f);

        MR::startLevelSound(this, "SE_SM_SURF_RAY_MOVE_WIND", lvl * 1000.0f);
    }

    f32 rotateRate = MR::abs(mRotation.z) / 70.0f;

    if (mInWater && rotateRate > 0.45f) {
        lvl = MR::getLinerValueFromMinMax(mSurfSpeed, 0.0f, 25.0f, 100.0f, 1000.0f);

        MR::startLevelSound(this, "SE_SM_LV_SURF_RAY_CURVE", lvl);
    }
}

void SurfRay::updateRiderMtx() {
    MtxPtr centerMtx = MR::getJointMtx(this, "Center");
    Pos3f riderMtx;
    riderMtx.setInline(centerMtx);

    riderMtx[0][3] += centerMtx[0][1] * 55.0f + centerMtx[0][2] * 12.0f;
    riderMtx[1][3] += centerMtx[1][1] * 55.0f + centerMtx[1][2] * 12.0f;
    riderMtx[2][3] += centerMtx[2][1] * 55.0f + centerMtx[2][2] * 12.0f;

    MR::setBaseTRMtx(mRider, riderMtx);
}

// Probes the body, 100 ahead, 150 above, 150 to each side across gravity and 150 to each side of the board; the
// first hit fills mWaterInfo.
bool SurfRay::tryInWater() {
    if (MR::getWaterAreaObj(&mWaterInfo, mPosition) || MR::getWaterAreaObj(&mWaterInfo, mFront * 100.0f + mPosition) ||
        MR::getWaterAreaObj(&mWaterInfo, mUp * 150.0f + mPosition)) {
        return true;
    }

    Vec3f rightDir(mFront);
    MR::rotateVecDegree(&rightDir, -mGravity, -90.0f);
    if (MR::getWaterAreaObj(&mWaterInfo, rightDir * 150.0f + mPosition)) {
        return true;
    }

    Vec3f leftDir(mFront);
    MR::rotateVecDegree(&leftDir, -mGravity, 90.0f);
    if (MR::getWaterAreaObj(&mWaterInfo, leftDir * 150.0f + mPosition)) {
        return true;
    }

    return MR::getWaterAreaObj(&mWaterInfo, mSide * 150.0f + mPosition) || MR::getWaterAreaObj(&mWaterInfo, -mSide * 150.0f + mPosition);
}

bool SurfRay::tryJumpOrFall() {
    if (MR::isSpin(PLAYER_1) && (MR::isBinded(this) || mWaterInfo.isInWater() || mAirTime < 15)) {
        // Jumps off the surface at 15, plus 5 per 40 of speed when level, none at a 70 degree roll.
        f32 jumpSpeed = (mSurfSpeed / 40.0f) * (((70.0f - MR::abs(mRotation.z)) * 5.0f) / 70.0f) + 15.0f;
        Vec3f jumpVel(mGroundNormal);

        if (mWaterInfo.isInWater()) {
            jumpVel.set(mWaterInfo.mSurfaceNormal);
        }

        jumpVel.scale(jumpSpeed);

        if (mOrthoVelocity.dot(mGravity) > 0.0f) {
            MR::vecKillElement(mOrthoVelocity, mGravity, &mOrthoVelocity);
        }

        mOrthoVelocity.add(jumpVel);

        if (mOrthoVelocity.length() > 25.0f) {
            MR::normalize(&mOrthoVelocity);
            mOrthoVelocity.mult(25.0f);
        }

        if (mOrthoVelocity.length() > 23.0f) {
            setNerve(GET_NERVE(SurfRay, SurfRayNrvRideJumpHigh));
        } else {
            setNerve(GET_NERVE(SurfRay, SurfRayNrvRideJump));
        }

        return true;
    }

    if (mAirTime > 45) {
        setNerve(GET_NERVE(SurfRay, SurfRayNrvRideFall));
        return true;
    }

    return false;
}

bool SurfRay::isTwistStart() const {
    f32 maxTwist = 0.0f;

    for (s32 i = 1; i < mTwistBufferSize; i++) {
        f32 twistDiff = MR::abs(mTwistBuffer[0] - mTwistBuffer[i]);

        if (twistDiff > maxTwist) {
            maxTwist = twistDiff;
        }
    }

    return maxTwist > ::sTwistStartDiffMin;
}

bool SurfRay::isRotateStart() const {
    if (mInTutorial) {
        return mLectureIdx == 7 || mLectureIdx == 11;
    }

    return mSurfSpeed > 3.0f || MR::isHeldJump(mPadChannel);
}
