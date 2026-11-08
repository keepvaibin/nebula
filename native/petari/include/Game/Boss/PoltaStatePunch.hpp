#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"

class Polta;
class HitSensor;
class PoltaArm;

class PoltaStatePunch : public ActorStateBase< Polta > {
public:
    PoltaStatePunch(Polta*);

    virtual void appear();

    void exeStart();
    void exeToWait();
    void exeAttack();

    bool isEnableAttack(const HitSensor*) const;
    bool isEnablePunchArm() const;

    PoltaArm* getAttackActor();

    bool mIsLeftArmActor;
    bool mIsActionAffectBody;
};
