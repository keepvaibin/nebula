#include "Game/LiveActor/ClippingActorHolder.hpp"
#include "Game/LiveActor/ClippingActorInfo.hpp"
#include "Game/LiveActor/ClippingGroupHolder.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ViewGroupCtrl.hpp"
#include "Game/NameObj/NameObjExecuteHolder.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_findGround_f1, "findGround.f1", 1.25f);

namespace {
    NEBULA_TUNABLE(s32, sListCapacity, 2560);
    NEBULA_TUNABLE(u32, sTableSize, 8192);

    u32 hashActor(const LiveActor* pActor) {
        return static_cast< u32 >(reinterpret_cast< uintptr_t >(pActor) >> 4) * 0x9E3779B1u;
    }

    void place(ClippingActorInfo** pTable, u32 mask, ClippingActorInfo* pInfo) {
        u32 slot = hashActor(pInfo->mActor) & mask;
        while (pTable[slot] != nullptr) {
            slot = (slot + 1) & mask;
        }
        pTable[slot] = pInfo;
    }
};  // namespace

ClippingActorHolder::ClippingActorHolder()
    : NameObj("クリッピングアクター保持"), mActorNum(), mTargets(new ClippingActorInfoList(::sListCapacity)),
      mInvalid(new ClippingActorInfoList(::sListCapacity)), mNonTargets(new ClippingActorInfoList(::sListCapacity)),
      mGroupTargets(new ClippingActorInfoList(::sListCapacity)), mViewGroupCtrl(new ViewGroupCtrl()), mPlanets(), mPlanetNum(),
      mTable(new ClippingActorInfo*[::sTableSize]()), mTableMask(::sTableSize - 1) {
    lumacore::setClass(this, lumacore::kClassId< ClippingActorHolder >);
}

void ClippingActorHolder::movement() {
    mViewGroupCtrl->update();

    for (s32 i = 0; i < mTargets->mCount; i++) {
        mTargets->mClippingActorList[i]->judgeClipping();
    }
}

void ClippingActorHolder::moveTo(ClippingActorInfo* pInfo, ClippingActorInfoList* pList) {
    if (pInfo->mList == pList) {
        return;
    }
    if (pInfo->mList != nullptr) {
        pInfo->mList->remove(pInfo);
    }
    pList->add(pInfo);
}

void ClippingActorHolder::insert(ClippingActorInfo* pInfo) {
    if (static_cast< u32 >(mActorNum) * 2 >= mTableMask) {
        ClippingActorInfo** pOld = mTable;
        const u32 oldSize = mTableMask + 1;
        mTableMask = oldSize * 2 - 1;
        mTable = new ClippingActorInfo*[oldSize * 2]();
        for (u32 i = 0; i < oldSize; i++) {
            if (pOld[i] != nullptr) {
                ::place(mTable, mTableMask, pOld[i]);
            }
        }
        delete[] pOld;
    }
    ::place(mTable, mTableMask, pInfo);
}

void ClippingActorHolder::registerActor(LiveActor* pActor) {
    ClippingActorInfo* pInfo = new ClippingActorInfo(pActor);
    insert(pInfo);
    mNonTargets->add(pInfo);
    mViewGroupCtrl->initMadeDuringPlay(pInfo);

    mActorNum++;
}

void ClippingActorHolder::initSystemInfo(LiveActor* pActor, const TableIter& rIter) {
    s32 viewGroupID = -1;

    if (MR::getPlacementViewGroupID(rIter, &viewGroupID)) {
        mViewGroupCtrl->initActorInfo(find(pActor), viewGroupID);
    }
}

void ClippingActorHolder::initViewGroupTable() {
    mViewGroupCtrl->startInitViewGroupTable();
    mViewGroupCtrl->initViewGroup(mTargets);
    mViewGroupCtrl->initViewGroup(mNonTargets);
    mViewGroupCtrl->initViewGroup(mGroupTargets);
    mViewGroupCtrl->initViewGroup(mInvalid);
    mViewGroupCtrl->endInitViewGroupTable();

    ClippingActorInfoList* lists[] = {mTargets, mInvalid, mNonTargets, mGroupTargets};
    s32 total = 0;
    for (s32 l = 0; l < 4; l++) {
        total += lists[l]->mCount;
    }
    mPlanets = new ClippingActorInfo*[total > 0 ? total : 1];
    for (s32 l = 0; l < 4; l++) {
        for (s32 i = 0; i < lists[l]->mCount; i++) {
            if (MR::isMovementPlanet(lists[l]->mClippingActorList[i]->mActor)) {
                mPlanets[mPlanetNum++] = lists[l]->mClippingActorList[i];
            }
        }
    }
}

