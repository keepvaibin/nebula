#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class BossStinkBugBomb : public LiveActor {
public:
    BossStinkBugBomb(const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void start(const Vec3f&, const Vec3f&);
    void exeWait();
    void exeExplosion();

};
