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
    f32 _14;  // mGroundFriction? - fric if ._18 <= _1C
    f32 _18;  // mAirFriction? - fric if ._18 > _1C
    s32 _1C;  // step?
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
    WalkerStateRunaway(LiveActor* pHost, TVec3f* pDirection, WalkerStateRunawayParam* pRunawayParam);

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

    // TODO: what are these values?
    bool check18() const {
        return _18 < mRunawayParam->_1C;
    }

    WalkerStateRunawayParam* mRunawayParam;
    TVec3f* mDirection;
    s32 _18;
    f32 mRunawaySpeed;
};
