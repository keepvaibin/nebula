#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class DinoPackunEggShell : public LiveActor {
public:
    DinoPackunEggShell(const char*, HitSensor*, MtxPtr);

    virtual ~DinoPackunEggShell();
    virtual void init(const TableIter&);
    virtual void control();
    virtual void calcAndSetBaseMtx();

    void exeWait();

    HitSensor* mSensor;
    MtxPtr mFollowMtx;
};
