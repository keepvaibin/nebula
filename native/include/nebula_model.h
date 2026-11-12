/* SPDX-License-Identifier: GPL-3.0-only */
/* A model as the store keeps it: one flat block in host order, used in place. Every *_at is a byte offset from the
   block's start. crates/nebula-import/src/cook/model_write.rs writes this layout. */
#pragma once

#include <stdint.h>
#include "nebula_limits.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NEBULA_MODEL_MAGIC 0x4C444D4Eu /* "NMDL" */
#define NEBULA_MODEL_VERSION 8u
#define NEBULA_MODEL_NONE NEBULA_NONE32

enum { NEBULA_SCALE_BASIC = 0, NEBULA_SCALE_SOFTIMAGE = 1, NEBULA_SCALE_MAYA = 2 };

/* NebulaModelHeader.flags, above the scale mode in the low 4 bits. */
enum {
    NEBULA_MODEL_HAS_BILLBOARD = 1u << 4,
    NEBULA_MODEL_MATERIALS_ONLY = 1u << 5, /* a material table for another model: no joints or meshes */
};

/* A pipeline's fixed state as one word, in one canonical form: what does nothing is zero. NEBULA_BLEND_*, NEBULA_FACTOR_*,
   NEBULA_COMPARE_*, NEBULA_CULL_*, NEBULA_STENCIL_*. */
#define NEBULA_STATE_BLEND(op, src, dst) ((uint32_t)(op) | (uint32_t)(op != 1 ? 0u : (uint32_t)(src)) << 2 | (uint32_t)(op != 1 ? 0u : (uint32_t)(dst)) << 6)
#define NEBULA_STATE_DEPTH(test, func, write) ((test) ? 1u << 10 | (uint32_t)(func) << 11 : 0u) | ((write) ? 1u << 14 : 0u)
#define NEBULA_STATE_CULL(cull) ((uint32_t)(cull) << 15)
#define NEBULA_STATE_COLOR_WRITE (1u << 17)
#define NEBULA_STATE_ALPHA_WRITE (1u << 18)
#define NEBULA_STATE_ALPHA_PASS (1u << 19) /* writes alpha where the pass's target keeps it */
#define NEBULA_STATE_STENCIL(func, op) ((uint32_t)(func) << 20 | (uint32_t)((func) != 0 ? (op) : 0) << 22)
#define NEBULA_STATE_STENCIL_OWN (1u << 24) /* its stencil, not the pass's; gone from the state a pipeline takes */
#define NEBULA_STATE_PIXEL_SHADING (1u << 25) /* alpha test cannot reject: shade per pixel, keep MSAA coverage */

/* A conservative proof independent of alpha and both reference values. Other compares may vary; NEVER and ALWAYS
   are constant. Used by dynamic materials to avoid sample-frequency shading for a uniformly passing alpha test. */
static inline int nebula_alpha_test_always(uint32_t compare0, uint32_t compare1, uint32_t logic) {
    switch (logic) {
    case 0: return compare0 == 7u && compare1 == 7u;
    case 1: return compare0 == 7u || compare1 == 7u;
    case 2: return (compare0 == 0u && compare1 == 7u) || (compare0 == 7u && compare1 == 0u);
    case 3: return (compare0 == 0u && compare1 == 0u) || (compare0 == 7u && compare1 == 7u);
    default: return 0;
    }
}

typedef struct NebulaModelHeader {
    uint32_t magic, version, size, flags;
    uint32_t joint_count, step_count, joint_mesh_count;
    uint32_t draw_matrix_count, full_weight_count, envelope_count, mix_count;
    uint32_t mesh_count, material_count, texture_count;
    uint32_t source_vertex_count; /* the shared positions the file had: a game that rewrites them writes this many */
    uint32_t joints_at, steps_at, joint_meshes_at;
    uint32_t draw_matrices_at, envelopes_at, mixes_at, inverse_binds_at, important_at;
    uint32_t meshes_at, materials_at, textures_at;
    uint32_t geometry_at, geometry_size; /* every stream, for one copy to the GPU */
    uint32_t hits_at;  /* material_count NebulaModelHit, in the materials' order */
    uint32_t paint_lights_at; /* mesh_count NebulaModelPaintLight, in the meshes' order; 0: none */
} NebulaModelHeader;

