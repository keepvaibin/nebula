// SPDX-License-Identifier: GPL-3.0-only
// WaterRoad (Game/Map/WaterRoad.cpp): the tube's water (texture 0) at two scrolls multiplied and halved; where its red
// equals the highlight level, the highlight added (high detail only); the screen behind (texture 2, by the projection
// matrix) tinted and added, its lookup moved by the indirect texture (1, the third scroll) at high detail; the
// environment map (3, by the eye-space normal) added; alpha the road's (or 1); drawn where above zero.
// The draw's texture matrices: high detail 0-2 the scrolls, 3 the projection, 4 the environment; low detail 0-1 the
// scrolls, 2 the projection, 3 the environment.
struct MaterialConstants {
    float4 highlight;  // rgb added where the red equals a (the level)
    float4 tint;       // the screen's tint (rgb)
    float4 params;     // x: 1 high detail (highlight, warp), 0 low; y: screen texels per indirect step; z: alpha
    float4 screen;     // xy: the screen texture's size
    float4 coord[15];  // texture matrices, three rows each
};

#define NATIVE_COORDS 3
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = float4(1.0, 1.0, 1.0, material.params.z);
    const bool high = material.params.x != 0.0;
    const float4 c0 = float4(v.uv[0], 1.0, 1.0);
    const float4 c1 = float4(v.uv[1], 1.0, 1.0);
    const float4 c2 = float4(v.uv[2], 1.0, 1.0);
    const float4 p = float4(v.pos, 1.0);
    const float4 n = float4(v.nrm, 1.0);
    const uint proj = high ? 9u : 6u;
    const uint env = high ? 12u : 9u;
    o.uv[0] = float4(dot(c0, material.coord[0]), dot(c0, material.coord[1]),
                     dot(c1, material.coord[3]), dot(c1, material.coord[4]));
    o.uv[1] = float4(dot(c2, material.coord[6]), dot(c2, material.coord[7]),
                     dot(n, material.coord[env]), dot(n, material.coord[env + 1u]));
    o.uv[2] = float4(dot(p, material.coord[proj]), dot(p, material.coord[proj + 1u]),
                     dot(p, material.coord[proj + 2u]), 1.0);
}

float4 material_color(NativeVaryings i) {
    if (round(i.color.a * 255.0) <= 0.0) {
        discard;
    }
    const bool high = material.params.x != 0.0;
    float3 c8 = TEX(0).Sample(SAMP(0), i.uv[0].xy).rgb * TEX(0).Sample(SAMP(0), i.uv[0].zw).rgb * 127.5;
    const float3 pattern8 = c8;
    if (high) {
        c8 += material.highlight.rgb * 255.0 * level_line(c8.r, material.highlight.a * 255.0);
    }
    float2 screen = i.uv[2].z != 0.0 ? i.uv[2].xy / i.uv[2].z : i.uv[2].xy;
    float2 move = float2(0.0, 0.0);  // the wobble in screen texture coordinates
    if (high) {
        const float2 step = round(TEX(1).Sample(SAMP(1), i.uv[1].xy).ab * 255.0) - 128.0;
        move = step * material.params.y / material.screen.xy;
        screen += move;
    }
    // The screen behind and the painted environment: what the tracer shows in their place under the traced light
    // (program.hlsli traced_sea; d09). Each sum is clamped as the game's stages clamp it; the pixel stage clamps the whole.
    const float3 behind = TEX(2).Sample(SAMP(2), screen).rgb;  // the scene behind, moved; its tint apart (d14)
    const float3 painted = TEX(3).Sample(SAMP(3), i.uv[1].zw).rgb;  // the painted environment: the traced mirror's place
    return traced_sea(pattern8 / 255.0, c8 / 255.0 - pattern8 / 255.0, behind, material.tint.rgb, painted, i.color.a,
                      move * line_point_raster.xy, i.pos);
}
