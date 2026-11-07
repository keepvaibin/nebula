#include "Game/AreaObj/WaterArea.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Util/PlacementUtil.hpp"

WaterArea::WaterArea(int formType, const char* pName) : AreaObj(formType, pName), _3C() {
}

void WaterArea::init(const TableIter& rIter) {
    AreaObj::init(rIter);

    if (MR::checkTableEntries(rIter)) {
        MR::getPlacementArg2NoInit(rIter, &_3C);
    }

    WaterAreaFunction::createWaterAreaHolder();
}

WaterAreaMgr::WaterAreaMgr(s32 maxNum, const char* pName) : AreaObjMgr(maxNum, pName) {
}
