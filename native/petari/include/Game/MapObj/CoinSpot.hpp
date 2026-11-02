#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class CoinSpot : public LiveActor {
public:
    CoinSpot(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void attackSensor(HitSensor*, HitSensor*);

    void initSensor();
    void exeWait();
    void exeIsInTornado();

    s32 _8C;
    bool mIsInTornado;
    s32 _94;
    s32 _98;
};
