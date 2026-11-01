// SPDX-License-Identifier: GPL-3.0-only
// OceanSphere's inside seen from without (Game/Map/OceanSphere.cpp loadMaterialBack): the environment map (0) looked up
// by the normal through the draw's texture matrix 0 (the camera's rotation, scaled), from one colour toward another by
// it; opaque.
struct MaterialConstants {
    float4 low;   // rgb where the map is black
    float4 high;  // rgb where it is white
};

#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    const float4 n = float4(v.nrm, 1.0);
    o.color = float4(1.0, 1.0, 1.0, 1.0);
    o.uv[0] = float4(dot(n, inline_tex_matrices[0]), dot(n, inline_tex_matrices[1]), 1.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    const float3 t = tex[0].Sample(samp[0], i.uv[0].xy).rgb;
    return float4(lerp(material.low.rgb, material.high.rgb, t), 1.0);
}
