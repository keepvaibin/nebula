#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/ActorMovementUtil.hpp"

class WalkerStateRunawayParam {
public:
    WalkerStateRunawayParam();

    const char* mWaitAction;
    const char* mRunawayAction;
    const char* mJumpAction;
    f32 mGroundAccel;
    f32 mAirAccel;
    f32 mRunFriction;
    f32 mWallJumpFriction;
    f32 mTurnMaxRateDegree;
    f32 mRunawayDistance;
    f32 mWaitDistance;
    f32 mRunawayTurnRateMaxDegree;
    f32 mRunawayTurnRateMinDegree;
    s32 mRunawayTurnTime;
    f32 mPlayerFrontLineLength;
    f32 mRunawayBckRatio;
    f32 mMinRunawayBckRate;
    f32 mMaxRunawayBckRate;
    s32 mWallJumpTime;
    f32 mWallReboundPower;
    f32 mWallJumpPowerH;
    f32 mWallJumpPowerV;
};

class WalkerStateRunaway : public ActorStateBase< LiveActor > {
public:
    WalkerStateRunaway(LiveActor* pHost, Vec3f* pDirection, WalkerStateRunawayParam* pRunawayParam);

    virtual void appear();

    bool tryRunaway();
    bool tryWait();
    bool tryWallJump();
    void exeWait();
    void exeRunaway();
    void exeWallJump();
    bool isRunning() const;

    bool isInWaitRange(f32 range) const {
        return !MR::isNearPlayer(getHost(), range);
    }

    WalkerStateRunawayParam* getParam() const {
        return mRunawayParam;
    }

    WalkerStateRunawayParam* mRunawayParam;
    Vec3f* mDirection;
    f32 mRunawaySpeed;
};
