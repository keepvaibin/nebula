#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class TreasureSpot : public MapObjActor {
public:
    TreasureSpot(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void control();
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeWait();
    void exeEnd();
    void exeSpout();
    void switchEmitGlow();

private:
    bool isSpouted() const;

public:
    bool mIsCoinFlower;
};
