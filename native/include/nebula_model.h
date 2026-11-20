/* SPDX-License-Identifier: GPL-3.0-only */
/* A model as the store keeps it: one flat block in host order, used in place. Every *_at is a byte offset from the
   block's start. crates/nebula-import/src/cook/model_write.rs writes this layout. */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "nebula_limits.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NEBULA_MODEL_MAGIC 0x4C444D4Eu /* "NMDL" */
#define NEBULA_MODEL_VERSION 13u /* 13: the arrays found by name in a section table (12 had them at fixed header words) */
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

/* The block's start. Its arrays are found by name in the section table at `sections_at` (FRAME-MODEL 2.7.2, 2.8): a
   reader finds each section it knows, reads the fields it knows of each entry (fields are only added at an entry's
   end, so a section's stride may be larger than the reader's) and skips one it does not know, unless that one is
   flagged required: then it refuses the file, naming the section. */
typedef struct NebulaModelHeader {
    uint32_t magic, version, size, flags;
    uint32_t section_count, sections_at;
    uint32_t reserved[22]; /* zero: the header keeps 12's 112 bytes, so every array lies where 12 put it */
} NebulaModelHeader;

typedef struct NebulaModelSection {
    uint32_t name; /* four characters, the first in the low byte: NEBULA_MODEL_SECTION('J', 'N', 'T', 'S') */
    uint32_t flags; /* NEBULA_MODEL_SECTION_REQUIRED */
    uint32_t at, size; /* bytes from the block's start */
    uint32_t count, stride; /* entries and bytes an entry; a byte section has stride 1 and count its size */
} NebulaModelSection;

#define NEBULA_MODEL_SECTION(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)
#define NEBULA_MODEL_SECTION_REQUIRED 1u

/* The section named `name`, or null. */
static inline const NebulaModelSection* nebula_model_section(const NebulaModelHeader* header, uint32_t name) {
    const NebulaModelSection* sections =
        (const NebulaModelSection*)((const unsigned char*)header + header->sections_at);
    for (uint32_t i = 0; i < header->section_count; i++) {
        if (sections[i].name == name) {
            return &sections[i];
        }
    }
    return 0;
}

/* What a reader of 13 uses, resolved once from the sections at load (nebula_model_view). */
typedef struct NebulaModelView {
    uint32_t joint_count, step_count, joint_mesh_count;
    uint32_t draw_matrix_count, full_weight_count, envelope_count, mix_count;
    uint32_t inverse_bind_count, important_count; /* inverse binds as written: none when the disc has none */
    uint32_t mesh_count, material_count, texture_count;
    uint32_t source_vertex_count; /* the shared positions the file had: a game that rewrites them writes this many */
    uint32_t joints_at, steps_at, joint_meshes_at;
    uint32_t draw_matrices_at, envelopes_at, mixes_at, inverse_binds_at, important_at;
    uint32_t meshes_at, materials_at, textures_at;
    uint32_t names_at, names_size;
    uint32_t geometry_at, geometry_size; /* every stream, for one copy to the GPU */
    uint32_t images_at, images_size;     /* the texture images (each texture's record_at lies inside) */
    /* A material's parts (FRAME-MODEL 2.9): materials_at is MATL's place, material_count its count. */
    uint32_t gx_at, gx_count;                         /* GXMT, NebulaModelGx */
    uint32_t forms_at, form_count;                    /* FORM, NebulaModelFormEntry */
    uint32_t slots_at, slot_count;                    /* SLOT, NebulaModelSlot */
    uint32_t coords_at, coord_count;                  /* GXTG, NebulaModelCoord */
    uint32_t coord_matrices_at, coord_matrix_count;   /* GXTM, NebulaModelCoordMatrix */
    uint32_t warps_at, warp_count;                    /* GXWP, NebulaModelWarp */
    uint32_t rows_at, row_count;                      /* FROW, u32 NEBULA_ROW_* */
    uint32_t maps_at, maps_size;                      /* FTEX, u8 material map numbers */
    uint32_t inputs_at, inputs_size;                  /* FINP, u8 NEBULA_INPUT_* */
    uint32_t samplers_at, sampler_count;              /* SAMP, NebulaModelSampler (FRAME-MODEL 2.10) */
    /* Each record section's stride as written: a reader steps by it, so a field added at a record's end is read past
       (FRAME-MODEL 2.7.2; nebula_model_entry). */
    uint32_t joint_stride, step_stride, draw_matrix_stride, envelope_stride, mix_stride, mesh_stride;
    uint32_t material_stride, gx_stride, form_stride, slot_stride, coord_stride, coord_matrix_stride, warp_stride;
    uint32_t texture_stride, sampler_stride;
} NebulaModelView;

