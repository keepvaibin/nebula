#include "Game/AreaObj/SwitchArea.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/System/LumaCore.hpp"

SwitchArea::SwitchArea(int formType, const char* pName) : AreaObj(formType, pName) {
    lumacore::setClass(this, lumacore::kClassId< SwitchArea >);
}

void SwitchArea::init(const TableIter& rIter) {
    AreaObj::init(rIter);
    MR::connectToSceneAreaObj(this);
}

void SwitchArea::movement() {
    if (!isUpdate()) {
        return;
    }

    bool enabled = !isValidSwitchB() || isOnSwitchB();

    if (enabled && isInVolume(*MR::getPlayerPos())) {
        if (mObjArg1 == -1) {
            onSwitchA();
        } else {
            offSwitchA();
        }
    } else if (mObjArg0 != -1) {
        offSwitchA();
    }
}

bool SwitchArea::isUpdate() const {
    if (mObjArg2 != -1 && !MR::isOnGroundPlayer()) {
        return false;
    }

    if (mObjArg0 != -1) {
        return true;
    }

    if (mObjArg1 == -1) {
        return !isOnSwitchA();
    }

    return isOnSwitchA();
}

const char* SwitchArea::getManagerName() const {
    return "SwitchArea";
}
