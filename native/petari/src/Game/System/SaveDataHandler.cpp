#include "Game/System/SaveDataHandler.hpp"
#include "Game/System/ConfigDataHolder.hpp"
#include "Game/System/GameDataHolder.hpp"
#include "Game/System/LumaSave.hpp"
#include "Game/System/LumaSaveChunk.hpp"
#include "Game/System/SaveBinary.hpp"
#include "Game/System/SaveDataFileAccessor.hpp"
#include "Game/System/SaveFileFormat.hpp"
#include "Game/System/SaveRequest.hpp"
#include "Game/System/UserFile.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include <nebula_module.h>
#include <cstring>

namespace {
    // Nebula.sav: "NBSV", the version, the CRC-32 of everything after the header, the file count; a row per file
    // (its name, its size); the files, side by side. Little-endian.
    const char cSaveFileName[] = "Nebula.sav";
    const char* const cReadNames[] = {
        "Nebula.sav", "Nebula.sav.1", "Nebula.sav.2", "GameData.bin", "GameData.bin.1", "GameData.bin.2",
    };
    const u32 cMagic = 0x5653424E;
    const u32 cVersion = 4;
    const u32 cHeaderSize = 16;
    const u32 cRowNameSize = 16;
    const u32 cRowSize = cRowNameSize + 4;
    const u32 cUserFiles = 6;
    // By SaveFileId::Kind.
    const char* const cKinds[] = {"mario", "luigi", "config"};
    const u32 cKindNum = SaveFileId::Kind_Num;

    // A file's name in Nebula.sav and the old saves: its kind, then its user ("luigi3").
    void makeFileName(char (&rName)[cRowNameSize], SaveFileId file) {
        nebula_snprintf(rName, sizeof(rName), "%s%1d", cKinds[file.mKind], file.mUser);
    }

    // The file of a name makeFileName writes; false for any other name.
    bool parseFileName(const char* pName, SaveFileId* pFile) {
        for (u32 kind = 0; kind < cKindNum; kind++) {
            const u32 length = static_cast< u32 >(strlen(cKinds[kind]));
            if (!MR::isEqualStringPrefix(pName, cKinds[kind]) || pName[length] == '\0' || pName[length + 1] != '\0') {
                continue;
            }
            const u32 user = static_cast< u32 >(pName[length] - '0');
            if (user < 1 || user > cUserFiles) {
                return false;
            }
            pFile->mKind = static_cast< SaveFileId::Kind >(kind);
            pFile->mUser = static_cast< s32 >(user);
            return true;
        }
        return false;
    }

    u32 sCrcTable[256];

    void initCrcTable() {
        for (u32 i = 0; i < 256; i++) {
            u32 value = i;
            for (s32 bit = 0; bit < 8; bit++) {
                value = (value & 1) != 0 ? 0xEDB88320u ^ (value >> 1) : value >> 1;
            }
            sCrcTable[i] = value;
        }
    }

    u32 crc32(const u8* pData, u32 size) {
        u32 value = 0xFFFFFFFFu;
        for (u32 i = 0; i < size; i++) {
            value = sCrcTable[(value ^ pData[i]) & 0xFF] ^ (value >> 8);
        }
        return value ^ 0xFFFFFFFFu;
    }

    u32 readU32(const u8* pSrc) {
        u32 value;
        memcpy(&value, pSrc, sizeof(value));
        return value;
    }

    void writeU32(u8* pDst, u32 value) {
        memcpy(pDst, &value, sizeof(value));
    }
}  // namespace

SaveDataHandler::SaveDataHandler(const UserFile* pUserFile)
    : mSaveRequest(new SaveRequest()), mReadIndex(0), mReadLength(0), mBuffer(nullptr), mBufferRoom(0), mFiles(nullptr),
      mMake(nullptr), mMakeRoom(0), mBaseRoom(0) {
    initCrcTable();

    // What a file takes in this game with its mods, and room beside it.
    const u32 cProbeRoom = 0x100000;
    u8* pProbe = new u8[cProbeRoom];
    const u32 needed = pUserFile->makeGameDataBinary(pProbe, cProbeRoom);
    delete[] pProbe;
    mBaseRoom = needed * 2 + 0x1000;
    mMakeRoom = mBaseRoom;
    mMake = new (Arenas::permanent(), 0) u8[mMakeRoom];
    mBufferRoom = (mMakeRoom + cRowSize) * cUserFiles * cKindNum + cHeaderSize;
    mBuffer = new (Arenas::permanent(), 0) u8[mBufferRoom];

    mFiles = new (Arenas::permanent(), 0) File[cUserFiles * cKindNum];
    for (u32 i = 0; i < cUserFiles * cKindNum; i++) {
        mFiles[i].mData = nullptr;
        mFiles[i].mSize = 0;
        mFiles[i].mRoom = 0;
    }

    for (u32 user = 0; user < cUserFiles; user++) {
        initializeUserFileMemory(user + 1, pUserFile);
    }
}

