#include "Game/System/SaveDataHandler.hpp"
#include "Game/System/GameDataHolder.hpp"
#include "Game/System/SaveFileFormat.hpp"
#include <JSystem/JUtility/JUTNativeLog.hpp>
#include "Game/LiveActor/Nerve.hpp"
#include "Game/System/SaveRequest.hpp"
#include "Game/System/SaveDataFileAccessor.hpp"
#include "Game/System/UserFile.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include <cstdio>
#include "Game/System/NebulaClock.hpp"

namespace {
    struct SaveFileSpec {
        const char* mName;
        u32 mBufferSize;
    };

    const char cSaveFileName[] = "GameData.bin";
    // A game file's room: the console's, or what this game's data needs, with mods adding galaxies and stars.
    u32 sGameDataRoom = 0xF80;
    // The buffers: past the file a save can be, even one made with a mod whose data was larger.
    u32 sBufferRoom = 0x10000;
    const u32 cConfigRoom = 0x60;
    const u32 cSaveFiles = 3;
    u32 roomOf(u32 file) {
        return file < 2 ? sGameDataRoom : cConfigRoom;
    }
    const char* const cSaveFileNames[] = {"mario", "luigi", "config"};
    const SaveFileSpec cSaveFileSpecSystem = {"sysconf", 0x80};
};  // namespace

// The file table keeps its system entry so a save stays the size every older save is (and that older builds read): nothing lives in it
// now. An empty file: its version, no chunks.
static void emptyFile(u8* pData, u32 size) {
    MR::zeroMemory(pData, size);
    pData[0] = 1;
}

namespace {
    NEW_NERVE(SaveDataHandlerWait, SaveDataHandler, Wait);
    NEW_NERVE(SaveDataHandlerProcessing, SaveDataHandler, Processing);
    NEW_NERVE(SaveDataHandlerSaveProcessingGameData, SaveDataHandler, SaveProcessingGameData);
    NEW_NERVE(SaveDataHandlerRemoveProcessingGameData, SaveDataHandler, RemoveProcessingGameData);
};  // namespace

