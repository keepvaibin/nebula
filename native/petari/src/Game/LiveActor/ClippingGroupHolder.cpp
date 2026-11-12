#include "Game/LiveActor/ClippingGroupHolder.hpp"
#include "Game/Util/Array.hpp"
#include "Game/LiveActor/ClippingActorInfo.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/PlacementId.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const s32 sGroupCapacity = 0x40;
};  // namespace

ClippingInfoGroup::ClippingInfoGroup(const char* pGroupName, s32 capacity)
    : NameObj(pGroupName), mCount(0), mInfos(), mGroupId(), mIsClipped(false) {
    lumacore::setClass(this, lumacore::kClassId< ClippingInfoGroup >);
    mInfos.room(capacity > 0 ? static_cast< u32 >(capacity) : 16);
}

void ClippingInfoGroup::setGroupNo(const TableIter& rIter) {
    mGroupId = new PlacementId(MR::createPlacementIdFromClippingGroupId(rIter));
}

void ClippingInfoGroup::registerInfo(ClippingActorInfo* pInfo) {
    mInfos.room(mCount + 1)[mCount++] = pInfo;
}

bool ClippingInfoGroup::isClippedNowAll() const {
    for (s32 i = 0; i < mCount; i++) {
        const ClippingActorInfo* pInfo = mInfos[i];
        if (!MR::isDead(pInfo->mActor) && (MR::isInvalidClipping(pInfo->mActor) || !pInfo->isJudgedToClip())) {
            return false;
        }
    }

    return true;
}

void ClippingInfoGroup::startClippedAll() {
    mIsClipped = true;

    for (s32 i = 0; i < mCount; i++) {
        if (!MR::isDead(mInfos[i]->mActor) && !MR::isClipped(mInfos[i]->mActor)) {
            mInfos[i]->startClipped();
        }
    }
}

void ClippingInfoGroup::endClippedAll() {
    mIsClipped = false;

    for (s32 i = 0; i < mCount; i++) {
        if (!MR::isDead(mInfos[i]->mActor) && MR::isClipped(mInfos[i]->mActor)) {
            mInfos[i]->endClipped();
        }
    }
}

void ClippingInfoGroup::updateDrawnClippedAll() {
    for (s32 i = 0; i < mCount; i++) {
        if (!MR::isDead(mInfos[i]->mActor) && MR::isClipped(mInfos[i]->mActor)) {
            mInfos[i]->updateDrawnClipped();
        }
    }
}

void ClippingGroupHolder::movement() {
    for (s32 i = 0; i < mNumGroups; i++) {
        ClippingInfoGroup* group = mInfoGroups[i];

        if (group->isClippedNowAll()) {
            if (!group->mIsClipped) {
                group->startClippedAll();
            } else {
                group->updateDrawnClippedAll();
            }
        } else if (group->mIsClipped) {
            group->endClippedAll();
        }
    }
}

void ClippingGroupHolder::createAndAdd(ClippingActorInfo* pInfo, const TableIter& rIter, s32 capacity) {
    ClippingInfoGroup* group = findGroup(rIter);

    if (group == nullptr) {
        group = createGroup(pInfo, rIter, capacity);
    }

    group->registerInfo(pInfo);
}

ClippingInfoGroup* ClippingGroupHolder::createGroup(ClippingActorInfo* pInfo, const TableIter& rIter, s32 capacity) {
    ClippingInfoGroup* group = new ClippingInfoGroup(pInfo->mActor->mName, capacity);
    group->setGroupNo(rIter);
    if (mNumGroups == mMaxGroups) {
        mInfoGroups = MR::growArray(mInfoGroups, mMaxGroups);
        mMaxGroups *= 2;
    }

    mInfoGroups[mNumGroups++] = group;
    return group;
}

ClippingInfoGroup* ClippingGroupHolder::findGroup(const TableIter& rIter) {
    const PlacementId id = MR::createPlacementIdFromClippingGroupId(rIter);

    for (s32 i = 0; i < mNumGroups; i++) {
        if (id == *mInfoGroups[i]->mGroupId) {
            return mInfoGroups[i];
        }
    }

    return nullptr;
}

ClippingGroupHolder::ClippingGroupHolder()
    : NameObj("クリッピングアクター保持"), mNumGroups(0), mInfoGroups(new ClippingInfoGroup*[::sGroupCapacity]()), mMaxGroups(::sGroupCapacity) {
    lumacore::setClass(this, lumacore::kClassId< ClippingGroupHolder >);
}
