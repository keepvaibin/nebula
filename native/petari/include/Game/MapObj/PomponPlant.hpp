#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class PomponPlant : public LiveActor {
public:
    PomponPlant(const char*);

    virtual void init(const TableIter&) override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

    bool tryGenItem();
    void exeWait();
    void exeSwingPointer();
    void exeSwingPlayer();

    s32 mItemType;
    s32 mItemCount;
    bool mCanGenItem;
};
