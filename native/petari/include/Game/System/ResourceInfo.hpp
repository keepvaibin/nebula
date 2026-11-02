#pragma once


class ResFileInfo {
public:
    ResFileInfo();

    void setName(const char*, bool);

    inline bool isEqualHashCode(u32 hash) const {
        return mHashCode == hash;
    }

    void* mResource;
    u32 _4;
    void* _8;
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
    const char* findResName(const void*) const;
    const char* getResName(const void*) const;

    ResFileInfo* mFileInfoTable;
    u32 mCount;

private:
    void* findRes(const char*) const;
};
