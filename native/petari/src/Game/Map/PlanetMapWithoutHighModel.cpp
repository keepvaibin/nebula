#include "Game/Map/PlanetMapWithoutHighModel.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util/ObjUtil.hpp"
#include <cstdio>

PlanetMapWithoutHighModel::PlanetMapWithoutHighModel(const char* pName) : MapObjActor(pName) {
}

void PlanetMapWithoutHighModel::connectToScene(const MapObjActorInitInfo&) {
    MR::connectToScenePlanet(this);
}

PlanetMapWithoutHighModel::~PlanetMapWithoutHighModel() {
}

void PlanetMapWithoutHighModel::init(const JMapInfoIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    info.setupHioNode("\201\237\201\237\203\156\203\103\203\202\203\146\203\213\226\263\202\265\202\314\230\146\220\257\201\237\201\237" /* ◆◆ハイモデル無しの惑星◆◆ */);
    info.setupDefaultPos();
    info.setupConnectToScene();
    info.setupEffect(nullptr);
    info.setupFarClipping(-1.0f);
    char modelName[256];
    snprintf(modelName, sizeof(modelName), "%sLow", mObjectName);
    info.setupModelName(modelName);
    initialize(rIter, info);
}
