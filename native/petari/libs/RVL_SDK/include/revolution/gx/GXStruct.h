#ifndef GXSTRUCT_H
#define GXSTRUCT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "revolution/gx/GXEnum.h"
#include "revolution/types.h"
#include <nebula_color.h>

// The game's colours are the native types (nebula_color.h); these names remain for the SDK's own prototypes.
typedef NebulaColor8 GXColor;
typedef NebulaColor16 GXColorS10;

typedef struct _GXTexObj {
#ifdef TARGET_PC
    // GX's record (GXTexObjInt) holds an 8-byte pointer natively: 40 bytes, 8-byte aligned (GXTexture.c checks).
    u64 dummy[5];
#else
    u32 dummy[8];
#endif
} GXTexObj;

typedef struct _GXTlutObj {
    u32 dummy[3];
} GXTlutObj;

typedef struct _GXLightObj {
    u32 dummy[16];
} GXLightObj;

typedef struct _GXVtxAttrFmtList {
    GXAttr attr;
    GXCompCnt cnt;
    GXCompType type;
    u8 frac;
} GXVtxAttrFmtList;

typedef struct _GXVtxDescList {
    GXAttr attr;
    GXAttrType type;
} GXVtxDescList;

typedef struct _GXFogAdjTable {
    u16 r[10];
} GXFogAdjTable;

#ifdef __cplusplus
}
#endif

#endif  // GXSTRUCT_H
