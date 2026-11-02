#pragma once

#include "JSystem/JAudio2/JAISeqDataMgr.hpp"

struct JAISeqData;
struct JAISeqDataRegion;

// BSC in host order, as the importer cooks the audio archive (its sequences stay bytes).
struct JAUSeqCollectionData {
    char magic[2];
    u16 mGroupNum;
    u32 mFileSize;
    u32 mSoundTableOffsets;
};

struct JAUSeqCollectionSoundTable {
    u32 mWaveNum;
    u32 mWaveSeqOffsets[];
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

    u16 mGroupNum;
    // In the file.
    const u32* mSoundTableOffsets;
    const JAUSeqCollectionData* mCollectionData;
    u32 mFileSize;
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

    JAISeqDataUser* user_;
};
