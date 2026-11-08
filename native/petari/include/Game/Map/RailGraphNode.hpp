#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class RailGraphNode {
public:
    RailGraphNode();

    Vec3f mPos;
    s32 mFirstEdge;
};