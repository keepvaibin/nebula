#ifndef RVL_FACE_LIBRARY_INTERNAL_SYSTEM_H
#define RVL_FACE_LIBRARY_INTERNAL_SYSTEM_H
#include "RFL_System.h"
#include "RFLi_Database.h"
#include "RFLi_NANDLoader.h"
#include "RFLi_Types.h"
#include "RFLi_Heap.h"
#include <revolution/types.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct RFLiManager {
    RFLiLoader loader;
    BOOL deluxeTex;
    RFLiCallback systemCb;
    char UNK_0x1F1C[0x4];
    u32 _1F20;
} RFLiManager;

void* RFLiAlloc32(u32 size);
void RFLiFree(void* block);
RFLiLoader* RFLiGetLoader(void);
RFLiManager* RFLiGetManager(void);
BOOL RFLiGetUseDeluxTex(void);

#ifdef __cplusplus
}
#endif
#endif
