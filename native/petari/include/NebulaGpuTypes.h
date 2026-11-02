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

typedef struct J3DNativeTexObj {
    const void* mImage;
    u16 mWidth;
    u16 mHeight;
    u8 mWrapS;      // NEBULA_WRAP_*
    u8 mWrapT;
    u8 mMinFilter;  // NEBULA_FILTER_*
    u8 mMagFilter;
    u8 mEdgeLod;
    u8 mMaxAniso;
    f32 mMinLod;
    f32 mMaxLod;
    f32 mLodBias;
} J3DNativeTexObj;


typedef struct J3DNativeLight {
    u8 mColor[4];
    f32 mAngle[3];     // a0-a2
    f32 mDistance[3];  // k0-k2
    f32 mPosition[3];
    f32 mDirection[3];
} J3DNativeLight;

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