// The smallest planet whose clipping sphere (it holds the model) has the actor in or just above it.
const ClippingActorInfo* ClippingActorHolder::findGround(const ClippingActorInfo* pInfo) const {
    const ClippingActorInfo* ground = nullptr;
    const Vec3f& pos = pInfo->mActor->mPosition;
    for (s32 i = 0; i < mPlanetNum; i++) {
        const ClippingActorInfo* planet = mPlanets[i];
        if (planet == pInfo) {
            return nullptr;
        }
        const f32 reach = planet->mRadius * cNum_findGround_f1;
        const f32 x = pos.x - planet->mCenter->x;
        const f32 y = pos.y - planet->mCenter->y;
        const f32 z = pos.z - planet->mCenter->z;
        if (x * x + y * y + z * z <= reach * reach && (ground == nullptr || planet->mRadius < ground->mRadius)) {
            ground = planet;
        }
    }
    return ground;
}

void ClippingActorHolder::entryLodCtrl(LodCtrl* pLodCtrl, const TableIter& rIter) {
    mViewGroupCtrl->entryLodCtrl(pLodCtrl, rIter);
}

void ClippingActorHolder::validateClipping(LiveActor* pActor) {
    pActor->mFlag.mIsInvalidClipping = false;

    ClippingActorInfo* pActorInfo = find(pActor);
    if (MR::isDead(pActor)) {
        moveTo(pActorInfo, mNonTargets);
    } else if (pActorInfo->isGroupClipping()) {
        moveTo(pActorInfo, mGroupTargets);
    } else {
        moveTo(pActorInfo, mTargets);
    }
}

void ClippingActorHolder::invalidateClipping(LiveActor* pActor) {
    pActor->mFlag.mIsInvalidClipping = true;

    moveTo(find(pActor), mInvalid);

    if (MR::isClipped(pActor)) {
        pActor->endClipped();
    }
}

void ClippingActorHolder::addToClippingTarget(LiveActor* pActor) {
    if (MR::isInvalidClipping(pActor)) {
        return;
    }

    ClippingActorInfo* pActorInfo = find(pActor);
    if (pActorInfo->mList != mNonTargets) {
        return;
    }

    moveTo(pActorInfo, pActorInfo->isGroupClipping() ? mGroupTargets : mTargets);
}

void ClippingActorHolder::removeFromClippingTarget(LiveActor* pActor) {
    if (MR::isInvalidClipping(pActor)) {
        return;
    }

    moveTo(find(pActor), mNonTargets);
}

ClippingActorInfo* ClippingActorHolder::startGroupClipping(LiveActor* pActor, const TableIter& rIter) {
    ClippingActorInfo* pActorInfo = find(pActor);

    if (pActorInfo->mList == mTargets) {
        moveTo(pActorInfo, mGroupTargets);
    } else if (pActorInfo->mList == mInvalid) {
        moveTo(pActorInfo, mGroupTargets);
        pActor->endClipped();
    }

    pActorInfo->setGroupClippingNo(rIter);

    return pActorInfo;
}

void ClippingActorHolder::setTypeToSphere(LiveActor* pActor, f32 range, const Vec3f* pCenter) {
    find(pActor)->setTypeToSphere(range, pCenter);
}

void ClippingActorHolder::setFarClipLevel(LiveActor* pActor, s32 level) {
    find(pActor)->setFarClipLevel(level);
}

ClippingActorInfo* ClippingActorHolder::find(const LiveActor* pActor) const {
    for (u32 slot = ::hashActor(pActor) & mTableMask; mTable[slot] != nullptr; slot = (slot + 1) & mTableMask) {
        if (mTable[slot]->mActor == pActor) {
            return mTable[slot];
        }
    }

    return nullptr;
}
