#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class TreasureSpot : public MapObjActor {
public:
    TreasureSpot(const char*);

    virtual void init(const TableIter&);
    virtual void control();
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    void exeWait();
    void exeEnd();
    void exeSpout();
    void switchEmitGlow();

    bool mIsCoinFlower;
};
