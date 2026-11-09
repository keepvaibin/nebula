#pragma once

#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/Sound/SoundObject.hpp"
#include "Game/System/NebulaArena.hpp"

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

class AudSoundObject : public SoundObject, ArenaOwned {
public:
    AudSoundObject(Vec3f*, u8, NebulaArena*);

    SoundHandle* startSound(SoundID);
    SoundHandle* startLevelSound(SoundID);
    virtual ~AudSoundObject();

    void addToSoundObjHolder();
    void setTrans(Vec3f*);
    bool writePort(SoundHandle*, u32, u16);
    bool isLimitedSound(SoundID);
    SoundHandle* startLevelSound(const char*);
    SoundHandle* startSoundParam(SoundID, s32 level, s32 subLevel);
    SoundHandle* startLevelSoundParam(SoundID, s32 level, s32 subLevel);
    SoundHandle* startLevelSoundParam(const char*, s32 level, s32 subLevel);
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
    void modifySe(SoundHandle*, s32 level, s32 subLevel);

    bool isEnableStartSound(SoundID soundID) {
        return AudWrap::getSystem()->isEnableStartSound(soundID);
    }

    u32 mSeVersion;  // Which variant of the owner's sounds plays; each actor class names its versions.
    s32 mMapCode;
    s32 mMapCodeExtra;
};
