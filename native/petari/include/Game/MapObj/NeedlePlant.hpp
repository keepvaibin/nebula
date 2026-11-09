#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class NeedlePlant : public MapObjActor {
public:
    NeedlePlant(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPush(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;
    virtual void connectToScene(const MapObjActorInitInfo&) override;

    void exeWait();
    void exeShake();

    s32 mObjArg0;
};
