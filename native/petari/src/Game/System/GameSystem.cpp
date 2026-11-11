#include "Game/System/LumaScript.hpp"
#include "Game/System/LumaHook.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/System/NebulaLoadTrace.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Render/FrameList.hpp"
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
#include "Game/System/GameSystemRestartProcess.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/System/GameFrame.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/SystemUtil.hpp"
#include <Game/Ui/UiInit.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>
#include "Game/Screen/NebulaMenuFocus.hpp"
#include "Game/Screen/NebulaPrepare.hpp"
#include "Game/Screen/NebulaSettingsMenu.hpp"
#include "Game/Screen/NebulaIcons.hpp"
#include "Game/System/NebulaCutsceneSkip.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/GameAssets.hpp"



namespace NrvGameSystem {
    NEW_NERVE(GameSystemInitializeAudio, GameSystem, InitializeAudio);
    NEW_NERVE(GameSystemInitializeBootScene, GameSystem, InitializeBootScene);
    NEW_NERVE(GameSystemLoadSystemArchive, GameSystem, LoadSystemArchive);
    NEW_NERVE(GameSystemNormal, GameSystem, Normal);
};  // namespace NrvGameSystem

namespace {
    // The drawers GameSystem::draw calls itself, outside the object lists, each under a draw slot of its own: they pair from
    // frame to frame by it, not by the numbers of whichever object drew before them (a full-screen fill would take a pane's
    // matrix when that pane's draw count changed).
    enum { Own_Sequence, Own_CutsceneSkip, Own_ScreenPreserver, Own_SystemWipe, Own_ErrorWatcher, Own_Overlay, Own_Prepare, Own_Settings, Own_Restart, Own_Count };
    u32 sOwnSlots[Own_Count];

    void drawOwn(u32 which) {
        if (sOwnSlots[which] == 0) {
            sOwnSlots[which] = Frame::newSlot();
        }
        Frame::setObject(sOwnSlots[which], 0);
    }
}  // namespace

// The game thread's entry; the native platform layer starts it.
extern "C" void nebula_game_main(void) {
    // Mods start here and hooks change only here (LumaCore.game_thread, loader contract 4).
    luma::markGameThread();
    NebulaArena::create(nullptr);
    Ui::LytInit();
    Arenas::create();
    Arenas::permanent()->makeCurrent();
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
      mObjHolder(nullptr), mSceneController(nullptr), mHomeButtonLayout(nullptr),
      mSystemWipeHolder(nullptr), mIsExecuteLoadSystemArchive(false), mWasSceneHeld(false), mIsSceneResumed(false) {
}

void GameSystem::init() {
    const NebulaHostServices* const pHost = nebula_host();
    pHost->mods_start(pHost->user);
    Frame::registerPipelineList();
    mObjHolder = new GameSystemObjHolder();
    mFontHolder = new GameSystemFontHolder();
    mFontHolder->createFontFromEmbeddedData();
    initNerve(GET_NERVE(GameSystem, GameSystemInitializeAudio));
    mSequenceDirector = new GameSequenceDirector();
    Frame::initDefaults();
    mSceneController = new GameSystemSceneController();
    mObjHolder->init();
    mErrorWatcher = new GameSystemErrorWatcher();
    SingletonHolder< GameSystemRestartProcess >::init();
    SingletonHolder< GameSystemRestartProcess >::get()->initWithoutIter();
    mHomeButtonLayout = new HomeButtonLayout();
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

    Arenas::permanent()->makeCurrent();
    SingletonHolder< NameObjRegister >::get()->setCurrentHolder(mObjHolder->mObjHolder);
    setNerve(GET_NERVE(GameSystem, GameSystemLoadSystemArchive));
}

void GameSystem::exeInitializeAudio() {
    if (MR::isFirstStep(this)) {
        mObjHolder->createAudioSystem();
    }

    updateSceneController();

    if (mObjHolder->mAudioSystem->isCreated()) {
        setNerve(GET_NERVE(GameSystem, GameSystemInitializeBootScene));
    }
}

void GameSystem::exeInitializeBootScene() {
    if (MR::isFirstStep(this)) {
        MR::requestChangeScene(SceneKind_Boot);
    }

    updateSceneController();
}

void GameSystem::exeLoadSystemArchive() {
    initAfterSystemArchiveLoaded();
    Arenas::permanent()->makeCurrent();
    GameSystemFunction::setSceneNameObjHolderToNameObjRegister();
    updateSceneController();
    setNerve(GET_NERVE(GameSystem, GameSystemNormal));
}

void GameSystem::exeNormal() {
    updateSceneController();
}

void GameSystem::initAfterSystemArchiveLoaded() {
    mFontHolder->createFontFromFile();
    mObjHolder->initAfterSystemArchiveLoaded();
    mHomeButtonLayout->initWithoutIter();
    mErrorWatcher->initAfterResourceLoaded();
    mSystemWipeHolder = MR::createSystemWipeHolder();
    NebulaIcons::init();
    NebulaCutsceneSkip::init();
    NebulaSettingsMenu::init();
    NebulaPrepare::init();
    mSceneController->initAfterSystemArchiveLoaded();
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
    drawOwn(Own_Sequence);
    mSequenceDirector->draw();
    drawOwn(Own_CutsceneSkip);
    NebulaCutsceneSkip::draw();
    // The settings menu covers everything, the wipes too; the pointer is drawn over it.
    const bool isSettingsShown = NebulaSettingsMenu::isShown();
    if (!isSettingsShown) {
        mObjHolder->drawStarPointer();
    }
    drawOwn(Own_ScreenPreserver);
    mObjHolder->drawBeforeEndRender();

    if (mSystemWipeHolder != nullptr) {
        drawOwn(Own_SystemWipe);
        mSystemWipeHolder->draw();
    }

    drawOwn(Own_ErrorWatcher);
    mErrorWatcher->draw();
    drawOwn(Own_Overlay);
    mSceneController->drawSceneOverlay();
    drawOwn(Own_Prepare);
    NebulaPrepare::draw();
    if (isSettingsShown) {
        drawOwn(Own_Settings);
        NebulaSettingsMenu::draw();
        mObjHolder->drawStarPointer();
    }
    drawOwn(Own_Restart);
    SingletonHolder< GameSystemRestartProcess >::get()->draw();
}

void GameSystem::update() {
    SingletonHolder< GameSystemRestartProcess >::get()->movement();
    mSceneController->checkRequestAndChangeScene();
    mObjHolder->update();
    NebulaMenuFocus::update();
    NebulaPrepare::update();
    lumascript::beginFrame();  // the scripts' step budgets; nothing when no mod has a script
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
    bool isResetProcessing = SingletonHolder< GameSystemRestartProcess >::get()->isActive();

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
    const bool isSceneHeld = mHomeButtonLayout->isActive() || GameSystemFunction::isOccurredSystemWarning() || isSkipPrompt || isSettingsMenu;
    mIsSceneResumed = mWasSceneHeld && !isSceneHeld;
    mWasSceneHeld = isSceneHeld;

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

    NebulaHost::gameContext(NEBULA_CONTEXT_SCREEN_COVERED, MR::isScreenCoveredByWipe() ? 1 : 0);
}

void GameSystem::calcAnim() {
    mSceneController->calcAnimScene();

    if (mSystemWipeHolder != nullptr) {
        mSystemWipeHolder->calcAnim();
    }
}
