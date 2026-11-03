#include "MiiLibInternal.h"

const Mii_CoordinateData scCoordinate = {1, 2, 0, FALSE, FALSE, FALSE};

// The library while it runs: the parts archive and the draw callbacks (its blocks are host allocations, Mii_Alloc32).
static Mii_Manager sManager;
static Mii_Manager* sRFLManager = NULL;

// Starts the library with the parts archive `resBuffer` (the game's MiiFaceDatabase.arc copy, in host order as the
// importer cooks it). The Miis are the host's (mii_record); nothing loads later, so the library is ready when this returns:
// MiiErrcode_Success, or MiiErrcode_Fatal for an archive it cannot read.
MiiErrcode MiiInitRes(void* resBuffer, u32 resSize, BOOL useDeluxTex) {
    if (resBuffer == NULL || !Mii_CheckResource(resBuffer, resSize)) {
        return MiiErrcode_Fatal;
    }
    if (sRFLManager != NULL) {
        return MiiErrcode_Success;
    }

    memset(&sManager, 0, sizeof(sManager));
    sRFLManager = &sManager;
    sRFLManager->deluxeTex = useDeluxTex;

    Mii_InitLoader();
    Mii_SetCoordinateData(&scCoordinate);

    Mii_Loader* loader = &sRFLManager->loader;
    loader->cacheSize = resSize;
    loader->cache = resBuffer;
    Mii_LoadResourceHeader();
    return MiiErrcode_Success;
}

void MiiExit(void) {
    sRFLManager = NULL;
}

BOOL MiiAvailable(void) {
    return sRFLManager != NULL;
}

void* Mii_Alloc32(u32 size) {
    return NebulaMiiAlloc(size);
}

void Mii_Free(void* block) {
    NebulaMiiFree(block);
}

Mii_Loader* Mii_GetLoader(void) {
    return !MiiAvailable() ? NULL : &Mii_GetManager()->loader;
}

Mii_Manager* Mii_GetManager(void) {
    return sRFLManager;
}

BOOL Mii_GetUseDeluxTex(void) {
    return !MiiAvailable() ? FALSE : Mii_GetManager()->deluxeTex;
}
