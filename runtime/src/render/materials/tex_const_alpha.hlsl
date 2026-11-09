// SPDX-License-Identifier: GPL-3.0-only
// Texture 0's colour at the first coordinate, at a constant alpha (FullScreenBlur's layers).
struct MaterialConstants {
    float4 color;  // a: the alpha (0-1)
};

#define NATIVE_COORDS 1
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = material.color;
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    return float4(TEX(0).Sample(SAMP(0), i.uv[0].xy).rgb, i.color.a);
}
