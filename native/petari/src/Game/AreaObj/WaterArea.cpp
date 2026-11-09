#include "Game/AreaObj/WaterArea.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Util/PlacementUtil.hpp"

WaterArea::WaterArea(int formType, const char* pName) : AreaObj(formType, pName) {
}

void WaterArea::init(const TableIter& rIter) {
    AreaObj::init(rIter);
    WaterAreaFunction::createWaterAreaHolder();
}

WaterAreaMgr::WaterAreaMgr(s32 firstCapacity, const char* pName) : AreaObjMgr(firstCapacity, pName) {
}
