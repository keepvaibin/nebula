#pragma once

#include "Game/System/NebulaList.hpp"

class NebulaArena;

// An object the arena holding its memory destroys when that arena is disposed of.
class JKRDisposer : public NebulaListLink< JKRDisposer > {
public:
    JKRDisposer();
    virtual ~JKRDisposer();

    NebulaArena* mHeap;
};
