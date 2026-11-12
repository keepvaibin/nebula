#include "Game/System/NebulaLoadTrace.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/NameObj/NameObjHolder.hpp"
#include "Game/Scene/GameScene.hpp"
#include "Game/Scene/IntermissionScene.hpp"
#include "Game/Scene/ScenarioSelectScene.hpp"
#include "Game/Scene/SceneFactory.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/System/AudSystemWrapper.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemFunction.hpp"
#include "Game/System/GameSystemObjHolder.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/System/ScenarioDataParser.hpp"
#include "Game/System/PlayerInputs.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/PlacementId.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/System/LumaClasses.hpp"


namespace {
    AudSystemWrapper* getAudioSystemWrapper() {
        return SingletonHolder< GameSystem >::get()->mObjHolder->mAudioSystem;
    }
};  // namespace

namespace NrvGameSystemSceneController {
    NEW_NERVE(GameSystemSceneControllerNotInitialized, GameSystemSceneController, NotInitialized);
    NEW_NERVE(GameSystemSceneControllerNormal, GameSystemSceneController, Normal);
    NEW_NERVE(GameSystemSceneControllerChoosePlayerVoices, GameSystemSceneController, ChoosePlayerVoices);
    NEW_NERVE(GameSystemSceneControllerInitializeScene, GameSystemSceneController, InitializeScene);
    NEW_NERVE(GameSystemSceneControllerInvalidateSystemWipe, GameSystemSceneController, InvalidateSystemWipe);
    NEW_NERVE(GameSystemSceneControllerWaitDrawDoneScene, GameSystemSceneController, WaitDrawDoneScene);
    NEW_NERVE(GameSystemSceneControllerDestroyScene, GameSystemSceneController, DestroyScene);
    NEW_NERVE(GameSystemSceneControllerReadyToStartScene, GameSystemSceneController, ReadyToStartScene);
    NEW_NERVE(GameSystemSceneControllerWaitDrawDoneSceneForDestroy, GameSystemSceneController, WaitDrawDoneScene);
    NEW_NERVE(GameSystemSceneControllerDestroySceneForDestroy, GameSystemSceneController, DestroyScene);
    NEW_NERVE(GameSystemSceneControllerDestroyed, GameSystemSceneController, Destroyed);
};  // namespace NrvGameSystemSceneController

SceneControlInfo::SceneControlInfo() : mScene(SceneKind_None), mStage(""), mStartIdInfo() {
    mScenarioNo = 1;
    mSelectedScenarioNo = 1;
    mStartIdInfo = new PlacementId(MR::getInitializeStartIdInfo());
}

void SceneControlInfo::setStage(const char* pStageName) {
    mStage = MR::internName(pStageName);
}

void SceneControlInfo::setStartIdInfo(const PlacementId& rInfo) {
    *mStartIdInfo = rInfo;
}

GameSystemSceneController::GameSystemSceneController()
    : mSpine(), mNextNerve(), mIsResetProcessing(), mScenarioParser(), mObjHolder(), mScene(), mSceneInitializeState(), mIntermissionScene(),
      mScenarioSelectScene() {
    mObjHolder = new NameObjHolder(0x1300);
    mSpine = new Spine(this, GET_NERVE(GameSystemSceneController, GameSystemSceneControllerNotInitialized));
    mIntermissionScene = static_cast< IntermissionScene* >(MR::createScene(SceneKind_Intermission));
    mScenarioSelectScene = new ScenarioSelectScene();
}

void GameSystemSceneController::initAfterSystemArchiveLoaded() {
    mScenarioParser = new ScenarioDataParser("シナリオデータ解析");
    mScenarioParser->initWithoutIter();

    mScenarioSelectScene->init();
}

void GameSystemSceneController::requestChangeScene() {
    if (isExistRequest()) {
        return;
    }

    if (mIsResetProcessing) {
        return;
    }

    mSceneInitializeState = SceneInitializeState_NotInit;

    requestChangeNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerWaitDrawDoneScene));
}

