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
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcWaterShadowPos_f1, "calcWaterShadowPos.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 260.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTutorial_f1, "exeTutorial.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTutorial_f2, "exeTutorial.f2", 0.85f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTutorial_f3, "exeTutorial.f3", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTutorial_f4, "exeTutorial.f4", 0.6f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeReady_f1, "exeReady.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateInfo_f1, "updateInfo.f1", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateInfo_f2, "updateInfo.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateInfo_f3, "updateInfo.f3", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateInfo_f4, "updateInfo.f4", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateToMap_f1, "updateToMap.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateToMap_f2, "updateToMap.f2", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateToWater_f1, "updateToWater.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateToWater_f2, "updateToWater.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateToWater_f3, "updateToWater.f3", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateToWater_f4, "updateToWater.f4", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateToWater_f5, "updateToWater.f5", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateToWater_f6, "updateToWater.f6", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateToWater_f7, "updateToWater.f7", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateToWater_f8, "updateToWater.f8", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateToWater_f9, "updateToWater.f9", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateToWater_f10, "updateToWater.f10", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateToWater_f11, "updateToWater.f11", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateAccel_f1, "updateAccel.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateAccel_f2, "updateAccel.f2", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateAccel_f3, "updateAccel.f3", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateAccel_f4, "updateAccel.f4", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateAccel_f5, "updateAccel.f5", 0.995f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateAccel_f6, "updateAccel.f6", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateAccel_f7, "updateAccel.f7", 0.95f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotate_f1, "updateRotate.f1", 0.7f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotate_f2, "updateRotate.f2", 0.15f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotate_f3, "updateRotate.f3", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotate_f4, "updateRotate.f4", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotate_f5, "updateRotate.f5", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotate_f6, "updateRotate.f6", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotate_f7, "updateRotate.f7", 0.95f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotate_f8, "updateRotate.f8", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotate_f9, "updateRotate.f9", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotate_f10, "updateRotate.f10", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotate_f11, "updateRotate.f11", 0.95f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotate_f12, "updateRotate.f12", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotate_f13, "updateRotate.f13", 0.7f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotate_f14, "updateRotate.f14", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotate_f15, "updateRotate.f15", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotate_f16, "updateRotate.f16", 1.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotate_f17, "updateRotate.f17", 0.00028f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateSound_f1, "updateSound.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateSound_f2, "updateSound.f2", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateSound_f3, "updateSound.f3", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateSound_f4, "updateSound.f4", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateSound_f5, "updateSound.f5", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateSound_f6, "updateSound.f6", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateSound_f7, "updateSound.f7", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateSound_f8, "updateSound.f8", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateSound_f9, "updateSound.f9", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateSound_f10, "updateSound.f10", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateSound_f11, "updateSound.f11", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateSound_f12, "updateSound.f12", 0.45f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateSound_f13, "updateSound.f13", 25.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateSound_f14, "updateSound.f14", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateSound_f15, "updateSound.f15", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRiderMtx_f1, "updateRiderMtx.f1", 55.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRiderMtx_f2, "updateRiderMtx.f2", 12.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRiderMtx_f3, "updateRiderMtx.f3", 55.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRiderMtx_f4, "updateRiderMtx.f4", 12.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRiderMtx_f5, "updateRiderMtx.f5", 55.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRiderMtx_f6, "updateRiderMtx.f6", 12.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryInWater_f1, "tryInWater.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryInWater_f2, "tryInWater.f2", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryInWater_f3, "tryInWater.f3", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryInWater_f4, "tryInWater.f4", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryInWater_f5, "tryInWater.f5", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryInWater_f6, "tryInWater.f6", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryInWater_f7, "tryInWater.f7", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryInWater_f8, "tryInWater.f8", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryJumpOrFall_f1, "tryJumpOrFall.f1", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryJumpOrFall_f2, "tryJumpOrFall.f2", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryJumpOrFall_f3, "tryJumpOrFall.f3", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryJumpOrFall_f4, "tryJumpOrFall.f4", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryJumpOrFall_f5, "tryJumpOrFall.f5", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryJumpOrFall_f6, "tryJumpOrFall.f6", 25.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryJumpOrFall_f7, "tryJumpOrFall.f7", 25.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryJumpOrFall_f8, "tryJumpOrFall.f8", 23.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isRotateStart_f1, "isRotateStart.f1", 3.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_updateInfo_reboundVelocityFromCollision_1, "updateInfo.reboundVelocityFromCollision.1", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateInfo_reboundVelocityFromCollision_2, "updateInfo.reboundVelocityFromCollision.2", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateInfo_reboundVelocityFromCollision_3, "updateInfo.reboundVelocityFromCollision.3", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateInfo_reboundVelocityFromCollision_4, "updateInfo.reboundVelocityFromCollision.4", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateInfo_reboundVelocityFromCollision_5, "updateInfo.reboundVelocityFromCollision.5", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateInfo_reboundVelocityFromCollision_6, "updateInfo.reboundVelocityFromCollision.6", 1.0f);

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
    NEBULA_TUNABLE(f32, sDistancePlayerNear, 1300.0f);
    NEBULA_TUNABLE(f32, sSpeedToRumble, 5.0f);
    NEBULA_TUNABLE(f32, sBlendRatioAxisUpAir, 0.02f);
    NEBULA_TUNABLE(f32, sBlendRatioAxisUpGround, 0.1f);
    NEBULA_TUNABLE(f32, sBlendRatioAxisFrontWall, 0.1f);
    NEBULA_TUNABLE(f32, sTwistStartDiffMin, 0.2f);
    NEBULA_TUNABLE(f32, sBtkSpeedMin, 1.0f);
    NEBULA_TUNABLE(f32, sBtkSpeedMax, 4.0f);
    NEBULA_TUNABLE(f32, sBtkSpeedRate, 0.4f);
    NEBULA_TUNABLE(f32, sShadowAlphaMax, 64.0f);
    NEBULA_TUNABLE(f32, sShadowAlphaSpeed, 10.0f);
    NEBULA_TUNABLE(f32, sSpeedToEmitEffect, 6.0f);
    NEBULA_TUNABLE(f32, sSpeedToEmitEffectFast, 20.0f);
    NEBULA_TUNABLE(f32, sMoveSe0StartSpeed, 2.5f);
    NEBULA_TUNABLE(f32, sMoveSe1StartSpeed, 7.5f);
    NEBULA_TUNABLE(f32, sMoveSe2StartSpeed, 14.0f);
    NEBULA_TUNABLE(f32, sMoveSe3StartSpeed, 25.0f);
    NEBULA_TUNABLE(f32, sMoveSe3MaxSpeed, 35.0f);
    NEBULA_TUNABLE(f32, sWindSeStartSpeed, 32.0f);
    NEBULA_TUNABLE(f32, sWindSeMaxSpeed, 50.0f);

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

            sample.add(rShadowDir * cNum_calcWaterShadowPos_f1);
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
    MR::addHitSensorRide(this, "body", 8, cNum_init_f1, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorBinder(this, "bind", 8, cNum_init_f2, Vec3f(0.0f, 0.0f, 0.0f));

    initBinder(cNum_init_f3, cNum_init_f4, 8);
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
    mOrthoVelocity.mult(cNum_exeTutorial_f1);
    mFront.set(heldFront);

    mSurfSpeed = 0.0f;
    mRotation.z *= cNum_exeTutorial_f2;

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

        f32 rot = MR::abs((mRotation.z / cNum_exeTutorial_f3) * cNum_exeTutorial_f4);

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
    mOrthoVelocity.mult(cNum_exeReady_f1);
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
        f32 sfxLvl = MR::getLinerValueFromMinMax(mSurfSpeed, 0.0f, cNum_updateInfo_f1, cNum_updateInfo_f2, cNum_updateInfo_f3);

        MR::startLevelSound(this, "SE_SM_LV_SURF_RAY_LAND", sfxLvl);

        if (MR::reboundVelocityFromCollision(this, cNum_updateInfo_reboundVelocityFromCollision_1, cNum_updateInfo_reboundVelocityFromCollision_2, cNum_updateInfo_reboundVelocityFromCollision_3) && mVelocity.length() > ::sSpeedToRumble) {
            MR::tryRumblePadMiddle(this, mPadChannel);
        }
    }

    if (MR::isBindedWall(this)) {
        if (mVelocity.dot(*MR::getWallNormal(this)) < -cNum_updateInfo_f4) {
            MR::startSound(this, "SE_SM_SURF_RAY_HIT_WALL");
        }

        if (MR::reboundVelocityFromCollision(this, cNum_updateInfo_reboundVelocityFromCollision_4, cNum_updateInfo_reboundVelocityFromCollision_5, cNum_updateInfo_reboundVelocityFromCollision_6) && mVelocity.length() > ::sSpeedToRumble) {
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
        fall.scale(cNum_updateToMap_f1);
    } else {
        fall.scale(cNum_updateToMap_f2);
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
            f32 lvl = MR::getLinerValueFromMinMax(mOrthoVelocity.length(), 0.0f, cNum_updateToWater_f1, cNum_updateToWater_f2, cNum_updateToWater_f3);

            if (mAirTime > NebulaFrameClock::stepsOfMilliseconds(500)) {
                MR::tryRumblePadMiddle(this, mPadChannel);
            }

            if (mAirTime > 80 && mOrthoVelocity.length() > cNum_updateToWater_f4) {
                MR::startSound(this, "SE_SM_SURF_RAY_JUMPIN2");
            } else if (mAirTime > 5 && mOrthoVelocity.length() > cNum_updateToWater_f5) {
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
            mVelocity.sub(mGravity * innerProd * cNum_updateToWater_f6);
        } else {
            f32 waveDepth = mWaterInfo.mCamWaterDepth + mWaterInfo.mWaveHeight;
            mVelocity.sub(mGravity * waveDepth * cNum_updateToWater_f7);
        }
    } else {
        if (mInWater) {
            f32 lvl = MR::getLinerValueFromMinMax(mSurfSpeed, 0.0f, cNum_updateToWater_f8, cNum_updateToWater_f9, cNum_updateToWater_f10);
            MR::startSound(this, "SE_SM_SURF_RAY_JUMPOUT1", lvl);
            mInWater = false;
        }

        mAirTime++;
    }

    WaterInfo waterInfo;
    Vec3f v(mPosition + mGravity * cNum_updateToWater_f11);

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
        mSurfSpeed += cNum_updateAccel_f1;
    } else {
        mSurfSpeed *= cNum_updateAccel_f2;
    }

    mSurfSpeed = MR::clamp(mSurfSpeed, 0.0f, cNum_updateAccel_f3);

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
        mOrthoVelocity.mult(cNum_updateAccel_f4);
    } else {
        mOrthoVelocity.mult(cNum_updateAccel_f5);
    }

    if (MR::isNearZero(mOrthoVelocity, cNum_updateAccel_f6)) {
        mOrthoVelocity.zero();
    }

    if (mWaterInfo.isInWater() && !isNerve(GET_NERVE(SurfRay, SurfRayNrvRideJump))) {
        mOrthoVelocity.mult(cNum_updateAccel_f7);
    }
}

