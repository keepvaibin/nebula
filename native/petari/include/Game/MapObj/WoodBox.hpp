#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class BenefitItemOneUp;
class ModelObj;

class WoodBox : public LiveActor {
public:
    WoodBox(const char*);

    virtual void init(const TableIter&) override;
    virtual void calcViewAndEntry() override;
    virtual void kill() override;
    virtual void control() override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void exeWait();
    void exeHit();
    void exeKilled();
    void doHit(HitSensor*, HitSensor*);

    s32 mFloorTouchTimer;
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
