#include "Game/System/SaveDataHandler.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/System/SaveRequest.hpp"
#include "Game/System/SaveDataFileAccessor.hpp"
#include "Game/System/SysConfigFile.hpp"
#include "Game/System/UserFile.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include <JSystem/JKernel/JKRExpHeap.hpp>
#include <cstdio>
#include "Game/System/NebulaClock.hpp"

namespace {
    struct SaveFileSpec {
        /* 0x00 */ const char* mName;
        /* 0x04 */ u32 mBufferSize;
    };

    const char cSaveFileName[] = "GameData.bin";
    const SaveFileSpec cSaveFileSpecTable[] = {{"mario", 0xF80}, {"luigi", 0xF80}, {"config", 0x60}};
    const SaveFileSpec cSaveFileSpecSystem = {"sysconf", 0x80};
};  // namespace

namespace {
    NEW_NERVE(SaveDataHandlerWait, SaveDataHandler, Wait);
    NEW_NERVE(SaveDataHandlerProcessing, SaveDataHandler, Processing);
    NEW_NERVE(SaveDataHandlerSaveProcessingGameData, SaveDataHandler, SaveProcessingGameData);
    NEW_NERVE(SaveDataHandlerRemoveProcessingGameData, SaveDataHandler, RemoveProcessingGameData);
};  // namespace

SaveDataHandler::SaveDataHandler(const SysConfigFile* pSysConfigFile, const UserFile* pUserFile)
    : NerveExecutor("SaveDataHandler"), mSaveRequest(), _C(), _14(), _18() {
    mSaveRequest = new SaveRequest();

    createCommunicationBuffer();
    resetSaveData(_18);
    initializeAllFileInSaveData(_18, pSysConfigFile, pUserFile);


    initNerve(GET_NERVE_ANON(SaveDataHandlerWait));
}

void SaveDataHandler::update() {
    updateNerve();
}

void SaveDataHandler::requestCheckEnableToCreate() {
    // The host's disk has room for a new save: no request, the round trip only.
    mSaveRequest->init();

    setNerve(GET_NERVE_ANON(SaveDataHandlerProcessing));
}

void SaveDataHandler::requestLoadSaveData() {
    MR::zeroMemory(_14, 0x10000);
    mSaveRequest->setReadSeq(::cSaveFileName, _14, 0x10000, &_C);
    mSaveRequest->start();

    setNerve(GET_NERVE_ANON(SaveDataHandlerProcessing));
}

bool SaveDataHandler::requestVerifyAfterLoadGameDataFile() {
    if (!isCorrectFileHeader(_14)) {
        return false;
    }

    const SaveDataFileAccessor file(_14);

    if (_C != NebulaAlignUp32(file.getFileSize())) {
        return false;
    }

    if (file.getCheckSum() != MR::calcCheckSum(_14 + sizeof(u32), file.getFileSize() - sizeof(u32))) {
        return false;
    }

    copySaveDataEachFile(_18, _14);

    return true;
}

void SaveDataHandler::initializeUserFileMemory(int index, const UserFile* pUserFile) {
    SaveDataFileAccessor fileAccessor = SaveDataFileAccessor(_18);

    for (u32 i = 0; i < sizeof(::cSaveFileSpecTable) / sizeof(*::cSaveFileSpecTable); i++) {
        char fileName[16];
        snprintf(fileName, sizeof(fileName), "%s%1d", ::cSaveFileSpecTable[i].mName, index);

        SaveDataUserFileInfo userFileInfo;
        fileAccessor.makeUserFileInfo(&userFileInfo, fileName);

        if (userFileInfo.mKind == 0) {
            pUserFile->makeGameDataBinary(userFileInfo.mData, userFileInfo.mDataSize);
        } else if (userFileInfo.mKind == 1) {
            pUserFile->makeConfigDataBinary(userFileInfo.mData, userFileInfo.mDataSize);
        }
    }
}

void SaveDataHandler::copyUserFileMemory(int indexDst, int indexSrc) {
    SaveDataFileAccessor fileAccessor = SaveDataFileAccessor(_18);

    for (u32 i = 0; i < sizeof(::cSaveFileSpecTable) / sizeof(*::cSaveFileSpecTable); i++) {
        char srcFileName[16];
        snprintf(srcFileName, sizeof(srcFileName), "%s%1d", ::cSaveFileSpecTable[i].mName, indexSrc);

        SaveDataUserFileInfo srcUserFileInfo;
        fileAccessor.makeUserFileInfo(&srcUserFileInfo, srcFileName);

        char dstFileName[16];
        snprintf(dstFileName, sizeof(dstFileName), "%s%1d", ::cSaveFileSpecTable[i].mName, indexDst);

        SaveDataUserFileInfo dstUserFileInfo;
        fileAccessor.makeUserFileInfo(&dstUserFileInfo, dstFileName);

        MR::copyMemory(dstUserFileInfo.mData, srcUserFileInfo.mData, dstUserFileInfo.mDataSize);
    }
}