void SurfRay::updateRotate() {
    f32 steerAccel;
    f32 accelX = 0.0f;

    if (isRotateStart()) {
        Vec3f accel(0.0f, 0.0f, 0.0f);
        MR::getMainTilt(&accel, mPadChannel);

        if (accel.z > -cNum_updateRotate_f1 || MR::abs(accel.x) > cNum_updateRotate_f2) {
            f32 twistAngle = accel.x;
            f32 accelZ = MR::clamp(-accel.z, 0.0f, 1.0f);

            if (twistAngle > 0.0f) {
                twistAngle += accelZ;
            } else if (twistAngle < 0.0f) {
                twistAngle -= accelZ;
            }

            steerAccel = MR::clamp(twistAngle, -cNum_updateRotate_f3, cNum_updateRotate_f4);
            accelX = accel.x;

            // Not for 0 or NaN.
            if (accelX < 0.0f || accelX > 0.0f) {
                mRayTilt = MR::getInterpolateValue(cNum_updateRotate_f5, mRayTilt, MR::clamp(accelX, -1.0f, 1.0f));
            }
        } else {
            mRayTilt = MR::getInterpolateValue(cNum_updateRotate_f6, mRayTilt, 0.0f);
            steerAccel = mSteerAccel * cNum_updateRotate_f7;
        }

        mSteerAccel = steerAccel;
        mSteerRate += steerAccel;
        mSteerRate = MR::clamp(mSteerRate, -cNum_updateRotate_f8, cNum_updateRotate_f9);
    } else {
        mRayTilt = MR::getInterpolateValue(cNum_updateRotate_f10, mRayTilt, 0.0f);
        mSteerAccel = 0.0f;
    }

    mSteerRate *= cNum_updateRotate_f11;
    if (MR::abs(mSteerRate) < cNum_updateRotate_f12) {
        mSteerRate = 0.0f;
    }

    mRotation.z *= cNum_updateRotate_f13;
    mRotation.z += mSteerRate;
    mRotation.z = MR::clamp(mRotation.z, -cNum_updateRotate_f14, cNum_updateRotate_f15);

    f32 rayTilt = MR::clamp(mRayTilt * cNum_updateRotate_f16, -1.0f, 1.0f);

    if (rayTilt > 0.0f) {
        MR::setBckBlendWeight(1.0f - rayTilt, 0.0f, rayTilt);
    } else {
        MR::setBckBlendWeight(rayTilt + 1.0f, -rayTilt, 0.0f);
    }

    Rot3f mtx;
    mtx.identity();
    mtx.setRotate(mBaseUp, -mRotation.z * cNum_updateRotate_f17);
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
            lvl = MR::getLinerValueFromMinMax(mSurfSpeed, ::sMoveSe0StartSpeed, ::sMoveSe1StartSpeed, cNum_updateSound_f1, 1.0f);
        } else {
            lvl = MR::getLinerValueFromMinMax(mSurfSpeed, ::sMoveSe2StartSpeed, ::sMoveSe3StartSpeed, 1.0f, 0.0f);
        }

        MR::startLevelSound(this, "SE_SM_LV_SURF_RAY_MOVE0", lvl * cNum_updateSound_f2);
    }

    if (mInWater) {
        if (mSurfSpeed > ::sMoveSe1StartSpeed) {
            lvl = MR::getLinerValueFromMinMax(mSurfSpeed, ::sMoveSe1StartSpeed, ::sMoveSe2StartSpeed, cNum_updateSound_f3, 1.0f);
            MR::startLevelSound(this, "SE_SM_LV_SURF_RAY_MOVE1", lvl * cNum_updateSound_f4);
        }

        if (mSurfSpeed > ::sMoveSe2StartSpeed) {
            lvl = MR::getLinerValueFromMinMax(mSurfSpeed, ::sMoveSe2StartSpeed, ::sMoveSe3StartSpeed, cNum_updateSound_f5, 1.0f);
            MR::startLevelSound(this, "SE_SM_LV_SURF_RAY_MOVE2", lvl * cNum_updateSound_f6);
        }

        if (mSurfSpeed > ::sMoveSe3StartSpeed) {
            lvl = MR::getLinerValueFromMinMax(mSurfSpeed, ::sMoveSe3StartSpeed, ::sMoveSe3MaxSpeed, cNum_updateSound_f7, 1.0f);
            MR::startLevelSound(this, "SE_SM_LV_SURF_RAY_MOVE3", lvl * cNum_updateSound_f8);
        }
    }

    if (mVelocity.length() > ::sWindSeStartSpeed) {
        lvl = MR::getLinerValueFromMinMax(mVelocity.length(), ::sWindSeStartSpeed, ::sWindSeMaxSpeed, cNum_updateSound_f9, 1.0f);

        MR::startLevelSound(this, "SE_SM_SURF_RAY_MOVE_WIND", lvl * cNum_updateSound_f10);
    }

    f32 rotateRate = MR::abs(mRotation.z) / cNum_updateSound_f11;

    if (mInWater && rotateRate > cNum_updateSound_f12) {
        lvl = MR::getLinerValueFromMinMax(mSurfSpeed, 0.0f, cNum_updateSound_f13, cNum_updateSound_f14, cNum_updateSound_f15);

        MR::startLevelSound(this, "SE_SM_LV_SURF_RAY_CURVE", lvl);
    }
}

