#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class BossStinkBugBomb : public LiveActor {
public:
    BossStinkBugBomb(const char*);

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void start(const Vec3f&, const Vec3f&);
    void exeWait();
    void exeExplosion();

    Quat4f _8C;
    Vec3f _9C;
};
