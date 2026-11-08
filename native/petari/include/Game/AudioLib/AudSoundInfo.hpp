#pragma once

#include "Game/Sound/StdSoundInfo.hpp"

// The game's reading of the sound table: the sound switches' bits.
class AudSoundInfo : public StdSoundInfo {
public:

    u16 getSoundSw(SoundID) const;
    bool isLimitNumPlaying(SoundID) const;
    bool isLimitCountPlaying(SoundID) const;
    u16 getChordResIdBgm(SoundID) const;
    bool isRhythmBgm(SoundID) const;
    bool isFanfareSe(SoundID) const;
    bool isUseFxMix(SoundID) const;
    bool isUseLpf(SoundID) const;

    static AudSoundInfo* getInstance() {
        return static_cast< AudSoundInfo* >(StdSoundInfo::getInstance());
    }
};
