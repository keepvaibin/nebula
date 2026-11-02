#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class LargeChainParts;

class LargeChain : public LiveActor {
public:
    LargeChain(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void makeActorAppeared();
    virtual void makeActorDead();

    void exeWait();
    void exeBreak();

    void createChainParts();
    void invalidateClippingAllChainParts();

    LargeChainParts* _8C;
    LargeChainParts* _90;
    LargeChainParts* mChainArray;
    u32 mChainCount;
};
