#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class Jiraira : public LiveActor {
public:
    Jiraira(const char*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void exeWait();
    void exeStepped();
    void exeExplode();
    void exePreRecover();
    void exeRecover();

    CollisionParts* mBodyCollision;
    f32 mExplodeRadius;
};
