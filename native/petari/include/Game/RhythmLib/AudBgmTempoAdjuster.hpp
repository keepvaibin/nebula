#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

class JAIStream;

// The music's beat sequence runs on its stream's samples (JASTrack::setSampleClock), one clock with no drift to correct.
class AudBgmTempoAdjuster {
public:
    AudBgmTempoAdjuster();

    void init();
    void setInfo(f32, u32, u32, u32, u32);
    void setStream(JAIStream*);
    void reject();
    // At a beat's note: how many stream samples the sequence is behind the stream (ahead: below 0).
    f32 beatProc(f32);
    // The stream's samples since the last call, across its loop; below 0 with no stream.
    static f32 samplesAdvanced(void* pSelf);

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
    u32 mLastPosition;
};
