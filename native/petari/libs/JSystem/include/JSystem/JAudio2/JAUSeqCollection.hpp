#pragma once

#include "JSystem/JAudio2/JAISeqDataMgr.hpp"

struct JAISeqData;
struct JAISeqDataRegion;

// BSC, big-endian.
struct JAUSeqCollectionData {
    /* 0x00 */ char magic[2];
    /* 0x02 */ NEBULA_BE(u16) mGroupNum;
    /* 0x04 */ NEBULA_BE(u32) mFileSize;
    /* 0x08 */ NEBULA_BE(u32) mSoundTableOffsets;
};

struct JAUSeqCollectionSoundTable {
    /* 0x00 */ NEBULA_BE(u32) mWaveNum;
    /* 0x04 */ NEBULA_BE(u32) mWaveSeqOffsets[];
};

class JAUSeqCollection {
public:
    JAUSeqCollection();

    void init(void const*);
    bool getSeqData(int, int, JAISeqData*);
    bool getSeqDataRegion(JAISeqDataRegion*);

    bool isValid() const {
        return mCollectionData != nullptr;
    }

    const JAUSeqCollectionData* getCollectionData() const {
        return mCollectionData;
    }

    /* 0x00 */ u16 mGroupNum;
    // In the file.
    /* 0x04 */ const NEBULA_BE(u32)* mSoundTableOffsets;
    /* 0x08 */ const JAUSeqCollectionData* mCollectionData;
    /* 0x0C */ u32 mFileSize;
};

class JAUSeqDataMgr_SeqCollection : public JAISeqDataMgr, public JAUSeqCollection {
public:
    JAUSeqDataMgr_SeqCollection();
    bool setSeqDataUser(JAISeqDataUser*);
    int releaseSeqData();
    SeqDataReturnValue getSeqData(JAISoundID, JAISeqData*);
    ~JAUSeqDataMgr_SeqCollection();

    const void* getResource() const {
        return mSoundTableOffsets;
    }
    void init(const void* pData) {
        JAUSeqCollection::init(pData);
    }

    /* 0x14 */ JAISeqDataUser* user_;
};
