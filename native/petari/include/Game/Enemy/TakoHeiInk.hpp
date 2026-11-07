#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class TakoHeiInk : public LiveActor {
public:
    TakoHeiInk(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void kill();
    virtual void control();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);

    void start(const Vec3f&, const Vec3f&, bool);
    void exeWait();
};
