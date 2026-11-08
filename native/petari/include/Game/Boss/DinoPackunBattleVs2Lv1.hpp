#pragma once

#include "Game/Boss/DinoPackunAction.hpp"
#include <nebula_geometry.hpp>

class DinoPackunStateDamage;
class DinoPackunTrackFireHolder;
class DinoPackunFireHolder;
class DinoPackunStateAwake;
class DinoPackunStateFire;

class DinoPackunBattleVs2Lv1 : public DinoPackunAction {
public:
    DinoPackunBattleVs2Lv1(DinoPackun*);

    virtual void init();
    virtual void appear();
    virtual void control();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void setTrackFire(DinoPackunTrackFireHolder* pHolder);
    void setFireBall(DinoPackunFireHolder* pHolder);
    void setMiddleBattle();
    void setLastBattle();
    void setBattleLevel(s32 level);
    bool isMovingNerve() const;
    f32 calcWiggleSign(s32 period) const;
    bool tryAwake();
    void startChaseAction();
    void updateChaseMouthFire();
    void updateOnOffFireTail(s32 period, s32 coolSteps, s32 signSteps);
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
    Vec3f mLastFireTailPos;
    s32 mWalkTurnSide;
    bool mHasFireTail;
    bool mHasFireMouth;
    bool mIsShortWalk;
    bool mIsAngry;
    bool mIsFireOnAwake;
    bool mIsCrazyOnAwake;
};
