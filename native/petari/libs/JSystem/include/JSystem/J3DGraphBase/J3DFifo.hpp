#pragma once

#include <revolution/gd.h>
#include <revolution/gx/GXVert.h>

inline void J3DFifoLoadBPCmd(u32 regval) {
    __NebulaGXCmdBP(regval);
}

void J3DFifoLoadTexCached(GXTexMapID, u32, GXTexCacheSize, u32, GXTexCacheSize);
