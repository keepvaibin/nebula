#pragma once


class AudBgm;
class AudBgmMgr;
class AudMeObject;
class AudRemixMgr;
class AudRemixSequencer;
class AudRhythmMeSystem;
class AudSceneMgr;
class AudSoundInfo;
class AudSoundNameConverter;
class AudSoundObjHolder;
class AudSoundObject;
class AudSystem;
class SoundHandle;

namespace AudWrap {
    AudSystem* getSystem();
    AudSoundInfo* getSoundInfo();
    AudSceneMgr* getSceneMgr();
    AudBgmMgr* getBgmMgr();
    AudBgm* getStageBgm();
    AudBgm* getSubBgm();
    SoundHandle* startStageBgm(u32, bool);
    SoundHandle* startSubBgm(u32, bool);
    void setNextIdStageBgm(u32);
    SoundHandle* startLastStageBgm();
    AudSoundObject* getSystemSeObject();
    AudSoundObject* getAtmosphereSeObject();
    AudSoundObjHolder* getSoundObjHolder();
    AudRhythmMeSystem* getRhythmMeSystem();
    AudMeObject* getSystemMeObject();
    AudRemixMgr* getRemixMgr();
    AudRemixSequencer* getRemixSequencer();
    AudSoundObject* getRemixSeqObject();
};  // namespace AudWrap