void SurfRay::updateRiderMtx() {
    MtxPtr centerMtx = MR::getJointMtx(this, "Center");
    Pos3f riderMtx;
    riderMtx.setInline(centerMtx);

    riderMtx[0][3] += centerMtx[0][1] * cNum_updateRiderMtx_f1 + centerMtx[0][2] * cNum_updateRiderMtx_f2;
    riderMtx[1][3] += centerMtx[1][1] * cNum_updateRiderMtx_f3 + centerMtx[1][2] * cNum_updateRiderMtx_f4;
    riderMtx[2][3] += centerMtx[2][1] * cNum_updateRiderMtx_f5 + centerMtx[2][2] * cNum_updateRiderMtx_f6;

    MR::setBaseTRMtx(mRider, riderMtx);
}

// Probes the body, 100 ahead, 150 above, 150 to each side across gravity and 150 to each side of the board; the
// first hit fills mWaterInfo.
bool SurfRay::tryInWater() {
    if (MR::getWaterAreaObj(&mWaterInfo, mPosition) || MR::getWaterAreaObj(&mWaterInfo, mFront * cNum_tryInWater_f1 + mPosition) ||
        MR::getWaterAreaObj(&mWaterInfo, mUp * cNum_tryInWater_f2 + mPosition)) {
        return true;
    }

    Vec3f rightDir(mFront);
    MR::rotateVecDegree(&rightDir, -mGravity, -cNum_tryInWater_f3);
    if (MR::getWaterAreaObj(&mWaterInfo, rightDir * cNum_tryInWater_f4 + mPosition)) {
        return true;
    }

    Vec3f leftDir(mFront);
    MR::rotateVecDegree(&leftDir, -mGravity, cNum_tryInWater_f5);
    if (MR::getWaterAreaObj(&mWaterInfo, leftDir * cNum_tryInWater_f6 + mPosition)) {
        return true;
    }

    return MR::getWaterAreaObj(&mWaterInfo, mSide * cNum_tryInWater_f7 + mPosition) || MR::getWaterAreaObj(&mWaterInfo, -mSide * cNum_tryInWater_f8 + mPosition);
}

