#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

class WalkerStateParam {
public:
    WalkerStateParam();

    f32 mGravityAccel;
    f32 mAirFriction;
    f32 mGroundFriction;
    f32 mPlayerNearDistance;
    f32 mPlayerSightFanDegreeH;
    f32 mPlayerSightFanDegreeV;
};
