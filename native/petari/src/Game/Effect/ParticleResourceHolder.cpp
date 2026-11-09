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
    : mResourceMgr(), mAutoEffectList(new Table), mParticleNames(new Table), mNumEffectNums() {
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

void ParticleResourceHolder::countAutoEffectNum() {
    for (TableIter iter = mAutoEffectList->begin(); iter != mAutoEffectList->end(); iter.mIndex++) {
        const char* pGroupName = nullptr;
        iter.getValue("GroupName", &pGroupName);

        if (pGroupName != nullptr) {
            bool found = false;

            for (EffectNum** pParticle = mEffectNums; pParticle != mEffectNums + mNumEffectNums; pParticle++) {
                if (MR::isEqualStringCase((*pParticle)->mGroupName, pGroupName)) {
                    (*pParticle)->mCount++;
                    found = true;
                    break;
                }
            }

            if (!found) {
                EffectNum* pParticle = new EffectNum(pGroupName);
                const int index = mNumEffectNums++;
                mEffectNums.at(index) = pParticle;
            }
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

    for (EffectNum* const* pParticle = mEffectNums; pParticle != mEffectNums + mNumEffectNums; pParticle++) {
        if (MR::isEqualStringCase((*pParticle)->mGroupName, pGroupName)) {
            return (*pParticle)->mCount;
        }
    }

    return 0;
}
