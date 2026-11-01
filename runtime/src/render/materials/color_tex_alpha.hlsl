// SPDX-License-Identifier: GPL-3.0-only
// A colour at a texture's alpha (texture 0, the first coordinate): StarPointerBlur's trail, SnowFloor's layers; drawn
// where the alpha is above a reference.
struct MaterialConstants {
    float4 color;   // rgb
    float4 params;  // x: the alpha reference (0-255; drawn where the alpha is above it), -1 always drawn
};

#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = float4(material.color.rgb, 1.0);
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    const float a = tex[0].Sample(samp[0], i.uv[0].xy).a;
    if (round(a * 255.0) <= material.params.x) {
        discard;
    }
    return float4(i.color.rgb, a);
}
