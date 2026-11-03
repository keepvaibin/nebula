#ifndef RVL_FACE_LIBRARY_INTERNAL_TEXTURE_H
#define RVL_FACE_LIBRARY_INTERNAL_TEXTURE_H
#include "MiiInternalTypes.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct Mii_Texture {
    u8 format;
    u8 alpha;
    u16 width;
    u16 height;
    u8 wrapS;
    u8 wrapT;
    u8 indexTexture;
    u8 colorFormat;
    u16 numColors;
    u32 paletteOfs;
    u8 enableLOD;
    u8 enableEdgeLOD;
    u8 enableBiasClamp;
    u8 enableMaxAniso;
    u8 minFilt;
    u8 magFilt;
    s8 minLOD;
    s8 maxLOD;
    u8 mipmapLevel;
    s8 reserved;
    s16 lodBias;
    u32 imageOfs;
} Mii_Texture;

inline void* Mii_GetTexImage(const Mii_Texture* tex) {
    return (u8*)tex + tex->imageOfs;
}

#ifdef __cplusplus
}
#endif
#endif
