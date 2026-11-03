#pragma once


// The sound table in host order, as the importer cooks it (native_data.rs speaker).
struct SpkParameters {
    u16 mWaveID;
    u8 _2;
    u8 mVolume;
    u16 mReleaseTime;
    u16 _padding;  // (?)
};

struct SpkFile {
    u32 count;
    u32 parametersOffset;
    u32 namesOffset;
    u32 initialized;
};

class SpkTable {
public:
    SpkTable();

    void setResource(void*);

    SpkParameters* getParameters(int idx) {
        return &mParameters[idx];
    }

    bool mInitialized;
    s32 mResourceCount;
    SpkParameters* mParameters;
    // The file and its name offsets from the file's start.
    const u8* mBase;
    const u32* mNameOffsets;

    const char* getName(int idx) const {
        return reinterpret_cast< const char* >(mBase + mNameOffsets[idx]);
    }
};
