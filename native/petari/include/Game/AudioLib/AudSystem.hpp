#pragma once

#include "Game/AudioLib/AudBgmMgr.hpp"
#include "Game/AudioLib/AudLimitedSound.hpp"
#include "Game/Sound/SoundAudible.hpp"
#include "Game/Sound/SoundAudience.hpp"
#include "Game/Sound/SoundSystem.hpp"
#include "Game/Sound/SoundManager.hpp"
#include <nebula_geometry.hpp>

class AudChordInfo;
class AudEffector;
class AudMeObject;
class AudRemixMgr;
class AudRhythmMeSystem;
class AudSceneMgr;
class AudSoundObjHolder;
class AudSoundObject;
class AudSystemVolumeController;
class SoundHandle;
class SoundID;
class SoundArchive;
class AssetFolder;
class NebulaArena;
class SpkSystem;

// SoundID groups (an id's top byte), which are also the SE categories; AudSoundId.hpp lists each group's sounds.
enum AudGroup {
    AudGroup_SystemSe = 0x0,  // SE_SY
    AudGroup_VoiceSe = 0x1,   // SE_PV, the player's voice
    AudGroup_HomeSe = 0xD,    // HOMESE, the HOME menu's
};

// The sound data and the sequence, chord, ME and remix archives in the store.
extern const char cSoundDataPath[];
extern const char cSeqArchivePath[];
extern const char cChordArchivePath[];
extern const char cMeArchivePath[];
extern const char cRemixArchivePath[];

class AudSystem : public SoundManager {
public:
    enum MenuState {
        MenuState_Off = 0,
        MenuState_Enter = 1,
        MenuState_Exit = 2,
        MenuState_Active = 3,
    };

    enum MenuType {
        MenuType_HomeButtonMenu = 0,
        MenuType_Pause = 1,
        MenuType_Error = 2,
    };

    AudSystem(SoundArchive*, AssetFolder*, AssetFolder*, AssetFolder*);

    virtual void frameWork();
    virtual void calc();
    virtual bool startSound(SoundID, SoundHandle*, const Vec3f*);
    virtual bool startLevelSound(SoundID, SoundHandle*, const Vec3f*);

    AudChordInfo* getChordInfo();
    void setSpeakerResource(AssetFolder*);
    void switchToLuigi(SoundID, SoundHandle*);
    bool isSubmitSeByVolumeSetting(SoundID);
    void updateOutputMode();
    void preProcessToReset();
    void resetAudio(u32, bool);
    void resumeReset();
    bool hasReset() const;
    void initSceneVolume();
    void pause();
    void unpause();
    void enterHomeButtonMenu();
    void exitHomeButtonMenu();
    void updateHomeButtonMenu();
    bool isHomeButtonMenuActive() const;
    void enterPauseMenu();
    void muteForCutsceneSkip(bool mute);
    void silenceSeForCutsceneSkip(bool silent);
    void fadeOutForQuit();
    void holdForSettingsMenu(bool hold, bool stayFaded);
    // LumaCore's world clock (services/time.md 7): the world's sound held (a mod holds the world, or its rate is beyond
    // what the sound follows) or given back.
    void holdForWorld(bool hold);
    void updateWorldHold();
    void keepWorldHold();
    void exitPauseMenu();
    void updatePauseMenu();
    bool isPauseMenuActive() const;
    bool isEnableStartSound(SoundID);
    void pauseForErrorWindow();
    void resumeFromErrorWindow();
    s32 getNumOfPlaying(SoundID);
    u32 getPlayCountMin(SoundID);
    void setMicMtx(MtxPtr, s32);
    const Vec3f& getMicPos(s32);
    void setFarCamera(bool);
    void setSeVolumeSet(s32, u32);
    void recoverSeVolumeSet(u32);
    void initVolumeSetting();
    void initCategoryArrangement();
    void setMeResource(AssetFolder*, u16, u16);
    void seFanfareProcess();
    bool isFanfareSePlaying();
    void registerLimitedSound(SoundID, s32);
    void clearAllLimitedSound();
    bool isRegisteredLimitedSound(SoundID);
    void updateLimitedSound();
    bool isAlreadyPlayingSoundNear(SoundID, const Vec3f*, f32);
    void setVolumeZeroForce(s32);

    // Sounds other than the system groups stay silent for this many frames.
    void setSeMuteFrames(s32 frames) {
        mSeMuteFrames = frames;
    }

    AudEffector* getAudEffector() const {
        return mAudEffector;
    }

    static AudSystem* msBasic;

    static AudSystem* get() {
        return msBasic;
    }

    bool mIsResetReady;
    bool mIsReset;
    bool mStopClock;  // the reset stops the engine's clock (a restart or quit): nothing waits for sounds to end
    SoundReset mReseter;
    bool mIsPaused;
    bool mIsTrigSeBlocked;  // blocks startSound
    bool mIsLevelSeBlocked;  // blocks startLevelSound
    s32 mSeMuteFrames;
    SoundArchive* mSoundArchive;
    SoundAudience mAudience;
    AudSoundObject* mSystemSeObject;
    AudSoundObject* mAtmosphereSeObject;
    AudMeObject* mSystemMeObject;
    AudSceneMgr* mSceneMgr;
    AudBgmMgr mBgmMgr;
    AudSoundObjHolder* mSoundObjHolder;
    AudRhythmMeSystem* mRhythmMeSystem;
    AudRemixMgr* mRemixMgr;
    AudEffector* mAudEffector;
    AudSystemVolumeController* mVolumeController;
    s32 mHomeButtonMenuState;
    s32 mHomeButtonToggleTime;
    s32 mPauseMenuState;
    s32 mPauseMenuToggleTime;
    bool mIsErrorWindowOpen;
    // The world's hold (holdForWorld): held now; the settings menu holds too; the music was paused by it; frames left of
    // the fade before the pause (-1 none).
    bool mWorldHeld = false;
    bool mSettingsHeld = false;
    bool mPausedByWorld = false;
    s32 mWorldHoldTime = -1;
    AudLimitedSoundInfo* mLimitedSoundInfo;
    SpkSystem* mSpkSystem;
};

AudSystem* AudNewAudSystem(NebulaArena*, AssetFolder*, AssetFolder*, AssetFolder*, AssetFolder*);
AudSystem* AudNewAudSystem_(SoundArchive*, AssetFolder*, AssetFolder*, AssetFolder*, int);