/* A painted mesh whose paint is one light (ray tracing's Physical style, RT-MASTER-PLAN 14.6): its vertex light fitted
   as a + b max(n.d, 0); the share b / (a + b) (0: the mesh stays as painted) and d in the model's space, octahedral,
   signed 16-bit (x, y; -32767 to 32767 for -1 to 1). */
typedef struct NebulaModelPaintLight {
    float share;
    int16_t direction[2];
} NebulaModelPaintLight;

/* Joints by number, the number animation tracks and the game use. */
typedef struct NebulaModelJoint {
    float scale[3];
    float rotation[3];
    uint32_t parent;
    float translation[3];
    float radius, min[3], max[3];
    uint32_t child, younger; /* NEBULA_MODEL_NONE when there is none */
    uint32_t first_mesh, mesh_count; /* in joint_meshes: materials in entry order */
    uint8_t kind, scale_compensate, billboard, reserved;
    uint32_t name_at, name_hash;
} NebulaModelJoint;

/* The walk Skeleton::evaluate takes: enter a joint, later leave it, parents before children. */
enum { NEBULA_STEP_ENTER = 0, NEBULA_STEP_LEAVE = 1 };
typedef struct NebulaModelStep {
    uint32_t joint, op;
} NebulaModelStep;

typedef struct NebulaModelDrawMatrix {
    uint32_t weighted; /* 0: index names a joint; 1: index names an envelope */
    uint32_t index;
} NebulaModelDrawMatrix;

typedef struct NebulaModelEnvelope {
    uint32_t first_mix, mix_count;
} NebulaModelEnvelope;

typedef struct NebulaModelMix {
    uint32_t joint;
    float weight;
} NebulaModelMix;

/* NebulaModelMesh.attributes */
enum {
    NEBULA_MESH_NORMAL = 1u << 0,
    NEBULA_MESH_COLOR0 = 1u << 1,
    NEBULA_MESH_COLOR1 = 1u << 2,
    NEBULA_MESH_TANGENTS = 1u << 3,
    NEBULA_MESH_GAIN = 1u << 4, /* stream 3: a word a vertex, the de-light gain (NEBULA_INPUT_GAIN) */
};

/* Where a mesh keeps coordinate n: stream 1 at 16 for coordinate 0, stream 3 at `offset` for the others;
   NEBULA_MESH_NO_COORD in `stream` when it has none. */
#define NEBULA_MESH_NO_COORD 0xFFu
typedef struct NebulaModelMeshCoord {
    uint8_t stream, reserved;
    uint16_t offset;
} NebulaModelMeshCoord;

/* Stream 0: position, 3 floats. Stream 1: normal 3 floats, colour word, coordinate 0 2 floats (24 bytes).
   Stream 2: draw matrix number, u32, present when single_matrix is NEBULA_MODEL_NONE. Stream 3: the further
   coordinates, 2 floats each in ascending order, then colour 1, then tangent and binormal, then the gain. */
typedef struct NebulaModelMesh {
    float min[3], max[3], radius;
    uint32_t vertex_count, index_count;
    uint32_t positions_at, attributes_at, matrices_at, further_at, indices_at;
    uint32_t attributes;
    uint32_t further_stride;
    uint32_t single_matrix;
    uint32_t material;
    uint8_t index_bytes, billboard, reserved[2];
    /* Its bending triangles refined: the streams hold refined_vertex_count vertices, vertex_count of them the plain mesh's;
       refined_index_count indices at refined_indices_at draw it round, 0 when it has no bending triangles. */
    uint32_t refined_vertex_count, refined_index_count, refined_indices_at;
    uint32_t flats_at; /* a bit per vertex: a flat solid's corner shaded soft, ray tracing shades its face. 0: none */
    uint32_t coord_count, coords_at; /* NebulaModelMeshCoord by coordinate number, at coords_at from this record */
} NebulaModelMesh;

static inline const NebulaModelMeshCoord* nebula_mesh_coords(const NebulaModelMesh* mesh) {
    return (const NebulaModelMeshCoord*)((const uint8_t*)mesh + mesh->coords_at);
}

static inline int nebula_mesh_has_coord(const NebulaModelMesh* mesh, uint32_t coord) {
    return coord < mesh->coord_count && nebula_mesh_coords(mesh)[coord].stream != NEBULA_MESH_NO_COORD;
}

/* What a constant row of a program holds: the material fills its rows by these. A row is a kind and an index,
   kind << 16 | index: kind 0 the fixed rows, then a coordinate's scale, a warp matrix's rows, a coordinate's rows. */
