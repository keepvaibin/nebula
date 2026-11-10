#include "Game/AreaObj/LightArea.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/System/LumaCore.hpp"

LightArea::LightArea(int formType, const char* pName) : AreaObj(formType, pName), mPlacedZoneID(-1) {
    lumacore::setClass(this, lumacore::kClassId< LightArea >);
}

void LightArea::init(const TableIter& rIter) {
    AreaObj::init(rIter);
    mPlacedZoneID = MR::getPlacedZoneId(rIter);
}
