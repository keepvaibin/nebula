#pragma once

#include <nebula_geometry.hpp>

class WaterArea;
class OceanBowl;
class OceanRing;
class OceanSphere;

class WaterInfo {
public:
    WaterInfo();

    bool isInWater() const;
    void clear();

    const Vec3f& getSurfacePos() const {
        return mSurfacePos;
    }

    f32 mCamWaterDepth;
    f32 mDepth;
    Vec3f mSurfacePos;
    Vec3f mSurfaceNormal;
    f32 mWaveHeight;
    Vec3f mStreamVec;
    f32 mEdgeDistance;
    Vec3f mEdgePos;
    const WaterArea* mWaterArea;
    const OceanBowl* mOceanBowl;
    const OceanRing* mOceanRing;
    const OceanSphere* mOceanSphere;
};
