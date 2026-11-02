#pragma once


class HashSortTable;
class ResourceHolder;

class XanimeSwapTable {
public:
    const char* mOriginal;
    const char* mSwapped;
};

class XanimeBckTable {
public:
    const char* mAnimationName;
};

class XanimeOfsInfo {
public:
    XanimeBckTable mParent;
    f32 _4;
    f32 _8;
    f32 _C;
    u32 _10;
};

class XanimeGroupInfo {
public:
    XanimeBckTable mParent;
    f32 mRate;
    u32 _8;
    f32 mStart;
    f32 mEnd;
    f32 mLoop;
    s32 mAttribute;
    u8 mBckTableVariant;
    u8 _1D;
    void* _20[4];
    f32 mWeights[4];
    XanimeOfsInfo** _40;
    u32 mHash;
    const char* mBckName;

    void init();
};

class XanimeDirectory {
public:
    enum DIRECTORY_TYPE { Recursive = 0, Leaf = 1 };

    const char* mName;
    u8 mDirectoryType;
    union {
        XanimeDirectory* mSubDirectories;
        XanimeGroupInfo* mSubInformations;
    };
    u32 mHash;
    u32 mSize;
};

struct XanimeBckTableEntry {
    const char* mFileName;
    f32 _4;
};

class XanimeBckTable1 {
public:
    XanimeBckTable mParent;
    const char* mFileName;
    u32 mAnimationHash;
    u32 mFileHash;
};

class XanimeBckTable2 {
public:
    XanimeBckTable mParent;
    XanimeBckTableEntry mEntries[2];
};

class XanimeBckTable3 {
public:
    XanimeBckTable mParent;
    XanimeBckTableEntry mEntries[3];
};

class XanimeBckTable4 {
public:
    XanimeBckTable mParent;
    XanimeBckTableEntry mEntries[4];
};

// size is 0x18
class XanimeAuxInfo {
public:
    XanimeBckTable mParent;
    u8 _4;
    XanimeOfsInfo* _8;

    // Unreferenced members
    u8 _C[12];
};

class XanimeResourceTable {
public:
    XanimeResourceTable(

        ResourceHolder*,

        XanimeGroupInfo*, XanimeAuxInfo*, XanimeOfsInfo*,

        XanimeBckTable*, XanimeBckTable2*, XanimeBckTable3*, XanimeBckTable4*,

        XanimeSwapTable*);

    XanimeResourceTable(ResourceHolder*);

    u32 initGroupInfo(ResourceHolder*, XanimeGroupInfo*, XanimeAuxInfo*, XanimeOfsInfo*, XanimeBckTable*, XanimeBckTable2*, XanimeBckTable3*,
                      XanimeBckTable4*, XanimeSwapTable*);

    const XanimeGroupInfo* getGroupInfo(const char*) const;
    const XanimeGroupInfo* getGroupInfo(const char*, XanimeDirectory*) const;
    XanimeGroupInfo* getGroupInfoFromHash(u32) const;
    u32 getIndex(XanimeDirectory*, const char*) const;
    u32 getIndexFromHash(u32) const;
    u32 getGroupIndex(const char*) const;
    u32 getSingleIndex(const char*) const;
    u32 getSimpleIndex(const char*) const;
    const char* swapBckName(const char*, XanimeSwapTable*) const;
    void* findResMotion(const char*) const;
    const char* findStringMotion(const char*) const;
    void createSortTable();
    bool search(XanimeBckTable**, const char*, u32) const;
    void init();

    u8 _0;
    u8 mMaxGroupInfoTableSize;
    u32 mAmountOfGroupInfos;
    u32 mAmountOfBckTables;
    u32 mAmountOfSimpleGroupInfos;
    XanimeGroupInfo* mGroupInfos;
    XanimeDirectory* mDirectories;
    XanimeBckTable1* mBckTables;
    XanimeGroupInfo _1C;
    XanimeGroupInfo* mSimpleGroupInfos;
    ResourceHolder* mResourceHolder;
    HashSortTable* mSortTable;
    XanimeSwapTable* mSwapTable;
};
