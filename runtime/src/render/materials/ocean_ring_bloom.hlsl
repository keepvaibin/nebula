// SPDX-License-Identifier: GPL-3.0-only
// OceanRingDrawer's bloom (Game/Map/OceanRingDrawer.cpp loadMaterialBloom): a base colour plus the water texture (0)
// at two scrolls each times a colour; alpha the two textures' alphas times the colour's alpha, summed; drawn where
// above zero.
struct MaterialConstants {
    float4 scale;  // rgb times each texture, a times each texture's alpha
    float4 base;   // rgb
};

#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = float4(1.0, 1.0, 1.0, 1.0);
    const float4 c0 = float4(v.uv[0], 1.0, 1.0);
    const float4 c1 = float4(v.uv[1], 1.0, 1.0);
    o.uv[0] = float4(dot(c0, native_tex_row(0)), dot(c0, native_tex_row(1)),
                     dot(c1, native_tex_row(3)), dot(c1, native_tex_row(4)));
}

float4 material_color(NativeVaryings i) {
    const float4 t0 = tex[0].Sample(samp[0], i.uv[0].xy);
    const float4 t1 = tex[0].Sample(samp[0], i.uv[0].zw);
    float3 c = saturate(material.base.rgb + t0.rgb * material.scale.rgb);
    float a = saturate(t0.a * material.scale.a);
    c = saturate(c + t1.rgb * material.scale.rgb);
    a = saturate(a + t1.a * material.scale.a);
    if (round(a * 255.0) <= 0.0) {
        discard;
    }
    return float4(c, a);
}
