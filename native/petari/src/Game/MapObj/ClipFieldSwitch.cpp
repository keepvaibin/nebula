#include "Game/MapObj/ClipFieldSwitch.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/ClipAreaHolder.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

ClipFieldSwitch::ClipFieldSwitch(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< ClipFieldSwitch >);
}

void ClipFieldSwitch::control() {
    mPosition = *MR::getPlayerPos();
    const bool isInArea = MR::isInClipArea(mPosition, 5.0f);

    if (MR::isValidSwitchA(this)) {
        if (isInArea) {
            MR::onSwitchA(this);
        } else {
            MR::offSwitchA(this);
        }
    }

    if (MR::isValidSwitchB(this)) {
        if (isInArea) {
            MR::offSwitchB(this);
        } else {
            MR::onSwitchB(this);
        }
    }
}

void ClipFieldSwitch::init(const TableIter& rIter) {
    MR::connectToSceneMapObjMovement(this);
    MR::useStageSwitchWriteA(this, rIter);
    MR::useStageSwitchWriteB(this, rIter);
    initHitSensor(1);
    MR::addHitSensorEye(this, "body", 8, 1000.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::invalidateClipping(this);
    appear();
}
