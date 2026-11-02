#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class BenefitItemOneUp;
class ModelObj;

class WoodBox : public LiveActor {
public:
    WoodBox(const char*);

    virtual ~WoodBox();
    virtual void init(const JMapInfoIter&);
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
    u16 _8E;
    u16 _90;
    u16 mHitPoint;
    bool mHasPowerStar;
    bool mIsNoRespawn;
    bool mPlaySolveSE;
    bool _97;
    Mtx mBaseMtx;
    s32 mCoinCount;
    s32 mStarBitCount;
    ModelObj* mBreakModel;
    ModelObj* mStarDemoModel;
    BenefitItemOneUp* mOneUp;
};
