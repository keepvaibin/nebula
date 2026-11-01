// SPDX-License-Identifier: GPL-3.0-only
// A colour at a texture's alpha (texture 0, the first coordinate): StarPointerBlur's trail.
struct MaterialConstants {
    float4 color;  // rgb
};

#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = float4(material.color.rgb, 1.0);
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    return float4(i.color.rgb, tex[0].Sample(samp[0], i.uv[0].xy).a);
}
