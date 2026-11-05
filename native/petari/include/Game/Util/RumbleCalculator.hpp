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
        return _4 < _8;
    }

    u32 _4;
    u32 _8;
    Vec3f _C;
    f32 _18;
    f32 _1C;
    f32 _20;
};

class RumbleCalculatorCosMultLinear : public RumbleCalculator {
public:
    RumbleCalculatorCosMultLinear(f32, f32, f32, u32);

    virtual void calcValues(Vec3f*, const Vec3f&);
};