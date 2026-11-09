#include "Game/Scene/GameScenePauseControl.hpp"
#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Scene/GameScene.hpp"
#include "Game/Screen/GamePauseSequence.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/System/PauseButtonCheckerInGame.hpp"

namespace {
    NEW_NERVE(GameScenePauseControlNormal, GameScenePauseControl, Normal);
};  // namespace

GameScenePauseControl::GameScenePauseControl(GameScene* pScene) : NerveExecutor("GameSceneポーズ制御") {
    mScene = pScene;
    mPauseChecker = nullptr;
    mPauseMenuOff = false;
    mPauseMenuNerve = nullptr;
    initNerve(GET_NERVE_ANON(GameScenePauseControlNormal));
    mPauseChecker = new PauseButtonCheckerInGame();
}

void GameScenePauseControl::registerNervePauseMenu(const Nerve* pNerve) {
    mPauseMenuNerve = pNerve;
}

void GameScenePauseControl::requestPauseMenuOff() {
    mPauseMenuOff = true;
}

void GameScenePauseControl::exeNormal() {
    tryStartPauseMenu();

    if (mPauseMenuOff) {
        AudWrap::getSystem()->exitPauseMenu();
        mScene->setNerveAfterPauseMenu();
        mPauseMenuOff = false;
        mScene->mPauseSeq->deactivate();
    }
}

bool GameScenePauseControl::tryStartPauseMenu() {
    if (mScene->isPermitToPauseMenu()) {
        mPauseChecker->update();

        // The window lost focus and the player chose to pause then: asked only while the pause menu may open, so a request in
        // a cutscene waits for the end of it (the host drops it when the window is in front again).
        const bool hostPause = NebulaHost::takePauseRequest() != 0;
        if (hostPause || mPauseChecker->isPermitToMinusPause() || mPauseChecker->isPermitToPlusPause()) {
            mScene->mPauseSeq->startPause(GamePauseSequence::ActivePause);
            mScene->setNerve(mPauseMenuNerve);
            return true;
        }
    }

    return false;
}
