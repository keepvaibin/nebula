#include "Game/Util/RailGraphUtil.hpp"
#include "Game/Map/RailGraph.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "math_types.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_addRailToGraph_f1, "addRailToGraph.f1", 100.0f);

namespace {
    inline void addRailToGraph(RailGraph* pGraph, TableIter currentRail, const Table* pPoints) {
        s32 pointCount = pPoints->getNumEntries();
        s32 firstNode = 0;
        s32 previousNode = -1;
        RailGraphEdge edge;

        for (s32 i = 0; i < pointCount; i++) {
            Vec3f position;
            MR::getRailPointPos0(TableIter(pPoints, i), &position);
            s32 nearNode = MR::getNearNodeIndex(pGraph, position, cNum_addRailToGraph_f1, nullptr);
            s32 node;
            if (nearNode == -1) {
                node = pGraph->addNode(position);
            } else {
                node = nearNode;
            }

            if (previousNode != -1 && node != previousNode) {
                pGraph->connectNodeTwoWay(previousNode, node, &edge);
            }

            if (i == 0) {
                firstNode = node;
            }

            previousNode = node;
            edge.setArgs(pPoints, i);
        }

        if (MR::isLoopRailPathIter(currentRail) && previousNode != -1 && firstNode != previousNode) {
            pGraph->connectNodeTwoWay(previousNode, firstNode, &edge);
        }
    }
};  // namespace

namespace MR {
    RailGraph* createRailGraphFromPlacement(const TableIter& rIter) {
        RailGraph* pGraph = new RailGraph;
        const Table* pRailPoints = nullptr;
        TableIter railIter;
        bool hasRail = true;
        getRailInfo(&railIter, &pRailPoints, rIter);

        // A chain visits each rail of the zone once at most; links that come back round (bad placement data) end it there.
        s32 railsLeft = getPlacedRailNum(getPlacedZoneId(rIter));

        while (hasRail) {
            ::addRailToGraph(pGraph, railIter, pRailPoints);

            hasRail = getNextLinkRailInfo(&railIter, &pRailPoints, railIter) && --railsLeft > 0;
        }

        return pGraph;
    }

    RailGraphIter* createRailGraphIter(const RailGraph* pGraph) {
        return new RailGraphIter(pGraph->getIterator());
    }

    void moveNextNode(RailGraphIter* pIter) {
        pIter->moveNodeNext();
    }

    void moveNodeNearPosition(RailGraphIter* pRailGraphIter, const Vec3f& rPosition, f32 maxDistance, RailGraphNodeSelecter* pSelector) {
        pRailGraphIter->setNode(getNearNodeIndex(pRailGraphIter->mGraph, rPosition, maxDistance, pSelector));
    }

    void selectReverseEdge(RailGraphIter* pRailGraphIter) {
        s32 next = pRailGraphIter->mSelectedEdge;
        pRailGraphIter->moveNodeNext();
        pRailGraphIter->selectEdge(next);
    }

    bool isSelectedEdge(const RailGraphIter* pRailGraphIter) {
        return pRailGraphIter->isSelectedEdge();
    }

    bool isWatchedPrevEdge(const RailGraphIter* pRailGraphIter) {
        return pRailGraphIter->isWatchedPrevEdge();
    }

    Vec3f* getCurrentNodePosition(const RailGraphIter* pGraph) {
        return &pGraph->getCurrentNode()->mPos;
    }

    Vec3f* getNextNodePosition(const RailGraphIter* pIter) {
        return &pIter->getNextNode()->mPos;
    }

    void calcWatchEdgeVector(const RailGraphIter* pIter, Vec3f* pEdge) {
        pEdge->set(pIter->getWatchNode()->mPos - pIter->getCurrentNode()->mPos);
    }

    void calcWatchEdgeDirection(const RailGraphIter* pRailGraphIter, Vec3f* pVec) {
        calcWatchEdgeVector(pRailGraphIter, pVec);
        MR::normalize(pVec);
    }

    s32 getNearNodeIndex(const RailGraph* pGraph, const Vec3f& rPosition, f32 maxDistance, RailGraphNodeSelecter* pSelector) {
        if (maxDistance < 0.0f) {
            maxDistance = FLOAT_MAX;
        }

        f32 nearestDistance = maxDistance;
        s32 nearestNode = -1;
        const s32 nodeCount = pGraph->getNodeNum();
        RailGraphIter iter = pGraph->getIterator();
        for (s32 i = 0; i < nodeCount; i++) {
            if (pSelector != nullptr) {
                iter.setNode(i);
                if (!pSelector->isSatisfy(iter)) {
                    continue;
                }
            }

            f32 distance = rPosition.distance(pGraph->getNode(i)->mPos);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearestNode = i;
            }
        }

        return nearestNode;
    }

    s32 getSelectEdgeArg0(const RailGraphIter* pIter) {
        return pIter->getCurrentEdge()->mPointArg[0];
    }

    s32 getSelectEdgeArg1(const RailGraphIter* pIter) {
        return pIter->getCurrentEdge()->mPointArg[1];
    }

    s32 getSelectEdgeArg2(const RailGraphIter* pIter) {
        return pIter->getCurrentEdge()->mPointArg[2];
    }

    s32 getSelectEdgeArg3(const RailGraphIter* pIter) {
        return pIter->getCurrentEdge()->mPointArg[3];
    }

    s32 getWatchEdgeArg7(const RailGraphIter* pIter) {
        return pIter->getWatchEdge()->mPointArg[7];
    }
};  // namespace MR
