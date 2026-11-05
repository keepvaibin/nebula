#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"
#include "Game/LiveActor/LiveActor.hpp"

class WalkerStateParam;

class WalkerStateFindPlayerParam {
public:
    WalkerStateFindPlayerParam();

    u32 mJumpStartStep;
    f32 mJumpVelocity;
    f32 mTurnMaxRateDegree;
};

class WalkerStateFindPlayer : public ActorStateBase< LiveActor > {
public:
    WalkerStateFindPlayer(LiveActor* pHost, Vec3f* pPosition, WalkerStateParam* pStateParam, WalkerStateFindPlayerParam* pFindPlayerParam);

    virtual void appear();

    void exeFind();
    void exeFindJumpStart();
    void exeFindJump();
    void exeFindJumpEnd();
    bool isInSightPlayer() const;
    bool isFindJumpBegin() const;
    bool isLandStart() const;

    Vec3f* mDirection;
    WalkerStateParam* mStateParam;
    WalkerStateFindPlayerParam* mFindPlayerParam;
};
