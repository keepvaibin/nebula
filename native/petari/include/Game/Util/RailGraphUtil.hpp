#pragma once

#include "Game/Map/RailGraphIter.hpp"
#include "Game/Map/RailGraphNode.hpp"
#include "Game/Util/Table.hpp"
#include <nebula_geometry.hpp>

class RailGraphNodeSelecter {
public:
    virtual bool isSatisfy(RailGraphIter& rIter) = 0;
};

namespace MR {
    RailGraph* createRailGraphFromPlacement(const TableIter& rIter);
    RailGraphIter* createRailGraphIter(const RailGraph* pRailGraph);
    void moveNextNode(RailGraphIter* pRailGraphIter);
    void moveNodeNearPosition(RailGraphIter* pRailGraphIter, const Vec3f& rPosition, f32 maxDistance, RailGraphNodeSelecter* pSelector);
    void selectReverseEdge(RailGraphIter* pRailGraphIter);
    bool isSelectedEdge(const RailGraphIter* pRailGraphIter);
    bool isWatchedPrevEdge(const RailGraphIter* pRailGraphIter);
    Vec3f* getCurrentNodePosition(const RailGraphIter* pRailGraphIter);
    Vec3f* getNextNodePosition(const RailGraphIter* pRailGraphIter);
    void calcWatchEdgeVector(const RailGraphIter* pIter, Vec3f* pEdge);
    void calcWatchEdgeDirection(const RailGraphIter* pRailGraphIter, Vec3f* pVec);
    s32 getNearNodeIndex(const RailGraph* pGraph, const Vec3f& rPosition, f32 maxDistance, RailGraphNodeSelecter* pSelector);
    s32 getSelectEdgeArg0(const RailGraphIter* pRailGraphIter);
    s32 getSelectEdgeArg1(const RailGraphIter* pRailGraphIter);
    s32 getSelectEdgeArg2(const RailGraphIter* pRailGraphIter);
    s32 getSelectEdgeArg3(const RailGraphIter* pRailGraphIter);
    s32 getWatchEdgeArg7(const RailGraphIter* pRailGraphIter);
};  // namespace MR
