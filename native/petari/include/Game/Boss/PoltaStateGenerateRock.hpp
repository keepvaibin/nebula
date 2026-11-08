#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"

class Polta;

class PoltaStateGenerateRock : public ActorStateBase< Polta > {
public:
    PoltaStateGenerateRock(Polta*);

    virtual void appear();

    void exeSign();
    void exeGenerate();
    void exeEnd();

    s32 mPatternIndex;
    // Rocks made this attack, and how many make one attack.
    s32 mRockCount;
    s32 mRockNum;
    s32 mEndDelayStep;
};
