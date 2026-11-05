#pragma once

#include "Game/AudioLib/AudAudience.hpp"
#include "Game/AudioLib/AudBgmMgr.hpp"
#include "Game/AudioLib/AudLimitedSound.hpp"
#include <JSystem/JAudio2/JAIAudible.hpp>
#include <JSystem/JAudio2/JAIAudience.hpp>
#include <JSystem/JAudio2/JAUStdSoundInfo.hpp>
#include <JSystem/JAudio2/JASAudioReseter.hpp>
#include <JSystem/JAudio2/JAUSoundMgr.hpp>
#include <JSystem/JGeometry/TMatrix.hpp>

class AudChordInfo;
class AudEffector;
class AudMeObject;
class AudRemixMgr;
class AudRhythmMeSystem;
class AudSceneMgr;
class AudSeStrategyMgr;
class AudSoundObjHolder;
class AudSoundObject;
class AudSystemVolumeController;
class JAISoundHandle;
class JAISoundID;
class JAUAudioData;
class AssetFolder;
class NebulaArena;
class SpkSystem;

extern const char sAudioResFile[];
extern const char sJaiSeqArc[];
extern const char sJaiChordArc[];
extern const char sJaiMeArc[];
extern const char sJaiRemixArc[];
extern const char sJaiAudioArc[];
extern const char sJaiSeqResArc[];

class AudSystem : public JAUSoundMgr {
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

    AudSystem(JAUAudioData*, AssetFolder*, AssetFolder*, AssetFolder*);

    virtual void frameWork();
    virtual void calc();
    virtual bool startSound(JAISoundID, JAISoundHandle*, const TVec3f*);
    virtual bool startLevelSound(JAISoundID, JAISoundHandle*, const TVec3f*);

    AudChordInfo* getChordInfo();
    void setSpeakerResource(AssetFolder*);
    void switchToLuigi(JAISoundID, JAISoundHandle*);
    bool isSubmitSeByVolumeSetting(JAISoundID);
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
    void exitPauseMenu();
    void updatePauseMenu();
    bool isPauseMenuActive() const;
    bool isEnableStartSound(JAISoundID);
    void pauseForErrorWindow();
    void resumeFromErrorWindow();
    s32 getNumOfPlaying(JAISoundID);
    u32 getPlayCountMin(JAISoundID);
    void setMicMtx(MtxPtr, s32);
    const TVec3f& getMicPos(s32);
    void setFarCamera(bool);
    void setSeVolumeSet(s32, u32);
    void recoverSeVolumeSet(u32);
    void initVolumeSetting();
    void initCategoryArrangement();
    void setMeResource(AssetFolder*, u16, u16);
    void seFanfareProcess();
    bool isFanfareSePlaying();
    void registerLimitedSound(JAISoundID, s32);
    void clearAllLimitedSound();
    bool isRegisteredLimitedSound(JAISoundID);
    void updateLimitedSound();
    bool isAlreadyPlayingSoundNear(JAISoundID, const TVec3f*, f32);
    void setVolumeZeroForce(s32);

    void set830(u32 var) {
        _830 = var;
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
    bool mStopThreads;
    JASAudioReseter mReseter;
    bool _828;
    bool mIsPaused;
    bool _82A;
    bool _82B;  // blocks startSound
    bool _82C;  // blocks startLevelSound
    s32 _830;
    JAUStdSoundInfo* mSoundInfo;
    JAUAudioData* mSectionHeap;
    AudAudience< 4 > mAudience;
    AudDopplerAudible< 4 >* mAudible;  // TODO: should this be merged into AudAudience<>??
    AudSoundObject* mSystemSeObject;
    AudSoundObject* mAtmosphereSeObject;
    AudMeObject* mSystemMeObject;
    AudSceneMgr* mSceneMgr;
    AudBgmMgr mBgmMgr;
    AudSoundObjHolder* mSoundObjHolder;
    AudRhythmMeSystem* mRhythmMeSystem;
    AudRemixMgr* mRemixMgr;
    AudEffector* mAudEffector;
    AudSeStrategyMgr* mSeStrategyMgr;
    AudSystemVolumeController* mVolumeController;
    s32 mHomeButtonMenuState;
    s32 mHomeButtonToggleTime;
    s32 mPauseMenuState;
    s32 mPauseMenuToggleTime;
    bool mIsErrorWindowOpen;
    AudLimitedSoundInfo* mLimitedSoundInfo;
    SpkSystem* mSpkSystem;
};

AudSystem* AudNewAudSystem(NebulaArena*, void*, AssetFolder*, AssetFolder*, AssetFolder*, AssetFolder*);
AudSystem* AudNewAudSystem_(JAUAudioData*, AssetFolder*, AssetFolder*, AssetFolder*, int);
