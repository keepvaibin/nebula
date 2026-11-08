#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class PalmIsland : public LiveActor {
public:
    PalmIsland(const char*);

    virtual void init(const TableIter&);

    void exeFloat();
    void exeWait();

    s32 mFloatStartStep;
    Vec3f mRipplePos;
    s32 mIsRising;
};
