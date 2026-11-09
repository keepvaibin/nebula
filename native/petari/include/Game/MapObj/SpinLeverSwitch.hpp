#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class MapObjConnector;

class SpinLeverSwitch : public LiveActor {
public:
    SpinLeverSwitch(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void exeWait();
    void exeSwitchOn();
    void exeEnd();

    MapObjConnector* mConnector;
};
