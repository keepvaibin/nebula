#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class CoinSpot : public LiveActor {
public:
    CoinSpot(const char*);

    virtual void init(const TableIter&) override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;

    void initSensor();
    void exeWait();
    void exeIsInTornado();

    s32 mCoinCount;
    bool mIsInTornado;
    s32 mSpawnTimer;
    s32 mSpawnedCount;
};
