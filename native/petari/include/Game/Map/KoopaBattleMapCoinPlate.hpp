#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class KoopaBattleMapCoinPlate : public LiveActor {
public:
    KoopaBattleMapCoinPlate(const char*);

    virtual void init(const TableIter&) override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

private:
    Pos3f mBaseMtx;
};
