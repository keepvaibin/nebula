#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

class Table;

// A link between two nodes of a RailGraph. Node and edge indices are -1 when unset.
class RailGraphEdge {
public:
    static const s32 cPointArgNum = 8;

    RailGraphEdge();

    void clearConnectInfo();
    // The other end from node, or -1 when node is neither end.
    s32 getNextNode(s32 node) const;
    // The edge that follows this one past node, or -1.
    s32 getNextEdge(s32 node) const;
    void setNextEdge(s32 edge, s32 node);
    // The rail point's point_arg0 to point_arg7.
    void setArgs(const Table* pPoints, s32 pointIndex);

    f32 mDistance;
    s32 mNodeA;
    s32 mNodeB;
    s32 mNextEdgeA;
    s32 mNextEdgeB;
    s32 mPointArg[cPointArgNum];
};
