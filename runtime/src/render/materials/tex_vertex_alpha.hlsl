// SPDX-License-Identifier: GPL-3.0-only
// Texture 0 at the first coordinate, its colour scaled, its alpha times the vertex alpha: MarineSnow's flakes (scale
// 1, each cell's opacity), Mario's footing mask (scale 0: black).
struct MaterialConstants {
    float4 params;  // x: the colour's scale
};

#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = v.color0;
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    const float4 t = tex[0].Sample(samp[0], i.uv[0].xy);
    return float4(t.rgb * material.params.x, t.a * i.color.a);
}
