#include "Game/Scene/SceneDataInitializer.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Scene/StageDataHolder.hpp"
#include "Game/System/LumaCore.hpp"

SceneDataInitializer::SceneDataInitializer() : NameObj("SceneDataInitializer") {
    lumacore::setClass(this, lumacore::kClassId< SceneDataInitializer >);
    mDataHolder = nullptr;
}

void SceneDataInitializer::createStageDataHolder() {
    mDataHolder = static_cast< StageDataHolder* >(MR::createSceneObj(SceneObj_StageDataHolder));
}

void SceneDataInitializer::startActorPlacement() {
    mDataHolder->initPlacement();
}

bool SceneDataInitializer::startActorPlacementStep(LoadSlice& rSlice) {
    return mDataHolder->initPlacementStep(rSlice);
}

void SceneDataInitializer::initAfterScenarioSelected() {
    mDataHolder->initAfterScenarioSelected();
}
