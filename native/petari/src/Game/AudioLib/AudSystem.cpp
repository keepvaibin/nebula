#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudEffector.hpp"
#include "Game/AudioLib/AudLimitedSound.hpp"
#include "Game/AudioLib/AudParams.hpp"
#include "Game/AudioLib/AudRemixMgr.hpp"
#include "Game/AudioLib/AudSceneMgr.hpp"
#include "Game/AudioLib/AudSeStrategy.hpp"
#include "Game/AudioLib/AudSoundId.hpp"
#include "Game/AudioLib/AudSoundInfo.hpp"
#include "Game/AudioLib/AudSoundObjHolder.hpp"
#include "Game/AudioLib/AudSoundObject.hpp"
#include "Game/AudioLib/AudSystemVolumeController.hpp"
#include "Game/RhythmLib/AudChordInfo.hpp"
#include "Game/RhythmLib/AudMeObject.hpp"
#include "Game/RhythmLib/AudMePlayer.hpp"
#include "Game/RhythmLib/AudRhythmMeSystem.hpp"
#include "Game/Speaker/SpkSystem.hpp"
#include "Game/Sound/SoundAudible.hpp"
#include "Game/Sound/EffectManager.hpp"
#include "Game/Sound/SeqSource.hpp"
#include "Game/Sound/SoundStarter.hpp"
#include "Game/Sound/PlayerLink.hpp"
#include "Game/Sound/SoundParams.hpp"
#include "Game/Sound/SoundArchive.hpp"
#include "Game/Sound/SoundEngine.hpp"
#include "Game/Sound/SoundManager.hpp"
#include "Game/Sound/SoundSystem.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/AssetFolder.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/NebulaSystemSettings.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/Util/Tunable.hpp"

const char cSoundDataPath[] = "/AudioRes/SMR";
const char cSeqArchivePath[] = "/AudioRes/Seqs/JaiSeq";
const char cChordArchivePath[] = "/AudioRes/Info/JaiChord";
const char cMeArchivePath[] = "/AudioRes/Info/JaiMe";
const char cRemixArchivePath[] = "/AudioRes/Info/JaiRemixSeq";

AudSystem* AudSystem::msBasic;

// The beat readers: the game side and the audio thread's must have the same count.
NEBULA_TUNABLE(u32, cRhythmParsers, 3);

AudSystem* AudNewAudSystem_(SoundArchive* pSoundArchive, AssetFolder* pChordArchive, AssetFolder* pMeArchive, AssetFolder* pRemixArchive, int maxChannels) {
    AudSystem* system = new AudSystem(pSoundArchive, pChordArchive, pMeArchive, pRemixArchive);
    system->mAudience.setListenerCount(maxChannels);
    return system;
}

// The engine, then the archive's banks (the engine's before its clock runs), then the clock.
AudSystem* AudNewAudSystem(NebulaArena* pArena, AssetFolder* pSeqArchive, AssetFolder* pChordArchive, AssetFolder* pMeArchive,
                           AssetFolder* pRemixArchive) {
    NebulaArena* previous = NebulaArena::current();
    NebulaArena* soundArena = NebulaArena::create(pArena);
    SoundSystem::create(soundArena, AudParams::outputGain);
    SoundArchive* audioData = new (soundArena, 0) SoundArchive(soundArena);
    soundArena->makeCurrent();

    audioData->load(cSoundDataPath);
    audioData->setAllWaves(AudSceneMgr::cPlayerVoiceBank);
    SoundEngine::get()->rhythm().setup(SoundEngine::get(), cRhythmParsers, soundArena);
    SoundSystem::start();

    if (pSeqArchive == nullptr && GameAssets::find(cSeqArchivePath) >= 0) {
        pSeqArchive = AssetFolder::mount(cSeqArchivePath, previous);
    }
    audioData->setSeqArchive(pSeqArchive);

    if (pChordArchive == nullptr) {
        pChordArchive = AssetFolder::mount(cChordArchivePath, soundArena);
    }

    if (pMeArchive == nullptr) {
        pMeArchive = AssetFolder::mount(cMeArchivePath, soundArena);
    }

    AudSystem* system = AudNewAudSystem_(audioData, pChordArchive, pMeArchive, pRemixArchive, 1);
    previous->makeCurrent();

    return system;
}

