#include "Game/Animation/XanimeResource.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/System/ResourceInfo.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Render/Clip.hpp"
#include <cstring>

namespace {
    void initNonGroupInfo(XanimeGroupInfo* pInfo, const char* pName) {
        pInfo->init();
        pInfo->mRate = 1.0f;
        pInfo->mParent.mAnimationName = pName;
        pInfo->mInterpoleFrame = 1;
        pInfo->mBckTableVariant = 1;
    }
}  // namespace

XanimeResourceTable::XanimeResourceTable(ResourceHolder* pResourceHolder) {
    init();

    mResourceHolder = pResourceHolder;
    mMaxGroupInfoTableSize = 1;

    initNonGroupInfo(&mNonGroupInfo, "non-group");
}

XanimeResourceTable::XanimeResourceTable(ResourceHolder* pResourceHolder, XanimeGroupInfo* pGroupInfo, XanimeAuxInfo* pAuxInfo,
                                         XanimeOfsInfo* pOfsInfo, XanimeBckTable* pBckTable1, XanimeBckTable2* pBckTable2,
                                         XanimeBckTable3* pBckTable3, XanimeBckTable4* pBckTable4, XanimeSwapTable* pSwapTable) {
    init();
    mHasGroupTable = true;
    mGroupInfos = pGroupInfo;
    mResourceHolder = pResourceHolder;
    mMaxGroupInfoTableSize = 1;
    mAmountOfGroupInfos = initGroupInfo(pResourceHolder, pGroupInfo, pAuxInfo, pOfsInfo, pBckTable1, pBckTable2, pBckTable3, pBckTable4, pSwapTable);
    createSortTable();

    mBckTables = reinterpret_cast< XanimeBckTable1* >(pBckTable1);

    u32 count = 0;
    if (mBckTables != nullptr) {
        for (; mBckTables[count].mParent.mAnimationName[0] != '\0'; count++) {
            mBckTables[count].mAnimationHash = MR::getHashCode(mBckTables[count].mParent.mAnimationName);
            mBckTables[count].mFileHash = MR::getHashCode(mBckTables[count].mFileName);
        }
    }

    mAmountOfBckTables = count;

    initNonGroupInfo(&mNonGroupInfo, "non-group");
}

void XanimeResourceTable::init() {
    mHasGroupTable = false;
    mMaxGroupInfoTableSize = 0;
    mAmountOfGroupInfos = 0;
    mAmountOfBckTables = 0;
    mGroupInfos = nullptr;
    mBckTables = nullptr;

    mResourceHolder = nullptr;
    mSwapTable = nullptr;
}

namespace {
    // Fills the bcks and weights of a 2-, 3- or 4-bck entry; returns the first bck's file name.
    template < typename T >
    const char* setBlendBcks(const XanimeResourceTable* pTable, XanimeGroupInfo* pEntry, const T* pBckTable) {
        const u32 count = sizeof(pBckTable->mEntries) / sizeof(pBckTable->mEntries[0]);
        for (u32 i = 0; i < count; i++) {
            pEntry->mBcks[i] = pTable->findResMotion(pBckTable->mEntries[i].mFileName);
            pEntry->mWeights[i] = pBckTable->mEntries[i].mWeight;
        }

        return pBckTable->mEntries[0].mFileName;
    }
}  // namespace