/* nebula_model_view's answer: NEBULA_MODEL_FINE, or what stops the load and the section (or version) it names. */
enum {
    NEBULA_MODEL_FINE = 0,
    NEBULA_MODEL_NOT_A_MODEL,        /* no magic, or a header or table past the block's end */
    NEBULA_MODEL_OTHER_VERSION,      /* `what`: the version found */
    NEBULA_MODEL_MISSING_SECTION,    /* `what`: the section's name */
    NEBULA_MODEL_UNKNOWN_REQUIRED,   /* `what`: a required section this reader does not know */
    NEBULA_MODEL_BAD_SECTION,        /* `what`: a section past the block's end, or with entries shorter than this reader's */
    NEBULA_MODEL_BAD_RANGE,          /* `what`: the section a material's, a form's or a GXMT entry's range leaves */
};
typedef struct NebulaModelCheck {
    uint32_t fault, what;
} NebulaModelCheck;

static inline NebulaModelCheck nebula_model_ranges(const void* block, const NebulaModelView* view);

/* Entry `index` of a section at `at` whose entries are `stride` bytes apart. */
static inline const void* nebula_model_entry(const void* block, uint32_t at, uint32_t stride, uint32_t index) {
    return (const unsigned char*)block + at + (size_t)index * stride;
}

/* The sections of 13, in file order: the view's fields each fills, its least stride, the alignment of its place and
   stride, and whether the stride must be exactly that (a value list or bytes, not a record that can gain fields). */
