#ifndef RVL_FACE_LIBRARY_INTERNAL_SYSTEM_H
#define RVL_FACE_LIBRARY_INTERNAL_SYSTEM_H
#include "RFL_System.h"
#include "RFLi_Database.h"
#include "RFLi_NANDLoader.h"
#include "RFLi_Types.h"
#include "RFLi_Heap.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct Mii_Manager {
    Mii_Loader loader;
    BOOL deluxeTex;
    Mii_Callback systemCb;
    char UNK_0x1F1C[0x4];
    u32 _1F20;
} Mii_Manager;

void* Mii_Alloc32(u32 size);
void Mii_Free(void* block);
Mii_Loader* Mii_GetLoader(void);
Mii_Manager* Mii_GetManager(void);
BOOL Mii_GetUseDeluxTex(void);

#ifdef __cplusplus
}
#endif
#endif
