// SPDX-License-Identifier: GPL-3.0-only
// OceanSphere's surface (Game/Map/OceanSphere.cpp loadMaterialFace): the water texture (0) at the first coordinate
// tinted by a colour, times the same texture at the second coordinate, doubled; where its red equals the highlight
// level, the highlight colour added (unclamped, as eight-bit hardware keeps it); then from the deep colour toward the
// shallow one by that result's low eight bits; alpha the deep colour's. Under the traced light the water model of the
// other seas (program.hlsli traced_sea).
struct MaterialConstants {
    float4 highlight;  // rgb added where the red equals a (the level)
    float4 deep;       // rgb, and the surface's alpha
    float4 shallow;    // rgb
    float4 tint;       // rgb
};

#define NATIVE_COORDS 2
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = float4(1.0, 1.0, 1.0, 1.0);
    o.uv[0] = float4(v.uv[0], v.uv[1]);
}

float4 material_color(NativeVaryings i) {
    const float3 t0 = TEX(0).Sample(SAMP(0), i.uv[0].xy).rgb;
    const float3 t1 = TEX(0).Sample(SAMP(0), i.uv[0].zw).rgb;
    float3 c = saturate(t0 * material.tint.rgb);
    c = saturate(t1 * c * 2.0);
    float3 c8 = c * 255.0;
    c8 += material.highlight.rgb * 255.0 * level_line(c8.r, material.highlight.a * 255.0);
    const float3 mix8 = c8 - 256.0 * floor(c8 / 256.0);
    const float3 water = saturate(lerp(material.deep.rgb, material.shallow.rgb, mix8 / 255.0));
    if ((view_flags & 512u) != 0u) {
        return float4(water, material.deep.a);  // seen from under water: the traced medium's (program.hlsli)
    }
    // Under the traced light one water model with the other seas (program.hlsli traced_sea): the game's colour is the
    // waves it draws, its deep colour the water's tint; it moves no screen behind it.
    return traced_sea(water, float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0), material.deep.rgb, float3(0.0, 0.0, 0.0),
                      material.deep.a, float2(0.0, 0.0), i.pos);
}
