#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SwitchBox : public LiveActor {
public:
    SwitchBox(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void appear() override;
    virtual void kill() override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void exeWait();
    void exeHit();
    void doHit(HitSensor* pSender, HitSensor* pReceiver);

    s32 mFloorTouchTimer;
    s32 mHitCoolDown;
    u16 mHitPoint;
};
