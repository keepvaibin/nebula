#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class BallRailPoint {
public:
    BallRailPoint();

    Vec3f mPos;
    Vec3f mSide;
    Vec3f mDir;
};

class BallRail : public LiveActor {
public:
    BallRail(const char*);

    virtual ~BallRail();
    virtual void init(const TableIter&);
    virtual void control();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void initRailPoints();
    void exeWait();
    void exeSetUp();
    void exeRun();
    inline void exeNoBind();

    BallRailPoint* mRailPoints;
    HitSensor* mBindSensor;
    Vec3f mBindStartPos;
    s32 mNumPoints;
    f32 mAcceleration;
    f32 mDeceleration;
    f32 mBindSensorRadius;
};