AudSystem::AudSystem(SoundArchive* pSoundArchive, AssetFolder* pChordArchive, AssetFolder* pMeArchive, AssetFolder* pRemixArchive)
    : mIsResetReady(), mIsReset(), mStopClock(), mIsPaused(), mIsTrigSeBlocked(), mIsLevelSeBlocked(), mSeMuteFrames(-1), mSoundArchive(pSoundArchive),
      mSystemSeObject(), mSystemMeObject(), mSceneMgr(), mRhythmMeSystem(), mRemixMgr(), mAudEffector(),
      mVolumeController(), mHomeButtonMenuState(), mHomeButtonToggleTime(-1), mPauseMenuState(), mPauseMenuToggleTime(-1),
      mIsErrorWindowOpen(), mLimitedSoundInfo(), mSpkSystem() {
    msBasic = this;
    mLimitedSoundInfo = new (mSoundArchive->getArena(), 0) AudLimitedSoundInfo[2];
    clearAllLimitedSound();

    SeqSource* effects = pSoundArchive->getEffectSource();
    if (effects != nullptr) {
        getSeMgr().setSeqDataMgr(effects);
    }
    getSeqMgr().setSeqDataMgr(pSoundArchive);

    setAudience(&mAudience);

    // Every sound's start reads the table.
    if (mSoundArchive->getSoundTable() == nullptr) {
        NEBULA_PANIC("the sound archive has no sound table");
    }
    new (mSoundArchive->getArena(), 0) AudSoundInfo();

    initCategoryArrangement();
    initVolumeSetting();

    mSoundObjHolder = new (mSoundArchive->getArena(), 0) AudSoundObjHolder(mSoundArchive->getArena(), AudParams::numInspectableSoundObj);
    mSystemSeObject = new (mSoundArchive->getArena(), 0) AudSoundObject(nullptr, 10, mSoundArchive->getArena());
    mAtmosphereSeObject = new (mSoundArchive->getArena(), 0) AudSoundObject(nullptr, 8, mSoundArchive->getArena());
    mSceneMgr = new (mSoundArchive->getArena(), 0) AudSceneMgr(mSoundArchive);
    mRhythmMeSystem = new (mSoundArchive->getArena(), 0) AudRhythmMeSystem(mSoundArchive->getArena(), cRhythmParsers, true);
    mRemixMgr = new (mSoundArchive->getArena(), 0) AudRemixMgr(mSoundArchive->getArena());
    mRemixMgr->init();
    mSeMgr.setSeCalc(&AudSeStrategy::calc);
    mAudEffector = new (mSoundArchive->getArena(), 0) AudEffector();
    mAudEffector->initFxLine();

    if (pChordArchive != nullptr) {
        mRhythmMeSystem->mChordInfo.init(pChordArchive);
    }

    if (pMeArchive != nullptr) {
        setMeResource(pMeArchive, 1, 0);
    }

    if (pRemixArchive != nullptr) {
        mRemixMgr->setRemixSeqResource(pRemixArchive->getResource(static_cast< u16 >(0)));
    }

    mSystemMeObject = new (mSoundArchive->getArena(), 0) AudMeObject(nullptr, 8, mSoundArchive->getArena());
    mSpkSystem = new (mSoundArchive->getArena(), 0) SpkSystem(mSoundArchive->getArena());
    mVolumeController = new (mSoundArchive->getArena(), 0) AudSystemVolumeController(this);
}

AudChordInfo* AudSystem::getChordInfo() {
    return &mRhythmMeSystem->mChordInfo;
}

void AudSystem::setSpeakerResource(AssetFolder* pArchive) {
    mSpkSystem->setResource(pArchive, 0, 1);
}

