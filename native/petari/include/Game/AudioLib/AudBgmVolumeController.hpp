#pragma once

#include "Game/AudioLib/AudFader.hpp"

// A BGM's volume: the product of its faders.
class AudBgmVolumeController {
public:
    enum Fader {
        // Held down while requested every frame; recover once the requests stop.
        Fader_VolDown,
        Fader_Interrupted,
        // Moved by the caller only.
        Fader_Aux,
        Fader_NoteFairy,
        Fader_Num,
    };

    AudBgmVolumeController();

    void update();
    f32 getVolume() const;
    void volDown(bool);
    void interruptedByOther();
    void moveAuxVolume(f32, s32);
    void moveNoteFairyVolume(f32, s32);

private:
    void holdFade(Fader fader, f32 volume, s32 time);

public:
    // Frames until a held fader recovers, or -1 when it is not held.
    s32 mTimers[Fader_Num];
    AudFader mFaders[Fader_Num];
};
