// SPDX-License-Identifier: GPL-3.0-only
// The lens flare as much as its bright object shows (Game/Screen/LensFlare.cpp, LensFlareDirector::endFlareDraw): the
// frame before the flare (TEX(0)) and with it (TEX(1)), mixed by the share flare_sight.hlsl wrote this frame (TEX(2),
// params.x 1; 0: none shows). Every flare material blends its source by its alpha, so this is the flare drawn with its
// alpha times the share. A pixel the flare did not change is left as it is (discarded), so the frame's other pixels keep
// their values, past white too.
struct MaterialConstants {
    float4 params;  // x 1 when TEX(2) holds this frame's share
};

#define NATIVE_COORDS 1
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.uv[0] = float4(v.uv[0], 0.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    uint width, height;
    TEX(0).GetDimensions(width, height);
    const int2 p = clamp(int2(floor(i.uv[0].xy * float2(width, height))), int2(0, 0), int2(width, height) - 1);
    const float4 without = TEX(0).Load(int3(p, 0));
    const float4 with = TEX(1).Load(int3(p, 0));
    if (all(with == without)) {
        discard;
    }
    const float share = material.params.x != 0.0 ? TEX(2).Load(int3(0, 0, 0)).r : 0.0;
    return lerp(without, with, share);
}
