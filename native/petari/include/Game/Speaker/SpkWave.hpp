#pragma once


// SpkRes wave data, read in place (host order, as the importer cooks it): header and 16-bit PCM.
struct WaveData {
    u32 mSize;
    u32 mLoopStartPos;
    u32 mLoopEndPos;
    s16 mWave[];
};

class SpkWave {
public:
    SpkWave();

    void setResource(void*);
    // The wave's header and samples, by the file's offset table (a u32 per wave from byte 8).
    const WaveData* getWaveData(s32) const;

    void* mResource;  // the speaker archive's wave file
};