void GameSystemSceneController::checkRequestAndChangeScene() {
    if (mNextNerve != nullptr) {
        mSpine->setNerve(mNextNerve);
        mNextNerve = nullptr;
    }

    mSpine->update();
    // A load's steps follow each other within the frame: the screen is black and nothing waits for them.
    while (mNextNerve == GET_NERVE(GameSystemSceneController, GameSystemSceneControllerChoosePlayerVoices) ||
           mNextNerve == GET_NERVE(GameSystemSceneController, GameSystemSceneControllerInitializeScene) ||
           mNextNerve == GET_NERVE(GameSystemSceneController, GameSystemSceneControllerInvalidateSystemWipe) ||
           mNextNerve == GET_NERVE(GameSystemSceneController, GameSystemSceneControllerReadyToStartScene)) {
        mSpine->setNerve(mNextNerve);
        mNextNerve = nullptr;
        mSpine->update();
    }
}

void GameSystemSceneController::initializeScene() {
    NebulaLoadTrace::Scope loadingTrace("initializeScene");
    NebulaArena::CurrentScope restorer(NebulaArena::current());
    mSceneInitializeState = SceneInitializeState_Init;
    mSceneInitializeHeld = false;

    if (Arenas::sceneFiles() == nullptr || !isSameAtNextSceneAndStage()) {
        Arenas::createSceneFiles();
    }

    Arenas::createScene();
    Arenas::scene()->makeCurrent();
    updateSceneControlInfo();
    MR::setRandomSeedFromStageName();

    if (mScenarioSelectScene->isActive()) {
        mScenarioSelectScene->validateScenarioSelect();
    }

    MR::resetPlayerInput();

    mScene = MR::createScene(mCurrSceneControlInfo.mScene);
    mScene->initNameObjListExecutor();
    mScene->initSceneObjHolder();
    mScene->init();

    if (!mSceneInitializeHeld) {
        SceneFunction::allocateDrawBufferActorList();
        mSceneInitializeDone = true;
    }
}

// The scene's load goes no further until a scenario is chosen (GameScene::init).
void GameSystemSceneController::holdSceneInitialize() {
    mSceneInitializeHeld = true;
}

bool GameSystemSceneController::resumeSceneInitialize() {
    if (!mSceneInitializeHeld) {
        return false;
    }
    NebulaArena::CurrentScope restorer(Arenas::scene());
    bool complete = false;
    NebulaLoadTrace::measure("initAfterScenarioChosen", [&] { complete = mScene->initAfterScenarioChosen(); });
    if (!complete) return false;
    mSceneInitializeHeld = false;
    SceneFunction::allocateDrawBufferActorList();
    mSceneInitializeDone = true;
    return true;
}

void GameSystemSceneController::destroyScene() {
    NebulaArena::CurrentScope restorer(NebulaArena::current());
    bool stopSound = isStopSound();

    delete mScene;
    mScene = nullptr;

    mObjHolder->clearArray();

    if (stopSound) {
        MR::stopAllSound(0);
    }

    GameSystemFunction::resetAllControllerRumble();
    MR::removeResourceHolders(Arenas::scene());

    if (releaseSceneFiles(false)) {
        Arenas::resetGame();
    } else {
        Arenas::destroyScene();
    }
    // Mods' objects went with the scene (contract classes): forgotten, their data freed. Nothing when there are none.
    luma::classes::sceneGone();
}

bool GameSystemSceneController::isStopSound() const {
    const bool isNextGame = mNextSceneControlInfo.mScene == SceneKind_Game;
    if (MR::isEqualStageName("AstroDome") && isNextGame) {
        return false;
    }

    if (MR::isEqualStageName("AstroGalaxy") && isNextGame && MR::isEqualString(mNextSceneControlInfo.mStage, "AstroDome")) {
        return false;
    }

    if (MR::isEqualStageName("LibraryRoom") && isNextGame) {
        return false;
    }

    if (MR::isEqualStageName("AstroGalaxy") && isNextGame && MR::isEqualString(mNextSceneControlInfo.mStage, "LibraryRoom")) {
        return false;
    }

    if (MR::isEqualStageName("HeavensDoorGalaxy") && isNextGame) {
        return false;
    }

    if (MR::isEqualStageName("AstroGalaxy") && isNextGame &&
        MR::isEqualString(mNextSceneControlInfo.mStage, "HeavensDoorGalaxy")) {
        return false;
    }

    return true;
}