#define NEBULA_ROW_KIND(row) ((uint32_t)(row) >> 16)
#define NEBULA_ROW_INDEX(row) ((uint32_t)(row) & 0xFFFFu)
enum {
    NEBULA_ROW_REGISTER = 0,       /* registers 1 to 3 of the stages */
    NEBULA_ROW_CONSTANT = 3,       /* constant colours 0 to 3 */
    NEBULA_ROW_MATERIAL_COLOR = 7, /* 2 */
    NEBULA_ROW_AMBIENT_COLOR = 9,  /* 2 */
    NEBULA_ROW_FOG = 11,           /* start, end, near, far */
    NEBULA_ROW_FOG_COLOR = 12,
    NEBULA_ROW_ALPHA_REF = 13,
    NEBULA_ROW_OVERRIDE = 14,      /* the replacing forms' values */
    NEBULA_ROW_SCALE = 1 << 16,    /* + coordinate: xy its scale, zw the size of the map of its number */
    NEBULA_ROW_WARP = 2 << 16,     /* + two per warp matrix */
    NEBULA_ROW_COORD = 3 << 16,    /* + three per coordinate */
};

/* A form's vertex inputs after position and matrix. */
enum {
    NEBULA_INPUT_NORMAL = 0,
    NEBULA_INPUT_COLOR0 = 1,
    NEBULA_INPUT_COLOR1 = 2,
    NEBULA_INPUT_TANGENT = 3,
    NEBULA_INPUT_BINORMAL = 4,
    NEBULA_INPUT_WIDEN = 5, /* a site's widened corner: one uint, which corner */
    NEBULA_INPUT_MATRIX = 6, /* a site's vertex carrying its draw matrix: one uint */
    NEBULA_INPUT_GAIN = 7, /* the painted light's de-light gain a channel (NEBULA_MESH_GAIN): one uint, signed bytes of
                              log2 x 64 (r, g, b), 0 a gain of 1 */
    NEBULA_INPUT_COORD0 = 16, /* + the mesh's coordinate number */
};

/* NebulaModelForm.flags */
enum {
    NEBULA_FORM_NORMAL_COORD = 1u << 0, /* coordinate 0 is the normal through the identity */
    NEBULA_FORM_LIT = 1u << 1,          /* lit at the pixel */
    NEBULA_FORM_ALPHA_TEST = 1u << 2,
    NEBULA_FORM_FOG_LINEAR = 1u << 3,   /* linear fog over a material cooked without any */
};

/* NebulaModelForm.pass: an edit of the material the programs were written from. */
enum {
    NEBULA_FORM_PASS_PLAYER_LIGHT = 1,
    NEBULA_FORM_PASS_REFLECTION_ALPHA = 2,
    NEBULA_FORM_PASS_PLAYER_OPACITY = 16,
    NEBULA_FORM_PASS_FLEXIBLE_COORDINATES = 32,
};

/* One way a material can draw: two programs, named by their texts' hashes, and what their constants and textures are. */
typedef struct NebulaModelForm {
    uint64_t vertex, pixel;
    uint32_t vertex_rows_at, pixel_rows_at; /* u32 rows, NEBULA_ROW_* */
    uint32_t textures_at, inputs_at;        /* u8 material texture numbers; u8 NEBULA_INPUT_* */
    uint32_t vertex_row_count, pixel_row_count;
    uint32_t texture_count, input_count;
    uint8_t replace, flags; /* replace: 0 the material's stages, else the replacing pass */
    uint8_t pass, reserved[13];
} NebulaModelForm;

typedef struct NebulaModelChannel {
    uint8_t enable, material_source, ambient_source, light_mask, diffuse, attenuation;
} NebulaModelChannel;

typedef struct NebulaModelCoord {
    uint8_t type, source, reserved[2];
    uint32_t matrix;
} NebulaModelCoord;

typedef struct NebulaModelCoordMatrix {
    uint32_t slot; /* which of the material's matrices */
    uint8_t projection, info, reserved[2];
    float center[3];
    float scale[2];
    float rotation;
    float translation[2];
    float effect[4][4];
} NebulaModelCoordMatrix;

typedef struct NebulaModelWarp {
    float rows[2][3];
} NebulaModelWarp;

