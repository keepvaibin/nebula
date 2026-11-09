// SPDX-License-Identifier: GPL-3.0-only
// WhirlPoolAccelerator (Game/Map/WhirlPoolAccelerator.cpp): the stream's colour from one colour toward another by
// its texture (0), at the vertex alpha; drawn where that alpha is above zero.
struct MaterialConstants {
    float4 low;   // the colour where the texture is black (rgb)
    float4 high;  // and where it is white (rgb)
};

#define NATIVE_COORDS 1
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = v.color0;
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    if (round(i.color.a * 255.0) <= 0.0) {
        discard;
    }
    const float3 t = TEX(0).Sample(SAMP(0), i.uv[0].xy).rgb;
    return float4(lerp(material.low.rgb, material.high.rgb, t), i.color.a);
}
