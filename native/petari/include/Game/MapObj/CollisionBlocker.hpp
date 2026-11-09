#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class CollisionBlocker : public LiveActor {
public:
    CollisionBlocker(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;

    void forceBreak();
};