typedef struct NebulaModelSectionUse {
    uint32_t name, at_field, count_field, size_field, stride_field;
    uint32_t stride, align, exact;
} NebulaModelSectionUse;
#define NEBULA_MODEL_VIEW_FIELD(field) ((uint32_t)(offsetof(NebulaModelView, field) / 4u))
#define NEBULA_MODEL_VIEW_NONE 0xFFFFFFFFu
static const NebulaModelSectionUse kNebulaModelSections[] = {
    {NEBULA_MODEL_SECTION('M', 'O', 'D', 'L'), NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_NONE, 8u, 4u, 0u},
    {NEBULA_MODEL_SECTION('J', 'N', 'T', 'S'), NEBULA_MODEL_VIEW_FIELD(joints_at), NEBULA_MODEL_VIEW_FIELD(joint_count), NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_FIELD(joint_stride), 96u, 4u, 0u},
    {NEBULA_MODEL_SECTION('S', 'T', 'E', 'P'), NEBULA_MODEL_VIEW_FIELD(steps_at), NEBULA_MODEL_VIEW_FIELD(step_count), NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_FIELD(step_stride), 8u, 4u, 0u},
    {NEBULA_MODEL_SECTION('J', 'M', 'S', 'H'), NEBULA_MODEL_VIEW_FIELD(joint_meshes_at), NEBULA_MODEL_VIEW_FIELD(joint_mesh_count), NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_NONE, 4u, 4u, 1u},
    {NEBULA_MODEL_SECTION('D', 'M', 'T', 'X'), NEBULA_MODEL_VIEW_FIELD(draw_matrices_at), NEBULA_MODEL_VIEW_FIELD(draw_matrix_count), NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_FIELD(draw_matrix_stride), 8u, 4u, 0u},
    {NEBULA_MODEL_SECTION('E', 'N', 'V', 'L'), NEBULA_MODEL_VIEW_FIELD(envelopes_at), NEBULA_MODEL_VIEW_FIELD(envelope_count), NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_FIELD(envelope_stride), 8u, 4u, 0u},
    {NEBULA_MODEL_SECTION('M', 'I', 'X', 'S'), NEBULA_MODEL_VIEW_FIELD(mixes_at), NEBULA_MODEL_VIEW_FIELD(mix_count), NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_FIELD(mix_stride), 8u, 4u, 0u},
    {NEBULA_MODEL_SECTION('I', 'B', 'N', 'D'), NEBULA_MODEL_VIEW_FIELD(inverse_binds_at), NEBULA_MODEL_VIEW_FIELD(inverse_bind_count), NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_NONE, 48u, 4u, 1u},
    {NEBULA_MODEL_SECTION('I', 'M', 'P', 'T'), NEBULA_MODEL_VIEW_FIELD(important_at), NEBULA_MODEL_VIEW_FIELD(important_count), NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_NONE, 4u, 4u, 1u},
    {NEBULA_MODEL_SECTION('M', 'E', 'S', 'H'), NEBULA_MODEL_VIEW_FIELD(meshes_at), NEBULA_MODEL_VIEW_FIELD(mesh_count), NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_FIELD(mesh_stride), 100u, 4u, 0u},
    {NEBULA_MODEL_SECTION('M', 'A', 'T', 'L'), NEBULA_MODEL_VIEW_FIELD(materials_at), NEBULA_MODEL_VIEW_FIELD(material_count), NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_FIELD(material_stride), 64u, 4u, 0u},
    {NEBULA_MODEL_SECTION('G', 'X', 'M', 'T'), NEBULA_MODEL_VIEW_FIELD(gx_at), NEBULA_MODEL_VIEW_FIELD(gx_count), NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_FIELD(gx_stride), 120u, 4u, 0u},
    {NEBULA_MODEL_SECTION('F', 'O', 'R', 'M'), NEBULA_MODEL_VIEW_FIELD(forms_at), NEBULA_MODEL_VIEW_FIELD(form_count), NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_FIELD(form_stride), 56u, 8u, 0u},
    {NEBULA_MODEL_SECTION('S', 'L', 'O', 'T'), NEBULA_MODEL_VIEW_FIELD(slots_at), NEBULA_MODEL_VIEW_FIELD(slot_count), NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_FIELD(slot_stride), 8u, 4u, 0u},
    {NEBULA_MODEL_SECTION('G', 'X', 'T', 'G'), NEBULA_MODEL_VIEW_FIELD(coords_at), NEBULA_MODEL_VIEW_FIELD(coord_count), NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_FIELD(coord_stride), 8u, 4u, 0u},
    {NEBULA_MODEL_SECTION('G', 'X', 'T', 'M'), NEBULA_MODEL_VIEW_FIELD(coord_matrices_at), NEBULA_MODEL_VIEW_FIELD(coord_matrix_count), NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_FIELD(coord_matrix_stride), 104u, 4u, 0u},
    {NEBULA_MODEL_SECTION('G', 'X', 'W', 'P'), NEBULA_MODEL_VIEW_FIELD(warps_at), NEBULA_MODEL_VIEW_FIELD(warp_count), NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_FIELD(warp_stride), 24u, 4u, 0u},
    {NEBULA_MODEL_SECTION('F', 'R', 'O', 'W'), NEBULA_MODEL_VIEW_FIELD(rows_at), NEBULA_MODEL_VIEW_FIELD(row_count), NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_NONE, 4u, 4u, 1u},
    {NEBULA_MODEL_SECTION('F', 'T', 'E', 'X'), NEBULA_MODEL_VIEW_FIELD(maps_at), NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_FIELD(maps_size), NEBULA_MODEL_VIEW_NONE, 1u, 1u, 1u},
    {NEBULA_MODEL_SECTION('F', 'I', 'N', 'P'), NEBULA_MODEL_VIEW_FIELD(inputs_at), NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_FIELD(inputs_size), NEBULA_MODEL_VIEW_NONE, 1u, 1u, 1u},
    {NEBULA_MODEL_SECTION('T', 'E', 'X', 'R'), NEBULA_MODEL_VIEW_FIELD(textures_at), NEBULA_MODEL_VIEW_FIELD(texture_count), NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_FIELD(texture_stride), 20u, 4u, 0u},
    {NEBULA_MODEL_SECTION('S', 'A', 'M', 'P'), NEBULA_MODEL_VIEW_FIELD(samplers_at), NEBULA_MODEL_VIEW_FIELD(sampler_count), NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_FIELD(sampler_stride), 20u, 4u, 0u},
    {NEBULA_MODEL_SECTION('N', 'A', 'M', 'E'), NEBULA_MODEL_VIEW_FIELD(names_at), NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_FIELD(names_size), NEBULA_MODEL_VIEW_NONE, 1u, 1u, 1u},
    {NEBULA_MODEL_SECTION('G', 'E', 'O', 'M'), NEBULA_MODEL_VIEW_FIELD(geometry_at), NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_FIELD(geometry_size), NEBULA_MODEL_VIEW_NONE, 1u, 1u, 1u},
    {NEBULA_MODEL_SECTION('T', 'I', 'M', 'G'), NEBULA_MODEL_VIEW_FIELD(images_at), NEBULA_MODEL_VIEW_NONE, NEBULA_MODEL_VIEW_FIELD(images_size), NEBULA_MODEL_VIEW_NONE, 1u, 1u, 1u},
};
#define NEBULA_MODEL_SECTION_COUNT (sizeof(kNebulaModelSections) / sizeof(kNebulaModelSections[0]))

