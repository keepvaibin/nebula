#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

class AudSeKeeper;
class AudSoundObject;
class SoundHandle;
class SoundID;
class LiveActor;

namespace MR {
    enum BossBgmID {
        BossBgmID_01_A,
        BossBgmID_01_B,
        BossBgmID_02_A,
        BossBgmID_02_B,
        BossBgmID_03_A,
        BossBgmID_03_B,
        BossBgmID_05_A,
        BossBgmID_05_B,
        BossBgmID_Begoman = BossBgmID_01_A,
        BossBgmID_StinkBugA = BossBgmID_01_A,
        BossBgmID_StinkBugB = BossBgmID_01_B,
        BossBgmID_DinoPackunA = BossBgmID_03_A,
        BossBgmID_DinoPackunB = BossBgmID_03_B,
        BossBgmID_DodoryuA = BossBgmID_05_A,
        BossBgmID_DodoryuB = BossBgmID_05_B,
        BossBgmID_TombSpiderA = BossBgmID_03_A,
        BossBgmID_TombSpiderB = BossBgmID_03_B,
        BossBgmID_TripodBossA = BossBgmID_02_A,
        BossBgmID_TripodBossB = BossBgmID_02_B,
    };

    enum EventBgmID {
        EventBgmID_Hurry,
        EventBgmID_Chase,
        EventBgmID_Race_01,
        EventBgmID_Race_02,
        EventBgmID_PenguinRace = EventBgmID_Race_01,
        EventBgmID_TeresaRace = EventBgmID_Race_02,
        EventBgmID_Surfing = EventBgmID_Race_01,
    };

    // level, subLevel: the values the sound's table reads (-1: plain start); lifeTime: frames a level sound
    // lasts without a restart (-1: one frame); fadeTime: stop fade in frames.
    SoundHandle* startSystemSE(const char* pName, s32 level = -1, s32 subLevel = -1);
    SoundHandle* startSystemSE(SoundID id, s32 level = -1, s32 subLevel = -1);
    SoundHandle* startSystemLevelSE(const char*, s32 level = -1, s32 subLevel = -1);
    SoundHandle* startSystemLevelSE(SoundID, s32 level = -1, s32 subLevel = -1);
    void stopSystemSE(const char* pName, u32 fadeTime = 0);
    void stopSystemSE(SoundID id, u32 fadeTime = 0);
    SoundHandle* startAtmosphereSE(const char* pName, s32 level = -1, s32 subLevel = -1);
    SoundHandle* startAtmosphereLevelSE(const char* pName, s32 level = -1, s32 subLevel = -1);
    SoundHandle* startSoundObject(AudSoundObject* pSoundObject, const char* pName);
    SoundHandle* startSoundObjectLevel(AudSoundObject* pSoundObject, const char* pName, s32 lifeTime = -1);
    SoundHandle* startSoundObjectLevel(AudSoundObject* pSoundObject, SoundID id, s32 lifeTime = -1);
    SoundHandle* startSoundObjectLevelParam(AudSoundObject* pSoundObject, const char* pName, s32 level = -1, s32 subLevel = -1, s32 lifeTime = -1);
    SoundHandle* startSoundObjectLevelParam(AudSoundObject* pSoundObject, SoundID id, s32 level = -1, s32 subLevel = -1, s32 lifeTime = -1);
    SoundHandle* startSound(const LiveActor*, const char*, s32 level = -1, s32 subLevel = -1);
    SoundHandle* startSound(const LiveActor*, SoundID, s32 level = -1, s32 subLevel = -1);
    SoundHandle* startSoundSeVer(const LiveActor* pActor, const char* pName, s32 level = -1, s32 subLevel = -1);
    SoundHandle* startLevelSound(const LiveActor* pActor, const char* pName, s32 level = -1, s32 subLevel = -1, s32 lifeTime = -1);
    SoundHandle* startLevelSound(const LiveActor* pActor, SoundID id, s32 level = -1, s32 subLevel = -1, s32 lifeTime = -1);
    SoundHandle* startLevelSoundSeVer(const LiveActor* pActor, const char* pName, s32 level = -1, s32 subLevel = -1, s32 lifeTime = -1);
    void stopSound(const LiveActor* pActor, const char* pName, u32 fadeTime = 0);
    void stopSound(const LiveActor* pActor, SoundID id, u32 fadeTime = 0);
    void setSeVersion(const LiveActor*, u32 version);
    void releaseSoundHandle(const LiveActor*, const char*);
    void startBas(const LiveActor*, const char*, bool reversed, f32 loopStartFrame, f32 loopEndFrame);
    void actorSoundMovement(LiveActor*);
    void addJointToSeKeeper(AudSeKeeper*, const char* pJointName, const char* pJointName2);
    void startLevelSoundSeKeeper(AudSeKeeper*, const char* pJointName, const char* pSoundName);
    bool hasME();
    void startSystemME(const char*);
    s32 getMapSoundCodeFoot(const LiveActor*);
    void setMapSondCodeGravity(const LiveActor*, s32);
    void startTalkSound(u8, const LiveActor*);
    void startRemixSound(s32 melodyNo, s32 noteNo, f32 beatLength);
    s32 getRemixMelodyNoteNum(s32);
    void limitedSound(const char*, s32 delay);
    SoundHandle* startStageBGM(const char*, bool lock);
    SoundHandle* startStageBGMFromStageName(const char*, const char*, s32);
    void stopStageBGM(u32 fadeTime);
    void unlockStageBGM();
    void setNextStageBGM(const char*);
    void moveVolumeStageBGM(f32 volume, u32 fadeTime);
    void moveVolumeStageBGMForNoteFairy(f32 volume, u32 fadeTime);
    SoundHandle* startLastStageBGM();
    bool isPlayingStageBgm();
    bool isPlayingStageBgmID(u32);
    bool isPlayingStageBgmName(const char*);
    bool isStopOrFadeoutStageBgmID(u32);
    bool isStopOrFadeoutBgmName(const char*);
    bool isPreparedStageBgm();
    void setStageBGMState(s32 state, u32 time);
    void setStageBGMStateBit(u32);
    SoundHandle* startSubBGM(const char*, bool lock);
    void stopSubBGM(u32 fadeTime);
    void moveVolumeSubBGM(f32 volume, u32 fadeTime);
    bool isPlayingSubBgmID(u32);
    void playLevelMarioPinchBGM(bool secondPinch);
    void stopMarioPinchBGMSoon();
    void submitTrigSE();
    void permitTrigSE();
    void submitLevelSE();
    void permitLevelSE();
    void submitSE();
    void permitSE();
    bool isPermitSE();
    void setCubeBgmChangeInvalid();
    bool isCubeBgmChangeInvalid();
    void clearBgmQueue();
    void setMicMtx(MtxPtr, s32 mic);
    void setSoundVolumeSetting(s32 volumeSet, u32 maxSteps);
    void recoverSoundVolumeSetting(u32 maxSteps);
    void setAudioEffectType(s32 effectType, s32 priority);
    void startCSSound(const char*, const char*, s32 padChannel);
    void startCSSound2P(const char*, const char*);
    void startSpinHitSound(const LiveActor*);
    void startBlowHitSound(const LiveActor*);
    void startPointerHitSound();
    void startPointerFreezeLevelSound(const LiveActor*);
    void start2PJumpAssistSound();
    void start2PJumpAssistJustSound();
    void start2PAttackAssistSound();
    void limitedStarPieceHitSound();
    void startCurrentStageBGM();
    SoundHandle* startBossBGM(s32);
    SoundHandle* startEventBGM(s32);
    SoundHandle* startAfterBossBGM();
};  // namespace MR
