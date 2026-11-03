#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"

class Polta;

class PoltaStateGenerateRock : public ActorStateBase< Polta > {
public:
    PoltaStateGenerateRock(Polta*);

    virtual ~PoltaStateGenerateRock();
    virtual void appear();

    void exeSign();
    void exeGenerate();
    void exeEnd();

    s32 mPatternIndex;
    s32 mIndexIntoPattern;
    s32 mMaxIndexIntoPattern;
    s32 mEndDelayStep;
};
