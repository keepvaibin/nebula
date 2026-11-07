#pragma once

#include <nebula_types.h>

// Which controllers' speakers play: a connected controller's does. The audio thread's SpeakerMixer is told of each change.
namespace SpkSpeakerCtrl {
    void setup();
    void connect(s32);
    void disconnect(s32);
    bool isEnable(s32);
    f32 getDeviceVolume(s32);
};  // namespace SpkSpeakerCtrl
