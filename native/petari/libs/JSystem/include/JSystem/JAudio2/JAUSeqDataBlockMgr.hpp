#pragma once

#include "Game/System/NebulaAtomic.hpp"
#include "JSystem/JAudio2/JAISeqDataMgr.hpp"
#include "JSystem/JSupport/JSUList.hpp"

struct JAISeqData;
struct JAISeqDataUser;
class AssetFolder;

struct JAUSeqDataBlock {
    JAUSeqDataBlock();

    JSULink< JAUSeqDataBlock > mLink;
    JAISoundID mSoundID;
    JAISeqDataRegion region;
    NebulaAtomic< int > field_0x1c;  // the load thread says when the data is there
};

class JAUSeqDataBlocks : public JSUList< JAUSeqDataBlock > {
public:
    u8* getSeqData(JAISoundID);
    JSULink< JAUSeqDataBlock >* seekFreeBlock(u32);
    bool append(JSULink< JAUSeqDataBlock >*);
    bool remove(JSULink< JAUSeqDataBlock >*);
    bool hasFailedBlock(JAISoundID);
};

class JAUDynamicSeqDataBlocks {
public:
    JAUDynamicSeqDataBlocks();
    void setSeqDataArchive(AssetFolder*);
    s32 getSeqData(JAISoundID, JAISeqDataUser*, JAISeqData*, bool);
    bool appendDynamicSeqDataBlock(JAUSeqDataBlock*);
    bool loadDynamicSeq(JAISoundID, bool, JAISeqDataUser*);
    JAUSeqDataBlock* releaseIdleDynamicSeqDataBlock_(JAISeqDataUser*, u32);
    void rearrangeLoadingSeqs_();

    JAUSeqDataBlocks const& getFreeBlocks() const {
        return mFreeBlocks;
    }
    JAUSeqDataBlocks const& getLoadedBlocks() const {
        return mLoadedBlocks;
    }
    AssetFolder* getSeqDataArchive() {
        return mSeqDataArchive;
    }

    JAUSeqDataBlocks mFreeBlocks;
    JAUSeqDataBlocks field_0xc;
    JAUSeqDataBlocks mLoadedBlocks;
    AssetFolder* mSeqDataArchive;
};
