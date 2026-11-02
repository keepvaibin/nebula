#pragma once

#include <JSystem/JGeometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class LiveActor;

class TerritoryMover {
public:
    TerritoryMover(f32);

    void decideNextTargetPos(const LiveActor*);
    bool isReachedTarget(const LiveActor*, f32);

    void setCenter(const TVec3f& rPos) {
        mCenter.set(rPos);
    }

    f32 mRadius;
    TVec3f mCenter;
    TVec3f mTarget;
};
