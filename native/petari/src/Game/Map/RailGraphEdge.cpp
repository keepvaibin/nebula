#include "Game/Map/RailGraphEdge.hpp"
#include "Game/Util.hpp"

RailGraphEdge::RailGraphEdge() {
    mNodeA = -1;
    mDistance = 0.0f;
    mNodeB = -1;
    mNextEdgeA = -1;
    mNextEdgeB = -1;
    mPointArg0 = -1;
    mPointArg1 = -1;
    mPointArg2 = -1;
    mPointArg3 = -1;
    mPointArg4 = -1;
    mPointArg5 = -1;
    mPointArg6 = -1;
    mPointArg7 = -1;
}

void RailGraphEdge::clearConnectInfo() {
    mNodeA = -1;
    mDistance = 0.0f;
    mNodeB = -1;
    mNextEdgeA = -1;
    mNextEdgeB = -1;
}

s32 RailGraphEdge::getNextNode(s32 a1) const {
    s32 node = mNodeA;

    if (node == a1) {
        return mNodeB;
    }

    if (mNodeB != a1) {
        return -1;
    }

    return node;
}

s32 RailGraphEdge::getNextEdge(s32 a1) const {
    if (mNodeA == a1) {
        return mNextEdgeA;
    }

    if (mNodeB == a1) {
        return mNextEdgeB;
    }

    return -1;
}

void RailGraphEdge::setNextEdge(s32 a1, s32 a2) {
    if (mNodeA == a2) {
        mNextEdgeA = a1;
    } else if (mNodeB == a2) {
        mNextEdgeB = a1;
    }
}

void RailGraphEdge::setArgs(const Table* pInfo, s32 a2) {
    pInfo->getValue< s32 >(a2, "point_arg0", &mPointArg0);
    pInfo->getValue< s32 >(a2, "point_arg1", &mPointArg1);
    pInfo->getValue< s32 >(a2, "point_arg2", &mPointArg2);
    pInfo->getValue< s32 >(a2, "point_arg3", &mPointArg3);
    pInfo->getValue< s32 >(a2, "point_arg4", &mPointArg4);
    pInfo->getValue< s32 >(a2, "point_arg5", &mPointArg5);
    pInfo->getValue< s32 >(a2, "point_arg6", &mPointArg6);
    pInfo->getValue< s32 >(a2, "point_arg7", &mPointArg7);
}