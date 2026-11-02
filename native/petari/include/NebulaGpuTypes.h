#ifndef GpuSTRUCT_H
#define GpuSTRUCT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <NebulaGpuEnums.h>
#include <nebula_color.h>

// The game's colours are the native types (nebula_color.h); these names remain for the SDK's own prototypes.
typedef NebulaColor8 GpuColor;
typedef NebulaColor16 GpuColorS10;

typedef struct _GpuTexObj {
    // the console's record (GpuTexObjInt) holds an 8-byte pointer natively: 40 bytes, 8-byte aligned (GpuTexture.c checks).
    u64 dummy[5];
} GpuTexObj;

typedef struct _GpuTlutObj {
    u32 dummy[3];
} GpuTlutObj;

typedef struct _GpuLightObj {
    u32 dummy[16];
} GpuLightObj;

typedef struct _GpuVtxAttrFmtList {
    GpuAttr attr;
    GpuCompCnt cnt;
    GpuCompType type;
    u8 frac;
} GpuVtxAttrFmtList;

typedef struct _GpuVtxDescList {
    GpuAttr attr;
    GpuAttrType type;
} GpuVtxDescList;

typedef struct _GpuFogAdjTable {
    u16 r[10];
} GpuFogAdjTable;

#ifdef __cplusplus
}
#endif

#endif  // GpuSTRUCT_H
