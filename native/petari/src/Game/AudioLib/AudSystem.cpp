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
#include "Game/Sound/StreamSource.hpp"
#include "Game/Sound/PlayerLink.hpp"
#include "Game/Sound/SoundParams.hpp"
#include "Game/Sound/SoundArchive.hpp"
#include "Game/Sound/SoundEngine.hpp"
#include "Game/Sound/SoundManager.hpp"
#include "Game/Sound/SoundSystem.hpp"
#include "Game/Sound/StdSoundInfo.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/AssetFolder.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/NebulaSystemSettings.hpp"
#include "Game/System/GameAssets.hpp"

const char sAudioResFile[] = "/AudioRes/SMR.szs";
const char sJaiSeqArc[] = "/AudioRes/Seqs/JaiSeq.arc";
const char sJaiChordArc[] = "/AudioRes/Info/JaiChord.arc";
const char sJaiMeArc[] = "/AudioRes/Info/JaiMe.arc";
const char sJaiRemixArc[] = "/AudioRes/Info/JaiRemixSeq.arc";
const char sJaiAudioArc[] = "AudioRes/SMR.baa";
const char sJaiSeqResArc[] = "/AudioRes/JaiSeq.arc";

AudSystem* AudSystem::msBasic;

// The beat readers: the game side and the audio thread's must have the same count.
static const u32 cRhythmParsers = 3;

AudSystem* AudNewAudSystem_(SoundArchive* pHeap, AssetFolder* pChordArchive, AssetFolder* pMeArchive, AssetFolder* pRemixArchive, int maxChannels) {
    AudSystem* system = new AudSystem(pHeap, pChordArchive, pMeArchive, pRemixArchive);
    system->mAudience.mMaxChannels = maxChannels;
    return system;
}

// The engine, then the archive's banks (the engine's before its clock runs), then the clock.
AudSystem* AudNewAudSystem(NebulaArena* pHeap, AssetFolder* pSeqArchive, AssetFolder* pChordArchive, AssetFolder* pMeArchive,
                           AssetFolder* pRemixArchive) {
    NebulaArena* currHeap = NebulaArena::getCurrentHeap();
    NebulaArena* soundHeap = NebulaArena::create(pHeap);
    SoundSystem::create(soundHeap, AudParams::outputGain);
    SoundArchive* audioData = new (soundHeap, 0) SoundArchive(soundHeap);
    soundHeap->becomeCurrentHeap();

    audioData->load(sAudioResFile);
    audioData->setAllWaves(AudSceneMgr::cPlayerVoiceBank);
    SoundEngine::get()->rhythm().setup(SoundEngine::get(), cRhythmParsers, soundHeap);
    SoundSystem::start();

    if (pSeqArchive == nullptr && GameAssets::find(sJaiSeqArc) >= 0) {
        pSeqArchive = AssetFolder::mount(sJaiSeqArc, currHeap);
    }
    audioData->setSeqArchive(pSeqArchive);

    if (pChordArchive == nullptr) {
        pChordArchive = AssetFolder::mount(sJaiChordArc, soundHeap);
    }

    if (pMeArchive == nullptr) {
        pMeArchive = AssetFolder::mount(sJaiMeArc, soundHeap);
    }

    AudSystem* system = AudNewAudSystem_(audioData, pChordArchive, pMeArchive, pRemixArchive, 1);
    currHeap->becomeCurrentHeap();

    return system;
}

static StdSoundInfo* newSoundInfo(SoundArchive* pAudioData) {
    return new (pAudioData->getHeap(), 0) AudSoundInfo();
}

