// SPDX-License-Identifier: GPL-3.0-only
// The game's screen texture passes (Game/Screen/ImageEffectLocalUtil.cpp drawTexture, and their callers:
// bloom, the fall-out field's mask, CameraCover): texture 0 over a screen tile, combined as
// the pass's mode names, in one stage:
//   mode (params.x): 0 modulate, tex x the material colour; 1 threshold, white where the texture's red is above the
//   material colour's, alpha 1; 4 alpha, the texture's alpha in every channel; 5 edge, the edge colour at its alpha
//   times the texture's; 6 luminance, the texture's in every channel, alpha 1; 7 modulate as 0, the texture being four
//   times the pass's size: four taps, each two texels wide, so every texel under the pixel counts.
struct MaterialConstants {
    float4 color;   // the material colour (rgba, 0-1)
    float4 edge;    // the edge colour
    float4 params;  // x the mode
};

#define NATIVE_COORDS 1
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    const uint mode = uint(material.params.x);
    if (mode == 7u) {
        uint width, height;
        TEX(0).GetDimensions(width, height);
        const float2 texel = 1.0 / float2(width, height);
        const float2 uv = i.uv[0].xy;
        const float4 box = (TEX(0).Sample(SAMP(0), uv + float2(-texel.x, -texel.y)) + TEX(0).Sample(SAMP(0), uv + float2(texel.x, -texel.y)) +
                            TEX(0).Sample(SAMP(0), uv + float2(-texel.x, texel.y)) + TEX(0).Sample(SAMP(0), uv + float2(texel.x, texel.y))) * 0.25;
        return saturate(box * material.color);
    }
    const float4 t = TEX(0).Sample(SAMP(0), i.uv[0].xy);
    if (mode == 0u) {
        return saturate(t * material.color);
    }
    if (mode == 1u) {
        const float on = round(t.r * 255.0) > round(material.color.r * 255.0) ? 1.0 : 0.0;
        return float4(on, on, on, 1.0);
    }
    if (mode == 4u) {
        return float4(t.a, t.a, t.a, t.a);
    }
    if (mode == 6u) {
        const float y = saturate(dot(t.rgb, float3(66.0, 129.0, 25.0) / 256.0) + 16.0 / 255.0);
        return float4(y, y, y, 1.0);
    }
    return float4(material.edge.rgb, saturate(material.edge.a * t.a));
}
