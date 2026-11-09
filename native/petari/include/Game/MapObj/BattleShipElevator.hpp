#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class BattleShipElevator : public MapObjActor {
public:
    BattleShipElevator(const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void exeWait() {};
    void exeMove();
    void exeEnd() {};
};
