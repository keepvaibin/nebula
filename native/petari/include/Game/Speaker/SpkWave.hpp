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
    s32 getWaveSize(s32) const;
    u32 getLoopStartPos(s32) const;
    u32 getLoopEndPos(s32) const;
    s16* getWave(s32) const;
    WaveData* getWaveData(s32) const;

    void* mResource;  // Raw data of AudioRes/SpkRes/SpkRes.arc
};
