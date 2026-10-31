#include "RVLFaceLibInternal.h"
#include <revolution/types.h>

#define RFL_SYSTEM_HEAP_SIZE 0x24800
#define RFL_WORK_SIZE 0x4B000
#define RFL_DELUXE_WORK_SIZE 0x64000

const RFLiCoordinateData scCoordinate = {1, 2, 0, FALSE, FALSE, FALSE};

// The library while it runs: the work memory the game gives it (its heaps), the parts archive, the draw callbacks.
static RFLiManager* sRFLManager = NULL;

u32 RFLGetWorkSize(BOOL deluxeTex) {
    return deluxeTex ? RFL_DELUXE_WORK_SIZE + sizeof(RFLiManager) : RFL_WORK_SIZE + sizeof(RFLiManager);
}

// Starts the library on `workBuffer` (RFLGetWorkSize bytes) with the parts archive `resBuffer` (the game's
// MiiFaceDatabase.arc copy, converted to host order here once). The Miis are the host's (mii_record); nothing loads
// later, so the library is ready when this returns: RFLErrcode_Success, or RFLErrcode_Fatal for an archive it cannot
// read.
RFLErrcode RFLInitRes(void* workBuffer, void* resBuffer, u32 resSize, BOOL useDeluxTex) {
    if (resBuffer == NULL || !RFLiCookResource(resBuffer, resSize)) {
        return RFLErrcode_Fatal;
    }
    if (sRFLManager != NULL) {
        return RFLErrcode_Success;
    }

    const u32 wholesize = useDeluxTex ? RFL_DELUXE_WORK_SIZE : RFL_WORK_SIZE;
    memset(workBuffer, 0, wholesize);

    sRFLManager = (RFLiManager*)workBuffer;
    sRFLManager->workBuffer = (u8*)workBuffer + sizeof(RFLiManager);
    sRFLManager->rootHeap = NebulaMiiHeapCreate(sRFLManager->workBuffer, wholesize - sizeof(RFLiManager));
    {
        void* buffer = NebulaMiiHeapAlloc(sRFLManager->rootHeap, RFL_SYSTEM_HEAP_SIZE, 32);
        sRFLManager->systemHeap = NebulaMiiHeapCreate(buffer, RFL_SYSTEM_HEAP_SIZE);
    }
    {
        const u32 size = NebulaMiiHeapLargestFree(sRFLManager->rootHeap);
        void* buffer = NebulaMiiHeapAlloc(sRFLManager->rootHeap, size, 8);
        sRFLManager->tmpHeap = NebulaMiiHeapCreate(buffer, size);
    }
    sRFLManager->deluxeTex = useDeluxTex;

    RFLSetIconDrawDoneCallback(NULL);
    RFLSetModelDrawDoneCallback(NULL);
    RFLiInitLoader();
    RFLiSetCoordinateData(&scCoordinate);

    RFLiLoader* loader = &sRFLManager->loader;
    loader->cached = TRUE;
    loader->cacheSize = resSize;
    loader->cache = resBuffer;
    RFLiLoadResourceHeader();
    return RFLErrcode_Success;
}

void RFLExit(void) {
    if (sRFLManager == NULL) {
        return;
    }

    RFLFreeCachedResource();

    NebulaMiiHeapDestroy(sRFLManager->tmpHeap);
    NebulaMiiHeapDestroy(sRFLManager->systemHeap);
    NebulaMiiHeapDestroy(sRFLManager->rootHeap);

    sRFLManager = NULL;
}

BOOL RFLAvailable(void) {
    return sRFLManager != NULL;
}

void* RFLiAlloc32(u32 size) {
    return NebulaMiiHeapAlloc(RFLiGetManager()->tmpHeap, size, 32);
}

void RFLiFree(void* block) {
    NebulaMiiHeapFree(RFLiGetManager()->tmpHeap, block);
}

RFLiLoader* RFLiGetLoader(void) {
    return !RFLAvailable() ? NULL : &RFLiGetManager()->loader;
}

RFLiManager* RFLiGetManager(void) {
    return sRFLManager;
}

BOOL RFLiGetUseDeluxTex(void) {
    return !RFLAvailable() ? FALSE : RFLiGetManager()->deluxeTex;
}
