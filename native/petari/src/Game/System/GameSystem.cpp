#include "Game/System/GameSystem.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/System/NebulaLoadTrace.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/GameThreads.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include "Game/LiveActor/Nerve.hpp"
#include "Game/NameObj/NameObjRegister.hpp"
#include "Game/Screen/HomeButtonLayout.hpp"
#include "Game/Screen/SystemWipeHolder.hpp"
#include "Game/System/AudSystemWrapper.hpp"
#include "Game/System/GameSequenceDirector.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/System/GameSystemErrorWatcher.hpp"
#include "Game/System/GameSystemFontHolder.hpp"
#include "Game/System/GameSystemFunction.hpp"
#include "Game/System/GameSystemObjHolder.hpp"
#include "Game/System/GameSystemResetAndPowerProcess.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/System/GameSystemStationedArchiveLoader.hpp"
#include "Game/System/HeapMemoryWatcher.hpp"
#include "Game/System/HomeButtonStateNotifier.hpp"
#include "Game/System/GameFrame.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/MutexHolder.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/SystemUtil.hpp"
#include <nw4r/lyt/init.h>
#include <NebulaGpu.h>
#include <nebula_math.h>
#include "Game/Screen/NebulaMenuFocus.hpp"
#include "Game/Screen/NebulaRtPrepare.hpp"
#include "Game/Screen/NebulaSettingsMenu.hpp"
#include "Game/Screen/NebulaIcons.hpp"
#include "Game/System/NebulaCutsceneSkip.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/GameAssets.hpp"


#define INIT_AUDIO_KEY "\203\111\201\133\203\146\203\102\203\111\217\211\212\372\211\273" /* オーディオ初期化 */  // "Audio Initialization"

namespace NrvGameSystem {
    NEW_NERVE(GameSystemInitializeAudio, GameSystem, InitializeAudio);
    NEW_NERVE(GameSystemInitializeBootScene, GameSystem, InitializeBootScene);
    NEW_NERVE(GameSystemLoadStationedArchive, GameSystem, LoadStationedArchive);
    NEW_NERVE(GameSystemWaitForReboot, GameSystem, WaitForReboot);
    NEW_NERVE(GameSystemNormal, GameSystem, Normal);
};  // namespace NrvGameSystem

// The game thread's entry; the native platform layer starts it.
extern "C" void nebula_game_main(void) {
    NebulaArena::create(nullptr);
    (MR::MutexHolder< 0 >::sMutex).init();
    (MR::MutexHolder< 2 >::sMutex).init();
    nw4r::lyt::LytInit();
    SingletonHolder< HeapMemoryWatcher >::init();
    SingletonHolder< HeapMemoryWatcher >::get()->setCurrentHeapToStationedHeap();
    SingletonHolder< GameSystem >::init();
    SingletonHolder< GameSystem >::get()->init();

    GameSystem* pGameSystem = SingletonHolder< GameSystem >::get();

    pGameSystem->drawFrame();

    while (true) {
        pGameSystem->frameLoop();
    }
}

GameSystem::GameSystem()
    : NerveExecutor("GameSystem"), mSequenceDirector(nullptr), mErrorWatcher(nullptr), mFontHolder(nullptr),
      mObjHolder(nullptr), mSceneController(nullptr), mStationedArchiveLoader(nullptr), mHomeButtonLayout(nullptr),
      mSystemWipeHolder(nullptr), mHomeButtonStateNotifier(nullptr), mIsExecuteLoadSystemArchive(false) {
}

void GameSystem::init() {
    nebula_host()->mods_start(nebula_host()->user);
    mObjHolder = new GameSystemObjHolder();
    mFontHolder = new GameSystemFontHolder();
    mFontHolder->createFontFromEmbeddedData();
    initNerve(GET_NERVE(GameSystem, GameSystemInitializeAudio));
    mSequenceDirector = new GameSequenceDirector();
    J3DNativeState::initDefaults();
    mSceneController = new GameSystemSceneController();
    mObjHolder->init();
    mErrorWatcher = new GameSystemErrorWatcher();
    SingletonHolder< GameSystemResetAndPowerProcess >::init();
    SingletonHolder< GameSystemResetAndPowerProcess >::get()->initWithoutIter();
    mStationedArchiveLoader = new GameSystemStationedArchiveLoader();
    mHomeButtonLayout = new HomeButtonLayout();
    mHomeButtonStateNotifier = new HomeButtonStateNotifier();
    setNerve(GET_NERVE(GameSystem, GameSystemInitializeAudio));
}

bool GameSystem::isExecuteLoadSystemArchive() const {
    return mIsExecuteLoadSystemArchive;
}

bool GameSystem::isDoneLoadSystemArchive() const {
    return isNerve(GET_NERVE(GameSystem, GameSystemNormal));
}

void GameSystem::startToLoadSystemArchive() {
    mIsExecuteLoadSystemArchive = true;

    SingletonHolder< HeapMemoryWatcher >::get()->setCurrentHeapToStationedHeap();
    SingletonHolder< NameObjRegister >::get()->setCurrentHolder(mObjHolder->mObjHolder);
    setNerve(GET_NERVE(GameSystem, GameSystemLoadStationedArchive));
}

void GameSystem::exeInitializeAudio() {
    if (MR::isFirstStep(this)) {
        MR::startFunctionAsyncExecute(MR::Functor(mObjHolder, &GameSystemObjHolder::createAudioSystem), INIT_AUDIO_KEY);
    }

    updateSceneController();

    if (MR::isEndFunctionAsyncExecute(INIT_AUDIO_KEY) && mObjHolder->mAudioSystem->isLoadDoneWaveDataAtSystemInit()) {
        MR::waitForEndFunctionAsyncExecute(INIT_AUDIO_KEY);
        setNerve(GET_NERVE(GameSystem, GameSystemInitializeBootScene));
    }
}