u32 XanimeResourceTable::initGroupInfo(ResourceHolder*, XanimeGroupInfo* pInfo, XanimeAuxInfo* pAuxInfo, XanimeOfsInfo* pOfsInfo,
                                       XanimeBckTable* pBckTable1, XanimeBckTable2* pBckTable2, XanimeBckTable3* pBckTable3,
                                       XanimeBckTable4* pBckTable4, XanimeSwapTable* pSwapTable) {
    mSwapTable = pSwapTable;
    u32 count = 0;
    u8 maxTableSize = 1;

    for (; pInfo[count].mParent.mAnimationName[0] != '\0'; count++) {
        XanimeGroupInfo* pEntry = &pInfo[count];
        const char* pName = pEntry->mParent.mAnimationName;

        for (u32 j = 0; j < ARRAY_SIZE(pEntry->mBcks); j++) {
            pEntry->mBcks[j] = nullptr;
            pEntry->mWeights[j] = 0.0f;
        }

        pEntry->mHash = MR::getHashCode(pName);
        pEntry->mBckTableVariant = 0;

        // search() moves each pointer onto the matching entry.
        XanimeBckTable* pTable1 = reinterpret_cast< XanimeBckTable* >(pBckTable1);
        XanimeBckTable* pTable2 = reinterpret_cast< XanimeBckTable* >(pBckTable2);
        XanimeBckTable* pTable3 = reinterpret_cast< XanimeBckTable* >(pBckTable3);
        XanimeBckTable* pTable4 = reinterpret_cast< XanimeBckTable* >(pBckTable4);

        const char* pFirstFileName = nullptr;
        if (search(&pTable1, pName, sizeof(XanimeBckTable1))) {
            const XanimeBckTable1* pBck = reinterpret_cast< const XanimeBckTable1* >(pTable1);
            pEntry->mBckTableVariant = 1;
            pEntry->mWeights[0] = 1.0f;
            pEntry->mBcks[0] = findResMotion(pBck->mFileName);
            pFirstFileName = pBck->mFileName;
        } else if (search(&pTable2, pName, sizeof(XanimeBckTable2))) {
            maxTableSize = maxTableSize < 2 ? 2 : maxTableSize;
            pEntry->mBckTableVariant = 2;
            pFirstFileName = setBlendBcks(this, pEntry, reinterpret_cast< const XanimeBckTable2* >(pTable2));
        } else if (search(&pTable3, pName, sizeof(XanimeBckTable3))) {
            maxTableSize = maxTableSize < 3 ? 3 : maxTableSize;
            pEntry->mBckTableVariant = 3;
            pFirstFileName = setBlendBcks(this, pEntry, reinterpret_cast< const XanimeBckTable3* >(pTable3));
        } else if (search(&pTable4, pName, sizeof(XanimeBckTable4))) {
            maxTableSize = 4;
            pEntry->mBckTableVariant = 4;
            pFirstFileName = setBlendBcks(this, pEntry, reinterpret_cast< const XanimeBckTable4* >(pTable4));
        }

        pEntry->mBckName = pFirstFileName;

        XanimeBckTable* pAuxTable = reinterpret_cast< XanimeBckTable* >(pAuxInfo);
        if (search(&pAuxTable, pName, sizeof(XanimeAuxInfo))) {
            XanimeAuxInfo* pAux = reinterpret_cast< XanimeAuxInfo* >(pAuxTable);
            pEntry->mAuxValue = pAux->mValue;
            pEntry->mAuxOfsInfo = &pAux->mOfsInfo;
        } else {
            pEntry->mAuxValue = 0;
            pEntry->mAuxOfsInfo = nullptr;
        }

        pEntry->mStart = 0.0f;
        if (pEntry->mBcks[0] == nullptr) {
            pEntry->mEnd = 0.0f;
            pEntry->mLoop = 0.0f;
            pEntry->mAttribute = 0;
        } else {
            pEntry->mAttribute = pEntry->mBcks[0]->getAttribute();
            pEntry->mLoop = 0.0f;
            pEntry->mEnd = static_cast< f32 >(pEntry->mBcks[0]->getFrameMax());
        }

        XanimeBckTable* pOfsTable = reinterpret_cast< XanimeBckTable* >(pOfsInfo);
        if (search(&pOfsTable, pName, sizeof(XanimeOfsInfo))) {
            const XanimeOfsInfo* pOfs = reinterpret_cast< const XanimeOfsInfo* >(pOfsTable);
            pEntry->mStart = pOfs->mStart;
            pEntry->mLoop = pOfs->mLoop;
            pEntry->mEnd = pOfs->mEnd;
            pEntry->mAttribute = pOfs->mAttribute;
        }
    }

    mMaxGroupInfoTableSize = maxTableSize;

    return count;
}

