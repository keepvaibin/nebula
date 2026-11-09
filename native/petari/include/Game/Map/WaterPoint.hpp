#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class WaterPoint {
public:
    WaterPoint(const Vec3f&, const Vec3f&, f32, f32, f32);

    void initAfterPlacement();
    void updatePos(f32 waveTheta1, f32 waveTheta2, f32 waveHeight1, f32 waveHeight2, f32 heightRate);
    f32 calcHeight(f32, f32, f32, f32, f32, f32) const;

    Vec3f mPosition;
    Vec3f mOrigPos;
    f32 mCoordAcrossRail;
    f32 mCoordOnRail;
    Vec3f mUpVec;
    f32 mHeight;
    u8 mAlpha;
};
