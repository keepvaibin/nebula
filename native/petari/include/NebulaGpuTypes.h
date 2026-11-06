#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <nebula_color.h>
#include <nebula_game_api.h>

typedef struct SiteTexture {
    const void* mImage;
    u32 mWidth;
    u32 mHeight;
    u8 mWrapS;      // NEBULA_WRAP_*
    u8 mWrapT;
    u8 mMinFilter;  // NEBULA_FILTER_*
    u8 mMagFilter;
    u8 mMaxAniso;
    f32 mMinLod;
    f32 mMaxLod;
    f32 mLodBias;
} SiteTexture;


typedef NebulaLight NebulaLight;

#ifdef __cplusplus
}
#endif

