#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class BenefitItemOneUp;
class ModelObj;

class WoodBox : public LiveActor {
public:
    WoodBox(const char*);

    virtual void init(const TableIter&);
    virtual void calcViewAndEntry();
    virtual void kill();
    virtual void control();
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void exeWait();
    void exeHit();
    void exeKilled();
    void doHit(HitSensor*, HitSensor*);

    u16 mFloorTouchTimer;
    u16 mHitPoint;
    bool mHasPowerStar;
    bool mIsNoRespawn;
    bool mPlaySolveSE;
    Mtx mBaseMtx;
    s32 mCoinCount;
    s32 mStarBitCount;
    ModelObj* mBreakModel;
    ModelObj* mStarDemoModel;
    BenefitItemOneUp* mOneUp;
};
