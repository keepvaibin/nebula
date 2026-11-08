#pragma once

#include "Game/Boss/DinoPackunAction.hpp"

class DinoPackunStateDamage;
class DinoPackunStateFire;
class DinoPackunTrackFireHolder;

class DinoPackunBattleEggVs2 : public DinoPackunAction {
public:
    DinoPackunBattleEggVs2(DinoPackun* pHost);

    virtual void init();
    virtual void appear();
    virtual void control();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void setTrackFire(DinoPackunTrackFireHolder* pHolder);
    void exeTurn();
    void exeWalk();
    void exeDamage();

    DinoPackunStateDamage* mStateDamage;
    DinoPackunTrackFireHolder* mTrackFireHolder;
    DinoPackunStateFire* mStateFire;
};
