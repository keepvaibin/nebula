#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class BombBirdBomb : public LiveActor {
public:
    BombBirdBomb(const char* pName);

    virtual void init(const TableIter&);
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPush(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    void hold(const Vec3f&);
    void start(const Vec3f&, const Vec3f&);

    void exeHold();
    void exeWait();
    void exeExplosion();

    Quat4f mRotateQuat;
    Vec3f mRotateMoment;
};
