#pragma once

#include "Game/Player/MarioState.hpp"
#include <nebula_math.h>

class MarioActor;

class MarioSlider : public MarioState {
public:
    MarioSlider(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();
    virtual bool postureCtrl(MtxPtr);

    void calcGroundAccel();
    void calcWallHit();

    Vec3f mVelocity;  // the slide's velocity
    Vec3f mSlopeDir;  // down the slope (calcGroundAccel)
    f32 mBrake;  // the stick's brake, smoothed
    f32 mLean;  // the stick's lean, smoothed
    u16 mAirTimer;  // frames off the ground before the drop
};
