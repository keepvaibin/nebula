// SPDX-License-Identifier: GPL-3.0-only
// WhirlPool (Game/MapObj/WhirlPool.cpp): the swirl texture at two scrolls multiplied, a highlight band where its red
// meets the band's level, over the screen behind warped by the indirect texture and tinted; alpha the vertex alpha,
// drawn where above zero. Textures: 0 the swirl, 1 the screen, 2 the indirect warp.
struct MaterialConstants {
    float4 band;           // the band's colour (rgb) and the red level it marks (a)
    float4 tint;           // the screen's tint (rgb)
    float4 screen_row[3];  // object position -> the screen texture's s, t, q
    float4 warp;           // xy: the warp per indirect step, in screen texture coordinates
};

#define NATIVE_COORDS 3
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = v.color0;
    o.uv[0] = float4(v.uv[0], v.uv[1]);
    o.uv[1] = float4(v.uv[2], 0.0, 0.0);
    const float4 p = float4(v.pos, 1.0);
    o.uv[2] = float4(dot(material.screen_row[0], p), dot(material.screen_row[1], p), dot(material.screen_row[2], p), 0.0);
}

float4 material_color(NativeVaryings i) {
    if (round(i.color.a * 255.0) <= 0.0) {
        discard;
    }
    float3 c = TEX(0).Sample(SAMP(0), i.uv[0].xy).rgb * TEX(0).Sample(SAMP(0), i.uv[0].zw).rgb * 0.5;
    c += material.band.rgb * level_line(c.r * 255.0, material.band.a * 255.0);
    const float2 step = round(TEX(2).Sample(SAMP(2), i.uv[1].xy).ab * 255.0) - 128.0;
    const float2 screen = (i.uv[2].z != 0.0 ? i.uv[2].xy / i.uv[2].z : i.uv[2].xy) + step * material.warp.xy;
    c = saturate(c + TEX(1).Sample(SAMP(1), screen).rgb * material.tint.rgb);
    return float4(c, i.color.a);
}
