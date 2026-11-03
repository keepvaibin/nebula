#ifndef RVL_FACE_LIBRARY_INTERNAL_NAND_LOADER_H
#define RVL_FACE_LIBRARY_INTERNAL_NAND_LOADER_H
#include "MiiInternalTypes.h"
#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct Mii_Texture;

typedef struct Mii_Archive {
    u16 numFiles;
    u16 biggestSize;
    u32 offset;
} Mii_Archive;

typedef struct Mii_Loader {
    Mii_Archive archives[Mii_ArcID_Max];
    u32 cacheSize;
    void* cache;
    u16 version;
    void* headerBuf1;
    void* headerBuf2;
    u8 numResources;
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