bool SurfRay::tryJumpOrFall() {
    if (MR::isSpin(PLAYER_1) && (MR::isBinded(this) || mWaterInfo.isInWater() || mAirTime < 15)) {
        // Jumps off the surface at 15, plus 5 per 40 of speed when level, none at a 70 degree roll.
        f32 jumpSpeed = (mSurfSpeed / cNum_tryJumpOrFall_f1) * (((cNum_tryJumpOrFall_f2 - MR::abs(mRotation.z)) * cNum_tryJumpOrFall_f3) / cNum_tryJumpOrFall_f4) + cNum_tryJumpOrFall_f5;
        Vec3f jumpVel(mGroundNormal);

        if (mWaterInfo.isInWater()) {
            jumpVel.set(mWaterInfo.mSurfaceNormal);
        }

        jumpVel.scale(jumpSpeed);

        if (mOrthoVelocity.dot(mGravity) > 0.0f) {
            MR::vecKillElement(mOrthoVelocity, mGravity, &mOrthoVelocity);
        }

        mOrthoVelocity.add(jumpVel);

        if (mOrthoVelocity.length() > cNum_tryJumpOrFall_f6) {
            MR::normalize(&mOrthoVelocity);
            mOrthoVelocity.mult(cNum_tryJumpOrFall_f7);
        }

        if (mOrthoVelocity.length() > cNum_tryJumpOrFall_f8) {
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

    return mSurfSpeed > cNum_isRotateStart_f1 || MR::isHeldJump(mPadChannel);
}
