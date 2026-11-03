#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"

class Polta;
class HitSensor;

class PoltaStateAttackGround : public ActorStateBase< Polta > {
public:
    PoltaStateAttackGround(Polta*);

    virtual ~PoltaStateAttackGround();
    virtual void appear();

    void exeStart();
    void exeAttack();
    void exeToWait();

    bool isEnableAttack(const HitSensor*) const;

    bool mIsAffectBody;
    s32 mAttackStartLength;
    s32 mAttackLength;
    s32 mAttackToWaitLength;
};
