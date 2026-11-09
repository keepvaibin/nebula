#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class IceStep : public LiveActor {
public:
    IceStep(const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;

    void setOn(u32 stepIndex, const Vec3f& rPosition, const Vec3f& rRotation);
    void destroy();
    void doHit();

    void exeWait();
    void exeHit();

    s32 mTimer;
};
