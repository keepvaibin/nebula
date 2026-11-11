// SPDX-License-Identifier: GPL-3.0-only
// FurDrawer (Game/Util/FurDrawer.cpp): one fur shell. Channel 0 lit by its lights (clamped diffuse), channel 1 the
// specular lights; the body texture (0) by channel 0 plus the shell's brightness, mixed toward the shell colour by
// the fur texture (1) plus the specular, doubled; the alpha the shell's base plus the fur's alpha by the shell alpha;
// drawn above the reference; fog.
struct MaterialConstants {
    float4 mat0;     // channel 0's material and ambient colours (0-1)
    float4 amb0;
    float4 mat1;     // channel 1's (specular)
    float4 amb1;
    float4 kshell;   // r: the shell's brightness, g: its alpha, b: its base alpha
    float4 color;    // rgb: the shell colour
    float4 params;   // x: channel 0's light mask, y: channel 1's, z: sources (bit 0 channel 0's ambient from the vertex,
                     // 1 its material, 2 and 3 channel 1's), w: 1 when specular
    float4 fog;      // start, end, near, far
    float4 fog_color;  // rgb; w: the fog type
    float4 coord[6];  // texture matrices (body, fur), three rows each
    float4 shell;     // x: how far this shell reaches, y: the alpha reference (0-255)
};

#define NATIVE_SHELL 1
#define NATIVE_COORDS 1
#define NATIVE_LIT 1
#include "native_material.hlsli"

float2 fur_coord(NativeVertex v, uint i) {
    const float4 c = float4(v.uv[0], 1.0, 1.0);
    return float2(dot(c, material.coord[3u * i]), dot(c, material.coord[3u * i + 1u]));
}

// Channels 0 and 1 (the specular) at a place: the lights each mask names over the channel's ambient.
void fur_light(float3 eye, float3 nrm, float4 color0, out float3 c0, out float3 c1) {
    const uint mask0 = uint(material.params.x);
    const uint mask1 = uint(material.params.y);
    const uint src = uint(material.params.z);
    float3 lit0 = float3(0.0, 0.0, 0.0);
    float3 lit1 = float3(0.0, 0.0, 0.0);
    // The masks travel in float constants (24 bits); the set holds as many lights as the game lit.
    for (uint n = 0u; n < min(native_light_count(), 24u); ++n) {
        if (((mask0 >> n) & 1u) != 0u) {
            lit0 += channel_light(n, 2u, 2u, eye, nrm).rgb;
        }
        if (((mask1 >> n) & 1u) != 0u) {
            lit1 += channel_light(n, 1u, 0u, eye, nrm).rgb;
        }
    }
    const float3 amb0 = (src & 1u) != 0u ? color0.rgb : material.amb0.rgb;
    const float3 mat0 = (src & 2u) != 0u ? color0.rgb : material.mat0.rgb;
    const float3 amb1 = (src & 4u) != 0u ? color0.rgb : material.amb1.rgb;
    const float3 mat1 = (src & 8u) != 0u ? color0.rgb : material.mat1.rgb;
    c0 = mat0 * saturate((AMBIENT_TERM(amb0) * 255.0 + lit0) / 255.0);
    c1 = mat1 * saturate((AMBIENT_TERM(amb1) * 255.0 + lit1) / 255.0);
}

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    float3 c0, c1;
    fur_light(v.eye, native_lit_normal(v), v.color0, c0, c1);
    o.color = float4(c0, material.mat0.a);
    o.color1 = float4(c1, material.mat1.a);
    o.uv[0] = float4(fur_coord(v, 0u), fur_coord(v, 1u));
}

#if NATIVE_TRACED
bool material_relight(inout NativeVaryings i, float3 nrm) {
    float3 c0, c1;
    fur_light(i.lit_eye, nrm, i.lit_color0, c0, c1);
    i.color.rgb = c0;
    i.color1.rgb = c1;
    return true;
}
#endif

float4 material_color(NativeVaryings i) {
    const float4 t0 = TEX(0).Sample(SAMP(0), i.uv[0].xy);
    const float4 t1 = TEX(1).Sample(SAMP(1), i.uv[0].zw);
    const float3 c0 = saturate(t0.rgb * i.color.rgb + material.kshell.r);
    const float3 spec = material.params.w != 0.0 ? i.color1.rgb : float3(0.0, 0.0, 0.0);
    float3 c = saturate((lerp(material.color.rgb, c0, t1.rgb) + spec) * 2.0);
    const float a = saturate(material.kshell.b + t1.a * material.kshell.g);
    if (round(a * 255.0) <= material.shell.y) {
        discard;
    }
    c = native_fog(c, i.pos, material.fog, material.fog_color);
    return float4(c, a);
}
