#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class BindCone;

class BallOpener : public LiveActor {
public:
    BallOpener(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void initAfterPlacement();
    virtual void control();
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

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