bool GameSystemSceneController::isReadyToStartScene() const {
    return isCurrentNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerReadyToStartScene)) &&
           !mScenarioSelectScene->isScenarioSelecting();
}

bool GameSystemSceneController::isNerveNormal() const {
    return isCurrentNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerNormal));
}

bool GameSystemSceneController::isFirstUpdateSceneNerveNormal() const {
    return isCurrentNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerNormal)) && mSpine->mStep == 1;
}

void GameSystemSceneController::startScene() {
    mScene->start();
    requestChangeNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerNormal));
}

void GameSystemSceneController::updateScene() {
    if (mIsResetProcessing) {
        return;
    }

    Scene* pScene = getCurrentSceneForExecute();

    if (pScene != nullptr) {
        pScene->update();
        mScenarioSelectScene->update();
    }
}

void GameSystemSceneController::updateSceneDuringResetProcessing() {
    if (mIsResetProcessing) {
        mScenarioSelectScene->update();
    }
}

void GameSystemSceneController::calcAnimScene() {
    Scene* pScene = getCurrentSceneForExecute();

    if (pScene != nullptr) {
        const NebulaMods::InStage inStage(mCurrSceneControlInfo.mScene == SceneKind_Game);
        pScene->calcAnim();
        mScenarioSelectScene->calcAnim();
    }
}

void GameSystemSceneController::drawScene() {
    Scene* pScene = getCurrentSceneForExecute();

    if (pScene != nullptr) {
        const NebulaMods::InStage inStage(mCurrSceneControlInfo.mScene == SceneKind_Game);
        pScene->draw();
        mScenarioSelectScene->draw();
    }
}

void GameSystemSceneController::drawSceneOverlay() {
    Scene* pScene = getCurrentSceneForExecute();
    if (pScene != nullptr && mCurrSceneControlInfo.mScene == SceneKind_Game) {
        static_cast< GameScene* >(pScene)->drawOverlay();
    }
}

NameObjListExecutor* GameSystemSceneController::getNameObjListExecutor() const {
    return mScene->mListExecutor;
}

SceneObjHolder* GameSystemSceneController::getSceneObjHolder() const {
    return mScene->mSceneObjHolder;
}

bool GameSystemSceneController::isExistSceneObjHolder() const {
    return mScene != nullptr && mScene->mSceneObjHolder != nullptr;
}

s32 GameSystemSceneController::getCurrentScenarioNo() const {
    return mCurrSceneControlInfo.mScenarioNo;
}

s32 GameSystemSceneController::getCurrentSelectedScenarioNo() const {
    return mCurrSceneControlInfo.mSelectedScenarioNo;
}

void GameSystemSceneController::setSceneInitializeState(SceneInitializeState state) {
    mSceneInitializeState = state;
}

bool GameSystemSceneController::isSceneInitializeState(SceneInitializeState state) const {
    return mSceneInitializeState == state;
}

void GameSystemSceneController::startScenarioSelectScene() {
    mScenarioSelectScene->start();
}

void GameSystemSceneController::startScenarioSelectSceneBackground() {
    mScenarioSelectScene->startBackground();
}

void GameSystemSceneController::setCurrentScenarioNo(s32 scenarioNo, s32 selectedScenarioNo) {
    mCurrSceneControlInfo.mScenarioNo = scenarioNo;
    mCurrSceneControlInfo.mSelectedScenarioNo = selectedScenarioNo;
}

void GameSystemSceneController::resetCurrentScenarioNo() {
    mCurrSceneControlInfo.mScenarioNo = -1;
}

bool GameSystemSceneController::isScenarioDecided() const {
    return mCurrSceneControlInfo.mScenarioNo > 0;
}

