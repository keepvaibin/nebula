#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"
#include "Game/LiveActor/LiveActor.hpp"

class WalkerStateParam;

class WalkerStateChaseParam {
public:
    WalkerStateChaseParam();

    f32 mChaseSpeed;
    s32 mChaseTime;
    s32 mForceChaseEndTime;
    f32 mTurnMaxRateDegree;
    s32 mChaseEndWaitTime;
};

class WalkerStateChase : public ActorStateBase< LiveActor > {
public:
    WalkerStateChase(LiveActor* pHost, Vec3f* pDirection, WalkerStateParam* pStateParam, WalkerStateChaseParam* pChaseParam);

    virtual void appear();

    void exeStart();
    void exeEnd();
    bool isRunning() const;

    WalkerStateParam* mStateParam;
    WalkerStateChaseParam* mChaseParam;
    Vec3f* mDirection;
};
