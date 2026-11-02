#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"
#include "Game/LiveActor/LiveActor.hpp"

class WalkerStateParam;
class TerritoryMover;

class WalkerStateWanderParam {
public:
    WalkerStateWanderParam();

    s32 mWaitTime;
    s32 mWalkTime;
    f32 mSpeed;
    f32 mTurnMaxRateDegree;
    f32 mTargetDistance;
};

class WalkerStateWander : public ActorStateBase< LiveActor > {
public:
    WalkerStateWander(LiveActor* pHost, TVec3f* pDirection, WalkerStateParam* pStateParam, WalkerStateWanderParam* pWanderParam);

    virtual void appear();

    void setWanderCenter(const TVec3f& rCenter);
    void exeWait();
    void exeWalk();

    TVec3f* mDirection;
    TerritoryMover* mTerritoryMover;
    WalkerStateParam* mStateParam;
    WalkerStateWanderParam* mWanderParam;
};
