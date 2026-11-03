#include "MiiLibInternal.h"
#include "MiiInternalTexture.h"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <stdio.h>

static u16 readU16(const void* p) {
    u16 value;
    memcpy(&value, p, sizeof(value));
    return value;
}

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

// RFL_Res.dat as the importer cooks it (crates/nebula-import cook/native_data.rs rfl_res_host): host order. The check
// below walks its layout once: FALSE for an archive that does not read as one (not cooked, or damaged).
static u16 rd16_(const u8* p) {
    u16 v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static u32 rd32_(const u8* p) {
    u32 v;
    memcpy(&v, p, sizeof(v));
    return v;
}

// One shape file from `p` to `end`: its tag, a face line's nine values, then positions, normals and (but for the
// forehead, hair, beard and nose) texture coordinates, each a count and its values.
static BOOL checkShape_(const u8* p, const u8* end, u32 arcIdx) {
    const BOOL txc = arcIdx != Mii_ArcID_ForeHead && arcIdx != Mii_ArcID_Hair && arcIdx != Mii_ArcID_Beard &&
                     arcIdx != Mii_ArcID_Nose;
    int section;

    p += 4;
    if (arcIdx == Mii_ArcID_Faceline) {
        if (p + 36 > end) {
            return FALSE;
        }
        p += 36;
    }
    for (section = 0; section < (txc ? 3 : 2); section++) {
        const u32 per = section == 2 ? 2 : 3;
        u32 count;

        if (p + 2 > end) {
            return FALSE;
        }
        count = rd16_(p);
        p += 2;
        if (section == 0 && count == 0) {
            return TRUE;
        }
        if (p + count * per * 2 > end) {
            return FALSE;
        }
        p += count * per * 2;
    }
    return TRUE;
}

static BOOL checkArchive_(const u8* res, u32 size) {
    u32 arcIdx;

    if (size < (Mii_ArcID_Max + 1) * 4) {
        return FALSE;
    }
    for (arcIdx = 0; arcIdx < Mii_ArcID_Max; arcIdx++) {
        const u32 section = rd32_(res + (arcIdx + 1) * 4);
        u32 numFiles;
        u32 i;
        const u8* table;
        const u8* data;

        if (section + 4 > size) {
            return FALSE;
        }
        numFiles = rd16_(res + section);
        table = res + section + 4;
        data = table + (numFiles + 1) * 4;
        if ((u32)(data - res) > size) {
            return FALSE;
        }
        for (i = 0; i < numFiles; i++) {
            const u32 self = rd32_(table + i * 4);
            const u32 next = rd32_(table + i * 4 + 4);

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
                if (next - self < sizeof(Mii_Texture)) {
                    return FALSE;
                }
                break;
            default:
                if (!checkShape_(data + self, data + next, arcIdx)) {
                    return FALSE;
                }
                break;
            }
        }
    }
    return TRUE;
}

BOOL Mii_CheckResource(const void* res, u32 size) {
    return checkArchive_((const u8*)res, size);
}

static void parseOnmemoryRes_(void) {
    Mii_Loader* loader;
    int i;

    loader = Mii_GetLoader();
    loader->version = readU16((u8*)loader->cache + 2);
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
        J3DNativeTextureRegisterRecord(Mii_GetTexImage(tex));
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

