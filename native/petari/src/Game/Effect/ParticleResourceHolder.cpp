#include "Game/Effect/ParticleResourceHolder.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/Table.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/AssetFolder.hpp"
#include "Game/Particle/ParticleResources.hpp"
#include <cstring>

namespace {
    // Byte-wise order of two names: negative, zero or positive.
    int compareNames(const char* pA, const char* pB) {
        const u8* pFirst = reinterpret_cast< const u8* >(pA);
        const u8* pSecond = reinterpret_cast< const u8* >(pB);
        while (*pFirst != 0 && *pFirst == *pSecond) {
            pFirst++;
            pSecond++;
        }
        return static_cast< int >(*pFirst) - static_cast< int >(*pSecond);
    }
};  // namespace

ParticleResourceHolder::ParticleResourceHolder(const char* pArchiveName)
    : mResourceMgr(), mAutoEffectList(new Table), mParticleNames(new Table), mEffectNums(nullptr), mNumEffectNums(), mGroupIndex(),
      mNextRows(nullptr) {
    AssetFolder* pArchive = MR::getArchive(pArchiveName);
    mResourceMgr = new ParticleResources(pArchive->getResource("Particles.jpc"), NebulaArena::current());
    mParticleNames->attach(pArchive->getResource("ParticleNames.bcsv"));
    mAutoEffectList->attach(pArchive->getResource("AutoEffectList.bcsv"));
    countAutoEffectNum();
}

const char* ParticleResourceHolder::getParticleName(u32 index) const {
    const char* pName = nullptr;
    if (mParticleNames == nullptr || index >= mParticleNames->getNumEntries() ||
        !mParticleNames->getValue(index, "name", &pName)) {
        return nullptr;
    }
    return pName;
}

u16 ParticleResourceHolder::getUserIndex(const char* pName) const {
    const Table* pInfo = mParticleNames;
    int first = 0;
    int last = pInfo->getNumEntries() - 1;

    while (first < last) {
        const int middle = (first + last) / 2;
        const char* pMiddleName = "";

        if (!pInfo->getValue(middle, "name", &pMiddleName)) {
            return 0xFFFF;
        }

        const int comparison = compareNames(pMiddleName, pName);

        if (comparison == 0) {
            return middle;
        }

        if (comparison < 0) {
            first = middle + 1;
        } else {
            last = middle;
        }
    }

    const char* pEntryName = "";

    if (!pInfo->getValue(first, "name", &pEntryName)) {
        return 0xFFFF;
    }

    if (MR::isEqualString(pEntryName, pName)) {
        return first;
    }

    // Mods' particles come after the game's sorted names.
    for (int row = pInfo->getNumEntries() - 1; row >= 0; row--) {
        if (pInfo->getValue(row, "name", &pEntryName) && MR::isEqualString(pEntryName, pName)) {
            return row;
        }
    }

    return 0xFFFF;
}

// One pass: at most one group per row.
void ParticleResourceHolder::countAutoEffectNum() {
    const s32 rows = static_cast< s32 >(mAutoEffectList->getNumEntries());
    mEffectNums = new EffectNum[rows > 0 ? rows : 1];
    mNextRows = new s32[rows > 0 ? rows : 1];
    mGroupIndex.reset(static_cast< u32 >(rows));

    for (TableIter iter = mAutoEffectList->begin(); iter != mAutoEffectList->end(); iter.mIndex++) {
        const s32 row = iter.mIndex;
        mNextRows[row] = -1;

        const char* pGroupName = nullptr;
        iter.getValue("GroupName", &pGroupName);

        if (pGroupName == nullptr) {
            continue;
        }

        const u32 group = mGroupIndex.find(pGroupName);
        if (group == MR::NameIndexOf< MR::NameKeyCase >::cNoValue) {
            EffectNum& rGroup = mEffectNums[mNumEffectNums];
            rGroup.mGroupName = pGroupName;
            rGroup.mCount = 1;
            rGroup.mFirstRow = row;
            rGroup.mLastRow = row;
            mGroupIndex.add(pGroupName, static_cast< u32 >(mNumEffectNums));
            mNumEffectNums++;
        } else {
            EffectNum& rGroup = mEffectNums[group];
            rGroup.mCount++;
            mNextRows[rGroup.mLastRow] = row;
            rGroup.mLastRow = row;
        }
    }
}

void ParticleResourceHolder::swapTexture(const ResTIMG* pImage, const char* pName) {
    mResourceMgr->swapTexture(pImage, pName);
}

bool ParticleResourceHolder::isExistInResource(const char* pName, u16* pIndex) const {
    const u16 index = getUserIndex(pName);

    if (index == 0xFFFF) {
        return false;
    }

    if (pIndex != nullptr) {
        *pIndex = index;
    }

    return true;
}

Table* ParticleResourceHolder::getAutoEffectListBinary() const {
    return mAutoEffectList;
}

int ParticleResourceHolder::getAutoEffectNum(const char* pGroupName) const {
    if (pGroupName == nullptr) {
        return 0;
    }

    const u32 group = mGroupIndex.find(pGroupName);
    return group != MR::NameIndexOf< MR::NameKeyCase >::cNoValue ? mEffectNums[group].mCount : 0;
}

s32 ParticleResourceHolder::getAutoEffectFirstRow(const char* pGroupName) const {
    if (pGroupName == nullptr) {
        return -1;
    }

    const u32 group = mGroupIndex.find(pGroupName);
    return group != MR::NameIndexOf< MR::NameKeyCase >::cNoValue ? mEffectNums[group].mFirstRow : -1;
}

s32 ParticleResourceHolder::getAutoEffectNextRow(s32 row) const {
    return mNextRows[row];
}
