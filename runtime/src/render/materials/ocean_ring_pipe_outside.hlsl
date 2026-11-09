// SPDX-License-Identifier: GPL-3.0-only
// OceanRingPipeOutside (Game/Map/OceanRingPipeOutside.cpp): the highlight texture (1) by the eye-space normal (the
// draw's texture matrix 1), its lookup moved by the indirect texture (0, the first scroll: texture matrix 0; its
// alpha and blue signed about 128), over a colour; light 2 (the actor's rim light, clamped diffuse) through the alpha
// channel thins the colour's alpha into a rim, added to the colour; opaque.
struct MaterialConstants {
    float4 color;    // rgb the base, a the rim's strength
    float4 warp;     // x: highlight texels per indirect step; zw: the highlight texture's size
    float4 channel;  // x: the alpha channel's material alpha, y: its ambient alpha (as the drawing state held them)
    float4 coord[6];  // texture matrices, three rows each
};

#define NATIVE_COORDS 1
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    const uint nq = v.pn_row;
    const float3 nt = float3(dot(native_normal_row(nq).xyz, v.nrm), dot(native_normal_row(nq + 1u).xyz, v.nrm),
                             dot(native_normal_row(nq + 2u).xyz, v.nrm));
    const float3 nrm = dot(nt, nt) > 0.0 ? normalize(nt) : nt;
    // Light 2: its alpha by the clamped facing toward its position.
    const float4 le2 = native_light_row(2u, 2u);
    const float4 le3 = native_light_row(2u, 3u);
    const float3 delta = float3(le2.z, le2.w, le3.x) - v.eye;
    const float3 ldir = dot(delta, delta) > 0.0 ? normalize(delta) : nrm;
    const float lit = native_light_row(2u, 0u).a * max(0.0, dot(ldir, nrm));
    o.color = float4(1.0, 1.0, 1.0,
                     material.channel.x * saturate((material.channel.y * 255.0 + lit) / 255.0));
    const float4 c0 = float4(v.uv[0], 1.0, 1.0);
    const float4 n = float4(v.nrm, 1.0);
    o.uv[0] = float4(dot(c0, material.coord[0]), dot(c0, material.coord[1]),
                     dot(n, material.coord[3]), dot(n, material.coord[4]));
}

float4 material_color(NativeVaryings i) {
    const float2 step = round(TEX(0).Sample(SAMP(0), i.uv[0].xy).ab * 255.0) - 128.0;
    const float2 texel = i.uv[0].zw * material.warp.zw + step * material.warp.x;
    const float3 c = saturate(material.color.rgb + TEX(1).Sample(SAMP(1), texel / material.warp.zw).rgb);
    const float a = saturate((material.color.a * (1.0 - i.color.a) - 0.5) * 2.0);
    return float4(saturate(c + a), 1.0);
}
