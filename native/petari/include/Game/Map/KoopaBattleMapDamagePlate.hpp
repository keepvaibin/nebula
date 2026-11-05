#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class KoopaBattleMapDamagePlate : public LiveActor {
public:
    KoopaBattleMapDamagePlate(const char*);

    virtual ~KoopaBattleMapDamagePlate();
    virtual void init(const JMapInfoIter&);
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void exeWait();
    void exeBreak();
    void exeRecover();

    Pos3f mBaseMtx;
};
