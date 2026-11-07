#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class IceStep : public LiveActor {
public:
    IceStep(const char*);

    virtual ~IceStep();
    virtual void init(const TableIter&);
    virtual void control();
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);

    void setOn(u32, const Vec3f&, const Vec3f&);
    void destroy();
    void doHit();

    void exeWait();
    void exeHit();

    Mtx _8C;
    u16 mTimer;
};
