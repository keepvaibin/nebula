// SPDX-License-Identifier: GPL-3.0-only
// The J3D materials' framework (E-04 step 3): every generated runtime/src/render/materials/j3d/m<id>.hlsl includes this.
// One constant layout for all of them (the game packs it from the material's values: J3DNativeMaterial), the native
// framework, and the vertex lighting from the draw's lights (float math).

struct MaterialConstants {
    float4 reg[4];        // the colour registers PREV, REG0-2 at the draw (0-1 scale; signed, beyond 1 possible)
    float4 kcolor[4];     // the constant colours 0-3
    float4 mat_color[2];  // channels 0 and 1: material colour
    float4 amb_color[2];  // and ambient colour
    float4 ind_mtx[6];    // indirect matrix m: rows 2m and 2m + 1 (xyz), its scale applied
    float4 fog;           // start, end, near, far
    float4 fog_color;     // rgb; w: the fog type (0 none)
    float4 alpha_ref;     // the alpha test's references 0 and 1 (0-1)
    float4 tex_scale[8];  // xy: texture coordinate i's scale (texels per unit), zw: map i's size (texels)
    float4 ovr;           // a pass that replaces the material's colour (j3d_override): x the mode (0 none), y and z
                          // alphas A0 and A1 (0-1), w the alpha reference (drawn above it; -1 always)
    float4 ovr_coord;     // x: coordinate 0 replaced (j3d_vertex_override; 0 none, 1 the vertex's normal)
};

#include "native_material.hlsli"

// A stage takes a register as an operand a, b or c through its low 8 bits: a value an
// unclamped stage left past 1 or below 0 wraps.
float j3d_in(float x) { return x - floor(x * 255.0 / 256.0) * 256.0 / 255.0; }
float3 j3d_in(float3 x) { return x - floor(x * 255.0 / 256.0) * 256.0 / 255.0; }
// The final result the same way (materials whose last stage does not clamp).
float4 j3d_out(float4 x) { return x - floor(x * 255.0 / 256.0) * 256.0 / 255.0; }

// fog over the colour (native_material.hlsli native_fog) with the material's fog values.
float3 j3d_fog(float3 c, float4 pos) {
    return native_fog(c, pos, material.fog, material.fog_color);
}

// A texture coordinate's projection: s and t over q where its generation made three.
float2 j3d_coord(float4 uv) {
    return uv.z != 0.0 && uv.w != 0.0 ? uv.xy / uv.z : uv.xy;
}

// Coordinate `coord` in texels of its scale (the size of the map its last stage reads).
float2 j3d_texel(float4 uv, uint coord) {
    return j3d_coord(uv) * material.tex_scale[coord].xy;
}

// Coordinate 0 replaced over the material's own stages (J3DNativeMaterial::overrideCoordinate): 1 the vertex's normal
// (the normal through the identity, 2x4, normalized: J3DModelX list 6, the ghost's shell over the screen's copy).
void j3d_vertex_override(NativeVertex v, inout NativeVaryings o) {
    if (material.ovr_coord.x == 1.0) {
        const float3 n = dot(v.nrm, v.nrm) > 0.0 ? normalize(v.nrm) : v.nrm;
        o.uv[0] = float4(n.xy, 1.0, 0.0);
    }
}

// A pass over the material's own geometry, channels and textures that replaces its stages (J3DNativeMaterial::
// override; the game's one-stage programs the game loads over a material): 1 texture 0 at coordinate 0 with alpha 1/4
// (J3DModelX list 2), 2 the channel colour with alpha A0 (MarioSearchLight's volume), 3 the channel colour with alpha
// RASA - K(1 - A0) (its cone: stage 0's constant alpha), 4 the channel colour with alpha (A1 + (A0 - A1) RASA) / 2 (its
// falloff), 5 texture 0 at coordinate 0 halved with stage 0's own alpha (`a0`: the material's stage 0 alpha combiner
// without channels; J3DModelX list 11, the reflection model). Fog as the material's.
float4 j3d_override(float4 t0, float4 ras, float k0, float a0, float4 pos) {
    const uint mode = uint(material.ovr.x);
    float4 c;
    if (mode == 1u) {
        c = float4(t0.rgb, 0.25);
    } else if (mode == 2u) {
        c = float4(ras.rgb, material.ovr.y);
    } else if (mode == 3u) {
        c = float4(ras.rgb, saturate(ras.a - k0 * (1.0 - material.ovr.y)));
    } else if (mode == 4u) {
        c = float4(ras.rgb, saturate(lerp(material.ovr.z, material.ovr.y, ras.a) * 0.5));
    } else {
        c = float4(saturate(t0.rgb * 0.5), a0);
    }
    if (round(c.a * 255.0) <= material.ovr.w) {
        discard;
    }
    c.rgb = j3d_fog(c.rgb, pos);
    return c;
}
