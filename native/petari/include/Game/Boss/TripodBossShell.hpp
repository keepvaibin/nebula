#pragma once

#include "Game/Boss/TripodBossFixPartsBase.hpp"

class ModelObj;

class TripodBossShell : public TripodBossFixPartsBase {
public:
    TripodBossShell(const char* pName);

    virtual void init(const TableIter&);
    virtual void kill();
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);

    virtual void activateTripodBoss();

    void exeNonActive();
    void exeWait();
    void exeBreak();

    ModelObj* mBreakModel;
};
