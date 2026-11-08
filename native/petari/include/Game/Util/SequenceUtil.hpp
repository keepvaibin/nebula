#pragma once


class PlacementId;

namespace MR {
    void requestChangeScene(const char*);
    void requestChangeSceneTitle();
    void requestChangeStageInGameAfterLoadingGameData();
    void requestChangeStageAfterStageClear();
    void requestChangeStageAfterMiss();
    void requestChangeStageInGameMoving(const char*, s32, const PlacementId&);
    void requestChangeStageInGameMoving(const char*, s32);
    void requestChangeSceneAfterGameOver();
    void requestChangeSceneAfterBoot();
    void requestChangeStageGoBackAstroDome();
    void requestStartScenarioSelect(const char*);
    void requestStartScenarioSelectForComet(const char*, s32);
    bool hasRetryGalaxySequence();
    bool isExecScenarioStarter();
    void requestPowerStarGetDemo();
    void requestGrandStarGetDemo();
    void requestStartGameOverDemo();
    void requestEndGameOverDemo();
    void requestEndMissDemo();
    void requestShowGalaxyMap();
    void executeOnWelcomeAndRetry();
    void requestGoToAstroGalaxy(s32 startId);
    void requestGoToAstroDomeFromAstroGalaxy(s32 scenarioNo, s32 startId);
};  // namespace MR
