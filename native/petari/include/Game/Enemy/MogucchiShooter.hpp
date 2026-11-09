#pragma once

#include "Game/LiveActor/PartsModel.hpp"

class MogucchiShooter : public PartsModel {
public:
    MogucchiShooter(LiveActor*, const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void calcAndSetBaseMtx() override;

    void panicDeath();
    void explosion();
    void hitShock();
    void anger();
    void stormStart();
    void storm();
    bool isLaughed() const;

    void exeWait();
    void exeShot();
    void exeTire();
    void exeDeathPanic();
    void exeExplosion();
    void exeLaugh();
    void exeShock();
    void exePanic();
    void exeHitShock();
    void exeHitPanic();
    void exeAnger();
    void exeStormStart();
    void exeStorm();

    void resetDirection();
    void faceToMario();

    Vec3f mFront;
};
