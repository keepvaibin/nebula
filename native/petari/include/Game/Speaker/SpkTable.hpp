#pragma once

#include <nebula_types.h>

// A sound of the speaker's table as the importer cooks it (native_data.rs speaker).
struct SpkParameters {
    u16 mWaveID;
    u16 mReleaseTime;
    u8 mVolume;
};
static_assert(sizeof(SpkParameters) == 6, "the cook's speaker record");

// The cooked table: the count, the record and name table offsets from the file's start.
struct SpkFile {
    u32 mCount;
    u32 mParametersOffset;
    u32 mNamesOffset;
};

class SpkTable {
public:
    SpkTable();

    void setResource(void*);

    const SpkParameters* getParameters(int idx) const {
        return &mParameters[idx];
    }

    bool mInitialized;
    s32 mResourceCount;
    const SpkParameters* mParameters;
    // The file and its name offsets from the file's start.
    const u8* mBase;
    const u32* mNameOffsets;

    const char* getName(int idx) const {
        return reinterpret_cast< const char* >(mBase + mNameOffsets[idx]);
    }
};
