#include <nebula_game_api.h>
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Scene/GameSceneFunction.hpp"
#include "Game/System/GalaxyMoveArgument.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlacementId.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/SystemUtil.hpp"

namespace {
    // Star bits given after a miss (0).
    const s32 cMissStarPieceNum = 0;
};  // namespace

namespace MR {
    void requestChangeScene(SceneKind kind) {
        GameSequenceFunction::requestChangeScene(kind);
    }

    void requestChangeSceneTitle() {
        GameSequenceFunction::requestGalaxyMove(GalaxyMoveArgument(GalaxyMoveArgument::Type_Title, nullptr, 1, nullptr));
    }

    void requestChangeStageInGameAfterLoadingGameData() {
        GameSequenceFunction::requestGalaxyMove(GalaxyMoveArgument(GalaxyMoveArgument::Type_AfterLoadingGameData, nullptr, 1, nullptr));
    }

    void requestChangeStageAfterStageClear() {
        GameSequenceFunction::requestGalaxyMove(GalaxyMoveArgument(GalaxyMoveArgument::Type_AfterStageClear, nullptr, 1, nullptr));
    }

    void requestChangeStageAfterMiss() {
        PlacementId info = *getPlayerRestartIdInfo();

        if (isGalaxyAnyCometAppearInCurrentStage()) {
            const PlacementId& rInitialInfo = getInitializeStartIdInfo();
            info.mId = rInitialInfo.mId;
            info.mZoneID = rInitialInfo.mZoneID;
        }

        GameSystemSceneController* pSceneController = SingletonHolder< GameSystem >::get()->mSceneController;
        GalaxyMoveArgument argument(GalaxyMoveArgument::Type_AfterMiss, pSceneController->mCurrSceneControlInfo.mStage, pSceneController->getCurrentScenarioNo(), &info);
        argument.mSelectedScenarioNo = pSceneController->getCurrentSelectedScenarioNo();

        GameSequenceFunction::requestGalaxyMove(argument);
        if (::cMissStarPieceNum != 0) {
            GameDataFunction::addStarPiece(::cMissStarPieceNum);
        }
    }

    void requestChangeStageInGameMoving(const char* pStageName, s32 scenarioNo, const PlacementId& rInfo) {
        GameSequenceFunction::requestGalaxyMove(GalaxyMoveArgument(GalaxyMoveArgument::Type_InGame, pStageName, scenarioNo, &rInfo));
    }

    void requestChangeStageInGameMoving(const char* pStageName, s32 scenarioNo) {
        requestChangeStageInGameMoving(pStageName, scenarioNo, getInitializeStartIdInfo());
    }

    void requestChangeSceneAfterGameOver() {
        resetSystemAndGameStatus();
        requestChangeSceneTitle();
    }

    void requestChangeSceneAfterBoot() {
        requestChangeSceneTitle();
    }

    void requestChangeStageGoBackAstroDome() {
        GameSequenceFunction::requestGalaxyMove(GalaxyMoveArgument(GalaxyMoveArgument::Type_GoBackAstroDome, nullptr, 1, nullptr));
    }

    void requestStartScenarioSelect(const char* pStageName) {
        GameSequenceFunction::requestGalaxyMove(GalaxyMoveArgument(GalaxyMoveArgument::Type_ScenarioSelect, pStageName, -1, nullptr));
    }

    void requestStartScenarioSelectForComet(const char* pStageName, s32 scenarioNo) {
        GameSequenceFunction::requestGalaxyMove(GalaxyMoveArgument(GalaxyMoveArgument::Type_ScenarioSelect, pStageName, scenarioNo, nullptr));
    }

    bool hasRetryGalaxySequence() {
        return GameSequenceFunction::hasRetryGalaxySequence();
    }

    bool isExecScenarioStarter() {
        return GameSceneFunction::isExecScenarioStarter();
    }

    void requestPowerStarGetDemo() {
        GameSceneFunction::requestPowerStarGetDemo();
    }

    void requestGrandStarGetDemo() {
        GameSceneFunction::requestGrandStarGetDemo();
    }

    void requestStartGameOverDemo() {
        GameSceneFunction::requestStartGameOverDemo();
    }

    void requestEndGameOverDemo() {
        GameSceneFunction::requestEndGameOverDemo();
    }

    void requestEndMissDemo() {
        GameSceneFunction::requestEndMissDemo();
    }

    void requestShowGalaxyMap() {
        GameSceneFunction::requestShowGalaxyMap();
    }

    void executeOnWelcomeAndRetry() {
        startGalaxyCometEvent();
        requestForceAppearHPMeter();
    }

    void requestGoToAstroGalaxy(s32 startId) {
        PlacementId info(startId, 0);

        GameSequenceFunction::requestGalaxyMove(GalaxyMoveArgument(GalaxyMoveArgument::Type_InGame, "AstroGalaxy", -1, &info));
    }

    void requestGoToAstroDomeFromAstroGalaxy(s32 scenarioNo, s32 startId) {
        PlacementId info(startId, 0);

        GameSequenceFunction::requestGalaxyMove(GalaxyMoveArgument(GalaxyMoveArgument::Type_InGame, "AstroDome", scenarioNo, &info));
    }
};  // namespace MR
