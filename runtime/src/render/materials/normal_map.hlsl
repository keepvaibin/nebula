// SPDX-License-Identifier: GPL-3.0-only
// NormalMapBase (Game/MapObj/NormalMapBase.cpp drawSetting, setStageForObject, drawMaterialSetting): a normal-mapped
// shape lit through a light gradient. The normal map (texture 0, coordinate 0) offsets coordinate 1 (the gradient's
// centre) by its normal through the indirect matrix the light directions make (8-bit, less 128 in s, t and u, in the
// gradient's texels); the gradient (texture 1) there gives light A's colour and its alpha; channel 0 (clamped diffuse,
// the colour mask's lights) and its alpha (signed diffuse, the alpha mask's lights) light it; the shape's texture
// (texture 2, coordinate 2) mixes C1 toward the lit colour, plus C0; the back-light mode picks the last stage.
// the game's stages, each clamped:
//   0: light A x raster (A: 0, 1 or the gradient; raster: channel 0 or 1 in mode 4); alpha the gradient's
//   1: C2 = stage 0 + light B x raster (B: 0, 1 or stage 0's alpha); alpha A0 + K (1 - (channel alpha or stage 0's))
//   2: C0 + lerp(C1, C2, texture 2) (modes 2 and 3: stage 1's alpha in place of C0; 4 and 7: C2; 5: texture 2)
//   3: modes 0-2 only, doubled: 0 stage 2 + its alpha, 1 stage 2 x its alpha, 2 C0 + C2 x stage 2
// Mode 6 is one stage: channel 0. K is the constant alpha the program leaves unselected (the state KASEL_1: 1). The
// alpha is not written (the draw's alpha update is off).
struct MaterialConstants {
    float4 mat0;     // channel 0's material colour (rgb) and its alpha channel's (a), 0-1
    float4 amb0;     // and its ambient colour
    float4 reg0;     // C0 (rgb), A0 (a)
    float4 reg1;     // C1
    float4 ind0;     // the indirect matrix's rows (xyz)
    float4 ind1;
    float4 params;   // x: the colour lights' mask, y: the alpha lights', z: sources (bit 0 the material colour from the
                     // vertex, bit 1 the ambient), w: the back-light mode
    float4 params2;  // x: light A (0 none, 1 one, 2 the gradient), y: light B (0 none, 1 one, 2 stage 0's alpha),
                     // z: 1 the raster is one (mode 4), w: 1 light B's alpha from stage 0
    float4 params3;  // x: 1 coordinate 0 through matrix 0 (the BTK), else the vertex's; y: K; zw: the gradient's size
    float4 coord[9];  // texture matrices, three rows each
};

#define NATIVE_COORDS 2
#include "native_material.hlsli"

float2 normal_map_coord(NativeVertex v, uint i) {
    const float4 c = float4(v.uv[0], 1.0, 1.0);
    return float2(dot(c, material.coord[3u * i]), dot(c, material.coord[3u * i + 1u]));
}

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    const uint nq = v.pn_row;
    const float3 nt = float3(dot(native_normal_row(nq).xyz, v.nrm), dot(native_normal_row(nq + 1u).xyz, v.nrm),
                             dot(native_normal_row(nq + 2u).xyz, v.nrm));
    const float3 nrm = dot(nt, nt) > 0.0 ? normalize(nt) : nt;
    const uint maskColor = uint(material.params.x);
    const uint maskAlpha = uint(material.params.y);
    const uint src = uint(material.params.z);
    float3 litColor = float3(0.0, 0.0, 0.0);
    float litAlpha = 0.0;
    for (uint n = 0u; n < min(native_light_count(), 24u); ++n) {
        if (((maskColor >> n) & 1u) != 0u) {
            litColor += channel_light(n, 2u, 2u, v.eye, nrm).rgb;
        }
        if (((maskAlpha >> n) & 1u) != 0u) {
            litAlpha += channel_light(n, 2u, 1u, v.eye, nrm).a;
        }
    }
    const float3 mat = (src & 1u) != 0u ? v.color0.rgb : material.mat0.rgb;
    const float3 amb = (src & 2u) != 0u ? v.color0.rgb : material.amb0.rgb;
    o.color = float4(mat * saturate((amb * 255.0 + litColor) / 255.0),
                     material.mat0.a * saturate((material.amb0.a * 255.0 + litAlpha) / 255.0));
    o.uv[0] = float4(material.params3.x != 0.0 ? normal_map_coord(v, 0u) : v.uv[0], normal_map_coord(v, 1u));
    o.uv[1] = float4(normal_map_coord(v, 2u), 0.0, 0.0);
}

float3 normal_map_select(float s, float3 x) {
    return s == 0.0 ? float3(0.0, 0.0, 0.0) : s == 1.0 ? float3(1.0, 1.0, 1.0) : x;
}

float4 material_color(NativeVaryings i) {
    const uint mode = uint(material.params.w);
    const float k = material.params3.y;
    if (mode == 6u) {
        return float4(i.color.rgb, k);
    }
    const float2 size = material.params3.zw;
    const float3 ind = round(TEX(0).Sample(SAMP(0), i.uv[0].xy).abg * 255.0) - 128.0;
    const float2 offset = float2(dot(material.ind0.xyz, ind), dot(material.ind1.xyz, ind));
    const float4 grad = TEX(1).Sample(SAMP(1), (i.uv[0].zw * size + offset) / size);
    const float3 rasc = material.params2.z != 0.0 ? float3(1.0, 1.0, 1.0) : i.color.rgb;

    // Stage 0.
    const float3 prev0 = saturate(normal_map_select(material.params2.x, grad.rgb) * rasc);
    const float alpha0 = grad.a;
    // Stage 1 (its colour to C2, its alpha to PREV).
    const float3 c2 = saturate(prev0 + normal_map_select(material.params2.y, float3(alpha0, alpha0, alpha0)) * rasc);
    const float lightB = material.params2.w != 0.0 ? alpha0 : i.color.a;
    const float alpha1 = saturate(material.reg0.a + k * (1.0 - lightB));
    // Stage 2.
    const float3 t2 = TEX(2).Sample(SAMP(2), i.uv[1].xy).rgb;
    const float3 mixed = lerp(material.reg1.rgb, c2, t2);
    if (mode == 4u || mode == 7u) {
        return float4(c2, k);
    }
    if (mode == 5u) {
        return float4(t2, k);
    }
    const float3 prev2 = saturate((mode == 2u || mode == 3u ? float3(alpha1, alpha1, alpha1) : material.reg0.rgb) + mixed);
    if (mode == 3u) {
        return float4(prev2, k);
    }
    // Stage 3, doubled.
    float3 c;
    if (mode == 1u) {
        c = alpha1 * prev2;
    } else if (mode == 2u) {
        c = material.reg0.rgb + c2 * prev2;
    } else {
        c = prev2 + alpha1;
    }
    return float4(saturate(c * 2.0), k);
}
