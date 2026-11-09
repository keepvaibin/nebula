#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class KillerGunnerSingle : public LiveActor {
public:
    KillerGunnerSingle(const char*);

    virtual void init(const TableIter&) override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;

    void exeWait();
    void exeCharge();
    void exeAttack();
    void exeShoot();
};