void AudSystem::frameWork() {
    SoundSystem::frame();
    updateHomeButtonMenu();
    updatePauseMenu();
    seFanfareProcess();

    if (mVolumeController != nullptr) {
        mVolumeController->update();
    }

    mBgmMgr.movement();
    mRhythmMeSystem->update();
    mRemixMgr->update();
    SoundManager::calc();
    mixOut();
    mSoundObjHolder->update();

    if (mSpkSystem != nullptr) {
        mSpkSystem->framework();
    }

    if (mSeMuteFrames >= 0) {
        mSeMuteFrames--;
        if (mSeMuteFrames <= 0) {
            mSeMuteFrames = -1;
        }
    }

    updateLimitedSound();
}

void AudSystem::calc() {
    if (mIsReset && mReseter.checkDone()) {
        if (!mStopClock) {
            // Everything stops; what is still ending ends over the next frames, which come back here.
            SoundManager::stop(0);
            SoundManager::calc();
            mixOut();
        }

        return;
    }

    updateOutputMode();

    SoundManager::calc();
}

bool AudSystem::startSound(SoundID soundID, SoundHandle* pHandle, const Vec3f* pPos) {
    if (NebulaMods::sHasSoundEffects) {
        soundID = NebulaMods::soundFor(soundID, mSceneMgr != nullptr && mSceneMgr->isPlayerModeLuigi());
    }

    if (NebulaMods::sWanted[NEBULA_MOD_EVENT_SOUND_START]) {
        const s32 played = NebulaMods::dispatch(NEBULA_MOD_EVENT_SOUND_START, static_cast< s32 >(static_cast< u32 >(soundID)), nullptr,
                                                NebulaMods::soundName(soundID));
        if (played < 0) {
            return false;
        }
        soundID = SoundID(static_cast< u32 >(played));
    }

    if (mIsResetReady && soundID.getGroupID() != AudGroup_HomeSe) {
        return false;
    }

    if (mIsReset) {
        return false;
    }

    if (mSeMuteFrames >= 0 && soundID.getGroupID() != AudGroup_HomeSe && soundID.getGroupID() != AudGroup_SystemSe) {
        return false;
    }

    if (mIsTrigSeBlocked && soundID.getGroupID() != AudGroup_HomeSe && soundID.getGroupID() != AudGroup_SystemSe) {
        return false;
    }

    if (isSubmitSeByVolumeSetting(soundID)) {
        return false;
    }

    if (soundID.getGroupID() != AudGroup_HomeSe && mHomeButtonMenuState != MenuState_Off) {  // only the HOME menu's own while it is up
        return false;
    }

    if (!SoundManager::startSound(soundID, pHandle, pPos)) {
        return false;
    }

    switchToLuigi(soundID, pHandle);
    return true;
}

bool AudSystem::startLevelSound(SoundID soundID, SoundHandle* pHandle, const Vec3f* pPos) {
    if (NebulaMods::sHasSoundEffects) {
        soundID = NebulaMods::soundFor(soundID, mSceneMgr != nullptr && mSceneMgr->isPlayerModeLuigi());
    }

    if (NebulaMods::sWanted[NEBULA_MOD_EVENT_SOUND_START]) {
        const s32 played = NebulaMods::dispatch(NEBULA_MOD_EVENT_SOUND_START, static_cast< s32 >(static_cast< u32 >(soundID)), nullptr,
                                                NebulaMods::soundName(soundID));
        if (played < 0) {
            return false;
        }
        soundID = SoundID(static_cast< u32 >(played));
    }

    if (mIsResetReady && soundID.getGroupID() != AudGroup_HomeSe) {
        return false;
    }

    if (mIsReset) {
        return false;
    }

    if (mSeMuteFrames >= 0 && soundID.getGroupID() != AudGroup_HomeSe && soundID.getGroupID() != AudGroup_SystemSe) {
        return false;
    }

    if (mIsLevelSeBlocked && soundID.getGroupID() != AudGroup_HomeSe && soundID.getGroupID() != AudGroup_SystemSe) {
        return false;
    }

    if (isSubmitSeByVolumeSetting(soundID)) {
        return false;
    }

    if (soundID.getGroupID() != AudGroup_HomeSe && mHomeButtonMenuState != MenuState_Off) {  // only the HOME menu's own while it is up
        return false;
    }

    if (!SoundStarter::startLevelSound(soundID, pHandle, pPos)) {
        return false;
    }

    return true;
}

