#include <nebula_game_api.h>
#include "Game/System/NebulaLoadTrace.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/System/LumaClasses.hpp"
#include "Game/System/LumaRefs.hpp"
#include "Game/Scene/PlacementInfoOrdered.hpp"
#include "Game/LiveActor/AllLiveActorGroup.hpp"
#include "Game/Map/PlanetMapCreator.hpp"
#include "Game/NameObj/ModelChangableObjFactory.hpp"
#include "Game/NameObj/NameObjFactory.hpp"
#include "Game/NameObj/LoadSlice.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/SceneUtil.hpp"

namespace {
    CreationFuncPtr getCreator(const PlacementInfoOrdered::Identifier& rIdentifier) {
        if (rIdentifier.mShapeId != -1) {
            return MR::getModelChangableObjCreator(rIdentifier.mName);
        }

        if (const CreationFuncPtr creator = NameObjFactory::getCreator(rIdentifier.mName)) {
            return creator;
        }
        // A class a mod places that no mod made (references.md 3, after start): the placeholder object stands there.
        return luma::refs::placeholderFor(rIdentifier.mName);
    }

    PlacementInfoOrdered::Identifier readIdentifier(const TableIter& rIter) {
        const char* pName = "";
        MR::getObjectName(&pName, rIter);
        s32 shapeId = -1;
        MR::getPlacementShapeIdWithInit(rIter, &shapeId);
        return PlacementInfoOrdered::Identifier(pName, shapeId);
    }

    bool isAfterScenario(const PlacementInfoOrdered::Identifier& rIdentifier) {
        return rIdentifier.mShapeId == -1 && PlanetMapCreatorFunction::isLoadArchiveAfterScenarioSelected(rIdentifier.mName);
    }

    // Equal identifiers (the same name and shape id) hash alike.
    u32 hashIdentifier(const PlacementInfoOrdered::Identifier& rIdentifier) {
        return MR::getHashCode(rIdentifier.mName) ^ (static_cast< u32 >(rIdentifier.mShapeId) * 0x9E3779B1u);
    }
};  // namespace

s32 PlacementInfoOrdered::countAfterScenario(const Table* pInfo) {
    s32 count = 0;
    for (s32 i = 0; i < pInfo->getNumEntries(); i++) {
        if (::isAfterScenario(::readIdentifier(TableIter(pInfo, i)))) {
            count++;
        }
    }

    return count;
}

PlacementInfoOrdered::PlacementInfoOrdered(int count)
    : mIndexArray(), mIndexCount(), mSetArray(), mOrderedSetArray(), mCount(count), mSetSlots(), mSetSlotMask() {
    mIndexArray = new Index[count];
    mSetArray = new SameIdSet[count];
    mOrderedSetArray = new SameIdSet*[count];
    // At most count sets: a table of at least twice that is never more than half full.
    u32 size = 8;
    while (size < 2 * static_cast< u32 >(count)) {
        size *= 2;
    }
    mSetSlots = new s32[size];
    mSetSlotMask = size - 1;
    for (u32 i = 0; i < size; i++) {
        mSetSlots[i] = -1;
    }
}

