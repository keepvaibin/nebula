#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SwingRopePoint;

class JumpBranch : public LiveActor {
public:
    JumpBranch(const char*);

    virtual void init(const JMapInfoIter& rIter);
    virtual void updateHitSensor(HitSensor* pSensor);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeWait();
    void exeWaitInvalid();
    void exeBind();

    bool updateBind();

    TVec3f mCenter;
    TVec3f mSide;
    TVec3f mUp;
    TVec3f mFront;
    LiveActor* mRider;
    SwingRopePoint* mSwingPoint;
    bool mSwingReverse;
};
