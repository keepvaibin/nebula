#pragma once

#include "Game/Boss/DinoPackunAction.hpp"
#include <JSystem/JGeometry/TVec.hpp>

class DinoPackunStateDamage;
class DinoPackunTrackFireHolder;
class DinoPackunFireHolder;
class DinoPackunStateAwake;
class DinoPackunStateFire;

class DinoPackunBattleVs2Lv1 : public DinoPackunAction {
public:
    DinoPackunBattleVs2Lv1(DinoPackun*);

    virtual ~DinoPackunBattleVs2Lv1();
    virtual void init();
    virtual void appear();
    virtual void control();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void setTrackFire(DinoPackunTrackFireHolder*);
    void setFireBall(DinoPackunFireHolder*);
    void setMiddleBattle();
    void setLastBattle();
    bool tryAwake();
    void updateOnOffFireTail(s32, s32, s32);
    bool emitFireMouth();
    bool emitFireTail();

    void exeTurn();
    void exeWalk();
    void endWalk();
    void exeFindCrazy();
    void exeCrazy();
    void endCrazy();
    void exeChase();
    void endChase();
    void exeAwake();
    void endAwake();
    void exeAttackHit();
    void exeDamage();

    DinoPackunStateDamage* mStateDamage;
    DinoPackunStateAwake* mStateAwake;
    DinoPackunTrackFireHolder* mTrackFireHolder;
    DinoPackunFireHolder* mFireHolder;
    DinoPackunStateFire* mStateFire;
    TVec3f _28;
    s32 _34;
    u8 _38;
    u8 _39;
    u8 _3A;
    u8 _3B;
    u8 _3C;
    u8 _3D;
};
