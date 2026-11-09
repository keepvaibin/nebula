#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class DinoPackunEggShell : public LiveActor {
public:
    DinoPackunEggShell(const char*, HitSensor*, MtxPtr);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;

    void exeWait();

    HitSensor* mSensor;
    MtxPtr mFollowMtx;
};
