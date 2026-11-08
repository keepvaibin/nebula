#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SpiderMapBlock : public LiveActor {
public:
    SpiderMapBlock(const char*);

    virtual void init(const TableIter&);
    virtual void kill();
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);

    f32 mPosZ;
};
