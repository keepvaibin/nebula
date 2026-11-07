#include "Game/AreaObj/DeathArea.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"

DeathArea::DeathArea(int formType, const char* pName) : AreaObj(formType, pName) {
}

void DeathArea::init(const TableIter& rIter) {
    AreaObj::init(rIter);
    MR::connectToSceneAreaObj(this);
}

void DeathArea::movement() {
    if (!isInVolume(*MR::getPlayerPos())) {
        return;
    }

    bool canKillPlayer = getDeathType() == DeathType_Any;

    if (!canKillPlayer) {
        return;
    }

    MR::forceKillPlayerByAbyss();
}

bool DeathArea::isInVolume(const Vec3f& rVec) const {
    if (isValidSwitchA() && !isOnSwitchA()) {
        return false;
    }

    return AreaObj::isInVolume(rVec);
}

s32 DeathArea::getDeathType() const {
    if (mObjArg0 != -1) {
        return mObjArg0;
    }

    return DeathType_Any;
}
