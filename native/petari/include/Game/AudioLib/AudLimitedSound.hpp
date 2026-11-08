#pragma once

#include "Game/Sound/Sound.hpp"

class AudLimitedSoundInfo {
public:
    AudLimitedSoundInfo();

    void init();
    void set(SoundID, s32);
    void update();
    bool isFree();

    SoundID mSoundID;
    s32 mDelay;
};