/* A model block of `size` bytes resolved into `view`: every section of 13 found and inside the block, each entry at
   least as long as this reader reads, and no required section it does not know. On a fault `view` is partly filled
   and must not be used. */
static inline NebulaModelCheck nebula_model_view(const void* block, uint32_t size, NebulaModelView* view) {
    NebulaModelCheck check = {NEBULA_MODEL_FINE, 0u};
    const NebulaModelHeader* header = (const NebulaModelHeader*)block;
    if (size < sizeof(NebulaModelHeader) || header->magic != NEBULA_MODEL_MAGIC) {
        check.fault = NEBULA_MODEL_NOT_A_MODEL;
        return check;
    }
    if (header->version != NEBULA_MODEL_VERSION) {
        check.fault = NEBULA_MODEL_OTHER_VERSION;
        check.what = header->version;
        return check;
    }
    if (header->size > size || header->sections_at > header->size ||
        header->section_count > (header->size - header->sections_at) / sizeof(NebulaModelSection) ||
        header->sections_at % 4u != 0u) {
        check.fault = NEBULA_MODEL_NOT_A_MODEL;
        return check;
    }
    uint32_t* fields = (uint32_t*)view;
    for (uint32_t i = 0; i < sizeof(NebulaModelView) / 4u; i++) {
        fields[i] = 0u;
    }
    const NebulaModelSection* sections =
        (const NebulaModelSection*)((const unsigned char*)block + header->sections_at);
    for (uint32_t i = 0; i < header->section_count; i++) {
        uint32_t known = 0u;
        for (uint32_t k = 0; k < NEBULA_MODEL_SECTION_COUNT; k++) {
            if (kNebulaModelSections[k].name == sections[i].name) {
                known = 1u;
            }
        }
        if (!known && (sections[i].flags & NEBULA_MODEL_SECTION_REQUIRED) != 0u) {
            check.fault = NEBULA_MODEL_UNKNOWN_REQUIRED;
            check.what = sections[i].name;
            return check;
        }
    }
    for (uint32_t k = 0; k < NEBULA_MODEL_SECTION_COUNT; k++) {
        const NebulaModelSectionUse* use = &kNebulaModelSections[k];
        const NebulaModelSection* section = nebula_model_section(header, use->name);
        if (section == 0) {
            check.fault = NEBULA_MODEL_MISSING_SECTION;
            check.what = use->name;
            return check;
        }
        const uint64_t entries = (uint64_t)section->count * section->stride;
        if (section->at > header->size || section->size > header->size - section->at || section->stride < use->stride ||
            entries > section->size || (use->exact != 0u && section->stride != use->stride) ||
            section->at % use->align != 0u || section->stride % use->align != 0u) {
            check.fault = NEBULA_MODEL_BAD_SECTION;
            check.what = use->name;
            return check;
        }
        if (use->at_field != NEBULA_MODEL_VIEW_NONE) {
            fields[use->at_field] = section->at;
        }
        if (use->count_field != NEBULA_MODEL_VIEW_NONE) {
            fields[use->count_field] = section->count;
        }
        if (use->size_field != NEBULA_MODEL_VIEW_NONE) {
            fields[use->size_field] = section->size;
        }
        if (use->stride_field != NEBULA_MODEL_VIEW_NONE) {
            fields[use->stride_field] = section->stride;
        }
        if (use->name == NEBULA_MODEL_SECTION('M', 'O', 'D', 'L')) {
            if (section->count < 1u) {
                check.fault = NEBULA_MODEL_BAD_SECTION;
                check.what = use->name;
                return check;
            }
            const uint32_t* scalars = (const uint32_t*)((const unsigned char*)block + section->at);
            view->full_weight_count = scalars[0];
            view->source_vertex_count = scalars[1];
        }
    }
    /* Envelopes blend joints by their inverse binds: one a joint (12's readers assumed it and read past the array). */
    if (view->envelope_count != 0u && view->inverse_bind_count < view->joint_count) {
        check.fault = NEBULA_MODEL_BAD_SECTION;
        check.what = NEBULA_MODEL_SECTION('I', 'B', 'N', 'D');
        return check;
    }
    return nebula_model_ranges(block, view);
}