void AudSystem::switchToLuigi(SoundID soundID, SoundHandle* pHandle) {
    if (!mSceneMgr->isPlayerModeLuigi()) {
        return;
    }

    if (pHandle == nullptr) {
        return;
    }

    if (!pHandle->isSoundAttached()) {
        return;
    }

    if (soundID.getGroupID() != AudGroup_VoiceSe) {
        return;
    }

    PlayerLink* player = pHandle->mSound->getPlayer();
    if (player != nullptr) {
        player->writePort(PlayerLink::root(), 12, 20);
    }
}

bool AudSystem::isSubmitSeByVolumeSetting(SoundID soundID) {
    if (mVolumeController == nullptr) {
        return false;
    }

    if (mVolumeController->mCurrVolumeSet < 3 && mVolumeController->mCurrVolumeSet >= 1) {
        switch (soundID) {
        case SE_SY_TIMER_A_2:
        case SE_SY_TIMER_A_1:
        case SE_SY_TIMER_A_0:
            return true;
        }

        return false;
    }

    return false;
}

void AudSystem::updateOutputMode() {
    switch (NebulaSystemSettings::soundMode()) {
    case NebulaSystemSettings::SoundModeMono:
        SoundSystem::setOutputMode(0);
        break;
    case NebulaSystemSettings::SoundModeStereo:
        SoundSystem::setOutputMode(1);
        break;
    case NebulaSystemSettings::SoundModeSurround:
        SoundSystem::setOutputMode(2);
        break;
    }
}

void AudSystem::preProcessToReset() {
    mSeqMgr.stop(0);
    mStreamMgr.stop(0);
    mBgmMgr.clearLastBGM(AudBgmMgr::BgmType_Stage);
    mBgmMgr.clearLastBGM(AudBgmMgr::BgmType_Sub);
    mBgmMgr.clearNextBGM(AudBgmMgr::BgmType_Stage);
    mBgmMgr.clearNextBGM(AudBgmMgr::BgmType_Sub);

    if (mRhythmMeSystem != nullptr) {
        mRhythmMeSystem->mMeMgr->stopAll(-2);
    }

    mIsResetReady = true;
}

void AudSystem::resetAudio(u32 time, bool stopClock) {
    mReseter.start(time * SoundEngine::cTicksPerStep, stopClock);
    mStopClock = stopClock;
    mIsReset = true;
}

void AudSystem::resumeReset() {
    mReseter.resume();
    initParams();
    initVolumeSetting();
    if (mVolumeController != nullptr) {
        mVolumeController->init();
    }

    mIsTrigSeBlocked = false;
    mIsLevelSeBlocked = false;
    mHomeButtonMenuState = MenuState_Off;
    mHomeButtonToggleTime = -1;
    mPauseMenuState = MenuState_Off;
    mPauseMenuToggleTime = -1;
    mIsReset = false;
    mIsResetReady = false;
}

bool AudSystem::hasReset() const {
    return mIsReset && mReseter.checkDone() && (mStopClock || getNumActiveSounds() == 0);
}

void AudSystem::initSceneVolume() {
    unpause();
    if (mVolumeController != nullptr) {
        mVolumeController->init();
    }

    setSeVolumeSet(AudSystemVolumeController::VolumeSet_Normal, 0);
    mSeqMgr.getParams()->moveVolume(AudParams::masterVolBgm, 0);
    mStreamMgr.getParams()->moveVolume(AudParams::masterVolStream, 0);
    mPauseMenuState = MenuState_Off;
    mPauseMenuToggleTime = -1;
}

void AudSystem::pause() {
    if (mIsPaused) {
        return;
    }

    mBgmMgr.pause();
    mRhythmMeSystem->setIdle(true);
    mIsPaused = true;
}

void AudSystem::unpause() {
    mBgmMgr.unpause();
    mRhythmMeSystem->setIdle(false);
    mIsPaused = false;
}

