#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class GroundChecker : public LiveActor {
public:
    GroundChecker(const char* pName, f32 radius, f32 offsetY);

    void bringFriction(f32 target, f32 rate);

    f32 mFriction;
};
