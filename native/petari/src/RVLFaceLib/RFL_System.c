#include "RVLFaceLibInternal.h"
#include <revolution/types.h>

const RFLiCoordinateData scCoordinate = {1, 2, 0, FALSE, FALSE, FALSE};

// The library while it runs: the parts archive and the draw callbacks (its blocks are host allocations, RFLiAlloc32).
static RFLiManager sManager;
static RFLiManager* sRFLManager = NULL;

// Starts the library with the parts archive `resBuffer` (the game's MiiFaceDatabase.arc copy, converted to host order
// here once). The Miis are the host's (mii_record); nothing loads later, so the library is ready when this returns:
// RFLErrcode_Success, or RFLErrcode_Fatal for an archive it cannot read.
RFLErrcode RFLInitRes(void* resBuffer, u32 resSize, BOOL useDeluxTex) {
    if (resBuffer == NULL || !RFLiCookResource(resBuffer, resSize)) {
        return RFLErrcode_Fatal;
    }
    if (sRFLManager != NULL) {
        return RFLErrcode_Success;
    }

    memset(&sManager, 0, sizeof(sManager));
    sRFLManager = &sManager;
    sRFLManager->deluxeTex = useDeluxTex;

    RFLiInitLoader();
    RFLiSetCoordinateData(&scCoordinate);

    RFLiLoader* loader = &sRFLManager->loader;
    loader->cacheSize = resSize;
    loader->cache = resBuffer;
    RFLiLoadResourceHeader();
    return RFLErrcode_Success;
}

void RFLExit(void) {
    sRFLManager = NULL;
}

BOOL RFLAvailable(void) {
    return sRFLManager != NULL;
}

void* RFLiAlloc32(u32 size) {
    return NebulaMiiAlloc(size);
}

void RFLiFree(void* block) {
    NebulaMiiFree(block);
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
