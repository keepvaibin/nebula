#pragma once

#include "Game/System/NerveExecutor.hpp"

class BossKameck;
class BossKameckBattleDemo;
class HitSensor;
class TableIter;

class BossKameckSequencer : public NerveExecutor {
public:
    BossKameckSequencer(const char*);

    virtual ~BossKameckSequencer() override {
    }

    virtual void start() {
    }

    virtual void init(BossKameck*, const TableIter&);
    virtual void update();
    virtual void attackSensor(HitSensor*, HitSensor*) = 0;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) = 0;

    BossKameck* mBossKameck;
    BossKameckBattleDemo* mBattleDemo;
};