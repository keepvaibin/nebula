#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"

class TombSpider;

class TombSpiderActionBase : public ActorStateBase< TombSpider > {
public:
    TombSpiderActionBase(TombSpider*, const char*);

    virtual void init();
    virtual void appear();

    bool updateChanceStart();
    bool updateChanceEnd();
    bool tryWaitChanceStartDemo(const Nerve* pNerve);
    bool tryWaitChanceEndDemo(const Nerve* pNerve);
    void startChanceDamage();
    bool updateWait(s32, s32);
    bool updateDamageEye();
    bool updateDamageHip();
    bool updateDamageGland();
    bool updateAttackStart();
    bool updateAttackLoop(s32, s32, f32, f32);
    bool updateAttackEnd();
    bool updateChanceStartNoDemo();
    bool updateChanceStartDemo();
    bool updateChanceWait(s32);
    bool updateChanceDamage();
    bool updateChanceEndNoDemo();
    bool updateChanceEndDemo();

    TombSpider* mParent;
    f32 mRotateAccel;
    f32 mRotateSpeed;
    s32 mHitStep;
    s32 mWaitTime;
    s32 mAttackTime;
    s32 mEnergy;
    // Body z at appear; a chance hit puts the body back there.
    f32 mBattlePosZ;
    bool mIsDemoChance;
};
