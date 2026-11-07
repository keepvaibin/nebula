#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class StarPieceSpot : public LiveActor {
public:
    StarPieceSpot(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void emit();
};
