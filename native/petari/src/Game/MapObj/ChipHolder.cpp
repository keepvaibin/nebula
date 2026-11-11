#include "Game/MapObj/ChipHolder.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/ChipBase.hpp"
#include "Game/MapObj/ChipCounter.hpp"
#include "Game/MapObj/ChipGroup.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/System/LumaCore.hpp"

ChipHolder::ChipHolder(const char* pName, s32 chipType) : NameObj(pName), mChipCounter(), mChipGroups(), mNumChipGroups(), mChipType(chipType) {
    lumacore::setClass(this, lumacore::kClassId< ChipHolder >);
}

void ChipHolder::registerChipGroup(ChipGroup* pChipGroup) {
    mChipGroups.at(mNumChipGroups++) = pChipGroup;
}

ChipGroup* ChipHolder::findChipGroup(s32 groupId) const {
    if (groupId < 0) {
        return nullptr;
    }

    for (s32 i = 0; i < mNumChipGroups; i++) {
        if (mChipGroups[i]->mStageSwitchArg == groupId) {
            return mChipGroups[i];
        }
    }

    return nullptr;
}

void MR::createChipHolder(s32 chipType) {
    switch (chipType) {
    case ChipBase::Type_Blue:
        createSceneObj(SceneObj_BlueChipHolder);
        break;
    case ChipBase::Type_Yellow:
        createSceneObj(SceneObj_YellowChipHolder);
        break;
    }
}

ChipHolder* MR::getChipHolder(s32 chipType) {
    switch (chipType) {
    case ChipBase::Type_Blue:
        return getSceneObj< ChipHolder >(SceneObj_BlueChipHolder);
    case ChipBase::Type_Yellow:
        return getSceneObj< ChipHolder >(SceneObj_YellowChipHolder);
    default:
        return nullptr;
    }
}

void MR::registerChipGroup(s32 chipType, ChipGroup* pChipGroup) {
    getChipHolder(chipType)->registerChipGroup(pChipGroup);
}

void MR::registerChip(s32 chipType, ChipBase* pChip, s32 groupId) {
    // A chip whose group is not placed (a bad group id) joins none.
    ChipHolder* pHolder = getChipHolder(chipType);
    ChipGroup* pGroup = pHolder != nullptr ? pHolder->findChipGroup(groupId) : nullptr;
    if (pGroup != nullptr) {
        pGroup->registerChip(pChip);
    }
}

void MR::noticeGetChip(s32 chipType, ChipBase* pChip, s32 groupId) {
    ChipHolder* pHolder = getChipHolder(chipType);
    ChipGroup* pGroup = pHolder != nullptr ? pHolder->findChipGroup(groupId) : nullptr;
    // A chip with no group is not counted, but still gives its air.
    if (pGroup != nullptr) {
        pGroup->noticeGet(pChip);
        pHolder->mChipCounter->setCount(pGroup->getGotCount());
    }
    incPlayerOxygen(8);
}

void MR::showChipCounter(s32 chipType, s32 groupId) {
    ChipHolder* pHolder = getChipHolder(chipType);
    ChipGroup* pGroup = pHolder != nullptr ? pHolder->findChipGroup(groupId) : nullptr;
    if (pGroup != nullptr) {
        pHolder->mChipCounter->requestShow(groupId, pGroup->getGotCount());
    }
}

void MR::hideChipCounter(s32 chipType, s32 groupId) {
    getChipHolder(chipType)->mChipCounter->requestHide(groupId);
}

void MR::requestStartChipCompleteDemo(s32 chipType, s32 groupId) {
    getChipHolder(chipType)->mChipCounter->requestComplete(groupId);
}

void MR::noticeEndChipCompleteDemo(s32 chipType, s32 groupId) {
    ChipHolder* pHolder = getChipHolder(chipType);
    ChipGroup* pGroup = pHolder != nullptr ? pHolder->findChipGroup(groupId) : nullptr;
    if (pGroup != nullptr) {
        pGroup->noticeEndCompleteDemo();
    }
}

s32 MR::getGotChipCount(s32 chipType, s32 groupId) {
    ChipHolder* pHolder = getChipHolder(chipType);
    ChipGroup* pGroup = pHolder != nullptr ? pHolder->findChipGroup(groupId) : nullptr;
    return pGroup != nullptr ? pGroup->getGotCount() : 0;
}

void MR::activateChipLayout() {
    if (isExistSceneObj(SceneObj_BlueChipHolder)) {
        getChipHolder(ChipBase::Type_Blue)->mChipCounter->requestActive();
    }

    if (isExistSceneObj(SceneObj_YellowChipHolder)) {
        getChipHolder(ChipBase::Type_Yellow)->mChipCounter->requestActive();
    }
}

void MR::deactivateChipLayout() {
    if (isExistSceneObj(SceneObj_BlueChipHolder)) {
        getChipHolder(ChipBase::Type_Blue)->mChipCounter->requestDeactive();
    }

    if (isExistSceneObj(SceneObj_YellowChipHolder)) {
        getChipHolder(ChipBase::Type_Yellow)->mChipCounter->requestDeactive();
    }
}

void ChipHolder::init(const TableIter& /*rIter*/) {
    switch (mChipType) {
    case ChipBase::Type_Blue:
        mChipCounter = new ChipCounter("ブルーチップカウンター", mChipType);
        break;
    case ChipBase::Type_Yellow:
        mChipCounter = new ChipCounter("イエローチップカウンター", mChipType);
        break;
    }

    mChipCounter->initWithoutIter();
}
