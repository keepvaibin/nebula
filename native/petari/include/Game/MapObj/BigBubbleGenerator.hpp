#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class BigBubble;

class BigBubbleGenerator : public LiveActor {
public:
    BigBubbleGenerator(const char* pName, bool isObstruct);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void control() override;

    void exeNonActive();
    void exeActive();

private:
    void initFromPlacementParam(const TableIter& rIter);
    void onActive();
    void offActive();
    void tryActive();
    void tryNonActive();

    s32 mSpawnTimer;
    f32 mMinSize;
    f32 mMaxSize;
    const bool mIsObstruct;
    s32 mShape;
    s32 mLimitterID;
    BigBubble* mAttachedBubble;
};

namespace MR {
    NameObj* createBigBubbleGenerator(const char* pName);
    NameObj* createBigObstructBubbleGenerator(const char* pName);
};  // namespace MR
