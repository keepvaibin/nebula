#pragma once

#include <revolution/types.h>

enum JASOutputRate {
    OUTPUT_RATE_0,
};

namespace JASDriver {
    void initAI(void (*)(void));
    void startDMA();
    void stopDMA();
    void setOutputRate(JASOutputRate);
    void updateDac();
    void mixFrame();
    void updateDSP();
    f32 getDacRate();
    u32 getSubFrames();
    u32 getFrameSamples();
    u32 getSubFrameCounter();
    void subframeCallback();
    void syncCallback();

    extern f32 sDacRate;
    extern u32 sSubFrames;
    extern u32 sOutputRate;
    extern volatile u32 sSubFrameCounter;
};  // namespace JASDriver
