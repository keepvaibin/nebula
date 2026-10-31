/* SPDX-License-Identifier: GPL-3.0-only */
/* A texture as Nebula keeps it: a 64-byte header, then its levels, largest first. Level l is max(1, width >> l) by
   max(1, height >> l) texels in rows from the top, packed (no row padding), each texel as `layout` says.

   The cooker (crates/nebula-import) writes one where a disc file held a texture's image: the image's offset in the
   file (ResTIMG, TPL, a font's sheets) then points at this record, colour-indexed images come with their palette
   applied, and the levels are what the renderer samples. Images the game makes while it runs are records too
   (NEBULA_TEXTURE_LINEAR_*). The renderer takes a record whole (texture_image, NEBULA_TEXTURE_IMAGE) and never decodes. */
#pragma once

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEBULA_TEXTURE_MAGIC "NbTextur"
#define NEBULA_TEXTURE_VERSION 1u
#define NEBULA_TEXTURE_HEADER 64u
#define NEBULA_TEXTURE_MAX_LEVELS 11u
/* NebulaTextureImage.format of a record handed over whole (texture_image). */
#define NEBULA_TEXTURE_IMAGE 0x40u
/* NebulaTextureImage.format of texels the game wrote, one level in rows from the top, packed, laid out as
   NEBULA_TEXTURE_L8 / _LA8 / _RGBA8 (the code minus NEBULA_TEXTURE_LINEAR_L8): the host keeps them as a record. */
#define NEBULA_TEXTURE_LINEAR_L8 0x41u
#define NEBULA_TEXTURE_LINEAR_LA8 0x42u
#define NEBULA_TEXTURE_LINEAR_RGBA8 0x43u
/* disc_format of a record the game made while running. */
#define NEBULA_TEXTURE_MADE 0xFFu

enum {
    NEBULA_TEXTURE_RGBA8 = 0, /* r, g, b, a */
    NEBULA_TEXTURE_L8 = 1,    /* l: r = g = b = a = l */
    NEBULA_TEXTURE_LA8 = 2,   /* l, a: r = g = b = l */
    /* 4x4 blocks in rows, ceil(width / 4) across, 8 bytes each: two RGB565 colours c0, c1 (little-endian), then 16 2-bit
       indices (little-endian; texel (x, y) of the block at bit 2 * (4y + x)) - BC1's block. Index 0 is c0, 1 is c1;
       with c0 > c1, 2 and 3 are (5 c0 + 3 c1) / 8 and (3 c0 + 5 c1) / 8 per channel (5- and 6-bit channels widened by
       bit replication first), else 2 is (c0 + c1) / 2 and 3 that colour with alpha 0. The disc's CMPR textures. */
    NEBULA_TEXTURE_BLOCK4 = 3,
};

enum {
    /* The renderer runs its dither-checker filters on each level as it uploads it (the game's own images: the filters
       ran on every image before textures were cooked, and the cooker has already run them on the disc's). */
    NEBULA_TEXTURE_FILTER_AT_UPLOAD = 1u << 0,
};

typedef struct NebulaTexture {
    char magic[8];          /* NEBULA_TEXTURE_MAGIC, no terminator */
    uint32_t version;       /* NEBULA_TEXTURE_VERSION */
    uint32_t size;          /* the whole record, header included */
    uint16_t width, height; /* level 0, in texels */
    uint8_t levels;         /* 1 .. NEBULA_TEXTURE_MAX_LEVELS */
    uint8_t layout;         /* NEBULA_TEXTURE_RGBA8, _L8 or _LA8 */
    uint8_t disc_format;    /* the GX format the disc stored it in (texture-pack names), NEBULA_TEXTURE_MADE */
    uint8_t flags;          /* NEBULA_TEXTURE_FILTER_AT_UPLOAD */
    /* Texture packs name a texture by these (Dolphin's convention): xxh64 of the disc's level-0 bytes, and of the
       palette entries a colour-indexed one used (0 for the others). 0 and 0 for an image the game makes. */
    uint64_t pack_hash;
    uint64_t pack_palette_hash;
    uint8_t reserved[24];
} NebulaTexture;

#ifdef __cplusplus
static_assert(sizeof(NebulaTexture) == NEBULA_TEXTURE_HEADER, "the texture header is 64 bytes");
#else
_Static_assert(sizeof(NebulaTexture) == NEBULA_TEXTURE_HEADER, "the texture header is 64 bytes");
#endif

/* The bytes of one texel in a layout of texels (RGBA8, L8, LA8). */
static inline uint32_t nebula_texture_texel_bytes(uint8_t layout) {
    return layout == NEBULA_TEXTURE_RGBA8 ? 4u : layout == NEBULA_TEXTURE_LA8 ? 2u : 1u;
}

/* The bytes of `levels` levels of a width x height texture in `layout`, header excluded. */
static inline uint32_t nebula_texture_level_bytes(uint32_t width, uint32_t height, uint32_t levels, uint8_t layout) {
    uint32_t total = 0;
    for (uint32_t l = 0; l < levels; l++) {
        const uint32_t w = width >> l != 0 ? width >> l : 1u;
        const uint32_t h = height >> l != 0 ? height >> l : 1u;
        total += layout == NEBULA_TEXTURE_BLOCK4 ? ((w + 3u) / 4u) * ((h + 3u) / 4u) * 8u
                                                 : w * h * nebula_texture_texel_bytes(layout);
    }
    return total;
}

/* The header of a one-level record the game keeps in static memory (an image it made): `{header, texels}`. */
#define NEBULA_TEXTURE_STATIC(width, height, layout, texel_bytes)                                                  \
    {{'N', 'b', 'T', 'e', 'x', 't', 'u', 'r'}, NEBULA_TEXTURE_VERSION, NEBULA_TEXTURE_HEADER + (texel_bytes), (width), \
     (height), 1, (layout), NEBULA_TEXTURE_MADE, 0, 0, 0, {0}}

/* The record at `p`, or NULL when `p` holds none (or one of another version). */
static inline const NebulaTexture* nebula_texture_record(const void* p) {
    const NebulaTexture* t = (const NebulaTexture*)p;
    if (p == NULL || memcmp(t->magic, NEBULA_TEXTURE_MAGIC, 8) != 0 || t->version != NEBULA_TEXTURE_VERSION) {
        return NULL;
    }
    return t;
}

/* Writes a record's header at `p` for an image the game makes; its levels follow at p + NEBULA_TEXTURE_HEADER. */
static inline void nebula_texture_init(void* p, uint16_t width, uint16_t height, uint8_t levels, uint8_t layout,
                                       uint8_t flags) {
    NebulaTexture* t = (NebulaTexture*)p;
    memset(t, 0, sizeof(*t));
    memcpy(t->magic, NEBULA_TEXTURE_MAGIC, 8);
    t->version = NEBULA_TEXTURE_VERSION;
    t->width = width;
    t->height = height;
    t->levels = levels;
    t->layout = layout;
    t->disc_format = NEBULA_TEXTURE_MADE;
    t->flags = flags;
    t->size = NEBULA_TEXTURE_HEADER + nebula_texture_level_bytes(width, height, levels, layout);
}

#ifdef __cplusplus
}
#endif
