// SPDX-License-Identifier: GPL-3.0-only
// ScreenBlurEffect (Game/Screen/ScreenBlurEffect.cpp): the blurred picture of the scene (texture 0) drawn over the
// scene at the effect's alpha where the scene's depth (texture 1, an 8-bit depth capture) lies beyond a threshold;
// nothing elsewhere. Both textures read at the quad's own coordinate.
struct MaterialConstants {
    float4 params;  // x: the effect's alpha (0-1); y: the depth beyond which the blur shows (0-1, 8-bit)
};

#define NATIVE_COORDS 1
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = float4(1.0, 1.0, 1.0, material.params.x);
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    const float depth = TEX(1).Sample(SAMP(1), i.uv[0].xy).r;
    if (round(depth * 255.0) <= round(material.params.y * 255.0)) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    return float4(TEX(0).Sample(SAMP(0), i.uv[0].xy).rgb, i.color.a);
}
