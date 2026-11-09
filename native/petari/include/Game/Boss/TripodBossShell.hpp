#pragma once

#include "Game/Boss/TripodBossFixPartsBase.hpp"

class ModelObj;

class TripodBossShell : public TripodBossFixPartsBase {
public:
    TripodBossShell(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;

    virtual void activateTripodBoss() override;

    void exeNonActive();
    void exeWait();
    void exeBreak();

    ModelObj* mBreakModel;
};
