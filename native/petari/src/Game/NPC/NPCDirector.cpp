#include "Game/NPC/NPCDirector.hpp"
#include "Game/NPC/NPCParameter.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

NPCDirector::NPCDirector() : NameObj("NPC指揮") {
    lumacore::setClass(this, lumacore::kClassId< NPCDirector >);
}

void NPCDirector::init(const TableIter& /*rIter*/) {
    mItemParameterReader = new NPCItemParameterReader("");
    mDataResourceHolder = MR::createAndAddResourceHolder("NPCData");
}
