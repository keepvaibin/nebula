#pragma once

#include "Game/LiveActor/LiveActor.hpp"

// Carries a rolling ball along its rail.
class BallRail : public LiveActor {
public:
    BallRail(const char*);

    virtual void init(const TableIter&);
    virtual void control();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void exeWait();
    void exeSetUp();
    void exeRun();
    void exeNoBind();

    HitSensor* mBindSensor;
    Vec3f mBindStartPos;
    f32 mAcceleration;
    f32 mDeceleration;
    f32 mBindSensorRadius;
};
