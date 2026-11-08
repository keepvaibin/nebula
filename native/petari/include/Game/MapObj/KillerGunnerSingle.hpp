#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class KillerGunnerSingle : public LiveActor {
public:
    KillerGunnerSingle(const char*);

    virtual void init(const TableIter&);
    virtual void attackSensor(HitSensor*, HitSensor*);

    void exeWait();
    void exeCharge();
    void exeAttack();
    void exeShoot();
};
