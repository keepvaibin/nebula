#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class PunchBox : public LiveActor {
public:
    PunchBox(const char*);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void doHit(HitSensor*, HitSensor*);

    void exeWait();
    void exeHit();

    u16 mFloorTouchTimer;
    u16 mHitCoolDown;
    u16 mShadowStep;
    u16 mHitPoint;
    bool mIsPlayerNear;
    bool mHasShadow;  // Obj_arg1
    s32 mCoinCount;   // Obj_arg0
};
