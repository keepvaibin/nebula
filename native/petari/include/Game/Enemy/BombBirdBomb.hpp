#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class BombBirdBomb : public LiveActor {
public:
    BombBirdBomb(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPush(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

    void hold(const Vec3f&);
    void start(const Vec3f&, const Vec3f&);

    void exeHold();
    void exeWait();
    void exeExplosion();

    Quat4f mRotateQuat;
    Vec3f mRotateMoment;
};
