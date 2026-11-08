#pragma once

#include "Game/Boss/PoltaActionBase.hpp"

class PoltaStateStagger;
class PoltaStateGroundRockAttack;
class PoltaStateGenerateRock;

class PoltaBattleLv1 : public PoltaActionBase {
public:
    PoltaBattleLv1(Polta*);

    virtual void appear();
    virtual void control();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    bool isEnableSensor(const HitSensor*) const;
    bool isEnableDamage() const;
    bool isEnableScream() const;

    void exeWait();
    void exeBreakBody();
    void exeStagger();
    void exeDamageCore();
    void addDamageBody();
    void exeGenerateRock();
    void exeGenerateGroundRock();
    void exeDamageBody();
    void endStagger();

    PoltaStateStagger* mStateStagger;
    PoltaStateGenerateRock* mStateGenerateRock;
    PoltaStateGroundRockAttack* mStateGroundRockAttack;
    s32 mPoltaHealth;
    s32 mAttackIndex;
};
