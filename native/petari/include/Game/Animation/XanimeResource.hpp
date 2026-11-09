#pragma once

#include "Game/Util/NameIndex.hpp"

class Clip;
class ResourceHolder;

class XanimeSwapTable {
public:
    const char* mOriginal;
    const char* mSwapped;
};

// Every table entry starts with its animation name; a table ends at an entry whose name is "".
class XanimeBckTable {
public:
    const char* mAnimationName;
};

// Frame range override for one animation.
class XanimeOfsInfo {
public:
    XanimeBckTable mParent;
    f32 mStart;
    f32 mEnd;
    f32 mLoop;
    u32 mAttribute;
};

// One named animation: up to four bcks blended by mWeights.
class XanimeGroupInfo {
public:
    XanimeBckTable mParent;
    f32 mRate;
    u32 mInterpoleFrame;
    f32 mStart;
    f32 mEnd;
    f32 mLoop;
    s32 mAttribute;
    // Number of bcks (0 when none was found, 1..4).
    u8 mBckTableVariant;
    // From the aux table (no table has entries).
    u8 mAuxValue;
    Clip* mBcks[4];
    f32 mWeights[4];
    XanimeOfsInfo** mAuxOfsInfo;
    u32 mHash;
    const char* mBckName;

    void init();
};

struct XanimeBckTableEntry {
    const char* mFileName;
    f32 mWeight;
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

class XanimeAuxInfo {
public:
    XanimeBckTable mParent;
    u8 mValue;
    XanimeOfsInfo* mOfsInfo;
};

class XanimeResourceTable {
public:
    // A table with named groups (the player's).
    XanimeResourceTable(ResourceHolder*, XanimeGroupInfo*, XanimeAuxInfo*, XanimeOfsInfo*, XanimeBckTable*, XanimeBckTable2*, XanimeBckTable3*,
                        XanimeBckTable4*, XanimeSwapTable*);

    // A table of bare bcks, played through the non-group info.
    XanimeResourceTable(ResourceHolder*);

    u32 initGroupInfo(ResourceHolder*, XanimeGroupInfo*, XanimeAuxInfo*, XanimeOfsInfo*, XanimeBckTable*, XanimeBckTable2*, XanimeBckTable3*,
                      XanimeBckTable4*, XanimeSwapTable*);

    const XanimeGroupInfo* getGroupInfo(const char*) const;
    XanimeGroupInfo* getGroupInfoFromHash(u32) const;
    u32 getIndexFromHash(u32) const;
    u32 getGroupIndex(const char*) const;
    u32 getSingleIndex(const char*) const;
    const char* swapBckName(const char*, XanimeSwapTable*) const;
    Clip* findResMotion(const char*) const;
    const char* findStringMotion(const char*) const;
    void createSortTable();
    bool search(XanimeBckTable**, const char*, u32) const;
    void init();

    bool mHasGroupTable;
    u32 mMaxGroupInfoTableSize;
    u32 mAmountOfGroupInfos;
    u32 mAmountOfBckTables;
    XanimeGroupInfo* mGroupInfos;
    XanimeBckTable1* mBckTables;
    // The group a bare bck plays through.
    XanimeGroupInfo mNonGroupInfo;
    ResourceHolder* mResourceHolder;
    MR::NameIndex mGroupIndex;  // a group's name to its index in mGroupInfos
    XanimeSwapTable* mSwapTable;
};
