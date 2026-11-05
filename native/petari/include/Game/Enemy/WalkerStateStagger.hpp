#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"
#include "Game/LiveActor/LiveActor.hpp"

class WalkerStateParam;
class WalkerStateStaggerParam;

class WalkerStateStaggerParam {
public:
    WalkerStateStaggerParam();

    f32 mPunchPowerH;
    f32 mPunchPowerV;
    f32 mPunchPowerSideH;
    s32 mStopSceneFrames;
    s32 mKickEnableStep;
    s32 mStaggerTime;
    s32 mRotateStartTime;
    s32 mRotateEndTime;
    f32 mStaggerFrontPower;
    f32 mStaggerSidePower;
    f32 mStaggerSideCircleRateDegree;
    f32 mRotateRateDegree;
};

class WalkerStateStagger : public ActorStateBase< LiveActor > {
public:
    WalkerStateStagger(LiveActor* pHost, Vec3f* pDirection, WalkerStateParam* pStateParam, WalkerStateStaggerParam* pStaggerParam);

    virtual void appear();

    void setPunchDirection(HitSensor* pSender, HitSensor* pReceiver);
    void exeStagger();
    void exeStaggerEnd();
    void reboundWall();
    bool isEnableKick() const;
    bool isUpsideDown() const;
    bool isStaggerStart() const;
    bool isSwooning(s32 swoonStep) const;
    bool isSpinning(s32 spinStartStep, s32 spinEndStep) const;
    bool isRecoverStart() const;

    WalkerStateParam* mStateParam;
    WalkerStateStaggerParam* mStaggerParam;
    Vec3f mVelH;
    Vec3f* mDirection;
};