void PlacementInfoOrdered::sort() {
    const s32 count = mSetNum;
    for (s32 i = 0; i < count; i++) {
        // Player archive loaders first, then every other set: the order of a restart, when all of a stage's files are already loaded.
        SameIdSet* pSet = mOrderedSetArray[i];
        pSet->mPriority = NameObjFactory::isPlayerArchiveLoaderObj(pSet->mName) ? 0 : 1;
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

    for (s32 i = 0; i < count; i++) {
        NebulaLoadTrace::sorted(mOrderedSetArray[i]->mName, mOrderedSetArray[i]->mPriority, mOrderedSetArray[i]->mList.getNum());
    }
}

void PlacementInfoOrdered::initPlacement() {
    LoadSlice whole = LoadSlice::whole();
    while (!initPlacementStep(whole)) {}
}

// One item of the slice for each placement made whole (an object's own initStep spends more for its pieces).
bool PlacementInfoOrdered::initPlacementStep(LoadSlice& rSlice) {
    if (!mPlacementStarted) {
        MR::startInitLiveActorSystemInfo();
        mPlacementStarted = true;
    }
    const s32 count = mSetNum;
    while (mPlacementSet < count) {
        SameIdSet* set = mOrderedSetArray[mPlacementSet];
        const CreationFuncPtr creator = ::getCreator(*set);
        if (mPlacementLink == nullptr) mPlacementLink = set->mList.getFirst();
        if (creator == nullptr || mPlacementLink == nullptr) {
            ++mPlacementSet;
            mPlacementLink = nullptr;
            continue;
        }
        const TableIter& iter = mPlacementLink->getObject()->getInfoIter();
        if (!NebulaMods::isMissingFiles(set->mName)) {
            NebulaLoadTrace::Scope trace(set->mName);
            MR::setCurrentPlacementZoneId(MR::getPlacedZoneId(iter));
            if (mInitializing == nullptr) {
                NebulaLoadTrace::placed(set->mName, MR::getPlacedZoneId(iter));
                // The game's creators take the Japanese names.
                mInitializing = creator(MR::getJapaneseObjectName(set->mName));
                mPlacementLink->getObject()->mMade = mInitializing;
                if (mInitializing == nullptr) {
                    // Nothing made (a mod's change removed it): this placement is left out.
                    MR::clearCurrentPlacementZoneId();
                    mPlacementLink = mPlacementLink->getNext();
                    if (mPlacementLink == nullptr) ++mPlacementSet;
                    continue;
                }
            }
            MR::initLiveActorSystemInfo(iter);
            bool ready;
            {
                // A name a mod aliased (classes.md, "alias"): its arguments while the object initialises.
                const luma::classes::AliasPlacing alias(set->mName, iter.mInfo, iter.mIndex);
                ready = mInitializing->initStep(iter, rSlice);
            }
            MR::initLiveActorSystemInfo(iter);
            MR::clearCurrentPlacementZoneId();
            if (!ready) return false;
            mInitializing = nullptr;
        }
        mPlacementLink = mPlacementLink->getNext();
        if (mPlacementLink == nullptr) ++mPlacementSet;
        if (rSlice.spend()) {
            if (mPlacementSet >= count) NebulaLoadTrace::placementDone();
            return mPlacementSet >= count;
        }
    }
    NebulaLoadTrace::placementDone();
    return true;
}

void PlacementInfoOrdered::insert(const Identifier& rIdentifier, const TableIter& rIter) {
    SameIdSet* pSet = find(rIdentifier);
    if (pSet == nullptr) {
        pSet = createSameIdSet(rIdentifier);
    }

    pSet->mList.append(createIndex(rIter));
}

PlacementInfoOrdered::SameIdSet* PlacementInfoOrdered::find(const Identifier& rIdentifier) const {
    for (u32 i = ::hashIdentifier(rIdentifier) & mSetSlotMask; mSetSlots[i] >= 0; i = (i + 1) & mSetSlotMask) {
        SameIdSet* pSet = &mSetArray[mSetSlots[i]];
        if (*pSet == rIdentifier) {
            return pSet;
        }
    }

    return nullptr;
}

// Called only for an identifier find() did not hold.
PlacementInfoOrdered::SameIdSet* PlacementInfoOrdered::createSameIdSet(const Identifier& rIdentifier) {
    SameIdSet* pSet = &mSetArray[mSetNum];
    mOrderedSetArray[mSetNum] = pSet;
    u32 i = ::hashIdentifier(rIdentifier) & mSetSlotMask;
    while (mSetSlots[i] >= 0) {
        i = (i + 1) & mSetSlotMask;
    }
    mSetSlots[i] = mSetNum;
    mSetNum++;
    *static_cast< Identifier* >(pSet) = rIdentifier;
    return pSet;
}

PlacementInfoOrdered::Index* PlacementInfoOrdered::createIndex(const TableIter& rIter) {
    Index* pIndex = &mIndexArray[mIndexCount];
    mIndexCount++;
    pIndex->mInfoIter = rIter;
    return pIndex;
}

PlacementInfoOrdered::Index::Index() : mInfoIter() {
}

NameObj* PlacementInfoOrdered::findMade(const Table* pTable, s32 entry) const {
    for (u32 i = 0; i < mIndexCount; i++) {
        if (mIndexArray[i].mInfoIter.mInfo == pTable && mIndexArray[i].mInfoIter.mIndex == entry) {
            return mIndexArray[i].mMade;
        }
    }
    return nullptr;
}

bool PlacementInfoOrdered::findPlaced(const void* pObject, TableIter* pOut) const {
    for (u32 i = 0; pObject != nullptr && i < mIndexCount; i++) {
        if (mIndexArray[i].mMade == pObject) {
            *pOut = mIndexArray[i].mInfoIter;
            return true;
        }
    }
    return false;
}

PlacementInfoOrdered::SameIdSet::SameIdSet() : Identifier(), mList() {
}

void PlacementInfoOrdered::attach(const Table* pInfo, PlacementInfoOrdered* pAfterScenario) {
    s32 count = pInfo->getNumEntries();
    for (s32 i = 0; i < count; i++) {
        TableIter iter(pInfo, i);
        const Identifier identifier = ::readIdentifier(iter);
        if (pAfterScenario != nullptr && ::isAfterScenario(identifier)) {
            pAfterScenario->insert(identifier, iter);
        } else {
            insert(identifier, iter);
        }
    }
}
