#include "Game/AreaObj/PlayerSeArea.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/System/LumaCore.hpp"

PlayerSeArea::PlayerSeArea(int formType, const char* pName) : AreaObj(formType, pName) {
    lumacore::setClass(this, lumacore::kClassId< PlayerSeArea >);
}

void PlayerSeArea::init(const TableIter& rIter) {
    AreaObj::init(rIter);
    MR::connectToSceneAreaObj(this);

    mSeType = mObjArg0;
}

void PlayerSeArea::movement() {
    if (!isInVolume(*MR::getPlayerPos()) || mSeType != 0) {
        return;
    }

    if (!MR::isPlayerJumpRising()) {
        MR::startLevelSoundPlayer("SE_PM_LV_LONG_FALL");
    }
}
