#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class CoinBox : public LiveActor {
public:
    CoinBox(const char*);

    virtual void init(const TableIter&) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void exeWait();
    void exeHit();
};
