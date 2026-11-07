#include "Game/Map/GroupSwitchWatcher.hpp"
#include "Game/Map/StageSwitch.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"

GroupSwitchWatcher::GroupSwitchWatcher(const char* pName)
    : LiveActor(pName), mSwitchCtrl(nullptr), mNumSwitches(-1), mLogicType(-1), mActionType(-1) {
}

void GroupSwitchWatcher::init(const TableIter& rIter) {
    MR::connectToSceneMapObjMovement(this);
    MR::getPlacementArg0NoInit(rIter, &mNumSwitches);
    MR::getPlacementArg1NoInit(rIter, &mLogicType);
    MR::getPlacementArg2NoInit(rIter, &mActionType);
    mSwitchCtrl = MR::createStageSwitchCtrl(this, rIter);
    MR::invalidateClipping(this);
    makeActorAppeared();
}

void GroupSwitchWatcher::control() {
    if (mActionType == -1) {
        if (!mSwitchCtrl->isOnSwitchA()) {
            bool activate = false;
            switch (mLogicType) {
            case -1:
                activate = mSwitchCtrl->isOnAllSwitchAfterB(mNumSwitches);
                break;
            case 0:
                activate = mSwitchCtrl->isOnAnyOneSwitchAfterB(mNumSwitches);
                break;
            }

            if (activate) {
                mSwitchCtrl->onSwitchA();
                kill();
            }
        }
    } else {
        bool activate = false;
        switch (mLogicType) {
        case -1:
            activate = mSwitchCtrl->isOnAllSwitchAfterB(mNumSwitches);
            break;
        case 0:
            activate = mSwitchCtrl->isOnAnyOneSwitchAfterB(mNumSwitches);
            break;
        }

        if (activate) {
            mSwitchCtrl->onSwitchA();
        } else {
            mSwitchCtrl->offSwitchA();
        }
    }
}
