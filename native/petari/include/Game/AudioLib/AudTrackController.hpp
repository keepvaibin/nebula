#pragma once

#include "Game/AudioLib/AudFader.hpp"

class SoundHandle;

class AudTrackController {
public:
    AudTrackController();

    void start(SoundHandle*);
    void stop();
    void mute();
    void unmute();
    void setMuteState(u8, s32, bool);
    void update();
    void muteIfVolumeZero();

    s32 mTrackNo;
    SoundHandle* mHandle;
    AudFader mFader;
    f32 mVolume;
    bool mAutoMute;
};
