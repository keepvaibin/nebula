#pragma once

#include "JSystem/JSupport/JSUList.hpp"
#include <revolution.h>

class JKRDvdFile;
class JKRHeap;

enum JKRExpandSwitch {
    EXPAND_SWITCH_UNKNOWN0 = 0,
    EXPAND_SWITCH_UNKNOWN1 = 1,
    EXPAND_SWITCH_UNKNOWN2 = 2,
};

struct SYaz0Header {
    NEBULA_BE(u32) signature;
    NEBULA_BE(u32) length;
};

class JKRDMCommand {
    JKRDMCommand();
    ~JKRDMCommand();
};

class JKRDvdRipper {
public:
    enum EAllocDirection {
        UNKNOWN_EALLOC_DIRECTION = 0,
        ALLOC_DIRECTION_FORWARD = 1,
        ALLOC_DIRECTION_BACKWARD = 2,
    };

    static void* loadToMainRAM(const char*, u8*, JKRExpandSwitch, u32, JKRHeap*, EAllocDirection, u32, int*, u32*);
    static void* loadToMainRAM(s32, u8*, JKRExpandSwitch, u32, JKRHeap*, EAllocDirection, u32, int*, u32*);
    static void* loadToMainRAM(JKRDvdFile*, u8*, JKRExpandSwitch, u32, JKRHeap*, EAllocDirection, u32, int*, u32*);

    static bool isErrorRetry(void) {
        return errorRetry;
    }
    inline static u32 getSZSBufferSize() {
        return sSZSBufferSize;
    }

    static JSUList< JKRDMCommand > sDvdAsyncList;
    static bool errorRetry;
    static u32 sSZSBufferSize;
};

inline void* JKRDvdToMainRam(JKRDvdFile* file, u8* dst, JKRExpandSwitch expandSwitch, u32 dstLength, JKRHeap* heap,
                             JKRDvdRipper::EAllocDirection allocDirection, u32 offset, int* compression, u32* returnSize) {
    return JKRDvdRipper::loadToMainRAM(file, dst, expandSwitch, dstLength, heap, allocDirection, offset, compression, returnSize);
}

inline void* JKRDvdToMainRam(s32 entryNum, u8* dst, JKRExpandSwitch expandSwitch, u32 dstLength, JKRHeap* heap,
                             JKRDvdRipper::EAllocDirection allocDirection, u32 offset, int* compression, u32* returnSize) {
    return JKRDvdRipper::loadToMainRAM(entryNum, dst, expandSwitch, dstLength, heap, allocDirection, offset, compression, returnSize);
}

inline void* JKRDvdToMainRam(const char* name, u8* dst, JKRExpandSwitch expandSwitch, u32 dstLength, JKRHeap* heap,
                             JKRDvdRipper::EAllocDirection allocDirection, u32 offset, int* compression, u32* returnSize) {
    return JKRDvdRipper::loadToMainRAM(name, dst, expandSwitch, dstLength, heap, allocDirection, offset, compression, returnSize);
}
