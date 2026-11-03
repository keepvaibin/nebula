#pragma once

#include <JSystem/JAudio2/JAISound.hpp>

class AudLimitedSoundInfo {
public:
    /// @brief Creates a new `AudLimitedSoundInfo`.
    AudLimitedSoundInfo();

    void init();
    void set(JAISoundID, s32);
    void update();
    bool isFree();

    JAISoundID mSoundID;
    s32 mDelay;
};
