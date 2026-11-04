#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

class JAIStream;

// A music's beat sequence and its stream are one clock: the sequence's place is worked out from the stream's sample
// (JASTrack::setSecondsClock). The sequence's first beat note is the stream's first sample; a loop in the stream is that
// many beats in the sequence.
class AudBgmTempoAdjuster {
public:
    AudBgmTempoAdjuster();

    void setInfo(f32 beatMul, u32 introBeats, u32 loopBeats, u32 loopStartSamples, u32 loopEndSamples);
    void setStream(JAIStream*);
    void reject();
    void setTempo(f32 tempo) { mTempo = tempo; }
    // True once after setStream: the next beat note is the stream's start.
    bool takeNewStream();
    // Where the sequence should be, in samples of its own time; below 0 with no stream.
    static f64 sequenceSeconds(void* pSelf);

    JAIStream* mStream;
    f32 mTempo;
    f32 mBeatMul;
    u32 mIntroBeats;
    u32 mLoopBeats;
    u32 mLoopStartSamples;
    u32 mLoopEndSamples;
    bool mIsNewStream;
};
