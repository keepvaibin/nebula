#include "Game/System/GameSystemObjHolder.hpp"
#include "Game/Effect/ParticleResourceHolder.hpp"
#include "Game/NameObj/NameObjRegister.hpp"
#include "Game/Screen/CaptureScreenDirector.hpp"
#include "Game/Screen/ScreenPreserver.hpp"
#include "Game/Screen/StarPointerDirector.hpp"
#include "Game/System/AudSystemWrapper.hpp"
#include "Game/System/AssetFolder.hpp"
#include "Game/System/ErrorMessageArchive.hpp"
#include "Game/System/FileLoader.hpp"
#include "Game/System/FunctionAsyncExecutor.hpp"
#include "Game/System/HeapMemoryWatcher.hpp"
#include "Game/System/Language.hpp"
#include "Game/System/MainLoopFramework.hpp"
#include "Game/System/MessageHolder.hpp"
#include "Game/System/RenderMode.hpp"
#include "Game/System/ResourceHolderManager.hpp"
#include "Game/System/PlayerInputs.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/NebulaFrameClock.hpp"

GameSystemObjHolder::GameSystemObjHolder()
    : mObjHolder(), mParticleResHolder(), mPictureSpan(), mCaptureScreenDirector(), mScreenPreserver(), mAudioSystem(), mPlayerInputs(),
      mFunctionAsyncExecutor(), mMessageHolder(), mStarPointerDirector(), mRandom(0), mLanguage(MR::getDecidedLanguageFromIPL()) {
    initStore();
    initNAND();
    initNameObj();
    MR::createAndAddArchive(AssetFolder::mount("/NebulaData/ErrorMessageArchive.arc", MR::getStationedHeap()), MR::getStationedHeap(),
                            "ErrorMessageArchive.arc");
}

void GameSystemObjHolder::initAfterStationedResourceLoaded() {
    SingletonHolder< NameObjRegister >::get()->setCurrentHolder(mObjHolder);
    mParticleResHolder = new ParticleResourceHolder("/ParticleData/Effect.arc");
    mMessageHolder->initGameData();
    MR::createStarPointerLayout();
}

void GameSystemObjHolder::initMessageResource() {
    AssetFolder* pArchive = nullptr;
    NebulaArena* pHeap = nullptr;
    MR::getMountedArchiveAndHeap("ErrorMessageArchive.arc", &pArchive, &pHeap);

    char systemMessagePath[256];
    const char* languagePrefix = MR::getCurrentLanguagePrefix();
    nebula_snprintf(systemMessagePath, sizeof(systemMessagePath), "/%s/MessageData/System.arc", languagePrefix);

    MR::createAndAddArchive(pArchive->getSubFolder(systemMessagePath, pHeap), pHeap, "/Memory/SystemMessage.arc");
    MR::createAndAddArchive(pArchive->getSubFolder("/LayoutData/ErrorMessageWindow.arc", pHeap), pHeap, "/Memory/ErrorMessageWindow.arc");

    MR::createAndAddLayoutHolderRawData("/Memory/ErrorMessageWindow.arc");

    mMessageHolder = new MessageHolder();
    mMessageHolder->initSystemData();
}

void GameSystemObjHolder::init() {
    initAudio();
    initPicture();
    initResourceHolderManager();
    initMessageResource();
    initFunctionAsyncExecutor();
    initGameController();
    initDisplay();
}

void GameSystemObjHolder::createAudioSystem() {
    if (mAudioSystem != nullptr) {
        mAudioSystem->createAudioSystem();
    }
}

void GameSystemObjHolder::update() {
    mAudioSystem->updateRhythm();
    mPlayerInputs->update();
    mFunctionAsyncExecutor->update();
    mStarPointerDirector->update();
}

void GameSystemObjHolder::updateAudioSystem() {
    if (mAudioSystem != nullptr) {
        mAudioSystem->movement();
    }
}

void GameSystemObjHolder::clearRequestFileInfo(bool unk) {
    SingletonHolder< FileLoader >::get()->clearRequestFileInfo(unk);
}

void GameSystemObjHolder::drawStarPointer() {
    mStarPointerDirector->draw();
}

void GameSystemObjHolder::drawBeforeEndRender() {
    mScreenPreserver->draw();
}

void GameSystemObjHolder::captureIfAllowForScreenPreserver() {
    mScreenPreserver->captureIfAllow();
}

void GameSystemObjHolder::initStore() {
    SingletonHolder< FileLoader >::init();
}

void GameSystemObjHolder::initNAND() {
}

void GameSystemObjHolder::initAudio() {
    mAudioSystem = new AudSystemWrapper(SingletonHolder< HeapMemoryWatcher >::get()->getAudSystemHeap(), MR::getStationedHeap());
    mAudioSystem->requestResourceForInitialize();
}

void GameSystemObjHolder::initPicture() {
    // The share of the safe area's reference width the picture spans (getSafetyFrameWidth).
    mPictureSpan = MR::isAspectRatioFlag16Per9() ? 686 : 670;
    NebulaFrameClock::startPicture();
}

void GameSystemObjHolder::initNameObj() {
    mObjHolder = new NameObjHolder(256);

    SingletonHolder< NameObjRegister >::init();
    SingletonHolder< NameObjRegister >::get()->setCurrentHolder(mObjHolder);
}

void GameSystemObjHolder::initFunctionAsyncExecutor() {
    mFunctionAsyncExecutor = new FunctionAsyncExecutor();
}

void GameSystemObjHolder::initResourceHolderManager() {
    SingletonHolder< ResourceHolderManager >::init();
}

void GameSystemObjHolder::initGameController() {
    initPlayerInput();
    initStarPointerDirector();
}

void GameSystemObjHolder::initPlayerInput() {
    mPlayerInputs = new PlayerInputs();
}

void GameSystemObjHolder::initStarPointerDirector() {
    mStarPointerDirector = new StarPointerDirector();
}

void GameSystemObjHolder::initDisplay() {
    MainLoopFramework* pManager = MainLoopFramework::createManager(true);
    pManager->setClearColor(Color8(30, 30, 200, 0));

    mCaptureScreenDirector = new CaptureScreenDirector();
    mScreenPreserver = new ScreenPreserver();

}
