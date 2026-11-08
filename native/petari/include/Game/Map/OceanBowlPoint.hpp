#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class OceanBowlPoint {
public:
    OceanBowlPoint(const Vec3f& rPos);

    void updatePos(f32 phaseX, f32 phaseZ);
    static f32 calcHeightStatic(f32 phaseX, f32 phaseZ, f32 x, f32 z);
    // waveScale 0 to 1: the wave's share and the vertex alpha (255 at 1).
    void reset(const Vec3f& rPos, f32 waveScale);

    Vec3f mVertexPosition;  // drawn position, wave applied
    Vec3f mPosition;        // rest position
    f32 mWaveScale;
    u8 mAlpha;
};
