#ifndef RVL_FACE_LIBRARY_INTERNAL_NAND_LOADER_H
#define RVL_FACE_LIBRARY_INTERNAL_NAND_LOADER_H
#include "RFLi_Types.h"
#include <revolution/types.h>
#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct Mii_Texture;

typedef struct Mii_Archive {
    u16 numFiles;    // at 0x0
    u16 biggestSize; // at 0x2
    u32 offset;      // at 0x4
} Mii_Archive;

typedef struct Mii_Loader {
    Mii_Archive archives[Mii_ArcID_Max]; // at 0x0
    u32 cacheSize;                       // at 0x90
    void* cache;                         // at 0x94
    u16 version;                         // at 0x9C
    void* headerBuf1;                    // at 0xA0
    void* headerBuf2;                    // at 0xA4
    u8 numResources;                     // at 0xA8
} Mii_Loader;

void Mii_InitLoader(void);
// The archive MiiInitRes is given, in host byte order (converted once); FALSE when it is not one.
BOOL Mii_CheckResource(const void* res, u32 size);
void Mii_LoadResourceHeader(void);
u32 Mii_GetTexSize(Mii_PartsTex part, u16 file);
struct Mii_Texture* Mii_LoadTexture(Mii_PartsTex part, u16 file, void* dst);
u32 Mii_GetShpTexSize(Mii_PartsShpTex part, u16 file);
struct Mii_Texture* Mii_LoadShpTexture(Mii_PartsShpTex part, u16 file,
                                       void* dst);
u32 Mii_GetShapeSize(Mii_PartsShp part, u16 file);
void* Mii_LoadShape(Mii_PartsShp part, u16 file, void* dst);

#ifdef __cplusplus
}
#endif
#endif
