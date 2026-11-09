#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class TreasureSpot : public MapObjActor {
public:
    TreasureSpot(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void exeWait();
    void exeEnd();
    void exeSpout();
    void switchEmitGlow();

private:
    bool isSpouted() const;

public:
    bool mIsCoinFlower;
};