void SaveDataHandler::waitRequest() const {
    while (!mSaveRequest->isDone()) {
#if defined(__x86_64__) || defined(__i386__)
        __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
        __asm__ volatile("yield");
#endif
    }
}

bool SaveDataHandler::requestLoadSaveData(int index) {
    if (static_cast< u32 >(index) >= ARRAY_SIZEU(cReadNames)) {
        return false;
    }

    waitRequest();

    mReadIndex = index;
    mReadLength = 0;
    mSaveRequest->setReadSeq(cReadNames[index], mBuffer, mBufferRoom, &mReadLength);
    mSaveRequest->start();
    return true;
}

bool SaveDataHandler::acceptLoaded() {
    return readNative() || readOld();
}

bool SaveDataHandler::readNative() {
    if (mReadLength < cHeaderSize || readU32(mBuffer) != cMagic) {
        return false;
    }

    const u32 version = readU32(mBuffer + 4);
    const u32 count = readU32(mBuffer + 12);
    if (version != cVersion || count > (mReadLength - cHeaderSize) / cRowSize ||
        readU32(mBuffer + 8) != crc32(mBuffer + cHeaderSize, mReadLength - cHeaderSize)) {
        nebula_log("save: %s is damaged\n", cReadNames[mReadIndex]);
        return false;
    }

    const u32 rows = cHeaderSize + count * cRowSize;
    u32 place = rows;
    for (u32 i = 0; i < count; i++) {
        const u32 size = readU32(mBuffer + cHeaderSize + i * cRowSize + cRowNameSize);
        if (size > mReadLength - place) {
            nebula_log("save: %s is damaged\n", cReadNames[mReadIndex]);
            return false;
        }
        place += size;
    }

    place = rows;
    for (u32 i = 0; i < count; i++) {
        const u8* const pRow = mBuffer + cHeaderSize + i * cRowSize;
        char name[cRowNameSize + 1];  // a row's name in Nebula.sav is cRowNameSize bytes; the terminator is ours
        memcpy(name, pRow, cRowNameSize);
        name[cRowNameSize] = '\0';
        const u32 size = readU32(pRow + cRowNameSize);
        SaveFileId file;
        if (parseFileName(name, &file)) {
            setFile(file, mBuffer + place, size);
        }
        place += size;
    }

    nebula_log("save: read %s\n", cReadNames[mReadIndex]);
    return true;
}

// A save of the old versions, once: the new file is written from what this keeps, the old file stays as it was.
bool SaveDataHandler::readOld() {
    NebulaSaveFormat::Header header;
    const NebulaSaveFormat::Error error = NebulaSaveFormat::validate(mBuffer, mReadLength, header);
    if (error != NebulaSaveFormat::Error::None) {
        nebula_log("save: %s: %s\n", cReadNames[mReadIndex], NebulaSaveFormat::message(error));
        return false;
    }

    const bool oldOrder = header.format == NebulaSaveFormat::Format::Wii2;
    UserFile* pUserFile = oldOrder ? new UserFile() : nullptr;
    gOldSaveOrder = oldOrder;
    SaveDataFileAccessor file(mBuffer);
    const u32 num = file.getUserFileInfoNum();
    for (u32 i = 0; i < num; i++) {
        const char* const pName = file.getFileName(i);
        SaveDataUserFileInfo info;
        file.makeUserFileInfo(&info, pName);
        SaveFileId id;
        if (info.mData == nullptr || info.mKind == SaveDataUserFileKind_SysConf || !parseFileName(pName, &id)) {
            continue;
        }

        if (!oldOrder) {
            setFile(id, info.mData, info.mDataSize);
            continue;
        }

        // In the old order: read, and made again in host order. A damaged file stays as it is, so its slot reads as damaged.
        pUserFile->resetAllData();
        u32 size = 0;
        if (info.mKind == SaveDataUserFileKind_Game) {
            pUserFile->loadFromGameDataBinary(id, info.mData, info.mDataSize);
            gOldSaveOrder = false;
            if (!pUserFile->mIsGameDataCorrupted) {
                fitMake(pUserFile);
                size = pUserFile->makeGameDataBinary(mMake, mMakeRoom);
            }
        } else {
            pUserFile->loadFromConfigDataBinary(id, info.mData, info.mDataSize);
            gOldSaveOrder = false;
            if (!pUserFile->mIsConfigDataCorrupted) {
                size = pUserFile->makeConfigDataBinary(mMake, mMakeRoom);
            }
        }

        if (size != 0) {
            setFile(id, mMake, size);
        } else {
            setFile(id, info.mData, info.mDataSize);
        }
        gOldSaveOrder = true;
    }

    gOldSaveOrder = false;
    delete pUserFile;
    nebula_log("save: read %s, an old save\n", cReadNames[mReadIndex]);
    return true;
}

