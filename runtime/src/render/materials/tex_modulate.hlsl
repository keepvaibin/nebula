// SPDX-License-Identifier: GPL-3.0-only
// A texture (0, the first coordinate) times the vertex colour, its alpha times a constant alpha (or the constant alpha
// alone); drawn where the result's alpha is above zero (Flag, the ropes, vines and stalks, SpaceCocoon).
struct MaterialConstants {
    float4 params;  // x: the constant alpha (0-1), y: 1 the constant alone (the texture's alpha unused)
};

#define NATIVE_COORDS 1
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = float4(v.color0.rgb, material.params.x);
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    const float4 t = TEX(0).Sample(SAMP(0), i.uv[0].xy);
    const float a = material.params.y != 0.0 ? i.color.a : t.a * i.color.a;
    if (round(a * 255.0) <= 0.0) {
        discard;
    }
    return float4(t.rgb * i.color.rgb, a);
}