void SaveDataHandler::restoreGameDataFile(const char* pUserFileName, void* pParam2, u32 param3) {
    SaveDataFileAccessor fileAccessor = SaveDataFileAccessor(_18);

    SaveDataUserFileInfo userFileInfo;
    fileAccessor.makeUserFileInfo(&userFileInfo, pUserFileName);

    MR::copyMemory(pParam2, userFileInfo.mData, userFileInfo.mDataSize);
}

void SaveDataHandler::storeUserFile(const UserFile* pUserFile) {
    SaveDataFileAccessor fileAccessor = SaveDataFileAccessor(_18);

    SaveDataUserFileInfo gameFileInfo;
    fileAccessor.makeUserFileInfo(&gameFileInfo, pUserFile->getGameDataName());
    pUserFile->makeGameDataBinary(gameFileInfo.mData, gameFileInfo.mDataSize);

    SaveDataUserFileInfo configFileInfo;
    fileAccessor.makeUserFileInfo(&configFileInfo, pUserFile->getConfigDataName());
    pUserFile->makeConfigDataBinary(configFileInfo.mData, configFileInfo.mDataSize);
}

void SaveDataHandler::storeSysConfigFile(const SysConfigFile* pSysConfigFile) {
    SaveDataFileAccessor fileAccessor = SaveDataFileAccessor(_18);

    SaveDataUserFileInfo userFileInfo;
    fileAccessor.makeUserFileInfo(&userFileInfo, ::cSaveFileSpecSystem.mName);
    pSysConfigFile->makeDataBinary(userFileInfo.mData, userFileInfo.mDataSize);
}

void SaveDataHandler::requestSaveSaveData() {
    SaveDataFileAccessor fileAccessorA = SaveDataFileAccessor(_18);
    SaveDataFileAccessor fileAccessorB = SaveDataFileAccessor(_14);

    const u32 fileSize = fileAccessorA.getFileSize();
    MR::copyMemory(_14, _18, fileSize);
    MR::fillMemory(_14 + fileSize, 0, NebulaAlignUp32(fileSize) - fileSize);
    fileAccessorB.setCheckSum(MR::calcCheckSum(_14 + sizeof(u32), fileAccessorB.getFileSize() - sizeof(u32)));

    mSaveRequest->setWriteSeq(::cSaveFileName, _14, NebulaAlignUp32(fileAccessorB.getFileSize()));
    mSaveRequest->start();

    setNerve(GET_NERVE_ANON(SaveDataHandlerSaveProcessingGameData));
}

void SaveDataHandler::requestRemoveSaveData() {
    setNerve(GET_NERVE_ANON(SaveDataHandlerRemoveProcessingGameData));
}

u32 SaveDataHandler::getEnoughtTempBufferSize() {
    return 0xF80;
}

bool SaveDataHandler::isDone() const {
    return isNerve(GET_NERVE_ANON(SaveDataHandlerWait));
}

SaveRequest::Result SaveDataHandler::getLastResult() const {
    return mSaveRequest->getResult();
}

void SaveDataHandler::exeWait() {
}

void SaveDataHandler::exeProcessing() {
    if (!mSaveRequest->isDone()) {
        return;
    }

    setNerve(GET_NERVE_ANON(SaveDataHandlerWait));
}

void SaveDataHandler::exeSaveProcessingGameData() {
    bool isErr = false;

    if (!trySave(&isErr, mSaveRequest->isDone())) {
        return;
    }

    // Saved or not, the sequence ends (the console wrote the System Menu's banner next; a PC has no System Menu).
    setNerve(GET_NERVE_ANON(SaveDataHandlerWait));
}

void SaveDataHandler::exeRemoveProcessingGameData() {
    bool isRemoved = false;

    if (!tryRemoveFile(::cSaveFileName, &isRemoved)) {
        return;
    }

    setNerve(GET_NERVE_ANON(SaveDataHandlerWait));
}

