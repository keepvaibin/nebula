#include "Game/System/GameSystemObjHolder.hpp"
#include "Game/Effect/ParticleResourceHolder.hpp"
#include "Game/NameObj/NameObjRegister.hpp"
#include "Game/Screen/CaptureScreenDirector.hpp"
#include "Game/Screen/ScreenPreserver.hpp"
#include "Game/Screen/StarPointerDirector.hpp"
#include "Game/System/AudSystemWrapper.hpp"
#include "Game/System/AssetFolder.hpp"
#include "Game/System/ErrorMessageArchive.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/System/Language.hpp"
#include "Game/System/MessageHolder.hpp"
#include "Game/System/ResourceHolderManager.hpp"
#include "Game/System/PlayerInputs.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/StringUtil.hpp"

GameSystemObjHolder::GameSystemObjHolder()
    : mObjHolder(), mParticleResHolder(), mCaptureScreenDirector(), mScreenPreserver(), mAudioSystem(), mPlayerInputs(), mMessageHolder(), mStarPointerDirector(), mRandom(0), mLanguage(MR::decideLanguage()) {
    initNameObj();
    MR::addArchive(AssetFolder::mount("/NebulaData/ErrorMessageArchive", Arenas::permanent()), Arenas::permanent(), "ErrorMessageArchive");
}

void GameSystemObjHolder::initAfterSystemArchiveLoaded() {
    SingletonHolder< NameObjRegister >::get()->setCurrentHolder(mObjHolder);
    mParticleResHolder = new ParticleResourceHolder("/ParticleData/Effect");
    mMessageHolder->initGameData();
    MR::createStarPointerLayout();
}

void GameSystemObjHolder::initMessageResource() {
    AssetFolder* pArchive = MR::getArchive("ErrorMessageArchive");
    NebulaArena* pArena = MR::getArchiveArena("ErrorMessageArchive");

    MR::FormattedName systemMessagePath;
    const char* languagePrefix = MR::getCurrentLanguagePrefix();
    systemMessagePath.format("/%s/MessageData/System", languagePrefix);

    MR::addArchive(pArchive->getSubFolder(systemMessagePath.c_str(), pArena), pArena, "/Memory/SystemMessage");
    MR::addArchive(pArchive->getSubFolder("/LayoutData/ErrorMessageWindow", pArena), pArena, "/Memory/ErrorMessageWindow");

    MR::createAndAddLayoutHolderRawData("/Memory/ErrorMessageWindow");

    mMessageHolder = new MessageHolder();
    mMessageHolder->initSystemData();
}

void GameSystemObjHolder::init() {
    initAudio();
    initPicture();
    initResourceHolderManager();
    initMessageResource();
    initGameController();
    initDisplay();
}

void GameSystemObjHolder::createAudioSystem() {
    if (mAudioSystem != nullptr) {
        mAudioSystem->createAudioSystem();
    }
}

void GameSystemObjHolder::update() {
    mPlayerInputs->update();
    mStarPointerDirector->update();
}

void GameSystemObjHolder::updateAudioSystem() {
    if (mAudioSystem != nullptr) {
        mAudioSystem->movement();
    }
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

void GameSystemObjHolder::initAudio() {
    mAudioSystem = new AudSystemWrapper(Arenas::permanent());
}

void GameSystemObjHolder::initPicture() {
    NebulaFrameClock::show(true);
}

void GameSystemObjHolder::initNameObj() {
    mObjHolder = new NameObjHolder(256);

    SingletonHolder< NameObjRegister >::init();
    SingletonHolder< NameObjRegister >::get()->setCurrentHolder(mObjHolder);
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
    mCaptureScreenDirector = new CaptureScreenDirector();
    mScreenPreserver = new ScreenPreserver();

}