AudSystem::AudSystem(SoundArchive* pSectionHeap, AssetFolder* pChordArchive, AssetFolder* pMeArchive, AssetFolder* pRemixArchive)
    : mIsResetReady(), mIsReset(), mStopThreads(), _828(), mIsPaused(), _82A(), _82B(), _82C(), _830(-1), mSoundInfo(), mSectionHeap(pSectionHeap),
      mAudible(), mSystemSeObject(), mSystemMeObject(), mSceneMgr(), mRhythmMeSystem(), mRemixMgr(), mAudEffector(),
      mSeStrategyMgr(), mVolumeController(), mHomeButtonMenuState(), mHomeButtonToggleTime(-1), mPauseMenuState(), mPauseMenuToggleTime(-1),
      mIsErrorWindowOpen(), mLimitedSoundInfo(), mSpkSystem() {
    msBasic = this;
    mLimitedSoundInfo = new (mSectionHeap->getHeap(), 0) AudLimitedSoundInfo[2];
    clearAllLimitedSound();

    SeqSource* effects = pSectionHeap->getEffectSource();
    if (effects != nullptr) {
        getSeMgr().setSeqDataMgr(effects);
    }
    getSeqMgr().setSeqDataMgr(pSectionHeap);

    setAudience(&mAudience);


    mSoundInfo = nullptr;
    SoundTable* soundTable = mSectionHeap->getSoundTable();
    if (soundTable != nullptr) {
        mSoundInfo = newSoundInfo(mSectionHeap);
    }

    if (mSoundInfo != nullptr) {
        mStreamMgr.setStreamDataMgr(mSoundInfo);
    }

    initCategoryArrangement();
    initVolumeSetting();

    mSoundObjHolder = new (mSectionHeap->getHeap(), 0) AudSoundObjHolder(mSectionHeap->getHeap(), AudParams::numInspectableSoundObj);
    mSystemSeObject = new (mSectionHeap->getHeap(), 0) AudSoundObject(nullptr, 10, mSectionHeap->getHeap());
    mAtmosphereSeObject = new (mSectionHeap->getHeap(), 0) AudSoundObject(nullptr, 8, mSectionHeap->getHeap());
    mSceneMgr = new (mSectionHeap->getHeap(), 0) AudSceneMgr(mSectionHeap);
    mRhythmMeSystem = new (mSectionHeap->getHeap(), 0) AudRhythmMeSystem(mSectionHeap->getHeap(), cRhythmParsers, true);
    mRemixMgr = new (mSectionHeap->getHeap(), 0) AudRemixMgr(mSectionHeap->getHeap());
    mRemixMgr->init();
    mSeStrategyMgr = new (mSectionHeap->getHeap(), 0) AudSeStrategyMgr();
    mSeMgr.setStrategyMgr(mSeStrategyMgr);
    mAudEffector = new (mSectionHeap->getHeap(), 0) AudEffector();
    mAudEffector->initFxLine(mSectionHeap->getHeap());

    if (pChordArchive != nullptr) {
        mRhythmMeSystem->mChordInfo.init(pChordArchive);
    }

    if (pMeArchive != nullptr) {
        setMeResource(pMeArchive, 1, 0);
    }

    if (pRemixArchive != nullptr) {
        mRemixMgr->setRemixSeqResource(pRemixArchive->getResource(static_cast< u16 >(0)));
    }

    mSystemMeObject = new (mSectionHeap->getHeap(), 0) AudMeObject(nullptr, 8, mSectionHeap->getHeap());
    mSpkSystem = new (mSectionHeap->getHeap(), 0) SpkSystem(mSectionHeap->getHeap());
    mVolumeController = new (mSectionHeap->getHeap(), 0) AudSystemVolumeController(this);
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

    if (_830 >= 0) {
        _830--;
        if (_830 <= 0) {
            _830 = -1;
        }
    }

    updateLimitedSound();
}

