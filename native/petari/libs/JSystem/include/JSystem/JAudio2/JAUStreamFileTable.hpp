#pragma once

#include "JSystem/JAudio2/JAIStreamDataMgr.hpp"

// bsft, in host order (cook/audio_data.rs).
struct BinaryStreamFileTable {
    char mIdentifier[4];
    u32 mNumFiles;
    s32 mFilePathOffsets[];
};

struct JAUStreamFileTable {
    JAUStreamFileTable();
    void init(void const*);
    u32 getNumFiles() const;
    const char* getFilePath(int) const;

    bool isValid() {
        return mData != nullptr;
    }

    const BinaryStreamFileTable* mData;
};

struct JAUStreamDataMgr_StreamFileTable : public JAIStreamDataMgr, JAUStreamFileTable {
    virtual s32 getStreamFileEntry(JAISoundID);
    virtual ~JAUStreamDataMgr_StreamFileTable() {
    }
};
