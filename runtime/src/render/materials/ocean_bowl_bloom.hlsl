// SPDX-License-Identifier: GPL-3.0-only
// OceanBowl's bloom pass (Game/Map/OceanBowl.cpp loadMaterialBloom, OceanBowlBloomDrawer: Beach Bowl's sea seen from
// under water): the water texture (0) at its two scrolls, each times the grey c0, over the grey c1; alpha the
// texture's alpha at each scroll times the vertex alpha, summed; drawn where above zero. the game's two stages:
// colour c1 + tex(uv0) c0, then + tex(uv1) c0; alpha tex(uv0).a ras.a, then + tex(uv1).a ras.a; each clamped.
struct MaterialConstants {
    float4 c0;      // the stages' multiplier (grey, 0-1)
    float4 c1;      // the first stage's base (grey, 0-1)
    float4 scroll;  // the two scrolls of the water texture (xy, zw)
};

#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = v.color0;
    o.uv[0] = float4(v.uv[0] + material.scroll.xy, v.uv[1] + material.scroll.zw);
}

float4 material_color(NativeVaryings i) {
    const float4 t0 = tex[0].Sample(samp[0], i.uv[0].xy);
    const float4 t1 = tex[0].Sample(samp[0], i.uv[0].zw);
    const float3 c = saturate(saturate(material.c1.rgb + t0.rgb * material.c0.rgb) + t1.rgb * material.c0.rgb);
    const float a = saturate(saturate(t0.a * i.color.a) + t1.a * i.color.a);
    if (round(a * 255.0) <= 0.0) {
        discard;
    }
    return float4(c, a);
}