SaveDataHandler::SaveDataHandler(const UserFile* pUserFile)
    : NerveExecutor("SaveDataHandler"), mSaveRequest(), _C(), _14(), _18() {
    mSaveRequest = new SaveRequest();

    // What a game file takes now, and room for what grows as it's played.
    u8* pProbe = new u8[0x100000];
    const u32 needed = pUserFile->mGameDataHolder->makeFileBinary(pProbe, 0x100000);
    delete[] pProbe;
    if (needed + 1024 > sGameDataRoom) {
        sGameDataRoom = (needed + 1024 + 31) & ~31u;
    }
    sBufferRoom = 0x10000;
    if ((sGameDataRoom * 2 + cConfigRoom) * 6 + 0x1000 > sBufferRoom) {
        sBufferRoom = (sGameDataRoom * 2 + cConfigRoom) * 6 + 0x1000;
    }
    // A save from a mod with more data may be larger than this game needs: it's read, and comes down to what this game keeps.
    sBufferRoom *= 2;

    createCommunicationBuffer();
    resetSaveData(_18);
    initializeAllFileInSaveData(_18, pUserFile);


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

// `copy` 0 is the save; 1 and 2 are the host's copies of the last two (save_files.cpp).
void SaveDataHandler::requestLoadSaveData(int copy) {
    static const char* const cNames[] = {"GameData.bin", "GameData.bin.1", "GameData.bin.2"};
    MR::zeroMemory(_14, sBufferRoom);
    mSaveRequest->setReadSeq(cNames[copy], _14, sBufferRoom, &_C);
    mSaveRequest->start();

    setNerve(GET_NERVE_ANON(SaveDataHandlerProcessing));
}

bool SaveDataHandler::requestVerifyAfterLoadGameDataFile() {
    // Old saves are version 2, big-endian or little-endian; a little-endian one is laid out as version 3.
    NebulaSaveFormat::Header header;
    const NebulaSaveFormat::Error error = NebulaSaveFormat::validate(_14, _C, header);
    if (error != NebulaSaveFormat::Error::None) {
        JUTNativeLog("save: %s\n", NebulaSaveFormat::message(error));
        return false;
    }
    const bool oldOrder = header.format == NebulaSaveFormat::Format::Wii2;
    gOldSaveOrder = oldOrder;
    JUTNativeLog("save: read a %s save\n", oldOrder ? "old order" : header.format == NebulaSaveFormat::Format::Switch2 ? "Switch" : "Nebula");

    SaveDataFileAccessor file(_14);
    if (!isCorrectFileHeader(_14)) {
        gOldSaveOrder = false;
        return false;
    }

    if (oldOrder) {
        convertFileHeader(_14);
    } else {
        file.setVersion(cSaveFileVersion);
    }

    copySaveDataEachFile(_18, _14);

    if (oldOrder) {
        convertUserFiles(_18);
    }

    return true;
}

// The header and file table of a version 2 file (read in the old order) in host order; its file's contents are
// converted after they are copied.
void SaveDataHandler::convertFileHeader(u8* pBuffer) {
    SaveDataFileAccessor file(pBuffer);
    const u32 fileSize = file.getFileSize();
    const u32 num = file.getUserFileInfoNum();
    u32 offsets[24];
    for (u32 i = 0; i < num; i++) {
        offsets[i] = file.getFileOffset(i);
    }
    const u32 checkSum = file.getCheckSum();
    gOldSaveOrder = false;
    file.setCheckSum(checkSum);
    file.setVersion(cSaveFileVersion);
    file.setUserFileInfoNum(num);
    file.setFileSize(fileSize);
    for (u32 i = 0; i < num; i++) {
        file.setFileOffset(i, offsets[i]);
    }
}

// Each user file of a version 2 save, read in the old order and written back in host order. The next save writes
// version 3.
void SaveDataHandler::convertUserFiles(u8* pBuffer) {
    SaveDataFileAccessor file(pBuffer);
    UserFile* pUserFile = new UserFile();
    const int num = file.getUserFileInfoNum();

    for (int i = 0; i < num; i++) {
        const char* pName = file.getFileName(i);
        SaveDataUserFileInfo info;
        file.makeUserFileInfo(&info, pName);
        if (info.mData == nullptr) {
            continue;
        }

        pUserFile->resetAllData();
        gOldSaveOrder = true;
        // A damaged file stays as it is, so its slot reads as damaged and not as an empty file.
        if (info.mKind == 0) {
            pUserFile->loadFromGameDataBinary(pName, info.mData, info.mDataSize);
            gOldSaveOrder = false;
            if (!pUserFile->mIsGameDataCorrupted) {
                pUserFile->makeGameDataBinary(info.mData, info.mDataSize);
            }
        } else if (info.mKind == 1) {
            pUserFile->loadFromConfigDataBinary(pName, info.mData, info.mDataSize);
            gOldSaveOrder = false;
            if (!pUserFile->mIsConfigDataCorrupted) {
                pUserFile->makeConfigDataBinary(info.mData, info.mDataSize);
            }
        } else {
            gOldSaveOrder = false;
            emptyFile(info.mData, info.mDataSize);
        }
    }

    delete pUserFile;
}

void SaveDataHandler::initializeUserFileMemory(int index, const UserFile* pUserFile) {
    SaveDataFileAccessor fileAccessor = SaveDataFileAccessor(_18);

    for (u32 i = 0; i < ::cSaveFiles; i++) {
        char fileName[16];
        nebula_snprintf(fileName, sizeof(fileName), "%s%1d", ::cSaveFileNames[i], index);

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

    for (u32 i = 0; i < ::cSaveFiles; i++) {
        char srcFileName[16];
        nebula_snprintf(srcFileName, sizeof(srcFileName), "%s%1d", ::cSaveFileNames[i], indexSrc);

        SaveDataUserFileInfo srcUserFileInfo;
        fileAccessor.makeUserFileInfo(&srcUserFileInfo, srcFileName);

        char dstFileName[16];
        nebula_snprintf(dstFileName, sizeof(dstFileName), "%s%1d", ::cSaveFileNames[i], indexDst);

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
    return sGameDataRoom;
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

    // Saved or not, the sequence ends.
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
    file.setVersion(cSaveFileVersion);
    file.setUserFileInfoNum(19);
    file.setFileSize(0);

    int fileIndex = 0;

    for (int i = 0; i < 6; i++) {
        for (u32 j = 0; j < ::cSaveFiles; j++) {
            char* pName = file.getFileName(fileIndex);

            MR::zeroMemory(pName, SaveDataFileAccessor::cNameSize);

            char fileName[256];
            nebula_snprintf(fileName, sizeof(fileName), "%s%1d", ::cSaveFileNames[j], i + 1);

            nebula_snprintf(pName, SaveDataFileAccessor::cNameSize, "%s", fileName);
            file.setFileOffset(fileIndex, offset);

            MR::zeroMemory(pBuffer + offset, ::roomOf(j));
            offset += ::roomOf(j);
            fileIndex++;
        }
    }

    char* pSystemName = file.getFileName(fileIndex);

    MR::zeroMemory(pSystemName, SaveDataFileAccessor::cNameSize);

    nebula_snprintf(pSystemName, SaveDataFileAccessor::cNameSize, "%s", ::cSaveFileSpecSystem.mName);
    file.setFileOffset(fileIndex, offset);

    MR::zeroMemory(pBuffer + offset, ::cSaveFileSpecSystem.mBufferSize);

    file.setFileSize(offset + ::cSaveFileSpecSystem.mBufferSize);

    u32 padding = NebulaAlignUp32(offset + ::cSaveFileSpecSystem.mBufferSize) - file.getFileSize();
    u8* pPadding = pBuffer + file.getFileSize();
    for (u32 i = 0; i < padding; i++) {
        pPadding[i] = 0;
    }
}

void SaveDataHandler::initializeAllFileInSaveData(u8* pBuffer, const UserFile* pUserFile) {
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
            emptyFile(userFileInfo.mData, userFileInfo.mDataSize);
        }
    }
}

bool SaveDataHandler::isCorrectFileHeader(const u8* pBuffer) {
    const SaveDataFileAccessor file(const_cast< u8* >(pBuffer));

    if (file.getFileSize() >= sBufferRoom) {
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
            const u32 copySize = srcUserFileInfo.mDataSize < dstUserFileInfo.mDataSize ? srcUserFileInfo.mDataSize : dstUserFileInfo.mDataSize;

            MR::copyMemory(dstUserFileInfo.mData, srcUserFileInfo.mData, copySize);

            if (copySize < dstUserFileInfo.mDataSize) {
                MR::fillMemory(dstUserFileInfo.mData + copySize, 0, dstUserFileInfo.mDataSize - copySize);
            }
        }
    }
}

void SaveDataHandler::createCommunicationBuffer() {
    _14 = new (MR::getStationedHeap(), 32) u8[sBufferRoom];
    _18 = new (MR::getStationedHeap(), 32) u8[sBufferRoom];
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
