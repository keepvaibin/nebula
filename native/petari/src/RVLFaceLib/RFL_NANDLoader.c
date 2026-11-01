#include "RVLFaceLibInternal.h"
#include "RFLi_Texture.h"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <cstdio>

#define LOADER_HEADER_BUF_1_SIZE 0x100
#define LOADER_HEADER_BUF_2_SIZE 0x20

void Mii_InitLoader(void) {
    Mii_Loader* loader;
    int i;

    loader = Mii_GetLoader();
    for (i = 0; i < Mii_ArcID_Max; i++) {
        loader->archives[i].numFiles = 0;
        loader->archives[i].biggestSize = 0;
        loader->archives[i].offset = 0;
    }

    loader->cacheSize = 0;
    loader->cache = NULL;
    loader->version = 0;
    loader->headerBuf1 = NULL;
    loader->headerBuf2 = NULL;
}

// RFL_Res.dat (the game's MiiFaceDatabase.arc hands it over) in host byte order: its header and section tables, the
// part textures' headers and the shapes' counts, translations and arrays. Texels, palettes and the shapes' primitive
// bytes stay as they are. A scene can hand the same archive over again: one whose layout checks out in host byte
// order is converted already. FALSE when it checks out in neither order.
static BOOL sHostOrder;  // which order the reads below take

static u16 be16_(const u8* p) {
    return sHostOrder ? (u16)(p[1] << 8 | p[0]) : (u16)(p[0] << 8 | p[1]);
}

