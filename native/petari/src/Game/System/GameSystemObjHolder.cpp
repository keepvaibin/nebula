#include "Game/System/GameSystemObjHolder.hpp"
#include "Game/Effect/ParticleResourceHolder.hpp"
#include "Game/NameObj/NameObjRegister.hpp"
#include "Game/Screen/CaptureScreenDirector.hpp"
#include "Game/Screen/ScreenPreserver.hpp"
#include "Game/Screen/StarPointerDirector.hpp"
#include "Game/System/AudSystemWrapper.hpp"
#include "Game/System/ErrorMessageArchive.hpp"
#include "Game/System/FileLoader.hpp"
#include "Game/System/FunctionAsyncExecutor.hpp"
#include "Game/System/HeapMemoryWatcher.hpp"
#include "Game/System/Language.hpp"
#include "Game/System/MainLoopFramework.hpp"
#include "Game/System/MessageHolder.hpp"
#include "Game/System/RenderMode.hpp"
#include "Game/System/ResourceHolderManager.hpp"
#include "Game/System/WPadHolder.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <JSystem/JKernel/JKRExpHeap.hpp>
#include "Game/System/NebulaFrameClock.hpp"

GameSystemObjHolder::GameSystemObjHolder()
    : mObjHolder(), mParticleResHolder(), mViWidth(), mCaptureScreenDirector(), mScreenPreserver(), mAudioSystem(), mWPadHolder(),
      mFunctionAsyncExecutor(), mMessageHolder(), mStarPointerDirector(), mRandom(0), mLanguage(MR::getDecidedLanguageFromIPL()) {
    initDvd();
    initNAND();
    initNameObj();
    MR::createAndAddArchive(NebulaDiscTables::loadErrorMessageArchive(MR::getStationedHeap()), MR::getStationedHeap(),
                            "ErrorMessageArchive.arc");
}

void GameSystemObjHolder::initAfterStationedResourceLoaded() {
    SingletonHolder< NameObjRegister >::get()->setCurrentHolder(mObjHolder);
    mParticleResHolder = new ParticleResourceHolder("/ParticleData/Effect.arc");
    mMessageHolder->initGameData();
    MR::createStarPointerLayout();
}

void GameSystemObjHolder::initMessageResource() {
    JKRArchive* pArchive = nullptr;
    JKRHeap* pHeap = nullptr;
    MR::getMountedArchiveAndHeap("ErrorMessageArchive.arc", &pArchive, &pHeap);

    char systemMessagePath[128];
    const char* languagePrefix = MR::getCurrentLanguagePrefix();
    snprintf(systemMessagePath, sizeof(systemMessagePath), "/%s/MessageData/System.arc", languagePrefix);

    void* pSystemMessageArchive = MR::copyFileFromArchive(pArchive, systemMessagePath, nullptr, 0);
    MR::createAndAddArchive(pSystemMessageArchive, pHeap, "/Memory/SystemMessage.arc");

    void* pErrorMessageWindowArchive = MR::copyFileFromArchive(pArchive, "/LayoutData/ErrorMessageWindow.arc", pHeap, 32);
    MR::createAndAddArchive(pErrorMessageWindowArchive, pHeap, "/Memory/ErrorMessageWindow.arc");

    MR::createAndAddLayoutHolderRawData("/Memory/ErrorMessageWindow.arc");

    mMessageHolder = new MessageHolder();
    mMessageHolder->initSystemData();
}

void GameSystemObjHolder::init() {
    initAudio();
    initRenderMode();
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
    mWPadHolder->update();
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

void GameSystemObjHolder::initDvd() {
    JKRMemArchive archive;

    JKRFileLoader::initializeVolumeList();
    SingletonHolder< FileLoader >::init();
}

void GameSystemObjHolder::initNAND() {
}

void GameSystemObjHolder::initAudio() {
    mAudioSystem = new AudSystemWrapper(SingletonHolder< HeapMemoryWatcher >::get()->getAudSystemHeap(), MR::getStationedHeap());
    mAudioSystem->requestResourceForInitialize();
}

void GameSystemObjHolder::initRenderMode() {
    // The game's frame, 640x456 in either aspect; the picture spans 686 of the television line's 720 in 16:9, 670 in
    // 4:3.
    mViWidth = MR::isAspectRatioFlag16Per9() ? 686 : 670;
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
    initWPad();
    initStarPointerDirector();
}

void GameSystemObjHolder::initWPad() {
    mWPadHolder = new WPadHolder();
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
