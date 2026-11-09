// SPDX-License-Identifier: GPL-3.0-only
// The movie's frame (Game/Screen/MovieDraw.cpp): its Y, U and V planes (textures 0, 1, 2; one
// channel each, U and V at half size) to RGB with the THP coefficients (in 8-bit units, its material
// stages' weights: 180/256 V for red, 88/256 U and 183/256 V for green, 227/256 U for blue). Opaque; depth ignored.
struct MaterialConstants {
    float4 unused;
};

#define NATIVE_COORDS 1
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.uv[0] = float4(v.uv[0], 0.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    const float y = TEX(0).Sample(SAMP(0), i.uv[0].xy).r * 255.0;
    const float u = TEX(1).Sample(SAMP(1), i.uv[0].xy).r * 255.0;
    const float v = TEX(2).Sample(SAMP(2), i.uv[0].xy).r * 255.0;
    const float3 rgb = float3(y + 2.0 * (v * (180.0 / 256.0) - 90.0),
                              y + 135.0 - (u * 88.0 + v * 183.0) / 256.0,
                              y + 2.0 * (u * (227.0 / 256.0) - 114.0));
    return float4(saturate(rgb / 255.0), 1.0);
}
