#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/JMapInfo.hpp"
#include <JSystem/JGeometry/TVec.hpp>
#include <cstddef>

class ClipAreaDropLaser : public LiveActor {
public:
    ClipAreaDropLaser(const char* pName);
    virtual ~ClipAreaDropLaser();
    virtual void init(const JMapInfoIter& rIter);
    virtual void control();
    virtual void draw() const;
    void exeWait();
    void exeMove();
    void incrementDrawCount();

    TVec3f mPoints[0x40];
    f32 mNumPointsToDraw;
    int mPointIndexToSkipDraw;
    int mDrawCount;
    f32 mSpeed;
};
