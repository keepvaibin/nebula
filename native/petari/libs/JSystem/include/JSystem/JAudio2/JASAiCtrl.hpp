#pragma once

#include <revolution/types.h>

enum JASOutputRate {
    OUTPUT_RATE_0,
};

// The layout of the samples a mix callback returns.
enum JASMixMode {
    MIX_MODE_MONO,        // one channel, both sides
    MIX_MODE_MONO_WIDE,   // one channel, the left side inverted
    MIX_MODE_EXTRA,       // the left side's samples, then the right's
    MIX_MODE_INTERLEAVE,  // stereo pairs (right, left)
};

namespace JASDriver {
    typedef s16* (*MixCallback)(s32);

    void initAI(void (*)(void));
    void startDMA();
    void stopDMA();
    void setOutputRate(JASOutputRate);
    void updateDac();
    void mixFrame();
    void updateDSP();
    void registerMixCallback(MixCallback, JASMixMode);
    f32 getDacRate();
    u32 getSubFrames();
    u32 getFrameSamples();
    u32 getSubFrameCounter();
    void subframeCallback();
    void syncCallback();

    extern JASMixMode sMixMode;
    extern f32 sDacRate;
    extern u32 sSubFrames;
    extern MixCallback extMixCallback;
    extern u32 sOutputRate;
    extern volatile u32 sSubFrameCounter;
};  // namespace JASDriver
