#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ModelObj;

class SubmarineVolcanoBigColumn : public LiveActor {
public:
    SubmarineVolcanoBigColumn(const char*);

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;

    void exeWait();
    void exeBreak();
    void pauseOff();
    void initBreakModel(const char*);

    ModelObj* mBreakModel;
    bool mIsSmallColumn;
};
