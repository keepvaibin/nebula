#pragma once

#include "Game/System/NerveExecutor.hpp"

class BossKameck;
class BossKameckAction;
class BossKameckBattleDemo;
class HitSensor;
class TableIter;

class BossKameckSequencer : public NerveExecutor {
public:
    BossKameckSequencer(const char*);

    virtual ~BossKameckSequencer() {
    }

    virtual void start() {
    }

    virtual void init(BossKameck*, const TableIter&);
    virtual void update();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgPush(HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    BossKameck* mBossKameck;
    BossKameckAction* _C;
    BossKameckBattleDemo* mBattleDemo;
};