void GameSystemSceneController::exeNotInitialized() {
    if (mSpine->mStep == 0) {
        mIntermissionScene->setCurrentSceneControllerState("[system setup]");
    }
}

void GameSystemSceneController::exeNormal() {
}

void GameSystemSceneController::exeWaitDrawDoneScene() {
    const Nerve* pNerve;

    if (isCurrentNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerWaitDrawDoneScene))) {
        if (mScene != nullptr) {
            pNerve = GET_NERVE(GameSystemSceneController, GameSystemSceneControllerDestroyScene);
        } else {
            pNerve = GET_NERVE(GameSystemSceneController, GameSystemSceneControllerChoosePlayerVoices);
        }
    } else if (mScene != nullptr) {
        pNerve = GET_NERVE(GameSystemSceneController, GameSystemSceneControllerDestroySceneForDestroy);
    } else {
        pNerve = GET_NERVE(GameSystemSceneController, GameSystemSceneControllerDestroyed);
    }

    requestChangeNerve(pNerve);
}

void GameSystemSceneController::exeDestroyScene() {
    if (mSpine->mStep == 0) {
        mIntermissionScene->setCurrentSceneControllerState("[destroy Scene: %s]", MR::getSceneKindName(mCurrSceneControlInfo.mScene));
        destroyScene();

        const Nerve* pNerve = GET_NERVE(GameSystemSceneController, GameSystemSceneControllerChoosePlayerVoices);

        if (isCurrentNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerDestroySceneForDestroy))) {
            pNerve = GET_NERVE(GameSystemSceneController, GameSystemSceneControllerDestroyed);
        }

        requestChangeNerve(pNerve);
    }
}

void GameSystemSceneController::exeChoosePlayerVoices() {
    ::getAudioSystemWrapper()->setPlayerLuigi(GameSystemFunction::isCreatedGameDataHolder() ? MR::isPlayerLuigi() : false);
    requestChangeNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerInitializeScene));
}

void GameSystemSceneController::exeInitializeScene() {
    if (mSpine->mStep == 0) {
        mIntermissionScene->setCurrentSceneControllerState("[initialize Scene: %s]", MR::getSceneKindName(mNextSceneControlInfo.mScene));
        mSceneInitializeDone = false;
        initializeScene();
    }

    if (mSceneInitializeDone) {
        mSceneInitializeState = SceneInitializeState_End;
        mIntermissionScene->setCurrentSceneControllerState("[scene ready]");

        requestChangeNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerInvalidateSystemWipe));
    }
}

void GameSystemSceneController::exeInvalidateSystemWipe() {
    bool isValidChangeNerve = false;

    if (!GameSystemFunction::isCreatedSystemWipe()) {
        isValidChangeNerve = true;
    } else if (mScenarioSelectScene->isExecForeground() || mScenarioSelectScene->mIsCanceled) {
        isValidChangeNerve = true;
    } else if (!MR::isSystemWipeActive()) {
        isValidChangeNerve = true;
    }

    if (isValidChangeNerve) {
        requestChangeNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerReadyToStartScene));
    }
}

void GameSystemSceneController::exeDestroyed() {
    if (mSpine->mStep == 0) {
        mIntermissionScene->setCurrentSceneControllerState("[empty scene]");
    }
}

void GameSystemSceneController::exeReadyToStartScene() {
    if (mIsResetProcessing) {
        const Nerve* pNerve = getNextNerveOnResetProcessing();

        if (pNerve != nullptr) {
            requestChangeNerve(pNerve);
        }
    }
}

void GameSystemSceneController::prepareReset() {
    mIsResetProcessing = true;

    bool isInitializingScene = isCurrentNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerInitializeScene)) ||
                               isNextNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerInitializeScene));

    mScenarioSelectScene->requestReset(isInitializingScene);

    if (isCurrentNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerNormal))) {
        const Nerve* nextNerve = getNextNerveOnResetProcessing();

        if (nextNerve != nullptr) {
            requestChangeNerve(nextNerve);
        }
    }
}