void AudSystem::enterHomeButtonMenu() {
    mHomeButtonMenuState = MenuState_Enter;
    mHomeButtonToggleTime = 18 + 1;
    mSeqMgr.getParams()->moveVolume(0.0f, 18);
    mStreamMgr.getParams()->moveVolume(0.0f, 18);
    setVolumeZeroForce(MenuType_HomeButtonMenu);
    setSeVolumeSet(AudSystemVolumeController::VolumeSet_HomeButtonMenu, 18);
}

void AudSystem::exitHomeButtonMenu() {
    updateOutputMode();
    mHomeButtonMenuState = MenuState_Exit;
    mHomeButtonToggleTime = 20 + 1;

    if (!isPauseMenuActive()) {
        mSeqMgr.getParams()->moveVolume(AudParams::masterVolBgm, 20);
        mStreamMgr.getParams()->moveVolume(AudParams::masterVolStream, 18);
    }

    recoverSeVolumeSet(20);

    if (!isPauseMenuActive() && !mIsErrorWindowOpen) {
        unpause();
    }
}

void AudSystem::updateHomeButtonMenu() {
    switch (mHomeButtonMenuState) {
    case MenuState_Off:
        break;
    case MenuState_Enter:
        if (mHomeButtonToggleTime > 0 && --mHomeButtonToggleTime <= 0) {
            mHomeButtonToggleTime = -1;
            mHomeButtonMenuState = MenuState_Active;
            if (!isPauseMenuActive()) {
                pause();
            }
        }

        break;
    case MenuState_Exit:
        if (mHomeButtonToggleTime > 0 && --mHomeButtonToggleTime <= 0) {
            mHomeButtonToggleTime = -1;
            mHomeButtonMenuState = MenuState_Off;
        }

        break;
    case MenuState_Active:
        break;
    }
}

bool AudSystem::isHomeButtonMenuActive() const {
    if (mHomeButtonMenuState == MenuState_Off || mHomeButtonMenuState == MenuState_Exit) {
        return false;
    }

    return true;
}

void AudSystem::enterPauseMenu() {
    mPauseMenuState = MenuState_Enter;
    mPauseMenuToggleTime = 18 + 1;
    mSeqMgr.getParams()->moveVolume(0.0f, 18);
    mStreamMgr.getParams()->moveVolume(0.0f, 18);
    setVolumeZeroForce(MenuType_Pause);
    setSeVolumeSet(AudSystemVolumeController::VolumeSet_PauseMenu, 18);
}

// Nebula's cutscene skip (NebulaCutsceneSkip): the music and the game's sounds fade as for the pause menu, but nothing
// is paused, so a cutscene waiting for a sound to end still ends; the menu's own sounds play. They return over the
// picture's fade in (the game's 30 frames).
void AudSystem::muteForCutsceneSkip(bool mute) {
    if (mute) {
        mSeqMgr.getParams()->moveVolume(0.0f, 10);
        mStreamMgr.getParams()->moveVolume(0.0f, 10);
        setSeVolumeSet(AudSystemVolumeController::VolumeSet_PauseMenu, 10);
    } else {
        mSeqMgr.getParams()->moveVolume(AudParams::masterVolBgm, 30);
        mStreamMgr.getParams()->moveVolume(AudParams::masterVolStream, 30);
        recoverSeVolumeSet(30);
    }
}

// While the skipped cutscene runs unseen, every sound effect is silent, the system's too (its talks' page turns); they
// return with the picture.
void AudSystem::silenceSeForCutsceneSkip(bool silent) {
    mSeMgr.getParams()->moveVolume(silent ? 0.0f : AudParams::masterVolSe, silent ? 0 : 30);
}

// Quit to desktop (NebulaFileSelectButton): everything fades with the picture (the game's 30 frames).
void AudSystem::fadeOutForQuit() {
    mSeqMgr.getParams()->moveVolume(0.0f, 30);
    mStreamMgr.getParams()->moveVolume(0.0f, 30);
    mSeMgr.getParams()->moveVolume(0.0f, 30);
}

