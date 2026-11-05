#pragma once

#include "JSystem/JAudio2/JAISeqDataMgr.hpp"
#include "JSystem/JAudio2/JAUBankTable.hpp"

class AssetFolder;
class JASBank;
class JASVoiceBank;
class JASWaveBank;
class NebulaArena;
struct JAIStreamDataMgr;
struct JAUSoundNameTable;
struct JAUSoundTable;

// What the sound system plays from: the sound tables, banks and waves the audio archive lists, and the sequences. It all
// lies in the store's files and stays for as long as the sound system does; a sequence's data is its file's, ready at once.
class JAUAudioData : public JAISeqDataMgr {
public:
    explicit JAUAudioData(NebulaArena* pHeap);

    NebulaArena* getHeap() const {
        return mHeap;
    }

    void setSeqArchive(AssetFolder* pArchive) {
        mSeqArchive = pArchive;
    }

    // From the audio archive (JAUAudioArcLoader).
    void* loadStoreFile(const char* pFileName);
    void newSoundTable(const void* pData);
    void newSoundNameTable(const void* pData);
    void newStreamFileTable(const void* pData);
    void newSeSeqCollection(const void* pData);
    void newStaticSeqData(JAISoundID id, const void* pData);
    void newWaveBank(u32 bankNo, const void* pData);
    void newBank(const void* pData, u32 waveBankNo);
    void newVoiceBank(u32 bankNo, u32 waveBankNo);
    void beginNewBankTable(u32 id, u32 count);
    void endNewBankTable();

    // Every wave of every bank but one, whose groups hold the same waves as each other and are chosen (loadWaveArc).
    void loadAllWaves(u32 exceptBank);
    // The groups `groups` names, a bit each, of a bank.
    void loadWaveArc(u32 bankNo, u32 groups);
    void eraseWaveArc(u32 bankNo, u32 groups);

    JAISeqDataMgr* getSeSeqDataMgr() const {
        return mSeSeqDataMgr;
    }

    JAIStreamDataMgr* getStreamDataMgr() const {
        return mStreamDataMgr;
    }

    JAUSoundTable* getSoundTable() const {
        return mSoundTable;
    }

    // JAISeqDataMgr: the sequences.
    virtual SeqDataReturnValue getSeqData(JAISoundID id, JAISeqData* pOut);
    virtual int releaseSeqData();
    virtual bool setSeqDataUser(JAISeqDataUser* pUser);

private:
    struct StaticSeq {
        JAISoundID mId;
        const void* mData;
        StaticSeq* mNext;
    };

    NebulaArena* mHeap;
    AssetFolder* mSeqArchive;
    JAUWaveBankTable mWaveBanks;
    JAUBankTable* mBuildingBankTable;
    JAISeqDataMgr* mSeSeqDataMgr;
    JAIStreamDataMgr* mStreamDataMgr;
    JAUSoundTable* mSoundTable;
    JAUSoundNameTable* mSoundNameTable;
    StaticSeq* mStaticSeqs;
};
