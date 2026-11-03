// SPDX-License-Identifier: GPL-3.0-only
// OceanBowl (Game/Map/OceanBowl.cpp, Beach Bowl's sea): the water texture at two scrolls multiplied, a highlight band
// where its red meets the band's level, and above water the screen behind warped by the indirect texture and tinted;
// alpha the vertex alpha times the shore mask, drawn where above zero. Textures: 0 the water, 1 the screen, 2 the
// indirect warp, 3 the shore mask.
struct MaterialConstants {
    float4 band;           // the band's colour (rgb) and the red level it marks (a)
    float4 tint;           // the screen's tint (rgb)
    float4 scroll01;       // the water's two scrolls (xy, zw)
    float4 scroll2m;       // the warp's scroll (xy) and the mask's offset (zw)
    float4 screen_row[3];  // world position -> the screen texture's s, t, q
    float4 warp;           // xy: the warp per indirect step, in screen texture coordinates; z: 1 above water
};

#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = v.color0;
    o.uv[0] = float4(v.uv[0] + material.scroll01.xy, v.uv[1] + material.scroll01.zw);
    o.uv[1] = float4(v.uv[2] + material.scroll2m.xy, v.uv[3] + material.scroll2m.zw);
    const float4 p = float4(v.pos, 1.0);
    o.uv[2] = float4(dot(material.screen_row[0], p), dot(material.screen_row[1], p), dot(material.screen_row[2], p), 0.0);
}

float4 material_color(NativeVaryings i) {
    const float alpha = i.color.a * tex[3].Sample(samp[3], i.uv[1].zw).a;
    if (round(alpha * 255.0) <= 0.0) {
        discard;
    }
    float3 c = tex[0].Sample(samp[0], i.uv[0].xy).rgb * tex[0].Sample(samp[0], i.uv[0].zw).rgb * 0.5;
    c += material.band.rgb * level_line(c.r * 255.0, material.band.a * 255.0);
    if (material.warp.z > 0.5) {
        const float2 step = round(tex[2].Sample(samp[2], i.uv[1].xy).ab * 255.0) - 128.0;
        const float2 screen = i.uv[2].xy / i.uv[2].z + step * material.warp.xy;
        c += tex[1].Sample(samp[1], screen).rgb * material.tint.rgb;
    }
    return float4(saturate(c), alpha);
}
