// SPDX-License-Identifier: GPL-3.0-only
// SpiderThread (Game/MapObj/SpiderThread.cpp): the thread texture (0, the first coordinate) times the vertex colour at
// its alpha, its lookup moved by the indirect texture (1, the second coordinate): the indirect texel's alpha and blue,
// signed about 128, scaled into thread texels; drawn where the alpha is above zero.
struct MaterialConstants {
    float4 warp;  // xy: thread texels per indirect step along s (from alpha) and t (from blue); zw: the thread size
};

#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = v.color0;
    o.uv[0] = float4(v.uv[0], v.uv[1]);
}

float4 material_color(NativeVaryings i) {
    const float2 step = round(tex[1].Sample(samp[1], i.uv[0].zw).ab * 255.0) - 128.0;
    const float2 texel = i.uv[0].xy * material.warp.zw + step * material.warp.xy;
    const float4 t = tex[0].Sample(samp[0], texel / material.warp.zw);
    if (round(t.a * 255.0) <= 0.0) {
        discard;
    }
    return float4(i.color.rgb * t.rgb, t.a);
}
