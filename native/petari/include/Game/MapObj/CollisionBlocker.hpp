#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class CollisionBlocker : public LiveActor {
public:
    CollisionBlocker(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);

    void forceBreak();
};
