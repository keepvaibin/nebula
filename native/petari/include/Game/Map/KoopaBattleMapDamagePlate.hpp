#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class KoopaBattleMapDamagePlate : public LiveActor {
public:
    KoopaBattleMapDamagePlate(const char*);

    virtual void init(const TableIter&) override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void exeWait();
    void exeBreak();
    void exeRecover();

    Pos3f mBaseMtx;
};
