#pragma once

#include "Game/LiveActor//LiveActor.hpp"

class SkeletalFishBabyRailHolder;

class SkeletalFishBabyRail : public LiveActor {
public:
    SkeletalFishBabyRail(const char*);

    virtual ~SkeletalFishBabyRail();
    virtual void init(const TableIter&);

    s32 _8C;
};