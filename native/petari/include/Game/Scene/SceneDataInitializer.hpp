#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <stdint.h>

class StageDataHolder;

class SceneDataInitializer : public NameObj {
public:
    SceneDataInitializer();

    void createStageDataHolder();
    void startActorPlacement();
    bool startActorPlacementStep(uint64_t deadline);
    void initAfterScenarioSelected();

    StageDataHolder* mDataHolder;
};
