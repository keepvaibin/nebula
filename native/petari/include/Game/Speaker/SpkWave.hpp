#pragma once

#include <revolution/types.h>

// SpkRes wave data, read in place: big-endian header and 16-bit PCM.
struct WaveData {
    NEBULA_BE(u32) mSize;
    NEBULA_BE(u32) mLoopStartPos;
    NEBULA_BE(u32) mLoopEndPos;
    NEBULA_BE(s16) mWave[];
};

class SpkWave {
public:
    SpkWave();

    void setResource(void*);
    s32 getWaveSize(s32) const;
    u32 getLoopStartPos(s32) const;
    u32 getLoopEndPos(s32) const;
    NEBULA_BE(s16)* getWave(s32) const;
    WaveData* getWaveData(s32) const;

    /* 0x0 */ void* mResource;  // Raw data of AudioRes/SpkRes/SpkRes.arc
};
