#pragma once

#include "JSystem/JSupport/JSUList.hpp"

class NebulaArena;

class JKRDisposer {
public:
    JKRDisposer();
    virtual ~JKRDisposer();

    /* 0x4 */ NebulaArena* mHeap;
    /* 0x8 */ JSULink< JKRDisposer > mLink;
};