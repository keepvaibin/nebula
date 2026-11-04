#include "Game/LiveActor/ViewGroupCtrl.hpp"
#include "Game/AreaObj/AreaObj.hpp"
#include "Game/LiveActor/ClippingActorInfo.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/JMapUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Map/StageSwitch.hpp"

namespace {
    // sLodCtrlNumMax
    const u32 sAllLodCtrlNumMax = 0x100;
};  // namespace

ViewGroupCtrl::ViewGroupCtrl() : mViewCubeMgr(), mMaxViewGroupId(), mViewGroupData(), mViewCtrlCount(), mLodCtrls(), mLodCtrlCapacity(::sAllLodCtrlNumMax), mPlanetsFromStart(false) {
    mLodCtrls = new LodCtrl*[::sAllLodCtrlNumMax];

    for (u32 i = 0; i < ::sAllLodCtrlNumMax; i++) {
        mLodCtrls[i] = nullptr;
    }
}

void ViewGroupCtrl::initActorInfo(ClippingActorInfo* pInfo, s32 groupID) {
    pInfo->_12 = groupID;

    if (groupID >= mMaxViewGroupId) {
        mMaxViewGroupId = groupID + 1;
    }
}

void ViewGroupCtrl::initViewGroup(ClippingActorInfoList* pList) {
    for (s32 i = 0; i < pList->_4; i++) {
        s32 id = pList->mClippingActorList[i]->_12;

        if (id < 0) {
            id = mMaxViewGroupId;
        }

        pList->mClippingActorList[i]->_14 = &mViewGroupData[id];
    }
}

void ViewGroupCtrl::initMadeDuringPlay(ClippingActorInfo* pInfo) {
    if (mViewGroupData != nullptr) {
        pInfo->_14 = &mViewGroupData[mMaxViewGroupId];
    }
}

void ViewGroupCtrl::endInitViewGroupTable() {
}

void ViewGroupCtrl::entryLodCtrl(LodCtrl* pCtrl, const JMapInfoIter& rIter) {
    s32 groupID = -1;
    if (MR::getJMapInfoViewGroupID(rIter, &groupID)) {
        pCtrl->mViewGroupID = groupID;
        if (mViewCtrlCount >= mLodCtrlCapacity) {
            const s32 grown = mLodCtrlCapacity * 2;
            LodCtrl** pList = new LodCtrl*[grown]();
            for (s32 i = 0; i < mViewCtrlCount; i++) {
                pList[i] = mLodCtrls[i];
            }
            delete[] mLodCtrls;
            mLodCtrls = pList;
            mLodCtrlCapacity = grown;
        }
        mLodCtrls[mViewCtrlCount] = pCtrl;
        mViewCtrlCount++;
    }
}

void ViewGroupCtrl::update() {
    ViewGroupCtrlDataEntry* entry;
    if (!mViewCubeMgr) {
        return;
    }

    for (s32 i = 0; i < mMaxViewGroupId; i++) {
        entry = &mViewGroupData[i];
        entry->_0 = false;
        entry->_1 = false;
        entry->_2 = false;
        entry->_3 = false;
        entry->_4 = false;
    }

    for (s32 i = 0; i < mViewCubeMgr->getNumAreaObj(); i++) {
        AreaObj* area = mViewCubeMgr->getAreaObj(i);
        if (area->isInVolume(*MR::getPlayerPos())) {
            ViewGroupCtrlDataEntry* entry = &mViewGroupData[area->mObjArg0];
            entry->_0 = true;
            if (area->mObjArg1 == 1) {
                entry->_1 = true;
            } else if (area->mObjArg1 == 2) {
                entry->_2 = true;
            } else if (area->mObjArg1 == 3) {
                entry->_3 = true;
            } else if (area->mObjArg1 == 4 && !(mPlanetsFromStart && area->mSwitchCtrl->isValidSwitchAppear())) {
                entry->_0 = false;
                entry->_4 = true;
            }
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
        ViewGroupCtrlDataEntry* entry = &mViewGroupData[i];
        entry->_0 = false;
        entry->_1 = false;
        entry->_2 = false;
        entry->_3 = false;
        entry->_4 = false;
    }

    for (s32 i = 0; i < mViewCtrlCount; i++) {
        LodCtrl* ctrl = mLodCtrls[i];
        s32 groupID = ctrl->mViewGroupID;
        if (groupID < 0) {
            groupID = mMaxViewGroupId;
        }

        ViewGroupCtrlDataEntry* entry = &mViewGroupData[groupID];
        ctrl->setViewCtrlPtr(&entry->_1, &entry->_3, &entry->_4);
    }
}
