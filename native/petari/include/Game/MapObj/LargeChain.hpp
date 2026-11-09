#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class LargeChainParts;

class LargeChain : public LiveActor {
public:
    LargeChain(const char*);

    virtual void init(const TableIter&) override;
    virtual void makeActorAppeared() override;
    virtual void makeActorDead() override;

    void exeWait();
    void exeBreak();

    void createChainParts();
    void invalidateClippingAllChainParts();

    LargeChainParts* mBottomFixParts;
    LargeChainParts* mTopFixParts;
    LargeChainParts* mChainArray;
    u32 mChainCount;
};
