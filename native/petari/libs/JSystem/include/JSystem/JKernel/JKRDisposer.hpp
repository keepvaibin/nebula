#pragma once

#include "JSystem/JSupport/JSUList.hpp"

class NebulaArena;

class JKRDisposer {
public:
    JKRDisposer();
    virtual ~JKRDisposer();

    NebulaArena* mHeap;
    JSULink< JKRDisposer > mLink;
};