const XanimeGroupInfo* XanimeResourceTable::getGroupInfo(const char* pName) const {
    const u32 index = getGroupIndex(pName);
    return index == MR::NameIndex::cNoValue ? nullptr : &mGroupInfos[index];
}

// A ghost's recorded frames name their group by hash alone.
u32 XanimeResourceTable::getIndexFromHash(u32 hash) const {
    return mGroupIndex.findHash(hash);
}

u32 XanimeResourceTable::getGroupIndex(const char* pName) const {
    return mHasGroupTable ? mGroupIndex.find(pName) : MR::NameIndex::cNoValue;
}

XanimeGroupInfo* XanimeResourceTable::getGroupInfoFromHash(u32 hash) const {
    const u32 index = getIndexFromHash(hash);
    return index == MR::NameIndex::cNoValue ? nullptr : &mGroupInfos[index];
}

u32 XanimeResourceTable::getSingleIndex(const char* pName) const {
    if (mBckTables == nullptr) {
        return MR::NameIndex::cNoValue;
    }

    const u32 hash = MR::getHashCode(pName);

    for (u32 i = 0; i < mAmountOfBckTables; i++) {
        if (hash == mBckTables[i].mAnimationHash) {
            return i;
        }
    }

    return MR::NameIndex::cNoValue;
}

void XanimeResourceTable::createSortTable() {
    mGroupIndex.reset(mAmountOfGroupInfos);
    for (u32 i = 0; i < mAmountOfGroupInfos; i++) {
        mGroupIndex.add(mGroupInfos[i].mParent.mAnimationName, mGroupInfos[i].mHash, i);
    }
}

Clip* XanimeResourceTable::findResMotion(const char* pName) const {
    const char* pFileName = swapBckName(pName, mSwapTable);
    ResTable* pTable = mResourceHolder->mMotionResTable;
    return pTable->isExistRes(pFileName) ? static_cast< Clip* >(pTable->getRes(pFileName)) : nullptr;
}

const char* XanimeResourceTable::findStringMotion(const char* pName) const {
    return mResourceHolder->mMotionResTable->findFileInfo(pName)->mName;
}

const char* XanimeResourceTable::swapBckName(const char* pOriginal, XanimeSwapTable* pTable) const {
    if (pTable == nullptr) {
        return pOriginal;
    }

    for (; pTable->mOriginal[0] != '\0'; pTable++) {
        if (MR::strcasecmp(pTable->mOriginal, pOriginal) == 0) {
            return pTable->mSwapped;
        }
    }

    return pOriginal;
}

// Walks a table of `elementSize` entries from *ppTable; on a match *ppTable is left on it.
bool XanimeResourceTable::search(XanimeBckTable** ppTable, const char* pName, u32 elementSize) const {
    if (*ppTable == nullptr) {
        return false;
    }

    while (!MR::isEqualString((*ppTable)->mAnimationName, pName)) {
        if ((*ppTable)->mAnimationName[0] == '\0') {
            return false;
        }

        *ppTable = reinterpret_cast< XanimeBckTable* >(reinterpret_cast< u8* >(*ppTable) + elementSize);
    }

    return true;
}

void XanimeGroupInfo::init() {
    mRate = 0.0f;
    mParent.mAnimationName = "not-initialized";
    mInterpoleFrame = 0;
    mStart = 0.0f;
    mEnd = 0.0f;
    mLoop = 0.0f;
    mAttribute = 0;
    mBckTableVariant = 0;
    mAuxValue = 0;
    for (u32 i = 0; i < ARRAY_SIZE(mBcks); i++) {
        mWeights[i] = 0.0f;
        mBcks[i] = nullptr;
    }

    mAuxOfsInfo = nullptr;
    mHash = 0;
    mBckName = nullptr;
}
