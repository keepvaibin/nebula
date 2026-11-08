#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FountainBig : public LiveActor {
public:
    FountainBig(const char*);

    virtual void init(const TableIter&);
    virtual void updateHitSensor(HitSensor*);
    virtual void attackSensor(HitSensor*, HitSensor*);

    void exeWait();
    void exeSign();
    void exeSignStop();
    void exeSpout();
    void exeSpoutEnd();

    Vec3f mClippingRadius;
    s32 mSpoutTimer;
};
