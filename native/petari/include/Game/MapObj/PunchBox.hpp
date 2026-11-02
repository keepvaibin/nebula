#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class PunchBox : public LiveActor {
public:
    PunchBox(const char*);

    virtual void init(const JMapInfoIter&);
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

    u16 _8C;
    u16 _8E;  // Unused?
    u16 _90;
    u16 _92;
    u16 _94;
    bool _96;
    bool _97;
    bool _98;
    bool mHasShadow;  // Obj_arg1
    s32 mCoinCount;   // Obj_arg0
};
