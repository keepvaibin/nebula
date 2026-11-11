// SPDX-License-Identifier: GPL-3.0-only
// TrampleStar (Game/MapObj/TrampleStar.cpp): channel 0 lit by light 0 (colour) and light 2 (alpha), spot attenuation,
// clamped diffuse; the colour the lit colour by texture 0 (the first coordinate) plus the shade (the constant alpha where
// the alpha channel is dark, doubled); the texture's alpha. Near Mario his shadow texture (1), projected from the
// position by the draw's texture matrix 1, darkens it.
struct MaterialConstants {
    float4 mat_color;  // channel 0's material colour (rgba, 0-1)
    float4 amb_color;  // and ambient colour
    float4 params;     // x: the constant alpha (0-1), y: 1 where Mario's shadow is drawn
    float4 coord[3];  // the shadow's projection
};

#define NATIVE_COORDS 2
#define NATIVE_LIT 1
#include "native_material.hlsli"

// Channel 0's colour at a place: light 0 over the ambient.
float3 trample_light(float3 eye, float3 nrm) {
    return material.mat_color.rgb * saturate((AMBIENT_TERM(material.amb_color.rgb) * 255.0 + channel_light(0u, 3u, 2u, eye, nrm).rgb) / 255.0);
}

#if NATIVE_TRACED
bool material_relight(inout NativeVaryings i, float3 nrm) {
    i.color.rgb = trample_light(i.lit_eye, nrm);
    return true;
}
#endif

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    const float3 nrm = native_lit_normal(v);
    o.color.rgb = trample_light(v.eye, nrm);
    o.color.a = material.mat_color.a * saturate((material.amb_color.a * 255.0 + channel_light(2u, 3u, 2u, v.eye, nrm).a) / 255.0);
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
    const float4 p = float4(v.pos, 1.0);
    float3 s = float3(dot(p, material.coord[0]), dot(p, material.coord[1]), dot(p, material.coord[2]));
    // q of 0: halved and clamped s and t.
    if (s.z == 0.0) {
        s.xy = clamp(s.xy * 0.5, -1.0, 1.0);
    }
    o.uv[1] = float4(s, 1.0);
}

float4 material_color(NativeVaryings i) {
    const float shade = saturate(2.0 * material.params.x * (1.0 - i.color.a));
    const float4 t = TEX(0).Sample(SAMP(0), i.uv[0].xy);
    float3 c = saturate(i.color.rgb * t.rgb + shade);
    if (material.params.y != 0.0) {
        const float3 s = i.uv[1].xyz;
        c *= 1.0 - TEX(1).Sample(SAMP(1), s.z != 0.0 ? s.xy / s.z : s.xy).rgb;
    }
    return float4(c, t.a);
}
