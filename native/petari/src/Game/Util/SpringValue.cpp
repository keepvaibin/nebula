#include "Game/Util/SpringValue.hpp"

SpringValue::SpringValue() {
    mRestValue = 0.0f;
    mSpringValue = 0.0f;
    mAccelFactor = 0.3f;
    mDamping = 0.8f;
    mVelocity = 0.0f;
}

SpringValue::SpringValue(f32 restValue, f32 value, f32 accelFactor, f32 damping, f32 velocity) {
    mRestValue = restValue;
    mSpringValue = value;
    mAccelFactor = accelFactor;
    mDamping = damping;
    mVelocity = velocity;
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
