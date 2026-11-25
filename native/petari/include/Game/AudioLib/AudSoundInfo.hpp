#pragma once

#include "Game/Sound/StdSoundInfo.hpp"

// The game's reading of the sound table: the sound switches.
class AudSoundInfo : public StdSoundInfo {
public:

    // An effect's or a music sequence's switches; none for another sound.
    SoundSwitches getSwitches(SoundID) const;
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
