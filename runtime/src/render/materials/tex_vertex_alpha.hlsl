// SPDX-License-Identifier: GPL-3.0-only
// Texture 0 at the first coordinate, its colour scaled, its alpha times the vertex alpha and the alpha scale: MarineSnow's
// flakes (colour scale 1, alpha scale each cell's opacity, a constant so a generated frame blends it), Mario's footing
// mask (colour scale 0: black, alpha scale 1).
struct MaterialConstants {
    float4 params;  // x: the colour's scale, y: the alpha's scale
};

#define NATIVE_COORDS 1
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = v.color0;
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    const float4 t = TEX(0).Sample(SAMP(0), i.uv[0].xy);
    return float4(t.rgb * material.params.x, t.a * i.color.a * material.params.y);
}
