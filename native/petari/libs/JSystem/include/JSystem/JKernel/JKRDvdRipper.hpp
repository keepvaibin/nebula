#pragma once

#include "JSystem/JSupport/JSUList.hpp"
#include <NebulaGpu.h>
#include <revolution/mtx.h>
#include <revolution/os.h>

class JKRDvdFile;
class JKRHeap;

class JKRDvdRipper {
public:
    enum EAllocDirection {
        UNKNOWN_EALLOC_DIRECTION = 0,
        ALLOC_DIRECTION_FORWARD = 1,
        ALLOC_DIRECTION_BACKWARD = 2,
    };

    static void* loadToMainRAM(const char*, u8*, u32, JKRHeap*, EAllocDirection, u32, u32*);
    static void* loadToMainRAM(s32, u8*, u32, JKRHeap*, EAllocDirection, u32, u32*);
    static void* loadToMainRAM(JKRDvdFile*, u8*, u32, JKRHeap*, EAllocDirection, u32, u32*);

    // The importer expands every compressed file (crates/nebula-import/src/cook/archive.rs): a Yaz0 or Yay0 file here
    // was not cooked.
    static void panicIfCompressed(const void* pData, const char* pName);

    static bool isErrorRetry(void) {
        return errorRetry;
    }

    static bool errorRetry;
};

inline void* JKRDvdToMainRam(JKRDvdFile* file, u8* dst, u32 dstLength, JKRHeap* heap, JKRDvdRipper::EAllocDirection allocDirection, u32 offset,
                             u32* returnSize) {
    return JKRDvdRipper::loadToMainRAM(file, dst, dstLength, heap, allocDirection, offset, returnSize);
}

inline void* JKRDvdToMainRam(s32 entryNum, u8* dst, u32 dstLength, JKRHeap* heap, JKRDvdRipper::EAllocDirection allocDirection, u32 offset,
                             u32* returnSize) {
    return JKRDvdRipper::loadToMainRAM(entryNum, dst, dstLength, heap, allocDirection, offset, returnSize);
}

inline void* JKRDvdToMainRam(const char* name, u8* dst, u32 dstLength, JKRHeap* heap, JKRDvdRipper::EAllocDirection allocDirection, u32 offset,
                             u32* returnSize) {
    return JKRDvdRipper::loadToMainRAM(name, dst, dstLength, heap, allocDirection, offset, returnSize);
}
