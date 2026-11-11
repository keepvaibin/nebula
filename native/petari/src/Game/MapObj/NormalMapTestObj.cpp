#include "Game/MapObj/NormalMapTestObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/NormalMapBase.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/System/LumaCore.hpp"

NormalMapTestObj::NormalMapTestObj(const char* pName) : NormalMapBase(pName) {
    lumacore::setClass(this, lumacore::kClassId< NormalMapTestObj >);
}

void NormalMapTestObj::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    NormalMapBase::setup("NormalMapTestObj");
    initHitSensor(1);
    MR::addBodyMessageSensorMapObj(this);
    MR::invalidateClipping(this);
    appear();
}