// Nebula's settings menu (NebulaSettingsMenu): as for the pause menu, the music fades out, the game's sounds end and the
// system's (the menu's own) play on; the menu stops the music once faded. On its close everything returns as it was.
// Over the pause or HOME Menu only the sound effects' volumes change, and that menu's close restores the rest.
// `stayFaded`: the music stays faded on the close, under the cutscene skip's question or its unseen run (the skip
// brings it back).
void AudSystem::holdForSettingsMenu(bool hold, bool stayFaded) {
    const bool otherMenu = isPauseMenuActive() || isHomeButtonMenuActive();
    if (hold) {
        if (!otherMenu) {
            mSeqMgr.getParams()->moveVolume(0.0f, 12);
            mStreamMgr.getParams()->moveVolume(0.0f, 12);
            setVolumeZeroForce(MenuType_Pause);
        }
        setSeVolumeSet(AudSystemVolumeController::VolumeSet_PauseMenu, 12);
        return;
    }
    if (!otherMenu && !stayFaded) {
        mSeqMgr.getParams()->moveVolume(AudParams::masterVolBgm, 20);
        mStreamMgr.getParams()->moveVolume(AudParams::masterVolStream, 20);
    }
    recoverSeVolumeSet(20);
}

void AudSystem::exitPauseMenu() {
    mPauseMenuState = MenuState_Exit;
    mPauseMenuToggleTime = 20 + 1;
    mSeqMgr.getParams()->moveVolume(AudParams::masterVolBgm, 20);
    mStreamMgr.getParams()->moveVolume(AudParams::masterVolStream, 20);
    recoverSeVolumeSet(20);
    unpause();
}

void AudSystem::updatePauseMenu() {
    switch (mPauseMenuState) {
    case MenuState_Off:
        break;
    case MenuState_Enter:
        if (mPauseMenuToggleTime > 0 && --mPauseMenuToggleTime <= 0) {
            mPauseMenuToggleTime = -1;
            mPauseMenuState = MenuState_Active;
            pause();
        }

        break;
    case MenuState_Exit:
        if (mPauseMenuToggleTime > 0 && --mPauseMenuToggleTime <= 0) {
            mPauseMenuToggleTime = -1;
            mPauseMenuState = MenuState_Off;
        }

        break;
    case MenuState_Active:
        break;
    }
}

bool AudSystem::isPauseMenuActive() const {
    if (mPauseMenuState == MenuState_Off || mPauseMenuState == MenuState_Exit) {
        return false;
    }

    return true;
}

bool AudSystem::isEnableStartSound(SoundID soundID) {
    if (mIsReset) {
        return false;
    }

    if (mIsResetReady && soundID.getGroupID() != AudGroup_HomeSe) {
        return false;
    }

    if (mSeMuteFrames >= 0 && soundID.getGroupID() != AudGroup_HomeSe && soundID.getGroupID() != AudGroup_SystemSe) {
        return false;
    }

    return true;
}

void AudSystem::pauseForErrorWindow() {
    mIsErrorWindowOpen = true;
    setVolumeZeroForce(MenuType_Error);
    mSeMgr.stop(10);
    pause();
}

void AudSystem::resumeFromErrorWindow() {
    if (!isPauseMenuActive() && !isHomeButtonMenuActive()) {
        unpause();
    }

    mIsErrorWindowOpen = false;
}

s32 AudSystem::getNumOfPlaying(SoundID soundID) {
    s32 count = 0;
    for (s32 category = 0; category < cEffectCategoryNum; category++) {
        for (NebulaListLink< EffectSound >* it = getSeMgr().getCategory(category)->getSeList()->getFirst(); it != getSeMgr().getCategory(0)->getSeList()->getEnd();
             it = it->getNext()) {
            if (it->getObject()->mSoundID == soundID) {
                count++;
            }
        }
    }

    return count;
}

