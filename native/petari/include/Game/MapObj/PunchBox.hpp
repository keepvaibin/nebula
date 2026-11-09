#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class PunchBox : public LiveActor {
public:
    PunchBox(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void doHit(HitSensor*, HitSensor*);

    void exeWait();
    void exeHit();

    s32 mFloorTouchTimer;
    s32 mHitCoolDown;
    u32 mShadowStep;
    u16 mHitPoint;
    bool mIsPlayerNear;
    bool mHasShadow;  // Obj_arg1
    s32 mCoinCount;   // Obj_arg0
};
