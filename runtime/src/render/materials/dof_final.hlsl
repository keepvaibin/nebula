// SPDX-License-Identifier: GPL-3.0-only
// The game's depth of field, its final pass (Game/Screen/DepthOfFieldBlur.cpp drawFinal): the blurred picture
// (texture 0) blended over the sharp one by an alpha the focus ramp (texture 1) gives at the depth (texture 2), as the
// two stages and indirect lookup did: stage 1's coordinate is wrapped to 0 and moved by the depth sample (8-bit, less
// 128 in s and t) through the matrix diag(scale, scale), in texture 1's texels; alpha that ramp's alpha times the
// material alpha.
struct MaterialConstants {
    float4 params;  // x the material alpha (0-1), y the indirect matrix's diagonal (0.5 x 2^exponent), zw texture 1's size
};

#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    const float3 c = tex[0].Sample(samp[0], i.uv[0].xy).rgb;
    const float3 sv = round(tex[2].Sample(samp[2], i.uv[0].xy).abg * 255.0);
    const float2 offset = float2(sv.x - 128.0, sv.y - 128.0) * material.params.y;
    const float ramp = tex[1].Sample(samp[1], offset / material.params.zw).a;
    return float4(c, saturate(ramp * material.params.x));
}
