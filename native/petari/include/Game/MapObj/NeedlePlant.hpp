#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class NeedlePlant : public MapObjActor {
public:
    NeedlePlant(const char* pName);

    virtual void init(const TableIter&);
    virtual void kill();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPush(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);
    virtual void connectToScene(const MapObjActorInitInfo&);

    void exeWait();
    void exeShake();

    s32 mObjArg0;
};
