#pragma once

#include "Game/System/SaveBinary.hpp"

// GameData.bin: a header (checksum of the rest, version, entry count, file size), then one entry per user file (a
// 12-byte name and the file's offset), then the files. Read and written field by field (SaveBinary.hpp).
enum SaveDataUserFileKind {
    SaveDataUserFileKind_Game = 0,     // "mario*", "luigi*"
    SaveDataUserFileKind_Config = 1,   // every other name
    SaveDataUserFileKind_SysConf = 2,  // "sysconf*"
};

struct SaveDataUserFileInfo {
    u8* mData;
    u32 mDataSize;
    u8 mKind;  // SaveDataUserFileKind
};

class SaveDataFileAccessor {
public:
    static const u32 cHeaderSize = 0x10;
    static const u32 cInfoSize = 0x10;
    static const u32 cNameSize = 12;

    SaveDataFileAccessor(u8*);

    u32 getVersion() const { return MR::loadSaveU32(mFile + 0x4); }
    u32 getUserFileInfoNum() const { return MR::loadSaveU32(mFile + 0x8); }
    u32 getFileSize() const { return MR::loadSaveU32(mFile + 0xC); }
    // The entry's name: cNameSize bytes, NUL-padded.
    char* getFileName(int index) const { return reinterpret_cast< char* >(mFile + cHeaderSize + index * cInfoSize); }
    u32 getFileOffset(int index) const { return MR::loadSaveU32(mFile + cHeaderSize + index * cInfoSize + cNameSize); }
    void makeUserFileInfo(SaveDataUserFileInfo*, const char*);

private:
    u8* mFile;
};
