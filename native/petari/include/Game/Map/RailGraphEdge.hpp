#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

class Table;

class RailGraphEdge {
public:
    RailGraphEdge();

    void clearConnectInfo();
    s32 getNextNode(s32) const;
    s32 getNextEdge(s32) const;
    void setNextEdge(s32, s32);
    void setArgs(const Table*, s32);

    void operator=(const RailGraphEdge& rEdge) {
        mDistance = rEdge.mDistance;
        mNodeA = rEdge.mNodeA;
        mNodeB = rEdge.mNodeB;
        mNextEdgeA = rEdge.mNextEdgeA;
        mNextEdgeB = rEdge.mNextEdgeB;
        mPointArg0 = rEdge.mPointArg0;
        mPointArg1 = rEdge.mPointArg1;
        mPointArg2 = rEdge.mPointArg2;
        mPointArg3 = rEdge.mPointArg3;
        mPointArg4 = rEdge.mPointArg4;
        mPointArg5 = rEdge.mPointArg5;
        mPointArg6 = rEdge.mPointArg6;
        mPointArg7 = rEdge.mPointArg7;
    }

    f32 mDistance;
    s32 mNodeA;
    s32 mNodeB;
    s32 mNextEdgeA;
    s32 mNextEdgeB;
    s32 mPointArg0;
    s32 mPointArg1;
    s32 mPointArg2;
    s32 mPointArg3;
    s32 mPointArg4;
    s32 mPointArg5;
    s32 mPointArg6;
    s32 mPointArg7;
};
