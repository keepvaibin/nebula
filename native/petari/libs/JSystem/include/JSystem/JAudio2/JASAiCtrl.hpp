#pragma once

#include "Game/System/NebulaAtomic.hpp"


enum JASOutputRate {
    OUTPUT_RATE_0,
};

namespace JASDriver {
    void startAudio();
    void startClock();
    void stopClock();
    void setOutputRate(JASOutputRate);
    void updateFrame();
    void mixFrame();
    void updateSubFrame();
    f32 getSoundRate();
    u32 getSubFrames();
    u32 getFrameSamples();
    u32 getSubFrameCounter();
    void subframeCallback();
    void syncCallback();

    extern f32 sSoundRate;
    extern u32 sSubFrames;
    extern u32 sOutputRate;
    extern NebulaAtomic< u32 > sSubFrameCounter;
};  // namespace JASDriver
