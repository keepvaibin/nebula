#include "Game/NPC/NPCSupportRail.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/System/LumaCore.hpp"

NPCSupportRail::NPCSupportRail(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< NPCSupportRail >);
}

void NPCSupportRail::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initRailRider(rIter);
    MR::joinToGroupArray(this, rIter, nullptr, 32);
    makeActorDead();
}
