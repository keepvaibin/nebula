#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SurprisedGalaxy : public LiveActor {
public:
    SurprisedGalaxy(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeExit();
    void exeWait();

    const char* mGalaxyName;
};
