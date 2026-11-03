// SPDX-License-Identifier: GPL-3.0-only
// FurDrawer (Game/Util/FurDrawer.cpp): one fur shell. Channel 0 lit by its lights (clamped diffuse), channel 1 the
// specular lights; the body texture (0) by channel 0 plus the shell's brightness, mixed toward the shell colour by
// the fur texture (1, its lookup moved by the indirect texture 2 at the shell's scale) plus the specular, doubled; the
// alpha the shell's base plus the fur's alpha by the shell alpha; drawn above the reference; fog.
struct MaterialConstants {
    float4 mat0;     // channel 0's material and ambient colours (0-1)
    float4 amb0;
    float4 mat1;     // channel 1's (specular)
    float4 amb1;
    float4 kshell;   // r: the shell's brightness, g: its alpha, b: its base alpha
    float4 color;    // rgb: the shell colour
    float4 params;   // x: channel 0's light mask, y: channel 1's, z: sources (bit 0 channel 0's ambient from the vertex,
                     // 1 its material, 2 and 3 channel 1's), w: 1 specular, 2 indirect
    float4 params2;  // x: the indirect scale, y: the alpha reference (0-255), zw: texture 1's size (texels)
    float4 fog;      // start, end, near, far
    float4 fog_color;  // rgb; w: the fog type
};

#include "native_material.hlsli"

float2 fur_coord(NativeVertex v, uint i) {
    const float4 c = float4(v.uv[0], 1.0, 1.0);
    return float2(dot(c, inline_tex_matrices[3u * i]), dot(c, inline_tex_matrices[3u * i + 1u]));
}

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    const uint nq = v.pn_row & 31u;
    const float3 nt = float3(dot(native_normal_row(nq).xyz, v.nrm), dot(native_normal_row(nq + 1u).xyz, v.nrm),
                             dot(native_normal_row(nq + 2u).xyz, v.nrm));
    const float3 nrm = dot(nt, nt) > 0.0 ? normalize(nt) : nt;
    const uint mask0 = uint(material.params.x);
    const uint mask1 = uint(material.params.y);
    const uint src = uint(material.params.z);
    float3 lit0 = float3(0.0, 0.0, 0.0);
    float3 lit1 = float3(0.0, 0.0, 0.0);
    for (uint n = 0u; n < 8u; ++n) {
        if (((mask0 >> n) & 1u) != 0u) {
            lit0 += j3d_light(n, 2u, 2u, v.eye, nrm).rgb;
        }
        if (((mask1 >> n) & 1u) != 0u) {
            lit1 += j3d_light(n, 1u, 0u, v.eye, nrm).rgb;
        }
    }
    const float3 amb0 = (src & 1u) != 0u ? v.color0.rgb : material.amb0.rgb;
    const float3 mat0 = (src & 2u) != 0u ? v.color0.rgb : material.mat0.rgb;
    const float3 amb1 = (src & 4u) != 0u ? v.color0.rgb : material.amb1.rgb;
    const float3 mat1 = (src & 8u) != 0u ? v.color0.rgb : material.mat1.rgb;
    o.color = float4(mat0 * saturate((amb0 * 255.0 + lit0) / 255.0), material.mat0.a);
    o.color1 = float4(mat1 * saturate((amb1 * 255.0 + lit1) / 255.0), material.mat1.a);
    o.uv[0] = float4(fur_coord(v, 0u), fur_coord(v, 1u));
    o.uv[1] = float4(fur_coord(v, 2u), 0.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    const uint flags = uint(material.params.w);
    float2 coord1 = i.uv[0].zw;
    if ((flags & 2u) != 0u) {
        // The offset lookup: the indirect texture's alpha and blue less 128, by the shell's scale, in texels.
        const float3 ind = round(tex[2].Sample(samp[2], i.uv[1].xy).abg * 255.0) - 128.0;
        coord1 = (coord1 * material.params2.zw + material.params2.x * ind.xy) / material.params2.zw;
    }
    const float4 t0 = tex[0].Sample(samp[0], i.uv[0].xy);
    const float4 t1 = tex[1].Sample(samp[1], coord1);
    const float3 c0 = saturate(t0.rgb * i.color.rgb + material.kshell.r);
    const float3 spec = (flags & 1u) != 0u ? i.color1.rgb : float3(0.0, 0.0, 0.0);
    float3 c = saturate((lerp(material.color.rgb, c0, t1.rgb) + spec) * 2.0);
    const float a = saturate(material.kshell.b + t1.a * material.kshell.g);
    if (round(a * 255.0) <= material.params2.y) {
        discard;
    }
    c = native_fog(c, i.pos, material.fog, material.fog_color);
    return float4(c, a);
}
