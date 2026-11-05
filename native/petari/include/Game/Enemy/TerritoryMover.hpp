#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class LiveActor;

class TerritoryMover {
public:
    TerritoryMover(f32);

    void decideNextTargetPos(const LiveActor*);
    bool isReachedTarget(const LiveActor*, f32);

    void setCenter(const Vec3f& rPos) {
        mCenter.set(rPos);
    }

    f32 mRadius;
    Vec3f mCenter;
    Vec3f mTarget;
};
