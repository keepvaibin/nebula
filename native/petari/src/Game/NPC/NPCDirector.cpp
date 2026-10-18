#include "Game/NPC/NPCDirector.hpp"
#include "Game/NPC/NPCParameter.hpp"
#include "Game/Util/ObjUtil.hpp"

NPCDirector::NPCDirector() : NameObj("NPC\216\167\212\366" /* NPC指揮 */) {
}

void NPCDirector::init(const JMapInfoIter& rIter) {
    mCapsParameterReader = new NPCCapsParameterReader("");
    mItemParameterReader = new NPCItemParameterReader("");
    mDataResourceHolder = MR::createAndAddResourceHolder("NPCData.arc");
}
