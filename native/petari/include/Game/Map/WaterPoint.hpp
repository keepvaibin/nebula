#pragma once

#include <JSystem/JGeometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class WaterPoint {
public:
    WaterPoint(const TVec3f&, const TVec3f&, f32, f32, f32, f32);

    void initAfterPlacement();
    void updatePos(f32, f32, f32, f32, f32);
    f32 calcHeight(f32, f32, f32, f32, f32, f32) const;

    TVec3f mPosition;
    TVec3f mOrigPos;
    f32 mCoordAcrossRail;
    f32 mCoordOnRail;
    TVec3f mUpVec;
    f32 mHeight;
    f32 mFlowSpeedRate;
    u8 mAlpha;
};
