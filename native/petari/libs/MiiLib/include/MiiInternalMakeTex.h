#ifndef RVL_FACE_LIBRARY_INTERNAL_MAKE_TEX_H
#define RVL_FACE_LIBRARY_INTERNAL_MAKE_TEX_H
#include "MiiInternalTypes.h"
#include <NebulaGpu.h>
#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct Mii_Texture;

typedef enum {
    MII_ORIGIN_CENTER,
    MII_ORIGIN_LEFT,
    MII_ORIGIN_RIGHT
} MII_ORIGIN;

typedef enum {
    MII_I_MASKRSL_32 = 32,
    MII_I_MASKRSL_64 = 64,
    MII_I_MASKRSL_128 = 128,
    MII_I_MASKRSL_256 = 256
} MII_I_MASKRSL;

typedef struct Mii_TexObj {
    struct Mii_Texture* texture;
    u8* image;
    J3DNativeTexObj tobj;
} Mii_TexObj;

typedef struct Mii_Part {
    f32 x;
    f32 y;
    f32 width;
    f32 height;
    f32 angle;
    MII_ORIGIN origin;
    Mii_TexObj ngtobj;
} Mii_Part;

typedef struct Mii_FaceParts {
    Mii_Part eyeR;
    Mii_Part eyeL;
    Mii_Part eyebrowR;
    Mii_Part eyebrowL;
    Mii_Part mouth;
    Mii_Part mustacheR;
    Mii_Part mustacheL;
    Mii_Part mole;
} Mii_FaceParts;

void Mii_SetupTextureDraw(void);
void Mii_BeginTexture(void* buffer, u16 width, u16 height, u32 clearRgba);
void Mii_MakeTexture(const Mii_CharInfo* info, u8** buffer,
                     MiiResolution resolution);
void Mii_InitPartTexture(Mii_TexObj* tobj);
void Mii_Setup2DCameraAndParam(f32 size);
void Mii_SetStage4Mole(void);
void Mii_SetStage4Mouth(u32 color);
void Mii_SetStage4Eye(u32 color, u32 type);
void Mii_SetStage4Eyebrow(u32 color);
void Mii_SetStage4Mustache(u32 color);
void Mii_SetFaceParts(const Mii_CharInfo* info, Mii_FaceParts* face,
                      MII_I_MASKRSL resolution);
void Mii_Capture(u8* buffer, const Mii_CharInfo* info, Mii_FaceParts* face,
                 MII_I_MASKRSL resolution);
void Mii_DrawFaceParts(Mii_Part* part);
void Mii_DrawQuad(f32 x, f32 y, f32 width, f32 height, f32 rotZ,
                  MII_ORIGIN origin);
MII_I_MASKRSL Mii_GetMaxMaskRsl(MiiResolution resolution);
u32 Mii_GetMaskSize(MII_I_MASKRSL resolution);
u32 Mii_GetMaskBufSize(MiiResolution resolution);

#ifdef __cplusplus
}
#endif
#endif
