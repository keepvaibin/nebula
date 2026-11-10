#include "Game/Map/LightDirector.hpp"
#include "Game/Util/GameLightSignal.hpp"
#include "Game/LiveActor/ActorLightCtrl.hpp"
#include "Game/Map/LightDataHolder.hpp"
#include "Game/Map/LightFunction.hpp"
#include "Game/Map/LightPointCtrl.hpp"
#include "Game/Map/LightZoneDataHolder.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

LightDirector::LightDirector()
    : NameObj("ライト指揮"), mAreaHolder(), mDataHolder(), mZoneDataHolder(), mDefaultAreaLight(), mPlayerLightCtrl(), mPointCtrl(), mResourceHolder() {
    lumacore::setClass(this, lumacore::kClassId< LightDirector >);
    GameLightSignal::sceneStart();
}

void LightDirector::init(const TableIter& /*rIter*/) {
    MR::connectToSceneMapObjMovement(this);
    LightFunction::loadAllLightWhite();

    mDataHolder = new LightDataHolder();
    mZoneDataHolder = new LightZoneDataHolder();
    mPointCtrl = new LightPointCtrl();
}

void LightDirector::initData() {
    mResourceHolder = LightFunction::loadLightArchive();
    mDataHolder->initLightData();
    mZoneDataHolder->initZoneData();
    mDefaultAreaLight = mDataHolder->findAreaLight(mZoneDataHolder->getDefaultStageAreaLightName());
}

void LightDirector::loadLightPlayer() const {
    mPlayerLightCtrl->loadLight();
    mPointCtrl->loadPointLight();
}

void LightDirector::loadLightCoin() const {
    LightFunction::loadLightInfoCoin(&mDataHolder->mCoinLight);
}

void LightDirector::movement() {
    mPointCtrl->update();
}
