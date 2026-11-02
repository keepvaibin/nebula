#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class BindCone;

class JumpHole : public LiveActor {
public:
    JumpHole(const char* pName);

    virtual ~JumpHole() {
    }

    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();
    virtual void control();
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeWait();
    void exeSetCenter();
    void exeSetUp();
    void exeRailMove();
    void bindHole();
    void initParabola(const TVec3f& rPosition);

    BindCone* mCone;
    HitSensor* mBoundSensor;
    f32 mLaunchTime;
    TVec3f mLaunchStart;
    TVec3f mBindVelocity;
    TVec3f mHoleGravity;
    TVec3f mLaunchGravity;
    TVec3f mLaunchDirection;
    u32 mLaunchDuration;
    f32 mLaunchDistance;
    f32 mLaunchQuadratic;
    f32 mLaunchLinear;
    s32 mSettledFrames;
};
