#include "JSystem/JAudio2/JAUAudioArcLoader.hpp"
#include "JSystem/JAudio2/JAISeMgr.hpp"
#include "JSystem/JAudio2/JAUAudioData.hpp"

JAUAudioArcLoader::JAUAudioArcLoader(JAUAudioData* pData) {
    mData = pData;
}

bool JAUAudioArcLoader::load(void const* pData) {
    return parse(pData);
}

bool JAUAudioArcLoader::load(char const* pFileName) {
    void const* data = mData->loadStoreFile(pFileName);
    return data != nullptr && load(data);
}

void JAUAudioArcLoader::readWS(u32 bankNo, void const* pData, u32) {
    mData->newWaveBank(bankNo, pData);
}

void JAUAudioArcLoader::readBNK(u32 waveBankNo, void const* pData) {
    mData->newBank(pData, waveBankNo);
}

void JAUAudioArcLoader::readBSC(void const* pData, u32) {
    mData->newSeSeqCollection(pData);
}

void JAUAudioArcLoader::readBST(void const* pData, u32) {
    mData->newSoundTable(pData);
}

void JAUAudioArcLoader::readBSTN(void const* pData, u32) {
    mData->newSoundNameTable(pData);
}

void JAUAudioArcLoader::readBMS(u32 id, void const* pData, u32) {
    mData->newStaticSeqData(id, pData);
}

// In the sequence archive, where any sequence is found.
void JAUAudioArcLoader::readBMS_fromArchive(u32) {
}

void JAUAudioArcLoader::newVoiceBank(u32 waveBankNo, u32 bankNo) {
    mData->newVoiceBank(bankNo, waveBankNo);
}

// The sequences need no blocks of memory to load into.
void JAUAudioArcLoader::newDynamicSeqBlock(u32) {
}

void JAUAudioArcLoader::readBSFT(void const* pData) {
    mData->newStreamFileTable(pData);
}

void JAUAudioArcLoader::beginBNKList(u32 id, u32 count) {
    mData->beginNewBankTable(id, count);
}

void JAUAudioArcLoader::endBNKList() {
    mData->endNewBankTable();
}

void JAUAudioArcLoader::readMaxSeCategory(int category, int maxActiveSe, int maxInactiveSe) {
    JAISeMgr* seMgr = JASGlobalInstance< JAISeMgr >::getInstance();
    if (seMgr != nullptr) {
        seMgr->getCategory(category)->setMaxActiveSe(maxActiveSe);
        seMgr->getCategory(category)->setMaxInactiveSe(maxInactiveSe);
    }
}
