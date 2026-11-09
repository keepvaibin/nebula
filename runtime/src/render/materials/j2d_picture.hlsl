// SPDX-License-Identifier: GPL-3.0-only
// J2DPicture (JSystem/J2DGraph/J2DPicture.cpp): texture 0's colour (its alpha where the texture has one, else 1), each
// further texture s blended in by the constant colour's and constant alpha's channel for its stage (A, B, G for 1-3: the
// K0/K2 selections 0x20 - 4s, 0x22 - 4s), all at the first coordinate; remapped between black and white, times the
// vertex colour; drawn where the alpha is above 0.
struct MaterialConstants {
    float4 konst_color;  // the colour ratios (rgba, 0-1)
    float4 konst_alpha;  // the alpha ratios
    float4 black;
    float4 white;
    float4 params;       // x: the texture count (1-4), y: the textures with alpha (bit s)
};

#define NATIVE_COORDS 1
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = v.color0;
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    const uint count = uint(material.params.x);
    const uint alphas = uint(material.params.y);
    const float2 uv = i.uv[0].xy;
    const float4 t0 = TEX(0).Sample(SAMP(0), uv);
    float4 c = float4(t0.rgb, (alphas & 1u) != 0u ? t0.a : 1.0);
    [unroll] for (uint s = 1u; s < 4u; ++s) {
        if (s < count) {
            const float4 ts = TEX(s).Sample(SAMP(s), uv);
            const float ta = ((alphas >> s) & 1u) != 0u ? ts.a : 1.0;
            const float kc = s == 1u ? material.konst_color.a : s == 2u ? material.konst_color.b : material.konst_color.g;
            const float ka = s == 1u ? material.konst_alpha.a : s == 2u ? material.konst_alpha.b : material.konst_alpha.g;
            c = saturate(float4(lerp(c.rgb, ts.rgb, kc), lerp(c.a, ta, ka)));
        }
    }
    c = saturate(lerp(material.black, material.white, c)) * i.color;
    if (round(c.a * 255.0) <= 0.0) {
        discard;
    }
    return c;
}
