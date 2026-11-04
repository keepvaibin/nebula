#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Native model trailer: descriptors immediately precede the footer, on a
// 16-byte boundary. Shader IDs are one-based indices into the build's J3D set.
enum NebulaMaterialPass {
    NEBULA_PASS_PLAYER_LIGHT = 1,
    NEBULA_PASS_REFLECTION_ALPHA = 2,
    NEBULA_PASS_LIGHT_MASK = 4,
    NEBULA_PASS_SHADOW_VOLUME = 8,
    NEBULA_PASS_PLAYER_OPACITY = 16,
    NEBULA_PASS_FLEXIBLE_COORDINATES = 32,
};

typedef struct NebulaCookedMaterial {
    uint32_t shader;
    uint32_t material_index;
    uint8_t blend_op, blend_src, blend_dst, reserved_blend;
    uint8_t depth_test, depth_func, depth_write, reserved_depth;
    uint8_t cull, early_depth, reserved_state[2];
    uint8_t coord_map[8]; /* the texture each coordinate samples, for its scale (255 none) */
    uint8_t used_maps, used_coords; /* bit per map and coordinate the stages use */
    uint8_t reserved[2];
    uint32_t passes[64];
} NebulaCookedMaterial;

typedef struct NebulaCookedMaterialFooter {
    char magic[4];
    uint32_t version;
    uint32_t count;
    uint32_t stride;
} NebulaCookedMaterialFooter;

static inline const NebulaCookedMaterial* nebula_cooked_materials(const void* data, size_t size, uint32_t* count) {
    NebulaCookedMaterialFooter footer;
    *count = 0;
    if (size < sizeof(footer) || size % 16 != 0) return NULL;
    memcpy(&footer, (const uint8_t*)data + size - sizeof(footer), sizeof(footer));
    if (memcmp(footer.magic, "NMAT", 4) != 0) return NULL;
    if (footer.version != 3 || footer.stride != sizeof(NebulaCookedMaterial) ||
        footer.count > (size - sizeof(footer)) / sizeof(NebulaCookedMaterial)) return NULL;
    const uint8_t* first = (const uint8_t*)data + size - sizeof(footer) - footer.count * sizeof(NebulaCookedMaterial);
    if ((uintptr_t)first % 16 != 0) return NULL;
    *count = footer.count;
    return (const NebulaCookedMaterial*)first;
}

#ifdef __cplusplus
static_assert(sizeof(NebulaCookedMaterial) == 288);
static_assert(sizeof(NebulaCookedMaterialFooter) == 16);
#endif
