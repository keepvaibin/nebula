#include "Game/MapObj/MapPartsRailGuideHolder.hpp"
#include "Game/MapObj/MapPartsRailGuideDrawer.hpp"
#include "Game/Util/Table.hpp"
#include "Game/System/LumaCore.hpp"

MapPartsRailGuideHolder::MapPartsRailGuideHolder() : NameObj("レールガイド保持"), mNumRailGuides() {
    lumacore::setClass(this, lumacore::kClassId< MapPartsRailGuideHolder >);
}

MapPartsRailGuideDrawer* MapPartsRailGuideHolder::createRailGuide(LiveActor* pHost, const char* pModelName, const TableIter& rIter) {
    s32 railId = -1;
    rIter.getValue("CommonPath_ID", &railId);
    MapPartsRailGuideDrawer* pDrawer = find(railId);
    if (pDrawer == nullptr) {
        pDrawer = new MapPartsRailGuideDrawer(pHost, pModelName);
        pDrawer->init(rIter);
        mDrawers.at(mNumRailGuides++) = pDrawer;
    }

    return pDrawer;
}

MapPartsRailGuideDrawer* MapPartsRailGuideHolder::find(s32 railId) {
    MapPartsRailGuideDrawer** pEnd = mDrawers + mNumRailGuides;
    for (MapPartsRailGuideDrawer** pDrawer = mDrawers; pDrawer != pEnd; pDrawer++) {
        if (railId == (*pDrawer)->mRailId) {
            return *pDrawer;
        }
    }

    return nullptr;
}
