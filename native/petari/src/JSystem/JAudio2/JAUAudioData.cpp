#include "JSystem/JAudio2/JAUAudioData.hpp"
#include "JSystem/JAudio2/JASBNKParser.hpp"
#include "JSystem/JAudio2/JASBankTable.hpp"
#include "JSystem/JAudio2/JASBasicWaveBank.hpp"
#include "JSystem/JAudio2/JASVoiceBank.hpp"
#include "JSystem/JAudio2/JASWSParser.hpp"
#include "JSystem/JAudio2/JASWaveArcLoader.hpp"
#include "JSystem/JAudio2/JAUSeqCollection.hpp"
#include "JSystem/JAudio2/JAUSoundInfo.hpp"
#include "JSystem/JAudio2/JAUSoundTable.hpp"
#include "JSystem/JAudio2/JAUStreamFileTable.hpp"
#include "Game/System/AssetFolder.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/System/NebulaArena.hpp"

namespace {
    class TStreamDataMgr : public JAIStreamDataMgr {
    public:
        explicit TStreamDataMgr(const void* pData) : mCount(0), mEntries(nullptr) {
            JAUStreamFileTable table;
            table.init(pData);
            if (!table.isValid()) {
                return;
            }
            mCount = table.getNumFiles();
            mEntries = new s32[mCount];
            for (u32 i = 0; i < mCount; i++) {
                mEntries[i] = GameAssets::find(table.getFilePath(i));
            }
        }

        virtual s32 getStreamFileEntry(JAISoundID id) {
            const u32 index = id.getWaveID();
            return index < mCount ? mEntries[index] : -1;
        }

        bool isValid() const {
            return mCount != 0;
        }

    private:
        u32 mCount;
        s32* mEntries;
    };
}  // namespace

JAUAudioData::JAUAudioData(NebulaArena* pHeap)
    : mHeap(pHeap), mSeqArchive(nullptr), mBuildingBankTable(nullptr), mSeSeqDataMgr(nullptr), mStreamDataMgr(nullptr),
      mSoundTable(nullptr), mSoundNameTable(nullptr), mStaticSeqs(nullptr) {
}

void* JAUAudioData::loadStoreFile(const char* pFileName) {
    return GameAssets::load(pFileName, nullptr, 0, mHeap, nullptr);
}

void JAUAudioData::newSoundTable(const void* pData) {
    mSoundTable = new (mHeap, 0) JAUSoundTable(true);
    mSoundTable->init(pData);
}

void JAUAudioData::newSoundNameTable(const void* pData) {
    mSoundNameTable = new (mHeap, 0) JAUSoundNameTable(true);
    mSoundNameTable->init(pData);
}

void JAUAudioData::newStreamFileTable(const void* pData) {
    TStreamDataMgr* pStreams = new (mHeap, 0) TStreamDataMgr(pData);
    mStreamDataMgr = pStreams->isValid() ? pStreams : nullptr;
}

void JAUAudioData::newSeSeqCollection(const void* pData) {
    JAUSeqDataMgr_SeqCollection* pCollection = new (mHeap, 0) JAUSeqDataMgr_SeqCollection();
    pCollection->init(pData);
    mSeSeqDataMgr = pCollection;
}

void JAUAudioData::newStaticSeqData(JAISoundID id, const void* pData) {
    StaticSeq* pSeq = new (mHeap, 0) StaticSeq{id, pData, mStaticSeqs};
    mStaticSeqs = pSeq;
}

void JAUAudioData::newWaveBank(u32 bankNo, const void* pData) {
    JASWaveBank* pBank = JASWSParser::createWaveBank(pData, mHeap);
    if (pBank != nullptr) {
        mWaveBanks.registWaveBank(bankNo, pBank);
    }
}

void JAUAudioData::loadAllWaves(u32 exceptBank) {
    for (u32 bankNo = 0; bankNo < 255; bankNo++) {
        JASWaveBank* pBank = bankNo != exceptBank ? mWaveBanks.getWaveBank(bankNo) : nullptr;
        for (u32 group = 0; pBank != nullptr && group < pBank->getArcCount(); group++) {
            JASWaveArc* pArc = pBank->getWaveArc(group);
            if (pArc->getStatus() == 0) {
                pArc->load();
            }
        }
    }
}

void JAUAudioData::loadWaveArc(u32 bankNo, u32 groups) {
    JASWaveBank* pBank = mWaveBanks.getWaveBank(bankNo);
    for (u32 group = 0; pBank != nullptr && group < pBank->getArcCount(); group++) {
        if ((groups & (1u << group)) != 0) {
            pBank->getWaveArc(group)->load();
        }
    }
}

void JAUAudioData::eraseWaveArc(u32 bankNo, u32 groups) {
    JASWaveBank* pBank = mWaveBanks.getWaveBank(bankNo);
    for (u32 group = 0; pBank != nullptr && group < pBank->getArcCount(); group++) {
        if ((groups & (1u << group)) != 0) {
            pBank->getWaveArc(group)->erase();
        }
    }
}

void JAUAudioData::newBank(const void* pData, u32 waveBankNo) {
    const u32 bankNo = JASBNKParser::getBankNumber(pData);
    JASBank* pBank = JASBNKParser::createBank(pData, mHeap);
    if (pBank == nullptr) {
        return;
    }
    if (mBuildingBankTable != nullptr) {
        mBuildingBankTable->registBank(bankNo, pBank);
    } else {
        JASDefaultBankTable::getInstance()->registBank(bankNo, pBank);
    }
    pBank->assignWaveBank(mWaveBanks.getWaveBank(waveBankNo));
}

void JAUAudioData::newVoiceBank(u32 bankNo, u32 waveBankNo) {
    JASBank* pBank = new (mHeap, 0) JASVoiceBank();
    if (mBuildingBankTable != nullptr) {
        mBuildingBankTable->registBank(bankNo, pBank);
    } else {
        JASDefaultBankTable::getInstance()->registBank(bankNo, pBank);
    }
    pBank->assignWaveBank(mWaveBanks.getWaveBank(waveBankNo));
}

void JAUAudioData::beginNewBankTable(u32 id, u32 count) {
    JASBank** ppBanks = new (mHeap, 0) JASBank*[count];
    mBuildingBankTable = new (mHeap, 0) JAUBankTable(id, ppBanks, count);
}

void JAUAudioData::endNewBankTable() {
    mBuildingBankTable = nullptr;
}

JAISeqDataMgr::SeqDataReturnValue JAUAudioData::getSeqData(JAISoundID id, JAISeqData* pOut) {
    for (const StaticSeq* pSeq = mStaticSeqs; pSeq != nullptr; pSeq = pSeq->mNext) {
        if (static_cast< u32 >(pSeq->mId) == static_cast< u32 >(id)) {
            pOut->set(pSeq->mData, 0);
            return SeqDataReturnValue_2;
        }
    }
    JAUSoundInfo* pInfo = JASGlobalInstance< JAUSoundInfo >::getInstance();
    AssetFolder::Item item;
    if (mSeqArchive != nullptr && pInfo != nullptr &&
        mSeqArchive->getItem(mSeqArchive->getEntry(), pInfo->getBgmSeqResourceID(id), &item)) {
        pOut->set(item.mData, 0);
        return SeqDataReturnValue_2;
    }
    return SeqDataReturnValue_0;
}

int JAUAudioData::releaseSeqData() {
    return 0;
}

bool JAUAudioData::setSeqDataUser(JAISeqDataUser*) {
    return true;
}