u32 AudSystem::getPlayCountMin(SoundID soundID) {
    u32 min = -1;
    for (s32 category = 0; category < cEffectCategoryNum; category++) {
        for (NebulaListLink< EffectSound >* it = getSeMgr().getCategory(category)->getSeList()->getFirst(); it != getSeMgr().getCategory(0)->getSeList()->getEnd();
             it = it->getNext()) {
            if (it->getObject()->mSoundID == soundID) {
                if (it->getObject()->getCount() < min) {
                    min = it->getObject()->getCount();
                }
            }
        }
    }

    return min;
}

void AudSystem::setMicMtx(MtxPtr pMtx, s32 mic) {
    mAudience.getListener(mic).setMtx(pMtx);
}

const Vec3f& AudSystem::getMicPos(s32 mic) {
    return mAudience.getListener(mic).getPos();
}

void AudSystem::setFarCamera(bool isFar) {
    mAudience.mFarCamera = isFar;
}

void AudSystem::setSeVolumeSet(s32 volumeSet, u32 maxSteps) {
    if (mVolumeController != nullptr) {
        mVolumeController->setSeVolumeSetTrig(volumeSet, maxSteps);
    }
}

void AudSystem::recoverSeVolumeSet(u32 maxSteps) {
    if (mVolumeController != nullptr) {
        mVolumeController->recoverSeVolumeSet(maxSteps);
    }
}

void AudSystem::initVolumeSetting() {
    setSeVolumeSet(AudSystemVolumeController::VolumeSet_Normal, 0);
    mSeMgr.getParams()->moveVolume(AudParams::masterVolSe, 0);
    mSeqMgr.getParams()->moveVolume(AudParams::masterVolBgm, 0);
    mStreamMgr.getParams()->moveVolume(AudParams::masterVolStream, 0);
    if (mRhythmMeSystem != nullptr) {
        mRhythmMeSystem->mMeMgr->mMeVolume = 1.0f;
    }
}

void AudSystem::initCategoryArrangement() {
    CategoryArrangement arrangement;

    arrangement.mItems[0] = AudParams::arrangeSE_SY;
    arrangement.mItems[1] = AudParams::arrangeSE_PV;
    arrangement.mItems[2] = AudParams::arrangeSE_PM;
    arrangement.mItems[3] = AudParams::arrangeSE_BV;
    arrangement.mItems[4] = AudParams::arrangeSE_BM;
    arrangement.mItems[5] = AudParams::arrangeSE_OJ;
    arrangement.mItems[6] = AudParams::arrangeSE_AT;
    arrangement.mItems[7] = AudParams::arrangeSE_DM;
    arrangement.mItems[8] = AudParams::arrangeSE_EV;
    arrangement.mItems[9] = AudParams::arrangeSE_EM;
    arrangement.mItems[10] = AudParams::arrangeSE_SV;
    arrangement.mItems[11] = AudParams::arrangeSE_SM;
    arrangement.mItems[12] = AudParams::arrangeSE_RS;
    arrangement.mItems[13] = AudParams::arrangeHOMESE;

    mSeMgr.setCategoryArrangement(arrangement);
}

void AudSystem::setMeResource(AssetFolder* pArchive, u16 tableIndex, u16 seqIndex) {
    if (pArchive == nullptr) {
        return;
    }

    void* meTable = pArchive->getResource(tableIndex);
    const void* meSeq = pArchive->getResource(seqIndex);
    mRhythmMeSystem->mMeMgr->setMeTable(meTable);
    mRhythmMeSystem->mMeMgr->setMeSeq(meSeq);
}

