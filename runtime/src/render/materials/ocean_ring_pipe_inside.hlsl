// SPDX-License-Identifier: GPL-3.0-only
// OceanRingPipeInside (Game/Map/OceanRingPipeInside.cpp): the pipe texture (0, the first coordinate through the draw's
// texture matrix 0) warped by itself (the second coordinate through texture matrix 1: its alpha and blue, signed about
// 128, scaled into texels); the colour from one register colour toward another by the texture, its alpha the
// texture's alpha squared, both times four.
struct MaterialConstants {
    float4 low;   // the colour where the texture is black (rgb)
    float4 high;  // and where it is white (rgb)
    float4 warp;  // x: texels per indirect step; zw: the texture's size
};

#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = float4(1.0, 1.0, 1.0, 1.0);
    const float4 c0 = float4(v.uv[0], 1.0, 1.0);
    const float4 c1 = float4(v.uv[1], 1.0, 1.0);
    o.uv[0] = float4(dot(c0, inline_tex_matrices[0]), dot(c0, inline_tex_matrices[1]),
                     dot(c1, inline_tex_matrices[3]), dot(c1, inline_tex_matrices[4]));
}

float4 material_color(NativeVaryings i) {
    const float2 step = round(tex[0].Sample(samp[0], i.uv[0].zw).ab * 255.0) - 128.0;
    const float2 texel = i.uv[0].xy * material.warp.zw + step * material.warp.x;
    const float4 t = tex[0].Sample(samp[0], texel / material.warp.zw);
    return saturate(float4(lerp(material.low.rgb, material.high.rgb, t.rgb), t.a * t.a) * 4.0);
}
