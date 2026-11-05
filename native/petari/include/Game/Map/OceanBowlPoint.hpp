#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class OceanBowlPoint {
public:
    OceanBowlPoint(const Vec3f&);

    void updatePos(f32, f32);
    static f32 calcHeightStatic(f32, f32, f32, f32);
    void reset(const Vec3f&, f32);

    Vec3f mVertexPosition;  // The position of the rendering vertex
    Vec3f mPosition;        // The actual position of the point without the wave height applied
    f32 mWaveScale;          // defaults to 1.0f
    u8 mAlpha;               // defaults to 0xFF
};
