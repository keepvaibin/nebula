// SPDX-License-Identifier: GPL-3.0-only
// A Mii's face texture parts (Game/Mii/MiiFaceTexture.cpp makeFaceTexture: the eyes, eyebrows, mouth, mustache and mole drawn
// into the face texture), texture 0 at the quad's coordinates:
//   form (params.x): 0 the colour c0 at the texture's alpha (mole, eyebrows, mustache); 1 the mouth, c0 red + c1 green
//   + blue (swap tables R, G, B); 2 the eye, c0 red + c1 blue + green. Alpha the texture's; drawn where above zero.
struct MaterialConstants {
    float4 c0;      // colour register 0 (rgba, 0-1)
    float4 c1;      // colour register 1
    float4 params;  // x the form
};

#define NATIVE_COORDS 1
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = v.color0;
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    const float4 t = TEX(0).Sample(SAMP(0), i.uv[0].xy);
    const uint form = uint(material.params.x);
    float3 c;
    if (form == 0u) {
        c = material.c0.rgb;
    } else if (form == 1u) {
        c = saturate(material.c0.rgb * t.r);
        c = saturate(c + material.c1.rgb * t.g);
        c = saturate(c + t.b);
    } else {
        c = saturate(material.c0.rgb * t.r);
        c = saturate(c + material.c1.rgb * t.b);
        c = saturate(c + t.g);
    }
    if (round(t.a * 255.0) <= 0.0) {
        discard;
    }
    return float4(c, t.a);
}
