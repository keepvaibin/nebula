#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class DeadLeaves : public MapObjActor {
public:
    DeadLeaves(const char*);

    virtual void init(const TableIter&) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

    void exeWait();
    void exeSpin();

    s32 mItemType;
};
