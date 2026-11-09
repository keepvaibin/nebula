#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SpiderMapBlock : public LiveActor {
public:
    SpiderMapBlock(const char*);

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;

    f32 mPosZ;
};
