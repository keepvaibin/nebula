#pragma once

// TODO

#include <NebulaGpu.h>
#include <nebula_math.h>

#define NUM_RHYTHM_INFO 6

class AudRhythmSeqParser;

// TODO: replace with proper struct/class
struct RhythmInfo {
    bool mIsActive;
    u32 _4;
    u32 _8;
    f32 _C;
    s64 mStartTime;
    s64 mPlayTime;
    s64 mLastMeasuredTime;
    s64 mPausedTime;
    u32 _30;
    u32 _34;
};

class AudRhythmHolder {
public:
    AudRhythmHolder();

    ~AudRhythmHolder(){};

    void movement();
    void copyInfoFromParser();
    void init();
    void copyInfoFromParserOne(u32, u32, u32, s64);

    f32 mModifiedTempo;
    f32 mTempo;
    f32 _8;
    bool mIsPaused;
    RhythmInfo mInfos[6];
    bool mIsRhythmActive;
    AudRhythmSeqParser* mSeqParser;
    u32 _168;
    u32 _16C;
};
