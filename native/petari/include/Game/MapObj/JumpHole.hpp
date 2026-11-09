#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class BindCone;

class JumpHole : public LiveActor {
public:
    JumpHole(const char* pName);

    virtual ~JumpHole() override {
    }

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void control() override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void exeWait();
    void exeSetCenter();
    void exeSetUp();
    void exeRailMove();
    void bindHole();
    void initParabola(const Vec3f& rPosition);

    BindCone* mCone;
    HitSensor* mBoundSensor;
    f32 mLaunchTime;
    Vec3f mLaunchStart;
    Vec3f mBindVelocity;
    Vec3f mHoleGravity;
    Vec3f mLaunchGravity;
    Vec3f mLaunchDirection;
    u32 mLaunchDuration;
    f32 mLaunchDistance;
    f32 mLaunchQuadratic;
    f32 mLaunchLinear;
    s32 mSettledFrames;
};
