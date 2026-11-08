#pragma once

#include "Game/Boss/TripodBossFixPartsBase.hpp"

class ModelObj;

class TripodBossCore : public TripodBossFixPartsBase {
public:
    TripodBossCore(const char* pName);

    virtual void init(const TableIter&);
    virtual void kill();
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);

    virtual void activateTripodBoss();

    void exeNonActive();
    void exeWait();
    void exeDamageDemo();
    void exeWarning();
    void exeBreak();

    ModelObj* mBreakModel;
    ModelObj* mBloomModel;
};
