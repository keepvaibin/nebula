#include "Game/AreaObj/MessageArea.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/System/LumaCore.hpp"

MessageArea::MessageArea(int formType, const char* pName) : AreaObj(formType, pName), mZoneID(-1) {
    lumacore::setClass(this, lumacore::kClassId< MessageArea >);
}

void MessageArea::init(const TableIter& rIter) {
    AreaObj::init(rIter);

    mZoneID = MR::getPlacedZoneId(rIter);
}
