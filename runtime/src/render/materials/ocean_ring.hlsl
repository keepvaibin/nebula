// SPDX-License-Identifier: GPL-3.0-only
// OceanRingDrawer (Game/Map/OceanRingDrawer.cpp loadMaterial): the water texture (0) at two scrolls multiplied and
// halved; where its red equals the highlight level, the highlight added (unclamped); then the screen behind (texture
// 1, by the draw's projection: texture matrix 3) tinted and added, its lookup moved by the indirect texture (2, the
// third scroll; its alpha and blue signed about 128) unless warp.y is 0; alpha the vertex alpha, drawn where above zero.
struct MaterialConstants {
    float4 highlight;  // rgb added where the red equals a (the level)
    float4 tint;       // the screen's tint (rgb)
    float4 warp;       // x: screen texels per indirect step; y: 1 warped, 0 not; zw: the screen texture's size
    float4 coord[12];  // texture matrices, three rows each
};

#define NATIVE_COORDS 3
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = v.color0;
    const float4 c0 = float4(v.uv[0], 1.0, 1.0);
    const float4 c1 = float4(v.uv[1], 1.0, 1.0);
    const float4 c2 = float4(v.uv[2], 1.0, 1.0);
    const float4 p = float4(v.pos, 1.0);
    o.uv[0] = float4(dot(c0, material.coord[0]), dot(c0, material.coord[1]),
                     dot(c1, material.coord[3]), dot(c1, material.coord[4]));
    o.uv[1] = float4(dot(c2, material.coord[6]), dot(c2, material.coord[7]), 0.0, 0.0);
    o.uv[2] = float4(dot(p, material.coord[9]), dot(p, material.coord[10]), dot(p, material.coord[11]), 1.0);
}

float4 material_color(NativeVaryings i) {
    if (round(i.color.a * 255.0) <= 0.0) {
        discard;
    }
    float3 c8 = TEX(0).Sample(SAMP(0), i.uv[0].xy).rgb * TEX(0).Sample(SAMP(0), i.uv[0].zw).rgb * 127.5;
    const float3 pattern8 = c8;
    c8 += material.highlight.rgb * 255.0 * level_line(c8.r, material.highlight.a * 255.0);
    float2 screen = i.uv[2].z != 0.0 ? i.uv[2].xy / i.uv[2].z : i.uv[2].xy;
    float2 move = float2(0.0, 0.0);  // the wobble in screen texture coordinates
    if (material.warp.y != 0.0) {
        const float2 step = round(TEX(2).Sample(SAMP(2), i.uv[1].xy).ab * 255.0) - 128.0;
        move = step * material.warp.x / material.warp.zw;
        screen += move;
    }
    const float3 behind = TEX(1).Sample(SAMP(1), screen).rgb * material.tint.rgb;
    // Under the traced light the wobble moves what the tracer shows (program.hlsli traced_sea; d09).
    return traced_sea(pattern8 / 255.0, c8 / 255.0 - pattern8 / 255.0, behind, i.color.a, move * line_point_raster.xy, i.pos);
}
