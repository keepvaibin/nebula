#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"

class Koopa;

class KoopaStateAttackFireLong : public ActorStateBase< Koopa > {
public:
    KoopaStateAttackFireLong(Koopa* pKoopa);

    virtual void init();
    virtual void appear();

    void exeStart();
    void exeEmit();

    s32 mMaxFire;
    s32 mFireEmitted;
};
