#pragma once

#include "Game/Gravity/PlanetGravity.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

class GravityInfo {
public:
    GravityInfo();

    void init();

    TVec3f mGravityVector;
    s32 mLargestPriority;
    PlanetGravity* mGravityInstance;
};
