#include "Game/Map/OceanHomeMapCtrl.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/Map/PlanetMap.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Map/WaterInfo.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"

// Not entirely sure if the createSceneObj would've been here or not. There's no evidence for or against it being here.
namespace {
    OceanHomeMapCtrl* getOceanHomeMapCtrl() {
        MR::createSceneObj(SceneObj_OceanHomeMapCtrl);

        return MR::getSceneObj< OceanHomeMapCtrl >(SceneObj_OceanHomeMapCtrl);
    }
};  // namespace

OceanHomeMapCtrl::OceanHomeMapCtrl() : NameObj("\203\111\201\133\203\126\203\203\203\223\203\172\201\133\203\200\222\156\214\140\220\247\214\344" /* オーシャンホーム地形制御 */) {
    mOceanHomePlanet = nullptr;
    mOceanRingPlanet = nullptr;
    _14 = 0;
    mOceanRingPlanetLowInWater = nullptr;
    _1C = 0;
    _20 = 0;
}

void OceanHomeMapCtrl::entryMapRing(PlanetMap* pPlanet) {
    mOceanRingPlanet = pPlanet;
    mOceanRingPlanetLowInWater = MR::createModelObjPlanetLow("\203\111\201\133\203\126\203\203\203\223\203\212\203\223\203\117\201\151\220\205\222\206\227\160Low\201\152" /* オーシャンリング（水中用Low） */, "OceanRingPlanetLowInWater", nullptr);

    MR::copyTransRotateScale(mOceanRingPlanet, mOceanRingPlanetLowInWater);
    MR::invalidateClipping(mOceanRingPlanetLowInWater);
    MR::tryStartAllAnim(mOceanRingPlanetLowInWater, "OceanRingPlanetLowInWater");
    mOceanRingPlanetLowInWater->kill();
}

void OceanHomeMapFunction::tryEntryOceanHomeMap(PlanetMap* pPlanet) {
    if (strcmp(pPlanet->mName, "\212\103\227\155\203\172\201\133\203\200\230\146\220\257" /* 海洋ホーム惑星 */) == 0) {
        ::getOceanHomeMapCtrl()->mOceanHomePlanet = pPlanet;
    } else if (strcmp(pPlanet->mName, "\203\111\201\133\203\126\203\203\203\223\203\212\203\223\203\117\230\146\220\257" /* オーシャンリング惑星 */) == 0) {
        ::getOceanHomeMapCtrl()->entryMapRing(pPlanet);
    }
}

void OceanHomeMapCtrl::init(const JMapInfoIter&) {
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
    } else if (!mOceanRingPlanet->mLODCtrl->isShowLowModel() && MR::isHiddenModel(mOceanRingPlanet)) {
        MR::showModel(mOceanRingPlanet);
        mOceanRingPlanet->mLODCtrl->validate();
    } else if (!MR::isDead(mOceanRingPlanetLowInWater)) {
        mOceanRingPlanetLowInWater->makeActorDead();
    }
}

OceanHomeMapCtrl::~OceanHomeMapCtrl() {
}
