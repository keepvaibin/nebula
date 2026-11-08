#pragma once

#include "Game/System/NerveExecutor.hpp"

class GameScene;
class PauseButtonCheckerInGame;

class GameScenePauseControl : public NerveExecutor {
public:
    GameScenePauseControl(GameScene*);

    void registerNervePauseMenu(const Nerve*);
    void requestPauseMenuOff();
    void exeNormal();
    bool tryStartPauseMenu();

    GameScene* mScene;
    PauseButtonCheckerInGame* mPauseChecker;
    bool mPauseMenuOff;
    const Nerve* mPauseMenuNerve;
};
