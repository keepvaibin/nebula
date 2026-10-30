#pragma once

#include <revolution/gx.h>
#include <revolution/mtx.h>
#include <revolution/os.h>
#include "Game/System/NebulaDisc.hpp"

class JKRHeap;

class FileRipper {
public:
    enum AllocDirection { UNK_0 = 0, UNK_1 = 1, UNK_2 = 2 };

    static void setup(u32, JKRHeap*);
    static s32 checkCompressed(const u8*);
    static void* loadToMainRAM(const char*, u8*, bool, JKRHeap*, AllocDirection);
    static bool decompressFromDVD(NebulaDisc::File*, void*, u32, u32, const u8*, u32);
    static bool decompressSzsSub(u8*, u8*);
    static u8* readSrcDataFirst();
    static u8* readSrcDataNext(u8*);
};