void SaveDataHandler::resetSaveData(u8* pBuffer) {
    SaveDataFileAccessor file(pBuffer);
    u32 offset = 0x140;

    file.setCheckSum(0);
    file.setVersion(2);
    file.setUserFileInfoNum(19);
    file.setFileSize(0);

    int fileIndex = 0;

    for (int i = 0; i < 6; i++) {
        for (u32 j = 0; j < sizeof(::cSaveFileSpecTable) / sizeof(*::cSaveFileSpecTable); j++) {
            char* pName = file.getFileName(fileIndex);

            MR::zeroMemory(pName, SaveDataFileAccessor::cNameSize);

            char fileName[32];
            snprintf(fileName, sizeof(fileName), "%s%1d", ::cSaveFileSpecTable[j].mName, i + 1);

            snprintf(pName, SaveDataFileAccessor::cNameSize, "%s", fileName);
            file.setFileOffset(fileIndex, offset);

            MR::zeroMemory(pBuffer + offset, ::cSaveFileSpecTable[j].mBufferSize);
            offset += ::cSaveFileSpecTable[j].mBufferSize;
            fileIndex++;
        }
    }

    char* pSystemName = file.getFileName(fileIndex);

    MR::zeroMemory(pSystemName, SaveDataFileAccessor::cNameSize);

    snprintf(pSystemName, SaveDataFileAccessor::cNameSize, "%s", ::cSaveFileSpecSystem.mName);
    file.setFileOffset(fileIndex, offset);

    MR::zeroMemory(pBuffer + offset, ::cSaveFileSpecSystem.mBufferSize);

    file.setFileSize(offset + ::cSaveFileSpecSystem.mBufferSize);

    u32 padding = NebulaAlignUp32(offset + ::cSaveFileSpecSystem.mBufferSize) - file.getFileSize();
    u8* pPadding = pBuffer + file.getFileSize();
    for (u32 i = 0; i < padding; i++) {
        pPadding[i] = 0;
    }
}

void SaveDataHandler::initializeAllFileInSaveData(u8* pBuffer, const SysConfigFile* pSysConfigFile, const UserFile* pUserFile) {
    SaveDataFileAccessor fileAccessor = SaveDataFileAccessor(pBuffer);

    const int num = fileAccessor.getUserFileInfoNum();

    for (int i = 0; i < num; i++) {
        SaveDataUserFileInfo userFileInfo;
        fileAccessor.makeUserFileInfo(&userFileInfo, fileAccessor.getFileName(i));

        if (userFileInfo.mKind == 0) {
            pUserFile->makeGameDataBinary(userFileInfo.mData, userFileInfo.mDataSize);
        } else if (userFileInfo.mKind == 1) {
            pUserFile->makeConfigDataBinary(userFileInfo.mData, userFileInfo.mDataSize);
        } else {
            pSysConfigFile->makeDataBinary(userFileInfo.mData, userFileInfo.mDataSize);
        }
    }
}

bool SaveDataHandler::isCorrectFileHeader(const u8* pBuffer) {
    const SaveDataFileAccessor file(const_cast< u8* >(pBuffer));

    if (file.getVersion() != 2) {
        return false;
    }

    if (file.getFileSize() >= 0x10000) {
        return false;
    }

    return file.getUserFileInfoNum() < 24;
}

void SaveDataHandler::copySaveDataEachFile(u8* pFileDst, const u8* pFileSrc) {
    SaveDataFileAccessor srcFileAccessor = SaveDataFileAccessor(const_cast< u8* >(pFileSrc));
    SaveDataFileAccessor dstFileAccessor = SaveDataFileAccessor(pFileDst);

    const int num = srcFileAccessor.getUserFileInfoNum();

    for (int i = 0; i < num; i++) {
        const char* pFileName = srcFileAccessor.getFileName(i);

        SaveDataUserFileInfo srcUserFileInfo;
        srcFileAccessor.makeUserFileInfo(&srcUserFileInfo, pFileName);

        SaveDataUserFileInfo dstUserFileInfo;
        dstFileAccessor.makeUserFileInfo(&dstUserFileInfo, pFileName);

        if (dstUserFileInfo.mData != nullptr) {
            int deltaSize = dstUserFileInfo.mDataSize - srcUserFileInfo.mDataSize;

            MR::copyMemory(dstUserFileInfo.mData, srcUserFileInfo.mData, dstUserFileInfo.mDataSize);

            if (deltaSize > 0) {
                MR::fillMemory(dstUserFileInfo.mData + dstUserFileInfo.mDataSize, 0, deltaSize);
            }
        }
    }
}

void SaveDataHandler::createCommunicationBuffer() {
    _14 = new (MR::getStationedHeapGDDR3(), 32) u8[0x10000];
    _18 = new (MR::getStationedHeapGDDR3(), 32) u8[0x10000];
}

bool SaveDataHandler::tryRemoveFile(const char* pFileName, bool* pIsRemoved) {
    if (MR::isFirstStep(this)) {
        mSaveRequest->setDelete(pFileName)->start();
    } else if (mSaveRequest->isDone()) {
        const SaveRequest::Result result = mSaveRequest->getResult();

        if (result == SaveRequest::Result_Done || result == SaveRequest::Result_NoFile) {
            *pIsRemoved = true;
            return true;
        } else {
            *pIsRemoved = false;
            return true;
        }
    }

    return false;
}

bool SaveDataHandler::trySave(bool* pIsErr, bool isDone) {
    if (!isDone) {
        return false;
    }

    if (mSaveRequest->getResult() == SaveRequest::Result_Done) {
        *pIsErr = false;
        return true;
    } else {
        *pIsErr = true;
        return true;
    }
}