/* A section's name as text ("JNTS"), for a message. */
static inline void nebula_model_section_text(uint32_t name, char out[5]) {
    for (int i = 0; i < 4; i++) {
        const char c = (char)(name >> (8 * i));
        out[i] = c >= 0x20 && c < 0x7F ? c : '?';
    }
    out[4] = 0;
}


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
    /* 1u << 4: unused from store 36 (the tracer's de-light gain) */
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
   coordinates, 2 floats each in ascending order, then colour 1, then tangent and binormal. */
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
    uint32_t reserved2; /* 0 */
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
    /* 7: unused from store 36 (the tracer's de-light gain) */
    NEBULA_INPUT_COORD0 = 16, /* + the mesh's coordinate number */
};

/* NebulaModelFormEntry.flags */
enum {
    NEBULA_FORM_NORMAL_COORD = 1u << 0, /* coordinate 0 is the normal through the identity */
    NEBULA_FORM_LIT = 1u << 1,          /* lit at the pixel */
    NEBULA_FORM_ALPHA_TEST = 1u << 2,
    NEBULA_FORM_FOG_LINEAR = 1u << 3,   /* linear fog over a material cooked without any */
};

/* NebulaModelFormEntry.pass: an edit of the material the programs were written from. */
enum {
    NEBULA_FORM_PASS_PLAYER_LIGHT = 1,
    NEBULA_FORM_PASS_REFLECTION_ALPHA = 2,
    NEBULA_FORM_PASS_PLAYER_OPACITY = 16,
    NEBULA_FORM_PASS_FLEXIBLE_COORDINATES = 32,
};

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

