#include "Game/NPC/NPCDirector.hpp"
#include "Game/NPC/NPCParameter.hpp"
#include "Game/Util/ObjUtil.hpp"

NPCDirector::NPCDirector() : NameObj("NPC指揮") {
}

void NPCDirector::init(const TableIter& /*rIter*/) {
    mItemParameterReader = new NPCItemParameterReader("");
    mDataResourceHolder = MR::createAndAddResourceHolder("NPCData");
}
