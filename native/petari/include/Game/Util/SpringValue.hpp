#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

// A damped spring around mRestValue.
class SpringValue {
public:
    SpringValue();
    SpringValue(f32 restValue, f32 value, f32 accelFactor, f32 damping, f32 velocity);

    void setParam(f32 restValue, f32 value, f32 accelFactor, f32 damping, f32 velocity);
    f32 update();

    // At rest, still.
    void reset();

    f32 mRestValue;
    f32 mSpringValue;
    f32 mAccelFactor;
    f32 mDamping;
    f32 mVelocity;
};
