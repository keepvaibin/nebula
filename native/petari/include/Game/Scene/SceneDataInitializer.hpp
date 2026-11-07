#include <stdint.h>
#pragma once

#include "Game/NameObj/NameObj.hpp"

class StageDataHolder;
class StageFileLoader;

class SceneDataInitializer : public NameObj {
public:
    SceneDataInitializer();

    virtual ~SceneDataInitializer();

    void startStageFileLoad();
    void startStageFileLoadAfterScenarioSelected();
    void createStageDataHolder();
    void startActorFileLoadCommon();
    void startActorFileLoadScenario();
    void startActorPlacement();
    bool startActorPlacementStep(uint64_t deadline);
    void initAfterScenarioSelected();

    StageFileLoader* mFileLoader;
    StageDataHolder* mDataHolder;
};