void GameSystem::exeInitializeBootScene() {
    if (GameSystemFunction::isResetProcessing()) {
        setNerve(GET_NERVE(GameSystem, GameSystemWaitForReboot));
    } else {
        if (MR::isFirstStep(this)) {
            MR::requestChangeScene("Boot");
        }

        updateSceneController();
    }
}

void GameSystem::exeLoadStationedArchive() {
    mStationedArchiveLoader->update();
    updateSceneController();

    if (mStationedArchiveLoader->isDone()) {
        setNerve(GET_NERVE(GameSystem, GameSystemNormal));
    }
}

void GameSystem::exeWaitForReboot() {
}

void GameSystem::exeNormal() {
    updateSceneController();
    mStationedArchiveLoader->update();
}

void GameSystem::initAfterStationedResourceLoaded() {
    mFontHolder->createFontFromFile();
    mObjHolder->initAfterStationedResourceLoaded();
    mHomeButtonLayout->initWithoutIter();
    mErrorWatcher->initAfterResourceLoaded();
    mSystemWipeHolder = MR::createSystemWipeHolder();
    NebulaIcons::init();
    NebulaCutsceneSkip::init();
    NebulaSettingsMenu::init();
    NebulaRtPrepare::init();
    mSceneController->initAfterStationedResourceLoaded();
    mSequenceDirector->initAfterResourceLoaded();
}

void GameSystem::frameLoop() {
    NebulaLoadTrace::frame();
    // The frame's wait comes before its update, so the input it reads is drawn at once, not a frame later.
    NebulaFrameClock::sleepUntilFrame();
    update();
    calcAnim();
    // A skipped cutscene runs on unseen: more of the game's steps while they fit in this frame.
    while (NebulaCutsceneSkip::tryExtraStep()) {
        update();
        calcAnim();
    }
    mObjHolder->captureIfAllowForScreenPreserver();
    NebulaFrameClock::beginFrame();
    drawFrame();
}

void GameSystem::drawFrame() {
    GameFrame::begin();
    draw();
    GameFrame::end();
}

void GameSystem::draw() {
    mSceneController->drawScene();
    mSequenceDirector->draw();
    NebulaCutsceneSkip::draw();
    // The settings menu covers everything, the wipes too; the pointer is drawn over it.
    const bool isSettingsShown = NebulaSettingsMenu::isShown();
    if (!isSettingsShown) {
        mObjHolder->drawStarPointer();
    }
    mObjHolder->drawBeforeEndRender();

    if (mSystemWipeHolder != nullptr) {
        mSystemWipeHolder->draw();
    }

    mErrorWatcher->draw();
    mSceneController->drawSceneOverlay();
    NebulaRtPrepare::draw();
    if (isSettingsShown) {
        NebulaSettingsMenu::draw();
        mObjHolder->drawStarPointer();
    }
    mHomeButtonLayout->draw();
    SingletonHolder< GameSystemResetAndPowerProcess >::get()->draw();
}

void GameSystem::update() {
    SingletonHolder< GameSystemResetAndPowerProcess >::get()->movement();
    mSceneController->checkRequestAndChangeScene();
    mObjHolder->update();
    NebulaMenuFocus::update();
    NebulaRtPrepare::update();
    NebulaMods::fire(NEBULA_MOD_EVENT_FRAME);
    NebulaSettingsMenu::update();
    NebulaIcons::update();
    // The skip's question waits under the settings menu.
    if (!NebulaSettingsMenu::isFreezingScene()) {
        NebulaCutsceneSkip::update();
    }
    mHomeButtonLayout->movement();

    if (!mHomeButtonLayout->isActive()) {
        mErrorWatcher->movement();
    }

    updateNerve();
}

void GameSystem::updateSceneController() {
    bool isSceneUpdate = true;
    bool isResetProcessing = SingletonHolder< GameSystemResetAndPowerProcess >::get()->isActive();

    if (isResetProcessing) {
        isSceneUpdate = false;
    }

    if (mHomeButtonLayout->isActive()) {
        isSceneUpdate = false;
    }

    if (GameSystemFunction::isOccurredSystemWarning()) {
        isSceneUpdate = false;
    }

    // The cutscene skip's question and its fade out, and the settings menu, hold the scene still as the HOME Menu does
    // (a movie resumes in step after them).
    const bool isSkipPrompt = NebulaCutsceneSkip::isFreezingScene();
    const bool isSettingsMenu = NebulaSettingsMenu::isFreezingScene();
    if (isSkipPrompt || isSettingsMenu) {
        isSceneUpdate = false;
    }
    mHomeButtonStateNotifier->update(mHomeButtonLayout->isActive() || GameSystemFunction::isOccurredSystemWarning() ||
                                     isSkipPrompt || isSettingsMenu);

    if (isSceneUpdate || isResetProcessing) {
        mSequenceDirector->update();
    }

    if (isSceneUpdate || mSceneController->isFirstUpdateSceneNerveNormal()) {
        if (mSystemWipeHolder != nullptr) {
            mSystemWipeHolder->movement();
        }

        mSceneController->updateScene();
    }
    else if (isSkipPrompt && !isSettingsMenu && mSystemWipeHolder != nullptr) {
        // The skip's fade to black runs over the held scene.
        mSystemWipeHolder->movement();
    }

    if (isResetProcessing) {
        mSceneController->updateSceneDuringResetProcessing();
    }

    // After the scene: what it asked for this frame starts now, not a frame later.
    mObjHolder->updateAudioSystem();
}

void GameSystem::calcAnim() {
    mSceneController->calcAnimScene();

    if (mSystemWipeHolder != nullptr) {
        mSystemWipeHolder->calcAnim();
    }
}
