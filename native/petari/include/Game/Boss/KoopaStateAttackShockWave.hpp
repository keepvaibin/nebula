#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"

class HitSensor;
class Koopa;

class KoopaStateAttackShockWave : public ActorStateBase< Koopa > {
public:
    KoopaStateAttackShockWave(Koopa* pKoopa);

    virtual ~KoopaStateAttackShockWave();
    virtual void init();
    virtual void appear();
    virtual void kill();

    bool attackSensor(HitSensor* pSender, HitSensor* pReceiver);

    void exeJumpStart();
    void exeJump();
    void exeLand();
    void exeJumpFast();

    s32 mMaxAttacks;
    s32 mAttacks;
    f32 mJumpVelocity;
    f32 mGravity;
    s32 mJumpDelay;
};
