#pragma once

#include <JSystem/JGeometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class OceanSpherePoint {
public:
    OceanSpherePoint(const TVec3f* pCenter, const TVec3f& rNormal, f32 wave1Pos, f32 wave2Pos, const TVec2f& rTexCoord);

    void updatePos(f32 radius, f32 wave1Time, f32 wave2Time);
    f32 calcHeight(f32 wave1Time, f32 wave2Time, f32 wave1Pos, f32 wave2Pos) const;

    TVec3f mPos;
    const TVec3f* mCenter;
    TVec3f mNormal;
    f32 mWave1Pos;
    f32 mWave2Pos;
    TVec2f mTexCoord;
};