SaveDataHandler::File* SaveDataHandler::findFile(SaveFileId file) const {
    if (file.mUser < 1 || static_cast< u32 >(file.mUser) > cUserFiles || file.mKind >= cKindNum) {
        return nullptr;
    }

    return &mFiles[(file.mUser - 1) * cKindNum + file.mKind];
}

void SaveDataHandler::setFile(SaveFileId file, const u8* pData, u32 size) {
    File* const pFile = findFile(file);
    if (pFile == nullptr) {
        return;
    }

    if (size > pFile->mRoom) {
        delete[] pFile->mData;
        pFile->mData = new (Arenas::permanent(), 0) u8[size];
        pFile->mRoom = size;
    }

    memcpy(pFile->mData, pData, size);
    pFile->mSize = size;
    // The REMOVED MODS line's count of mods' values in memory (services-save 5).
    if (file.mKind != SaveFileId::Kind_Config) {
        luma::save::fileStored(static_cast< u32 >(file.mUser - 1) * 2 + file.mKind, pFile->mData, pFile->mSize);
    }
}

bool SaveDataHandler::forgetLumaMod(const char* pId) {
    bool held = false;
    for (u32 user = 1; user <= cUserFiles; user++) {
        for (u32 kind = 0; kind < SaveFileId::Kind_Config; kind++) {
            const SaveFileId id = {static_cast< SaveFileId::Kind >(kind), static_cast< s32 >(user)};
            const File* const pFile = findFile(id);
            u32 at = 0;
            u32 chunkSize = 0;
            if (pFile == nullptr || !LumaSaveChunk::findInFile(pFile->mData, pFile->mSize, &at, &chunkSize)) {
                continue;
            }
            LumaSaveChunk values(nullptr);
            if (values.parse(pFile->mData + at + BinaryDataChunk::cDataOffset, chunkSize - BinaryDataChunk::cDataOffset) !=
                    LumaSaveChunk::Result_Ok ||
                values.getModBytes(pId) == 0) {
                continue;
            }
            held = true;
            values.forgetMod(pId);
            // The file again with the chunk's data shorter: the part before it, its header with the new size, the data,
            // and the chunks after it as they were.
            const u32 dataSize = values.getDataSize();
            const u32 newChunk = BinaryDataChunk::cDataOffset + dataSize;
            const u32 tail = pFile->mSize - at - chunkSize;
            const u32 newSize = at + newChunk + tail;
            u8* const pNew = new u8[newSize];
            if (pNew == nullptr) {
                continue;  // no memory now: the next save tries again (the id stays forgotten)
            }
            memcpy(pNew, pFile->mData, at + BinaryDataChunk::cDataOffset);
            MR::storeSaveU32(pNew + at + BinaryDataChunk::cSize, newChunk);
            (void)values.serialize(pNew + at + BinaryDataChunk::cDataOffset, dataSize);
            memcpy(pNew + at + newChunk, pFile->mData + at + chunkSize, tail);
            setFile(id, pNew, newSize);
            delete[] pNew;
        }
    }
    return held;
}

