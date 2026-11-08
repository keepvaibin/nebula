#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SphereRailDash : public LiveActor {
public:
    SphereRailDash(const char*);

    virtual void init(const TableIter&);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void exeWait();
    void exeRailMove();

    HitSensor* mBinderSensor;
    Vec3f mOffset;
    f32 mEndSpeed;
    Vec3f mOffsetVelocity;
    f32 mCoordCubic;
    f32 mCoordQuadratic;
    f32 mCoordLinear;
    s32 mMoveStep;
};