void AudSystem::calc() {
    if (mIsReset && mReseter.checkDone()) {
        if (!mStopThreads) {
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
    if (mIsResetReady && soundID.getGroupID() != 0xD) {  // HOMESE
        return false;
    }

    if (mIsReset) {
        return false;
    }

    if (_828) {
        return false;
    }

    if (_82A) {
        return false;
    }

    if (_830 >= 0 && soundID.getGroupID() != 0xD && soundID.getGroupID() != 0x0) {  // HOMESE, SE_SY
        return false;
    }

    if (_82B && soundID.getGroupID() != 0xD && soundID.getGroupID() != 0x0) {  // HOMESE, SE_SY
        return false;
    }

    if (isSubmitSeByVolumeSetting(soundID)) {
        return false;
    }

    if (soundID.getGroupID() != 0xD && mHomeButtonMenuState != 0) {  // disable non-home-menu sounds if menu is not cleared
        return false;
    }

    if (!SoundManager::startSound(soundID, pHandle, pPos)) {
        return false;
    }

    switchToLuigi(soundID, pHandle);
    return true;
}

bool AudSystem::startLevelSound(SoundID soundID, SoundHandle* pHandle, const Vec3f* pPos) {
    if (mIsResetReady && soundID.getGroupID() != 0xD) {  // HOMESE
        return false;
    }

    if (mIsReset) {
        return false;
    }

    if (_828) {
        return false;
    }

    if (_82A) {
        return false;
    }

    if (_830 >= 0 && soundID.getGroupID() != 0xD && soundID.getGroupID() != 0x0) {  // HOMESE, SE_SY
        return false;
    }

    if (_82C && soundID.getGroupID() != 0xD && soundID.getGroupID() != 0x0) {  // HOMESE, SE_SY
        return false;
    }

    if (isSubmitSeByVolumeSetting(soundID)) {
        return false;
    }

    if (soundID.getGroupID() != 0xD && mHomeButtonMenuState != 0) {  // disable non-home-menu sounds if menu is not cleared
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

    if (soundID.getGroupID() != 0x1) {  // SE_PV
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

void AudSystem::resetAudio(u32 time, bool stopThreads) {
    mReseter.start(time * SoundEngine::cTicksPerStep, stopThreads);
    mStopThreads = stopThreads;
    mIsReset = true;
}

void AudSystem::resumeReset() {
    mReseter.resume();
    initParams();
    initVolumeSetting();
    if (mVolumeController != nullptr) {
        mVolumeController->init();
    }

    _82A = false;
    _82B = false;
    _82C = false;
    mHomeButtonMenuState = MenuState_Off;
    mHomeButtonToggleTime = -1;
    mPauseMenuState = MenuState_Off;
    mPauseMenuToggleTime = -1;
    mIsReset = false;
    mIsResetReady = false;
}

bool AudSystem::hasReset() const {
    return mIsReset && mReseter.checkDone() && (mStopThreads || getNumActiveSounds() == 0);
}

void AudSystem::initSceneVolume() {
    unpause();
    if (mVolumeController != nullptr) {
        mVolumeController->init();
    }

    setSeVolumeSet(AudSystemVolumeController::VolumeSet_0, 0);
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

// Quit to desktop (NebulaQuitButton): everything fades with the picture (the game's 30 frames).
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

    if (_828) {
        return false;
    }

    if (mIsResetReady && soundID.getGroupID() != 0xD) {  // HOMESE
        return false;
    }

    if (_830 >= 0 && soundID.getGroupID() != 0xD && soundID.getGroupID() != 0x0) {  // HOMESE, SE_SY
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
    for (s32 category = 0; category < EffectManager::NUM_CATEGORIES; category++) {
        for (JSULink< EffectSound >* it = getSeMgr().getCategory(category)->getSeList()->getFirst(); it != getSeMgr().getCategory(0)->getSeList()->getEnd();
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
    for (s32 category = 0; category < EffectManager::NUM_CATEGORIES; category++) {
        for (JSULink< EffectSound >* it = getSeMgr().getCategory(category)->getSeList()->getFirst(); it != getSeMgr().getCategory(0)->getSeList()->getEnd();
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
    mAudience.mStates[mic].setMtx(pMtx);
}

const Vec3f& AudSystem::getMicPos(s32 mic) {
    return mAudience.mStates[mic].getPos();
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
    setSeVolumeSet(AudSystemVolumeController::VolumeSet_0, 0);
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

void AudSystem::setMeResource(AssetFolder* pArchive, u16 a1, u16 a2) {
    if (pArchive == nullptr) {
        return;
    }

    void* meTable = pArchive->getResource(a1);
    const void* meSeq = pArchive->getResource(a2);
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
    for (s32 category = 0; category < EffectManager::NUM_CATEGORIES; category++) {
        for (JSULink< EffectSound >* it = getSeMgr().getCategory(category)->getSeList()->getFirst(); it != getSeMgr().getCategory(0)->getSeList()->getEnd();
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
    for (s32 category = 0; category < EffectManager::NUM_CATEGORIES; category++) {
        for (JSULink< EffectSound >* it = getSeMgr().getCategory(category)->getSeList()->getFirst(); it != getSeMgr().getCategory(0)->getSeList()->getEnd();
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
    for (s32 category = 0; category < EffectManager::NUM_CATEGORIES; category++) {
        if (category == 0xD) {  // HOMESE
            continue;
        }

        if (menuType == MenuType_Pause && category == 0x0) {  // pause and SE_SY
            continue;
        }

        mSeMgr.getCategory(category)->stop(18);
    }
}
