#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class OceanSpherePoint {
public:
    OceanSpherePoint(const Vec3f* pCenter, const Vec3f& rNormal, f32 wave1Pos, f32 wave2Pos, const Vec2f& rTexCoord);

    void updatePos(f32 radius, f32 wave1Time, f32 wave2Time);
    f32 calcHeight(f32 wave1Time, f32 wave2Time, f32 wave1Pos, f32 wave2Pos) const;

    Vec3f mPos;
    const Vec3f* mCenter;
    Vec3f mNormal;
    f32 mWave1Pos;
    f32 mWave2Pos;
    Vec2f mTexCoord;
};
