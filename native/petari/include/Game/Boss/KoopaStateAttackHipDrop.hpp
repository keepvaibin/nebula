#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"
#include <JSystem/JGeometry/TVec.hpp>

class HitSensor;
class Koopa;

class KoopaStateAttackHipDrop : public ActorStateBase< Koopa > {
public:
    KoopaStateAttackHipDrop(Koopa* pKoopa);

    virtual ~KoopaStateAttackHipDrop();
    virtual void init();
    virtual void appear();
    virtual void kill();

    bool attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    bool isDamage() const;
    bool isEnableGuard() const;

    void exeRun();
    void exeJumpStart();
    void exeJumpUp();
    void exeJumpWaitAir();
    void exeJumpAttackDown();
    void exeLand();
    void exeDamage();

private:
    TVec3f mGroundPosition;
    s32 mMaxAttacks;
    s32 mAttacks;
    s32 mAttackDelay;
    s32 mJumpTime;
};
