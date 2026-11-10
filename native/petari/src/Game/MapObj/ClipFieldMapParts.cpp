#include "Game/MapObj/ClipFieldMapParts.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

ClipFieldMapParts::ClipFieldMapParts(const char* pName) : GeneralMapParts(pName) {
    lumacore::setClass(this, lumacore::kClassId< ClipFieldMapParts >);
}

void ClipFieldMapParts::control() {
    if (MR::isValidSwitchA(this)) {
        if (MR::isOnSwitchA(this)) {
            MR::validateCollisionParts(this);
        } else {
            MR::invalidateCollisionParts(this);
        }
    }

    GeneralMapParts::control();
}

void ClipFieldMapParts::connectToScene() {
    MR::connectToClippedMapParts(this);
}

void ClipFieldMapParts::init(const TableIter& rIter) {
    GeneralMapParts::init(rIter);
    MR::needStageSwitchReadA(this, rIter);
    getSensor("body")->setType(ATYPE_CLIP_FIELD_MAP_PARTS);
}
