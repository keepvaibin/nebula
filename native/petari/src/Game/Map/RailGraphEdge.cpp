#include "Game/Map/RailGraphEdge.hpp"
#include "Game/Util.hpp"

namespace {
    const char* const cPointArgNames[] = {"point_arg0", "point_arg1", "point_arg2", "point_arg3",
                                          "point_arg4", "point_arg5", "point_arg6", "point_arg7"};
    static_assert(ARRAY_SIZE(cPointArgNames) == RailGraphEdge::cPointArgNum, "one name per point arg");
};  // namespace

RailGraphEdge::RailGraphEdge() {
    clearConnectInfo();

    for (s32 i = 0; i < cPointArgNum; i++) {
        mPointArg[i] = -1;
    }
}

void RailGraphEdge::clearConnectInfo() {
    mNodeA = -1;
    mDistance = 0.0f;
    mNodeB = -1;
    mNextEdgeA = -1;
    mNextEdgeB = -1;
}

s32 RailGraphEdge::getNextNode(s32 node) const {
    if (mNodeA == node) {
        return mNodeB;
    }

    if (mNodeB == node) {
        return mNodeA;
    }

    return -1;
}

s32 RailGraphEdge::getNextEdge(s32 node) const {
    if (mNodeA == node) {
        return mNextEdgeA;
    }

    if (mNodeB == node) {
        return mNextEdgeB;
    }

    return -1;
}

void RailGraphEdge::setNextEdge(s32 edge, s32 node) {
    if (mNodeA == node) {
        mNextEdgeA = edge;
    } else if (mNodeB == node) {
        mNextEdgeB = edge;
    }
}

void RailGraphEdge::setArgs(const Table* pPoints, s32 pointIndex) {
    for (s32 i = 0; i < cPointArgNum; i++) {
        pPoints->getValue< s32 >(pointIndex, ::cPointArgNames[i], &mPointArg[i]);
    }
}
