#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class KoopaBattleMapCoinPlate : public LiveActor {
public:
    KoopaBattleMapCoinPlate(const char*);

    virtual void init(const TableIter&);
    virtual void calcAndSetBaseMtx();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

private:
    Pos3f mBaseMtx;
};
