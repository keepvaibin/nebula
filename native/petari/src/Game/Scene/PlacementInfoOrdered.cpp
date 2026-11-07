#include <nebula_game_api.h>
extern "C" const NebulaHostServices* nebula_host(void);
#include "Game/System/NebulaLoadTrace.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/Scene/PlacementInfoOrdered.hpp"
#include "Game/LiveActor/AllLiveActorGroup.hpp"
#include "Game/Map/PlanetMapCreator.hpp"
#include "Game/NameObj/ModelChangableObjFactory.hpp"
#include "Game/NameObj/NameObjFactory.hpp"
#include "Game/Util/JMapUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/SceneUtil.hpp"

namespace {
    CreationFuncPtr getCreator(const PlacementInfoOrdered::Identifier& rIdentifier) {
        if (rIdentifier.mShapeId != -1) {
            return MR::getModelChangableObjCreator(rIdentifier.mName);
        }

        return NameObjFactory::getCreator(rIdentifier.mName);
    }
};  // namespace

PlacementInfoOrdered::PlacementInfoOrdered(int count) : mIndexArray(), mIndexCount(), mSetArray(), mOrderedSetArray(), mCount(count) {
    mIndexArray = new Index[count];
    mSetArray = new SameIdSet[count];
    mOrderedSetArray = new SameIdSet*[count];
    MR::zeroMemory(mOrderedSetArray, count * sizeof(SameIdSet*));
}

void PlacementInfoOrdered::sort() {
    s32 count = getUsedArrayNum();
    for (s32 i = 0; i < count; i++) {
        SameIdSet* pSet = mOrderedSetArray[i];
        if (NameObjFactory::isPlayerArchiveLoaderObj(pSet->mName)) {
            pSet->mPriority = 0;
        } else {
            const JMapInfoIter& rIter = static_cast< Index* >(pSet->mList.mHead->mValue)->mInfoIter;
            bool fromStore;
            if (pSet->mShapeId != -1) {
                fromStore = MR::isReadResourceFromStoreAtModelChangableObj(pSet->mName, pSet->mShapeId);
            } else {
                fromStore = NameObjFactory::isReadResourceFromStore(pSet->mName, rIter);
            }

            pSet->mPriority = (fromStore != false) + 1;
        }
    }

    s32 gap = 13;
    while (gap < count) {
        gap = gap * 3 + 1;
    }

    for (gap /= 9; gap > 0; gap /= 3) {
        for (s32 i = gap; i < count; i++) {
            SameIdSet* pSet = mOrderedSetArray[i];
            s32 j = i - gap;
            while (j >= 0 && *pSet < *mOrderedSetArray[j]) {
                mOrderedSetArray[j + gap] = mOrderedSetArray[j];
                j -= gap;
            }

            mOrderedSetArray[j + gap] = pSet;
        }
    }
}

void PlacementInfoOrdered::requestFileLoad() {
    s32 count = getUsedArrayNum();
    for (s32 i = 0; i < count; i++) {
        SameIdSet* pSet = getSameIdSet(i);
        if (::getCreator(*pSet) != nullptr) {
            if (pSet->mShapeId != -1) {
                MR::requestMountModelChangableObjArchives(pSet->mName, pSet->mShapeId);
            } else {
                for (MR::BothDirList< Index >::iterator iter = pSet->mList.begin(); !iter.isEnd(); iter++) {
                    NameObjFactory::requestMountObjectArchives(pSet->mName, iter->mInfoIter);
                }
            }
        }
    }
}

void PlacementInfoOrdered::initPlacement() {
    while (!initPlacementStep(UINT64_MAX)) {}
}

