#pragma once

#include "Game/System/NerveExecutor.hpp"
#include <nebula_geometry.hpp>

class ScenarioTitle;

class GameSceneScenarioOpeningCameraState : public NerveExecutor {
public:
    GameSceneScenarioOpeningCameraState();

    void update();
    bool isDone() const;
    void start();
    void end();
    void exeWait();
    void exePlay();
    bool trySkipTrigger() const;

    Pos3f mBaseMtx;
    ScenarioTitle* mScenarioTitle;
};
