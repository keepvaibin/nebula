#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class RailGraphIter;
class RailGraphNode;
class RailGraphEdge;

// Nodes and two-way edges; both arrays double when full.
class RailGraph {
public:
    RailGraph();

    // The new node's index.
    s32 addNode(const Vec3f& rPos);
    // pEdge, when given, supplies the edge's attributes; its connections are replaced.
    void connectNodeTwoWay(s32 nodeA, s32 nodeB, const RailGraphEdge* pEdge);
    RailGraphNode* getNode(s32 index) const;
    RailGraphEdge* getEdge(s32 index) const;
    bool isValidEdge(s32 edgeIndex) const;
    void connectEdgeToNode(s32 nodeIndex, s32 edgeIndex);
    RailGraphIter getIterator() const;

    s32 getNodeNum() const {
        return mNumNodes;
    }

private:
    RailGraphNode* mNodes;
    s32 mNodeCapacity;
    s32 mNumNodes;
    RailGraphEdge* mEdges;
    s32 mEdgeCapacity;
    s32 mNumEdges;
};
