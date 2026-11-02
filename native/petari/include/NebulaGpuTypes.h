#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <nebula_color.h>

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

#ifdef __cplusplus
}
#endif

