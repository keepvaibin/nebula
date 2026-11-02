#pragma once

#include "Game/AudioLib/AudFader.hpp"

#define NUM_FADERS 5

class AudBgmVolumeController {
public:
    /// @brief Creates a new `AudBgmVolumeController`.
    AudBgmVolumeController();

    ~AudBgmVolumeController() {};

    void update();
    f32 getVolume() const;
    void volDown(bool);
    void interruptedByOther();
    void moveAuxVolume(f32, s32);
    void moveNoteFairyVolume(f32, s32);

    struct FaderCtrl {
        f32 mVolume;
        s32 mTime;
        s32 mRecoverTime;
    };

    s32 mTimers[NUM_FADERS];
    FaderCtrl mFaderCtrl[NUM_FADERS];
    AudFader mFaders[NUM_FADERS];
    bool mIsMuted;
};
