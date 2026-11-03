#pragma once

#include "JSystem/JAudio2/JASGlobal.hpp"
#include "JSystem/JAudio2/JAUBankTable.hpp"
#include "JSystem/JAudio2/JAUSeqDataBlockMgr.hpp"
#include "JSystem/JKernel/JKRDisposer.hpp"


struct JAUBits255 {
    u32 mWords[8] = {};

    bool test(u32 bit) const {
        return (mWords[bit >> 5] >> (bit & 31)) & 1;
    }
    void set(u32 bit, bool value = true) {
        const u32 mask = 1u << (bit & 31);
        mWords[bit >> 5] = value ? mWords[bit >> 5] | mask : mWords[bit >> 5] & ~mask;
    }
    void reset(u32 bit) {
        set(bit, false);
    }
};

struct JAISeqData;
struct JAISeqDataMgr;
struct JAISeqDataUser;
struct JAIStreamDataMgr;
class JASVoiceBank;
class JAUBankTableLink;
class JAUDisposer_;
class JAUSectionHeap;
struct JAUSoundTable;
struct JAUSoundNameTable;
class AssetFolder;
class NebulaArena;

class JAUSection : public JKRDisposer, protected JSULink< JAUSection > {
public:
    struct TSectionData {
        TSectionData();
        void resetRegisteredBankTables();
        void resetRegisteredWaveBankTables();

        JAUDynamicSeqDataBlocks field_0x00;
        JSUList< JAUSeqDataBlock > field_0x28;
        JAUBits255 registeredWaveBankTables;
        JAUBits255 registeredBankTables;
        JAUBankTableDictionary field_0x74;
        JAISeqDataMgr* field_0x80;
        const void* mBstDst;
        const void* mBstnDst;
        JSUList< JAUDisposer_ > field_0x8c;
    };

    JAUSection(JAUSectionHeap*, u32, s32);
    void finishBuild();
    virtual void dispose();
    JAUSoundTable* newSoundTable(void const*, u32, bool);
    JAUSoundNameTable* newSoundNameTable(void const*, u32, bool);
    JAIStreamDataMgr* newStreamFileTable(void const*, bool);
    JAISeqDataMgr* newSeSeqCollection(void const*, u32);
    u8* newStaticSeqDataBlock_(JAISoundID, u32);
    bool newStaticSeqData(JAISoundID, void const*, u32);
    bool newStaticSeqData(JAISoundID);
    void* loadStoreFile(const char*, bool);
    void* newCopy(void const*, u32, s32);
    JASWaveBank* newWaveBank(u32, void const*);
    bool loadWaveArc(u32, u32);
    bool loadWaveArc(u32);
    bool eraseWaveArc(u32);
    bool eraseWaveArc(u32, u32);
    JASBank* newBank(void const*, u32);
    JASVoiceBank* newVoiceBank(u32, u32);
    bool beginNewBankTable(u32, u32);
    JAUBankTable* endNewBankTable();

    bool isBuilding() const {
        return field_0x2c;
    }

    bool isOpen() const;
    JAUSectionHeap* asSectionHeap() {
        if ((JAUSection*)sectionHeap_ == this) {
            return sectionHeap_;
        }

        return nullptr;
    }

    JAUSectionHeap* getSectionHeap() {
        return sectionHeap_;
    }

    const TSectionData& getSectionData() const {
        return data_;
    }

    JSUList< JAUDisposer_ >& get_C4() {
        return data_.field_0x8c;
    }

    NebulaArena* getHeap_();

    u32 field_0x28;
    bool field_0x2c;
    JAUSectionHeap* sectionHeap_;
    JAUBankTableLink* buildingBankTable_;
    TSectionData data_;
};

class JAUSectionHeap : public JAUSection, public JASGlobalInstance< JAUSectionHeap >, public JAISeqDataMgr {
public:
    struct TSectionHeapData {
        TSectionHeapData();

        JAUWaveBankTable waveBankTable;
        JAISeqDataUser* seqDataUser;
        JAUDynamicSeqDataBlocks seqDataBlocks;
        JAISeqDataMgr* seSeqDataMgr_;
        JAIStreamDataMgr* streamDataMgr_;
        JAUSoundTable* soundTable;
        JAUSoundNameTable* soundNameTable;
    };

    void setSeqDataArchive(AssetFolder*);
    JAUSectionHeap(NebulaArena*, bool, s32);
    JAUSection* getOpenSection();
    bool setSeqDataUser(JAISeqDataUser*);
    bool newDynamicSeqBlock(u32);
    int newDynamicSeqBlocks(u32, int);
    SeqDataReturnValue getSeqData(JAISoundID, JAISeqData*);
    int releaseSeqData();
    bool isWaveLoaded(u32, u32);

    JAISeqDataMgr* getSeqSeqDataMgr() {
        return this;
    }

    JAISeqDataMgr* getSeSeqDataMgr() {
        return sectionHeapData_.seSeqDataMgr_;
    }

    JAIStreamDataMgr* getStreamDataMgr() {
        return sectionHeapData_.streamDataMgr_;
    }

    TSectionHeapData const& getSectionHeapData() const {
        return sectionHeapData_;
    }

    JAUWaveBankTable& getWaveBankTable() {
        return sectionHeapData_.waveBankTable;
    }

    JAUSoundTable* getSoundTable();
    NebulaArena* getHeap() {
        if (getOpenSection()->isBuilding() != 0) {
            return mHeap;
        }

        return nullptr;
    }

    NebulaArena* mHeap;
    int field_0xe4;
    JSUList< JAUSection > mSectionList;
    TSectionHeapData sectionHeapData_;
};

inline NebulaArena* JAUSection::getHeap_() {
    return sectionHeap_->mHeap;
}

inline bool JAUSection::isOpen() const {
    return sectionHeap_->getOpenSection() == this;
}

JAUSectionHeap* JAUNewSectionHeap(bool);
