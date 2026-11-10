#include "Game/Map/PlanetMapWithoutHighModel.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

PlanetMapWithoutHighModel::PlanetMapWithoutHighModel(const char* pName) : MapObjActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< PlanetMapWithoutHighModel >);
}

void PlanetMapWithoutHighModel::connectToScene(const MapObjActorInitInfo&) {
    MR::connectToScenePlanet(this);
}

void PlanetMapWithoutHighModel::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    info.setupDefaultPos();
    info.setupConnectToScene();
    info.setupEffect(nullptr);
    info.setupFarClipping(-1.0f);
    MR::FormattedName modelName("%sLow", mObjectName);
    info.setupModelName(modelName.c_str());
    initialize(rIter, info);
}
