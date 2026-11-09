#pragma once

#include "Game/LiveActor/PartsModel.hpp"

class WaterBazookaCapsule : public PartsModel {
public:
    WaterBazookaCapsule(LiveActor*, const char*);

    virtual void init(const TableIter& rIter) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void crackCapsule();
    void breakCapsule();
    bool isPlayerOnCapsule() const;

    void exeNormal();
    void exeCrack();
    void exeBreak();
};
