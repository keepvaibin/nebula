// SPDX-License-Identifier: GPL-3.0-only
// The game's bloom blur (Game/Screen/BloomEffect.cpp drawBlur): texture 0 sampled at up to eight offsets of the tile's
// coordinates, each times the intensity, summed as the console's stages chained them (unclamped) and taken at the result's low
// 8 bits as the pixel engine did; alpha 1.
struct MaterialConstants {
    float4 color;       // the intensity (rgb, 0-1)
    float4 offsets[4];  // the samples' offsets, two a row (xy, zw)
    float4 params;      // x the sample count (1-8)
};

#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    const uint count = uint(material.params.x);
    float3 sum = float3(0.0, 0.0, 0.0);
    [unroll] for (uint s = 0u; s < 8u; ++s) {
        if (s < count) {
            const float4 pair = material.offsets[s >> 1u];
            const float2 offset = (s & 1u) != 0u ? pair.zw : pair.xy;
            sum += tex[0].Sample(samp[0], i.uv[0].xy + offset).rgb * material.color.rgb;
        }
    }
    const float3 v = round(sum * 255.0);
    return float4((v - 256.0 * floor(v / 256.0)) / 255.0, 1.0);
}
