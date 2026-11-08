#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class MapPartsRailGuidePoint : public LiveActor {
public:
    MapPartsRailGuidePoint(const LiveActor*, const char* pName, f32 coord, bool isPointModel);
    MapPartsRailGuidePoint(const LiveActor*, const char* pName, int pointIndex, bool isPointModel);

    virtual void init(const TableIter&);

    f32 mCoord;
    bool mIsPointModel;
};