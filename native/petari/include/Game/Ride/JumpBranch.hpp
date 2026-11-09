#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SwingRopePoint;

class JumpBranch : public LiveActor {
public:
    JumpBranch(const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void updateHitSensor(HitSensor* pSensor) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void exeWait();
    void exeWaitInvalid();
    void exeBind();

    bool updateBind();

    Vec3f mCenter;
    Vec3f mSide;
    Vec3f mUp;
    Vec3f mFront;
    LiveActor* mRider;
    SwingRopePoint* mSwingPoint;
    bool mSwingReverse;
};
