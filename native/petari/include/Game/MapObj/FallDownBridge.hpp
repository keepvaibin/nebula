#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FallDownBridge : public LiveActor {
public:
    FallDownBridge(const char*);

    virtual void init(const TableIter&);
    virtual void control();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);
    virtual void calcAndSetBaseMtx();

    void exeWait();
    void exeQuake();

    Vec3f mClipCenter;
    f32 mAngle;
    f32 mAngleSpeed;
    f32 mMinAngle;
    f32 mMaxAngle;
    bool mIsFallen;
};
