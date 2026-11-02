#pragma once

#include "Game/AudioLib/AudFader.hpp"

class JAISoundHandle;

class AudTrackController {
public:
    /// @brief Creates a new `AudTrackController`.
    AudTrackController();

    ~AudTrackController() {};

    void start(JAISoundHandle*);
    void stop();
    void mute();
    void unmute();
    void setMuteState(u8, s32, bool);
    void update();
    void muteIfVolumeZero();

    s32 mTrackNo;
    JAISoundHandle* mHandle;
    AudFader mFader;
    f32 mVolume;
    bool mAutoMute;
};
