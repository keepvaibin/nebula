#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"

class Koopa;

class KoopaStateAttackFireShort : public ActorStateBase< Koopa > {
public:
    KoopaStateAttackFireShort(Koopa* pKoopa);

    virtual ~KoopaStateAttackFireShort();
    virtual void init();
    virtual void appear();

    void exeStart();
    void exeEmit();

    s32 mFireEmitted;
    s32 mMaxFire;
    s32 mFireDelay;
};
