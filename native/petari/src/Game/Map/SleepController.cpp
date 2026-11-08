#include "Game/Map/SleepController.hpp"
#include "Game/Map/ActorAppearSwitchListener.hpp"
#include "Game/Map/StageSwitch.hpp"

SleepController::SleepController(const TableIter& rIter, SwitchEventListener* pListener)
    : mSwitchIdInfo(StageSwitchFunction::createSwitchIdInfo("SW_SLEEP", rIter, false)), mSwitchEventListener(pListener),
      mIsOnSwitchByIdInfo() {
}

void SleepController::initSync() {
    if (StageSwitchFunction::isOnSwitchBySwitchIdInfo(*mSwitchIdInfo)) {
        mSwitchEventListener->listenSwitchOnEvent();
    } else {
        mSwitchEventListener->listenSwitchOffEvent();
    }
}

void SleepController::update() {
    bool isOnSwitchByIdInfo = StageSwitchFunction::isOnSwitchBySwitchIdInfo(*mSwitchIdInfo);

    if (!mIsOnSwitchByIdInfo && isOnSwitchByIdInfo) {
        mSwitchEventListener->listenSwitchOnEvent();
    }

    if (mIsOnSwitchByIdInfo && !isOnSwitchByIdInfo) {
        mSwitchEventListener->listenSwitchOffEvent();
    }

    mIsOnSwitchByIdInfo = isOnSwitchByIdInfo;
}
