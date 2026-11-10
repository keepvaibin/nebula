#include "Game/Map/TimerSwitch.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

TimerSwitch::TimerSwitch(const char* pName) : LiveActor(pName), mTimeLimit(-1), mTimeLeft(-1) {
    lumacore::setClass(this, lumacore::kClassId< TimerSwitch >);
}

void TimerSwitch::init(const TableIter& rIter) {
    MR::connectToSceneMapObjMovement(this);
    MR::getPlacementArg0NoInit(rIter, &mTimeLimit);
    MR::needStageSwitchWriteA(this, rIter);
    MR::needStageSwitchReadB(this, rIter);
    MR::invalidateClipping(this);
    makeActorAppeared();
}

void TimerSwitch::control() {
    if (mTimeLeft < 0 && MR::isOnSwitchB(this)) {
        mTimeLeft = mTimeLimit;
    }

    if (mTimeLeft <= 0) {
        return;
    }

    mTimeLeft--;

    if (mTimeLeft > 0) {
        return;
    }

    MR::onSwitchA(this);
    kill();
}