typedef struct NebulaModelFog {
    uint8_t type, reserved[3];
    float start, end, near_z, far_z;
    uint8_t color[4];
} NebulaModelFog;

/* NebulaModelMaterial.flags */
enum {
    NEBULA_MATERIAL_HAS_AMBIENT = 1u << 0,
    NEBULA_MATERIAL_HAS_STAGE_COLORS = 1u << 1,
    NEBULA_MATERIAL_HAS_INDIRECT = 1u << 2,
    NEBULA_MATERIAL_HAS_FOG = 1u << 3,
    NEBULA_MATERIAL_ON_BILLBOARD = 1u << 4, /* its joint turns to the eye; eye-space coordinates read world rows */
};

/* NebulaModelSlot.uses: the stages read the map, the stages or warp orders read the coordinate. */
enum { NEBULA_SLOT_USES_MAP = 1u << 0, NEBULA_SLOT_USES_COORD = 1u << 1 };
#define NEBULA_SLOT_NO_MAP 0xFFFFu

/* A material's slot n: map n, coordinate n and matrix n, as many as anything in the material names. */
typedef struct NebulaModelSlot {
    uint32_t texture;   /* the model texture map n shows, NEBULA_MODEL_NONE none */
    uint16_t scale_map; /* the map coordinate n's scale comes from, NEBULA_SLOT_NO_MAP none */
    uint8_t uses, reserved;
} NebulaModelSlot;

typedef struct NebulaModelMaterial {
    uint32_t name_at, name_hash;
    uint32_t same_as; /* materials of a model with one value are the same in everything */
    uint32_t mesh, joint;
    uint8_t mode, cull, flags, early_depth;
    uint32_t channel_count, coord_count, warp_count, slot_count;
    uint32_t matrix_count, form_count;
    uint8_t material_color[2][4], ambient_color[2][4];
    int16_t registers[4][4];
    uint8_t constants[4][4];
    NebulaModelChannel channels[4];
    uint8_t alpha_comp0, alpha_op, alpha_comp1, alpha_ref0, alpha_ref1;
    uint8_t blend_type, blend_src, blend_dst;
    uint8_t depth_test, depth_func, depth_write, has_scale;
    float normal_scale[3];
    NebulaModelFog fog;
    uint32_t state; /* its pipeline state, NEBULA_STATE_* */
    uint32_t coords_at, slots_at, matrices_at, warps_at, forms_at;
} NebulaModelMaterial;

/* What a ray reads of a material where it meets it (crates/nebula-import/src/cook/hit_record.rs): its class, from
   the material's own data, the numbers that class takes, and the material's stages as data, for a shader that cannot
   run the material's program (a ray's hit is not a pixel of its draw). */
enum {
    NEBULA_HIT_DIELECTRIC = 0, /* lit, opaque, no painted reflection */
    NEBULA_HIT_GLOSS = 1,      /* a lit base under a painted reflection or an authored highlight */
    NEBULA_HIT_METAL = 2,      /* a painted reflection carries the look over a dark base */
    NEBULA_HIT_GLASS = 3,      /* blended, with a painted reflection or a warp */
    NEBULA_HIT_CUTOUT = 4,     /* unused from model 6 (alpha-tested materials take their look's class and
                                  NEBULA_HIT_ALPHA_TEST); kept so the numbers stay */
    NEBULA_HIT_RAMP = 5,       /* shaded through a ramp map */
    NEBULA_HIT_EMISSIVE = 6,   /* shows itself without light: added to what is behind it, or blended, unlit and bright */
    NEBULA_HIT_PAINTED = 7,    /* not lit, opaque: its colours are its light */
    NEBULA_HIT_TRANSLUCENT = 8, /* blended, no reflection */
    NEBULA_HIT_WATER = 9,      /* a water surface (the cooker's rt_water.tsv) */
    NEBULA_HIT_CLASS_COUNT
};

