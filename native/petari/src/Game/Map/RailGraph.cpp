#include "Game/Map/RailGraph.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Map/RailGraphEdge.hpp"
#include "Game/Map/RailGraphIter.hpp"
#include "Game/Map/RailGraphNode.hpp"

namespace {
    const s32 cInitialCapacity = 0x100;
}  // namespace

RailGraph::RailGraph()
    : mNodes(new RailGraphNode[::cInitialCapacity]), mNodeCapacity(::cInitialCapacity), mNumNodes(0),
      mEdges(new RailGraphEdge[::cInitialCapacity]), mEdgeCapacity(::cInitialCapacity), mNumEdges(0) {
}

s32 RailGraph::addNode(const Vec3f& rPos) {
    if (mNumNodes == mNodeCapacity) {
        mNodes = MR::growArray(mNodes, mNodeCapacity);
        mNodeCapacity *= 2;
    }

    RailGraphNode& rNode = mNodes[mNumNodes];
    rNode.mPos = rPos;
    rNode.mFirstEdge = -1;
    return mNumNodes++;
}

void RailGraph::connectNodeTwoWay(s32 nodeA, s32 nodeB, const RailGraphEdge* pEdge) {
    RailGraphEdge edge;

    if (pEdge != nullptr) {
        edge = *pEdge;
    }

    edge.clearConnectInfo();
    edge.mNodeA = nodeA;
    edge.mNodeB = nodeB;
    edge.mDistance = mNodes[nodeA].mPos.distance(mNodes[nodeB].mPos);

    if (mNumEdges == mEdgeCapacity) {
        mEdges = MR::growArray(mEdges, mEdgeCapacity);
        mEdgeCapacity *= 2;
    }

    const s32 edgeIndex = mNumEdges++;
    mEdges[edgeIndex] = edge;
    connectEdgeToNode(nodeA, edgeIndex);
    connectEdgeToNode(nodeB, edgeIndex);
}

RailGraphNode* RailGraph::getNode(s32 index) const {
    return &mNodes[index];
}

RailGraphEdge* RailGraph::getEdge(s32 index) const {
    return &mEdges[index];
}

bool RailGraph::isValidEdge(s32 edgeIndex) const {
    return edgeIndex >= 0 && edgeIndex < mNumEdges;
}

// Appends the edge to the end of the node's edge chain.
void RailGraph::connectEdgeToNode(s32 nodeIndex, s32 edgeIndex) {
    RailGraphNode& rNode = mNodes[nodeIndex];

    if (rNode.mFirstEdge == -1) {
        rNode.mFirstEdge = edgeIndex;
        return;
    }

    s32 last = rNode.mFirstEdge;
    for (s32 next = mEdges[last].getNextEdge(nodeIndex); next != -1; next = mEdges[next].getNextEdge(nodeIndex)) {
        last = next;
    }

    mEdges[last].setNextEdge(edgeIndex, nodeIndex);
}

RailGraphIter RailGraph::getIterator() const {
    return RailGraphIter(this);
}
