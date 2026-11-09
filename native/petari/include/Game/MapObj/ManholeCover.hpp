#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class ManholeCover : public MapObjActor {
public:
    ManholeCover(const char* pName) : MapObjActor(pName) {
    }

    virtual void init(const TableIter&) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void exeWait();
    void exeOpen();
    void exeRattle();
};
