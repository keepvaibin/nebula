#include "Game/Effect/ParticleResourceHolder.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/Table.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/AssetFolder.hpp"
#include "Game/Particle/ParticleResources.hpp"
#include <nebula_module.h>

ParticleResourceHolder::ParticleResourceHolder(const char* pArchiveName)
    : mResourceMgr(), mAutoEffectList(new Table), mParticleNames(new Table), mEffectNums(nullptr), mNumEffectNums(), mGroupIndex(),
      mNextRows(nullptr), mUserIndices() {
    AssetFolder* pArchive = MR::getArchive(pArchiveName);
    mResourceMgr = new ParticleResources(pArchive->getResource("Particles.jpc"), NebulaArena::current());
    mParticleNames->attach(pArchive->getResource("ParticleNames.bcsv"));
    mAutoEffectList->attach(pArchive->getResource("AutoEffectList.bcsv"));
    countAutoEffectNum();

    // The game's names are sorted and unique; mods' rows follow them (cook/overlay.rs). A name held twice keeps its first row.
    const s32 rows = mParticleNames->getNumEntries();
    const s32 column = rows > 0 ? mParticleNames->searchItemInfo("name") : -1;
    mUserIndices.reset(column >= 0 ? static_cast< u32 >(rows) : 0);
    u32 heldTwice = 0;
    for (s32 row = 0; column >= 0 && row < rows && row < 0xFFFF; row++) {
        const char* pName = "";
        mParticleNames->getValueFast(row, column, &pName);
        if (!mUserIndices.add(pName, static_cast< u32 >(row))) {
            heldTwice++;
        }
    }
    if (heldTwice != 0) {
        nebula_log("particle names: %u of %d held twice (the first row is used)\n", heldTwice, rows);
    }
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
    const u32 row = mUserIndices.find(pName);
    return row != MR::NameIndex::cNoValue ? static_cast< u16 >(row) : 0xFFFF;
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
