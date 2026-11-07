#pragma once


class SoundID;

namespace AudStageBgmWrap {
    SoundID changeStageNameToSoundID(const char*, const char*, s32);
    SoundID getCometEventBgm(const char*);
    SoundID getKoopaFortressAppearBgm(const char*);
};  // namespace AudStageBgmWrap
