#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class RumbleCalculator {
public:
    RumbleCalculator(f32, f32, f32, u32);

    virtual void calcValues(Vec3f*, const Vec3f&) = 0;

    void start(u32);
    void calc();
    void reset();

    bool isRumbling() const {
        return mFrame < mDuration;
    }

    u32 mFrame;
    u32 mDuration;
    Vec3f mValue;
    f32 mFrequency;
    f32 mPhaseOffset;
    f32 mAmplitude;
};

class RumbleCalculatorCosMultLinear : public RumbleCalculator {
public:
    RumbleCalculatorCosMultLinear(f32, f32, f32, u32);

    virtual void calcValues(Vec3f*, const Vec3f&) override;
};