/* NebulaModelGx.flags */
enum {
    NEBULA_MATERIAL_HAS_AMBIENT = 1u << 0,
    NEBULA_MATERIAL_HAS_STAGE_COLORS = 1u << 1,
    NEBULA_MATERIAL_HAS_INDIRECT = 1u << 2,
    NEBULA_MATERIAL_HAS_FOG = 1u << 3,
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



/* MATL: a material apart from the console's fields (FRAME-MODEL 2.9). Its parts by index and count in their
   sections; names are offsets of the block. */
enum {
    NEBULA_MATL_TRANSLUCENT = 1u << 0,  /* drawn in the translucent buffer (decided at the cook) */
    NEBULA_MATL_NORMAL_SCALE = 1u << 1, /* its normals are scaled by normal_scale */
    NEBULA_MATL_ON_BILLBOARD = 1u << 2, /* its joint turns to the eye; eye-space coordinates read world rows */
};
typedef struct NebulaModelMatl {
    uint32_t name_at, name_hash;
    uint32_t same_as; /* materials of a model with one value are the same in everything */
    uint32_t mesh, joint;
    uint32_t flags;          /* NEBULA_MATL_* */
    uint32_t state;          /* its pipeline state, NEBULA_STATE_* */
    uint8_t light_masks[4];  /* each light channel's lights; 0 for a channel that is off */
    uint32_t gx;             /* its GXMT entry: the console's material, read until step 3's last landing */
    uint32_t first_slot, slot_count; /* in SLOT */
    uint32_t first_form, form_count; /* in FORM */
    float normal_scale[3];
} NebulaModelMatl;

/* FORM: one program pair of a material, its rows, map numbers and inputs by range. */
typedef struct NebulaModelFormEntry {
    uint64_t vertex, pixel; /* the programs' hashes */
    uint32_t material;
    uint32_t first_vertex_row, vertex_row_count, first_pixel_row, pixel_row_count; /* in FROW */
    uint32_t first_texture, texture_count; /* in FTEX: the material's map numbers */
    uint32_t first_input, input_count;     /* in FINP: NEBULA_INPUT_* */
    uint8_t replace, flags; /* replace: 0 the material's stages, else the replacing pass */
    uint8_t pass, reserved;
} NebulaModelFormEntry;

/* GXMT: the console's material fields, apart (flags NEBULA_MATERIAL_HAS_AMBIENT, _HAS_STAGE_COLORS, _HAS_INDIRECT,
   _HAS_FOG). Goes with step 3's last landing. */
typedef struct NebulaModelGx {
    uint32_t flags;
    uint8_t material_color[2][4], ambient_color[2][4];
    int16_t registers[4][4];
    uint8_t constants[4][4];
    uint8_t alpha_ref0, alpha_ref1, reserved[2];
    NebulaModelFog fog;
    uint32_t first_coord, coord_count;   /* in GXTG */
    uint32_t first_matrix, matrix_count; /* in GXTM */
    uint32_t first_warp, warp_count;     /* in GXWP */
} NebulaModelGx;

/* TEXR: a texture; its image is the record at record_at (TIMG). */
typedef struct NebulaModelTexture {
    uint32_t name_at, record_at;
    uint32_t width, height;
    uint32_t sampler; /* its SAMP entry */
} NebulaModelTexture;

/* SAMP: a sampler as the renderer takes it (NebulaFrameTexture's sampler fields), decoded at the cook
   (FRAME-MODEL 2.10). */
typedef struct NebulaModelSampler {
    uint8_t wrap_s, wrap_t; /* 0 clamp, 1 repeat, 2 mirror */
    uint8_t min_linear, mag_linear;
    uint8_t mip_filter;     /* 0 the first level only, 1 the nearest level, 2 between levels */
    uint8_t anisotropy, reserved[2];
    float min_lod, max_lod, lod_bias;
} NebulaModelSampler;

/* A sampler from the console's codes, the rule the cook applies to a model's textures (model_write.rs
   sampler_bytes). The game's own uses, a disc picture swapped into a model and the draw sites, go with E3 step 4
   (P-155), when those pictures' samplers are decoded at their cook. */
static inline NebulaModelSampler nebula_sampler_from_codes(uint8_t wrap_s, uint8_t wrap_t, uint8_t min_code, uint8_t mag_code,
                                                           uint8_t anisotropy, float min_lod, float max_lod, float lod_bias) {
    NebulaModelSampler s;
    s.wrap_s = wrap_s;
    s.wrap_t = wrap_t;
    s.min_linear = (uint8_t)(min_code == 1u || min_code == 3u || min_code == 5u);
    s.mag_linear = (uint8_t)(mag_code != 0u);
    s.mip_filter = (uint8_t)(min_code < 2u ? 0u : min_code < 4u ? 1u : 2u);
    s.anisotropy = anisotropy;
    s.reserved[0] = 0u;
    s.reserved[1] = 0u;
    s.min_lod = min_lod;
    s.max_lod = max_lod;
    s.lod_bias = lod_bias;
    return s;
}

/* Every material's, form's and GXMT entry's parts lie inside their sections (FRAME-MODEL 2.9), and every texture's
   sampler in SAMP (2.10), so a reader indexes them without a check. A fault names the section a range leaves. */
static inline int nebula_model_range_out(uint32_t first, uint32_t count, uint32_t total) {
    return (uint64_t)first + count > total;
}
static inline NebulaModelCheck nebula_model_ranges(const void* block, const NebulaModelView* view) {
    NebulaModelCheck check = {NEBULA_MODEL_FINE, 0u};
    const unsigned char* base = (const unsigned char*)block;
    for (uint32_t i = 0; i < view->material_count && check.fault == NEBULA_MODEL_FINE; i++) {
        const NebulaModelMatl* matl = (const NebulaModelMatl*)nebula_model_entry(base, view->materials_at, view->material_stride, i);
        check.fault = NEBULA_MODEL_BAD_RANGE;
        if (matl->gx >= view->gx_count) {
            check.what = NEBULA_MODEL_SECTION('G', 'X', 'M', 'T');
        } else if (nebula_model_range_out(matl->first_slot, matl->slot_count, view->slot_count)) {
            check.what = NEBULA_MODEL_SECTION('S', 'L', 'O', 'T');
        } else if (nebula_model_range_out(matl->first_form, matl->form_count, view->form_count)) {
            check.what = NEBULA_MODEL_SECTION('F', 'O', 'R', 'M');
        } else {
            check.fault = NEBULA_MODEL_FINE;
        }
    }
    for (uint32_t i = 0; i < view->gx_count && check.fault == NEBULA_MODEL_FINE; i++) {
        const NebulaModelGx* gx = (const NebulaModelGx*)nebula_model_entry(base, view->gx_at, view->gx_stride, i);
        check.fault = NEBULA_MODEL_BAD_RANGE;
        if (nebula_model_range_out(gx->first_coord, gx->coord_count, view->coord_count)) {
            check.what = NEBULA_MODEL_SECTION('G', 'X', 'T', 'G');
        } else if (nebula_model_range_out(gx->first_matrix, gx->matrix_count, view->coord_matrix_count)) {
            check.what = NEBULA_MODEL_SECTION('G', 'X', 'T', 'M');
        } else if (nebula_model_range_out(gx->first_warp, gx->warp_count, view->warp_count)) {
            check.what = NEBULA_MODEL_SECTION('G', 'X', 'W', 'P');
        } else {
            check.fault = NEBULA_MODEL_FINE;
        }
    }
    for (uint32_t i = 0; i < view->form_count && check.fault == NEBULA_MODEL_FINE; i++) {
        const NebulaModelFormEntry* form = (const NebulaModelFormEntry*)nebula_model_entry(base, view->forms_at, view->form_stride, i);
        check.fault = NEBULA_MODEL_BAD_RANGE;
        if (form->material >= view->material_count) {
            check.what = NEBULA_MODEL_SECTION('M', 'A', 'T', 'L');
        } else if (nebula_model_range_out(form->first_vertex_row, form->vertex_row_count, view->row_count) ||
                   nebula_model_range_out(form->first_pixel_row, form->pixel_row_count, view->row_count)) {
            check.what = NEBULA_MODEL_SECTION('F', 'R', 'O', 'W');
        } else if (nebula_model_range_out(form->first_texture, form->texture_count, view->maps_size)) {
            check.what = NEBULA_MODEL_SECTION('F', 'T', 'E', 'X');
        } else if (nebula_model_range_out(form->first_input, form->input_count, view->inputs_size)) {
            check.what = NEBULA_MODEL_SECTION('F', 'I', 'N', 'P');
        } else {
            check.fault = NEBULA_MODEL_FINE;
        }
    }
    for (uint32_t i = 0; i < view->texture_count && check.fault == NEBULA_MODEL_FINE; i++) {
        const NebulaModelTexture* texture =
            (const NebulaModelTexture*)nebula_model_entry(base, view->textures_at, view->texture_stride, i);
        if (texture->sampler >= view->sampler_count) {
            check.fault = NEBULA_MODEL_BAD_RANGE;
            check.what = NEBULA_MODEL_SECTION('S', 'A', 'M', 'P');
        }
    }
    return check;
}

#ifdef __cplusplus
}
static_assert(sizeof(NebulaModelCoord) == 8);
static_assert(sizeof(NebulaModelMeshCoord) == 4);
static_assert(sizeof(NebulaModelSlot) == 8);
static_assert(sizeof(NebulaModelStep) == 8);
static_assert(sizeof(NebulaModelDrawMatrix) == 8);
static_assert(sizeof(NebulaModelHeader) == 28 * 4);
static_assert(sizeof(NebulaModelJoint) == 96);
static_assert(sizeof(NebulaModelMesh) == 100);
static_assert(sizeof(NebulaModelMatl) == 64);
static_assert(sizeof(NebulaModelFormEntry) == 56);
static_assert(sizeof(NebulaModelGx) == 120);
static_assert(sizeof(NebulaModelTexture) == 20);
static_assert(sizeof(NebulaModelSampler) == 20);
static_assert(sizeof(NebulaModelCoordMatrix) == 104);
static_assert(sizeof(NebulaModelWarp) == 24);
static_assert(sizeof(NebulaModelFog) == 24);
#endif
