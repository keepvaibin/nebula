#include "Game/Util/SpringValue.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_SpringValue_f1, "SpringValue.f1", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_SpringValue_f2, "SpringValue.f2", 0.8f);

SpringValue::SpringValue() {
    setParam(0.0f, 0.0f, cNum_SpringValue_f1, cNum_SpringValue_f2, 0.0f);
}

SpringValue::SpringValue(f32 restValue, f32 value, f32 accelFactor, f32 damping, f32 velocity) {
    setParam(restValue, value, accelFactor, damping, velocity);
}

void SpringValue::setParam(f32 restValue, f32 value, f32 accelFactor, f32 damping, f32 velocity) {
    mRestValue = restValue;
    mSpringValue = value;
    mAccelFactor = accelFactor;
    mDamping = damping;
    mVelocity = velocity;
}

// Moves by the velocity, then pulls the velocity toward the rest value and damps it.
f32 SpringValue::update() {
    mSpringValue += mVelocity;
    mVelocity += mAccelFactor * (mRestValue - mSpringValue);
    mVelocity *= mDamping;
    return mSpringValue;
}

void SpringValue::reset() {
    mSpringValue = mRestValue;
    mVelocity = 0.0f;
}
