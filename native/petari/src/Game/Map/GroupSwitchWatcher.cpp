#include "Game/Map/GroupSwitchWatcher.hpp"
#include "Game/Map/StageSwitch.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

GroupSwitchWatcher::GroupSwitchWatcher(const char* pName)
    : LiveActor(pName), mSwitchCtrl(nullptr), mNumSwitches(-1), mLogicType(-1), mActionType(-1) {
    lumacore::setClass(this, lumacore::kClassId< GroupSwitchWatcher >);
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

bool GroupSwitchWatcher::isGroupOn() const {
    switch (mLogicType) {
    case -1:
        return mSwitchCtrl->isOnAllSwitchAfterB(mNumSwitches);
    case 0:
        return mSwitchCtrl->isOnAnyOneSwitchAfterB(mNumSwitches);
    default:
        return false;
    }
}

void GroupSwitchWatcher::control() {
    if (mActionType == -1) {
        if (!mSwitchCtrl->isOnSwitchA() && isGroupOn()) {
            mSwitchCtrl->onSwitchA();
            kill();
        }
    } else if (isGroupOn()) {
        mSwitchCtrl->onSwitchA();
    } else {
        mSwitchCtrl->offSwitchA();
    }
}
