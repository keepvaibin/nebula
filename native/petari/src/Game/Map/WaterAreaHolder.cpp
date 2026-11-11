#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Map/OceanBowl.hpp"
#include "Game/Map/OceanRing.hpp"
#include "Game/Map/OceanSphere.hpp"
#include "Game/Map/WaterInfo.hpp"
#include "Game/Map/WhirlPoolAccelerator.hpp"
#include "Game/MapObj/WhirlPool.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/WaterCameraFilter.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {

    WaterAreaHolder* getWaterAreaHolder() {
        return MR::getSceneObj< WaterAreaHolder >(SceneObj_WaterAreaHolder);
    }
};  // namespace

WaterAreaHolder::WaterAreaHolder()
    : NameObj("水エリア保持"), mNumOceanBowls(0), mOceanBowls(), mNumOceanRings(0), mOceanRings(), mNumOceanSpheres(0),
      mOceanSpheres(), mNumWhirlPools(0), mWhirlPools(), mNumWhirlPoolAccelerators(0), mWhirlPoolAccelerators(),
      mCamInWater(false), mCameraFilter(nullptr), mUseBloom(false) {
    lumacore::setClass(this, lumacore::kClassId< WaterAreaHolder >);
    mWaterInfo.clear();

    if (MR::isEqualStageName("HeavenlyBeachGalaxy") || MR::isEqualStageName("OceanRingGalaxy")) {
        MR::createNormalBloom();
        mUseBloom = true;
    }

    MR::connectToSceneScreenEffectMovement(this);
    mCameraFilter = new WaterCameraFilter();
    mCameraFilter->initWithoutIter();
}

void WaterAreaHolder::entryOceanBowl(OceanBowl* pOceanBowl) {
    mOceanBowls.at(mNumOceanBowls) = pOceanBowl;
    mNumOceanBowls++;
}

void WaterAreaHolder::entryOceanRing(OceanRing* pOceanRing) {
    mOceanRings.at(mNumOceanRings) = pOceanRing;
    mNumOceanRings++;
}

void WaterAreaHolder::entryOceanSphere(OceanSphere* pOceanSphere) {
    mOceanSpheres.at(mNumOceanSpheres) = pOceanSphere;
    mNumOceanSpheres++;
}

void WaterAreaHolder::entryWhirlPool(WhirlPool* pWhirlPool) {
    mWhirlPools.at(mNumWhirlPools) = pWhirlPool;
    mNumWhirlPools++;
}

void WaterAreaHolder::entryWhirlPoolAccelerator(WhirlPoolAccelerator* pWhirlPoolAccelerator) {
    mWhirlPoolAccelerators.at(mNumWhirlPoolAccelerators) = pWhirlPoolAccelerator;
    mNumWhirlPoolAccelerators++;
}

void WaterAreaHolder::movement() {
    if (MR::getWaterAreaObj(&mWaterInfo, MR::getCamPos())) {
        if (!mCamInWater) {
            if (mUseBloom) {
                MR::turnOnNormalBloom();
                MR::setNormalBloomIntensity(255);
                MR::setNormalBloomThreshold(128);
                MR::setNormalBloomBlurIntensity1(40);
                MR::setNormalBloomBlurIntensity2(20);
            }
            mCamInWater = true;
        }
        MR::getWaterAreaInfo(&mWaterInfo, MR::getCamPos(), Vec3f(0.0f, -1.0f, 0.0f), false);
    } else {
        if (mCamInWater) {
            if (mUseBloom) {
                MR::setImageEffectControlAuto();
            }
            mCamInWater = false;
        }
    }
}

void WaterAreaFunction::entryOceanBowl(OceanBowl* pOceanBowl) {
    createWaterAreaHolder();
    ::getWaterAreaHolder()->entryOceanBowl(pOceanBowl);
}