const u8* SaveDataHandler::getFile(SaveFileId file, u32* pSize) const {
    const File* const pFile = findFile(file);
    *pSize = pFile != nullptr ? pFile->mSize : 0;
    return pFile != nullptr ? pFile->mData : nullptr;
}

void SaveDataHandler::initializeUserFileMemory(int index, const UserFile* pUserFile) {
    fitMake(pUserFile);
    for (u32 kind = 0; kind < cKindNum; kind++) {
        const u32 size = kind != SaveFileId::Kind_Config ? pUserFile->makeGameDataBinary(mMake, mMakeRoom)
                                                         : pUserFile->makeConfigDataBinary(mMake, mMakeRoom);
        setFile({static_cast< SaveFileId::Kind >(kind), index}, mMake, size);
    }
}

void SaveDataHandler::copyUserFileMemory(int indexDst, int indexSrc) {
    for (u32 kind = 0; kind < cKindNum; kind++) {
        u32 size;
        const u8* const pData = getFile({static_cast< SaveFileId::Kind >(kind), indexSrc}, &size);
        setFile({static_cast< SaveFileId::Kind >(kind), indexDst}, pData, size);
    }
}

void SaveDataHandler::storeUserFile(const UserFile* pUserFile) {
    luma::save::applyForgets(this, pUserFile->mGameDataHolder);
    fitMake(pUserFile);
    u32 size = pUserFile->makeGameDataBinary(mMake, mMakeRoom);
    setFile(pUserFile->getGameDataFile(), mMake, size);
    size = pUserFile->makeConfigDataBinary(mMake, mMakeRoom);
    setFile(pUserFile->getConfigDataFile(), mMake, size);
}

const char SaveDataHandler::cExportMagic[5] = "NSAV";

namespace {
    void putLittle32(u8* pAt, u32 value) {
        for (u32 i = 0; i < 4; i++) {
            pAt[i] = static_cast< u8 >(value >> (8 * i));
        }
    }

    // export's scratch for a stored file's config (is it created?); made once, on the game thread.
    ConfigDataHolder* sExportConfig = nullptr;
}  // namespace

SaveDataHandler::ExportResult SaveDataHandler::makeExport(int user, const UserFile* pLoaded, u8** ppBytes, u32* pLength) {
    *ppBytes = nullptr;
    *pLength = 0;
    const SaveFileId config = {SaveFileId::Kind_Config, user};
    if (pLoaded != nullptr) {
        if (!pLoaded->isCreated()) {
            return Export_Empty;
        }
    } else {
        const File* const pConfig = findFile(config);
        if (pConfig == nullptr || pConfig->mSize == 0) {
            return Export_Empty;
        }
        if (sExportConfig == nullptr) {
            sExportConfig = new ConfigDataHolder();
            if (sExportConfig == nullptr) {
                return Export_Memory;
            }
        }
        sExportConfig->resetAllData();
        if (!sExportConfig->loadFromFileBinary(config, pConfig->mData, pConfig->mSize) || !sExportConfig->isCreated()) {
            return Export_Empty;
        }
    }

    // The loaded file's game data, made now into mMake (as storeUserFile makes it, the forgotten ids out first).
    const SaveFileId loadedGame = pLoaded != nullptr ? pLoaded->getGameDataFile() : SaveFileId{SaveFileId::Kind_Num, 0};
    u32 made = 0;
    if (pLoaded != nullptr) {
        luma::save::applyForgets(this, pLoaded->mGameDataHolder);
        fitMake(pLoaded);
        made = pLoaded->makeGameDataBinary(mMake, mMakeRoom);
    }
    // Room: the head, three lengths, the game data parts, and the config: made into the form directly (it is at most
    // the fixed part's room), else as stored.
    u32 room = cExportHead + 4 * cKindNum;
    for (u32 kind = 0; kind < cKindNum; kind++) {
        const SaveFileId file = {static_cast< SaveFileId::Kind >(kind), user};
        if (pLoaded != nullptr && kind == SaveFileId::Kind_Config) {
            room += mBaseRoom;
        } else if (pLoaded != nullptr && kind == loadedGame.mKind) {
            room += made;
        } else {
            const File* const pFile = findFile(file);
            room += pFile != nullptr ? pFile->mSize : 0;
        }
    }
    u8* const pBytes = new u8[room];
    if (pBytes == nullptr) {
        return Export_Memory;
    }
    memcpy(pBytes, cExportMagic, 4);
    pBytes[4] = 1;  // version, little-endian u16
    pBytes[5] = 0;
    pBytes[6] = static_cast< u8 >(cKindNum);
    pBytes[7] = 0;
    u32 at = cExportHead;
    for (u32 kind = 0; kind < cKindNum; kind++) {
        u32 size = 0;
        if (pLoaded != nullptr && kind == SaveFileId::Kind_Config) {
            size = pLoaded->makeConfigDataBinary(pBytes + at + 4, room - at - 4);
        } else if (pLoaded != nullptr && kind == loadedGame.mKind) {
            size = made;
            memcpy(pBytes + at + 4, mMake, size);
        } else {
            const File* const pFile = findFile({static_cast< SaveFileId::Kind >(kind), user});
            size = pFile != nullptr ? pFile->mSize : 0;
            if (size != 0) {
                memcpy(pBytes + at + 4, pFile->mData, size);
            }
        }
        putLittle32(pBytes + at, size);
        at += 4 + size;
    }
    *ppBytes = pBytes;
    *pLength = at;
    return Export_Ok;
}

