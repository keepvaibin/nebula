#pragma once

#include "Game/Util/NameIndex.hpp"

class ResFileInfo {
public:
    ResFileInfo();

    void setName(const char*, bool);

    inline bool isEqualHashCode(u32 hash) const {
        return mHashCode == hash;
    }

    void* mResource;
    u32 mSize;
    void* mData;
    char* mName;
    u32 mHashCode;
};

class ResTable {
public:
    ResTable();

    void newFileInfoTable(u32);
    ResFileInfo* add(const char*, void*, bool);
    const char* getResName(u32) const;
    void* getRes(u32) const;
    void* getRes(const char*) const;
    ResFileInfo* findFileInfo(const char*) const;
    ResFileInfo* getFileInfo(u32) const;
    bool isExistRes(const char*) const;
    int getResIndex(const char*) const;

    ResFileInfo* mFileInfoTable;
    u32 mCount;

private:
    // Keyed by each entry's case-folded name hash; a lookup by name takes the first entry of its hash.
    MR::NameIndexOf< MR::NameKeyCase > mIndex;

    void* findRes(const char*) const;
};
