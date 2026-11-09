// SPDX-License-Identifier: GPL-3.0-only
// Two colours mixed by texture 0's alpha (the first coordinate); the alpha the texture's times the first colour's,
// scaled and offset: MarioFoo's beam and trail (scale 2), WarpPod's glow (offset 1: opaque, added).
struct MaterialConstants {
    float4 color0;  // rgba, 0-1: at alpha 0, and the alpha's factor
    float4 color1;  // rgb, 0-1: at alpha 1
    float4 params;  // y: the alpha's scale, z: its offset
};

#define NATIVE_COORDS 1
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = v.color0;
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    const float ta = TEX(0).Sample(SAMP(0), i.uv[0].xy).a;
    return float4(lerp(material.color0.rgb, material.color1.rgb, ta),
                  saturate(material.params.y * ta * material.color0.a + material.params.z));
}
