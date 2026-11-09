#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Map/WaterInfo.hpp"

class ActorJointCtrl;
class ProjmapEffectMtxSetter;

class SurfRay : public LiveActor {
public:
    /// @param padChannel The player.
    SurfRay(const char* pName, s32 padChannel = 0);

    virtual void init(const TableIter&);
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void exeWaitPlayer();
    void exeTurnToWait();
    void exeWait();
    void exeRideAccel();
    void exeRideAccelTwist();
    void exeRideFree();
    void exeRideFreeTwist();
    void exeRideJump();
    void exeRideJumpHigh();
    void exeRideFall();
    void exeRideLand();
    void exeTutorial();
    void exeWipeOut();
    void exeWipeIn();
    void exeReady();

    void resetAllInfo();

    bool updateRideAccel();
    bool updateRideFree();
    bool updateRide();
    void updateInfo();
    void updateToMap();
    void updateToWater();
    void updateAccel();
    void updateRotate();
    void updateSound();
    void updateRiderMtx();

    bool tryInWater();
    bool tryJumpOrFall();
    bool isTwistStart() const;
    bool isRotateStart() const;

    f32 mSurfSpeed;
    Vec3f mOrthoVelocity;

    f32 mSteerRate;
    f32 mSteerAccel;

    Vec3f mFront;
    Vec3f mUp;
    Vec3f mSide;
    Vec3f mBaseUp;    // base up vector for modification
    Vec3f mBaseSide;  // base side vector for modification
    Vec3f mGroundNormal;
    Vec3f mWarpPos;

    LiveActor* mRider;

    s32 mPadChannel;
    f32 mRayTilt;
    bool mInWater;
    s32 mAirTime;
    Vec3f mWaterNormal;
    WaterInfo mWaterInfo;
    Vec3f mWaterShadowPos;
    f32 mShadowAlpha;

    s32 mTwistBufferSize;
    f32* mTwistBuffer;

    bool mInTutorialArea;
    bool mInTutorial;
    s32 mLectureIdx;

    Pos3f mEffectHostMtx;
    ActorJointCtrl* mActorJointCtrl;
    ProjmapEffectMtxSetter* mProjmapFxMtxSetter;
};