/* NebulaModelHit.flags */
enum {
    NEBULA_HIT_LIT = 1u << 0,          /* a colour channel sums lights */
    NEBULA_HIT_TWO_SIDED = 1u << 1,
    NEBULA_HIT_ALPHA_TEST = 1u << 2,
    NEBULA_HIT_VERTEX_COLOUR = 1u << 3, /* its material colour is the vertex's */
    NEBULA_HIT_ENVIRONMENT = 1u << 4,   /* it reads a map through the normal */
    NEBULA_HIT_DRAWN_SHADOW = 1u << 5,  /* it is a shadow drawn by hand (a shadow model or decal): a renderer that
                                           traces shadows neither draws it nor lets it cast one */
    NEBULA_HIT_PAINTED_SHADOW = 1u << 6, /* painted (vertex colour, not lit) on a model whose paint holds the shadows
                                            things cast on it (the cooker's study of its vertex light): a renderer that
                                            traces shadows lets only lit things shadow it (RT-MASTER-PLAN 14.6) */
    NEBULA_HIT_DRAWN_SHEEN = 1u << 7,  /* a painted sheen drawn as an added layer over a body that mirrors (c49): a
                                          renderer that traces reflections neither draws it nor lets a ray meet it */
};

/* One combiner stage as the game's file orders it (the cooker's StageIr). */
typedef struct NebulaModelStage {
    uint8_t coord, map, channel;
    uint8_t color_in[4], color_op, color_bias, color_scale, color_clamp, color_out;
    uint8_t alpha_in[4], alpha_op, alpha_bias, alpha_scale, alpha_clamp, alpha_out;
    uint8_t raster_swap, texture_swap, color_constant, alpha_constant;
    uint8_t warp[9];
    uint8_t reserved[2];
} NebulaModelStage;

typedef struct NebulaModelHit {
    uint8_t klass;       /* NEBULA_HIT_* */
    uint8_t stage_count;
    uint8_t flags;
    uint8_t glow_source; /* where a glow's colour comes from as the game animates it: 0 fixed (glow's own), 1 + a
                            NEBULA_ROW_* code under NEBULA_ROW_OVERRIDE (the register, constant or material colour that
                            carries what it shows unlit), 16 the vertex colour */
    float roughness, metal, ior, transmission;
    float base[3];      /* its colour under full light, maps mid grey: 0 to 1 */
    float emission[3];  /* what it shows with no light at all */
    float environment_share; /* how much of its look a painted reflection carries, 0 to 1 */
    uint32_t stages_at; /* stage_count NebulaModelStage, from the model's start; 0: none */
    uint8_t swaps[4][4];
    float thickness;    /* units light crosses inside it: a closed mesh's 3 x volume / area, else half the smallest
                           extent of its meshes' bounds */
    float glow[4];      /* a model that glows (the cooker's rt_glow.tsv, or a surface added to what is behind it): the
                           light's colour, 0 to 1, and in w its intensity in candela (100 units a metre); below
                           zero: a luminance, candela a square metre of the mesh; 0: it lights nothing */
    float env_tint[3];  /* its painted reflection's colour: the map's mean times what multiplies it, largest channel 1 */
    uint8_t env_map;    /* the material's map read through the normal (its painted reflection), 255 none */
    uint8_t reserved[3];
    float body, shine, tint_keep; /* the review's (rt_materials.tsv): a see-through body's coverage, the reflection's
                                     gain, how much of its tint stays; below 0: the class's own */
    float reserved2;
} NebulaModelHit;

typedef struct NebulaModelTexture {
    uint32_t name_at, record_at;
    uint32_t width, height;
    uint8_t format, wrap_s, wrap_t, mipmap;
    uint8_t anisotropy, min_filter, mag_filter, reserved;
    uint32_t levels;
    float min_lod, max_lod, lod_bias;
} NebulaModelTexture;

#ifdef __cplusplus
}
static_assert(sizeof(NebulaModelCoord) == 8);
static_assert(sizeof(NebulaModelMeshCoord) == 4);
static_assert(sizeof(NebulaModelSlot) == 8);
static_assert(sizeof(NebulaModelStep) == 8);
static_assert(sizeof(NebulaModelDrawMatrix) == 8);
static_assert(sizeof(NebulaModelHeader) == 30 * 4);
static_assert(sizeof(NebulaModelStage) == 36);
static_assert(sizeof(NebulaModelHit) == 120);
static_assert(sizeof(NebulaModelJoint) == 96);
static_assert(sizeof(NebulaModelMesh) == 100);
static_assert(sizeof(NebulaModelForm) == 64);
static_assert(sizeof(NebulaModelMaterial) == 208);
static_assert(sizeof(NebulaModelTexture) == 40);
static_assert(sizeof(NebulaModelCoordMatrix) == 104);
static_assert(sizeof(NebulaModelWarp) == 24);
static_assert(sizeof(NebulaModelFog) == 24);
#endif
