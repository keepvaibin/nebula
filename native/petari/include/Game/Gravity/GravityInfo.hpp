#pragma once

#include "Game/Gravity/PlanetGravity.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

class GravityInfo {
public:
    GravityInfo();

    void init();

    Vec3f mGravityVector;
    PlanetGravity* mGravityInstance;
};
