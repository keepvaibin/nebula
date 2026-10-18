#pragma once

#include "nebula_disc.hpp"
#include <revolution/types.h>

struct SpkParameters {
    /* 0x00 */ NEBULA_BE(u16) mWaveID;
    /* 0x02 */ u8 _2;
    /* 0x03 */ u8 mVolume;
    /* 0x04 */ NEBULA_BE(u16) mReleaseTime;
    /* 0x06 */ NEBULA_BE(u16) _padding;  // (?)
};

struct SpkFile {
    /* 0x0 */ NEBULA_BE(u32) count;
    /* 0x4 */ NEBULA_BE(u32) parametersOffset;
    /* 0x8 */ NEBULA_BE(u32) namesOffset;
    /* 0xC */ NEBULA_BE(u32) initialized;
};

class SpkTable {
public:
    SpkTable();

    void setResource(void*);

    SpkParameters* getParameters(int idx) {
        return &mParameters[idx];
    }

    /* 0x0 */ bool mInitialized;
    /* 0x4 */ s32 mResourceCount;
    /* 0x8 */ SpkParameters* mParameters;
    // The file's name offsets, relocated in place.
    /* 0xC */ NEBULA_SELFREL(const char)* mNames;
};
