#pragma once

#include "Game/System/SaveBinary.hpp"
#include <revolution/types.h>

// GameData.bin: a header (checksum of the rest, version, entry count, file size), then one entry per user file (a
// 12-byte name and the file's offset), then the files. Read and written field by field (SaveBinary.hpp).
struct SaveDataUserFileInfo {
    /* 0x00 */ u8* mData;
    /* 0x04 */ u32 mDataSize;
    /* 0x08 */ u8 mKind;
};

class SaveDataFileAccessor {
public:
    static const u32 cHeaderSize = 0x10;
    static const u32 cInfoSize = 0x10;
    static const u32 cNameSize = 12;

    SaveDataFileAccessor(u8*);

    u32 getCheckSum() const { return MR::loadSaveU32(mFile + 0x0); }
    void setCheckSum(u32 value) { MR::storeSaveU32(mFile + 0x0, value); }
    u32 getVersion() const { return MR::loadSaveU32(mFile + 0x4); }
    void setVersion(u32 value) { MR::storeSaveU32(mFile + 0x4, value); }
    u32 getUserFileInfoNum() const { return MR::loadSaveU32(mFile + 0x8); }
    void setUserFileInfoNum(u32 value) { MR::storeSaveU32(mFile + 0x8, value); }
    u32 getFileSize() const { return MR::loadSaveU32(mFile + 0xC); }
    void setFileSize(u32 value) { MR::storeSaveU32(mFile + 0xC, value); }
    // The entry's name: cNameSize bytes, NUL-padded.
    char* getFileName(int index) const { return reinterpret_cast< char* >(mFile + cHeaderSize + index * cInfoSize); }
    u32 getFileOffset(int index) const { return MR::loadSaveU32(mFile + cHeaderSize + index * cInfoSize + cNameSize); }
    void setFileOffset(int index, u32 value) { MR::storeSaveU32(mFile + cHeaderSize + index * cInfoSize + cNameSize, value); }
    void makeUserFileInfo(SaveDataUserFileInfo*, const char*);

private:
    /* 0x00 */ u8* mFile;
};
