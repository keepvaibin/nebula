// SPDX-License-Identifier: GPL-3.0-only
// The frame's depth for depth of field and screen blur (Game/Screen/ImageEffectLocalUtil.cpp drawFrameDepth): the depth
// buffer's four texels under a half-size pixel, the game's depth one minus their mean as the buffer is reversed.
// Loaded, not sampled: not every device filters a depth format.
struct MaterialConstants {
    float4 unused;
};

#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.uv[0] = float4(v.uv[0], 0.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    uint width, height;
    tex[0].GetDimensions(width, height);
    const int2 last = int2(width, height) - 1;
    const int2 p = clamp(int2(floor(i.uv[0].xy * float2(width, height) - 0.5)), int2(0, 0), last);
    const int2 q = min(p + 1, last);
    const float mean = (tex[0].Load(int3(p, 0)).r + tex[0].Load(int3(q.x, p.y, 0)).r + tex[0].Load(int3(p.x, q.y, 0)).r +
                        tex[0].Load(int3(q, 0)).r) * 0.25;
    const float depth = 1.0 - mean;
    return float4(depth, depth, depth, 1.0);
}
