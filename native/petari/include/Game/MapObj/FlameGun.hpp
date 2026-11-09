#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FlameGun : public LiveActor {
public:
    FlameGun(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void endClipped() override;
    virtual void updateHitSensor(HitSensor* pSensor) override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;

    void initSensor();
    void exeSwitchWait();
    void exeWait();
    void exePreRadiate();
    void exeRadiate();
    void exeRotate();
    void exeRadiateOnly();

    s32 mState;
};
