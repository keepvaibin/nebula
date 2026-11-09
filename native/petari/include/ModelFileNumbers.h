#pragma once

// The numbers the cooked layout material records and the Mii shape table store for culling, light terms, colour
// sources, texture-coordinate generation, matrices and blending. Code reading those records names them here;
// everything else speaks the host's values (nebula_game_api.h).
enum {
    // Face culling.
    MODEL_FILE_CULL_NONE = 0,
    MODEL_FILE_CULL_FRONT = 1,
    MODEL_FILE_CULL_BACK = 2,

    // Light attenuation: specular lobe, spot cone, none.
    MODEL_FILE_ATTEN_SPECULAR = 0,
    MODEL_FILE_ATTEN_SPOT = 1,
    MODEL_FILE_ATTEN_NONE = 2,

    // Diffuse term: clamped N.L.
    MODEL_FILE_DIFFUSE_CLAMP = 2,

    // Light mask bits.
    MODEL_FILE_LIGHT0 = 1,
    MODEL_FILE_LIGHT1 = 2,

    // Where a lighting channel takes its colour from: the material register or the vertex.
    MODEL_FILE_SOURCE_REGISTER = 0,
    MODEL_FILE_SOURCE_VERTEX = 1,

    // Texture-coordinate generation: a 2x4 matrix applied to a texture coordinate source.
    MODEL_FILE_TEXGEN_MATRIX_2x4 = 1,
    MODEL_FILE_TEXGEN_SOURCE_TEX0 = 4,

    // Texture matrix slots: ten 3-row matrices from the first, then the identity.
    MODEL_FILE_MATRIX_FIRST = 30,
    MODEL_FILE_MATRIX_IDENTITY = 60,

    // The alpha compare combine: both compares must pass.
    MODEL_FILE_ALPHA_OP_AND = 0,

    // Blend mode: add (blend), subtract.
    MODEL_FILE_BLEND_ADD = 1,
    MODEL_FILE_BLEND_SUBTRACT = 3,

    // Blend factors.
    MODEL_FILE_FACTOR_DST_COLOR = 2,
    MODEL_FILE_FACTOR_INV_DST_COLOR = 3,
    MODEL_FILE_FACTOR_SRC_ALPHA = 4,
    MODEL_FILE_FACTOR_INV_SRC_ALPHA = 5,
};
