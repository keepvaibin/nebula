#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class BindCone;

class BallOpener : public LiveActor {
public:
    BallOpener(const char* pName);

    virtual ~BallOpener();
    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();
    virtual void control();
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeWait();
    void exeSetCenter();
    void exeOpen();
    void bindHole();

    BindCone* mCone;
    HitSensor* mBoundSensor;
    TVec3f mHoleGravity;
    TVec3f _A0;
    TVec3f mInitialVelocity;
    TVec3f mBindVelocity;
    s32 mSettledFrames;
};