bool GameSystemSceneController::isPreparedReset() {
    return isCurrentNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerDestroyed)) && mScenarioSelectScene->isResetEnd();
}

void GameSystemSceneController::restartGameAfterResetting() {
    releaseSceneFiles(true);
    Arenas::resetGame();

    mIsResetProcessing = false;
}

bool GameSystemSceneController::isExistRequest() const {
    return isNextNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerWaitDrawDoneScene)) ||
           isCurrentNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerWaitDrawDoneScene));
}

bool GameSystemSceneController::isSameAtNextSceneAndStage() const {
    return mCurrSceneControlInfo.mScene == mNextSceneControlInfo.mScene &&
           MR::isEqualString(mCurrSceneControlInfo.mStage, mNextSceneControlInfo.mStage) &&
           mCurrSceneControlInfo.mScenarioNo == mNextSceneControlInfo.mScenarioNo;
}

void GameSystemSceneController::updateSceneControlInfo() {
    mCurrSceneControlInfo.mScene = mNextSceneControlInfo.mScene;
    mCurrSceneControlInfo.mStage = mNextSceneControlInfo.mStage;
    mCurrSceneControlInfo.mScenarioNo = mNextSceneControlInfo.mScenarioNo;
    mCurrSceneControlInfo.mSelectedScenarioNo = mNextSceneControlInfo.mSelectedScenarioNo;
    mCurrSceneControlInfo.setStartIdInfo(*mNextSceneControlInfo.mStartIdInfo);

    mNextSceneControlInfo.mScene = SceneKind_None;
    mNextSceneControlInfo.mStage = "";
    mNextSceneControlInfo.mScenarioNo = -1;
    mNextSceneControlInfo.mSelectedScenarioNo = -1;
}

Scene* GameSystemSceneController::getCurrentSceneForExecute() const {
    if (mScene != nullptr) {
        if (isCurrentNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerNormal))) {
            return mScene;
        }
    }

    return mIntermissionScene;
}

const Nerve* GameSystemSceneController::getNextNerveOnResetProcessing() const {
    const Nerve* nextNerve = nullptr;

    if (isCurrentNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerWaitDrawDoneSceneForDestroy)) ||
        isCurrentNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerDestroySceneForDestroy)) ||
        isCurrentNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerDestroyed))) {
    } else if (isCurrentNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerDestroyScene)) ||
               isCurrentNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerChoosePlayerVoices)) ||
               isCurrentNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerNotInitialized))) {
        nextNerve = GET_NERVE(GameSystemSceneController, GameSystemSceneControllerDestroyed);
    } else if (isCurrentNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerWaitDrawDoneScene)) ||
               isCurrentNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerNormal)) ||
               isCurrentNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerInitializeScene)) ||
               isCurrentNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerInvalidateSystemWipe)) ||
               isCurrentNerve(GET_NERVE(GameSystemSceneController, GameSystemSceneControllerReadyToStartScene))) {
        nextNerve = GET_NERVE(GameSystemSceneController, GameSystemSceneControllerWaitDrawDoneSceneForDestroy);
    }

    return nextNerve;
}

bool GameSystemSceneController::releaseSceneFiles(bool isForce) {
    if (!isForce && Arenas::sceneFiles() != nullptr && isSameAtNextSceneAndStage()) {
        return false;
    }

    MR::removeResourceHolders(Arenas::sceneFiles());

    return true;
}

void GameSystemSceneController::requestChangeNerve(const Nerve* pNerve) {
    const Nerve* pNextNerve;

    if (mIsResetProcessing) {
        pNextNerve = getNextNerveOnResetProcessing();
    } else {
        pNextNerve = nullptr;
    }

    if (pNextNerve == nullptr) {
        pNextNerve = pNerve;
    }

    mNextNerve = pNextNerve;
}

bool GameSystemSceneController::isCurrentNerve(const Nerve* pNerve) const {
    return pNerve == mSpine->getCurrentNerve();
}

bool GameSystemSceneController::isNextNerve(const Nerve* pNerve) const {
    return mNextNerve == pNerve;
}
