#include "Game/Map/RailGraph.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Map/RailGraphEdge.hpp"
#include "Game/Map/RailGraphIter.hpp"
#include "Game/Map/RailGraphNode.hpp"

RailGraph::RailGraph() {
    mNodes = 0;
    mNodeCount = 0;
    mNumNodes = 0;
    mEdges = 0;
    mEdgeCount = 0;
    mNumEdges = 0;

    mNodes = new RailGraphNode[0x100];
    mNodeCount = 0x100;
    mEdges = new RailGraphEdge[0x100];
    mEdgeCount = 0x100;
}

s32 RailGraph::addNode(const Vec3f& rVec) {
    RailGraphNode node;
    node.mPos.x = rVec.x;
    node.mPos.y = rVec.y;
    node.mPos.z = rVec.z;
    s32 cnt = mNumNodes;
    mNumNodes = cnt + 1;
    if (cnt == mNodeCount) {
        mNodes = MR::growArray(mNodes, mNodeCount);
        mNodeCount *= 2;
    }

    RailGraphNode* nodes = &mNodes[cnt];
    nodes->mPos = node.mPos;
    nodes->mFirstEdge = node.mFirstEdge;
    return mNumNodes - 1;
}

void RailGraph::connectNodeTwoWay(s32 a1, s32 a2, const RailGraphEdge* pEdge) {
    RailGraphEdge edge;

    if (pEdge) {
        edge = *pEdge;
    }

    edge.clearConnectInfo();
    edge.mNodeA = a1;
    edge.mNodeB = a2;
    edge.mDistance = mNodes[a1].mPos.distance(mNodes[a2].mPos);
    s32 cnt = mNumEdges;
    mNumEdges = cnt + 1;
    if (cnt == mEdgeCount) {
        mEdges = MR::growArray(mEdges, mEdgeCount);
        mEdgeCount *= 2;
    }
    mEdges[cnt] = edge;
    s32 edgeIdx = mNumEdges - 1;
    connectEdgeToNode(a1, edgeIdx);
    connectEdgeToNode(a2, edgeIdx);
}

RailGraphNode* RailGraph::getNode(s32 idx) const {
    return &mNodes[idx];
}

RailGraphEdge* RailGraph::getEdge(s32 idx) const {
    return &mEdges[idx];
}

bool RailGraph::isValidEdge(s32 edgeIdx) const {
    bool ret = false;

    if (edgeIdx >= 0 && edgeIdx < mNumEdges) {
        ret = true;
    }

    return ret;
}

void RailGraph::connectEdgeToNode(s32 a1, s32 a2) {
    RailGraphNode* node = &mNodes[a1];
    s32 val = node->mFirstEdge;

    if (val == -1) {
        node->mFirstEdge = a2;
    } else {
        s32 next_edge = mEdges[val].getNextEdge(a1);

        while (next_edge != -1) {
            val = next_edge;
            next_edge = mEdges[next_edge].getNextEdge(a1);
        }

        mEdges[val].setNextEdge(a2, a1);
    }
}

RailGraphIter RailGraph::getIterator() const {
    return RailGraphIter(this);
}
