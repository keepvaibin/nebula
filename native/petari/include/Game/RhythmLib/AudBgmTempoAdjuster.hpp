#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

class JAIStream;

class AudBgmTempoAdjuster {
public:
    AudBgmTempoAdjuster();

    void init();
    void setInfo(f32, u32, u32, u32, u32);
    void setStream(JAIStream*);
    void reject();
    f32 beatProc(f32);
    f32 calcAdjustRate(f32, f32);

    JAIStream* mStream;
    f32 mBeatMul;
    u32 mIntroBeats;
    u32 mLoopBeats;
    u32 mLoopStartSamples;
    u32 mLoopEndSamples;
    f32 mLoopSamples;
    u32 mLoopCount;
    u32 mCurrentBeat;
    bool mIsBeatNeedSync;
    bool mIsStreamNeedSync;
};
