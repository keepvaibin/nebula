#pragma once

#include "Game/NameObj/NameObj.hpp"

class LiveActor;
class MapPartsRailGuideDrawer;

class MapPartsRailGuideHolder : public NameObj {
public:
    MapPartsRailGuideHolder();

    virtual ~MapPartsRailGuideHolder();
    virtual void init(const JMapInfoIter& rIter);

    MapPartsRailGuideDrawer* createRailGuide(LiveActor* pHost, const char* pModelName, const JMapInfoIter& rIter);
    MapPartsRailGuideDrawer* find(s32 railId);

    MapPartsRailGuideDrawer* mDrawers[64];
    s32 mNumRailGuides;
};
