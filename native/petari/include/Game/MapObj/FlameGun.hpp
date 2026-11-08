#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FlameGun : public LiveActor {
public:
    FlameGun(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void initAfterPlacement();
    virtual void endClipped();
    virtual void updateHitSensor(HitSensor* pSensor);
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);

    void initSensor();
    void exeSwitchWait();
    void exeWait();
    void exePreRadiate();
    void exeRadiate();
    void exeRotate();
    void exeRadiateOnly();

    s32 mState;
};
