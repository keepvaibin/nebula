#include "Game/LiveActor/ViewGroupCtrl.hpp"
#include "Game/AreaObj/AreaObj.hpp"
#include "Game/LiveActor/ClippingActorInfo.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Map/StageSwitch.hpp"

namespace {
    const u32 sAllLodCtrlNumMax = 0x100;
    const s32 sInsideHoldSteps = 30;  // half a second
};  // namespace

// Only the first mViewCtrlCount slots are read.
ViewGroupCtrl::ViewGroupCtrl()
    : mViewCubeMgr(), mMaxViewGroupId(), mViewGroupData(), mViewCtrlCount(), mLodCtrls(new LodCtrl*[::sAllLodCtrlNumMax]),
      mLodCtrlCapacity(::sAllLodCtrlNumMax), mPlanetsFromStart(false) {
}

void ViewGroupCtrl::initActorInfo(ClippingActorInfo* pInfo, s32 groupID) {
    pInfo->mViewGroupId = groupID;

    if (groupID >= mMaxViewGroupId) {
        mMaxViewGroupId = groupID + 1;
    }
}

void ViewGroupCtrl::initViewGroup(ClippingActorInfoList* pList) {
    for (s32 i = 0; i < pList->mCount; i++) {
        ClippingActorInfo* pInfo = pList->mClippingActorList[i];
        pInfo->mViewGroup = &mViewGroupData[pInfo->mViewGroupId < 0 ? mMaxViewGroupId : pInfo->mViewGroupId];
    }
}

void ViewGroupCtrl::initMadeDuringPlay(ClippingActorInfo* pInfo) {
    if (mViewGroupData != nullptr) {
        pInfo->mViewGroup = &mViewGroupData[mMaxViewGroupId];
    }
}

void ViewGroupCtrl::endInitViewGroupTable() {
}

void ViewGroupCtrl::entryLodCtrl(LodCtrl* pCtrl, const TableIter& rIter) {
    s32 groupID = -1;
    if (MR::getPlacementViewGroupID(rIter, &groupID)) {
        pCtrl->mViewGroupID = groupID;
        if (mViewCtrlCount >= mLodCtrlCapacity) {
            mLodCtrls = MR::growArray(mLodCtrls, mLodCtrlCapacity);
            mLodCtrlCapacity *= 2;
        }
        mLodCtrls[mViewCtrlCount] = pCtrl;
        mViewCtrlCount++;
    }
}

void ViewGroupCtrl::update() {
    if (!mViewCubeMgr) {
        return;
    }

    for (s32 i = 0; i < mMaxViewGroupId; i++) {
        mViewGroupData[i].clear();
    }

    for (s32 i = 0; i < mViewCubeMgr->getNumAreaObj(); i++) {
        AreaObj* area = mViewCubeMgr->getAreaObj(i);
        if (area->isInVolume(*MR::getPlayerPos())) {
            ViewGroupCtrlDataEntry* entry = &mViewGroupData[area->mObjArg0];
            entry->mIsPlayerInside = true;
            if (area->mObjArg1 == 1) {
                entry->mIsNear = true;
            } else if (area->mObjArg1 == 3) {
                entry->mIsFar = true;
            } else if (area->mObjArg1 == 4 && !(mPlanetsFromStart && area->mSwitchCtrl->isValidSwitchAppear())) {
                entry->mIsPlayerInside = false;
                entry->mIsHidden = true;
            }
        }
    }

    // A player on a cube's edge (or a platform carrying it across) leaves and re-enters every few steps; each time the
    // whole group switched between the camera's far plane and its own far level, stopping and starting every actor in
    // it. Staying "inside" a while after leaving only keeps those actors running a little longer. A hiding cube is not held.
    for (s32 i = 0; i < mMaxViewGroupId; i++) {
        ViewGroupCtrlDataEntry& rEntry = mViewGroupData[i];
        if (rEntry.mIsPlayerInside) {
            rEntry.mInsideHold = ::sInsideHoldSteps;
        } else if (!rEntry.mIsHidden && rEntry.mInsideHold > 0) {
            rEntry.mInsideHold--;
            rEntry.mIsPlayerInside = true;
        } else {
            rEntry.mInsideHold = 0;
        }
    }
}

void ViewGroupCtrl::startInitViewGroupTable() {
    mViewCubeMgr = MR::getAreaObjManager("ViewGroupCtrlCube");
    // Gateway's first planet hides the galaxy's other planets until Rosalina has spoken (the cube under a story switch).
    // The sky holds them from the start here; its cube without a switch still hides them from inside the last planet.
    mPlanetsFromStart = MR::isEqualStageName("HeavensDoorGalaxy");
    if (mViewCubeMgr) {
        for (s32 i = 0; i < mViewCubeMgr->getNumAreaObj(); i++) {
            s32 groupID = mViewCubeMgr->getAreaObj(i)->mObjArg0;
            if (groupID >= mMaxViewGroupId) {
                mMaxViewGroupId = groupID + 1;
            }
        }
    }

    mViewGroupData = new ViewGroupCtrlDataEntry[mMaxViewGroupId + 1];
    for (s32 i = 0; i < mMaxViewGroupId + 1; i++) {
        mViewGroupData[i].clear();
    }

    for (s32 i = 0; i < mViewCtrlCount; i++) {
        LodCtrl* ctrl = mLodCtrls[i];
        s32 groupID = ctrl->mViewGroupID;
        if (groupID < 0) {
            groupID = mMaxViewGroupId;
        }

        ViewGroupCtrlDataEntry* entry = &mViewGroupData[groupID];
        ctrl->setViewCtrlPtr(&entry->mIsNear, &entry->mIsFar, &entry->mIsHidden);
    }
}