bool PlacementInfoOrdered::initPlacementStep(uint64_t deadline) {
    if (!mPlacementStarted) {
        MR::startInitLiveActorSystemInfo();
        mPlacementStarted = true;
    }
    const s32 count = getUsedArrayNum();
    while (mPlacementSet < count) {
        SameIdSet* set = mOrderedSetArray[mPlacementSet];
        const CreationFuncPtr creator = ::getCreator(*set);
        if (mPlacementLink == nullptr) mPlacementLink = set->mList.mHead;
        if (creator == nullptr || mPlacementLink == nullptr) {
            ++mPlacementSet;
            mPlacementLink = nullptr;
            continue;
        }
        const JMapInfoIter& iter = static_cast<Index*>(mPlacementLink->mValue)->getInfoIter();
        if (!NebulaMods::isMissingFiles(set->mName, iter)) {
            NebulaLoadTrace::Scope trace(set->mName);
            MR::setCurrentPlacementZoneId(MR::getPlacedZoneId(iter));
            if (mInitializing == nullptr) mInitializing = creator(MR::getJapaneseObjectName(set->mName));
            MR::initLiveActorSystemInfo(iter);
            const bool ready = mInitializing->initStep(iter, deadline);
            MR::initLiveActorSystemInfo(iter);
            MR::clearCurrentPlacementZoneId();
            if (!ready) return false;
            mInitializing = nullptr;
        }
        mPlacementLink = mPlacementLink->mNextLink;
        if (mPlacementLink == nullptr) ++mPlacementSet;
        if (nebula_host()->clock_ns(nebula_host()->user) >= deadline) return mPlacementSet >= count;
    }
    return true;
}

void PlacementInfoOrdered::insert(const Identifier& rIdentifier, const JMapInfoIter& rIter) {
    SameIdSet* pSet = find(rIdentifier);
    if (pSet == nullptr) {
        pSet = createSameIdSet(rIdentifier);
    }

    pSet->mList.append(createIndex(rIter));
}

s32 PlacementInfoOrdered::getUsedArrayNum() const {
    s32 count = 0;
    s32 i = 0;
    while (i < mCount && mOrderedSetArray[i] != nullptr) {
        i++;
        count++;
    }

    return count;
}

PlacementInfoOrdered::SameIdSet* PlacementInfoOrdered::find(const Identifier& rIdentifier) const {
    s32 count = getUsedArrayNum();
    for (s32 i = 0; i < count; i++) {
        SameIdSet* pSet = mOrderedSetArray[i];
        if (pSet != nullptr && *pSet == rIdentifier) {
            return pSet;
        }
    }

    return nullptr;
}

PlacementInfoOrdered::SameIdSet* PlacementInfoOrdered::createSameIdSet(const Identifier& rIdentifier) {
    s32 index = getUsedArrayNum();
    SameIdSet* pSet = &mSetArray[index];
    mOrderedSetArray[index] = pSet;
    *static_cast< Identifier* >(pSet) = rIdentifier;
    return pSet;
}

PlacementInfoOrdered::Index* PlacementInfoOrdered::createIndex(const JMapInfoIter& rIter) {
    Index* pIndex = &mIndexArray[mIndexCount];
    mIndexCount++;
    pIndex->mInfoIter = rIter;
    return pIndex;
}

PlacementInfoOrdered::Index::Index() : MR::BothDirLink< Index >(this), mInfoIter() {
}

PlacementInfoOrdered::Index::~Index() {
}

PlacementInfoOrdered::SameIdSet::SameIdSet() : Identifier(), mList() {
}

PlacementInfoOrdered::SameIdSet::~SameIdSet() {
}

void PlacementInfoOrdered::attach(const JMapInfo* pInfo, PlacementInfoOrdered* pAfterScenario) {
    s32 count = pInfo->getNumEntries();
    for (s32 i = 0; i < count; i++) {
        JMapInfoIter iter(pInfo, i);
        const char* pName = "";
        MR::getObjectName(&pName, iter);
        s32 shapeId = -1;
        MR::getJMapInfoShapeIdWithInit(iter, &shapeId);
        Identifier identifier(pName, shapeId);
        if (pAfterScenario != nullptr && shapeId == -1 && PlanetMapCreatorFunction::isLoadArchiveAfterScenarioSelected(pName)) {
            pAfterScenario->insert(identifier, iter);
        } else {
            insert(identifier, iter);
        }
    }
}
