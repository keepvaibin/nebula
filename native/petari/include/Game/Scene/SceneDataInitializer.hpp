#include <stdint.h>
#pragma once

#include "Game/NameObj/NameObj.hpp"

class StageDataHolder;

class SceneDataInitializer : public NameObj {
public:
    SceneDataInitializer();

    virtual ~SceneDataInitializer();

    void createStageDataHolder();
    void startActorPlacement();
    bool startActorPlacementStep(uint64_t deadline);
    void initAfterScenarioSelected();

    StageDataHolder* mDataHolder;
};
