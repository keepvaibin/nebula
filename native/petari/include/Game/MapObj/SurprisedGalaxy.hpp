#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SurprisedGalaxy : public LiveActor {
public:
    SurprisedGalaxy(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void exeExit();
    void exeWait();

    const char* mGalaxyName;
};