void SaveDataHandler::copyUserFile(UserFile* pDst, const UserFile* pSrc) {
    fitMake(pSrc);
    u32 size = pSrc->makeGameDataBinary(mMake, mMakeRoom);
    pDst->loadFromGameDataBinary(pSrc->getGameDataFile(), mMake, size);
    size = pSrc->makeConfigDataBinary(mMake, mMakeRoom);
    pDst->loadFromConfigDataBinary(pSrc->getConfigDataFile(), mMake, size);
}

void SaveDataHandler::fitMake(const UserFile* pUserFile) {
    const u32 room = mBaseRoom + pUserFile->mGameDataHolder->getLumaChunkRoom();
    if (room <= mMakeRoom) {
        return;
    }
    delete[] mMake;
    mMake = new (Arenas::permanent(), 0) u8[room];
    mMakeRoom = room;
}

void SaveDataHandler::requestSaveSaveData() {
    waitRequest();
    luma::save::applyForgets(this, nullptr);
    const u32 count = cUserFiles * cKindNum;
    u32 total = cHeaderSize + count * cRowSize;
    for (u32 i = 0; i < count; i++) {
        total += mFiles[i].mSize;
    }

    if (total > mBufferRoom) {
        delete[] mBuffer;
        mBufferRoom = total;
        mBuffer = new (Arenas::permanent(), 0) u8[mBufferRoom];
    }

    writeU32(mBuffer, cMagic);
    writeU32(mBuffer + 4, cVersion);
    writeU32(mBuffer + 12, count);
    u32 place = cHeaderSize + count * cRowSize;
    for (u32 i = 0; i < count; i++) {
        u8* const pRow = mBuffer + cHeaderSize + i * cRowSize;
        char name[cRowNameSize];  // the file's fixed-width row name (makeFileName fits it)
        makeFileName(name, {static_cast< SaveFileId::Kind >(i % cKindNum), static_cast< s32 >(i / cKindNum + 1)});
        memset(pRow, 0, cRowNameSize);
        memcpy(pRow, name, strlen(name));
        writeU32(pRow + cRowNameSize, mFiles[i].mSize);
        memcpy(mBuffer + place, mFiles[i].mData, mFiles[i].mSize);
        place += mFiles[i].mSize;
    }
    writeU32(mBuffer + 8, crc32(mBuffer + cHeaderSize, total - cHeaderSize));

    mReadLength = 0;
    mSaveRequest->setWriteSeq(cSaveFileName, mBuffer, total);
    mSaveRequest->start();
}

bool SaveDataHandler::isDone() {
    if (!mSaveRequest->isDone()) {
        return false;
    }

    // Larger than the room (a save made with more mods): read again with room for it.
    if (mReadLength > mBufferRoom && mSaveRequest->getResult() == SaveRequest::Result_Corrupted) {
        const u32 length = mReadLength;
        delete[] mBuffer;
        mBufferRoom = length;
        mBuffer = new (Arenas::permanent(), 0) u8[mBufferRoom];
        requestLoadSaveData(mReadIndex);
        return false;
    }

    return true;
}

SaveRequest::Result SaveDataHandler::getLastResult() const {
    return mSaveRequest->getResult();
}
