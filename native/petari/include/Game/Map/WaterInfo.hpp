#pragma once

#include <JSystem/JGeometry.hpp>

class WaterArea;
class OceanBowl;
class OceanRing;
class OceanSphere;

class WaterInfo {
public:
    WaterInfo();

    bool isInWater() const;
    void clear();

    const TVec3f& getSurfacePos() const {
        return mSurfacePos;
    }

    f32 mCamWaterDepth;
    f32 _4;
    TVec3f mSurfacePos;
    TVec3f mSurfaceNormal;
    f32 mWaveHeight;
    TVec3f mStreamVec;
    f32 mEdgeDistance;
    TVec3f mEdgePos;
    const WaterArea* mWaterArea;
    const OceanBowl* mOceanBowl;
    const OceanRing* mOceanRing;
    const OceanSphere* mOceanSphere;
};
