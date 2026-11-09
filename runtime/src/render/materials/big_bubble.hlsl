// SPDX-License-Identifier: GPL-3.0-only
// BigBubbleDrawer (Game/MapObj/BigBubbleDrawer.cpp): the rainbow texture (0) with a colour mixed in by the bubble
// environment map's alpha (1, looked up by the eye-space normal), a second colour over it by its alpha; the bubble's
// alpha from the two textures and the second colour. Textures: 0 the rainbow, 1 the bubble environment map.
struct MaterialConstants {
    float4 mix_color;   // mixed in by the environment map's alpha (rgb)
    float4 over_color;  // laid over by its own alpha (rgba)
    float4 env_row[3];  // eye-space normal -> the environment map's s, t, q
};

#define NATIVE_COORDS 2
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = float4(1.0, 1.0, 1.0, 1.0);
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
    const float4 n = float4(v.nrm, 1.0);
    o.uv[1] = float4(dot(material.env_row[0], n), dot(material.env_row[1], n), dot(material.env_row[2], n), 1.0);
}

float4 material_color(NativeVaryings i) {
    const float4 rainbow = TEX(0).Sample(SAMP(0), i.uv[0].xy);
    const float2 env_uv = i.uv[1].z != 0.0 ? i.uv[1].xy / i.uv[1].z : i.uv[1].xy;
    const float env = TEX(1).Sample(SAMP(1), env_uv).a;
    float3 c = saturate(rainbow.rgb + material.mix_color.rgb * env);
    float a = env;
    c = saturate(lerp(c, material.over_color.rgb, material.over_color.a));
    a = lerp(a, 0.875, material.over_color.a);
    return float4(c, a);
}
