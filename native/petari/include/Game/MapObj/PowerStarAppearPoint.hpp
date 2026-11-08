#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ActorCameraInfo;

class PowerStarAppearPoint : public LiveActor {
public:
    PowerStarAppearPoint(const char* pName);

    virtual ~PowerStarAppearPoint();
    virtual void init(const TableIter& rIter);

    ActorCameraInfo* mCameraInfo;
};
