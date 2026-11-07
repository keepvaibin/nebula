#pragma once

#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/Sound/SoundObject.hpp"
#include <JSystem/JKernel/JKRDisposer.hpp>

class SoundID;
class SoundHandle;

enum SE_GROUP {
    SE_SYSTEM = 0x00,
    SE_PLAYER_VOICE = 0x01,
    SE_PLAYER_MOTION = 0x02,
    SE_BOSS_VOICE = 0x03,
    SE_BOSS_MOTION = 0x04,
    SE_OBJECT = 0x05,
    SE_ATMOSPHERE = 0x06,
    SE_DEMO = 0x07,
    SE_ENEMY_VOICE = 0x08,
    SE_ENEMY_MOTION = 0x09,
    SE_SUPPORTER_VOICE = 0x0A,
    SE_SUPPORTER_MOTION = 0x0B,
    SE_REMIX_SEQ = 0x0C,
    SE_HOME_BUTTON_MENU = 0x0D,
};

enum BGM_GROUP {
    MAIN_BGM = 0x00,
    MULTI_BGM = 0x01,
};

class AudSoundObjHashData {
public:
    AudSoundObjHashData();

    void init();

    const char* mName;
    u32 mHash;
    SoundID mID;
};

class AudSoundObject : public SoundObject, JKRDisposer {
public:
    AudSoundObject(Vec3f*, u8, NebulaArena*);

    virtual SoundHandle* startSound(SoundID);
    virtual SoundHandle* startLevelSound(SoundID);
    virtual ~AudSoundObject();

    void addToSoundObjHolder();
    void setTrans(Vec3f*);
    bool writePort(SoundHandle*, u32, u16);
    bool isLimitedSound(SoundID);
    SoundHandle* startLevelSound(const char*);
    SoundHandle* startSoundParam(SoundID, s32, s32);
    SoundHandle* startLevelSoundParam(SoundID, s32, s32);
    SoundHandle* startLevelSoundParam(const char*, s32, s32);
    bool modifyLimitedSound(SoundID);
    bool isPlayingID(SoundID);
    void releaseHandle(SoundID);
    void setMapCode(s32);
    void setMapCodeExtra(s32);
    s32 getMapCode() const;
    void clearMapCode();
    SoundID convertNameToLevelSEID(const char*);
    void limitVoiceOne(SoundID);
    void stopCategorySound(u32, u32);
    void setMapCodeToPort(SoundHandle*, SoundID);
    void setCutoffToPort(SoundHandle*, SoundID);
    SoundID convertSoundIdFromSeVersion(SoundID) const;
    void modifySe_Kawamura(SoundHandle*, s32);
    bool modifyLimitedSound_Kawamura(SoundID);
    void modifySe_Takezawa(SoundHandle*, s32, s32);
    bool modifyLimitedSound_Takezawa(SoundID);
    void modifySe_Gohara(SoundHandle*, s32, s32);

    bool isEnableStartSound(SoundID soundID) {
        return AudWrap::getSystem()->isEnableStartSound(soundID);
    }

    u32 mSeVersion;  // TODO: Kuribo and Metbo "_DARK"?
    s32 mMapCode;
    s32 mMapCodeExtra;
    AudSoundObjHashData* mHashDatas;
    s32 mNumHandles;
    s32 mNumSounds;
};
