#include "Game/AreaObj/DeathArea.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/System/LumaCore.hpp"

DeathArea::DeathArea(int formType, const char* pName) : AreaObj(formType, pName) {
    lumacore::setClass(this, lumacore::kClassId< DeathArea >);
}

void DeathArea::init(const TableIter& rIter) {
    AreaObj::init(rIter);
    MR::connectToSceneAreaObj(this);
}

void DeathArea::movement() {
    if (isInVolume(*MR::getPlayerPos()) && getDeathType() == DeathType_Any) {
        MR::forceKillPlayerByAbyss();
    }
}

bool DeathArea::isInVolume(const Vec3f& rVec) const {
    if (isValidSwitchA() && !isOnSwitchA()) {
        return false;
    }

    return AreaObj::isInVolume(rVec);
}

s32 DeathArea::getDeathType() const {
    return mObjArg0 != -1 ? mObjArg0 : DeathType_Any;
}
