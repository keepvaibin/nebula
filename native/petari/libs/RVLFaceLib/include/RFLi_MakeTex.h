#ifndef RVL_FACE_LIBRARY_INTERNAL_MAKE_TEX_H
#define RVL_FACE_LIBRARY_INTERNAL_MAKE_TEX_H
#include "RFLi_Types.h"
#include <NebulaGpu.h>
#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct Mii_Texture;

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
    struct Mii_Texture* texture; // at 0x0
    u8* image;                   // at 0x4
    J3DNativeTexObj tobj;               // at 0x8
} Mii_TexObj;

typedef struct Mii_Part {
    f32 x;             // at 0x0
    f32 y;             // at 0x4
    f32 width;         // at 0x8
    f32 height;        // at 0xC
    f32 angle;         // at 0x10
    MII_ORIGIN origin; // at 0x14
    Mii_TexObj ngtobj; // at 0x18
} Mii_Part;

typedef struct Mii_FaceParts {
    Mii_Part eyeR;      // at 0x0
    Mii_Part eyeL;      // at 0x40
    Mii_Part eyebrowR;  // at 0x80
    Mii_Part eyebrowL;  // at 0xC0
    Mii_Part mouth;     // at 0x100
    Mii_Part mustacheR; // at 0x140
    Mii_Part mustacheL; // at 0x180
    Mii_Part mole;      // at 0x1C0
} Mii_FaceParts;

void Mii_SetupTextureDraw(void);
void Mii_BeginTexture(void* buffer, u16 width, u16 height, u32 clearRgba);
void Mii_MakeTexture(const Mii_CharInfo* info, u8** buffer,
                     MiiResolution resolution);
void Mii_InitRFLTexture(Mii_TexObj* tobj);
void Mii_Setup2DCameraAndParam(void);
void Mii_SetTev4Mole(void);
void Mii_SetTev4Mouth(u32 color);
void Mii_SetTev4Eye(u32 color, u32 type);
void Mii_SetTev4Eyebrow(u32 color);
void Mii_SetTev4Mustache(u32 color);
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
