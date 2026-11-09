#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FallDownBridge : public LiveActor {
public:
    FallDownBridge(const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;
    virtual void calcAndSetBaseMtx() override;

    void exeWait();
    void exeQuake();

    Vec3f mClipCenter;
    f32 mAngle;
    f32 mAngleSpeed;
    f32 mMinAngle;
    f32 mMaxAngle;
    bool mIsFallen;
};
