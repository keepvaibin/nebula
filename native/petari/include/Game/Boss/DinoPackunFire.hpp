#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/LiveActorGroup.hpp"

class DinoPackunFire;

class DinoPackunFireHolder : public DeriveActorGroup< DinoPackunFire > {
public:
    DinoPackunFireHolder(s32);
};

class DinoPackunFire : public LiveActor {
public:
    DinoPackunFire(const char*);

    virtual void init(const TableIter&);
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);

    void appearShot(const Vec3f&, const Vec3f&);
    void exeShot();
    void exeGround();

    Quat4f mRollQuat;
};
