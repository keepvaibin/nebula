#pragma once

#include <NebulaGpu.h>
#include <revolution/mtx.h>
#include <revolution/os.h>
#include "Game/System/NebulaDisc.hpp"

class JKRHeap;

class FileRipper {
public:
    enum AllocDirection { UNK_0 = 0, UNK_1 = 1, UNK_2 = 2 };

    static void* loadToMainRAM(const char*, u8*, JKRHeap*, AllocDirection);
};