void WaterAreaFunction::entryOceanRing(OceanRing* pOceanRing) {
    createWaterAreaHolder();
    ::getWaterAreaHolder()->entryOceanRing(pOceanRing);
}

void WaterAreaFunction::entryOceanSphere(OceanSphere* pOceanSphere) {
    createWaterAreaHolder();
    ::getWaterAreaHolder()->entryOceanSphere(pOceanSphere);
}

void WaterAreaFunction::entryWhirlPool(WhirlPool* pWhirlPool) {
    createWaterAreaHolder();
    ::getWaterAreaHolder()->entryWhirlPool(pWhirlPool);
}

void WaterAreaFunction::entryWhirlPoolAccelerator(WhirlPoolAccelerator* pWhirlPoolAccelerator) {
    createWaterAreaHolder();
    ::getWaterAreaHolder()->entryWhirlPoolAccelerator(pWhirlPoolAccelerator);
}

void WaterAreaFunction::createWaterAreaHolder() {
    MR::createSceneObj(SceneObj_WaterAreaHolder);
}

bool WaterAreaFunction::tryInOceanArea(const Vec3f& rPos, WaterInfo* pWaterInfo) {
    if (!MR::isExistSceneObj(SceneObj_WaterAreaHolder)) {
        return false;
    }

    WaterAreaHolder* const pHolder = ::getWaterAreaHolder();
    for (s32 idx = 0; idx < pHolder->mNumOceanBowls; idx++) {
        OceanBowl* const pBowl = pHolder->mOceanBowls[idx];
        if (pBowl->isInWater(rPos)) {
            pWaterInfo->mOceanBowl = pBowl;
            return true;
        }
    }

    for (s32 idx = 0; idx < pHolder->mNumOceanRings; idx++) {
        OceanRing* const pRing = pHolder->mOceanRings[idx];
        if (pRing->isInWater(rPos)) {
            pWaterInfo->mOceanRing = pRing;
            return true;
        }
    }

    for (s32 idx = 0; idx < pHolder->mNumOceanSpheres; idx++) {
        OceanSphere* const pSphere = pHolder->mOceanSpheres[idx];
        if (pSphere->isInWater(rPos)) {
            pWaterInfo->mOceanSphere = pSphere;
            return true;
        }
    }

    return false;
}

bool WaterAreaFunction::tryInWhirlPoolAccelerator(const Vec3f& rPos, Vec3f* pVel) {
    if (!MR::isExistSceneObj(SceneObj_WaterAreaHolder)) {
        return false;
    }

    WaterAreaHolder* const pHolder = ::getWaterAreaHolder();
    for (s32 idx = 0; idx < pHolder->mNumWhirlPoolAccelerators; idx++) {
        if (pHolder->mWhirlPoolAccelerators[idx]->calcInfo(rPos, pVel)) {
            return true;
        }
    }

    return false;
}

bool WaterAreaFunction::isCameraInWaterForCameraUtil() {
    return MR::isExistSceneObj(SceneObj_WaterAreaHolder) && ::getWaterAreaHolder()->mCamInWater;
}

f32 WaterAreaFunction::getCameraWaterDepth() {
    if (MR::isExistSceneObj(SceneObj_WaterAreaHolder)) {
        return ::getWaterAreaHolder()->mWaterInfo.mCamWaterDepth;
    }

    return 0.0f;
}

WaterInfo* WaterAreaFunction::getCameraWaterInfo() {
    if (MR::isExistSceneObj(SceneObj_WaterAreaHolder)) {
        return &::getWaterAreaHolder()->mWaterInfo;
    }

    return nullptr;
}

void WaterAreaFunction::pauseOffWaterArea() {
    if (MR::isExistSceneObj(SceneObj_WaterAreaHolder)) {
        WaterAreaHolder* const pHolder = ::getWaterAreaHolder();
        MR::requestMovementOn(pHolder);
        MR::requestMovementOn(pHolder->mCameraFilter);
    }
}
