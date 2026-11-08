#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"

class Polta;

class PoltaStateGroundRockAttack : public ActorStateBase< Polta > {
public:
    PoltaStateGroundRockAttack(Polta*);

    virtual void appear();

    void exeSign();
    void exeGenerate();
    void exeEnd();

    s32 mRockNum;
    bool mIsClockwise;
};
