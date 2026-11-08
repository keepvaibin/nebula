#pragma once

#include "Game/Util/Array.hpp"

#include "Game/NameObj/NameObj.hpp"

class LiveActor;
class MapPartsRailGuideDrawer;

class MapPartsRailGuideHolder : public NameObj {
public:
    MapPartsRailGuideHolder();

    MapPartsRailGuideDrawer* createRailGuide(LiveActor* pHost, const char* pModelName, const TableIter& rIter);
    MapPartsRailGuideDrawer* find(s32 railId);

    MR::GrowTable< MapPartsRailGuideDrawer*, 64 > mDrawers;
    s32 mNumRailGuides;
};
