#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class BigBubble;

class BigBubbleGenerator : public LiveActor {
public:
    BigBubbleGenerator(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void control();

    void initFromJMapParam(const JMapInfoIter&);

    void onActive();
    void offActive();
    bool tryActive();
    bool tryNonActive();

    bool isActiveRange();
    bool isNonActiveRange();

    void exeNonActive();
    void exeActive();

    s32 mSpawnTimer;
    f32 mMinSize;
    f32 mMaxSize;
    s32 mIsObstruct;  // NOTE: is a bool but stored as s32
    s32 mShape;
    s32 mLimitterID;
    BigBubble* mAttachedBubble;
};

namespace MR {
    NameObj* createBigBubbleGenerator(const char*);
    NameObj* createBigObstructBubbleGenerator(const char*);
};  // namespace MR
