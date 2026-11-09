#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FountainBig : public LiveActor {
public:
    FountainBig(const char*);

    virtual void init(const TableIter&) override;
    virtual void updateHitSensor(HitSensor*) override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;

    void exeWait();
    void exeSign();
    void exeSignStop();
    void exeSpout();
    void exeSpoutEnd();

    Vec3f mClippingRadius;
    s32 mSpoutTimer;
};
