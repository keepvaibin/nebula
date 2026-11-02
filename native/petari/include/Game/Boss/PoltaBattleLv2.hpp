#pragma once

#include "Game/Boss/PoltaActionBase.hpp"

class PoltaStateStagger;
class PoltaStateGroundRockAttack;
class PoltaStateGenerateRock;
class PoltaStateAttackGround;
class PoltaStatePunch;

class PoltaBattleLv2 : public PoltaActionBase {
public:
    PoltaBattleLv2(Polta*);

    virtual void appear();
    virtual void control();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    bool tryAttackGround();
    void setNerveGenerateRock();
    bool tryGenerateRock();

    void exeBreakBody();
    void exeStagger();
    void exeDamageCore();
    void endStagger();
    void exeDamageBody();
    void exeGenerateRock();
    void exeGenerateGroundRock();
    void exeAttackGround();
    void exePunch();
    void exeWait();

    bool isEnableSensor(const HitSensor*) const;
    bool isEnableAttack(const HitSensor*) const;
    bool isEnableDamage() const;
    bool isEnableScream() const;
    void addDamageBody();

    PoltaStateAttackGround* mStateAttackGround;
    PoltaStatePunch* mStatePunch;
    PoltaStateStagger* mStateStagger;
    PoltaStateGenerateRock* mStateGenerateRock;
    PoltaStateGroundRockAttack* mStateGroundRockAttack;
    s32 mPoltaHealth;
    s32 _2C;
};
