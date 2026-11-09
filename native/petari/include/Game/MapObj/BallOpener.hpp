#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class BindCone;

class BallOpener : public LiveActor {
public:
    BallOpener(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void control() override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void exeWait();
    void exeSetCenter();
    void exeOpen();
    void bindHole();

    BindCone* mCone;
    HitSensor* mBoundSensor;
    Vec3f mHoleGravity;
    Vec3f mInitialVelocity;
    Vec3f mBindVelocity;
    s32 mSettledFrames;
};
