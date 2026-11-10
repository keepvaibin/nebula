#include "Game/Map/SwitchSynchronizer.hpp"
#include "Game/Map/StageSwitch.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

SwitchSynchronizer::SwitchSynchronizer(const char* pName) : NameObj(pName), mSwitchCtrl(), mIsCheckSameSwitchState(true) {
    lumacore::setClass(this, lumacore::kClassId< SwitchSynchronizer >);
}

void SwitchSynchronizer::init(const TableIter& rIter) {
    MR::connectToSceneMapObjMovement(this);

    mSwitchCtrl = MR::createStageSwitchCtrl(this, rIter);
}

// A is kept opposite to B (mIsCheckSameSwitchState) or equal to it.
void SwitchSynchronizer::movement() {
    const bool isOnA = mSwitchCtrl->isOnSwitchA();
    const bool isOnB = mSwitchCtrl->isOnSwitchB();
    const bool isWantedA = mIsCheckSameSwitchState ? !isOnB : isOnB;
    if (isOnA && !isWantedA) {
        mSwitchCtrl->offSwitchA();
    } else if (!isOnA && isWantedA) {
        mSwitchCtrl->onSwitchA();
    }
}
