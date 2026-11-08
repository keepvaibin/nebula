#pragma once

#include "Game/NPC/NPCActor.hpp"

class PenguinMaster : public NPCActor {
public:
    PenguinMaster(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeWait();
    void exeReaction();
};
