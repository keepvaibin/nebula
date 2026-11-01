// SPDX-License-Identifier: GPL-3.0-only
// The J3D materials' framework (E-04 step 3): every generated runtime/src/render/materials/j3d/m<id>.hlsl includes this.
// One constant layout for all of them (the game packs it from the material's values: J3DNativeMaterial), the native
// framework, and the vertex lighting from the draw's lights (float math).

struct MaterialConstants {
    float4 reg[4];        // the TEV registers PREV, REG0-2 at the draw (0-1 scale; signed, beyond 1 possible)
    float4 konst[4];      // konst colours 0-3
    float4 mat_color[2];  // channels 0 and 1: material colour
    float4 amb_color[2];  // and ambient colour
    float4 ind_mtx[6];    // indirect matrix m: rows 2m and 2m + 1 (xyz), its scale applied
    float4 fog;           // start, end, near, far
    float4 fog_color;     // rgb; w: the GX fog type (0 none)
    float4 alpha_ref;     // the alpha test's references 0 and 1 (0-1)
    float4 tex_scale[8];  // xy: texture coordinate i's GX scale (texels per unit), zw: map i's size (texels)
};

#include "native_material.hlsli"

// GX's TEV takes a register as an operand a, b or c through its low 8 bits (Dolphin: tevin & 255): a value an
// unclamped stage left past 1 or below 0 wraps.
float j3d_in(float x) { const float v = round(x * 255.0); return (v - 256.0 * floor(v / 256.0)) / 255.0; }
float3 j3d_in(float3 x) { const float3 v = round(x * 255.0); return (v - 256.0 * floor(v / 256.0)) / 255.0; }
// The final result as the pixel engine takes it: its low 8 bits (the uber shader's tev_wrap).
float4 j3d_out(float4 x) { const float4 v = round(x * 255.0); return (v - 256.0 * floor(v / 256.0)) / 255.0; }

// GX fog over the colour: its type (fog_color.w: GX_FOG_* - perspective 2-7 by the eye depth, orthographic 10-15
// by the depth between near and far), the range start-end, its curve.
float3 j3d_fog(float3 c, float4 pos) {
    const uint type = uint(material.fog_color.w);
    if (type == 0u) {
        return c;
    }
    const float ze = (type & 8u) != 0u ? lerp(material.fog.z, material.fog.w, pos.z) : pos.w;
    float f = saturate((ze - material.fog.x) / max(material.fog.y - material.fog.x, 1.0e-6));
    switch (type & 7u) {
    case 4u: f = 1.0 - exp2(-8.0 * f); break;
    case 5u: f = 1.0 - exp2(-8.0 * f * f); break;
    case 6u: f = exp2(-8.0 * (1.0 - f)); break;
    case 7u: f = 1.0 - f; f = exp2(-8.0 * f * f); break;
    default: break;
    }
    return lerp(c, material.fog_color.rgb, f);
}

// A texture coordinate's projection: s and t over q where its generation made three.
float2 j3d_coord(float4 uv) {
    return uv.z != 0.0 && uv.w != 0.0 ? uv.xy / uv.z : uv.xy;
}

// Coordinate `coord` in texels of its GX scale (the size of the map its last stage reads, as GX scales it).
float2 j3d_texel(float4 uv, uint coord) {
    return j3d_coord(uv) * material.tex_scale[coord].xy;
}
