#include "JSystem/JAudio2/JAUSeqCollection.hpp"
#include <cstring>

JAUSeqCollection::JAUSeqCollection() {
    mCollectionData = nullptr;
}

void JAUSeqCollection::init(void const* param_0) {
    mCollectionData = (const JAUSeqCollectionData*)param_0;

    if (mCollectionData->magic[0] != 'S' || mCollectionData->magic[1] != 'C') {
        mCollectionData = nullptr;
        return;
    }

    mGroupNum = mCollectionData->mGroupNum;
    mFileSize = mCollectionData->mFileSize;
    mSoundTableOffsets = &mCollectionData->mSoundTableOffsets;
}

bool JAUSeqCollection::getSeqData(int groupId, int waveId, JAISeqData* pSeqData) {
    if (groupId >= mGroupNum) {
        return false;
    }

    // Sound tables follow each other at any address: a table is its wave count, then that many offsets.
    const u8* const pSoundTable = reinterpret_cast< const u8* >(mCollectionData) + mSoundTableOffsets[groupId];
    u32 waveNum;
    memcpy(&waveNum, pSoundTable, sizeof(waveNum));
    if (waveId >= waveNum) {
        return false;
    }

    u32 seqOffset;
    memcpy(&seqOffset, pSoundTable + sizeof(u32) * (1 + waveId), sizeof(seqOffset));
    pSeqData->set((void*)mCollectionData, seqOffset);
    return true;
}

bool JAUSeqCollection::getSeqDataRegion(JAISeqDataRegion* pSeqDataRegion) {
    if (isValid()) {
        pSeqDataRegion->addr = (u8*)mCollectionData;
        pSeqDataRegion->size = mFileSize;
        return true;
    }

    return false;
}

JAUSeqDataMgr_SeqCollection::JAUSeqDataMgr_SeqCollection() {
    user_ = nullptr;
}

bool JAUSeqDataMgr_SeqCollection::setSeqDataUser(JAISeqDataUser* pSeqDataUser) {
    user_ = pSeqDataUser;
    return true;
}

int JAUSeqDataMgr_SeqCollection::releaseSeqData() {
    if (user_) {
        JAISeqDataRegion region;
        getSeqDataRegion(&region);
        int result = user_->releaseSeqData(region);
        return result;
    }
    return 2;
}

JAUSeqDataMgr_SeqCollection::SeqDataReturnValue JAUSeqDataMgr_SeqCollection::getSeqData(JAISoundID soundId, JAISeqData* pSeqData) {
    if (!isValid()) {
        return SeqDataReturnValue_0;
    }
    if (JAUSeqCollection::getSeqData(soundId.getGroupID(), soundId.getWaveID(), pSeqData)) {
        return SeqDataReturnValue_2;
    }
    return SeqDataReturnValue_0;
}

JAUSeqDataMgr_SeqCollection::~JAUSeqDataMgr_SeqCollection() {
}
