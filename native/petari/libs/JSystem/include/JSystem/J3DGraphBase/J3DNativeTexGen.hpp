#pragma once

#include <NebulaGpu.h>

// A texture coordinate's generation as the renderer takes it (nebula_game_api.h NebulaMaterialProgram::tex_gen,
// NebulaShapeTexGen): the file's type and source as two bytes, its post-transform as a row.
// tools/dev/gen_j3d_materials.py native_tex_gen converts dumped records the same way.
namespace J3DNativeTexGen {
    // Bits 0-1 the kind (0 a matrix, 1 emboss, 2 colour 0, 3 colour 1), bit 2 projective (three outputs), bit 3 an
    // implicit w of 1 (position, normal, binormals), bits 4-6 the emboss light.
    inline u8 kind(u32 type, u32 source) {
        u32 kind = 0, light = 0;
        if (type >= J3D_TG_BUMP0 && type <= J3D_TG_BUMP7) {
            kind = 1;
            light = type - J3D_TG_BUMP0;
        } else if (type == J3D_TG_SRTG) {
            kind = source == J3D_TG_COLOR0 ? 2 : 3;
        }
        const u32 projective = type == J3D_TG_MTX3x4 ? 1 : 0;
        const u32 form = source <= J3D_TG_TANGENT ? 1 : 0;
        return static_cast< u8 >(kind | projective << 2 | form << 3 | light << 4);
    }

    // Bits 0-3 the source row (0 position, 1 normal, 2 colour, 3 binormal, 4 tangent, 5 + n texture coordinate n),
    // bits 4-6 the emboss source coordinate.
    inline u8 source(u32 source) {
        u32 row = 5, emboss = 5;
        if (source <= J3D_TG_TANGENT) {
            static const u8 kRow[4] = {0, 1, 3, 4};
            row = kRow[source];
        } else if (source >= J3D_TG_TEX0 && source <= J3D_TG_TEX7) {
            row = 5 + (source - J3D_TG_TEX0);
        } else if (source >= J3D_TG_TEXCOORD0 && source <= J3D_TG_TEXCOORD6) {
            emboss = source - J3D_TG_TEXCOORD0;
        } else if (source == J3D_TG_COLOR0 || source == J3D_TG_COLOR1) {
            row = 2;
        }
        return static_cast< u8 >(row | (emboss & 7) << 4);
    }

    // The post-transform matrix as a row (J3D_PTTEXMTX0 + 3k as 3k, J3D_PTIDENTITY as 61).
    inline u8 post(u32 postMatrix) {
        return static_cast< u8 >(postMatrix >= J3D_PTTEXMTX0 ? postMatrix - J3D_PTTEXMTX0 : postMatrix);
    }
}  // namespace J3DNativeTexGen
