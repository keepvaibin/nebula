#include "Game/LiveActor/MessageSensorHolder.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

MessageSensorHolder::MessageSensorHolder(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< MessageSensorHolder >);
}

void MessageSensorHolder::init(const TableIter& /*rIter*/) {
    initHitSensor(1);
    MR::addBodyMessageSensor(this, ATYPE_MESSAGE_SENSOR);
    MR::invalidateClipping(this);
    makeActorAppeared();
}
