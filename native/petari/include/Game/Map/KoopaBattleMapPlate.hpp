#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class KoopaBattleMapPlate : public LiveActor {
public:
    KoopaBattleMapPlate(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void calcAndSetBaseMtx() override;
    bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

private:
    Pos3f mBaseMtx;
};
