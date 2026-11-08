#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class DeadLeaves : public MapObjActor {
public:
    DeadLeaves(const char*);

    virtual void init(const TableIter&);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    void exeWait();
    void exeSpin();

    s32 mItemType;
};
