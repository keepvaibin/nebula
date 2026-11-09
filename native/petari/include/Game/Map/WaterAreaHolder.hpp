#pragma once

#include "Game/Util/Array.hpp"
#include "Game/Map/WaterInfo.hpp"
#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class OceanBowl;
class OceanRing;
class OceanSphere;
class WaterCameraFilter;
class WhirlPool;
class WhirlPoolAccelerator;

class WaterAreaHolder : public NameObj {
public:
    WaterAreaHolder();

    virtual void movement() override;

    void entryOceanBowl(OceanBowl*);
    void entryOceanRing(OceanRing*);
    void entryOceanSphere(OceanSphere*);
    void entryWhirlPool(WhirlPool*);
    void entryWhirlPoolAccelerator(WhirlPoolAccelerator*);

    s32 mNumOceanBowls;
    MR::GrowTable< OceanBowl*, 8 > mOceanBowls;
    s32 mNumOceanRings;
    MR::GrowTable< OceanRing*, 8 > mOceanRings;
    s32 mNumOceanSpheres;
    MR::GrowTable< OceanSphere*, 8 > mOceanSpheres;
    s32 mNumWhirlPools;
    MR::GrowTable< WhirlPool*, 8 > mWhirlPools;
    s32 mNumWhirlPoolAccelerators;
    MR::GrowTable< WhirlPoolAccelerator*, 8 > mWhirlPoolAccelerators;
    bool mCamInWater;
    WaterInfo mWaterInfo;
    WaterCameraFilter* mCameraFilter;
    bool mUseBloom;
};

namespace WaterAreaFunction {
    void entryOceanBowl(OceanBowl*);
    void entryOceanRing(OceanRing*);
    void entryOceanSphere(OceanSphere*);
    void entryWhirlPool(WhirlPool*);
    void entryWhirlPoolAccelerator(WhirlPoolAccelerator*);
    void createWaterAreaHolder();
    bool tryInOceanArea(const Vec3f&, WaterInfo*);
    bool tryInWhirlPoolAccelerator(const Vec3f&, Vec3f*);
    bool isCameraInWaterForCameraUtil();
    f32 getCameraWaterDepth();
    WaterInfo* getCameraWaterInfo();
    void pauseOffWaterArea();
};  // namespace WaterAreaFunction