static u32 be32_(const u8* p) {
    return sHostOrder ? (u32)p[3] << 24 | (u32)p[2] << 16 | (u32)p[1] << 8 | p[0]
                      : (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static void swap16_(u8* p) {
    const u8 b = p[0];
    p[0] = p[1];
    p[1] = b;
}

static void swap32_(u8* p) {
    u8 b = p[0];
    p[0] = p[3];
    p[3] = b;
    b = p[1];
    p[1] = p[2];
    p[2] = b;
}

// One shape file from `p` to `end`: checks it (`apply` FALSE) or converts it.
static BOOL cookShape_(u8* p, const u8* end, u32 arcIdx, BOOL apply) {
    const BOOL txc = arcIdx != Mii_ArcID_ForeHead && arcIdx != Mii_ArcID_Hair && arcIdx != Mii_ArcID_Beard &&
                     arcIdx != Mii_ArcID_Nose;
    int section;

    p += 4;  // its tag
    if (arcIdx == Mii_ArcID_Faceline) {
        int i;
        if (p + 36 > end) {
            return FALSE;
        }
        for (i = 0; i < 9; i++) {
            if (apply) {
                swap32_(p);
            }
            p += 4;
        }
    }
    // positions (3 values), normals (3), texture coordinates (2): a count, then the values
    for (section = 0; section < (txc ? 3 : 2); section++) {
        const u32 per = section == 2 ? 2 : 3;
        u32 count;
        u32 i;

        if (p + 2 > end) {
            return FALSE;
        }
        count = be16_(p);
        if (apply) {
            swap16_(p);
        }
        p += 2;
        if (section == 0 && count == 0) {
            return TRUE;
        }
        if (p + count * per * 2 > end) {
            return FALSE;
        }
        for (i = 0; i < count * per; i++) {
            if (apply) {
                swap16_(p);
            }
            p += 2;
        }
    }
    return TRUE;
}

static BOOL cookArchive_(u8* res, u32 size, BOOL apply) {
    u32 arcIdx;

    if (size < (Mii_ArcID_Max + 1) * 4) {
        return FALSE;
    }
    for (arcIdx = 0; arcIdx < Mii_ArcID_Max; arcIdx++) {
        const u32 section = be32_(res + (arcIdx + 1) * 4);
        u32 numFiles;
        u32 i;
        u8* table;
        u8* data;

        if (section + 4 > size) {
            return FALSE;
        }
        numFiles = be16_(res + section);
        table = res + section + 4;
        data = table + (numFiles + 1) * 4;
        if ((u32)(data - res) > size) {
            return FALSE;
        }
        for (i = 0; i < numFiles; i++) {
            const u32 self = be32_(table + i * 4);
            const u32 next = be32_(table + i * 4 + 4);
            u8* file = data + self;

            if (next < self || (u32)(data - res) + next > size) {
                return FALSE;
            }
            if (next == self) {
                continue;
            }
            switch (arcIdx) {
            case Mii_ArcID_Eye:
            case Mii_ArcID_Eyebrow:
            case Mii_ArcID_FaceTex:
            case Mii_ArcID_GlassTex:
            case Mii_ArcID_Mole:
            case Mii_ArcID_Mouth:
            case Mii_ArcID_Mustache:
            case Mii_ArcID_NlineTex:
            case Mii_ArcID_CapTex:
                // Mii_Texture: width, height, colour count, palette and image offsets, LOD bias
                if (next - self < sizeof(Mii_Texture)) {
                    return FALSE;
                }
                if (apply) {
                    swap16_(file + 0x2);
                    swap16_(file + 0x4);
                    swap16_(file + 0xA);
                    swap32_(file + 0xC);
                    swap16_(file + 0x1A);
                    swap32_(file + 0x1C);
                }
                break;
            default:
                if (!cookShape_(file, data + next, arcIdx, apply)) {
                    return FALSE;
                }
                break;
            }
        }
        if (apply) {
            for (i = 0; i <= numFiles; i++) {
                swap32_(table + i * 4);
            }
            swap16_(res + section);
            swap16_(res + section + 2);
        }
    }
    if (apply) {
        for (arcIdx = 0; arcIdx < Mii_ArcID_Max; arcIdx++) {
            swap32_(res + (arcIdx + 1) * 4);
        }
        swap16_(res);
        swap16_(res + 2);
    }
    return TRUE;
}

BOOL Mii_CookResource(void* res, u32 size) {
    u8* bytes = (u8*)res;

    sHostOrder = TRUE;
    if (cookArchive_(bytes, size, FALSE)) {
        return TRUE;  // converted already
    }
    sHostOrder = FALSE;
    return cookArchive_(bytes, size, FALSE) && cookArchive_(bytes, size, TRUE);
}

static void parseOnmemoryRes_(void) NO_INLINE {
    Mii_Loader* loader;
    int i;

    loader = Mii_GetLoader();
    loader->version = *(u16*)((u8*)loader->cache + 2);
    for (i = 0; i < Mii_ArcID_Max; i++) {
        // Pointer to section offset in header
        u32* offset = (u32*)((u8*)loader->cache + ((i + 1) * 4));
        // Pointer to section
        u32* p_section = (u32*)((u8*)loader->cache + *offset);
        // Load archive
        loader->archives[i].numFiles = ((u16*)p_section)[0];
        loader->archives[i].biggestSize = ((u16*)p_section)[1];
        loader->archives[i].offset = *offset + 4;
    }
}

// The parts archive's sections (the one MiiInitRes was given).
void Mii_LoadResourceHeader(void) {
    parseOnmemoryRes_();
}

static u32 getCachedLength_(Mii_Loader* loader, u32 arcIdx, u16 fileIdx) {
    Mii_Archive* arc = &loader->archives[arcIdx];
    const void* arcBuf = (u8*)loader->cache + arc->offset;
    const u32 next = ((u32*)arcBuf)[fileIdx + 1];
    const u32 self = ((u32*)arcBuf)[fileIdx];
    return next - self;
}

static u32 getLength_(u32 arcIdx, u16 fileIdx) {
    Mii_Loader* loader;

    loader = Mii_GetLoader();
    if (loader == NULL) {
        return 0;
    }

    if (fileIdx >= loader->archives[arcIdx].numFiles) {
        return 0;
    }

    return getCachedLength_(loader, arcIdx, fileIdx);
}

static void* getCachedFile_(void* dst, Mii_Loader* loader, u32 arcIdx,
                            u16 fileIdx) {
    const u8* cache = (u8*)loader->cache;
    Mii_Archive* arc = &loader->archives[arcIdx];
    const void* arcBuf = cache + arc->offset;
    const u32 self = ((u32*)arcBuf)[fileIdx];
    const u32 next = ((u32*)arcBuf)[fileIdx + 1];
    const u32 size = next - self;
    const u32 src = arc->offset + self + (arc->numFiles * 4);

    memcpy(dst, src + 4 + cache, size);
    return dst;
}

static void* getFile_(void* dst, u32 arcIdx, u16 fileIdx) {
    Mii_Loader* loader;

    if (!MiiAvailable()) {
        return NULL;
    }

    loader = Mii_GetLoader();
    if (loader == NULL) {
        return NULL;
    }

    if (fileIdx >= loader->archives[arcIdx].numFiles) {
        return NULL;
    }

    return getCachedFile_(dst, loader, arcIdx, fileIdx);
}

u32 Mii_GetTexSize(Mii_PartsTex part, u16 file) {
    static const u32 scParts2Arc[] = {Mii_ArcID_Eye, Mii_ArcID_Eyebrow,
                                      Mii_ArcID_Mouth, Mii_ArcID_Mustache,
                                      Mii_ArcID_Mole};
    return getLength_(scParts2Arc[part], file);
}

// A part texture's image handed to the renderer as it loads (its base level).
static Mii_Texture* handOver(Mii_Texture* tex) {
    if (tex != NULL) {
        J3DNativeTextureRegisterImage(Mii_GetTexImage(tex), tex->format, tex->width, tex->height, 1);
    }
    return tex;
}

Mii_Texture* Mii_LoadTexture(Mii_PartsTex part, u16 file, void* dst) {
    static const u32 scParts2Arc[] = {Mii_ArcID_Eye, Mii_ArcID_Eyebrow,
                                      Mii_ArcID_Mouth, Mii_ArcID_Mustache,
                                      Mii_ArcID_Mole};
    return handOver((Mii_Texture*)getFile_(dst, scParts2Arc[part], file));
}

u32 Mii_GetShpTexSize(Mii_PartsShpTex part, u16 file) {
    static const u32 scParts2Arc[] = {Mii_ArcID_FaceTex, Mii_ArcID_CapTex,
                                      Mii_ArcID_NlineTex, Mii_ArcID_GlassTex};
    return getLength_(scParts2Arc[part], file);
}

Mii_Texture* Mii_LoadShpTexture(Mii_PartsShpTex part, u16 file, void* dst) {
    static const u32 scParts2Arc[] = {Mii_ArcID_FaceTex, Mii_ArcID_CapTex,
                                      Mii_ArcID_NlineTex, Mii_ArcID_GlassTex};
    // Mii_InitTexRes copies the image into the model and hands that copy over; this buffer is freed.
    return (Mii_Texture*)getFile_(dst, scParts2Arc[part], file);
}

u32 Mii_GetShapeSize(Mii_PartsShp part, u16 file) {
    static const u32 scParts2Arc[] = {
        Mii_ArcID_Nose,  Mii_ArcID_ForeHead, Mii_ArcID_Faceline,
        Mii_ArcID_Hair,  Mii_ArcID_Cap,      Mii_ArcID_Beard,
        Mii_ArcID_Nline, Mii_ArcID_Mask,     Mii_ArcID_Glass};
    return getLength_(scParts2Arc[part], file);
}

void* Mii_LoadShape(Mii_PartsShp part, u16 file, void* dst) {
    static const u32 scParts2Arc[] = {
        Mii_ArcID_Nose,  Mii_ArcID_ForeHead, Mii_ArcID_Faceline,
        Mii_ArcID_Hair,  Mii_ArcID_Cap,      Mii_ArcID_Beard,
        Mii_ArcID_Nline, Mii_ArcID_Mask,     Mii_ArcID_Glass};
    return getFile_(dst, scParts2Arc[part], file);
}

