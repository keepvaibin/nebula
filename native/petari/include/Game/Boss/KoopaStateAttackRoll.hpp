#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"

class HitSensor;
class Koopa;
class KoopaFigureBall;

class KoopaStateAttackRoll : public ActorStateBase< Koopa > {
public:
    KoopaStateAttackRoll(Koopa* pKoopa);

    virtual void init();
    virtual void appear();
    virtual void kill();

    bool tryCalcAndSetBaseMtx();
    bool attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    bool isDamage(u32 msg, HitSensor* pSender, HitSensor* pReceiver) const;
    bool isEnableGuard() const;

    void exeWaitToStart();
    void exeStart();
    void exeRollAir();
    void exeRollGround();
    void exeEndAir();
    void exeEndLand();

    KoopaFigureBall* mFigureBall;
    s32 mRollDelay;
    s32 mRollTime;
    bool mIsHitPlayer;
    bool mIsStartNow;
};