void AudSystem::seFanfareProcess() {
    if (mBgmMgr.mBgm[AudBgmMgr::BgmType_Stage] != nullptr) {
        switch (mBgmMgr.mBgm[AudBgmMgr::BgmType_Stage]->getSoundID()) {
        case STM_BOSS_01_A:
        case STM_BOSS_02_A:
        case STM_BOSS_02_B:
        case STM_BOSS_03_A:
        case STM_BOSS_03_B:
        case STM_RACE_01:
        case STM_BOSS_04:
        case STM_BOSS_05_A:
        case STM_BOSS_06_A:
        case STM_BOSS_06_B:
        case STM_BOSS_01_B:
        case STM_BOSS_05_B:
        case STM_KINOPIO_TANKEN:
        case STM_BOSS_09_A:
        case STM_BOSS_09_B:
            return;
        }
    }

    if (mBgmMgr.mBgm[AudBgmMgr::BgmType_Sub] != nullptr) {
        switch (mBgmMgr.mBgm[AudBgmMgr::BgmType_Sub]->getSoundID()) {
        case BGM_MISS:
        case BGM_GAMEOVER:
        case BGM_PINCH_1:
            mBgmMgr.volDownLevel(true);
            return;
        }
    }

    bool firstMorph = false;
    if (getNumOfPlaying(SE_SY_MORPH) != 0) {
        if (mBgmMgr.mBgm[AudBgmMgr::BgmType_Sub] != nullptr) {
            if (mBgmMgr.mBgm[AudBgmMgr::BgmType_Sub]->getSoundID() == BGM_FIRST_MORPH_B) {
                firstMorph = true;
            }
        }
    }

    if (isFanfareSePlaying()) {
        mBgmMgr.volDownLevel(firstMorph);
    }
}

bool AudSystem::isFanfareSePlaying() {
    AudSoundInfo* info = AudSoundInfo::getInstance();
    for (s32 category = 0; category < cEffectCategoryNum; category++) {
        for (NebulaListLink< EffectSound >* it = getSeMgr().getCategory(category)->getSeList()->getFirst(); it != getSeMgr().getCategory(0)->getSeList()->getEnd();
             it = it->getNext()) {
            if (info->isFanfareSe(it->getObject()->mSoundID)) {
                return true;
            }
        }
    }

    return false;
}

void AudSystem::registerLimitedSound(SoundID soundID, s32 delay) {
    stopSoundID(soundID);

    for (s32 i = 0; i < 2; i++) {
        if (mLimitedSoundInfo[i].mSoundID == soundID) {
            return;
        }
    }

    for (s32 i = 0; i < 2; i++) {
        if (mLimitedSoundInfo[i].isFree()) {
            mLimitedSoundInfo[i].set(soundID, delay);
            break;
        }
    }
}

void AudSystem::clearAllLimitedSound() {
    for (s32 i = 0; i < 2; i++) {
        mLimitedSoundInfo[i].init();
    }
}

bool AudSystem::isRegisteredLimitedSound(SoundID soundID) {
    for (s32 i = 0; i < 2; i++) {
        if (mLimitedSoundInfo[i].mSoundID == soundID) {
            return true;
        }
    }

    return false;
}

void AudSystem::updateLimitedSound() {
    for (s32 i = 0; i < 2; i++) {
        mLimitedSoundInfo[i].update();
    }
}

bool AudSystem::isAlreadyPlayingSoundNear(SoundID soundID, const Vec3f* pPos, f32 range) {
    Vec3f pos;
    for (s32 category = 0; category < cEffectCategoryNum; category++) {
        for (NebulaListLink< EffectSound >* it = getSeMgr().getCategory(category)->getSeList()->getFirst(); it != getSeMgr().getCategory(0)->getSeList()->getEnd();
             it = it->getNext()) {
            if (it->getObject()->mSoundID != soundID) {
                continue;
            }

            bool valid;
            SoundAudible* audible = it->getObject()->mAudible;
            if (audible != nullptr) {
                pos.set(audible->getPos());
                valid = true;
            } else {
                valid = false;
            }

            if (!valid) {
                continue;
            }

            Vec3f diff(0.0f);
            diff.sub(pos, *pPos);
            if (diff.length() <= range && diff.length() != 0.0f) {
                return true;
            }
        }
    }

    return false;
}

void AudSystem::setVolumeZeroForce(s32 menuType) {
    for (s32 category = 0; category < cEffectCategoryNum; category++) {
        if (category == AudGroup_HomeSe) {
            continue;
        }

        if (menuType == MenuType_Pause && category == AudGroup_SystemSe) {
            continue;
        }

        mSeMgr.getCategory(category)->stop(18);
    }
}
