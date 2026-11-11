#include "Game/Map/OceanHomeMapCtrl.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/Map/PlanetMap.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Map/WaterInfo.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

// Not entirely sure if the createSceneObj would've been here or not. There's no evidence for or against it being here.
namespace {
    OceanHomeMapCtrl* getOceanHomeMapCtrl() {
        MR::createSceneObj(SceneObj_OceanHomeMapCtrl);

        return MR::getSceneObj< OceanHomeMapCtrl >(SceneObj_OceanHomeMapCtrl);
    }
};  // namespace

OceanHomeMapCtrl::OceanHomeMapCtrl() : NameObj("オーシャンホーム地形制御") {
    lumacore::setClass(this, lumacore::kClassId< OceanHomeMapCtrl >);
    mOceanHomePlanet = nullptr;
    mOceanRingPlanet = nullptr;
    mOceanRingPlanetLowInWater = nullptr;
}

void OceanHomeMapCtrl::entryMapRing(PlanetMap* pPlanet) {
    mOceanRingPlanet = pPlanet;
    mOceanRingPlanetLowInWater = MR::createModelObjPlanetLow("オーシャンリング（水中用Low）", "OceanRingPlanetLowInWater", nullptr);

    MR::copyTransRotateScale(mOceanRingPlanet, mOceanRingPlanetLowInWater);
    MR::invalidateClipping(mOceanRingPlanetLowInWater);
    MR::tryStartAllAnim(mOceanRingPlanetLowInWater, "OceanRingPlanetLowInWater");
    mOceanRingPlanetLowInWater->kill();
}

void OceanHomeMapFunction::tryEntryOceanHomeMap(PlanetMap* pPlanet) {
    if (MR::isEqualString(pPlanet->mName, "海洋ホーム惑星")) {
        ::getOceanHomeMapCtrl()->mOceanHomePlanet = pPlanet;
    } else if (MR::isEqualString(pPlanet->mName, "オーシャンリング惑星")) {
        ::getOceanHomeMapCtrl()->entryMapRing(pPlanet);
    }
}

void OceanHomeMapCtrl::init(const TableIter&) {
    MR::connectToSceneMapObjMovement(this);
}

void OceanHomeMapCtrl::movement() {
    if (MR::isClipped(mOceanRingPlanet)) {
        return;
    }

    if (WaterAreaFunction::getCameraWaterInfo()->mOceanBowl) {
        if (MR::isDead(mOceanRingPlanetLowInWater)) {
            mOceanRingPlanetLowInWater->makeActorAppeared();
        } else if (!MR::isHiddenModel(mOceanRingPlanet)) {
            MR::hideModel(mOceanRingPlanet);
            mOceanRingPlanet->mLODCtrl->invalidate();
        }
    } else if (MR::isHiddenModel(mOceanRingPlanet)) {
        MR::showModel(mOceanRingPlanet);
        mOceanRingPlanet->mLODCtrl->validate();
    } else if (!MR::isDead(mOceanRingPlanetLowInWater)) {
        mOceanRingPlanetLowInWater->makeActorDead();
    }
}
