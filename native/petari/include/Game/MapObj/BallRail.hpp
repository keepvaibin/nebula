#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class BallRailPoint {
public:
    BallRailPoint();

    Vec3f _0;
    Vec3f _C;
    Vec3f _18;
    Vec3f _24;
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
    HitSensor* _90;
    Vec3f _94;
    s32 mNumPoints;
    f32 mAcceleration;
    f32 mDeceleration;
    f32 _AC;
};
